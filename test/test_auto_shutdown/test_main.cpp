#include <unity.h>
#include <vector>
#include "core/auto_shutdown.h"

// US-114: shutdown.default once per OB+LB event, re-armed only by OL

static const char* OB_LB = "OB DISCHRG LB";

void setUp(void) {}
void tearDown(void) {}

static AutoShutdown enabledShutdown() {
    AutoShutdown a;
    a.setEnabled(true);
    return a;
}

void test_off_by_default_and_armed(void) {
    AutoShutdown a;
    TEST_ASSERT_FALSE(a.enabled());
    TEST_ASSERT_TRUE(a.armed());
    TEST_ASSERT_FALSE(a.update(true, false, OB_LB, true));
    TEST_ASSERT_TRUE(a.armed());
}

void test_fires_once_on_battery_low(void) {
    AutoShutdown a = enabledShutdown();
    TEST_ASSERT_TRUE(a.update(true, false, OB_LB, true));
    TEST_ASSERT_FALSE(a.armed());
    for (int i = 0; i < 20; i++) TEST_ASSERT_FALSE(a.update(true, false, OB_LB, true));
}

void test_online_rearms_for_the_next_event(void) {
    AutoShutdown a = enabledShutdown();
    TEST_ASSERT_TRUE(a.update(true, false, OB_LB, true));
    // Still on battery, LB gone: no re-arm
    TEST_ASSERT_FALSE(a.update(true, false, "OB DISCHRG", true));
    TEST_ASSERT_FALSE(a.update(true, false, OB_LB, true));
    TEST_ASSERT_FALSE(a.update(true, false, "OL CHRG", true));
    TEST_ASSERT_TRUE(a.armed());
    TEST_ASSERT_TRUE(a.update(true, false, OB_LB, true));
}

void test_low_battery_on_line_rearms_without_firing(void) {
    AutoShutdown a = enabledShutdown();
    TEST_ASSERT_TRUE(a.update(true, false, OB_LB, true));
    TEST_ASSERT_FALSE(a.update(true, false, "OL CHRG LB", true));
    TEST_ASSERT_TRUE(a.armed());
}

void test_needs_both_ob_and_lb(void) {
    AutoShutdown a = enabledShutdown();
    TEST_ASSERT_FALSE(a.update(true, false, "OB DISCHRG", true));
    TEST_ASSERT_FALSE(a.update(true, false, "OL CHRG LB", true));
    TEST_ASSERT_FALSE(a.update(true, false, "LB", true));
    TEST_ASSERT_FALSE(a.update(true, false, "Unknown", true));
    TEST_ASSERT_FALSE(a.update(true, false, "", true));
    TEST_ASSERT_FALSE(a.update(true, false, nullptr, true));
    TEST_ASSERT_TRUE(a.armed());
}

void test_whole_words_only(void) {
    TEST_ASSERT_TRUE(AutoShutdown::hasToken("OB DISCHRG LB", "OB"));
    TEST_ASSERT_TRUE(AutoShutdown::hasToken("OB DISCHRG LB", "LB"));
    TEST_ASSERT_TRUE(AutoShutdown::hasToken("  OL  ", "OL"));
    TEST_ASSERT_FALSE(AutoShutdown::hasToken("OBX LBX", "OB"));
    TEST_ASSERT_FALSE(AutoShutdown::hasToken("XOB", "OB"));
    TEST_ASSERT_FALSE(AutoShutdown::hasToken("OLB", "LB"));
    TEST_ASSERT_FALSE(AutoShutdown::hasToken("BOOST", "OB"));
    TEST_ASSERT_FALSE(AutoShutdown::hasToken("O", "OB"));
    TEST_ASSERT_FALSE(AutoShutdown::hasToken(nullptr, "OB"));
    TEST_ASSERT_FALSE(AutoShutdown::hasToken("OB", ""));
    TEST_ASSERT_TRUE(AutoShutdown::onBatteryLow("LB OB"));
    TEST_ASSERT_FALSE(AutoShutdown::onBatteryLow("OB BOOST"));
}

void test_alarm_first_word(void) {
    AutoShutdown a = enabledShutdown();
    TEST_ASSERT_TRUE(a.update(true, false, "ALARM OB DISCHRG LB", true));
}

void test_disabled_never_fires(void) {
    AutoShutdown a;
    TEST_ASSERT_FALSE(a.update(true, false, OB_LB, true));
    TEST_ASSERT_FALSE(a.update(true, false, OB_LB, true));
}

void test_turned_on_during_an_event_not_handled(void) {
    AutoShutdown a;
    TEST_ASSERT_FALSE(a.update(true, false, OB_LB, true));
    a.setEnabled(true);
    TEST_ASSERT_TRUE(a.update(true, false, OB_LB, true));
}

