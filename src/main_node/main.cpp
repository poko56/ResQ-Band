// ============================================================================
// ResQ-MainNode  (ESP32-S3)
// ----------------------------------------------------------------------------
// - TDMA coordinator: broadcasts a BEACON every TDMA_CYCLE_MS in slot 0 so all
//   bands/pins/nodes stay phase-aligned to it.
// - Receives HEARTBEAT, SOS_TAP, SOS_FALL, PIN_SIGHTING, FOUND, RING_ACK.
// - Computes a triage priority score per Band and TX'es an ASSIGNMENT to the
//   highest-priority unassigned band in slot 7.
// - USB-CDC native serial acts as a JSON bridge to the web dispatcher
//   (line-delimited JSON, one event per line).
// - Buzzer trips locally if an SOS arrives and no ResQ-Node ack within
//   MAIN_NODE_ALARM_MS (operator may not be at the web yet).
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "ResQConfig.h"
#include "ResQProtocol.h"

// ----------------------------------------------------------------------------
// State
// ----------------------------------------------------------------------------
struct BandState {
  uint32_t      band_id;
  bool          known;
  bool          is_assigned;
  bool          is_rescued;
  uint32_t      assigned_node_id;
  uint32_t      assigned_at_ms;
  uint32_t      last_heartbeat_ms;
  ResQ::SOSPacket last_packet;
  int16_t       pin_rssi[TDMA_MAX_PINS];
  int8_t        pin_snr[TDMA_MAX_PINS];
  uint32_t      pin_last_heard_ms[TDMA_MAX_PINS];
  int16_t       manual_boost;
  ResQ::AssignReason last_reason;
  uint8_t       last_score;
};

struct PinState {
  uint32_t pin_device_id;
  bool     online;
  uint32_t last_sighting_ms;
};

struct ResQNodeState {
  uint32_t node_id;
  uint32_t last_seen_ms;
  uint32_t current_assignment_band;
};

constexpr uint8_t MAX_BANDS = 8;
constexpr uint8_t MAX_NODES = 4;

static BandState     g_bands[MAX_BANDS];
static PinState      g_pins[TDMA_MAX_PINS] = {};
static ResQNodeState g_nodes[MAX_NODES]    = {};

static uint32_t g_main_id              = 0;
static uint32_t g_cycle_id             = 0;
static uint32_t g_last_beacon_ms       = 0;
static uint32_t g_last_dispatch_ms     = 0;
static uint32_t g_last_usb_hb_ms       = 0;
static uint32_t g_last_lora_retry_ms   = 0;
static uint32_t g_last_led_ms          = 0;
static uint32_t g_last_rx_flash_ms     = 0;
static uint32_t g_last_tx_flash_ms     = 0;
static uint32_t g_last_host_msg_ms     = 0;   // last serial cmd from web
static uint32_t g_rx_count             = 0;
static uint32_t g_rx_dropped           = 0;
static uint32_t g_sos_pending_until_ms = 0;   // local alarm deadline
static bool     g_local_alarm_active   = false;
static bool     g_lora_ready           = false;
static int      g_pending_dispatch_idx = -1;

// ----------------------------------------------------------------------------
// Forward decls
// ----------------------------------------------------------------------------
static void   send_json_event(const JsonDocument& doc);
static void   handle_serial_command(const char* line, size_t len);
static void   tx_beacon();
static void   tx_assignment();
static void   tx_ring_cmd(uint32_t band_id, uint16_t dur_ms, uint16_t freq, uint8_t pat);
static int    find_or_create_band(uint32_t band_id);
static void   on_lora_rx(int packet_size);
static void   update_status_led(uint32_t now);
static int    compute_score(const BandState& b, uint32_t now_ms);
static int    pick_dispatch_target(uint32_t now_ms);
static int    pick_best_pin(const BandState& b);
static ResQ::AssignReason classify_reason(const BandState& b, uint32_t now_ms);

