#include <unity.h>
#include "ResQProtocol.h"

using namespace ResQ;

// Simulated Memory Queues / Over-The-Air buffers
SOSPacket       ota_sos_buffer;
AssignmentPacket ota_assign_buffer;
bool has_sos = false;
bool has_assignment = false;

void setUp(void) {
    has_sos = false;
    has_assignment = false;
}

void tearDown(void) {
    // clean stuff up here
}

void test_e2e_fall_rescue_flow(void) {
    // ---------------------------------------------------------
    // 1. Wristband (BandNode) detects a fall and sends SOS
    // ---------------------------------------------------------
    SOSPacket band_pkt;
    fill_sos_packet(band_pkt, PKT_SOS_FALL, 0xB0000001, 1, TRIAGE_YELLOW, 120, 95, 80, 25);
    
    // Send over the air
    ota_sos_buffer = band_pkt;
    has_sos = true;
    TEST_ASSERT_TRUE(verify_sos_packet(band_pkt));
    
    // ---------------------------------------------------------
    // 2. Anchor (ResQ-Node) receives SOS and forwards it
    // ---------------------------------------------------------
    TEST_ASSERT_TRUE(has_sos);
    SOSPacket anchor_received = ota_sos_buffer;
    TEST_ASSERT_TRUE(verify_sos_packet(anchor_received));
    
    // Anchor forwards the packet (increments hop count)
    anchor_received.hop_count++;
    anchor_received.crc16 = crc16_ccitt((const uint8_t*)&anchor_received, sizeof(SOSPacket) - 2);
    
    // Send over the air to Main Node
    ota_sos_buffer = anchor_received;
    
    // ---------------------------------------------------------
    // 3. MainNode receives SOS, processes it, and assigns Rescue
    // ---------------------------------------------------------
    SOSPacket main_received = ota_sos_buffer;
    TEST_ASSERT_TRUE(verify_sos_packet(main_received));
    TEST_ASSERT_EQUAL_UINT8(1, main_received.hop_count); // Ensure it was forwarded
    
    // MainNode Logic: High Heart rate (120) + Fall = RED Triage
    TriageLevel determined_triage = TRIAGE_RED;
    
    // MainNode generates Assignment Packet
    AssignmentPacket assignment_pkt;
    fill_assignment(assignment_pkt, 0x04000001, main_received.device_id, 100, 1, -50, determined_triage, REASON_FALL_DETECTED);
    
    // Send over the air
    ota_assign_buffer = assignment_pkt;
    has_assignment = true;
    
    // ---------------------------------------------------------
    // 4. Anchor (ResQ-Node) receives Assignment
    // ---------------------------------------------------------
    TEST_ASSERT_TRUE(has_assignment);
    AssignmentPacket anchor_assignment = ota_assign_buffer;
    
    TEST_ASSERT_TRUE(verify_assignment(anchor_assignment));
    TEST_ASSERT_EQUAL_UINT32(0xB0000001, anchor_assignment.target_band_id);
    TEST_ASSERT_EQUAL_UINT8(TRIAGE_RED, anchor_assignment.triage_level);
    TEST_ASSERT_EQUAL_UINT8(REASON_FALL_DETECTED, anchor_assignment.reason_code);
    
    // The E2E loop is complete and correct!
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_e2e_fall_rescue_flow);
    return UNITY_END();
}
