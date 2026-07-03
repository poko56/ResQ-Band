// Band-Node v0.1
// อ่านค่า MPU6050 (accel/gyro) + MAX30102 (HR) แล้ว print ออก Serial
// ยังไม่ส่ง LoRa - อาทิตย์หน้าค่อยทำ
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "config.h"

Adafruit_MPU6050 mpu;
MAX30105 maxSensor;

// HR moving average - ค่ามันแกว่งต้องเฉลี่ย 4 ค่าก่อน
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float bpm = 0;
int bpmAvg = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  Wire.begin(PIN_SDA, PIN_SCL);

  Serial.println();
  Serial.println("== Band-Node v0.1 ==");

  // MPU6050
  if (!mpu.begin()) {
    Serial.println("ไม่เจอ MPU6050 - check wiring");
    while (1) { digitalWrite(PIN_LED, !digitalRead(PIN_LED)); delay(200); }
  }
  Serial.println("MPU6050 OK");
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // MAX30102
  if (!maxSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("ไม่เจอ MAX30102");
    while (1) { digitalWrite(PIN_LED, !digitalRead(PIN_LED)); delay(500); }
  }
  Serial.println("MAX30102 OK");
  maxSensor.setup();
  maxSensor.setPulseAmplitudeRed(0x0A);
  maxSensor.setPulseAmplitudeGreen(0);

  Serial.println("ready");
}

void loop() {
  // อ่าน accel
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);

  // HR
  long irValue = maxSensor.getIR();
  if (checkForBeat(irValue)) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    bpm = 60.0 / (delta / 1000.0);

    if (bpm > 20 && bpm < 255) {
      rates[rateSpot++] = (byte)bpm;
      rateSpot %= RATE_SIZE;
      int sum = 0;
      for (byte i = 0; i < RATE_SIZE; i++) sum += rates[i];
      bpmAvg = sum / RATE_SIZE;
    }
  }

  // print ทุก 500ms - ไม่ต้องถี่กว่านี้
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 500) {
    lastPrint = millis();

    // fall detect แบบง่าย: รวม g แล้วถ้าน้อยกว่า 4 = free fall
    float gTotal = sqrt(a.acceleration.x * a.acceleration.x +
                        a.acceleration.y * a.acceleration.y +
                        a.acceleration.z * a.acceleration.z);

    Serial.printf("ACC %.2f %.2f %.2f (|g|=%.2f) | HR %d avg %d | IR %ld\n",
                  a.acceleration.x, a.acceleration.y, a.acceleration.z,
                  gTotal, (int)bpm, bpmAvg, irValue);

    if (gTotal < 4.0) {
      Serial.println(">> FALL detected <<");
      digitalWrite(PIN_BUZZER, HIGH);
      delay(150);
      digitalWrite(PIN_BUZZER, LOW);
    }

    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
  }
}
