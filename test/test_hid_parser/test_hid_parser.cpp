#include <unity.h>
#include "HIDParser.h"

void setUp(void) {}
void tearDown(void) {}

void test_hid_parser_basic(void) {
    // A simple HID report descriptor for testing
    const uint8_t desc[] = {
        0x05, 0x84, // Usage Page (UPS)
        0x09, 0x04, // Usage (UPS)
        0xA1, 0x01, // Collection (Application)
        0x85, 0x01, // Report ID (1)
        0x09, 0x02, // Usage (Present Status)
        0xA1, 0x02, // Collection (Logical)
        0x09, 0xD0, // Usage (AC Present)
        0x75, 0x01, // Report Size (1)
        0x95, 0x01, // Report Count (1)
        0x81, 0x02, // Input (Data,Var,Abs)
        0xC0,       // End Collection
        0xC0        // End Collection
    };
    
    HIDParser parser;
    parser.parseReportDescriptor(desc, sizeof(desc));
    
    const HIDUsageDef* usage = parser.getUsageDef(HID_USAGE_UPS_ACPRAESENT);
    TEST_ASSERT_NOT_NULL(usage);
    TEST_ASSERT_EQUAL_UINT8(1, usage->report_id);
    TEST_ASSERT_EQUAL_UINT16(0, usage->bit_offset);
    TEST_ASSERT_EQUAL_UINT16(1, usage->bit_size);
}

void test_extract_usage(void) {
    HIDUsageDef def;
    def.usage = HID_USAGE_UPS_ACPRAESENT;
    def.report_id = 0x01;
    def.bit_offset = 0;
    def.bit_size = 1;
    def.type = 0x81;
    
    // With report ID prefix
    uint8_t data[] = { 0x01, 0x01 }; // report_id=1, val=1 (bit 0)
    int32_t val = HIDParser::extractUsage(&def, 0x01, data, sizeof(data));
    TEST_ASSERT_EQUAL(1, val);
    
    // Without report ID prefix (though in our implementation length>1 and data[0]==report_id checks for prefix)
    uint8_t data2[] = { 0x01 }; // This would be interpreted as report_id=1 if report_id=1 was expected, wait...
    // Our implementation does: if (length > 1 && data[0] == report_id) bit_offset += 8;
    // So if length == 1, it assumes no prefix.
    uint8_t data3[] = { 0x01 }; 
    int32_t val3 = HIDParser::extractUsage(&def, 0x02, data3, sizeof(data3)); // Not prefix because data[0] != report_id (0x01 != 0x02)
    TEST_ASSERT_EQUAL(1, val3);
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_hid_parser_basic);
    RUN_TEST(test_extract_usage);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_hid_parser_basic);
    RUN_TEST(test_extract_usage);
    UNITY_END();
}
void loop() {}
#endif
#endif
