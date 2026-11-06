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
    // A device that issues no control request never fails
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

// Issue #60: a timed-out transfer still held by the USB stack. An interface restart
// cannot free EP0, so the monitor goes straight to the restart, 3 s after the first
// failure, before the data turns stale.
void test_stuck_control_transfer_restarts_directly(void) {
    LinkMonitor m(testConfig());
    m.reset(0);
    m.onAlive(1000);
    m.onLinkFailure(9000); // the last answer was 8 s earlier: the wait starts here
    m.onLinkFailure(9500);
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == m.tick(11999, true));
    TEST_ASSERT_TRUE(LinkMonitor::Action::RESTART == m.tick(12000, true));
    TEST_ASSERT_EQUAL_UINT8(0, m.recoveries());
    TEST_ASSERT_FALSE(m.isStale(12000));
}

void test_stuck_flag_without_failures_never_acts(void) {
    LinkMonitor m(testConfig());
    m.reset(0);
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == m.tick(3600000, true));
}

// The late callback arrived: the pipe is free again and the normal ladder applies
void test_unstuck_pipe_uses_recovery_ladder(void) {
    LinkMonitor m(testConfig());
    m.reset(0);
    m.onLinkFailure(1000);
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == m.tick(31000, false));
    TEST_ASSERT_TRUE(LinkMonitor::Action::RECOVER == m.tick(60000, false));
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

// US-108: a watchdog never fed is never periodic and never acts, the case of QUIRK_NO_INTERRUPT_PIPE
// (APC 5G), where USBHostUPS drops the INPUT reports before onInput()
void test_input_never_fed_never_acts(void) {
    InputWatchdog wd;
    wd.reset(0);
    TEST_ASSERT_FALSE(wd.isPeriodic());
    TEST_ASSERT_FALSE(wd.isStale(600000));
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == wd.tick(600000));
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == wd.tick(3600000));
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

// US-123 (issue #111): the MGE Protection Center sends one INPUT report a minute, every value
// of which the poll also requests. A silence freezes nothing: no stale data, no recovery, no
// restart, even after a day.
void test_input_values_polled_silence_never_acts(void) {
    InputWatchdog wd;
    wd.reset(0);
    uint32_t last = feedBursts(wd, 0, 9, 60000);
    TEST_ASSERT_TRUE(wd.isPeriodic());
    TEST_ASSERT_EQUAL_UINT32(300000, wd.silenceThresholdMs());
    wd.setValuesPolled(true);
    TEST_ASSERT_TRUE(wd.valuesPolled());

    const uint32_t later[] = {300000, 360000, 420000, 3600000, 86400000};
    for (uint32_t d : later) {
        TEST_ASSERT_FALSE(wd.isStale(last + d));
        TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == wd.tick(last + d));
    }
    TEST_ASSERT_EQUAL_UINT8(0, wd.recoveries());
    // The period is still known, for the export
    TEST_ASSERT_TRUE(wd.isPeriodic());
    TEST_ASSERT_EQUAL_UINT32(60000, wd.periodMs());
}

// US-123: one notice per silence, armed again by the next INPUT report
void test_input_values_polled_notice_once_per_silence(void) {
    InputWatchdog wd;
    wd.reset(0);
    uint32_t last = feedBursts(wd, 0, 9, 60000);
    wd.setValuesPolled(true);

    TEST_ASSERT_FALSE(wd.takeSilenceNotice(last + 299999));
    TEST_ASSERT_TRUE(wd.takeSilenceNotice(last + 300000));
    TEST_ASSERT_FALSE(wd.takeSilenceNotice(last + 400000));
    TEST_ASSERT_FALSE(wd.takeSilenceNotice(last + 3600000));

    // The reports come back, then stop again
    uint32_t back = last + 3600000;
    wd.onInput(back);
    TEST_ASSERT_FALSE(wd.takeSilenceNotice(back + 1000));
    TEST_ASSERT_TRUE(wd.takeSilenceNotice(back + 300000));
    TEST_ASSERT_FALSE(wd.takeSilenceNotice(back + 300001));

    // Values not polled: no notice, the ladder acts instead
    InputWatchdog plain;
    plain.reset(0);
    last = feedBursts(plain, 0, 9, 60000);
    TEST_ASSERT_FALSE(plain.takeSilenceNotice(last + 300000));
    TEST_ASSERT_TRUE(LinkMonitor::Action::RECOVER == plain.tick(last + 300000));
}

