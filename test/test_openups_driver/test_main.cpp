#include <unity.h>
#include "OpenUPSDriver.h"
#include "IHidTransport.h"
#include "UPSData.h"
#include <string>
#include <vector>

// OpenUPSDriver on openupsHid2Nut() (US-078): scales of the product ID, the status functions
// of openups-hid.c, the BatteryPresent deviation (issue #26) and the ups.load estimate.

class MockOpenUPSHost : public IHidTransport {
public:
    std::vector<HIDUsageDef> _usages;
    uint16_t _pid = 0xD005;

    void lock() const override {}
    void unlock() const override {}

    HIDParser _hid_parser;
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    String getActiveBeeperPath() const override { return ""; }
    uint32_t getQuirks() const override { return 0; }
    bool isPollingPaused() const override { return false; }
    bool requestReport(uint8_t, uint8_t, uint16_t) override { return true; }
    bool requestStringDescriptor(uint8_t) override { return true; }
    uint16_t getVID() const override { return 0x04D8; }
    uint16_t getPID() const override { return _pid; }
};

static OpenUPSDriver driver;
static MockOpenUPSHost mockHost;
static UPSData ups_data;

static void addUsage(const char* path, uint8_t report_id, uint16_t bit_offset, uint16_t bit_size) {
    HIDUsageDef u;
    u.report_id = report_id;
    u.report_type = 3;
    u.bit_offset = bit_offset;
    u.bit_size = bit_size;
    u.found = true;
    strncpy(u.path, path, sizeof(u.path) - 1);
    u.path[sizeof(u.path) - 1] = '\0';
    mockHost._usages.push_back(u);
}

// Report 1: the PresentStatus bits of openups_hid2nut; report 2: the measures (16 bits each)
static void openupsLayout() {
    static const char* const status[] = {
        "Good", "InternalFailure", "Overload", "OverTemperature", "ShutdownImminent",
        "BelowRemainingCapacityLimit", "RemainingTimeLimitExpired", "Charging", "Discharging",
        "NeedReplacement", "ACPresent", "BatteryPresent",
    };
    for (uint16_t b = 0; b < sizeof(status) / sizeof(status[0]); b++) {
        char path[80];
        snprintf(path, sizeof(path), "UPS.PowerSummary.PresentStatus.%s", status[b]);
        addUsage(path, 1, b, 1);
    }
    addUsage("UPS.PowerSummary.Output.Voltage", 2, 0, 16);
    addUsage("UPS.PowerSummary.Output.Current", 2, 16, 16);
    addUsage("UPS.PowerSummary.Input.Voltage", 2, 32, 16);
    addUsage("UPS.PowerSummary.Input.Current", 2, 48, 16);
    addUsage("UPS.PowerSummary.Temperature", 2, 64, 16);
}

void setUp(void) {
    ups_data = UPSData();
    mockHost._usages.clear();
    mockHost._pid = 0xD005;
    driver.setup();
    openupsLayout();
}

void tearDown(void) {}

// As USBHostUPS: decodeReport() and then postDecode(), where the NUT table writes its rows
static void decode(const std::vector<uint8_t>& bytes) {
    driver.decodeReport(&mockHost, bytes[0], 3, bytes.data(), bytes.size(), ups_data);
    driver.postDecode(&mockHost, bytes[0], 3, bytes.data(), bytes.size(), ups_data);
}

static std::string status() { return UPSData::computeUPSStatusString(ups_data).c_str(); }

// Output 122 (12.2 V), 13 (1.3 A); input 123, 12; temperature 5.12 (exactly 25 °C)
static const std::vector<uint8_t> kMeasures = { 2, 122, 0, 13, 0, 123, 0, 12, 0, 0, 0 };

// Bits: Good (0), Charging (7), ACPresent (10), BatteryPresent (11)
static std::vector<uint8_t> statusReport(uint16_t bits) { return { 1, (uint8_t)(bits & 0xFF), (uint8_t)(bits >> 8) }; }
static const uint16_t GOOD = 1 << 0, INTERNAL_FAILURE = 1 << 1, CHARGING = 1 << 7, DISCHARGING = 1 << 8,
                      AC_PRESENT = 1 << 10, BATTERY_PRESENT = 1 << 11;

