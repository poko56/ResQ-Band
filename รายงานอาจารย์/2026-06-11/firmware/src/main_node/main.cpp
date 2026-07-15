// MainNode - ESP32-S3
// ฟัง LoRa packet แล้ว dump ออก Serial (ผ่าน USB CDC)
// ยังไม่มี protocol parsing - แค่อ่าน raw bytes
#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "config.h"

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  Serial.println();
  Serial.println("== MainNode v0.1 ==");

  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_CS);
  LoRa.setPins(PIN_LORA_CS, PIN_LORA_RST, PIN_LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init fail - check ขา SPI");
    while (1) delay(500);
  }
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  Serial.printf("LoRa OK @ %.1f MHz SF%d\n", LORA_FREQ / 1e6, LORA_SF);
  Serial.println("รอรับ packet...");
}

void loop() {
  int sz = LoRa.parsePacket();
  if (sz > 0) {
    Serial.printf("[RX] %d bytes  rssi=%d  snr=%.1f  data: ",
                  sz, LoRa.packetRssi(), LoRa.packetSnr());
    while (LoRa.available()) {
      char c = (char)LoRa.read();
      // ถ้าเป็น printable ก็แสดงเป็น char, ไม่งั้นเป็น hex
      if (c >= 32 && c < 127) Serial.print(c);
      else                    Serial.printf("\\x%02X", (uint8_t)c);
    }
    Serial.println();

    // กระพริบไฟเวลามี packet เข้ามา
    digitalWrite(PIN_LED, HIGH);
    delay(30);
    digitalWrite(PIN_LED, LOW);
  }
}
