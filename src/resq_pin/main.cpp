// ============================================================================
// ResQ-Pin  (ESP32 classic, mast-mounted LoRa anchor)
// ----------------------------------------------------------------------------
// Purpose: be a *passive ear* for Bands. Records (band_id, rssi, snr,
// when_heard) for every HEARTBEAT / SOS_TAP / SOS_FALL it picks up off
// the air, then once per TDMA cycle TXes a PIN_SIGHTING report so the
// MainNode can keep a per-band RSSI fingerprint and decide which anchor
// the survivor is nearest.
//
//   slot 0          MainNode BEACON              (we listen, sync)
//   slot 1..2       Band HEARTBEAT               (we record)
//   slot 3 + PIN_IDX our PIN_SIGHTING TX          (we report)
//   slot 7          MainNode commands            (we ignore)
//   slot 8          ResQ-Node                    (we ignore)
//   slot 9          ALOHA emergency window       (we record SOS bursts)
//
// Set per-unit identity at compile time with -D PIN_INDEX=2 (0..3).
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <Preferences.h>
#include "ResQConfig.h"
#include "ResQProtocol.h"
#if ENABLE_OTA
  #include <WiFi.h>
  #include "ResQOTA.h"
#endif

// ----------------------------------------------------------------------------
// Tunables
// ----------------------------------------------------------------------------

// Drop sightings older than this from the table before TX so the report
// doesn't claim we just heard a band that's actually long gone.
static constexpr uint32_t SIGHTING_STALE_MS = TDMA_CYCLE_MS * 3;   // 30 s
static constexpr uint32_t LORA_RETRY_MS     = 5000;

// ----------------------------------------------------------------------------
// State
// ----------------------------------------------------------------------------
struct LocalSighting {
  uint32_t band_id;          // 0 = empty slot
  int16_t  rssi;
  int8_t   snr;
  uint32_t last_heard_ms;
};

static uint32_t      g_device_id          = 0;
static bool          g_lora_ready         = false;
static uint32_t      g_last_lora_retry_ms = 0;
static uint32_t      g_last_beacon_ms     = 0;
static uint32_t      g_beacon_cycle_id    = 0;
static bool          g_beacon_seen        = false;
static uint32_t      g_last_tx_cycle_id   = 0xFFFFFFFF;
static uint32_t      g_beacon_start_ms    = 0;   // millis() when slot 0 of the last heard cycle began
static uint32_t      g_total_heard        = 0;
static LocalSighting g_table[ResQ::PIN_SIGHTING_MAX] = {};
#if ENABLE_OTA
static uint32_t      g_next_ota_check_ms  = 0;
#endif

// Identify / Button state
static uint32_t      g_identify_until_ms  = 0;
static uint32_t      g_next_join_req_ms   = 0;

static Preferences   g_prefs;
static uint8_t       g_pin_index          = 255;

// ----------------------------------------------------------------------------
// LoRa init (non-fatal, same pattern as MainNode/Band)
// ----------------------------------------------------------------------------
static bool connect_lora() {
  SPI.begin();   // ESP32 classic default SPI pins (VSPI: 18/19/23/5)
  LoRa.setPins(PIN_LORA_SS, PIN_LORA_RST, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) return false;
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setTxPower(LORA_FIXED_TX_POWER_DBM);
  LoRa.enableCrc();
  LoRa.receive();
  return true;
}

// ----------------------------------------------------------------------------
// Sighting table
// ----------------------------------------------------------------------------
static void record_sighting(uint32_t band_id, int16_t rssi, int8_t snr) {
  if (band_id == 0) return;
  const uint32_t now = millis();

  // 1) Update existing entry if we already track this band
  for (auto& s : g_table) {
    if (s.band_id == band_id) {
      s.rssi          = rssi;
      s.snr           = snr;
      s.last_heard_ms = now;
      return;
    }
  }
  // 2) Drop into first empty slot
  for (auto& s : g_table) {
    if (s.band_id == 0) {
      s = { band_id, rssi, snr, now };
      return;
    }
  }
  // 3) Replace oldest entry
  size_t oldest_idx = 0;
  for (size_t i = 1; i < ResQ::PIN_SIGHTING_MAX; ++i) {
    if (g_table[i].last_heard_ms < g_table[oldest_idx].last_heard_ms) oldest_idx = i;
  }
  g_table[oldest_idx] = { band_id, rssi, snr, now };
}

static void purge_stale_sightings(uint32_t now) {
  for (auto& s : g_table) {
    if (s.band_id != 0 && now - s.last_heard_ms > SIGHTING_STALE_MS) {
      s = {};
    }
  }
}

