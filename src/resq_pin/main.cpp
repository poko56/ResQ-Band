#include <Arduino.h>
#include "ResQConfig.h"
#include "ResQProtocol.h"

static uint32_t g_device_id = 0;
static uint32_t g_last_scan_ms = 0;

static void print_banner() {
  Serial.println();
  Serial.println(F("================================"));
  Serial.printf( "  %s\n", BOARD_NAME);
  Serial.printf( "  FW %s\n", FW_VERSION);
  Serial.printf( "  Device ID: %08X\n", g_device_id);
  Serial.printf( "  Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.println(F("================================"));
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED_STATUS, OUTPUT);
  digitalWrite(PIN_LED_STATUS, LOW);

  g_device_id = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFFF);
  print_banner();

  Serial.println(F("[BOOT] Gateway ready - awaiting LoRa/ESP-NOW traffic"));
}

void loop() {
  const uint32_t now = millis();

  if (now - g_last_scan_ms >= 5000) {
    g_last_scan_ms = now;
    Serial.printf("[ALIVE] uptime=%lus heap=%u\n",
                  now / 1000, ESP.getFreeHeap());
    digitalWrite(PIN_LED_STATUS, !digitalRead(PIN_LED_STATUS));
  }
}
