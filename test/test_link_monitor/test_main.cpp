#include <unity.h>
#include "LinkMonitor.h"

// Issue #47 / ADR 0008: recovery state machine of the USB control pipe

static LinkMonitor::Config testConfig() {
    // backoff 2s..30s, stale after 20s, link timeout 60s, 2 recoveries
    return LinkMonitor::defaultConfig();
}

void setUp(void) {}
void tearDown(void) {}

void test_healthy_link_never_acts(void) {
    LinkMonitor m(testConfig());
    m.reset(1000);
    for (uint32_t t = 1000; t < 600000; t += 3000) {
        m.onAlive(t);
        TEST_ASSERT_TRUE(m.canPoll(t));
        TEST_ASSERT_FALSE(m.isStale(t));
        TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == m.tick(t));
    }
}

void test_quiet_link_without_failures_never_acts(void) {
    // Devices with QUIRK_NO_GET_REPORT never issue control requests
    LinkMonitor m(testConfig());
    m.reset(0);
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == m.tick(3600000));
    TEST_ASSERT_FALSE(m.isStale(3600000));
}

void test_backoff_grows_and_caps(void) {
    LinkMonitor m(testConfig());
    m.reset(0);

    m.onLinkFailure(10000);
    TEST_ASSERT_FALSE(m.canPoll(10000));
    TEST_ASSERT_FALSE(m.canPoll(11999));
    TEST_ASSERT_TRUE(m.canPoll(12000));   // 2s

    m.onLinkFailure(20000);
    TEST_ASSERT_FALSE(m.canPoll(23999));
    TEST_ASSERT_TRUE(m.canPoll(24000));   // 4s

    m.onLinkFailure(30000);
    TEST_ASSERT_TRUE(m.canPoll(38000));   // 8s

    for (int i = 0; i < 10; i++) m.onLinkFailure(40000);
    TEST_ASSERT_FALSE(m.canPoll(69999));
    TEST_ASSERT_TRUE(m.canPoll(70000));   // capped at 30s
}

void test_alive_clears_backoff_and_failures(void) {
    LinkMonitor m(testConfig());
    m.reset(0);
    m.onLinkFailure(1000);
    m.onLinkFailure(2000);
    TEST_ASSERT_EQUAL_UINT8(2, m.failures());

    m.onAlive(2500);
    TEST_ASSERT_EQUAL_UINT8(0, m.failures());
    TEST_ASSERT_TRUE(m.canPoll(2500));
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == m.tick(200000));
}

void test_stale_after_failures(void) {
    LinkMonitor m(testConfig());
    m.reset(0);
    m.onAlive(1000);
    m.onLinkFailure(6000);
    TEST_ASSERT_FALSE(m.isStale(20999));
    TEST_ASSERT_TRUE(m.isStale(21000));   // 20s since the last answer

    m.onAlive(22000);
    TEST_ASSERT_FALSE(m.isStale(22000));
}

void test_escalation_recover_then_restart(void) {
    LinkMonitor m(testConfig());
    m.reset(0);
    m.onAlive(1000);
    m.onLinkFailure(6000);

    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == m.tick(60999));
    TEST_ASSERT_TRUE(LinkMonitor::Action::RECOVER == m.tick(61000));
    TEST_ASSERT_EQUAL_UINT8(1, m.recoveries());
    TEST_ASSERT_TRUE(m.canPoll(61000));   // recovery lifts the backoff
    TEST_ASSERT_TRUE(m.isStale(61000));   // but data stays stale

    m.onLinkFailure(62000);
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == m.tick(120999));
    TEST_ASSERT_TRUE(LinkMonitor::Action::RECOVER == m.tick(121000));
    TEST_ASSERT_EQUAL_UINT8(2, m.recoveries());

    m.onLinkFailure(122000);
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == m.tick(180999));
    TEST_ASSERT_TRUE(LinkMonitor::Action::RESTART == m.tick(181000));
}

void test_answer_after_recovery_resets_escalation(void) {
    LinkMonitor m(testConfig());
    m.reset(0);
    m.onLinkFailure(1000);
    TEST_ASSERT_TRUE(LinkMonitor::Action::RECOVER == m.tick(60000));

    m.onAlive(61000);
    TEST_ASSERT_EQUAL_UINT8(0, m.recoveries());

    // A later, unrelated outage starts again from the first recovery step
    m.onLinkFailure(500000);
    TEST_ASSERT_TRUE(LinkMonitor::Action::RECOVER == m.tick(560000));
}

void test_millis_wraparound(void) {
    LinkMonitor m(testConfig());
    const uint32_t near_wrap = 0xFFFFFC00u; // 1024 ms before millis() wraps
    m.reset(near_wrap);
    m.onLinkFailure(near_wrap);
    TEST_ASSERT_FALSE(m.canPoll(near_wrap + 1000));
    TEST_ASSERT_TRUE(m.canPoll(near_wrap + 2000));  // wraps past zero
    TEST_ASSERT_TRUE(LinkMonitor::Action::RECOVER == m.tick(near_wrap + 60000));
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_healthy_link_never_acts);
    RUN_TEST(test_quiet_link_without_failures_never_acts);
    RUN_TEST(test_backoff_grows_and_caps);
    RUN_TEST(test_alive_clears_backoff_and_failures);
    RUN_TEST(test_stale_after_failures);
    RUN_TEST(test_escalation_recover_then_restart);
    RUN_TEST(test_answer_after_recovery_resets_escalation);
    RUN_TEST(test_millis_wraparound);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_healthy_link_never_acts);
    RUN_TEST(test_quiet_link_without_failures_never_acts);
    RUN_TEST(test_backoff_grows_and_caps);
    RUN_TEST(test_alive_clears_backoff_and_failures);
    RUN_TEST(test_stale_after_failures);
    RUN_TEST(test_escalation_recover_then_restart);
    RUN_TEST(test_answer_after_recovery_resets_escalation);
    RUN_TEST(test_millis_wraparound);
    UNITY_END();
}
void loop() {}
#endif
#endif
