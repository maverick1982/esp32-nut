#include <unity.h>
#include <string.h>
#include "InputReassembler.h"

// Review A4: INPUT reports longer than one packet

void setUp(void) {}
void tearDown(void) {}

static InputReassembler r;
static const uint8_t* out;
static size_t out_len;

static bool feed(const uint8_t* pkt, size_t len, uint32_t ts, size_t mps, size_t expected) {
    return r.feed(pkt, len, ts, mps, expected, out, out_len);
}

void test_single_packet_report_passes_through(void) {
    r.reset();
    const uint8_t pkt[] = {8, 1, 2, 3};
    TEST_ASSERT_TRUE(feed(pkt, sizeof(pkt), 0, 8, 4));
    TEST_ASSERT_EQUAL(4, out_len);
    TEST_ASSERT_EQUAL_PTR(pkt, out);
}

void test_full_packet_of_exact_length_passes_through(void) {
    // An 8-byte report on MPS 8 is complete: it must not wait for more
    r.reset();
    const uint8_t pkt[8] = {11, 1, 2, 3, 4, 5, 6, 7};
    TEST_ASSERT_TRUE(feed(pkt, 8, 0, 8, 8));
    TEST_ASSERT_EQUAL(8, out_len);
    TEST_ASSERT_FALSE(r.pending());
}

void test_low_speed_report_split_in_packets(void) {
    // 20-byte report on MPS 8: 8 + 8 + 4
    r.reset();
    uint8_t report[20];
    for (int i = 0; i < 20; i++) report[i] = (uint8_t)(i == 0 ? 0x21 : i);
    TEST_ASSERT_FALSE(feed(report, 8, 0, 8, 20));
    TEST_ASSERT_TRUE(r.pending());
    TEST_ASSERT_FALSE(feed(report + 8, 8, 10, 8, 0));
    TEST_ASSERT_TRUE(feed(report + 16, 4, 20, 8, 0));
    TEST_ASSERT_EQUAL(20, out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(report, out, 20);
    TEST_ASSERT_FALSE(r.pending());
}

void test_report_multiple_of_mps_completes_on_length(void) {
    // APC Smart-UPS report 137: 64 bytes, on MPS 8 = 8 full packets
    r.reset();
    uint8_t report[64];
    for (int i = 0; i < 64; i++) report[i] = (uint8_t)(i == 0 ? 137 : i);
    for (int p = 0; p < 7; p++) {
        TEST_ASSERT_FALSE(feed(report + p * 8, 8, p * 10, 8, p == 0 ? 64 : 0));
    }
    TEST_ASSERT_TRUE(feed(report + 56, 8, 70, 8, 0));
    TEST_ASSERT_EQUAL(64, out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(report, out, 64);
}

void test_short_packet_ends_report_early(void) {
    r.reset();
    const uint8_t a[8] = {0x21, 1, 2, 3, 4, 5, 6, 7};
    const uint8_t b[3] = {8, 9, 10};
    TEST_ASSERT_FALSE(feed(a, 8, 0, 8, 20));
    TEST_ASSERT_TRUE(feed(b, 3, 10, 8, 0));
    TEST_ASSERT_EQUAL(11, out_len);
}

void test_stale_fragment_dropped(void) {
    r.reset();
    const uint32_t dropped = r.dropped();
    const uint8_t a[8] = {0x21, 1, 2, 3, 4, 5, 6, 7};
    const uint8_t next[4] = {8, 1, 2, 3};
    TEST_ASSERT_FALSE(feed(a, 8, 0, 8, 20));
    // The rest never came: 600 ms later a new report starts
    TEST_ASSERT_TRUE(feed(next, 4, 600, 8, 4));
    TEST_ASSERT_EQUAL(4, out_len);
    TEST_ASSERT_EQUAL_UINT8(8, out[0]);
    TEST_ASSERT_EQUAL_UINT32(dropped + 1, r.dropped());
}

void test_unknown_length_or_mps_passes_through(void) {
    r.reset();
    const uint8_t pkt[8] = {99, 1, 2, 3, 4, 5, 6, 7};
    TEST_ASSERT_TRUE(feed(pkt, 8, 0, 8, 0));  // unknown report ID
    TEST_ASSERT_TRUE(feed(pkt, 8, 0, 0, 20)); // unknown MPS
    TEST_ASSERT_FALSE(r.pending());
}

void test_extra_bytes_capped_to_expected(void) {
    r.reset();
    const uint8_t a[8] = {0x21, 1, 2, 3, 4, 5, 6, 7};
    const uint8_t b[8] = {8, 9, 0, 0, 0, 0, 0, 0}; // padded last packet
    TEST_ASSERT_FALSE(feed(a, 8, 0, 8, 10));
    TEST_ASSERT_TRUE(feed(b, 8, 10, 8, 0));
    TEST_ASSERT_EQUAL(10, out_len);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_single_packet_report_passes_through);
    RUN_TEST(test_full_packet_of_exact_length_passes_through);
    RUN_TEST(test_low_speed_report_split_in_packets);
    RUN_TEST(test_report_multiple_of_mps_completes_on_length);
    RUN_TEST(test_short_packet_ends_report_early);
    RUN_TEST(test_stale_fragment_dropped);
    RUN_TEST(test_unknown_length_or_mps_passes_through);
    RUN_TEST(test_extra_bytes_capped_to_expected);
    return UNITY_END();
}
