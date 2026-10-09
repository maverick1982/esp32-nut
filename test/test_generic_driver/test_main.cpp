#include <unity.h>
#include "GenericDriver.h"
#include "IUSBHostUPS.h"

class MockGenericHost : public IUSBHostUPS {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;

    void lock() const override {}
    void unlock() const override {}
    UPSDataLock getUPSData() const override { return UPSDataLock(_data, this); }
    String getUPSStatusString() const override { return "OL"; }
    bool setBeeper(bool) override { return true; }
    bool isConnected() const override { return true; }

    HIDParser _hid_parser;
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
    String activeBeeper;
    String getActiveBeeperPath() const override { return activeBeeper; }
    uint32_t getQuirks() const override { return 0; }
    bool isPollingPaused() const override { return false; }
    bool requestReport(uint8_t, uint8_t, uint16_t) override { return true; }
    bool requestStringDescriptor(uint8_t) override { return true; }
};

static GenericDriver driver;
static MockGenericHost mockHost;
static UPSData ups_data;

static void addUsage(const char* path, uint8_t report_id, uint8_t report_type,
                     uint16_t bit_offset, uint16_t bit_size) {
    HIDUsageDef u;
    u.report_id = report_id;
    u.report_type = report_type;
    u.bit_offset = bit_offset;
    u.bit_size = bit_size;
    u.found = true;
    strncpy(u.path, path, sizeof(u.path) - 1);
    u.path[sizeof(u.path) - 1] = '\0';
    mockHost._usages.push_back(u);
}

void setUp(void) {
    ups_data = UPSData();
    mockHost._usages.clear();
    mockHost.activeBeeper = "";
    driver.setup();
}

void tearDown(void) {}

