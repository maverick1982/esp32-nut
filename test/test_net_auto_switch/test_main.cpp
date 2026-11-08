#include <unity.h>
#include <stdint.h>
#include "NetAutoSwitch.h"

// Switch between Ethernet and Wi-Fi in the AUTO link mode (US-127): 10 s without the
// cable, 5 s with it back, 15 s of hold after every switch, millis() wrap. Since issue #64
// the way back to Ethernet looks at the link alone (the W5500 in standby asks for no
// address), and a cable with link and no address is not tried again until it is replugged

void setUp(void) {}
void tearDown(void) {}

// Cable connected and the board on it with an address, or cable unplugged
static AutoAction up(NetAutoSwitch& s, uint32_t t) { return s.step(t, true, true); }
static AutoAction down(NetAutoSwitch& s, uint32_t t) { return s.step(t, false, false); }

void test_ethernet_lost_switches_to_wifi_after_10_seconds(void) {
    NetAutoSwitch s;
    s.begin(0, AutoLink::ETHERNET, true);
    TEST_ASSERT_TRUE(up(s, 19000) == AutoAction::NONE);
    TEST_ASSERT_TRUE(down(s, 20000) == AutoAction::NONE);
    TEST_ASSERT_TRUE(down(s, 29900) == AutoAction::NONE);
    TEST_ASSERT_TRUE(down(s, 30100) == AutoAction::TO_WIFI);
}

void test_without_wifi_configured_never_leaves_ethernet(void) {
    NetAutoSwitch s;
    s.begin(0, AutoLink::ETHERNET, false);
    for (uint32_t t = 20000; t <= 200000; t += 10000) {
        TEST_ASSERT_TRUE(down(s, t) == AutoAction::NONE);
    }
}

void test_ethernet_back_switches_from_wifi_after_5_seconds(void) {
    NetAutoSwitch s;
    s.begin(0, AutoLink::WIFI, true);
    TEST_ASSERT_TRUE(down(s, 39000) == AutoAction::NONE);
    TEST_ASSERT_TRUE(up(s, 40000) == AutoAction::NONE);
    TEST_ASSERT_TRUE(up(s, 44900) == AutoAction::NONE);
    TEST_ASSERT_TRUE(up(s, 45100) == AutoAction::TO_ETHERNET);
}

