#include "Sensors.h"

MAX30105 particleSensor;

uint8_t current_hr = 0;
uint8_t current_spo2 = 0;

// Pulse sensor fitted on this band, told apart by PART_ID (reg 0xFF) at init.
// Both share I2C address 0x57. The SparkFun library only drives the MAX30102;
// the older MAX30100 has a different register map and is driven directly here.
enum class PulseChip : uint8_t { NONE, MAX30102, MAX30100 };
static PulseChip s_chip = PulseChip::NONE;

static bool     s_present = false;      // a pulse sensor was found
static uint32_t s_contact_since = 0;    // millis() when skin contact began, 0 = none
static uint32_t s_last_probe_ms = 0;

bool sensor_present() { return s_present; }
uint32_t contact_ms() { return s_contact_since ? millis() - s_contact_since : 0; }

static const byte RATE_SIZE = 8;   // wider window -> steadier average
static byte rates[RATE_SIZE];
static byte rateSpot = 0;
static byte rateCount = 0;   // how many of rates[] hold a real reading so far
static long lastBeat = 0;
static float beatsPerMinute;

// A PPG pulse has a main systolic peak followed ~250 ms later by the dicrotic
// notch. checkForBeat() happily fires on both, which double-counts and roughly
// doubles the reported rate (a real 90 bpm read as ~190). Ignoring any
// detection that lands too soon after the previous one rejects the notch.
// IR DC level that means skin is on the sensor. At IR drive 0x20 a fingertip
// reads ~110k; band 384E8B2B lying untouched read ~21k (light bouncing off the
// case), which the old 20k threshold took for contact and found a fake HR 47.
static const long SKIN_IR_MIN_MAX30102 = 50000;
// MAX30100 (16-bit ADC, IR 27.1 mA) on band 3CB88B2B: untouched ~4.3k,
// fingertip ~50k (below the 65535 ceiling, pulse swing ~1.3k).
static const long SKIN_IR_MIN_MAX30100 = 15000;
static long skin_ir_min() { return s_chip == PulseChip::MAX30100 ? SKIN_IR_MIN_MAX30100 : SKIN_IR_MIN_MAX30102; }
static const long MIN_BEAT_INTERVAL_MS = 300;   // ceiling of 200 bpm
static const float HR_MIN_BPM = 30.0f;
static const float HR_MAX_BPM = 180.0f;

// ---- SpO2 (ratio of ratios) --------------------------------------------------
// Over a SPO2_WINDOW_MS window, the pulsatile (AC = peak-to-peak) and steady
// (DC = mean) parts of the red and IR signals give
//   R = (AC_red / DC_red) / (AC_ir / DC_ir),  SpO2 ~= 110 - 25 * R
// the common empirical curve for reflective MAX3010x sensors. It is NOT
// calibrated against a medical oximeter: treat it as an indication (±3-5 %),
// not a clinical value. Readings outside 70-100 % (motion, poor contact) are
// dropped, and a valid HR is required.
static const uint32_t SPO2_WINDOW_MS = 4000;
static const uint32_t SPO2_MIN_SAMPLES = 200;   // ~1 s at 200 sps (MAX30102), ~2 s at 100 sps (MAX30100)
static struct {
  uint32_t start_ms = 0;
  uint32_t n = 0;
  double   sumRed = 0, sumIr = 0;
  uint32_t minRed = UINT32_MAX, maxRed = 0, minIr = UINT32_MAX, maxIr = 0;
} s_win;
static float s_spo2_ema = 0;   // smoothed estimate, 0 = none yet

static void spo2_reset_window(uint32_t now) {
  s_win.start_ms = now;
  s_win.n = 0;
  s_win.sumRed = s_win.sumIr = 0;
  s_win.minRed = s_win.minIr = UINT32_MAX;
  s_win.maxRed = s_win.maxIr = 0;
}

static void spo2_add_sample(uint32_t red, uint32_t ir) {
  s_win.n++;
  s_win.sumRed += red; s_win.sumIr += ir;
  if (red < s_win.minRed) s_win.minRed = red;
  if (red > s_win.maxRed) s_win.maxRed = red;
  if (ir  < s_win.minIr)  s_win.minIr  = ir;
  if (ir  > s_win.maxIr)  s_win.maxIr  = ir;
}

