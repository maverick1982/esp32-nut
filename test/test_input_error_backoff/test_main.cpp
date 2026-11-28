#include <unity.h>
#include "InputErrorBackoff.h"

// Issue #36: the interface restarts after IN pipe errors are spaced out, and the board
// restart waits until the errors have gone on for a while

typedef InputErrorBackoff B;

void setUp(void) {}
void tearDown(void) {}

// Feeds one error and runs the restart as soon as it is due; returns the time of the restart
static uint32_t errorAndRecover(B& b, uint32_t now) {
    TEST_ASSERT_TRUE(b.onError(now) == B::Action::RECOVER);
    TEST_ASSERT_TRUE(b.pending());
    uint32_t at = now + b.delayMs();
    TEST_ASSERT_FALSE(b.due(at - 1));
    TEST_ASSERT_TRUE(b.due(at));
    b.onRecoveryStarted();
    TEST_ASSERT_FALSE(b.pending());
    return at;
}

void test_idle_by_default(void) {
    B b;
    TEST_ASSERT_FALSE(b.pending());
    TEST_ASSERT_FALSE(b.due(100000));
    TEST_ASSERT_EQUAL_UINT8(0, b.recoveries());
    TEST_ASSERT_EQUAL_UINT32(0, b.streakMs(5000));
}

void test_first_restart_waits_first_delay(void) {
    B b;
    TEST_ASSERT_TRUE(b.onError(1000) == B::Action::RECOVER);
    TEST_ASSERT_EQUAL_UINT32(B::FIRST_DELAY_MS, b.delayMs());
    TEST_ASSERT_FALSE(b.due(1000));
    TEST_ASSERT_FALSE(b.due(1000 + B::FIRST_DELAY_MS - 1));
    TEST_ASSERT_TRUE(b.due(1000 + B::FIRST_DELAY_MS));
    TEST_ASSERT_EQUAL_UINT8(1, b.recoveries());
}

void test_delay_doubles_up_to_max(void) {
    B b;
    uint32_t now = 1000;
    uint32_t expected = B::FIRST_DELAY_MS;
    for (int i = 0; i < 12; i++) {
        // Errors closer than ESCALATE_AFTER_MS in total: always RECOVER
        TEST_ASSERT_TRUE(b.onError(now) == B::Action::RECOVER);
        TEST_ASSERT_EQUAL_UINT32(expected, b.delayMs());
        b.onRecoveryStarted();
        now += 10;
        expected = (expected * 2 > B::MAX_DELAY_MS) ? B::MAX_DELAY_MS : expected * 2;
    }
    TEST_ASSERT_EQUAL_UINT32(B::MAX_DELAY_MS, b.delayMs());
}

// Errors 50 ms after every interface restart until onError() escalates; returns the action
static B::Action runUntilEscalation(B& b, uint32_t& now) {
    for (;;) {
        B::Action a = b.onError(now);
        if (a != B::Action::RECOVER) return a;
        now += b.delayMs();
        b.onRecoveryStarted();
        now += 50 + 80;
    }
}

// The trace of the Powercom (issue #36): a new error 50 ms after every restart. The old
// code restarted the board after 0.4 s; now the escalation waits ESCALATE_AFTER_MS, and the
// first one power-cycles the USB port (issue #163).
void test_continuous_errors_escalate_after_escalate_time(void) {
    B b;
    const uint32_t t0 = 25539219;
    uint32_t now = t0;
    int recoveries = 0;
    for (;;) {
        B::Action a = b.onError(now);
        if (a != B::Action::RECOVER) {
            TEST_ASSERT_TRUE(a == B::Action::CYCLE_PORT);
            break;
        }
        recoveries++;
        TEST_ASSERT_TRUE(now - t0 < B::ESCALATE_AFTER_MS);
        now += b.delayMs();
        b.onRecoveryStarted();
        now += 50 + 80; // pipe start, then the next error
    }
    TEST_ASSERT_TRUE(now - t0 >= B::ESCALATE_AFTER_MS);
    TEST_ASSERT_TRUE(now - t0 < B::ESCALATE_AFTER_MS + B::MAX_DELAY_MS + 200);
    TEST_ASSERT_TRUE(recoveries >= B::MIN_RECOVERIES);
    TEST_ASSERT_FALSE(b.pending());
}

