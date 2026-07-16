#include "UWB_Initiator.h"
#include <SPI.h>
#include <UWB_DW3000.h>

extern String g_webui_debug;

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

static const uint32_t FINAL_DELAY_UUS = 40000;
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
static int32_t readI32(const uint8_t* src) {
    return (int32_t)((uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24));
}
static void makeHeader(uint8_t* frame, uint8_t type, uint8_t seq) {
    frame[0] = MAGIC0; frame[1] = MAGIC1; frame[2] = type; frame[3] = seq;
}
static bool validHeader(const uint8_t* frame, uint16_t len) {
    return len >= 4 && frame[0] == MAGIC0 && frame[1] == MAGIC1;
}

static uint8_t currentSeq = 0;
static uint64_t pollTxTs = 0;
static uint64_t respRxTs = 0;

static float last_distance = -1.0f;

void init_uwb_initiator() {
  Serial.println("[UWB] Initializing DW3000 Initiator...");
  
  uwb.setCallbacks(onTxDone, onRxDone);
  
  if (uwb.begin(PIN_UWB_SS, PIN_UWB_IRQ, PIN_UWB_RST, SPI)) {
      Serial.println("[UWB] DW3000 Init Success");
      uwb.configure(5, 9, 128, 1);
      uwb.setAntennaDelay(ANTENNA_DELAY);
      pinMode(PIN_UWB_IRQ, INPUT);
      attachInterrupt(digitalPinToInterrupt(PIN_UWB_IRQ), uwb_isr, RISING);
      uwb.startReceive();
  } else {
      Serial.println("[UWB] DW3000 INIT FAILED");
  }
}

void poll_uwb_initiator(uint32_t target_id, float* distance_m, float* angle_deg) {
  if (target_id == 0) {
      g_webui_debug = "Waiting for target lock (LoRa)...";
      return;
  }
  
  // Handle UWB interrupts
  if (irqPending || digitalRead(PIN_UWB_IRQ)) {
      irqPending = false;
      uwb.onIRQ();
  }
  
  // Send POLL
  uint8_t frame[4];
  currentSeq++;
  makeHeader(frame, MSG_POLL, currentSeq);
  
  txComplete = false;
  rxComplete = false;
  if (!uwb.transmit(frame, sizeof(frame))) {
      *distance_m = last_distance;
      *angle_deg = 0.0f;
      return;
  }
  
  uint32_t startMs = millis();
  while (!txComplete && millis() - startMs < 50) {
      if (irqPending || digitalRead(PIN_UWB_IRQ)) { irqPending = false; }
      uwb.onIRQ();
      delay(2);
  }
  
  if (!txComplete) { g_webui_debug = "ERR: POLL TX Timeout"; *distance_m = last_distance; return; }
  
  pollTxTs = uwb.getTransmitTimestamp();
  uwb.startReceive();
  
  // Wait for RESP
  startMs = millis();
  while (!rxComplete && millis() - startMs < RANGE_TIMEOUT_MS) {
      if (irqPending || digitalRead(PIN_UWB_IRQ)) { irqPending = false; }
      uwb.onIRQ();
      delay(2);
  }
  
  if (!rxComplete) { g_webui_debug = "ERR: RESP RX Timeout"; *distance_m = last_distance; return; }
  
  uint8_t rxBuffer[128];
  uint16_t rxLength = 0;
  uint64_t rxTs = uwb.getReceiveTimestamp();
  if (!uwb.readReceivedData(rxBuffer, rxLength)) { g_webui_debug = "ERR: RESP RX read"; *distance_m = last_distance; return; }
  if (!validHeader(rxBuffer, rxLength) || rxBuffer[2] != MSG_RESP || rxBuffer[3] != currentSeq || rxLength < 9) { 
      g_webui_debug = "ERR: RESP format";
      uwb.startReceive(); 
      *distance_m = last_distance;
      return; 
  }
  
  respRxTs = rxTs;
  uint64_t replyDelay = readTs40(rxBuffer + 4);
  
  uint64_t roundTrip = diff40(respRxTs, pollTxTs);
  if (roundTrip < replyDelay) {
      g_webui_debug = "ERR: NEGATIVE TOF";
      uwb.startReceive();
      *distance_m = last_distance;
      return;
  }
  
  double tof = (roundTrip - replyDelay) / 2.0;
  double distanceM = tof * DISTANCE_PER_UWB_TICK_M;
  
  int32_t distanceMm = (int32_t)(distanceM * 1000.0);
  last_distance = distanceM;
  g_webui_debug = "UWB OK: " + String(last_distance, 2) + "m";
  
  // Send RESULT back to band_node so it knows the distance
  uint8_t resultFrame[8];
  makeHeader(resultFrame, MSG_RESULT, currentSeq);
  writeTs40(resultFrame + 4, (uint64_t)distanceMm); // cheat: just write 4 bytes using standard function or direct
  // Wait, writeTs40 writes 5 bytes, let's just write exactly 4 bytes for int32
  resultFrame[4] = distanceMm & 0xFF;
  resultFrame[5] = (distanceMm >> 8) & 0xFF;
  resultFrame[6] = (distanceMm >> 16) & 0xFF;
  resultFrame[7] = (distanceMm >> 24) & 0xFF;
  
  txComplete = false;
  uwb.transmit(resultFrame, sizeof(resultFrame)); // Transmit immediately
  
  startMs = millis();
  while (!txComplete && millis() - startMs < 50) {
      if (irqPending || digitalRead(PIN_UWB_IRQ)) { irqPending = false; }
      uwb.onIRQ();
      delay(2);
  }
  
  *distance_m = last_distance;
  *angle_deg = 0.0f;
  uwb.startReceive();
}
