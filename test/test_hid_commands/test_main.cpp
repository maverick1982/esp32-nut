#include <Arduino.h>
#include <unity.h>
#include <string>
#include "HidControl.h"
#include "NutTable.h"
#include "DriverVarStore.h"
#include "HIDParser.h"
#include <fstream>
#include <sstream>

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

// --- Generic table (NutTable::genericHid2Nut, US-079) ---
// The commands of a driver without a brand subdriver come from the HU_TYPE_CMD rows of the
// generic table: these tests check that they behave as the former usage groups (US-058..US-097)

static const NutTable::HidInfo* genericTable(size_t& n) {
    return NutTable::genericHid2Nut(n);
}

static std::vector<const UPSCommandInfo*> buildGeneric(const std::vector<HIDUsageDef>& usages, bool beeper_available,
                                                       bool shutdown_cmds = true, bool mute_allowed = true) {
    size_t n;
    const NutTable::HidInfo* t = genericTable(n);
    return HidControl::buildCommands(usages, beeper_available, shutdown_cmds, mute_allowed, t, n);
}

static bool resolveGeneric(const char* name, const std::vector<HIDUsageDef>& usages, HIDUsageDef& def, uint32_t& value) {
    size_t n;
    const NutTable::HidInfo* t = genericTable(n);
    return HidControl::resolveWrite(name, usages, def, value, t, n);
}

static HidControl::StepsResult stepsGeneric(const char* name, const char* param, const std::vector<HIDUsageDef>& usages,
                                            int32_t off_delay, int32_t on_delay, std::vector<HidControl::WriteStep>& steps) {
    size_t n;
    const NutTable::HidInfo* t = genericTable(n);
    return HidControl::resolveShutdownSteps(name, param, usages, off_delay, on_delay, steps, t, n);
}

// beeper.mute writes 3 on a multi-bit FEATURE beeper (mge-hid.c, apc-hid.c, cps-hid.c)
static HIDUsageDef beeperField(uint16_t bits, uint8_t report_type, int32_t lmin, int32_t lmax) {
    HIDUsageDef d = mk("UPS.PowerSummary.AudibleAlarmControl", 0x0084005a, report_type);
    d.bit_size = bits;
    d.logical_min = lmin;
    d.logical_max = lmax;
    return d;
}

void test_empty_descriptor_has_no_commands() {
    std::vector<HIDUsageDef> usages;
    TEST_ASSERT_EQUAL(0, buildGeneric(usages, false).size());
}

void test_beeper_available_gives_beeper_commands() {
    // US-079: the beeper commands are HU_TYPE_CMD rows, bound only to an AudibleAlarmControl usage
    std::vector<HIDUsageDef> usages = { beeperField(1, 0x03, 0, 1) };
    TEST_ASSERT_EQUAL_STRING("beeper.enable beeper.disable beeper.toggle",
                             names(buildGeneric(usages, true)).c_str());
    std::vector<HIDUsageDef> none;
    TEST_ASSERT_EQUAL(0, buildGeneric(none, true).size());
}

void test_delay_before_shutdown_alone() {
    std::vector<HIDUsageDef> usages = { mk("UPS.PowerSummary.DelayBeforeShutdown", 0x00840057, 0x03) };
    TEST_ASSERT_EQUAL_STRING("load.off load.off.delay shutdown.stop shutdown.default",
                             names(buildGeneric(usages, false)).c_str());
}

void test_shutdown_and_startup_delays() {
    std::vector<HIDUsageDef> usages = {
        mk("UPS.PowerSummary.DelayBeforeShutdown", 0x00840057, 0x03),
        mk("UPS.PowerSummary.DelayBeforeStartup", 0x00840056, 0x03),
    };
    TEST_ASSERT_EQUAL_STRING(
        "load.off load.on load.off.delay load.on.delay shutdown.return shutdown.stayoff shutdown.stop shutdown.default",
        names(buildGeneric(usages, false)).c_str());
}

void test_non_feature_usages_are_ignored() {
    std::vector<HIDUsageDef> usages = {
        mk("UPS.PowerSummary.DelayBeforeShutdown", 0x00840057, 0x01),
        mk("UPS.BatterySystem.Battery.Test", 0x00840058, 0x02),
    };
    TEST_ASSERT_EQUAL(0, buildGeneric(usages, false).size());
}

void test_battery_test_paths() {
    std::vector<HIDUsageDef> a = { mk("UPS.BatterySystem.Battery.Test", 0x00840058, 0x03) };
    std::vector<HIDUsageDef> b = { mk("UPS.Output.Test", 0x00840058, 0x03) };
    std::vector<HIDUsageDef> c = { mk("UPS.Battery.Test", 0x00840058, 0x03) };
    const char* expected = "test.battery.start.quick test.battery.start.deep test.battery.stop";
    TEST_ASSERT_EQUAL_STRING(expected, names(buildGeneric(a, false)).c_str());
    TEST_ASSERT_EQUAL_STRING(expected, names(buildGeneric(b, false)).c_str());
    TEST_ASSERT_EQUAL_STRING(expected, names(buildGeneric(c, false)).c_str());
}

// US-079: the generic rows match the APC fields by path, in the HIDParser notation of the
// vendor usages, no longer by usage code on any path
void test_apc_vendor_fields_match_by_path() {
    std::vector<HIDUsageDef> usages = {
        mk("UPS.0xFF860005.0xFF86007D", 0xff86007d, 0x03),
        mk("UPS.0xFF860005.0xFF86007E", 0xff86007e, 0x03),
        mk("UPS.0xFF860005.0xFF86007C", 0xff86007c, 0x03),
        mk("UPS.PowerSummary.0xFF860072", 0xff860072, 0x03),
    };
    TEST_ASSERT_EQUAL_STRING(
        "test.panel.start test.panel.stop load.off load.on load.off.delay load.on.delay "
        "shutdown.return shutdown.stayoff shutdown.stop shutdown.reboot shutdown.default",
        names(buildGeneric(usages, false)).c_str());
}

void test_delay_before_reboot() {
    std::vector<HIDUsageDef> usages = { mk("UPS.PowerSummary.DelayBeforeReboot", 0x00840055, 0x03) };
    TEST_ASSERT_EQUAL_STRING("shutdown.reboot shutdown.default", names(buildGeneric(usages, false)).c_str());
}

void test_destructive_flags() {
    std::vector<HIDUsageDef> usages = {
        mk("UPS.PowerSummary.DelayBeforeShutdown", 0x00840057, 0x03),
        mk("UPS.PowerSummary.DelayBeforeStartup", 0x00840056, 0x03),
        mk("UPS.PowerSummary.DelayBeforeReboot", 0x00840055, 0x03),
        mk("UPS.BatterySystem.Battery.Test", 0x00840058, 0x03),
        mk("UPS.PowerSummary.0xFF860072", 0xff860072, 0x03),
        beeperField(1, 0x03, 0, 1),  // US-079: the beeper rows need the usage
    };
    auto cmds = buildGeneric(usages, true);
    TEST_ASSERT_EQUAL(17, cmds.size());
    for (const auto* c : cmds) {
        bool destructive = strncmp(c->name, "load.", 5) == 0 || strncmp(c->name, "shutdown.", 9) == 0;
        TEST_ASSERT_EQUAL_MESSAGE(destructive, c->destructive, c->name);
    }
}

void test_find_known_and_unknown() {
    const UPSCommandInfo* c = UpsCommands::find("test.battery.start.quick");
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_STRING("Start a quick battery test", c->description);
    TEST_ASSERT_FALSE(c->destructive);
    TEST_ASSERT_NULL(UpsCommands::find("foo"));
    TEST_ASSERT_NULL(UpsCommands::find(nullptr));
}

// US-057: usage and value written by the test commands
void test_resolve_battery_test_values() {
    std::vector<HIDUsageDef> usages = { mk("UPS.BatterySystem.Battery.Test", 0x00840058, 0x03) };
    HIDUsageDef def;
    uint32_t value = 99;
    TEST_ASSERT_TRUE(resolveGeneric("test.battery.start.quick", usages, def, value));
    TEST_ASSERT_EQUAL_UINT32(1, value);
    TEST_ASSERT_EQUAL_STRING("UPS.BatterySystem.Battery.Test", def.path);
    TEST_ASSERT_TRUE(resolveGeneric("test.battery.start.deep", usages, def, value));
    TEST_ASSERT_EQUAL_UINT32(2, value);
    TEST_ASSERT_TRUE(resolveGeneric("test.battery.stop", usages, def, value));
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
    TEST_ASSERT_TRUE(resolveGeneric("test.battery.start.quick", usages, def, value));
    TEST_ASSERT_EQUAL_UINT8(0x03, def.report_type);
    TEST_ASSERT_EQUAL_UINT8(20, def.report_id);
}

