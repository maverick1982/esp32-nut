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

    HIDParser _hid_parser;
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
    String getActiveBeeperPath() const override { return "UPS.PowerSummary.AudibleAlarmControl"; }
    uint32_t getQuirks() const override { return _quirks; }
    uint16_t _pid = 0x0501;
    uint16_t getPID() const override { return _pid; }
    bool isPollingPaused() const override { return false; }
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
    mockHost._pid = 0x0501;
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
    strcpy(u_ac.path, "UPS.PowerSummary.PresentStatus.ACPresent");
    u_ac.found = true;

    HIDUsageDef u_volt;
    u_volt.report_id = 0x02;
    u_volt.report_type = 1;
    u_volt.bit_offset = 0;
    u_volt.bit_size = 16;
    u_volt.exponent = -1; // Scale factor 0.1V (2305 -> 230.5V)
    u_volt.unit = 0;
    strcpy(u_volt.path, "UPS.Output.Voltage");
    u_volt.found = true;

    mockHost._usages.push_back(u_ac);
    mockHost._usages.push_back(u_volt);

    uint8_t r1[] = { 0x01, 0x01 };
    driver.decodeReport(&mockHost, 0x01, 1, r1, sizeof(r1), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.status.ac_present"));
    TEST_ASSERT_TRUE(ups_data.getBool("ups.status.ac_present"));

    // 2305 = 0x0901 -> 0x01, 0x09
    uint8_t r2[] = { 0x02, 0x01, 0x09 };
    driver.decodeReport(&mockHost, 0x02, 1, r2, sizeof(r2), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("output.voltage"));
    TEST_ASSERT_FLOAT_WITHIN(0.05, 230.5, ups_data.getFloat("output.voltage"));
}

