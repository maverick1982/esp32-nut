#include <unity.h>
#include <math.h>
#include "UPSData.h"

// US-146 (#128): the ups.realpower estimate of UPSData::updateRealPower() with the nominal
// powers of the custom variables. Sources in order: custom W, W of the UPS, custom VA x 0.6,
// VA of the UPS x 0.6; decimal load rounded to the watt; the estimate goes when no source is
// left; a device that measures or computes ups.realpower is never touched.

void setUp(void) {}
void tearDown(void) {}

static String realpower(const UPSData& d) { return d.get("ups.realpower", "<none>"); }

void test_custom_watts_with_decimal_load(void) {
    UPSData d;
    d.set("ups.load", "10.4");
    d.setCustomPowerNominals(980, 0);
    d.updateRealPower();
    TEST_ASSERT_EQUAL_STRING("102", realpower(d).c_str());
    TEST_ASSERT_TRUE(d.realPowerEstimated());
}

void test_ups_watts_keep_the_decimal_load(void) {
    // Before US-146 the load was truncated to 10 %: 98 W
    UPSData d;
    d.set("ups.realpower.nominal", "980");
    d.set("ups.load", "10.4");
    d.updateRealPower();
    TEST_ASSERT_EQUAL_STRING("102", realpower(d).c_str());
}

void test_custom_va_times_0_6(void) {
    UPSData d;
    d.set("ups.load", "10");
    d.setCustomPowerNominals(0, 1500);
    d.updateRealPower();
    TEST_ASSERT_EQUAL_STRING("90", realpower(d).c_str());
}

void test_custom_watts_win_over_ups_watts(void) {
    UPSData d;
    d.set("ups.realpower.nominal", "500");
    d.set("ups.load", "50");
    d.setCustomPowerNominals(1000, 0);
    d.updateRealPower();
    TEST_ASSERT_EQUAL_STRING("500", realpower(d).c_str());
}

void test_ups_watts_win_over_custom_va(void) {
    UPSData d;
    d.set("ups.realpower.nominal", "500");
    d.set("ups.load", "50");
    d.setCustomPowerNominals(0, 1500);
    d.updateRealPower();
    TEST_ASSERT_EQUAL_STRING("250", realpower(d).c_str());
}

void test_custom_va_wins_over_ups_va(void) {
    UPSData d;
    d.set("ups.power.nominal", "2000");
    d.set("ups.load", "50");
    d.setCustomPowerNominals(0, 1000);
    d.updateRealPower();
    TEST_ASSERT_EQUAL_STRING("300", realpower(d).c_str());
}

void test_zero_negative_and_nan_are_ignored(void) {
    UPSData d;
    d.set("ups.load", "50");
    d.setCustomPowerNominals(0, -5);
    d.updateRealPower();
    TEST_ASSERT_FALSE(d.hasKey("ups.realpower"));
    TEST_ASSERT_FALSE(d.realPowerEstimated());
    d.setCustomPowerNominals(NAN, INFINITY);
    d.updateRealPower();
    TEST_ASSERT_FALSE(d.hasKey("ups.realpower"));
}

void test_invalid_custom_falls_back_to_the_ups_value(void) {
    UPSData d;
    d.set("ups.realpower.nominal", "500");
    d.set("ups.load", "50");
    d.setCustomPowerNominals(-1, 0);
    d.updateRealPower();
    TEST_ASSERT_EQUAL_STRING("250", realpower(d).c_str());
}

void test_removing_the_custom_value_removes_the_estimate(void) {
    UPSData d;
    d.set("ups.load", "50");
    d.setCustomPowerNominals(1000, 0);
    d.updateRealPower();
    TEST_ASSERT_EQUAL_STRING("500", realpower(d).c_str());
    d.setCustomPowerNominals(0, 0);
    d.updateRealPower();
    TEST_ASSERT_FALSE(d.hasKey("ups.realpower"));
    TEST_ASSERT_FALSE(d.realPowerEstimated());
}

void test_removing_the_custom_value_falls_back_to_the_ups_nominal(void) {
    UPSData d;
    d.set("ups.realpower.nominal", "500");
    d.set("ups.load", "50");
    d.setCustomPowerNominals(1000, 0);
    d.updateRealPower();
    TEST_ASSERT_EQUAL_STRING("500", realpower(d).c_str());
    d.setCustomPowerNominals(0, 0);
    d.updateRealPower();
    TEST_ASSERT_EQUAL_STRING("250", realpower(d).c_str());
}

void test_estimate_goes_with_the_load(void) {
    UPSData d;
    d.set("ups.load", "50");
    d.setCustomPowerNominals(1000, 0);
    d.updateRealPower();
    TEST_ASSERT_TRUE(d.hasKey("ups.realpower"));
    d.remove("ups.load");
    d.updateRealPower();
    TEST_ASSERT_FALSE(d.hasKey("ups.realpower"));
}

void test_measured_device_is_never_estimated(void) {
    // markRealPowerMeasured() after an estimate: the estimate goes, none comes back
    UPSData d;
    d.set("ups.load", "50");
    d.setCustomPowerNominals(1000, 0);
    d.updateRealPower();
    d.markRealPowerMeasured();
    TEST_ASSERT_FALSE(d.hasKey("ups.realpower"));
    d.updateRealPower();
    TEST_ASSERT_FALSE(d.hasKey("ups.realpower"));

    // A value of the device stays as it is
    UPSData m;
    m.markRealPowerMeasured();
    m.set("ups.realpower", "321");
    m.set("ups.load", "50");
    m.setCustomPowerNominals(1000, 0);
    m.updateRealPower();
    TEST_ASSERT_EQUAL_STRING("321", realpower(m).c_str());
}