// Closes the window when it is due; returns the latest SpO2 (0 = no value).
static uint8_t spo2_update(uint32_t now, bool finger, uint8_t hr) {
  if (!finger || hr == 0) { s_spo2_ema = 0; spo2_reset_window(now); return 0; }
  if (now - s_win.start_ms < SPO2_WINDOW_MS) return (uint8_t)(s_spo2_ema + 0.5f);
  if (s_win.n >= SPO2_MIN_SAMPLES) {
    const double dcRed = s_win.sumRed / s_win.n, dcIr = s_win.sumIr / s_win.n;
    const double acRed = (double)s_win.maxRed - s_win.minRed;
    const double acIr  = (double)s_win.maxIr  - s_win.minIr;
    if (dcRed > 0 && dcIr > 0 && acIr > 0) {
      const double r = (acRed / dcRed) / (acIr / dcIr);
      const double spo2 = 110.0 - 25.0 * r;
      if (spo2 >= 70.0 && spo2 <= 100.0) {
        s_spo2_ema = s_spo2_ema == 0 ? (float)spo2 : 0.7f * s_spo2_ema + 0.3f * (float)spo2;
      }
    }
  }
  spo2_reset_window(now);
  return (uint8_t)(s_spo2_ema + 0.5f);
}

// Apply the MAX30102 sampling config (mode, sample rate, LED currents). Kept
// separate so both first-time init and bus recovery use identical settings.
static void configure_max() {
  particleSensor.setup(0x1F /*power*/, 2 /*sampleAvg*/, 2 /*ledMode RED+IR*/,
                       400 /*sampleRate*/, 411 /*pulseWidth*/, 4096 /*adcRange*/);
  particleSensor.setPulseAmplitudeRed(0x24);
  particleSensor.setPulseAmplitudeIR(0x20);   // mid drive, DC ~110k, no clipping
}

// ---- MAX30100 ------------------------------------------------------------------
static const uint8_t MAX30100_ADDR       = 0x57;
static const uint8_t M100_FIFO_WR_PTR    = 0x02;
static const uint8_t M100_OVF_COUNTER    = 0x03;
static const uint8_t M100_FIFO_RD_PTR    = 0x04;
static const uint8_t M100_FIFO_DATA      = 0x05;
static const uint8_t M100_MODE_CONFIG    = 0x06;   // bit6 reset, [2:0] mode
static const uint8_t M100_SPO2_CONFIG    = 0x07;   // bit6 hi-res, [4:2] rate, [1:0] pulse width
static const uint8_t M100_LED_CONFIG     = 0x09;   // [7:4] red current, [3:0] IR current
static const uint8_t PART_ID_MAX30100    = 0x11;
static const uint8_t PART_ID_MAX30102    = 0x15;

static int read_reg(uint8_t reg) {
  Wire.beginTransmission(MAX30100_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(MAX30100_ADDR, (uint8_t)1) != 1) return -1;
  return Wire.read();
}

static bool write_reg(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(MAX30100_ADDR);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

// SpO2 mode (red + IR), 100 samples/s, 1600 us pulses = 16-bit ADC with hi-res,
// both LEDs at 27.1 mA (0x8). Full 50 mA is the common library default but
// tends to saturate through a fingertip, the same problem the MAX30102 had.
static void configure_max30100() {
  write_reg(M100_MODE_CONFIG, 0x40);                  // reset
  for (int i = 0; i < 20 && (read_reg(M100_MODE_CONFIG) & 0x40); i++) delay(2);
  write_reg(M100_SPO2_CONFIG, 0x40 | (0x01 << 2) | 0x03);
  write_reg(M100_LED_CONFIG, (0x8 << 4) | 0x8);
  write_reg(M100_FIFO_WR_PTR, 0);
  write_reg(M100_OVF_COUNTER, 0);
  write_reg(M100_FIFO_RD_PTR, 0);
  write_reg(M100_MODE_CONFIG, 0x03);                  // SpO2 mode
}

// Drain the 16-deep FIFO. Each sample is IR then red, 16-bit big-endian.
template <typename F>
static void read_max30100_fifo(F on_sample) {
  const int wr = read_reg(M100_FIFO_WR_PTR), rd = read_reg(M100_FIFO_RD_PTR), ovf = read_reg(M100_OVF_COUNTER);
  if (wr < 0 || rd < 0) return;
  int n = (wr - rd) & 0x0F;
  if (n == 0 && ovf > 0) n = 16;                      // full and wrapped
  while (n > 0) {
    const int batch = n > 8 ? 8 : n;                  // 32 bytes per read
    Wire.beginTransmission(MAX30100_ADDR);
    Wire.write(M100_FIFO_DATA);
    if (Wire.endTransmission(false) != 0) return;
    if (Wire.requestFrom(MAX30100_ADDR, (uint8_t)(batch * 4)) != batch * 4) return;
    for (int i = 0; i < batch; i++) {
      const uint16_t ir  = (uint16_t)(Wire.read() << 8) | Wire.read();
      const uint16_t red = (uint16_t)(Wire.read() << 8) | Wire.read();
      on_sample((uint32_t)red, (long)ir);
    }
    n -= batch;
  }
}

// Tear down and rebuild the I2C bus. The ESP32 hardware I2C peripheral can latch
// into a permanent timeout state ("returned Error 263" = ESP_ERR_TIMEOUT) after
// a single glitch - e.g. the LoRa radio starting on a marginal board. Once
// stuck, every read fails forever and the FIFO write pointer freezes (IR = 0),
// even after the radio goes back to sleep. Only a full Wire.end()/begin() clears
// it. poll_sensors() calls this when no fresh sample has arrived for a while.
static void recover_i2c_bus() {
  Wire.end();
  delay(2);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);
  if (s_chip == PulseChip::MAX30100) {
    if (read_reg(0xFF) == PART_ID_MAX30100) configure_max30100();
  } else if (particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    configure_max();
  }
}