// get_voltage_multiplier(): 0.1 for PID 0xD005, "%.2f" voltages and "%.3f" currents
void test_openups_scales_pid_d005(void) {
    decode(kMeasures);
    TEST_ASSERT_EQUAL_STRING("12.20", ups_data.get("output.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("1.300", ups_data.get("output.current").c_str());
    TEST_ASSERT_EQUAL_STRING("12.30", ups_data.get("input.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("1.200", ups_data.get("input.current").c_str());
}

// The constants of PID 0xD004 (openups-hid.c:37-41)
void test_openups_scales_pid_d004(void) {
    mockHost._pid = 0xD004;
    decode(kMeasures);
    TEST_ASSERT_EQUAL_STRING("313.66", ups_data.get("output.voltage").c_str());  // 122 * 2.571
    TEST_ASSERT_EQUAL_STRING("20.947", ups_data.get("output.current").c_str()); // 13 * 1.6113
    TEST_ASSERT_EQUAL_STRING("436.04", ups_data.get("input.voltage").c_str());   // 123 * 3.545
    TEST_ASSERT_EQUAL_STRING("0.993", ups_data.get("input.current").c_str());   // 12 * 0.08274
}

// Any other product ID keeps the scales at 1, as the static defaults upstream
void test_openups_scales_other_pid(void) {
    mockHost._pid = 0xD006;
    decode(kMeasures);
    TEST_ASSERT_EQUAL_STRING("122.00", ups_data.get("output.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("13.000", ups_data.get("output.current").c_str());
}

// openups_temperature_fun on UPS.PowerSummary.Temperature: battery.temperature only, the
// thermistor reading never becomes ups.temperature (before US-078 it did, 5.5 on the W150)
void test_openups_temperature_is_battery_only(void) {
    HIDUsageDef& t = mockHost._usages.back();
    t.exponent = -2;
    decode({ 2, 122, 0, 13, 0, 123, 0, 12, 0, 0x00, 0x02 }); // 512 -> 5.12 -> exactly 25 °C
    TEST_ASSERT_EQUAL_STRING("25", ups_data.get("battery.temperature").c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.temperature"));
}

// openups_online_fun, openups_charging_fun, openups_off_fun
void test_openups_status_functions(void) {
    // No FullyCharged usage: CHRG needs battery.charge below 100, as ups_status_set()
    ups_data.set("battery.charge", "80");
    decode(statusReport(GOOD | CHARGING | AC_PRESENT | BATTERY_PRESENT));
    TEST_ASSERT_EQUAL_STRING("OL CHRG", status().c_str());

    decode(statusReport(GOOD | DISCHARGING | BATTERY_PRESENT));
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG", status().c_str());

    // Good = 0: openups_off_fun gives "off"
    decode(statusReport(AC_PRESENT | BATTERY_PRESENT));
    TEST_ASSERT_EQUAL_STRING("OL OFF", status().c_str()); // token order of ups_status_set()
}

// InternalFailure is commfault_info upstream
void test_openups_internal_failure(void) {
    decode(statusReport(GOOD | INTERNAL_FAILURE | AC_PRESENT | BATTERY_PRESENT));
    TEST_ASSERT_EQUAL_STRING("ALARM OL", status().c_str());
}

// Deviation (issue #26, US-055): BatteryPresent = 0 with the battery fitted on some units;
// filterStatus() drops the nobattery bit, so no RB and no "No battery installed!" alarm
void test_openups_battery_present_ignored(void) {
    decode(statusReport(GOOD | AC_PRESENT));
    TEST_ASSERT_EQUAL_STRING("OL", status().c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.status.no_battery"));
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.alarm"));

    decode(statusReport(GOOD | AC_PRESENT | BATTERY_PRESENT));
    TEST_ASSERT_EQUAL_STRING("OL", status().c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.status.no_battery"));
}

// Extension (ADR 0011 rule 2): ups.load from output.voltage x output.current over the nominal
// power of the model name, 150 W without one
void test_openups_load_estimate(void) {
    decode(kMeasures); // 12.20 V x 1.300 A = 15.86 W
    TEST_ASSERT_EQUAL_STRING("150", ups_data.get("ups.realpower.nominal").c_str());
    TEST_ASSERT_EQUAL_STRING("11", ups_data.get("ups.load").c_str());
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.realpower"));

    ups_data.set("ups.model", "Smart UPS W120");
    decode(kMeasures);
    TEST_ASSERT_EQUAL_STRING("120", ups_data.get("ups.realpower.nominal").c_str());
    TEST_ASSERT_EQUAL_STRING("13", ups_data.get("ups.load").c_str());
}

// stringid_conversion rows 296, 299 and 300: ups.serial, battery.type, battery.mfr.date from
// the string descriptors whose indices the reports give
void test_openups_string_rows(void) {
    addUsage("UPS.PowerSummary.iSerialNumber", 3, 0, 8);
    addUsage("UPS.PowerSummary.iDeviceChemistry", 3, 8, 8);
    addUsage("UPS.PowerSummary.iOEMInformation", 3, 16, 8);
    decode({ 3, 3, 8, 9 });

    const struct { uint8_t index; const char* text; } strings[] = {
        { 3, "E4B323491C40" }, { 8, "LiFePO4" }, { 9, "OPEN" },
    };
    for (const auto& str : strings) {
        std::vector<uint8_t> desc = { (uint8_t)(2 + 2 * strlen(str.text)), 0x03 };
        for (const char* c = str.text; *c; c++) {
            desc.push_back((uint8_t)*c);
            desc.push_back(0);
        }
        driver.parseStringDescriptor(&mockHost, str.index, desc.data(), desc.size(), ups_data);
    }
    TEST_ASSERT_EQUAL_STRING("E4B323491C40", ups_data.get("ups.serial").c_str());
    TEST_ASSERT_EQUAL_STRING("LiFePO4", ups_data.get("battery.type").c_str());
    TEST_ASSERT_EQUAL_STRING("OPEN", ups_data.get("battery.mfr.date").c_str());
}

// openups_format_mfr(): "openUPS" without a manufacturer string, the USB string otherwise
void test_openups_mfr_fallback(void) {
    driver.formatDeviceStrings(ups_data);
    TEST_ASSERT_EQUAL_STRING("openUPS", ups_data.get("ups.mfr").c_str());

    ups_data.set("ups.mfr", "WalleCube");
    driver.formatDeviceStrings(ups_data);
    TEST_ASSERT_EQUAL_STRING("WalleCube", ups_data.get("ups.mfr").c_str());
}

// openups-hid.c has no commands: nothing from the table, no load/shutdown (US-058)
void test_openups_no_commands(void) {
    size_t n = 0;
    const NutTable::HidInfo* t = driver.commandTable(n);
    TEST_ASSERT_TRUE(n > 0);
    for (size_t i = 0; i < n; i++) TEST_ASSERT_FALSE_MESSAGE(NutTable::isCommandRow(t[i]), t[i].info_type);
    TEST_ASSERT_FALSE(driver.shutdownCommandsSupported());
}

#ifdef PIO_UNIT_TESTING
static void runAll() {
    RUN_TEST(test_openups_scales_pid_d005);
    RUN_TEST(test_openups_scales_pid_d004);
    RUN_TEST(test_openups_scales_other_pid);
    RUN_TEST(test_openups_temperature_is_battery_only);
    RUN_TEST(test_openups_status_functions);
    RUN_TEST(test_openups_internal_failure);
    RUN_TEST(test_openups_battery_present_ignored);
    RUN_TEST(test_openups_load_estimate);
    RUN_TEST(test_openups_string_rows);
    RUN_TEST(test_openups_mfr_fallback);
    RUN_TEST(test_openups_no_commands);
}

#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    runAll();
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    runAll();
    UNITY_END();
}
void loop() {}
#endif
#endif
