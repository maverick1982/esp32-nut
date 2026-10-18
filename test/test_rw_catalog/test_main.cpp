#include <Arduino.h>
#include <unity.h>
#include <string>
#include "NutTable.h"
#include "IUSBHostUPS.h"
#include "GenericDriver.h"
#include "EatonDriver.h"
#include "APCDriver.h"

// NutTable.h (US-066): the HID -> NUT table shaped like hid_info_t. Expected values come
// from the upstream sources cited in each test, never from the output of the code.

using namespace NutTable;

void setUp(void) {}
void tearDown(void) {}

static HIDUsageDef mk(const char* path, uint32_t usage, uint8_t report_type, uint8_t report_id = 1,
                      std::initializer_list<uint8_t> coll_index = {}) {
    HIDUsageDef d;
    d.usage = usage;
    d.report_type = report_type;
    d.report_id = report_id;
    d.found = true;
    strncpy(d.path, path, sizeof(d.path) - 1);
    size_t i = 0;
    for (uint8_t c : coll_index) d.coll_index[i++] = c;
    return d;
}

static const HidInfo* findRow(const HidInfo* t, size_t n, const char* name) {
    for (size_t i = 0; i < n; i++) {
        if (strcmp(t[i].info_type, name) == 0) return &t[i];
    }
    return nullptr;
}

static std::string joined(const std::vector<const char*>& v) {
    std::string out;
    for (const char* s : v) {
        if (!out.empty()) out += " ";
        out += s;
    }
    return out;
}

// --- pathMatches ---

void test_path_with_index_matches_only_that_index() {
    // UPS.Flow.[4].ConfigVoltage: Flow is the collection at depth 1, index 4 -> 5
    TEST_ASSERT_TRUE(pathMatches(mk("UPS.Flow.ConfigVoltage", 0x00840040, 3, 1, { 0, 5 }), "UPS.Flow.[4].ConfigVoltage"));
    TEST_ASSERT_FALSE(pathMatches(mk("UPS.Flow.ConfigVoltage", 0x00840040, 3, 1, { 0, 0 }), "UPS.Flow.[4].ConfigVoltage"));
    TEST_ASSERT_FALSE(pathMatches(mk("UPS.Flow.ConfigVoltage", 0x00840040, 3, 1, { 0, 4 }), "UPS.Flow.[4].ConfigVoltage"));
}

void test_path_without_index_rejects_indexed_usage() {
    TEST_ASSERT_FALSE(pathMatches(mk("UPS.Flow.ConfigVoltage", 0x00840040, 3, 1, { 0, 5 }), "UPS.Flow.ConfigVoltage"));
    TEST_ASSERT_TRUE(pathMatches(mk("UPS.Flow.ConfigVoltage", 0x00840040, 3), "UPS.Flow.ConfigVoltage"));
}

void test_path_hex_segments_and_lengths() {
    // RemainingCapacityLimitSetting is mge 0xffff004d, unnamed in NUTUsages.h
    HIDUsageDef u = mk("UPS.PowerSummary.0xFFFF004D", 0xffff004d, 3);
    TEST_ASSERT_TRUE(pathMatches(u, "UPS.PowerSummary.0xFFFF004D"));
    TEST_ASSERT_FALSE(pathMatches(u, "UPS.PowerSummary"));
    TEST_ASSERT_FALSE(pathMatches(u, "UPS.PowerSummary.0xFFFF004D.Extra"));
    TEST_ASSERT_FALSE(pathMatches(u, "UPS.PowerSummary.0xFFFF004E"));
}

void test_path_nested_indices() {
    // UPS.OutletSystem.Outlet.[2].0xFFFF005D: index on the collection at depth 2
    HIDUsageDef u = mk("UPS.OutletSystem.Outlet.0xFFFF005D", 0xffff005d, 3, 1, { 0, 0, 3 });
    TEST_ASSERT_TRUE(pathMatches(u, "UPS.OutletSystem.Outlet.[2].0xFFFF005D"));
    TEST_ASSERT_FALSE(pathMatches(u, "UPS.OutletSystem.Outlet.[3].0xFFFF005D"));
}

// --- bind / onValue: the INIT rule of hid_ups_walk ---

