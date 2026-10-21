// ============================================================================
// ResQ-Band-Node v2 (Worn wristband)
// ----------------------------------------------------------------------------
// - LoRa 433 MHz TDMA slotting
// - MAX30102 I2C pulse / heart rate sensor
// - Vitals Critical SOS Trigger
// - Remote RING_CMD buzzer actuation
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "ResQConfig.h"
#include "ResQProtocol.h"
#include "Sensors.h"

// Disable brownout detector
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ----------------------------------------------------------------------------
// Tunables (override per unit with build_flags if needed)
// ----------------------------------------------------------------------------
#ifndef BAND_INDEX
#define BAND_INDEX 0
#endif

// Known bands and their TDMA slot (band index 0 -> slot 1, 1 -> slot 2),
// matched on the device ID derived from the MAC. Unknown boards fall back to
// the BAND_INDEX build flag. Keeps two bands from being flashed into the
// same slot by accident.
struct KnownBand { uint32_t device_id; uint8_t index; };
static const KnownBand KNOWN_BANDS[] = {
  {0x3CB88B2B, 0},   // Band 1 (board that used to be pillar 2)
  {0x384E8B2B, 1},   // Band 2
};
static uint8_t g_band_index = BAND_INDEX;

// --- Beacon lock ------------------------------------------------------------
// The radio sleeps most of the cycle (see LORA_DUTY_CYCLE), so it can't hear
// every beacon. Instead:
//  * acquire: listen up to one full cycle for a beacon (at boot, and again
//    every ACQUIRE_RETRY_MS while no MainNode is heard);
//  * locked: transmit in our own slot, timed from the last beacon;
//  * resync: every RESYNC_EVERY cycles wake RESYNC_EARLY_MS before the
//    expected beacon to correct clock drift. RESYNC_MAX_MISSES misses in a
//    row drop back to acquire.
static constexpr uint32_t ACQUIRE_LISTEN_MS = TDMA_CYCLE_MS + 500;
static constexpr uint32_t ACQUIRE_RETRY_MS  = 90000;
static constexpr uint8_t  RESYNC_EVERY      = 6;
static constexpr uint32_t RESYNC_EARLY_MS   = 150;
static constexpr uint32_t RESYNC_LATE_MS    = 400;
static constexpr uint8_t  RESYNC_MAX_MISSES = 3;

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
static uint32_t g_last_hb_sent_cycle = 0xFFFFFFFF;   // cycle id we last TX'd in

static bool     g_locked          = false;
static uint32_t g_beacon_start_ms = 0;     // millis() when slot 0 of the last heard cycle began
static uint32_t g_next_acquire_ms = 0;
static uint32_t g_resync_at_ms    = 0;     // when to open the next resync window
static uint32_t g_resync_until_ms = 0;     // 0 = no resync window open
static uint8_t  g_resync_misses   = 0;

// --- LoRa radio duty-cycling ------------------------------------------------
// Keeping the SX1278 in continuous RX draws steady current + SPI noise that
// tips a marginal board's MAX30102 I2C bus into timeouts (err263) so HR never
// samples. Instead we keep the radio ASLEEP (rail quiet -> clean I2C -> HR
// reads), wake only to transmit the heartbeat once per cycle, then open a
// short RX window to still catch BEACON/RING_CMD before sleeping again.
// Set to 0 to fall back to always-on RX.
#define LORA_DUTY_CYCLE 1
static const uint32_t LORA_LISTEN_MS = 1200;   // RX window opened after each TX
static uint32_t g_radio_listen_until_ms = 0;   // 0 = radio asleep
static uint32_t g_post_tx_until_ms      = 0;   // end of the listen window after our last TX

// Keep the radio receiving until at least `until_ms` (extends, never shortens).
static void radio_listen_until(uint32_t until_ms) {
#if LORA_DUTY_CYCLE
  if (g_radio_listen_until_ms == 0) {
    LoRa.receive();
    g_radio_listen_until_ms = until_ms;
  } else if ((int32_t)(until_ms - g_radio_listen_until_ms) > 0) {
    g_radio_listen_until_ms = until_ms;
  }
#else
  (void)until_ms;   // always receiving
#endif
}

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
static volatile bool g_lora_rx_flag = false;
static void IRAM_ATTR lora_isr() {
  g_lora_rx_flag = true;
}

