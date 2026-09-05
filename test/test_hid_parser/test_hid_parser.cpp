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
    bool success = parser.parseReportDescriptor(desc, sizeof(desc));
    TEST_ASSERT_TRUE(success);
    
    const HIDUsageDef* usage = parser.getUsageDef(HID_USAGE_UPS_ACPRAESENT);
    TEST_ASSERT_NOT_NULL(usage);
    TEST_ASSERT_EQUAL_UINT8(1, usage->report_id);
    TEST_ASSERT_EQUAL_UINT16(0, usage->bit_offset);
    TEST_ASSERT_EQUAL_UINT16(1, usage->bit_size);
}

void test_extract_usage_aligned(void) {
    HIDUsageDef def;
    def.usage = HID_USAGE_UPS_ACPRAESENT;
    def.report_id = 0x01;
    def.report_type = 1;
    def.bit_offset = 0;
    def.bit_size = 1;
    def.exponent = 0;
    def.unit = 0;
    def.found = true;
    
    // With report ID prefix: Report 1, value 1 at bit 0
    uint8_t data[] = { 0x01, 0x01 };
    double val = HIDParser::extractUsage(&def, 0x01, data, sizeof(data));
    TEST_ASSERT_EQUAL_FLOAT(1.0, val);

    // Value 0 at bit 0
    uint8_t data_zero[] = { 0x01, 0x00 };
    val = HIDParser::extractUsage(&def, 0x01, data_zero, sizeof(data_zero));
    TEST_ASSERT_EQUAL_FLOAT(0.0, val);
}

void test_extract_unaligned_bitfields(void) {
    // Usage located at bit offset 3, length 5 bits (value within byte 0)
    HIDUsageDef def;
    def.usage = 0x00840040; // PercentLoad
    def.report_id = 0x05;
    def.report_type = 1;
    def.bit_offset = 3;
    def.bit_size = 5;
    def.exponent = 0;
    def.unit = 0;
    def.found = true;

    // Report ID: 0x05, Byte: (0x19 << 3) | 0x07 = 0xCF (Payload contains 25 at bits 3..7)
    uint8_t data[] = { 0x05, (uint8_t)((25 << 3) | 0x07) };
    double val = HIDParser::extractUsage(&def, 0x05, data, sizeof(data));
    TEST_ASSERT_EQUAL_FLOAT(25.0, val);

    // Usage spanning across 2 bytes: bit offset 6, length 8 bits (crosses byte 0 and byte 1)
    HIDUsageDef def_cross;
    def_cross.usage = 0x00840030; // Voltage
    def_cross.report_id = 0x0A;
    def_cross.report_type = 1;
    def_cross.bit_offset = 6;
    def_cross.bit_size = 8;
    def_cross.exponent = 0;
    def_cross.unit = 0;
    def_cross.found = true;

    // Target value: 230 (0xE6). Shifted by 6 bits across byte 0 and byte 1
    // Byte 0 low 6 bits = garbage (0x3F), high 2 bits = low 2 bits of 230 (0xE6 & 3 = 2) -> (2 << 6) | 0x3F = 0xBF
    // Byte 1 low 6 bits = high 6 bits of 230 (0xE6 >> 2 = 57) -> 57 = 0x39
    uint8_t data_cross[] = { 0x0A, 0xBF, 0x39 };
    double val_cross = HIDParser::extractUsage(&def_cross, 0x0A, data_cross, sizeof(data_cross));
    TEST_ASSERT_EQUAL_FLOAT(230.0, val_cross);
}

