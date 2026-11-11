#include <unity.h>
#include "InputSilenceGuard.h"

// Issue #138: a Back-UPS BX whose interrupt pipe goes silent (also right after a USB port
// power cycle) answers GET_REPORT with frozen values. A system restart frees it, an
// interface restart does not: the guard asks for the system restart, once.

typedef InputSilenceGuard G;
typedef InputSilenceGuard::Action A;

static const uint32_t SILENCE = 15000;

void setUp(void) {}
void tearDown(void) {}

static void armed(G& g, uint32_t at) {
    g.reset(0);
    g.setSilenceMs(SILENCE);
    g.onInput(at);
}

void test_off_by_default(void) {
    G g;
    TEST_ASSERT_FALSE(g.isStale(0)); // no report yet, but off
    g.onInput(1000);
    TEST_ASSERT_FALSE(g.isSilent(1000000));
    TEST_ASSERT_FALSE(g.isStale(1000000));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(1000000));
}

// The log of 2026-11-10: after the port cycle at 00:56:44 the BX750MI enumerated, answered
// every GET_REPORT, and never sent another INPUT report until it was unplugged at 08:38
void test_claim_counts_as_last_input(void) {
    G g;
    g.setSilenceMs(SILENCE);
    g.reset(1000);
    TEST_ASSERT_FALSE(g.seenInput());
    // Stale from the claim: a UPS that sends nothing is never healthy for RestartPolicy
    TEST_ASSERT_TRUE(g.isStale(1000));
    TEST_ASSERT_FALSE(g.isSilent(1000 + SILENCE - 1));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(1000 + SILENCE - 1));
    TEST_ASSERT_EQUAL(A::RESTART, g.tick(1000 + SILENCE));
}

void test_first_input_makes_fresh(void) {
    G g;
    g.setSilenceMs(SILENCE);
    g.reset(1000);
    g.onInput(1050);
    TEST_ASSERT_TRUE(g.seenInput());
    TEST_ASSERT_FALSE(g.isStale(1060));
    TEST_ASSERT_TRUE(g.isStale(1050 + SILENCE));
}

void test_silent_at_threshold(void) {
    G g;
    armed(g, 5000);
    TEST_ASSERT_FALSE(g.isSilent(5000 + SILENCE - 1));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(5000 + SILENCE - 1));
    TEST_ASSERT_TRUE(g.isSilent(5000 + SILENCE));
    TEST_ASSERT_EQUAL_UINT32(SILENCE, g.silentForMs(5000 + SILENCE));
}

void test_restart_asked_once(void) {
    G g;
    armed(g, 0);
    TEST_ASSERT_EQUAL(A::RESTART, g.tick(SILENCE));
    // Asked once: the restart request stays pending in the owner
    TEST_ASSERT_EQUAL(A::NONE, g.tick(SILENCE + 10));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(SILENCE + 600000));
    TEST_ASSERT_TRUE(g.isSilent(SILENCE + 600000));
    TEST_ASSERT_TRUE(g.isStale(SILENCE + 600000));
}

void test_input_rearms(void) {
    G g;
    armed(g, 0);
    TEST_ASSERT_EQUAL(A::RESTART, g.tick(SILENCE));
    g.onInput(SILENCE + 1500);
    TEST_ASSERT_FALSE(g.isSilent(SILENCE + 1500));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(2 * SILENCE + 1499));
    // A later silence asks again
    TEST_ASSERT_EQUAL(A::RESTART, g.tick(2 * SILENCE + 1500));
}

void test_reset_starts_over(void) {
    G g;
    armed(g, 0);
    TEST_ASSERT_EQUAL(A::RESTART, g.tick(SILENCE));
    uint32_t claim = SILENCE + 100; // claim after a re-enumeration
    g.reset(claim);
    TEST_ASSERT_FALSE(g.isSilent(claim + SILENCE - 1));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(claim + SILENCE - 1));
    TEST_ASSERT_EQUAL(A::RESTART, g.tick(claim + SILENCE));
}

void test_turned_off_while_silent(void) {
    G g;
    armed(g, 0);
    g.setSilenceMs(0); // e.g. setup() of the driver, before the model is read again
    TEST_ASSERT_FALSE(g.isSilent(SILENCE));
    TEST_ASSERT_FALSE(g.isStale(SILENCE));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(SILENCE));
    g.setSilenceMs(SILENCE);
    TEST_ASSERT_EQUAL(A::RESTART, g.tick(SILENCE + 1));
}