void test_bind_any_report_type_first_in_descriptor_order() {
    // HIDGetItemData() takes the first item with the path, whatever its report type
    static const HidInfo t[] = {
        { "battery.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.RemainingCapacityLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
    };
    std::vector<HIDUsageDef> only_input = { mk("UPS.PowerSummary.RemainingCapacityLimit", 0x00850029, 1, 7) };
    std::vector<Bound> b = bind(t, 1, only_input);
    TEST_ASSERT_EQUAL(1, b.size());
    TEST_ASSERT_EQUAL(0, b[0].idx);
    TEST_ASSERT_EQUAL(-1, b[0].twin_idx);

    std::vector<HIDUsageDef> both = {
        mk("UPS.PowerSummary.RemainingCapacityLimit", 0x00850029, 1, 7),
        mk("UPS.PowerSummary.RemainingCapacityLimit", 0x00850029, 3, 7),
    };
    b = bind(t, 1, both);
    TEST_ASSERT_EQUAL(0, b[0].idx);
    TEST_ASSERT_EQUAL(1, b[0].twin_idx);
}

void test_bind_skips_rows_without_path() {
    size_t n;
    const HidInfo* t = apcHid2Nut(n);
    std::vector<HIDUsageDef> none;
    TEST_ASSERT_EQUAL(0, bind(t, n, none).size());
}

void test_mge_first_row_wins_battery_charge_low() {
    // mge-hid.c:2013-2014: the RW RemainingCapacityLimitSetting row comes first
    size_t n;
    const HidInfo* t = mgeHid2Nut(n);
    std::vector<HIDUsageDef> usages = {
        mk("UPS.PowerSummary.RemainingCapacityLimit", 0x00850029, 3, 2),
        mk("UPS.PowerSummary.0xFFFF004D", 0xffff004d, 3, 3),
    };
    std::vector<Bound> b = bind(t, n, usages);
    TEST_ASSERT_EQUAL(1, b.size()); // the read-only row is not in the table yet
    TEST_ASSERT_EQUAL_STRING("battery.charge.low", b[0].info->info_type);
    TEST_ASSERT_EQUAL(1, b[0].idx);
}

static const HidInfo kTwoCandidates[] = {
    { "input.sensitivity", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.A", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kApcSensitivity) },
    { "input.sensitivity", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.B", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kCpsSensitivity) },
};

void test_failed_first_value_hands_over_to_next_row() {
    std::vector<HIDUsageDef> usages = { mk("UPS.A", 1, 3), mk("UPS.B", 2, 3) };
    std::vector<Bound> b = bind(kTwoCandidates, 2, usages);
    Context ctx;
    // The later row converts first, but waits for the earlier one (table order)
    onValue(b, 1, 1, ctx);
    TEST_ASSERT_TRUE(b[1].role == Bound::Role::PENDING);
    // The earlier row fails its first conversion: it dies, the later one owns the name
    onValue(b, 0, 9, ctx);
    TEST_ASSERT_TRUE(b[0].role == Bound::Role::DEAD);
    TEST_ASSERT_TRUE(b[1].role == Bound::Role::ACTIVE);
    TEST_ASSERT_EQUAL_STRING("normal", b[1].value.c_str());
    // A later good value does not bring the dead row back
    onValue(b, 0, 1, ctx);
    TEST_ASSERT_TRUE(b[0].role == Bound::Role::DEAD);
    TEST_ASSERT_TRUE(b[1].role == Bound::Role::ACTIVE);
}

void test_first_good_value_wins_and_later_failure_keeps_value() {
    std::vector<HIDUsageDef> usages = { mk("UPS.A", 1, 3), mk("UPS.B", 2, 3) };
    std::vector<Bound> b = bind(kTwoCandidates, 2, usages);
    Context ctx;
    onValue(b, 0, 2, ctx);
    TEST_ASSERT_TRUE(b[0].role == Bound::Role::ACTIVE);
    TEST_ASSERT_TRUE(b[1].role == Bound::Role::DEAD);
    TEST_ASSERT_EQUAL_STRING("high", b[0].value.c_str());
    // ups_infoval_set() failing after INIT does not change the value
    onValue(b, 0, 9, ctx);
    TEST_ASSERT_TRUE(b[0].role == Bound::Role::ACTIVE);
    TEST_ASSERT_EQUAL_STRING("high", b[0].value.c_str());
}

void test_finish_init_fails_unread_rows() {
    std::vector<HIDUsageDef> usages = { mk("UPS.A", 1, 3), mk("UPS.B", 2, 3) };
    std::vector<Bound> b = bind(kTwoCandidates, 2, usages);
    Context ctx;
    onValue(b, 1, 0, ctx);
    TEST_ASSERT_TRUE(b[1].role == Bound::Role::PENDING); // blocked by the unread row A
    finishInit(b);
    TEST_ASSERT_TRUE(b[0].role == Bound::Role::DEAD);
    TEST_ASSERT_TRUE(b[1].role == Bound::Role::ACTIVE);
}

void test_absent_rows_take_default() {
    // mge-hid.c:2247: outlet.desc, HU_FLAG_ABSENT, default "Main Outlet"
    size_t n;
    const HidInfo* t = mgeHid2Nut(n);
    std::vector<HIDUsageDef> usages = { mk("UPS.OutletSystem.Outlet.OutletID", 0x00840021, 3, 11, { 0, 0, 2 }) };
    std::vector<Bound> b = bind(t, n, usages);
    TEST_ASSERT_EQUAL(1, b.size());
    TEST_ASSERT_EQUAL_STRING("outlet.desc", b[0].info->info_type);
    TEST_ASSERT_TRUE(b[0].role == Bound::Role::ACTIVE);
    TEST_ASSERT_EQUAL_STRING("Main Outlet", b[0].value.c_str());
}

void test_read_only_row_is_bound_but_not_rw() {
    // The engine must already work for the read-only rows of the migrations (US-073..)
    static const HidInfo t[] = {
        { "battery.charge.low", 0, 0, "UPS.RO", "%.0f", 0, Conv::Number, nullptr, 0 },
        { "battery.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.RW", "%.0f", 0, Conv::Number, nullptr, 0 },
    };
    std::vector<HIDUsageDef> usages = { mk("UPS.RO", 1, 3), mk("UPS.RW", 2, 3) };
    std::vector<Bound> b = bind(t, 2, usages);
    Context ctx;
    onValue(b, 1, 20, ctx);
    onValue(b, 0, 10, ctx);
    // An earlier read-only row owns the name, as in upstream: the variable is not RW
    TEST_ASSERT_TRUE(b[0].role == Bound::Role::ACTIVE);
    TEST_ASSERT_TRUE(b[1].role == Bound::Role::DEAD);
    TEST_ASSERT_EQUAL_STRING("10", b[0].value.c_str());
    TEST_ASSERT_FALSE(b[0].info->info_flags & ST_FLAG_RW);
}

// --- conversions ---

static String conv(const HidInfo& info, double v, Context& ctx, DecodeResult expect = DecodeResult::OK) {
    String out;
    TEST_ASSERT_TRUE(decode(info, v, ctx, out) == expect);
    return out;
}

void test_number_rounds_like_percent_0f() {
    HidInfo info = { "x", ST_FLAG_RW, 5, "UPS.X", "%.0f", 0, Conv::Number, nullptr, 0 };
    Context ctx;
    TEST_ASSERT_EQUAL_STRING("20", conv(info, 19.6, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("300", conv(info, 300, ctx).c_str());
}

void test_lookup_unknown_value_fails() {
    HidInfo info = { "input.sensitivity", ST_FLAG_RW, 10, "UPS.X", "%s", 0, Conv::Lookup, NUT_LKP(kApcSensitivity) };
    Context ctx;
    TEST_ASSERT_EQUAL_STRING("low", conv(info, 0, ctx).c_str());     // apc_sensitivity_info
    TEST_ASSERT_EQUAL_STRING("medium", conv(info, 1, ctx).c_str());
    conv(info, 3, ctx, DecodeResult::FAIL);
}

void test_battery_date() {
    // date_conversion_fun: 0 -> "not set"; (year-1980)*512 + month*32 + day
    HidInfo info = { "battery.mfr.date", ST_FLAG_RW, 10, "UPS.X", "%s", 0, Conv::BatteryDate, nullptr, 0 };
    Context ctx;
    TEST_ASSERT_EQUAL_STRING("not set", conv(info, 0, ctx).c_str());
    // 0x4D68 = 38*512 + 11*32 + 8 -> 2018/11/08
    TEST_ASSERT_EQUAL_STRING("2018/11/08", conv(info, 0x4D68, ctx).c_str());
}

void test_mge_date_and_time() {
    // 1710510330 s = 2024-03-15 13:45:30 UTC
    HidInfo date = { "ups.date", ST_FLAG_RW, 10, "UPS.X", "%s", 0, Conv::MgeDate, nullptr, 0 };
    HidInfo time = { "ups.time", ST_FLAG_RW, 10, "UPS.X", "%s", 0, Conv::MgeTime, nullptr, 0 };
    Context ctx;
    TEST_ASSERT_EQUAL_STRING("2024/03/15", conv(date, 1710510330.0, ctx).c_str());
    TEST_ASSERT_EQUAL_STRING("13:45:30", conv(time, 1710510330.0, ctx).c_str());
}

// --- ENUM, model-dependent conversions ---

void test_enum_values_plain_lookup() {
    size_t n;
    const HidInfo* mge = mgeHid2Nut(n);
    Context ctx;
    // mge-hid.c:2095, HU_FLAG_ENUM with eaton_enable_disable_info
    TEST_ASSERT_EQUAL_STRING("disabled enabled", joined(enumValues(*findRow(mge, n, "ups.shutdown"), ctx)).c_str());
    // mge-hid.c:2196: SensitivityMode has no HU_FLAG_ENUM, so no ENUM list
    TEST_ASSERT_EQUAL(0, enumValues(*findRow(mge, n, "input.sensitivity"), ctx).size());
    const HidInfo* cps = cpsHid2Nut(n);
    // cps-hid.c:416, HU_FLAG_ENUM with cps_sensitivity_info
    TEST_ASSERT_EQUAL_STRING("low normal high", joined(enumValues(*findRow(cps, n, "input.sensitivity"), ctx)).c_str());
}

void test_nominal_voltage_class() {
    size_t n;
    const HidInfo* mge = mgeHid2Nut(n);
    const HidInfo& row = *findRow(mge, n, "output.voltage.nominal");
    Context ctx; // MGE_DEFAULT: line-interactive or online
    TEST_ASSERT_EQUAL_STRING("230", conv(row, 230, ctx).c_str());
    // nominal_output_voltage_fun: 230 V class; 200/208 also on non-offline models
    TEST_ASSERT_EQUAL_STRING("200 208 220 230 240", joined(enumValues(row, ctx)).c_str());
    conv(row, 120, ctx, DecodeResult::FAIL);

    Context offline;
    offline.mge_type = MGE_DEFAULT_OFFLINE;
    TEST_ASSERT_EQUAL_STRING("230", conv(row, 230, offline).c_str());
    TEST_ASSERT_EQUAL_STRING("220 230 240", joined(enumValues(row, offline)).c_str());

    Context lv;
    TEST_ASSERT_EQUAL_STRING("120", conv(row, 120, lv).c_str());
    TEST_ASSERT_EQUAL_STRING("100 110 120 127", joined(enumValues(row, lv)).c_str());
}

void test_model_dependent_rows_wait_for_context() {
    size_t n;
    const HidInfo* mge = mgeHid2Nut(n);
    Context ctx;
    ctx.ready = false;
    conv(*findRow(mge, n, "output.voltage.nominal"), 230, ctx, DecodeResult::DEFER);
    conv(*findRow(mge, n, "outlet.power"), 25, ctx, DecodeResult::DEFER);
    TEST_ASSERT_EQUAL(-1, ctx.nominal); // the class is not fixed by a deferred value
}

void test_pegasus_check() {
    size_t n;
    const HidInfo* mge = mgeHid2Nut(n);
    const HidInfo& power = *findRow(mge, n, "outlet.power");
    const HidInfo& sw = *findRow(mge, n, "outlet.2.switchable");

    Context pegasus;
    pegasus.mge_type = mgeModelType("Protection Station", "650");
    TEST_ASSERT_EQUAL_HEX(MGE_PEGASUS, pegasus.mge_type);
    TEST_ASSERT_EQUAL_STRING("25", conv(power, 25, pegasus).c_str());
    TEST_ASSERT_EQUAL_STRING("10 25 60", joined(enumValues(power, pegasus)).c_str());
    TEST_ASSERT_EQUAL_STRING("yes", conv(sw, 1, pegasus).c_str());

    // eaton_check_pegasus_fun switches on (mge_type & 0xFF00): MGE_3S (0x110) lands in the
    // MGE_PEGASUS case, so a 3S 700 is accepted even in Europe, exactly as upstream
    Context s3;
    s3.mge_type = mgeModelType("3S", "700");
    s3.country = COUNTRY_EUROPE;
    TEST_ASSERT_EQUAL_HEX(MGE_3S, s3.mge_type);
    TEST_ASSERT_EQUAL_STRING("60", conv(power, 60, s3).c_str());

    Context other; // the Eaton 3S of our fixture reports iProduct "Eaton 3S": MGE_DEFAULT
    other.mge_type = mgeModelType("Eaton 3S", "700");
    TEST_ASSERT_EQUAL_HEX(MGE_DEFAULT, other.mge_type);
    conv(power, 25, other, DecodeResult::FAIL);
    conv(sw, 1, other, DecodeResult::FAIL);
    TEST_ASSERT_EQUAL(0, enumValues(power, other).size());
}

void test_model_type_lookup() {
    TEST_ASSERT_EQUAL_HEX(MGE_DEFAULT_OFFLINE, mgeModelType("ELLIPSE", "650"));
    TEST_ASSERT_EQUAL_HEX(MGE_PEGASUS, mgeModelType("Ellipse ECO", "1600"));
    TEST_ASSERT_EQUAL_HEX(MGE_DEFAULT, mgeModelType("Eaton 5P", "650")); // EATON_5P >= MGE_DEFAULT
    TEST_ASSERT_EQUAL_HEX(MGE_DEFAULT, mgeModelType("ELLIPSE", ""));     // no iModel: no lookup
}

// --- reverse conversions (US-067): encode() ---

// Exact comparison of integral values (Unity is built without double support)
#define ASSERT_EXACT(expected, actual) do { \
        double e_ = (double)(expected), a_ = (double)(actual); \
        char m_[80]; \
        snprintf(m_, sizeof(m_), "expected %.0f, got %.0f", e_, a_); \
        TEST_ASSERT_TRUE_MESSAGE(e_ == a_, m_); \
    } while (0)

// encode() expected to succeed: returns the HID value
static double enc(const HidInfo& info, const char* val, const Context& ctx = Context(), const UPSData& data = UPSData()) {
    double out = -12345;
    TEST_ASSERT_TRUE_MESSAGE(encode(info, val, ctx, data, out) == EncodeResult::OK, val);
    return out;
}

static void encFails(const HidInfo& info, const char* val, const Context& ctx = Context(), const UPSData& data = UPSData()) {
    double out = -12345;
    TEST_ASSERT_TRUE_MESSAGE(encode(info, val, ctx, data, out) == EncodeResult::FAIL, val);
    ASSERT_EXACT(-12345, out); // nothing written on failure
}

void test_encode_number_truncates_like_atol() {
    // setvar(): atol(val) (usbhid-ups.c:1231), integer part truncated toward zero
    HidInfo info = { "battery.charge.low", ST_FLAG_RW, 5, "UPS.X", "%.0f", 0, Conv::Number, nullptr, 0 };
    ASSERT_EXACT(30, enc(info, "30"));
    ASSERT_EXACT(-5, enc(info, "-5"));
    ASSERT_EXACT(7, enc(info, "+7"));
    ASSERT_EXACT(30, enc(info, " 30 "));
    ASSERT_EXACT(30, enc(info, "30.0"));
    ASSERT_EXACT(30, enc(info, "30.7"));  // atol("30.7") = 30
    ASSERT_EXACT(0, enc(info, "-0.5"));   // atol("-0.5") = 0
    ASSERT_EXACT(12, enc(info, "12.34"));
    ASSERT_EXACT(30, enc(info, "30."));
    // int32_t bounds: -2^31 .. 2^31-1
    ASSERT_EXACT(-2147483648.0, enc(info, "-2147483648"));
    ASSERT_EXACT(2147483647.0, enc(info, "2147483647"));
}

void test_encode_number_rejects_what_atol_reads_partially() {
    // Deviation: atol() gives 0 for "abc" (a 0% LB threshold), 30 for "30x", 1 for "1e3"
    HidInfo info = { "battery.charge.low", ST_FLAG_RW, 5, "UPS.X", "%.0f", 0, Conv::Number, nullptr, 0 };
    encFails(info, "abc");
    encFails(info, "30x");
    encFails(info, "1e3");
    encFails(info, "0x10");
    encFails(info, ".");
    encFails(info, "-");
    encFails(info, ".5");
    encFails(info, "");
    encFails(info, "   ");
    encFails(info, "99999999999");
    encFails(info, "2147483648"); // 2^31, one past INT32_MAX
    encFails(info, "3 0");
    encFails(info, nullptr);
}

void test_encode_lookup_is_strcmp() {
    // hu_find_valinfo(): strcmp on apc_sensitivity_info, so case matters
    HidInfo info = { "input.sensitivity", ST_FLAG_RW, 10, "UPS.X", "%s", 0, Conv::Lookup, NUT_LKP(kApcSensitivity) };
    ASSERT_EXACT(0, enc(info, "low"));
    ASSERT_EXACT(1, enc(info, "medium"));
    ASSERT_EXACT(2, enc(info, "high"));
    encFails(info, "Medium");
    encFails(info, "normal"); // a cps value, not in the apc table
    encFails(info, "");
}

void test_encode_nominal_voltage_and_pegasus_threshold() {
    // nominal_output_voltage_info and pegasus_threshold_info have no nuf: plain strcmp
    size_t n;
    const HidInfo* mge = mgeHid2Nut(n);
    const HidInfo& voltage = *findRow(mge, n, "output.voltage.nominal");
    const HidInfo& power = *findRow(mge, n, "outlet.power");
    ASSERT_EXACT(230, enc(voltage, "230"));
    ASSERT_EXACT(127, enc(voltage, "127"));
    encFails(voltage, "231");
    encFails(voltage, "230.0"); // a lookup, not a number
    ASSERT_EXACT(25, enc(power, "25"));
    encFails(power, "30");
}

void test_encode_pegasus_yes_no() {
    // pegasus_yes_no_info_nuf() (mge-hid.c:877): never fails; 0 on non Pegasus models
    size_t n;
    const HidInfo* mge = mgeHid2Nut(n);
    const HidInfo& sw = *findRow(mge, n, "outlet.2.switchable");
    Context pegasus;
    pegasus.mge_type = MGE_PEGASUS;
    ASSERT_EXACT(1, enc(sw, "yes", pegasus));
    ASSERT_EXACT(1, enc(sw, "yesss", pegasus)); // strncmp(value, "yes", 3)
    ASSERT_EXACT(0, enc(sw, "nope", pegasus));  // "assuming no"
    ASSERT_EXACT(0, enc(sw, "no", pegasus));
    Context other; // MGE_DEFAULT: switch default branch returns 0
    ASSERT_EXACT(0, enc(sw, "yes", other));
    Context s3_europe; // MGE_3S & 0xFF00 = MGE_PEGASUS: accepted, as pegasusValid()
    s3_europe.mge_type = MGE_3S;
    s3_europe.country = COUNTRY_EUROPE;
    ASSERT_EXACT(1, enc(sw, "yes", s3_europe));
    encFails(sw, nullptr, pegasus);
}

void test_encode_battery_date() {
    // date_conversion_reverse() (usbhid-ups.c:561): ((y-1980)<<9) + (m<<5) + d
    HidInfo info = { "battery.mfr.date", ST_FLAG_RW, 10, "UPS.X", "%s", 0, Conv::BatteryDate, nullptr, 0 };
    // 2024/03/15: (44<<9) + (3<<5) + 15 = 22528 + 96 + 15 = 22639
    ASSERT_EXACT(22639, enc(info, "2024/03/15"));
    ASSERT_EXACT((44 << 9) + (3 << 5) + 15, enc(info, "2024/03/15"));
    // 1980/01/01: (0<<9) + (1<<5) + 1 = 33
    ASSERT_EXACT(33, enc(info, "1980/01/01"));
    // 2107/12/31: (127<<9) + (12<<5) + 31 = 65024 + 384 + 31 = 65439, last 7-bit year
    ASSERT_EXACT(65439, enc(info, "2107/12/31"));
    // Deviation: upstream writes 0 ("not set") for these
    encFails(info, "2200/01/01");
    encFails(info, "2108/01/01");
    encFails(info, "1979/12/31");
    encFails(info, "2024/13/01");
    encFails(info, "2024/00/10");
    encFails(info, "2024/03/32");
    encFails(info, "2024/03/00");
    encFails(info, "not set");
    encFails(info, "2024/3/15");
    encFails(info, "2024/03/15x");
    encFails(info, "");
}

void test_encode_mge_date_and_time() {
    // mge_date_conversion_nuf() / mge_time_conversion_nuf() (mge-hid.c:626-655): the new
    // half plus the other half from ups.time / ups.date, seconds since the epoch (UTC here)
    HidInfo date = { "ups.date", ST_FLAG_RW, 10, "UPS.X", "%s", 0, Conv::MgeDate, nullptr, 0 };
    HidInfo time = { "ups.time", ST_FLAG_RW, 10, "UPS.X", "%s", 0, Conv::MgeTime, nullptr, 0 };
    Context ctx;
    // Days 1970-01-01 -> 2026-01-01: 56 years * 365 + 14 leap days (1972..2024) = 20454
    // 2026-10-15 is day 273 (Jan..Sep: 31+28+31+30+31+30+31+31+30) + 14 = 287 of the year
    // 20454 + 287 = 20741 days; 20741 * 86400 = 1792022400
    // + 12:34:56 = 12*3600 + 34*60 + 56 = 45296 -> 1792067696
    UPSData with_time;
    with_time.set("ups.time", "12:34:56");
    ASSERT_EXACT(1792067696.0, enc(date, "2026/10/15", ctx, with_time));
    // + 23:59:59 = 86399 -> 1792108799
    UPSData with_date;
    with_date.set("ups.date", "2026/10/15");
    ASSERT_EXACT(1792108799.0, enc(time, "23:59:59", ctx, with_date));
    // 2024 is a leap year: 2024/02/29 exists. Days 1970 -> 2024-01-01: 54*365 + 13 = 19723;
    // + 31 (Jan) + 28 = 19782 days -> 19782 * 86400 = 1709164800, + 12:34:56 = 1709210096
    ASSERT_EXACT(1709210096.0, enc(date, "2024/02/29", ctx, with_time));

    // Deviation: upstream writes 0 (1970) when strptime() fails
    encFails(date, "2026/02/29", ctx, with_time); // not a leap year
    encFails(date, "2026/13/01", ctx, with_time);
    encFails(date, "1969/12/31", ctx, with_time);
    encFails(date, "yesterday", ctx, with_time);
    encFails(date, "2026/10/15", ctx, UPSData()); // other half unknown
    encFails(time, "24:00:00", ctx, with_date);
    encFails(time, "12:60:00", ctx, with_date);
    encFails(time, "12:34", ctx, with_date);
    encFails(time, "12:34:56 extra", ctx, with_date);
    encFails(time, "12:34:56", ctx, with_time); // ups.date missing
}

void test_encode_decode_round_trip() {
    Context ctx;
    String out;
    double v;
    // Every entry of a lookup table comes back as itself
    HidInfo sens = { "input.sensitivity", ST_FLAG_RW, 10, "UPS.X", "%s", 0, Conv::Lookup, NUT_LKP(kMgeSensitivity) };
    for (uint8_t i = 0; i < sens.lkp_count; i++) {
        v = enc(sens, sens.lkp[i].nut);
        TEST_ASSERT_TRUE(decode(sens, v, ctx, out) == DecodeResult::OK);
        TEST_ASSERT_EQUAL_STRING(sens.lkp[i].nut, out.c_str());
    }
    HidInfo bdate = { "battery.mfr.date", ST_FLAG_RW, 10, "UPS.X", "%s", 0, Conv::BatteryDate, nullptr, 0 };
    const char* dates[] = { "2018/11/08", "1980/01/01", "2107/12/31", "2024/03/15" };
    for (const char* d : dates) {
        TEST_ASSERT_TRUE(decode(bdate, enc(bdate, d), ctx, out) == DecodeResult::OK);
        TEST_ASSERT_EQUAL_STRING(d, out.c_str());
    }
    // ups.date / ups.time: both halves survive (test_mge_date_and_time has the inverse)
    HidInfo date = { "ups.date", ST_FLAG_RW, 10, "UPS.X", "%s", 0, Conv::MgeDate, nullptr, 0 };
    HidInfo time = { "ups.time", ST_FLAG_RW, 10, "UPS.X", "%s", 0, Conv::MgeTime, nullptr, 0 };
    UPSData clock;
    clock.set("ups.date", "2024/03/15");
    clock.set("ups.time", "13:45:30");
    v = enc(date, "2024/03/15", ctx, clock);
    ASSERT_EXACT(1710510330.0, v); // same instant as test_mge_date_and_time
    v = enc(date, "2030/07/01", ctx, clock);
    TEST_ASSERT_TRUE(decode(date, v, ctx, out) == DecodeResult::OK);
    TEST_ASSERT_EQUAL_STRING("2030/07/01", out.c_str());
    TEST_ASSERT_TRUE(decode(time, v, ctx, out) == DecodeResult::OK);
    TEST_ASSERT_EQUAL_STRING("13:45:30", out.c_str());
    v = enc(time, "01:02:03", ctx, clock);
    TEST_ASSERT_TRUE(decode(time, v, ctx, out) == DecodeResult::OK);
    TEST_ASSERT_EQUAL_STRING("01:02:03", out.c_str());
    TEST_ASSERT_TRUE(decode(date, v, ctx, out) == DecodeResult::OK);
    TEST_ASSERT_EQUAL_STRING("2024/03/15", out.c_str());
}

void test_timegm_utc_known_instants() {
    ASSERT_EXACT(0, timegmUtc(1970, 1, 1, 0, 0, 0));
    // 2000-03-01: 30 years (1970..1999) * 365 + 7 leap days (1972..1996) = 10957 days to
    // 2000-01-01, + 31 + 29 (2000 is leap) = 11017 days -> 951868800
    ASSERT_EXACT(951868800LL, timegmUtc(2000, 3, 1, 0, 0, 0));
    // 2038-01-19 03:14:08 is 2^31 = 2147483648
    ASSERT_EXACT(2147483648LL, timegmUtc(2038, 1, 19, 3, 14, 8));
}

// --- write plan of the driver (US-067): planSet / setDriverSideValue ---
// setvar() of usbhid-ups.c:1167-1245 without the write. Synthetic usages, raw values
// computed by hand from the field (bit size, exponent, logical range).

class PlanHost : public IUSBHostUPS {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;
    HIDParser _hid_parser;

    void lock() const override {}
    void unlock() const override {}
    UPSDataLock getUPSData() const override { return UPSDataLock(_data, this); }
    String getUPSStatusString() const override { return "OL"; }
    bool setBeeper(bool) override { return true; }
    bool isConnected() const override { return true; }
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
    String getActiveBeeperPath() const override { return ""; }
    uint32_t getQuirks() const override { return 0; }
    bool isPollingPaused() const override { return false; }
    bool requestReport(uint8_t, uint8_t, uint16_t) override { return true; }
    bool requestStringDescriptor(uint8_t) override { return true; }
};

// A field at bit 0 of its report, with its logical range and unit exponent
static HIDUsageDef field(const char* path, uint32_t usage, uint8_t report_id, uint8_t report_type,
                         uint16_t bit_size, int32_t lmin, int32_t lmax, int8_t exponent = 0,
                         std::initializer_list<uint8_t> coll = {}) {
    HIDUsageDef d = mk(path, usage, report_type, report_id, coll);
    d.bit_offset = 0;
    d.bit_size = bit_size;
    d.logical_min = lmin;
    d.logical_max = lmax;
    d.exponent = exponent;
    return d;
}

// One report carrying value in its first one or two bytes, through decode and postDecode
static void feedReport(GenericDriver& drv, PlanHost& host, UPSData& data, uint8_t report_id,
                       uint8_t report_type, uint16_t value, size_t bytes = 1) {
    uint8_t buf[3] = { report_id, (uint8_t)(value & 0xFF), (uint8_t)(value >> 8) };
    drv.decodeReport(&host, report_id, report_type, buf, 1 + bytes, data);
    drv.postDecode(&host, report_id, report_type, buf, 1 + bytes, data);
}

#define ASSERT_STATUS(expected, actual) TEST_ASSERT_EQUAL_INT((int)(expected), (int)(actual))

// A driver with a reduced table: a read-only row and a number scaled by 10^-2
class ReducedTableDriver : public GenericDriver {
public:
    const HidInfo* hid2nut(size_t& n) const override {
        static const HidInfo t[] = {
            { "ups.readonly", 0, 0, "UPS.RO", "%.0f", 0, Conv::Number, nullptr, 0 },
            { "battery.scaled", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Scaled", "%.0f", 0, Conv::Number, nullptr, 0 },
        };
        n = sizeof(t) / sizeof(t[0]);
        return t;
    }
};

// mge-hid.c:2013 battery.charge.low = UPS.PowerSummary.0xFFFF004D, a FEATURE byte 0..100
static void eatonChargeLow(PlanHost& host) {
    host._usages.push_back(field("UPS.PowerSummary.0xFFFF004D", 0xffff004d, 0x22, 3, 8, 0, 100));
}

void test_plan_set_unknown_variable() {
    PlanHost host;
    EatonDriver drv;
    drv.setup();
    UPSData data;
    eatonChargeLow(host);
    SetPlan plan;
    // Nothing bound before the first report: find_nut_info() finds no hiddata
    ASSERT_STATUS(SetStatus::UNKNOWN, drv.planSet(data, host._usages, "battery.charge.low", "30", plan));
    feedReport(drv, host, data, 0x22, 3, 20);
    ASSERT_STATUS(SetStatus::UNKNOWN, drv.planSet(data, host._usages, "no.such.var", "30", plan));
    ASSERT_STATUS(SetStatus::UNKNOWN, drv.planSet(data, host._usages, "", "30", plan));
    ASSERT_STATUS(SetStatus::UNKNOWN, drv.planSet(data, host._usages, nullptr, "30", plan));
    // Not in the table: ups.delay.* is not in data either (applyDefaultDelays never ran)
    ASSERT_STATUS(SetStatus::UNKNOWN, drv.planSet(data, host._usages, "ups.delay.shutdown", "60", plan));
    TEST_ASSERT_NULL(plan.name); // the plan is filled only on OK
}

void test_plan_set_dead_row_is_unknown() {
    // apc-hid.c:414 input.sensitivity: a first value outside apc_sensitivity_info (3) fails
    // the INIT conversion, the row dies (hiddata = NULL) and setvar() cannot find it
    PlanHost host;
    APCDriver drv;
    drv.setup();
    UPSData data;
    host._usages.push_back(field("UPS.Input.0xFF860061", 0xff860061, 0x35, 3, 8, 0, 2));
    feedReport(drv, host, data, 0x35, 3, 3);
    TEST_ASSERT_TRUE(drv.nutBindings()[0].role == Bound::Role::DEAD);
    SetPlan plan;
    ASSERT_STATUS(SetStatus::UNKNOWN, drv.planSet(data, host._usages, "input.sensitivity", "high", plan));
}

void test_plan_set_read_only_row_is_unknown() {
    // setvar(): !(info_flags & ST_FLAG_RW) -> STAT_SET_UNKNOWN
    PlanHost host;
    ReducedTableDriver drv;
    drv.setup();
    UPSData data;
    host._usages.push_back(field("UPS.RO", 0x00840001, 0x01, 3, 8, 0, 100));
    feedReport(drv, host, data, 0x01, 3, 5);
    TEST_ASSERT_EQUAL_STRING("5", data.get("ups.readonly").c_str());
    SetPlan plan;
    ASSERT_STATUS(SetStatus::UNKNOWN, drv.planSet(data, host._usages, "ups.readonly", "6", plan));
}

void test_plan_set_hid_number() {
    PlanHost host;
    EatonDriver drv;
    drv.setup();
    UPSData data;
    eatonChargeLow(host);
    feedReport(drv, host, data, 0x22, 3, 20);
    TEST_ASSERT_EQUAL_STRING("20", data.get("battery.charge.low").c_str());

    SetPlan plan;
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, host._usages, "battery.charge.low", "30", plan));
    TEST_ASSERT_TRUE(plan.kind == SetPlan::Kind::HID);
    TEST_ASSERT_EQUAL_STRING("battery.charge.low", plan.name);
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.0xFFFF004D", plan.def.path);
    TEST_ASSERT_EQUAL_HEX8(0x22, plan.def.report_id);
    TEST_ASSERT_EQUAL(3, plan.def.report_type);
    TEST_ASSERT_EQUAL_INT32(30, plan.raw); // exponent 0: the number itself
    TEST_ASSERT_EQUAL_STRING("30", plan.value.c_str());
    // atol("30.7") = 30
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, host._usages, "battery.charge.low", "30.7", plan));
    TEST_ASSERT_EQUAL_INT32(30, plan.raw);
    // planSet changes nothing: the value is still the one read
    TEST_ASSERT_EQUAL_STRING("20", data.get("battery.charge.low").c_str());
}

void test_plan_set_name_is_case_insensitive_and_canonical() {
    // find_nut_info(): strcasecmp
    PlanHost host;
    EatonDriver drv;
    drv.setup();
    UPSData data;
    eatonChargeLow(host);
    feedReport(drv, host, data, 0x22, 3, 20);
    SetPlan plan;
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, host._usages, "Battery.CHARGE.Low", "25", plan));
    TEST_ASSERT_EQUAL_STRING("battery.charge.low", plan.name);
    TEST_ASSERT_EQUAL_INT32(25, plan.raw);
}

