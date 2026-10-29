#include <Arduino.h>
#include <unity.h>
#include <string>
#include "NutTable.h"
#include <cstdlib>
#include <new>

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
    TEST_ASSERT_EQUAL(4, b.size());
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

// --- structure of the table

void test_mge_rows_have_supported_formats() {
    size_t n;
    const HidInfo* t = mgeHid2Nut(n);
    for (size_t i = 0; i < n; i++) {
        const HidInfo& r = t[i];
        // Rows that never format a value: driver-side defaults, internal rows, string indices
        if ((r.hidflags & HU_FLAG_ABSENT) || isInternalRow(r) || r.conv == Conv::StringId) continue;
        TEST_ASSERT_NOT_NULL_MESSAGE(r.dfl, r.info_type);
        bool number = strcmp(r.dfl, "%.0f") == 0 || strcmp(r.dfl, "%.1f") == 0 || strcmp(r.dfl, "%.2f") == 0;
        TEST_ASSERT_TRUE_MESSAGE(number || strcmp(r.dfl, "%s") == 0, r.info_type);
        // A "%s" row needs a conversion that gives text
        if (strcmp(r.dfl, "%s") == 0) TEST_ASSERT_TRUE_MESSAGE(r.conv != Conv::Number, r.info_type);
    }
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
    RUN_TEST(test_mge_rows_have_supported_formats);
    return UNITY_END();
}
