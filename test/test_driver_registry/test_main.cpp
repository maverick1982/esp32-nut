#include <unity.h>
#include <string.h>
#include <memory>
#include <vector>
#include "DriverRegistry.h"
#include "IUPSDriver.h"

// US-132: family chosen before the report descriptor (ADR 0013, section 2):
// explicit HID rows -> Q* rows (qx_usb_id[] of nut_repo/drivers/nutdrv_qx.c) -> generic HID

using DriverRegistry::Family;

void setUp(void) {}
void tearDown(void) {}

static void assertQx(const char* transport, uint16_t vid, uint16_t pid, const char* mfr, const char* product) {
    DriverRegistry::Match m = DriverRegistry::match(vid, pid, mfr, product);
    char msg[96];
    snprintf(msg, sizeof(msg), "%04x:%04x \"%s\"/\"%s\"", vid, pid, mfr ? mfr : "(null)", product ? product : "(null)");
    TEST_ASSERT_TRUE_MESSAGE(m.family == Family::QX, msg);
    TEST_ASSERT_FALSE_MESSAGE(m.explicit_hid, msg);
    TEST_ASSERT_NOT_NULL_MESSAGE(m.qx_transport, msg);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(transport, m.qx_transport, msg);
}

static void assertExplicitHid(uint16_t vid, uint16_t pid, const char* driver_name,
                              const char* mfr = "", const char* product = "") {
    DriverRegistry::Match m = DriverRegistry::match(vid, pid, mfr, product);
    TEST_ASSERT_TRUE(m.family == Family::HID);
    TEST_ASSERT_TRUE(m.explicit_hid);
    TEST_ASSERT_NULL(m.qx_transport);
    std::unique_ptr<IUPSDriver> d(DriverRegistry::create(vid, pid, mfr, product));
    TEST_ASSERT_EQUAL_STRING(driver_name, d->getDriverName());
}

static HIDUsageDef usage(uint32_t value) {
    HIDUsageDef u;
    u.usage = value;
    return u;
}

// --- Cases of the story ---------------------------------------------------------------

// #91: VIDA Armour 1200VA, no manufacturer string
void test_issue91_mec0003_without_manufacturer_is_qx_hunnox(void) {
    assertQx("hunnox", 0x0001, 0x0000, "", "MEC0003");
    assertQx("hunnox", 0x0001, 0x0000, nullptr, "MEC0003");
}

// #28: Fideltronik/MEC LUPUS
void test_issue28_mec_mec0003_is_qx_fabula(void) {
    assertQx("fabula", 0x0001, 0x0000, "MEC", "MEC0003");
}

void test_apc_is_explicit_hid(void) {
    assertExplicitHid(0x051D, 0x0002, "APCDriver");
}

void test_phoenixtec_ffff_is_explicit_hid_liebert(void) {
    assertExplicitHid(0x06DA, 0xFFFF, "LiebertDriver");
}

void test_unknown_vid_with_ups_page_falls_back_to_generic(void) {
    DriverRegistry::Match m = DriverRegistry::match(0x1234, 0x5678, "Acme", "UPS");
    TEST_ASSERT_TRUE(m.family == Family::HID);
    TEST_ASSERT_FALSE(m.explicit_hid);
    TEST_ASSERT_NULL(m.qx_transport);
    std::vector<HIDUsageDef> usages = { usage(0x00010001), usage(0x00840002) };
    TEST_ASSERT_TRUE(DriverRegistry::hasUpsPages(usages));
    std::unique_ptr<IUPSDriver> d(DriverRegistry::create(0x1234, 0x5678, "Acme", "UPS"));
    TEST_ASSERT_EQUAL_STRING("GenericDriver", d->getDriverName());
}

void test_unknown_vid_without_pages_is_ignored(void) {
    DriverRegistry::Match m = DriverRegistry::match(0x1234, 0x5678);
    TEST_ASSERT_TRUE(m.family == Family::HID);
    TEST_ASSERT_FALSE(m.explicit_hid);
    std::vector<HIDUsageDef> usages = { usage(0x00010006), usage(0xFF000001) };
    TEST_ASSERT_FALSE(DriverRegistry::hasUpsPages(usages));
}

// --- String comparison of qx_is_usb_device_supported() --------------------------------

void test_strings_compare_without_case(void) {
    assertQx("fabula", 0x0001, 0x0000, "mec", "mec0003");
}

void test_atcl_strings_are_fuji(void) {
    assertQx("fuji", 0x0001, 0x0000, "ATCL FOR UPS", "ATCL FOR UPS");
}