void test_plan_set_conversion_failures() {
    PlanHost host;
    EatonDriver drv;
    drv.setup();
    UPSData data;
    eatonChargeLow(host);
    feedReport(drv, host, data, 0x22, 3, 20);
    SetPlan plan;
    // Not a number (atol() would write 0)
    ASSERT_STATUS(SetStatus::CONVERSION_FAILED, drv.planSet(data, host._usages, "battery.charge.low", "abc", plan));
    // 150 > Logical Maximum 100 (upstream saturates or truncates the bits)
    ASSERT_STATUS(SetStatus::CONVERSION_FAILED, drv.planSet(data, host._usages, "battery.charge.low", "150", plan));
    // Negative in a field whose Logical Minimum is 0
    ASSERT_STATUS(SetStatus::CONVERSION_FAILED, drv.planSet(data, host._usages, "battery.charge.low", "-1", plan));
    ASSERT_STATUS(SetStatus::CONVERSION_FAILED, drv.planSet(data, host._usages, "battery.charge.low", nullptr, plan));
    TEST_ASSERT_NULL(plan.name);
}

void test_plan_set_scaled_field() {
    // Exponent -2: setvar() truncates 12.34 to 12 (atol), then 12 / 10^-2 = 1200
    PlanHost host;
    ReducedTableDriver drv;
    drv.setup();
    UPSData data;
    host._usages.push_back(field("UPS.Scaled", 0x00840002, 0x02, 3, 16, 0, 10000, -2));
    feedReport(drv, host, data, 0x02, 3, 1200, 2); // 1200 * 10^-2 = 12
    TEST_ASSERT_EQUAL_STRING("12", data.get("battery.scaled").c_str());
    SetPlan plan;
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, host._usages, "battery.scaled", "12.34", plan));
    TEST_ASSERT_EQUAL_INT32(1200, plan.raw);
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, host._usages, "battery.scaled", "100", plan));
    TEST_ASSERT_EQUAL_INT32(10000, plan.raw); // the Logical Maximum itself
    // 101 -> 10100 > 10000
    ASSERT_STATUS(SetStatus::CONVERSION_FAILED, drv.planSet(data, host._usages, "battery.scaled", "101", plan));
}

