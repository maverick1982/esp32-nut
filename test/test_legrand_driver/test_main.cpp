#include <unity.h>
#include <string.h>
#include <memory>
#include "LegrandDriver.h"
#include "DriverRegistry.h"
#include "CommandCatalog.h"
#include "IUSBHostUPS.h"

// US-087: Legrand Keor PDU / Keor SP, after nut_repo/drivers/legrand-hid.c

class MockLegrandHost : public IUSBHostUPS {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;
    String _beeperPath;

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
    String getActiveBeeperPath() const override { return _beeperPath; }
    uint32_t getQuirks() const override { return 0; }
    bool isPollingPaused() const override { return false; }
    bool requestReport(uint8_t, uint8_t, uint16_t) override { return true; }
    bool requestStringDescriptor(uint8_t) override { return true; }
};

static LegrandDriver driver;
static MockLegrandHost mockHost;
static UPSData ups_data;

static HIDUsageDef makeUsage(const char* path, uint8_t report_id, uint8_t report_type,
                             uint16_t bit_offset, uint16_t bit_size) {
    HIDUsageDef u;
    u.report_id = report_id;
    u.report_type = report_type;
    u.bit_offset = bit_offset;
    u.bit_size = bit_size;
    u.found = true;
    strncpy(u.path, path, sizeof(u.path) - 1);
    u.path[sizeof(u.path) - 1] = '\0';
    return u;
}

static void addUsage(const char* path, uint8_t report_id, uint8_t report_type,
                     uint16_t bit_offset, uint16_t bit_size) {
    mockHost._usages.push_back(makeUsage(path, report_id, report_type, bit_offset, bit_size));
}

// Beeper of the Keor PDU 800 (issue #90): FEATURE 0x0C, 8 bits, logical 1..3
static void addKeorBeeper() {
    HIDUsageDef u = makeUsage("UPS.PowerSummary.AudibleAlarmControl", 0x0C, 3, 0, 8);
    u.logical_min = 1;
    u.logical_max = 3;
    mockHost._usages.push_back(u);
    mockHost._beeperPath = "UPS.PowerSummary.AudibleAlarmControl";
}

static bool hasCommand(const std::vector<const UPSCommandInfo*>& cmds, const char* name) {
    for (const auto* c : cmds) {
        if (strcmp(c->name, name) == 0) return true;
    }
    return false;
}

void setUp(void) {
    ups_data = UPSData();
    mockHost._usages.clear();
    mockHost._beeperPath = "";
    mockHost._iManufacturer = 0;
    mockHost._iProduct = 0;
    mockHost._iSerialNumber = 0;
    driver.setup();
}

void tearDown(void) {}

// legrand_usb_device_table: Keor PDU and Keor SP; any other Legrand product stays generic
void test_registry_selects_legrand_for_keor_pdu_and_sp(void) {
    std::unique_ptr<IUPSDriver> pdu(DriverRegistry::create(0x1CB0, 0x0038));
    std::unique_ptr<IUPSDriver> sp(DriverRegistry::create(0x1CB0, 0x0032));
    std::unique_ptr<IUPSDriver> other(DriverRegistry::create(0x1CB0, 0x0001));
    TEST_ASSERT_EQUAL_STRING("LegrandDriver", pdu->getDriverName());
    TEST_ASSERT_EQUAL_STRING("LegrandDriver", sp->getDriverName());
    TEST_ASSERT_EQUAL_STRING("GenericDriver", other->getDriverName());
}

// legrand-hid.c: beeper.enable writes 2, beeper.disable writes 3
void test_beeper_encoding_uses_legrand_values(void) {
    TEST_ASSERT_EQUAL_UINT8(2, driver.encodeBeeperValue(true, 8));
    TEST_ASSERT_EQUAL_UINT8(3, driver.encodeBeeperValue(false, 8));
    TEST_ASSERT_EQUAL_UINT8(1, driver.encodeBeeperValue(true, 1));
    TEST_ASSERT_EQUAL_UINT8(0, driver.encodeBeeperValue(false, 1));
}

static void decodeBeeper(uint8_t value) {
    uint8_t report[] = { 0x0C, value };
    driver.decodeReport(&mockHost, 0x0C, 3, report, sizeof(report), ups_data);
}

