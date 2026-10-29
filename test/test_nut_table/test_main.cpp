#include <Arduino.h>
#include <unity.h>
#include <string>
#include "NutTable.h"
#include <cstdlib>
#include <new>
#include <fstream>
#include <set>
#include <sstream>

// Counts the allocations of this suite (US-073 rework): String is std::string in the mock,
// whose texts longer than 15 characters go to the heap
static size_t g_allocs = 0;
void* operator new(size_t n) {
    g_allocs++;
    void* p = malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }

// NutTable.h read-only and BOOL rows (US-073). Expected values come from the upstream
// sources cited in each test (usbhid-ups.c, mge-hid.c), never from the output of the code.

using namespace NutTable;

void setUp(void) {}
void tearDown(void) {}

static HIDUsageDef mk(const char* path, uint32_t usage, uint8_t report_type, uint8_t report_id = 1) {
    HIDUsageDef d;
    d.usage = usage;
    d.report_type = report_type;
    d.report_id = report_id;
    d.found = true;
    strncpy(d.path, path, sizeof(d.path) - 1);
    return d;
}

// The row of mgeHid2Nut() with this name and path
static const HidInfo& mgeRow(const char* name, const char* path) {
    size_t n;
    const HidInfo* t = mgeHid2Nut(n);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(t[i].info_type, name) == 0 && strcmp(t[i].hidpath, path) == 0) return t[i];
    }
    TEST_FAIL_MESSAGE(path);
    return t[0];
}

static String conv(const HidInfo& info, double v, Context& ctx, DecodeResult expect = DecodeResult::OK,
                   const UPSData* data = nullptr) {
    String out;
    TEST_ASSERT_TRUE(decode(info, v, ctx, out, data) == expect);
    return out;
}

static bool bit(const StatusBits& s, int b) { return (s.set & statusBit(b)) != 0; }

// --- read-only conversions

