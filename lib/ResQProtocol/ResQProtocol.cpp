#include "ResQProtocol.h"

namespace ResQ {

// ---- CRC helper -----------------------------------------------------------
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

// Helper: compute CRC over everything except the trailing uint16_t crc16.
template <typename Pkt>
static inline uint16_t compute_packet_crc(const Pkt& pkt) {
  return crc16_ccitt(reinterpret_cast<const uint8_t*>(&pkt),
                     sizeof(Pkt) - sizeof(uint16_t));
}

template <typename Pkt>
static inline bool verify_packet_basic(const Pkt& pkt, PacketType expected_type) {
  if (pkt.magic != MAGIC) return false;
  if (pkt.protocol_version != PROTOCOL_VERSION) return false;
  if (pkt.packet_type != expected_type) return false;
  return compute_packet_crc(pkt) == pkt.crc16;
}

// ============================================================================
// SOSPacket
// ============================================================================
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
  pkt.crc16            = compute_packet_crc(pkt);
}

bool verify_sos_packet(const SOSPacket& pkt) {
  if (pkt.magic != MAGIC) return false;
  if (pkt.protocol_version != PROTOCOL_VERSION) return false;
  if (pkt.hop_count > MAX_HOP_COUNT) return false;
  // packet_type is one of HEARTBEAT / SOS_TAP / SOS_FALL - accept any of them
  if (pkt.packet_type != PKT_HEARTBEAT &&
      pkt.packet_type != PKT_SOS_TAP   &&
      pkt.packet_type != PKT_SOS_FALL) return false;
  return compute_packet_crc(pkt) == pkt.crc16;
}

// ============================================================================
// BeaconPacket
// ============================================================================
void fill_beacon(BeaconPacket& pkt,
                 uint32_t main_device_id,
                 uint32_t cycle_id,
                 uint32_t epoch_ms,
                 uint8_t flags) {
  pkt.magic            = MAGIC;
  pkt.packet_type      = PKT_BEACON;
  pkt.protocol_version = PROTOCOL_VERSION;
  pkt.main_device_id   = main_device_id;
  pkt.cycle_id         = cycle_id;
  pkt.epoch_ms         = epoch_ms;
  pkt.flags            = flags;
  pkt.crc16            = compute_packet_crc(pkt);
}
bool verify_beacon(const BeaconPacket& pkt) {
  return verify_packet_basic(pkt, PKT_BEACON);
}

// ============================================================================
// PinSightingPacket
// ============================================================================
void init_pin_sighting(PinSightingPacket& pkt,
                       uint8_t pin_index,
                       uint32_t pin_device_id) {
  pkt.magic            = MAGIC;
  pkt.packet_type      = PKT_PIN_SIGHTING;
  pkt.protocol_version = PROTOCOL_VERSION;
  pkt.pin_index        = pin_index;
  pkt.pin_device_id    = pin_device_id;
  pkt.num_sightings    = 0;
  for (uint8_t i = 0; i < PIN_SIGHTING_MAX; ++i) {
    pkt.sightings[i] = { 0, 0, 0, 0 };
  }
  pkt.crc16 = 0;
}

bool add_pin_sighting(PinSightingPacket& pkt,
                      uint32_t band_device_id,
                      int16_t rssi,
                      int8_t snr,
                      uint16_t last_seen_dms) {
  if (pkt.num_sightings >= PIN_SIGHTING_MAX) return false;
  Sighting& s = pkt.sightings[pkt.num_sightings];
  s.band_device_id = band_device_id;
  s.rssi           = rssi;
  s.snr            = snr;
  s.last_seen_dms  = last_seen_dms;
  pkt.num_sightings++;
  return true;
}

void finalize_pin_sighting(PinSightingPacket& pkt) {
  pkt.crc16 = compute_packet_crc(pkt);
}

bool verify_pin_sighting(const PinSightingPacket& pkt) {
  if (!verify_packet_basic(pkt, PKT_PIN_SIGHTING)) return false;
  if (pkt.num_sightings > PIN_SIGHTING_MAX) return false;
  return true;
}

// ============================================================================
// AssignmentPacket
// ============================================================================
void fill_assignment(AssignmentPacket& pkt,
                     uint32_t resq_node_id,
                     uint32_t target_band_id,
                     uint8_t priority_score,
                     uint8_t best_pin_index,
                     int16_t best_rssi,
                     TriageLevel triage,
                     AssignReason reason) {
  pkt.magic            = MAGIC;
  pkt.packet_type      = PKT_ASSIGNMENT;
  pkt.protocol_version = PROTOCOL_VERSION;
  pkt.resq_node_id     = resq_node_id;
  pkt.target_band_id   = target_band_id;
  pkt.priority_score   = priority_score;
  pkt.best_pin_index   = best_pin_index;
  pkt.best_rssi        = best_rssi;
  pkt.triage_level     = triage;
  pkt.reason_code      = reason;
  pkt.crc16            = compute_packet_crc(pkt);
}
bool verify_assignment(const AssignmentPacket& pkt) {
  return verify_packet_basic(pkt, PKT_ASSIGNMENT);
}

