#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "ResQConfig.h"
#include "ResQProtocol.h"

static uint32_t g_device_id  = 0;
static uint16_t g_seq        = 0;
static uint32_t g_last_hb_ms = 0;

static void print_banner() {
  Serial.println();
  Serial.println(F("================================"));
  Serial.printf( "  %s\n", BOARD_NAME);
  Serial.printf( "  FW %s\n", FW_VERSION);
  Serial.printf( "  Device ID: %08X\n", g_device_id);
  Serial.printf( "  Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.println(F("================================"));
}

static void init_lora() {
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_SS);
  LoRa.setPins(PIN_LORA_SS, PIN_LORA_RST, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println(F("[LoRa] init failed - check wiring"));
    while (true) {
      digitalWrite(PIN_LED_STATUS, !digitalRead(PIN_LED_STATUS));
      delay(150);
    }
  }
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setTxPower(LORA_TX_POWER_DBM);
  LoRa.enableCrc();
  Serial.printf("[LoRa] ready @ %.1f MHz SF%d\n", LORA_FREQUENCY / 1e6, LORA_SPREADING_FACTOR);
}

static bool send_packet(const ResQ::SOSPacket& pkt) {
  if (!LoRa.beginPacket()) return false;
  LoRa.write(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
  return LoRa.endPacket() == 1;
}

static uint8_t read_battery_pct() {
  return 100;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_BUZZER,     OUTPUT);
  digitalWrite(PIN_LED_STATUS, LOW);
  digitalWrite(PIN_BUZZER,     LOW);

  g_device_id = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFFF);
  print_banner();

  init_lora();
}

void loop() {
  const uint32_t now = millis();

  if (now - g_last_hb_ms >= HEARTBEAT_INTERVAL_MS) {
    g_last_hb_ms = now;
    ++g_seq;

    ResQ::SOSPacket pkt{};
    ResQ::fill_sos_packet(pkt,
                          ResQ::PKT_HEARTBEAT,
                          g_device_id,
                          g_seq,
                          ResQ::TRIAGE_GREEN,
                          0,
                          0,
                          read_battery_pct(),
                          10);

    digitalWrite(PIN_LED_STATUS, HIGH);
    const bool ok = send_packet(pkt);
    digitalWrite(PIN_LED_STATUS, LOW);

    Serial.printf("[HB] seq=%u triage=%s crc=%04X tx=%s\n",
                  pkt.sequence,
                  ResQ::triage_label(static_cast<ResQ::TriageLevel>(pkt.triage_level)),
                  pkt.crc16,
                  ok ? "OK" : "FAIL");
  }
}