void test_on_wifi_the_link_alone_brings_back_ethernet(void) {
    // Issue #64: in standby the W5500 has no DHCP, so it never has an address on Wi-Fi
    NetAutoSwitch s;
    s.begin(0, AutoLink::WIFI, true);
    TEST_ASSERT_TRUE(s.step(40000, true, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(44900, true, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(45100, true, false) == AutoAction::TO_ETHERNET);
}

void test_on_ethernet_the_address_is_still_needed(void) {
    // Link up and no address for more than 10 s: back to Wi-Fi
    NetAutoSwitch s;
    s.begin(0, AutoLink::ETHERNET, true);
    TEST_ASSERT_TRUE(s.step(20000, true, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(29900, true, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(30100, true, false) == AutoAction::TO_WIFI);
}

void test_cable_without_address_waits_for_a_replug(void) {
    NetAutoSwitch s;
    s.begin(0, AutoLink::WIFI, true);
    s.switched(50000, AutoLink::ETHERNET);
    // DHCP never answers on the cable: the board goes back to Wi-Fi with the link up
    uint32_t t = 50000;
    while (s.step(t, true, false) != AutoAction::TO_WIFI) t += 100;
    s.switched(t, AutoLink::WIFI);
    TEST_ASSERT_TRUE(s.waitingForReplug());
    // The link stays up: no bounce back to Ethernet
    for (uint32_t u = t; u <= t + 120000; u += 1000) {
        TEST_ASSERT_TRUE(s.step(u, true, false) == AutoAction::NONE);
    }
    // Unplugged and plugged back in: a new try after the usual 5 s
    const uint32_t replug = t + 121000;
    TEST_ASSERT_TRUE(s.step(replug, false, false) == AutoAction::NONE);
    TEST_ASSERT_FALSE(s.waitingForReplug());
    TEST_ASSERT_TRUE(s.step(replug + 1000, true, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(replug + 6100, true, false) == AutoAction::TO_ETHERNET);
}

void test_cable_without_address_tried_again_after_5_minutes(void) {
    NetAutoSwitch s;
    s.begin(0, AutoLink::ETHERNET, true);
    TEST_ASSERT_TRUE(s.step(20000, true, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(30100, true, false) == AutoAction::TO_WIFI);
    s.switched(30100, AutoLink::WIFI);
    TEST_ASSERT_TRUE(s.waitingForReplug());
    TEST_ASSERT_TRUE(s.step(30100 + NetAutoSwitch::RETRY_MS - 1000, true, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(30100 + NetAutoSwitch::RETRY_MS, true, false) == AutoAction::NONE);
    TEST_ASSERT_FALSE(s.waitingForReplug());
    TEST_ASSERT_TRUE(s.step(30100 + NetAutoSwitch::RETRY_MS + 5100, true, false) == AutoAction::TO_ETHERNET);
}

void test_cable_unplugged_on_ethernet_is_not_an_address_failure(void) {
    NetAutoSwitch s;
    s.begin(0, AutoLink::ETHERNET, true);
    TEST_ASSERT_TRUE(down(s, 20000) == AutoAction::NONE);
    TEST_ASSERT_TRUE(down(s, 30100) == AutoAction::TO_WIFI);
    s.switched(30100, AutoLink::WIFI);
    TEST_ASSERT_FALSE(s.waitingForReplug());
    TEST_ASSERT_TRUE(s.step(50000, true, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(55100, true, false) == AutoAction::TO_ETHERNET);
}

void test_probe_without_address_starts_waiting_for_a_replug(void) {
    // Startup probe: link but no DHCP address, the board starts on Wi-Fi
    NetAutoSwitch s;
    s.begin(0, AutoLink::WIFI, true, true);
    TEST_ASSERT_TRUE(s.waitingForReplug());
    TEST_ASSERT_TRUE(s.step(60000, true, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(61000, false, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(62000, true, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(s.step(67100, true, false) == AutoAction::TO_ETHERNET);

    // Without the flag, or with Ethernet as the start link, nothing is held back
    NetAutoSwitch e;
    e.begin(0, AutoLink::ETHERNET, true, true);
    TEST_ASSERT_FALSE(e.waitingForReplug());
}

void test_no_switch_within_15_seconds_of_a_switch(void) {
    NetAutoSwitch s;
    s.begin(0, AutoLink::ETHERNET, true);
    s.switched(100000, AutoLink::WIFI);
    TEST_ASSERT_TRUE(s.active() == AutoLink::WIFI);
    // The cable is already back: the 5 s are long over, the hold is not
    TEST_ASSERT_TRUE(up(s, 100000) == AutoAction::NONE);
    TEST_ASSERT_TRUE(up(s, 105100) == AutoAction::NONE);
    TEST_ASSERT_TRUE(up(s, 114900) == AutoAction::NONE);
    TEST_ASSERT_TRUE(up(s, 115100) == AutoAction::TO_ETHERNET);
}

void test_no_switch_within_15_seconds_of_begin(void) {
    NetAutoSwitch s;
    s.begin(1000, AutoLink::ETHERNET, true);
    TEST_ASSERT_TRUE(down(s, 1000) == AutoAction::NONE);
    TEST_ASSERT_TRUE(down(s, 15900) == AutoAction::NONE);  // 14.9 s: hold
    TEST_ASSERT_TRUE(down(s, 16100) == AutoAction::TO_WIFI);
}

void test_short_interruption_restarts_the_wait(void) {
    NetAutoSwitch s;
    s.begin(0, AutoLink::WIFI, true);
    TEST_ASSERT_TRUE(up(s, 20000) == AutoAction::NONE);
    TEST_ASSERT_TRUE(down(s, 23000) == AutoAction::NONE);
    TEST_ASSERT_TRUE(up(s, 26000) == AutoAction::NONE);
    TEST_ASSERT_TRUE(up(s, 30900) == AutoAction::NONE);  // 4.9 s since it came back
    TEST_ASSERT_TRUE(up(s, 31100) == AutoAction::TO_ETHERNET);

    // Same on Ethernet: a cable back for a moment restarts the 10 s
    s.begin(0, AutoLink::ETHERNET, true);
    TEST_ASSERT_TRUE(down(s, 20000) == AutoAction::NONE);
    TEST_ASSERT_TRUE(up(s, 28000) == AutoAction::NONE);
    TEST_ASSERT_TRUE(down(s, 29000) == AutoAction::NONE);
    TEST_ASSERT_TRUE(down(s, 38900) == AutoAction::NONE);
    TEST_ASSERT_TRUE(down(s, 39100) == AutoAction::TO_WIFI);
}

void test_worst_case_stays_within_30_seconds(void) {
    // Cable unplugged 1 s after a switch back to Ethernet: the hold is still running
    NetAutoSwitch s;
    s.begin(0, AutoLink::WIFI, true);
    s.switched(50000, AutoLink::ETHERNET);
    const uint32_t unplugged = 51000;
    uint32_t at = 0;
    for (uint32_t t = unplugged; t <= unplugged + 30000; t += 100) {
        if (down(s, t) == AutoAction::TO_WIFI) {
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
    TEST_ASSERT_TRUE(up(s, start + 15000) == AutoAction::NONE);
    const uint32_t lost = UINT32_MAX - 1000;
    TEST_ASSERT_TRUE(down(s, lost) == AutoAction::NONE);
    TEST_ASSERT_TRUE(down(s, (uint32_t)(lost + 9900)) == AutoAction::NONE);
    TEST_ASSERT_TRUE(down(s, (uint32_t)(lost + 10100)) == AutoAction::TO_WIFI);

    // The 5 minutes of a cable without address across the wrap
    NetAutoSwitch w;
    const uint32_t gaveUp = UINT32_MAX - 60000;
    w.begin(gaveUp, AutoLink::WIFI, true, true);
    TEST_ASSERT_TRUE(w.step((uint32_t)(gaveUp + NetAutoSwitch::RETRY_MS - 1000), true, false) == AutoAction::NONE);
    TEST_ASSERT_TRUE(w.waitingForReplug());
    TEST_ASSERT_TRUE(w.step((uint32_t)(gaveUp + NetAutoSwitch::RETRY_MS), true, false) == AutoAction::NONE);
    TEST_ASSERT_FALSE(w.waitingForReplug());
}

void test_step_does_not_change_the_active_link(void) {
    NetAutoSwitch s;
    s.begin(0, AutoLink::ETHERNET, true);
    TEST_ASSERT_TRUE(down(s, 20000) == AutoAction::NONE);
    TEST_ASSERT_TRUE(down(s, 31000) == AutoAction::TO_WIFI);
    TEST_ASSERT_TRUE(s.active() == AutoLink::ETHERNET);
    // Still asked until the manager carries it out
    TEST_ASSERT_TRUE(down(s, 32000) == AutoAction::TO_WIFI);
    s.switched(32000, AutoLink::WIFI);
    TEST_ASSERT_TRUE(s.active() == AutoLink::WIFI);
    TEST_ASSERT_TRUE(down(s, 33000) == AutoAction::NONE);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_ethernet_lost_switches_to_wifi_after_10_seconds);
    RUN_TEST(test_without_wifi_configured_never_leaves_ethernet);
    RUN_TEST(test_ethernet_back_switches_from_wifi_after_5_seconds);
    RUN_TEST(test_on_wifi_the_link_alone_brings_back_ethernet);
    RUN_TEST(test_on_ethernet_the_address_is_still_needed);
    RUN_TEST(test_cable_without_address_waits_for_a_replug);
    RUN_TEST(test_cable_without_address_tried_again_after_5_minutes);
    RUN_TEST(test_cable_unplugged_on_ethernet_is_not_an_address_failure);
    RUN_TEST(test_probe_without_address_starts_waiting_for_a_replug);
    RUN_TEST(test_no_switch_within_15_seconds_of_a_switch);
    RUN_TEST(test_no_switch_within_15_seconds_of_begin);
    RUN_TEST(test_short_interruption_restarts_the_wait);
    RUN_TEST(test_worst_case_stays_within_30_seconds);
    RUN_TEST(test_millis_wrap);
    RUN_TEST(test_step_does_not_change_the_active_link);
    return UNITY_END();
}