void test_plan_set_input_row_writes_feature_twin() {
    // apc-hid.c:335 UPS.PowerSummary.RemainingCapacityLimit: the INPUT usage comes first in
    // the descriptor, the FEATURE one with the same report ID is its twin
    PlanHost host;
    APCDriver drv;
    drv.setup();
    UPSData data;
    host._usages.push_back(field("UPS.PowerSummary.RemainingCapacityLimit", 0x00850029, 0x08, 1, 8, 0, 100));
    host._usages.push_back(field("UPS.PowerSummary.RemainingCapacityLimit", 0x00850029, 0x08, 3, 8, 0, 100));
    feedReport(drv, host, data, 0x08, 1, 10);
    TEST_ASSERT_EQUAL(0, drv.nutBindings()[0].idx);
    TEST_ASSERT_EQUAL(1, drv.nutBindings()[0].twin_idx);
    SetPlan plan;
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, host._usages, "battery.charge.low", "25", plan));
    TEST_ASSERT_EQUAL(3, plan.def.report_type);
    TEST_ASSERT_EQUAL_HEX8(0x08, plan.def.report_id);
    TEST_ASSERT_EQUAL_INT32(25, plan.raw);
}

void test_plan_set_input_only_row_fails() {
    // No FEATURE usage to write (deviation: upstream tries the INPUT item and fails)
    PlanHost host;
    APCDriver drv;
    drv.setup();
    UPSData data;
    host._usages.push_back(field("UPS.PowerSummary.RemainingCapacityLimit", 0x00850029, 0x08, 1, 8, 0, 100));
    feedReport(drv, host, data, 0x08, 1, 10);
    TEST_ASSERT_EQUAL_STRING("10", data.get("battery.charge.low").c_str());
    SetPlan plan;
    ASSERT_STATUS(SetStatus::FAILED, drv.planSet(data, host._usages, "battery.charge.low", "25", plan));
    // The conversion is checked first: a bad value is still CONVERSION_FAILED
    ASSERT_STATUS(SetStatus::CONVERSION_FAILED, drv.planSet(data, host._usages, "battery.charge.low", "x", plan));
}

