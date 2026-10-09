#include <Arduino.h>
#include <unity.h>
#include <string>
#include "CommandCatalog.h"

void setUp(void) {}
void tearDown(void) {}

static HIDUsageDef mk(const char* path, uint32_t usage, uint8_t report_type) {
    HIDUsageDef d;
    d.usage = usage;
    d.report_type = report_type;
    d.found = true;
    strncpy(d.path, path, sizeof(d.path) - 1);
    return d;
}

// Command names joined by spaces, in catalog order
static std::string names(const std::vector<const UPSCommandInfo*>& cmds) {
    std::string out;
    for (const auto* c : cmds) {
        if (!out.empty()) out += " ";
        out += c->name;
    }
    return out;
}

void test_empty_descriptor_has_no_commands() {
    std::vector<HIDUsageDef> usages;
    TEST_ASSERT_EQUAL(0, CommandCatalog::build(usages, false).size());
}

void test_beeper_available_gives_beeper_commands() {
    std::vector<HIDUsageDef> usages;
    TEST_ASSERT_EQUAL_STRING("beeper.enable beeper.disable beeper.toggle",
                             names(CommandCatalog::build(usages, true)).c_str());
}

void test_delay_before_shutdown_alone() {
    std::vector<HIDUsageDef> usages = { mk("UPS.PowerSummary.DelayBeforeShutdown", 0x00840057, 0x03) };
    TEST_ASSERT_EQUAL_STRING("load.off load.off.delay shutdown.stop",
                             names(CommandCatalog::build(usages, false)).c_str());
}

void test_shutdown_and_startup_delays() {
    std::vector<HIDUsageDef> usages = {
        mk("UPS.PowerSummary.DelayBeforeShutdown", 0x00840057, 0x03),
        mk("UPS.PowerSummary.DelayBeforeStartup", 0x00840056, 0x03),
    };
    TEST_ASSERT_EQUAL_STRING(
        "load.off load.on load.off.delay load.on.delay shutdown.return shutdown.stayoff shutdown.stop",
        names(CommandCatalog::build(usages, false)).c_str());
}

void test_non_feature_usages_are_ignored() {
    std::vector<HIDUsageDef> usages = {
        mk("UPS.PowerSummary.DelayBeforeShutdown", 0x00840057, 0x01),
        mk("UPS.BatterySystem.Battery.Test", 0x00840058, 0x02),
    };
    TEST_ASSERT_EQUAL(0, CommandCatalog::build(usages, false).size());
}

void test_battery_test_paths() {
    std::vector<HIDUsageDef> a = { mk("UPS.BatterySystem.Battery.Test", 0x00840058, 0x03) };
    std::vector<HIDUsageDef> b = { mk("UPS.Output.Test", 0x00840058, 0x03) };
    std::vector<HIDUsageDef> c = { mk("UPS.Battery.Test", 0x00840058, 0x03) };
    const char* expected = "test.battery.start.quick test.battery.start.deep test.battery.stop";
    TEST_ASSERT_EQUAL_STRING(expected, names(CommandCatalog::build(a, false)).c_str());
    TEST_ASSERT_EQUAL_STRING(expected, names(CommandCatalog::build(b, false)).c_str());
    TEST_ASSERT_EQUAL_STRING(expected, names(CommandCatalog::build(c, false)).c_str());
}

void test_apc_vendor_codes_match_on_any_path() {
    std::vector<HIDUsageDef> usages = {
        mk("UPS.ff860005.ff86007d", 0xff86007d, 0x03),
        mk("UPS.ff860005.ff86007e", 0xff86007e, 0x03),
        mk("UPS.ff860005.ff86007c", 0xff86007c, 0x03),
        mk("UPS.PowerSummary.ff860072", 0xff860072, 0x03),
    };
    TEST_ASSERT_EQUAL_STRING(
        "test.panel.start test.panel.stop load.off load.on load.off.delay load.on.delay "
        "shutdown.return shutdown.stayoff shutdown.stop shutdown.reboot",
        names(CommandCatalog::build(usages, false)).c_str());
}

void test_delay_before_reboot() {
    std::vector<HIDUsageDef> usages = { mk("UPS.PowerSummary.DelayBeforeReboot", 0x00840055, 0x03) };
    TEST_ASSERT_EQUAL_STRING("shutdown.reboot", names(CommandCatalog::build(usages, false)).c_str());
}

void test_destructive_flags() {
    std::vector<HIDUsageDef> usages = {
        mk("UPS.PowerSummary.DelayBeforeShutdown", 0x00840057, 0x03),
        mk("UPS.PowerSummary.DelayBeforeStartup", 0x00840056, 0x03),
        mk("UPS.PowerSummary.DelayBeforeReboot", 0x00840055, 0x03),
        mk("UPS.BatterySystem.Battery.Test", 0x00840058, 0x03),
        mk("UPS.PowerSummary.ff860072", 0xff860072, 0x03),
    };
    auto cmds = CommandCatalog::build(usages, true);
    TEST_ASSERT_EQUAL(16, cmds.size());
    for (const auto* c : cmds) {
        bool destructive = strncmp(c->name, "load.", 5) == 0 || strncmp(c->name, "shutdown.", 9) == 0;
        TEST_ASSERT_EQUAL_MESSAGE(destructive, c->destructive, c->name);
    }
}

