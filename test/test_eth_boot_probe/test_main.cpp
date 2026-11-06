#include <unity.h>
#include <string.h>
#include "EthBootProbe.h"

// Startup probe of the W5500 link: chip, cable link and DHCP timeouts (US-126)

void setUp(void) {}
void tearDown(void) {}

void test_module_not_started_fails_at_once(void) {
    EthBootProbe p;
    p.begin(1000, false);
    TEST_ASSERT_TRUE(p.step(1000, false, false) == EthProbeOutcome::NO_CHIP);
}

void test_link_and_address_connect(void) {
    EthBootProbe p;
    p.begin(0, true);
    TEST_ASSERT_TRUE(p.step(1000, true, false) == EthProbeOutcome::WAITING);
    TEST_ASSERT_TRUE(p.step(2000, true, true) == EthProbeOutcome::CONNECTED);
}

void test_no_link_times_out_after_5_seconds(void) {
    EthBootProbe p;
    p.begin(0, true);
    TEST_ASSERT_TRUE(p.step(4900, false, false) == EthProbeOutcome::WAITING);
    TEST_ASSERT_TRUE(p.step(5100, false, false) == EthProbeOutcome::NO_LINK);
}

void test_link_without_address_times_out_15_seconds_after_the_link(void) {
    EthBootProbe p;
    p.begin(0, true);
    TEST_ASSERT_TRUE(p.step(1000, true, false) == EthProbeOutcome::WAITING);
    TEST_ASSERT_TRUE(p.step(15900, true, false) == EthProbeOutcome::WAITING);
    TEST_ASSERT_TRUE(p.step(16100, true, false) == EthProbeOutcome::NO_IP);
}

void test_link_drop_restarts_the_dhcp_wait_but_not_the_link_wait(void) {
    EthBootProbe p;
    p.begin(0, true);
    TEST_ASSERT_TRUE(p.step(1000, true, false) == EthProbeOutcome::WAITING);
    // The link drops before 5 s and comes back: the DHCP wait starts again
    TEST_ASSERT_TRUE(p.step(3000, false, false) == EthProbeOutcome::WAITING);
    TEST_ASSERT_TRUE(p.step(4000, true, false) == EthProbeOutcome::WAITING);
    TEST_ASSERT_TRUE(p.step(18000, true, false) == EthProbeOutcome::WAITING);
    TEST_ASSERT_TRUE(p.step(19100, true, false) == EthProbeOutcome::NO_IP);

    // A link that drops after the link wait has expired ends the probe
    EthBootProbe q;
    q.begin(0, true);
    TEST_ASSERT_TRUE(q.step(1000, true, false) == EthProbeOutcome::WAITING);
    TEST_ASSERT_TRUE(q.step(8000, false, false) == EthProbeOutcome::NO_LINK);
}

void test_millis_wrap_during_the_wait(void) {
    const uint32_t start = 0xFFFFFFFFu - 2000u;
    EthBootProbe p;
    p.begin(start, true);
    TEST_ASSERT_TRUE(p.step(start + 4900u, false, false) == EthProbeOutcome::WAITING);  // wrapped
    TEST_ASSERT_TRUE(p.step(start + 5100u, false, false) == EthProbeOutcome::NO_LINK);

    EthBootProbe q;
    q.begin(start, true);
    TEST_ASSERT_TRUE(q.step(start + 1000u, true, false) == EthProbeOutcome::WAITING);
    TEST_ASSERT_TRUE(q.step(start + 15900u, true, false) == EthProbeOutcome::WAITING);
    TEST_ASSERT_TRUE(q.step(start + 16100u, true, false) == EthProbeOutcome::NO_IP);
}

void test_timeouts(void) {
    TEST_ASSERT_EQUAL_UINT32(5000, (uint32_t)EthBootProbe::ETH_LINK_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_UINT32(15000, (uint32_t)EthBootProbe::ETH_DHCP_TIMEOUT_MS);
}

void test_reasons(void) {
    TEST_ASSERT_EQUAL_STRING("no_chip", ethProbeReason(EthProbeOutcome::NO_CHIP));
    TEST_ASSERT_EQUAL_STRING("no_link", ethProbeReason(EthProbeOutcome::NO_LINK));
    TEST_ASSERT_EQUAL_STRING("no_ip", ethProbeReason(EthProbeOutcome::NO_IP));
    TEST_ASSERT_EQUAL_STRING("", ethProbeReason(EthProbeOutcome::CONNECTED));
    TEST_ASSERT_EQUAL_STRING("", ethProbeReason(EthProbeOutcome::WAITING));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_module_not_started_fails_at_once);
    RUN_TEST(test_link_and_address_connect);
    RUN_TEST(test_no_link_times_out_after_5_seconds);
    RUN_TEST(test_link_without_address_times_out_15_seconds_after_the_link);
    RUN_TEST(test_link_drop_restarts_the_dhcp_wait_but_not_the_link_wait);
    RUN_TEST(test_millis_wrap_during_the_wait);
    RUN_TEST(test_timeouts);
    RUN_TEST(test_reasons);
    return UNITY_END();
}