void test_plan_set_uses_the_active_row() {
    // apc-hid.c:339-340: two battery.runtime.low rows; the first one read wins, the other
    // is dropped, and the plan writes the winner's usage
    PlanHost host;
    APCDriver drv;
    drv.setup();
    UPSData data;
    host._usages.push_back(field("UPS.Battery.RemainingTimeLimit", 0x0085002a, 0x0A, 3, 16, 0, 0));
    host._usages.push_back(field("UPS.PowerSummary.RemainingTimeLimit", 0x0085002a, 0x0B, 3, 16, 0, 0));
    feedReport(drv, host, data, 0x0A, 3, 120, 2);
    feedReport(drv, host, data, 0x0B, 3, 150, 2);
    SetPlan plan;
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, host._usages, "battery.runtime.low", "300", plan));
    TEST_ASSERT_EQUAL_STRING("UPS.Battery.RemainingTimeLimit", plan.def.path);
    TEST_ASSERT_EQUAL_HEX8(0x0A, plan.def.report_id);
    // No declared range (max == min): only the 16 bits bound the value
    TEST_ASSERT_EQUAL_INT32(300, plan.raw);
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, host._usages, "battery.runtime.low", "65535", plan));
    TEST_ASSERT_EQUAL_INT32(65535, plan.raw);
    ASSERT_STATUS(SetStatus::CONVERSION_FAILED, drv.planSet(data, host._usages, "battery.runtime.low", "65536", plan));
}