static bool connect_lora() {
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, -1);
  LoRa.setPins(PIN_LORA_SS, PIN_LORA_RST, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) return false;
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setTxPower(LORA_BAND_TX_POWER_DBM);
  LoRa.enableCrc();
  
  pinMode(PIN_LORA_DIO0, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_LORA_DIO0), lora_isr, RISING);

#if LORA_DUTY_CYCLE
  LoRa.sleep();             // stay asleep until the first heartbeat; keeps I2C clean
  g_radio_listen_until_ms = 0;
#else
  LoRa.receive();           // continuous listen
#endif
  return true;
}

// ----------------------------------------------------------------------------
// Battery
// ----------------------------------------------------------------------------
// Battery voltage needs a 2 x 100k divider from the Li-Po + to PIN_VBAT_ADC
// (GPIO 34). Boards without it report 0 = "not measured", which the
// web/handheld render as "--". Build with -D HAS_VBAT_DIVIDER=1 once fitted.
#ifndef HAS_VBAT_DIVIDER
#define HAS_VBAT_DIVIDER 0
#endif
static uint8_t read_battery_pct() {
#if HAS_VBAT_DIVIDER
  uint32_t mv = 0;
  for (int i = 0; i < 8; ++i) mv += analogReadMilliVolts(PIN_VBAT_ADC);
  const float vbat = (mv / 8.0f) * VBAT_DIVIDER_RATIO;
  const float pct = (vbat - VBAT_EMPTY_MV) * 100.0f / (VBAT_FULL_MV - VBAT_EMPTY_MV);
  return (uint8_t)constrain((int)(pct + 0.5f), 1, 100);   // 1..100 so 0 still means "unknown"
#else
  return 0;
#endif
}

