#include <unity.h>
#include "DescriptorRetry.h"

// Issue #97: a report descriptor that does not arrive at the first read is read again

void setUp(void) {}
void tearDown(void) {}

void test_idle_by_default(void) {
    DescriptorRetry r;
    TEST_ASSERT_FALSE(r.pending());
    TEST_ASSERT_FALSE(r.due(0));
    TEST_ASSERT_FALSE(r.due(100000));
}

void test_retry_after_delay(void) {
    DescriptorRetry r;
    const uint32_t t0 = 5000;
    TEST_ASSERT_TRUE(r.onFailure(t0));
    TEST_ASSERT_TRUE(r.pending());
    TEST_ASSERT_FALSE(r.due(t0));
    TEST_ASSERT_FALSE(r.due(t0 + DescriptorRetry::RETRY_DELAY_MS - 1));
    TEST_ASSERT_TRUE(r.due(t0 + DescriptorRetry::RETRY_DELAY_MS));
}

void test_gives_up_after_max_attempts(void) {
    DescriptorRetry r;
    uint32_t now = 1000;
    // The first read (CONNECTED event) and the retries but the last one leave another try
    for (uint8_t i = 1; i < DescriptorRetry::MAX_ATTEMPTS; i++) {
        TEST_ASSERT_TRUE(r.onFailure(now));
        TEST_ASSERT_EQUAL_UINT8(i, r.attempts());
        now += DescriptorRetry::RETRY_DELAY_MS;
        TEST_ASSERT_TRUE(r.due(now));
    }
    TEST_ASSERT_FALSE(r.onFailure(now));
    TEST_ASSERT_FALSE(r.pending());
    TEST_ASSERT_FALSE(r.due(now + 100000));
}

void test_clear_restarts_the_count(void) {
    DescriptorRetry r;
    TEST_ASSERT_TRUE(r.onFailure(1000));
    TEST_ASSERT_TRUE(r.onFailure(3000));
    r.clear();
    TEST_ASSERT_FALSE(r.pending());
    TEST_ASSERT_EQUAL_UINT8(0, r.attempts());
    // A new interface gets every attempt again
    for (uint8_t i = 1; i < DescriptorRetry::MAX_ATTEMPTS; i++) {
        TEST_ASSERT_TRUE(r.onFailure(10000));
    }
    TEST_ASSERT_FALSE(r.onFailure(10000));
}

void test_due_across_millis_wrap(void) {
    DescriptorRetry r;
    const uint32_t t0 = 0xFFFFFFFFu - 500;
    TEST_ASSERT_TRUE(r.onFailure(t0));
    TEST_ASSERT_FALSE(r.due(t0 + 1000));
    TEST_ASSERT_TRUE(r.due(t0 + DescriptorRetry::RETRY_DELAY_MS));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_by_default);
    RUN_TEST(test_retry_after_delay);
    RUN_TEST(test_gives_up_after_max_attempts);
    RUN_TEST(test_clear_restarts_the_count);
    RUN_TEST(test_due_across_millis_wrap);
    return UNITY_END();
}
