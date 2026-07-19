#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "ResQConfig.h"
#include "ResQProtocol.h"
#include "Display.h"
#include "BLE_Scanner.h"
#include "WebUI.h"

enum SearchMode : uint8_t {
  MODE_LORA_SWEEP   = 0,
  MODE_BLE_PINPOINT = 1,   // last-metre hot/cold from the band's BLE beacon
};

// ---- BLE last-metre test config --------------------------------------------
// 0 = scan for ANY ResQ-Band beacon (simplest for the prototype). Set to a
// specific band device_id to lock onto just that wearer.
#define BLE_TEST_TARGET  0u

static uint32_t   g_device_id    = 0;
// BLE beacon on the band is disabled for now (BT-radio brown-out), so boot
// into LoRa sweep - the working last-metre mode (RSSI hot/cold from the band's
// heartbeat). BLE pinpoint stays available via the mode button for when the
// band's beacon is brought back.
static SearchMode g_mode         = MODE_LORA_SWEEP;
static uint32_t   g_last_tick_ms = 0;

static uint32_t   g_target_id    = BLE_TEST_TARGET;
static int16_t    g_last_rssi    = -120;
static uint8_t    g_last_hr      = 0;
static uint8_t    g_last_spo2    = 0;
static uint8_t    g_last_batt    = 0;
static uint8_t    g_last_triage  = 0;

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
  return (m == MODE_LORA_SWEEP) ? "LoRa-Sweep" : "BLE-Pinpoint";
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

volatile bool g_lora_rx_flag = false;
volatile int  g_lora_packet_size = 0;

static void IRAM_ATTR lora_isr() {
  g_lora_rx_flag = true;
}

static bool connect_lora() {
  // Use standard VSPI pins for LoRa on classic ESP32
  SPI.begin(18, 19, 23, -1);
  LoRa.setPins(PIN_LORA_SS, PIN_LORA_RST, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) return false;
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.enableCrc();
  
  pinMode(PIN_LORA_DIO0, INPUT);
  // attachInterrupt(digitalPinToInterrupt(PIN_LORA_DIO0), lora_isr, RISING);
  
  // Start listening (continuous mode is set by parsePacket automatically if we poll)
  return true;
}

extern String g_webui_debug;
uint32_t g_rx_count = 0;

static void on_lora_rx(int packet_size) {
  g_rx_count++;
  if (packet_size < 2) return;
  
  g_lora_packet_size = packet_size;
  g_lora_rx_flag = true;
}