// ============================================================================
// Setup
// ============================================================================
// Returns true on success. Non-fatal: caller may retry from loop().
static bool connect_lora() {
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_SS);
  LoRa.setPins(PIN_LORA_SS, PIN_LORA_RST, PIN_LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    return false;
  }
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setTxPower(LORA_TX_POWER_DBM);
  LoRa.enableCrc();
  LoRa.onReceive(on_lora_rx);
  LoRa.receive();
  return true;
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  setCpuFrequencyMhz(240);

  Serial.begin(USB_SERIAL_BAUD);
  // Give HWCDC a moment to settle so the first Hello isn't lost to a
  // late host enumeration. Non-blocking - we do not wait for !Serial.
  delay(250);

  // PIN_LED_STATUS is a WS2812 neopixel - DO NOT pinMode(OUTPUT) + digitalWrite.
  // Just use neopixelWrite(). First call sets pin mode internally.
  neopixelWrite(PIN_LED_STATUS, 0, 0, 40);   // boot indicator: dim blue
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  g_main_id = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF);

  // Initialize band slots
  for (uint8_t i = 0; i < MAX_BANDS; ++i) {
    g_bands[i].known = false;
  }

  // LoRa is non-fatal at boot - keep main loop alive so the web UI can
  // still connect and show "MainNode online but LoRa missing" diagnostics.
  g_lora_ready = connect_lora();
  g_last_lora_retry_ms = millis();

  // Hello banner via JSON line
  JsonDocument doc;
  doc["t"]       = "hello";
  doc["fw"]      = FW_VERSION;
  doc["board"]   = BOARD_NAME;
  char id_hex[9];
  snprintf(id_hex, sizeof(id_hex), "%08X", g_main_id);
  doc["main_id"] = id_hex;
  doc["lora_mhz"] = LORA_FREQUENCY / 1e6;
  doc["sf"]      = LORA_SPREADING_FACTOR;
  doc["tdma_cycle_ms"] = TDMA_CYCLE_MS;
  doc["lora_ready"]    = g_lora_ready;
  doc["ts"]      = (uint32_t)millis();
  send_json_event(doc);

  if (!g_lora_ready) {
    JsonDocument warn;
    warn["t"]   = "lora_init_failed";
    warn["msg"] = "SX1278/SX1276 not detected on SPI - will retry every 5s";
    warn["pins"]["sck"]  = PIN_LORA_SCK;
    warn["pins"]["miso"] = PIN_LORA_MISO;
    warn["pins"]["mosi"] = PIN_LORA_MOSI;
    warn["pins"]["ss"]   = PIN_LORA_SS;
    warn["pins"]["rst"]  = PIN_LORA_RST;
    warn["pins"]["dio0"] = PIN_LORA_DIO0;
    send_json_event(warn);
  }
}

// ============================================================================
// Main loop
// ============================================================================
void loop() {
  const uint32_t now = millis();

  // --- Periodic LoRa retry if init failed at boot ---------------------------
  if (!g_lora_ready && now - g_last_lora_retry_ms >= 5000) {
    g_last_lora_retry_ms = now;
    g_lora_ready = connect_lora();
    JsonDocument r;
    r["t"]      = g_lora_ready ? "lora_ready" : "lora_init_failed";
    r["msg"]    = g_lora_ready ? "LoRa came online" : "still no LoRa response";
    r["ts"]     = (uint32_t)millis();
    send_json_event(r);
  }

  // --- TDMA coordinator: beacon every cycle (only if radio up) --------------
  if (g_lora_ready && now - g_last_beacon_ms >= TDMA_CYCLE_MS) {
    g_last_beacon_ms = now;
    g_cycle_id++;
    tx_beacon();
  }

  // --- Dispatch assignment in slot 7 (MainNode cmd slot) --------------------
  // Re-evaluate every 2s; only TX if there's something to assign.
  if (g_lora_ready && now - g_last_dispatch_ms >= 2000) {
    g_last_dispatch_ms = now;
    int idx = pick_dispatch_target(now);
    if (idx >= 0) {
      tx_assignment();
    }
  }

  // --- Periodic stats over USB ----------------------------------------------
  if (now - g_last_usb_hb_ms >= USB_HEARTBEAT_MS) {
    g_last_usb_hb_ms = now;
    JsonDocument doc;
    doc["t"]        = "stats";
    doc["cycle"]    = g_cycle_id;
    doc["uptime_s"] = now / 1000;
    doc["rx"]       = g_rx_count;
    doc["dropped"]  = g_rx_dropped;
    doc["lora_ready"] = g_lora_ready;
    uint8_t known = 0;
    for (uint8_t i = 0; i < MAX_BANDS; ++i) if (g_bands[i].known) known++;
    doc["bands"]    = known;
    doc["heap"]     = ESP.getFreeHeap();
    doc["ts"]       = now;
    send_json_event(doc);
  }

  // --- RGB status LED -------------------------------------------------------
  update_status_led(now);

  // --- Local alarm if SOS unacked too long ----------------------------------
  if (g_sos_pending_until_ms > 0 && now > g_sos_pending_until_ms) {
    g_local_alarm_active = true;
    g_sos_pending_until_ms = 0;
  }
  if (g_local_alarm_active) {
    digitalWrite(PIN_BUZZER, (now / 250) & 1);
  } else {
    digitalWrite(PIN_BUZZER, LOW);
  }

  // --- Drain serial command queue -------------------------------------------
  static char   line_buf[512];
  static size_t line_len = 0;
  while (Serial.available()) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (line_len > 0) {
        line_buf[line_len] = 0;
        handle_serial_command(line_buf, line_len);
        line_len = 0;
      }
    } else if (line_len < sizeof(line_buf) - 1) {
      line_buf[line_len++] = (char)c;
    } else {
      line_len = 0;  // overflow - drop line
    }
  }
}

