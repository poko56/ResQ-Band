// ============================================================================
// ResQ-Band-Node  (ESP32-C3, worn wristband)
// ----------------------------------------------------------------------------
// MVP: protocol-correct skeleton. Real sensors (MPU6050 fall/tap, MAX30102
// HR/SpO2) come in the next round - vitals are stubbed for now so the rest
// of the stack (MainNode dispatch, web priority queue) can be exercised
// end-to-end as soon as a Band board joins the air.
//
// Responsibilities now:
//   - LoRa init with retry (non-fatal on failure, mirrors MainNode pattern)
//   - Listen for BEACON to phase-lock our TDMA slot
//   - TX HEARTBEAT (SOSPacket) once per cycle inside our own slot
//   - RX RING_CMD targeting us  ->  drive buzzer + reply RING_ACK
//   - Battery monitor via 2x100k voltage divider on PIN_VBAT_ADC
//   - Status LED hints: dark=boot, blink=TX, slow blink=no LoRa
//
// Coming next:
//   - MPU6050 tap interrupt -> emit SOS_TAP
//   - MPU6050 free-fall detect -> emit SOS_FALL with g-force
//   - MAX30102 polled HR / SpO2
//   - UWB DW3000 (last-meter)
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "ResQConfig.h"
#include "ResQProtocol.h"
#include "Sensors.h"
#include "UWB_Logic.h"

// ----------------------------------------------------------------------------
// Tunables (override per unit with build_flags if needed)
// ----------------------------------------------------------------------------
#ifndef BAND_INDEX
#define BAND_INDEX 0
#endif

// Inside our TDMA slot, wait this long after slot start before TX. Gives the
// previous slot's tail time to settle and de-correlates collisions when
// multiple boards drift slightly.
static constexpr uint32_t SLOT_TX_OFFSET_MS = 50;

// Slot 9 is the ALOHA emergency window. We sit it out for routine HB but use
// it (with random jitter) for SOS bursts so we don't have to wait a full
// cycle to scream.
static constexpr uint32_t LORA_RETRY_MS  = 5000;
static constexpr uint32_t SETUP_DELAY_MS = 200;

// Battery curve thresholds (Li-Po 3.7V, calibrated against VBAT_FULL/EMPTY)
static constexpr float ADC_REF_MV          = 3300.0f;
static constexpr float ADC_FULL_SCALE      = 4095.0f;

// ----------------------------------------------------------------------------
// State
// ----------------------------------------------------------------------------
static uint32_t g_device_id          = 0;
static uint16_t g_seq                = 0;
static uint32_t g_last_beacon_ms     = 0;
static uint32_t g_beacon_cycle_id    = 0;
static bool     g_beacon_seen        = false;
static bool     g_lora_ready         = false;
static uint32_t g_last_lora_retry_ms = 0;
static uint32_t g_last_hb_sent_cycle = 0;     // cycle id we last TX'd in
static uint32_t g_buzz_until_ms      = 0;
static uint16_t g_buzz_freq_hz       = 2000;
static uint8_t  g_buzz_pattern       = 0;

// Vitals - updated by Sensors
static uint8_t  g_hr   = 0;
static uint8_t  g_spo2 = 0;
static int16_t  g_g_x10 = 10;     // 1.0 g

// ----------------------------------------------------------------------------
// LoRa init (non-fatal; the loop retries every LORA_RETRY_MS)
// ----------------------------------------------------------------------------
static bool connect_lora() {
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_SS);
  LoRa.setPins(PIN_LORA_SS, PIN_LORA_RST, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) return false;
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setTxPower(LORA_TX_POWER_DBM);
  LoRa.enableCrc();
  LoRa.receive();   // start listening
  return true;
}

// ----------------------------------------------------------------------------
// Battery
// ----------------------------------------------------------------------------
static uint8_t read_battery_pct() {
  // Read multiple samples for stability
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 8; ++i) sum += analogRead(PIN_VBAT_ADC);
  const float raw = sum / 8.0f;
  const float mv  = (raw / ADC_FULL_SCALE) * ADC_REF_MV * VBAT_DIVIDER_RATIO;
  if (mv >= VBAT_FULL_MV)  return 100;
  if (mv <= VBAT_EMPTY_MV) return 0;
  return (uint8_t)((mv - VBAT_EMPTY_MV) * 100.0f / (VBAT_FULL_MV - VBAT_EMPTY_MV));
}

