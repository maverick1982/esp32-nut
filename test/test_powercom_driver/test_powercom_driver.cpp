#include <unity.h>
#include "PowercomDriver.h"
#include "IUSBHostUPS.h"

class MockPowercomHost : public IUSBHostUPS {
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

    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
    String getActiveBeeperPath() const override { return "UPS.PowerSummary.AudibleAlarmControl"; }
    uint32_t getQuirks() const override { return 0; }
    uint16_t _pid = 0x0004;
    uint16_t getPID() const override { return _pid; }
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

void setUp(void) {}
void tearDown(void) {}

void test_powercom_0xa4_valid(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;
    
    // " 13.7 2"
    // Report ID (0xA4) is byte 0
    uint8_t data[] = { 0xA4, ' ', '1', '3', '.', '7', ' ', '2' };
    
    driver.decodeReport(&host, 0xA4, 3, data, sizeof(data), ups_data);
    
    TEST_ASSERT_EQUAL_FLOAT(13.7f, ups_data.getFloat("battery.voltage"));
    TEST_ASSERT_TRUE(ups_data.hasKey("battery.voltage"));
}

void test_powercom_0xa4_invalid_no_dot(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;
    
    uint8_t data[] = { 0xA4, ' ', '1', '3', '7', ' ', '2', ' ' };
    
    driver.decodeReport(&host, 0xA4, 3, data, sizeof(data), ups_data);
    
    // Should not update batteryVoltage if no dot is present
    TEST_ASSERT_FALSE(ups_data.hasKey("battery.voltage"));
}

void test_powercom_0xa4_invalid_empty(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;
    
    uint8_t data[] = { 0xA4, ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
    
    driver.decodeReport(&host, 0xA4, 3, data, sizeof(data), ups_data);
    
    // Should not crash and not update batteryVoltage
    TEST_ASSERT_FALSE(ups_data.hasKey("battery.voltage"));
}

void test_powercom_0xa4_invalid_garbage(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;
    
    uint8_t data[] = { 0xA4, 'a', 'b', 'c', 'd', 'e', 'f', 'g' };
    
    driver.decodeReport(&host, 0xA4, 3, data, sizeof(data), ups_data);
    
    // Should not crash and not update batteryVoltage
    TEST_ASSERT_FALSE(ups_data.hasKey("battery.voltage"));
}

void test_powercom_beeper_mapping(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;
    
    // Powercom NUT definition: 1 = enabled, 2 = disabled
    // When AudibleAlarmControl is extracted as 1, beeperEnabled must be true
    // When AudibleAlarmControl is extracted as 2, beeperEnabled must be false
    HIDUsageDef def;
    def.path = "UPS.PowerSummary.AudibleAlarmControl";
    def.report_id = 0x1F;
    def.report_type = 3;
    def.bit_size = 8;
    def.bit_offset = 0;
    def.exponent = 0;
    def.unit = 0;
    def.found = true;
    host._usages.push_back(def);

    uint8_t data_enable[] = { 0x1F, 0x01 };
    driver.decodeReport(&host, 0x1F, 3, data_enable, sizeof(data_enable), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.beeper.status"));
    TEST_ASSERT_EQUAL_STRING("enabled", ups_data.get("ups.beeper.status").c_str());

    uint8_t data_disable[] = { 0x1F, 0x02 };
    driver.decodeReport(&host, 0x1F, 3, data_disable, sizeof(data_disable), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.beeper.status"));
    TEST_ASSERT_EQUAL_STRING("disabled", ups_data.get("ups.beeper.status").c_str());

    // Test encodeBeeperValue
    TEST_ASSERT_EQUAL_UINT8(1, driver.encodeBeeperValue(true, 8));
    TEST_ASSERT_EQUAL_UINT8(2, driver.encodeBeeperValue(false, 8));
    TEST_ASSERT_EQUAL_UINT8(1, driver.encodeBeeperValue(true, 1));
    TEST_ASSERT_EQUAL_UINT8(0, driver.encodeBeeperValue(false, 1));
}



#include "HIDParser.h"
#include <fstream>
#include <sstream>
#include <ArduinoJson.h>

void test_powercom_real_descriptor_parsing(void) {
    std::ifstream file("test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json");
    TEST_ASSERT_TRUE_MESSAGE(file.is_open(), "Fixture file powercom_spd750u_vid0d9f_pid0004_issue21.json must exist");

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string jsonStr = buffer.str();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonStr);
    TEST_ASSERT_FALSE_MESSAGE(err, "JSON deserialization failed");

    JsonArray hexArr = doc["report_descriptor_hex"].as<JsonArray>();
    std::vector<uint8_t> rawDesc;
    for (JsonVariant v : hexArr) {
        std::string hex = v.as<std::string>();
        uint8_t byte = (uint8_t)strtol(hex.c_str(), nullptr, 16);
        rawDesc.push_back(byte);
    }

    TEST_ASSERT_EQUAL_INT(996, rawDesc.size());

    HIDParser parser;
    parser.parseReportDescriptor(rawDesc.data(), rawDesc.size());
    const auto& usages = parser.getUsages();
    TEST_ASSERT_GREATER_THAN(0, usages.size());

    // Verify key Powercom usages are found in descriptor
    bool found_present_status = false;
    bool found_remaining_capacity = false;
    bool found_run_time_to_empty = false;
    bool found_beeper = false;

    for (const auto& u : usages) {
        if (u.path.indexOf("PresentStatus") >= 0 || u.usage == 0x00840002 || u.usage == 0x00850044) found_present_status = true;
        if (u.path.indexOf("RemainingCapacity") >= 0 || u.usage == 0x00850066) found_remaining_capacity = true;
        if (u.path.indexOf("RunTimeToEmpty") >= 0 || u.usage == 0x00850068) found_run_time_to_empty = true;
        if (u.path.indexOf("AudibleAlarmControl") >= 0 || u.usage == 0x0084005A || u.usage == 0x0085005A) found_beeper = true;
    }

    TEST_ASSERT_TRUE_MESSAGE(found_present_status, "PresentStatus usage should be present");
    TEST_ASSERT_TRUE_MESSAGE(found_remaining_capacity, "RemainingCapacity usage should be present");
    TEST_ASSERT_TRUE_MESSAGE(found_run_time_to_empty, "RunTimeToEmpty usage should be present");
    TEST_ASSERT_TRUE_MESSAGE(found_beeper, "AudibleAlarmControl usage should be present");
    TEST_ASSERT_TRUE_MESSAGE(parser.hasFeatureBeeperControl(), "Powercom SPD-750U descriptor contains Feature report for beeper");
}

void test_powercom_spurious_zero_ignored_when_online(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;

    HIDUsageDef u_in_volt;
    u_in_volt.report_id = 0x12;
    u_in_volt.report_type = 3;
    u_in_volt.bit_offset = 0;
    u_in_volt.bit_size = 16;
    u_in_volt.exponent = -1;
    u_in_volt.unit = 0;
    u_in_volt.path = "UPS.Input.Voltage";
    u_in_volt.found = true;
    host._usages.push_back(u_in_volt);

    HIDUsageDef u_out_volt;
    u_out_volt.report_id = 0x1D;
    u_out_volt.report_type = 3;
    u_out_volt.bit_offset = 0;
    u_out_volt.bit_size = 16;
    u_out_volt.exponent = -1;
    u_out_volt.unit = 0;
    u_out_volt.path = "UPS.Output.Voltage";
    u_out_volt.found = true;
    host._usages.push_back(u_out_volt);

    HIDUsageDef u_temp;
    u_temp.report_id = 0x2C;
    u_temp.report_type = 3;
    u_temp.bit_offset = 0;
    u_temp.bit_size = 8;
    u_temp.exponent = 0;
    u_temp.unit = 0;
    u_temp.path = "UPS.Battery.Temperature";
    u_temp.found = true;
    host._usages.push_back(u_temp);

    // Initial valid state: Online (OL), 218V in, 216V out, 30C
    ups_data.set("ups.status.ac_present", "1");
    ups_data.set("input.voltage", "218.0");
    ups_data.set("output.voltage", "216.0");
    ups_data.set("battery.temperature", "30.0");

    // Simulate empty / zeroed Feature Report responses (0.0V, 0C)
    uint8_t zero_in_volt[] = { 0x12, 0x00, 0x00 };
    driver.decodeReport(&host, 0x12, 3, zero_in_volt, sizeof(zero_in_volt), ups_data);

    uint8_t zero_out_volt[] = { 0x1D, 0x00, 0x00 };
    driver.decodeReport(&host, 0x1D, 3, zero_out_volt, sizeof(zero_out_volt), ups_data);

    uint8_t zero_temp[] = { 0x2C, 0x00 };
    driver.decodeReport(&host, 0x2C, 3, zero_temp, sizeof(zero_temp), ups_data);

    // Assert: established valid values must NOT be overwritten by spurious zeros when OL
    TEST_ASSERT_EQUAL_STRING("218.0", ups_data.get("input.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("216.0", ups_data.get("output.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("30.0", ups_data.get("battery.temperature").c_str());
}

void test_powercom_zero_input_voltage_allowed_when_on_battery(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;

    HIDUsageDef u_in_volt;
    u_in_volt.report_id = 0x12;
    u_in_volt.report_type = 3;
    u_in_volt.bit_offset = 0;
    u_in_volt.bit_size = 16;
    u_in_volt.exponent = -1;
    u_in_volt.unit = 0;
    u_in_volt.path = "UPS.Input.Voltage";
    u_in_volt.found = true;
    host._usages.push_back(u_in_volt);

    // Initial state: On Battery (OB)
    ups_data.set("ups.status.ac_present", "0");
    ups_data.set("ups.status.discharging", "1");
    ups_data.set("input.voltage", "218.0");

    uint8_t zero_in_volt[] = { 0x12, 0x00, 0x00 };
    driver.decodeReport(&host, 0x12, 3, zero_in_volt, sizeof(zero_in_volt), ups_data);

    // When on battery, 0.0V is a legitimate real reading and must be updated
    TEST_ASSERT_EQUAL_STRING("0.0", ups_data.get("input.voltage").c_str());
}

void test_powercom_two_tier_polling_and_static_skipping(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;

    // Report 0x11: Nominal Input Voltage (Static)
    HIDUsageDef u_nom;
    u_nom.report_id = 0x11;
    u_nom.report_type = 3;
    u_nom.bit_offset = 0;
    u_nom.bit_size = 16;
    u_nom.path = "UPS.Input.ConfigVoltage";
    u_nom.found = true;
    host._usages.push_back(u_nom);

    // Report 0x12: Input Voltage (Dynamic)
    HIDUsageDef u_dyn;
    u_dyn.report_id = 0x12;
    u_dyn.report_type = 3;
    u_dyn.bit_offset = 0;
    u_dyn.bit_size = 16;
    u_dyn.path = "UPS.Input.Voltage";
    u_dyn.found = true;
    host._usages.push_back(u_dyn);

    driver.setup();

    // t = 1000: Initial cycle starts full walk
    driver.loop(&host, ups_data, 1000); // step 1
    driver.loop(&host, ups_data, 1800); // step 2
    driver.loop(&host, ups_data, 2600); // step 3
    driver.loop(&host, ups_data, 3400); // step 4
    driver.loop(&host, ups_data, 4200); // step 5: Report 0x11
    driver.loop(&host, ups_data, 5000); // step 6: Report 0x12
    driver.loop(&host, ups_data, 5800); // step 7: Done initial cycle
    driver.loop(&host, ups_data, 6600); // Initial 0xA4 report

    // Simulate Report 0x11 decoded
    ups_data.set("input.voltage.nominal", "220");

    size_t req_count_after_init = host._requestedReports.size();
    TEST_ASSERT_GREATER_THAN(0, req_count_after_init);

    // Clear recorded reports
    host._requestedReports.clear();

    // t = 10000: Quick Poll (in between 30s) -> No feature reports should be polled!
    driver.loop(&host, ups_data, 10000);
    driver.loop(&host, ups_data, 10800);
    TEST_ASSERT_EQUAL_UINT32(0, host._requestedReports.size());

    // t = 36000: 30s elapsed -> Full Poll triggers!
    driver.loop(&host, ups_data, 36000);
    driver.loop(&host, ups_data, 36800);
    driver.loop(&host, ups_data, 37600);
    driver.loop(&host, ups_data, 38400);
    driver.loop(&host, ups_data, 39200);

    // During this recurring full poll: Report 0x12 (dynamic) should be requested,
    // but Report 0x11 (static, already populated) must NOT be requested!
    bool requested_0x11 = false;
    bool requested_0x12 = false;
    for (const auto& r : host._requestedReports) {
        if (r.first == 0x11) requested_0x11 = true;
        if (r.first == 0x12) requested_0x12 = true;
    }
    TEST_ASSERT_FALSE_MESSAGE(requested_0x11, "Static report 0x11 should NOT be polled again in recurring cycle");
    TEST_ASSERT_TRUE_MESSAGE(requested_0x12, "Dynamic report 0x12 should be polled during recurring full poll");
}

void test_powercom_status_reports_not_polled_over_ep0(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;

    // Report 0x0A: Status (Input & Feature)
    HIDUsageDef u_status_in;
    u_status_in.report_id = 0x0A;
    u_status_in.report_type = 1;
    u_status_in.path = "UPS.PowerSummary.PresentStatus.ACPresent";
    u_status_in.found = true;
    host._usages.push_back(u_status_in);

    HIDUsageDef u_status_feat;
    u_status_feat.report_id = 0x0A;
    u_status_feat.report_type = 3;
    u_status_feat.path = "UPS.PowerSummary.PresentStatus.ACPresent";
    u_status_feat.found = true;
    host._usages.push_back(u_status_feat);

    // Report 0x12: Input Voltage (Feature)
    HIDUsageDef u_volt;
    u_volt.report_id = 0x12;
    u_volt.report_type = 3;
    u_volt.path = "UPS.Input.Voltage";
    u_volt.found = true;
    host._usages.push_back(u_volt);

    driver.setup();

    // Run walk steps
    for (uint32_t t = 1000; t <= 10000; t += 800) {
        driver.loop(&host, ups_data, t);
    }

    bool polled_0x0a = false;
    for (const auto& r : host._requestedReports) {
        if (r.first == 0x0A) polled_0x0a = true;
    }
    TEST_ASSERT_FALSE_MESSAGE(polled_0x0a, "Report 0x0A (PresentStatus) should NOT be polled over EP0; left to Interrupt IN");
}

void test_powercom_spurious_zero_ignored_when_status_unknown_unless_discharging(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;

    HIDUsageDef u_in_volt;
    u_in_volt.report_id = 0x12;
    u_in_volt.report_type = 3;
    u_in_volt.bit_offset = 0;
    u_in_volt.bit_size = 16;
    u_in_volt.exponent = -1;
    u_in_volt.unit = 0;
    u_in_volt.path = "UPS.Input.Voltage";
    u_in_volt.found = true;
    host._usages.push_back(u_in_volt);

    HIDUsageDef u_out_volt;
    u_out_volt.report_id = 0x1D;
    u_out_volt.report_type = 3;
    u_out_volt.bit_offset = 0;
    u_out_volt.bit_size = 16;
    u_out_volt.exponent = -1;
    u_out_volt.unit = 0;
    u_out_volt.path = "UPS.Output.Voltage";
    u_out_volt.found = true;
    host._usages.push_back(u_out_volt);

    HIDUsageDef u_temp;
    u_temp.report_id = 0x2C;
    u_temp.report_type = 3;
    u_temp.bit_offset = 0;
    u_temp.bit_size = 8;
    u_temp.exponent = 0;
    u_temp.unit = 0;
    u_temp.path = "UPS.Battery.Temperature";
    u_temp.found = true;
    host._usages.push_back(u_temp);

    HIDUsageDef u_load;
    u_load.report_id = 0x2A;
    u_load.report_type = 3;
    u_load.bit_offset = 0;
    u_load.bit_size = 8;
    u_load.exponent = 0;
    u_load.unit = 0;
    u_load.path = "UPS.PowerSummary.PercentLoad";
    u_load.found = true;
    host._usages.push_back(u_load);

    HIDUsageDef u_beeper;
    u_beeper.report_id = 0x2D;
    u_beeper.report_type = 3;
    u_beeper.bit_offset = 0;
    u_beeper.bit_size = 1;
    u_beeper.exponent = 0;
    u_beeper.unit = 0;
    u_beeper.path = "UPS.PowerSummary.AudibleAlarmControl";
    u_beeper.found = true;
    host._usages.push_back(u_beeper);

    // Initial state: Established readings, but ups.status is "Unknown" (flapping, ac_present=0, discharging=0)
    ups_data.set("input.voltage", "218.0");
    ups_data.set("output.voltage", "216.0");
    ups_data.set("battery.temperature", "30.0");
    ups_data.set("ups.beeper.status", "enabled");

    TEST_ASSERT_EQUAL_STRING("Unknown", UPSData::computeUPSStatusString(ups_data).c_str());
    TEST_ASSERT_FALSE(ups_data.isOnline());

    // Simulate empty / zeroed Feature Report responses (0.0V, 0C, disabled beeper)
    uint8_t zero_in_volt[] = { 0x12, 0x00, 0x00 };
    driver.decodeReport(&host, 0x12, 3, zero_in_volt, sizeof(zero_in_volt), ups_data);

    uint8_t zero_out_volt[] = { 0x1D, 0x00, 0x00 };
    driver.decodeReport(&host, 0x1D, 3, zero_out_volt, sizeof(zero_out_volt), ups_data);

    uint8_t zero_temp[] = { 0x2C, 0x00 };
    driver.decodeReport(&host, 0x2C, 3, zero_temp, sizeof(zero_temp), ups_data);

    uint8_t zero_beeper[] = { 0x2D, 0x00 };
    driver.decodeReport(&host, 0x2D, 3, zero_beeper, sizeof(zero_beeper), ups_data);

    // Assert: Even when status is "Unknown", spurious zeros must NOT overwrite active telemetries unless discharging/shutdown
    TEST_ASSERT_EQUAL_STRING("218.0", ups_data.get("input.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("216.0", ups_data.get("output.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("30.0", ups_data.get("battery.temperature").c_str());
    TEST_ASSERT_EQUAL_STRING("enabled", ups_data.get("ups.beeper.status").c_str());
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_powercom_0xa4_valid);
    RUN_TEST(test_powercom_0xa4_invalid_no_dot);
    RUN_TEST(test_powercom_0xa4_invalid_empty);
    RUN_TEST(test_powercom_0xa4_invalid_garbage);
    RUN_TEST(test_powercom_beeper_mapping);
    RUN_TEST(test_powercom_real_descriptor_parsing);
    RUN_TEST(test_powercom_spurious_zero_ignored_when_online);
    RUN_TEST(test_powercom_zero_input_voltage_allowed_when_on_battery);
    RUN_TEST(test_powercom_two_tier_polling_and_static_skipping);
    RUN_TEST(test_powercom_status_reports_not_polled_over_ep0);
    RUN_TEST(test_powercom_spurious_zero_ignored_when_status_unknown_unless_discharging);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_powercom_0xa4_valid);
    RUN_TEST(test_powercom_0xa4_invalid_no_dot);
    RUN_TEST(test_powercom_0xa4_invalid_empty);
    RUN_TEST(test_powercom_0xa4_invalid_garbage);
    RUN_TEST(test_powercom_beeper_mapping);
    RUN_TEST(test_powercom_real_descriptor_parsing);
    RUN_TEST(test_powercom_spurious_zero_ignored_when_online);
    RUN_TEST(test_powercom_zero_input_voltage_allowed_when_on_battery);
    RUN_TEST(test_powercom_two_tier_polling_and_static_skipping);
    RUN_TEST(test_powercom_status_reports_not_polled_over_ep0);
    RUN_TEST(test_powercom_spurious_zero_ignored_when_status_unknown_unless_discharging);
    UNITY_END();
}
void loop() {}
#endif
#endif


