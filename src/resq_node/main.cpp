#include <Arduino.h>
#include "ResQConfig.h"
#include "ResQProtocol.h"

enum SearchMode : uint8_t {
  MODE_LORA_SWEEP   = 0,
  MODE_UWB_PINPOINT = 1,
};

static uint32_t   g_device_id    = 0;
static SearchMode g_mode         = MODE_LORA_SWEEP;
static uint32_t   g_last_tick_ms = 0;

static const char* mode_label(SearchMode m) {
  return (m == MODE_LORA_SWEEP) ? "LoRa-Sweep" : "UWB-Pinpoint";
}

static void print_banner() {
  Serial.println();
  Serial.println(F("================================"));
  Serial.printf( "  %s\n", BOARD_NAME);
  Serial.printf( "  FW %s\n", FW_VERSION);
  Serial.printf( "  Device ID: %08X\n", g_device_id);
  Serial.printf( "  Mode:      %s\n", mode_label(g_mode));
  Serial.println(F("================================"));
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_VIBRATION,  OUTPUT);
  pinMode(PIN_BUZZER,     OUTPUT);
  pinMode(PIN_BTN_MODE,   INPUT_PULLUP);
  pinMode(PIN_BTN_FOUND,  INPUT_PULLUP);

  digitalWrite(PIN_LED_STATUS, LOW);
  digitalWrite(PIN_VIBRATION,  LOW);
  digitalWrite(PIN_BUZZER,     LOW);

  g_device_id = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFFF);
  print_banner();
}

void loop() {
  const uint32_t now = millis();

  static uint32_t last_btn_ms = 0;
  if (digitalRead(PIN_BTN_MODE) == LOW && now - last_btn_ms > 250) {
    last_btn_ms = now;
    g_mode = (g_mode == MODE_LORA_SWEEP) ? MODE_UWB_PINPOINT : MODE_LORA_SWEEP;
    Serial.printf("[MODE] switched to %s\n", mode_label(g_mode));
  }

  if (now - g_last_tick_ms >= 1000) {
    g_last_tick_ms = now;
    Serial.printf("[TICK] mode=%s rssi=-- range=--\n", mode_label(g_mode));
    digitalWrite(PIN_LED_STATUS, !digitalRead(PIN_LED_STATUS));
  }
}