static void process_lora_packet() {
  int packet_size = g_lora_packet_size;
  
  uint8_t buf[64];
  size_t n = 0;
  while (LoRa.available() && n < sizeof(buf)) buf[n++] = (uint8_t)LoRa.read();

  static uint32_t c_hb = 0, c_bc = 0, c_pin = 0, c_other = 0;
  static size_t last_hb_n = 0;
  
  const uint8_t ptype = ResQ::peek_packet_type(buf, n);
  if (ptype == ResQ::PKT_HEARTBEAT || ptype == ResQ::PKT_SOS_TAP || ptype == ResQ::PKT_SOS_FALL) {
    c_hb++;
    last_hb_n = n;
  }
  else if (ptype == ResQ::PKT_BEACON) c_bc++;
  else if (ptype == ResQ::PKT_PIN_SIGHTING) c_pin++;
  else c_other++;

  g_webui_debug = "HB:" + String(c_hb) + " (n=" + String(last_hb_n) + ") BC:" + String(c_bc) + " PIN:" + String(c_pin) + " O:" + String(c_other);
    // We care about HEARTBEAT or SOS from band_node
  if ((ptype == ResQ::PKT_HEARTBEAT || ptype == ResQ::PKT_SOS_TAP || ptype == ResQ::PKT_SOS_FALL) 
      && n >= sizeof(ResQ::SOSPacket)) {
    ResQ::SOSPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    
    int16_t current_rssi = LoRa.packetRssi();
    update_seen_band(pkt.device_id, current_rssi, pkt.heart_rate, pkt.spo2);
    
    // Auto lock-on to the first seen band or update if it's the current target
    if (g_target_id == 0 || g_target_id == pkt.device_id) {
      g_target_id   = pkt.device_id;
      g_last_rssi   = current_rssi;
      g_last_hr     = pkt.heart_rate;
      g_last_spo2   = pkt.spo2;
      g_last_batt   = pkt.battery_pct;
      g_last_triage = pkt.triage_level;

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
      const bool isNewTarget = (g_target_id != pkt.target_band_id);

      // Lock onto the dispatched band and adopt the wearer vitals that rode
      // along with the command, so the rescuer sees who they are fetching and
      // their condition the instant the dispatch arrives - even if this
      // handheld has never heard the band's own heartbeat.
      g_target_id   = pkt.target_band_id;
      g_last_hr     = pkt.heart_rate;
      g_last_spo2   = pkt.spo2;
      g_last_batt   = pkt.battery_pct;
      g_last_triage = pkt.triage_level;
      if (pkt.best_rssi != -127) g_last_rssi = pkt.best_rssi;
      ble_set_target(g_target_id);   // point the BLE scanner at this wearer

      // Feed it into the seen-bands table so the WebUI target list shows the
      // vitals right away.
      update_seen_band(pkt.target_band_id, pkt.best_rssi, pkt.heart_rate, pkt.spo2);

      Serial.printf("[ASSIGNMENT] band=%08X  HR=%u SpO2=%u batt=%u%% triage=%u score=%u\n",
                    (unsigned)pkt.target_band_id, pkt.heart_rate, pkt.spo2,
                    pkt.battery_pct, pkt.triage_level, pkt.priority_score);

      display_lora_sweep(g_target_id, g_last_rssi, g_last_hr, g_last_spo2);

      if (isNewTarget) {
        // Distinct triple buzz for a fresh dispatch
        for (int i = 0; i < 3; i++) {
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
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
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
  
  // 4. BLE scanner runs continuously in the background on the 2.4 GHz radio,
  //    independent of LoRa (433 MHz) and coexisting with the WiFi AP. The
  //    pinpoint mode just reads its smoothed RSSI - no SPI, so LoRa stays up.
  init_ble_scanner();
  ble_set_target(g_target_id);

  if (g_mode == MODE_BLE_PINPOINT) {
    display_uwb_pinpoint(g_target_id, -1.0f, 0.0f);  // radar panel, reused for BLE
  } else {
    display_lora_sweep(0, 0, 0, 0); // initial draw
  }
}

void loop() {
  const uint32_t now = millis();
  
  static uint32_t alive_print_ms = 0;
  if (now - alive_print_ms > 2000) {
      alive_print_ms = now;
      Serial.printf("[resq_node] Alive. Mode: %s\n", mode_label(g_mode));
  }

  // --- Hybrid Polling + Interrupt ---
  // Process LoRa in EVERY mode. Gating this on LoRa-sweep meant that in
  // BLE-pinpoint (the boot default) the node silently dropped every packet -
  // including the dispatch ASSIGNMENT and the band heartbeats that carry the
  // wearer's live HR, so the vitals panel stayed at "--".
  // --- Poll LoRa instead of using ISR ---
  int packet_size = LoRa.parsePacket();
  if (packet_size) {
    g_lora_packet_size = packet_size;
    process_lora_packet();
  }

  static uint32_t last_btn_ms = 0;
  if ((digitalRead(PIN_BTN_MODE) == LOW && now - last_btn_ms > 250) || webui_wants_mode_switch) {
    if (webui_wants_mode_switch) webui_wants_mode_switch = false;
    
    last_btn_ms = now;
    g_mode = (g_mode == MODE_LORA_SWEEP) ? MODE_BLE_PINPOINT : MODE_LORA_SWEEP;
    Serial.printf("[Mode] Switched to %s\n", mode_label(g_mode));

    // BLE never touches the SPI bus, so LoRa can stay initialised across the
    // switch - no sleep / re-init dance needed.
    if (g_mode == MODE_BLE_PINPOINT) {
      ble_set_target(g_target_id);
      display_uwb_pinpoint(g_target_id, -1.0f, 0.0f);
    } else {
      display_lora_sweep(g_target_id, g_last_rssi, g_last_hr, g_last_spo2);
    }
  }

  // Check manual WebUI assignment
  if (webui_wants_target_id != 0) {
    if (g_target_id != webui_wants_target_id) {
      g_target_id = webui_wants_target_id;
      ble_set_target(g_target_id);
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
  
  // Cleanup old seen bands (>30s)
  uint32_t cleanup_now = millis();
  for (size_t i = 0; i < g_seen_count; ) {
    if (cleanup_now - g_seen_bands[i].last_seen_ms > 30000) {
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
        ble_set_target(0);
        g_last_hr = g_last_spo2 = g_last_batt = g_last_triage = 0;
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
    digitalWrite(PIN_LED_STATUS, ((now / 500) & 1) ? HIGH : LOW);
    
    if (g_mode == MODE_BLE_PINPOINT) {
      int rssi = 0; uint32_t age = 0;
      float dist = -1.0f;   // <0 => "Out of Range" on the display
      if (ble_get_target(&rssi, &age) && age < 3000) {
        // RSSI -> rough distance (path loss: ref -55 dBm @ 1 m, n = 2.5).
        // This is a proximity estimate for hot/cold, not a precise range.
        dist = powf(10.0f, (-55.0f - (float)rssi) / (10.0f * 2.5f));
        g_last_rssi = (int16_t)rssi;
      }
      display_uwb_pinpoint(g_target_id, dist, 0.0f);
      webui_set_target(g_target_id, g_mode, dist, 0.0f);
    }
  }
  
  webui_set_bands(g_seen_bands, g_seen_count);
  webui_set_vitals(g_last_hr, g_last_spo2, g_last_batt, g_last_triage);
  
  if (g_mode == MODE_LORA_SWEEP) {
    webui_set_target(g_target_id, g_mode, -1.0f, 0.0f);
  }

  webui_loop();
}
