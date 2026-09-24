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

// Review A1: no device attached means nothing current to serve
void test_stale_without_device(void) {
    const uint32_t grace = 15000;
    // Boot: the UPS is still enumerating
    TEST_ASSERT_FALSE(LinkMonitor::isStaleWithoutDevice(false, 0, grace));
    TEST_ASSERT_FALSE(LinkMonitor::isStaleWithoutDevice(false, 14999, grace));
    // No UPS showed up in time
    TEST_ASSERT_TRUE(LinkMonitor::isStaleWithoutDevice(false, 15000, grace));
    // A UPS was attached and went away (e.g. USB reset on a blackout): stale at once,
    // even inside the boot grace
    TEST_ASSERT_TRUE(LinkMonitor::isStaleWithoutDevice(true, 5000, grace));
    TEST_ASSERT_TRUE(LinkMonitor::isStaleWithoutDevice(true, 3600000, grace));
}

// Review A2: watchdog on the INPUT pipe

// CyberPower-like: reports 8 and 11 together every 3 s. Returns the last report time.
static uint32_t feedBursts(InputWatchdog& wd, uint32_t start, int bursts, uint32_t period) {
    uint32_t t = start;
    for (int i = 0; i < bursts; i++) {
        t = start + i * period;
        wd.onInput(t);
        wd.onInput(t + 10); // same burst
    }
    return t + 10;
}

void test_input_periodic_silence_recovers_then_restarts(void) {
    InputWatchdog wd;
    wd.reset(0);
    uint32_t last = feedBursts(wd, 1000, 9, 3000); // 8 intervals
    TEST_ASSERT_TRUE(wd.isPeriodic());
    TEST_ASSERT_EQUAL_UINT32(3000, wd.periodMs());
    TEST_ASSERT_EQUAL_UINT32(30000, wd.silenceThresholdMs()); // 5 x 3 s is below the floor

    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == wd.tick(last + 29999));
    TEST_ASSERT_FALSE(wd.isStale(last + 29999));

    TEST_ASSERT_TRUE(wd.isStale(last + 30000));
    TEST_ASSERT_TRUE(LinkMonitor::Action::RECOVER == wd.tick(last + 30000));
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == wd.tick(last + 30001));
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == wd.tick(last + 89999));
    TEST_ASSERT_TRUE(LinkMonitor::Action::RECOVER == wd.tick(last + 90000));
    TEST_ASSERT_TRUE(LinkMonitor::Action::RESTART == wd.tick(last + 150000));
}

void test_input_resume_clears_stale_and_keeps_period(void) {
    InputWatchdog wd;
    wd.reset(0);
    uint32_t last = feedBursts(wd, 1000, 9, 3000);
    TEST_ASSERT_TRUE(LinkMonitor::Action::RECOVER == wd.tick(last + 40000));

    // Reports come back after the recovery
    wd.onInput(last + 41000);
    TEST_ASSERT_FALSE(wd.isStale(last + 41000));
    TEST_ASSERT_EQUAL_UINT8(0, wd.recoveries());
    // The 41 s gap was a silence, not a period
    TEST_ASSERT_TRUE(wd.isPeriodic());
    TEST_ASSERT_EQUAL_UINT32(3000, wd.periodMs());
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == wd.tick(last + 41000 + 29999));
    TEST_ASSERT_TRUE(LinkMonitor::Action::RECOVER == wd.tick(last + 41000 + 30000));
}

void test_input_long_period_scales_threshold(void) {
    InputWatchdog wd;
    wd.reset(0);
    uint32_t last = feedBursts(wd, 0, 9, 10000);
    TEST_ASSERT_EQUAL_UINT32(50000, wd.silenceThresholdMs());
    TEST_ASSERT_FALSE(wd.isStale(last + 49999));
    TEST_ASSERT_TRUE(wd.isStale(last + 50000));
}

void test_input_change_driven_device_never_acts(void) {
    // Reports only on changes (APC): irregular intervals
    InputWatchdog wd;
    wd.reset(0);
    const uint32_t gaps[] = {1000, 7000, 2000, 30000, 5000, 1500, 60000, 4000, 9000, 2500};
    uint32_t t = 0;
    for (uint32_t g : gaps) {
        t += g;
        wd.onInput(t);
    }
    TEST_ASSERT_FALSE(wd.isPeriodic());
    TEST_ASSERT_FALSE(wd.isStale(t + 3600000));
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == wd.tick(t + 3600000));
}

void test_input_not_enough_samples_never_acts(void) {
    InputWatchdog wd;
    wd.reset(0);
    uint32_t last = feedBursts(wd, 0, 8, 3000); // 7 intervals
    TEST_ASSERT_FALSE(wd.isPeriodic());
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == wd.tick(last + 3600000));

    // A new device forgets the learned period
    last = feedBursts(wd, last + 3000, 9, 3000);
    TEST_ASSERT_TRUE(wd.isPeriodic());
    wd.reset(last);
    TEST_ASSERT_FALSE(wd.isPeriodic());
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == wd.tick(last + 3600000));
}

void test_input_jitter_tolerated(void) {
    InputWatchdog wd;
    wd.reset(0);
    const uint32_t gaps[] = {3000, 3200, 2900, 3100, 2800, 3000, 3300, 2950};
    uint32_t t = 0;
    wd.onInput(t);
    for (uint32_t g : gaps) {
        t += g;
        wd.onInput(t);
    }
    TEST_ASSERT_TRUE(wd.isPeriodic());
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
    RUN_TEST(test_stale_without_device);
    RUN_TEST(test_input_periodic_silence_recovers_then_restarts);
    RUN_TEST(test_input_resume_clears_stale_and_keeps_period);
    RUN_TEST(test_input_long_period_scales_threshold);
    RUN_TEST(test_input_change_driven_device_never_acts);
    RUN_TEST(test_input_not_enough_samples_never_acts);
    RUN_TEST(test_input_jitter_tolerated);
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
    RUN_TEST(test_stale_without_device);
    RUN_TEST(test_input_periodic_silence_recovers_then_restarts);
    RUN_TEST(test_input_resume_clears_stale_and_keeps_period);
    RUN_TEST(test_input_long_period_scales_threshold);
    RUN_TEST(test_input_change_driven_device_never_acts);
    RUN_TEST(test_input_not_enough_samples_never_acts);
    RUN_TEST(test_input_jitter_tolerated);
    UNITY_END();
}
void loop() {}
#endif
#endif
