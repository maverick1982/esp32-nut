#include <unity.h>
#include "APCDriver.h"
#include "IHidTransport.h"
#include "UPSData.h"

class MockAPCHost : public IHidTransport {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;
    std::vector<uint8_t> _requestedStrings;
    std::vector<std::pair<uint8_t, uint8_t>> _requestedReports;

    void lock() const override {}
    void unlock() const override {}

    HIDParser _hid_parser;
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    String getActiveBeeperPath() const override { return "UPS.PowerSummary.AudibleAlarmControl"; }
    uint32_t getQuirks() const override { return 0; }
    bool isPollingPaused() const override { return false; }
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

// As USBHostUPS: every report goes through decodeReport() and then postDecode(), where the
// NUT table writes its rows (US-076: every APC value comes from apcHid2Nut())
static void decode(uint8_t report_id, uint8_t report_type, const uint8_t* data, size_t length) {
    driver.decodeReport(&mockHost, report_id, report_type, data, length, ups_data);
    driver.postDecode(&mockHost, report_id, report_type, data, length, ups_data);
}

void test_apc_power_summary_status(void) {
    HIDUsageDef u_ac;
    u_ac.report_id = 0x01;
    u_ac.report_type = 1;
    u_ac.bit_offset = 0;
    u_ac.bit_size = 1;
    u_ac.exponent = 0;
    u_ac.unit = 0;
    strcpy(u_ac.path, "UPS.PowerSummary.PresentStatus.ACPresent");
    u_ac.found = true;

    HIDUsageDef u_dischrg;
    u_dischrg.report_id = 0x01;
    u_dischrg.report_type = 1;
    u_dischrg.bit_offset = 1;
    u_dischrg.bit_size = 1;
    u_dischrg.exponent = 0;
    u_dischrg.unit = 0;
    strcpy(u_dischrg.path, "UPS.PowerSummary.PresentStatus.Discharging");
    u_dischrg.found = true;

    HIDUsageDef u_need_repl;
    u_need_repl.report_id = 0x01;
    u_need_repl.report_type = 1;
    u_need_repl.bit_offset = 2;
    u_need_repl.bit_size = 1;
    u_need_repl.exponent = 0;
    u_need_repl.unit = 0;
    strcpy(u_need_repl.path, "UPS.PowerSummary.PresentStatus.NeedReplacement");
    u_need_repl.found = true;

    mockHost._usages.push_back(u_ac);
    mockHost._usages.push_back(u_dischrg);
    mockHost._usages.push_back(u_need_repl);

    // AC Present = 1, Discharging = 0, NeedReplacement = 1 (bits: 1 | 0 | 4 = 0x05)
    uint8_t r1[] = { 0x01, 0x05 };
    decode(0x01, 1, r1, sizeof(r1));
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.status.ac_present"));
    TEST_ASSERT_TRUE(ups_data.getBool("ups.status.ac_present"));
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.status.discharging"));
    TEST_ASSERT_FALSE(ups_data.getBool("ups.status.discharging"));
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.status.replace_battery"));
    TEST_ASSERT_TRUE(ups_data.getBool("ups.status.replace_battery"));
}

void test_apc_battery_replace_date_packed_bcd(void) {
    // APC packed BCD date format: 0x011025 -> 2025/01/10 (UPS.Battery.APCBattReplaceDate -> battery.date)
    HIDUsageDef u_date;
    u_date.report_id = 0x07;
    u_date.report_type = 3;
    u_date.bit_offset = 0;
    u_date.bit_size = 24;
    u_date.exponent = 0;
    u_date.unit = 0;
    strcpy(u_date.path, "UPS.Battery.APCBattReplaceDate");
    u_date.found = true;

    mockHost._usages.push_back(u_date);

    // Standard USB HID PDC path UPS.PowerSummary.ManufacturerDate -> ups.mfr.date: 2006/09/15 (val 13615 = 0x352F)
    HIDUsageDef u_ups_mfr_date;
    u_ups_mfr_date.report_id = 0x09;
    u_ups_mfr_date.report_type = 3;
    u_ups_mfr_date.bit_offset = 0;
    u_ups_mfr_date.bit_size = 16;
    u_ups_mfr_date.exponent = 0;
    u_ups_mfr_date.unit = 0;
    strcpy(u_ups_mfr_date.path, "UPS.PowerSummary.ManufacturerDate");
    u_ups_mfr_date.found = true;

    mockHost._usages.push_back(u_ups_mfr_date);

    // Standard USB HID PDC path UPS.Battery.ManufacturerDate -> battery.mfr.date: 2026/08/07 (val 23815 = 0x5D07)
    HIDUsageDef u_batt_mfr_date;
    u_batt_mfr_date.report_id = 0x0B;
    u_batt_mfr_date.report_type = 3;
    u_batt_mfr_date.bit_offset = 0;
    u_batt_mfr_date.bit_size = 16;
    u_batt_mfr_date.exponent = 0;
    u_batt_mfr_date.unit = 0;
    strcpy(u_batt_mfr_date.path, "UPS.Battery.ManufacturerDate");
    u_batt_mfr_date.found = true;

    mockHost._usages.push_back(u_batt_mfr_date);

    // 1. Decode UPS.PowerSummary.ManufacturerDate (2006/09/15: 0x352F -> 0x2F, 0x35)
    uint8_t r_ups_mfr_date[] = { 0x09, 0x2F, 0x35 };
    decode(0x09, 3, r_ups_mfr_date, sizeof(r_ups_mfr_date));
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.mfr.date"));
    TEST_ASSERT_EQUAL_STRING("2006/09/15", ups_data.get("ups.mfr.date").c_str());

    // 2. Decode UPS.Battery.ManufacturerDate (2026/08/07: 0x5D07 -> 0x07, 0x5D)
    uint8_t r_batt_mfr_date[] = { 0x0B, 0x07, 0x5D };
    decode(0x0B, 3, r_batt_mfr_date, sizeof(r_batt_mfr_date));
    TEST_ASSERT_TRUE(ups_data.hasKey("battery.mfr.date"));
    TEST_ASSERT_EQUAL_STRING("2026/08/07", ups_data.get("battery.mfr.date").c_str());

    // 3. Decode Battery.APCBattReplaceDate (2025/01/10: 0x011025 -> LE: 0x25, 0x10, 0x01)
    uint8_t r_batt_date[] = { 0x07, 0x25, 0x10, 0x01 };
    decode(0x07, 3, r_batt_date, sizeof(r_batt_date));
    TEST_ASSERT_TRUE(ups_data.hasKey("battery.date"));
    TEST_ASSERT_EQUAL_STRING("2025/01/10", ups_data.get("battery.date").c_str());

    // Verify all 3 dates coexist without collision or overwriting
    TEST_ASSERT_EQUAL_STRING("2006/09/15", ups_data.get("ups.mfr.date").c_str());
    TEST_ASSERT_EQUAL_STRING("2026/08/07", ups_data.get("battery.mfr.date").c_str());
    TEST_ASSERT_EQUAL_STRING("2025/01/10", ups_data.get("battery.date").c_str());
}

void test_apc_load_and_real_power_calculation(void) {
    HIDUsageDef u_load;
    u_load.report_id = 0x08;
    u_load.report_type = 1;
    u_load.bit_offset = 0;
    u_load.bit_size = 8;
    u_load.exponent = 0;
    u_load.unit = 0;
    strcpy(u_load.path, "UPS.PowerConverter.PercentLoad"); // apc-hid.c:353
    u_load.found = true;

    mockHost._usages.push_back(u_load);

    // ConfigActivePower = 600W
    ups_data.set("ups.realpower.nominal", "600");

    // Load: 50%
    uint8_t r_load[] = { 0x08, 50 };
    decode(0x08, 1, r_load, sizeof(r_load));
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.load"));
    TEST_ASSERT_EQUAL_UINT8(50, ups_data.getFloat("ups.load"));
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.realpower"));
    TEST_ASSERT_EQUAL_UINT16(300, ups_data.getFloat("ups.realpower")); // 50% di 600W = 300W
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
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.mfr"));
    TEST_ASSERT_EQUAL_STRING("APC by ", ups_data.get("ups.mfr").c_str());
}

void test_apc_realpower_recalculated_when_config_arrives_after_load(void) {
    HIDUsageDef u_load;
    u_load.report_id = 0x08;
    u_load.report_type = 1;
    u_load.bit_offset = 0;
    u_load.bit_size = 8;
    strcpy(u_load.path, "UPS.PowerConverter.PercentLoad"); // apc-hid.c:353
    u_load.found = true;

    HIDUsageDef u_cap;
    u_cap.report_id = 0x14;
    u_cap.report_type = 3;
    u_cap.bit_offset = 0;
    u_cap.bit_size = 16;
    strcpy(u_cap.path, "UPS.PowerConverter.ConfigActivePower"); // apc-hid.c:376
    u_cap.found = true;

    mockHost._usages.push_back(u_load);
    mockHost._usages.push_back(u_cap);

    // 1. Load arrives first (30%)
    uint8_t r_load[] = { 0x08, 30 };
    decode(0x08, 1, r_load, sizeof(r_load));
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.load"));
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.realpower"));

    // 2. ConfigActivePower arrives after (700W = 0x02BC -> 0xBC, 0x02)
    uint8_t r_cap[] = { 0x14, 0xBC, 0x02 };
    decode(0x14, 3, r_cap, sizeof(r_cap));
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.realpower.nominal"));
    TEST_ASSERT_EQUAL_UINT16(700, ups_data.getFloat("ups.realpower.nominal"));
    // 30% of 700W = 210W
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.realpower"));
    TEST_ASSERT_EQUAL_UINT16(210, ups_data.getFloat("ups.realpower"));
}

// Issue 67: apc-hid.c espone battery.voltage.nominal con "%.1f".
void test_apc_battery_voltage_nominal_one_decimal(void) {
    HIDUsageDef u;
    u.report_id = 0x08;
    u.report_type = 3;
    u.bit_offset = 0;
    u.bit_size = 16;
    u.exponent = -2;
    u.unit = 0;
    strcpy(u.path, "UPS.PowerSummary.ConfigVoltage");
    u.found = true;
    mockHost._usages.push_back(u);

    uint8_t r1200[] = { 0x08, 0xB0, 0x04 }; // 12.00 V (Back-UPS CS, issue 48)
    decode(0x08, 3, r1200, sizeof(r1200));
    TEST_ASSERT_EQUAL_STRING("12.0", ups_data.get("battery.voltage.nominal").c_str());

    uint8_t r1360[] = { 0x08, 0x50, 0x05 }; // 13.60 V
    decode(0x08, 3, r1360, sizeof(r1360));
    TEST_ASSERT_EQUAL_STRING("13.6", ups_data.get("battery.voltage.nominal").c_str());
}

// Issue 76: ups.model without the firmware, as apc-hid.c apc_format_model()
static void assertSplit(const char* product, const char* model, const char* fw, const char* aux) {
    String m, f, a;
    TEST_ASSERT_TRUE_MESSAGE(APCDriver::splitProduct(product, m, f, a), product);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(model, m.c_str(), product);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(fw, f.c_str(), product);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(aux, a.c_str(), product);
}

void test_apc_split_product_real_strings(void) {
    // Product strings from the fixtures and from issue 71
    assertSplit("Back-UPS ES 700G FW:871.O4 .I USB FW:O4 ", "Back-UPS ES 700G", "871.O4 .I", "O4");
    assertSplit("Back-UPS CS 500 FW:808.q14 .I USB FW:q14 ", "Back-UPS CS 500", "808.q14 .I", "q14");
    assertSplit("Back-UPS BX1500G FW:866.L3 .D USB FW:L3 ", "Back-UPS BX1500G", "866.L3 .D", "L3");
    // The double space of the model stays, as upstream
    assertSplit("Back-UPS RS  900MI FW:948.g3 .I USB FW:g3     ", "Back-UPS RS  900MI", "948.g3 .I", "g3");
    // No "USB FW:" part
    assertSplit("Back-UPS CS 750-RS FW:936.b1 .I", "Back-UPS CS 750-RS", "936.b1 .I", "");
    assertSplit("Smart-UPS 750 FW:UPS 09.3 / ID=18", "Smart-UPS 750", "UPS 09.3 / ID=18", "");

    String m, f, a;
    TEST_ASSERT_FALSE(APCDriver::splitProduct("Back-UPS BX750MI", m, f, a));
}

void test_apc_format_device_strings(void) {
    UPSData d;
    d.set("ups.model", "Back-UPS ES 700G FW:871.O4 .I USB FW:O4");
    driver.formatDeviceStrings(d);
    TEST_ASSERT_EQUAL_STRING("Back-UPS ES 700G", d.get("ups.model").c_str());
    TEST_ASSERT_EQUAL_STRING("871.O4 .I", d.get("ups.firmware").c_str());
    TEST_ASSERT_EQUAL_STRING("O4", d.get("ups.firmware.aux").c_str());

    // Idempotent: called again after every string descriptor
    driver.formatDeviceStrings(d);
    TEST_ASSERT_EQUAL_STRING("Back-UPS ES 700G", d.get("ups.model").c_str());
    TEST_ASSERT_EQUAL_STRING("871.O4 .I", d.get("ups.firmware").c_str());

    // Without "FW:" nothing changes and no firmware key appears
    UPSData bx;
    bx.set("ups.model", "Back-UPS BX750MI");
    driver.formatDeviceStrings(bx);
    TEST_ASSERT_EQUAL_STRING("Back-UPS BX750MI", bx.get("ups.model").c_str());
    TEST_ASSERT_FALSE(bx.hasKey("ups.firmware"));
    TEST_ASSERT_FALSE(bx.hasKey("ups.firmware.aux"));

    // The BX detection works on the split model
    UPSData bx1500;
    bx1500.set("ups.model", "Back-UPS BX1500G FW:866.L3 .D USB FW:L3");
    driver.formatDeviceStrings(bx1500);
    TEST_ASSERT_TRUE(APCDriver::isBackUpsBX(bx1500.get("ups.model")));
}

// US-058, user decision of 2026-10-09: the DelayBefore* fields are the timers
// (ups.timer.*); ups.delay.* are configured delays the host sets, never the driver
static void addDelayUsage(const char* path, uint8_t report_id) {
    HIDUsageDef u;
    u.report_id = report_id;
    u.report_type = 3;
    u.bit_offset = 0;
    u.bit_size = 16;
    strcpy(u.path, path);
    u.found = true;
    mockHost._usages.push_back(u);
}

// The timers of apc-hid.c:357-371. The APCGeneralCollection usages have hex paths
// (0xFF860005.0xFF86007D/7E); for ups.timer.shutdown the first row read wins (358,
// PowerSummary), as the INIT rule of usbhid-ups (US-076)
void test_apc_delays_set_only_timers(void) {
    addDelayUsage("UPS.PowerSummary.DelayBeforeShutdown", 0x15);
    addDelayUsage("UPS.0xFF860005.0xFF86007D", 0x16);
    addDelayUsage("UPS.0xFF860005.0xFF86007E", 0x17);

    uint8_t r_ps[] = { 0x15, 30, 0 };
    decode(0x15, 3, r_ps, sizeof(r_ps));
    TEST_ASSERT_EQUAL_STRING("30", ups_data.get("ups.timer.shutdown").c_str());

    uint8_t r_sd[] = { 0x16, 40, 0 };
    decode(0x16, 3, r_sd, sizeof(r_sd));
    TEST_ASSERT_EQUAL_STRING("30", ups_data.get("ups.timer.shutdown").c_str()); // row 370 dropped

    uint8_t r_st[] = { 0x17, 50, 0 };
    decode(0x17, 3, r_st, sizeof(r_st));
    TEST_ASSERT_EQUAL_STRING("50", ups_data.get("ups.timer.start").c_str());

    TEST_ASSERT_FALSE(ups_data.hasKey("ups.delay.shutdown"));
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.delay.start"));
    TEST_ASSERT_TRUE(driver.shutdownCommandsSupported());
    TEST_ASSERT_EQUAL_INT32(20, driver.defaultOffDelay());
    TEST_ASSERT_EQUAL_INT32(30, driver.defaultOnDelay());
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
    decode(report_id, report_type, data, sizeof(data));
}

// apc-hid.c:414 + apc_sensitivity_info: UPS.Input.APCSensitivity (0xff860061), 0/1/2
void test_apc_input_sensitivity_from_nut_table(void) {
    mockHost._usages.push_back(nutField("UPS.Input.0xFF860061", 0xff860061, 0x35, 3));
    feed(0x35, 3, 0);
    TEST_ASSERT_EQUAL_STRING("low", ups_data.get("input.sensitivity").c_str());
    feed(0x35, 3, 1);
    TEST_ASSERT_EQUAL_STRING("medium", ups_data.get("input.sensitivity").c_str());
    feed(0x35, 3, 2);
    TEST_ASSERT_EQUAL_STRING("high", ups_data.get("input.sensitivity").c_str());

    std::vector<NutTable::RWVariable> rw;
    driver.writableVariables(ups_data, rw);
    TEST_ASSERT_EQUAL(1, rw.size());
    TEST_ASSERT_EQUAL_STRING("input.sensitivity", rw[0].name);
    TEST_ASSERT_EQUAL(10, rw[0].aux);
    TEST_ASSERT_EQUAL(0, rw[0].enums.size()); // no HU_FLAG_ENUM upstream
}

// A first value outside apc_sensitivity_info: upstream never creates the variable
void test_apc_input_sensitivity_unknown_first_value(void) {
    mockHost._usages.push_back(nutField("UPS.Input.0xFF860061", 0xff860061, 0x35, 3));
    feed(0x35, 3, 3);
    TEST_ASSERT_FALSE(ups_data.hasKey("input.sensitivity"));
    std::vector<NutTable::RWVariable> rw;
    driver.writableVariables(ups_data, rw);
    TEST_ASSERT_EQUAL(0, rw.size());
}

// --- apcHid2Nut() is the only source of the keys it owns (US-076, ADR 0011) ---

static void feed16(uint8_t report_id, uint8_t report_type, uint16_t value) {
    uint8_t data[] = { report_id, (uint8_t)(value & 0xFF), (uint8_t)(value >> 8) };
    decode(report_id, report_type, data, sizeof(data));
}

// apc-hid.c:352-353: "%.1f" from UPS.Output.PercentLoad, "%.0f" from PowerConverter.PercentLoad
void test_apc_load_format_follows_row(void) {
    mockHost._usages.push_back(nutField("UPS.PowerConverter.PercentLoad", 0x00840035, 0x10, 3));
    feed(0x10, 3, 40);
    TEST_ASSERT_EQUAL_STRING("40", ups_data.get("ups.load").c_str());

    driver.setup();
    ups_data = UPSData();
    mockHost._usages.clear();
    mockHost._usages.push_back(nutField("UPS.Output.PercentLoad", 0x00840035, 0x11, 3));
    feed(0x11, 3, 40);
    TEST_ASSERT_EQUAL_STRING("40.0", ups_data.get("ups.load").c_str());
}

// apc-hid.c:342: battery.voltage "%.1f" (the generic mapping gave two decimals)
void test_apc_battery_voltage_one_decimal(void) {
    HIDUsageDef volt = nutField("UPS.PowerSummary.Voltage", 0x00840030, 0x12, 3, 16);
    volt.exponent = -2;
    mockHost._usages.push_back(volt);
    feed16(0x12, 3, 1376);
    TEST_ASSERT_EQUAL_STRING("13.8", ups_data.get("battery.voltage").c_str());
}

// apc-hid.c:399 apcstatusflag_info: 0 is OFF, 8 and 16 clear it
void test_apc_status_flag_off(void) {
    mockHost._usages.push_back(nutField("UPS.PowerSummary.PresentStatus.ACPresent", 0x008400d0, 0x13, 3, 1));
    mockHost._usages.push_back(nutField("UPS.PowerSummary.0xFF860060", 0xff860060, 0x14, 3));
    feed(0x13, 3, 1);
    feed(0x14, 3, 0);
    TEST_ASSERT_EQUAL_STRING("1", ups_data.get("ups.status.off").c_str());
    TEST_ASSERT_TRUE(UPSData::computeUPSStatusString(ups_data).indexOf("OFF") >= 0);
    feed(0x14, 3, 8);
    TEST_ASSERT_EQUAL_STRING("0", ups_data.get("ups.status.off").c_str());
    TEST_ASSERT_TRUE(UPSData::computeUPSStatusString(ups_data).indexOf("OFF") < 0);
}

// apc-hid.c:403-404: APCLineFailCause gives input.transfer.reason (US-107)
void test_apc_line_fail_cause_reason(void) {
    mockHost._usages.push_back(nutField("UPS.Input.0xFF860052", 0xff860052, 0x15, 3));
    feed(0x15, 3, 8);
    TEST_ASSERT_EQUAL_STRING("input voltage out of range", ups_data.get("input.transfer.reason").c_str());
    // 7 has no text in the vrange row: the vrange bit stays, as with a NULL hu_find_infoval()
    feed(0x15, 3, 7);
    TEST_ASSERT_EQUAL_STRING("input voltage out of range", ups_data.get("input.transfer.reason").c_str());
    feed(0x15, 3, 0); // both rows clear their bit
    TEST_ASSERT_FALSE(ups_data.hasKey("input.transfer.reason"));
    feed(0x15, 3, 7);
    TEST_ASSERT_EQUAL_STRING("input frequency out of range", ups_data.get("input.transfer.reason").c_str());
}

// apc-hid.c:371: ups.timer.reboot on APCGeneralCollection.APCDelayBeforeReboot (hex path)
void test_apc_general_collection_timers(void) {
    HIDUsageDef reboot = nutField("UPS.0xFF860005.0xFF86007C", 0xff86007c, 0x16, 3, 16);
    reboot.logical_min = -1;
    mockHost._usages.push_back(reboot);
    feed16(0x16, 3, 0xFFFF); // -1
    TEST_ASSERT_EQUAL_STRING("-1", ups_data.get("ups.timer.reboot").c_str());
}

// apc-hid.c:347-349: PowerSummary.APCBattReplaceDate is the second battery.mfr.date row, 0 is
// "not set" (apc_date_conversion)
void test_apc_battery_dates(void) {
    HIDUsageDef ps = nutField("UPS.PowerSummary.APCBattReplaceDate", 0xff860016, 0x17, 3, 24);
    mockHost._usages.push_back(ps);
    uint8_t r[] = { 0x17, 0x25, 0x10, 0x01 }; // 0x011025: 2025/01/10
    decode(0x17, 3, r, sizeof(r));
    TEST_ASSERT_EQUAL_STRING("2025/01/10", ups_data.get("battery.mfr.date").c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("battery.date"));

    driver.setup();
    ups_data = UPSData();
    mockHost._usages.clear();
    mockHost._usages.push_back(nutField("UPS.Battery.APCBattReplaceDate", 0xff860016, 0x18, 3, 24));
    uint8_t zero[] = { 0x18, 0, 0, 0 };
    decode(0x18, 3, zero, sizeof(zero));
    TEST_ASSERT_EQUAL_STRING("not set", ups_data.get("battery.date").c_str());
}

// Standard NUT names apc-hid does not read stay as extensions (ADR 0011 rule 2), the
// non-standard battery.capacity.full goes
void test_apc_extensions_kept(void) {
    mockHost._usages.push_back(nutField("UPS.Input.Frequency", 0x00840032, 0x19, 3, 16));
    mockHost._usages.push_back(nutField("UPS.BatterySystem.Battery.DesignCapacity", 0x00850083, 0x1A, 3, 16));
    mockHost._usages.push_back(nutField("UPS.BatterySystem.Battery.FullChargeCapacity", 0x00850067, 0x1B, 3, 16));
    feed16(0x19, 3, 50);
    feed16(0x1A, 3, 36000);
    feed16(0x1B, 3, 36000);
    TEST_ASSERT_EQUAL_STRING("50.0", ups_data.get("input.frequency").c_str());
    // mge_battery_capacity of the genericHid2Nut() row (mge-hid.c:2016, US-079): "%.2f"
    TEST_ASSERT_EQUAL_STRING("10.00", ups_data.get("battery.capacity").c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("battery.capacity.full"));
}

// The generic status mappings are superseded: only the BOOL rows of apc_hid2nut set the keys
void test_apc_superseded_status_paths(void) {
    mockHost._usages.push_back(nutField("UPS.PowerSummary.PresentStatus.FullyCharged", 0x00850066, 0x1C, 3, 1));
    feed(0x1C, 3, 1);
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.status.fully_charged")); // apc-hid does not read it
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_apc_power_summary_status);
    RUN_TEST(test_apc_battery_replace_date_packed_bcd);
    RUN_TEST(test_apc_load_and_real_power_calculation);
    RUN_TEST(test_apc_loop_polling_and_string_requests);
    RUN_TEST(test_apc_realpower_recalculated_when_config_arrives_after_load);
    RUN_TEST(test_apc_battery_voltage_nominal_one_decimal);
    RUN_TEST(test_apc_split_product_real_strings);
    RUN_TEST(test_apc_format_device_strings);
    RUN_TEST(test_apc_delays_set_only_timers);
    RUN_TEST(test_apc_input_sensitivity_from_nut_table);
    RUN_TEST(test_apc_input_sensitivity_unknown_first_value);
    RUN_TEST(test_apc_load_format_follows_row);
    RUN_TEST(test_apc_battery_voltage_one_decimal);
    RUN_TEST(test_apc_status_flag_off);
    RUN_TEST(test_apc_line_fail_cause_reason);
    RUN_TEST(test_apc_general_collection_timers);
    RUN_TEST(test_apc_battery_dates);
    RUN_TEST(test_apc_extensions_kept);
    RUN_TEST(test_apc_superseded_status_paths);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_apc_power_summary_status);
    RUN_TEST(test_apc_battery_replace_date_packed_bcd);
    RUN_TEST(test_apc_load_and_real_power_calculation);
    RUN_TEST(test_apc_loop_polling_and_string_requests);
    RUN_TEST(test_apc_realpower_recalculated_when_config_arrives_after_load);
    RUN_TEST(test_apc_battery_voltage_nominal_one_decimal);
    RUN_TEST(test_apc_split_product_real_strings);
    RUN_TEST(test_apc_format_device_strings);
    RUN_TEST(test_apc_delays_set_only_timers);
    RUN_TEST(test_apc_input_sensitivity_from_nut_table);
    RUN_TEST(test_apc_input_sensitivity_unknown_first_value);
    RUN_TEST(test_apc_load_format_follows_row);
    RUN_TEST(test_apc_battery_voltage_one_decimal);
    RUN_TEST(test_apc_status_flag_off);
    RUN_TEST(test_apc_line_fail_cause_reason);
    RUN_TEST(test_apc_general_collection_timers);
    RUN_TEST(test_apc_battery_dates);
    RUN_TEST(test_apc_extensions_kept);
    RUN_TEST(test_apc_superseded_status_paths);
    UNITY_END();
}
void loop() {}
#endif
#endif