// ----------------------------------------------------------------------------
// LoRa TX helpers
// ----------------------------------------------------------------------------
template <typename Pkt>
static bool tx_packet(const Pkt& pkt) {
  if (!g_lora_ready) return false;
  LoRa.idle();   // wakes the radio from sleep into standby for TX
#if LORA_DUTY_CYCLE
  if (!LoRa.beginPacket()) {
    if (g_radio_listen_until_ms) LoRa.receive(); else LoRa.sleep();
    return false;
  }
  LoRa.write(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
  const bool ok = LoRa.endPacket() == 1;
  // Open a short listen window so we still catch RING_CMD, then the loop puts
  // the radio back to sleep once it (and any acquire/resync window) expires.
  LoRa.receive();
  g_post_tx_until_ms = millis() + LORA_LISTEN_MS;
  if (g_radio_listen_until_ms == 0 || (int32_t)(g_post_tx_until_ms - g_radio_listen_until_ms) > 0) {
    g_radio_listen_until_ms = g_post_tx_until_ms;
  }
#else
  if (!LoRa.beginPacket()) { LoRa.receive(); return false; }
  LoRa.write(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
  const bool ok = LoRa.endPacket() == 1;
  LoRa.receive();
#endif
  return ok;
}

// Skin contact with no beat for this long counts as "no pulse" (BLACK). The
// beat detector needs several pulses to settle, so shorter is just warming up.
static const uint32_t NO_PULSE_CONFIRM_MS = 20000;

static void tx_heartbeat() {
  ++g_seq;
  read_vitals(&g_hr, &g_spo2);
  
  // HR 0 means BLACK (no pulse) only when the sensor has been on skin long
  // enough to find a beat. A missing sensor, a band nobody is wearing, or the
  // first seconds after contact report NO_DATA instead, so the dispatcher can
  // tell a hardware/wearing problem from a casualty with no pulse.
  ResQ::TriageLevel triage = ResQ::TRIAGE_GREEN;
  if (g_hr == 0) {
    if (!sensor_present()) { triage = ResQ::TRIAGE_NO_DATA; g_spo2 = ResQ::SPO2_NO_SENSOR; }
    else if (contact_ms() < NO_PULSE_CONFIRM_MS) triage = ResQ::TRIAGE_NO_DATA;
    else triage = ResQ::TRIAGE_BLACK;
  }
  else if (g_hr > VITAL_HR_CRITICAL_HIGH || (g_hr > 0 && g_hr < VITAL_HR_CRITICAL_LOW) || (g_spo2 > 0 && g_spo2 <= VITAL_SPO2_CRITICAL)) triage = ResQ::TRIAGE_RED;
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

static void tx_emergency(uint8_t cause, float /*max_g*/) {
  ++g_seq;
  read_vitals(&g_hr, &g_spo2);
  g_g_x10 = 0; // G-force cleared in v2 (no MPU6050)
  
  ResQ::SOSPacket pkt{};
  ResQ::fill_sos_packet(pkt,
                        ResQ::PKT_SOS_VITALS,
                        g_device_id,
                        g_seq,
                        ResQ::TRIAGE_RED, // Emergency implies RED triage
                        g_hr,
                        g_spo2,
                        read_battery_pct(),
                        0);
  digitalWrite(PIN_LED_STATUS, HIGH);
  tx_packet(pkt);
  digitalWrite(PIN_LED_STATUS, LOW);
  Serial.printf("[SOS] cause=%u (Vitals Critical) tx_seq=%u\n", cause, g_seq);

  // Sound locally too, so rescuers nearby can hear where the wearer is.
  g_buzz_until_ms = millis() + 3000;
  g_buzz_freq_hz  = 2500;
  g_buzz_pattern  = 2;
}

// ----------------------------------------------------------------------------
// LoRa RX handlers
// ----------------------------------------------------------------------------
static void handle_beacon(const ResQ::BeaconPacket& pkt) {
  const uint32_t now = millis();
  g_beacon_seen     = true;
  g_beacon_cycle_id = pkt.cycle_id;
  g_last_beacon_ms  = now;
  g_beacon_start_ms = now - TDMA_BEACON_AIRTIME_MS;
  if (!g_locked) {
    Serial.printf("[TDMA] locked to MainNode beacon (cycle %lu) -> slot %u\n",
                  (unsigned long)pkt.cycle_id, (unsigned)(TDMA_SLOT_BAND_BASE + g_band_index));
  }
  g_locked          = true;
  g_resync_misses   = 0;
  g_resync_until_ms = 0;
  g_resync_at_ms    = g_beacon_start_ms + RESYNC_EVERY * TDMA_CYCLE_MS - RESYNC_EARLY_MS;
#if LORA_DUTY_CYCLE
  // Got what we listened for: sleep again, except for a post-TX window that
  // may still be waiting for a RING_CMD.
  const uint32_t keep = (int32_t)(g_post_tx_until_ms - now) > 0 ? g_post_tx_until_ms : now;
  if (g_radio_listen_until_ms != 0) g_radio_listen_until_ms = keep;
#endif
}

// Acquire / resync bookkeeping, run every loop.
static void update_sync(uint32_t now) {
  if (!g_lora_ready) return;
  if (!g_locked) {
    if ((int32_t)(now - g_next_acquire_ms) >= 0) {
      g_next_acquire_ms = now + ACQUIRE_RETRY_MS;
      Serial.println("[TDMA] listening for MainNode beacon...");
      radio_listen_until(now + ACQUIRE_LISTEN_MS);
    }
    return;
  }
  if (g_resync_until_ms == 0) {
    if ((int32_t)(now - g_resync_at_ms) >= 0) {
      g_resync_until_ms = g_resync_at_ms + RESYNC_EARLY_MS + TDMA_BEACON_AIRTIME_MS + RESYNC_LATE_MS;
      radio_listen_until(g_resync_until_ms);
    }
  } else if ((int32_t)(now - g_resync_until_ms) > 0) {
    // Window closed with no beacon (handle_beacon would have cleared it).
    g_resync_until_ms = 0;
    if (++g_resync_misses >= RESYNC_MAX_MISSES) {
      g_locked = false;
      g_next_acquire_ms = now;
      Serial.println("[TDMA] lost MainNode beacon - re-acquiring");
    } else {
      g_resync_at_ms += TDMA_CYCLE_MS;   // try the next cycle's beacon
    }
  }
}

static void handle_ring_cmd(const ResQ::RingCmdPacket& pkt) {
  if (pkt.target_band_id != g_device_id) return;   // not for me
  // No battery gate (no battery-monitor hardware) - always ring.
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
// An ACTIVE buzzer (has its own oscillator) beeps on a plain HIGH. A PASSIVE
// one stays silent on DC and needs a square wave - build with
// -D BUZZER_PASSIVE=1 if the boot beep is silent.
#ifndef BUZZER_PASSIVE
#define BUZZER_PASSIVE 0
#endif
static constexpr uint8_t BUZZER_LEDC_CH = 0;

static void buzzer_set(bool on) {
  static int8_t s_state = -1;
  if (s_state == (on ? 1 : 0)) return;
  s_state = on ? 1 : 0;
#if BUZZER_PASSIVE
  ledcWriteTone(BUZZER_LEDC_CH, on ? g_buzz_freq_hz : 0);
#else
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
#endif
}

static void buzzer_begin() {
#if BUZZER_PASSIVE
  ledcSetup(BUZZER_LEDC_CH, 2000, 8);
  ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CH);
#else
  pinMode(PIN_BUZZER, OUTPUT);
#endif
  buzzer_set(false);
}

static void update_buzzer(uint32_t now) {
  if (now >= g_buzz_until_ms) {
    buzzer_set(false);
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
  buzzer_set(on);
}

// ----------------------------------------------------------------------------
// TDMA gate - returns true once per cycle, inside our assigned slot
// ----------------------------------------------------------------------------
static bool should_tx_heartbeat(uint32_t now) {
  if (!g_lora_ready) return false;
  if (!g_locked) {
    // No MainNode heard (yet): free-run one heartbeat per cycle so the band is
    // still visible to pillars and handhelds.
    static uint32_t next_free = 0;
    if ((int32_t)(now - next_free) >= 0) { next_free = now + TDMA_CYCLE_MS; return true; }
    return false;
  }
  // Locked: once per cycle, early in our own slot.
  const uint32_t since    = now - g_beacon_start_ms;
  const uint32_t cycle    = g_beacon_cycle_id + since / TDMA_CYCLE_MS;
  const uint32_t in_cycle = since % TDMA_CYCLE_MS;
  const uint32_t slot_at  = (TDMA_SLOT_BAND_BASE + g_band_index) * TDMA_SLOT_MS;
  if (in_cycle >= slot_at + TDMA_SLOT_TX_OFFSET_MS &&
      in_cycle <  slot_at + TDMA_SLOT_TX_WINDOW_MS &&
      cycle != g_last_hb_sent_cycle) {
    g_last_hb_sent_cycle = cycle;
    return true;
  }
  return false;
}

// ============================================================================
// Setup / loop
// ============================================================================
void setup() {
  // NOTE: brownout detector left ENABLED. Clobbering RTC_CNTL_BROWN_OUT_REG
  // was hanging the BT controller init (NimBLEDevice::init). With LoRa + sensors
  // now on a separate AMS1117 rail, the ESP32's own 3V3 is stable enough to
  // keep BOD on. If it brownout-resets, power the ESP32 from a stronger 5V.

  Serial.begin(115200);
  delay(SETUP_DELAY_MS);

  // Lower the CPU clock to cut the ESP32's own current draw, so the shared 3V3
  // rail sags less while the LoRa radio is active. On a marginal board this
  // keeps the I2C bus to the MAX30102 from timing out (err263) without needing
  // external pull-ups. 80 MHz is plenty for LoRa + TDMA + one I2C sensor.
  setCpuFrequencyMhz(80);

  pinMode(PIN_LED_STATUS, OUTPUT);
  digitalWrite(PIN_LED_STATUS, LOW);
  buzzer_begin();

  // Short boot beep: proves the buzzer and its wiring work before deployment.
  buzzer_set(true);
  delay(150);
  buzzer_set(false);

  analogReadResolution(12);

  // The first 3-4 bytes of the MAC address are the manufacturer OUI, which are identical
  // across chips from the same batch. We must shift by 16 bits to include the last 2 bytes 
  // (which actually differ between chips) into the 32-bit device ID.
  g_device_id = (uint32_t)(ESP.getEfuseMac() >> 16);
  bool known = false;
  for (const auto& k : KNOWN_BANDS) {
    if (k.device_id == g_device_id) { g_band_index = k.index; known = true; break; }
  }
  if (g_band_index >= TDMA_MAX_BANDS) g_band_index = 0;

  Serial.println();
  Serial.printf("== %s fw=%s ==\n", BOARD_NAME, FW_VERSION);
  Serial.printf("device_id=%08X  band_index=%u  slot=%u  (%s)\n",
                g_device_id, (unsigned)g_band_index,
                (unsigned)(TDMA_SLOT_BAND_BASE + g_band_index),
                known ? "from device table" : "BAND_INDEX build flag");

  // --- Sensors FIRST -------------------------------------------------------
  // MAX30102 (HR), powered from the AMS1117 rail.
  // The MAX30102 MUST be initialised while the LoRa radio is still OFF. Once
  // SX1278 is put into continuous RX its steady current draw sags the shared
  // rail just enough that the MAX browns out and stops ACKing on I2C
  // (init then reads PART_ID=0xFF and begin() fails), even though a standalone
  // I2C scan with the radio off finds it perfectly (0x57 / PART_ID 0x15). So
  // bring the sensor up first, in the same clean-rail condition as the scanner,
  // then start the radio.
  init_sensors();

  // --- LoRa ----------------------------------------------------------------
  delay(200); // let the rail settle after boot
  g_lora_ready = connect_lora();
  g_last_lora_retry_ms = millis();
  Serial.printf("[LoRa] init=%s\n", g_lora_ready ? "OK" : "FAIL (will retry)");

  Serial.println("[Setup] complete, entering loop");
}

void loop() {
  const uint32_t now = millis();

  // --- Poll LoRa instead of using ISR ---
  if (g_lora_ready && g_lora_rx_flag) {
    g_lora_rx_flag = false;
    int packet_size = LoRa.parsePacket();
    if (packet_size) {
      on_lora_rx(packet_size);
    }
  }

#if LORA_DUTY_CYCLE
  // Close the post-TX listen window: put the radio back to sleep so the I2C bus
  // to the MAX30102 is quiet again and HR can sample cleanly.
  if (g_lora_ready && g_radio_listen_until_ms != 0 && now >= g_radio_listen_until_ms) {
    LoRa.sleep();
    g_radio_listen_until_ms = 0;
  }
#endif

  // --- LoRa retry while down -----------------------------------------------
  if (!g_lora_ready && now - g_last_lora_retry_ms >= LORA_RETRY_MS) {
    g_last_lora_retry_ms = now;
    g_lora_ready = connect_lora();
    if (g_lora_ready) {
      Serial.println("[LoRa] recovered");
    }
  }

  // --- Status LED while no radio: slow blink -------------------------------
  if (!g_lora_ready) {
    digitalWrite(PIN_LED_STATUS, ((now / 500) & 1) ? HIGH : LOW);
  }

  // --- Beacon acquire / resync, then TDMA-gated heartbeat -----------------
  update_sync(now);
  if (should_tx_heartbeat(now)) {
    tx_heartbeat();
  }

  // --- Buzzer drive --------------------------------------------------------
  update_buzzer(now);

  // --- Sensor Poll & Emergency Trigger -------------------------------------
  poll_sensors();

  uint8_t sos_cause = 0;
  float sos_g = 0.0f;
  if (check_emergency_triggers(&sos_cause, &sos_g)) {
    static uint32_t last_emergency_tx = 0;
    if (now - last_emergency_tx > 5000) { // 5 seconds cooldown
      last_emergency_tx = now;
      tx_emergency(sos_cause, sos_g);
    } else {
      // Suppress spamming
      Serial.println("[SOS] Suppressed (cooldown active)");
    }
  }
}