void test_extract_exponent_and_unit_scaling(void) {
    // 1. Exponent -1 (e.g. 2305 * 10^-1 = 230.5V)
    HIDUsageDef def_expo;
    def_expo.usage = 0x00840030;
    def_expo.report_id = 0x10;
    def_expo.report_type = 1;
    def_expo.bit_offset = 0;
    def_expo.bit_size = 16;
    def_expo.exponent = -1;
    def_expo.unit = 0;
    def_expo.found = true;

    // 2305 = 0x0901 -> Little endian: 0x01, 0x09
    uint8_t data_expo[] = { 0x10, 0x01, 0x09 };
    double val_expo = HIDParser::extractUsage(&def_expo, 0x10, data_expo, sizeof(data_expo));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 230.5, val_expo);

    // 2. Standard HID PDC Volt Unit (0x00F0D121 with raw value 230 * 10^7 standard Volt unit)
    HIDUsageDef def_volt_unit;
    def_volt_unit.usage = 0x00840030;
    def_volt_unit.report_id = 0x11;
    def_volt_unit.report_type = 1;
    def_volt_unit.bit_offset = 0;
    def_volt_unit.bit_size = 16;
    def_volt_unit.exponent = 7; // Declared exponent is 7, with unit 0x00F0D121 unit_expo becomes 7 - 7 = 0
    def_volt_unit.unit = 0x00F0D121;
    def_volt_unit.found = true;

    // 230 = 0x00E6 -> Little endian: 0xE6, 0x00
    uint8_t data_volt[] = { 0x11, 0xE6, 0x00 };
    double val_volt = HIDParser::extractUsage(&def_volt_unit, 0x11, data_volt, sizeof(data_volt));
    TEST_ASSERT_EQUAL_FLOAT(230.0, val_volt);
}

void test_report_id_mismatch_and_no_report_id(void) {
    HIDUsageDef def;
    def.usage = 0x00840030;
    def.report_id = 0x02; // Expected Report ID: 2
    def.report_type = 1;
    def.bit_offset = 0;
    def.bit_size = 8;
    def.exponent = 0;
    def.unit = 0;
    def.found = true;

    // Buffer arrives with Report ID 0x01 -> must be discarded (return 0.0)
    uint8_t data_mismatch[] = { 0x01, 0xFF };
    double val_mismatch = HIDParser::extractUsage(&def, 0x02, data_mismatch, sizeof(data_mismatch));
    TEST_ASSERT_EQUAL_FLOAT(0.0, val_mismatch);

    // Device with NO Report ID (def.report_id = 0)
    HIDUsageDef def_no_id;
    def_no_id.usage = 0x00840030;
    def_no_id.report_id = 0;
    def_no_id.report_type = 1;
    def_no_id.bit_offset = 0;
    def_no_id.bit_size = 8;
    def_no_id.exponent = 0;
    def_no_id.unit = 0;
    def_no_id.found = true;

    // Direct payload without Report ID byte
    uint8_t data_raw[] = { 120 };
    double val_raw = HIDParser::extractUsage(&def_no_id, 0, data_raw, sizeof(data_raw));
    TEST_ASSERT_EQUAL_FLOAT(120.0, val_raw);
}

void test_extract_short_report_tolerance(void) {
    HIDUsageDef def;
    def.usage = 0x00840030; // Voltage
    def.report_id = 0x20;
    def.report_type = 1;
    def.bit_offset = 0;
    def.bit_size = 32;
    def.exponent = 0;
    def.unit = 0;
    def.found = true;
    
    // Buggy UPS sends only 2 bytes of payload for a 32-bit field.
    // Length is 3 (1 byte report ID + 2 bytes payload)
    // Values: ID=0x20, payload=0xFC, 0x08 (which is 2300 or 0x08FC in little endian)
    uint8_t data[] = { 0x20, 0xFC, 0x08 };
    double val = HIDParser::extractUsage(&def, 0x20, data, sizeof(data));
    TEST_ASSERT_EQUAL_FLOAT(2300.0, val);
}

void test_null_or_corrupted_buffer_tolerance(void) {
    HIDUsageDef def;
    def.found = true;
    def.bit_size = 8;

    // NULL pointer
    TEST_ASSERT_EQUAL_FLOAT(0.0, HIDParser::extractUsage(&def, 0x01, nullptr, 10));

    // NULL def
    uint8_t dummy[] = { 1, 2, 3 };
    TEST_ASSERT_EQUAL_FLOAT(0.0, HIDParser::extractUsage(nullptr, 0x01, dummy, 3));

    // def->found = false
    def.found = false;
    TEST_ASSERT_EQUAL_FLOAT(0.0, HIDParser::extractUsage(&def, 0x01, dummy, 3));

    // Zero length
    def.found = true;
    TEST_ASSERT_EQUAL_FLOAT(0.0, HIDParser::extractUsage(&def, 0x01, dummy, 0));
}

