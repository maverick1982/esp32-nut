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
    TEST_ASSERT_EQUAL_STRING("load.off load.off.delay shutdown.stop shutdown.default",
                             names(CommandCatalog::build(usages, false)).c_str());
}

void test_shutdown_and_startup_delays() {
    std::vector<HIDUsageDef> usages = {
        mk("UPS.PowerSummary.DelayBeforeShutdown", 0x00840057, 0x03),
        mk("UPS.PowerSummary.DelayBeforeStartup", 0x00840056, 0x03),
    };
    TEST_ASSERT_EQUAL_STRING(
        "load.off load.on load.off.delay load.on.delay shutdown.return shutdown.stayoff shutdown.stop shutdown.default",
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
        "shutdown.return shutdown.stayoff shutdown.stop shutdown.reboot shutdown.default",
        names(CommandCatalog::build(usages, false)).c_str());
}

void test_delay_before_reboot() {
    std::vector<HIDUsageDef> usages = { mk("UPS.PowerSummary.DelayBeforeReboot", 0x00840055, 0x03) };
    TEST_ASSERT_EQUAL_STRING("shutdown.reboot shutdown.default", names(CommandCatalog::build(usages, false)).c_str());
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
    TEST_ASSERT_EQUAL(17, cmds.size());
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

// beeper.mute writes 3 on a multi-bit FEATURE beeper (mge-hid.c, apc-hid.c, cps-hid.c)
static HIDUsageDef beeperField(uint16_t bits, uint8_t report_type, int32_t lmin, int32_t lmax) {
    HIDUsageDef d = mk("UPS.PowerSummary.AudibleAlarmControl", 0x0084005a, report_type);
    d.bit_size = bits;
    d.logical_min = lmin;
    d.logical_max = lmax;
    return d;
}

void test_mute_listed_for_multibit_feature_beeper() {
    std::vector<HIDUsageDef> usages = { beeperField(8, 0x03, 1, 3) };
    TEST_ASSERT_EQUAL_STRING("beeper.enable beeper.disable beeper.toggle beeper.mute",
                             names(CommandCatalog::build(usages, true)).c_str());
    // Not controllable (quirk, EcoFlow): no beeper command at all
    TEST_ASSERT_EQUAL(0, CommandCatalog::build(usages, false).size());
}

void test_mute_not_listed_when_field_cannot_hold_3() {
    std::vector<HIDUsageDef> one_bit = { beeperField(1, 0x03, 0, 1) };
    std::vector<HIDUsageDef> output = { beeperField(8, 0x02, 1, 3) };
    std::vector<HIDUsageDef> range2 = { beeperField(8, 0x03, 1, 2) };
    const char* expected = "beeper.enable beeper.disable beeper.toggle";
    TEST_ASSERT_EQUAL_STRING(expected, names(CommandCatalog::build(one_bit, true)).c_str());
    TEST_ASSERT_EQUAL_STRING(expected, names(CommandCatalog::build(output, true)).c_str());
    TEST_ASSERT_EQUAL_STRING(expected, names(CommandCatalog::build(range2, true)).c_str());
}

void test_resolve_mute_writes_3_on_active_beeper() {
    std::vector<HIDUsageDef> usages = { beeperField(8, 0x03, 0, 0) };  // no declared range
    HIDUsageDef def;
    uint32_t value = 0;
    TEST_ASSERT_TRUE(CommandCatalog::resolveWrite("beeper.mute", usages, def, value));
    TEST_ASSERT_EQUAL_UINT32(3, value);
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.AudibleAlarmControl", def.path);

    std::vector<HIDUsageDef> one_bit = { beeperField(1, 0x03, 0, 1) };
    TEST_ASSERT_FALSE(CommandCatalog::resolveWrite("beeper.mute", one_bit, def, value));
}

// --- US-058: load and shutdown sequences ---

static HIDUsageDef field(const char* path, uint32_t usage, int32_t lmin, int32_t lmax) {
    HIDUsageDef d = mk(path, usage, 0x03);
    d.bit_size = 16;
    d.logical_min = lmin;
    d.logical_max = lmax;
    return d;
}

static std::vector<HIDUsageDef> allDelays() {
    return {
        mk("UPS.PowerSummary.DelayBeforeShutdown", 0x00840057, 0x03),
        mk("UPS.PowerSummary.DelayBeforeStartup", 0x00840056, 0x03),
        mk("UPS.PowerSummary.DelayBeforeReboot", 0x00840055, 0x03),
    };
}

// Steps as "path=value" joined by spaces
static std::string stepsStr(const std::vector<CommandCatalog::WriteStep>& steps) {
    std::string out;
    for (const auto& s : steps) {
        if (!out.empty()) out += " ";
        out += s.def.path;
        out += "=" + std::to_string(s.value);
    }
    return out;
}

static std::string resolve(const char* name, const char* param, const std::vector<HIDUsageDef>& usages,
                           CommandCatalog::StepsResult expected = CommandCatalog::StepsResult::OK) {
    std::vector<CommandCatalog::WriteStep> steps;
    CommandCatalog::StepsResult r = CommandCatalog::resolveShutdownSteps(name, param, usages, 20, 30, steps);
    TEST_ASSERT_EQUAL_MESSAGE((int)expected, (int)r, name);
    return stepsStr(steps);
}

#define SD "UPS.PowerSummary.DelayBeforeShutdown"
#define ST "UPS.PowerSummary.DelayBeforeStartup"
#define RB "UPS.PowerSummary.DelayBeforeReboot"

void test_steps_load_commands() {
    auto u = allDelays();
    TEST_ASSERT_EQUAL_STRING(SD "=0", resolve("load.off", nullptr, u).c_str());
    TEST_ASSERT_EQUAL_STRING(ST "=0", resolve("load.on", nullptr, u).c_str());
    TEST_ASSERT_EQUAL_STRING(SD "=20", resolve("load.off.delay", nullptr, u).c_str());
    TEST_ASSERT_EQUAL_STRING(SD "=30", resolve("load.off.delay", "30", u).c_str());
    TEST_ASSERT_EQUAL_STRING(ST "=30", resolve("load.on.delay", nullptr, u).c_str());
    TEST_ASSERT_EQUAL_STRING(ST "=5", resolve("load.on.delay", "5", u).c_str());
    TEST_ASSERT_EQUAL_STRING(SD "=-1", resolve("load.off.delay", "-1", u).c_str());
    TEST_ASSERT_EQUAL_STRING(SD "=20", resolve("load.off.delay", "", u).c_str());  // empty = default
}

void test_steps_shutdown_commands() {
    auto u = allDelays();
    TEST_ASSERT_EQUAL_STRING(ST "=30 " SD "=20", resolve("shutdown.return", nullptr, u).c_str());
    TEST_ASSERT_EQUAL_STRING(ST "=-1 " SD "=20", resolve("shutdown.stayoff", nullptr, u).c_str());
    TEST_ASSERT_EQUAL_STRING(SD "=-1", resolve("shutdown.stop", nullptr, u).c_str());
    TEST_ASSERT_EQUAL_STRING(RB "=10", resolve("shutdown.reboot", nullptr, u).c_str());
    TEST_ASSERT_EQUAL_STRING(RB "=5", resolve("shutdown.reboot", "5", u).c_str());
}

void test_steps_use_given_delays() {
    auto u = allDelays();
    std::vector<CommandCatalog::WriteStep> steps;
    TEST_ASSERT_EQUAL((int)CommandCatalog::StepsResult::OK,
        (int)CommandCatalog::resolveShutdownSteps("shutdown.return", nullptr, u, 60, 120, steps));
    TEST_ASSERT_EQUAL_STRING(ST "=120 " SD "=60", stepsStr(steps).c_str());
}

void test_steps_param_ignored_by_other_commands() {
    auto u = allDelays();
    TEST_ASSERT_EQUAL_STRING(SD "=0", resolve("load.off", "abc", u).c_str());
    TEST_ASSERT_EQUAL_STRING(SD "=-1", resolve("shutdown.stop", "30", u).c_str());
    TEST_ASSERT_EQUAL_STRING(ST "=30 " SD "=20", resolve("shutdown.return", "5", u).c_str());
}

void test_steps_output_and_apc_paths() {
    std::vector<HIDUsageDef> output = {
        mk("UPS.Output.DelayBeforeShutdown", 0x00840057, 0x03),
        mk("UPS.Output.DelayBeforeStartup", 0x00840056, 0x03),
        mk("UPS.Output.DelayBeforeReboot", 0x00840055, 0x03),
    };
    TEST_ASSERT_EQUAL_STRING("UPS.Output.DelayBeforeStartup=30 UPS.Output.DelayBeforeShutdown=20",
                             resolve("shutdown.return", nullptr, output).c_str());
    TEST_ASSERT_EQUAL_STRING("UPS.Output.DelayBeforeReboot=10", resolve("shutdown.reboot", nullptr, output).c_str());

    std::vector<HIDUsageDef> apc = {
        mk("UPS.0xFF860005.0xFF86007D", 0xff86007d, 0x03),
        mk("UPS.0xFF860005.0xFF86007E", 0xff86007e, 0x03),
        mk("UPS.0xFF860005.0xFF86007C", 0xff86007c, 0x03),
    };
    TEST_ASSERT_EQUAL_STRING("UPS.0xFF860005.0xFF86007E=-1 UPS.0xFF860005.0xFF86007D=20",
                             resolve("shutdown.stayoff", nullptr, apc).c_str());
    // APCGeneralCollection's reboot field is not the Back-UPS CS quirk
    TEST_ASSERT_EQUAL_STRING("UPS.0xFF860005.0xFF86007E=30 UPS.0xFF860005.0xFF86007D=20",
                             resolve("shutdown.return", nullptr, apc).c_str());
    TEST_ASSERT_EQUAL_STRING("UPS.0xFF860005.0xFF86007C=10", resolve("shutdown.reboot", nullptr, apc).c_str());

    // INPUT-only fields are not written
    std::vector<HIDUsageDef> input = { mk(SD, 0x00840057, 0x01) };
    TEST_ASSERT_EQUAL_STRING("", resolve("load.off", nullptr, input, CommandCatalog::StepsResult::NOT_SUPPORTED).c_str());
}

// apc-hid.c: Back-UPS CS, shutdown.return = 1 on UPS.Output.APCDelayBeforeReboot
void test_steps_apc_cs_return_quirk() {
    std::vector<HIDUsageDef> named = allDelays();
    named.push_back(mk("UPS.Output.APCDelayBeforeReboot", 0xff86007c, 0x03));
    TEST_ASSERT_EQUAL_STRING("UPS.Output.APCDelayBeforeReboot=1", resolve("shutdown.return", nullptr, named).c_str());

    // HIDParser names the vendor usage by code
    std::vector<HIDUsageDef> by_code = { mk("UPS.Output.0xFF86007C", 0xff86007c, 0x03) };
    TEST_ASSERT_EQUAL_STRING("UPS.Output.0xFF86007C=1", resolve("shutdown.return", nullptr, by_code).c_str());
    // apc-hid.c uses that field for shutdown.return only: never for shutdown.reboot
    resolve("shutdown.reboot", nullptr, by_code, CommandCatalog::StepsResult::NOT_SUPPORTED);

    // Deeper than Output, or INPUT: no quirk
    std::vector<HIDUsageDef> deeper = { mk("UPS.Output.0xFF860005.0xFF86007C", 0xff86007c, 0x03) };
    resolve("shutdown.return", nullptr, deeper, CommandCatalog::StepsResult::NOT_SUPPORTED);
    std::vector<HIDUsageDef> input = { mk("UPS.Output.0xFF86007C", 0xff86007c, 0x01) };
    resolve("shutdown.return", nullptr, input, CommandCatalog::StepsResult::NOT_SUPPORTED);
}

// apc-hid.c: UPS.Output.APCDelayBeforeReboot serves shutdown.return = 1 only; shutdown.reboot
// uses the standard DelayBeforeReboot and UPS.APCGeneralCollection.APCDelayBeforeReboot
void test_reboot_group_excludes_apc_cs_field() {
    // CS field alone: no shutdown.reboot, neither in LIST CMD nor as a sequence
    std::vector<HIDUsageDef> cs_only = { mk("UPS.Output.0xFF86007C", 0xff86007c, 0x03) };
    TEST_ASSERT_NULL(CommandCatalog::rebootField(cs_only));
    TEST_ASSERT_EQUAL(std::string::npos, names(CommandCatalog::build(cs_only, false)).find("shutdown.reboot"));
    std::vector<HIDUsageDef> named_cs = { mk("UPS.Output.APCDelayBeforeReboot", 0xff86007c, 0x03) };
    TEST_ASSERT_NULL(CommandCatalog::rebootField(named_cs));
    resolve("shutdown.reboot", nullptr, named_cs, CommandCatalog::StepsResult::NOT_SUPPORTED);

    // With the APCGeneralCollection field, that one is the reboot field even after the CS one
    std::vector<HIDUsageDef> both = {
        mk("UPS.Output.0xFF86007C", 0xff86007c, 0x03),
        mk("UPS.0xFF860005.0xFF86007C", 0xff86007c, 0x03),
    };
    TEST_ASSERT_EQUAL_STRING("UPS.0xFF860005.0xFF86007C=10", resolve("shutdown.reboot", nullptr, both).c_str());
    TEST_ASSERT_EQUAL_STRING("UPS.Output.0xFF86007C=1", resolve("shutdown.return", nullptr, both).c_str());
    TEST_ASSERT_EQUAL_STRING("shutdown.reboot shutdown.default", names(CommandCatalog::build(both, false)).c_str());

    // Standard DelayBeforeReboot next to the CS field (Back-UPS CS 500)
    std::vector<HIDUsageDef> standard = {
        mk("UPS.Output.0xFF86007C", 0xff86007c, 0x03),
        mk("UPS.Output.DelayBeforeReboot", 0x00840055, 0x03),
    };
    TEST_ASSERT_EQUAL_STRING("UPS.Output.DelayBeforeReboot=10", resolve("shutdown.reboot", nullptr, standard).c_str());
}

void test_steps_invalid_param() {
    auto u = allDelays();
    const CommandCatalog::StepsResult inv = CommandCatalog::StepsResult::INVALID_ARGUMENT;
    TEST_ASSERT_EQUAL_STRING("", resolve("load.off.delay", "abc", u, inv).c_str());
    TEST_ASSERT_EQUAL_STRING("", resolve("load.off.delay", "-5", u, inv).c_str());
    TEST_ASSERT_EQUAL_STRING("", resolve("load.off.delay", "12x", u, inv).c_str());
    TEST_ASSERT_EQUAL_STRING("", resolve("load.on.delay", "+5", u, inv).c_str());
    TEST_ASSERT_EQUAL_STRING("", resolve("load.on.delay", " 5", u, inv).c_str());
    TEST_ASSERT_EQUAL_STRING("", resolve("shutdown.reboot", "-", u, inv).c_str());
    TEST_ASSERT_EQUAL_STRING("", resolve("shutdown.reboot", "99999999999", u, inv).c_str());

    // Declared logical range
    std::vector<HIDUsageDef> ranged = { field(SD, 0x00840057, -1, 600) };
    TEST_ASSERT_EQUAL_STRING(SD "=600", resolve("load.off.delay", "600", ranged).c_str());
    TEST_ASSERT_EQUAL_STRING("", resolve("load.off.delay", "601", ranged, inv).c_str());
    std::vector<HIDUsageDef> from_zero = { field(SD, 0x00840057, 0, 600) };
    TEST_ASSERT_EQUAL_STRING("", resolve("load.off.delay", "-1", from_zero, inv).c_str());
}

void test_steps_missing_usage() {
    std::vector<HIDUsageDef> only_sd = { mk(SD, 0x00840057, 0x03) };
    const CommandCatalog::StepsResult ns = CommandCatalog::StepsResult::NOT_SUPPORTED;
    resolve("load.on", nullptr, only_sd, ns);
    resolve("load.on.delay", "5", only_sd, ns);
    resolve("shutdown.return", nullptr, only_sd, ns);
    resolve("shutdown.stayoff", nullptr, only_sd, ns);
    resolve("shutdown.reboot", nullptr, only_sd, ns);
    resolve("shutdown.default", nullptr, allDelays(), ns);
    resolve("test.battery.start.quick", nullptr, allDelays(), ns);
    resolve(nullptr, nullptr, allDelays(), ns);
}

// A value that does not fit the field would be truncated by writeField: on 16 bits
// 65536 would become 0 (immediate shutdown). Range not declared here.
void test_steps_value_must_fit_field() {
    const CommandCatalog::StepsResult inv = CommandCatalog::StepsResult::INVALID_ARGUMENT;
    std::vector<HIDUsageDef> u16 = { field(SD, 0x00840057, 0, 0) };
    TEST_ASSERT_EQUAL_STRING(SD "=65535", resolve("load.off.delay", "65535", u16).c_str());
    TEST_ASSERT_EQUAL_STRING("", resolve("load.off.delay", "65536", u16, inv).c_str());
    TEST_ASSERT_EQUAL_STRING(SD "=-1", resolve("shutdown.stop", nullptr, u16).c_str());

    // Signed field (logical_min < 0, range declared wider than the field)
    std::vector<HIDUsageDef> s16 = { field(SD, 0x00840057, -1, 100000) };
    TEST_ASSERT_EQUAL_STRING(SD "=32767", resolve("load.off.delay", "32767", s16).c_str());
    TEST_ASSERT_EQUAL_STRING("", resolve("load.off.delay", "32768", s16, inv).c_str());
}

// ups.delay.* missing or invalid (kNoDelay): the commands that need them are not run,
// an explicit parameter still works
void test_steps_without_configured_delays() {
    auto u = allDelays();
    const CommandCatalog::StepsResult ns = CommandCatalog::StepsResult::NOT_SUPPORTED;
    const int32_t none = CommandCatalog::kNoDelay;
    std::vector<CommandCatalog::WriteStep> steps;
    TEST_ASSERT_EQUAL((int)ns, (int)CommandCatalog::resolveShutdownSteps("load.off.delay", nullptr, u, none, none, steps));
    TEST_ASSERT_EQUAL((int)ns, (int)CommandCatalog::resolveShutdownSteps("load.on.delay", nullptr, u, none, none, steps));
    TEST_ASSERT_EQUAL((int)ns, (int)CommandCatalog::resolveShutdownSteps("shutdown.return", nullptr, u, none, none, steps));
    TEST_ASSERT_EQUAL((int)ns, (int)CommandCatalog::resolveShutdownSteps("shutdown.stayoff", nullptr, u, none, none, steps));
    TEST_ASSERT_EQUAL(0, steps.size());
    TEST_ASSERT_EQUAL((int)CommandCatalog::StepsResult::OK,
                      (int)CommandCatalog::resolveShutdownSteps("load.off.delay", "45", u, none, none, steps));
    TEST_ASSERT_EQUAL_STRING(SD "=45", stepsStr(steps).c_str());
    TEST_ASSERT_EQUAL((int)CommandCatalog::StepsResult::OK,
                      (int)CommandCatalog::resolveShutdownSteps("load.off", nullptr, u, none, none, steps));
}

void test_shutdown_default_availability() {
    std::vector<HIDUsageDef> reboot_only = { mk(RB, 0x00840055, 0x03) };
    TEST_ASSERT_EQUAL_STRING("shutdown.reboot shutdown.default",
                             names(CommandCatalog::build(reboot_only, false)).c_str());
    std::vector<HIDUsageDef> none = { mk("UPS.BatterySystem.Battery.Test", 0x00840058, 0x03) };
    TEST_ASSERT_EQUAL_STRING("test.battery.start.quick test.battery.start.deep test.battery.stop",
                             names(CommandCatalog::build(none, false)).c_str());
    const UPSCommandInfo* c = CommandCatalog::find("shutdown.default");
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_TRUE(c->destructive);
    TEST_ASSERT_EQUAL_STRING("Run the driver-defined UPS shutdown sequence (as opposed to user-configured 'sdcommands')",
                             c->description);
}

void test_build_without_shutdown_commands() {
    std::vector<HIDUsageDef> usages = allDelays();
    usages.push_back(mk("UPS.BatterySystem.Battery.Test", 0x00840058, 0x03));
    usages.push_back(mk("UPS.PowerSummary.ff860072", 0xff860072, 0x03));
    TEST_ASSERT_EQUAL_STRING(
        "beeper.enable beeper.disable beeper.toggle test.battery.start.quick test.battery.start.deep "
        "test.battery.stop test.panel.start test.panel.stop",
        names(CommandCatalog::build(usages, true, false)).c_str());
}

void test_apply_default_delays() {
    UPSData d;
    CommandCatalog::applyDefaultDelays(d, allDelays(), 20, 30, true);
    TEST_ASSERT_EQUAL_STRING("20", d.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_EQUAL_STRING("30", d.get("ups.delay.start").c_str());

    UPSData cps;
    CommandCatalog::applyDefaultDelays(cps, allDelays(), 60, 120, true);
    TEST_ASSERT_EQUAL_STRING("60", cps.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_EQUAL_STRING("120", cps.get("ups.delay.start").c_str());

    UPSData only_sd;
    CommandCatalog::applyDefaultDelays(only_sd, { mk(SD, 0x00840057, 0x03) }, 20, 30, true);
    TEST_ASSERT_TRUE(only_sd.hasKey("ups.delay.shutdown"));
    TEST_ASSERT_FALSE(only_sd.hasKey("ups.delay.start"));

    UPSData only_st;
    CommandCatalog::applyDefaultDelays(only_st, { mk(ST, 0x00840056, 0x03) }, 20, 30, true);
    TEST_ASSERT_FALSE(only_st.hasKey("ups.delay.shutdown"));
    TEST_ASSERT_TRUE(only_st.hasKey("ups.delay.start"));

    UPSData disabled;
    CommandCatalog::applyDefaultDelays(disabled, allDelays(), 20, 30, false);
    TEST_ASSERT_FALSE(disabled.hasKey("ups.delay.shutdown"));
    TEST_ASSERT_FALSE(disabled.hasKey("ups.delay.start"));

    UPSData none;
    CommandCatalog::applyDefaultDelays(none, { mk(RB, 0x00840055, 0x03) }, 20, 30, true);
    TEST_ASSERT_FALSE(none.hasKey("ups.delay.shutdown"));
    TEST_ASSERT_FALSE(none.hasKey("ups.delay.start"));
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
    RUN_TEST(test_mute_listed_for_multibit_feature_beeper);
    RUN_TEST(test_mute_not_listed_when_field_cannot_hold_3);
    RUN_TEST(test_resolve_mute_writes_3_on_active_beeper);
    RUN_TEST(test_steps_load_commands);
    RUN_TEST(test_steps_shutdown_commands);
    RUN_TEST(test_steps_use_given_delays);
    RUN_TEST(test_steps_param_ignored_by_other_commands);
    RUN_TEST(test_steps_output_and_apc_paths);
    RUN_TEST(test_steps_apc_cs_return_quirk);
    RUN_TEST(test_reboot_group_excludes_apc_cs_field);
    RUN_TEST(test_steps_invalid_param);
    RUN_TEST(test_steps_missing_usage);
    RUN_TEST(test_steps_value_must_fit_field);
    RUN_TEST(test_steps_without_configured_delays);
    RUN_TEST(test_shutdown_default_availability);
    RUN_TEST(test_build_without_shutdown_commands);
    RUN_TEST(test_apply_default_delays);
    return UNITY_END();
}