void test_find_known_and_unknown() {
    const UPSCommandInfo* c = CommandCatalog::find("test.battery.start.quick");
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_STRING("Start a quick battery test", c->description);
    TEST_ASSERT_FALSE(c->destructive);
    TEST_ASSERT_NULL(CommandCatalog::find("foo"));
    TEST_ASSERT_NULL(CommandCatalog::find(nullptr));
}

// US-057: usage and value written by the test commands
void test_resolve_battery_test_values() {
    std::vector<HIDUsageDef> usages = { mk("UPS.BatterySystem.Battery.Test", 0x00840058, 0x03) };
    HIDUsageDef def;
    uint32_t value = 99;
    TEST_ASSERT_TRUE(CommandCatalog::resolveWrite("test.battery.start.quick", usages, def, value));
    TEST_ASSERT_EQUAL_UINT32(1, value);
    TEST_ASSERT_EQUAL_STRING("UPS.BatterySystem.Battery.Test", def.path);
    TEST_ASSERT_TRUE(CommandCatalog::resolveWrite("test.battery.start.deep", usages, def, value));
    TEST_ASSERT_EQUAL_UINT32(2, value);
    TEST_ASSERT_TRUE(CommandCatalog::resolveWrite("test.battery.stop", usages, def, value));
    TEST_ASSERT_EQUAL_UINT32(3, value);
}

// APC declare UPS.Battery.Test as INPUT and FEATURE: the FEATURE one is written
void test_resolve_prefers_feature_usage() {
    HIDUsageDef in = mk("UPS.Battery.Test", 0x00840058, 0x01);
    in.report_id = 10;
    HIDUsageDef feat = mk("UPS.Battery.Test", 0x00840058, 0x03);
    feat.report_id = 20;
    std::vector<HIDUsageDef> usages = { in, feat };
    HIDUsageDef def;
    uint32_t value;
    TEST_ASSERT_TRUE(CommandCatalog::resolveWrite("test.battery.start.quick", usages, def, value));
    TEST_ASSERT_EQUAL_UINT8(0x03, def.report_type);
    TEST_ASSERT_EQUAL_UINT8(20, def.report_id);
}

void test_resolve_panel_test_values() {
    std::vector<HIDUsageDef> usages = { mk("UPS.0xFF860072", 0xff860072, 0x03) };
    HIDUsageDef def;
    uint32_t value = 99;
    TEST_ASSERT_TRUE(CommandCatalog::resolveWrite("test.panel.start", usages, def, value));
    TEST_ASSERT_EQUAL_UINT32(1, value);
    TEST_ASSERT_EQUAL_UINT32(0xff860072, def.usage);
    TEST_ASSERT_TRUE(CommandCatalog::resolveWrite("test.panel.stop", usages, def, value));
    TEST_ASSERT_EQUAL_UINT32(0, value);
}

void test_resolve_rejects_other_commands() {
    std::vector<HIDUsageDef> usages = {
        mk("UPS.PowerSummary.DelayBeforeShutdown", 0x00840057, 0x03),
        mk("UPS.PowerSummary.DelayBeforeStartup", 0x00840056, 0x03),
        mk("UPS.PowerSummary.AudibleAlarmControl", 0x0084005a, 0x03),
    };
    HIDUsageDef def;
    uint32_t value;
    TEST_ASSERT_FALSE(CommandCatalog::resolveWrite("test.battery.start.quick", usages, def, value)); // no Test usage
    TEST_ASSERT_FALSE(CommandCatalog::resolveWrite("beeper.enable", usages, def, value));
    TEST_ASSERT_FALSE(CommandCatalog::resolveWrite("shutdown.return", usages, def, value));
    TEST_ASSERT_FALSE(CommandCatalog::resolveWrite("foo", usages, def, value));
    TEST_ASSERT_FALSE(CommandCatalog::resolveWrite(nullptr, usages, def, value));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_descriptor_has_no_commands);
    RUN_TEST(test_beeper_available_gives_beeper_commands);
    RUN_TEST(test_delay_before_shutdown_alone);
    RUN_TEST(test_shutdown_and_startup_delays);
    RUN_TEST(test_non_feature_usages_are_ignored);
    RUN_TEST(test_battery_test_paths);
    RUN_TEST(test_apc_vendor_codes_match_on_any_path);
    RUN_TEST(test_delay_before_reboot);
    RUN_TEST(test_destructive_flags);
    RUN_TEST(test_find_known_and_unknown);
    RUN_TEST(test_resolve_battery_test_values);
    RUN_TEST(test_resolve_prefers_feature_usage);
    RUN_TEST(test_resolve_panel_test_values);
    RUN_TEST(test_resolve_rejects_other_commands);
    return UNITY_END();
}
