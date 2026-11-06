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
    const NetBootInput in = { true, false, spi_eth, config_valid, ssid_set, manual, NetworkMode::AUTO };
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

// --- US-127: link mode (Wi-Fi / Ethernet / Automatic) ---

static const NetworkMode MODES[3] = { NetworkMode::AUTO, NetworkMode::ETHERNET, NetworkMode::WIFI };
static const EthProbeOutcome OUTCOMES[5] = { EthProbeOutcome::WAITING, EthProbeOutcome::CONNECTED,
                                             EthProbeOutcome::NO_CHIP, EthProbeOutcome::NO_LINK,
                                             EthProbeOutcome::NO_IP };

static NetBootInput s3m(NetworkMode mode, bool config_valid, bool ssid_set, bool manual) {
    NetBootInput in = s3(true, config_valid, ssid_set, manual);
    in.mode = mode;
    return in;
}

static bool samePlan(const NetBootPlan& a, const NetBootPlan& b) {
    return a.link == b.link && a.captive_portal == b.captive_portal && a.ap_idle_timeout == b.ap_idle_timeout &&
           a.recovery_window == b.recovery_window && a.default_config == b.default_config &&
           a.fallback == b.fallback && a.auto_switch == b.auto_switch &&
           a.fallback_on_no_link == b.fallback_on_no_link;
}

void test_auto_with_wifi_configured_switches_while_running(void) {
    NetBootPlan p = planNetwork(s3m(NetworkMode::AUTO, true, true, false));
    TEST_ASSERT_TRUE(p.link == BootLink::ETHERNET);
    TEST_ASSERT_TRUE(p.fallback == BootLink::WIFI_STATION);
    TEST_ASSERT_TRUE(p.fallback_on_no_link);
    TEST_ASSERT_TRUE(p.auto_switch);
    TEST_ASSERT_FALSE(p.default_config);
}

void test_auto_without_ssid_falls_back_to_ap_without_switching(void) {
    // Never to or from the access point
    NetBootPlan p = planNetwork(s3m(NetworkMode::AUTO, true, false, false));
    TEST_ASSERT_TRUE(p.link == BootLink::ETHERNET);
    TEST_ASSERT_TRUE(p.fallback == BootLink::WIFI_ACCESS_POINT);
    TEST_ASSERT_TRUE(p.fallback_on_no_link);
    TEST_ASSERT_FALSE(p.auto_switch);

    p = planNetwork(s3m(NetworkMode::AUTO, false, false, false));
    TEST_ASSERT_TRUE(p.fallback == BootLink::WIFI_ACCESS_POINT);
    TEST_ASSERT_TRUE(p.default_config);
    TEST_ASSERT_FALSE(p.auto_switch);
}

void test_ethernet_mode_stays_on_the_cable(void) {
    NetBootPlan p = planNetwork(s3m(NetworkMode::ETHERNET, true, true, false));
    TEST_ASSERT_TRUE(p.link == BootLink::ETHERNET);
    TEST_ASSERT_TRUE(p.fallback == BootLink::WIFI_STATION);  // only for a silent module
    TEST_ASSERT_FALSE(p.fallback_on_no_link);
    TEST_ASSERT_FALSE(p.auto_switch);

    p = planNetwork(s3m(NetworkMode::ETHERNET, false, false, false));
    TEST_ASSERT_TRUE(p.link == BootLink::ETHERNET);
    TEST_ASSERT_TRUE(p.fallback == BootLink::WIFI_ACCESS_POINT);
    TEST_ASSERT_TRUE(p.default_config);
    TEST_ASSERT_FALSE(p.fallback_on_no_link);
    TEST_ASSERT_FALSE(p.auto_switch);
}

void test_wifi_mode_ignores_the_w5500(void) {
    for (int valid = 0; valid < 2; valid++) {
        for (int ssid = 0; ssid < 2; ssid++) {
            for (int manual = 0; manual < 2; manual++) {
                NetBootPlan p = planNetwork(s3m(NetworkMode::WIFI, valid, ssid, manual));
                NetBootPlan without = planNetwork(s3(false, valid, ssid, manual));
                TEST_ASSERT_TRUE(samePlan(p, without));
                TEST_ASSERT_TRUE(p.link != BootLink::ETHERNET);
                TEST_ASSERT_TRUE(p.fallback == BootLink::NONE);
                TEST_ASSERT_FALSE(p.auto_switch);
            }
        }
    }
    TEST_ASSERT_TRUE(planNetwork(s3m(NetworkMode::WIFI, true, true, false)).link == BootLink::WIFI_STATION);
}

