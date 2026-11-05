#include <unity.h>
#include <string.h>
#include "PortCyclePolicy.h"

// US-122 (issue #120): a stuck control transfer is recovered with a root port power
// cycle; the restart stays only as a safety net.

typedef PortCyclePolicy::Decision D;
typedef PortCyclePolicy P;

void setUp(void) {}
void tearDown(void) {}

void test_first_stuck_cycles_port(void) {
    PortCyclePolicy p;
    TEST_ASSERT_FALSE(p.waiting());
    TEST_ASSERT_TRUE(D::CYCLE_PORT == p.onStuck(190000));
    TEST_ASSERT_TRUE(p.waiting());
    TEST_ASSERT_EQUAL_UINT32(190000, p.cycleStartedAt());
    TEST_ASSERT_EQUAL_UINT32(1, p.cyclesSinceBoot());
}

void test_stuck_again_before_proven_restarts(void) {
    PortCyclePolicy p;
    p.onStuck(1000);
    TEST_ASSERT_TRUE(D::RESTART == p.onStuck(5000));
    TEST_ASSERT_EQUAL_STRING(P::STUCK_AGAIN_REASON, p.lastRestartReason());
    // A restart is not a cycle
    TEST_ASSERT_EQUAL_UINT32(1, p.cyclesSinceBoot());
}

void test_proven_allows_next_cycle(void) {
    PortCyclePolicy p;
    p.onStuck(1000);
    p.onProven();
    TEST_ASSERT_FALSE(p.waiting());
    TEST_ASSERT_TRUE(D::CYCLE_PORT == p.onStuck(2000));
    TEST_ASSERT_EQUAL_UINT32(2, p.cyclesSinceBoot());
}

void test_back_timeout_once(void) {
    PortCyclePolicy p;
    const uint32_t t0 = 10000;
    TEST_ASSERT_FALSE(p.backTimedOut(t0 + 60000)); // not waiting: never
    p.onStuck(t0);
    TEST_ASSERT_FALSE(p.backTimedOut(t0 + P::BACK_TIMEOUT_MS - 1));
    TEST_ASSERT_TRUE(p.backTimedOut(t0 + P::BACK_TIMEOUT_MS));
    TEST_ASSERT_FALSE(p.backTimedOut(t0 + P::BACK_TIMEOUT_MS + 1));
    TEST_ASSERT_FALSE(p.backTimedOut(t0 + 10 * P::BACK_TIMEOUT_MS));
}

void test_proven_cancels_back_timeout(void) {
    PortCyclePolicy p;
    p.onStuck(0);
    p.onProven();
    TEST_ASSERT_FALSE(p.backTimedOut(P::BACK_TIMEOUT_MS));
    // The next cycle gets its own timeout
    p.onStuck(100000);
    TEST_ASSERT_TRUE(p.backTimedOut(100000 + P::BACK_TIMEOUT_MS));
}

// Cycle and proven again quickly, n times starting at t, one cycle every step ms
static uint32_t cycleTimes(PortCyclePolicy& p, uint8_t n, uint32_t t, uint32_t step) {
    for (uint8_t i = 0; i < n; i++, t += step) {
        TEST_ASSERT_TRUE(D::CYCLE_PORT == p.onStuck(t));
        p.onProven();
    }
    return t;
}

void test_window_limit(void) {
    PortCyclePolicy p;
    const uint32_t t0 = 5000;
    uint32_t t = cycleTimes(p, P::MAX_CYCLES_PER_WINDOW, t0, 60000);
    // 13th within the hour of the first
    TEST_ASSERT_TRUE(D::RESTART == p.onStuck(t));
    TEST_ASSERT_EQUAL_STRING(P::TOO_MANY_REASON, p.lastRestartReason());
    TEST_ASSERT_FALSE(p.waiting());
    TEST_ASSERT_EQUAL_UINT32(P::MAX_CYCLES_PER_WINDOW, p.cyclesSinceBoot());
    TEST_ASSERT_TRUE(D::RESTART == p.onStuck(t0 + P::WINDOW_MS - 1));
}

