#include <unity.h>
#include "CyberPowerDriver.h"
#include "IUSBHostUPS.h"
#include "Quirks.h"

class MockCyberPowerHost : public IUSBHostUPS {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;
    uint32_t _quirks = 0;
    std::vector<uint8_t> _requestedStrings;

    void lock() const override {}
    void unlock() const override {}
    UPSDataLock getUPSData() const override { return UPSDataLock(_data, this); }
    String getUPSStatusString() const override { return "OL"; }
    bool setBeeper(bool) override { return true; }
    bool isConnected() const override { return true; }

    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
    String getActiveBeeperPath() const override { return "UPS.PowerSummary.AudibleAlarmControl"; }
    uint32_t getQuirks() const override { return _quirks; }
    bool isControlPending() const override { return false; }
    bool requestReport(uint8_t, uint8_t, uint16_t) override { return true; }
    bool requestStringDescriptor(uint8_t index) override {
        _requestedStrings.push_back(index);
        return true;
    }
};

static CyberPowerDriver driver;
static MockCyberPowerHost mockHost;
static UPSData ups_data;

void setUp(void) {
    ups_data = UPSData();
    mockHost._usages.clear();
    mockHost._requestedStrings.clear();
    mockHost._quirks = 0;
    mockHost._iManufacturer = 0;
    mockHost._iProduct = 0;
    mockHost._iSerialNumber = 0;
    driver.setup();
}

void tearDown(void) {}

void test_cyberpower_status_and_voltage(void) {
    HIDUsageDef u_ac;
    u_ac.report_id = 0x01;
    u_ac.report_type = 1;
    u_ac.bit_offset = 0;
    u_ac.bit_size = 1;
    u_ac.exponent = 0;
    u_ac.unit = 0;
    u_ac.path = "UPS.PowerSummary.PresentStatus.ACPresent";
    u_ac.found = true;

    HIDUsageDef u_volt;
    u_volt.report_id = 0x02;
    u_volt.report_type = 1;
    u_volt.bit_offset = 0;
    u_volt.bit_size = 16;
    u_volt.exponent = -1; // Scale factor 0.1V (2305 -> 230.5V)
    u_volt.unit = 0;
    u_volt.path = "UPS.Output.Voltage";
    u_volt.found = true;

    mockHost._usages.push_back(u_ac);
    mockHost._usages.push_back(u_volt);

    uint8_t r1[] = { 0x01, 0x01 };
    driver.decodeReport(&mockHost, 0x01, 1, r1, sizeof(r1), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.acPresent);
    TEST_ASSERT_TRUE(ups_data.acPresent);

    // 2305 = 0x0901 -> 0x01, 0x09
    uint8_t r2[] = { 0x02, 0x01, 0x09 };
    driver.decodeReport(&mockHost, 0x02, 1, r2, sizeof(r2), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.outputVoltage);
    TEST_ASSERT_FLOAT_WITHIN(0.05, 230.5, ups_data.outputVoltage);
}

void test_cyberpower_inverted_string_quirk(void) {
    mockHost._iManufacturer = 1;
    mockHost._iProduct = 2;
    mockHost._quirks = QUIRK_INVERT_STRINGS;

    // Normal UTF-16LE inverted bitwise: ~'C' = ~0x43 = 0xBC, high byte ~0x00 = 0xFF
    // Manufacturer inverted: "CPS"
    uint8_t desc_inv_mfr[] = { 8, 0x03, (uint8_t)~'C', 0xFF, (uint8_t)~'P', 0xFF, (uint8_t)~'S', 0xFF };
    driver.parseStringDescriptor(&mockHost, 1, desc_inv_mfr, sizeof(desc_inv_mfr), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.manufacturer);
    TEST_ASSERT_EQUAL_STRING("CPS", ups_data.manufacturer.c_str());

    // Auto-detect inverted string (even if quirk flag wasn't set, high byte == 0xFF)
    mockHost._quirks = 0; // reset quirk
    uint8_t desc_inv_prod[] = { 10, 0x03, (uint8_t)~'1', 0xFF, (uint8_t)~'5', 0xFF, (uint8_t)~'0', 0xFF, (uint8_t)~'0', 0xFF };
    driver.parseStringDescriptor(&mockHost, 2, desc_inv_prod, sizeof(desc_inv_prod), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.product);
    TEST_ASSERT_EQUAL_STRING("1500", ups_data.product.c_str());
}

