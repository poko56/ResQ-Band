#include "Sensors.h"

MAX30105 particleSensor;

uint8_t current_hr = 0;
uint8_t current_spo2 = 0;

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
static const long MIN_BEAT_INTERVAL_MS = 300;   // ceiling of 200 bpm
static const float HR_MIN_BPM = 30.0f;
static const float HR_MAX_BPM = 180.0f;

// Apply the MAX30102 sampling config (mode, sample rate, LED currents). Kept
// separate so both first-time init and bus recovery use identical settings.
static void configure_max() {
  particleSensor.setup(0x1F /*power*/, 2 /*sampleAvg*/, 2 /*ledMode RED+IR*/,
                       400 /*sampleRate*/, 411 /*pulseWidth*/, 4096 /*adcRange*/);
  particleSensor.setPulseAmplitudeRed(0x24);
  particleSensor.setPulseAmplitudeIR(0x20);   // mid drive, DC ~110k, no clipping
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
  if (particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
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
  auto readReg = [](uint8_t reg) -> int {
    Wire.beginTransmission(0x57);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom((uint8_t)0x57, (uint8_t)1) != 1) return -1;
    return Wire.read();
  };

  bool max_ok = particleSensor.begin(Wire, I2C_SPEED_STANDARD);
  int part = readReg(0xFF);
  Serial.printf("[Sensors] MAX30102 begin=%d PART_ID=0x%02X\n", max_ok, part & 0xFF);
  if (!max_ok && part == 0x15) {
    Serial.println("[Sensors] begin() gate failed but PART_ID ok -> forcing init");
    max_ok = true;   // chip verified present; proceed with setup() below
  }
  if (!max_ok) {
    Serial.println("[Sensors] MAX30102 was not found. Please check wiring/power.");
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
  // Update Heart Rate
  particleSensor.check(); // Check the sensor for new data

  // poll_sensors() runs every loop iteration but the FIFO only has fresh data
  // on some of them. Keep the last real sample in a static, otherwise a poll
  // with an empty FIFO looks like "IR = 0" and wipes the heart rate below.
  static long s_last_ir = 0;
  bool got_sample = false;
  while (particleSensor.available()) {
    long irValue = particleSensor.getFIFOIR(); // Read from FIFO
    particleSensor.nextSample(); // Move to next sample
    s_last_ir = irValue;
    got_sample = true;

    if (checkForBeat(irValue) == true) {
      long delta = millis() - lastBeat;

      // Refractory gate: a detection this soon after the previous one is the
      // dicrotic notch of the same pulse, not a new beat. Drop it WITHOUT
      // moving lastBeat, so the next real systolic peak is timed from the
      // genuine previous beat.
      if (delta < MIN_BEAT_INTERVAL_MS) continue;

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

  // SpO2 is not measured in this build (the red/IR ratio algorithm is not
  // ported), so always report 0 -> the web/handheld render it as "--".
  // HR is the only real MAX30102 output.
  current_spo2 = 0;
  if (s_last_ir <= 20000) {
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
