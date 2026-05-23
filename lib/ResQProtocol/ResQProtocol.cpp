#include "ResQProtocol.h"

namespace ResQ {

uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t b = 0; b < 8; ++b) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

void fill_sos_packet(SOSPacket& pkt,
                     PacketType type,
                     uint32_t device_id,
                     uint16_t sequence,
                     TriageLevel triage,
                     uint8_t heart_rate,
                     uint8_t spo2,
                     uint8_t battery_pct,
                     int16_t g_force_x10) {
  pkt.magic            = MAGIC;
  pkt.packet_type      = type;
  pkt.protocol_version = PROTOCOL_VERSION;
  pkt.device_id        = device_id;
  pkt.sequence         = sequence;
  pkt.triage_level     = triage;
  pkt.heart_rate       = heart_rate;
  pkt.spo2             = spo2;
  pkt.battery_pct      = battery_pct;
  pkt.last_g_force_x10 = g_force_x10;
  pkt.hop_count        = 0;

  const size_t crc_offset = sizeof(SOSPacket) - sizeof(uint16_t);
  pkt.crc16 = crc16_ccitt(reinterpret_cast<const uint8_t*>(&pkt), crc_offset);
}

bool verify_sos_packet(const SOSPacket& pkt) {
  if (pkt.magic != MAGIC) return false;
  if (pkt.protocol_version != PROTOCOL_VERSION) return false;
  if (pkt.hop_count > MAX_HOP_COUNT) return false;
  const size_t crc_offset = sizeof(SOSPacket) - sizeof(uint16_t);
  return crc16_ccitt(reinterpret_cast<const uint8_t*>(&pkt), crc_offset) == pkt.crc16;
}

const char* triage_label(TriageLevel level) {
  switch (level) {
    case TRIAGE_GREEN:  return "GREEN";
    case TRIAGE_YELLOW: return "YELLOW";
    case TRIAGE_RED:    return "RED";
    case TRIAGE_BLACK:  return "BLACK";
    default:            return "UNKNOWN";
  }
}

}
