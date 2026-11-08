#include <unity.h>
#include "LinkWaitReminder.h"

// Issue #136: without a cable the log repeats "no Ethernet link" instead of going silent

static const uint32_t FIRST = LinkWaitReminder::FIRST_MS;
static const uint32_t REPEAT = LinkWaitReminder::REPEAT_MS;

void setUp(void) {}
void tearDown(void) {}

void test_first_reminder_after_ten_seconds(void) {
    LinkWaitReminder r;
    r.begin(1000);
    TEST_ASSERT_EQUAL_UINT32(0, r.update(false, 1000));
    TEST_ASSERT_EQUAL_UINT32(0, r.update(false, 1000 + FIRST - 1));
    TEST_ASSERT_EQUAL_UINT32(FIRST / 1000, r.update(false, 1000 + FIRST));
}

void test_repeats_every_minute(void) {
    LinkWaitReminder r;
    r.begin(0);
    TEST_ASSERT_EQUAL_UINT32(10, r.update(false, FIRST));
    TEST_ASSERT_EQUAL_UINT32(0, r.update(false, FIRST + 1));
    TEST_ASSERT_EQUAL_UINT32(0, r.update(false, FIRST + REPEAT - 1));
    TEST_ASSERT_EQUAL_UINT32(70, r.update(false, FIRST + REPEAT));
    TEST_ASSERT_EQUAL_UINT32(0, r.update(false, FIRST + REPEAT + 10));
    TEST_ASSERT_EQUAL_UINT32(130, r.update(false, FIRST + 2 * REPEAT));
}

void test_link_up_never_reminds(void) {
    LinkWaitReminder r;
    r.begin(0);
    for (uint32_t now = 0; now <= 5 * REPEAT; now += 500) {
        TEST_ASSERT_EQUAL_UINT32(0, r.update(true, now));
    }
}

void test_link_up_before_first_reminder_cancels_it(void) {
    LinkWaitReminder r;
    r.begin(0);
    TEST_ASSERT_EQUAL_UINT32(0, r.update(false, 3000));
    TEST_ASSERT_EQUAL_UINT32(0, r.update(true, 3500));
    TEST_ASSERT_EQUAL_UINT32(0, r.update(true, 2 * FIRST));
}

// Cable pulled later: a new wait, counted from the loss and with its own first reminder
void test_link_lost_starts_a_new_wait(void) {
    LinkWaitReminder r;
    r.begin(0);
    TEST_ASSERT_EQUAL_UINT32(10, r.update(false, FIRST));
    TEST_ASSERT_EQUAL_UINT32(0, r.update(true, 20000));
    const uint32_t lost = 500000;
    TEST_ASSERT_EQUAL_UINT32(0, r.update(false, lost));
    TEST_ASSERT_EQUAL_UINT32(0, r.update(false, lost + FIRST - 1));
    TEST_ASSERT_EQUAL_UINT32(10, r.update(false, lost + FIRST));
}

void test_correct_across_millis_wraparound(void) {
    LinkWaitReminder r;
    const uint32_t start = 0xFFFFFFFFu - 4000;  // wraps 4 s after the start
    r.begin(start);
    TEST_ASSERT_EQUAL_UINT32(0, r.update(false, start + FIRST - 1));
    TEST_ASSERT_EQUAL_UINT32(10, r.update(false, start + FIRST));
    TEST_ASSERT_EQUAL_UINT32(70, r.update(false, start + FIRST + REPEAT));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_reminder_after_ten_seconds);
    RUN_TEST(test_repeats_every_minute);
    RUN_TEST(test_link_up_never_reminds);
    RUN_TEST(test_link_up_before_first_reminder_cancels_it);
    RUN_TEST(test_link_lost_starts_a_new_wait);
    RUN_TEST(test_correct_across_millis_wraparound);
    return UNITY_END();
}
