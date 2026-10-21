#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "ResQConfig.h"
#include "ResQProtocol.h"
#include "Display.h"
#include "WebUI.h"
#include "Calibration.h"

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
// Log-distance path loss:  d = 10 ^ ((RSSI@1m - rssi) / (10 * n))
//
// RSSI@1m and n come from Calibration (set in the field from the WebUI's
// calibration panel and kept in NVS). Until then these defaults apply.
// RSSI@1m scales with the band's TX power, so the default is derived from
// LORA_BAND_TX_POWER_DBM plus the path loss at 1 m, RSSI_1M_LOSS.
// Field fit 2026-10-20, band -> handheld at 1/2/5/8/10/15/23.5 m:
// RSSI@1m = -75.8 dBm, n = 2.51, R^2 0.97  ->  RSSI_1M_LOSS = 10 - (-75.8) ~= 86.
// The dispatcher web's default path-loss model uses the same curve.
static const float RSSI_1M_LOSS = 86.0f;
static const float DEFAULT_RSSI_REF_1M = (float)LORA_BAND_TX_POWER_DBM - RSSI_1M_LOSS;
static const float DEFAULT_PATH_LOSS_N = 2.5f;    // 2.0 = free space; higher through rubble

// Packet RSSI stops at the noise floor (~-120 dBm at SF9); below it the SNR
// goes negative and the signal power is about RSSI + SNR.
// SpO2 0xFF flags "band has no pulse sensor" (with TRIAGE_NO_DATA); show it as no value.
static uint8_t shown_spo2(uint8_t v) { return v == ResQ::SPO2_NO_SENSOR ? 0 : v; }

static int16_t effective_rssi(int16_t rssi, float snr) {
  return snr < 0 ? (int16_t)lroundf((float)rssi + snr) : rssi;
}

// Rough distance in metres from a packet RSSI (<0 => unknown / out of range).
static float rssi_to_distance_m(int16_t rssi) {
  if (rssi >= 0 || rssi <= RSSI_NONE) return -1.0f;   // no / invalid reading
  float d = powf(10.0f, (cal_ref_1m() - (float)rssi) / (10.0f * cal_n()));
  if (d < 0.5f)    d = 0.5f;                       // clamp implausibly small
  if (d > 2000.0f) return -1.0f;                   // beyond useful range
  return d;
}

#define MAX_SEEN_BANDS 10
static SeenBand g_seen_bands[MAX_SEEN_BANDS];
static size_t g_seen_count = 0;

static int16_t median_rssi(const int16_t* v, uint8_t n) {
  int16_t s[SEEN_RSSI_SAMPLES];
  memcpy(s, v, n * sizeof(int16_t));
  for (uint8_t i = 1; i < n; i++)
    for (uint8_t j = i; j > 0 && s[j - 1] > s[j]; j--) { int16_t t = s[j]; s[j] = s[j - 1]; s[j - 1] = t; }
  return (n % 2) ? s[n / 2] : (int16_t)((s[n / 2 - 1] + s[n / 2]) / 2);
}

// rssi = RSSI_NONE when the band was not heard by this handheld (e.g. vitals
// relayed in an ASSIGNMENT): vitals update, distance stays on our own readings.
static void update_seen_band(uint32_t id, int16_t rssi, uint8_t hr, uint8_t spo2) {
  uint32_t now = millis();
  SeenBand* b = nullptr;
  for (size_t i = 0; i < g_seen_count; i++)
    if (g_seen_bands[i].id == id) { b = &g_seen_bands[i]; break; }
  if (!b) {
    size_t slot = g_seen_count;
    if (g_seen_count < MAX_SEEN_BANDS) g_seen_count++;
    else {
      slot = 0;   // replace the oldest
      for (size_t i = 1; i < MAX_SEEN_BANDS; i++)
        if (now - g_seen_bands[i].last_seen_ms > now - g_seen_bands[slot].last_seen_ms) slot = i;
    }
    b = &g_seen_bands[slot];
    *b = SeenBand{};
    b->id = id;
    b->rssi = RSSI_NONE;
  }
  b->hr = hr;
  b->spo2 = spo2;
  b->last_seen_ms = now;
  if (rssi == RSSI_NONE) return;

  // Median of the last few heartbeats (~10 s apart) evens out fading; start
  // over after a long silence so an old reading doesn't drag a fresh one.
  if (b->n_samples && now - b->last_heard_ms > 30000) b->n_samples = 0;
  if (b->n_samples == SEEN_RSSI_SAMPLES) {
    memmove(b->samples, b->samples + 1, (SEEN_RSSI_SAMPLES - 1) * sizeof(int16_t));
    b->n_samples--;
  }
  b->samples[b->n_samples++] = rssi;
  b->rssi = median_rssi(b->samples, b->n_samples);
  b->last_heard_ms = now;
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
  LoRa.setTxPower(LORA_TX_POWER_DBM);
  LoRa.enableCrc();
  
  pinMode(PIN_LORA_DIO0, INPUT);
  // attachInterrupt(digitalPinToInterrupt(PIN_LORA_DIO0), lora_isr, RISING);
  
  // Start listening (continuous mode is set by parsePacket automatically if we poll)
  return true;
}

