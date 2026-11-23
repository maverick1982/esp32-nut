#include <unity.h>
#include <string.h>
#include <memory>
#include "BelkinDriver.h"
#include "DriverRegistry.h"
#include "HidControl.h"
#include "IHidTransport.h"
#include "UPSData.h"
#include "Quirks.h"

// Belkin (050d) and Liebert / Vertiv (10af:0000-0008), after nut_repo/drivers/belkin-hid.c

class MockBelkinHost : public IHidTransport {
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

static BelkinDriver driver;
static MockBelkinHost mockHost;
static UPSData ups_data;

// HID units of the Vertiv Edge 750 descriptor: Volt and Hertz
static const uint32_t UNIT_VOLT = 0x00F0D121;
static const uint32_t UNIT_HERTZ = 0x0000F001;

static void addUsage(const char* path, uint8_t report_id, uint8_t report_type,
                     uint16_t bit_offset, uint16_t bit_size, int8_t exponent = 0, uint32_t unit = 0) {
    HIDUsageDef u;
    u.report_id = report_id;
    u.report_type = report_type;
    u.bit_offset = bit_offset;
    u.bit_size = bit_size;
    u.logical_max = bit_size >= 16 ? 65535 : (1 << bit_size) - 1;
    u.exponent = exponent;
    u.unit = unit;
    u.found = true;
    strncpy(u.path, path, sizeof(u.path) - 1);
    u.path[sizeof(u.path) - 1] = '\0';
    mockHost._usages.push_back(u);
}

template <size_t N>
static void decode(uint8_t report_type, const uint8_t (&report)[N]) {
    driver.decodeReport(&mockHost, report[0], report_type, report, N, ups_data);
    // As USBHostUPS: the NUT table writes its rows in postDecode()
    driver.postDecode(&mockHost, report[0], report_type, report, N, ups_data);
}

static std::string names(const std::vector<const UPSCommandInfo*>& cmds) {
    std::string out;
    for (const auto* c : cmds) {
        if (!out.empty()) out += " ";
        out += c->name;
    }
    return out;
}

static std::string commands() {
    size_t n = 0;
    const NutTable::HidInfo* t = driver.commandTable(n);
    return names(HidControl::buildCommands(mockHost._usages, true, driver.shutdownCommandsSupported(),
                                           driver.beeperMuteSupported(), t, n));
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

// The Vertiv Edge 750 (10af:0002) declares its line voltages in Volt with exponent 1 and its
// frequencies in Hertz with exponent 1: the Liebert PSI5 rows (belkin-hid.c:520-524) correct
// them with liebert_line_voltage_info and divide_by_100_conversion
void test_vertiv_edge_line_values(void) {
    addUsage("UPS.PowerConverter.Input.Voltage", 0x18, 3, 0, 16, 1, UNIT_VOLT);
    addUsage("UPS.PowerConverter.Input.Frequency", 0x19, 3, 0, 16, 1, UNIT_HERTZ);
    addUsage("UPS.PowerConverter.Output.Voltage", 0x1B, 3, 0, 16, 1, UNIT_VOLT);
    addUsage("UPS.PowerConverter.Output.Frequency", 0x1C, 3, 0, 16, 1, UNIT_HERTZ);
    addUsage("UPS.Flow.ConfigVoltage", 0x01, 3, 0, 8, 7, UNIT_VOLT);
    addUsage("UPS.Flow.ConfigApparentPower", 0x03, 3, 0, 16, 7, 0x0000D121);
    const uint8_t in_v[] = { 0x18, 108, 9 };  // 2412
    const uint8_t in_f[] = { 0x19, 243, 1 };  // 499
    const uint8_t out_v[] = { 0x1B, 109, 9 }; // 2413
    const uint8_t out_f[] = { 0x1C, 243, 1 };
    const uint8_t nominal[] = { 0x01, 230 };
    const uint8_t power[] = { 0x03, 0xEE, 0x02 }; // 750
    decode(3, in_v);
    decode(3, in_f);
    decode(3, out_v);
    decode(3, out_f);
    decode(3, nominal);
    decode(3, power);
    TEST_ASSERT_EQUAL_STRING("241.2", ups_data.get("input.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("49.9", ups_data.get("input.frequency").c_str());
    TEST_ASSERT_EQUAL_STRING("241.3", ups_data.get("output.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("49.9", ups_data.get("output.frequency").c_str());
    // belkin-hid.c:519, 522: UPS.Flow.ConfigVoltage is both nominal voltages
    TEST_ASSERT_EQUAL_STRING("230", ups_data.get("input.voltage.nominal").c_str());
    TEST_ASSERT_EQUAL_STRING("230", ups_data.get("output.voltage.nominal").c_str());
    // belkin-hid.c:528: the nominal apparent power is battery.capacity upstream, and the
    // genericHid2Nut() ups.power.nominal of the same field is superseded
    TEST_ASSERT_EQUAL_STRING("750", ups_data.get("battery.capacity").c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.power.nominal"));
}

// The BELKIN* usages of page 0x86 (belkin_usage_lkp): divide_by_10_conversion, the UPS type
// word and the test result
void test_belkin_vendor_rows(void) {
    addUsage("UPS.0x0086002A.0x0086001A.0x00860030", 0x20, 3, 0, 16);  // BELKINInput.BELKINVoltage
    addUsage("UPS.0x0086002A.0x0086001A.0x00860032", 0x20, 3, 16, 16); // BELKINInput.BELKINFrequency
    addUsage("UPS.0x00860029.0x0086007C", 0x21, 3, 0, 8);              // BELKINDevice.BELKINUPSType
    addUsage("UPS.0x00860027.0x00860058", 0x22, 3, 0, 8);              // BELKINControls.BELKINTest
    const uint8_t input[] = { 0x20, 0xFC, 0x08, 0xF5, 0x01 };          // 2300, 501
    const uint8_t type[] = { 0x21, 0x32 };
    const uint8_t test[] = { 0x22, 1 };
    decode(3, input);
    decode(3, type);
    decode(3, test);
    TEST_ASSERT_EQUAL_STRING("230.0", ups_data.get("input.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("50.1", ups_data.get("input.frequency").c_str());
    TEST_ASSERT_EQUAL_STRING("3", ups_data.get("ups.firmware").c_str());
    TEST_ASSERT_EQUAL_STRING("line-interactive", ups_data.get("ups.type").c_str());
    TEST_ASSERT_EQUAL_STRING("Done and passed", ups_data.get("ups.test.result").c_str());
}

// First row wins, as upstream: BELKINCharge (row 474) before PowerSummary.RemainingCapacity (510)
void test_belkin_charge_before_powersummary(void) {
    addUsage("UPS.0x00860010.0x00860039", 0x30, 3, 0, 8);
    addUsage("UPS.PowerSummary.RemainingCapacity", 0x31, 3, 0, 8);
    const uint8_t belkin[] = { 0x30, 87 };
    const uint8_t summary[] = { 0x31, 12 };
    decode(3, belkin);
    decode(3, summary);
    TEST_ASSERT_EQUAL_STRING("87", ups_data.get("battery.charge").c_str());
}

// belkin-hid.c:542-549: the bits of BELKINPowerStatus and BELKINBatteryStatus
void test_belkin_status_words(void) {
    addUsage("UPS.0x00860028.0x00860021", 0x40, 1, 0, 16); // BELKINPowerStatus
    addUsage("UPS.0x00860028.0x00860022", 0x41, 1, 0, 8);  // BELKINBatteryStatus
    const uint8_t on_mains[] = { 0x40, 0x00, 0x00 };
    const uint8_t battery_ok[] = { 0x41, 0x00 };
    decode(1, on_mains);
    decode(1, battery_ok);
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(ups_data).c_str());
    const uint8_t on_battery[] = { 0x40, 0x11, 0x00 };  // AC failure, overload
    const uint8_t battery_low[] = { 0x41, 0x84 };      // low battery, replace battery
    decode(1, on_battery);
    decode(1, battery_low);
    // belkin_online_conversion gives "!online", never "offline", and belkin_hid2nut has no
    // discharging row on these words: ups_status_set() serves no power state (usbhid-ups.c:2744)
    TEST_ASSERT_EQUAL_STRING("ALARM LB OVER RB", UPSData::computeUPSStatusString(ups_data).c_str());
}

// Liebert units: PresentStatus flags with liebert_*_info (belkin-hid.c:534-539)
void test_liebert_present_status(void) {
    addUsage("UPS.PowerSummary.PresentStatus.ACPresent", 0x32, 1, 0, 1);
    addUsage("UPS.PowerSummary.PresentStatus.Charging", 0x32, 1, 1, 1);
    addUsage("UPS.PowerSummary.PresentStatus.NeedReplacement", 0x32, 1, 2, 1);
    addUsage("UPS.PowerSummary.RemainingCapacity", 0x34, 1, 0, 8);
    const uint8_t charge[] = { 0x34, 80 }; // CHRG needs a charge below 100 without FullyCharged
    decode(1, charge);
    const uint8_t report[] = { 0x32, 0x07 };
    decode(1, report);
    TEST_ASSERT_EQUAL_STRING("ALARM OL CHRG RB", UPSData::computeUPSStatusString(ups_data).c_str());
    const uint8_t on_battery[] = { 0x32, 0x00 };
    decode(1, on_battery);
    // liebert_online_info clears online and sets nothing else: no power state, as upstream
    TEST_ASSERT_EQUAL_STRING("Unknown", UPSData::computeUPSStatusString(ups_data).c_str());
}

// belkin_format_mfr / belkin_format_model
void test_mfr_and_model_fallback(void) {
    driver.formatDeviceStrings(ups_data);
    TEST_ASSERT_EQUAL_STRING("Belkin", ups_data.get("ups.mfr").c_str());
    TEST_ASSERT_EQUAL_STRING("unknown", ups_data.get("ups.model").c_str());

    setUp();
    ups_data.set("ups.mfr", "   ");
    driver.formatDeviceStrings(ups_data);
    TEST_ASSERT_EQUAL_STRING("Belkin", ups_data.get("ups.mfr").c_str());

    setUp();
    ups_data.set("ups.mfr", "  Vertiv Co.");
    ups_data.set("ups.model", "Vertiv Edge");
    driver.formatDeviceStrings(ups_data);
    TEST_ASSERT_EQUAL_STRING("Vertiv Co.", ups_data.get("ups.mfr").c_str());
    TEST_ASSERT_EQUAL_STRING("Vertiv Edge", ups_data.get("ups.model").c_str());
}

// Commands from the HU_TYPE_CMD rows of belkin_hid2nut (555-568) and those usbhid-ups derives
void test_commands_follow_belkin_table(void) {
    addUsage("UPS.0x00860027.0x00860058", 0x22, 3, 0, 8);  // BELKINTest
    addUsage("UPS.0x00860027.0x0086005A", 0x23, 3, 0, 8);  // BELKINAudibleAlarmControl
    addUsage("UPS.0x00860027.0x00860055", 0x24, 3, 0, 16); // BELKINDelayBeforeReboot
    addUsage("UPS.0x00860027.0x00860056", 0x25, 3, 0, 16); // BELKINDelayBeforeStartup
    addUsage("UPS.0x00860027.0x00860057", 0x26, 3, 0, 16); // BELKINDelayBeforeShutdown
    TEST_ASSERT_EQUAL_STRING(
        "beeper.enable beeper.disable beeper.toggle beeper.mute test.battery.start.quick test.battery.start.deep "
        "test.battery.stop load.off load.on load.off.delay load.on.delay shutdown.return shutdown.stayoff "
        "shutdown.stop shutdown.reboot shutdown.default",
        commands().c_str());
}

// A Liebert unit has no BELKIN* control usage: no command, as usbhid-ups with belkin-hid.c
// (GenericDriver offered the beeper and test commands on the standard usages)
void test_no_command_on_standard_usages(void) {
    addUsage("UPS.BatterySystem.Battery.Test", 0x10, 3, 0, 8);
    addUsage("UPS.PowerSummary.AudibleAlarmControl", 0x11, 3, 0, 8);
    mockHost._beeperPath = "UPS.PowerSummary.AudibleAlarmControl";
    addUsage("UPS.OutletSystem.Outlet.DelayBeforeShutdown", 0x15, 3, 0, 16);
    TEST_ASSERT_EQUAL_STRING("", commands().c_str());
}

// ups.delay.* on the BELKINControls paths of belkin_hid2nut (496-497)
void test_delay_paths_follow_belkin_table(void) {
    const NutTable::DelayPaths* p = driver.delayPaths();
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT32(1, p->n_shutdown);
    TEST_ASSERT_EQUAL_STRING("UPS.0x00860027.0x00860057", p->shutdown[0]);
    TEST_ASSERT_EQUAL_UINT32(1, p->n_startup);
    TEST_ASSERT_EQUAL_STRING("UPS.0x00860027.0x00860056", p->startup[0]);
}

// The Vertiv Edge answers FEATURE report 30 with more bytes than it declares: only
// 10af:0002 gets QUIRK_MIN_REQUEST_8, the other Liebert and Belkin PIDs keep their length
static uint32_t quirksOf(uint16_t vid, uint16_t pid) {
    uint32_t q = 0;
    for (int i = 0; UPS_QUIRKS[i].vid != 0; i++) {
        if (UPS_QUIRKS[i].vid == vid && (UPS_QUIRKS[i].pid == 0xFFFF || UPS_QUIRKS[i].pid == pid)) q |= UPS_QUIRKS[i].flags;
    }
    return q;
}

void test_vertiv_edge_min_request_quirk(void) {
    TEST_ASSERT_TRUE(quirksOf(0x10AF, 0x0002) & QUIRK_MIN_REQUEST_8);
    TEST_ASSERT_FALSE(quirksOf(0x10AF, 0x0000) & QUIRK_MIN_REQUEST_8);
    TEST_ASSERT_FALSE(quirksOf(0x10AF, 0x1000) & QUIRK_MIN_REQUEST_8);
    TEST_ASSERT_FALSE(quirksOf(0x050D, 0x0980) & QUIRK_MIN_REQUEST_8);
}

#ifdef PIO_UNIT_TESTING
static void runAll() {
    RUN_TEST(test_vertiv_edge_line_values);
    RUN_TEST(test_belkin_vendor_rows);
    RUN_TEST(test_belkin_charge_before_powersummary);
    RUN_TEST(test_belkin_status_words);
    RUN_TEST(test_liebert_present_status);
    RUN_TEST(test_mfr_and_model_fallback);
    RUN_TEST(test_commands_follow_belkin_table);
    RUN_TEST(test_no_command_on_standard_usages);
    RUN_TEST(test_delay_paths_follow_belkin_table);
    RUN_TEST(test_vertiv_edge_min_request_quirk);
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
