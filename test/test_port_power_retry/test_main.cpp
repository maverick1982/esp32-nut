#include <unity.h>
#include "PortPowerOnRetry.h"

// Issue #78: after the power off of a root port cycle, the power on is retried until
// ESP-IDF accepts it (the HCD port is in RECOVERY until the device is freed).

typedef PortPowerOnRetry R;
typedef PortPowerOnRetry::Outcome O;

void setUp(void) {}
void tearDown(void) {}

void test_idle_until_started(void) {
    R r;
    TEST_ASSERT_FALSE(r.pending());
    TEST_ASSERT_FALSE(r.due(1000));
    TEST_ASSERT_FALSE(r.inFastWindow(1000));
    TEST_ASSERT_TRUE(O::DONE == r.onResult(false, 1000)); // nothing pending
}

void test_first_attempt_after_off_pause(void) {
    R r;
    r.start(10000);
    TEST_ASSERT_TRUE(r.pending());
    TEST_ASSERT_FALSE(r.due(10000 + R::OFF_MS - 1));
    TEST_ASSERT_EQUAL_UINT32(1, r.msUntilDue(10000 + R::OFF_MS - 1));
    TEST_ASSERT_TRUE(r.due(10000 + R::OFF_MS));
    TEST_ASSERT_EQUAL_UINT32(0, r.msUntilDue(10000 + R::OFF_MS));
}

void test_accepted_at_once(void) {
    R r;
    r.start(0);
    TEST_ASSERT_TRUE(O::DONE == r.onResult(true, R::OFF_MS));
    TEST_ASSERT_FALSE(r.pending());
    TEST_ASSERT_EQUAL_UINT16(1, r.attempts());
}

void test_fast_retries_then_accepted(void) {
    R r;
    r.start(0);
    uint32_t t = R::OFF_MS;
    TEST_ASSERT_TRUE(O::RETRY == r.onResult(false, t));
    TEST_ASSERT_FALSE(r.due(t + R::FAST_RETRY_MS - 1));
    t += R::FAST_RETRY_MS;
    TEST_ASSERT_TRUE(r.due(t));
    TEST_ASSERT_TRUE(O::RETRY == r.onResult(false, t));
    t += R::FAST_RETRY_MS;
    TEST_ASSERT_TRUE(O::DONE == r.onResult(true, t));
    TEST_ASSERT_EQUAL_UINT16(3, r.attempts());
    TEST_ASSERT_FALSE(r.inFastWindow(t));
}

void test_still_off_once_after_fast_window(void) {
    R r;
    r.start(0);
    uint32_t t = R::OFF_MS;
    while (r.inFastWindow(t)) {
        TEST_ASSERT_TRUE(O::RETRY == r.onResult(false, t));
        t += r.msUntilDue(t);
    }
    // First failure past the window: one WARN, then slow retries without limit
    TEST_ASSERT_TRUE(O::STILL_OFF == r.onResult(false, t));
    TEST_ASSERT_TRUE(r.pending());
    TEST_ASSERT_FALSE(r.due(t + R::SLOW_RETRY_MS - 1));
    t += R::SLOW_RETRY_MS;
    TEST_ASSERT_TRUE(O::RETRY == r.onResult(false, t));
    t += R::SLOW_RETRY_MS;
    TEST_ASSERT_TRUE(O::DONE == r.onResult(true, t));
    TEST_ASSERT_FALSE(r.pending());
}

void test_restart_clears_state(void) {
    R r;
    r.start(0);
    uint32_t t = R::OFF_MS;
    while (r.inFastWindow(t)) {
        r.onResult(false, t);
        t += r.msUntilDue(t);
    }
    TEST_ASSERT_TRUE(O::STILL_OFF == r.onResult(false, t));
    r.onResult(true, t + R::SLOW_RETRY_MS);
    // A new cycle warns again and counts from zero
    r.start(100000);
    TEST_ASSERT_EQUAL_UINT16(0, r.attempts());
    t = 100000 + R::OFF_MS;
    while (r.inFastWindow(t)) {
        r.onResult(false, t);
        t += r.msUntilDue(t);
    }
    TEST_ASSERT_TRUE(O::STILL_OFF == r.onResult(false, t));
}

void test_millis_wrap(void) {
    R r;
    const uint32_t start = 0xFFFFFF00u;
    r.start(start);
    TEST_ASSERT_FALSE(r.due(start + 100));
    const uint32_t first = start + R::OFF_MS; // wraps past 0
    TEST_ASSERT_TRUE(r.due(first));
    TEST_ASSERT_TRUE(r.inFastWindow(first));
    TEST_ASSERT_TRUE(O::RETRY == r.onResult(false, first));
    TEST_ASSERT_TRUE(r.inFastWindow(first + R::FAST_RETRY_MS));
    TEST_ASSERT_FALSE(r.inFastWindow(start + R::OFF_MS + R::FAST_WINDOW_MS));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_until_started);
    RUN_TEST(test_first_attempt_after_off_pause);
    RUN_TEST(test_accepted_at_once);
    RUN_TEST(test_fast_retries_then_accepted);
    RUN_TEST(test_still_off_once_after_fast_window);
    RUN_TEST(test_restart_clears_state);
    RUN_TEST(test_millis_wrap);
    return UNITY_END();
}
