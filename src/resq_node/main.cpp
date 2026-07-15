#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "ResQConfig.h"
#include "ResQProtocol.h"
#include "Display.h"
#include "UWB_Initiator.h"

enum SearchMode : uint8_t {
  MODE_LORA_SWEEP   = 0,
  MODE_UWB_PINPOINT = 1,
};

static uint32_t   g_device_id    = 0;
static SearchMode g_mode         = MODE_LORA_SWEEP;
static uint32_t   g_last_tick_ms = 0;

// Current locked target state
static uint32_t   g_target_id    = 0;
static int16_t    g_last_rssi    = -120;
static uint8_t    g_last_hr      = 0;
static uint8_t    g_last_spo2    = 0;

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

static bool connect_lora() {
  // Use standard VSPI pins for LoRa on classic ESP32
  SPI.begin(18, 19, 23, PIN_LORA_SS);
  LoRa.setPins(PIN_LORA_SS, PIN_LORA_RST, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) return false;
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.enableCrc();
  LoRa.receive();
  return true;
}

static void on_lora_rx(int packet_size) {
  if (g_mode != MODE_LORA_SWEEP) {
    // Ignore LoRa if in UWB mode to save SPI bus contention
    while (LoRa.available()) LoRa.read();
    return;
  }
  
  if (packet_size < 2) {
    while (LoRa.available()) LoRa.read();
    return;
  }
  
  uint8_t buf[64];
  size_t n = 0;
  while (LoRa.available() && n < sizeof(buf)) buf[n++] = (uint8_t)LoRa.read();

  const uint8_t ptype = ResQ::peek_packet_type(buf, n);
  
  // We care about HEARTBEAT or SOS from band_node
  if ((ptype == ResQ::PKT_HEARTBEAT || ptype == ResQ::PKT_SOS_TAP || ptype == ResQ::PKT_SOS_FALL) 
      && n >= sizeof(ResQ::SOSPacket)) {
    ResQ::SOSPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    
    // Auto lock-on to the first seen band or update if it's the current target
    if (g_target_id == 0 || g_target_id == pkt.device_id) {
      g_target_id = pkt.device_id;
      g_last_rssi = LoRa.packetRssi();
      g_last_hr = pkt.heart_rate;
      g_last_spo2 = pkt.spo2;
      
      // Update display immediately
      display_lora_sweep(g_target_id, g_last_rssi, g_last_hr, g_last_spo2);
      
      // Haptic feedback based on RSSI
      if (g_last_rssi > -70) {
        digitalWrite(PIN_VIBRATION, HIGH);
        delay(50);
        digitalWrite(PIN_VIBRATION, LOW);
      }
    }
  }
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
  
  init_display();
  init_uwb_initiator();
  
  if (connect_lora()) {
    Serial.println("[LoRa] Init OK");
    LoRa.onReceive(on_lora_rx);
  } else {
    Serial.println("[LoRa] Init FAILED");
  }
  
  display_lora_sweep(0, 0, 0, 0); // initial draw
}

void loop() {
  const uint32_t now = millis();

  static uint32_t last_btn_ms = 0;
  if (digitalRead(PIN_BTN_MODE) == LOW && now - last_btn_ms > 250) {
    last_btn_ms = now;
    g_mode = (g_mode == MODE_LORA_SWEEP) ? MODE_UWB_PINPOINT : MODE_LORA_SWEEP;
    Serial.printf("[MODE] switched to %s\n", mode_label(g_mode));
    
    if (g_mode == MODE_LORA_SWEEP) {
      display_lora_sweep(g_target_id, g_last_rssi, g_last_hr, g_last_spo2);
      LoRa.receive();
    } else {
      display_uwb_pinpoint(g_target_id, 0.0f, 0.0f);
    }
  }

  // Found button (Confirm rescued)
  static uint32_t last_found_btn_ms = 0;
  if (digitalRead(PIN_BTN_FOUND) == LOW && now - last_found_btn_ms > 1000) {
    last_found_btn_ms = now;
    if (g_target_id != 0) {
      Serial.printf("[FOUND] Confirming rescue for %08X\n", g_target_id);
      digitalWrite(PIN_BUZZER, HIGH);
      delay(200);
      digitalWrite(PIN_BUZZER, LOW);
      // Here we would send PKT_FOUND to MainNode via LoRa
      g_target_id = 0; // reset target
      if (g_mode == MODE_LORA_SWEEP) display_lora_sweep(0,0,0,0);
      else display_uwb_pinpoint(0,0,0);
    }
  }

  if (now - g_last_tick_ms >= 500) {
    g_last_tick_ms = now;
    digitalWrite(PIN_LED_STATUS, !digitalRead(PIN_LED_STATUS));
    
    if (g_mode == MODE_UWB_PINPOINT) {
      float distance_m = -1.0f;
      float angle_deg = 0.0f;
      poll_uwb_initiator(g_target_id, &distance_m, &angle_deg);
      
      display_uwb_pinpoint(g_target_id, distance_m, angle_deg);
      
      // Haptic feedback based on UWB distance (Geiger counter style)
      if (distance_m > 0.0f && distance_m < 2.0f) {
        digitalWrite(PIN_VIBRATION, HIGH);
        delay(100);
        digitalWrite(PIN_VIBRATION, LOW);
      }
    }
  }
}
