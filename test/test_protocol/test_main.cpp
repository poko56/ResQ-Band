#include <unity.h>
#include "ResQProtocol.h"

using namespace ResQ;

void setUp(void) {}
void tearDown(void) {}

void test_sos_packet(void) {
    SOSPacket pkt;
    fill_sos_packet(pkt, PKT_HEARTBEAT, 0x12345678, 1, TRIAGE_YELLOW, 85, 98, 90, 10);
    TEST_ASSERT_EQUAL_UINT8(MAGIC, pkt.magic);
    TEST_ASSERT_EQUAL_UINT8(PKT_HEARTBEAT, pkt.packet_type);
    TEST_ASSERT_TRUE(verify_sos_packet(pkt));
    pkt.heart_rate = 90;
    TEST_ASSERT_FALSE(verify_sos_packet(pkt));
}

void test_beacon_packet(void) {
    BeaconPacket pkt;
    fill_beacon(pkt, 0x87654321, 100, 1000000, 0x01);
    TEST_ASSERT_EQUAL_UINT8(MAGIC, pkt.magic);
    TEST_ASSERT_EQUAL_UINT8(PKT_BEACON, pkt.packet_type);
    TEST_ASSERT_TRUE(verify_beacon(pkt));
}

void test_assignment_packet(void) {
    AssignmentPacket pkt;
    fill_assignment(pkt, 0x111, 0x222, 100, 1, -50, TRIAGE_RED, REASON_FALL_DETECTED);
    TEST_ASSERT_EQUAL_UINT8(MAGIC, pkt.magic);
    TEST_ASSERT_EQUAL_UINT8(PKT_ASSIGNMENT, pkt.packet_type);
    TEST_ASSERT_TRUE(verify_assignment(pkt));
}

void test_pin_sighting_packet(void) {
    PinSightingPacket pkt;
    init_pin_sighting(pkt, 1, 0xAAA);
    TEST_ASSERT_TRUE(add_pin_sighting(pkt, 0xBBB, -70, 5, 10));
    finalize_pin_sighting(pkt);
    TEST_ASSERT_EQUAL_UINT8(MAGIC, pkt.magic);
    TEST_ASSERT_EQUAL_UINT8(PKT_PIN_SIGHTING, pkt.packet_type);
    TEST_ASSERT_EQUAL_UINT8(1, pkt.num_sightings);
    TEST_ASSERT_TRUE(verify_pin_sighting(pkt));
}

void test_found_packet(void) {
    FoundPacket pkt;
    fill_found(pkt, 0xCCC, 0xDDD, 5000, -40, OUTCOME_RESCUED);
    TEST_ASSERT_EQUAL_UINT8(PKT_FOUND, pkt.packet_type);
    TEST_ASSERT_TRUE(verify_found(pkt));
}

void test_ring_cmd_packet(void) {
    RingCmdPacket pkt;
    fill_ring_cmd(pkt, 0xEEE, 1000, 2000, 1);
    TEST_ASSERT_EQUAL_UINT8(PKT_RING_CMD, pkt.packet_type);
    TEST_ASSERT_TRUE(verify_ring_cmd(pkt));
}

void test_ring_ack_packet(void) {
    RingAckPacket pkt;
    fill_ring_ack(pkt, 0xFFF, 1);
    TEST_ASSERT_EQUAL_UINT8(PKT_RING_ACK, pkt.packet_type);
    TEST_ASSERT_TRUE(verify_ring_ack(pkt));
}

void test_pin_management_packets(void) {
    PinIdentifyCmdPacket id_pkt;
    fill_pin_identify_cmd(id_pkt, 0x11, 5000);
    TEST_ASSERT_TRUE(verify_pin_identify_cmd(id_pkt));

    PinButtonAckPacket btn_pkt;
    fill_pin_button_ack(btn_pkt, 0x22);
    TEST_ASSERT_TRUE(verify_pin_button_ack(btn_pkt));

    PinSetSlotCmdPacket slot_pkt;
    fill_pin_set_slot_cmd(slot_pkt, 0x33, 2);
    TEST_ASSERT_TRUE(verify_pin_set_slot_cmd(slot_pkt));

    PinJoinReqPacket join_pkt;
    fill_pin_join_req(join_pkt, 0x44);
    TEST_ASSERT_TRUE(verify_pin_join_req(join_pkt));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_sos_packet);
    RUN_TEST(test_beacon_packet);
    RUN_TEST(test_assignment_packet);
    RUN_TEST(test_pin_sighting_packet);
    RUN_TEST(test_found_packet);
    RUN_TEST(test_ring_cmd_packet);
    RUN_TEST(test_ring_ack_packet);
    RUN_TEST(test_pin_management_packets);
    return UNITY_END();
}
