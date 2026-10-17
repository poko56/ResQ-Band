// ============================================================================
// ResQ-Band  ·  HR-only bench reader  (NO LoRa, NO WiFi)
// ----------------------------------------------------------------------------
// Purpose: prove the MAX30102 reads a real heart rate. On the full band
// firmware, bringing up the LoRa radio floods the I2C bus with timeouts
// (ESP_ERR_TIMEOUT / "Error 263") and the sensor stops sampling. This sketch
// keeps the radio completely OFF so the I2C bus stays clean, and just reads
// and prints HR. Put a fingertip on the sensor and watch the BPM.
//
// Wiring (same as Band-Node): MAX30102  SDA->GPIO21  SCL->GPIO22  VIN->3V3  GND->GND
// Build/flash:  pio run -e hr_read -t upload
// ============================================================================
#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

static const int PIN_SDA = 21;
static const int PIN_SCL = 22;

MAX30105 particleSensor;

// ---- HR detection state (mirrors the band's Sensors.cpp) -------------------
static const byte  RATE_SIZE = 8;              // averaging window
static byte  rates[RATE_SIZE];
static byte  rateSpot  = 0;
static byte  rateCount = 0;
static long  lastBeat  = 0;
static float beatsPerMinute = 0;
static uint8_t current_hr = 0;

// A PPG pulse has a systolic peak then a dicrotic notch ~250 ms later.
// checkForBeat() fires on both, doubling the rate; a refractory gate rejects
// any detection that lands too soon after the previous real beat.
static const long  MIN_BEAT_INTERVAL_MS = 300;   // ceiling ~200 bpm
static const float HR_MIN_BPM = 30.0f;
static const float HR_MAX_BPM = 180.0f;

void setup() {
  // Cheap USB power + no LoRa here, but keep the boot bullet-proof anyway.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("== ResQ-Band HR-only reader (no LoRa) ==");

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(100000);   // 100 kHz - steadier than 400 kHz on long/weak wiring

  // Verify the part id ourselves and force-init if begin()'s single probe is
  // flaky even though the chip is genuinely present (scan reads 0x15).
  auto readReg = [](uint8_t reg) -> int {
    Wire.beginTransmission(0x57);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom((uint8_t)0x57, (uint8_t)1) != 1) return -1;
    return Wire.read();
  };

  bool ok = particleSensor.begin(Wire, I2C_SPEED_STANDARD);
  int part = readReg(0xFF);
  Serial.printf("[HR] begin=%d PART_ID=0x%02X\n", ok, part & 0xFF);
  if (!ok && part == 0x15) { Serial.println("[HR] begin gate failed but chip present -> forcing"); ok = true; }
  if (!ok) {
    Serial.println("[HR] MAX30102 NOT FOUND - check SDA/SCL, 4.7k pull-ups, power/GND");
    return;
  }
  Serial.println("[HR] MAX30102 Found!");

  // RED+IR (ledMode 2). IR drives beat detection and must be driven; 0x40 is a
  // mid current that avoids saturating the 18-bit ADC through a fingertip.
  particleSensor.setup(0x1F /*power*/, 2 /*sampleAvg*/, 2 /*ledMode RED+IR*/,
                       400 /*sampleRate*/, 411 /*pulseWidth*/, 4096 /*adcRange*/);
  particleSensor.setPulseAmplitudeRed(0x24);
  particleSensor.setPulseAmplitudeIR(0x40);

  Serial.printf("[HR] cfg  MODE=0x%02X  RED_PA=0x%02X  IR_PA=0x%02X  dieTemp=%.1fC\n",
                readReg(0x09) & 0xFF, readReg(0x0C) & 0xFF, readReg(0x0D) & 0xFF,
                particleSensor.readTemperature());
  Serial.println("[HR] Put a fingertip on the sensor...");
}

void loop() {
  static long s_last_ir = 0;
  particleSensor.check();
  while (particleSensor.available()) {
    long irValue = particleSensor.getFIFOIR();
    particleSensor.nextSample();
    s_last_ir = irValue;

    if (checkForBeat(irValue)) {
      long delta = millis() - lastBeat;
      if (delta < MIN_BEAT_INTERVAL_MS) continue;   // reject dicrotic notch
      lastBeat = millis();

      beatsPerMinute = 60.0f / (delta / 1000.0f);
      if (beatsPerMinute >= HR_MIN_BPM && beatsPerMinute <= HR_MAX_BPM) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;
        if (rateCount < RATE_SIZE) rateCount++;
        int avg = 0;
        for (byte x = 0; x < rateCount; x++) avg += rates[x];
        current_hr = (uint8_t)(avg / rateCount);
      }
    }
  }

  // No finger -> IR collapses to the ambient baseline; drop stale HR.
  if (s_last_ir <= 20000) { current_hr = 0; rateCount = 0; rateSpot = 0; }

  // 1 Hz report with the peak-to-peak IR swing (that AC component IS the pulse)
  // and the FIFO write pointer (must keep advancing = sensor really sampling).
  static uint32_t last_print = 0;
  static long ir_min = 0x7FFFFFFF, ir_max = 0;
  if (s_last_ir > 0) { if (s_last_ir < ir_min) ir_min = s_last_ir; if (s_last_ir > ir_max) ir_max = s_last_ir; }
  if (millis() - last_print > 1000) {
    last_print = millis();
    long swing = (ir_max > ir_min) ? (ir_max - ir_min) : 0;
    Serial.printf("[HR] IR=%ld swing=%ld  BPM=%u  wp=%u\n",
                  s_last_ir, swing, current_hr, particleSensor.getWritePointer());
    ir_min = 0x7FFFFFFF; ir_max = 0;
  }
}
