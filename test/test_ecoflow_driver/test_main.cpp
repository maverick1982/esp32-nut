#include <unity.h>
#include "EcoFlowDriver.h"
#include "IUSBHostUPS.h"

class MockEcoFlowHost : public IUSBHostUPS {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;
    std::vector<uint8_t> _requestedStrings;
    std::vector<std::pair<uint8_t, uint8_t>> _requestedReports;

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
    String getActiveBeeperPath() const override { return ""; }
    uint32_t getQuirks() const override { return 0; }
    bool isPollingPaused() const override { return false; }
    bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t) override {
        _requestedReports.push_back({report_id, report_type});
        return true;
    }
    bool requestStringDescriptor(uint8_t index) override {
        _requestedStrings.push_back(index);
        return true;
    }
};

static EcoFlowDriver driver;
static MockEcoFlowHost mockHost;
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
    mockHost._requestedStrings.clear();
    mockHost._requestedReports.clear();
    mockHost._iManufacturer = 0;
    mockHost._iProduct = 0;
    mockHost._iSerialNumber = 0;
    driver.setup();
}

void tearDown(void) {}

// ecoflow-hid.c: RunTimeToEmpty is in minutes and is converted to seconds (x60)
void test_ecoflow_runtime_minutes_to_seconds(void) {
    addUsage("UPS.PowerSummary.RunTimeToEmpty", 0x0D, 1, 0, 16);

    // 1928 minutes = 0x0788
    uint8_t report[] = { 0x0D, 0x88, 0x07 };
    driver.decodeReport(&mockHost, 0x0D, 1, report, sizeof(report), ups_data);

    TEST_ASSERT_TRUE(ups_data.hasKey("battery.runtime"));
    TEST_ASSERT_EQUAL_UINT32(115680, ups_data.getFloat("battery.runtime")); // 1928 * 60
}

void test_ecoflow_capacity_and_warning_mappings(void) {
    addUsage("UPS.PowerSummary.DesignCapacity", 0x17, 3, 0, 8);
    addUsage("UPS.PowerSummary.FullChargeCapacity", 0x0E, 3, 0, 8);
    addUsage("UPS.PowerSummary.WarningCapacityLimit", 0x0F, 3, 0, 8);
    addUsage("UPS.PowerSummary.RemainingTimeLimit", 0x08, 3, 0, 16);

    uint8_t design[] = { 0x17, 0x64 };        // 100 %
    uint8_t full[] = { 0x0E, 0x64 };          // 100 %
    uint8_t warning[] = { 0x0F, 0x0A };       // 10 %
    uint8_t timeLimit[] = { 0x08, 0x58, 0x02 }; // 600 minutes

    driver.decodeReport(&mockHost, 0x17, 3, design, sizeof(design), ups_data);
    driver.decodeReport(&mockHost, 0x0E, 3, full, sizeof(full), ups_data);
    driver.decodeReport(&mockHost, 0x0F, 3, warning, sizeof(warning), ups_data);
    driver.decodeReport(&mockHost, 0x08, 3, timeLimit, sizeof(timeLimit), ups_data);

    TEST_ASSERT_EQUAL_UINT8(100, ups_data.getFloat("battery.capacity.nominal"));
    TEST_ASSERT_EQUAL_UINT8(100, ups_data.getFloat("battery.capacity"));
    TEST_ASSERT_EQUAL_UINT8(10, ups_data.getFloat("battery.charge.warning"));
    TEST_ASSERT_EQUAL_UINT32(36000, ups_data.getFloat("battery.runtime.low")); // 600 min -> s
}

// Upstream maps UPS.Flow.[4].ConfigActivePower to ups.power.nominal; the base class
// also maps the collapsed path to ups.realpower.nominal (mge-hid behavior).
void test_ecoflow_config_active_power_keeps_generic_realpower_nominal(void) {
    addUsage("UPS.Flow.ConfigActivePower", 0x01, 3, 0, 16);

    uint8_t report[] = { 0x01, 0x04, 0x01 }; // 260
    driver.decodeReport(&mockHost, 0x01, 3, report, sizeof(report), ups_data);

    TEST_ASSERT_EQUAL_UINT16(260, ups_data.getFloat("ups.power.nominal"));
    TEST_ASSERT_EQUAL_UINT16(260, ups_data.getFloat("ups.realpower.nominal"));
}