// ============================================================================
// FoundPacket
// ============================================================================
void fill_found(FoundPacket& pkt,
                uint32_t resq_node_id,
                uint32_t target_band_id,
                uint32_t found_at_ms,
                int16_t final_rssi,
                FoundOutcome outcome) {
  pkt.magic            = MAGIC;
  pkt.packet_type      = PKT_FOUND;
  pkt.protocol_version = PROTOCOL_VERSION;
  pkt.resq_node_id     = resq_node_id;
  pkt.target_band_id   = target_band_id;
  pkt.found_at_ms      = found_at_ms;
  pkt.final_rssi       = final_rssi;
  pkt.outcome          = outcome;
  pkt.crc16            = compute_packet_crc(pkt);
}
bool verify_found(const FoundPacket& pkt) {
  return verify_packet_basic(pkt, PKT_FOUND);
}

// ============================================================================
// RingCmdPacket
// ============================================================================
void fill_ring_cmd(RingCmdPacket& pkt,
                   uint32_t target_band_id,
                   uint16_t duration_ms,
                   uint16_t buzz_freq_hz,
                   uint8_t pattern) {
  pkt.magic            = MAGIC;
  pkt.packet_type      = PKT_RING_CMD;
  pkt.protocol_version = PROTOCOL_VERSION;
  pkt.target_band_id   = target_band_id;
  pkt.duration_ms      = duration_ms;
  pkt.buzz_freq_hz     = buzz_freq_hz;
  pkt.pattern          = pattern;
  pkt.crc16            = compute_packet_crc(pkt);
}
bool verify_ring_cmd(const RingCmdPacket& pkt) {
  return verify_packet_basic(pkt, PKT_RING_CMD);
}

// ============================================================================
// RingAckPacket
// ============================================================================
void fill_ring_ack(RingAckPacket& pkt,
                   uint32_t device_id,
                   uint8_t status) {
  pkt.magic            = MAGIC;
  pkt.packet_type      = PKT_RING_ACK;
  pkt.protocol_version = PROTOCOL_VERSION;
  pkt.device_id        = device_id;
  pkt.status           = status;
  pkt.crc16            = compute_packet_crc(pkt);
}
bool verify_ring_ack(const RingAckPacket& pkt) {
  return verify_packet_basic(pkt, PKT_RING_ACK);
}

// ============================================================================
// Labels
// ============================================================================
const char* triage_label(TriageLevel level) {
  switch (level) {
    case TRIAGE_GREEN:  return "GREEN";
    case TRIAGE_YELLOW: return "YELLOW";
    case TRIAGE_RED:    return "RED";
    case TRIAGE_BLACK:  return "BLACK";
    default:            return "UNKNOWN";
  }
}

const char* packet_type_label(PacketType t) {
  switch (t) {
    case PKT_HEARTBEAT:    return "HEARTBEAT";
    case PKT_BEACON:       return "BEACON";
    case PKT_PIN_SIGHTING: return "PIN_SIGHTING";
    case PKT_ASSIGNMENT:   return "ASSIGNMENT";
    case PKT_FOUND:        return "FOUND";
    case PKT_RING_CMD:     return "RING_CMD";
    case PKT_RING_ACK:     return "RING_ACK";
    case PKT_SOS_TAP:      return "SOS_TAP";
    case PKT_SOS_FALL:     return "SOS_FALL";
    case PKT_OTA_BEGIN:    return "OTA_BEGIN";
    case PKT_OTA_DATA:     return "OTA_DATA";
    case PKT_OTA_END:      return "OTA_END";
    case PKT_OTA_ACK:      return "OTA_ACK";
    case PKT_OTA_ABORT:    return "OTA_ABORT";
    default:               return "UNKNOWN";
  }
}

const char* assign_reason_label(AssignReason r) {
  switch (r) {
    case REASON_VITALS_CRITICAL: return "vitals_critical";
    case REASON_FALL_DETECTED:   return "fall_detected";
    case REASON_TAP_SOS:         return "tap_sos";
    case REASON_SILENT_TOO_LONG: return "silent_too_long";
    case REASON_MANUAL_OVERRIDE: return "manual_override";
    case REASON_BATTERY_LOW:     return "battery_low";
    default:                     return "unknown";
  }
}

const char* found_outcome_label(FoundOutcome o) {
  switch (o) {
    case OUTCOME_RESCUED:      return "rescued";
    case OUTCOME_DECEASED:     return "deceased";
    case OUTCOME_NOT_FOUND:    return "not_found";
    case OUTCOME_NEEDS_BACKUP: return "needs_backup";
    default:                   return "unknown";
  }
}

}  // namespace ResQ
