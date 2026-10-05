#include <unity.h>
#include "RestartPolicy.h"

// Review A7: no endless boot loop of controlled restarts

typedef RestartPolicy::Decision D;

void setUp(void) {}
void tearDown(void) {}

void test_no_request_no_action(void) {
    RestartPolicy p(0);
    TEST_ASSERT_TRUE(D::NONE == p.update(false, true, 1000));
    TEST_ASSERT_TRUE(D::NONE == p.update(false, false, 2000));
}

void test_first_restart_immediate(void) {
    RestartPolicy p(0);
    TEST_ASSERT_TRUE(D::RESTART == p.update(true, false, 180000));
}

void test_backoff_grows(void) {
    const uint32_t delays[] = {60000, 300000, 900000};
    for (uint8_t n = 1; n <= 3; n++) {
        RestartPolicy p(n);
        const uint32_t t0 = 200000;
        TEST_ASSERT_TRUE(D::WAIT == p.update(true, false, t0));
        TEST_ASSERT_EQUAL_UINT32(delays[n - 1], p.msUntilRestart(t0));
        TEST_ASSERT_TRUE(D::WAIT == p.update(true, false, t0 + delays[n - 1] - 1));
        TEST_ASSERT_TRUE(D::RESTART == p.update(true, false, t0 + delays[n - 1]));
    }
}

void test_degraded_after_threshold(void) {
    RestartPolicy p(RestartPolicy::MAX_RESTARTS);
    TEST_ASSERT_TRUE(p.isDegraded());
    TEST_ASSERT_TRUE(D::DEGRADED == p.update(true, false, 1000));
    TEST_ASSERT_TRUE(D::DEGRADED == p.update(true, false, 100000000));
}

void test_new_enumeration_cancels_wait(void) {
    RestartPolicy p(2);
    TEST_ASSERT_TRUE(D::WAIT == p.update(true, false, 1000));
    // The UPS enumerated again: USBHostUPS dropped the request
    TEST_ASSERT_TRUE(D::NONE == p.update(false, true, 2000));
    // A later request starts a new full wait
    TEST_ASSERT_TRUE(D::WAIT == p.update(true, false, 400000));
    TEST_ASSERT_TRUE(D::WAIT == p.update(true, false, 400000 + 299999));
    TEST_ASSERT_TRUE(D::RESTART == p.update(true, false, 400000 + 300000));
}

void test_healthy_period_clears_counter(void) {
    RestartPolicy p(3);
    p.update(false, true, 10000);
    TEST_ASSERT_TRUE(D::NONE == p.update(false, true, 10000 + RestartPolicy::HEALTHY_RESET_MS - 1));
    TEST_ASSERT_EQUAL_UINT8(3, p.consecutive());
    TEST_ASSERT_FALSE(p.takeCleared());

    p.update(false, true, 10000 + RestartPolicy::HEALTHY_RESET_MS);
    TEST_ASSERT_EQUAL_UINT8(0, p.consecutive());
    TEST_ASSERT_TRUE(p.takeCleared());
    TEST_ASSERT_FALSE(p.takeCleared());
    // Back to an immediate first restart
    TEST_ASSERT_TRUE(D::RESTART == p.update(true, false, 20000 + RestartPolicy::HEALTHY_RESET_MS));
}

void test_unhealthy_interrupts_healthy_period(void) {
    RestartPolicy p(1);
    p.update(false, true, 0);
    p.update(false, false, 500000); // stale for a while
    p.update(false, true, 600000);
    TEST_ASSERT_EQUAL_UINT8(1, p.consecutive()); // the healthy period restarts from here
    p.update(false, true, 600000 + RestartPolicy::HEALTHY_RESET_MS);
    TEST_ASSERT_EQUAL_UINT8(0, p.consecutive());
}

// Issue #60: an APC Back-UPS BX locks EP0 again some minutes after each restart. Each
// restart worked, so each one runs at once and the board never enters degraded mode.
void test_restart_that_worked_keeps_next_restart_immediate(void) {
    RestartPolicy p(0);
    uint32_t t = 0;
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_TRUE(D::RESTART == p.update(true, false, t));
        // After the restart (the RTC count survives it): fresh data for 5 minutes
        p = RestartPolicy(p.consecutive() + 1);
        for (uint32_t ms = 0; ms <= 300000; ms += 1000) p.update(false, true, t + ms);
        TEST_ASSERT_EQUAL_UINT8(0, p.consecutive());
        t += 301000;
    }
}

// Issue #60: the BX locked EP0 65 s after a boot. A full poll answered by the UPS
// proves the restart worked: 30 s of fresh data clear the counter.
void test_proven_control_pipe_clears_counter_sooner(void) {
    RestartPolicy p(1);
    p.update(false, true, 10000, true);
    TEST_ASSERT_TRUE(D::NONE == p.update(false, true, 10000 + RestartPolicy::PROVEN_RESET_MS - 1, true));
    TEST_ASSERT_EQUAL_UINT8(1, p.consecutive());
    p.update(false, true, 10000 + RestartPolicy::PROVEN_RESET_MS, true);
    TEST_ASSERT_EQUAL_UINT8(0, p.consecutive());
    TEST_ASSERT_TRUE(p.takeCleared());
    // The lockup 65 s after the boot restarts at once
    TEST_ASSERT_TRUE(D::RESTART == p.update(true, false, 75000, false));
}

// Not proven (e.g. INPUT-only device): the 2 minute rule
void test_unproven_keeps_healthy_reset(void) {
    RestartPolicy p(1);
    p.update(false, true, 10000, false);
    p.update(false, true, 10000 + RestartPolicy::PROVEN_RESET_MS, false);
    TEST_ASSERT_EQUAL_UINT8(1, p.consecutive());
    p.update(false, true, 10000 + RestartPolicy::HEALTHY_RESET_MS, false);
    TEST_ASSERT_EQUAL_UINT8(0, p.consecutive());
}

// A device that locks up within 30 s of every boot still reaches degraded mode
void test_fast_failing_device_still_degrades(void) {
    uint8_t consecutive = 0;
    for (int i = 0; i < RestartPolicy::MAX_RESTARTS; i++) {
        RestartPolicy p(consecutive);
        p.update(false, true, 1000, true);
        p.update(false, true, 1000 + RestartPolicy::PROVEN_RESET_MS - 1000, true);
        TEST_ASSERT_TRUE(D::NONE != p.update(true, false, 1000 + RestartPolicy::PROVEN_RESET_MS, false));
        consecutive = p.consecutive() + 1; // the restart ran
    }
    RestartPolicy p(consecutive);
    TEST_ASSERT_TRUE(D::DEGRADED == p.update(true, false, 1000, false));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_no_request_no_action);
    RUN_TEST(test_first_restart_immediate);
    RUN_TEST(test_backoff_grows);
    RUN_TEST(test_degraded_after_threshold);
    RUN_TEST(test_new_enumeration_cancels_wait);
    RUN_TEST(test_healthy_period_clears_counter);
    RUN_TEST(test_unhealthy_interrupts_healthy_period);
    RUN_TEST(test_restart_that_worked_keeps_next_restart_immediate);
    RUN_TEST(test_proven_control_pipe_clears_counter_sooner);
    RUN_TEST(test_unproven_keeps_healthy_reset);
    RUN_TEST(test_fast_failing_device_still_degrades);
    return UNITY_END();
}