void test_resolve_panel_test_values() {
    std::vector<HIDUsageDef> usages = { mk("UPS.0xFF860072", 0xff860072, 0x03) };
    HIDUsageDef def;
    uint32_t value = 99;
    TEST_ASSERT_TRUE(resolveGeneric("test.panel.start", usages, def, value));
    TEST_ASSERT_EQUAL_UINT32(1, value);
    TEST_ASSERT_EQUAL_UINT32(0xff860072, def.usage);
    TEST_ASSERT_TRUE(resolveGeneric("test.panel.stop", usages, def, value));
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
    TEST_ASSERT_FALSE(resolveGeneric("test.battery.start.quick", usages, def, value)); // no Test usage
    TEST_ASSERT_FALSE(resolveGeneric("beeper.enable", usages, def, value));
    TEST_ASSERT_FALSE(resolveGeneric("shutdown.return", usages, def, value));
    TEST_ASSERT_FALSE(resolveGeneric("foo", usages, def, value));
    TEST_ASSERT_FALSE(resolveGeneric(nullptr, usages, def, value));
}

void test_mute_listed_for_multibit_feature_beeper() {
    std::vector<HIDUsageDef> usages = { beeperField(8, 0x03, 1, 3) };
    TEST_ASSERT_EQUAL_STRING("beeper.enable beeper.disable beeper.toggle beeper.mute",
                             names(buildGeneric(usages, true)).c_str());
    // Not controllable (quirk, EcoFlow): no beeper command at all
    TEST_ASSERT_EQUAL(0, buildGeneric(usages, false).size());
}

void test_mute_not_listed_when_field_cannot_hold_3() {
    std::vector<HIDUsageDef> one_bit = { beeperField(1, 0x03, 0, 1) };
    std::vector<HIDUsageDef> output = { beeperField(8, 0x02, 1, 3) };
    std::vector<HIDUsageDef> range2 = { beeperField(8, 0x03, 1, 2) };
    const char* expected = "beeper.enable beeper.disable beeper.toggle";
    TEST_ASSERT_EQUAL_STRING(expected, names(buildGeneric(one_bit, true)).c_str());
    TEST_ASSERT_EQUAL_STRING(expected, names(buildGeneric(output, true)).c_str());
    TEST_ASSERT_EQUAL_STRING(expected, names(buildGeneric(range2, true)).c_str());
}

void test_resolve_mute_writes_3_on_active_beeper() {
    std::vector<HIDUsageDef> usages = { beeperField(8, 0x03, 0, 0) };  // no declared range
    HIDUsageDef def;
    uint32_t value = 0;
    TEST_ASSERT_TRUE(resolveGeneric("beeper.mute", usages, def, value));
    TEST_ASSERT_EQUAL_UINT32(3, value);
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.AudibleAlarmControl", def.path);

    std::vector<HIDUsageDef> one_bit = { beeperField(1, 0x03, 0, 1) };
    TEST_ASSERT_FALSE(resolveGeneric("beeper.mute", one_bit, def, value));
}

