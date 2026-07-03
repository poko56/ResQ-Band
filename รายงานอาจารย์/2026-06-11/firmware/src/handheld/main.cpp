// Handheld (เครื่องพกพาสำหรับกู้ภัย)
// ทดสอบ OLED + ปุ่ม + LoRa init
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <LoRa.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "config.h"

Adafruit_SH1106G oled(128, 64, &Wire, -1);

void drawStatus(const char* line1, const char* line2) {
  oled.clearDisplay();
  oled.setTextSize(2);
  oled.setTextColor(SH110X_WHITE);
  oled.setCursor(0, 0);
  oled.println(line1);
  if (line2) oled.println(line2);
  oled.display();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  if (!oled.begin(0x3C, true)) {
    Serial.println("OLED fail");
    while (1) delay(500);
  }
  drawStatus("Rescue", "Handheld");

  // LoRa
  SPI.begin();
  LoRa.setPins(PIN_LORA_CS, PIN_LORA_RST, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    drawStatus("LoRa", "FAIL");
    while (1);
  }
  LoRa.setSpreadingFactor(LORA_SF);
  drawStatus("Ready", "Press");
  Serial.println("Handheld ready");
}

void loop() {
  // ปุ่มทดสอบ - กดแล้วเปลี่ยน text บน OLED
  static int presses = 0;
  if (digitalRead(PIN_BTN) == LOW) {
    presses++;
    char buf[16];
    snprintf(buf, sizeof(buf), "x%d", presses);
    drawStatus("Press", buf);
    Serial.printf("ปุ่มกด %d ครั้ง\n", presses);
    delay(250);  // debounce แบบง่าย
  }

  // รับ LoRa
  int sz = LoRa.parsePacket();
  if (sz > 0) {
    Serial.printf("RX %d bytes rssi=%d\n", sz, LoRa.packetRssi());
    while (LoRa.available()) LoRa.read();
    digitalWrite(PIN_LED, HIGH);
    delay(30);
    digitalWrite(PIN_LED, LOW);
  }
}
