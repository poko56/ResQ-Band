#include <SPI.h>
#include <string.h>
#include <UWB_Core.h>

// IMPORTANT: Set to 1000 for DW1000, or 3000 for DW3000
#define UWB_MODULE 3000

#if UWB_MODULE == 3000
#include <UWB_DW3000.h>
#include <DW3000Ng/DW3000Ng.hpp>
UWB_DW3000 uwb;
#else
#include <UWB_DW1000.h>
UWB_DW1000 uwb;
#endif

HardwareSerial Serial(PA10, PA9); // STM32 Serial mapping

// IMPORTANT:
// 1 = initiator/tag. It starts ranging and prints RESULT received from anchor.
// 0 = responder/anchor. It answers with delayed TX, calculates distance, and sends RESULT.
#define NODE_MODE_INITIATOR 0

// Pins mapped for STM32F103C8T6 (stm32duino) based on CubeIDE project
const int PIN_CS = PA4;
const int PIN_IRQ = PB0;
const int PIN_RST = PB1;

// SPI pins
const int PIN_SCK = PA5;
const int PIN_MISO = PA6;
const int PIN_MOSI = PA7;

// Initial/default antenna delay. This MUST be calibrated for your PCB + antenna.
// A wrong antenna delay gives a mostly constant distance offset.
static const uint16_t ANTENNA_DELAY = 16436; // DW1000 common start value; OK as first approximation for DW3000 too.

// Delayed TX timing. Start with 5000 UWB us for STM32F103 + Arduino/SPI stability.
// After it works, you can try 3000, 2000, 1000. If delayed TX fails, increase it.
static const uint32_t RESP_DELAY_UUS  = 5000;
static const uint32_t FINAL_DELAY_UUS = 5000;
static const uint32_t RESULT_DELAY_UUS = 3000;
static const uint32_t RANGE_PERIOD_MS = 300;
static const uint32_t RANGE_TIMEOUT_MS = 120;

// 1 UWB microsecond = 63898 device timestamp ticks in the Qorvo examples.
static const uint32_t UUS_TO_UWB_TIME = 63898UL;
static const double DISTANCE_PER_UWB_TICK_M = 0.0046917639786159;
static const uint64_t UWB40_MASK = 0xFFFFFFFFFFULL;

static const uint8_t MAGIC0 = 0x55;
static const uint8_t MAGIC1 = 0xAA;
static const uint8_t MSG_POLL   = 0x01;
static const uint8_t MSG_RESP   = 0x02;
static const uint8_t MSG_FINAL  = 0x03;
static const uint8_t MSG_RESULT = 0x04;

volatile bool txComplete = false;
volatile bool rxComplete = false;
volatile bool irqPending = false;

uint16_t rxLength = 0;
uint8_t rxBuffer[128];

uint8_t seqCounter = 0;
uint32_t stateStartedMs = 0;
uint32_t lastRangeMs = 0;

uint8_t currentSeq = 0;
uint64_t pollTxTs = 0;
uint64_t respRxTs = 0;

uint64_t savedPollRxTs = 0;
uint64_t savedRespTxTs = 0;
uint8_t savedSeq = 0;

#if NODE_MODE_INITIATOR
enum InitiatorState {
    INIT_IDLE,
    INIT_WAIT_POLL_TX,
    INIT_WAIT_RESP,
    INIT_WAIT_RESULT
};
InitiatorState initiatorState = INIT_IDLE;
#else
enum ResponderState {
    RESP_WAIT_POLL,
    RESP_WAIT_FINAL
};
ResponderState responderState = RESP_WAIT_POLL;
#endif

static uint32_t uusToUwbTicks(uint32_t uus) {
    return uus * UUS_TO_UWB_TIME;
}

static uint64_t diff40(uint64_t later, uint64_t earlier) {
    return (later - earlier) & UWB40_MASK;
}

static void writeTs40(uint8_t* dst, uint64_t value) {
    value &= UWB40_MASK;
    for (uint8_t i = 0; i < 5; i++) {
        dst[i] = (uint8_t)((value >> (8 * i)) & 0xFF);
    }
}

static uint64_t readTs40(const uint8_t* src) {
    uint64_t value = 0;
    for (uint8_t i = 0; i < 5; i++) {
        value |= ((uint64_t)src[i]) << (8 * i);
    }
    return value & UWB40_MASK;
}