// ----------------------------------------------------------------------------
// LoRa RX - ISR-ish callback
// ----------------------------------------------------------------------------
static void on_lora_rx(int packet_size) {
  if (packet_size < 2) {
    while (LoRa.available()) LoRa.read();
    return;
  }
  uint8_t buf[64];
  size_t n = 0;
  while (LoRa.available() && n < sizeof(buf)) buf[n++] = (uint8_t)LoRa.read();

  const int    rssi  = LoRa.packetRssi();
  const float  snrf  = LoRa.packetSnr();
  const int8_t snr   = (int8_t)constrain((int)snrf, -128, 127);
  const uint8_t ptype = ResQ::peek_packet_type(buf, n);

  // Pick up beacons for phase lock
  if (ptype == ResQ::PKT_BEACON && n >= sizeof(ResQ::BeaconPacket)) {
    ResQ::BeaconPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    if (ResQ::verify_beacon(pkt)) {
      g_beacon_seen     = true;
      g_beacon_cycle_id = pkt.cycle_id;
      g_last_beacon_ms  = millis();
      g_beacon_start_ms = g_last_beacon_ms - TDMA_BEACON_AIRTIME_MS;
    }
    return;
  }

  // Anything Band-shaped contributes to our sighting table
  if ((ptype == ResQ::PKT_HEARTBEAT ||
       ptype == ResQ::PKT_SOS_TAP   ||
       ptype == ResQ::PKT_SOS_FALL  ||
       ptype == ResQ::PKT_SOS_VITALS) &&
      n >= sizeof(ResQ::SOSPacket)) {
    ResQ::SOSPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    if (!ResQ::verify_sos_packet(pkt)) return;
    record_sighting(pkt.device_id, (int16_t)rssi, snr);
    ++g_total_heard;
    return;
  }

  // Identify command
  if (ptype == ResQ::PKT_PIN_IDENTIFY_CMD && n >= sizeof(ResQ::PinIdentifyCmdPacket)) {
    ResQ::PinIdentifyCmdPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    if (ResQ::verify_pin_identify_cmd(pkt) && pkt.pin_device_id == g_device_id) {
      g_identify_until_ms = millis() + pkt.duration_ms;
      Serial.printf("[CMD] identify for %u ms\n", pkt.duration_ms);
    }
    return;
  }

  // Set slot command
  if (ptype == ResQ::PKT_PIN_SET_SLOT_CMD && n >= sizeof(ResQ::PinSetSlotCmdPacket)) {
    ResQ::PinSetSlotCmdPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    if (ResQ::verify_pin_set_slot_cmd(pkt) && pkt.pin_device_id == g_device_id) {
      g_pin_index = pkt.slot_index;
      g_prefs.putUChar("pin_idx", g_pin_index);
      // Human-facing count is 1-based (Pillar 1..4); g_pin_index stays 0-based
      // internally for the TDMA slot math and array indexing.
      Serial.printf("[CMD] assigned to Pillar %u (pin_index %u)\n",
                    (unsigned)(g_pin_index + 1), (unsigned)g_pin_index);
      
      // Flash LED to acknowledge
      for (int i=0; i<3; i++) {
        digitalWrite(PIN_LED_STATUS, HIGH);
        delay(100);
        digitalWrite(PIN_LED_STATUS, LOW);
        delay(100);
      }
    }
    return;
  }
}

// ----------------------------------------------------------------------------
// TDMA slot gate - returns true once per cycle inside our assigned slot
// ----------------------------------------------------------------------------
static bool should_tx_report(uint32_t now) {
  if (!g_lora_ready || g_pin_index == 255) return false;

  // Solo fallback: if we never see a beacon, still cough up a report each
  // cycle so a single-pin lab setup can verify the data path end-to-end.
  if (!g_beacon_seen) {
    static uint32_t next_free = 0;
    if (now >= next_free) { next_free = now + TDMA_CYCLE_MS; return true; }
    return false;
  }

  // Project the cycle forward from the last beacon heard. This used to measure
  // the slot without wrapping at the cycle length, so a single missed beacon
  // pushed the slot index past 9 and the pillar went silent until it heard
  // another one - a pillar at the edge of MainNode's range looked offline for
  // long stretches. Clock drift is ~1 ms/min, far inside the slot window, so
  // extrapolating across missed beacons is safe.
  const uint32_t since    = now - g_beacon_start_ms;
  const uint32_t cycle    = g_beacon_cycle_id + since / TDMA_CYCLE_MS;
  const uint32_t in_cycle = since % TDMA_CYCLE_MS;
  const uint32_t slot_at  = (TDMA_SLOT_PIN_BASE + g_pin_index) * TDMA_SLOT_MS;

  if (in_cycle >= slot_at + TDMA_SLOT_TX_OFFSET_MS &&
      in_cycle <  slot_at + TDMA_SLOT_TX_WINDOW_MS &&
      cycle != g_last_tx_cycle_id) {
    g_last_tx_cycle_id = cycle;
    return true;
  }
  return false;
}