void test_has_feature_beeper_control(void) {
    // 1. Descriptor with AudibleAlarmControl in Feature Report (0xB1 = Feature)
    const uint8_t desc_feature_beeper[] = {
        0x05, 0x84, // Usage Page (UPS)
        0x09, 0x04, // Usage (UPS)
        0xA1, 0x01, // Collection (Application)
        0x85, 0x13, // Report ID (19)
        0x09, 0x5A, // Usage (AudibleAlarmControl)
        0x75, 0x08, // Report Size (8)
        0x95, 0x01, // Report Count (1)
        0xB1, 0x02, // Feature (Data,Var,Abs)
        0xC0        // End Collection
    };
    HIDParser parser1;
    TEST_ASSERT_TRUE(parser1.parseReportDescriptor(desc_feature_beeper, sizeof(desc_feature_beeper)));
    TEST_ASSERT_TRUE(parser1.hasFeatureBeeperControl());

    // 2. Descriptor with AudibleAlarmControl in Input Report only (0x81 = Input)
    const uint8_t desc_input_beeper[] = {
        0x05, 0x84, // Usage Page (UPS)
        0x09, 0x04, // Usage (UPS)
        0xA1, 0x01, // Collection (Application)
        0x85, 0x01, // Report ID (1)
        0x09, 0x5A, // Usage (AudibleAlarmControl)
        0x75, 0x08, // Report Size (8)
        0x95, 0x01, // Report Count (1)
        0x81, 0x02, // Input (Data,Var,Abs)
        0xC0        // End Collection
    };
    HIDParser parser2;
    TEST_ASSERT_TRUE(parser2.parseReportDescriptor(desc_input_beeper, sizeof(desc_input_beeper)));
    TEST_ASSERT_FALSE(parser2.hasFeatureBeeperControl());

    // 3. Descriptor without AudibleAlarmControl
    const uint8_t desc_no_beeper[] = {
        0x05, 0x84, // Usage Page (UPS)
        0x09, 0x04, // Usage (UPS)
        0xA1, 0x01, // Collection (Application)
        0x85, 0x01, // Report ID (1)
        0x09, 0xD0, // Usage (AC Present)
        0x75, 0x01, // Report Size (1)
        0x95, 0x01, // Report Count (1)
        0x81, 0x02, // Input (Data,Var,Abs)
        0xC0        // End Collection
    };
    HIDParser parser3;
    TEST_ASSERT_TRUE(parser3.parseReportDescriptor(desc_no_beeper, sizeof(desc_no_beeper)));
    TEST_ASSERT_FALSE(parser3.hasFeatureBeeperControl());
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_hid_parser_basic);
    RUN_TEST(test_extract_usage_aligned);
    RUN_TEST(test_extract_unaligned_bitfields);
    RUN_TEST(test_extract_exponent_and_unit_scaling);
    RUN_TEST(test_report_id_mismatch_and_no_report_id);
    RUN_TEST(test_extract_short_report_tolerance);
    RUN_TEST(test_null_or_corrupted_buffer_tolerance);
    RUN_TEST(test_has_feature_beeper_control);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_hid_parser_basic);
    RUN_TEST(test_extract_usage_aligned);
    RUN_TEST(test_extract_unaligned_bitfields);
    RUN_TEST(test_extract_exponent_and_unit_scaling);
    RUN_TEST(test_report_id_mismatch_and_no_report_id);
    RUN_TEST(test_extract_short_report_tolerance);
    RUN_TEST(test_null_or_corrupted_buffer_tolerance);
    RUN_TEST(test_has_feature_beeper_control);
    UNITY_END();
}
void loop() {}
#endif
#endif