void init_sensors() {
  Serial.println("[Sensors] Initializing I2C sensors...");
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  // Run the bus at 100 kHz. At 400 kHz (I2C_SPEED_FAST) a unit with slightly
  // longer wires / weaker pull-ups fails MAX30102 init even though the chip is
  // clearly on the bus (an I2C scan finds 0x57). 100 kHz is plenty for HR.
  Wire.setClock(100000);

  // 1. Init MAX30102 FIRST, on a fresh bus.
  //    The SparkFun begin() gates on readPartID()==0x15, and on some boards that
  //    single probe fails intermittently even though the chip is genuinely there
  //    (an I2C scan reads 0x57 -> PART_ID 0x15 -> REV 0x03 fine). So we verify the
  //    part id ourselves with a direct register read and, if it's a real
  //    MAX30102/05, force initialisation via setup() regardless of what begin()
  //    returned. begin() is still called first because setup() relies on the
  //    library's stored _i2caddr.
  // MAX30100 first: SparkFun's begin() would gate it out on PART_ID anyway.
  const int part0 = read_reg(0xFF);
  if (part0 == PART_ID_MAX30100) {
    s_chip = PulseChip::MAX30100;
    s_present = true;
    s_last_probe_ms = millis();
    Serial.println("[Sensors] MAX30100 Found! (PART_ID=0x11)");
    configure_max30100();
    return;
  }

  bool max_ok = particleSensor.begin(Wire, I2C_SPEED_STANDARD);
  int part = read_reg(0xFF);
  Serial.printf("[Sensors] MAX30102 begin=%d PART_ID=0x%02X\n", max_ok, part & 0xFF);
  if (!max_ok && part == PART_ID_MAX30102) {
    Serial.println("[Sensors] begin() gate failed but PART_ID ok -> forcing init");
    max_ok = true;   // chip verified present; proceed with setup() below
  }
  s_present = max_ok;
  s_chip = max_ok ? PulseChip::MAX30102 : PulseChip::NONE;
  s_last_probe_ms = millis();
  if (!max_ok) {
    Serial.println("[Sensors] No MAX30102/MAX30100 found. Please check wiring/power.");
  } else {
    Serial.println("[Sensors] MAX30102 Found!");
    // MAX30102 has RED + IR only -> ledMode 2 (the library default is 3, which
    // is for the 3-LED MAX30105). Critically, the IR LED must be driven: it is
    // the channel checkForBeat() reads, and leaving it dark made IR read 0 so
    // no pulse was ever detected.
    // sampleAvg 2 keeps more of the pulsatile waveform than the library
    // default of 4. IR drive is tuned by hand below rather than fixed here:
    // 0x24 was too dim through a fingertip (AC swing ~0.3% of DC, too small to
    // detect), but 0x7F fully saturated the ADC (pinned at 262143, the 18-bit
    // max) which also destroys the AC signal. 0x40 is the middle ground.
    // IR drive is set inside configure_max() to 0x20 (DC ~110k): 0x40 pushed the
    // DC to ~243k, up against the 18-bit ADC ceiling, clipping the pulse and
    // making the rate read wildly wrong (138 one run, 38 the next).
    configure_max();
  }
}