// US-123: a new device starts with unknown coverage, so the ladder of review A2 applies
void test_input_values_polled_reset_by_new_device(void) {
    InputWatchdog wd;
    wd.reset(0);
    wd.setValuesPolled(true);
    wd.reset(1000);
    TEST_ASSERT_FALSE(wd.valuesPolled());

    uint32_t last = feedBursts(wd, 1000, 9, 3000);
    TEST_ASSERT_TRUE(wd.isStale(last + 30000));
    TEST_ASSERT_TRUE(LinkMonitor::Action::RECOVER == wd.tick(last + 30000));
    TEST_ASSERT_FALSE(wd.takeSilenceNotice(last + 30000));
}

// US-123: a change-driven device is never periodic, so no notice either
void test_input_change_driven_polled_no_notice(void) {
    InputWatchdog wd;
    wd.reset(0);
    wd.setValuesPolled(true);
    const uint32_t gaps[] = {1000, 7000, 2000, 30000, 5000, 1500, 60000, 4000, 9000, 2500};
    uint32_t t = 0;
    for (uint32_t g : gaps) {
        t += g;
        wd.onInput(t);
    }
    TEST_ASSERT_FALSE(wd.isPeriodic());
    TEST_ASSERT_FALSE(wd.takeSilenceNotice(t + 3600000));
    TEST_ASSERT_TRUE(LinkMonitor::Action::NONE == wd.tick(t + 3600000));
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
    RUN_TEST(test_stuck_control_transfer_restarts_directly);
    RUN_TEST(test_stuck_flag_without_failures_never_acts);
    RUN_TEST(test_unstuck_pipe_uses_recovery_ladder);
    RUN_TEST(test_input_periodic_silence_recovers_then_restarts);
    RUN_TEST(test_input_resume_clears_stale_and_keeps_period);
    RUN_TEST(test_input_long_period_scales_threshold);
    RUN_TEST(test_input_change_driven_device_never_acts);
    RUN_TEST(test_input_not_enough_samples_never_acts);
    RUN_TEST(test_input_never_fed_never_acts);
    RUN_TEST(test_input_jitter_tolerated);
    RUN_TEST(test_input_values_polled_silence_never_acts);
    RUN_TEST(test_input_values_polled_notice_once_per_silence);
    RUN_TEST(test_input_values_polled_reset_by_new_device);
    RUN_TEST(test_input_change_driven_polled_no_notice);
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
    RUN_TEST(test_stuck_control_transfer_restarts_directly);
    RUN_TEST(test_stuck_flag_without_failures_never_acts);
    RUN_TEST(test_unstuck_pipe_uses_recovery_ladder);
    RUN_TEST(test_input_periodic_silence_recovers_then_restarts);
    RUN_TEST(test_input_resume_clears_stale_and_keeps_period);
    RUN_TEST(test_input_long_period_scales_threshold);
    RUN_TEST(test_input_change_driven_device_never_acts);
    RUN_TEST(test_input_not_enough_samples_never_acts);
    RUN_TEST(test_input_never_fed_never_acts);
    RUN_TEST(test_input_jitter_tolerated);
    RUN_TEST(test_input_values_polled_silence_never_acts);
    RUN_TEST(test_input_values_polled_notice_once_per_silence);
    RUN_TEST(test_input_values_polled_reset_by_new_device);
    RUN_TEST(test_input_change_driven_polled_no_notice);
    UNITY_END();
}
void loop() {}
#endif
#endif
