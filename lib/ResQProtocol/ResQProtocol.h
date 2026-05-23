#pragma once

#include <Arduino.h>

namespace ResQ {

constexpr uint8_t  MAGIC             = 0xA5;
constexpr uint8_t  PROTOCOL_VERSION  = 1;
constexpr uint8_t  MAX_HOP_COUNT     = 4;
constexpr uint16_t OTA_PAYLOAD_BYTES = 240;

enum PacketType : uint8_t {
  PKT_HEARTBEAT = 0x01,
  PKT_SOS       = 0x02,
  PKT_TRIAGE    = 0x03,
  PKT_RELAY     = 0x10,
  PKT_OTA_BEGIN = 0xA1,
  PKT_OTA_DATA  = 0xA2,
  PKT_OTA_END   = 0xA3,
  PKT_OTA_ACK   = 0xA4,
  PKT_OTA_ABORT = 0xA5,
};

enum TriageLevel : uint8_t {
  TRIAGE_GREEN  = 0,
  TRIAGE_YELLOW = 1,
  TRIAGE_RED    = 2,
  TRIAGE_BLACK  = 3,
};

struct __attribute__((packed)) SOSPacket {
  uint8_t  magic;
  uint8_t  packet_type;
  uint8_t  protocol_version;
  uint32_t device_id;
  uint16_t sequence;
  uint8_t  triage_level;
  uint8_t  heart_rate;
  uint8_t  spo2;
  uint8_t  battery_pct;
  int16_t  last_g_force_x10;
  uint8_t  hop_count;
  uint16_t crc16;
};
static_assert(sizeof(SOSPacket) == 18, "SOSPacket layout drifted - check packing");

struct __attribute__((packed)) RelayHeader {
  uint8_t  magic;
  uint8_t  packet_type;
  uint8_t  ttl;
  uint8_t  origin_mac[6];
  uint8_t  payload_len;
};

struct __attribute__((packed)) OTABeginPayload {
  char     version[16];
  uint32_t total_size;
  uint16_t total_chunks;
  uint8_t  md5[16];
  uint8_t  device_type;
};

struct __attribute__((packed)) OTAChunkHeader {
  uint8_t  cmd;
  uint16_t chunk_id;
  uint16_t total_chunks;
  uint16_t crc16;
};

uint16_t crc16_ccitt(const uint8_t* data, size_t len);

void fill_sos_packet(SOSPacket& pkt,
                     PacketType type,
                     uint32_t device_id,
                     uint16_t sequence,
                     TriageLevel triage,
                     uint8_t heart_rate,
                     uint8_t spo2,
                     uint8_t battery_pct,
                     int16_t g_force_x10);

bool verify_sos_packet(const SOSPacket& pkt);

const char* triage_label(TriageLevel level);

}