void test_cyberpower_loop_polling_and_string_requests(void) {
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

void test_cyberpower_load_and_zero_load_reset(void) {
    // ConfigActivePower on Report 0x18 (16 bit, 810W)
    HIDUsageDef u_cap;
    u_cap.report_id = 0x18;
    u_cap.report_type = 3; // Feature
    u_cap.bit_offset = 0;
    u_cap.bit_size = 16;
    u_cap.exponent = 0;
    u_cap.unit = 0;
    u_cap.path = "UPS.Output.ConfigActivePower";
    u_cap.found = true;

    // PercentLoad on Report 0x13 (8 bit)
    HIDUsageDef u_load;
    u_load.report_id = 0x13;
    u_load.report_type = 3; // Feature
    u_load.bit_offset = 0;
    u_load.bit_size = 8;
    u_load.exponent = 0;
    u_load.unit = 0;
    u_load.path = "UPS.Output.PercentLoad";
    u_load.found = true;

    mockHost._usages.push_back(u_cap);
    mockHost._usages.push_back(u_load);

    // 1. Decode ConfigActivePower = 810 W (0x032A -> 0x2A, 0x03)
    uint8_t r_cap[] = { 0x18, 0x2A, 0x03 };
    driver.decodeReport(&mockHost, 0x18, 3, r_cap, sizeof(r_cap), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.configActivePower);
    TEST_ASSERT_EQUAL_UINT16(810, ups_data.configActivePower);

    // 2. Decode Load = 11%
    uint8_t r_load11[] = { 0x13, 0x0B };
    driver.decodeReport(&mockHost, 0x13, 3, r_load11, sizeof(r_load11), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.load);
    TEST_ASSERT_EQUAL_UINT8(11, ups_data.load);
    TEST_ASSERT_TRUE(ups_data.has.realPower);
    TEST_ASSERT_EQUAL_UINT16((810 * 11) / 100, ups_data.realPower); // 89 W

    // 3. Decode Load = 0% (zero payload [0x13, 0x00])
    uint8_t r_load0[] = { 0x13, 0x00 };
    driver.decodeReport(&mockHost, 0x13, 3, r_load0, sizeof(r_load0), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.load);
    TEST_ASSERT_EQUAL_UINT8(0, ups_data.load);
    TEST_ASSERT_TRUE(ups_data.has.realPower);
    TEST_ASSERT_EQUAL_UINT16(0, ups_data.realPower);
}

void test_cyberpower_realpower_recalculated_when_config_arrives_after_load(void) {
    // 1. Setup usages
    HIDUsageDef u_load;
    u_load.report_id = 0x13;
    u_load.report_type = 3;
    u_load.bit_offset = 0;
    u_load.bit_size = 8;
    u_load.path = "UPS.Output.PercentLoad";
    u_load.found = true;

    HIDUsageDef u_cap;
    u_cap.report_id = 0x18;
    u_cap.report_type = 3;
    u_cap.bit_offset = 0;
    u_cap.bit_size = 16;
    u_cap.path = "UPS.Output.ConfigActivePower";
    u_cap.found = true;

    mockHost._usages.push_back(u_load);
    mockHost._usages.push_back(u_cap);

    // 2. Load arrives FIRST (before configActivePower is known)
    uint8_t r_load[] = { 0x13, 20 }; // 20%
    driver.decodeReport(&mockHost, 0x13, 3, r_load, sizeof(r_load), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.load);
    TEST_ASSERT_EQUAL_UINT8(20, ups_data.load);
    // ConfigActivePower not yet known -> realPower should not be valid yet
    TEST_ASSERT_FALSE(ups_data.has.realPower);

    // 3. ConfigActivePower arrives AFTER load (e.g. 500W = 0x01F4 -> 0xF4, 0x01)
    uint8_t r_cap[] = { 0x18, 0xF4, 0x01 };
    driver.decodeReport(&mockHost, 0x18, 3, r_cap, sizeof(r_cap), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.configActivePower);
    TEST_ASSERT_EQUAL_UINT16(500, ups_data.configActivePower);
    // realPower must be recalculated IMMEDIATELY: 20% of 500W = 100W
    TEST_ASSERT_TRUE(ups_data.has.realPower);
    TEST_ASSERT_EQUAL_UINT16(100, ups_data.realPower);
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_cyberpower_status_and_voltage);
    RUN_TEST(test_cyberpower_inverted_string_quirk);
    RUN_TEST(test_cyberpower_loop_polling_and_string_requests);
    RUN_TEST(test_cyberpower_load_and_zero_load_reset);
    RUN_TEST(test_cyberpower_realpower_recalculated_when_config_arrives_after_load);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_cyberpower_status_and_voltage);
    RUN_TEST(test_cyberpower_inverted_string_quirk);
    RUN_TEST(test_cyberpower_loop_polling_and_string_requests);
    RUN_TEST(test_cyberpower_load_and_zero_load_reset);
    RUN_TEST(test_cyberpower_realpower_recalculated_when_config_arrives_after_load);
    UNITY_END();
}
void loop() {}
#endif
#endif

