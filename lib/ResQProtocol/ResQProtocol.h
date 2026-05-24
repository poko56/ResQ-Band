#pragma once

#include <Arduino.h>

// ============================================================================
// ResQ-Band v2 wire protocol
// ----------------------------------------------------------------------------
// All packets share the same header prefix and a trailing CRC16-CCITT so the
// receiver can pick a packet type by its second byte without needing length
// negotiation. Structs are __packed__ - layout drift will static_assert at
// compile time.
//
// Wire byte order: little-endian (whatever ESP32 emits natively).
// ============================================================================

namespace ResQ {

constexpr uint8_t  MAGIC             = 0xA5;
constexpr uint8_t  PROTOCOL_VERSION  = 2;
constexpr uint8_t  MAX_HOP_COUNT     = 4;

// ---- Packet type enum (single byte at offset +1) --------------------------
enum PacketType : uint8_t {
  // Routine traffic
  PKT_HEARTBEAT    = 0x01,  // Band -> Main: vitals + battery + status (SOSPacket)
  PKT_BEACON       = 0x10,  // Main -> all : TDMA sync + flags  (BeaconPacket)
  PKT_PIN_SIGHTING = 0x30,  // Pin  -> Main: heard bands list   (PinSightingPacket)
  PKT_ASSIGNMENT   = 0x40,  // Main -> Node: go fetch band X    (AssignmentPacket)
  PKT_FOUND        = 0x41,  // Node -> Main: outcome of rescue  (FoundPacket)
  PKT_RING_CMD     = 0x50,  // Node -> Band: sound buzzer       (RingCmdPacket)
  PKT_RING_ACK     = 0x51,  // Band -> Node: buzzer started     (RingAckPacket)
  // Emergency (uses SOSPacket, distinguished by type)
  PKT_SOS_TAP      = 0x20,  // Band -> Main: 3-tap SOS          (SOSPacket)
  PKT_SOS_FALL     = 0x21,  // Band -> Main: fall detected      (SOSPacket)
};

enum TriageLevel : uint8_t {
  TRIAGE_GREEN  = 0,
  TRIAGE_YELLOW = 1,
  TRIAGE_RED    = 2,
  TRIAGE_BLACK  = 3,
};

// Why the dispatcher picked this band (sent inside AssignmentPacket)
enum AssignReason : uint8_t {
  REASON_VITALS_CRITICAL = 0,
  REASON_FALL_DETECTED   = 1,
  REASON_TAP_SOS         = 2,
  REASON_SILENT_TOO_LONG = 3,
  REASON_MANUAL_OVERRIDE = 4,
  REASON_BATTERY_LOW     = 5,
};

// Outcome of a rescue attempt at the scene (sent inside FoundPacket)
enum FoundOutcome : uint8_t {
  OUTCOME_RESCUED        = 0,   // person extracted & responsive
  OUTCOME_DECEASED       = 1,   // person extracted but unresponsive (BLACK tag)
  OUTCOME_NOT_FOUND      = 2,   // arrived at location, no person there
  OUTCOME_NEEDS_BACKUP   = 3,   // need additional team / equipment
};

// ============================================================================
// SOSPacket - used for HEARTBEAT, SOS_TAP, SOS_FALL (carries vitals)
// ============================================================================
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
static_assert(sizeof(SOSPacket) == 18, "SOSPacket layout drifted");

// ============================================================================
// BeaconPacket - MainNode publishes once per TDMA cycle (slot 0)
// ============================================================================
struct __attribute__((packed)) BeaconPacket {
  uint8_t  magic;
  uint8_t  packet_type;
  uint8_t  protocol_version;
  uint32_t main_device_id;
  uint32_t cycle_id;        // monotonic across reboots? no - across uptime
  uint32_t epoch_ms;        // MainNode millis() at TX, lets others align phase
  uint8_t  flags;           // bit0: emergency_mode  bit1: pause_routine
  uint16_t crc16;
};
static_assert(sizeof(BeaconPacket) == 18, "BeaconPacket layout drifted");

// ============================================================================
// PinSightingPacket - Pin reports which bands it has heard recently
// Fixed-size to keep TDMA-slot serialization simple. Unused entries have
// band_device_id == 0.
// ============================================================================
constexpr uint8_t PIN_SIGHTING_MAX = 4;

struct __attribute__((packed)) Sighting {
  uint32_t band_device_id;
  int16_t  rssi;
  int8_t   snr;
  uint16_t last_seen_dms;   // deciseconds since heard (cap 65535 ~ 1.8h)
};
static_assert(sizeof(Sighting) == 9, "Sighting layout drifted");

struct __attribute__((packed)) PinSightingPacket {
  uint8_t  magic;
  uint8_t  packet_type;
  uint8_t  protocol_version;
  uint8_t  pin_index;
  uint32_t pin_device_id;
  uint8_t  num_sightings;
  Sighting sightings[PIN_SIGHTING_MAX];
  uint16_t crc16;
};
static_assert(sizeof(PinSightingPacket) == 47, "PinSightingPacket layout drifted");

// ============================================================================
// AssignmentPacket - MainNode dispatches a rescue target to a ResQ-Node
// ============================================================================
struct __attribute__((packed)) AssignmentPacket {
  uint8_t  magic;
  uint8_t  packet_type;
  uint8_t  protocol_version;
  uint32_t resq_node_id;       // 0 = broadcast (any node may accept)
  uint32_t target_band_id;
  uint8_t  priority_score;     // 0-255, higher = more urgent
  uint8_t  best_pin_index;     // pin with highest RSSI for this band
  int16_t  best_rssi;
  uint8_t  triage_level;       // hint for handheld display
  uint8_t  reason_code;        // AssignReason
  uint16_t crc16;
};
static_assert(sizeof(AssignmentPacket) == 19, "AssignmentPacket layout drifted");

// ============================================================================
// FoundPacket - ResQ-Node reports outcome of rescue attempt
// ============================================================================
struct __attribute__((packed)) FoundPacket {
  uint8_t  magic;
  uint8_t  packet_type;
  uint8_t  protocol_version;
  uint32_t resq_node_id;
  uint32_t target_band_id;
  uint32_t found_at_ms;
  int16_t  final_rssi;
  uint8_t  outcome;            // FoundOutcome
  uint16_t crc16;
};
static_assert(sizeof(FoundPacket) == 20, "FoundPacket layout drifted");

// ============================================================================
// RingCmdPacket - ResQ-Node tells Band to sound its buzzer
// ============================================================================
struct __attribute__((packed)) RingCmdPacket {
  uint8_t  magic;
  uint8_t  packet_type;
  uint8_t  protocol_version;
  uint32_t target_band_id;
  uint16_t duration_ms;
  uint16_t buzz_freq_hz;
  uint8_t  pattern;            // 0=solid, 1=pulse, 2=siren
  uint16_t crc16;
};
static_assert(sizeof(RingCmdPacket) == 14, "RingCmdPacket layout drifted");

// ============================================================================
// RingAckPacket - Band confirms buzzer activation
// ============================================================================
struct __attribute__((packed)) RingAckPacket {
  uint8_t  magic;
  uint8_t  packet_type;
  uint8_t  protocol_version;
  uint32_t device_id;
  uint8_t  status;             // 0=buzzing, 1=already buzzing, 2=batt too low
  uint16_t crc16;
};
static_assert(sizeof(RingAckPacket) == 10, "RingAckPacket layout drifted");

// ============================================================================
// API
// ============================================================================

uint16_t crc16_ccitt(const uint8_t* data, size_t len);

// SOSPacket (heartbeat / SOS_TAP / SOS_FALL)
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

// BeaconPacket
void fill_beacon(BeaconPacket& pkt,
                 uint32_t main_device_id,
                 uint32_t cycle_id,
                 uint32_t epoch_ms,
                 uint8_t flags);
bool verify_beacon(const BeaconPacket& pkt);

// PinSightingPacket - call init, add_sighting up to PIN_SIGHTING_MAX times,
// then finalize before TX.
void init_pin_sighting(PinSightingPacket& pkt,
                       uint8_t pin_index,
                       uint32_t pin_device_id);
bool add_pin_sighting(PinSightingPacket& pkt,
                      uint32_t band_device_id,
                      int16_t rssi,
                      int8_t snr,
                      uint16_t last_seen_dms);
void finalize_pin_sighting(PinSightingPacket& pkt);
bool verify_pin_sighting(const PinSightingPacket& pkt);

// AssignmentPacket
void fill_assignment(AssignmentPacket& pkt,
                     uint32_t resq_node_id,
                     uint32_t target_band_id,
                     uint8_t priority_score,
                     uint8_t best_pin_index,
                     int16_t best_rssi,
                     TriageLevel triage,
                     AssignReason reason);
bool verify_assignment(const AssignmentPacket& pkt);

// FoundPacket
void fill_found(FoundPacket& pkt,
                uint32_t resq_node_id,
                uint32_t target_band_id,
                uint32_t found_at_ms,
                int16_t final_rssi,
                FoundOutcome outcome);
bool verify_found(const FoundPacket& pkt);

// RingCmdPacket
void fill_ring_cmd(RingCmdPacket& pkt,
                   uint32_t target_band_id,
                   uint16_t duration_ms,
                   uint16_t buzz_freq_hz,
                   uint8_t pattern);
bool verify_ring_cmd(const RingCmdPacket& pkt);

// RingAckPacket
void fill_ring_ack(RingAckPacket& pkt,
                   uint32_t device_id,
                   uint8_t status);
bool verify_ring_ack(const RingAckPacket& pkt);

// Friendly labels (also used in serial logs)
const char* triage_label(TriageLevel level);
const char* packet_type_label(PacketType t);
const char* assign_reason_label(AssignReason r);
const char* found_outcome_label(FoundOutcome o);

// Peek the packet type from a raw LoRa buffer (returns 0xFF if invalid)
inline uint8_t peek_packet_type(const uint8_t* buf, size_t len) {
  if (len < 2 || buf[0] != MAGIC) return 0xFF;
  return buf[1];
}

}  // namespace ResQ