// Deviation 2 of the plan: ups.beeper.status with the Legrand meaning of the field
void test_beeper_status_uses_legrand_meaning(void) {
    addKeorBeeper();
    decodeBeeper(2);
    TEST_ASSERT_EQUAL_STRING("enabled", ups_data.get("ups.beeper.status").c_str());
    decodeBeeper(3);
    TEST_ASSERT_EQUAL_STRING("disabled", ups_data.get("ups.beeper.status").c_str());
    decodeBeeper(2);
    decodeBeeper(1);
    TEST_ASSERT_EQUAL_STRING("disabled", ups_data.get("ups.beeper.status").c_str());
}

// No beeper.mute (3 would disable the beeper); enable, disable and toggle stay
void test_catalog_has_no_beeper_mute(void) {
    addKeorBeeper();
    auto cmds = CommandCatalog::build(mockHost._usages, true, driver.shutdownCommandsSupported(),
                                      driver.beeperMuteSupported());
    TEST_ASSERT_TRUE(hasCommand(cmds, "beeper.enable"));
    TEST_ASSERT_TRUE(hasCommand(cmds, "beeper.disable"));
    TEST_ASSERT_TRUE(hasCommand(cmds, "beeper.toggle"));
    TEST_ASSERT_FALSE(hasCommand(cmds, "beeper.mute"));

    // Same field, driver that allows it: beeper.mute is offered (no regression)
    auto generic = CommandCatalog::build(mockHost._usages, true, true, true);
    TEST_ASSERT_TRUE(hasCommand(generic, "beeper.mute"));
}

// legrand-hid.c: test.battery.* on UPS.Output.Test with 1 / 2 / 3
void test_battery_test_on_output_test(void) {
    addUsage("UPS.Output.Test", 0x14, 3, 0, 8);
    auto cmds = CommandCatalog::build(mockHost._usages, false, true, false);
    TEST_ASSERT_TRUE(hasCommand(cmds, "test.battery.start.quick"));
    TEST_ASSERT_TRUE(hasCommand(cmds, "test.battery.start.deep"));
    TEST_ASSERT_TRUE(hasCommand(cmds, "test.battery.stop"));

    HIDUsageDef def;
    uint32_t value = 0;
    TEST_ASSERT_TRUE(CommandCatalog::resolveWrite("test.battery.start.quick", mockHost._usages, def, value));
    TEST_ASSERT_EQUAL_STRING("UPS.Output.Test", def.path);
    TEST_ASSERT_EQUAL_UINT32(1, value);
    TEST_ASSERT_TRUE(CommandCatalog::resolveWrite("test.battery.start.deep", mockHost._usages, def, value));
    TEST_ASSERT_EQUAL_UINT32(2, value);
    TEST_ASSERT_TRUE(CommandCatalog::resolveWrite("test.battery.stop", mockHost._usages, def, value));
    TEST_ASSERT_EQUAL_UINT32(3, value);
}

// Deviation 1 of the plan: no divide_by_10 on PowerSummary.Voltage/ConfigVoltage; the
// Keor PDU declares Volts with exponent 6 (raw 137 -> 13.7 V, raw 120 -> 12 V)
void test_no_legrand_voltage_conversions(void) {
    HIDUsageDef v = makeUsage("UPS.PowerSummary.Voltage", 0x0A, 3, 0, 16);
    v.exponent = 6;
    v.unit = 0x00F0D121;
    v.logical_max = 65535;
    mockHost._usages.push_back(v);
    HIDUsageDef c = makeUsage("UPS.PowerSummary.ConfigVoltage", 0x09, 3, 0, 16);
    c.exponent = 6;
    c.unit = 0x00F0D121;
    c.logical_max = 65535;
    mockHost._usages.push_back(c);

    uint8_t volt[] = { 0x0A, 137, 0 };
    driver.decodeReport(&mockHost, 0x0A, 3, volt, sizeof(volt), ups_data);
    uint8_t nominal[] = { 0x09, 120, 0 };
    driver.decodeReport(&mockHost, 0x09, 3, nominal, sizeof(nominal), ups_data);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 13.7f, ups_data.getFloat("battery.voltage"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.0f, ups_data.getFloat("battery.voltage.nominal"));
}