void test_0001_without_strings_is_krauler(void) {
    assertQx("krauler", 0x0001, 0x0000, "", "");
    assertQx("krauler", 0x0001, 0x0000, nullptr, nullptr);
    assertQx("krauler", 0x0001, 0x0000, "Other", "Thing");
}

// Issue #91 (qx-hunnox-v1 log of 2026-11-11): ESP-IDF read no string from the VIDA, the
// claim took krauler. The host reads the strings itself only where they decide the row.
void test_qx_needs_strings_only_for_shared_ids(void) {
    TEST_ASSERT_TRUE(DriverRegistry::qxNeedsStrings(0x0001, 0x0000));  // fabula/hunnox/fuji/krauler/snr
    TEST_ASSERT_TRUE(DriverRegistry::qxNeedsStrings(0x06DA, 0x0002));  // phoenixtec / cypress
    TEST_ASSERT_FALSE(DriverRegistry::qxNeedsStrings(0x0665, 0x5161)); // cypress, no strings
    TEST_ASSERT_FALSE(DriverRegistry::qxNeedsStrings(0x1CB0, 0x0035)); // krauler, no strings
    TEST_ASSERT_FALSE(DriverRegistry::qxNeedsStrings(0x051D, 0x0002)); // APC: HID, no Q* row
    TEST_ASSERT_FALSE(DriverRegistry::qxNeedsStrings(0x0463, 0xFFFF)); // Eaton: HID
}

void test_issue91_product_read_by_host_is_hunnox(void) {
    assertQx("krauler", 0x0001, 0x0000, "", "");       // strings left empty by the enumeration
    assertQx("hunnox", 0x0001, 0x0000, "", "MEC0003"); // product read as NUT does
}

// --- Explicit HID rows against the Q* rows of the same vendors ------------------------

void test_legrand_keor_stays_hid_and_daker_is_qx(void) {
    assertExplicitHid(0x1CB0, 0x0032, "LegrandDriver");
    assertExplicitHid(0x1CB0, 0x0038, "LegrandDriver");
    assertQx("krauler", 0x1CB0, 0x0035, "", "");
}

// The CH340 row of NUT has no USB subdriver: not copied
void test_qinheng_ch340_is_not_qx(void) {
    DriverRegistry::Match m = DriverRegistry::match(0x1A86, 0x7523, "", "USB Serial");
    TEST_ASSERT_TRUE(m.family == Family::HID);
    TEST_ASSERT_FALSE(m.explicit_hid);
    TEST_ASSERT_NULL(m.qx_transport);
}

// --- Every row of QX_USB_ID[] ---------------------------------------------------------

struct QxCase {
    uint16_t vid;
    uint16_t pid;
    const char* mfr;
    const char* product;
    const char* transport;
};

static const QxCase QX_CASES[] = {
    { 0x05B8, 0x0000, "", "", "cypress" },     // Agiler UPS
    { 0xFFFF, 0x0000, "", "", "ablerex" },     // Ablerex 625L USB
    { 0x1CB0, 0x0035, "", "", "krauler" },     // Legrand Daker DK / DK Plus
    { 0x0665, 0x5161, "", "", "cypress" },     // Belkin F6C1200-UNV / Voltronic Power
    { 0x06DA, 0x0002, "Phoenixtec Power", "USB Cable (V2.00)", "phoenixtec" }, // Masterguard A Series
    { 0x06DA, 0x0002, "", "", "cypress" },     // Online Yunto YQ450
    { 0x06DA, 0x0002, "Phoenixtec Power", "Other", "cypress" }, // product string differs
    { 0x06DA, 0x0003, "", "", "ippon" },       // Mustek Powermust
    { 0x06DA, 0x0004, "", "", "cypress" },     // Phoenixtec Innova 3/1 T
    { 0x06DA, 0x0005, "", "", "cypress" },     // Phoenixtec Innova RT
    { 0x06DA, 0x0201, "", "", "cypress" },     // Phoenixtec Innova T
    { 0x06DA, 0x0601, "", "", "phoenix" },     // Online Zinto A
    { 0x0F03, 0x0001, "", "", "cypress" },     // Unitek Alpha 1200Sx
    { 0x14F0, 0x00C9, "", "", "phoenix" },     // GE EP series
    { 0x0483, 0x0035, "", "", "sgs" },         // TS Shara
    { 0x0590, 0x00B7, "", "", "omron" },       // OMRON BN150T
    { 0x0001, 0x0000, "MEC", "MEC0003", "fabula" },             // Fideltronik/MEC LUPUS 500 USB
    { 0x0001, 0x0000, "", "MEC0003", "hunnox" },                // Hunnox HNX 850
    { 0x0001, 0x0000, "ATCL FOR UPS", "ATCL FOR UPS", "fuji" }, // Fuji
    { 0x0001, 0x0000, "", "", "krauler" },     // Krauler UP-M500VA
    // SNR-UPS-LID-XXXX: krauler (NULL strings) comes first in NUT, so snr is never reached
    { 0x0001, 0x0000, "SNR", "MEC0003", "hunnox" },
    { 0x0925, 0x1234, "", "", "armac" },       // Armac
};

