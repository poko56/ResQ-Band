#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "ResQConfig.h"
#include "ResQProtocol.h"
#include "Display.h"
#include "UWB_Initiator.h"
#include "WebUI.h"

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

#define MAX_SEEN_BANDS 10
static SeenBand g_seen_bands[MAX_SEEN_BANDS];
static size_t g_seen_count = 0;

static void update_seen_band(uint32_t id, int16_t rssi, uint8_t hr, uint8_t spo2) {
  uint32_t now = millis();
  for (size_t i = 0; i < g_seen_count; i++) {
    if (g_seen_bands[i].id == id) {
      g_seen_bands[i].rssi = rssi;
      g_seen_bands[i].hr = hr;
      g_seen_bands[i].spo2 = spo2;
      g_seen_bands[i].last_seen_ms = now;
      return;
    }
  }
  if (g_seen_count < MAX_SEEN_BANDS) {
    g_seen_bands[g_seen_count++] = {id, rssi, hr, spo2, now};
  } else {
    // Replace oldest
    size_t oldest = 0;
    for (size_t i = 1; i < MAX_SEEN_BANDS; i++) {
      if (now - g_seen_bands[i].last_seen_ms > now - g_seen_bands[oldest].last_seen_ms) {
        oldest = i;
      }
    }
    g_seen_bands[oldest] = {id, rssi, hr, spo2, now};
  }
}

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
static void on_lora_rx(int packet_size);

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
  LoRa.onReceive(on_lora_rx);
  LoRa.receive();
  return true;
}

volatile bool g_lora_rx_flag = false;
volatile int  g_lora_packet_size = 0;

extern String g_webui_debug;
uint32_t g_rx_count = 0;

static void on_lora_rx(int packet_size) {
  g_rx_count++;
  if (g_mode != MODE_LORA_SWEEP) return;
  
  if (packet_size < 2) return;
  
  g_lora_packet_size = packet_size;
  g_lora_rx_flag = true;
}