void poll_sensors() {
  // No sensor: polling would only spin I2C errors and bus recoveries. Probe
  // again once a minute in case a loose connector comes back.
  if (!s_present) {
    current_hr = 0; current_spo2 = 0; s_contact_since = 0;
    if (millis() - s_last_probe_ms > 60000) init_sensors();
    return;
  }

  // poll_sensors() runs every loop iteration but the FIFO only has fresh data
  // on some of them. Keep the last real sample in a static, otherwise a poll
  // with an empty FIFO looks like "IR = 0" and wipes the heart rate below.
  static long s_last_ir = 0;
  bool got_sample = false;
  auto on_sample = [&](uint32_t redValue, long irValue) {
    spo2_add_sample(redValue, (uint32_t)irValue);
    s_last_ir = irValue;
    got_sample = true;

    if (checkForBeat(irValue) == true) {
      long delta = millis() - lastBeat;

      // Refractory gate: a detection this soon after the previous one is the
      // dicrotic notch of the same pulse, not a new beat. Drop it WITHOUT
      // moving lastBeat, so the next real systolic peak is timed from the
      // genuine previous beat.
      if (delta < MIN_BEAT_INTERVAL_MS) return;

      lastBeat = millis();

      beatsPerMinute = 60 / (delta / 1000.0);

      if (beatsPerMinute >= HR_MIN_BPM && beatsPerMinute <= HR_MAX_BPM) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;
        if (rateCount < RATE_SIZE) rateCount++;

        // Average only over slots that hold a real beat - averaging in the
        // zero-initialised remainder of rates[] on the first few beats was
        // pulling the reported HR down to a fake ~30 bpm.
        int avg_hr = 0;
        for (byte x = 0; x < rateCount; x++) avg_hr += rates[x];
        avg_hr /= rateCount;
        current_hr = (uint8_t)avg_hr;
      }
    }
  };

  if (s_chip == PulseChip::MAX30100) {
    read_max30100_fifo(on_sample);
  } else {
    particleSensor.check(); // Check the sensor for new data
    while (particleSensor.available()) {
      long irValue = particleSensor.getFIFOIR(); // Read from FIFO
      uint32_t redValue = particleSensor.getFIFORed();
      particleSensor.nextSample(); // Move to next sample
      on_sample(redValue, irValue);
    }
  }

  // Stuck-bus recovery. If the FIFO stops delivering samples for a while the
  // ESP32 I2C peripheral has almost certainly latched into its timeout state
  // (err263) - rebuild the bus to un-stick it. With the LoRa radio duty-cycled
  // (asleep most of the cycle) one recovery is usually enough to get a clean
  // run of samples before the next radio wake.
  {
    static uint32_t s_last_sample_ms = 0;
    static uint32_t s_last_recover_ms = 0;
    const uint32_t now = millis();
    if (got_sample || s_last_sample_ms == 0) s_last_sample_ms = now;
    if ((now - s_last_sample_ms) > 1500 && (now - s_last_recover_ms) > 1500) {
      recover_i2c_bus();
      s_last_recover_ms = now;
      s_last_sample_ms  = now;   // give the rebuilt bus time to refill the FIFO
    }
  }

  // SpO2 from the red/IR ratio (approximate, see spo2_update); 0 = no value,
  // which the web/handheld render as "--".
  const bool skin = s_last_ir > skin_ir_min();
  current_spo2 = spo2_update(millis(), skin, current_hr);
  if (skin) {
    if (!s_contact_since) s_contact_since = millis() | 1;
  } else {
    s_contact_since = 0;
  }
  if (!skin) {
    current_hr = 0;   // no finger on the sensor -> no valid HR
    rateCount  = 0;   // drop stale beats so the next contact starts clean
    rateSpot   = 0;
  }

  // Debug print every 1 second. Also report the peak-to-peak swing of IR over
  // the interval: that AC component IS the pulse. A swing of only a few tens
  // of counts means the optical coupling is too poor for beat detection.
  static uint32_t last_print = 0;
  static long ir_min = 0x7FFFFFFF, ir_max = 0;
  if (s_last_ir > 0) {
    if (s_last_ir < ir_min) ir_min = s_last_ir;
    if (s_last_ir > ir_max) ir_max = s_last_ir;
  }
  if (millis() - last_print > 1000) {
    last_print = millis();
    long swing = (ir_max > ir_min) ? (ir_max - ir_min) : 0;
    Serial.printf("[Sensors] IR=%ld swing=%ld HR=%u\n", s_last_ir, swing, current_hr);
    ir_min = 0x7FFFFFFF; ir_max = 0;
  }
}

void read_vitals(uint8_t* hr, uint8_t* spo2) {
  *hr = current_hr;
  *spo2 = current_spo2;
}

bool check_emergency_triggers(uint8_t* cause, float* max_g) {
  *max_g = 0.0f;
  // Vitals Critical Trigger (v2):
  // Triggers emergency only when a pulse is detected (current_hr > 0)
  // and is critically low (< 40 bpm) or critically high (> 150 bpm).
  if (current_hr > 0 && (current_hr < VITAL_HR_CRITICAL_LOW || current_hr > VITAL_HR_CRITICAL_HIGH)) {
    *cause = 1; // Critical vitals alarm
    return true;
  }
  return false;
}
