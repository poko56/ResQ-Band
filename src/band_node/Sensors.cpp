#include "Sensors.h"

Adafruit_MPU6050 mpu;
MAX30105 particleSensor;

uint8_t current_hr = 0;
uint8_t current_spo2 = 0;
bool s_fall_detected = false;
bool s_tap_detected = false;
float s_max_g_force = 0.0f;
bool s_mpu_found = false;

static const byte RATE_SIZE = 4;
static byte rates[RATE_SIZE]; 
static byte rateSpot = 0;
static long lastBeat = 0;
static float beatsPerMinute;

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
    particleSensor.setup(); // Default setup
    particleSensor.setPulseAmplitudeRed(0x0A); // Turn Red LED to low to indicate sensor is running
    particleSensor.setPulseAmplitudeGreen(0);  // Turn off Green LED
  }
}

void poll_sensors() {
  // Update Heart Rate
  particleSensor.check(); // Check the sensor for new data
  
  long irValue = 0;
  while (particleSensor.available()) {
    irValue = particleSensor.getFIFOIR(); // Read from FIFO
    particleSensor.nextSample(); // Move to next sample

    if (checkForBeat(irValue) == true) {
      long delta = millis() - lastBeat;
      lastBeat = millis();

      beatsPerMinute = 60 / (delta / 1000.0);

      if (beatsPerMinute < 255 && beatsPerMinute > 20) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;

        // Take average
        int avg_hr = 0;
        for (byte x = 0 ; x < RATE_SIZE ; x++)
          avg_hr += rates[x];
        avg_hr /= RATE_SIZE;
        current_hr = (uint8_t)avg_hr;
      }
    }
  }

  // Fake SpO2 for now if we don't have full algorithm ported, or set base value
  // Debug print every 1 second
  static uint32_t last_print = 0;
  if (millis() - last_print > 1000) {
    last_print = millis();
    Serial.printf("[Sensors] IR=%ld HR=%u SpO2=%u\n", irValue, current_hr, current_spo2);
  }

  // Fake SpO2 for now if we don't have full algorithm ported, or set base value
  if (irValue > 20000) {
    current_spo2 = 98; // Valid reading pseudo
  } else {
    current_hr = 0;
    current_spo2 = 0; // No finger
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