void test_double_reset_starts_ap_in_every_mode(void) {
    for (int m = 0; m < 3; m++) {
        NetBootPlan p = planNetwork(s3m(MODES[m], true, true, true));
        TEST_ASSERT_TRUE(p.link == BootLink::WIFI_ACCESS_POINT);
        TEST_ASSERT_TRUE(p.captive_portal);
        TEST_ASSERT_TRUE(p.ap_idle_timeout);
        TEST_ASSERT_TRUE(p.fallback == BootLink::NONE);
        TEST_ASSERT_FALSE(p.auto_switch);
    }
}

void test_needs_fallback_for_every_mode_and_outcome(void) {
    // expected[mode][outcome]: AUTO, ETHERNET, WIFI x WAITING, CONNECTED, NO_CHIP, NO_LINK, NO_IP
    static const bool expected[3][5] = {
        { false, false, true, true, true },
        { false, false, true, false, false },
        { false, false, false, false, false },  // no fallback: the W5500 is not used
    };
    for (int m = 0; m < 3; m++) {
        const NetBootPlan p = planNetwork(s3m(MODES[m], true, true, false));
        for (int o = 0; o < 5; o++) {
            TEST_ASSERT_EQUAL_MESSAGE(expected[m][o], needsFallback(p, OUTCOMES[o]), "mode x outcome");
        }
    }
    // Same rules towards the access point (no Wi-Fi configured)
    const NetBootPlan eth_ap = planNetwork(s3m(NetworkMode::ETHERNET, false, false, false));
    TEST_ASSERT_TRUE(needsFallback(eth_ap, EthProbeOutcome::NO_CHIP));
    TEST_ASSERT_FALSE(needsFallback(eth_ap, EthProbeOutcome::NO_LINK));
    const NetBootPlan auto_ap = planNetwork(s3m(NetworkMode::AUTO, false, false, false));
    TEST_ASSERT_TRUE(needsFallback(auto_ap, EthProbeOutcome::NO_IP));
}

void test_fallback_plan_keeps_auto_switch_only_towards_the_station(void) {
    NetBootPlan p = fallbackPlan(planNetwork(s3m(NetworkMode::AUTO, true, true, false)));
    TEST_ASSERT_TRUE(p.link == BootLink::WIFI_STATION);
    TEST_ASSERT_TRUE(p.auto_switch);
    TEST_ASSERT_FALSE(p.fallback_on_no_link);
    TEST_ASSERT_TRUE(p.fallback == BootLink::NONE);

    p = fallbackPlan(planNetwork(s3m(NetworkMode::AUTO, true, false, false)));
    TEST_ASSERT_TRUE(p.link == BootLink::WIFI_ACCESS_POINT);
    TEST_ASSERT_FALSE(p.auto_switch);
    TEST_ASSERT_FALSE(p.fallback_on_no_link);

    p = fallbackPlan(planNetwork(s3m(NetworkMode::ETHERNET, true, true, false)));
    TEST_ASSERT_TRUE(p.link == BootLink::WIFI_STATION);
    TEST_ASSERT_FALSE(p.auto_switch);
}

void test_boards_without_wifi_ignore_the_mode(void) {
    for (int m = 0; m < 3; m++) {
        for (int valid = 0; valid < 2; valid++) {
            for (int manual = 0; manual < 2; manual++) {
                const NetBootInput in = { false, true, true, (bool)valid, (bool)valid, (bool)manual, MODES[m] };
                const NetBootPlan p = planNetwork(in);
                TEST_ASSERT_TRUE(samePlan(p, planNetwork(ETHERNET_ONLY, valid, manual)));
                TEST_ASSERT_FALSE(p.auto_switch);
                TEST_ASSERT_FALSE(needsFallback(p, EthProbeOutcome::NO_CHIP));
                TEST_ASSERT_FALSE(needsFallback(p, EthProbeOutcome::NO_LINK));
            }
        }
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_auto_with_wifi_configured_switches_while_running);
    RUN_TEST(test_auto_without_ssid_falls_back_to_ap_without_switching);
    RUN_TEST(test_ethernet_mode_stays_on_the_cable);
    RUN_TEST(test_wifi_mode_ignores_the_w5500);
    RUN_TEST(test_double_reset_starts_ap_in_every_mode);
    RUN_TEST(test_needs_fallback_for_every_mode_and_outcome);
    RUN_TEST(test_fallback_plan_keeps_auto_switch_only_towards_the_station);
    RUN_TEST(test_boards_without_wifi_ignore_the_mode);
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
