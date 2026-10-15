#include <Arduino.h>
#include <unity.h>
#include <string>
#include "NutTable.h"

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
    RUN_TEST(test_describe_case_insensitive);
    RUN_TEST(test_ecoflow_table);
    RUN_TEST(test_tables_hold_only_rw_rows_without_ups_delay);
    return UNITY_END();
}
