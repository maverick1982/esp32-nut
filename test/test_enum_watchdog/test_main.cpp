#include <unity.h>
#include "EnumWatchdog.h"
#include "RestartPolicy.h"

// US-121 (issue #78): after a controlled restart the UPS may never enumerate again, and
// ESP-IDF does not retry. The watchdog power-cycles the root port, then asks for a restart.

typedef EnumWatchdog::Action A;
typedef RestartPolicy::Decision D;

static const uint32_t FIRST = EnumWatchdog::FIRST_CYCLE_MS;
static const uint32_t STEP = EnumWatchdog::CYCLE_INTERVAL_MS;

void setUp(void) {}
void tearDown(void) {}

// Normal boot (counter 0): the owner never arms it, nothing happens for an hour
void test_not_armed_never_acts(void) {
    EnumWatchdog w;
    for (uint32_t t = 0; t <= 3600000; t += 1000) {
        TEST_ASSERT_TRUE(A::NONE == w.tick(false, t));
    }
    TEST_ASSERT_FALSE(w.armed());
    TEST_ASSERT_EQUAL_UINT8(0, w.cycles());
}

void test_cycles_then_restart(void) {
    EnumWatchdog w;
    const uint32_t t0 = 3000;
    w.arm(t0);
    TEST_ASSERT_TRUE(A::NONE == w.tick(false, t0 + FIRST - 1));
    TEST_ASSERT_TRUE(A::CYCLE_PORT == w.tick(false, t0 + FIRST));
    TEST_ASSERT_EQUAL_UINT8(1, w.cycles());
    TEST_ASSERT_TRUE(A::NONE == w.tick(false, t0 + FIRST + 1));
    TEST_ASSERT_TRUE(A::NONE == w.tick(false, t0 + FIRST + STEP - 1));
    TEST_ASSERT_TRUE(A::CYCLE_PORT == w.tick(false, t0 + FIRST + STEP));
    TEST_ASSERT_EQUAL_UINT8(2, w.cycles());
    TEST_ASSERT_TRUE(A::NONE == w.tick(false, t0 + FIRST + 2 * STEP - 1));
    TEST_ASSERT_TRUE(A::REQUEST_RESTART == w.tick(false, t0 + FIRST + 2 * STEP));
    TEST_ASSERT_TRUE(w.restartRequested());
    // Once only, and no more cycles
    for (uint32_t t = t0 + FIRST + 2 * STEP; t <= t0 + 3600000; t += 1000) {
        TEST_ASSERT_TRUE(A::NONE == w.tick(false, t));
    }
    TEST_ASSERT_EQUAL_UINT8(EnumWatchdog::MAX_PORT_CYCLES, w.cycles());
}

// A late tick does not pile the actions up: one per call
void test_late_tick_gives_one_action_per_call(void) {
    EnumWatchdog w;
    w.arm(0);
    const uint32_t late = FIRST + 10 * STEP;
    TEST_ASSERT_TRUE(A::CYCLE_PORT == w.tick(false, late));
    TEST_ASSERT_TRUE(A::CYCLE_PORT == w.tick(false, late));
    TEST_ASSERT_TRUE(A::REQUEST_RESTART == w.tick(false, late));
    TEST_ASSERT_TRUE(A::NONE == w.tick(false, late));
}

static void expectDisarmedForGood(EnumWatchdog& w, uint32_t from) {
    TEST_ASSERT_FALSE(w.armed());
    // Even if the device goes away again later
    for (uint32_t t = from; t <= from + 3600000; t += 1000) {
        TEST_ASSERT_TRUE(A::NONE == w.tick(false, t));
    }
}

void test_enumeration_cancels_before_cycles(void) {
    EnumWatchdog w;
    w.arm(3000);
    TEST_ASSERT_TRUE(A::NONE == w.tick(true, 5000));
    expectDisarmedForGood(w, 5000);
    TEST_ASSERT_EQUAL_UINT8(0, w.cycles());
}

void test_enumeration_cancels_between_cycles(void) {
    EnumWatchdog w;
    w.arm(0);
    TEST_ASSERT_TRUE(A::CYCLE_PORT == w.tick(false, FIRST));
    TEST_ASSERT_TRUE(A::NONE == w.tick(true, FIRST + 2000)); // the cycle worked
    expectDisarmedForGood(w, FIRST + 2000);
    TEST_ASSERT_EQUAL_UINT8(1, w.cycles());
    TEST_ASSERT_FALSE(w.restartRequested());
}

void test_enumeration_cancels_after_request(void) {
    EnumWatchdog w;
    w.arm(0);
    w.tick(false, FIRST);
    w.tick(false, FIRST + STEP);
    TEST_ASSERT_TRUE(A::REQUEST_RESTART == w.tick(false, FIRST + 2 * STEP));
    TEST_ASSERT_TRUE(A::NONE == w.tick(true, FIRST + 2 * STEP + 1000));
    expectDisarmedForGood(w, FIRST + 2 * STEP + 1000);
}