void test_report_stamped_after_now(void) {
    G g;
    armed(g, 100000);
    // The HID task stamped the report after the poll task read millis()
    TEST_ASSERT_FALSE(g.isSilent(99990));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(99990));
}

void test_millis_wrap(void) {
    G g;
    armed(g, 0xFFFFF000u);
    TEST_ASSERT_FALSE(g.isSilent(0x00001000u));
    uint32_t at = 0xFFFFF000u + SILENCE; // wraps
    TEST_ASSERT_EQUAL(A::NONE, g.tick(at - 1));
    TEST_ASSERT_EQUAL(A::RESTART, g.tick(at));
}

// Tim's case: 18 reports per second for an hour, then nothing while GET_REPORT keeps working
void test_issue138_timeline(void) {
    G g;
    g.reset(0);
    g.setSilenceMs(SILENCE);
    uint32_t t = 0;
    for (; t < 3600000u; t += 55) {
        g.onInput(t);
        TEST_ASSERT_EQUAL(A::NONE, g.tick(t));
        TEST_ASSERT_FALSE(g.isStale(t));
    }
    uint32_t last = t - 55;
    uint32_t restartAt = 0;
    int restarts = 0;
    for (uint32_t now = last; now < last + 5 * 60000u; now += 10) {
        if (g.tick(now) == A::RESTART) {
            restarts++;
            restartAt = now;
        }
    }
    TEST_ASSERT_EQUAL(1, restarts);
    TEST_ASSERT_EQUAL_UINT32(last + SILENCE, restartAt);
}

// bx-frozen-v2 log of 2026-11-11 (08:20): 18 reports per second, a port cycle for a stuck
// control transfer, the UPS enumerates again and sends nothing more. v2 restarted the
// interface after 60 s (no report came back) and the board 60 s later; now the board
// restarts 15 s after the claim, and the data is stale from the claim on.
void test_issue138_after_port_cycle(void) {
    G g;
    g.setSilenceMs(SILENCE);
    g.reset(0);
    uint32_t t = 0;
    for (; t < 3600000u; t += 55) g.onInput(t);
    uint32_t claim = t + 2331; // "UPS back after the USB port power cycle in 2331 ms"
    g.reset(claim);
    TEST_ASSERT_TRUE(g.isStale(claim + 1385)); // "First full poll done after 1385 ms"
    uint32_t restartAt = 0;
    for (uint32_t now = claim; now < claim + 10 * 60000u; now += 10) {
        if (g.tick(now) == A::RESTART) restartAt = now;
        TEST_ASSERT_TRUE(g.isStale(now));
    }
    TEST_ASSERT_EQUAL_UINT32(claim + SILENCE, restartAt);
}

// The other port cycles of the same log: the reports come back right after the claim
// (1125-1133 in the minute of the cycle), the guard does nothing
void test_port_cycle_with_reports_back(void) {
    G g;
    g.setSilenceMs(SILENCE);
    g.reset(0);
    uint32_t t = 0;
    for (; t < 600000u; t += 55) g.onInput(t);
    uint32_t claim = t + 2283;
    g.reset(claim);
    TEST_ASSERT_TRUE(g.isStale(claim));
    for (uint32_t now = claim + 200; now < claim + 600000u; now += 55) {
        g.onInput(now);
        TEST_ASSERT_EQUAL(A::NONE, g.tick(now));
        TEST_ASSERT_FALSE(g.isStale(now));
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_off_by_default);
    RUN_TEST(test_claim_counts_as_last_input);
    RUN_TEST(test_first_input_makes_fresh);
    RUN_TEST(test_silent_at_threshold);
    RUN_TEST(test_restart_asked_once);
    RUN_TEST(test_input_rearms);
    RUN_TEST(test_reset_starts_over);
    RUN_TEST(test_turned_off_while_silent);
    RUN_TEST(test_report_stamped_after_now);
    RUN_TEST(test_millis_wrap);
    RUN_TEST(test_issue138_timeline);
    RUN_TEST(test_issue138_after_port_cycle);
    RUN_TEST(test_port_cycle_with_reports_back);
    return UNITY_END();
}