static void process_lora_packet() {
  int packet_size = g_lora_packet_size;
  
  uint8_t buf[64];
  size_t n = 0;
  while (LoRa.available() && n < sizeof(buf)) buf[n++] = (uint8_t)LoRa.read();

  const uint8_t ptype = ResQ::peek_packet_type(buf, n);
  
  if (g_webui_debug.indexOf("POLL") == -1) {
    g_webui_debug = "RX=" + String(g_rx_count) + " SZ=" + String(packet_size) + " n=" + String(n) + " PT=" + String(ptype);
  }

    // We care about HEARTBEAT or SOS from band_node
  if ((ptype == ResQ::PKT_HEARTBEAT || ptype == ResQ::PKT_SOS_TAP || ptype == ResQ::PKT_SOS_FALL) 
      && n >= sizeof(ResQ::SOSPacket)) {
    ResQ::SOSPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    
    int16_t current_rssi = LoRa.packetRssi();
    update_seen_band(pkt.device_id, current_rssi, pkt.heart_rate, pkt.spo2);
    
    // Auto lock-on to the first seen band or update if it's the current target
    if (g_target_id == 0 || g_target_id == pkt.device_id) {
      g_target_id = pkt.device_id;
      g_last_rssi = current_rssi;
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
  } else if (ptype == ResQ::PKT_ASSIGNMENT && n >= sizeof(ResQ::AssignmentPacket)) {
    ResQ::AssignmentPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    
    // Accept assignment if broadcast or addressed to us
    if (pkt.resq_node_id == 0 || pkt.resq_node_id == g_device_id) {
      if (g_target_id != pkt.target_band_id) {
        g_target_id = pkt.target_band_id;
        Serial.printf("[ASSIGNMENT] Dispatch assigned band %lu\n", (unsigned long)g_target_id);
        
        display_lora_sweep(g_target_id, g_last_rssi, g_last_hr, g_last_spo2);
        
        // Distinct vibration for new assignment
        for (int i=0; i<3; i++) {
          digitalWrite(PIN_VIBRATION, HIGH);
          delay(100);
          digitalWrite(PIN_VIBRATION, LOW);
          delay(100);
        }
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
  
  // 1. WiFi AP must start first, before any SPI device calls SPI.begin()
  init_web_ui();
  
  // 2. Display (I2C - safe, no SPI conflict)
  init_display();
  
  // 3. LoRa takes ownership of VSPI bus (SCK=18, MISO=19, MOSI=23)
  if (connect_lora()) {
    Serial.println("[LoRa] Init OK");
  } else {
    Serial.println("[LoRa] Init FAILED");
  }
  
  // 4. UWB init deferred - only enter UWB mode on button press
  // init_uwb_initiator() is called from mode-switch handler
  
  display_lora_sweep(0, 0, 0, 0); // initial draw
}

void loop() {
  const uint32_t now = millis();

  // --- Hybrid Polling + Interrupt ---
  if (!g_lora_rx_flag && g_mode == MODE_LORA_SWEEP) {
    int packet_size = LoRa.parsePacket();
    if (packet_size) {
      g_lora_packet_size = packet_size;
      g_lora_rx_flag = true;
      g_webui_debug = "POLL SZ=" + String(packet_size);
    }
  }

  if (g_lora_rx_flag) {
    g_lora_rx_flag = false;
    process_lora_packet();
  }

  static uint32_t last_btn_ms = 0;
  if ((digitalRead(PIN_BTN_MODE) == LOW && now - last_btn_ms > 250) || webui_wants_mode_switch) {
    if (webui_wants_mode_switch) webui_wants_mode_switch = false;
    
    last_btn_ms = now;
    g_mode = (g_mode == MODE_LORA_SWEEP) ? MODE_UWB_PINPOINT : MODE_LORA_SWEEP;
    Serial.printf("[Mode] Switched to %s\n", mode_label(g_mode));
    
    if (g_mode == MODE_UWB_PINPOINT) {
      LoRa.onReceive(NULL); // Stop LoRa interrupts
      LoRa.sleep();         // Put LoRa to sleep to release SPI bus
      display_uwb_pinpoint(g_target_id, -1.0f, 0.0f);
      init_uwb_initiator();
    } else {
      init_display(); // Re-init I2C display to be safe
      connect_lora(); // Re-init LoRa and attach interrupt
      display_lora_sweep(g_target_id, g_last_rssi, g_last_hr, g_last_spo2);
    }
  }

  // Check manual WebUI assignment
  if (webui_wants_target_id != 0) {
    if (g_target_id != webui_wants_target_id) {
      g_target_id = webui_wants_target_id;
      Serial.printf("[ASSIGNMENT] WebUI assigned band %lu\n", (unsigned long)g_target_id);
      if (g_mode == MODE_LORA_SWEEP) {
        display_lora_sweep(g_target_id, g_last_rssi, g_last_hr, g_last_spo2);
      }
      for (int i=0; i<3; i++) {
        digitalWrite(PIN_VIBRATION, HIGH);
        delay(100);
        digitalWrite(PIN_VIBRATION, LOW);
        delay(100);
      }
    }
    webui_wants_target_id = 0;
  }
  
  // Cleanup old seen bands (>10s)
  for (size_t i = 0; i < g_seen_count; ) {
    if (now - g_seen_bands[i].last_seen_ms > 10000) {
      g_seen_bands[i] = g_seen_bands[g_seen_count - 1];
      g_seen_count--;
    } else {
      i++;
    }
  }

  // Found button (Confirm rescued) - Requires 1-second hold to avoid EMI triggers
  static uint32_t btn_found_press_start = 0;
  if (digitalRead(PIN_BTN_FOUND) == LOW) {
    if (btn_found_press_start == 0) {
      btn_found_press_start = now;
    } else if (now - btn_found_press_start > 1000) {
      if (g_target_id != 0) {
        Serial.printf("[FOUND] Confirming rescue for %08X\n", g_target_id);
        digitalWrite(PIN_BUZZER, HIGH);
        delay(200);
        digitalWrite(PIN_BUZZER, LOW);
        // Here we would send PKT_FOUND to MainNode via LoRa
        g_target_id = 0; // reset target
        btn_found_press_start = 0; // reset hold timer
        if (g_mode == MODE_LORA_SWEEP) display_lora_sweep(0,0,0,0);
        else display_uwb_pinpoint(0,0,0);
      }
    }
  } else {
    btn_found_press_start = 0;
  }

  if (now - g_last_tick_ms >= 500) {
    g_last_tick_ms = now;
    digitalWrite(PIN_LED_STATUS, !digitalRead(PIN_LED_STATUS));
    
    if (g_mode == MODE_UWB_PINPOINT) {
      float dist = -1.0f;
      float angle = 0.0f; // placeholder for angle
      poll_uwb_initiator(g_target_id, &dist, &angle);
      
      display_uwb_pinpoint(g_target_id, dist, angle);
      webui_set_target(g_target_id, g_mode, dist, angle);
    }
  }
  
  webui_set_bands(g_seen_bands, g_seen_count);
  
  if (g_mode == MODE_LORA_SWEEP) {
    webui_set_target(g_target_id, g_mode, -1.0f, 0.0f);
  }

  webui_loop();
}
