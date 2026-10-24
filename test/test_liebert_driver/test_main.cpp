#include <unity.h>
#include <string.h>
#include <memory>
#include "LiebertDriver.h"
#include "DriverRegistry.h"
#include "CommandCatalog.h"
#include "IUSBHostUPS.h"

// US-097: Phoenixtec 06da:ffff, after nut_repo/drivers/liebert-hid.c

class MockLiebertHost : public IUSBHostUPS {
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

static LiebertDriver driver;
static MockLiebertHost mockHost;
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

template <size_t N>
static void decode(uint8_t report_type, const uint8_t (&report)[N]) {
    driver.decodeReport(&mockHost, report[0], report_type, report, N, ups_data);
}

static std::string names(const std::vector<const UPSCommandInfo*>& cmds) {
    std::string out;
    for (const auto* c : cmds) {
        if (!out.empty()) out += " ";
        out += c->name;
    }
    return out;
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

static std::string driverFor(uint16_t pid, const char* mfr, const char* product) {
    std::unique_ptr<IUPSDriver> d(DriverRegistry::create(0x06DA, pid, mfr, product));
    return d->getDriverName();
}

// usbhid-ups subdriver_list order on 06da:ffff: mge-hid (AEG), idowell-hid (GoldenMate),
// liebert-hid (everything else); other Phoenixtec products stay generic
void test_registry_follows_upstream_claims(void) {
    TEST_ASSERT_EQUAL_STRING("LiebertDriver", driverFor(0xFFFF, "PPC", "Offline UPS").c_str());
    TEST_ASSERT_EQUAL_STRING("LiebertDriver", driverFor(0xFFFF, "", "").c_str());
    TEST_ASSERT_EQUAL_STRING("LiebertDriver", driverFor(0xFFFF, nullptr, nullptr).c_str());
    TEST_ASSERT_EQUAL_STRING("EatonDriver", driverFor(0xFFFF, "AEG", "").c_str());
    TEST_ASSERT_EQUAL_STRING("EatonDriver", driverFor(0xFFFF, "", "AEG PROTECT NAS").c_str());
    TEST_ASSERT_EQUAL_STRING("GenericDriver", driverFor(0xFFFF, "-BMS-", "").c_str());
    TEST_ASSERT_EQUAL_STRING("GenericDriver", driverFor(0xFFFF, "", "Smart-Battery").c_str());
    TEST_ASSERT_EQUAL_STRING("GenericDriver", driverFor(0x0001, "PPC", "Offline UPS").c_str());
    // The VID/PID alone still selects the other drivers
    std::unique_ptr<IUPSDriver> eaton(DriverRegistry::create(0x0463, 0xFFFF));
    TEST_ASSERT_EQUAL_STRING("EatonDriver", eaton->getDriverName());
}

// liebert-hid.c: UPS.Flow.[4].ConfigVoltage/ConfigFrequency are the output nominal values;
// GenericDriver's input.voltage.nominal from the same field is dropped (deviation 2)
void test_flow_nominal_values_are_output_ones(void) {
    addUsage("UPS.Flow.ConfigVoltage", 0x72, 3, 0, 8);
    addUsage("UPS.Flow.ConfigFrequency", 0x36, 3, 0, 8);
    const uint8_t voltage[] = { 0x72, 230 };
    decode(3, voltage);
    const uint8_t frequency[] = { 0x36, 50 };
    decode(3, frequency);
    TEST_ASSERT_EQUAL_STRING("230", ups_data.get("output.voltage.nominal").c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("input.voltage.nominal"));
    TEST_ASSERT_EQUAL_STRING("50", ups_data.get("output.frequency.nominal").c_str());
}

void test_powersummary_capacities_are_experimental(void) {
    addUsage("UPS.PowerSummary.DesignCapacity", 0x0B, 3, 0, 8);
    addUsage("UPS.PowerSummary.FullChargeCapacity", 0x0B, 3, 8, 8);
    const uint8_t report[] = { 0x0B, 100, 95 };
    decode(3, report);
    TEST_ASSERT_EQUAL_STRING("100", ups_data.get("experimental.battery.capacity.nominal").c_str());
    TEST_ASSERT_EQUAL_STRING("95", ups_data.get("experimental.battery.capacity").c_str());
}

// "%.2f" for the frequencies, as liebert_hid2nut
void test_frequencies_use_upstream_format(void) {
    addUsage("UPS.PowerConverter.Output.Frequency", 0x42, 3, 0, 8);
    addUsage("UPS.PowerConverter.Input.Frequency", 0x31, 3, 0, 8);
    const uint8_t out[] = { 0x42, 50 };
    decode(3, out);
    const uint8_t in[] = { 0x31, 60 };
    decode(3, in);
    TEST_ASSERT_EQUAL_STRING("50.00", ups_data.get("output.frequency").c_str());
    TEST_ASSERT_EQUAL_STRING("60.00", ups_data.get("input.frequency").c_str());
}

// Output.*VoltageTransfer is output.transfer.* upstream: GenericDriver's input.transfer.*
// from it is dropped, unless the vendor usages give input.transfer.*
void test_voltage_transfer_rows(void) {
    addUsage("UPS.PowerConverter.Output.HighVoltageTransfer", 0x14, 3, 0, 16);
    const uint8_t high[] = { 0x14, 0x08, 0x01 }; // 264
    decode(3, high);
    TEST_ASSERT_EQUAL_STRING("264.0", ups_data.get("output.transfer.high").c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("input.transfer.high"));

    setUp();
    addUsage("UPS.PowerConverter.Output.HighVoltageTransfer", 0x14, 3, 0, 16);
    addUsage("UPS.PowerConverter.Output.0xFFFF0058", 0x14, 3, 16, 16);
    const uint8_t both[] = { 0x14, 0x08, 0x01, 0x0A, 0x01 }; // 264, 266
    decode(3, both);
    TEST_ASSERT_EQUAL_STRING("264.0", ups_data.get("output.transfer.high").c_str());
    TEST_ASSERT_EQUAL_STRING("266", ups_data.get("input.transfer.high").c_str());
}

// Deviation 3: off_info, commfault_info, trim_info and boost_info kept as internal keys
void test_status_flags_are_internal_keys(void) {
    addUsage("UPS.PowerSummary.PresentStatus.Good", 0x01, 1, 0, 8);
    addUsage("UPS.PowerSummary.PresentStatus.InternalFailure", 0x01, 1, 8, 8);
    addUsage("UPS.PowerConverter.Input.PresentStatus.Buck", 0x04, 1, 0, 1);
    addUsage("UPS.PowerConverter.Input.PresentStatus.Boost", 0x04, 1, 1, 1);
    const uint8_t summary[] = { 0x01, 0, 1 };
    decode(1, summary);
    const uint8_t input[] = { 0x04, 0x02 };
    decode(1, input);
    TEST_ASSERT_EQUAL_STRING("0", ups_data.get("ups.status.good").c_str());
    TEST_ASSERT_EQUAL_STRING("1", ups_data.get("ups.status.internal_failure").c_str());
    TEST_ASSERT_EQUAL_STRING("0", ups_data.get("ups.status.trim").c_str());
    TEST_ASSERT_EQUAL_STRING("1", ups_data.get("ups.status.boost").c_str());
}

// liebert-hid.c: battery.type from the iDeviceChemistry string (stringid_conversion)
void test_battery_type_from_chemistry_string(void) {
    addUsage("UPS.PowerSummary.iDeviceChemistry", 0x10, 3, 0, 8);
    const uint8_t report[] = { 0x10, 5 };
    decode(3, report);
    const uint8_t str[] = { 10, 0x03, 'P', 0, 'b', 0, 'A', 0, 'c', 0 };
    driver.parseStringDescriptor(&mockHost, 5, str, sizeof(str), ups_data);
    TEST_ASSERT_EQUAL_STRING("PbAc", ups_data.get("battery.type").c_str());
}

// liebert_format_mfr(): the Vendor string, or "Liebert" when there is none or it is empty
void test_mfr_fallback(void) {
    driver.loop(&mockHost, ups_data, 0);
    TEST_ASSERT_EQUAL_STRING("Liebert", ups_data.get("ups.mfr").c_str());

    setUp();
    mockHost._iManufacturer = 1;
    const uint8_t empty[] = { 2, 0x03 };
    driver.parseStringDescriptor(&mockHost, 1, empty, sizeof(empty), ups_data);
    TEST_ASSERT_EQUAL_STRING("Liebert", ups_data.get("ups.mfr").c_str());

    setUp();
    mockHost._iManufacturer = 1;
    const uint8_t ppc[] = { 8, 0x03, 'P', 0, 'P', 0, 'C', 0 };
    driver.parseStringDescriptor(&mockHost, 1, ppc, sizeof(ppc), ups_data);
    TEST_ASSERT_EQUAL_STRING("PPC", ups_data.get("ups.mfr").c_str());
}

// Commands of liebert_hid2nut and those usbhid-ups derives, plus shutdown.default (US-058)
void test_commands_follow_liebert_table(void) {
    addUsage("UPS.PowerSummary.AudibleAlarmControl", 0x13, 3, 0, 8);
    mockHost._beeperPath = "UPS.PowerSummary.AudibleAlarmControl";
    addUsage("UPS.BatterySystem.Battery.Test", 0x24, 3, 0, 8);
    addUsage("UPS.PowerSummary.DelayBeforeShutdown", 0x09, 3, 0, 32);
    addUsage("UPS.PowerSummary.DelayBeforeStartup", 0x0A, 3, 0, 32);
    auto cmds = CommandCatalog::build(mockHost._usages, true, driver.shutdownCommandsSupported(),
                                      driver.beeperMuteSupported(), driver.commandAllowList());
    TEST_ASSERT_EQUAL_STRING(
        "beeper.toggle test.battery.start load.off load.on load.off.delay load.on.delay "
        "shutdown.return shutdown.stayoff shutdown.stop shutdown.default",
        names(cmds).c_str());
}

// ups.delay.* on the PowerSummary paths of liebert_hid2nut
void test_delay_paths_follow_liebert_table(void) {
    const NutTable::DelayPaths* p = driver.delayPaths();
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT32(1, p->n_shutdown);
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.DelayBeforeShutdown", p->shutdown[0]);
    TEST_ASSERT_EQUAL_UINT32(1, p->n_startup);
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.DelayBeforeStartup", p->startup[0]);
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_registry_follows_upstream_claims);
    RUN_TEST(test_flow_nominal_values_are_output_ones);
    RUN_TEST(test_powersummary_capacities_are_experimental);
    RUN_TEST(test_frequencies_use_upstream_format);
    RUN_TEST(test_voltage_transfer_rows);
    RUN_TEST(test_status_flags_are_internal_keys);
    RUN_TEST(test_battery_type_from_chemistry_string);
    RUN_TEST(test_mfr_fallback);
    RUN_TEST(test_commands_follow_liebert_table);
    RUN_TEST(test_delay_paths_follow_liebert_table);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_registry_follows_upstream_claims);
    RUN_TEST(test_flow_nominal_values_are_output_ones);
    RUN_TEST(test_powersummary_capacities_are_experimental);
    RUN_TEST(test_frequencies_use_upstream_format);
    RUN_TEST(test_voltage_transfer_rows);
    RUN_TEST(test_status_flags_are_internal_keys);
    RUN_TEST(test_battery_type_from_chemistry_string);
    RUN_TEST(test_mfr_fallback);
    RUN_TEST(test_commands_follow_liebert_table);
    RUN_TEST(test_delay_paths_follow_liebert_table);
    UNITY_END();
}
void loop() {}
#endif
#endif
