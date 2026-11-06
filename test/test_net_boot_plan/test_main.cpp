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

// --- US-126: W5500 on a Wi-Fi board ---

static NetBootInput s3(bool spi_eth, bool config_valid, bool ssid_set, bool manual) {
    const NetBootInput in = { true, false, spi_eth, config_valid, ssid_set, manual };
    return in;
}

void test_w5500_with_valid_config_tries_ethernet_then_station(void) {
    NetBootPlan p = planNetwork(s3(true, true, true, false));
    TEST_ASSERT_TRUE(p.link == BootLink::ETHERNET);
    TEST_ASSERT_TRUE(p.fallback == BootLink::WIFI_STATION);
    TEST_ASSERT_FALSE(p.default_config);
    TEST_ASSERT_FALSE(p.captive_portal);
    TEST_ASSERT_FALSE(p.recovery_window);
}

void test_w5500_without_config_tries_ethernet_then_ap(void) {
    NetBootPlan p = planNetwork(s3(true, false, false, false));
    TEST_ASSERT_TRUE(p.link == BootLink::ETHERNET);
    TEST_ASSERT_TRUE(p.fallback == BootLink::WIFI_ACCESS_POINT);
    TEST_ASSERT_TRUE(p.default_config);

    // A valid configuration without a Wi-Fi network has no station to fall back to
    p = planNetwork(s3(true, true, false, false));
    TEST_ASSERT_TRUE(p.link == BootLink::ETHERNET);
    TEST_ASSERT_TRUE(p.fallback == BootLink::WIFI_ACCESS_POINT);
    TEST_ASSERT_FALSE(p.default_config);
}

void test_w5500_double_reset_starts_ap(void) {
    NetBootPlan p = planNetwork(s3(true, true, true, true));
    TEST_ASSERT_TRUE(p.link == BootLink::WIFI_ACCESS_POINT);
    TEST_ASSERT_TRUE(p.captive_portal);
    TEST_ASSERT_TRUE(p.ap_idle_timeout);
    TEST_ASSERT_TRUE(p.fallback == BootLink::NONE);

    p = planNetwork(s3(true, false, false, true));
    TEST_ASSERT_TRUE(p.link == BootLink::WIFI_ACCESS_POINT);
    TEST_ASSERT_FALSE(p.ap_idle_timeout);
}

void test_valid_config_without_ssid_and_without_w5500_starts_ap(void) {
    NetBootPlan p = planNetwork(s3(false, true, false, false));
    TEST_ASSERT_TRUE(p.link == BootLink::WIFI_ACCESS_POINT);
    TEST_ASSERT_TRUE(p.captive_portal);
    TEST_ASSERT_FALSE(p.ap_idle_timeout);
    TEST_ASSERT_TRUE(p.fallback == BootLink::NONE);
}

void test_without_w5500_the_plan_has_no_fallback(void) {
    for (int valid = 0; valid < 2; valid++) {
        for (int manual = 0; manual < 2; manual++) {
            TEST_ASSERT_TRUE(planNetwork(WIFI_ONLY, valid, manual).fallback == BootLink::NONE);
            TEST_ASSERT_TRUE(planNetwork(ETHERNET_ONLY, valid, manual).fallback == BootLink::NONE);
        }
    }
}

void test_fallback_to_ap_has_captive_portal_and_no_timeout(void) {
    NetBootPlan p = fallbackPlan(planNetwork(s3(true, false, false, false)));
    TEST_ASSERT_TRUE(p.link == BootLink::WIFI_ACCESS_POINT);
    TEST_ASSERT_TRUE(p.captive_portal);
    TEST_ASSERT_FALSE(p.ap_idle_timeout);
    TEST_ASSERT_FALSE(p.default_config);
    TEST_ASSERT_TRUE(p.fallback == BootLink::NONE);
}

void test_fallback_to_station_has_no_portal(void) {
    NetBootPlan p = fallbackPlan(planNetwork(s3(true, true, true, false)));
    TEST_ASSERT_TRUE(p.link == BootLink::WIFI_STATION);
    TEST_ASSERT_FALSE(p.captive_portal);
    TEST_ASSERT_FALSE(p.ap_idle_timeout);
    TEST_ASSERT_FALSE(p.default_config);
    TEST_ASSERT_TRUE(p.fallback == BootLink::NONE);
}

void test_boards_without_wifi_ignore_the_w5500_input(void) {
    for (int valid = 0; valid < 2; valid++) {
        for (int manual = 0; manual < 2; manual++) {
            const NetBootInput in = { false, true, true, (bool)valid, (bool)valid, (bool)manual };
            NetBootPlan p = planNetwork(in);
            NetBootPlan old = planNetwork(ETHERNET_ONLY, valid, manual);
            TEST_ASSERT_TRUE(p.link == old.link);
            TEST_ASSERT_TRUE(p.fallback == BootLink::NONE);
            TEST_ASSERT_EQUAL(old.recovery_window, p.recovery_window);
            TEST_ASSERT_EQUAL(old.default_config, p.default_config);
            TEST_ASSERT_FALSE(p.captive_portal);
        }
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_w5500_with_valid_config_tries_ethernet_then_station);
    RUN_TEST(test_w5500_without_config_tries_ethernet_then_ap);
    RUN_TEST(test_w5500_double_reset_starts_ap);
    RUN_TEST(test_valid_config_without_ssid_and_without_w5500_starts_ap);
    RUN_TEST(test_without_w5500_the_plan_has_no_fallback);
    RUN_TEST(test_fallback_to_ap_has_captive_portal_and_no_timeout);
    RUN_TEST(test_fallback_to_station_has_no_portal);
    RUN_TEST(test_boards_without_wifi_ignore_the_w5500_input);
    RUN_TEST(test_wifi_valid_config_starts_station);
    RUN_TEST(test_wifi_double_reset_starts_ap_with_timeout);
    RUN_TEST(test_wifi_invalid_config_starts_ap_without_timeout);
    RUN_TEST(test_no_wifi_never_starts_ap);
    RUN_TEST(test_no_wifi_double_reset_asks_recovery_window);
    RUN_TEST(test_board_without_links);
    RUN_TEST(test_default_config_only_without_wifi);
    return UNITY_END();
}