// ----------------------------------------------------------------------------
// TX report
// ----------------------------------------------------------------------------
static void tx_sighting_report() {
  const uint32_t now = millis();
  purge_stale_sightings(now);

  ResQ::PinSightingPacket pkt;
  ResQ::init_pin_sighting(pkt, g_pin_index, g_device_id);

  for (const auto& s : g_table) {
    if (s.band_id == 0) continue;
    const uint32_t age_ms  = now - s.last_heard_ms;
    const uint16_t age_dms = (age_ms / 100 > UINT16_MAX) ? UINT16_MAX : (uint16_t)(age_ms / 100);
    ResQ::add_pin_sighting(pkt, s.band_id, s.rssi, s.snr, age_dms);
  }
  ResQ::finalize_pin_sighting(pkt);

  digitalWrite(PIN_LED_STATUS, HIGH);
  LoRa.idle();
  bool ok = false;
  if (LoRa.beginPacket()) {
    LoRa.write(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    ok = LoRa.endPacket() == 1;
  }
  LoRa.receive();
  digitalWrite(PIN_LED_STATUS, LOW);

  if (g_beacon_seen) {
    Serial.printf("[REPORT] cycle=%lu (beacon %lus ago) ",
                  (unsigned long)g_last_tx_cycle_id, (unsigned long)((millis() - g_last_beacon_ms) / 1000));
  } else {
    Serial.print("[REPORT] free-run (no MainNode beacon yet) ");
  }
  Serial.printf("sightings=%u tx=%s heard=%lu\n",
                pkt.num_sightings,
                ok ? "OK" : "FAIL",
                (unsigned long)g_total_heard);
}

// ============================================================================
// Setup / loop
// ============================================================================
void setup() {
  // Cheap USB power + LoRa current spikes -> VCC dip -> brown-out reset loop.
  // Disable BOD so we boot reliably; revert when running off proper PSU/battery.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(200);

  pinMode(PIN_LED_STATUS, OUTPUT);
  digitalWrite(PIN_LED_STATUS, LOW);

  // Known pillars, matched on the FULL 6-byte MAC. (Matching only the first
  // 4 bytes made boards from one batch collide: they share 38:18:2b:8x.)
  struct KnownPillar { uint8_t mac[6]; uint8_t pin_index; };
  static const KnownPillar KNOWN_PILLARS[] = {
    {{0x38, 0x18, 0x2b, 0x8a, 0x90, 0x88}, 0},   // Pillar 1
    {{0x38, 0x18, 0x2b, 0x8b, 0x24, 0x48}, 1},   // Pillar 2 (replacement board)
    {{0x68, 0x25, 0xdd, 0xfd, 0x11, 0xf8}, 2},   // Pillar 3
    {{0x00, 0x4b, 0x12, 0x34, 0xda, 0xa4}, 3},   // Pillar 4
  };
  // getEfuseMac() packs the MAC little-endian: byte 0 of the MAC is the LSB.
  const uint64_t efuse = ESP.getEfuseMac();
  uint8_t mac[6];
  for (int i = 0; i < 6; ++i) mac[i] = (uint8_t)(efuse >> (8 * i));

  g_pin_index = 255;
  for (const auto& k : KNOWN_PILLARS) {
    if (memcmp(k.mac, mac, 6) == 0) {
      g_pin_index = k.pin_index;
      g_device_id = 0xAAAA0001 + k.pin_index;   // AAAA0001..AAAA0004
      break;
    }
  }
  if (g_pin_index == 255) {
    // Unknown board: ID from the MAC's unique tail, slot from NVS (assigned
    // from the dispatcher's Anchors page) or unassigned until then.
    g_device_id = (uint32_t)(efuse >> 16);
    g_prefs.begin("resq", false);
    g_pin_index = g_prefs.getUChar("pin_idx", 255);
  }
  Serial.printf("MAC=%02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  Serial.println();
  Serial.printf("== %s fw=%s ==\n", BOARD_NAME, FW_VERSION);
  // Pillar is counted 1..4 for humans; pin_index is the 0-based internal index
  // used for the TDMA slot and array positions (255 = not yet assigned).
  const unsigned pillar_no = (g_pin_index == 255) ? 0 : (unsigned)(g_pin_index + 1);
  Serial.printf("Pillar=%u  device_id=%08X  pin_index=%u  slot=%u\n",
                pillar_no, g_device_id, (unsigned)g_pin_index,
                (unsigned)(g_pin_index == 255 ? 0 : TDMA_SLOT_PIN_BASE + g_pin_index));

  g_lora_ready = connect_lora();
  g_last_lora_retry_ms = millis();
  Serial.printf("[LoRa] init=%s\n", g_lora_ready ? "OK" : "FAIL (will retry)");

  // if (g_lora_ready) LoRa.onReceive(on_lora_rx);

#if ENABLE_OTA
  // Background OTA: try to join WiFi and pull the latest firmware on first
  // boot, then re-check every OTA_CHECK_INTERVAL_MS. Pins are mast-mounted
  // and hard to reach, so this is the only way to update them remotely.
  // ensure_wifi blocks up to 8s on first boot; LoRa RX continues in ISR.
  if (strlen(WIFI_SSID) > 0) {
    Serial.printf("[WiFi] joining %s ...\n", WIFI_SSID);
    if (ResQOTA::ensure_wifi(WIFI_SSID, WIFI_PASSWORD, 8000)) {
      Serial.printf("[WiFi] %s rssi=%d ip=%s\n",
                    WIFI_SSID, WiFi.RSSI(),
                    WiFi.localIP().toString().c_str());
      // First check 5s into the run so LoRa sync has a moment first.
      g_next_ota_check_ms = millis() + 5000;
    } else {
      Serial.println("[WiFi] join failed - OTA disabled this boot");
    }
  } else {
    Serial.println("[OTA] no WIFI_SSID in secrets.h - skipping");
  }
#endif
}

void loop() {
  const uint32_t now = millis();

  // --- Poll LoRa instead of using ISR ---
  if (g_lora_ready) {
    int packet_size = LoRa.parsePacket();
    if (packet_size) {
      on_lora_rx(packet_size);
    }
  }

  // --- LoRa retry while down ----------------------------------------------
  if (!g_lora_ready && now - g_last_lora_retry_ms >= LORA_RETRY_MS) {
    g_last_lora_retry_ms = now;
    g_lora_ready = connect_lora();
    if (g_lora_ready) {
      // LoRa.onReceive(on_lora_rx);
      Serial.println("[LoRa] recovered");
    }
  }

  // --- Unassigned Join Request ---------------------------------------------
  if (g_lora_ready && g_pin_index == 255 && now >= g_next_join_req_ms) {
    // 5-10s random jitter
    g_next_join_req_ms = now + 5000 + random(5000);

    ResQ::PinJoinReqPacket req;
    ResQ::fill_pin_join_req(req, g_device_id);

    digitalWrite(PIN_LED_STATUS, HIGH);
    LoRa.idle();
    if (LoRa.beginPacket()) {
      LoRa.write(reinterpret_cast<const uint8_t*>(&req), sizeof(req));
      LoRa.endPacket();
    }
    LoRa.receive();
    digitalWrite(PIN_LED_STATUS, LOW);
    
    Serial.println("[JOIN] sent join request");
  }

  // --- Status LED ----------------------------------------------------------
  // Identify mode:   Solid ON (bright)
  // No radio:        slow 1 Hz blink
  // No beacon yet:   double-blink (looking for MainNode)
  // Beacon locked:   off most of the time, brief on at TX time (set above)
  if (g_identify_until_ms > 0 && now < g_identify_until_ms) {
    digitalWrite(PIN_LED_STATUS, HIGH); // Solid ON for unambiguous identification
  } else if (!g_lora_ready) {
    digitalWrite(PIN_LED_STATUS, ((now / 500) & 1) ? HIGH : LOW);
  } else if (g_pin_index == 255) {
    // Unassigned: fast double-blink every second
    const uint32_t phase = now % 1000;
    digitalWrite(PIN_LED_STATUS, (phase < 50 || (phase >= 150 && phase < 200)) ? HIGH : LOW);
  } else if (!g_beacon_seen) {
    const uint32_t phase = now % 1000;
    digitalWrite(PIN_LED_STATUS, (phase < 80 || (phase >= 200 && phase < 280)) ? HIGH : LOW);
  } else {
    digitalWrite(PIN_LED_STATUS, LOW);
  }

  // --- TDMA-gated report ---------------------------------------------------
  if (should_tx_report(now)) {
    tx_sighting_report();
  }

#if ENABLE_OTA
  // --- Silent background OTA pull -----------------------------------------
  if (g_next_ota_check_ms > 0 &&
      now >= g_next_ota_check_ms &&
      WiFi.status() == WL_CONNECTED) {
    g_next_ota_check_ms = now + OTA_CHECK_INTERVAL_MS;
    Serial.println("[OTA] checking for updates...");
    // run_once returns only on no-update / failure (success reboots us).
    ResQOTA::run_once(WIFI_SSID, WIFI_PASSWORD,
                      OTA_REPO_OWNER, OTA_REPO_NAME, OTA_BINARY_NAME,
                      FW_VERSION);
  }
#endif
}
