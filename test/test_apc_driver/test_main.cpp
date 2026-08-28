#include <unity.h>
#include "APCDriver.h"
#include "IUSBHostUPS.h"

class MockAPCHost : public IUSBHostUPS {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;
    std::vector<uint8_t> _requestedStrings;
    std::vector<std::pair<uint8_t, uint8_t>> _requestedReports;

    const UPSData& getUPSData() const override { return _data; }
    String getUPSStatusString() const override { return "OL"; }
    bool setBeeper(bool) override { return true; }
    bool isConnected() const override { return true; }

    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
    String getActiveBeeperPath() const override { return "UPS.PowerSummary.AudibleAlarmControl"; }
    uint32_t getQuirks() const override { return 0; }
    bool isControlPending() const override { return false; }
    bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t) override {
        _requestedReports.push_back({report_id, report_type});
        return true;
    }
    bool requestStringDescriptor(uint8_t index) override {
        _requestedStrings.push_back(index);
        return true;
    }
};

static APCDriver driver;
static MockAPCHost mockHost;
static UPSData ups_data;

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

void test_apc_power_summary_status(void) {
    HIDUsageDef u_ac;
    u_ac.report_id = 0x01;
    u_ac.report_type = 1;
    u_ac.bit_offset = 0;
    u_ac.bit_size = 1;
    u_ac.exponent = 0;
    u_ac.unit = 0;
    u_ac.path = "UPS.PowerSummary.PresentStatus.ACPresent";
    u_ac.found = true;

    HIDUsageDef u_dischrg;
    u_dischrg.report_id = 0x01;
    u_dischrg.report_type = 1;
    u_dischrg.bit_offset = 1;
    u_dischrg.bit_size = 1;
    u_dischrg.exponent = 0;
    u_dischrg.unit = 0;
    u_dischrg.path = "UPS.PowerSummary.PresentStatus.Discharging";
    u_dischrg.found = true;

    HIDUsageDef u_need_repl;
    u_need_repl.report_id = 0x01;
    u_need_repl.report_type = 1;
    u_need_repl.bit_offset = 2;
    u_need_repl.bit_size = 1;
    u_need_repl.exponent = 0;
    u_need_repl.unit = 0;
    u_need_repl.path = "UPS.PowerSummary.PresentStatus.NeedReplacement";
    u_need_repl.found = true;

    mockHost._usages.push_back(u_ac);
    mockHost._usages.push_back(u_dischrg);
    mockHost._usages.push_back(u_need_repl);

    // AC Present = 1, Discharging = 0, NeedReplacement = 1 (bits: 1 | 0 | 4 = 0x05)
    uint8_t r1[] = { 0x01, 0x05 };
    driver.decodeReport(&mockHost, 0x01, 1, r1, sizeof(r1), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.acPresent);
    TEST_ASSERT_TRUE(ups_data.acPresent);
    TEST_ASSERT_TRUE(ups_data.has.discharging);
    TEST_ASSERT_FALSE(ups_data.discharging);
    TEST_ASSERT_TRUE(ups_data.has.needReplacement);
    TEST_ASSERT_TRUE(ups_data.needReplacement);
}

void test_apc_battery_replace_date_packed_bcd(void) {
    // APC packed BCD date format: 0x052324 -> 2024/05/23
    HIDUsageDef u_date;
    u_date.report_id = 0x07;
    u_date.report_type = 3;
    u_date.bit_offset = 0;
    u_date.bit_size = 24;
    u_date.exponent = 0;
    u_date.unit = 0;
    u_date.path = "UPS.PowerSummary.APCBattReplaceDate";
    u_date.found = true;

    mockHost._usages.push_back(u_date);

    // Date packed: 0x052324 (Little endian: 0x24, 0x23, 0x05)
    uint8_t r_date[] = { 0x07, 0x24, 0x23, 0x05 };
    driver.decodeReport(&mockHost, 0x07, 3, r_date, sizeof(r_date), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.batteryMfrDate);
    TEST_ASSERT_EQUAL_STRING("2024/05/23", ups_data.batteryMfrDate.c_str());
}

void test_apc_load_and_real_power_calculation(void) {
    HIDUsageDef u_load;
    u_load.report_id = 0x08;
    u_load.report_type = 1;
    u_load.bit_offset = 0;
    u_load.bit_size = 8;
    u_load.exponent = 0;
    u_load.unit = 0;
    u_load.path = "UPS.PowerSummary.PercentLoad";
    u_load.found = true;

    mockHost._usages.push_back(u_load);

    // ConfigActivePower = 600W
    ups_data.has.configActivePower = true;
    ups_data.configActivePower = 600;

    // Load: 50%
    uint8_t r_load[] = { 0x08, 50 };
    driver.decodeReport(&mockHost, 0x08, 1, r_load, sizeof(r_load), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.load);
    TEST_ASSERT_EQUAL_UINT8(50, ups_data.load);
    TEST_ASSERT_TRUE(ups_data.has.realPower);
    TEST_ASSERT_EQUAL_UINT16(300, ups_data.realPower); // 50% di 600W = 300W
}

void test_apc_loop_polling_and_string_requests(void) {
    mockHost._iManufacturer = 1;
    mockHost._iProduct = 2;
    mockHost._iSerialNumber = 3;

    // Step 1: Start fast poll, request string 1 (Manufacturer)
    driver.loop(&mockHost, ups_data, 1000);
    TEST_ASSERT_EQUAL_UINT32(1, mockHost._requestedStrings.size());
    TEST_ASSERT_EQUAL_UINT8(1, mockHost._requestedStrings[0]);

    // Step 2 (+60ms): Request string 2 (Product)
    driver.loop(&mockHost, ups_data, 1060);
    TEST_ASSERT_EQUAL_UINT32(2, mockHost._requestedStrings.size());
    TEST_ASSERT_EQUAL_UINT8(2, mockHost._requestedStrings[1]);

    // Step 3 (+60ms): Request string 3 (Serial Number)
    driver.loop(&mockHost, ups_data, 1120);
    TEST_ASSERT_EQUAL_UINT32(3, mockHost._requestedStrings.size());
    TEST_ASSERT_EQUAL_UINT8(3, mockHost._requestedStrings[2]);

    // Now simulate descriptor response for Manufacturer
    uint8_t desc_mfr[] = { 18, 0x03, 'A', 0, 'P', 0, 'C', 0, ' ', 0, 'b', 0, 'y', 0, ' ', 0 };
    driver.parseStringDescriptor(&mockHost, 1, desc_mfr, sizeof(desc_mfr), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.manufacturer);
    TEST_ASSERT_EQUAL_STRING("APC by ", ups_data.manufacturer.c_str());
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_apc_power_summary_status);
    RUN_TEST(test_apc_battery_replace_date_packed_bcd);
    RUN_TEST(test_apc_load_and_real_power_calculation);
    RUN_TEST(test_apc_loop_polling_and_string_requests);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_apc_power_summary_status);
    RUN_TEST(test_apc_battery_replace_date_packed_bcd);
    RUN_TEST(test_apc_load_and_real_power_calculation);
    RUN_TEST(test_apc_loop_polling_and_string_requests);
    UNITY_END();
}
void loop() {}
#endif
#endif