void test_window_slides(void) {
    PortCyclePolicy p;
    const uint32_t t0 = 5000;
    cycleTimes(p, P::MAX_CYCLES_PER_WINDOW, t0, 60000);
    // One hour after the oldest cycle, it drops out of the window
    TEST_ASSERT_TRUE(D::CYCLE_PORT == p.onStuck(t0 + P::WINDOW_MS));
    p.onProven();
    // The next oldest is at t0 + 60 s
    TEST_ASSERT_TRUE(D::RESTART == p.onStuck(t0 + 60000 + P::WINDOW_MS - 1));
    TEST_ASSERT_TRUE(D::CYCLE_PORT == p.onStuck(t0 + 60000 + P::WINDOW_MS));
    TEST_ASSERT_EQUAL_UINT32(P::MAX_CYCLES_PER_WINDOW + 2, p.cyclesSinceBoot());
}

void test_cycles_since_boot(void) {
    PortCyclePolicy p;
    TEST_ASSERT_EQUAL_UINT32(0, p.cyclesSinceBoot());
    // One cycle every 10 min for 5 hours: never more than 6 per hour
    cycleTimes(p, 30, 1000, 600000);
    TEST_ASSERT_EQUAL_UINT32(30, p.cyclesSinceBoot());
}

void test_millis_wrap(void) {
    PortCyclePolicy p;
    const uint32_t t0 = 0xFFFFFFFFu - 10000u;
    TEST_ASSERT_TRUE(D::CYCLE_PORT == p.onStuck(t0));
    TEST_ASSERT_FALSE(p.backTimedOut(t0 + 20000)); // wrapped, 20 s elapsed
    TEST_ASSERT_TRUE(p.backTimedOut(t0 + P::BACK_TIMEOUT_MS));
    p.onProven();
    // The window counts across the wrap too
    PortCyclePolicy w;
    uint32_t t = cycleTimes(w, P::MAX_CYCLES_PER_WINDOW, t0, 1000);
    TEST_ASSERT_TRUE(D::RESTART == w.onStuck(t));
    TEST_ASSERT_TRUE(D::CYCLE_PORT == w.onStuck(t0 + P::WINDOW_MS));
}

void test_reasons_fit_restart_record(void) {
    TEST_ASSERT_TRUE(strlen(P::STUCK_AGAIN_REASON) < 64);
    TEST_ASSERT_TRUE(strlen(P::TOO_MANY_REASON) < 64);
    TEST_ASSERT_TRUE(strlen(P::NOT_BACK_REASON) < 64);
}

// The serial log of issue #120: seconds from each recovery to the next stuck transfer
// (13 restarts in about 2 hours). With the port cycle the UPS is proven again about 6 s
// after each cycle; none of them must end in a restart.
void test_issue120_timeline(void) {
    static const uint32_t gapsS[] = {190, 452, 518, 28, 158, 760, 84, 200, 260, 100, 1810, 519, 339};
    const uint32_t BACK_MS = 6000;
    PortCyclePolicy p;
    uint32_t t = 0;
    for (uint8_t i = 0; i < sizeof(gapsS) / sizeof(gapsS[0]); i++) {
        t += gapsS[i] * 1000u;
        TEST_ASSERT_TRUE_MESSAGE(D::CYCLE_PORT == p.onStuck(t), "issue #120 stuck transfer led to a restart");
        for (uint32_t w = t; w < t + BACK_MS; w += 500) TEST_ASSERT_FALSE(p.backTimedOut(w));
        t += BACK_MS;
        p.onProven();
    }
    TEST_ASSERT_EQUAL_UINT32(13, p.cyclesSinceBoot());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_first_stuck_cycles_port);
    RUN_TEST(test_stuck_again_before_proven_restarts);
    RUN_TEST(test_proven_allows_next_cycle);
    RUN_TEST(test_back_timeout_once);
    RUN_TEST(test_proven_cancels_back_timeout);
    RUN_TEST(test_window_limit);
    RUN_TEST(test_window_slides);
    RUN_TEST(test_cycles_since_boot);
    RUN_TEST(test_millis_wrap);
    RUN_TEST(test_reasons_fit_restart_record);
    RUN_TEST(test_issue120_timeline);
    return UNITY_END();
}