// ----------------------------------------------------------------------------
// LoRa TX helpers
// ----------------------------------------------------------------------------
template <typename Pkt>
static bool tx_packet(const Pkt& pkt) {
  if (!g_lora_ready) return false;
  LoRa.idle();
  if (!LoRa.beginPacket()) { LoRa.receive(); return false; }
  LoRa.write(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
  const bool ok = LoRa.endPacket() == 1;
  LoRa.receive();
  return ok;
}

static void tx_heartbeat() {
  ++g_seq;
  read_vitals(&g_hr, &g_spo2);
  
  ResQ::TriageLevel triage = ResQ::TRIAGE_GREEN;
  if (g_hr > VITAL_HR_CRITICAL_HIGH || (g_hr > 0 && g_hr < VITAL_HR_CRITICAL_LOW) || (g_spo2 > 0 && g_spo2 <= VITAL_SPO2_CRITICAL)) triage = ResQ::TRIAGE_RED;
  else if (g_hr > VITAL_HR_WARN_HIGH || (g_hr > 0 && g_hr < VITAL_HR_WARN_LOW) || (g_spo2 > 0 && g_spo2 <= VITAL_SPO2_WARN)) triage = ResQ::TRIAGE_YELLOW;

  ResQ::SOSPacket pkt{};
  ResQ::fill_sos_packet(pkt,
                        ResQ::PKT_HEARTBEAT,
                        g_device_id,
                        g_seq,
                        triage,
                        g_hr,
                        g_spo2,
                        read_battery_pct(),
                        g_g_x10);
  digitalWrite(PIN_LED_STATUS, HIGH);
  const bool ok = tx_packet(pkt);
  digitalWrite(PIN_LED_STATUS, LOW);

  Serial.printf("[HB] cycle=%lu seq=%u batt=%u%% tx=%s\n",
                (unsigned long)g_beacon_cycle_id,
                pkt.sequence,
                pkt.battery_pct,
                ok ? "OK" : "FAIL");
}

static void tx_ring_ack(uint8_t status) {
  ResQ::RingAckPacket pkt;
  ResQ::fill_ring_ack(pkt, g_device_id, status);
  tx_packet(pkt);
}

static void tx_emergency(uint8_t cause, float max_g) {
  ++g_seq;
  read_vitals(&g_hr, &g_spo2);
  g_g_x10 = (int16_t)(max_g * 10);
  
  ResQ::SOSPacket pkt{};
  ResQ::fill_sos_packet(pkt,
                        (cause == 1) ? ResQ::PKT_SOS_TAP : ResQ::PKT_SOS_FALL,
                        g_device_id,
                        g_seq,
                        ResQ::TRIAGE_RED, // Emergency implies RED triage
                        g_hr,
                        g_spo2,
                        read_battery_pct(),
                        g_g_x10);
  digitalWrite(PIN_LED_STATUS, HIGH);
  tx_packet(pkt);
  digitalWrite(PIN_LED_STATUS, LOW);
  Serial.printf("[SOS] cause=%u max_g=%.1f tx_seq=%u\n", cause, max_g, g_seq);
}

// ----------------------------------------------------------------------------
// LoRa RX handlers
// ----------------------------------------------------------------------------
static void handle_beacon(const ResQ::BeaconPacket& pkt) {
  g_beacon_seen     = true;
  g_beacon_cycle_id = pkt.cycle_id;
  g_last_beacon_ms  = millis();
}

static void handle_ring_cmd(const ResQ::RingCmdPacket& pkt) {
  if (pkt.target_band_id != g_device_id) return;   // not for me
  const uint8_t batt = read_battery_pct();
  if (batt < BATT_LOW_PCT) {
    tx_ring_ack(2);     // status 2 = battery too low
    return;
  }
  g_buzz_until_ms = millis() + pkt.duration_ms;
  g_buzz_freq_hz  = pkt.buzz_freq_hz;
  g_buzz_pattern  = pkt.pattern;
  tx_ring_ack(0);
  Serial.printf("[RING] %ums @ %uHz pattern=%u\n",
                pkt.duration_ms, pkt.buzz_freq_hz, pkt.pattern);
}

static void on_lora_rx(int packet_size) {
  if (packet_size < 2) {
    while (LoRa.available()) LoRa.read();
    return;
  }
  uint8_t buf[64];
  size_t n = 0;
  while (LoRa.available() && n < sizeof(buf)) buf[n++] = (uint8_t)LoRa.read();

  const uint8_t ptype = ResQ::peek_packet_type(buf, n);

  if (ptype == ResQ::PKT_BEACON && n >= sizeof(ResQ::BeaconPacket)) {
    ResQ::BeaconPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    if (ResQ::verify_beacon(pkt)) handle_beacon(pkt);
  } else if (ptype == ResQ::PKT_RING_CMD && n >= sizeof(ResQ::RingCmdPacket)) {
    ResQ::RingCmdPacket pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    if (ResQ::verify_ring_cmd(pkt)) handle_ring_cmd(pkt);
  }
}

// ----------------------------------------------------------------------------
// Buzzer driver - runs from loop(), supports 3 patterns
// ----------------------------------------------------------------------------
static void update_buzzer(uint32_t now) {
  if (now >= g_buzz_until_ms) {
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }
  bool on = false;
  switch (g_buzz_pattern) {
    case 0:  on = true; break;                            // solid
    case 1:  on = ((now / 200) & 1);          break;      // pulse 2.5 Hz
    case 2:  on = ((now / 80) & 1) != ((now / 250) & 1);  // siren-ish
             break;
    default: on = ((now / 200) & 1); break;
  }
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
}

// ----------------------------------------------------------------------------
// TDMA gate - returns true once per cycle, inside our assigned slot
// ----------------------------------------------------------------------------
static bool should_tx_heartbeat(uint32_t now) {
  if (!g_lora_ready) return false;
  if (!g_beacon_seen) {
    // Free-running fallback so we can verify TX even before MainNode is alive.
    // Once we see a beacon we'll switch to phase-locked.
    static uint32_t next_free = 0;
    if (now >= next_free) {
      next_free = now + TDMA_CYCLE_MS;
      return true;
    }
    return false;
  }

  const uint32_t since_beacon = now - g_last_beacon_ms;
  const uint32_t slot_idx     = since_beacon / TDMA_SLOT_MS;
  const uint32_t into_slot    = since_beacon % TDMA_SLOT_MS;
  const uint32_t my_slot      = TDMA_SLOT_BAND_BASE + BAND_INDEX;

  // Only TX once per cycle, inside our slot, after the TX offset
  if (slot_idx == my_slot &&
      into_slot >= SLOT_TX_OFFSET_MS &&
      g_last_hb_sent_cycle != g_beacon_cycle_id) {
    g_last_hb_sent_cycle = g_beacon_cycle_id;
    return true;
  }
  return false;
}

// ============================================================================
// Setup / loop
// ============================================================================
void setup() {
  // LoRa TX surge dips VCC under cheap USB power -> brown-out reset loop.
  // Disable BOD so we boot reliably; re-enable in production with proper PSU.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(SETUP_DELAY_MS);

  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_BUZZER,     OUTPUT);
  digitalWrite(PIN_LED_STATUS, LOW);
  digitalWrite(PIN_BUZZER,     LOW);

  analogReadResolution(12);

  g_device_id = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF);

  Serial.println();
  Serial.printf("== %s fw=%s ==\n", BOARD_NAME, FW_VERSION);
  Serial.printf("device_id=%08X  band_index=%u  slot=%u\n",
                g_device_id, (unsigned)BAND_INDEX,
                (unsigned)(TDMA_SLOT_BAND_BASE + BAND_INDEX));

  g_lora_ready = connect_lora();
  g_last_lora_retry_ms = millis();
  Serial.printf("[LoRa] init=%s\n", g_lora_ready ? "OK" : "FAIL (will retry)");

  if (g_lora_ready) LoRa.onReceive(on_lora_rx);
  
  init_sensors();
  init_uwb();
}

void loop() {
  const uint32_t now = millis();

  // --- LoRa retry while down -----------------------------------------------
  if (!g_lora_ready && now - g_last_lora_retry_ms >= LORA_RETRY_MS) {
    g_last_lora_retry_ms = now;
    g_lora_ready = connect_lora();
    if (g_lora_ready) {
      LoRa.onReceive(on_lora_rx);
      Serial.println("[LoRa] recovered");
    }
  }

  // --- Status LED while no radio: slow blink -------------------------------
  if (!g_lora_ready) {
    digitalWrite(PIN_LED_STATUS, ((now / 500) & 1) ? HIGH : LOW);
  }

  // --- TDMA-gated heartbeat ------------------------------------------------
  if (should_tx_heartbeat(now)) {
    tx_heartbeat();
  }

  // --- Buzzer drive --------------------------------------------------------
  update_buzzer(now);

  // --- Sensor Poll & Emergency Trigger -------------------------------------
  poll_sensors();
  poll_uwb();
  
  uint8_t sos_cause = 0;
  float sos_g = 0.0f;
  if (check_emergency_triggers(&sos_cause, &sos_g)) {
    tx_emergency(sos_cause, sos_g);
  }
}