void test_ecoflow_outlet_timers(void) {
    addUsage("UPS.OutletSystem.Outlet.DelayBeforeReboot", 0x13, 3, 0, 16);
    addUsage("UPS.OutletSystem.Outlet.DelayBeforeShutdown", 0x12, 3, 0, 16);

    uint8_t reboot[] = { 0x13, 0x1E, 0x00 };   // 30
    uint8_t shutdown[] = { 0x12, 0x3C, 0x00 }; // 60

    driver.decodeReport(&mockHost, 0x13, 3, reboot, sizeof(reboot), ups_data);
    driver.decodeReport(&mockHost, 0x12, 3, shutdown, sizeof(shutdown), ups_data);

    TEST_ASSERT_EQUAL_UINT8(30, ups_data.getFloat("ups.timer.reboot"));
    TEST_ASSERT_EQUAL_UINT8(60, ups_data.getFloat("ups.timer.shutdown"));
}

// ecoflow-hid.c: iDeviceChemistry via stringid_conversion -> battery.type
void test_ecoflow_chemistry_string_descriptor(void) {
    addUsage("UPS.PowerSummary.iDeviceChemistry", 0x1F, 3, 0, 8);

    uint8_t report[] = { 0x1F, 0x02 }; // string index 2
    driver.decodeReport(&mockHost, 0x1F, 3, report, sizeof(report), ups_data);

    // The next full poll must request the chemistry string
    driver.loop(&mockHost, ups_data, 1000);
    TEST_ASSERT_EQUAL_UINT32(1, mockHost._requestedStrings.size());
    TEST_ASSERT_EQUAL_UINT8(2, mockHost._requestedStrings[0]);

    // "Li" as a UTF-16LE USB string descriptor
    uint8_t desc[] = { 6, 0x03, 'L', 0, 'i', 0 };
    driver.parseStringDescriptor(&mockHost, 2, desc, sizeof(desc), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("battery.type"));
    TEST_ASSERT_EQUAL_STRING("Li", ups_data.get("battery.type").c_str());
}

void test_ecoflow_present_status(void) {
    addUsage("UPS.PowerSummary.PresentStatus.Discharging", 0x07, 1, 1, 1);
    addUsage("UPS.PowerSummary.PresentStatus.ACPresent", 0x07, 1, 2, 1);

    uint8_t onBattery[] = { 0x07, 0x02 }; // Discharging=1, ACPresent=0
    driver.decodeReport(&mockHost, 0x07, 1, onBattery, sizeof(onBattery), ups_data);
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG", UPSData::computeUPSStatusString(ups_data).c_str());

    uint8_t online[] = { 0x07, 0x04 }; // Discharging=0, ACPresent=1
    driver.decodeReport(&mockHost, 0x07, 1, online, sizeof(online), ups_data);
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(ups_data).c_str());
}

// A negative RunTimeToEmpty means "no estimate" upstream; GenericDriver has already
// stored the raw negative, so the EcoFlow handler must drop it instead of returning.
void test_ecoflow_runtime_negative_is_dropped(void) {
    addUsage("UPS.PowerSummary.RunTimeToEmpty", 0x0D, 1, 0, 16);
    mockHost._usages.back().logical_min = -32768;

    uint8_t report[] = { 0x0D, 0xFF, 0xFF }; // -1
    driver.decodeReport(&mockHost, 0x0D, 1, report, sizeof(report), ups_data);

    TEST_ASSERT_FALSE(ups_data.hasKey("battery.runtime"));
}

// ecoflow-hid.c: ecoflow_format_mfr() falls back to "EcoFlow" when the device
// descriptor exposes no Vendor string.
void test_ecoflow_mfr_falls_back_when_no_manufacturer_string(void) {
    mockHost._iManufacturer = 0;

    driver.loop(&mockHost, ups_data, 1000);

    TEST_ASSERT_TRUE(ups_data.hasKey("ups.mfr"));
    TEST_ASSERT_EQUAL_STRING("EcoFlow", ups_data.get("ups.mfr").c_str());
}