static void writeI32(uint8_t* dst, int32_t value) {
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
    dst[2] = (uint8_t)((value >> 16) & 0xFF);
    dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

static int32_t readI32(const uint8_t* src) {
    return (int32_t)((uint32_t)src[0] |
                    ((uint32_t)src[1] << 8) |
                    ((uint32_t)src[2] << 16) |
                    ((uint32_t)src[3] << 24));
}

static void makeHeader(uint8_t* frame, uint8_t type, uint8_t seq) {
    frame[0] = MAGIC0;
    frame[1] = MAGIC1;
    frame[2] = type;
    frame[3] = seq;
}

static bool validHeader(const uint8_t* frame, uint16_t len) {
    return len >= 4 && frame[0] == MAGIC0 && frame[1] == MAGIC1;
}

static double calculateDsTwrDistanceM(uint64_t pollTx, uint64_t pollRx,
                                      uint64_t respTx, uint64_t respRx,
                                      uint64_t finalTx, uint64_t finalRx) {
    double Ra = (double)diff40(respRx, pollTx);   // initiator: POLL TX -> RESP RX
    double Rb = (double)diff40(finalRx, respTx);  // responder: RESP TX -> FINAL RX
    double Da = (double)diff40(finalTx, respRx);  // initiator reply delay
    double Db = (double)diff40(respTx, pollRx);   // responder reply delay

    double denominator = Ra + Rb + Da + Db;
    if (denominator <= 0.0) return -9999.0;

    double tofTicks = ((Ra * Rb) - (Da * Db)) / denominator;
    return tofTicks * DISTANCE_PER_UWB_TICK_M;
}

void onTxDone() {
    txComplete = true;
}

void onRxDone() {
    rxComplete = true;
}

void uwb_isr() {
    irqPending = true;
}

#if NODE_MODE_INITIATOR
static void sendPoll() {
    uint8_t frame[4];
    currentSeq = ++seqCounter;
    makeHeader(frame, MSG_POLL, currentSeq);

    if (uwb.transmit(frame, sizeof(frame))) {
        initiatorState = INIT_WAIT_POLL_TX;
        stateStartedMs = millis();
        Serial.print("POLL sent request seq=");
        Serial.println(currentSeq);
    } else {
        initiatorState = INIT_IDLE;
        Serial.println("POLL TX start failed");
    }
}

static void processInitiatorRx(uint64_t rxTs, const uint8_t* frame, uint16_t len) {
    if (!validHeader(frame, len)) return;

    uint8_t type = frame[2];
    uint8_t seq = frame[3];

    if (initiatorState == INIT_WAIT_RESP && type == MSG_RESP && seq == currentSeq && len >= 14) {
        respRxTs = rxTs;

        // These two timestamps are sent by responder for debug/verification.
        uint64_t responderPollRxTs = readTs40(frame + 4);
        uint64_t responderRespTxTs = readTs40(frame + 9);
        (void)responderPollRxTs;
        (void)responderRespTxTs;

        uint64_t finalTxTs = uwb.calculateDelayedTransmitTimestamp(respRxTs, uusToUwbTicks(FINAL_DELAY_UUS));

        uint8_t finalFrame[19];
        makeHeader(finalFrame, MSG_FINAL, currentSeq);
        writeTs40(finalFrame + 4,  pollTxTs);
        writeTs40(finalFrame + 9,  respRxTs);
        writeTs40(finalFrame + 14, finalTxTs);

        if (uwb.transmitDelayedAt(finalFrame, sizeof(finalFrame), finalTxTs)) {
            Serial.print("FINAL delayed TX done seq=");
            Serial.println(currentSeq);
            initiatorState = INIT_WAIT_RESULT;
            stateStartedMs = millis();
            uwb.startReceive();
        } else {
            Serial.println("FINAL delayed TX failed. Increase FINAL_DELAY_UUS.");
            initiatorState = INIT_IDLE;
            uwb.startReceive();
        }
        return;
    }

    if (initiatorState == INIT_WAIT_RESULT && type == MSG_RESULT && seq == currentSeq && len >= 8) {
        int32_t distanceMm = readI32(frame + 4);
        Serial.print("RESULT seq=");
        Serial.print(currentSeq);
        Serial.print(" distance=");
        Serial.print(distanceMm / 1000.0, 3);
        Serial.println(" m");

        initiatorState = INIT_IDLE;
        lastRangeMs = millis();
        return;
    }
}
#else
static void sendResult(uint8_t seq, double distanceM, uint64_t finalRxTs) {
    int32_t distanceMm = (int32_t)(distanceM * 1000.0 + (distanceM >= 0 ? 0.5 : -0.5));

    uint8_t resultFrame[8];
    makeHeader(resultFrame, MSG_RESULT, seq);
    writeI32(resultFrame + 4, distanceMm);

    // RESULT is not part of the ranging calculation, but we still send it with delayed TX
    // so that the initiator has enough time to switch from FINAL TX back to RX.
    uint64_t resultTxTs = uwb.calculateDelayedTransmitTimestamp(finalRxTs, uusToUwbTicks(RESULT_DELAY_UUS));
    if (!uwb.transmitDelayedAt(resultFrame, sizeof(resultFrame), resultTxTs)) {
        Serial.println("RESULT delayed TX failed. Increase RESULT_DELAY_UUS.");
    }

    responderState = RESP_WAIT_POLL;
    uwb.startReceive();
}

static void processResponderRx(uint64_t rxTs, const uint8_t* frame, uint16_t len) {
    if (!validHeader(frame, len)) return;

    uint8_t type = frame[2];
    uint8_t seq = frame[3];

    if (type == MSG_POLL && len >= 4) {
        savedSeq = seq;
        savedPollRxTs = rxTs;
        savedRespTxTs = uwb.calculateDelayedTransmitTimestamp(savedPollRxTs, uusToUwbTicks(RESP_DELAY_UUS));

        uint8_t respFrame[14];
        makeHeader(respFrame, MSG_RESP, savedSeq);
        writeTs40(respFrame + 4, savedPollRxTs);
        writeTs40(respFrame + 9, savedRespTxTs);

        if (uwb.transmitDelayedAt(respFrame, sizeof(respFrame), savedRespTxTs)) {
            Serial.print("RESP delayed TX done seq=");
            Serial.println(savedSeq);
            responderState = RESP_WAIT_FINAL;
            stateStartedMs = millis();
            uwb.startReceive();
        } else {
            Serial.println("RESP delayed TX failed. Increase RESP_DELAY_UUS.");
            responderState = RESP_WAIT_POLL;
            uwb.startReceive();
        }
        return;
    }

    if (responderState == RESP_WAIT_FINAL && type == MSG_FINAL && seq == savedSeq && len >= 19) {
        uint64_t finalRxTs = rxTs;
        uint64_t initiatorPollTxTs  = readTs40(frame + 4);
        uint64_t initiatorRespRxTs  = readTs40(frame + 9);
        uint64_t initiatorFinalTxTs = readTs40(frame + 14);

        double distanceM = calculateDsTwrDistanceM(
            initiatorPollTxTs,
            savedPollRxTs,
            savedRespTxTs,
            initiatorRespRxTs,
            initiatorFinalTxTs,
            finalRxTs
        );

        Serial.print("DS-TWR seq=");
        Serial.print(savedSeq);
        Serial.print(" distance=");
        Serial.print(distanceM, 3);
        Serial.print(" m | rxPower=");
        Serial.print(uwb.getReceivePower());
        Serial.println(" dBm");

        sendResult(savedSeq, distanceM, finalRxTs);
        return;
    }
}
#endif

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("UWB DS-TWR initialization...");

    SPI.setMOSI(PIN_MOSI);
    SPI.setMISO(PIN_MISO);
    SPI.setSCLK(PIN_SCK);

    uwb.setCallbacks(onTxDone, onRxDone);

    if (uwb.begin(PIN_CS, PIN_IRQ, PIN_RST, SPI)) {
        Serial.println("UWB Init Success.");
        uwb.configure(5, 9, 128, 1); // Channel 5, Code 9, Preamble 128, 6.8 Mbps
        uwb.setAntennaDelay(ANTENNA_DELAY);

        attachInterrupt(digitalPinToInterrupt(PIN_IRQ), uwb_isr, RISING);

#if NODE_MODE_INITIATOR
        Serial.println("Node configured as INITIATOR/TAG.");
        initiatorState = INIT_IDLE;
        lastRangeMs = millis() - RANGE_PERIOD_MS;
#else
        Serial.println("Node configured as RESPONDER/ANCHOR.");
        responderState = RESP_WAIT_POLL;
        uwb.startReceive();
#endif
    } else {
        Serial.println("UWB Init Failed.");
        while (1) { delay(100); }
    }
}