extern String g_webui_debug;

// ---- Buzzer ------------------------------------------------------------------
// The handheld uses a piezo (passive) buzzer, which is silent on a plain HIGH;
// drive it with a tone. Build with -D BUZZER_PASSIVE=0 for an active buzzer.
#ifndef BUZZER_PASSIVE
#define BUZZER_PASSIVE 1
#endif
static constexpr uint8_t BUZZER_LEDC_CH = 0;

static void buzzer_begin() {
#if BUZZER_PASSIVE
  ledcSetup(BUZZER_LEDC_CH, 2500, 8);
  ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CH);
  ledcWriteTone(BUZZER_LEDC_CH, 0);
#else
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
#endif
}

static void beep(uint16_t ms, uint16_t freq = 2500) {
#if BUZZER_PASSIVE
  ledcWriteTone(BUZZER_LEDC_CH, freq);
  delay(ms);
  ledcWriteTone(BUZZER_LEDC_CH, 0);
#else
  (void)freq;
  digitalWrite(PIN_BUZZER, HIGH);
  delay(ms);
  digitalWrite(PIN_BUZZER, LOW);
#endif
}

// ---- Ring the target band ------------------------------------------------------
// The band only listens briefly once per TDMA cycle, so repeat RING_CMD until
// it acks (see RING_RETRY_* in ResQConfig.h).
static struct {
  uint32_t band_id    = 0;   // 0 = idle
  uint32_t until_ms   = 0;
  uint32_t next_tx_ms = 0;
} g_ring;

