#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "ResQConfig.h"
#include "ResQProtocol.h"
#include "Display.h"
#include "WebUI.h"

// Single last-metre mode: LoRa RSSI hot/cold + distance from the band's
// heartbeat. (BLE pinpoint removed - the band's BT beacon browned out its rail.)

static uint32_t   g_device_id    = 0;
static uint32_t   g_last_tick_ms = 0;
static uint32_t   g_target_id    = 0;
static int16_t    g_last_rssi    = -120;
static uint8_t    g_last_hr      = 0;
static uint8_t    g_last_spo2    = 0;
static uint8_t    g_last_batt    = 0;
static uint8_t    g_last_triage  = 0;

// ---- RSSI -> distance (LoRa last-metre estimate) ---------------------------
// Log-distance path loss:  d = 10 ^ ((RSSI_REF_1M - rssi) / (10 * PATH_LOSS_N))
//
// RSSI_REF_1M is the packet RSSI you would measure ~1 m from the band. It scales
// with the band's TX power, so we DERIVE it from LORA_TX_POWER_DBM plus one
// calibration constant, RSSI_1M_LOSS (the path loss at 1 m, antennas included).
// Because the reference tracks TX power, dropping the band to 2 dBm no longer
// inflates the distance - the whole RSSI curve shifts down and so does the ref.
//
// To calibrate in the field: stand 1 m from the band, read the RSSI the handheld
// shows for it, then set RSSI_1M_LOSS = LORA_TX_POWER_DBM - that_rssi.
//   e.g. TX = 2 dBm and RSSI at 1 m = -38  ->  RSSI_1M_LOSS = 2 - (-38) = 40
static const float RSSI_1M_LOSS = 87.5f;                        // <-- field-calibrate
static const float RSSI_REF_1M  = (float)LORA_TX_POWER_DBM - RSSI_1M_LOSS;
static const float PATH_LOSS_N  = 2.5f;    // 2.0 = free space; higher through rubble

// Rough distance in metres from a packet RSSI (<0 => unknown / out of range).
static float rssi_to_distance_m(int16_t rssi) {
  if (rssi >= 0 || rssi <= -120) return -1.0f;   // no / invalid reading
  float d = powf(10.0f, (RSSI_REF_1M - (float)rssi) / (10.0f * PATH_LOSS_N));
  if (d < 0.1f)   d = 0.1f;                        // clamp implausibly small
  if (d > 500.0f) return -1.0f;                    // beyond useful range
  return d;
}

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

static void print_banner() {
  Serial.println();
  Serial.println(F("================================"));
  Serial.printf( "  %s\n", BOARD_NAME);
  Serial.printf( "  FW %s\n", FW_VERSION);
  Serial.printf( "  Device ID: %08X\n", g_device_id);
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
  
  display_lora_sweep(0, 0, 0, 0); // initial draw
}

void loop() {
  const uint32_t now = millis();
  
  static uint32_t alive_print_ms = 0;
  if (now - alive_print_ms > 2000) {
      alive_print_ms = now;
      Serial.println("[resq_node] Alive.");
  }

  // Poll LoRa (single mode now - LoRa RSSI last-metre; BLE removed).
  int packet_size = LoRa.parsePacket();
  if (packet_size) {
    g_lora_packet_size = packet_size;
    process_lora_packet();
  }

  // Check manual WebUI assignment
  if (webui_wants_target_id != 0) {
    if (g_target_id != webui_wants_target_id) {
      g_target_id = webui_wants_target_id;
      Serial.printf("[ASSIGNMENT] WebUI assigned band %lu\n", (unsigned long)g_target_id);
      display_lora_sweep(g_target_id, g_last_rssi, g_last_hr, g_last_spo2);
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
        g_last_hr = g_last_spo2 = g_last_batt = g_last_triage = 0;
        btn_found_press_start = 0; // reset hold timer
        display_lora_sweep(0,0,0,0);
      }
    }
  } else {
    btn_found_press_start = 0;
  }

  if (now - g_last_tick_ms >= 500) {
    g_last_tick_ms = now;
    digitalWrite(PIN_LED_STATUS, ((now / 500) & 1) ? HIGH : LOW);
    
    // Estimate range from the target band's most recent HEARTBEAT RSSI.
    // Heartbeats arrive ~once per 10 s TDMA cycle, so keep a generous
    // freshness window before declaring it out of range.
    float dist = -1.0f;   // <0 => "Out of Range" on the display
    if (g_target_id != 0) {
      for (size_t i = 0; i < g_seen_count; i++) {
        if (g_seen_bands[i].id == g_target_id &&
            (now - g_seen_bands[i].last_seen_ms) < 15000) {
          dist = rssi_to_distance_m(g_seen_bands[i].rssi);
          break;
        }
      }
    }

    webui_set_target(g_target_id, 0, dist, 0.0f);
  }

  webui_set_bands(g_seen_bands, g_seen_count);
  webui_set_vitals(g_last_hr, g_last_spo2, g_last_batt, g_last_triage);

  webui_loop();
}
