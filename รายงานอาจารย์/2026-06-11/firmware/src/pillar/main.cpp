// Pillar (เสารับสัญญาณ)
// ฟัง LoRa แล้วโชว์ RSSI ออก Serial
#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "config.h"

unsigned long pktCount = 0;

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED, OUTPUT);

  SPI.begin();  // VSPI default
  LoRa.setPins(PIN_LORA_CS, PIN_LORA_RST, PIN_LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa fail");
    while (1) { digitalWrite(PIN_LED, !digitalRead(PIN_LED)); delay(200); }
  }
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(125E3);
  Serial.println("Pillar ready");
}

void loop() {
  int sz = LoRa.parsePacket();
  if (sz > 0) {
    pktCount++;
    Serial.printf("#%lu  %d bytes  rssi=%d  snr=%.1f\n",
                  pktCount, sz, LoRa.packetRssi(), LoRa.packetSnr());
    while (LoRa.available()) LoRa.read();  // ทิ้ง payload ไปก่อน
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
  }
}