void test_every_qx_row_gives_its_transport(void) {
    for (const auto& c : QX_CASES) assertQx(c.transport, c.vid, c.pid, c.mfr, c.product);
}

// No explicit HID row hides a Q* row: a new HID row that does breaks this test
void test_no_explicit_hid_row_hides_a_qx_row(void) {
    for (const auto& c : QX_CASES) {
        DriverRegistry::Match m = DriverRegistry::match(c.vid, c.pid, c.mfr, c.product);
        char msg[32];
        snprintf(msg, sizeof(msg), "%04x:%04x", c.vid, c.pid);
        TEST_ASSERT_FALSE_MESSAGE(m.explicit_hid, msg);
        TEST_ASSERT_TRUE_MESSAGE(m.family == Family::QX, msg);
    }
}

// A Q* VID with a PID outside the table falls back to HID (generic, or explicit for 06da:ffff)
void test_qx_vendor_with_other_pid_is_hid(void) {
    DriverRegistry::Match m = DriverRegistry::match(0x0665, 0x0001);
    TEST_ASSERT_TRUE(m.family == Family::HID);
    TEST_ASSERT_FALSE(m.explicit_hid);
}

// --- hasUpsPages() --------------------------------------------------------------------

void test_has_ups_pages_with_ups_page(void) {
    std::vector<HIDUsageDef> usages = { usage(0x00840030) };
    TEST_ASSERT_TRUE(DriverRegistry::hasUpsPages(usages));
}

void test_has_ups_pages_with_battery_page_only(void) {
    std::vector<HIDUsageDef> usages = { usage(0x00010001), usage(0x00850066) };
    TEST_ASSERT_TRUE(DriverRegistry::hasUpsPages(usages));
}

void test_has_ups_pages_false_with_other_pages(void) {
    std::vector<HIDUsageDef> usages = { usage(0x00010001), usage(0xFF000001), usage(0x00860001) };
    TEST_ASSERT_FALSE(DriverRegistry::hasUpsPages(usages));
}

void test_has_ups_pages_false_when_empty(void) {
    std::vector<HIDUsageDef> usages;
    TEST_ASSERT_FALSE(DriverRegistry::hasUpsPages(usages));
}

#ifdef PIO_UNIT_TESTING
static void runAll() {
    RUN_TEST(test_issue91_mec0003_without_manufacturer_is_qx_hunnox);
    RUN_TEST(test_issue28_mec_mec0003_is_qx_fabula);
    RUN_TEST(test_apc_is_explicit_hid);
    RUN_TEST(test_phoenixtec_ffff_is_explicit_hid_liebert);
    RUN_TEST(test_unknown_vid_with_ups_page_falls_back_to_generic);
    RUN_TEST(test_unknown_vid_without_pages_is_ignored);
    RUN_TEST(test_strings_compare_without_case);
    RUN_TEST(test_atcl_strings_are_fuji);
    RUN_TEST(test_0001_without_strings_is_krauler);
    RUN_TEST(test_qx_needs_strings_only_for_shared_ids);
    RUN_TEST(test_issue91_product_read_by_host_is_hunnox);
    RUN_TEST(test_legrand_keor_stays_hid_and_daker_is_qx);
    RUN_TEST(test_qinheng_ch340_is_not_qx);
    RUN_TEST(test_every_qx_row_gives_its_transport);
    RUN_TEST(test_no_explicit_hid_row_hides_a_qx_row);
    RUN_TEST(test_qx_vendor_with_other_pid_is_hid);
    RUN_TEST(test_has_ups_pages_with_ups_page);
    RUN_TEST(test_has_ups_pages_with_battery_page_only);
    RUN_TEST(test_has_ups_pages_false_with_other_pages);
    RUN_TEST(test_has_ups_pages_false_when_empty);
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