void test_off_and_on_again_after_firing(void) {
    AutoShutdown a = enabledShutdown();
    TEST_ASSERT_TRUE(a.update(true, false, OB_LB, true));
    a.setEnabled(false);
    TEST_ASSERT_FALSE(a.update(true, false, OB_LB, true));
    a.setEnabled(true);
    TEST_ASSERT_FALSE(a.update(true, false, OB_LB, true));
    TEST_ASSERT_FALSE(a.armed());
}

void test_no_command_keeps_armed(void) {
    AutoShutdown a = enabledShutdown();
    TEST_ASSERT_FALSE(a.update(true, false, OB_LB, false));
    TEST_ASSERT_TRUE(a.armed());
    // The command shows up: it fires
    TEST_ASSERT_TRUE(a.update(true, false, OB_LB, true));
}

void test_disconnected_or_stale_change_nothing(void) {
    AutoShutdown a = enabledShutdown();
    TEST_ASSERT_FALSE(a.update(false, false, OB_LB, true));
    TEST_ASSERT_FALSE(a.update(true, true, OB_LB, true));
    TEST_ASSERT_TRUE(a.armed());
    TEST_ASSERT_TRUE(a.update(true, false, OB_LB, true));
    // OL read while disconnected or stale does not re-arm
    TEST_ASSERT_FALSE(a.update(false, false, "OL", true));
    TEST_ASSERT_FALSE(a.update(true, true, "OL", true));
    TEST_ASSERT_FALSE(a.armed());
    TEST_ASSERT_FALSE(a.update(true, false, OB_LB, true));
}

void test_restore_arm_after_not_connected(void) {
    AutoShutdown a = enabledShutdown();
    TEST_ASSERT_TRUE(a.update(true, false, OB_LB, true));
    a.restoreArm();
    TEST_ASSERT_TRUE(a.armed());
    TEST_ASSERT_TRUE(a.update(true, false, OB_LB, true));
    TEST_ASSERT_FALSE(a.update(true, false, OB_LB, true));
}

void test_offers_shutdown_default(void) {
    struct Cmd { const char* name; };
    const Cmd ret = { "shutdown.return" }, def = { "shutdown.default" }, near = { "shutdown.defaultx" },
              beeper = { "beeper.toggle" }, unnamed = { nullptr };
    TEST_ASSERT_TRUE(AutoShutdown::offersShutdownDefault(std::vector<const Cmd*>{ &beeper, &def, &ret }));
    TEST_ASSERT_FALSE(AutoShutdown::offersShutdownDefault(std::vector<const Cmd*>{ &ret, &near, &beeper }));
    TEST_ASSERT_FALSE(AutoShutdown::offersShutdownDefault(std::vector<const Cmd*>{}));
    // nullptr items and names are skipped, not dereferenced
    TEST_ASSERT_FALSE(AutoShutdown::offersShutdownDefault(std::vector<const Cmd*>{ nullptr, &unnamed }));
    TEST_ASSERT_TRUE(AutoShutdown::offersShutdownDefault(std::vector<const Cmd*>{ nullptr, &unnamed, &def }));
}

void test_availability(void) {
    TEST_ASSERT_TRUE(autoShutdownAvailability(true, true) == AutoShutdownAvailability::AVAILABLE);
    TEST_ASSERT_TRUE(autoShutdownAvailability(true, false) == AutoShutdownAvailability::NO_COMMAND);
    TEST_ASSERT_TRUE(autoShutdownAvailability(false, true) == AutoShutdownAvailability::NO_UPS);
    TEST_ASSERT_TRUE(autoShutdownAvailability(false, false) == AutoShutdownAvailability::NO_UPS);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_off_by_default_and_armed);
    RUN_TEST(test_fires_once_on_battery_low);
    RUN_TEST(test_online_rearms_for_the_next_event);
    RUN_TEST(test_low_battery_on_line_rearms_without_firing);
    RUN_TEST(test_needs_both_ob_and_lb);
    RUN_TEST(test_whole_words_only);
    RUN_TEST(test_alarm_first_word);
    RUN_TEST(test_disabled_never_fires);
    RUN_TEST(test_turned_on_during_an_event_not_handled);
    RUN_TEST(test_off_and_on_again_after_firing);
    RUN_TEST(test_no_command_keeps_armed);
    RUN_TEST(test_disconnected_or_stale_change_nothing);
    RUN_TEST(test_restore_arm_after_not_connected);
    RUN_TEST(test_offers_shutdown_default);
    RUN_TEST(test_availability);
    return UNITY_END();
}