void test_plan_set_lookup() {
    // apc-hid.c:414 + apc_sensitivity_info: low 0, medium 1, high 2
    PlanHost host;
    APCDriver drv;
    drv.setup();
    UPSData data;
    host._usages.push_back(field("UPS.Input.0xFF860061", 0xff860061, 0x35, 3, 8, 0, 2));
    feedReport(drv, host, data, 0x35, 3, 1);
    TEST_ASSERT_EQUAL_STRING("medium", data.get("input.sensitivity").c_str());
    SetPlan plan;
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, host._usages, "input.sensitivity", "high", plan));
    TEST_ASSERT_TRUE(plan.kind == SetPlan::Kind::HID);
    TEST_ASSERT_EQUAL_INT32(2, plan.raw);
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, host._usages, "input.sensitivity", "low", plan));
    TEST_ASSERT_EQUAL_INT32(0, plan.raw);
    ASSERT_STATUS(SetStatus::CONVERSION_FAILED, drv.planSet(data, host._usages, "input.sensitivity", "foo", plan));
    ASSERT_STATUS(SetStatus::CONVERSION_FAILED, drv.planSet(data, host._usages, "input.sensitivity", "High", plan));
}

// mge-hid.c:2247 outlet.desc, HU_FLAG_ABSENT with dfl "Main Outlet", bound when
// UPS.OutletSystem.Outlet.[1].OutletID exists (Outlet is the collection at depth 2)
static void eatonOutletId(PlanHost& host) {
    host._usages.push_back(field("UPS.OutletSystem.Outlet.OutletID", 0x00840021, 0x0B, 3, 8, 0, 0, 0, { 0, 0, 2 }));
}

void test_plan_set_absent_row_is_driver_side() {
    // setvar(): HU_FLAG_ABSENT -> dstate_setinfo(info_type, "%s", val), value as it is
    PlanHost host;
    EatonDriver drv;
    drv.setup();
    UPSData data;
    eatonOutletId(host);
    feedReport(drv, host, data, 0x0B, 3, 1);
    TEST_ASSERT_EQUAL_STRING("Main Outlet", data.get("outlet.desc").c_str());
    SetPlan plan;
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, host._usages, "OUTLET.DESC", " Rack  A ", plan));
    TEST_ASSERT_TRUE(plan.kind == SetPlan::Kind::DRIVER_SIDE);
    TEST_ASSERT_EQUAL_STRING("outlet.desc", plan.name);
    TEST_ASSERT_EQUAL_STRING(" Rack  A ", plan.value.c_str());
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, host._usages, "outlet.desc", "", plan));
    TEST_ASSERT_EQUAL_STRING("", plan.value.c_str());
    // Not applied by the plan
    TEST_ASSERT_EQUAL_STRING("Main Outlet", data.get("outlet.desc").c_str());
}

void test_plan_set_ups_delay() {
    // ups.delay.* comes from applyDefaultDelays, outside the table; any driver
    GenericDriver drv;
    drv.setup();
    UPSData data;
    std::vector<HIDUsageDef> none;
    data.set("ups.delay.shutdown", "20");
    SetPlan plan;
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, none, "ups.delay.shutdown", "60", plan));
    TEST_ASSERT_TRUE(plan.kind == SetPlan::Kind::DRIVER_SIDE);
    TEST_ASSERT_EQUAL_STRING("ups.delay.shutdown", plan.name);
    TEST_ASSERT_EQUAL_STRING("60", plan.value.c_str());
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, none, "UPS.Delay.Shutdown", "0", plan));
    TEST_ASSERT_EQUAL_STRING("ups.delay.shutdown", plan.name);
    TEST_ASSERT_EQUAL_STRING("0", plan.value.c_str());
    // Only integers >= 0 (deviation decided in the spec: upstream keeps any string)
    ASSERT_STATUS(SetStatus::CONVERSION_FAILED, drv.planSet(data, none, "ups.delay.shutdown", "-1", plan));
    ASSERT_STATUS(SetStatus::CONVERSION_FAILED, drv.planSet(data, none, "ups.delay.shutdown", "abc", plan));
    ASSERT_STATUS(SetStatus::CONVERSION_FAILED, drv.planSet(data, none, "ups.delay.shutdown", "1.5", plan));
    ASSERT_STATUS(SetStatus::CONVERSION_FAILED, drv.planSet(data, none, "ups.delay.shutdown", "", plan));
    // ups.delay.start was not set for this driver: the variable does not exist
    ASSERT_STATUS(SetStatus::UNKNOWN, drv.planSet(data, none, "ups.delay.start", "30", plan));
    data.set("ups.delay.start", "30");
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, none, "ups.delay.start", "45", plan));
    TEST_ASSERT_EQUAL_STRING("ups.delay.start", plan.name);
    TEST_ASSERT_EQUAL_STRING("45", plan.value.c_str());
}

void test_driver_side_value_survives_bind_until_setup() {
    PlanHost host;
    EatonDriver drv;
    drv.setup();
    UPSData data;
    eatonOutletId(host);
    feedReport(drv, host, data, 0x0B, 3, 1);
    TEST_ASSERT_EQUAL_STRING("Main Outlet", data.get("outlet.desc").c_str());

    drv.setDriverSideValue("outlet.desc", "Rack A");
    feedReport(drv, host, data, 0x0B, 3, 1);
    TEST_ASSERT_EQUAL_STRING("Rack A", data.get("outlet.desc").c_str());

    // A new bind (the usage list changed) starts again from dfl: the value is set again
    host._usages.push_back(field("UPS.PowerSummary.0xFFFF004D", 0xffff004d, 0x22, 3, 8, 0, 100));
    feedReport(drv, host, data, 0x0B, 3, 1);
    TEST_ASSERT_EQUAL_STRING("Rack A", data.get("outlet.desc").c_str());

    // A second value replaces the first one
    drv.setDriverSideValue("outlet.desc", "Rack B");
    feedReport(drv, host, data, 0x0B, 3, 1);
    TEST_ASSERT_EQUAL_STRING("Rack B", data.get("outlet.desc").c_str());

    // setup() forgets it: the next bind publishes the default again
    drv.setup();
    data = UPSData();
    feedReport(drv, host, data, 0x0B, 3, 1);
    TEST_ASSERT_EQUAL_STRING("Main Outlet", data.get("outlet.desc").c_str());
}

void test_driver_side_value_set_before_first_bind() {
    // The host sets the saved values right after setup(), before any report is decoded
    PlanHost host;
    EatonDriver drv;
    drv.setup();
    UPSData data;
    eatonOutletId(host);
    drv.setDriverSideValue("outlet.desc", "Server room");
    drv.setDriverSideValue("outlet.1.desc", "Not on this UPS"); // its row is not bound: ignored
    feedReport(drv, host, data, 0x0B, 3, 1);
    TEST_ASSERT_EQUAL_STRING("Server room", data.get("outlet.desc").c_str());
    TEST_ASSERT_FALSE(data.hasKey("outlet.1.desc"));
}

