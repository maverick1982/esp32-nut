#include <unity.h>
#include "ApIdleTimeout.h"

// US-064: the manual AP mode restarts in STA after 10 minutes without stations

static const uint32_t T = ApIdleTimeout::DEFAULT_TIMEOUT_MS;

void setUp(void) {}
void tearDown(void) {}

void test_disabled_never_expires(void) {
    ApIdleTimeout t(false);
    t.begin(1000);
    TEST_ASSERT_FALSE(t.isEnabled());
    TEST_ASSERT_FALSE(t.update(0, 1000 + T));
    TEST_ASSERT_FALSE(t.update(0, 1000 + 10 * T));
    TEST_ASSERT_EQUAL_UINT32(0, t.msRemaining(1000));
}

void test_no_station_expires_after_ten_minutes(void) {
    ApIdleTimeout t(true);
    t.begin(5000);
    TEST_ASSERT_TRUE(t.isEnabled());
    TEST_ASSERT_FALSE(t.update(0, 5000 + T - 1));
    TEST_ASSERT_TRUE(t.update(0, 5000 + T));
}

void test_expiry_reported_once(void) {
    ApIdleTimeout t(true);
    t.begin(0);
    TEST_ASSERT_TRUE(t.update(0, T));
    TEST_ASSERT_FALSE(t.update(0, T + 1));
    TEST_ASSERT_FALSE(t.update(0, 3 * T));
}

void test_connected_station_keeps_ap_up(void) {
    ApIdleTimeout t(true);
    t.begin(0);
    for (uint32_t now = 0; now <= 3 * T; now += 1000) {
        TEST_ASSERT_FALSE(t.update(1, now));
    }
}

// The count starts from the last update that still saw a station (in the loop, ~10 ms
// before the first update without one)
void test_count_restarts_when_last_station_leaves(void) {
    ApIdleTimeout t(true);
    t.begin(0);
    const uint32_t last_seen = 2 * T;
    TEST_ASSERT_FALSE(t.update(2, last_seen - 1000));
    TEST_ASSERT_FALSE(t.update(1, last_seen));
    TEST_ASSERT_FALSE(t.update(0, last_seen + 10));
    TEST_ASSERT_FALSE(t.update(0, last_seen + T - 1));
    TEST_ASSERT_TRUE(t.update(0, last_seen + T));
}

void test_reconnect_midway_resets_count(void) {
    ApIdleTimeout t(true);
    t.begin(0);
    TEST_ASSERT_FALSE(t.update(0, T / 2));
    const uint32_t last_seen = T / 2 + 1000;
    TEST_ASSERT_FALSE(t.update(1, last_seen));     // a phone joins, then leaves
    TEST_ASSERT_FALSE(t.update(0, last_seen + 10));
    TEST_ASSERT_FALSE(t.update(0, T));             // 10 min from begin are not enough
    TEST_ASSERT_FALSE(t.update(0, last_seen + T - 1));
    TEST_ASSERT_TRUE(t.update(0, last_seen + T));
}

void test_millis_wraparound(void) {
    ApIdleTimeout t(true);
    const uint32_t start = UINT32_MAX - 1000;
    t.begin(start);
    TEST_ASSERT_FALSE(t.update(0, start + 2000)); // wrapped
    TEST_ASSERT_FALSE(t.update(0, start + T - 1));
    TEST_ASSERT_TRUE(t.update(0, start + T));
}

void test_ms_remaining(void) {
    ApIdleTimeout t(true);
    t.begin(100);
    TEST_ASSERT_EQUAL_UINT32(T, t.msRemaining(100));
    TEST_ASSERT_EQUAL_UINT32(T - 60000, t.msRemaining(60100));
    TEST_ASSERT_EQUAL_UINT32(0, t.msRemaining(100 + T));
    TEST_ASSERT_TRUE(t.update(0, 100 + T));
    TEST_ASSERT_EQUAL_UINT32(0, t.msRemaining(100 + T + 1));
}

void test_custom_timeout(void) {
    ApIdleTimeout t(true, 5000);
    t.begin(0);
    TEST_ASSERT_FALSE(t.update(0, 4999));
    TEST_ASSERT_TRUE(t.update(0, 5000));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_disabled_never_expires);
    RUN_TEST(test_no_station_expires_after_ten_minutes);
    RUN_TEST(test_expiry_reported_once);
    RUN_TEST(test_connected_station_keeps_ap_up);
    RUN_TEST(test_count_restarts_when_last_station_leaves);
    RUN_TEST(test_reconnect_midway_resets_count);
    RUN_TEST(test_millis_wraparound);
    RUN_TEST(test_ms_remaining);
    RUN_TEST(test_custom_timeout);
    return UNITY_END();
}