// legrand-hid.c: BOOL UPS.Output.Overload -> overload_info (Keor PDU: FEATURE 0x17 bit 1)
void test_output_overload_sets_status(void) {
    addUsage("UPS.Output.Good", 0x17, 3, 0, 1);
    addUsage("UPS.Output.Overload", 0x17, 3, 1, 1);
    uint8_t on[] = { 0x17, 0x03 };
    driver.decodeReport(&mockHost, 0x17, 3, on, sizeof(on), ups_data);
    TEST_ASSERT_EQUAL_STRING("1", ups_data.get("ups.status.overload").c_str());
    uint8_t off[] = { 0x17, 0x01 };
    driver.decodeReport(&mockHost, 0x17, 3, off, sizeof(off), ups_data);
    TEST_ASSERT_EQUAL_STRING("0", ups_data.get("ups.status.overload").c_str());
}

// Rows of legrand_hid2nut that GenericDriver does not map (Keor PDU: FEATURE 0x07 byte 4
// is WarningCapacityLimit = 20)
void test_legrand_only_rows(void) {
    addUsage("UPS.PowerSummary.WarningCapacityLimit", 0x07, 3, 0, 8);
    addUsage("UPS.OutletSystem.Outlet.PercentLoad", 0x30, 3, 0, 8);
    addUsage("UPS.BatterySystem.Battery.ConfigVoltage", 0x31, 3, 0, 8);
    uint8_t warn[] = { 0x07, 20 };
    driver.decodeReport(&mockHost, 0x07, 3, warn, sizeof(warn), ups_data);
    uint8_t load[] = { 0x30, 42 };
    driver.decodeReport(&mockHost, 0x30, 3, load, sizeof(load), ups_data);
    uint8_t nominal[] = { 0x31, 12 };
    driver.decodeReport(&mockHost, 0x31, 3, nominal, sizeof(nominal), ups_data);
    TEST_ASSERT_EQUAL_STRING("20", ups_data.get("battery.charge.warning").c_str());
    TEST_ASSERT_EQUAL_STRING("42", ups_data.get("ups.load").c_str());
    TEST_ASSERT_EQUAL_STRING("12", ups_data.get("battery.voltage.nominal").c_str());
}

// legrand_format_mfr(): "Legrand" when the device has no Vendor string
void test_mfr_fallback_without_vendor_string(void) {
    mockHost._iManufacturer = 0;
    driver.loop(&mockHost, ups_data, 0);
    TEST_ASSERT_EQUAL_STRING("Legrand", ups_data.get("ups.mfr").c_str());
}

// ups.delay.* on the Outlet and Output paths of legrand_hid2nut
void test_delay_paths_follow_legrand_table(void) {
    const NutTable::DelayPaths* p = driver.delayPaths();
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT32(2, p->n_shutdown);
    TEST_ASSERT_EQUAL_STRING("UPS.OutletSystem.Outlet.DelayBeforeShutdown", p->shutdown[0]);
    TEST_ASSERT_EQUAL_STRING("UPS.Output.DelayBeforeShutdown", p->shutdown[1]);
    TEST_ASSERT_EQUAL_UINT32(2, p->n_startup);
    TEST_ASSERT_EQUAL_STRING("UPS.OutletSystem.Outlet.DelayBeforeStartup", p->startup[0]);
    TEST_ASSERT_EQUAL_STRING("UPS.Output.DelayBeforeStartup", p->startup[1]);
    TEST_ASSERT_TRUE(driver.shutdownCommandsSupported());
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_registry_selects_legrand_for_keor_pdu_and_sp);
    RUN_TEST(test_beeper_encoding_uses_legrand_values);
    RUN_TEST(test_beeper_status_uses_legrand_meaning);
    RUN_TEST(test_catalog_has_no_beeper_mute);
    RUN_TEST(test_battery_test_on_output_test);
    RUN_TEST(test_no_legrand_voltage_conversions);
    RUN_TEST(test_output_overload_sets_status);
    RUN_TEST(test_legrand_only_rows);
    RUN_TEST(test_mfr_fallback_without_vendor_string);
    RUN_TEST(test_delay_paths_follow_legrand_table);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_registry_selects_legrand_for_keor_pdu_and_sp);
    RUN_TEST(test_beeper_encoding_uses_legrand_values);
    RUN_TEST(test_beeper_status_uses_legrand_meaning);
    RUN_TEST(test_catalog_has_no_beeper_mute);
    RUN_TEST(test_battery_test_on_output_test);
    RUN_TEST(test_no_legrand_voltage_conversions);
    RUN_TEST(test_output_overload_sets_status);
    RUN_TEST(test_legrand_only_rows);
    RUN_TEST(test_mfr_fallback_without_vendor_string);
    RUN_TEST(test_delay_paths_follow_legrand_table);
    UNITY_END();
}
void loop() {}
#endif
#endif