void loop() {
    if (irqPending || digitalRead(PIN_IRQ)) {
        irqPending = false;
        uwb.onIRQ();
    }

    if (txComplete) {
        txComplete = false;
#if NODE_MODE_INITIATOR
        if (initiatorState == INIT_WAIT_POLL_TX) {
            pollTxTs = uwb.getTransmitTimestamp();
            Serial.print("POLL TX timestamp=");
            Serial.println((uint32_t)(pollTxTs & 0xFFFFFFFFUL));
            initiatorState = INIT_WAIT_RESP;
            stateStartedMs = millis();
            uwb.startReceive();
        }
#endif
    }

    if (rxComplete) {
        rxComplete = false;
        rxLength = 0;
        uint64_t rxTs = uwb.getReceiveTimestamp();

        if (uwb.readReceivedData(rxBuffer, rxLength)) {
#if NODE_MODE_INITIATOR
            processInitiatorRx(rxTs, rxBuffer, rxLength);
#else
            processResponderRx(rxTs, rxBuffer, rxLength);
#endif
        }
    }

#if NODE_MODE_INITIATOR
    if (initiatorState == INIT_IDLE && millis() - lastRangeMs >= RANGE_PERIOD_MS) {
        lastRangeMs = millis();
        sendPoll();
    }

    if ((initiatorState == INIT_WAIT_RESP || initiatorState == INIT_WAIT_RESULT) &&
        millis() - stateStartedMs > RANGE_TIMEOUT_MS) {
        Serial.println("Ranging timeout");
        initiatorState = INIT_IDLE;
        uwb.startReceive();
    }
#else
    if (responderState == RESP_WAIT_FINAL && millis() - stateStartedMs > RANGE_TIMEOUT_MS) {
        Serial.println("FINAL timeout");
        responderState = RESP_WAIT_POLL;
        uwb.startReceive();
    }

#endif
}
