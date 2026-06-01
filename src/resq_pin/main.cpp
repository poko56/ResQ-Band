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
#include "ResQConfig.h"
#include "ResQProtocol.h"

// ----------------------------------------------------------------------------
// Tunables
// ----------------------------------------------------------------------------
#ifndef PIN_INDEX
#define PIN_INDEX 0
#endif

// Drop sightings older than this from the table before TX so the report
// doesn't claim we just heard a band that's actually long gone.
static constexpr uint32_t SIGHTING_STALE_MS = TDMA_CYCLE_MS * 3;   // 30 s
static constexpr uint32_t LORA_RETRY_MS     = 5000;
static constexpr uint32_t SLOT_TX_OFFSET_MS = 50;

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
static uint32_t      g_last_tx_cycle_id   = 0;
static uint32_t      g_total_heard        = 0;
static LocalSighting g_table[ResQ::PIN_SIGHTING_MAX] = {};

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
  LoRa.setTxPower(LORA_TX_POWER_DBM);
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
    }
    return;
  }

  // Anything Band-shaped contributes to our sighting table
  if ((ptype == ResQ::PKT_HEARTBEAT ||
       ptype == ResQ::PKT_SOS_TAP   ||
       ptype == ResQ::PKT_SOS_FALL) &&
      n >= sizeof(ResQ::SOSPacket)) {
    ResQ::SOSPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    if (!ResQ::verify_sos_packet(pkt)) return;
    record_sighting(pkt.device_id, (int16_t)rssi, snr);
    ++g_total_heard;
  }
}

// ----------------------------------------------------------------------------
// TDMA slot gate - returns true once per cycle inside our assigned slot
// ----------------------------------------------------------------------------
static bool should_tx_report(uint32_t now) {
  if (!g_lora_ready) return false;

  // Solo fallback: if we never see a beacon, still cough up a report each
  // cycle so a single-pin lab setup can verify the data path end-to-end.
  if (!g_beacon_seen) {
    static uint32_t next_free = 0;
    if (now >= next_free) { next_free = now + TDMA_CYCLE_MS; return true; }
    return false;
  }

  const uint32_t since_beacon = now - g_last_beacon_ms;
  const uint32_t slot_idx     = since_beacon / TDMA_SLOT_MS;
  const uint32_t into_slot    = since_beacon % TDMA_SLOT_MS;
  const uint32_t my_slot      = TDMA_SLOT_PIN_BASE + PIN_INDEX;

  if (slot_idx == my_slot &&
      into_slot >= SLOT_TX_OFFSET_MS &&
      g_last_tx_cycle_id != g_beacon_cycle_id) {
    g_last_tx_cycle_id = g_beacon_cycle_id;
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
  ResQ::init_pin_sighting(pkt, (uint8_t)PIN_INDEX, g_device_id);

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

  Serial.printf("[REPORT] cycle=%lu sightings=%u tx=%s heard=%lu\n",
                (unsigned long)g_beacon_cycle_id,
                pkt.num_sightings,
                ok ? "OK" : "FAIL",
                (unsigned long)g_total_heard);
}

// ============================================================================
// Setup / loop
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_LED_STATUS, OUTPUT);
  digitalWrite(PIN_LED_STATUS, LOW);

  g_device_id = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF);

  Serial.println();
  Serial.printf("== %s fw=%s ==\n", BOARD_NAME, FW_VERSION);
  Serial.printf("device_id=%08X  pin_index=%u  slot=%u\n",
                g_device_id, (unsigned)PIN_INDEX,
                (unsigned)(TDMA_SLOT_PIN_BASE + PIN_INDEX));

  g_lora_ready = connect_lora();
  g_last_lora_retry_ms = millis();
  Serial.printf("[LoRa] init=%s\n", g_lora_ready ? "OK" : "FAIL (will retry)");

  if (g_lora_ready) LoRa.onReceive(on_lora_rx);
}

void loop() {
  const uint32_t now = millis();

  // --- LoRa retry while down ----------------------------------------------
  if (!g_lora_ready && now - g_last_lora_retry_ms >= LORA_RETRY_MS) {
    g_last_lora_retry_ms = now;
    g_lora_ready = connect_lora();
    if (g_lora_ready) {
      LoRa.onReceive(on_lora_rx);
      Serial.println("[LoRa] recovered");
    }
  }

  // --- Status LED ----------------------------------------------------------
  // No radio:        slow 1 Hz blink
  // No beacon yet:   double-blink (looking for MainNode)
  // Beacon locked:   off most of the time, brief on at TX time (set above)
  if (!g_lora_ready) {
    digitalWrite(PIN_LED_STATUS, ((now / 500) & 1) ? HIGH : LOW);
  } else if (!g_beacon_seen) {
    const uint32_t phase = now % 1000;
    digitalWrite(PIN_LED_STATUS, (phase < 80 || (phase >= 200 && phase < 280)) ? HIGH : LOW);
  }

  // --- TDMA-gated report ---------------------------------------------------
  if (should_tx_report(now)) {
    tx_sighting_report();
  }
}