void test_number_follows_row_format() {
    // dstate_setinfo_dynamic() with the row's dfl
    Context ctx;
    HidInfo r0 = { "x", 0, 0, "UPS.X", "%.0f", 0, Conv::Number, nullptr, 0 };
    HidInfo r1 = { "x", 0, 0, "UPS.X", "%.1f", 0, Conv::Number, nullptr, 0 };
    HidInfo r2 = { "x", 0, 0, "UPS.X", "%.2f", 0, Conv::Number, nullptr, 0 };
    HidInfo bad = { "x", 0, 0, "UPS.X", "%d", 0, Conv::Number, nullptr, 0 };
    TEST_ASSERT_EQUAL_STRING("12", conv(r0, 12.34, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("12.3", conv(r1, 12.34, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("12.34", conv(r2, 12.34, ctx).c_str());
    conv(bad, 12.34, ctx, DecodeResult::FAIL);
}

void test_kelvin_celsius() {
    // kelvin_celsius_conversion_fun (usbhid-ups.c:644): Kelvin only within 273..373
    const HidInfo& row = mgeRow("battery.temperature", "UPS.BatterySystem.Battery.Temperature");
    Context ctx;
    TEST_ASSERT_EQUAL_STRING("20.0", conv(row, 293.15, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("25.0", conv(row, 25, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("374.0", conv(row, 374, ctx).c_str());
}

void test_battery_capacity_as_to_ah() {
    // mge_battery_capacity_fun (mge-hid.c:754): "%.2f" of value / 3600
    const HidInfo& row = mgeRow("battery.capacity", "UPS.BatterySystem.Battery.DesignCapacity");
    Context ctx;
    TEST_ASSERT_EQUAL_STRING("7.00", conv(row, 25200, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("2.50", conv(row, 9000, ctx).c_str());
}

void test_battery_voltage_only_for_some_series() {
    // mge_battery_voltage_fun / mge_battery_voltage_nominal_fun (mge-hid.c:690-745)
    const HidInfo& v = mgeRow("battery.voltage", "UPS.PowerSummary.Voltage");
    const HidInfo& nom = mgeRow("battery.voltage.nominal", "UPS.PowerSummary.ConfigVoltage");
    Context ctx;
    ctx.ready = false;
    conv(v, 27.3, ctx, DecodeResult::DEFER);
    conv(nom, 24, ctx, DecodeResult::DEFER);
    ctx.ready = true;
    ctx.mge_type = MGE_3S;
    conv(v, 27.3, ctx, DecodeResult::FAIL);
    conv(nom, 24, ctx, DecodeResult::FAIL);
    ctx.mge_type = EATON_5P;
    TEST_ASSERT_EQUAL_STRING("27.3", conv(v, 27.3, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("24", conv(nom, 24, ctx).c_str());
    ctx.mge_type = MGE_EVOLUTION_650; // nominal forced to 12 V
    TEST_ASSERT_EQUAL_STRING("12", conv(nom, 24, ctx).c_str());
    ctx.mge_type = MGE_EVOLUTION_850;
    TEST_ASSERT_EQUAL_STRING("24", conv(nom, 24, ctx).c_str());
    ctx.mge_type = EATON_9E;
    TEST_ASSERT_EQUAL_STRING("54.0", conv(v, 54, ctx).c_str());
}

void test_string_id_waits_for_descriptor() {
    // stringid_conversion: the value is an index, the text comes with the descriptor
    const HidInfo& row = mgeRow("battery.type", "UPS.PowerSummary.iDeviceChemistry");
    Context ctx;
    conv(row, 5, ctx, DecodeResult::DEFER);
    conv(row, 0, ctx, DecodeResult::FAIL);

    std::vector<HIDUsageDef> usages = { mk("UPS.PowerSummary.iDeviceChemistry", 0x00850089, 3, 16) };
    size_t n;
    const HidInfo* t = mgeHid2Nut(n);
    std::vector<Bound> b = bind(t, n, usages);
    TEST_ASSERT_EQUAL(1, b.size());
    onValue(b, 0, 5, ctx);
    TEST_ASSERT_EQUAL(5, b[0].string_index);
    TEST_ASSERT_FALSE(b[0].has);
    finishInit(b); // waiting for its string: not a failure
    TEST_ASSERT_TRUE(b[0].first == Bound::First::UNREAD);
    onString(b, 0, String("PbAc"));
    TEST_ASSERT_TRUE(b[0].role == Bound::Role::ACTIVE);
    TEST_ASSERT_EQUAL_STRING("PbAc", b[0].value.c_str());
    onValue(b, 0, 5, ctx); // next reports keep the text
    TEST_ASSERT_EQUAL_STRING("PbAc", b[0].value.c_str());
    TEST_ASSERT_TRUE(b[0].has);
}

void test_plain_lookups() {
    Context ctx;
    // test_read_info (usbhid-ups.c:501)
    const HidInfo& test = mgeRow("ups.test.result", "UPS.BatterySystem.Battery.Test");
    TEST_ASSERT_EQUAL_STRING("Done and passed", conv(test, 1, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("Test scheduled", conv(test, 7, ctx).c_str());
    conv(test, 8, ctx, DecodeResult::FAIL);
    // eaton_charger_type_info (mge-hid.c:448)
    const HidInfo& type = mgeRow("battery.charger.type", "UPS.BatterySystem.Charger.0xFFFF006B");
    TEST_ASSERT_EQUAL_STRING("None", conv(type, 0, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("Constant Charge (CC)", conv(type, 5, ctx).c_str());
}

void test_model_types() {
    // mge_model_names (mge-hid.c:1760-1990)
    TEST_ASSERT_EQUAL_HEX(MGE_3S, mgeModelType("3S", "700"));
    TEST_ASSERT_EQUAL_HEX(MGE_DEFAULT_OFFLINE, mgeModelType("3S", "550"));
    TEST_ASSERT_EQUAL_HEX(EATON_5P, mgeModelType("Eaton 5P", "1550"));
    TEST_ASSERT_EQUAL_HEX(EATON_9E, mgeModelType("Eaton 9E", "1000i"));
    TEST_ASSERT_EQUAL_HEX(MGE_EVOLUTION_650, mgeModelType("Evolution", "650"));
    TEST_ASSERT_EQUAL_HEX(EATON_5P, mgeModelType("Ellipse PRO", "1200 ")); // trailing space upstream
    TEST_ASSERT_EQUAL_HEX(MGE_DEFAULT, mgeModelType("Ellipse PRO", "1200"));
    TEST_ASSERT_EQUAL_HEX(MGE_DEFAULT, mgeModelType("Nobody", "1"));
}

// --- status bits (process_boolean_info, usbhid-ups.c:2030)

void test_status_online_offline_neuter_each_other() {
    StatusBits s;
    applyStatusToken(s, "online");
    TEST_ASSERT_TRUE(bit(s, ST_ONLINE));
    TEST_ASSERT_FALSE(bit(s, ST_OFFLINE));
    applyStatusToken(s, "offline");
    TEST_ASSERT_FALSE(bit(s, ST_ONLINE));
    TEST_ASSERT_TRUE(bit(s, ST_OFFLINE));
    // "!online" only clears ONLINE: OFFLINE stays as it was
    applyStatusToken(s, "online");
    applyStatusToken(s, "!online");
    TEST_ASSERT_FALSE(bit(s, ST_ONLINE));
    TEST_ASSERT_FALSE(bit(s, ST_OFFLINE));
}

void test_status_fullycharged_and_clear() {
    StatusBits s;
    applyStatusToken(s, "fullycharged");
    TEST_ASSERT_TRUE(bit(s, ST_FULLYCHARGED));
    applyStatusToken(s, "notfullycharged");
    TEST_ASSERT_FALSE(bit(s, ST_FULLYCHARGED));
    TEST_ASSERT_TRUE(bit(s, ST_NOTFULLYCHARGED));
    applyStatusToken(s, "OFF"); // strcasecmp
    TEST_ASSERT_TRUE(bit(s, ST_OFF));
    applyStatusToken(s, "!off");
    TEST_ASSERT_FALSE(bit(s, ST_OFF));
    uint32_t before = s.set, known = s.known;
    applyStatusToken(s, "normal"); // eco_mode_info "normal" is no status: ignored
    applyStatusToken(s, nullptr);
    TEST_ASSERT_EQUAL_HEX32(before, s.set);
    TEST_ASSERT_EQUAL_HEX32(known, s.known);
}

static std::string keysOf(const StatusBits& s) {
    std::string out;
    statusKeys(s, [&out](const char* key, const char* value) {
        if (!out.empty()) out += " ";
        out += key;
        out += "=";
        out += value ? value : "-";
    });
    return out;
}

void test_status_keys() {
    StatusBits s;
    TEST_ASSERT_EQUAL_STRING("", keysOf(s).c_str()); // nothing known, no key
    applyStatusToken(s, "online");
    applyStatusToken(s, "!dischrg");
    applyStatusToken(s, "!off");
    TEST_ASSERT_EQUAL_STRING("ups.status.ac_present=1 ups.status.discharging=0 ups.status.off=0", keysOf(s).c_str());
    applyStatusToken(s, "offline");
    applyStatusToken(s, "dischrg");
    TEST_ASSERT_EQUAL_STRING("ups.status.ac_present=0 ups.status.discharging=1 ups.status.off=0", keysOf(s).c_str());
    applyStatusToken(s, "!offline"); // no power state left: the key goes
    applyStatusToken(s, "nobattery");
    applyStatusToken(s, "notfullycharged");
    TEST_ASSERT_EQUAL_STRING("ups.status.ac_present=- ups.status.fully_charged=0 ups.status.discharging=1 "
                             "ups.status.off=0 ups.status.no_battery=1", keysOf(s).c_str());
}

// --- BOOL functions and ABM (mge-hid.c:240-600)

void test_bool_lookups_give_tokens() {
    Context ctx;
    const HidInfo& good = mgeRow("BOOL", "UPS.PowerSummary.PresentStatus.Good");
    TEST_ASSERT_EQUAL_STRING("off", statusToken(good, 0, ctx, nullptr)); // off_info is inverted
    TEST_ASSERT_EQUAL_STRING("!off", statusToken(good, 1, ctx, nullptr));
    const HidInfo& present = mgeRow("BOOL", "UPS.BatterySystem.Battery.PresentStatus.Present");
    TEST_ASSERT_EQUAL_STRING("nobattery", statusToken(present, 0, ctx, nullptr));
    const HidInfo& onbatt = mgeRow("BOOL", "UPS.PowerConverter.Input.[3].PresentStatus.Used");
    TEST_ASSERT_EQUAL_STRING("!online", statusToken(onbatt, 1, ctx, nullptr));
    TEST_ASSERT_EQUAL_STRING("online", statusToken(onbatt, 0, ctx, nullptr));
    TEST_ASSERT_NULL(statusToken(good, 2, ctx, nullptr)); // no lookup entry
}

void test_eaton_charging_hides_chrg_at_full_charge() {
    // eaton_abm_check_chrg_fun: CC-mode devices keep Charging at 100 %
    const HidInfo& chrg = mgeRow("BOOL", "UPS.PowerSummary.PresentStatus.Charging");
    Context ctx;
    UPSData data;
    TEST_ASSERT_EQUAL_STRING("chrg", statusToken(chrg, 1, ctx, nullptr));
    TEST_ASSERT_EQUAL_STRING("chrg", statusToken(chrg, 1, ctx, &data)); // no battery.charge yet
    data.set("battery.charge", "80");
    TEST_ASSERT_EQUAL_STRING("chrg", statusToken(chrg, 1, ctx, &data));
    data.set("battery.charge", "100");
    TEST_ASSERT_EQUAL_STRING("!chrg", statusToken(chrg, 1, ctx, &data));
    TEST_ASSERT_EQUAL_STRING("!chrg", statusToken(chrg, 0, ctx, &data));
}

void test_converter_online_only_when_not_off() {
    // eaton_converter_online_fun: nothing while OFF
    const HidInfo& used = mgeRow("BOOL", "UPS.PowerConverter.Input.[1].PresentStatus.Used");
    Context ctx;
    TEST_ASSERT_EQUAL_STRING("online", statusToken(used, 1, ctx, nullptr));
    TEST_ASSERT_EQUAL_STRING("!online", statusToken(used, 0, ctx, nullptr));
    applyStatusToken(ctx.status, "off");
    TEST_ASSERT_NULL(statusToken(used, 1, ctx, nullptr));
}

void test_abm_mode_path() {
    const HidInfo& enable = mgeRow("battery.charger.abm.status", "UPS.BatterySystem.Charger.0xFFFF0087");
    const HidInfo& pathMode = mgeRow("battery.charger.mode.status", "UPS.BatterySystem.Charger.0xFFFF0094");
    const HidInfo& pathStatus = mgeRow("battery.charger.type.status", "UPS.BatterySystem.Charger.0xFFFF00E9");
    const HidInfo& status = mgeRow("battery.charger.status", "UPS.BatterySystem.Charger.0xFFFF0094");
    const HidInfo& flag = mgeRow("BOOL", "UPS.BatterySystem.Charger.0xFFFF0094");
    const HidInfo& dischrg = mgeRow("BOOL", "UPS.PowerSummary.PresentStatus.Discharging");
    Context ctx;
    // ABM unknown: no charger status, legacy flags in use
    conv(status, 1, ctx, DecodeResult::CLEAR);
    TEST_ASSERT_NULL(statusToken(flag, 1, ctx, nullptr));
    TEST_ASSERT_EQUAL_STRING("dischrg", statusToken(dischrg, 1, ctx, nullptr));

    applyInternal(enable, 1, ctx);
    TEST_ASSERT_EQUAL(ABM_ENABLED, ctx.abm);
    applyInternal(pathMode, 1, ctx);
    applyInternal(pathStatus, 1, ctx); // the first path seen wins
    TEST_ASSERT_EQUAL(ABM_PATH_MODE, ctx.abm_path);
    TEST_ASSERT_EQUAL_STRING("charging", conv(status, 1, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("discharging", conv(status, 2, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("floating", conv(status, 3, ctx).c_str());
    conv(status, 5, ctx, DecodeResult::DEFER);
    TEST_ASSERT_EQUAL_STRING("chrg", statusToken(flag, 1, ctx, nullptr));
    TEST_ASSERT_EQUAL_STRING("chrg", statusToken(flag, 3, ctx, nullptr));
    TEST_ASSERT_EQUAL_STRING("dischrg", statusToken(flag, 2, ctx, nullptr));
    TEST_ASSERT_NULL(statusToken(flag, 4, ctx, nullptr));
    // With ABM the legacy flags only clear
    TEST_ASSERT_EQUAL_STRING("!dischrg", statusToken(dischrg, 1, ctx, nullptr));
    TEST_ASSERT_EQUAL_STRING("!chrg", statusToken(mgeRow("BOOL", "UPS.PowerSummary.PresentStatus.Charging"), 1, ctx, nullptr));

    // ABM switched off: the path resets, the status goes
    applyInternal(enable, 0, ctx);
    applyInternal(pathMode, 1, ctx);
    TEST_ASSERT_EQUAL(ABM_PATH_UNKNOWN, ctx.abm_path);
    conv(status, 1, ctx, DecodeResult::CLEAR);
}

void test_abm_status_path() {
    const HidInfo& enable = mgeRow("battery.charger.abm.status", "UPS.BatterySystem.Charger.0xFFFF0087");
    const HidInfo& pathStatus = mgeRow("battery.charger.type.status", "UPS.BatterySystem.Charger.0xFFFF00E9");
    const HidInfo& status = mgeRow("battery.charger.status", "UPS.BatterySystem.Charger.0xFFFF00E9");
    const HidInfo& flag = mgeRow("BOOL", "UPS.BatterySystem.Charger.0xFFFF00E9");
    Context ctx;
    applyInternal(enable, 1, ctx);
    applyInternal(pathStatus, 2, ctx);
    TEST_ASSERT_EQUAL(ABM_PATH_STATUS, ctx.abm_path);
    TEST_ASSERT_EQUAL_STRING("floating", conv(status, 2, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("discharging", conv(status, 4, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("off", conv(status, 6, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("chrg", statusToken(flag, 2, ctx, nullptr));
    TEST_ASSERT_EQUAL_STRING("dischrg", statusToken(flag, 4, ctx, nullptr));
    TEST_ASSERT_NULL(statusToken(flag, 3, ctx, nullptr));
    // Unknown enable values count as unknown ABM
    applyInternal(enable, 7, ctx);
    TEST_ASSERT_EQUAL(ABM_UNKNOWN, ctx.abm);
}

void test_country_row() {
    const HidInfo& row = mgeRow("device.country", "UPS.PowerSummary.0xFFFF0095");
    Context ctx;
    TEST_ASSERT_FALSE(ctx.country_seen);
    applyInternal(row, 0, ctx);
    TEST_ASSERT_TRUE(ctx.country_seen);
    TEST_ASSERT_EQUAL(COUNTRY_EUROPE, ctx.country);
}

// --- engine rules for internal rows

void test_internal_rows_always_active() {
    std::vector<HIDUsageDef> usages = {
        mk("UPS.PowerSummary.PresentStatus.ACPresent", 0x008500d0, 3, 1),
        mk("UPS.PowerSummary.PresentStatus.Good", 0x00840061, 3, 1),
        mk("UPS.PowerSummary.0xFFFF0095", 0xffff0095, 3, 21),
        mk("UPS.PowerSummary.RemainingCapacity", 0x00850066, 3, 6),
    };
    size_t n;
    const HidInfo* t = mgeHid2Nut(n);
    std::vector<Bound> b = bind(t, n, usages);
    // BOOL ACPresent, Good, device.country, battery.charge, and since US-074 outlet.1.status
    // (mge-hid.c:2268, on_off_info on ACPresent for the low end models)
    TEST_ASSERT_EQUAL(5, b.size());
    Context ctx;
    for (const Bound& x : b) {
        if (isInternalRow(*x.info)) TEST_ASSERT_TRUE(x.role == Bound::Role::ACTIVE);
        else TEST_ASSERT_TRUE(x.role == Bound::Role::PENDING);
    }
    for (size_t i = 0; i < b.size(); i++) {
        if (strcmp(b[i].info->hidpath, "UPS.PowerSummary.PresentStatus.ACPresent") == 0) onValue(b, i, 7, ctx); // no token
        if (strcmp(b[i].info->hidpath, "UPS.PowerSummary.0xFFFF0095") == 0) onValue(b, i, 0, ctx);
    }
    finishInit(b);
    for (const Bound& x : b) {
        if (isInternalRow(*x.info)) {
            TEST_ASSERT_TRUE(x.role == Bound::Role::ACTIVE);
            TEST_ASSERT_FALSE(x.has); // never a published value
        } else {
            TEST_ASSERT_TRUE(x.role == Bound::Role::DEAD); // RemainingCapacity never read
        }
    }
    // Every BOOL row keeps working after INIT
    for (size_t i = 0; i < b.size(); i++) {
        if (strcmp(b[i].info->hidpath, "UPS.PowerSummary.PresentStatus.ACPresent") == 0) onValue(b, i, 1, ctx);
        if (strcmp(b[i].info->hidpath, "UPS.PowerSummary.PresentStatus.Good") == 0) onValue(b, i, 0, ctx);
    }
    TEST_ASSERT_TRUE(bit(ctx.status, ST_ONLINE));
    TEST_ASSERT_TRUE(bit(ctx.status, ST_OFF));
}

void test_clear_removes_published_value() {
    std::vector<HIDUsageDef> usages = {
        mk("UPS.BatterySystem.Charger.0xFFFF0087", 0xffff0087, 3, 41),
        mk("UPS.BatterySystem.Charger.0xFFFF0094", 0xffff0094, 3, 38),
    };
    size_t n;
    const HidInfo* t = mgeHid2Nut(n);
    std::vector<Bound> b = bind(t, n, usages);
    Context ctx;
    size_t enable = 0, status = 0;
    for (size_t i = 0; i < b.size(); i++) {
        if (b[i].info->conv == Conv::AbmEnabled) enable = i;
        if (b[i].info->conv == Conv::AbmStatus) status = i;
    }
    onValue(b, enable, 1, ctx);
    onValue(b, status, 1, ctx);
    TEST_ASSERT_TRUE(b[status].role == Bound::Role::ACTIVE);
    TEST_ASSERT_EQUAL_STRING("charging", b[status].value.c_str());
    onValue(b, enable, 0, ctx);
    onValue(b, status, 1, ctx);
    TEST_ASSERT_FALSE(b[status].has);
    TEST_ASSERT_TRUE(b[status].cleared);
}

// --- no String copied when the value is the same (US-073 rework, ADR 0007)

// The counter must see String allocations, or the two tests below prove nothing
void test_allocation_counter_sees_strings() {
    size_t before = g_allocs;
    {
        String s("a text longer than the small string buffer");
        TEST_ASSERT_TRUE(s.length() > 15);
    }
    TEST_ASSERT_TRUE(g_allocs > before);
}

void test_lookup_same_value_does_not_allocate() {
    // mge-hid.c:2059, eaton_charger_type_info: 5 is "Constant Charge (CC)", 20 characters
    std::vector<HIDUsageDef> usages = { mk("UPS.BatterySystem.Charger.0xFFFF006B", 0xffff006b, 3, 38) };
    size_t n;
    const HidInfo* t = mgeHid2Nut(n);
    std::vector<Bound> b = bind(t, n, usages);
    TEST_ASSERT_EQUAL(1, b.size());
    Context ctx;
    onValue(b, 0, 5, ctx);
    TEST_ASSERT_EQUAL_STRING("Constant Charge (CC)", b[0].value.c_str());
    size_t before = g_allocs;
    onValue(b, 0, 5, ctx);
    TEST_ASSERT_EQUAL(before, g_allocs);
    onValue(b, 0, 4, ctx); // a new value still replaces the old one
    TEST_ASSERT_EQUAL_STRING("ABM", b[0].value.c_str());
}

void test_abm_status_same_value_does_not_allocate() {
    // mge-hid.c:2052, eaton_abm_status_fun on the Status path: 4 is "discharging"
    std::vector<HIDUsageDef> usages = { mk("UPS.BatterySystem.Charger.0xFFFF00E9", 0xffff00e9, 3, 7) };
    size_t n;
    const HidInfo* t = mgeHid2Nut(n);
    std::vector<Bound> b = bind(t, n, usages);
    size_t status = b.size();
    for (size_t i = 0; i < b.size(); i++) {
        if (b[i].info->conv == Conv::AbmStatus) status = i;
    }
    TEST_ASSERT_TRUE(status < b.size());
    Context ctx;
    ctx.abm = ABM_ENABLED;
    ctx.abm_path = ABM_PATH_STATUS;
    onValue(b, status, 4, ctx);
    TEST_ASSERT_EQUAL_STRING("discharging", b[status].value.c_str());
    size_t before = g_allocs;
    onValue(b, status, 4, ctx);
    TEST_ASSERT_EQUAL(before, g_allocs);
}

// --- UPS, Output and Outlet pages (US-074)

void test_us074_lookups() {
    Context ctx;
    // mge_upstype_conversion, on_off_info, eaton_outlet_protection_status_info,
    // outlet_eco_yes_no_info, beeper_info: verbatim
    HidInfo type = { "ups.type", 0, 0, "UPS.X", "%s", 0, Conv::Lookup, NUT_LKP(kUpsType) };
    TEST_ASSERT_EQUAL_STRING("offline / line interactive", conv(type, 1, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("online - hot standby redundancy", conv(type, 5, ctx).c_str());
    conv(type, 0, ctx, DecodeResult::FAIL);
    HidInfo onoff = { "outlet.1.status", 0, 0, "UPS.X", "%s", 0, Conv::Lookup, NUT_LKP(kOnOff) };
    TEST_ASSERT_EQUAL_STRING("off", conv(onoff, 0, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("on", conv(onoff, 1, ctx).c_str());
    HidInfo prot = { "outlet.1.protect.status", 0, 0, "UPS.X", "%s", 0, Conv::Lookup, NUT_LKP(kOutletProtection) };
    TEST_ASSERT_EQUAL_STRING("not protected", conv(prot, 1, ctx).c_str());
    HidInfo eco = { "outlet.1.ecocontrol", 0, 0, "UPS.X", "%s", 0, Conv::Lookup, NUT_LKP(kOutletEco) };
    TEST_ASSERT_EQUAL_STRING("The outlet is ECO controlled", conv(eco, 1, ctx).c_str());
    HidInfo beeper = { "ups.beeper.status", 0, 0, "UPS.X", "%s", 0, Conv::Lookup, NUT_LKP(kBeeperInfo) };
    TEST_ASSERT_EQUAL_STRING("disabled", conv(beeper, 1, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("muted", conv(beeper, 3, ctx).c_str());
}

void test_power_factor() {
    // mge_powerfactor_conversion_fun: "%.2f" of value / 100
    HidInfo pf = { "output.powerfactor", 0, 0, "UPS.X", "%s", 0, Conv::MgePowerFactor, nullptr, 0 };
    Context ctx;
    TEST_ASSERT_EQUAL_STRING("0.95", conv(pf, 95, ctx).c_str());
}

void test_compute_realpower() {
    // eaton_compute_realpower_fun (mge-hid.c:1404-1431): round(load * 0.01 * nominal * pf)
    HidInfo rp = { "ups.realpower", 0, 0, "UPS.X", "-1", 0, Conv::EatonComputeRealpower, nullptr, 0 };
    Context ctx;
    UPSData data;
    conv(rp, 700, ctx, DecodeResult::DEFER, nullptr);
    conv(rp, 700, ctx, DecodeResult::DEFER, &data);        // no load, no nominal: NULL upstream
    data.set("ups.load", "5");
    conv(rp, 700, ctx, DecodeResult::DEFER, &data);        // no nominal
    data.set("ups.power.nominal", "700");
    TEST_ASSERT_EQUAL_STRING("28", conv(rp, 700, ctx, DecodeResult::OK, &data).c_str()); // pf 0.80
    data.set("output.powerfactor", "0.60");
    TEST_ASSERT_EQUAL_STRING("21", conv(rp, 700, ctx, DecodeResult::OK, &data).c_str());
}

// --- cps-hid.c conversions (US-075)

void test_cps_battcharge() {
    // cps_battcharge_fun: "%.0f", clamped to 100
    HidInfo charge = { "battery.charge", 0, 0, "UPS.X", "%s", 0, Conv::CpsBattCharge, nullptr, 0 };
    Context ctx;
    TEST_ASSERT_EQUAL_STRING("87", conv(charge, 87, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("100", conv(charge, 120, ctx).c_str());
}

void test_cps_battstatus() {
    // cps_battstatus_fun: "%.0f%%"
    HidInfo status = { "battery.status", 0, 0, "UPS.X", "%s", 0, Conv::CpsBattStatus, nullptr, 0 };
    Context ctx;
    TEST_ASSERT_EQUAL_STRING("100%", conv(status, 100, ctx).c_str());
}

void test_cps_battvolt_scale() {
    // cps_battvolt_fun + cps_adjust_battery_scale: 2/3 when voltage / nominal > 1.4, decided once
    HidInfo volt = { "battery.voltage", 0, 0, "UPS.X", "%s", 0, Conv::CpsBattVolt, nullptr, 0 };
    UPSData data;
    data.set("battery.voltage.nominal", "12");
    Context ctx;
    ctx.cps.might_need_scale = true; // PID 0x0501
    TEST_ASSERT_EQUAL_STRING("13.9", conv(volt, 20.8, ctx, DecodeResult::OK, &data).c_str());
    TEST_ASSERT_TRUE(ctx.cps.battery_checked);
    TEST_ASSERT_EQUAL_STRING("12.0", conv(volt, 18.0, ctx, DecodeResult::OK, &data).c_str()); // scale kept

    Context other; // no cps_battery_scale hook
    TEST_ASSERT_EQUAL_STRING("20.8", conv(volt, 20.8, other, DecodeResult::OK, &data).c_str());

    Context no_nominal;
    no_nominal.cps.might_need_scale = true;
    UPSData empty;
    TEST_ASSERT_EQUAL_STRING("20.8", conv(volt, 20.8, no_nominal, DecodeResult::OK, &empty).c_str());
    TEST_ASSERT_FALSE(no_nominal.cps.battery_checked); // decided by a later value
    TEST_ASSERT_EQUAL_STRING("20.8", conv(volt, 20.8, no_nominal, DecodeResult::OK, nullptr).c_str());

    Context below;
    below.cps.might_need_scale = true;
    UPSData nominal24;
    nominal24.set("battery.voltage.nominal", "24");
    TEST_ASSERT_EQUAL_STRING("27.2", conv(volt, 27.2, below, DecodeResult::OK, &nominal24).c_str());
    TEST_ASSERT_TRUE(below.cps.battery_checked);
    TEST_ASSERT_EQUAL_STRING("40.0", conv(volt, 40.0, below, DecodeResult::OK, &nominal24).c_str()); // 1.0 kept
}

void test_cps_frequency_scale() {
    // cps_input_freq_fun / cps_output_freq_fun: one factor per direction, decided once
    HidInfo in = { "input.frequency", 0, 0, "UPS.X", "%.1f", 0, Conv::CpsInputFreq, nullptr, 0 };
    HidInfo out = { "output.frequency", 0, 0, "UPS.Y", "%.1f", 0, Conv::CpsOutputFreq, nullptr, 0 };
    UPSData data;
    Context ctx;
    ctx.cps.might_need_scale = true;
    TEST_ASSERT_EQUAL_STRING("50.1", conv(in, 501, ctx, DecodeResult::OK, &data).c_str());
    TEST_ASSERT_EQUAL_STRING("49.9", conv(in, 499, ctx, DecodeResult::OK, &data).c_str());
    TEST_ASSERT_EQUAL_STRING("50.0", conv(out, 50, ctx, DecodeResult::OK, &data).c_str()); // its own factor
    TEST_ASSERT_EQUAL_STRING("50.1", conv(out, 50.1, ctx, DecodeResult::OK, &data).c_str());

    Context other; // no hook: as reported
    TEST_ASSERT_EQUAL_STRING("501.0", conv(in, 501, other, DecodeResult::OK, &data).c_str());

    // input.frequency.nominal published: 60 Hz +-5 %, so 50 fits neither factor
    Context nominal60;
    nominal60.cps.might_need_scale = true;
    data.set("input.frequency.nominal", "60");
    TEST_ASSERT_EQUAL_STRING("50.0", conv(in, 50, nominal60, DecodeResult::OK, &data).c_str());
    TEST_ASSERT_FALSE(nominal60.cps.input_checked);

    // output.frequency.low/high published: 500 / 10 = 50 is inside 47..53
    Context limits;
    limits.cps.might_need_scale = true;
    data.set("output.frequency.low", "47");
    data.set("output.frequency.high", "53");
    TEST_ASSERT_EQUAL_STRING("50.0", conv(out, 500, limits, DecodeResult::OK, &data).c_str());
    TEST_ASSERT_TRUE(limits.cps.output_checked);
    // 46 fits neither 47..53 nor (4.6) it: published as it is, undecided
    Context below;
    below.cps.might_need_scale = true;
    TEST_ASSERT_EQUAL_STRING("46.0", conv(out, 46, below, DecodeResult::OK, &data).c_str());
    TEST_ASSERT_FALSE(below.cps.output_checked);
}

void test_cps_frequency_scale_rule() {
    // cps_adjust_frequency_scale: guessed nominal, then +-5 % of it, or 45..65 Hz
    double scale = -1;
    TEST_ASSERT_TRUE(cpsFrequencyScale(50.0, 0, 0, 0, scale));
    TEST_ASSERT_EQUAL_FLOAT(1.0, scale);
    TEST_ASSERT_TRUE(cpsFrequencyScale(500.0, 0, 0, 0, scale));
    TEST_ASSERT_EQUAL_FLOAT(0.1, scale);
    TEST_ASSERT_TRUE(cpsFrequencyScale(60.0, 0, 0, 0, scale));
    TEST_ASSERT_EQUAL_FLOAT(1.0, scale);
    TEST_ASSERT_TRUE(cpsFrequencyScale(599.0, 0, 0, 0, scale));
    TEST_ASSERT_EQUAL_FLOAT(0.1, scale);
    // Known nominal: 50 Hz +-5%
    TEST_ASSERT_TRUE(cpsFrequencyScale(498.0, 50, 0, 0, scale));
    TEST_ASSERT_EQUAL_FLOAT(0.1, scale);
    // Neither 1 nor 0.1 fits: undecided
    TEST_ASSERT_FALSE(cpsFrequencyScale(1000.0, 0, 0, 0, scale));
    TEST_ASSERT_FALSE(cpsFrequencyScale(56.0, 50, 0, 0, scale));
}

void test_status_lookups_cps() {
    // fullycharged_info and timelimitexpired_info (usbhid-ups.c:452-476)
    HidInfo full = { "BOOL", 0, 0, "UPS.X", nullptr, 0, Conv::Lookup, NUT_LKP(kFullychargedInfo) };
    HidInfo expired = { "BOOL", 0, 0, "UPS.Y", nullptr, 0, Conv::Lookup, NUT_LKP(kTimelimitexpiredInfo) };
    Context ctx;
    applyStatusToken(ctx.status, statusToken(full, 0, ctx, nullptr));
    TEST_ASSERT_TRUE(bit(ctx.status, ST_NOTFULLYCHARGED));
    applyStatusToken(ctx.status, statusToken(full, 1, ctx, nullptr));
    TEST_ASSERT_TRUE(bit(ctx.status, ST_FULLYCHARGED));
    TEST_ASSERT_FALSE(bit(ctx.status, ST_NOTFULLYCHARGED));
    TEST_ASSERT_EQUAL_STRING("timelimitexp", statusToken(expired, 1, ctx, nullptr));
    TEST_ASSERT_EQUAL_STRING("!timelimitexp", statusToken(expired, 0, ctx, nullptr));
}

// --- apc-hid.c conversions (US-076) ---

void test_apc_date() {
    // apc_date_conversion_fun: BCD 0xMMDDYY, years from 70 in the 1900s
    HidInfo date = { "battery.mfr.date", 0, 0, "UPS.X", "%s", 0, Conv::ApcDate, nullptr, 0 };
    Context ctx;
    TEST_ASSERT_EQUAL_STRING("2002/10/22", conv(date, 0x102202, ctx).c_str()); // the upstream example
    TEST_ASSERT_EQUAL_STRING("1970/10/22", conv(date, 0x102270, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("2069/09/25", conv(date, 0x092569, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("not set", conv(date, 0, ctx).c_str());
}

void test_apc_status_lookups() {
    // apcstatusflag_info, apc_linefailcause_vrange_info, apc_linefailcause_frange_info
    HidInfo flag = { "BOOL", 0, 0, "UPS.X", nullptr, 0, Conv::Lookup, NUT_LKP(kApcStatusFlag) };
    HidInfo vrange = { "BOOL", 0, 0, "UPS.Y", nullptr, 0, Conv::Lookup, NUT_LKP(kApcLineFailCauseVrange) };
    HidInfo frange = { "BOOL", 0, 0, "UPS.Y", nullptr, 0, Conv::Lookup, NUT_LKP(kApcLineFailCauseFrange) };
    Context ctx;
    applyStatusToken(ctx.status, statusToken(flag, 0, ctx, nullptr));
    TEST_ASSERT_TRUE(bit(ctx.status, ST_OFF));
    TEST_ASSERT_EQUAL_STRING("!off", statusToken(flag, 8, ctx, nullptr));
    TEST_ASSERT_EQUAL_STRING("!off", statusToken(flag, 16, ctx, nullptr));
    TEST_ASSERT_NULL(statusToken(flag, 4, ctx, nullptr));
    applyStatusToken(ctx.status, statusToken(flag, 8, ctx, nullptr));
    TEST_ASSERT_FALSE(bit(ctx.status, ST_OFF));

    TEST_ASSERT_EQUAL_STRING("vrange", statusToken(vrange, 8, ctx, nullptr));
    TEST_ASSERT_EQUAL_STRING("vrange", statusToken(vrange, 1, ctx, nullptr));
    TEST_ASSERT_EQUAL_STRING("!vrange", statusToken(vrange, 0, ctx, nullptr));
    TEST_ASSERT_NULL(statusToken(vrange, 7, ctx, nullptr));
    TEST_ASSERT_EQUAL_STRING("frange", statusToken(frange, 7, ctx, nullptr));
    TEST_ASSERT_EQUAL_STRING("!frange", statusToken(frange, 0, ctx, nullptr));
    TEST_ASSERT_NULL(statusToken(frange, 8, ctx, nullptr));
}

void test_command_rows_never_published() {
    // HU_TYPE_CMD rows: bound, but outside the INIT rule and never given a value
    static const HidInfo t[] = {
        { "shutdown.stop", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", "-1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        { "ups.timer.shutdown", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", "%.0f", 0, Conv::Number, nullptr, 0 },
    };
    std::vector<HIDUsageDef> usages = { mk("UPS.PowerSummary.DelayBeforeShutdown", 0x00840057, 3, 9) };
    std::vector<Bound> b = bind(t, 2, usages);
    TEST_ASSERT_EQUAL(2, b.size());
    Context ctx;
    onValue(b, 0, 20, ctx);
    onValue(b, 1, 20, ctx);
    finishInit(b);
    TEST_ASSERT_FALSE(b[0].has);
    TEST_ASSERT_TRUE(b[0].role == Bound::Role::PENDING);
    TEST_ASSERT_TRUE(b[0].first == Bound::First::UNREAD);
    TEST_ASSERT_TRUE(b[1].role == Bound::Role::ACTIVE);
    TEST_ASSERT_EQUAL_STRING("20", b[1].value.c_str());
}

// --- vendor ups.alarm rows (US-107)

void test_alarm_rows_of_mge_table() {
    static const struct { const char* path; const char* text; } rows[] = {
        { "UPS.PowerSummary.PresentStatus.0xFFFF0068", "Emergency stop!" },                         // mge-hid.c:2155
        { "UPS.PowerConverter.Input.[1].PresentStatus.0xFFFF0078", "Wiring fault!" },               // mge-hid.c:2156
        { "UPS.PowerSummary.PresentStatus.0xFFFF002D", "Fatal EEPROM fault!" },                     // mge-hid.c:2157
        { "UPS.PowerConverter.0xFFFF0014.PresentStatus.0xFFFF0074", "Inverter AC voltage too high!" }, // mge-hid.c:2158
        { "UPS.PowerConverter.0xFFFF0014.PresentStatus.0xFFFF0075", "Inverter AC voltage too low!" },  // mge-hid.c:2159
        { "UPS.PowerConverter.Output.PresentStatus.0xFFFF004A", "Output short circuit!" },          // mge-hid.c:2160
    };
    size_t n;
    const HidInfo* t = mgeHid2Nut(n);
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        if (!isAlarmRow(t[i])) continue;
        TEST_ASSERT_TRUE_MESSAGE(count < 6, "more ups.alarm rows than mge-hid.c:2155-2160");
        TEST_ASSERT_EQUAL_STRING(rows[count].path, t[i].hidpath);
        TEST_ASSERT_TRUE(isInternalRow(t[i]));
        TEST_ASSERT_EQUAL(0, t[i].info_flags & ST_FLAG_RW); // never in LIST RW
        TEST_ASSERT_EQUAL(1, t[i].lkp_count);
        TEST_ASSERT_EQUAL(1, t[i].lkp[0].hid);
        TEST_ASSERT_EQUAL_STRING(rows[count].text, t[i].lkp[0].nut);
        count++;
    }
    TEST_ASSERT_EQUAL(6, count);
}

static String vendorText(const std::vector<Bound>& b) {
    String out;
    vendorAlarms(b, out);
    return out;
}

void test_vendor_alarms_in_table_order() {
    std::vector<HIDUsageDef> usages = {
        mk("UPS.PowerConverter.Output.PresentStatus.0xFFFF004A", 0xffff004a, 3, 3),
        mk("UPS.PowerSummary.PresentStatus.0xFFFF0068", 0xffff0068, 3, 3),
    };
    size_t n;
    const HidInfo* t = mgeHid2Nut(n);
    std::vector<Bound> b = bind(t, n, usages);
    TEST_ASSERT_EQUAL(2, b.size());
    for (const Bound& x : b) TEST_ASSERT_TRUE(x.role == Bound::Role::ACTIVE); // duplicates allowed
    // Rows never read give nothing
    TEST_ASSERT_EQUAL_STRING("", vendorText(b).c_str());

    // bind() keeps the table order: EmergencyStop (2155) first
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.PresentStatus.0xFFFF0068", b[0].info->hidpath);
    Context ctx;
    onValue(b, 1, 1, ctx);
    onValue(b, 0, 0, ctx); // no text for 0, as a failed hu_find_infoval()
    TEST_ASSERT_EQUAL_STRING("Output short circuit!", vendorText(b).c_str());
    onValue(b, 0, 1, ctx);
    // Table order, not the order of the reads
    TEST_ASSERT_EQUAL_STRING("Emergency stop! Output short circuit!", vendorText(b).c_str());

    finishInit(b);
    for (const Bound& x : b) {
        TEST_ASSERT_TRUE(x.role == Bound::Role::ACTIVE);
        TEST_ASSERT_FALSE(x.has); // never a published value
    }
    // No status bit comes from them
    walkInternalRows(b, ctx);
    TEST_ASSERT_EQUAL_UINT32(0, ctx.status.known);
}

// --- structure of the table

// Every row of a table formats its value with a format formatNumber() supports, or "%s" with a
// conversion that gives text
static void checkSupportedFormats(const HidInfo* t, size_t n) {
    for (size_t i = 0; i < n; i++) {
        const HidInfo& r = t[i];
        // Rows that never format a value: driver-side defaults, internal rows, string indices,
        // instant commands (dfl is the value written) and eaton_compute_realpower_info (dfl
        // "-1" upstream, the function formats the value)
        if ((r.hidflags & HU_FLAG_ABSENT) || isInternalRow(r) || isCommandRow(r) || r.conv == Conv::StringId ||
            r.conv == Conv::EatonComputeRealpower)
            continue;
        TEST_ASSERT_NOT_NULL_MESSAGE(r.dfl, r.info_type);
        bool number = strcmp(r.dfl, "%.0f") == 0 || strcmp(r.dfl, "%.1f") == 0 || strcmp(r.dfl, "%.2f") == 0;
        TEST_ASSERT_TRUE_MESSAGE(number || strcmp(r.dfl, "%s") == 0, r.info_type);
        // A "%s" row needs a conversion that gives text
        if (strcmp(r.dfl, "%s") == 0) TEST_ASSERT_TRUE_MESSAGE(r.conv != Conv::Number, r.info_type);
    }
}

void test_mge_rows_have_supported_formats() {
    size_t n;
    const HidInfo* t = mgeHid2Nut(n);
    checkSupportedFormats(t, n);
}

void test_apc_rows_have_supported_formats() {
    size_t n;
    const HidInfo* t = apcHid2Nut(n);
    checkSupportedFormats(t, n);
}

void test_cps_rows_have_supported_formats() {
    size_t n;
    const HidInfo* t = cpsHid2Nut(n);
    checkSupportedFormats(t, n);
}

// --- Coverage of the upstream table (ADR 0011 rule 1, US-074 rework) ---

static std::vector<std::string> readLines(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::string msg = std::string("cannot open ") + path;
        TEST_FAIL_MESSAGE(msg.c_str());
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) lines.push_back(line);
    return lines;
}

static std::string joinNumbers(const std::vector<int>& v) {
    std::string out;
    for (int n : v) out += (out.empty() ? "" : " ") + std::to_string(n);
    return out;
}

/**
 * Every row of the upstream table (lines starting with { "..." between the table name and
 * its { NULL, 0, 0, NULL terminator, commented out or in #if 0 too) must be cited in the
 * port as <upstream basename>:N or :N-M (a row, or a comment with the reason), and every
 * cited number must be a row of the upstream table. Reusable by the next migrations.
 */
static void checkTableCoverage(const char* upstream_file, const char* upstream_table, const char* source_file,
                               const char* function_start, const char* function_end) {
    // Rows of the upstream table, 1-based line numbers
    std::vector<std::string> up = readLines(upstream_file);
    std::set<int> rows;
    bool in_table = false;
    for (size_t i = 0; i < up.size(); i++) {
        const std::string& l = up[i];
        if (!in_table) {
            in_table = l.find(upstream_table) != std::string::npos;
            continue;
        }
        if (l.find("{ NULL, 0, 0, NULL") != std::string::npos) break;
        size_t p = l.find_first_not_of(" \t");
        if (p != std::string::npos && l.compare(p, 2, "/*") == 0) p = l.find_first_not_of(" \t", p + 2);
        if (p != std::string::npos && l.compare(p, 1, "{") == 0) {
            size_t q = l.find_first_not_of(" \t", p + 1);
            if (q != std::string::npos && l[q] == '"') rows.insert((int)i + 1);
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(rows.size() > 0, "no upstream rows found");

    // Citations inside the port function
    std::ifstream f(source_file);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string src = ss.str();
    size_t a = src.find(function_start);
    size_t b = src.find(function_end, a == std::string::npos ? 0 : a);
    TEST_ASSERT_TRUE_MESSAGE(a != std::string::npos && b != std::string::npos, function_start);
    std::string body = src.substr(a, b - a);
    std::string base = upstream_file;
    base = base.substr(base.find_last_of('/') + 1) + ":";
    std::set<int> cited;
    std::vector<int> unknown;
    for (size_t p = body.find(base); p != std::string::npos; p = body.find(base, p + 1)) {
        const char* c = body.c_str() + p + base.size();
        char* end = nullptr;
        long first = strtol(c, &end, 10);
        if (end == c) continue;
        long last = first;
        if (*end == '-' && end[1] >= '0' && end[1] <= '9') last = strtol(end + 1, nullptr, 10);
        for (long n : { first, last }) {
            if (!rows.count((int)n)) unknown.push_back((int)n);
        }
        for (long n = first; n <= last; n++) cited.insert((int)n);
    }

    std::vector<int> missing;
    for (int r : rows) {
        if (!cited.count(r)) missing.push_back(r);
    }
    std::string msg = std::string(base) + " rows not in the port (add the row or a comment): " + joinNumbers(missing);
    TEST_ASSERT_TRUE_MESSAGE(missing.empty(), msg.c_str());
    msg = std::string(base) + " cited lines that are not rows of the table: " + joinNumbers(unknown);
    TEST_ASSERT_TRUE_MESSAGE(unknown.empty(), msg.c_str());
}

void test_mge_table_covers_every_upstream_row() {
    // ADR 0011 rule 1: every mge_hid2nut row is in mgeHid2Nut(), as a row or as a comment
    checkTableCoverage("nut_repo/drivers/mge-hid.c", "static hid_info_t mge_hid2nut[]", "lib/USBHostUPS/src/NutTable.h",
                       "inline const HidInfo* mgeHid2Nut", "inline const HidInfo* apcHid2Nut");
}

void test_apc_table_covers_every_upstream_row() {
    // ADR 0011 rule 1: every apc_hid2nut row is in apcHid2Nut(), as a row or as a comment (US-076)
    checkTableCoverage("nut_repo/drivers/apc-hid.c", "static hid_info_t apc_hid2nut[]", "lib/USBHostUPS/src/NutTable.h",
                       "inline const HidInfo* apcHid2Nut", "inline const HidInfo* cpsHid2Nut");
}

void test_cps_table_covers_every_upstream_row() {
    // ADR 0011 rule 1: every cps_hid2nut row is in cpsHid2Nut(), as a row or as a comment (US-075)
    checkTableCoverage("nut_repo/drivers/cps-hid.c", "static hid_info_t cps_hid2nut[]", "lib/USBHostUPS/src/NutTable.h",
                       "inline const HidInfo* cpsHid2Nut", "inline const HidInfo* ecoflowHid2Nut");
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_number_follows_row_format);
    RUN_TEST(test_kelvin_celsius);
    RUN_TEST(test_battery_capacity_as_to_ah);
    RUN_TEST(test_battery_voltage_only_for_some_series);
    RUN_TEST(test_string_id_waits_for_descriptor);
    RUN_TEST(test_plain_lookups);
    RUN_TEST(test_model_types);
    RUN_TEST(test_status_online_offline_neuter_each_other);
    RUN_TEST(test_status_fullycharged_and_clear);
    RUN_TEST(test_status_keys);
    RUN_TEST(test_bool_lookups_give_tokens);
    RUN_TEST(test_eaton_charging_hides_chrg_at_full_charge);
    RUN_TEST(test_converter_online_only_when_not_off);
    RUN_TEST(test_abm_mode_path);
    RUN_TEST(test_abm_status_path);
    RUN_TEST(test_country_row);
    RUN_TEST(test_internal_rows_always_active);
    RUN_TEST(test_clear_removes_published_value);
    RUN_TEST(test_allocation_counter_sees_strings);
    RUN_TEST(test_lookup_same_value_does_not_allocate);
    RUN_TEST(test_abm_status_same_value_does_not_allocate);
    RUN_TEST(test_us074_lookups);
    RUN_TEST(test_power_factor);
    RUN_TEST(test_compute_realpower);
    RUN_TEST(test_cps_battcharge);
    RUN_TEST(test_cps_battstatus);
    RUN_TEST(test_cps_battvolt_scale);
    RUN_TEST(test_cps_frequency_scale);
    RUN_TEST(test_cps_frequency_scale_rule);
    RUN_TEST(test_status_lookups_cps);
    RUN_TEST(test_apc_date);
    RUN_TEST(test_apc_status_lookups);
    RUN_TEST(test_command_rows_never_published);
    RUN_TEST(test_mge_rows_have_supported_formats);
    RUN_TEST(test_mge_table_covers_every_upstream_row);
    RUN_TEST(test_apc_rows_have_supported_formats);
    RUN_TEST(test_apc_table_covers_every_upstream_row);
    RUN_TEST(test_cps_rows_have_supported_formats);
    RUN_TEST(test_cps_table_covers_every_upstream_row);
    RUN_TEST(test_alarm_rows_of_mge_table);
    RUN_TEST(test_vendor_alarms_in_table_order);
    return UNITY_END();
}
