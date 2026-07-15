#include <Arduino.h>
#include <LoRa.h>
#include <SPI.h>
#include "ResQConfig.h"
#include "ResQProtocol.h"

// Standard ESP32 DevKit V1 SPI pins
#define LORA_SCK_PIN  5
#define LORA_MISO_PIN 19
#define LORA_MOSI_PIN 27
#define LORA_SS_PIN   18
#define LORA_RST_PIN  14
#define LORA_DI0_PIN  26

void print_hex(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    Serial.printf("%02X ", data[i]);
    if ((i + 1) % 16 == 0) Serial.println();
  }
  if (len % 16 != 0) Serial.println();
}

void parse_resq_packet(const uint8_t* data, size_t len) {
  if (len < 1) {
    Serial.println("  [Error] Packet too short to contain type.");
    return;
  }
  uint8_t type = data[0];
  Serial.printf("  Type: %02X (", type);
  switch (type) {
    case ResQ::PKT_BEACON: Serial.print("BEACON"); break;
    case ResQ::PKT_HEARTBEAT: Serial.print("HEARTBEAT"); break;
    case ResQ::PKT_PIN_SIGHTING: Serial.print("PIN_SIGHTING"); break;
    case ResQ::PKT_SOS_TAP: Serial.print("SOS_TAP"); break;
    case ResQ::PKT_SOS_FALL: Serial.print("SOS_FALL"); break;
    case ResQ::PKT_FOUND: Serial.print("FOUND"); break;
    case ResQ::PKT_RING_CMD: Serial.print("RING_CMD"); break;
    case ResQ::PKT_RING_ACK: Serial.print("RING_ACK"); break;
    case ResQ::PKT_PIN_JOIN_REQ: Serial.print("PIN_JOIN_REQ"); break;
    case ResQ::PKT_PIN_IDENTIFY_CMD: Serial.print("PIN_IDENTIFY_CMD"); break;
    case ResQ::PKT_PIN_BUTTON_ACK: Serial.print("PIN_BUTTON_ACK"); break;
    case ResQ::PKT_PIN_SET_SLOT_CMD: Serial.print("PIN_SET_SLOT_CMD"); break;
    default: Serial.print("UNKNOWN"); break;
  }
  Serial.println(")");

  // Attempt to parse Pin Sighting as it's our main interest
  if (type == ResQ::PKT_PIN_SIGHTING) {
    if (len >= sizeof(ResQ::PinSightingPacket)) {
      ResQ::PinSightingPacket pkt;
      memcpy(&pkt, data, sizeof(pkt));
      Serial.printf("  Pin ID: %08X, Pin Index: %d, Sightings: %d\n", pkt.pin_device_id, pkt.pin_index, pkt.num_sightings);
      for (int i = 0; i < pkt.num_sightings; ++i) {
        Serial.printf("    - Band ID: %08X, RSSI: %d, SNR: %d\n", pkt.sightings[i].band_device_id, pkt.sightings[i].rssi, pkt.sightings[i].snr);
      }
      if (ResQ::verify_pin_sighting(pkt)) {
        Serial.println("  [Status] Checksum/Verification: PASS");
      } else {
        Serial.println("  [Status] Checksum/Verification: FAILED!");
      }
    } else {
      Serial.printf("  [Error] Packet length (%d) is shorter than expected PinSightingPacket struct size.\n", len);
    }
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial); // wait for serial port to connect

  Serial.println("\n\n============================================");
  Serial.println("🚀 ResQ-Band LoRa Sniffer Initializing...");
  Serial.println("============================================");

  SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_SS_PIN);
  LoRa.setPins(LORA_SS_PIN, LORA_RST_PIN, LORA_DI0_PIN);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("❌ LoRa init failed. Check wiring!");
    while (1) { delay(1000); }
  }

  // Use the exact settings from ResQConfig.h
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  LoRa.setSyncWord(LORA_SYNC_WORD);

  // Promiscuous mode: Disable CRC checking so we receive corrupted packets too!
  // LoRa.enableCrc(); // Intentionally disabled or left default to catch all

  Serial.printf("✅ LoRa Sniffer Active on %.1f MHz\n", LORA_FREQUENCY / 1e6);
  Serial.printf("   SF: %d, BW: %.1f kHz, CR: 4/%d\n", LORA_SPREADING_FACTOR, LORA_BANDWIDTH / 1000.0, LORA_CODING_RATE);
  Serial.println("⏳ Waiting for incoming packets...");
  Serial.println("============================================");
}

void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    uint32_t now = millis();
    int rssi = LoRa.packetRssi();
    float snr = LoRa.packetSnr();

    Serial.printf("\n[T+%lu] 📡 PACKET RECEIVED! (Len: %d bytes, RSSI: %d dBm, SNR: %.1f dB)\n", now, packetSize, rssi, snr);
    
    uint8_t buffer[256];
    size_t len = 0;
    while (LoRa.available() && len < sizeof(buffer)) {
      buffer[len++] = LoRa.read();
    }

    Serial.println("Raw Hex Dump:");
    print_hex(buffer, len);

    Serial.println("ResQ Protocol Parser:");
    parse_resq_packet(buffer, len);
    
    Serial.println("--------------------------------------------");
  }
}