// ============================================================================
// LoRa RX
// ============================================================================
static void on_lora_rx(int packet_size) {
  if (packet_size < 2) {
    g_rx_dropped++;
    while (LoRa.available()) LoRa.read();
    return;
  }

  uint8_t buf[64];
  size_t n = 0;
  while (LoRa.available() && n < sizeof(buf)) buf[n++] = (uint8_t)LoRa.read();

  const int  rssi = LoRa.packetRssi();
  const float snr = LoRa.packetSnr();

  uint8_t ptype = ResQ::peek_packet_type(buf, n);
  g_last_rx_flash_ms = millis();

  // -- HEARTBEAT / SOS_TAP / SOS_FALL all use SOSPacket --
  if ((ptype == ResQ::PKT_HEARTBEAT ||
       ptype == ResQ::PKT_SOS_TAP   ||
       ptype == ResQ::PKT_SOS_FALL) && n >= sizeof(ResQ::SOSPacket)) {
    ResQ::SOSPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    if (!ResQ::verify_sos_packet(pkt)) { g_rx_dropped++; return; }
    g_rx_count++;

    int idx = find_or_create_band(pkt.device_id);
    if (idx < 0) { g_rx_dropped++; return; }
    BandState& b = g_bands[idx];
    b.last_heartbeat_ms = millis();
    b.last_packet       = pkt;

    if (ptype == ResQ::PKT_SOS_TAP || ptype == ResQ::PKT_SOS_FALL) {
      g_sos_pending_until_ms = millis() + MAIN_NODE_ALARM_MS;
    }

    JsonDocument out;
    out["t"]         = "band";
    char id_hex[9]; snprintf(id_hex, sizeof(id_hex), "%08X", pkt.device_id);
    out["id"]        = id_hex;
    out["ptype"]     = ResQ::packet_type_label((ResQ::PacketType)ptype);
    out["seq"]       = pkt.sequence;
    out["triage"]    = pkt.triage_level;
    out["hr"]        = pkt.heart_rate;
    out["spo2"]      = pkt.spo2;
    out["batt"]      = pkt.battery_pct;
    out["g_x10"]     = pkt.last_g_force_x10;
    out["rssi"]      = rssi;
    out["snr"]       = snr;
    out["ts"]        = (uint32_t)millis();
    send_json_event(out);
  }
  // -- PIN_SIGHTING --
  else if (ptype == ResQ::PKT_PIN_SIGHTING && n >= sizeof(ResQ::PinSightingPacket)) {
    ResQ::PinSightingPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    if (!ResQ::verify_pin_sighting(pkt)) { g_rx_dropped++; return; }
    g_rx_count++;

    if (pkt.pin_index < TDMA_MAX_PINS) {
      g_pins[pkt.pin_index].pin_device_id    = pkt.pin_device_id;
      g_pins[pkt.pin_index].online           = true;
      g_pins[pkt.pin_index].last_sighting_ms = millis();

      for (uint8_t i = 0; i < pkt.num_sightings; ++i) {
        const ResQ::Sighting& s = pkt.sightings[i];
        if (s.band_device_id == 0) continue;
        int bidx = find_or_create_band(s.band_device_id);
        if (bidx < 0) continue;
        g_bands[bidx].pin_rssi[pkt.pin_index]          = s.rssi;
        g_bands[bidx].pin_snr[pkt.pin_index]           = s.snr;
        g_bands[bidx].pin_last_heard_ms[pkt.pin_index] = millis() - (uint32_t)s.last_seen_dms * 100UL;
      }
    }

    JsonDocument out;
    out["t"]        = "pin_sighting";
    out["pin"]      = pkt.pin_index;
    char pid[9]; snprintf(pid, sizeof(pid), "%08X", pkt.pin_device_id);
    out["pin_id"]   = pid;
    JsonArray arr = out["sightings"].to<JsonArray>();
    for (uint8_t i = 0; i < pkt.num_sightings; ++i) {
      const ResQ::Sighting& s = pkt.sightings[i];
      JsonObject o = arr.add<JsonObject>();
      char bid[9]; snprintf(bid, sizeof(bid), "%08X", s.band_device_id);
      o["band"]   = bid;
      o["rssi"]   = s.rssi;
      o["snr"]    = s.snr;
      o["age_ms"] = (uint32_t)s.last_seen_dms * 100UL;
    }
    out["rssi"]     = rssi;
    out["snr"]      = snr;
    out["ts"]       = (uint32_t)millis();
    send_json_event(out);
  }
  // -- FOUND --
  else if (ptype == ResQ::PKT_FOUND && n >= sizeof(ResQ::FoundPacket)) {
    ResQ::FoundPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    if (!ResQ::verify_found(pkt)) { g_rx_dropped++; return; }
    g_rx_count++;

    int bidx = find_or_create_band(pkt.target_band_id);
    if (bidx >= 0) {
      g_bands[bidx].is_rescued      = true;
      g_bands[bidx].is_assigned     = false;
      g_bands[bidx].assigned_node_id = 0;
    }
    g_local_alarm_active   = false;
    g_sos_pending_until_ms = 0;

    JsonDocument out;
    out["t"]        = "found";
    char nid[9]; snprintf(nid, sizeof(nid), "%08X", pkt.resq_node_id);
    char bid[9]; snprintf(bid, sizeof(bid), "%08X", pkt.target_band_id);
    out["node_id"] = nid;
    out["band"]    = bid;
    out["outcome"] = ResQ::found_outcome_label((ResQ::FoundOutcome)pkt.outcome);
    out["rssi"]    = pkt.final_rssi;
    out["ts"]      = (uint32_t)millis();
    send_json_event(out);
  }
  // -- RING_ACK --
  else if (ptype == ResQ::PKT_RING_ACK && n >= sizeof(ResQ::RingAckPacket)) {
    ResQ::RingAckPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    if (!ResQ::verify_ring_ack(pkt)) { g_rx_dropped++; return; }
    g_rx_count++;

    JsonDocument out;
    out["t"]      = "ring_ack";
    char bid[9]; snprintf(bid, sizeof(bid), "%08X", pkt.device_id);
    out["band"]   = bid;
    out["status"] = pkt.status;
    out["ts"]     = (uint32_t)millis();
    send_json_event(out);
  } else {
    g_rx_dropped++;
  }

  LoRa.receive();
}

