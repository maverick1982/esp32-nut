#include <unity.h>
#include "NetBootPlan.h"

// Boot network decisions for boards with and without Wi-Fi (US-090, ADR 0010 §3)

static const BoardCaps WIFI_ONLY = { true, false };
static const BoardCaps ETHERNET_ONLY = { false, true };
static const BoardCaps NO_NETWORK = { false, false };

void setUp(void) {}
void tearDown(void) {}

void test_wifi_valid_config_starts_station(void) {
    NetBootPlan p = planNetwork(WIFI_ONLY, true, false);
    TEST_ASSERT_TRUE(p.link == BootLink::WIFI_STATION);
    TEST_ASSERT_FALSE(p.captive_portal);
    TEST_ASSERT_FALSE(p.ap_idle_timeout);
    TEST_ASSERT_FALSE(p.recovery_window);
}

void test_wifi_double_reset_starts_ap_with_timeout(void) {
    // PDR 0001 D7: a normal mode to go back to, so the AP closes after 10 idle minutes
    NetBootPlan p = planNetwork(WIFI_ONLY, true, true);
    TEST_ASSERT_TRUE(p.link == BootLink::WIFI_ACCESS_POINT);
    TEST_ASSERT_TRUE(p.captive_portal);
    TEST_ASSERT_TRUE(p.ap_idle_timeout);
    TEST_ASSERT_FALSE(p.recovery_window);
}

void test_wifi_invalid_config_starts_ap_without_timeout(void) {
    NetBootPlan p = planNetwork(WIFI_ONLY, false, false);
    TEST_ASSERT_TRUE(p.link == BootLink::WIFI_ACCESS_POINT);
    TEST_ASSERT_TRUE(p.captive_portal);
    TEST_ASSERT_FALSE(p.ap_idle_timeout);

    // Double reset without a valid configuration: still nothing to go back to
    p = planNetwork(WIFI_ONLY, false, true);
    TEST_ASSERT_TRUE(p.link == BootLink::WIFI_ACCESS_POINT);
    TEST_ASSERT_FALSE(p.ap_idle_timeout);
}

void test_no_wifi_never_starts_ap(void) {
    for (int valid = 0; valid < 2; valid++) {
        for (int manual = 0; manual < 2; manual++) {
            NetBootPlan p = planNetwork(ETHERNET_ONLY, valid, manual);
            TEST_ASSERT_TRUE(p.link == BootLink::ETHERNET);
            TEST_ASSERT_FALSE(p.captive_portal);
            TEST_ASSERT_FALSE(p.ap_idle_timeout);
        }
    }
}

void test_no_wifi_double_reset_asks_recovery_window(void) {
    // PDR 0002 D5: the double reset opens the credential recovery window (US-091)
    TEST_ASSERT_TRUE(planNetwork(ETHERNET_ONLY, true, true).recovery_window);
    TEST_ASSERT_FALSE(planNetwork(ETHERNET_ONLY, true, false).recovery_window);
}

void test_board_without_links(void) {
    NetBootPlan p = planNetwork(NO_NETWORK, true, false);
    TEST_ASSERT_TRUE(p.link == BootLink::NONE);
    TEST_ASSERT_FALSE(p.captive_portal);
}

void test_default_config_only_without_wifi(void) {
    // US-092: no setup page without Wi-Fi, so the defaults are saved and the board joins
    // the network; with Wi-Fi the access point asks for the configuration as before
    TEST_ASSERT_TRUE(planNetwork(ETHERNET_ONLY, false, false).default_config);
    TEST_ASSERT_TRUE(planNetwork(ETHERNET_ONLY, false, true).default_config);
    TEST_ASSERT_FALSE(planNetwork(ETHERNET_ONLY, true, false).default_config);
    TEST_ASSERT_FALSE(planNetwork(ETHERNET_ONLY, true, true).default_config);
    for (int valid = 0; valid < 2; valid++) {
        for (int manual = 0; manual < 2; manual++) {
            TEST_ASSERT_FALSE(planNetwork(WIFI_ONLY, valid, manual).default_config);
        }
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_wifi_valid_config_starts_station);
    RUN_TEST(test_wifi_double_reset_starts_ap_with_timeout);
    RUN_TEST(test_wifi_invalid_config_starts_ap_without_timeout);
    RUN_TEST(test_no_wifi_never_starts_ap);
    RUN_TEST(test_no_wifi_double_reset_asks_recovery_window);
    RUN_TEST(test_board_without_links);
    RUN_TEST(test_default_config_only_without_wifi);
    return UNITY_END();
}
