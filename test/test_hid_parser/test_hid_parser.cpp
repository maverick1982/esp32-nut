#include <unity.h>
#include "HIDParser.h"
#include "NUTUsages.h"

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

#include <fstream>
#include <sstream>
#include <ArduinoJson.h>

void test_cyberpower_br700elcd_beeper(void) {
    std::ifstream file("test/fixtures/cyberpower/cyberpower_br700elcd_vid0764_pid0501.json");
    TEST_ASSERT_TRUE_MESSAGE(file.is_open(), "Fixture file must exist");

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
    
    HIDParser parser;
    TEST_ASSERT_TRUE(parser.parseReportDescriptor(rawDesc.data(), rawDesc.size()));
    TEST_ASSERT_TRUE(parser.hasFeatureBeeperControl());
    
    // Check that AudibleAlarmControl is parsed correctly
    const HIDUsageDef* usage = parser.getUsageDef(0x0084005A); // AudibleAlarmControl
    TEST_ASSERT_NOT_NULL(usage);
    TEST_ASSERT_EQUAL_UINT8(128, usage->report_id); // 0x80
    TEST_ASSERT_EQUAL_UINT8(3, usage->report_type); // Feature = 3
    TEST_ASSERT_EQUAL_UINT16(0, usage->bit_offset);
    TEST_ASSERT_EQUAL_UINT16(8, usage->bit_size);
    
    // Calculate expected length for report 128
    uint16_t max_bit_bound = 0;
    for (const auto& u : parser.getUsages()) {
        if (u.report_id == usage->report_id && u.report_type == usage->report_type) {
            if (u.bit_offset + u.bit_size > max_bit_bound) {
                max_bit_bound = u.bit_offset + u.bit_size;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT16(8, max_bit_bound); // Only 1 byte for data
    uint16_t expected_length = (max_bit_bound + 7) / 8 + 1; // 1 byte data + 1 byte ID = 2 bytes
    TEST_ASSERT_EQUAL_UINT16(2, expected_length);
}

// Review A4: declared INPUT lengths drive the reassembly of multi-packet reports
void test_input_length_and_report_ids(void) {
    const uint8_t desc[] = {
        0x05, 0x84,       // Usage Page (Power Device)
        0x09, 0x04,       // Usage (UPS)
        0xA1, 0x01,       // Collection (Application)
        0x85, 0x21,       //   Report ID 0x21
        0x09, 0x30,       //   Usage (Voltage)
        0x75, 0x08,       //   Report Size 8
        0x95, 0x14,       //   Report Count 20
        0x81, 0x02,       //   Input
        0xC0              // End Collection
    };
    HIDParser parser;
    TEST_ASSERT_TRUE(parser.parseReportDescriptor(desc, sizeof(desc)));
    TEST_ASSERT_TRUE(parser.usesReportIds());
    TEST_ASSERT_EQUAL_UINT16(21, parser.getInputLength(0x21)); // 20 bytes + report ID
    TEST_ASSERT_EQUAL_UINT16(0, parser.getInputLength(0x22));  // unknown: no guess
}

void test_input_length_without_report_ids(void) {
    const uint8_t desc[] = {
        0x05, 0x84, 0x09, 0x04, 0xA1, 0x01,
        0x09, 0x30, 0x75, 0x08, 0x95, 0x0A, 0x81, 0x02, // 10 bytes, no report ID
        0xC0
    };
    HIDParser parser;
    TEST_ASSERT_TRUE(parser.parseReportDescriptor(desc, sizeof(desc)));
    TEST_ASSERT_FALSE(parser.usesReportIds());
    TEST_ASSERT_EQUAL_UINT16(10, parser.getInputLength(0));
}

// Review M1: Logical Minimum < 0 means a signed field (e.g. discharge current)
void test_signed_field_sign_extended(void) {
    const uint8_t desc[] = {
        0x05, 0x84, 0x09, 0x04, 0xA1, 0x01,
        0x85, 0x01,
        0x09, 0x31,             // Usage (Current)
        0x16, 0x00, 0x80,       // Logical Minimum (-32768)
        0x26, 0xFF, 0x7F,       // Logical Maximum (32767)
        0x75, 0x10, 0x95, 0x01, 0xB1, 0x02, // 16 bits, Feature
        0x09, 0x30,             // Usage (Voltage)
        0x15, 0x00,             // Logical Minimum (0)
        0x26, 0xFF, 0x00,       // Logical Maximum (255)
        0x75, 0x08, 0x95, 0x01, 0xB1, 0x02, // 8 bits, Feature
        0xC0
    };
    HIDParser parser;
    parser.parseReportDescriptor(desc, sizeof(desc));
    const HIDUsageDef* current = parser.getUsageDef(0x00840031);
    const HIDUsageDef* voltage = parser.getUsageDef(0x00840030);
    TEST_ASSERT_NOT_NULL(current);
    TEST_ASSERT_NOT_NULL(voltage);
    TEST_ASSERT_EQUAL_INT32(-32768, current->logical_min);
    TEST_ASSERT_EQUAL_INT32(32767, current->logical_max);

    const uint8_t report[] = {0x01, 0xF6, 0xFF, 0xF0}; // current = -10, voltage = 240
    TEST_ASSERT_EQUAL_FLOAT(-10.0, HIDParser::extractUsage(current, 1, report, sizeof(report)));
    // Logical Minimum 0: the top bit is magnitude, not sign
    TEST_ASSERT_EQUAL_FLOAT(240.0, HIDParser::extractUsage(voltage, 1, report, sizeof(report)));
}

void test_unsigned_logical_max_in_one_byte(void) {
    // "Logical Maximum (255)" sent as 0x25 0xFF is -1 as a signed byte: read it as 255
    const uint8_t desc[] = {
        0x05, 0x84, 0x09, 0x04, 0xA1, 0x01,
        0x09, 0x30, 0x15, 0x00, 0x25, 0xFF, 0x75, 0x08, 0x95, 0x01, 0xB1, 0x02,
        0xC0
    };
    HIDParser parser;
    parser.parseReportDescriptor(desc, sizeof(desc));
    const HIDUsageDef* voltage = parser.getUsageDef(0x00840030);
    TEST_ASSERT_NOT_NULL(voltage);
    TEST_ASSERT_EQUAL_INT32(0, voltage->logical_min);
    TEST_ASSERT_EQUAL_INT32(255, voltage->logical_max);
}

void test_wide_field_clamped_to_32_bits(void) {
    HIDUsageDef def;
    def.found = true;
    def.report_id = 0;
    def.bit_offset = 0;
    def.bit_size = 64; // shifting by 64 was undefined behaviour
    const uint8_t report[] = {0x78, 0x56, 0x34, 0x12, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_FLOAT((double)0x12345678, HIDParser::extractUsage(&def, 0, report, sizeof(report)));
}

void test_long_item_skipped(void) {
    const uint8_t desc[] = {
        0x05, 0x84, 0x09, 0x04, 0xA1, 0x01,
        0xFE, 0x03, 0x10, 0x81, 0x02, 0x09, // long item: 3 data bytes that look like items
        0x09, 0x30, 0x75, 0x08, 0x95, 0x01, 0xB1, 0x02,
        0xC0
    };
    HIDParser parser;
    parser.parseReportDescriptor(desc, sizeof(desc));
    TEST_ASSERT_EQUAL(1, parser.getUsages().size());
    TEST_ASSERT_NOT_NULL(parser.getUsageDef(0x00840030));
}

void test_usage_minimum_maximum_expanded(void) {
    const uint8_t desc[] = {
        0x05, 0x84, 0x09, 0x04, 0xA1, 0x01,
        0x05, 0x85,             // Usage Page (Battery System)
        0x19, 0xD0,             // Usage Minimum (ACPresent)
        0x29, 0xD2,             // Usage Maximum (0xD2)
        0x75, 0x01, 0x95, 0x03, 0x81, 0x02, // 3 bits, Input
        0x75, 0x05, 0x95, 0x01, 0x81, 0x03, // padding
        0xC0
    };
    HIDParser parser;
    parser.parseReportDescriptor(desc, sizeof(desc));
    TEST_ASSERT_EQUAL(3, parser.getUsages().size());
    const HIDUsageDef* ac = parser.getUsageDef(0x008500D0);
    const HIDUsageDef* third = parser.getUsageDef(0x008500D2);
    TEST_ASSERT_NOT_NULL(ac);
    TEST_ASSERT_NOT_NULL(third);
    TEST_ASSERT_EQUAL_UINT16(0, ac->bit_offset);
    TEST_ASSERT_EQUAL_UINT16(2, third->bit_offset);
}

void test_usage_lookup_stops_at_sentinel(void) {
    // Issue #55: usage 0 matched the { NULL, 0 } sentinel and gave a NULL name
    TEST_ASSERT_NULL(nut_usage_lookup(0));
    TEST_ASSERT_EQUAL_STRING("0x00000000", get_nut_usage_name(0).c_str());
    TEST_ASSERT_EQUAL_STRING("UPS", nut_usage_lookup(0x00840004));
    TEST_ASSERT_EQUAL_STRING("APCBattReplaceDate", nut_usage_lookup(0xFF860016));
}

void test_collection_without_usage(void) {
    // Tail of the APC Back-UPS BX1500G descriptor (issue #55): a Physical
    // collection with no Usage around a vendor Feature
    const uint8_t desc[] = {
        0x05, 0x84, 0x09, 0x04, 0xA1, 0x01,     // UPS application collection
        0xA1, 0x00,                             // Collection (Physical), no Usage
        0x06, 0x00, 0xFF, 0x85, 0x80, 0x09, 0x55,
        0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x01, 0xB1, 0x82,
        0xC0,
        0xC0
    };
    HIDParser parser;
    TEST_ASSERT_TRUE(parser.parseReportDescriptor(desc, sizeof(desc)));
    const HIDUsageDef* vendor = parser.getUsageDef(0xFF000055);
    TEST_ASSERT_NOT_NULL(vendor);
    TEST_ASSERT_EQUAL_UINT8(0x80, vendor->report_id);
    TEST_ASSERT_EQUAL_STRING("UPS.0x00000000.0xFF000055", vendor->path);
}

void test_path_truncated_to_buffer(void) {
    // Nested collections longer than path[80]: truncated, still terminated
    const uint8_t desc[] = {
        0x05, 0x84, 0x09, 0x04, 0xA1, 0x01,
        0x09, 0x24, 0xA1, 0x00, 0x09, 0x24, 0xA1, 0x00, 0x09, 0x24, 0xA1, 0x00,
        0x09, 0x24, 0xA1, 0x00, 0x09, 0x24, 0xA1, 0x00, 0x09, 0x24, 0xA1, 0x00,
        0x09, 0x24, 0xA1, 0x00, 0x09, 0x24, 0xA1, 0x00,
        0x09, 0x30, 0x75, 0x08, 0x95, 0x01, 0xB1, 0x02,
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,
        0xC0
    };
    HIDParser parser;
    parser.parseReportDescriptor(desc, sizeof(desc));
    const HIDUsageDef* v = parser.getUsageDef(0x00840030);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL(sizeof(v->path) - 1, strlen(v->path));
    TEST_ASSERT_EQUAL_STRING_LEN("UPS.PowerSummary.PowerSummary.", v->path, 30);
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
    RUN_TEST(test_cyberpower_br700elcd_beeper);
    RUN_TEST(test_input_length_and_report_ids);
    RUN_TEST(test_input_length_without_report_ids);
    RUN_TEST(test_signed_field_sign_extended);
    RUN_TEST(test_unsigned_logical_max_in_one_byte);
    RUN_TEST(test_wide_field_clamped_to_32_bits);
    RUN_TEST(test_long_item_skipped);
    RUN_TEST(test_usage_minimum_maximum_expanded);
    RUN_TEST(test_usage_lookup_stops_at_sentinel);
    RUN_TEST(test_collection_without_usage);
    RUN_TEST(test_path_truncated_to_buffer);
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
    RUN_TEST(test_cyberpower_br700elcd_beeper);
    RUN_TEST(test_input_length_and_report_ids);
    RUN_TEST(test_input_length_without_report_ids);
    RUN_TEST(test_signed_field_sign_extended);
    RUN_TEST(test_unsigned_logical_max_in_one_byte);
    RUN_TEST(test_wide_field_clamped_to_32_bits);
    RUN_TEST(test_long_item_skipped);
    RUN_TEST(test_usage_minimum_maximum_expanded);
    RUN_TEST(test_usage_lookup_stops_at_sentinel);
    RUN_TEST(test_collection_without_usage);
    RUN_TEST(test_path_truncated_to_buffer);
    UNITY_END();
}
void loop() {}
#endif
#endif

