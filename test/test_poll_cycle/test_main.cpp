#include <unity.h>
#include <string.h>
#include "GenericDriver.h"
#include "CyberPowerDriver.h"
#include "EatonDriver.h"
#include "IUSBHostUPS.h"
#include "Quirks.h"

// Review S3 / A6 / M4: one poll state machine in GenericDriver, policies in the drivers

class MockPollHost : public IUSBHostUPS {
public:
    std::vector<HIDUsageDef> _usages;
    std::vector<std::pair<uint8_t, uint8_t>> _reports; // (id, type)
    std::vector<uint8_t> _strings;
    uint32_t _quirks = 0;
    // Optional answers to GET_REPORT, decoded by _driver into *_data
    IUPSDriver* _driver = nullptr;
    UPSData* _data = nullptr;
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> _answers;
    bool _paused = false;
    HIDParser _hid_parser;

    void lock() const override {}
    void unlock() const override {}
    UPSDataLock getUPSData() const override { static UPSData d; return UPSDataLock(d, this); }
    String getUPSStatusString() const override { return "OL"; }
    bool setBeeper(bool) override { return true; }
    bool isConnected() const override { return true; }
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
    String getActiveBeeperPath() const override { return ""; }
    uint32_t getQuirks() const override { return _quirks; }
    bool isPollingPaused() const override { return _paused; }
    bool requestReport(uint8_t id, uint8_t type, uint16_t) override {
        _reports.push_back({id, type});
        for (auto& a : _answers) {
            if (a.first == id && _driver && _data) {
                _driver->decodeReport(this, id, type, a.second.data(), a.second.size(), *_data);
            }
        }
        return true;
    }
    bool requestStringDescriptor(uint8_t index) override {
        _strings.push_back(index);
        return true;
    }

    void addUsage(uint32_t usage, uint8_t id, uint8_t type, const char* path) {
        HIDUsageDef u;
        u.usage = usage;
        u.report_id = id;
        u.report_type = type;
        u.bit_size = 8;
        u.found = true;
        strncpy(u.path, path, sizeof(u.path) - 1);
        _usages.push_back(u);
    }
};

static MockPollHost host;
static UPSData data;

// Report 1: status (FEATURE). Report 2: nominal values (FEATURE). Report 3: INPUT only.
// Report 1 also exists as INPUT (same values as the FEATURE report).
static void standardDevice() {
    host.addUsage(0x008500D0, 1, 3, "UPS.PowerSummary.PresentStatus.ACPresent");
    host.addUsage(0x00850066, 1, 3, "UPS.PowerSummary.RemainingCapacity");
    host.addUsage(0x008500D0, 1, 1, "UPS.PowerSummary.PresentStatus.ACPresent");
    host.addUsage(0x00840040, 2, 3, "UPS.Flow.ConfigVoltage");
    host.addUsage(0x00840030, 3, 1, "UPS.Input.Voltage");
}

// Runs loop() every 60 ms from t0 to t1
static void runLoop(IUPSDriver& drv, uint32_t t0, uint32_t t1) {
    for (uint32_t t = t0; t <= t1; t += 60) drv.loop(&host, data, t);
}

void setUp(void) {
    host = MockPollHost();
    data = UPSData();
}

void tearDown(void) {}

void test_first_cycle_is_full(void) {
    standardDevice();
    GenericDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1500);
    // Every report once, INPUT 1 skipped for its FEATURE twin, INPUT 3 kept
    TEST_ASSERT_EQUAL(3, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(1, host._reports[0].first);
    TEST_ASSERT_EQUAL_UINT8(3, host._reports[0].second);
    TEST_ASSERT_EQUAL_UINT8(2, host._reports[1].first);
    TEST_ASSERT_EQUAL_UINT8(3, host._reports[2].first);
    TEST_ASSERT_EQUAL_UINT8(1, host._reports[2].second);
}

void test_quick_poll_reads_status_reports_only(void) {
    standardDevice();
    GenericDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1500);
    host._reports.clear();

    // 2 s later: quick poll, only report 1
    runLoop(drv, 3000, 3500);
    TEST_ASSERT_EQUAL(1, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(1, host._reports[0].first);

    // 30 s after the first cycle: full poll again
    host._reports.clear();
    runLoop(drv, 31000, 31500);
    TEST_ASSERT_EQUAL(3, host._reports.size());
}

void test_requests_are_spaced(void) {
    standardDevice();
    GenericDriver drv;
    drv.setup();
    drv.loop(&host, data, 1000); // first request right away
    drv.loop(&host, data, 1010);
    drv.loop(&host, data, 1049);
    TEST_ASSERT_EQUAL(1, host._reports.size());
    drv.loop(&host, data, 1050);
    TEST_ASSERT_EQUAL(2, host._reports.size());
}

