// Standalone I2C bus scanner for diagnosing the band's MAX30102 / MPU6050.
// Uses the same SDA/SCL the band firmware does (GPIO 21 / 22). Prints every
// responding address once per second so you can see which sensor is (not)
// on the bus. Expected: 0x57 = MAX30102, 0x68 = MPU6050.
#include <Arduino.h>
#include <Wire.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

static const int PIN_SDA = 21;
static const int PIN_SCL = 22;

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.printf("== I2C scanner  SDA=%d SCL=%d ==\n", PIN_SDA, PIN_SCL);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(100000);   // slow & safe for scanning
}

void loop() {
  int found = 0;
  Serial.println("[SCAN] ...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      const char* who =
        (addr == 0x57) ? " <- MAX30102" :
        (addr == 0x68) ? " <- MPU6050"  :
        (addr == 0x69) ? " <- MPU6050(alt)" :
        (addr == 0x3C) ? " <- OLED"     : "";
      Serial.printf("[SCAN] FOUND 0x%02X%s\n", addr, who);
      found++;
    }
  }
  if (found == 0) {
    Serial.println("[SCAN] NOTHING on the bus -> check SDA/SCL wiring, pull-ups, power/GND");
  } else {
    Serial.printf("[SCAN] total devices: %d\n", found);
  }

  // If a MAX3010x answered, read its identity registers. The SparkFun library
  // rejects the part in begin() unless PART_ID (0xFF) == 0x15, so a clone with
  // a different id looks "not found" even though it ACKs at 0x57.
  Wire.beginTransmission(0x57);
  if (Wire.endTransmission() == 0) {
    auto rd = [](uint8_t reg) -> int {
      Wire.beginTransmission(0x57);
      Wire.write(reg);
      if (Wire.endTransmission(false) != 0) return -1;
      if (Wire.requestFrom(0x57, 1) != 1) return -1;
      return Wire.read();
    };
    int part = rd(0xFF);   // PART_ID, genuine MAX30102/05 = 0x15
    int rev  = rd(0xFE);   // REV_ID
    Serial.printf("[SCAN] 0x57 PART_ID=0x%02X REV=0x%02X  %s\n",
                  part & 0xFF, rev & 0xFF,
                  (part == 0x15) ? "(genuine - lib should work)"
                                 : "(NOT 0x15 -> clone, lib begin() rejects it)");
  }
  delay(1500);
}