// --- reset of the driver-side values (US-068): resetDriverSideValue ---

void test_reset_driver_side_value_back_to_dfl() {
    PlanHost host;
    EatonDriver drv;
    drv.setup();
    UPSData data;
    eatonOutletId(host);
    drv.setDriverSideValue("outlet.desc", "Rack A");
    feedReport(drv, host, data, 0x0B, 3, 1);
    TEST_ASSERT_EQUAL_STRING("Rack A", data.get("outlet.desc").c_str());

    // The dfl of mge-hid.c:2247 comes back, in the reset and in the next postDecode
    String value = "stale";
    TEST_ASSERT_TRUE(drv.resetDriverSideValue("outlet.desc", value));
    TEST_ASSERT_EQUAL_STRING("Main Outlet", value.c_str());
    feedReport(drv, host, data, 0x0B, 3, 1);
    TEST_ASSERT_EQUAL_STRING("Main Outlet", data.get("outlet.desc").c_str());

    // The override is gone: a new bind (the usage list changed) does not set Rack A again
    eatonChargeLow(host);
    feedReport(drv, host, data, 0x0B, 3, 1);
    TEST_ASSERT_EQUAL_STRING("Main Outlet", data.get("outlet.desc").c_str());

    // Reset again without an override: still the default
    value = "";
    TEST_ASSERT_TRUE(drv.resetDriverSideValue("outlet.desc", value));
    TEST_ASSERT_EQUAL_STRING("Main Outlet", value.c_str());
}

void test_reset_driver_side_value_only_absent_rows() {
    PlanHost host;
    EatonDriver drv;
    drv.setup();
    UPSData data;
    eatonOutletId(host);
    eatonChargeLow(host);
    feedReport(drv, host, data, 0x0B, 3, 1);
    feedReport(drv, host, data, 0x22, 3, 40);
    TEST_ASSERT_EQUAL_STRING("40", data.get("battery.charge.low").c_str());

    // battery.charge.low is a HID row, not driver-side; an unknown name has no row
    String value = "untouched";
    TEST_ASSERT_FALSE(drv.resetDriverSideValue("battery.charge.low", value));
    TEST_ASSERT_FALSE(drv.resetDriverSideValue("outlet.99.desc", value));
    TEST_ASSERT_FALSE(drv.resetDriverSideValue(nullptr, value));
    TEST_ASSERT_EQUAL_STRING("untouched", value.c_str());
    feedReport(drv, host, data, 0x22, 3, 40);
    TEST_ASSERT_EQUAL_STRING("40", data.get("battery.charge.low").c_str());

    // ups.delay.* lives outside the table: the host sets it again with applyDefaultDelays()
    TEST_ASSERT_FALSE(drv.resetDriverSideValue("ups.delay.shutdown", value));
}

void test_complete_initial_poll_applies_finish_init() {
    // Two rows with one name (kTwoCandidates shape): the second waits for the first, which
    // is never read. completeInitialPoll() ends INIT as the first full poll would.
    class TwoRowsDriver : public GenericDriver {
    public:
        const HidInfo* hid2nut(size_t& n) const override { n = 2; return kTwoCandidates; }
    };
    PlanHost host;
    TwoRowsDriver drv;
    drv.setup();
    UPSData data;
    host._usages.push_back(field("UPS.A", 0x00840003, 0x03, 3, 8, 0, 2));
    host._usages.push_back(field("UPS.B", 0x00840004, 0x04, 3, 8, 0, 2));
    feedReport(drv, host, data, 0x04, 3, 2);
    TEST_ASSERT_TRUE(drv.nutBindings()[1].role == Bound::Role::PENDING);
    TEST_ASSERT_FALSE(drv.initialPollDone());
    drv.completeInitialPoll();
    TEST_ASSERT_TRUE(drv.initialPollDone());
    TEST_ASSERT_TRUE(drv.nutBindings()[0].role == Bound::Role::DEAD);
    TEST_ASSERT_TRUE(drv.nutBindings()[1].role == Bound::Role::ACTIVE);
    // cps_sensitivity_info: 2 = "high"
    TEST_ASSERT_EQUAL_STRING("high", drv.nutBindings()[1].value.c_str());
    SetPlan plan;
    ASSERT_STATUS(SetStatus::OK, drv.planSet(data, host._usages, "input.sensitivity", "normal", plan));
    TEST_ASSERT_EQUAL_STRING("UPS.B", plan.def.path);
    TEST_ASSERT_EQUAL_INT32(1, plan.raw);
}

// --- checkSetValue: steps 5-8 of set_var() in nut_repo/server/netset.c (US-068) ---

// A LIST RW catalog as the host reports it
static std::vector<RWVariable> setCatalog() {
    return {
        { "battery.charge.low", 10, {}, false },
        { "input.sensitivity", 10, { "low", "medium", "high" }, false },
        { "input.broken", 0, {}, false }, // aux 0: the "insanity from the driver" of upsd
        { "outlet.desc", 20, {}, true },
    };
}

void test_check_set_value_ok_and_canonical_name() {
    std::vector<RWVariable> rw = setCatalog();
    const RWVariable* var = nullptr;
    TEST_ASSERT_TRUE(checkSetValue(rw, "battery.charge.low", "30", var) == SetCheck::OK);
    TEST_ASSERT_TRUE(var == &rw[0]);
    // get_var_ptr() looks the name up with strcasecmp: var is the canonical entry
    var = nullptr;
    TEST_ASSERT_TRUE(checkSetValue(rw, "BATTERY.Charge.LOW", "30", var) == SetCheck::OK);
    TEST_ASSERT_NOT_NULL(var);
    TEST_ASSERT_EQUAL_STRING("battery.charge.low", var->name);
    TEST_ASSERT_TRUE(checkSetValue(rw, "Input.Sensitivity", "medium", var) == SetCheck::OK);
    TEST_ASSERT_EQUAL_STRING("input.sensitivity", var->name);
    // No ENUM: any string within aux, the empty one included
    TEST_ASSERT_TRUE(checkSetValue(rw, "outlet.desc", "", var) == SetCheck::OK);
    TEST_ASSERT_TRUE(var == &rw[3]);
}

void test_check_set_value_not_writable() {
    std::vector<RWVariable> rw = setCatalog();
    const RWVariable* var = &rw[0];
    TEST_ASSERT_TRUE(checkSetValue(rw, "battery.charge", "30", var) == SetCheck::NOT_WRITABLE);
    TEST_ASSERT_NULL(var);
    TEST_ASSERT_TRUE(checkSetValue(rw, "battery.charge.low.x", "30", var) == SetCheck::NOT_WRITABLE);
    TEST_ASSERT_TRUE(checkSetValue(rw, "", "30", var) == SetCheck::NOT_WRITABLE);
    std::vector<RWVariable> empty;
    TEST_ASSERT_TRUE(checkSetValue(empty, "battery.charge.low", "30", var) == SetCheck::NOT_WRITABLE);
    TEST_ASSERT_NULL(var);
}

void test_check_set_value_invalid_aux() {
    std::vector<RWVariable> rw = setCatalog();
    const RWVariable* var = nullptr;
    // aux < 1 comes before the length: even the empty value is refused
    TEST_ASSERT_TRUE(checkSetValue(rw, "input.broken", "1", var) == SetCheck::INVALID_AUX);
    TEST_ASSERT_TRUE(var == &rw[2]);
    TEST_ASSERT_TRUE(checkSetValue(rw, "input.broken", "", var) == SetCheck::INVALID_AUX);
}

void test_check_set_value_length_limit() {
    std::vector<RWVariable> rw = setCatalog();
    const RWVariable* var = nullptr;
    // strlen(newval) > aux: exactly aux characters pass, aux + 1 do not
    TEST_ASSERT_TRUE(checkSetValue(rw, "battery.charge.low", "1234567890", var) == SetCheck::OK);
    TEST_ASSERT_TRUE(checkSetValue(rw, "battery.charge.low", "12345678901", var) == SetCheck::TOO_LONG);
    TEST_ASSERT_TRUE(var == &rw[0]);
    TEST_ASSERT_TRUE(checkSetValue(rw, "outlet.desc", "12345678901234567890", var) == SetCheck::OK);
    TEST_ASSERT_TRUE(checkSetValue(rw, "outlet.desc", "123456789012345678901", var) == SetCheck::TOO_LONG);
    TEST_ASSERT_TRUE(var == &rw[3]);
    // The length comes before the ENUM check
    TEST_ASSERT_TRUE(checkSetValue(rw, "input.sensitivity", "veryveryhigh", var) == SetCheck::TOO_LONG);
}