void test_cyberpower_inverted_string_quirk(void) {
    mockHost._iManufacturer = 1;
    mockHost._iProduct = 2;
    mockHost._quirks = QUIRK_INVERT_STRINGS;

    // Normal UTF-16LE inverted bitwise: ~'C' = ~0x43 = 0xBC, high byte ~0x00 = 0xFF
    // Manufacturer inverted: "CPS"
    uint8_t desc_inv_mfr[] = { 8, 0x03, (uint8_t)~'C', 0xFF, (uint8_t)~'P', 0xFF, (uint8_t)~'S', 0xFF };
    driver.parseStringDescriptor(&mockHost, 1, desc_inv_mfr, sizeof(desc_inv_mfr), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.mfr"));
    TEST_ASSERT_EQUAL_STRING("CPS", ups_data.get("ups.mfr").c_str());

    // Auto-detect inverted string (even if quirk flag wasn't set, high byte == 0xFF)
    mockHost._quirks = 0; // reset quirk
    uint8_t desc_inv_prod[] = { 10, 0x03, (uint8_t)~'1', 0xFF, (uint8_t)~'5', 0xFF, (uint8_t)~'0', 0xFF, (uint8_t)~'0', 0xFF };
    driver.parseStringDescriptor(&mockHost, 2, desc_inv_prod, sizeof(desc_inv_prod), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.model"));
    TEST_ASSERT_EQUAL_STRING("1500", ups_data.get("ups.model").c_str());
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
    strcpy(u_cap.path, "UPS.Output.ConfigActivePower");
    u_cap.found = true;

    // PercentLoad on Report 0x13 (8 bit)
    HIDUsageDef u_load;
    u_load.report_id = 0x13;
    u_load.report_type = 3; // Feature
    u_load.bit_offset = 0;
    u_load.bit_size = 8;
    u_load.exponent = 0;
    u_load.unit = 0;
    strcpy(u_load.path, "UPS.Output.PercentLoad");
    u_load.found = true;

    mockHost._usages.push_back(u_cap);
    mockHost._usages.push_back(u_load);

    // 1. Decode ConfigActivePower = 810 W (0x032A -> 0x2A, 0x03)
    uint8_t r_cap[] = { 0x18, 0x2A, 0x03 };
    driver.decodeReport(&mockHost, 0x18, 3, r_cap, sizeof(r_cap), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.realpower.nominal"));
    TEST_ASSERT_EQUAL_UINT16(810, ups_data.getFloat("ups.realpower.nominal"));

    // 2. Decode Load = 11%
    uint8_t r_load11[] = { 0x13, 0x0B };
    driver.decodeReport(&mockHost, 0x13, 3, r_load11, sizeof(r_load11), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.load"));
    TEST_ASSERT_EQUAL_UINT8(11, ups_data.getFloat("ups.load"));
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.realpower"));
    TEST_ASSERT_EQUAL_UINT16((810 * 11) / 100, ups_data.getFloat("ups.realpower")); // 89 W

    // 3. Decode Load = 0% (zero payload [0x13, 0x00])
    uint8_t r_load0[] = { 0x13, 0x00 };
    driver.decodeReport(&mockHost, 0x13, 3, r_load0, sizeof(r_load0), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.load"));
    TEST_ASSERT_EQUAL_UINT8(0, ups_data.getFloat("ups.load"));
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.realpower"));
    TEST_ASSERT_EQUAL_UINT16(0, ups_data.getFloat("ups.realpower"));
}

void test_cyberpower_realpower_recalculated_when_config_arrives_after_load(void) {
    // 1. Setup usages
    HIDUsageDef u_load;
    u_load.report_id = 0x13;
    u_load.report_type = 3;
    u_load.bit_offset = 0;
    u_load.bit_size = 8;
    strcpy(u_load.path, "UPS.Output.PercentLoad");
    u_load.found = true;

    HIDUsageDef u_cap;
    u_cap.report_id = 0x18;
    u_cap.report_type = 3;
    u_cap.bit_offset = 0;
    u_cap.bit_size = 16;
    strcpy(u_cap.path, "UPS.Output.ConfigActivePower");
    u_cap.found = true;

    mockHost._usages.push_back(u_load);
    mockHost._usages.push_back(u_cap);

    // 2. Load arrives FIRST (before configActivePower is known)
    uint8_t r_load[] = { 0x13, 20 }; // 20%
    driver.decodeReport(&mockHost, 0x13, 3, r_load, sizeof(r_load), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.load"));
    TEST_ASSERT_EQUAL_UINT8(20, ups_data.getFloat("ups.load"));
    // ConfigActivePower not yet known -> realPower should not be valid yet
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.realpower"));

    // 3. ConfigActivePower arrives AFTER load (e.g. 500W = 0x01F4 -> 0xF4, 0x01)
    uint8_t r_cap[] = { 0x18, 0xF4, 0x01 };
    driver.decodeReport(&mockHost, 0x18, 3, r_cap, sizeof(r_cap), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.realpower.nominal"));
    TEST_ASSERT_EQUAL_UINT16(500, ups_data.getFloat("ups.realpower.nominal"));
    // realPower must be recalculated IMMEDIATELY: 20% of 500W = 100W
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.realpower"));
    TEST_ASSERT_EQUAL_UINT16(100, ups_data.getFloat("ups.realpower"));
}

// US-058: DEFAULT_OFFDELAY_CPS / DEFAULT_ONDELAY_CPS of usbhid-ups.h (the CPS firmware
// rounds the delays down to multiples of 60 s)
void test_cyberpower_default_delays(void) {
    TEST_ASSERT_EQUAL_INT32(60, driver.defaultOffDelay());
    TEST_ASSERT_EQUAL_INT32(120, driver.defaultOnDelay());
    TEST_ASSERT_TRUE(driver.shutdownCommandsSupported());
}


// --- NUT table (US-066) ---

static HIDUsageDef nutField(const char* path, uint32_t usage, uint8_t report_id, uint8_t report_type,
                            uint16_t bit_size = 8, std::initializer_list<uint8_t> coll = {}) {
    HIDUsageDef d;
    strcpy(d.path, path);
    d.usage = usage;
    d.report_id = report_id;
    d.report_type = report_type;
    d.bit_offset = 0;
    d.bit_size = bit_size;
    d.found = true;
    size_t i = 0;
    for (uint8_t c : coll) d.coll_index[i++] = c;
    return d;
}

static void feed(uint8_t report_id, uint8_t report_type, uint8_t value) {
    uint8_t data[] = { report_id, value };
    driver.decodeReport(&mockHost, report_id, report_type, data, sizeof(data), ups_data);
    driver.postDecode(&mockHost, report_id, report_type, data, sizeof(data), ups_data);
}

// cps-hid.c:416 + cps_sensitivity_info: UPS.Output.CPSInputSensitivity (0xff010043)
void test_cyberpower_input_sensitivity_from_nut_table(void) {
    mockHost._usages.push_back(nutField("UPS.Output.0xFF010043", 0xff010043, 0x1A, 3));
    feed(0x1A, 3, 0);
    TEST_ASSERT_EQUAL_STRING("low", ups_data.get("input.sensitivity").c_str());
    feed(0x1A, 3, 1);
    TEST_ASSERT_EQUAL_STRING("normal", ups_data.get("input.sensitivity").c_str());
    feed(0x1A, 3, 2);
    TEST_ASSERT_EQUAL_STRING("high", ups_data.get("input.sensitivity").c_str());

    std::vector<NutTable::RWVariable> rw;
    driver.writableVariables(ups_data, rw);
    TEST_ASSERT_EQUAL(1, rw.size());
    TEST_ASSERT_EQUAL(0, rw[0].aux); // info_len 0, copied as upstream
    TEST_ASSERT_EQUAL(3, rw[0].enums.size());
    TEST_ASSERT_EQUAL_STRING("normal", rw[0].enums[1]);
}

// --- cps-hid.c variables and conversions (issue #43) ---

static void feed16(uint8_t report_id, uint8_t report_type, uint16_t value) {
    uint8_t data[] = { report_id, (uint8_t)(value & 0xFF), (uint8_t)(value >> 8) };
    driver.decodeReport(&mockHost, report_id, report_type, data, sizeof(data), ups_data);
}

// cps_battvolt on PID 0x0501: 2/3 when the reading is above 1.4 x nominal (CP1350C fixture)
void test_cyberpower_battery_voltage_scale(void) {
    mockHost._usages.push_back(nutField("UPS.PowerSummary.ConfigVoltage", 0x00840040, 0x09, 3));
    HIDUsageDef volt = nutField("UPS.PowerSummary.Voltage", 0x00840030, 0x0A, 3, 16);
    volt.exponent = -1;
    mockHost._usages.push_back(volt);

    // Before the nominal voltage the scale is not decided: the value as it is
    feed16(0x0A, 3, 208);
    TEST_ASSERT_EQUAL_STRING("20.8", ups_data.get("battery.voltage").c_str());
    feed(0x09, 3, 12);
    feed16(0x0A, 3, 208);
    TEST_ASSERT_EQUAL_STRING("13.9", ups_data.get("battery.voltage").c_str());
    // Decided once: a lower reading keeps the 2/3 scale
    feed16(0x0A, 3, 150);
    TEST_ASSERT_EQUAL_STRING("10.0", ups_data.get("battery.voltage").c_str());
}

void test_cyberpower_battery_voltage_no_scale(void) {
    mockHost._usages.push_back(nutField("UPS.PowerSummary.ConfigVoltage", 0x00840040, 0x09, 3));
    HIDUsageDef volt = nutField("UPS.PowerSummary.Voltage", 0x00840030, 0x0A, 3, 16);
    volt.exponent = -1;
    mockHost._usages.push_back(volt);

    // Within 1.4 x nominal: "%.1f" as cps_battvolt, not the two decimals of the generic driver
    feed(0x09, 3, 24);
    feed16(0x0A, 3, 272);
    TEST_ASSERT_EQUAL_STRING("27.2", ups_data.get("battery.voltage").c_str());

    // PID 0x0601 has no cps_battery_scale hook
    driver.setup();
    ups_data = UPSData();
    mockHost._pid = 0x0601;
    feed(0x09, 3, 12);
    feed16(0x0A, 3, 208);
    TEST_ASSERT_EQUAL_STRING("20.8", ups_data.get("battery.voltage").c_str());
}

void test_cyberpower_frequency_scale_rule(void) {
    double scale = -1;
    TEST_ASSERT_TRUE(CyberPowerDriver::frequencyScale(50.0, 0, 0, 0, scale));
    TEST_ASSERT_EQUAL_FLOAT(1.0, scale);
    TEST_ASSERT_TRUE(CyberPowerDriver::frequencyScale(500.0, 0, 0, 0, scale));
    TEST_ASSERT_EQUAL_FLOAT(0.1, scale);
    TEST_ASSERT_TRUE(CyberPowerDriver::frequencyScale(60.0, 0, 0, 0, scale));
    TEST_ASSERT_EQUAL_FLOAT(1.0, scale);
    TEST_ASSERT_TRUE(CyberPowerDriver::frequencyScale(599.0, 0, 0, 0, scale));
    TEST_ASSERT_EQUAL_FLOAT(0.1, scale);
    // Known nominal: 50 Hz +-5%
    TEST_ASSERT_TRUE(CyberPowerDriver::frequencyScale(498.0, 50, 0, 0, scale));
    TEST_ASSERT_EQUAL_FLOAT(0.1, scale);
    // Neither 1 nor 0.1 fits: undecided
    TEST_ASSERT_FALSE(CyberPowerDriver::frequencyScale(1000.0, 0, 0, 0, scale));
    TEST_ASSERT_FALSE(CyberPowerDriver::frequencyScale(56.0, 50, 0, 0, scale));
}

void test_cyberpower_frequency_scaled_on_0501(void) {
    mockHost._usages.push_back(nutField("UPS.Input.Frequency", 0x00840032, 0x0E, 3, 16));
    mockHost._usages.push_back(nutField("UPS.Output.Frequency", 0x00840032, 0x12, 3, 16));

    feed16(0x0E, 3, 500);
    TEST_ASSERT_EQUAL_STRING("50.0", ups_data.get("input.frequency").c_str());
    feed16(0x0E, 3, 499);
    TEST_ASSERT_EQUAL_STRING("49.9", ups_data.get("input.frequency").c_str());
    // Output decided on its own
    feed16(0x12, 3, 50);
    TEST_ASSERT_EQUAL_STRING("50.0", ups_data.get("output.frequency").c_str());

    // PID 0x0601: never scaled
    driver.setup();
    ups_data = UPSData();
    mockHost._pid = 0x0601;
    feed16(0x0E, 3, 500);
    TEST_ASSERT_EQUAL_STRING("500.0", ups_data.get("input.frequency").c_str());
}

void test_cyberpower_cps_variables(void) {
    mockHost._usages.push_back(nutField("UPS.PowerSummary.WarningCapacityLimit", 0x0085008c, 0x07, 3));
    HIDUsageDef full = nutField("UPS.PowerSummary.FullChargeCapacity", 0x00850067, 0x07, 3);
    full.bit_offset = 8;
    mockHost._usages.push_back(full);
    mockHost._usages.push_back(nutField("UPS.Output.ConfigApparentPower", 0x00840043, 0x18, 3, 16));
    HIDUsageDef shut = nutField("UPS.Output.DelayBeforeShutdown", 0x00840057, 0x15, 3, 16);
    shut.logical_min = -1;
    mockHost._usages.push_back(shut);
    HIDUsageDef start = nutField("UPS.Output.DelayBeforeStartup", 0x00840056, 0x16, 3, 16);
    start.logical_min = -1;
    mockHost._usages.push_back(start);

    uint8_t r7[] = { 0x07, 20, 100 };
    driver.decodeReport(&mockHost, 0x07, 3, r7, sizeof(r7), ups_data);
    TEST_ASSERT_EQUAL_STRING("20", ups_data.get("battery.charge.warning").c_str());
    TEST_ASSERT_EQUAL_STRING("100%", ups_data.get("battery.status").c_str());
    feed16(0x18, 3, 1500);
    TEST_ASSERT_EQUAL_STRING("1500", ups_data.get("ups.power.nominal").c_str());
    feed16(0x15, 3, 0xFFC4); // -60
    TEST_ASSERT_EQUAL_STRING("-60", ups_data.get("ups.timer.shutdown").c_str());
    feed16(0x16, 3, 120);
    TEST_ASSERT_EQUAL_STRING("120", ups_data.get("ups.timer.start").c_str());
}

static void runLoopSteps(int steps) {
    for (int i = 0; i < steps; i++) driver.loop(&mockHost, ups_data, 1000 + i * 60);
}

void test_cyberpower_string_values(void) {
    mockHost._usages.push_back(nutField("UPS.PowerSummary.iDeviceChemistry", 0x00850089, 0x03, 3));
    mockHost._usages.push_back(nutField("UPS.PowerSummary.iOEMInformation", 0x0085008f, 0x05, 3));
    mockHost._usages.push_back(nutField("UPS.PowerSummary.0xFF0100D0", 0xff0100d0, 0x1B, 3));
    feed(0x03, 3, 4);
    feed(0x05, 3, 5);
    feed(0x1B, 3, 6);

    // Asked in the next full poll
    runLoopSteps(8);
    TEST_ASSERT_EQUAL_UINT32(3, mockHost._requestedStrings.size());
    TEST_ASSERT_EQUAL_UINT8(4, mockHost._requestedStrings[0]);
    TEST_ASSERT_EQUAL_UINT8(6, mockHost._requestedStrings[1]);
    TEST_ASSERT_EQUAL_UINT8(5, mockHost._requestedStrings[2]);

    uint8_t chem[] = { 8, 0x03, 'P', 0, 'b', 0, 'A', 0 };
    driver.parseStringDescriptor(&mockHost, 4, chem, sizeof(chem), ups_data);
    TEST_ASSERT_EQUAL_STRING("PbA", ups_data.get("battery.type").c_str());
    // Inverted strings of some firmwares, as the device strings
    uint8_t fw[] = { 6, 0x03, (uint8_t)~'C', 0xFF, (uint8_t)~'R', 0xFF };
    driver.parseStringDescriptor(&mockHost, 6, fw, sizeof(fw), ups_data);
    TEST_ASSERT_EQUAL_STRING("CR", ups_data.get("ups.firmware").c_str());
    uint8_t oem[] = { 8, 0x03, 'C', 0, 'P', 0, 'S', 0 };
    driver.parseStringDescriptor(&mockHost, 5, oem, sizeof(oem), ups_data);
    TEST_ASSERT_EQUAL_STRING("CPS", ups_data.get("battery.mfr.date").c_str());
}

// One index for two values: iOEMInformation is the manufacturer string on many models
void test_cyberpower_string_index_shared_with_device_string(void) {
    mockHost._iManufacturer = 1;
    mockHost._usages.push_back(nutField("UPS.PowerSummary.iOEMInformation", 0x0085008f, 0x05, 3));
    feed(0x05, 3, 1);
    uint8_t mfr[] = { 8, 0x03, 'C', 0, 'P', 0, 'S', 0 };
    driver.parseStringDescriptor(&mockHost, 1, mfr, sizeof(mfr), ups_data);
    TEST_ASSERT_EQUAL_STRING("CPS", ups_data.get("ups.mfr").c_str());
    TEST_ASSERT_EQUAL_STRING("CPS", ups_data.get("battery.mfr.date").c_str());
}

// BR700ELCD layout (issue #40): no string request beyond the device strings
void test_cyberpower_vendor_layout_adds_no_requests(void) {
    mockHost._usages.push_back(nutField("UPS.PowerSummary.iOEMInformation", 0x0085008f, 0x03, 3));
    mockHost._usages.push_back(nutField("UPS.PowerSummary.0xFF0100D0", 0xff0100d0, 0x1E, 3));
    mockHost._usages.push_back(nutField("UPS.Output.DelayBeforeShutdown", 0x00840057, 134, 3, 16));
    feed(0x03, 3, 4);
    feed(0x1E, 3, 5);

    runLoopSteps(10);
    TEST_ASSERT_EQUAL_UINT32(0, mockHost._requestedStrings.size());
}

// ups.realpower is estimated only for devices that do not measure it
void test_cyberpower_measured_realpower_not_estimated(void) {
    mockHost._usages.push_back(nutField("UPS.Output.PercentLoad", 0x00840035, 0x13, 3));
    mockHost._usages.push_back(nutField("UPS.Output.ConfigActivePower", 0x00840044, 0x18, 3, 16));
    mockHost._usages.push_back(nutField("UPS.Output.ActivePower", 0x00840034, 0x19, 3, 16));

    // From the descriptor, before the first ActivePower read
    driver.loop(&mockHost, ups_data, 1000);
    feed16(0x18, 3, 1000);
    feed(0x13, 3, 16);
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.realpower"));

    feed16(0x19, 3, 153);
    TEST_ASSERT_EQUAL_STRING("153", ups_data.get("ups.realpower").c_str());
    // A later load no longer overwrites the measure (it gave 160 W)
    feed(0x13, 3, 16);
    TEST_ASSERT_EQUAL_STRING("153", ups_data.get("ups.realpower").c_str());
}

// ActivePower in a report the driver never reads: the estimate stays
void test_cyberpower_unread_activepower_keeps_estimate(void) {
    mockHost._usages.push_back(nutField("UPS.Output.PercentLoad", 0x00840035, 0x13, 3));
    mockHost._usages.push_back(nutField("UPS.Output.ConfigActivePower", 0x00840044, 0x18, 3, 16));
    mockHost._usages.push_back(nutField("UPS.Output.ActivePower", 0x00840034, 131, 3, 16));

    driver.loop(&mockHost, ups_data, 1000);
    feed16(0x18, 3, 1000);
    feed(0x13, 3, 16);
    TEST_ASSERT_EQUAL_STRING("160", ups_data.get("ups.realpower").c_str());
}

// driver.name, not ups.type (the UPS topology in NUT)
void test_cyberpower_driver_name(void) {
    driver.loop(&mockHost, ups_data, 1000);
    TEST_ASSERT_EQUAL_STRING("CyberPowerDriver", ups_data.get("driver.name").c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.type"));
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
    RUN_TEST(test_cyberpower_default_delays);
    RUN_TEST(test_cyberpower_input_sensitivity_from_nut_table);
    RUN_TEST(test_cyberpower_battery_voltage_scale);
    RUN_TEST(test_cyberpower_battery_voltage_no_scale);
    RUN_TEST(test_cyberpower_frequency_scale_rule);
    RUN_TEST(test_cyberpower_frequency_scaled_on_0501);
    RUN_TEST(test_cyberpower_cps_variables);
    RUN_TEST(test_cyberpower_string_values);
    RUN_TEST(test_cyberpower_string_index_shared_with_device_string);
    RUN_TEST(test_cyberpower_vendor_layout_adds_no_requests);
    RUN_TEST(test_cyberpower_measured_realpower_not_estimated);
    RUN_TEST(test_cyberpower_unread_activepower_keeps_estimate);
    RUN_TEST(test_cyberpower_driver_name);
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
    RUN_TEST(test_cyberpower_default_delays);
    RUN_TEST(test_cyberpower_input_sensitivity_from_nut_table);
    RUN_TEST(test_cyberpower_battery_voltage_scale);
    RUN_TEST(test_cyberpower_battery_voltage_no_scale);
    RUN_TEST(test_cyberpower_frequency_scale_rule);
    RUN_TEST(test_cyberpower_frequency_scaled_on_0501);
    RUN_TEST(test_cyberpower_cps_variables);
    RUN_TEST(test_cyberpower_string_values);
    RUN_TEST(test_cyberpower_string_index_shared_with_device_string);
    RUN_TEST(test_cyberpower_vendor_layout_adds_no_requests);
    RUN_TEST(test_cyberpower_measured_realpower_not_estimated);
    RUN_TEST(test_cyberpower_unread_activepower_keeps_estimate);
    RUN_TEST(test_cyberpower_driver_name);
    UNITY_END();
}
void loop() {}
#endif
#endif

