#include "UWB_Logic.h"
#include <SPI.h>
#include <UWB_DW3000.h>
#include "ResQConfig.h"

static UWB_DW3000 uwb;

static const uint16_t ANTENNA_DELAY = 16385;
static const uint32_t UUS_TO_UWB_TIME = 63898UL;
static const double DISTANCE_PER_UWB_TICK_M = 0.0046917639786159;
static const uint64_t UWB40_MASK = 0xFFFFFFFFFFULL;

static const uint8_t MAGIC0 = 0x55;
static const uint8_t MAGIC1 = 0xAA;
static const uint8_t MSG_POLL   = 0x01;
static const uint8_t MSG_RESP   = 0x02;
static const uint8_t MSG_FINAL  = 0x03;
static const uint8_t MSG_RESULT = 0x04;

static const uint32_t RESP_DELAY_UUS  = 15000;
static const uint32_t RESULT_DELAY_UUS = 10000;
static const uint32_t RANGE_TIMEOUT_MS = 120;

static volatile bool txComplete = false;
static volatile bool rxComplete = false;
static volatile bool irqPending = false;

static void onTxDone() { txComplete = true; }
static void onRxDone() { rxComplete = true; }
static void uwb_isr()  { irqPending = true; }

static uint32_t uusToUwbTicks(uint32_t uus) { return uus * UUS_TO_UWB_TIME; }
static uint64_t diff40(uint64_t later, uint64_t earlier) { return (later - earlier) & UWB40_MASK; }
static void writeTs40(uint8_t* dst, uint64_t value) {
    value &= UWB40_MASK;
    for (uint8_t i = 0; i < 5; i++) dst[i] = (uint8_t)((value >> (8 * i)) & 0xFF);
}
static uint64_t readTs40(const uint8_t* src) {
    uint64_t value = 0;
    for (uint8_t i = 0; i < 5; i++) value |= ((uint64_t)src[i]) << (8 * i);
    return value & UWB40_MASK;
}
static void writeI32(uint8_t* dst, int32_t value) {
    dst[0] = (uint8_t)(value & 0xFF); dst[1] = (uint8_t)((value >> 8) & 0xFF);
    dst[2] = (uint8_t)((value >> 16) & 0xFF); dst[3] = (uint8_t)((value >> 24) & 0xFF);
}
static void makeHeader(uint8_t* frame, uint8_t type, uint8_t seq) {
    frame[0] = MAGIC0; frame[1] = MAGIC1; frame[2] = type; frame[3] = seq;
}
static bool validHeader(const uint8_t* frame, uint16_t len) {
    return len >= 4 && frame[0] == MAGIC0 && frame[1] == MAGIC1;
}

static uint8_t savedSeq = 0;
static uint64_t savedPollRxTs = 0;
static uint64_t savedRespTxTs = 0;

enum ResponderState { RESP_WAIT_POLL, RESP_WAIT_FINAL };
static ResponderState responderState = RESP_WAIT_POLL;
static uint32_t stateStartedMs = 0;

void init_uwb() {
  Serial.println("[UWB] Initializing DW3000 Responder...");
  uwb.setCallbacks(onTxDone, onRxDone);
  
  if (uwb.begin(PIN_UWB_SS, PIN_UWB_IRQ, PIN_UWB_RST, SPI)) {
      Serial.println("[UWB] DW3000 Init Success");
      uwb.configure(5, 9, 128, 1);
      uwb.setAntennaDelay(ANTENNA_DELAY);
      pinMode(PIN_UWB_IRQ, INPUT);
      attachInterrupt(digitalPinToInterrupt(PIN_UWB_IRQ), uwb_isr, RISING);
      uwb.startReceive();
      responderState = RESP_WAIT_POLL;
  } else {
      Serial.println("[UWB] DW3000 INIT FAILED");
  }
}

