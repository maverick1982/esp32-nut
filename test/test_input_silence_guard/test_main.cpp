#include <unity.h>
#include "InputSilenceGuard.h"

// Issue #138: a Back-UPS BX whose interrupt pipe goes silent answers GET_REPORT with
// frozen values. One interface restart, then a system restart a minute later.

typedef InputSilenceGuard G;
typedef InputSilenceGuard::Action A;

static const uint32_t SILENCE = 60000;

void setUp(void) {}
void tearDown(void) {}

static void armed(G& g, uint32_t at) {
    g.reset(0);
    g.setSilenceMs(SILENCE);
    g.onInput(at);
}

void test_off_by_default(void) {
    G g;
    g.onInput(1000);
    TEST_ASSERT_FALSE(g.isSilent(1000000));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(1000000));
}

void test_unarmed_until_first_input(void) {
    G g;
    g.setSilenceMs(SILENCE);
    g.reset(0);
    // A UPS that never sent an INPUT report since the claim (e.g. interrupt pipe disabled)
    TEST_ASSERT_FALSE(g.isSilent(10 * SILENCE));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(10 * SILENCE));
}

void test_silent_at_threshold(void) {
    G g;
    armed(g, 5000);
    TEST_ASSERT_FALSE(g.isSilent(5000 + SILENCE - 1));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(5000 + SILENCE - 1));
    TEST_ASSERT_TRUE(g.isSilent(5000 + SILENCE));
    TEST_ASSERT_EQUAL_UINT32(SILENCE, g.silentForMs(5000 + SILENCE));
}

void test_recover_then_restart(void) {
    G g;
    armed(g, 0);
    TEST_ASSERT_EQUAL(A::RECOVER, g.tick(SILENCE));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(SILENCE + 10));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(SILENCE + G::RETRY_MS - 1));
    TEST_ASSERT_EQUAL(A::RESTART, g.tick(SILENCE + G::RETRY_MS));
    // Asked once: the restart request stays pending in the owner
    TEST_ASSERT_EQUAL(A::NONE, g.tick(SILENCE + 5 * G::RETRY_MS));
    TEST_ASSERT_TRUE(g.isSilent(SILENCE + 5 * G::RETRY_MS));
}

void test_input_after_recovery_clears(void) {
    G g;
    armed(g, 0);
    TEST_ASSERT_EQUAL(A::RECOVER, g.tick(SILENCE));
    g.onInput(SILENCE + 1500); // the interface restart helped
    TEST_ASSERT_FALSE(g.isSilent(SILENCE + 1500));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(SILENCE + G::RETRY_MS));
    // A later silence starts again from the interface restart
    TEST_ASSERT_EQUAL(A::RECOVER, g.tick(2 * SILENCE + 1500));
}

void test_reset_disarms(void) {
    G g;
    armed(g, 0);
    TEST_ASSERT_EQUAL(A::RECOVER, g.tick(SILENCE));
    g.reset(SILENCE + 100); // claim after a re-enumeration
    TEST_ASSERT_FALSE(g.isSilent(10 * SILENCE));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(10 * SILENCE));
}

void test_turned_off_while_silent(void) {
    G g;
    armed(g, 0);
    g.setSilenceMs(0); // e.g. setup() of the driver after the interface restart
    TEST_ASSERT_FALSE(g.isSilent(SILENCE));
    TEST_ASSERT_EQUAL(A::NONE, g.tick(SILENCE));
    g.setSilenceMs(SILENCE);
    TEST_ASSERT_EQUAL(A::RECOVER, g.tick(SILENCE + 1));
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
    TEST_ASSERT_EQUAL(A::RECOVER, g.tick(at));
    TEST_ASSERT_EQUAL(A::RESTART, g.tick(at + G::RETRY_MS));
}

// Tim's case: 18 reports per second, then nothing while GET_REPORT keeps working
void test_issue138_timeline(void) {
    G g;
    g.reset(0);
    g.setSilenceMs(SILENCE);
    uint32_t t = 0;
    for (; t < 3600000u; t += 55) {
        g.onInput(t);
        TEST_ASSERT_EQUAL(A::NONE, g.tick(t));
    }
    uint32_t last = t - 55;
    uint32_t now = last;
    int recovers = 0, restarts = 0;
    for (; now < last + 5 * 60000u; now += 10) {
        A a = g.tick(now);
        if (a == A::RECOVER) recovers++;
        if (a == A::RESTART) restarts++;
    }
    TEST_ASSERT_EQUAL(1, recovers);
    TEST_ASSERT_EQUAL(1, restarts);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_off_by_default);
    RUN_TEST(test_unarmed_until_first_input);
    RUN_TEST(test_silent_at_threshold);
    RUN_TEST(test_recover_then_restart);
    RUN_TEST(test_input_after_recovery_clears);
    RUN_TEST(test_reset_disarms);
    RUN_TEST(test_turned_off_while_silent);
    RUN_TEST(test_report_stamped_after_now);
    RUN_TEST(test_millis_wrap);
    RUN_TEST(test_issue138_timeline);
    return UNITY_END();
}