// APC Smart-UPS (#78): the beeper path is declared as Input first, then as Feature.
// The host writes the Feature item, so beeper.mute is offered and written there.
void test_active_beeper_prefers_feature_over_input() {
    HIDUsageDef input = beeperField(8, 0x01, 1, 3);
    input.report_id = 0x14;
    input.bit_offset = 0;
    HIDUsageDef feature = beeperField(8, 0x03, 1, 3);
    feature.report_id = 0x14;
    feature.bit_offset = 8;
    std::vector<HIDUsageDef> usages = { input, feature };

    TEST_ASSERT_EQUAL_STRING("beeper.enable beeper.disable beeper.toggle beeper.mute",
                             names(buildGeneric(usages, true)).c_str());
    HIDUsageDef def;
    uint32_t value = 0;
    TEST_ASSERT_TRUE(resolveGeneric("beeper.mute", usages, def, value));
    TEST_ASSERT_EQUAL_UINT8(0x03, def.report_type);
    TEST_ASSERT_EQUAL_UINT16(8, def.bit_offset);

    // Input only: nothing to mute
    std::vector<HIDUsageDef> input_only = { input };
    TEST_ASSERT_FALSE(resolveGeneric("beeper.mute", input_only, def, value));
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
static std::string stepsStr(const std::vector<HidControl::WriteStep>& steps) {
    std::string out;
    for (const auto& s : steps) {
        if (!out.empty()) out += " ";
        out += s.def.path;
        out += "=" + std::to_string(s.value);
    }
    return out;
}

static std::string resolve(const char* name, const char* param, const std::vector<HIDUsageDef>& usages,
                           HidControl::StepsResult expected = HidControl::StepsResult::OK) {
    std::vector<HidControl::WriteStep> steps;
    HidControl::StepsResult r = stepsGeneric(name, param, usages, 20, 30, steps);
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
    std::vector<HidControl::WriteStep> steps;
    TEST_ASSERT_EQUAL((int)HidControl::StepsResult::OK,
        (int)stepsGeneric("shutdown.return", nullptr, u, 60, 120, steps));
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
    TEST_ASSERT_EQUAL_STRING("", resolve("load.off", nullptr, input, HidControl::StepsResult::NOT_SUPPORTED).c_str());
}

// apc-hid.c: Back-UPS CS, shutdown.return = 1 on UPS.Output.APCDelayBeforeReboot
void test_steps_apc_cs_return_quirk() {
    // The row of apc-hid.c:461 comes before the composite command, also with both delays
    std::vector<HIDUsageDef> with_delays = allDelays();
    with_delays.push_back(mk("UPS.Output.0xFF86007C", 0xff86007c, 0x03));
    TEST_ASSERT_EQUAL_STRING("UPS.Output.0xFF86007C=1", resolve("shutdown.return", nullptr, with_delays).c_str());

    // HIDParser names the vendor usage by code (NUTUsages.h has no APC names): the row's path
    std::vector<HIDUsageDef> by_code = { mk("UPS.Output.0xFF86007C", 0xff86007c, 0x03) };
    TEST_ASSERT_EQUAL_STRING("UPS.Output.0xFF86007C=1", resolve("shutdown.return", nullptr, by_code).c_str());
    // apc-hid.c uses that field for shutdown.return only: never for shutdown.reboot
    resolve("shutdown.reboot", nullptr, by_code, HidControl::StepsResult::NOT_SUPPORTED);

    // Deeper than Output, or INPUT: no quirk
    std::vector<HIDUsageDef> deeper = { mk("UPS.Output.0xFF860005.0xFF86007C", 0xff86007c, 0x03) };
    resolve("shutdown.return", nullptr, deeper, HidControl::StepsResult::NOT_SUPPORTED);
    std::vector<HIDUsageDef> input = { mk("UPS.Output.0xFF86007C", 0xff86007c, 0x01) };
    resolve("shutdown.return", nullptr, input, HidControl::StepsResult::NOT_SUPPORTED);
}

// apc-hid.c: UPS.Output.APCDelayBeforeReboot serves shutdown.return = 1 only; shutdown.reboot
// uses the standard DelayBeforeReboot and UPS.APCGeneralCollection.APCDelayBeforeReboot
void test_reboot_group_excludes_apc_cs_field() {
    // CS field alone: no shutdown.reboot, neither in LIST CMD nor as a sequence
    std::vector<HIDUsageDef> cs_only = { mk("UPS.Output.0xFF86007C", 0xff86007c, 0x03) };
    TEST_ASSERT_EQUAL(std::string::npos, names(buildGeneric(cs_only, false)).find("shutdown.reboot"));
    resolve("shutdown.reboot", nullptr, cs_only, HidControl::StepsResult::NOT_SUPPORTED);

    // With the APCGeneralCollection field, that one is the reboot field even after the CS one
    std::vector<HIDUsageDef> both = {
        mk("UPS.Output.0xFF86007C", 0xff86007c, 0x03),
        mk("UPS.0xFF860005.0xFF86007C", 0xff86007c, 0x03),
    };
    TEST_ASSERT_EQUAL_STRING("UPS.0xFF860005.0xFF86007C=10", resolve("shutdown.reboot", nullptr, both).c_str());
    TEST_ASSERT_EQUAL_STRING("UPS.Output.0xFF86007C=1", resolve("shutdown.return", nullptr, both).c_str());
    // shutdown.return from the Back-UPS CS row of its own (apc-hid.c:461), as on APCDriver (US-076):
    // before US-079 the generic groups offered it only with both delays
    TEST_ASSERT_EQUAL_STRING("shutdown.return shutdown.reboot shutdown.default", names(buildGeneric(both, false)).c_str());

    // Standard DelayBeforeReboot next to the CS field (Back-UPS CS 500)
    std::vector<HIDUsageDef> standard = {
        mk("UPS.Output.0xFF86007C", 0xff86007c, 0x03),
        mk("UPS.Output.DelayBeforeReboot", 0x00840055, 0x03),
    };
    TEST_ASSERT_EQUAL_STRING("UPS.Output.DelayBeforeReboot=10", resolve("shutdown.reboot", nullptr, standard).c_str());
}

void test_steps_invalid_param() {
    auto u = allDelays();
    const HidControl::StepsResult inv = HidControl::StepsResult::INVALID_ARGUMENT;
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
    const HidControl::StepsResult ns = HidControl::StepsResult::NOT_SUPPORTED;
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
    const HidControl::StepsResult inv = HidControl::StepsResult::INVALID_ARGUMENT;
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
    const HidControl::StepsResult ns = HidControl::StepsResult::NOT_SUPPORTED;
    const int32_t none = HidControl::kNoDelay;
    std::vector<HidControl::WriteStep> steps;
    TEST_ASSERT_EQUAL((int)ns, (int)stepsGeneric("load.off.delay", nullptr, u, none, none, steps));
    TEST_ASSERT_EQUAL((int)ns, (int)stepsGeneric("load.on.delay", nullptr, u, none, none, steps));
    TEST_ASSERT_EQUAL((int)ns, (int)stepsGeneric("shutdown.return", nullptr, u, none, none, steps));
    TEST_ASSERT_EQUAL((int)ns, (int)stepsGeneric("shutdown.stayoff", nullptr, u, none, none, steps));
    TEST_ASSERT_EQUAL(0, steps.size());
    TEST_ASSERT_EQUAL((int)HidControl::StepsResult::OK,
                      (int)stepsGeneric("load.off.delay", "45", u, none, none, steps));
    TEST_ASSERT_EQUAL_STRING(SD "=45", stepsStr(steps).c_str());
    TEST_ASSERT_EQUAL((int)HidControl::StepsResult::OK,
                      (int)stepsGeneric("load.off", nullptr, u, none, none, steps));
}

void test_shutdown_default_availability() {
    std::vector<HIDUsageDef> reboot_only = { mk(RB, 0x00840055, 0x03) };
    TEST_ASSERT_EQUAL_STRING("shutdown.reboot shutdown.default",
                             names(buildGeneric(reboot_only, false)).c_str());
    std::vector<HIDUsageDef> none = { mk("UPS.BatterySystem.Battery.Test", 0x00840058, 0x03) };
    TEST_ASSERT_EQUAL_STRING("test.battery.start.quick test.battery.start.deep test.battery.stop",
                             names(buildGeneric(none, false)).c_str());
    const UPSCommandInfo* c = UpsCommands::find("shutdown.default");
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_TRUE(c->destructive);
    TEST_ASSERT_EQUAL_STRING("Run the driver-defined UPS shutdown sequence (as opposed to user-configured 'sdcommands')",
                             c->description);
}

void test_build_without_shutdown_commands() {
    std::vector<HIDUsageDef> usages = allDelays();
    usages.push_back(mk("UPS.BatterySystem.Battery.Test", 0x00840058, 0x03));
    // The rows match the path, as HIDParser names an unnamed usage ("0x%08X"), not the code
    usages.push_back(mk("UPS.PowerSummary.0xFF860072", 0xff860072, 0x03));
    usages.push_back(beeperField(1, 0x03, 0, 1)); // US-079: the beeper rows need the usage
    TEST_ASSERT_EQUAL_STRING(
        "beeper.enable beeper.disable beeper.toggle test.battery.start.quick test.battery.start.deep "
        "test.battery.stop test.panel.start test.panel.stop",
        names(buildGeneric(usages, true, false)).c_str());
}

// The plain test.battery.start is a row of liebert-hid.c only (US-097, US-106): the generic
// table offers quick/deep/stop
void test_plain_battery_test_only_from_its_row() {
    std::vector<HIDUsageDef> usages = { mk("UPS.BatterySystem.Battery.Test", 0x00840058, 0x03) };
    TEST_ASSERT_EQUAL_STRING("test.battery.start.quick test.battery.start.deep test.battery.stop",
                             names(buildGeneric(usages, false)).c_str());
    size_t n;
    const NutTable::HidInfo* t = NutTable::liebertHid2Nut(n);
    TEST_ASSERT_EQUAL_STRING("test.battery.start", names(HidControl::buildCommands(usages, false, true, true, t, n)).c_str());
    const UPSCommandInfo* c = UpsCommands::find("test.battery.start");
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_STRING("Start a battery test", c->description);
    TEST_ASSERT_FALSE(c->destructive);
}

void test_resolve_plain_battery_test_writes_1() {
    std::vector<HIDUsageDef> usages = { mk("UPS.BatterySystem.Battery.Test", 0x00840058, 0x03) };
    HIDUsageDef def;
    uint32_t value = 99;
    TEST_ASSERT_FALSE(resolveGeneric("test.battery.start", usages, def, value)); // no generic row
    size_t n;
    const NutTable::HidInfo* t = NutTable::liebertHid2Nut(n); // liebert-hid.c:137
    TEST_ASSERT_TRUE(HidControl::resolveWrite("test.battery.start", usages, def, value, t, n));
    TEST_ASSERT_EQUAL_UINT32(1, value);
    TEST_ASSERT_EQUAL_STRING("UPS.BatterySystem.Battery.Test", def.path);
}

void test_apply_default_delays() {
    UPSData d;
    NutTable::applyDefaultDelays(d, allDelays(), 20, 30, true, NutTable::genericDelayPaths());
    TEST_ASSERT_EQUAL_STRING("20", d.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_EQUAL_STRING("30", d.get("ups.delay.start").c_str());

    UPSData cps;
    NutTable::applyDefaultDelays(cps, allDelays(), 60, 120, true, NutTable::genericDelayPaths());
    TEST_ASSERT_EQUAL_STRING("60", cps.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_EQUAL_STRING("120", cps.get("ups.delay.start").c_str());

    UPSData only_sd;
    NutTable::applyDefaultDelays(only_sd, { mk(SD, 0x00840057, 0x03) }, 20, 30, true, NutTable::genericDelayPaths());
    TEST_ASSERT_TRUE(only_sd.hasKey("ups.delay.shutdown"));
    TEST_ASSERT_FALSE(only_sd.hasKey("ups.delay.start"));

    UPSData only_st;
    NutTable::applyDefaultDelays(only_st, { mk(ST, 0x00840056, 0x03) }, 20, 30, true, NutTable::genericDelayPaths());
    TEST_ASSERT_FALSE(only_st.hasKey("ups.delay.shutdown"));
    TEST_ASSERT_TRUE(only_st.hasKey("ups.delay.start"));

    UPSData disabled;
    NutTable::applyDefaultDelays(disabled, allDelays(), 20, 30, false, NutTable::genericDelayPaths());
    TEST_ASSERT_FALSE(disabled.hasKey("ups.delay.shutdown"));
    TEST_ASSERT_FALSE(disabled.hasKey("ups.delay.start"));

    UPSData none;
    NutTable::applyDefaultDelays(none, { mk(RB, 0x00840055, 0x03) }, 20, 30, true, NutTable::genericDelayPaths());
    TEST_ASSERT_FALSE(none.hasKey("ups.delay.shutdown"));
    TEST_ASSERT_FALSE(none.hasKey("ups.delay.start"));

    // US-066: the HU_FLAG_ABSENT rows need the path in any report type, as
    // HIDGetItemData(); the commands still need a FEATURE field to write
    UPSData input_only;
    std::vector<HIDUsageDef> input = { mk(SD, 0x00840057, 0x01) };
    NutTable::applyDefaultDelays(input_only, input, 20, 30, true, NutTable::genericDelayPaths());
    TEST_ASSERT_EQUAL_STRING("20", input_only.get("ups.delay.shutdown").c_str());
    size_t n_generic;
    const NutTable::HidInfo* generic = NutTable::genericHid2Nut(n_generic);
    TEST_ASSERT_NULL(HidControl::cmdField(generic, n_generic, input, "load.off.delay"));

    // A vendor path for ups.delay.shutdown (Powercom PCMDelayBeforeShutdown, 0x00ff0057)
    UPSData vendor;
    NutTable::applyDefaultDelays(vendor, { mk("UPS.PowerSummary.0x00FF0057", 0x00ff0057, 0x03) }, 20, 60, true,
                                       NutTable::powercomDelayPaths());
    TEST_ASSERT_EQUAL_STRING("20", vendor.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_FALSE(vendor.hasKey("ups.delay.start"));
    UPSData no_vendor;
    NutTable::applyDefaultDelays(no_vendor, { mk("UPS.PowerSummary.0x00FF0057", 0x00ff0057, 0x03) }, 20, 60, true, NutTable::genericDelayPaths());
    TEST_ASSERT_FALSE(no_vendor.hasKey("ups.delay.shutdown"));

    // Each subdriver has its own paths, as usbhid-ups: cps-hid.c:386-387 declares only
    // UPS.Output.*, mge-hid.c:2066-2067 only UPS.PowerSummary.*
    std::vector<HIDUsageDef> summary = { mk(SD, 0x00840057, 0x03), mk(ST, 0x00840056, 0x03) };
    UPSData cps_summary;
    NutTable::applyDefaultDelays(cps_summary, summary, 60, 120, true, NutTable::cpsDelayPaths());
    TEST_ASSERT_FALSE(cps_summary.hasKey("ups.delay.shutdown"));
    TEST_ASSERT_FALSE(cps_summary.hasKey("ups.delay.start"));
    UPSData mge_summary;
    NutTable::applyDefaultDelays(mge_summary, summary, 20, 30, true, NutTable::mgeDelayPaths());
    TEST_ASSERT_EQUAL_STRING("20", mge_summary.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_EQUAL_STRING("30", mge_summary.get("ups.delay.start").c_str());
    std::vector<HIDUsageDef> output = { mk("UPS.Output.DelayBeforeShutdown", 0x00840057, 0x03) };
    UPSData mge_output;
    NutTable::applyDefaultDelays(mge_output, output, 20, 30, true, NutTable::mgeDelayPaths());
    TEST_ASSERT_FALSE(mge_output.hasKey("ups.delay.shutdown"));
    // apc-hid.c:367-368: the APC codes only under APCGeneralCollection (0xff860005)
    UPSData apc_general;
    NutTable::applyDefaultDelays(apc_general, { mk("UPS.0xFF860005.0xFF86007D", 0xff86007d, 0x03) }, 20, 30, true,
                                       NutTable::apcDelayPaths());
    TEST_ASSERT_TRUE(apc_general.hasKey("ups.delay.shutdown"));
    UPSData apc_elsewhere;
    NutTable::applyDefaultDelays(apc_elsewhere, { mk("UPS.Output.0xFF86007D", 0xff86007d, 0x03) }, 20, 30, true,
                                       NutTable::apcDelayPaths());
    TEST_ASSERT_FALSE(apc_elsewhere.hasKey("ups.delay.shutdown"));
}

// --- US-067: driver-side variables saved with SET VAR ---

typedef std::vector<std::pair<String, String>> SavedVars;

void test_driver_var_store_json_round_trip() {
    SavedVars vars;
    vars.push_back(std::make_pair(String("ups.delay.shutdown"), String("60")));
    vars.push_back(std::make_pair(String("outlet.1.desc"), String("Rack \"A\" NAS")));
    vars.push_back(std::make_pair(String("ups.delay.start"), String("0")));
    String json = DriverVarStore::toJson(vars);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ups.delay.shutdown\":\"60\",\"outlet.1.desc\":\"Rack \\\"A\\\" NAS\",\"ups.delay.start\":\"0\"}",
        json.c_str());

    SavedVars back;
    TEST_ASSERT_TRUE(DriverVarStore::fromJson(json, back));
    TEST_ASSERT_EQUAL(3, back.size());
    TEST_ASSERT_EQUAL_STRING("ups.delay.shutdown", back[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("60", back[0].second.c_str());
    TEST_ASSERT_EQUAL_STRING("outlet.1.desc", back[1].first.c_str());
    TEST_ASSERT_EQUAL_STRING("Rack \"A\" NAS", back[1].second.c_str());
    TEST_ASSERT_EQUAL_STRING("ups.delay.start", back[2].first.c_str());
    TEST_ASSERT_EQUAL_STRING("0", back[2].second.c_str());

    // Nothing saved yet
    TEST_ASSERT_EQUAL_STRING("{}", DriverVarStore::toJson(SavedVars()).c_str());
    TEST_ASSERT_TRUE(DriverVarStore::fromJson("{}", back));
    TEST_ASSERT_EQUAL(0, back.size());
}

void test_driver_var_store_corrupt_json() {
    const char* corrupt[] = {
        "{\"ups.delay.shutdown\":\"60\"",   // truncated
        "",                                 // empty
        "[\"ups.delay.shutdown\",\"60\"]",  // not an object
        "{\"ups.delay.shutdown\":60}",      // not a string
        "{\"outlet.1.desc\":\"PC\",\"ups.delay.start\":null}",
    };
    for (size_t i = 0; i < sizeof(corrupt) / sizeof(corrupt[0]); i++) {
        SavedVars out;
        out.push_back(std::make_pair(String("stale"), String("1")));
        TEST_ASSERT_FALSE_MESSAGE(DriverVarStore::fromJson(corrupt[i], out), corrupt[i]);
        TEST_ASSERT_EQUAL_MESSAGE(0, out.size(), corrupt[i]);
    }
}

void test_driver_var_store_upsert() {
    SavedVars vars;
    DriverVarStore::upsert(vars, "ups.delay.shutdown", "60");
    DriverVarStore::upsert(vars, "outlet.1.desc", "PC");
    DriverVarStore::upsert(vars, "ups.delay.shutdown", "90");
    TEST_ASSERT_EQUAL(2, vars.size());
    TEST_ASSERT_EQUAL_STRING("ups.delay.shutdown", vars[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("90", vars[0].second.c_str());
    TEST_ASSERT_EQUAL_STRING("outlet.1.desc", vars[1].first.c_str());
    TEST_ASSERT_EQUAL_STRING("PC", vars[1].second.c_str());
    // Empty description: a value like any other
    DriverVarStore::upsert(vars, "outlet.1.desc", "");
    TEST_ASSERT_EQUAL(2, vars.size());
    TEST_ASSERT_EQUAL_STRING("", vars[1].second.c_str());
    TEST_ASSERT_EQUAL_STRING("{\"ups.delay.shutdown\":\"90\",\"outlet.1.desc\":\"\"}",
                             DriverVarStore::toJson(vars).c_str());
}

void test_apply_default_delays_with_saved_values() {
    // Synthetic Eaton descriptor: mge-hid.c paths, driver defaults 20/30
    std::vector<HIDUsageDef> eaton = allDelays();
    SavedVars saved;
    saved.push_back(std::make_pair(String("ups.delay.shutdown"), String("60")));
    UPSData d;
    NutTable::applyDefaultDelays(d, eaton, 20, 30, true, NutTable::mgeDelayPaths(), &saved);
    TEST_ASSERT_EQUAL_STRING("60", d.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_EQUAL_STRING("30", d.get("ups.delay.start").c_str());

    // Next string descriptor: the host calls it again, the saved value stays
    NutTable::applyDefaultDelays(d, eaton, 20, 30, true, NutTable::mgeDelayPaths(), &saved);
    TEST_ASSERT_EQUAL_STRING("60", d.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_EQUAL_STRING("30", d.get("ups.delay.start").c_str());

    // 0 is a valid delay
    SavedVars zero;
    zero.push_back(std::make_pair(String("ups.delay.start"), String("0")));
    UPSData z;
    NutTable::applyDefaultDelays(z, eaton, 20, 30, true, NutTable::mgeDelayPaths(), &zero);
    TEST_ASSERT_EQUAL_STRING("20", z.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_EQUAL_STRING("0", z.get("ups.delay.start").c_str());

    // Not an integer >= 0 any more: the default stays
    const char* invalid[] = { "abc", "-1", "", "30.5", "60s" };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        SavedVars bad;
        bad.push_back(std::make_pair(String("ups.delay.shutdown"), String(invalid[i])));
        UPSData b;
        NutTable::applyDefaultDelays(b, eaton, 20, 30, true, NutTable::mgeDelayPaths(), &bad);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("20", b.get("ups.delay.shutdown").c_str(), invalid[i]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("30", b.get("ups.delay.start").c_str(), invalid[i]);
    }

    // Other saved variables are left alone
    SavedVars other;
    other.push_back(std::make_pair(String("outlet.1.desc"), String("90")));
    other.push_back(std::make_pair(String("UPS.DELAY.SHUTDOWN"), String("90")));
    UPSData o;
    NutTable::applyDefaultDelays(o, eaton, 20, 30, true, NutTable::mgeDelayPaths(), &other);
    TEST_ASSERT_EQUAL_STRING("20", o.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_EQUAL_STRING("30", o.get("ups.delay.start").c_str());
    TEST_ASSERT_FALSE(o.hasKey("outlet.1.desc"));
}

void test_saved_delays_only_for_present_variables() {
    SavedVars saved;
    saved.push_back(std::make_pair(String("ups.delay.shutdown"), String("60")));
    saved.push_back(std::make_pair(String("ups.delay.start"), String("90")));

    // Driver without delay variables (delay_vars false): no key at all
    UPSData disabled;
    NutTable::applyDefaultDelays(disabled, allDelays(), 20, 30, false, NutTable::genericDelayPaths(), &saved);
    TEST_ASSERT_FALSE(disabled.hasKey("ups.delay.shutdown"));
    TEST_ASSERT_FALSE(disabled.hasKey("ups.delay.start"));

    // Device without DelayBeforeStartup: ups.delay.start does not exist for it
    UPSData only_sd;
    NutTable::applyDefaultDelays(only_sd, { mk(SD, 0x00840057, 0x03) }, 20, 30, true,
                                       NutTable::mgeDelayPaths(), &saved);
    TEST_ASSERT_EQUAL_STRING("60", only_sd.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_FALSE(only_sd.hasKey("ups.delay.start"));

    // No delay path at all
    UPSData none;
    NutTable::applyDefaultDelays(none, { mk(RB, 0x00840055, 0x03) }, 20, 30, true, NutTable::genericDelayPaths(), &saved);
    TEST_ASSERT_FALSE(none.hasKey("ups.delay.shutdown"));
    TEST_ASSERT_FALSE(none.hasKey("ups.delay.start"));

    // Empty saved list: the driver defaults, as without the parameter
    SavedVars empty;
    UPSData dflt;
    NutTable::applyDefaultDelays(dflt, allDelays(), 20, 30, true, NutTable::genericDelayPaths(), &empty);
    TEST_ASSERT_EQUAL_STRING("20", dflt.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_EQUAL_STRING("30", dflt.get("ups.delay.start").c_str());
}

// --- US-068: reset of the driver-side variables to the driver defaults ---

void test_driver_var_store_remove() {
    SavedVars vars;
    DriverVarStore::upsert(vars, "ups.delay.shutdown", "60");
    DriverVarStore::upsert(vars, "outlet.1.desc", "PC");
    DriverVarStore::upsert(vars, "ups.delay.start", "90");

    // The middle entry goes, the others keep their order
    TEST_ASSERT_TRUE(DriverVarStore::remove(vars, "outlet.1.desc"));
    TEST_ASSERT_EQUAL(2, vars.size());
    TEST_ASSERT_EQUAL_STRING("ups.delay.shutdown", vars[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("60", vars[0].second.c_str());
    TEST_ASSERT_EQUAL_STRING("ups.delay.start", vars[1].first.c_str());
    TEST_ASSERT_EQUAL_STRING("90", vars[1].second.c_str());
    TEST_ASSERT_EQUAL_STRING("{\"ups.delay.shutdown\":\"60\",\"ups.delay.start\":\"90\"}",
                             DriverVarStore::toJson(vars).c_str());

    // Not saved (again, other case, null): nothing changes
    TEST_ASSERT_FALSE(DriverVarStore::remove(vars, "outlet.1.desc"));
    TEST_ASSERT_FALSE(DriverVarStore::remove(vars, "UPS.DELAY.START"));
    TEST_ASSERT_FALSE(DriverVarStore::remove(vars, nullptr));
    TEST_ASSERT_EQUAL(2, vars.size());

    TEST_ASSERT_TRUE(DriverVarStore::remove(vars, "ups.delay.shutdown"));
    TEST_ASSERT_TRUE(DriverVarStore::remove(vars, "ups.delay.start"));
    TEST_ASSERT_EQUAL(0, vars.size());

    // Empty list stays empty
    TEST_ASSERT_FALSE(DriverVarStore::remove(vars, "ups.delay.start"));
    TEST_ASSERT_EQUAL(0, vars.size());
    TEST_ASSERT_EQUAL_STRING("{}", DriverVarStore::toJson(vars).c_str());
}

void test_default_delays_after_reset() {
    // Synthetic Eaton descriptor: mge-hid.c paths, driver defaults 20/30
    std::vector<HIDUsageDef> eaton = allDelays();
    SavedVars saved;
    saved.push_back(std::make_pair(String("ups.delay.shutdown"), String("60")));
    saved.push_back(std::make_pair(String("ups.delay.start"), String("90")));
    UPSData d;
    NutTable::applyDefaultDelays(d, eaton, 20, 30, true, NutTable::mgeDelayPaths(), &saved);
    TEST_ASSERT_EQUAL_STRING("60", d.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_EQUAL_STRING("90", d.get("ups.delay.start").c_str());

    // Reset of ups.delay.shutdown: the same UPSData gets the driver default again, the
    // other saved delay stays
    TEST_ASSERT_TRUE(DriverVarStore::remove(saved, "ups.delay.shutdown"));
    NutTable::applyDefaultDelays(d, eaton, 20, 30, true, NutTable::mgeDelayPaths(), &saved);
    TEST_ASSERT_EQUAL_STRING("20", d.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_EQUAL_STRING("90", d.get("ups.delay.start").c_str());
}

// --- Commands from the mge-hid table (US-074) ---

// Usages of a fixture's report descriptor (the "report_descriptor_hex" array)
static std::vector<HIDUsageDef> fixtureUsages(const char* file, HIDParser& parser) {
    std::ifstream f(file);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    size_t a = text.find("\"report_descriptor_hex\"");
    size_t b = text.find(']', a);
    std::vector<uint8_t> desc;
    for (size_t p = text.find("0x", a); p != std::string::npos && p < b; p = text.find("0x", p + 2)) {
        desc.push_back((uint8_t)strtol(text.substr(p + 2, 2).c_str(), nullptr, 16));
    }
    TEST_ASSERT_TRUE_MESSAGE(desc.size() > 0, file);
    parser.parseReportDescriptor(desc.data(), desc.size());
    return parser.getUsages();
}

// The commands, steps and values of a table (mgeHid2Nut by default) are those of the
// catalog without a table, with the driver's configured delays
static void checkSameCommandsAsGeneric(const char* file, const NutTable::HidInfo* (*table)(size_t&) = NutTable::mgeHid2Nut,
                                       int32_t off_delay = 20, int32_t on_delay = 30) {
    HIDParser parser;
    std::vector<HIDUsageDef> u = fixtureUsages(file, parser);
    size_t n;
    const NutTable::HidInfo* mge = table(n);
    for (bool beeper : { true, false }) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(names(buildGeneric(u, beeper)).c_str(),
            names(HidControl::buildCommands(u, beeper, true, true, mge, n)).c_str(), file);
    }
    // Same writes for every load / shutdown command, with and without a parameter
    static const char* const cmds[] = { "load.off", "load.on", "load.off.delay", "load.on.delay", "shutdown.stop",
                                        "shutdown.return", "shutdown.stayoff" };
    for (const char* c : cmds) {
        for (const char* param : { (const char*)nullptr, "45" }) {
            // A shutdown.return row takes the parameter (instcmd(), US-076); the table-less
            // Back-UPS CS case ignored it: compared without a parameter only
            if (param && strcmp(c, "shutdown.return") == 0 && HidControl::cmdField(mge, n, u, c)) continue;
            std::vector<HidControl::WriteStep> legacy, table_steps;
            auto r1 = stepsGeneric(c, param, u, off_delay, on_delay, legacy);
            auto r2 = HidControl::resolveShutdownSteps(c, param, u, off_delay, on_delay, table_steps, mge, n);
            TEST_ASSERT_EQUAL_MESSAGE((int)r1, (int)r2, c);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(stepsStr(legacy).c_str(), stepsStr(table_steps).c_str(), c);
        }
    }
}

void test_mge_table_gives_same_commands_on_eaton_3s() {
    checkSameCommandsAsGeneric("test/fixtures/eaton/eaton_3s_vid0463_pidffff.json");
}

void test_mge_table_gives_same_commands_on_eaton_5p() {
    checkSameCommandsAsGeneric("test/fixtures/eaton/eaton_5p_vid0463_pidffff_issue18.json");
}

void test_mge_table_mute_on_first_row_field() {
    // mge-hid.c:2337: beeper.mute on UPS.BatterySystem.Battery.AudibleAlarmControl, 3
    HIDParser parser;
    std::vector<HIDUsageDef> u = fixtureUsages("test/fixtures/eaton/eaton_3s_vid0463_pidffff.json", parser);
    size_t n;
    const NutTable::HidInfo* mge = NutTable::mgeHid2Nut(n);
    HIDUsageDef def;
    uint32_t value = 0;
    TEST_ASSERT_TRUE(HidControl::resolveWrite("beeper.mute", u, def, value, mge, n));
    TEST_ASSERT_EQUAL_STRING("UPS.BatterySystem.Battery.AudibleAlarmControl", def.path);
    TEST_ASSERT_EQUAL(3, value);
    TEST_ASSERT_FALSE(HidControl::resolveWrite("beeper.enable", u, def, value, mge, n)); // via setBeeperField

    // A 1-bit beeper cannot hold 3: no beeper.mute
    HIDUsageDef bit = mk("UPS.PowerSummary.AudibleAlarmControl", 0x0084005a, 0x03);
    bit.bit_size = 1;
    std::vector<HIDUsageDef> one = { bit };
    TEST_ASSERT_EQUAL_STRING("beeper.enable beeper.disable beeper.toggle",
                             names(HidControl::buildCommands(one, true, true, true, mge, n)).c_str());
}

void test_mge_table_commands_outside_catalog_not_listed() {
    // outlet.N.load.*, experimental.*, bypass.* and beeper.on/off are rows of mge-hid.c
    // (2333-2334, 2347-2367) but not names of UpsCommands.h
    std::vector<HIDUsageDef> u = {
        mk("UPS.PowerConverter.Input.Switchable", 0x0084006c, 0x03),
        mk("UPS.PowerSummary.AudibleAlarmControl", 0x0084005a, 0x03),
    };
    u[0].coll_index[2] = 6; // Input.[5]
    HIDUsageDef outlet = mk("UPS.OutletSystem.Outlet.DelayBeforeShutdown", 0x00840057, 0x03);
    outlet.coll_index[2] = 3; // Outlet.[2]
    u.push_back(outlet);
    size_t n;
    const NutTable::HidInfo* mge = NutTable::mgeHid2Nut(n);
    TEST_ASSERT_EQUAL_STRING("beeper.enable beeper.disable beeper.toggle beeper.mute",
                             names(HidControl::buildCommands(u, true, true, true, mge, n)).c_str());
    TEST_ASSERT_NOT_NULL(HidControl::cmdField(mge, n, u, "outlet.1.load.off"));
    TEST_ASSERT_NOT_NULL(HidControl::cmdField(mge, n, u, "experimental.ecomode.start"));
}

void test_mge_table_reboot_from_row() {
    // mge-hid.c:2332: shutdown.reboot on PowerSummary.DelayBeforeReboot, default 10
    std::vector<HIDUsageDef> u = { mk(RB, 0x00840055, 0x03) };
    size_t n;
    const NutTable::HidInfo* mge = NutTable::mgeHid2Nut(n);
    TEST_ASSERT_EQUAL_STRING("shutdown.reboot shutdown.default",
                             names(HidControl::buildCommands(u, false, true, true, mge, n)).c_str());
    std::vector<HidControl::WriteStep> steps;
    TEST_ASSERT_EQUAL((int)HidControl::StepsResult::OK,
        (int)HidControl::resolveShutdownSteps("shutdown.reboot", nullptr, u, 20, 30, steps, mge, n));
    TEST_ASSERT_EQUAL_STRING(RB "=10", stepsStr(steps).c_str());
    TEST_ASSERT_EQUAL((int)HidControl::StepsResult::OK,
        (int)HidControl::resolveShutdownSteps("shutdown.reboot", "30", u, 20, 30, steps, mge, n));
    TEST_ASSERT_EQUAL_STRING(RB "=30", stepsStr(steps).c_str());
}

void test_mge_table_mute_respects_declared_range() {
    // A 2-bit beeper declared 1..2 cannot take 3: no beeper.mute (as muteField())
    HIDUsageDef b = mk("UPS.PowerSummary.AudibleAlarmControl", 0x0084005a, 0x03);
    b.bit_size = 2;
    b.logical_min = 1;
    b.logical_max = 2;
    std::vector<HIDUsageDef> u = { b };
    size_t n;
    const NutTable::HidInfo* mge = NutTable::mgeHid2Nut(n);
    TEST_ASSERT_EQUAL_STRING("beeper.enable beeper.disable beeper.toggle",
                             names(HidControl::buildCommands(u, true, true, true, mge, n)).c_str());
    HIDUsageDef def;
    uint32_t value = 0;
    TEST_ASSERT_FALSE(HidControl::resolveWrite("beeper.mute", u, def, value, mge, n));
}

// --- Commands from the cps-hid table (US-075) ---

// Same LIST CMD and same load / shutdown writes on every CyberPower fixture, with the
// CyberPower delays 60/120 (US-058)
void test_cps_table_gives_same_commands_on_fixtures() {
    static const char* const files[] = {
        "test/fixtures/cyberpower/cyberpower_br700elcd_vid0764_pid0501.json",
        "test/fixtures/cyberpower/cyberpower_cp1350c_vid0764_pid0501.json",
        "test/fixtures/cyberpower/cyberpower_cp1500epfclcd_vid0764_pid0501.json",
        "test/fixtures/cyberpower/cyberpower_cp1600epfclcd_vid0764_pid0601_issue48.json",
        "test/fixtures/cyberpower/cyberpower_ut850eg_vid0764_pid0501_discussion95.json",
    };
    for (const char* f : files) checkSameCommandsAsGeneric(f, NutTable::cpsHid2Nut, 60, 120);
}

void test_cps_table_write_values() {
    // cps-hid.c:427 test.battery.start.deep = 2 and 437 beeper.mute = 3
    HIDParser parser;
    std::vector<HIDUsageDef> u = fixtureUsages("test/fixtures/cyberpower/cyberpower_cp1500epfclcd_vid0764_pid0501.json", parser);
    size_t n;
    const NutTable::HidInfo* cps = NutTable::cpsHid2Nut(n);
    HIDUsageDef def;
    uint32_t value = 0;
    TEST_ASSERT_TRUE(HidControl::resolveWrite("test.battery.start.deep", u, def, value, cps, n));
    TEST_ASSERT_EQUAL_STRING("UPS.Output.Test", def.path);
    TEST_ASSERT_EQUAL(2, value);
    TEST_ASSERT_TRUE(HidControl::resolveWrite("beeper.mute", u, def, value, cps, n));
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.AudibleAlarmControl", def.path);
    TEST_ASSERT_EQUAL(3, value);
}

void test_cps_table_beeper_on_off_not_listed() {
    // beeper.on/off (cps-hid.c:433-434) are rows, not names of UpsCommands.h
    std::vector<HIDUsageDef> u = { mk("UPS.PowerSummary.AudibleAlarmControl", 0x0084005a, 0x03) };
    size_t n;
    const NutTable::HidInfo* cps = NutTable::cpsHid2Nut(n);
    TEST_ASSERT_EQUAL_STRING("beeper.enable beeper.disable beeper.toggle beeper.mute",
                             names(HidControl::buildCommands(u, true, true, true, cps, n)).c_str());
    TEST_ASSERT_NOT_NULL(HidControl::cmdField(cps, n, u, "beeper.on"));
}

void test_cps_table_reboot_only_from_output() {
    // cps-hid.c:432: shutdown.reboot on UPS.Output.DelayBeforeReboot, default 10
    std::vector<HIDUsageDef> out = { mk("UPS.Output.DelayBeforeReboot", 0x00840055, 0x03) };
    size_t n;
    const NutTable::HidInfo* cps = NutTable::cpsHid2Nut(n);
    TEST_ASSERT_EQUAL_STRING("shutdown.reboot shutdown.default",
                             names(HidControl::buildCommands(out, false, true, true, cps, n)).c_str());
    std::vector<HidControl::WriteStep> steps;
    TEST_ASSERT_EQUAL((int)HidControl::StepsResult::OK,
        (int)HidControl::resolveShutdownSteps("shutdown.reboot", nullptr, out, 60, 120, steps, cps, n));
    TEST_ASSERT_EQUAL_STRING("UPS.Output.DelayBeforeReboot=10", stepsStr(steps).c_str());
    // Difference from the catalog without a table (US-075): cps-hid has no row on
    // PowerSummary.DelayBeforeReboot, so a CyberPower declaring only that one has no
    // shutdown.reboot, as with usbhid-ups (no CyberPower fixture has it)
    std::vector<HIDUsageDef> ps = { mk(RB, 0x00840055, 0x03) };
    TEST_ASSERT_EQUAL_STRING("shutdown.reboot shutdown.default", names(buildGeneric(ps, false)).c_str());
    TEST_ASSERT_EQUAL_STRING("", names(HidControl::buildCommands(ps, false, true, true, cps, n)).c_str());
}

// --- Commands from the apc-hid table (US-076) ---

static const char* const kApcFixtures[] = {
    "test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98.json",
    "test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98_onbattery.json",
    "test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98_online.json",
    "test/fixtures/apc/apc_backups_bx1500g_vid051d_pid0002_issue55.json",
    "test/fixtures/apc/apc_backups_bx750mi_vid051d_pid0002_issue60.json",
    "test/fixtures/apc/apc_backups_cs500_vid051d_pid0002_issue18.json",
    "test/fixtures/apc/apc_backups_cs750rs_vid051d_pid0002_issue48.json",
    "test/fixtures/apc/apc_backups_es700g_vid051d_pid0002_issue71.json",
    "test/fixtures/apc/apc_backups_rs900mi_vid051d_pid0002_issue18.json",
    "test/fixtures/apc/apc_backups_xs700u_vid051d_pid0002_issue20.json",
    "test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue13.json",
    "test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue78.json",
    "test/fixtures/apc/apc_smartups_750_vid051d_pid0003_issue22.json",
};

// Same LIST CMD and same load / shutdown writes on every APC fixture (US-058 delays 20/30)
void test_apc_table_gives_same_commands_on_fixtures() {
    for (const char* f : kApcFixtures) checkSameCommandsAsGeneric(f, NutTable::apcHid2Nut, 20, 30);
}

// LIST CMD from apc_hid2nut, written by hand from the FEATURE usages of each descriptor and the
// command rows of apc-hid.c (not from the catalog without a table): direct rows, then what
// upsdrv_initinfo() derives (load.off / load.on, shutdown.return / stayoff), then the project
// rules (beeper.toggle, shutdown.default), in the order of UpsCommands.h
void test_apc_table_commands_from_upstream_rows() {
    static const struct { const char* file; const char* expected; } cases[] = {
        // 435/437/439 UPS.Battery.Test, 446/448 PowerSummary.DelayBeforeShutdown,
        // 463-467 PowerSummary.AudibleAlarmControl; no DelayBeforeStartup, no DelayBeforeReboot
        { "test/fixtures/apc/apc_backups_xs700u_vid051d_pid0002_issue20.json",
          "beeper.enable beeper.disable beeper.toggle beeper.mute test.battery.start.quick test.battery.start.deep "
          "test.battery.stop load.off load.off.delay shutdown.stop shutdown.default" },
        // 440/441 UPS.APCPanelTest, 456/458/459 APCGeneralCollection.APCDelayBeforeShutdown and
        // APCDelayBeforeReboot (no APCDelayBeforeStartup), 463-467; no Battery.Test (issue #71)
        { "test/fixtures/apc/apc_backups_es700g_vid051d_pid0002_issue71.json",
          "beeper.enable beeper.disable beeper.toggle beeper.mute test.panel.start test.panel.stop "
          "load.off load.off.delay shutdown.stop shutdown.reboot shutdown.default" },
        // 435/437/439, 440/441, 446/448/449 PowerSummary delays, 451-454 Output delays (452 gives
        // load.on.delay), 461 Output.APCDelayBeforeReboot (shutdown.return), 463-467
        { "test/fixtures/apc/apc_backups_cs500_vid051d_pid0002_issue18.json",
          "beeper.enable beeper.disable beeper.toggle beeper.mute test.battery.start.quick test.battery.start.deep "
          "test.battery.stop test.panel.start test.panel.stop load.off load.on load.off.delay load.on.delay "
          "shutdown.return shutdown.stayoff shutdown.stop shutdown.reboot shutdown.default" },
    };
    size_t n;
    const NutTable::HidInfo* apc = NutTable::apcHid2Nut(n);
    for (const auto& c : cases) {
        HIDParser parser;
        std::vector<HIDUsageDef> u = fixtureUsages(c.file, parser);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.expected, names(HidControl::buildCommands(u, true, true, true, apc, n)).c_str(), c.file);
    }
}

void test_apc_cs500_shutdown_return_from_row() {
    // apc-hid.c:461: shutdown.return is one write of 1 on UPS.Output.APCDelayBeforeReboot, or
    // of the parameter (instcmd(), usbhid-ups.c:1112) when it fits the field's declared range;
    // shutdown.reboot never uses that field
    HIDParser parser;
    std::vector<HIDUsageDef> u = fixtureUsages("test/fixtures/apc/apc_backups_cs500_vid051d_pid0002_issue18.json", parser);
    size_t n;
    const NutTable::HidInfo* apc = NutTable::apcHid2Nut(n);
    std::vector<HidControl::WriteStep> steps;
    TEST_ASSERT_EQUAL((int)HidControl::StepsResult::OK,
        (int)HidControl::resolveShutdownSteps("shutdown.return", nullptr, u, 20, 30, steps, apc, n));
    TEST_ASSERT_EQUAL_STRING("UPS.Output.0xFF86007C=1", stepsStr(steps).c_str());
    TEST_ASSERT_EQUAL((int)HidControl::StepsResult::OK,
        (int)HidControl::resolveShutdownSteps("shutdown.return", "0", u, 20, 30, steps, apc, n));
    TEST_ASSERT_EQUAL_STRING("UPS.Output.0xFF86007C=0", stepsStr(steps).c_str());
    TEST_ASSERT_EQUAL((int)HidControl::StepsResult::INVALID_ARGUMENT,
        (int)HidControl::resolveShutdownSteps("shutdown.return", "abc", u, 20, 30, steps, apc, n));
    TEST_ASSERT_EQUAL((int)HidControl::StepsResult::OK,
        (int)HidControl::resolveShutdownSteps("shutdown.reboot", nullptr, u, 20, 30, steps, apc, n));
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.DelayBeforeReboot=10", stepsStr(steps).c_str());

    // The row alone lists shutdown.return, without the two delays
    std::vector<HIDUsageDef> only = { mk("UPS.Output.0xFF86007C", 0xff86007c, 0x03) };
    TEST_ASSERT_EQUAL_STRING("shutdown.return shutdown.default",
                             names(HidControl::buildCommands(only, false, true, true, apc, n)).c_str());
}

void test_apc_panel_test_rows() {
    // apc-hid.c:440-443: test.panel.* on UPS.APCPanelTest, then UPS.PowerSummary.APCPanelTest
    size_t n;
    const NutTable::HidInfo* apc = NutTable::apcHid2Nut(n);
    HIDUsageDef def;
    uint32_t value = 0;
    for (const char* path : { "UPS.0xFF860072", "UPS.PowerSummary.0xFF860072" }) {
        std::vector<HIDUsageDef> u = { mk(path, 0xff860072, 0x03) };
        TEST_ASSERT_EQUAL_STRING_MESSAGE("test.panel.start test.panel.stop",
                                         names(HidControl::buildCommands(u, false, true, true, apc, n)).c_str(), path);
        TEST_ASSERT_TRUE(HidControl::resolveWrite("test.panel.start", u, def, value, apc, n));
        TEST_ASSERT_EQUAL_STRING(path, def.path);
        TEST_ASSERT_EQUAL(1, value);
        TEST_ASSERT_TRUE(HidControl::resolveWrite("test.panel.stop", u, def, value, apc, n));
        TEST_ASSERT_EQUAL(0, value);
    }
}

void test_apc_beeper_on_off_not_listed() {
    // beeper.on/off (apc-hid.c:463-464) are rows, not names of UpsCommands.h
    std::vector<HIDUsageDef> u = { mk("UPS.PowerSummary.AudibleAlarmControl", 0x0084005a, 0x03) };
    size_t n;
    const NutTable::HidInfo* apc = NutTable::apcHid2Nut(n);
    TEST_ASSERT_EQUAL_STRING("beeper.enable beeper.disable beeper.toggle beeper.mute",
                             names(HidControl::buildCommands(u, true, true, true, apc, n)).c_str());
}

// --- Commands from the powercom-hid table (US-077) ---

static const char* const kSpd750u = "test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json";

void test_powercom_spd750u_commands_from_table() {
    // SPD-750U: QUIRK_NO_BEEPER_CONTROL (no beeper) and shutdownCommandsSupported() false.
    // Only test.battery.start.quick (powercom-hid.c:661): powercom-hid has no
    // test.battery.start.deep / test.battery.stop rows, which the catalog without a table
    // offered from UPS.Battery.Test
    HIDParser parser;
    std::vector<HIDUsageDef> u = fixtureUsages(kSpd750u, parser);
    size_t n;
    const NutTable::HidInfo* t = NutTable::powercomHid2Nut(n);
    TEST_ASSERT_EQUAL_STRING("test.battery.start.quick", names(HidControl::buildCommands(u, false, false, true, t, n)).c_str());
    TEST_ASSERT_EQUAL_STRING("test.battery.start.quick test.battery.start.deep test.battery.stop",
                             names(buildGeneric(u, false, false, true)).c_str());
}

void test_powercom_beeper_without_quirk() {
    // The same descriptor on a Powercom without the quirk: beeper.enable/disable (659-660) and
    // toggle; no beeper.mute, which powercom-hid has no row for
    HIDParser parser;
    std::vector<HIDUsageDef> u = fixtureUsages(kSpd750u, parser);
    size_t n;
    const NutTable::HidInfo* t = NutTable::powercomHid2Nut(n);
    TEST_ASSERT_EQUAL_STRING("beeper.enable beeper.disable beeper.toggle test.battery.start.quick",
                             names(HidControl::buildCommands(u, true, false, true, t, n)).c_str());
}

void test_powercom_load_shutdown_never_listed() {
    // load.on.delay and shutdown.return/stayoff (662-664) are bound on the SPD-750U, load.on,
    // load.off, shutdown.return/stayoff (665-668, 729) on the PCM / vendor delays: none is
    // listed while shutdownCommandsSupported() is false (US-058)
    HIDParser parser;
    std::vector<HIDUsageDef> u = fixtureUsages(kSpd750u, parser);
    std::vector<HIDUsageDef> pcm = { mk("UPS.PowerSummary.0x00FF0056", 0x00ff0056, 0x03),
                                     mk("UPS.PowerSummary.0x00FF0057", 0x00ff0057, 0x03) };
    size_t n;
    const NutTable::HidInfo* t = NutTable::powercomHid2Nut(n);
    TEST_ASSERT_NOT_NULL(HidControl::cmdField(t, n, u, "load.on.delay"));
    TEST_ASSERT_NOT_NULL(HidControl::cmdField(t, n, u, "shutdown.return"));
    TEST_ASSERT_NOT_NULL(HidControl::cmdField(t, n, pcm, "load.off"));
    for (const auto* usages : { &u, &pcm }) {
        std::string list = names(HidControl::buildCommands(*usages, true, false, true, t, n));
        TEST_ASSERT_TRUE_MESSAGE(list.find("load.") == std::string::npos, list.c_str());
        TEST_ASSERT_TRUE_MESSAGE(list.find("shutdown.") == std::string::npos, list.c_str());
    }
}

void test_powercom_quick_test_value() {
    // powercom-hid.c:661: test.battery.start.quick writes 1 on UPS.Battery.Test
    HIDParser parser;
    std::vector<HIDUsageDef> u = fixtureUsages(kSpd750u, parser);
    size_t n;
    const NutTable::HidInfo* t = NutTable::powercomHid2Nut(n);
    HIDUsageDef def;
    uint32_t value = 0;
    TEST_ASSERT_TRUE(HidControl::resolveWrite("test.battery.start.quick", u, def, value, t, n));
    TEST_ASSERT_EQUAL_STRING("UPS.Battery.Test", def.path);
    TEST_ASSERT_EQUAL(1, value);
}

// --- Commands from the ecoflow-hid and openups-hid tables (US-078) ---

void test_ecoflow_commands_from_table() {
    // ecoflow-hid.c has its beeper.* and shutdown.* rows only in a comment: no command, whatever
    // the device has. EcoFlowDriver also says beeper not controllable and no shutdown commands
    // (US-054, US-058). Without a table the catalog offered the battery tests of UPS.Battery.Test
    std::vector<HIDUsageDef> u = { mk("UPS.PowerSummary.AudibleAlarmControl", 0x0084005a, 0x03),
                                   mk("UPS.Battery.Test", 0x00840058, 0x03),
                                   mk("UPS.OutletSystem.Outlet.DelayBeforeShutdown", 0x00840057, 0x03),
                                   mk("UPS.OutletSystem.Outlet.DelayBeforeReboot", 0x00840055, 0x03) };
    size_t n;
    const NutTable::HidInfo* t = NutTable::ecoflowHid2Nut(n);
    TEST_ASSERT_EQUAL_STRING("", names(HidControl::buildCommands(u, false, false, true, t, n)).c_str());
    TEST_ASSERT_EQUAL_STRING("test.battery.start.quick test.battery.start.deep test.battery.stop",
                             names(buildGeneric(u, false, false, true)).c_str());
}

void test_openups_commands_from_table() {
    // openups-hid.c has no command rows: nothing, even on usages the generic catalog would
    // use (the W150 replay checks its own empty LIST CMD)
    std::vector<HIDUsageDef> u = { mk("UPS.PowerSummary.AudibleAlarmControl", 0x0084005a, 0x03),
                                   mk("UPS.Battery.Test", 0x00840058, 0x03) };
    size_t n;
    const NutTable::HidInfo* t = NutTable::openupsHid2Nut(n);
    TEST_ASSERT_EQUAL_STRING("", names(HidControl::buildCommands(u, true, false, true, t, n)).c_str());
}

// --- beeper.toggle from a row of its own (liebert-hid.c:141, US-106) ---

void test_beeper_toggle_from_its_own_row() {
    static const NutTable::HidInfo toggleOnly[] = {
        { "beeper.toggle", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "1", NutTable::HU_TYPE_CMD, NutTable::Conv::Number, nullptr, 0 },
    };
    std::vector<HIDUsageDef> u = { mk("UPS.PowerSummary.AudibleAlarmControl", 0x0084005a, 0x03) };
    TEST_ASSERT_EQUAL_STRING("beeper.toggle", names(HidControl::buildCommands(u, true, false, true, toggleOnly, 1)).c_str());
    // Not controllable: nothing
    TEST_ASSERT_EQUAL_STRING("", names(HidControl::buildCommands(u, false, false, true, toggleOnly, 1)).c_str());
    // Without the usage the row is not bound
    std::vector<HIDUsageDef> none;
    TEST_ASSERT_EQUAL_STRING("", names(HidControl::buildCommands(none, true, false, true, toggleOnly, 1)).c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_descriptor_has_no_commands);
    RUN_TEST(test_beeper_available_gives_beeper_commands);
    RUN_TEST(test_delay_before_shutdown_alone);
    RUN_TEST(test_shutdown_and_startup_delays);
    RUN_TEST(test_non_feature_usages_are_ignored);
    RUN_TEST(test_battery_test_paths);
    RUN_TEST(test_apc_vendor_fields_match_by_path);
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
    RUN_TEST(test_active_beeper_prefers_feature_over_input);
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
    RUN_TEST(test_plain_battery_test_only_from_its_row);
    RUN_TEST(test_resolve_plain_battery_test_writes_1);
    RUN_TEST(test_apply_default_delays);
    RUN_TEST(test_driver_var_store_json_round_trip);
    RUN_TEST(test_mge_table_gives_same_commands_on_eaton_3s);
    RUN_TEST(test_mge_table_gives_same_commands_on_eaton_5p);
    RUN_TEST(test_mge_table_mute_on_first_row_field);
    RUN_TEST(test_mge_table_commands_outside_catalog_not_listed);
    RUN_TEST(test_mge_table_reboot_from_row);
    RUN_TEST(test_mge_table_mute_respects_declared_range);
    RUN_TEST(test_cps_table_gives_same_commands_on_fixtures);
    RUN_TEST(test_cps_table_write_values);
    RUN_TEST(test_cps_table_beeper_on_off_not_listed);
    RUN_TEST(test_cps_table_reboot_only_from_output);
    RUN_TEST(test_apc_table_gives_same_commands_on_fixtures);
    RUN_TEST(test_apc_table_commands_from_upstream_rows);
    RUN_TEST(test_apc_cs500_shutdown_return_from_row);
    RUN_TEST(test_apc_panel_test_rows);
    RUN_TEST(test_apc_beeper_on_off_not_listed);
    RUN_TEST(test_powercom_spd750u_commands_from_table);
    RUN_TEST(test_powercom_beeper_without_quirk);
    RUN_TEST(test_powercom_load_shutdown_never_listed);
    RUN_TEST(test_powercom_quick_test_value);
    RUN_TEST(test_ecoflow_commands_from_table);
    RUN_TEST(test_openups_commands_from_table);
    RUN_TEST(test_beeper_toggle_from_its_own_row);
    RUN_TEST(test_driver_var_store_corrupt_json);
    RUN_TEST(test_driver_var_store_upsert);
    RUN_TEST(test_apply_default_delays_with_saved_values);
    RUN_TEST(test_saved_delays_only_for_present_variables);
    RUN_TEST(test_driver_var_store_remove);
    RUN_TEST(test_default_delays_after_reset);
    return UNITY_END();
}