// Issue #163: the IN pipe stuck again after the port cycle restarts the board
void test_second_escalation_within_gap_restarts(void) {
    B b;
    uint32_t now = 1000;
    TEST_ASSERT_TRUE(runUntilEscalation(b, now) == B::Action::CYCLE_PORT);
    b.reset(); // the cycle disconnects the UPS, the new claim resets again
    now += 3000;
    TEST_ASSERT_TRUE(runUntilEscalation(b, now) == B::Action::RESTART);
    TEST_ASSERT_FALSE(b.pending());
}

// A new streak PORT_CYCLE_GAP_MS after the last cycle gets a cycle again
void test_escalation_after_gap_cycles_again(void) {
    B b;
    uint32_t now = 1000;
    TEST_ASSERT_TRUE(runUntilEscalation(b, now) == B::Action::CYCLE_PORT);
    const uint32_t cycled_at = now;
    b.reset();
    now = cycled_at + B::PORT_CYCLE_GAP_MS - B::ESCALATE_AFTER_MS - 5000;
    TEST_ASSERT_TRUE(runUntilEscalation(b, now) == B::Action::RESTART);
    TEST_ASSERT_TRUE(now - cycled_at < B::PORT_CYCLE_GAP_MS);
    b.reset();
    now = cycled_at + B::PORT_CYCLE_GAP_MS;
    TEST_ASSERT_TRUE(runUntilEscalation(b, now) == B::Action::CYCLE_PORT);
}

void test_cycle_gap_across_millis_wrap(void) {
    B b;
    uint32_t now = 0xFFFFFFFFu - 2000;
    TEST_ASSERT_TRUE(runUntilEscalation(b, now) == B::Action::CYCLE_PORT); // past the wrap
    b.reset();
    now += 1000;
    TEST_ASSERT_TRUE(runUntilEscalation(b, now) == B::Action::RESTART);
}

void test_restart_needs_min_recoveries(void) {
    B b;
    // Two errors far apart in time but closer than QUIET_RESET_MS
    TEST_ASSERT_TRUE(b.onError(1000) == B::Action::RECOVER);
    b.onRecoveryStarted();
    TEST_ASSERT_TRUE(b.onError(1000 + B::ESCALATE_AFTER_MS + 1) == B::Action::RECOVER);
    b.onRecoveryStarted();
    TEST_ASSERT_TRUE(b.onError(1000 + B::ESCALATE_AFTER_MS + 2) == B::Action::RECOVER);
    b.onRecoveryStarted();
    TEST_ASSERT_EQUAL_UINT8(B::MIN_RECOVERIES, b.recoveries());
    TEST_ASSERT_TRUE(b.onError(1000 + B::ESCALATE_AFTER_MS + 3) == B::Action::CYCLE_PORT);
}

void test_reset_ends_the_streak(void) {
    B b;
    uint32_t now = 1000;
    for (int i = 0; i < 5; i++) now = errorAndRecover(b, now) + 50;
    b.reset(); // INPUT report, claim or disconnection
    TEST_ASSERT_FALSE(b.pending());
    TEST_ASSERT_EQUAL_UINT8(0, b.recoveries());
    now += B::ESCALATE_AFTER_MS;
    TEST_ASSERT_TRUE(b.onError(now) == B::Action::RECOVER);
    TEST_ASSERT_EQUAL_UINT32(B::FIRST_DELAY_MS, b.delayMs());
    TEST_ASSERT_EQUAL_UINT8(1, b.recoveries());
}