// A Vendor string that decodes to empty keeps the same "EcoFlow" fallback.
void test_ecoflow_mfr_falls_back_when_manufacturer_string_empty(void) {
    mockHost._iManufacturer = 3;

    uint8_t empty[] = { 2, 0x03 }; // bLength 2, STRING descriptor, no characters
    driver.parseStringDescriptor(&mockHost, 3, empty, sizeof(empty), ups_data);

    TEST_ASSERT_TRUE(ups_data.hasKey("ups.mfr"));
    TEST_ASSERT_EQUAL_STRING("EcoFlow", ups_data.get("ups.mfr").c_str());
}

// A reported Vendor string is preserved, not overwritten by the fallback.
void test_ecoflow_mfr_keeps_reported_vendor(void) {
    mockHost._iManufacturer = 3;

    uint8_t desc[] = { 4, 0x03, 'A', 0 }; // "A" as UTF-16LE USB string descriptor
    driver.parseStringDescriptor(&mockHost, 3, desc, sizeof(desc), ups_data);

    TEST_ASSERT_EQUAL_STRING("A", ups_data.get("ups.mfr").c_str());
}

// ecoflow-hid.c leaves the beeper commands commented out: the device ignores them.
void test_ecoflow_beeper_not_controllable(void) {
    TEST_ASSERT_FALSE(driver.beeperControllable());
}

// ecoflow-hid.c maps only UPS.OutletSystem.Outlet.DelayBeforeShutdown to
// ups.timer.shutdown and exposes no ups.delay.shutdown; GenericDriver maps the
// PowerSummary usage to both, so EcoFlowDriver must undo it. Outlet first.
void test_ecoflow_shutdown_timer_outlet_wins_when_outlet_first(void) {
    addUsage("UPS.OutletSystem.Outlet.DelayBeforeShutdown", 0x12, 3, 0, 16);
    addUsage("UPS.PowerSummary.DelayBeforeShutdown", 0x20, 3, 0, 16);

    uint8_t outlet[] = { 0x12, 0x3C, 0x00 }; // 60
    uint8_t power[] = { 0x20, 0x1E, 0x00 };  // 30
    driver.decodeReport(&mockHost, 0x12, 3, outlet, sizeof(outlet), ups_data);
    driver.decodeReport(&mockHost, 0x20, 3, power, sizeof(power), ups_data);

    TEST_ASSERT_EQUAL_UINT8(60, ups_data.getFloat("ups.timer.shutdown"));
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.delay.shutdown"));
}

// Same, with the PowerSummary report decoded last (the base value must not win).
void test_ecoflow_shutdown_timer_outlet_wins_when_powersummary_first(void) {
    addUsage("UPS.OutletSystem.Outlet.DelayBeforeShutdown", 0x12, 3, 0, 16);
    addUsage("UPS.PowerSummary.DelayBeforeShutdown", 0x20, 3, 0, 16);

    uint8_t power[] = { 0x20, 0x1E, 0x00 };  // 30
    uint8_t outlet[] = { 0x12, 0x3C, 0x00 }; // 60
    driver.decodeReport(&mockHost, 0x20, 3, power, sizeof(power), ups_data);
    driver.decodeReport(&mockHost, 0x12, 3, outlet, sizeof(outlet), ups_data);

    TEST_ASSERT_EQUAL_UINT8(60, ups_data.getFloat("ups.timer.shutdown"));
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.delay.shutdown"));
}

// Without the Outlet usage neither key is exposed (upstream leaves the PowerSummary
// timer unmapped).
void test_ecoflow_shutdown_timer_absent_without_outlet(void) {
    addUsage("UPS.PowerSummary.DelayBeforeShutdown", 0x20, 3, 0, 16);

    uint8_t power[] = { 0x20, 0x1E, 0x00 }; // 30
    driver.decodeReport(&mockHost, 0x20, 3, power, sizeof(power), ups_data);

    TEST_ASSERT_FALSE(ups_data.hasKey("ups.timer.shutdown"));
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.delay.shutdown"));
}

