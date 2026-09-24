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
    TEST_ASSERT_EQUAL_UINT8(1, p.consecutive()); // the 10 minutes restart from here
    p.update(false, true, 600000 + RestartPolicy::HEALTHY_RESET_MS);
    TEST_ASSERT_EQUAL_UINT8(0, p.consecutive());
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
    return UNITY_END();
}
