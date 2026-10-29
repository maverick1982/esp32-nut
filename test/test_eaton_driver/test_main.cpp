#include <unity.h>
#include "EatonDriver.h"
#include "IHidTransport.h"
#include "UPSData.h"

class MockEatonHost : public IHidTransport {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;
    std::vector<uint8_t> _requestedStrings;

    void lock() const override {}
    void unlock() const override {}

    HIDParser _hid_parser;
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    String getActiveBeeperPath() const override { return "UPS.PowerSummary.AudibleAlarmControl"; }
    uint32_t getQuirks() const override { return 0; }
    bool isPollingPaused() const override { return false; }
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

// As USBHostUPS: the hand-written mappings, then the NUT table (US-066, US-073)
static void decodeAll(uint8_t report_id, uint8_t report_type, const uint8_t* data, size_t length) {
    driver.decodeReport(&mockHost, report_id, report_type, data, length, ups_data);
    driver.postDecode(&mockHost, report_id, report_type, data, length, ups_data);
}

void test_eaton_ac_present_and_discharging(void) {
    HIDUsageDef u_ac;
    u_ac.report_id = 0x01;
    u_ac.report_type = 1;
    u_ac.bit_offset = 0;
    u_ac.bit_size = 1;
    strcpy(u_ac.path, "UPS.PowerSummary.PresentStatus.ACPresent");
    u_ac.found = true;

    HIDUsageDef u_dischrg;
    u_dischrg.report_id = 0x01;
    u_dischrg.report_type = 1;
    u_dischrg.bit_offset = 4;
    u_dischrg.bit_size = 1;
    strcpy(u_dischrg.path, "UPS.PowerSummary.PresentStatus.Discharging");
    u_dischrg.found = true;

    mockHost._usages.push_back(u_ac);
    mockHost._usages.push_back(u_dischrg);

    // 1. Report con AC Present = 1, Discharging = 0
    uint8_t r1[] = { 0x01, 0x01 };
    decodeAll(0x01, 1, r1, sizeof(r1));
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.status.ac_present"));
    TEST_ASSERT_TRUE(ups_data.getBool("ups.status.ac_present"));
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.status.discharging"));
    TEST_ASSERT_FALSE(ups_data.getBool("ups.status.discharging"));

    // 2. Report con AC Present = 0, Discharging = 1
    uint8_t r2[] = { 0x01, 0x10 };
    decodeAll(0x01, 1, r2, sizeof(r2));
    TEST_ASSERT_FALSE(ups_data.getBool("ups.status.ac_present"));
    TEST_ASSERT_TRUE(ups_data.getBool("ups.status.discharging"));
}

void test_eaton_voltage_and_battery(void) {
    HIDUsageDef u_cap;
    u_cap.report_id = 0x06;
    u_cap.report_type = 1;
    u_cap.bit_offset = 0;
    u_cap.bit_size = 8;
    u_cap.exponent = 0;
    u_cap.unit = 0;
    strcpy(u_cap.path, "UPS.PowerSummary.RemainingCapacity");
    u_cap.found = true;

    HIDUsageDef u_runtime;
    u_runtime.report_id = 0x06;
    u_runtime.report_type = 1;
    u_runtime.bit_offset = 8;
    u_runtime.bit_size = 32;
    u_runtime.exponent = 0;
    u_runtime.unit = 0;
    strcpy(u_runtime.path, "UPS.PowerSummary.RunTimeToEmpty");
    u_runtime.found = true;

    mockHost._usages.push_back(u_cap);
    mockHost._usages.push_back(u_runtime);

    // Report ID 0x06: capacity 85% (0x55), runtime 1200 sec (0x000004B0)
    uint8_t r6[] = { 0x06, 0x55, 0xB0, 0x04, 0x00, 0x00 };
    decodeAll(0x06, 1, r6, sizeof(r6));
    TEST_ASSERT_TRUE(ups_data.hasKey("battery.charge"));
    TEST_ASSERT_EQUAL_UINT8(85, ups_data.getFloat("battery.charge"));
    TEST_ASSERT_TRUE(ups_data.hasKey("battery.runtime"));
    TEST_ASSERT_EQUAL_UINT32(1200, ups_data.getFloat("battery.runtime"));
}

void test_eaton_string_descriptors(void) {
    mockHost._iManufacturer = 1;
    mockHost._iProduct = 2;
    mockHost._iSerialNumber = 3;

    // String descriptor Eaton (UTF-16LE: "Eaton")
    uint8_t desc_mfr[] = { 12, 0x03, 'E', 0, 'a', 0, 't', 0, 'o', 0, 'n', 0 };
    driver.parseStringDescriptor(&mockHost, 1, desc_mfr, sizeof(desc_mfr), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.mfr"));
    TEST_ASSERT_EQUAL_STRING("Eaton", ups_data.get("ups.mfr").c_str());

    // Product "3S 700"
    uint8_t desc_prod[] = { 14, 0x03, '3', 0, 'S', 0, ' ', 0, '7', 0, '0', 0, '0', 0 };
    driver.parseStringDescriptor(&mockHost, 2, desc_prod, sizeof(desc_prod), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.model"));
    TEST_ASSERT_EQUAL_STRING("3S 700", ups_data.get("ups.model").c_str());
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

void test_eaton_realpower_recalculated_when_config_arrives_after_load(void) {
    HIDUsageDef u_load;
    u_load.report_id = 0x01;
    u_load.report_type = 1;
    u_load.bit_offset = 0;
    u_load.bit_size = 8;
    strcpy(u_load.path, "UPS.PowerSummary.PercentLoad");
    u_load.found = true;

    HIDUsageDef u_cap;
    u_cap.report_id = 0x02;
    u_cap.report_type = 3;
    u_cap.bit_offset = 0;
    u_cap.bit_size = 16;
    strcpy(u_cap.path, "UPS.Flow.ConfigActivePower");
    u_cap.found = true;

    mockHost._usages.push_back(u_load);
    mockHost._usages.push_back(u_cap);

    // 1. Load arrives first (40%)
    uint8_t r_load[] = { 0x01, 40 };
    decodeAll(0x01, 1, r_load, sizeof(r_load));
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.load"));
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.realpower"));

    // 2. ConfigActivePower arrives after (1000W = 0x03E8 -> 0xE8, 0x03)
    uint8_t r_cap[] = { 0x02, 0xE8, 0x03 };
    decodeAll(0x02, 3, r_cap, sizeof(r_cap));
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.realpower.nominal"));
    TEST_ASSERT_EQUAL_UINT16(1000, ups_data.getFloat("ups.realpower.nominal"));
    // 40% of 1000W = 400W
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.realpower"));
    TEST_ASSERT_EQUAL_UINT16(400, ups_data.getFloat("ups.realpower"));
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

static void feedString(uint8_t index, const char* s) {
    uint8_t buf[64] = { 0 };
    size_t n = strlen(s);
    buf[0] = (uint8_t)(2 + 2 * n);
    buf[1] = 0x03;
    for (size_t i = 0; i < n; i++) buf[2 + 2 * i] = (uint8_t)s[i];
    driver.parseStringDescriptor(&mockHost, index, buf, buf[0], ups_data);
}

// mge-hid.c:2013-2014: battery.charge.low comes from RemainingCapacityLimitSetting, the
// first row; the generic RemainingCapacityLimit mapping never overrides it
void test_eaton_battery_charge_low_owned_by_table(void) {
    mockHost._usages.push_back(nutField("UPS.PowerSummary.RemainingCapacityLimit", 0x00850029, 0x08, 3));
    mockHost._usages.push_back(nutField("UPS.PowerSummary.0xFFFF004D", 0xffff004d, 0x22, 3));
    feed(0x22, 3, 20);
    feed(0x08, 3, 10);
    TEST_ASSERT_EQUAL_STRING("20", ups_data.get("battery.charge.low").c_str());

    driver.setup();
    ups_data = UPSData();
    feed(0x08, 3, 10);
    feed(0x22, 3, 20);
    TEST_ASSERT_EQUAL_STRING("20", ups_data.get("battery.charge.low").c_str());
}

// mge-hid.c:2247: outlet.desc, HU_FLAG_ABSENT, default "Main Outlet" when
// UPS.OutletSystem.Outlet.[1].OutletID exists
void test_eaton_outlet_desc_default(void) {
    mockHost._usages.push_back(nutField("UPS.OutletSystem.Outlet.OutletID", 0x00840021, 0x0B, 3, 8, { 0, 0, 2 }));
    feed(0x0B, 3, 1);
    TEST_ASSERT_EQUAL_STRING("Main Outlet", ups_data.get("outlet.desc").c_str());
    std::vector<NutTable::RWVariable> rw;
    driver.writableVariables(ups_data, rw);
    TEST_ASSERT_EQUAL(1, rw.size());
    TEST_ASSERT_EQUAL_STRING("outlet.desc", rw[0].name);
    TEST_ASSERT_EQUAL(20, rw[0].aux);
}

// mge_format_model(): iProduct "Protection Station" + iModel "650" is MGE_PEGASUS, so
// outlet.power (pegasus_threshold_info) is published with ENUM 10/25/60. The row waits
// for the model before its first value counts.
void test_eaton_model_type_from_imodel(void) {
    mockHost._iProduct = 2;
    mockHost._usages.push_back(nutField("UPS.PowerSummary.0xFFFF00F0", 0xffff00f0, 0x01, 3));
    mockHost._usages.push_back(nutField("UPS.OutletSystem.Outlet.ConfigApparentPower", 0x00840043, 0x0C, 3, 8, { 0, 0, 2 }));

    feed(0x01, 3, 5);   // iModel string index
    feed(0x0C, 3, 25);  // before the model is known: deferred
    TEST_ASSERT_FALSE(ups_data.hasKey("outlet.power"));

    feedString(2, "Protection Station");
    feedString(5, "650");
    driver.loop(&mockHost, ups_data, 1000);
    TEST_ASSERT_TRUE(driver.nutContext().ready);
    TEST_ASSERT_EQUAL_HEX(NutTable::MGE_PEGASUS, driver.nutContext().mge_type);

    feed(0x0C, 3, 25);
    TEST_ASSERT_EQUAL_STRING("25", ups_data.get("outlet.power").c_str());
    std::vector<NutTable::RWVariable> rw;
    driver.writableVariables(ups_data, rw);
    TEST_ASSERT_EQUAL(1, rw.size());
    TEST_ASSERT_EQUAL_STRING("outlet.power", rw[0].name);
    TEST_ASSERT_EQUAL(3, rw[0].enums.size());
}

// mge_format_model(): an iModel string that never arrives counts as empty, so the model
// falls back to UPS.Flow.[4].ConfigApparentPower ("650" -> Protection Station 650 is
// MGE_PEGASUS). Decided at the end of the first full poll, upstream's initinfo.
void test_eaton_model_falls_back_to_apparent_power(void) {
    mockHost._iProduct = 2;
    mockHost._usages.push_back(nutField("UPS.PowerSummary.0xFFFF00F0", 0xffff00f0, 0x01, 3));
    mockHost._usages.push_back(nutField("UPS.Flow.ConfigApparentPower", 0x00840043, 0x0D, 3, 16, { 0, 5 }));
    feedString(2, "Protection Station");
    feed(0x01, 3, 5);    // iModel string index, whose string is never received
    uint8_t ap[] = { 0x0D, 0x8A, 0x02 }; // 650
    driver.decodeReport(&mockHost, 0x0D, 3, ap, sizeof(ap), ups_data);
    driver.postDecode(&mockHost, 0x0D, 3, ap, sizeof(ap), ups_data);

    uint32_t now = 1000;
    for (int i = 0; i < 500 && !driver.initialPollDone(); i++, now += 1000) driver.loop(&mockHost, ups_data, now);
    TEST_ASSERT_TRUE(driver.initialPollDone());
    driver.loop(&mockHost, ups_data, now);
    TEST_ASSERT_TRUE(driver.nutContext().ready);
    TEST_ASSERT_EQUAL_HEX(NutTable::MGE_PEGASUS, driver.nutContext().mge_type);
}

// The Eaton 3S of the fixtures reports iProduct "Eaton 3S": no mge_model_names row, so
// MGE_DEFAULT as upstream, and outlet.power is never published
void test_eaton_3s_model_is_default(void) {
    mockHost._iProduct = 2;
    mockHost._usages.push_back(nutField("UPS.OutletSystem.Outlet.ConfigApparentPower", 0x00840043, 0x0C, 3, 8, { 0, 0, 2 }));
    feedString(2, "Eaton 3S");
    driver.loop(&mockHost, ups_data, 1000);
    TEST_ASSERT_TRUE(driver.nutContext().ready);
    TEST_ASSERT_EQUAL_HEX(NutTable::MGE_DEFAULT, driver.nutContext().mge_type);
    feed(0x0C, 3, 25);
    TEST_ASSERT_FALSE(ups_data.hasKey("outlet.power"));
}

// --- mgeHid2Nut() read-only and BOOL rows (US-073) ---

// One status bit of FEATURE report 0x01
static HIDUsageDef statusField(const char* path, uint32_t usage, uint16_t bit) {
    HIDUsageDef d = nutField(path, usage, 0x01, 3, 1);
    d.bit_offset = bit;
    return d;
}

// The PowerSummary.PresentStatus bits of the Eaton 3S, in one report
static void addStatusFields() {
    mockHost._usages.push_back(statusField("UPS.PowerSummary.PresentStatus.ACPresent", 0x008500d0, 0));
    mockHost._usages.push_back(statusField("UPS.PowerSummary.PresentStatus.Charging", 0x00850044, 1));
    mockHost._usages.push_back(statusField("UPS.PowerSummary.PresentStatus.Discharging", 0x00850045, 2));
    mockHost._usages.push_back(statusField("UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", 0x00850042, 3));
    mockHost._usages.push_back(statusField("UPS.PowerSummary.PresentStatus.NeedReplacement", 0x0085004b, 4));
    mockHost._usages.push_back(statusField("UPS.PowerSummary.PresentStatus.Good", 0x00840061, 5));
    mockHost._usages.push_back(statusField("UPS.PowerSummary.PresentStatus.InternalFailure", 0x00840062, 6));
}

static const uint8_t AC = 0x01, CHRG = 0x02, DISCHRG = 0x04, LOWBATT = 0x08, REPLACE = 0x10, GOOD = 0x20;

static String status() { return UPSData::computeUPSStatusString(ups_data); }

// online_info, eaton_discharging_info (ABM unknown): mains lost, then back
void test_eaton_status_on_battery_and_back(void) {
    addStatusFields();
    feed(0x01, 3, DISCHRG | GOOD);
    TEST_ASSERT_EQUAL_STRING("0", ups_data.get("ups.status.ac_present").c_str());
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG", status().c_str());
    feed(0x01, 3, AC | GOOD);
    TEST_ASSERT_EQUAL_STRING("OL", status().c_str());
}

// lowbatt_info and replacebatt_info
void test_eaton_status_low_battery_and_replace(void) {
    addStatusFields();
    feed(0x01, 3, DISCHRG | LOWBATT | GOOD);
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG LB", status().c_str());
    feed(0x01, 3, AC | REPLACE | GOOD);
    TEST_ASSERT_EQUAL_STRING("OL RB", status().c_str());
}

// off_info is inverted (Good 0 = "off") and commfault_info replaces the old internal keys.
// OFF is not rendered yet: ups.status is the same as with Good = 1.
void test_eaton_status_good_is_off_info(void) {
    addStatusFields();
    feed(0x01, 3, AC | GOOD);
    TEST_ASSERT_EQUAL_STRING("0", ups_data.get("ups.status.off").c_str());
    TEST_ASSERT_EQUAL_STRING("0", ups_data.get("ups.status.commfault").c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.status.good"));
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.status.internal_failure"));
    feed(0x01, 3, AC);
    TEST_ASSERT_EQUAL_STRING("1", ups_data.get("ups.status.off").c_str());
    TEST_ASSERT_EQUAL_STRING("OL", status().c_str());
}

// eaton_abm_check_chrg_fun: CHRG while charging below 100 %, not at 100 %
void test_eaton_status_charging_below_full(void) {
    addStatusFields();
    ups_data.set("battery.charge", "80");
    feed(0x01, 3, AC | CHRG | GOOD);
    TEST_ASSERT_EQUAL_STRING("OL CHRG", status().c_str());
    ups_data.set("battery.charge", "100");
    feed(0x01, 3, AC | CHRG | GOOD);
    TEST_ASSERT_EQUAL_STRING("OL", status().c_str());
}

// mge_battery_capacity (%.2f Ah) and ADR 0011 rule 2: battery.capacity.full is not a NUT name
void test_eaton_capacity_full_never_published(void) {
    mockHost._usages.push_back(nutField("UPS.BatterySystem.Battery.DesignCapacity", 0x00850083, 0x23, 3, 16));
    mockHost._usages.push_back(nutField("UPS.BatterySystem.Battery.FullChargeCapacity", 0x00850067, 0x24, 3, 16));
    uint8_t design[] = { 0x23, 0x70, 0x62 };   // 25200 As
    uint8_t full[] = { 0x24, 0x70, 0x62 };
    decodeAll(0x23, 3, design, sizeof(design));
    decodeAll(0x24, 3, full, sizeof(full));
    TEST_ASSERT_EQUAL_STRING("7.00", ups_data.get("battery.capacity").c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("battery.capacity.full"));
}

// ADR 0011 rule 2: standard names upstream mge-hid does not read stay as extensions
void test_eaton_mfr_dates_kept(void) {
    mockHost._usages.push_back(nutField("UPS.PowerSummary.ManufacturerDate", 0x00850085, 0x25, 3, 16));
    mockHost._usages.push_back(nutField("UPS.Battery.ManufacturerDate", 0x00850085, 0x26, 3, 16));
    uint8_t ups_date[] = { 0x25, 0xB1, 0x50 };  // (2020-1980)<<9 | 5<<5 | 17 = 20657
    uint8_t batt_date[] = { 0x26, 0xB1, 0x50 };
    decodeAll(0x25, 3, ups_date, sizeof(ups_date));
    decodeAll(0x26, 3, batt_date, sizeof(batt_date));
    TEST_ASSERT_EQUAL_STRING("2020/05/17", ups_data.get("ups.mfr.date").c_str());
    TEST_ASSERT_EQUAL_STRING("2020/05/17", ups_data.get("battery.mfr.date").c_str());
}

// mge_battery_voltage_fun: UPS.PowerSummary.Voltage is battery.voltage only on some series
void test_eaton_battery_voltage_depends_on_series(void) {
    mockHost._iProduct = 2;
    mockHost._usages.push_back(nutField("UPS.PowerSummary.0xFFFF00F0", 0xffff00f0, 0x10, 3));
    mockHost._usages.push_back(nutField("UPS.PowerSummary.Voltage", 0x00840030, 0x0E, 3));
    feed(0x10, 3, 5);   // iModel string index
    feedString(2, "Eaton 3S");
    feedString(5, "700");
    driver.loop(&mockHost, ups_data, 1000);
    TEST_ASSERT_TRUE(driver.nutContext().ready);
    feed(0x0E, 3, 27);
    driver.completeInitialPoll();
    feed(0x0E, 3, 27);
    TEST_ASSERT_FALSE(ups_data.hasKey("battery.voltage"));

    driver.setup();
    ups_data = UPSData();
    feed(0x10, 3, 5);
    feedString(2, "Eaton 5P");
    feedString(5, "1550");
    driver.loop(&mockHost, ups_data, 1000);
    TEST_ASSERT_EQUAL_HEX(NutTable::EATON_5P, driver.nutContext().mge_type);
    feed(0x0E, 3, 27);
    TEST_ASSERT_EQUAL_STRING("27.0", ups_data.get("battery.voltage").c_str());
}

// stringid_conversion: battery.type from the iDeviceChemistry string
void test_eaton_battery_type_from_string_row(void) {
    mockHost._usages.push_back(nutField("UPS.PowerSummary.iDeviceChemistry", 0x00850089, 0x10, 3));
    feed(0x10, 3, 5);
    driver.loop(&mockHost, ups_data, 1000);
    TEST_ASSERT_EQUAL_UINT32(1, mockHost._requestedStrings.size());
    TEST_ASSERT_EQUAL_UINT8(5, mockHost._requestedStrings[0]);
    feedString(5, "PbAc");
    TEST_ASSERT_EQUAL_STRING("PbAc", ups_data.get("battery.type").c_str());
}

// ABM enabled (5P/9E): eaton_discharging_info gives "!dischrg" and the Charger.Mode row
// (eaton_abm_chrg_dischrg_info, mge-hid.c:2116) gives "dischrg". Upstream applies both in
// table order at every walk; here they live in different reports, and DISCHRG must not
// depend on which report came last.
void test_eaton_abm_discharging_whatever_the_report_order(void) {
    addStatusFields();
    mockHost._usages.push_back(nutField("UPS.BatterySystem.Charger.0xFFFF0087", 0xffff0087, 0x29, 3)); // ABMEnable
    mockHost._usages.push_back(nutField("UPS.BatterySystem.Charger.0xFFFF0094", 0xffff0094, 0x26, 3)); // Mode
    feed(0x29, 3, 1);           // ABM enabled
    feed(0x26, 3, 2);           // Mode 2: discharging (ABM_PATH_MODE)
    feed(0x01, 3, DISCHRG | GOOD);
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG", status().c_str());
    TEST_ASSERT_EQUAL_STRING("discharging", ups_data.get("battery.charger.status").c_str());
    feed(0x01, 3, DISCHRG | GOOD); // the next quick poll of the status report alone
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG", status().c_str());

    driver.setup();
    ups_data = UPSData();
    feed(0x29, 3, 1);
    feed(0x01, 3, DISCHRG | GOOD);
    feed(0x26, 3, 2);
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG", status().c_str());
    ups_data.set("battery.charge", "80"); // CHRG needs a charge below 100 % (computeUPSStatusString)
    feed(0x26, 3, 1);           // Mode 1: charging, mains back
    feed(0x01, 3, AC | CHRG | GOOD);
    TEST_ASSERT_EQUAL_STRING("charging", ups_data.get("battery.charger.status").c_str());
    TEST_ASSERT_EQUAL_STRING("OL CHRG", status().c_str());
}

// eaton_check_country_fun through the device.country row: never published
void test_eaton_country_from_table_row(void) {
    mockHost._usages.push_back(nutField("UPS.PowerSummary.0xFFFF0095", 0xffff0095, 0x15, 3));
    feed(0x15, 3, 0);
    TEST_ASSERT_TRUE(driver.nutContext().country_seen);
    TEST_ASSERT_EQUAL(NutTable::COUNTRY_EUROPE, driver.nutContext().country);
    TEST_ASSERT_FALSE(ups_data.hasKey("device.country"));
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_eaton_ac_present_and_discharging);
    RUN_TEST(test_eaton_voltage_and_battery);
    RUN_TEST(test_eaton_string_descriptors);
    RUN_TEST(test_eaton_loop_polling_and_string_requests);
    RUN_TEST(test_eaton_realpower_recalculated_when_config_arrives_after_load);
    RUN_TEST(test_eaton_battery_charge_low_owned_by_table);
    RUN_TEST(test_eaton_outlet_desc_default);
    RUN_TEST(test_eaton_model_type_from_imodel);
    RUN_TEST(test_eaton_model_falls_back_to_apparent_power);
    RUN_TEST(test_eaton_3s_model_is_default);
    RUN_TEST(test_eaton_status_on_battery_and_back);
    RUN_TEST(test_eaton_status_low_battery_and_replace);
    RUN_TEST(test_eaton_status_good_is_off_info);
    RUN_TEST(test_eaton_status_charging_below_full);
    RUN_TEST(test_eaton_capacity_full_never_published);
    RUN_TEST(test_eaton_mfr_dates_kept);
    RUN_TEST(test_eaton_battery_voltage_depends_on_series);
    RUN_TEST(test_eaton_battery_type_from_string_row);
    RUN_TEST(test_eaton_abm_discharging_whatever_the_report_order);
    RUN_TEST(test_eaton_country_from_table_row);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_eaton_ac_present_and_discharging);
    RUN_TEST(test_eaton_voltage_and_battery);
    RUN_TEST(test_eaton_string_descriptors);
    RUN_TEST(test_eaton_loop_polling_and_string_requests);
    RUN_TEST(test_eaton_realpower_recalculated_when_config_arrives_after_load);
    RUN_TEST(test_eaton_battery_charge_low_owned_by_table);
    RUN_TEST(test_eaton_outlet_desc_default);
    RUN_TEST(test_eaton_model_type_from_imodel);
    RUN_TEST(test_eaton_model_falls_back_to_apparent_power);
    RUN_TEST(test_eaton_3s_model_is_default);
    RUN_TEST(test_eaton_status_on_battery_and_back);
    RUN_TEST(test_eaton_status_low_battery_and_replace);
    RUN_TEST(test_eaton_status_good_is_off_info);
    RUN_TEST(test_eaton_status_charging_below_full);
    RUN_TEST(test_eaton_capacity_full_never_published);
    RUN_TEST(test_eaton_mfr_dates_kept);
    RUN_TEST(test_eaton_battery_voltage_depends_on_series);
    RUN_TEST(test_eaton_battery_type_from_string_row);
    RUN_TEST(test_eaton_abm_discharging_whatever_the_report_order);
    RUN_TEST(test_eaton_country_from_table_row);
    UNITY_END();
}
void loop() {}
#endif
#endif

