#include <unity.h>
#include <stdint.h>
#include "NetAutoSwitch.h"

// Switch between Ethernet and Wi-Fi in the AUTO link mode (US-127): 10 s without the
// cable, 5 s with it back, 15 s of hold after every switch, millis() wrap

void setUp(void) {}
void tearDown(void) {}

void test_ethernet_lost_switches_to_wifi_after_10_seconds(void) {
    NetAutoSwitch s;
    s.begin(0, AutoLink::ETHERNET, true);
    TEST_ASSERT_TRUE(s.step(19000, true) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(20000, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(29900, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(30100, false) == AutoAction::TO_WIFI);
}

void test_without_wifi_configured_never_leaves_ethernet(void) {
    NetAutoSwitch s;
    s.begin(0, AutoLink::ETHERNET, false);
    for (uint32_t t = 20000; t <= 200000; t += 10000) {
        TEST_ASSERT_TRUE(s.step(t, false) == AutoAction::NONE);
    }
}

void test_ethernet_back_switches_from_wifi_after_5_seconds(void) {
    NetAutoSwitch s;
    s.begin(0, AutoLink::WIFI, true);
    TEST_ASSERT_TRUE(s.step(39000, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(40000, true) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(44900, true) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(45100, true) == AutoAction::TO_ETHERNET);
}

void test_no_switch_within_15_seconds_of_a_switch(void) {
    NetAutoSwitch s;
    s.begin(0, AutoLink::ETHERNET, true);
    s.switched(100000, AutoLink::WIFI);
    TEST_ASSERT_TRUE(s.active() == AutoLink::WIFI);
    // The cable is already back: the 5 s are long over, the hold is not
    TEST_ASSERT_TRUE(s.step(100000, true) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(105100, true) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(114900, true) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(115100, true) == AutoAction::TO_ETHERNET);
}

void test_no_switch_within_15_seconds_of_begin(void) {
    NetAutoSwitch s;
    s.begin(1000, AutoLink::ETHERNET, true);
    TEST_ASSERT_TRUE(s.step(1000, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(15900, false) == AutoAction::NONE);  // 14.9 s: hold
    TEST_ASSERT_TRUE(s.step(16100, false) == AutoAction::TO_WIFI);
}

void test_short_interruption_restarts_the_wait(void) {
    NetAutoSwitch s;
    s.begin(0, AutoLink::WIFI, true);
    TEST_ASSERT_TRUE(s.step(20000, true) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(23000, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(26000, true) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(30900, true) == AutoAction::NONE);  // 4.9 s since it came back
    TEST_ASSERT_TRUE(s.step(31100, true) == AutoAction::TO_ETHERNET);

    // Same on Ethernet: a cable back for a moment restarts the 10 s
    s.begin(0, AutoLink::ETHERNET, true);
    TEST_ASSERT_TRUE(s.step(20000, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(28000, true) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(29000, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(38900, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(39100, false) == AutoAction::TO_WIFI);
}

void test_worst_case_stays_within_30_seconds(void) {
    // Cable unplugged 1 s after a switch back to Ethernet: the hold is still running
    NetAutoSwitch s;
    s.begin(0, AutoLink::WIFI, true);
    s.switched(50000, AutoLink::ETHERNET);
    const uint32_t unplugged = 51000;
    uint32_t at = 0;
    for (uint32_t t = unplugged; t <= unplugged + 30000; t += 100) {
        if (s.step(t, false) == AutoAction::TO_WIFI) {
            at = t;
            break;
        }
    }
    TEST_ASSERT_NOT_EQUAL(0, at);
    TEST_ASSERT_TRUE(at - unplugged <= 15000);
    TEST_ASSERT_TRUE(at - unplugged < 30000);
}

void test_millis_wrap(void) {
    NetAutoSwitch s;
    const uint32_t start = UINT32_MAX - 20000;
    s.begin(start, AutoLink::ETHERNET, true);
    TEST_ASSERT_TRUE(s.step(start + 15000, true) == AutoAction::NONE);
    const uint32_t lost = UINT32_MAX - 1000;
    TEST_ASSERT_TRUE(s.step(lost, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step((uint32_t)(lost + 9900), false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step((uint32_t)(lost + 10100), false) == AutoAction::TO_WIFI);
}

void test_step_does_not_change_the_active_link(void) {
    NetAutoSwitch s;
    s.begin(0, AutoLink::ETHERNET, true);
    TEST_ASSERT_TRUE(s.step(20000, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(31000, false) == AutoAction::TO_WIFI);
    TEST_ASSERT_TRUE(s.active() == AutoLink::ETHERNET);
    // Still asked until the manager carries it out
    TEST_ASSERT_TRUE(s.step(32000, false) == AutoAction::TO_WIFI);
    s.switched(32000, AutoLink::WIFI);
    TEST_ASSERT_TRUE(s.active() == AutoLink::WIFI);
    TEST_ASSERT_TRUE(s.step(33000, false) == AutoAction::NONE);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_ethernet_lost_switches_to_wifi_after_10_seconds);
    RUN_TEST(test_without_wifi_configured_never_leaves_ethernet);
    RUN_TEST(test_ethernet_back_switches_from_wifi_after_5_seconds);
    RUN_TEST(test_no_switch_within_15_seconds_of_a_switch);
    RUN_TEST(test_no_switch_within_15_seconds_of_begin);
    RUN_TEST(test_short_interruption_restarts_the_wait);
    RUN_TEST(test_worst_case_stays_within_30_seconds);
    RUN_TEST(test_millis_wrap);
    RUN_TEST(test_step_does_not_change_the_active_link);
    return UNITY_END();
}
