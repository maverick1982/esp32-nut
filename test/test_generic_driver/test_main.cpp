#include <unity.h>
#include "GenericDriver.h"
#include "OpenUPSDriver.h"
#include "IHidTransport.h"
#include "UPSData.h"

class MockGenericHost : public IHidTransport {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;

    void lock() const override {}
    void unlock() const override {}

    HIDParser _hid_parser;
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
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

// US-058, user decision of 2026-10-09: DelayBeforeShutdown is the timer (ups.timer.shutdown);
// ups.delay.* are configured delays set by the host, never written by the driver
void test_delay_before_shutdown_sets_only_timer(void) {
    addUsage("UPS.PowerSummary.DelayBeforeShutdown", 0x15, 3, 0, 16);
    uint8_t report[] = { 0x15, 30, 0 };
    driver.decodeReport(&mockHost, 0x15, 3, report, sizeof(report), ups_data);
    TEST_ASSERT_EQUAL_STRING("30", ups_data.get("ups.timer.shutdown").c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.delay.shutdown"));
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.delay.start"));
}

// DEFAULT_OFFDELAY / DEFAULT_ONDELAY of usbhid-ups.h, shutdown commands offered
void test_default_delays_and_shutdown_commands(void) {
    TEST_ASSERT_EQUAL_INT32(20, driver.defaultOffDelay());
    TEST_ASSERT_EQUAL_INT32(30, driver.defaultOnDelay());
    TEST_ASSERT_TRUE(driver.shutdownCommandsSupported());
}

// US-058: openups-hid.c has no load/shutdown commands (no dedicated OpenUPS test env)
void test_openups_no_shutdown_commands(void) {
    OpenUPSDriver openups;
    TEST_ASSERT_FALSE(openups.shutdownCommandsSupported());
}

// --- NUT table engine (US-073): a driver with a small table of its own

static const NutTable::HidInfo kEngineTable[] = {
    { "ups.load", 0, 0, "UPS.PowerSummary.PercentLoad", "%.0f", 0, NutTable::Conv::Number, nullptr, 0 },
    { "ups.firmware", 0, 0, "UPS.PowerSummary.0xFFFF00F1", "%s", 0, NutTable::Conv::StringId, nullptr, 0 },
    { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.ACPresent", nullptr, 0, NutTable::Conv::Lookup, NUT_LKP(NutTable::kOnlineInfo) },
    { "battery.charger.status", 0, 0, "UPS.BatterySystem.Charger.0xFFFF00E9", "%.0f", 0, NutTable::Conv::AbmStatus, nullptr, 0 },
};

class TableDriver : public GenericDriver {
public:
    using GenericDriver::collectStringRequests;
    const NutTable::HidInfo* hid2nut(size_t& n) const override {
        n = sizeof(kEngineTable) / sizeof(kEngineTable[0]);
        return kEngineTable;
    }
    const char* const* supersededGenericPaths(size_t& n) const override {
        static const char* const paths[] = { "UPS.PowerSummary.PercentLoad", "UPS.PowerSummary.RunTimeToEmpty" };
        n = 2;
        return paths;
    }
};

static TableDriver tableDriver;

// Usages of the engine tests: one 8-bit value per report, report ID first
static void addEngineUsages() {
    addUsage("UPS.PowerSummary.PresentStatus.ACPresent", 1, 3, 0, 1);
    addUsage("UPS.BatterySystem.Charger.0xFFFF00E9", 3, 3, 0, 8);
    addUsage("UPS.PowerSummary.PercentLoad", 7, 3, 0, 8);
    addUsage("UPS.PowerSummary.RunTimeToEmpty", 7, 3, 8, 8);
    addUsage("UPS.PowerSummary.0xFFFF00F1", 16, 3, 0, 8);
    addUsage("UPS.Unmapped", 9, 3, 0, 8);
    tableDriver.setup();
}

static void feed(GenericDriver& drv, std::initializer_list<uint8_t> bytes) {
    std::vector<uint8_t> r(bytes);
    drv.decodeReport(&mockHost, r[0], 3, r.data(), r.size(), ups_data);
    drv.postDecode(&mockHost, r[0], 3, r.data(), r.size(), ups_data);
}

void test_engine_report_without_rows_writes_nothing(void) {
    addEngineUsages();
    feed(tableDriver, { 1, 1 });
    feed(tableDriver, { 7, 5, 60 });
    uint32_t before = ups_data.writes();
    feed(tableDriver, { 9, 42 }); // no row and no mapping on report 9
    TEST_ASSERT_EQUAL_UINT32(before, ups_data.writes());
}

void test_engine_same_value_is_not_rewritten(void) {
    addEngineUsages();
    feed(tableDriver, { 7, 5, 60 });
    TEST_ASSERT_EQUAL_STRING("5", ups_data.get("ups.load").c_str());
    uint32_t before = ups_data.writes();
    feed(tableDriver, { 7, 5, 60 });
    TEST_ASSERT_EQUAL_UINT32(before, ups_data.writes());
}

void test_engine_overwritten_value_is_restored(void) {
    addEngineUsages();
    feed(tableDriver, { 7, 5, 60 });
    ups_data.set("ups.load", "99"); // as a hand-written mapping would
    feed(tableDriver, { 7, 5, 60 });
    TEST_ASSERT_EQUAL_STRING("5", ups_data.get("ups.load").c_str());
}

void test_engine_superseded_mapping_does_not_write(void) {
    addEngineUsages();
    feed(tableDriver, { 7, 5, 60 });
    TEST_ASSERT_FALSE(ups_data.hasKey("battery.runtime")); // superseded, no table row
    feed(driver, { 7, 5, 60 }); // the plain GenericDriver still maps it
    TEST_ASSERT_EQUAL_STRING("60", ups_data.get("battery.runtime").c_str());
}

void test_engine_string_id_row(void) {
    addEngineUsages();
    feed(tableDriver, { 16, 6 });
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.firmware"));
    std::vector<uint8_t> wanted;
    tableDriver.collectStringRequests(&mockHost, ups_data, wanted);
    TEST_ASSERT_EQUAL(1, wanted.size());
    TEST_ASSERT_EQUAL(6, wanted[0]);

    const uint8_t desc[] = { 8, 0x03, 'F', 0, 'W', 0, '1', 0 };
    tableDriver.parseStringDescriptor(&mockHost, 6, desc, sizeof(desc), ups_data);
    TEST_ASSERT_EQUAL_STRING("FW1", ups_data.get("ups.firmware").c_str());
    wanted.clear();
    tableDriver.collectStringRequests(&mockHost, ups_data, wanted);
    TEST_ASSERT_EQUAL(0, wanted.size());
}

// UTF-16LE string descriptor of an ASCII text
static std::vector<uint8_t> stringDescriptor(const char* text) {
    std::vector<uint8_t> d = { (uint8_t)(2 + 2 * strlen(text)), 0x03 };
    for (const char* c = text; *c; c++) {
        d.push_back((uint8_t)*c);
        d.push_back(0);
    }
    return d;
}

// stringid_conversion_fun (usbhid-ups.c:598-603) reads into static char buf[20]: a table
// row gets at most 19 characters; the other strings are not table rows and stay whole
void test_engine_string_id_truncated_to_19_chars(void) {
    addEngineUsages();
    mockHost._iManufacturer = 1;
    feed(tableDriver, { 16, 6 });
    std::vector<uint8_t> fw = stringDescriptor("0123456789ABCDEFGHIJKLMNO"); // 25 characters
    tableDriver.parseStringDescriptor(&mockHost, 6, fw.data(), fw.size(), ups_data);
    TEST_ASSERT_EQUAL_STRING("0123456789ABCDEFGHI", ups_data.get("ups.firmware").c_str());
    std::vector<uint8_t> mfr = stringDescriptor("EATON EATON EATON EATON"); // 23 characters
    tableDriver.parseStringDescriptor(&mockHost, 1, mfr.data(), mfr.size(), ups_data);
    TEST_ASSERT_EQUAL_STRING("EATON EATON EATON EATON", ups_data.get("ups.mfr").c_str());
    mockHost._iManufacturer = 0;
}

void test_engine_bool_row_sets_internal_key(void) {
    addEngineUsages();
    feed(tableDriver, { 1, 1 });
    TEST_ASSERT_EQUAL_STRING("1", ups_data.get("ups.status.ac_present").c_str());
    feed(tableDriver, { 1, 0 });
    TEST_ASSERT_EQUAL_STRING("0", ups_data.get("ups.status.ac_present").c_str());
}

void test_engine_clear_removes_key(void) {
    addEngineUsages();
    // ABM unknown (no ABMEnable row): eaton_abm_status_fun deletes the variable
    ups_data.set("battery.charger.status", "charging");
    feed(tableDriver, { 3, 1 });
    TEST_ASSERT_FALSE(ups_data.hasKey("battery.charger.status"));
}

void test_engine_load_updates_realpower(void) {
    addEngineUsages();
    ups_data.set("ups.power.nominal", "1000");
    feed(tableDriver, { 7, 50, 60 });
    // UPSData::updateRealPower(): 1000 VA * 0.6 * 50 %
    TEST_ASSERT_EQUAL_STRING("300", ups_data.get("ups.realpower").c_str());
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_engine_report_without_rows_writes_nothing);
    RUN_TEST(test_engine_same_value_is_not_rewritten);
    RUN_TEST(test_engine_overwritten_value_is_restored);
    RUN_TEST(test_engine_superseded_mapping_does_not_write);
    RUN_TEST(test_engine_string_id_row);
    RUN_TEST(test_engine_string_id_truncated_to_19_chars);
    RUN_TEST(test_engine_bool_row_sets_internal_key);
    RUN_TEST(test_engine_clear_removes_key);
    RUN_TEST(test_engine_load_updates_realpower);
    RUN_TEST(test_battery_present_is_inverted);
    RUN_TEST(test_remaining_time_limit_expired);
    RUN_TEST(test_fully_charged_and_discharged);
    RUN_TEST(test_unrelated_report_creates_no_extended_keys);
    RUN_TEST(test_battery_voltage_nominal_is_rounded);
    RUN_TEST(test_test_result_decoded);
    RUN_TEST(test_test_result_unknown_value_ignored);
    RUN_TEST(test_test_result_from_output_test);
    RUN_TEST(test_beeper_status_muted);
    RUN_TEST(test_delay_before_shutdown_sets_only_timer);
    RUN_TEST(test_default_delays_and_shutdown_commands);
    RUN_TEST(test_openups_no_shutdown_commands);
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
    RUN_TEST(test_delay_before_shutdown_sets_only_timer);
    RUN_TEST(test_default_delays_and_shutdown_commands);
    RUN_TEST(test_openups_no_shutdown_commands);
    UNITY_END();
}
void loop() {}
#endif
#endif