void test_disconnection_while_pending_clears_it(void) {
    B b;
    TEST_ASSERT_TRUE(b.onError(1000) == B::Action::RECOVER);
    TEST_ASSERT_TRUE(b.pending());
    b.reset();
    TEST_ASSERT_FALSE(b.pending());
    TEST_ASSERT_FALSE(b.due(1000 + B::FIRST_DELAY_MS));
}

void test_input_ends_the_streak(void) {
    B b;
    uint32_t now = 1000;
    for (int i = 0; i < 4; i++) now = errorAndRecover(b, now) + 50;
    b.onInput();
    TEST_ASSERT_EQUAL_UINT8(0, b.recoveries());
    TEST_ASSERT_TRUE(b.onError(now) == B::Action::RECOVER);
    TEST_ASSERT_EQUAL_UINT32(B::FIRST_DELAY_MS, b.delayMs());
}

void test_input_queued_before_the_error_keeps_the_restart(void) {
    B b;
    TEST_ASSERT_TRUE(b.onError(1000) == B::Action::RECOVER);
    b.onInput(); // INPUT event dequeued after the error
    TEST_ASSERT_TRUE(b.pending());
    TEST_ASSERT_TRUE(b.due(1000 + B::FIRST_DELAY_MS));
    TEST_ASSERT_EQUAL_UINT8(1, b.recoveries());
}

void test_quiet_period_starts_a_new_streak(void) {
    B b;
    uint32_t now = 1000;
    for (int i = 0; i < 4; i++) now = errorAndRecover(b, now) + 50;
    // No error for QUIET_RESET_MS, and no INPUT report either (UPS without periodic INPUT)
    now += B::QUIET_RESET_MS;
    TEST_ASSERT_TRUE(b.onError(now) == B::Action::RECOVER);
    TEST_ASSERT_EQUAL_UINT32(B::FIRST_DELAY_MS, b.delayMs());
    TEST_ASSERT_EQUAL_UINT8(1, b.recoveries());
    TEST_ASSERT_EQUAL_UINT32(0, b.streakMs(now));
}

void test_sporadic_errors_never_restart(void) {
    B b;
    uint32_t now = 1000;
    for (int i = 0; i < 50; i++) {
        TEST_ASSERT_TRUE(b.onError(now) == B::Action::RECOVER);
        b.onRecoveryStarted();
        now += B::QUIET_RESET_MS + 1;
    }
}

void test_due_across_millis_wrap(void) {
    B b;
    const uint32_t t0 = 0xFFFFFFFFu - 50;
    TEST_ASSERT_TRUE(b.onError(t0) == B::Action::RECOVER);
    TEST_ASSERT_FALSE(b.due(t0 + B::FIRST_DELAY_MS - 1));
    TEST_ASSERT_TRUE(b.due(t0 + B::FIRST_DELAY_MS));
    b.onRecoveryStarted();
    TEST_ASSERT_EQUAL_UINT32(1000, b.streakMs(t0 + 1000));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_by_default);
    RUN_TEST(test_first_restart_waits_first_delay);
    RUN_TEST(test_delay_doubles_up_to_max);
    RUN_TEST(test_continuous_errors_escalate_after_escalate_time);
    RUN_TEST(test_second_escalation_within_gap_restarts);
    RUN_TEST(test_escalation_after_gap_cycles_again);
    RUN_TEST(test_cycle_gap_across_millis_wrap);
    RUN_TEST(test_restart_needs_min_recoveries);
    RUN_TEST(test_reset_ends_the_streak);
    RUN_TEST(test_disconnection_while_pending_clears_it);
    RUN_TEST(test_input_ends_the_streak);
    RUN_TEST(test_input_queued_before_the_error_keeps_the_restart);
    RUN_TEST(test_quiet_period_starts_a_new_streak);
    RUN_TEST(test_sporadic_errors_never_restart);
    RUN_TEST(test_due_across_millis_wrap);
    return UNITY_END();
}
