#include <unity.h>
#include <stdint.h>
#include "RecoveryWindow.h"

// Recovery window of the web credentials on boards without Wi-Fi (US-091)

static const uint32_t TEN_MIN = 600000;

void setUp(void) {}
void tearDown(void) {}

void test_closed_by_default(void) {
    RecoveryWindow w;
    TEST_ASSERT_FALSE(w.isOpen(0));
    TEST_ASSERT_FALSE(w.isOpen(123456));
    TEST_ASSERT_EQUAL_UINT32(0, w.remainingMs(1000));
    TEST_ASSERT_TRUE(w.update(1000) == RecoveryWindow::Event::NONE);
}

void test_open_for_ten_minutes(void) {
    RecoveryWindow w;
    TEST_ASSERT_EQUAL_UINT32(TEN_MIN, w.durationMs());
    w.open(5000);
    TEST_ASSERT_TRUE(w.isOpen(5000));
    TEST_ASSERT_TRUE(w.isOpen(5000 + TEN_MIN - 1));
    TEST_ASSERT_FALSE(w.isOpen(5000 + TEN_MIN));
}

void test_expiry_reported_once(void) {
    RecoveryWindow w;
    w.open(0);
    TEST_ASSERT_TRUE(w.update(TEN_MIN - 1) == RecoveryWindow::Event::NONE);
    TEST_ASSERT_TRUE(w.update(TEN_MIN) == RecoveryWindow::Event::EXPIRED);
    TEST_ASSERT_TRUE(w.update(TEN_MIN + 1) == RecoveryWindow::Event::NONE);
    TEST_ASSERT_FALSE(w.isOpen(TEN_MIN + 1));
}

void test_used_closes_without_expiry(void) {
    RecoveryWindow w;
    w.open(0);
    w.markUsed();
    TEST_ASSERT_FALSE(w.isOpen(1));
    TEST_ASSERT_EQUAL_UINT32(0, w.remainingMs(1));
    TEST_ASSERT_TRUE(w.update(TEN_MIN) == RecoveryWindow::Event::NONE);
}

void test_across_millis_wrap(void) {
    RecoveryWindow w;
    const uint32_t t0 = UINT32_MAX - 1000;
    w.open(t0);
    TEST_ASSERT_TRUE(w.isOpen(t0 + 5000)); // wrapped past zero
    TEST_ASSERT_TRUE(w.isOpen(t0 + TEN_MIN - 1));
    TEST_ASSERT_FALSE(w.isOpen(t0 + TEN_MIN));
}

void test_remaining_time_decreases(void) {
    RecoveryWindow w;
    w.open(1000);
    TEST_ASSERT_EQUAL_UINT32(TEN_MIN, w.remainingMs(1000));
    TEST_ASSERT_EQUAL_UINT32(TEN_MIN - 60000, w.remainingMs(61000));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_closed_by_default);
    RUN_TEST(test_open_for_ten_minutes);
    RUN_TEST(test_expiry_reported_once);
    RUN_TEST(test_used_closes_without_expiry);
    RUN_TEST(test_across_millis_wrap);
    RUN_TEST(test_remaining_time_decreases);
    return UNITY_END();
}