void test_value_of_the_device_is_never_removed(void) {
    // ups.realpower written by someone else (not the estimate) and no source: kept
    UPSData d;
    d.set("ups.realpower", "77");
    d.updateRealPower();
    TEST_ASSERT_EQUAL_STRING("77", realpower(d).c_str());
}

void test_no_write_when_the_value_does_not_change(void) {
    UPSData d;
    d.set("ups.load", "50");
    d.setCustomPowerNominals(1000, 0);
    uint32_t before = d.writes();
    d.updateRealPower();
    d.updateRealPower();
    TEST_ASSERT_EQUAL_UINT32(before + 1, d.writes());
}

void test_setter_reports_a_change_only_when_something_changes(void) {
    UPSData d;
    TEST_ASSERT_FALSE(d.setCustomPowerNominals(0, 0));
    TEST_ASSERT_TRUE(d.setCustomPowerNominals(980, 0));
    TEST_ASSERT_FALSE(d.setCustomPowerNominals(980, 0));
    TEST_ASSERT_TRUE(d.setCustomPowerNominals(980, 1500));
    TEST_ASSERT_FALSE(d.setCustomPowerNominals(980, 1500));
    TEST_ASSERT_TRUE(d.setCustomPowerNominals(-3, 1500));
    TEST_ASSERT_FALSE(d.setCustomPowerNominals(0, 1500)); // -3 and 0 are both "none"
}

// --- US-146 rework: the source of the last estimate, for GET /api/ups-vars ---

void test_source_of_each_branch(void) {
    UPSData a;
    a.set("ups.load", "50");
    a.setCustomPowerNominals(980, 1500);
    a.updateRealPower();
    TEST_ASSERT_TRUE(a.realPowerSource() == RealPowerSource::CUSTOM_W);
    TEST_ASSERT_EQUAL_FLOAT(980.0f, a.realPowerSourceNominal());

    UPSData b;
    b.set("ups.load", "50");
    b.set("ups.realpower.nominal", "900");
    b.setCustomPowerNominals(0, 1500);
    b.updateRealPower();
    TEST_ASSERT_TRUE(b.realPowerSource() == RealPowerSource::UPS_W);
    TEST_ASSERT_EQUAL_FLOAT(900.0f, b.realPowerSourceNominal());

    UPSData c;
    c.set("ups.load", "50");
    c.set("ups.power.nominal", "2000");
    c.setCustomPowerNominals(0, 1500);
    c.updateRealPower();
    TEST_ASSERT_TRUE(c.realPowerSource() == RealPowerSource::CUSTOM_VA);
    TEST_ASSERT_EQUAL_FLOAT(1500.0f, c.realPowerSourceNominal());

    UPSData d;
    d.set("ups.load", "50");
    d.set("ups.power.nominal", "2000");
    d.updateRealPower();
    TEST_ASSERT_TRUE(d.realPowerSource() == RealPowerSource::UPS_VA);
    TEST_ASSERT_EQUAL_FLOAT(2000.0f, d.realPowerSourceNominal());
}

void test_source_none_without_estimate(void) {
    UPSData d;
    TEST_ASSERT_TRUE(d.realPowerSource() == RealPowerSource::NONE);
    d.set("ups.load", "50");
    d.setCustomPowerNominals(980, 0);
    d.updateRealPower();
    d.setCustomPowerNominals(0, 0);
    d.updateRealPower(); // removed: no source left
    TEST_ASSERT_TRUE(d.realPowerSource() == RealPowerSource::NONE);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, d.realPowerSourceNominal());

    UPSData m;
    m.set("ups.load", "50");
    m.setCustomPowerNominals(980, 0);
    m.updateRealPower();
    m.markRealPowerMeasured();
    TEST_ASSERT_TRUE(m.realPowerSource() == RealPowerSource::NONE);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, m.realPowerSourceNominal());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_custom_watts_with_decimal_load);
    RUN_TEST(test_ups_watts_keep_the_decimal_load);
    RUN_TEST(test_custom_va_times_0_6);
    RUN_TEST(test_custom_watts_win_over_ups_watts);
    RUN_TEST(test_ups_watts_win_over_custom_va);
    RUN_TEST(test_custom_va_wins_over_ups_va);
    RUN_TEST(test_zero_negative_and_nan_are_ignored);
    RUN_TEST(test_invalid_custom_falls_back_to_the_ups_value);
    RUN_TEST(test_removing_the_custom_value_removes_the_estimate);
    RUN_TEST(test_removing_the_custom_value_falls_back_to_the_ups_nominal);
    RUN_TEST(test_estimate_goes_with_the_load);
    RUN_TEST(test_measured_device_is_never_estimated);
    RUN_TEST(test_value_of_the_device_is_never_removed);
    RUN_TEST(test_no_write_when_the_value_does_not_change);
    RUN_TEST(test_setter_reports_a_change_only_when_something_changes);
    RUN_TEST(test_source_of_each_branch);
    RUN_TEST(test_source_none_without_estimate);
    return UNITY_END();
}