// Upstream nobattery_info is inverted: BatteryPresent 0 means "no battery".
void test_battery_present_is_inverted(void) {
    addUsage("UPS.PowerSummary.PresentStatus.BatteryPresent", 0x07, 1, 0, 1);

    uint8_t absent[] = { 0x07, 0x00 };
    driver.decodeReport(&mockHost, 0x07, 1, absent, sizeof(absent), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.status.no_battery"));
    TEST_ASSERT_TRUE(ups_data.getBool("ups.status.no_battery"));

    uint8_t present[] = { 0x07, 0x01 };
    driver.decodeReport(&mockHost, 0x07, 1, present, sizeof(present), ups_data);
    TEST_ASSERT_FALSE(ups_data.getBool("ups.status.no_battery"));
}

void test_remaining_time_limit_expired(void) {
    addUsage("UPS.PowerSummary.PresentStatus.RemainingTimeLimitExpired", 0x07, 1, 0, 1);

    uint8_t set[] = { 0x07, 0x01 };
    driver.decodeReport(&mockHost, 0x07, 1, set, sizeof(set), ups_data);
    TEST_ASSERT_TRUE(ups_data.getBool("ups.status.remaining_time_limit_expired"));

    uint8_t clear[] = { 0x07, 0x00 };
    driver.decodeReport(&mockHost, 0x07, 1, clear, sizeof(clear), ups_data);
    TEST_ASSERT_FALSE(ups_data.getBool("ups.status.remaining_time_limit_expired"));
}

void test_fully_charged_and_discharged(void) {
    addUsage("UPS.PowerSummary.PresentStatus.FullyCharged", 0x07, 1, 0, 1);
    addUsage("UPS.PowerSummary.PresentStatus.FullyDischarged", 0x07, 1, 1, 1);

    uint8_t charged[] = { 0x07, 0x01 }; // FullyCharged=1, FullyDischarged=0
    driver.decodeReport(&mockHost, 0x07, 1, charged, sizeof(charged), ups_data);
    TEST_ASSERT_TRUE(ups_data.getBool("ups.status.fully_charged"));
    TEST_ASSERT_FALSE(ups_data.getBool("ups.status.depleted"));

    uint8_t discharged[] = { 0x07, 0x02 }; // FullyCharged=0, FullyDischarged=1
    driver.decodeReport(&mockHost, 0x07, 1, discharged, sizeof(discharged), ups_data);
    TEST_ASSERT_FALSE(ups_data.getBool("ups.status.fully_charged"));
    TEST_ASSERT_TRUE(ups_data.getBool("ups.status.depleted"));
}

void test_unrelated_report_creates_no_extended_keys(void) {
    addUsage("UPS.PowerSummary.PresentStatus.ACPresent", 0x07, 1, 0, 1);

    uint8_t report[] = { 0x07, 0x01 };
    driver.decodeReport(&mockHost, 0x07, 1, report, sizeof(report), ups_data);

    TEST_ASSERT_FALSE(ups_data.hasKey("ups.status.no_battery"));
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.status.depleted"));
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.status.remaining_time_limit_expired"));
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.status.fully_charged"));
}

// Issue 67: arrotondata come il "%.0f" di mge-hid/cps-hid, non troncata.
void test_battery_voltage_nominal_is_rounded(void) {
    addUsage("UPS.PowerSummary.ConfigVoltage", 0x08, 3, 0, 16);
    mockHost._usages.back().exponent = -1;

    uint8_t v129[] = { 0x08, 0x81, 0x00 }; // 12.9 V
    driver.decodeReport(&mockHost, 0x08, 3, v129, sizeof(v129), ups_data);
    TEST_ASSERT_EQUAL_STRING("13", ups_data.get("battery.voltage.nominal").c_str());

    uint8_t v240[] = { 0x08, 0xF0, 0x00 }; // 24.0 V
    driver.decodeReport(&mockHost, 0x08, 3, v240, sizeof(v240), ups_data);
    TEST_ASSERT_EQUAL_STRING("24", ups_data.get("battery.voltage.nominal").c_str());
}

// ups.test.result follows test_read_info of usbhid-ups (US-057)
void test_test_result_decoded(void) {
    addUsage("UPS.BatterySystem.Battery.Test", 0x0A, 3, 0, 8);

    uint8_t passed[] = { 0x0A, 1 };
    driver.decodeReport(&mockHost, 0x0A, 3, passed, sizeof(passed), ups_data);
    TEST_ASSERT_EQUAL_STRING("Done and passed", ups_data.get("ups.test.result").c_str());

    uint8_t running[] = { 0x0A, 5 };
    driver.decodeReport(&mockHost, 0x0A, 3, running, sizeof(running), ups_data);
    TEST_ASSERT_EQUAL_STRING("In progress", ups_data.get("ups.test.result").c_str());

    uint8_t none[] = { 0x0A, 6 };
    driver.decodeReport(&mockHost, 0x0A, 3, none, sizeof(none), ups_data);
    TEST_ASSERT_EQUAL_STRING("No test initiated", ups_data.get("ups.test.result").c_str());
}

void test_test_result_unknown_value_ignored(void) {
    addUsage("UPS.BatterySystem.Battery.Test", 0x0A, 3, 0, 8);

    uint8_t zero[] = { 0x0A, 0 };
    driver.decodeReport(&mockHost, 0x0A, 3, zero, sizeof(zero), ups_data);
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.test.result"));

    uint8_t nine[] = { 0x0A, 9 };
    driver.decodeReport(&mockHost, 0x0A, 3, nine, sizeof(nine), ups_data);
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.test.result"));
}

// CyberPower reports the test on UPS.Output.Test
void test_test_result_from_output_test(void) {
    addUsage("UPS.Output.Test", 0x14, 3, 0, 8);

    uint8_t passed[] = { 0x14, 1 };
    driver.decodeReport(&mockHost, 0x14, 3, passed, sizeof(passed), ups_data);
    TEST_ASSERT_EQUAL_STRING("Done and passed", ups_data.get("ups.test.result").c_str());
}

// AudibleAlarmControl 3 is "muted", as beeper_info in usbhid-ups
void test_beeper_status_muted(void) {
    addUsage("UPS.PowerSummary.AudibleAlarmControl", 0x18, 3, 0, 8);
    mockHost.activeBeeper = "UPS.PowerSummary.AudibleAlarmControl";

    uint8_t muted[] = { 0x18, 3 };
    driver.decodeReport(&mockHost, 0x18, 3, muted, sizeof(muted), ups_data);
    TEST_ASSERT_EQUAL_STRING("muted", ups_data.get("ups.beeper.status").c_str());

    uint8_t enabled[] = { 0x18, 2 };
    driver.decodeReport(&mockHost, 0x18, 3, enabled, sizeof(enabled), ups_data);
    TEST_ASSERT_EQUAL_STRING("enabled", ups_data.get("ups.beeper.status").c_str());

    uint8_t disabled[] = { 0x18, 1 };
    driver.decodeReport(&mockHost, 0x18, 3, disabled, sizeof(disabled), ups_data);
    TEST_ASSERT_EQUAL_STRING("disabled", ups_data.get("ups.beeper.status").c_str());
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_battery_present_is_inverted);
    RUN_TEST(test_remaining_time_limit_expired);
    RUN_TEST(test_fully_charged_and_discharged);
    RUN_TEST(test_unrelated_report_creates_no_extended_keys);
    RUN_TEST(test_battery_voltage_nominal_is_rounded);
    RUN_TEST(test_test_result_decoded);
    RUN_TEST(test_test_result_unknown_value_ignored);
    RUN_TEST(test_test_result_from_output_test);
    RUN_TEST(test_beeper_status_muted);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_battery_present_is_inverted);
    RUN_TEST(test_remaining_time_limit_expired);
    RUN_TEST(test_fully_charged_and_discharged);
    RUN_TEST(test_unrelated_report_creates_no_extended_keys);
    RUN_TEST(test_battery_voltage_nominal_is_rounded);
    RUN_TEST(test_test_result_decoded);
    RUN_TEST(test_test_result_unknown_value_ignored);
    RUN_TEST(test_test_result_from_output_test);
    RUN_TEST(test_beeper_status_muted);
    UNITY_END();
}
void loop() {}
#endif
#endif