// ============================================================================
// TX helpers
// ============================================================================
static void tx_beacon() {
  ResQ::BeaconPacket pkt;
  uint8_t flags = 0;
  if (g_sos_pending_until_ms > 0 || g_local_alarm_active) flags |= 0x01;
  ResQ::fill_beacon(pkt, g_main_id, g_cycle_id, millis(), flags);
  LoRa.idle();
  if (LoRa.beginPacket()) {
    LoRa.write(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    LoRa.endPacket();
  }
  LoRa.receive();
  g_last_tx_flash_ms = millis();

  JsonDocument doc;
  doc["t"]      = "beacon";
  doc["cycle"]  = g_cycle_id;
  doc["flags"]  = flags;
  doc["ts"]     = (uint32_t)millis();
  send_json_event(doc);
}

static void tx_assignment() {
  if (g_pending_dispatch_idx < 0) return;
  BandState& b = g_bands[g_pending_dispatch_idx];

  int best_pin = pick_best_pin(b);
  int16_t best_rssi = (best_pin >= 0) ? b.pin_rssi[best_pin] : -127;
  ResQ::AssignReason reason = classify_reason(b, millis());

  int score = compute_score(b, millis());
  if (score < 0)   score = 0;
  if (score > 255) score = 255;
  b.last_score  = (uint8_t)score;
  b.last_reason = reason;

  ResQ::AssignmentPacket pkt;
  ResQ::fill_assignment(pkt,
                        /*resq_node_id*/ 0,
                        b.band_id,
                        (uint8_t)score,
                        (uint8_t)(best_pin < 0 ? 0xFF : best_pin),
                        best_rssi,
                        (ResQ::TriageLevel)b.last_packet.triage_level,
                        reason);

  LoRa.idle();
  if (LoRa.beginPacket()) {
    LoRa.write(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    LoRa.endPacket();
  }
  LoRa.receive();
  g_last_tx_flash_ms = millis();

  b.is_assigned      = true;
  b.assigned_at_ms   = millis();
  b.assigned_node_id = 0;

  JsonDocument doc;
  doc["t"]       = "assignment";
  char bid[9]; snprintf(bid, sizeof(bid), "%08X", b.band_id);
  doc["band"]    = bid;
  doc["score"]   = score;
  doc["pin"]     = best_pin;
  doc["rssi"]    = best_rssi;
  doc["triage"]  = b.last_packet.triage_level;
  doc["reason"]  = ResQ::assign_reason_label(reason);
  doc["ts"]      = (uint32_t)millis();
  send_json_event(doc);

  g_pending_dispatch_idx = -1;
}

static void tx_ring_cmd(uint32_t band_id, uint16_t dur_ms, uint16_t freq, uint8_t pat) {
  ResQ::RingCmdPacket pkt;
  ResQ::fill_ring_cmd(pkt, band_id, dur_ms, freq, pat);
  LoRa.idle();
  if (LoRa.beginPacket()) {
    LoRa.write(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    LoRa.endPacket();
  }
  LoRa.receive();
  g_last_tx_flash_ms = millis();
}

// ============================================================================
// Triage / dispatch logic
// ============================================================================
static int find_or_create_band(uint32_t band_id) {
  for (uint8_t i = 0; i < MAX_BANDS; ++i) {
    if (g_bands[i].known && g_bands[i].band_id == band_id) return i;
  }
  for (uint8_t i = 0; i < MAX_BANDS; ++i) {
    if (!g_bands[i].known) {
      g_bands[i] = {};
      g_bands[i].band_id = band_id;
      g_bands[i].known   = true;
      for (uint8_t p = 0; p < TDMA_MAX_PINS; ++p) g_bands[i].pin_rssi[p] = -127;
      return i;
    }
  }
  return -1;
}

static int compute_score(const BandState& b, uint32_t now_ms) {
  int score = 0;
  uint8_t hr   = b.last_packet.heart_rate;
  uint8_t spo2 = b.last_packet.spo2;

  if (hr > 0) {
    if (hr < VITAL_HR_CRITICAL_LOW || hr > VITAL_HR_CRITICAL_HIGH) score += 100;
    else if (hr < VITAL_HR_WARN_LOW || hr > VITAL_HR_WARN_HIGH)    score += 30;
  }
  if (spo2 > 0) {
    if      (spo2 < VITAL_SPO2_CRITICAL) score += 80;
    else if (spo2 < VITAL_SPO2_WARN)     score += 20;
  }

  int16_t gabs = b.last_packet.last_g_force_x10;
  if (gabs < 0) gabs = -gabs;
  if (gabs > FALL_G_THRESHOLD_X10) score += 150;

  uint32_t silent_ms = (now_ms >= b.last_heartbeat_ms) ? (now_ms - b.last_heartbeat_ms) : 0;
  int silent_score = (int)((silent_ms / 1000) / 60) * 20;
  if (silent_score > 200) silent_score = 200;
  score += silent_score;

  if (b.last_packet.battery_pct > 0 && b.last_packet.battery_pct < BATT_LOW_PCT) score += 30;
  score += b.manual_boost;
  if (b.is_assigned) score -= 500;
  if (b.is_rescued)  score -= 100000;
  return score;
}

static int pick_dispatch_target(uint32_t now_ms) {
  int best_idx = -1;
  int best_score = 0;
  for (uint8_t i = 0; i < MAX_BANDS; ++i) {
    if (!g_bands[i].known) continue;
    if (g_bands[i].is_rescued) continue;
    if (g_bands[i].is_assigned) continue;
    int s = compute_score(g_bands[i], now_ms);
    if (s > best_score) {
      best_score = s;
      best_idx   = i;
    }
  }
  if (best_idx >= 0 && best_score > 0) {
    g_pending_dispatch_idx = best_idx;
    return best_idx;
  }
  return -1;
}

static int pick_best_pin(const BandState& b) {
  int best = -1; int16_t best_r = -127;
  for (uint8_t p = 0; p < TDMA_MAX_PINS; ++p) {
    if (b.pin_rssi[p] > best_r) { best_r = b.pin_rssi[p]; best = p; }
  }
  return (best_r > -127) ? best : -1;
}

static ResQ::AssignReason classify_reason(const BandState& b, uint32_t now_ms) {
  if (b.manual_boost > 0) return ResQ::REASON_MANUAL_OVERRIDE;
  if (b.last_packet.packet_type == ResQ::PKT_SOS_TAP)  return ResQ::REASON_TAP_SOS;
  if (b.last_packet.packet_type == ResQ::PKT_SOS_FALL) return ResQ::REASON_FALL_DETECTED;
  uint8_t hr = b.last_packet.heart_rate, spo2 = b.last_packet.spo2;
  if ((hr > 0 && (hr < VITAL_HR_CRITICAL_LOW || hr > VITAL_HR_CRITICAL_HIGH)) ||
      (spo2 > 0 && spo2 < VITAL_SPO2_CRITICAL)) return ResQ::REASON_VITALS_CRITICAL;
  if (b.last_packet.battery_pct > 0 && b.last_packet.battery_pct < BATT_LOW_PCT)
    return ResQ::REASON_BATTERY_LOW;
  uint32_t silent_ms = (now_ms >= b.last_heartbeat_ms) ? (now_ms - b.last_heartbeat_ms) : 0;
  if (silent_ms > 60000) return ResQ::REASON_SILENT_TOO_LONG;
  return ResQ::REASON_VITALS_CRITICAL;
}

// ============================================================================
// USB Serial JSON IO
// ============================================================================
static void send_json_event(const JsonDocument& doc) {
  serializeJson(doc, Serial);
  Serial.write('\n');
}

static uint32_t parse_hex32(const char* s) {
  if (!s) return 0;
  return (uint32_t)strtoul(s, nullptr, 16);
}

static void handle_serial_command(const char* line, size_t len) {
  g_last_host_msg_ms = millis();   // host alive marker -> LED tint
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line, len);
  if (err) {
    JsonDocument r;
    r["t"]   = "err";
    r["msg"] = "json parse";
    send_json_event(r);
    return;
  }
  const char* cmd = doc["c"] | "";

  if (!strcmp(cmd, "ring_band")) {
    uint32_t band = parse_hex32(doc["band"] | "");
    uint16_t dur  = doc["duration_ms"] | 3000;
    uint16_t freq = doc["freq"]        | 2000;
    uint8_t  pat  = doc["pattern"]     | 1;
    tx_ring_cmd(band, dur, freq, pat);
    JsonDocument r; r["t"] = "ack"; r["c"] = "ring_band"; send_json_event(r);
  }
  else if (!strcmp(cmd, "manual_priority")) {
    uint32_t band = parse_hex32(doc["band"] | "");
    int16_t boost = doc["boost"] | 1000;
    int idx = find_or_create_band(band);
    if (idx >= 0) g_bands[idx].manual_boost = boost;
    JsonDocument r; r["t"] = "ack"; r["c"] = "manual_priority"; send_json_event(r);
  }
  else if (!strcmp(cmd, "mark_rescued")) {
    uint32_t band = parse_hex32(doc["band"] | "");
    int idx = find_or_create_band(band);
    if (idx >= 0) {
      g_bands[idx].is_rescued = true;
      g_bands[idx].is_assigned = false;
    }
    g_local_alarm_active = false;
    g_sos_pending_until_ms = 0;
    JsonDocument r; r["t"] = "ack"; r["c"] = "mark_rescued"; send_json_event(r);
  }
  else if (!strcmp(cmd, "clear_alarm")) {
    g_local_alarm_active = false;
    g_sos_pending_until_ms = 0;
    JsonDocument r; r["t"] = "ack"; r["c"] = "clear_alarm"; send_json_event(r);
  }
  else if (!strcmp(cmd, "ping")) {
    // Pong carries the same identity payload as hello so the web bridge can
    // bootstrap state when it attaches AFTER the boot-time hello was lost.
    JsonDocument r;
    r["t"]            = "pong";
    char id_hex[9]; snprintf(id_hex, sizeof(id_hex), "%08X", g_main_id);
    r["main_id"]      = id_hex;
    r["fw"]           = FW_VERSION;
    r["board"]        = BOARD_NAME;
    r["cycle"]        = g_cycle_id;
    r["uptime_s"]     = (uint32_t)(millis() / 1000);
    r["lora_ready"]   = g_lora_ready;
    r["tdma_cycle_ms"] = TDMA_CYCLE_MS;
    r["ts"]           = (uint32_t)millis();
    send_json_event(r);
  }
  else {
    JsonDocument r;
    r["t"]   = "err";
    r["msg"] = "unknown cmd";
    r["c"]   = cmd;
    send_json_event(r);
  }
}

// ============================================================================
// RGB status LED  (onboard WS2812 on ESP32-S3 devkitc-1)
// ----------------------------------------------------------------------------
// Color encodes the most urgent device state; brief flashes overlay LoRa
// RX (blue) and TX (cyan) activity so the operator can tell traffic is
// flowing even without opening the dashboard.
//
//   BASE COLOR (driven by device state)
//   -----------------------------------
//   bright blue fade   | first 1.5s after boot/reset  (visible "I rebooted")
//   slow green breath  | healthy idle, LoRa ready, no work
//   slow orange breath | LoRa SX1278 not responding (check wiring)
//   pulsing red        | SOS pending - waiting for ResQ-Node to ack
//   strobe red bright  | local alarm - no ack in MAIN_NODE_ALARM_MS
//
//   OVERLAYS (transient, win over base)
//   -----------------------------------
//   80 ms blue flash   | LoRa RX packet (every received frame)
//   80 ms cyan flash   | LoRa TX packet (beacon, assignment, ring_cmd)
//   80 ms white blip   | host (web app) is talking to us - every ~1.5s
//                        while at least one serial command arrived in
//                        the last 8s, so you can see "web is connected"
//                        without checking the browser
//
// neopixelWrite() is non-blocking and self-initializes the GPIO on first call.
// ============================================================================
static inline uint8_t breath(uint32_t now_ms, uint32_t period_ms, uint8_t lo, uint8_t hi) {
  // Triangle-wave breathing, integer math (no sinf to keep ISR-safe-ish).
  uint32_t phase = now_ms % period_ms;
  uint32_t half  = period_ms / 2;
  uint32_t v     = (phase < half) ? phase : (period_ms - phase);
  return (uint8_t)(lo + (hi - lo) * v / half);
}

static void update_status_led(uint32_t now) {
  if (now - g_last_led_ms < 33) return;   // ~30 fps update cap
  g_last_led_ms = now;

  uint8_t r = 0, g = 0, b = 0;

  // ---- boot indicator: bright blue fading over 1.5s ----------------------
  // Visible signal to the operator: "I just powered on / reset just now".
  // After this window, normal state-driven colors take over.
  if (now < 1500) {
    uint8_t v = (uint8_t)(80 - (now * 60 / 1500));   // 80 -> 20 dim
    neopixelWrite(PIN_LED_STATUS, 0, v / 4, v);
    return;
  }

  // ---- base color (priority order) ----
  if (g_local_alarm_active) {
    // Bright red strobe at 5 Hz
    bool on = ((now / 100) & 1);
    r = on ? 200 : 0;
  } else if (g_sos_pending_until_ms > 0) {
    // Pulsing red - SOS in flight
    r = breath(now, 600, 40, 180);
  } else if (!g_lora_ready) {
    // Slow orange breath - radio missing, retrying
    uint8_t v = breath(now, 2000, 8, 50);
    r = v;
    g = v / 4;
  } else {
    // Healthy idle - dim green slow breath
    g = breath(now, 3000, 2, 18);
  }

  // ---- host-attached heartbeat (cool white blip every 1.5s) --------------
  // Web bridge keepalive pings us every 5s. As long as we heard from the
  // host within the last 8s, paint a short white blip so the operator can
  // see "yes, the laptop is actually talking to me".
  const bool host_attached = (g_last_host_msg_ms > 0) &&
                             (now - g_last_host_msg_ms < 8000);
  if (host_attached) {
    uint32_t phase = now % 1500;
    if (phase < 80) { r = 30; g = 30; b = 50; }   // cool-white heartbeat
  }

  // ---- activity overlays (highest priority - latest write wins) ----------
  if (now - g_last_tx_flash_ms < 80) { r = 0; g = 40; b = 40; }   // TX: cyan
  if (now - g_last_rx_flash_ms < 80) { r = 0; g = 10; b = 80; }   // RX: blue

  neopixelWrite(PIN_LED_STATUS, r, g, b);
}