static void tx_ring_cmd(uint32_t band_id) {
  ResQ::RingCmdPacket pkt;
  ResQ::fill_ring_cmd(pkt, band_id, 5000 /*ms*/, 2500 /*Hz*/, 2 /*siren*/);
  LoRa.idle();
  if (LoRa.beginPacket()) {
    LoRa.write(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    LoRa.endPacket();
  }
  LoRa.receive();
}

static void start_ring(uint32_t band_id) {
  if (band_id == 0) return;
  g_ring.band_id    = band_id;
  g_ring.until_ms   = millis() + RING_RETRY_WINDOW_MS;
  g_ring.next_tx_ms = 0;
  webui_set_ring_state(RING_SENDING);
  beep(60);
  Serial.printf("[RING] requesting band %08X to sound\n", (unsigned)band_id);
}

// ---- FOUND report to MainNode ---------------------------------------------------
// MainNode does not ack FOUND, so the report is sent FOUND_REPEATS times in
// this node's TDMA slot (8), one per cycle. Without a recent beacon (MainNode
// out of earshot) it is sent right away instead, once a second.
static constexpr uint8_t  FOUND_REPEATS        = 3;
static constexpr uint32_t BEACON_FRESH_MS      = 30000;
static uint32_t g_beacon_start_ms = 0;   // millis() when slot 0 of the last heard cycle began
static uint32_t g_beacon_cycle    = 0;

static struct {
  uint32_t band_id   = 0;
  uint8_t  outcome   = 0;
  int16_t  rssi      = 0;
  uint8_t  left      = 0;      // sends remaining; 0 = idle
  uint32_t last_cycle = 0xFFFFFFFF;
  uint32_t last_ms   = 0;
} g_found;

static void queue_found(uint32_t band_id, ResQ::FoundOutcome outcome, int16_t rssi) {
  if (band_id == 0) return;
  g_found.band_id = band_id;
  g_found.outcome = outcome;
  g_found.rssi = rssi;
  g_found.left = FOUND_REPEATS;
  g_found.last_cycle = 0xFFFFFFFF;
  g_found.last_ms = 0;
  webui_set_found_state(FOUND_SENDING);
  Serial.printf("[FOUND] band %08X outcome=%s queued\n", (unsigned)band_id, ResQ::found_outcome_label(outcome));
}

static void tx_found() {
  ResQ::FoundPacket pkt;
  ResQ::fill_found(pkt, g_device_id, g_found.band_id, millis(), g_found.rssi, (ResQ::FoundOutcome)g_found.outcome);
  LoRa.idle();
  if (LoRa.beginPacket()) {
    LoRa.write(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    LoRa.endPacket();
  }
  LoRa.receive();
}

static void update_found(uint32_t now) {
  if (g_found.left == 0) return;
  bool send = false;
  const bool synced = g_beacon_start_ms != 0 && now - g_beacon_start_ms < BEACON_FRESH_MS;
  if (synced) {
    const uint32_t since  = now - g_beacon_start_ms;
    const uint32_t cycle  = g_beacon_cycle + since / TDMA_CYCLE_MS;
    const uint32_t inCyc  = since % TDMA_CYCLE_MS;
    const uint32_t slotAt = TDMA_SLOT_RESQ_NODE * TDMA_SLOT_MS;
    send = inCyc >= slotAt + TDMA_SLOT_TX_OFFSET_MS && inCyc < slotAt + TDMA_SLOT_TX_WINDOW_MS && cycle != g_found.last_cycle;
    if (send) g_found.last_cycle = cycle;
  } else {
    send = now - g_found.last_ms >= 1000;
  }
  if (!send) return;
  g_found.last_ms = now;
  tx_found();
  if (--g_found.left == 0) {
    webui_set_found_state(FOUND_SENT);
    Serial.printf("[FOUND] band %08X reported%s\n", (unsigned)g_found.band_id, synced ? " in slot 8" : " (no beacon, sent immediately)");
    beep(80); delay(80); beep(80);
  }
}

static void update_ring(uint32_t now) {
  // A new target starts with a clean ring status on the WebUI.
  static uint32_t s_last_target = 0;
  if (g_target_id != s_last_target) {
    s_last_target = g_target_id;
    if (g_ring.band_id != g_target_id) { g_ring.band_id = 0; webui_set_ring_state(RING_IDLE); }
    if (g_found.left == 0 && g_target_id != 0) webui_set_found_state(FOUND_IDLE);
  }
  if (g_ring.band_id == 0) return;
  if (now >= g_ring.until_ms) {
    Serial.printf("[RING] no ack from %08X\n", (unsigned)g_ring.band_id);
    g_ring.band_id = 0;
    webui_set_ring_state(RING_TIMEOUT);
  } else if (now >= g_ring.next_tx_ms) {
    g_ring.next_tx_ms = now + RING_RETRY_INTERVAL_MS;
    tx_ring_cmd(g_ring.band_id);
  }
}
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
  const bool is_band_pkt = ptype == ResQ::PKT_HEARTBEAT || ptype == ResQ::PKT_SOS_TAP ||
                           ptype == ResQ::PKT_SOS_FALL  || ptype == ResQ::PKT_SOS_VITALS;
  if (is_band_pkt) {
    c_hb++;
    last_hb_n = n;
  }
  else if (ptype == ResQ::PKT_BEACON) {
    c_bc++;
    if (n >= sizeof(ResQ::BeaconPacket)) {
      ResQ::BeaconPacket b;
      memcpy(&b, buf, sizeof(b));
      if (ResQ::verify_beacon(b)) {
        g_beacon_start_ms = millis() - TDMA_BEACON_AIRTIME_MS;
        g_beacon_cycle = b.cycle_id;
      }
    }
  }
  else if (ptype == ResQ::PKT_PIN_SIGHTING) c_pin++;
  else c_other++;

  g_webui_debug = "HB:" + String(c_hb) + " (n=" + String(last_hb_n) + ") BC:" + String(c_bc) + " PIN:" + String(c_pin) + " O:" + String(c_other);
    // We care about HEARTBEAT or SOS from band_node
  if (is_band_pkt && n >= sizeof(ResQ::SOSPacket)) {
    ResQ::SOSPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    
    int16_t current_rssi = effective_rssi(LoRa.packetRssi(), LoRa.packetSnr());
    update_seen_band(pkt.device_id, current_rssi, pkt.heart_rate, shown_spo2(pkt.spo2));
    cal_feed(pkt.device_id, current_rssi);
    
    // Auto lock-on to the first seen band or update if it's the current target
    if (g_target_id == 0 || g_target_id == pkt.device_id) {
      g_target_id   = pkt.device_id;
      g_last_rssi   = current_rssi;
      g_last_hr     = pkt.heart_rate;
      g_last_spo2   = shown_spo2(pkt.spo2);
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
  } else if (ptype == ResQ::PKT_RING_ACK && n >= sizeof(ResQ::RingAckPacket)) {
    ResQ::RingAckPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    if (ResQ::verify_ring_ack(pkt) && pkt.device_id == g_ring.band_id) {
      g_ring.band_id = 0;
      webui_set_ring_state(RING_ACKED);
      Serial.printf("[RING] band %08X is sounding\n", (unsigned)pkt.device_id);
      beep(80); delay(80); beep(80);   // double beep = band confirmed
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
      g_last_spo2   = shown_spo2(pkt.spo2);
      g_last_batt   = pkt.battery_pct;
      g_last_triage = pkt.triage_level;

      // Feed it into the seen-bands table so the WebUI target list shows the
      // vitals right away. best_rssi is what a pillar heard, not this handheld,
      // so it must not feed the distance estimate.
      update_seen_band(pkt.target_band_id, RSSI_NONE, pkt.heart_rate, shown_spo2(pkt.spo2));

      Serial.printf("[ASSIGNMENT] band=%08X  HR=%u SpO2=%u batt=%u%% triage=%u score=%u\n",
                    (unsigned)pkt.target_band_id, pkt.heart_rate, shown_spo2(pkt.spo2),
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
  buzzer_begin();
  pinMode(PIN_BTN_MODE,   INPUT_PULLUP);
  pinMode(PIN_BTN_FOUND,  INPUT_PULLUP);

  digitalWrite(PIN_LED_STATUS, LOW);
  digitalWrite(PIN_VIBRATION,  LOW);
  beep(150);   // boot beep: proves the buzzer works

  g_device_id = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFFF);
  print_banner();
  
  // 1. WiFi AP must start first, before any SPI device calls SPI.begin()
  cal_begin(DEFAULT_RSSI_REF_1M, DEFAULT_PATH_LOSS_N);
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

  // Ring button (MODE): make the locked band sound so rescuers can hear it
  {
    static bool     s_btn_was_down = false;
    static uint32_t s_btn_down_ms  = 0;
    const bool down = digitalRead(PIN_BTN_MODE) == LOW;
    if (down && !s_btn_was_down) s_btn_down_ms = now;
    if (!down && s_btn_was_down && now - s_btn_down_ms > 50) start_ring(g_target_id);   // debounced release
    s_btn_was_down = down;
  }
  if (webui_wants_ring) {
    webui_wants_ring = false;
    start_ring(g_target_id);
  }
  update_ring(now);

  // Outcome chosen on the WebUI (rescued / deceased / not found / backup)
  if (webui_wants_found != 0 && g_target_id != 0) {
    const auto outcome = (ResQ::FoundOutcome)(webui_wants_found - 1);
    queue_found(g_target_id, outcome, g_last_rssi);
    if (outcome == ResQ::OUTCOME_RESCUED || outcome == ResQ::OUTCOME_DECEASED) {
      g_target_id = 0;   // this person is done; free the handheld for the next one
      g_last_hr = g_last_spo2 = g_last_batt = g_last_triage = 0;
    }
  }
  webui_wants_found = 0;
  update_found(now);

  // Found button (Confirm rescued) - Requires 1-second hold to avoid EMI triggers
  static uint32_t btn_found_press_start = 0;
  if (digitalRead(PIN_BTN_FOUND) == LOW) {
    if (btn_found_press_start == 0) {
      btn_found_press_start = now;
    } else if (now - btn_found_press_start > 1000) {
      if (g_target_id != 0) {
        Serial.printf("[FOUND] Confirming rescue for %08X\n", g_target_id);
        beep(200);
        queue_found(g_target_id, ResQ::OUTCOME_RESCUED, g_last_rssi);
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
        if (g_seen_bands[i].id == g_target_id && g_seen_bands[i].n_samples &&
            (now - g_seen_bands[i].last_heard_ms) < 25000) {
          dist = rssi_to_distance_m(g_seen_bands[i].rssi);
          break;
        }
      }
    }

    webui_set_target(g_target_id, 0, dist, 0.0f);
  }

  webui_set_bands(g_seen_bands, g_seen_count);
  webui_set_vitals(g_last_hr, g_last_spo2, g_last_batt, g_last_triage);

  cal_tick();
  webui_loop();
}
