#include "Sensors.h"

Adafruit_MPU6050 mpu;
MAX30105 particleSensor;

uint8_t current_hr = 0;
uint8_t current_spo2 = 0;
bool s_fall_detected = false;
bool s_tap_detected = false;
float s_max_g_force = 0.0f;
bool s_mpu_found = false;

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

void init_sensors() {
  Serial.println("[Sensors] Initializing I2C sensors...");
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  // 1. Init MPU6050
  if (!mpu.begin()) {
    Serial.println("[Sensors] Failed to find MPU6050 chip");
    s_mpu_found = false;
  } else {
    Serial.println("[Sensors] MPU6050 Found!");
    s_mpu_found = true;
    mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);
    mpu.setMotionDetectionThreshold(10); // Tap threshold
    mpu.setMotionDetectionDuration(20);
    mpu.setInterruptPinLatch(true);
    mpu.setMotionInterrupt(true);
  }

  // 2. Init MAX30102
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
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
    particleSensor.setup(0x1F /*powerLevel*/, 2 /*sampleAvg*/, 2 /*ledMode RED+IR*/,
                         400 /*sampleRate*/, 411 /*pulseWidth*/, 4096 /*adcRange*/);
    particleSensor.setPulseAmplitudeRed(0x24);
    particleSensor.setPulseAmplitudeIR(0x40);   // IR drives HR - mid drive, avoid saturation
  }
}

void poll_sensors() {
  // Update Heart Rate
  particleSensor.check(); // Check the sensor for new data
  
  // poll_sensors() runs every loop iteration but the FIFO only has fresh data
  // on some of them. Keep the last real sample in a static, otherwise a poll
  // with an empty FIFO looks like "IR = 0" and wipes the heart rate below.
  static long s_last_ir = 0;
  while (particleSensor.available()) {
    long irValue = particleSensor.getFIFOIR(); // Read from FIFO
    particleSensor.nextSample(); // Move to next sample
    s_last_ir = irValue;

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

  // Update MPU6050 only if found
  if (s_mpu_found) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    
    // Calculate total G vector (1g = 9.8 m/s^2)
    float total_g = sqrt(a.acceleration.x * a.acceleration.x + 
                         a.acceleration.y * a.acceleration.y + 
                         a.acceleration.z * a.acceleration.z) / 9.81f;
    
    if (total_g > s_max_g_force) {
      s_max_g_force = total_g;
    }
    
    // Free fall threshold (near 0 G) or High G impact
    if (total_g > (FALL_G_THRESHOLD_X10 / 10.0f)) {
      s_fall_detected = true;
    }
    
    // Tap detection via interrupt flag
    if (mpu.getMotionInterruptStatus()) {
      s_tap_detected = true;
    }
  }
}

void read_vitals(uint8_t* hr, uint8_t* spo2) {
  *hr = current_hr;
  *spo2 = current_spo2;
}

bool check_emergency_triggers(uint8_t* cause, float* max_g) {
  if (s_tap_detected) {
    *cause = 1; // Tap
    *max_g = 0;
    s_tap_detected = false;
    return true;
  }
  
  if (s_fall_detected) {
    *cause = 2; // Fall
    *max_g = s_max_g_force;
    s_fall_detected = false;
    s_max_g_force = 0;
    return true;
  }
  
  return false;
}