static double calculateDsTwrDistanceM(uint64_t pollTx, uint64_t pollRx, uint64_t respTx, uint64_t respRx, uint64_t finalTx, uint64_t finalRx) {
    double Ra = (double)diff40(respRx, pollTx);
    double Rb = (double)diff40(finalRx, respTx);
    double Da = (double)diff40(finalTx, respRx);
    double Db = (double)diff40(respTx, pollRx);
    double denominator = Ra + Rb + Da + Db;
    if (denominator <= 0.0) return -1.0;
    return (((Ra * Rb) - (Da * Db)) / denominator) * DISTANCE_PER_UWB_TICK_M;
}

void poll_uwb() {
  if (irqPending || digitalRead(PIN_UWB_IRQ)) {
      irqPending = false;
      uwb.onIRQ();
  }
  
  if (rxComplete) {
      rxComplete = false;
      uint16_t rxLength = 0;
      uint8_t rxBuffer[128];
      uint64_t rxTs = uwb.getReceiveTimestamp();
      
      if (uwb.readReceivedData(rxBuffer, rxLength) && validHeader(rxBuffer, rxLength)) {
          uint8_t type = rxBuffer[2];
          uint8_t seq = rxBuffer[3];
          
          if (type == MSG_POLL && rxLength >= 4) {
              Serial.println("[UWB] RX POLL");
              savedSeq = seq;
              savedPollRxTs = rxTs;
              savedRespTxTs = uwb.calculateDelayedTransmitTimestamp(savedPollRxTs, uusToUwbTicks(RESP_DELAY_UUS));
              
              uint8_t respFrame[14];
              makeHeader(respFrame, MSG_RESP, savedSeq);
              writeTs40(respFrame + 4, savedPollRxTs);
              writeTs40(respFrame + 9, savedRespTxTs);
              
              txComplete = false;
              if (uwb.transmitDelayedAt(respFrame, sizeof(respFrame), savedRespTxTs)) {
                  responderState = RESP_WAIT_FINAL;
                  stateStartedMs = millis();
                  Serial.println("[UWB] TX RESP OK");
              } else {
                  Serial.println("[UWB] TX RESP FAIL");
                  responderState = RESP_WAIT_POLL;
              }
              uwb.startReceive();
          } 
          else if (responderState == RESP_WAIT_FINAL && type == MSG_FINAL && seq == savedSeq && rxLength >= 19) {
              uint64_t finalRxTs = rxTs;
              uint64_t initiatorPollTxTs  = readTs40(rxBuffer + 4);
              uint64_t initiatorRespRxTs  = readTs40(rxBuffer + 9);
              uint64_t initiatorFinalTxTs = readTs40(rxBuffer + 14);
              
              double distanceM = calculateDsTwrDistanceM(initiatorPollTxTs, savedPollRxTs, savedRespTxTs, initiatorRespRxTs, initiatorFinalTxTs, finalRxTs);
              
              int32_t distanceMm = (int32_t)(distanceM * 1000.0);
              uint8_t resultFrame[8];
              makeHeader(resultFrame, MSG_RESULT, savedSeq);
              writeI32(resultFrame + 4, distanceMm);
              
              uint64_t resultTxTs = uwb.calculateDelayedTransmitTimestamp(finalRxTs, uusToUwbTicks(RESULT_DELAY_UUS));
              txComplete = false;
              uwb.transmitDelayedAt(resultFrame, sizeof(resultFrame), resultTxTs);
              
              Serial.printf("[UWB] RX FINAL, Dist = %.2f m\n", distanceM);
              
              responderState = RESP_WAIT_POLL;
              uwb.startReceive();
          }
      } else {
          uwb.startReceive();
      }
  }
  
  if (responderState == RESP_WAIT_FINAL && millis() - stateStartedMs > RANGE_TIMEOUT_MS) {
      Serial.println("[UWB] FINAL RX TIMEOUT");
      responderState = RESP_WAIT_POLL;
      uwb.startReceive();
  }
}