void test_check_set_value_invalid_enum() {
    std::vector<RWVariable> rw = setCatalog();
    const RWVariable* var = nullptr;
    TEST_ASSERT_TRUE(checkSetValue(rw, "input.sensitivity", "normal", var) == SetCheck::INVALID_VALUE);
    TEST_ASSERT_TRUE(var == &rw[1]);
    // ENUM values are compared with strcmp, not strcasecmp
    TEST_ASSERT_TRUE(checkSetValue(rw, "input.sensitivity", "Medium", var) == SetCheck::INVALID_VALUE);
    TEST_ASSERT_TRUE(checkSetValue(rw, "input.sensitivity", "", var) == SetCheck::INVALID_VALUE);
    TEST_ASSERT_TRUE(checkSetValue(rw, "input.sensitivity", "high", var) == SetCheck::OK);
}

void test_writable_variables_driver_side_flag() {
    // Eaton: battery.charge.low is a HID FEATURE row, outlet.desc is HU_FLAG_ABSENT
    // (mge-hid.c:2247) and ups.delay.* is set by CommandCatalog::applyDefaultDelays
    PlanHost host;
    EatonDriver drv;
    drv.setup();
    UPSData data;
    eatonChargeLow(host);
    eatonOutletId(host);
    feedReport(drv, host, data, 0x22, 3, 20);
    feedReport(drv, host, data, 0x0B, 3, 1);
    data.set("ups.delay.shutdown", "20");
    data.set("ups.delay.start", "30");
    TEST_ASSERT_EQUAL_STRING("20", data.get("battery.charge.low").c_str());
    TEST_ASSERT_EQUAL_STRING("Main Outlet", data.get("outlet.desc").c_str());

    std::vector<RWVariable> rw;
    drv.writableVariables(data, rw);
    TEST_ASSERT_EQUAL(4, rw.size());
    const char* names[] = { "battery.charge.low", "outlet.desc", "ups.delay.shutdown", "ups.delay.start" };
    const bool driver_side[] = { false, true, true, true };
    for (size_t i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_STRING(names[i], rw[i].name);
        TEST_ASSERT_EQUAL_MESSAGE(driver_side[i], rw[i].driver_side, names[i]);
    }
}

// --- descriptions and tables ---

void test_describe_case_insensitive() {
    // nut_repo/data/cmdvartab
    TEST_ASSERT_EQUAL_STRING("Remaining battery level when UPS switches to LB (percent)", describe("battery.charge.low"));
    TEST_ASSERT_EQUAL_STRING("Remaining battery level when UPS switches to LB (percent)", describe("BATTERY.CHARGE.LOW"));
    TEST_ASSERT_EQUAL_STRING("Interval to wait after shutdown with delay command (seconds)", describe("ups.delay.shutdown"));
    TEST_ASSERT_NULL(describe("outlet.3.desc")); // not in cmdvartab
    TEST_ASSERT_NULL(describe("no.such.var"));
}

void test_ecoflow_table() {
    // ecoflow-hid.c:181
    size_t n;
    const HidInfo* t = ecoflowHid2Nut(n);
    std::vector<HIDUsageDef> usages = { mk("UPS.PowerSummary.RemainingCapacityLimit", 0x00850029, 3) };
    std::vector<Bound> b = bind(t, n, usages);
    TEST_ASSERT_EQUAL(1, b.size());
    TEST_ASSERT_EQUAL_STRING("battery.charge.low", b[0].info->info_type);
    TEST_ASSERT_EQUAL(5, b[0].info->info_len);
}

void test_tables_hold_only_rw_rows_without_ups_delay() {
    // US-066 scope: the RW rows of each subdriver, minus ups.delay.* (applyDefaultDelays)
    // and the three stateful mge rows left to US-070
    const HidInfo* (*tables[])(size_t&) = { mgeHid2Nut, apcHid2Nut, cpsHid2Nut, ecoflowHid2Nut };
    const size_t expected[] = { 55, 9, 8, 1 };
    for (size_t t = 0; t < 4; t++) {
        size_t n;
        const HidInfo* rows = tables[t](n);
        TEST_ASSERT_EQUAL(expected[t], n);
        for (size_t i = 0; i < n; i++) {
            TEST_ASSERT_TRUE(rows[i].info_flags & ST_FLAG_RW);
            TEST_ASSERT_TRUE(strncmp(rows[i].info_type, "ups.delay.", 10) != 0);
            TEST_ASSERT_TRUE(strcmp(rows[i].info_type, "input.bypass.switch.on") != 0);
            TEST_ASSERT_TRUE(strcmp(rows[i].info_type, "input.bypass.switch.off") != 0);
            TEST_ASSERT_TRUE(strcmp(rows[i].info_type, "input.eco.switchable") != 0);
        }
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_path_with_index_matches_only_that_index);
    RUN_TEST(test_path_without_index_rejects_indexed_usage);
    RUN_TEST(test_path_hex_segments_and_lengths);
    RUN_TEST(test_path_nested_indices);
    RUN_TEST(test_bind_any_report_type_first_in_descriptor_order);
    RUN_TEST(test_bind_skips_rows_without_path);
    RUN_TEST(test_mge_first_row_wins_battery_charge_low);
    RUN_TEST(test_failed_first_value_hands_over_to_next_row);
    RUN_TEST(test_first_good_value_wins_and_later_failure_keeps_value);
    RUN_TEST(test_finish_init_fails_unread_rows);
    RUN_TEST(test_absent_rows_take_default);
    RUN_TEST(test_read_only_row_is_bound_but_not_rw);
    RUN_TEST(test_number_rounds_like_percent_0f);
    RUN_TEST(test_lookup_unknown_value_fails);
    RUN_TEST(test_battery_date);
    RUN_TEST(test_mge_date_and_time);
    RUN_TEST(test_enum_values_plain_lookup);
    RUN_TEST(test_nominal_voltage_class);
    RUN_TEST(test_model_dependent_rows_wait_for_context);
    RUN_TEST(test_pegasus_check);
    RUN_TEST(test_model_type_lookup);
    RUN_TEST(test_encode_number_truncates_like_atol);
    RUN_TEST(test_encode_number_rejects_what_atol_reads_partially);
    RUN_TEST(test_encode_lookup_is_strcmp);
    RUN_TEST(test_encode_nominal_voltage_and_pegasus_threshold);
    RUN_TEST(test_encode_pegasus_yes_no);
    RUN_TEST(test_encode_battery_date);
    RUN_TEST(test_encode_mge_date_and_time);
    RUN_TEST(test_encode_decode_round_trip);
    RUN_TEST(test_timegm_utc_known_instants);
    RUN_TEST(test_plan_set_unknown_variable);
    RUN_TEST(test_plan_set_dead_row_is_unknown);
    RUN_TEST(test_plan_set_read_only_row_is_unknown);
    RUN_TEST(test_plan_set_hid_number);
    RUN_TEST(test_plan_set_name_is_case_insensitive_and_canonical);
    RUN_TEST(test_plan_set_conversion_failures);
    RUN_TEST(test_plan_set_scaled_field);
    RUN_TEST(test_plan_set_input_row_writes_feature_twin);
    RUN_TEST(test_plan_set_input_only_row_fails);
    RUN_TEST(test_plan_set_uses_the_active_row);
    RUN_TEST(test_plan_set_lookup);
    RUN_TEST(test_plan_set_absent_row_is_driver_side);
    RUN_TEST(test_plan_set_ups_delay);
    RUN_TEST(test_driver_side_value_survives_bind_until_setup);
    RUN_TEST(test_driver_side_value_set_before_first_bind);
    RUN_TEST(test_reset_driver_side_value_back_to_dfl);
    RUN_TEST(test_reset_driver_side_value_only_absent_rows);
    RUN_TEST(test_complete_initial_poll_applies_finish_init);
    RUN_TEST(test_check_set_value_ok_and_canonical_name);
    RUN_TEST(test_check_set_value_not_writable);
    RUN_TEST(test_check_set_value_invalid_aux);
    RUN_TEST(test_check_set_value_length_limit);
    RUN_TEST(test_check_set_value_invalid_enum);
    RUN_TEST(test_writable_variables_driver_side_flag);
    RUN_TEST(test_describe_case_insensitive);
    RUN_TEST(test_ecoflow_table);
    RUN_TEST(test_tables_hold_only_rw_rows_without_ups_delay);
    return UNITY_END();
}