void test_millis_wrap(void) {
    EnumWatchdog w;
    const uint32_t t0 = 0xFFFFFFFFu - 30000u;
    w.arm(t0);
    TEST_ASSERT_TRUE(A::NONE == w.tick(false, t0 + 40000)); // wrapped, 40 s elapsed
    TEST_ASSERT_TRUE(A::CYCLE_PORT == w.tick(false, t0 + FIRST));
    TEST_ASSERT_TRUE(A::CYCLE_PORT == w.tick(false, t0 + FIRST + STEP));
    TEST_ASSERT_TRUE(A::REQUEST_RESTART == w.tick(false, t0 + FIRST + 2 * STEP));
}

struct BootOutcome {
    uint8_t cycles;
    uint32_t requestAt;  // when the watchdog asked for the restart
    uint32_t decisionAt; // when RestartPolicy said RESTART or DEGRADED
    D decision;
};

// One boot with no device: the owner arms the watchdog in begin() (3 s after the boot),
// USBHostUPS turns REQUEST_RESTART into a restart request and main.cpp asks RestartPolicy
static BootOutcome simulateBootWithoutUps(uint8_t consecutive) {
    EnumWatchdog w;
    RestartPolicy p(consecutive);
    const uint32_t t0 = 3000;
    w.arm(t0);
    bool requested = false;
    BootOutcome out = {0, 0, 0, D::NONE};
    for (uint32_t now = t0; now <= t0 + 3600000; now += 1000) {
        A a = w.tick(false, now);
        if (a == A::CYCLE_PORT) out.cycles++;
        if (a == A::REQUEST_RESTART) {
            requested = true;
            out.requestAt = now;
        }
        D d = p.update(requested, false, now);
        if (d == D::RESTART || d == D::DEGRADED) {
            out.decision = d;
            out.decisionAt = now;
            return out;
        }
    }
    return out;
}

// The Demonstrates line of the spec: port cycles, then the 1/5/15 min ladder of
// RestartPolicy, unchanged, up to degraded mode
void test_ladder_to_degraded(void) {
    const uint32_t delays[] = {60000, 300000, 900000};
    for (uint8_t n = 1; n < RestartPolicy::MAX_RESTARTS; n++) {
        BootOutcome o = simulateBootWithoutUps(n);
        TEST_ASSERT_EQUAL_UINT8(EnumWatchdog::MAX_PORT_CYCLES, o.cycles);
        TEST_ASSERT_EQUAL_UINT32(3000 + FIRST + 2 * STEP, o.requestAt);
        TEST_ASSERT_TRUE(D::RESTART == o.decision);
        TEST_ASSERT_EQUAL_UINT32(o.requestAt + delays[n - 1], o.decisionAt);
    }
    BootOutcome last = simulateBootWithoutUps(RestartPolicy::MAX_RESTARTS);
    TEST_ASSERT_EQUAL_UINT8(EnumWatchdog::MAX_PORT_CYCLES, last.cycles);
    TEST_ASSERT_TRUE(D::DEGRADED == last.decision);
    TEST_ASSERT_EQUAL_UINT32(last.requestAt, last.decisionAt);
}

// The UPS enumerates while the restart waits: claimInterface() clears the request and
// RestartPolicy cancels the wait
void test_enumeration_mid_ladder_cancels_restart(void) {
    EnumWatchdog w;
    RestartPolicy p(2);
    w.arm(0);
    bool requested = false;
    uint32_t now = 0;
    for (; now <= FIRST + 2 * STEP; now += 1000) {
        if (w.tick(false, now) == A::REQUEST_RESTART) requested = true;
    }
    TEST_ASSERT_TRUE(requested);
    TEST_ASSERT_TRUE(D::WAIT == p.update(requested, false, now));
    requested = false; // the UPS enumerated again
    TEST_ASSERT_TRUE(A::NONE == w.tick(true, now + 1000));
    TEST_ASSERT_TRUE(D::NONE == p.update(requested, false, now + 1000));
    for (uint32_t t = now + 1000; t <= now + 3600000; t += 1000) {
        TEST_ASSERT_TRUE(A::NONE == w.tick(false, t));
        TEST_ASSERT_TRUE(D::NONE == p.update(false, false, t));
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_not_armed_never_acts);
    RUN_TEST(test_cycles_then_restart);
    RUN_TEST(test_late_tick_gives_one_action_per_call);
    RUN_TEST(test_enumeration_cancels_before_cycles);
    RUN_TEST(test_enumeration_cancels_between_cycles);
    RUN_TEST(test_enumeration_cancels_after_request);
    RUN_TEST(test_millis_wrap);
    RUN_TEST(test_ladder_to_degraded);
    RUN_TEST(test_enumeration_mid_ladder_cancels_restart);
    return UNITY_END();
}