// ecoflow-hid.c maps UPS.PowerSummary.ConfigVoltage with "%.1f"; GenericDriver
// rounds it to a whole volt (issue 67).
void test_ecoflow_battery_voltage_nominal_one_decimal(void) {
    addUsage("UPS.PowerSummary.ConfigVoltage", 0x08, 3, 0, 16);
    mockHost._usages.back().exponent = -2;

    uint8_t r1200[] = { 0x08, 0xB0, 0x04 }; // 12.00 V
    driver.decodeReport(&mockHost, 0x08, 3, r1200, sizeof(r1200), ups_data);
    TEST_ASSERT_EQUAL_STRING("12.0", ups_data.get("battery.voltage.nominal").c_str());

    uint8_t r1360[] = { 0x08, 0x50, 0x05 }; // 13.60 V
    driver.decodeReport(&mockHost, 0x08, 3, r1360, sizeof(r1360), ups_data);
    TEST_ASSERT_EQUAL_STRING("13.6", ups_data.get("battery.voltage.nominal").c_str());
}

// US-058: ecoflow-hid.c leaves the shutdown commands commented out
void test_ecoflow_no_shutdown_commands(void) {
    TEST_ASSERT_FALSE(driver.shutdownCommandsSupported());
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_ecoflow_runtime_minutes_to_seconds);
    RUN_TEST(test_ecoflow_capacity_and_warning_mappings);
    RUN_TEST(test_ecoflow_config_active_power_keeps_generic_realpower_nominal);
    RUN_TEST(test_ecoflow_outlet_timers);
    RUN_TEST(test_ecoflow_chemistry_string_descriptor);
    RUN_TEST(test_ecoflow_present_status);
    RUN_TEST(test_ecoflow_runtime_negative_is_dropped);
    RUN_TEST(test_ecoflow_mfr_falls_back_when_no_manufacturer_string);
    RUN_TEST(test_ecoflow_mfr_falls_back_when_manufacturer_string_empty);
    RUN_TEST(test_ecoflow_mfr_keeps_reported_vendor);
    RUN_TEST(test_ecoflow_beeper_not_controllable);
    RUN_TEST(test_ecoflow_shutdown_timer_outlet_wins_when_outlet_first);
    RUN_TEST(test_ecoflow_shutdown_timer_outlet_wins_when_powersummary_first);
    RUN_TEST(test_ecoflow_shutdown_timer_absent_without_outlet);
    RUN_TEST(test_ecoflow_no_shutdown_commands);
    RUN_TEST(test_ecoflow_battery_voltage_nominal_one_decimal);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_ecoflow_runtime_minutes_to_seconds);
    RUN_TEST(test_ecoflow_capacity_and_warning_mappings);
    RUN_TEST(test_ecoflow_config_active_power_keeps_generic_realpower_nominal);
    RUN_TEST(test_ecoflow_outlet_timers);
    RUN_TEST(test_ecoflow_chemistry_string_descriptor);
    RUN_TEST(test_ecoflow_present_status);
    RUN_TEST(test_ecoflow_runtime_negative_is_dropped);
    RUN_TEST(test_ecoflow_mfr_falls_back_when_no_manufacturer_string);
    RUN_TEST(test_ecoflow_mfr_falls_back_when_manufacturer_string_empty);
    RUN_TEST(test_ecoflow_mfr_keeps_reported_vendor);
    RUN_TEST(test_ecoflow_beeper_not_controllable);
    RUN_TEST(test_ecoflow_shutdown_timer_outlet_wins_when_outlet_first);
    RUN_TEST(test_ecoflow_shutdown_timer_outlet_wins_when_powersummary_first);
    RUN_TEST(test_ecoflow_shutdown_timer_absent_without_outlet);
    RUN_TEST(test_ecoflow_no_shutdown_commands);
    RUN_TEST(test_ecoflow_battery_voltage_nominal_one_decimal);
    UNITY_END();
}
void loop() {}
#endif
#endif
