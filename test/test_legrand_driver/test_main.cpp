#include <unity.h>
#include <string.h>
#include <memory>
#include <vector>
#include "LegrandDriver.h"
#include "DriverRegistry.h"
#include "CommandCatalog.h"
#include "IHidTransport.h"
#include "UPSData.h"

// US-087: Legrand Keor PDU / Keor SP, after nut_repo/drivers/legrand-hid.c

class MockLegrandHost : public IHidTransport {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;
    String _beeperPath;

    void lock() const override {}
    void unlock() const override {}

    HIDParser _hid_parser;
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
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

// As USBHostUPS: decodeReport() and then postDecode(), where the NUT table writes its rows
// (US-106: every Legrand value but the beeper status comes from legrandHid2Nut())
static void decode(uint8_t type, const std::vector<uint8_t>& bytes) {
    driver.decodeReport(&mockHost, bytes[0], type, bytes.data(), bytes.size(), ups_data);
    driver.postDecode(&mockHost, bytes[0], type, bytes.data(), bytes.size(), ups_data);
}

// A Volts usage of the Keor PDU 800: unit exponent 6 (issue #90)
static void addVolts(const char* path, uint8_t report_id) {
    HIDUsageDef v = makeUsage(path, report_id, 3, 0, 16);
    v.exponent = 6;
    v.unit = 0x00F0D121;
    v.logical_max = 65535;
    mockHost._usages.push_back(v);
}

// Deviation 1 (US-087): no divide_by_10 on PowerSummary.Voltage/ConfigVoltage (legrand-hid.c:139,
// 141); the Keor PDU declares Volts with exponent 6 (raw 137 -> 13.7 V, raw 120 -> 12 V). The rows
// keep the "%0.1f" of the excluded function (US-106, user decision of 2026-10-30)
void test_no_legrand_voltage_conversions(void) {
    addVolts("UPS.PowerSummary.Voltage", 0x0A);
    addVolts("UPS.PowerSummary.ConfigVoltage", 0x09);

    decode(3, { 0x0A, 137, 0 });
    decode(3, { 0x09, 120, 0 });

    TEST_ASSERT_EQUAL_STRING("13.7", ups_data.get("battery.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("12.0", ups_data.get("battery.voltage.nominal").c_str());
}

// legrand-hid.c:129 and 149: UPS.Input.Voltage and UPS.Output.Voltage with "%.0f" (no
// conversion upstream): 230.2 V is published as 230, as usbhid-ups does
void test_input_output_voltage_upstream_format(void) {
    addVolts("UPS.Input.Voltage", 0x0F);
    addVolts("UPS.Output.Voltage", 0x12);

    decode(3, { 0x0F, 0xFE, 0x08 }); // 2302 -> 230.2 V
    decode(3, { 0x12, 0xFE, 0x08 });

    TEST_ASSERT_EQUAL_STRING("230", ups_data.get("input.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("230", ups_data.get("output.voltage").c_str());
}

// legrand-hid.c:144 writes UPS.PowerSummary.RuntimeToEmpty: usage names ignore case, as in
// string_to_path(), so the RunTimeToEmpty usage of the Keor PDU binds
void test_runtime_from_upstream_row(void) {
    addUsage("UPS.PowerSummary.RunTimeToEmpty", 0x08, 3, 0, 16);

    decode(3, { 0x08, 0x70, 0x08 }); // 2160 s

    TEST_ASSERT_EQUAL_STRING("2160", ups_data.get("battery.runtime").c_str());
}

// legrand-hid.c:162: BOOL UPS.Output.Overload -> overload_info (Keor PDU: FEATURE 0x17 bit 1)
void test_output_overload_sets_status(void) {
    addUsage("UPS.Output.Good", 0x17, 3, 0, 1);
    addUsage("UPS.Output.Overload", 0x17, 3, 1, 1);
    addUsage("UPS.PowerSummary.PresentStatus.ACPresent", 0x0B, 3, 0, 1);
    decode(3, { 0x0B, 0x01 });
    decode(3, { 0x17, 0x03 });
    TEST_ASSERT_EQUAL_STRING("1", ups_data.get("ups.status.overload").c_str());
    TEST_ASSERT_EQUAL_STRING("OL OVER", UPSData::computeUPSStatusString(ups_data).c_str());
    decode(3, { 0x17, 0x01 });
    TEST_ASSERT_EQUAL_STRING("0", ups_data.get("ups.status.overload").c_str());
}

// Rows of legrand_hid2nut that GenericDriver does not map (Keor PDU: FEATURE 0x07 byte 4
// is WarningCapacityLimit = 20)
void test_legrand_only_rows(void) {
    addUsage("UPS.PowerSummary.WarningCapacityLimit", 0x07, 3, 0, 8);
    addUsage("UPS.OutletSystem.Outlet.PercentLoad", 0x30, 3, 0, 8);
    addUsage("UPS.BatterySystem.Battery.ConfigVoltage", 0x31, 3, 0, 8);
    decode(3, { 0x07, 20 });
    decode(3, { 0x30, 42 });
    decode(3, { 0x31, 12 });
    TEST_ASSERT_EQUAL_STRING("20", ups_data.get("battery.charge.warning").c_str());
    TEST_ASSERT_EQUAL_STRING("42", ups_data.get("ups.load").c_str());
    TEST_ASSERT_EQUAL_STRING("12", ups_data.get("battery.voltage.nominal").c_str());
}

// legrand_format_mfr(): "Legrand" when the device has no Vendor string, or an empty one
void test_mfr_fallback_without_vendor_string(void) {
    driver.formatDeviceStrings(ups_data);
    TEST_ASSERT_EQUAL_STRING("Legrand", ups_data.get("ups.mfr").c_str());
    ups_data.set("ups.mfr", "LEGRAND");
    driver.formatDeviceStrings(ups_data);
    TEST_ASSERT_EQUAL_STRING("LEGRAND", ups_data.get("ups.mfr").c_str());
}

// Commands from the HU_TYPE_CMD rows of legrand_hid2nut on the Keor PDU usages: beeper.enable /
// disable (183-184) write 2 and 3, test.battery.* on UPS.Output.Test (178-180), load.*.delay on
// UPS.Output.DelayBefore* (171-172) and what usbhid-ups derives from them. No shutdown.stop:
// legrand-hid has no row for it (US-106)
void test_commands_from_legrand_table(void) {
    addKeorBeeper();
    addUsage("UPS.Output.Test", 0x14, 3, 0, 8);
    addUsage("UPS.Output.DelayBeforeShutdown", 0x15, 3, 0, 16);
    addUsage("UPS.Output.DelayBeforeStartup", 0x16, 3, 0, 16);
    size_t n = 0;
    const NutTable::HidInfo* t = driver.commandTable(n);
    auto cmds = CommandCatalog::build(mockHost._usages, true, driver.shutdownCommandsSupported(),
                                      driver.beeperMuteSupported(), nullptr, t, n);
    for (const char* name : { "beeper.enable", "beeper.disable", "beeper.toggle", "test.battery.start.quick",
                              "test.battery.start.deep", "test.battery.stop", "load.off", "load.on",
                              "load.off.delay", "load.on.delay", "shutdown.return", "shutdown.stayoff",
                              "shutdown.default" }) {
        TEST_ASSERT_TRUE_MESSAGE(hasCommand(cmds, name), name);
    }
    TEST_ASSERT_FALSE(hasCommand(cmds, "shutdown.stop"));
    TEST_ASSERT_FALSE(hasCommand(cmds, "beeper.mute"));

    HIDUsageDef def;
    uint32_t value = 0;
    TEST_ASSERT_TRUE(CommandCatalog::resolveWrite("test.battery.start.deep", mockHost._usages, def, value, t, n));
    TEST_ASSERT_EQUAL_STRING("UPS.Output.Test", def.path);
    TEST_ASSERT_EQUAL_UINT32(2, value);
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
static void runAll() {
    RUN_TEST(test_registry_selects_legrand_for_keor_pdu_and_sp);
    RUN_TEST(test_beeper_encoding_uses_legrand_values);
    RUN_TEST(test_beeper_status_uses_legrand_meaning);
    RUN_TEST(test_catalog_has_no_beeper_mute);
    RUN_TEST(test_battery_test_on_output_test);
    RUN_TEST(test_no_legrand_voltage_conversions);
    RUN_TEST(test_input_output_voltage_upstream_format);
    RUN_TEST(test_runtime_from_upstream_row);
    RUN_TEST(test_output_overload_sets_status);
    RUN_TEST(test_legrand_only_rows);
    RUN_TEST(test_mfr_fallback_without_vendor_string);
    RUN_TEST(test_commands_from_legrand_table);
    RUN_TEST(test_delay_paths_follow_legrand_table);
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
