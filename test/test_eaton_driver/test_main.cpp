#include <unity.h>
#include "EatonDriver.h"
#include "IUSBHostUPS.h"

class MockEatonHost : public IUSBHostUPS {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;
    std::vector<uint8_t> _requestedStrings;

    const UPSData& getUPSData() const override { return _data; }
    String getUPSStatusString() const override { return "OL"; }
    bool setBeeper(bool) override { return true; }
    bool isConnected() const override { return true; }

    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
    String getActiveBeeperPath() const override { return "UPS.PowerSummary.AudibleAlarmControl"; }
    uint32_t getQuirks() const override { return 0; }
    bool isControlPending() const override { return false; }
    bool requestReport(uint8_t, uint8_t, uint16_t) override { return true; }
    bool requestStringDescriptor(uint8_t index) override {
        _requestedStrings.push_back(index);
        return true;
    }
};

static EatonDriver driver;
static MockEatonHost mockHost;
static UPSData ups_data;

void setUp(void) {
    ups_data = UPSData();
    mockHost._usages.clear();
    mockHost._requestedStrings.clear();
    mockHost._iManufacturer = 0;
    mockHost._iProduct = 0;
    mockHost._iSerialNumber = 0;
    driver.setup();
}

void tearDown(void) {}

void test_eaton_ac_present_and_discharging(void) {
    HIDUsageDef u_ac;
    u_ac.report_id = 0x01;
    u_ac.report_type = 1;
    u_ac.bit_offset = 0;
    u_ac.bit_size = 1;
    u_ac.path = "UPS.PowerSummary.PresentStatus.ACPresent";
    u_ac.found = true;

    HIDUsageDef u_dischrg;
    u_dischrg.report_id = 0x01;
    u_dischrg.report_type = 1;
    u_dischrg.bit_offset = 4;
    u_dischrg.bit_size = 1;
    u_dischrg.path = "UPS.PowerSummary.PresentStatus.Discharging";
    u_dischrg.found = true;

    mockHost._usages.push_back(u_ac);
    mockHost._usages.push_back(u_dischrg);

    // 1. Report con AC Present = 1, Discharging = 0
    uint8_t r1[] = { 0x01, 0x01 };
    driver.decodeReport(&mockHost, 0x01, 1, r1, sizeof(r1), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.acPresent);
    TEST_ASSERT_TRUE(ups_data.acPresent);
    TEST_ASSERT_TRUE(ups_data.has.discharging);
    TEST_ASSERT_FALSE(ups_data.discharging);

    // 2. Report con AC Present = 0, Discharging = 1
    uint8_t r2[] = { 0x01, 0x10 };
    driver.decodeReport(&mockHost, 0x01, 1, r2, sizeof(r2), ups_data);
    TEST_ASSERT_FALSE(ups_data.acPresent);
    TEST_ASSERT_TRUE(ups_data.discharging);
}

void test_eaton_voltage_and_battery(void) {
    HIDUsageDef u_cap;
    u_cap.report_id = 0x06;
    u_cap.report_type = 1;
    u_cap.bit_offset = 0;
    u_cap.bit_size = 8;
    u_cap.exponent = 0;
    u_cap.unit = 0;
    u_cap.path = "UPS.PowerSummary.RemainingCapacity";
    u_cap.found = true;

    HIDUsageDef u_runtime;
    u_runtime.report_id = 0x06;
    u_runtime.report_type = 1;
    u_runtime.bit_offset = 8;
    u_runtime.bit_size = 32;
    u_runtime.exponent = 0;
    u_runtime.unit = 0;
    u_runtime.path = "UPS.PowerSummary.RunTimeToEmpty";
    u_runtime.found = true;

    mockHost._usages.push_back(u_cap);
    mockHost._usages.push_back(u_runtime);

    // Report ID 0x06: capacity 85% (0x55), runtime 1200 sec (0x000004B0)
    uint8_t r6[] = { 0x06, 0x55, 0xB0, 0x04, 0x00, 0x00 };
    driver.decodeReport(&mockHost, 0x06, 1, r6, sizeof(r6), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.remainingCapacity);
    TEST_ASSERT_EQUAL_UINT8(85, ups_data.remainingCapacity);
    TEST_ASSERT_TRUE(ups_data.has.runTimeToEmpty);
    TEST_ASSERT_EQUAL_UINT32(1200, ups_data.runTimeToEmpty);
}

void test_eaton_string_descriptors(void) {
    mockHost._iManufacturer = 1;
    mockHost._iProduct = 2;
    mockHost._iSerialNumber = 3;

    // String descriptor Eaton (UTF-16LE: "Eaton")
    uint8_t desc_mfr[] = { 12, 0x03, 'E', 0, 'a', 0, 't', 0, 'o', 0, 'n', 0 };
    driver.parseStringDescriptor(&mockHost, 1, desc_mfr, sizeof(desc_mfr), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.manufacturer);
    TEST_ASSERT_EQUAL_STRING("Eaton", ups_data.manufacturer.c_str());

    // Product "3S 700"
    uint8_t desc_prod[] = { 14, 0x03, '3', 0, 'S', 0, ' ', 0, '7', 0, '0', 0, '0', 0 };
    driver.parseStringDescriptor(&mockHost, 2, desc_prod, sizeof(desc_prod), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.product);
    TEST_ASSERT_EQUAL_STRING("3S 700", ups_data.product.c_str());
}

void test_eaton_loop_polling_and_string_requests(void) {
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
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_eaton_ac_present_and_discharging);
    RUN_TEST(test_eaton_voltage_and_battery);
    RUN_TEST(test_eaton_string_descriptors);
    RUN_TEST(test_eaton_loop_polling_and_string_requests);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_eaton_ac_present_and_discharging);
    RUN_TEST(test_eaton_voltage_and_battery);
    RUN_TEST(test_eaton_string_descriptors);
    RUN_TEST(test_eaton_loop_polling_and_string_requests);
    UNITY_END();
}
void loop() {}
#endif
#endif