void test_paused_polling_resumes_where_it_stopped(void) {
    standardDevice();
    GenericDriver drv;
    drv.setup();
    drv.loop(&host, data, 1000);
    host._paused = true;
    runLoop(drv, 1060, 5000);
    TEST_ASSERT_EQUAL(1, host._reports.size());
    host._paused = false;
    runLoop(drv, 5060, 5120); // the rest of the paused cycle
    TEST_ASSERT_EQUAL(3, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(2, host._reports[1].first);
    // Then the quick poll that fell due meanwhile
    runLoop(drv, 5180, 5180);
    TEST_ASSERT_EQUAL(4, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(1, host._reports[3].first);
}

void test_missing_strings_first_in_full_cycle(void) {
    standardDevice();
    host._iManufacturer = 1;
    host._iProduct = 2;
    data.set("ups.model", "known");
    GenericDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1500);
    TEST_ASSERT_EQUAL(1, host._strings.size()); // model already known
    TEST_ASSERT_EQUAL_UINT8(1, host._strings[0]);
    TEST_ASSERT_EQUAL(3, host._reports.size());
}

void test_no_get_report_quirk_skips_reports(void) {
    standardDevice();
    host._quirks = QUIRK_NO_GET_REPORT;
    GenericDriver drv;
    drv.setup();
    runLoop(drv, 1000, 40000);
    TEST_ASSERT_EQUAL(0, host._reports.size());
}

void test_report_id_zero_polled(void) {
    // Review M4: a device without report IDs was never polled
    host.addUsage(0x00850066, 0, 3, "UPS.PowerSummary.RemainingCapacity");
    GenericDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1200);
    TEST_ASSERT_EQUAL(1, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(0, host._reports[0].first);
}

void test_cyberpower_policy(void) {
    standardDevice();
    host.addUsage(0x00840040, 4, 3, "UPS.Flow.ConfigFrequency");  // hangs some firmwares
    host.addUsage(0x00840040, 130, 3, "UPS.Vendor");              // vendor defined
    CyberPowerDriver drv;
    drv.setup();
    runLoop(drv, 1000, 2000);
    // FEATURE 1 and 2 only: no INPUT on EP0, no report 4 or >= 130
    TEST_ASSERT_EQUAL(2, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(1, host._reports[0].first);
    TEST_ASSERT_EQUAL_UINT8(2, host._reports[1].first);

    // No quick poll: nothing until the next full poll 30 s later
    host._reports.clear();
    runLoop(drv, 2060, 30900);
    TEST_ASSERT_EQUAL(0, host._reports.size());
    runLoop(drv, 31000, 31500);
    TEST_ASSERT_EQUAL(2, host._reports.size());
}

void test_eaton_skips_reports_254_255(void) {
    standardDevice();
    host.addUsage(0x00840040, 254, 3, "UPS.X");
    host.addUsage(0x00840040, 255, 3, "UPS.Y");
    EatonDriver drv;
    drv.setup();
    runLoop(drv, 1000, 2000);
    TEST_ASSERT_EQUAL(3, host._reports.size());
    for (const auto& r : host._reports) {
        TEST_ASSERT_TRUE(r.first != 254 && r.first != 255);
    }
}

void test_string_index_found_in_reports_requested_same_cycle(void) {
    // Eaton: the battery chemistry string index is a FEATURE value (iDeviceChemistry).
    // It used to be requested only at the next full poll, 30 s later.
    standardDevice();
    host.addUsage(0x00850089, 2, 3, "UPS.PowerSummary.iDeviceChemistry");
    host._usages.back().bit_offset = 8; // after ConfigVoltage in report 2
    EatonDriver drv;
    drv.setup();
    host._driver = &drv;
    host._data = &data;
    host._answers.push_back({2, {0x02, 0xE6, 0x05}}); // report 2: ConfigVoltage 230, chemistry index 5

    runLoop(drv, 1000, 1500);
    TEST_ASSERT_EQUAL(3, host._reports.size());
    TEST_ASSERT_EQUAL(1, host._strings.size());
    TEST_ASSERT_EQUAL_UINT8(5, host._strings[0]);

    // Not asked twice in the same cycle, and the quick poll asks no strings
    runLoop(drv, 1560, 3500);
    TEST_ASSERT_EQUAL(1, host._strings.size());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_first_cycle_is_full);
    RUN_TEST(test_quick_poll_reads_status_reports_only);
    RUN_TEST(test_requests_are_spaced);
    RUN_TEST(test_paused_polling_resumes_where_it_stopped);
    RUN_TEST(test_missing_strings_first_in_full_cycle);
    RUN_TEST(test_no_get_report_quirk_skips_reports);
    RUN_TEST(test_report_id_zero_polled);
    RUN_TEST(test_cyberpower_policy);
    RUN_TEST(test_eaton_skips_reports_254_255);
    RUN_TEST(test_string_index_found_in_reports_requested_same_cycle);
    return UNITY_END();
}
