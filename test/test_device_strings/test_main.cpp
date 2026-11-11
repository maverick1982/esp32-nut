#include <unity.h>
#include <string.h>
#include "DeviceStrings.h"

void setUp(void) {}
void tearDown(void) {}

// Review C4: conversion of the USB string descriptors, always NUL-terminated

void test_plain_string() {
    const wchar_t src[] = {'C', 'P', 'S', 0};
    char buf[8];
    TEST_ASSERT_EQUAL(3, DeviceStrings::toAscii(src, 32, false, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("CPS", buf);
}

void test_trailing_spaces_trimmed() {
    const wchar_t src[] = {'E', 'a', 't', 'o', 'n', ' ', ' ', 0};
    char buf[16];
    DeviceStrings::toAscii(src, 32, false, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("Eaton", buf);
}

void test_inverted_string_autodetected() {
    // 0xFF00 | ~c, as sent by some CyberPower firmwares
    const wchar_t src[] = {(wchar_t)(0xFF00 | (uint8_t)~'C'), (wchar_t)(0xFF00 | (uint8_t)~'P'), 0};
    char buf[8];
    DeviceStrings::toAscii(src, 32, false, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("CP", buf);
}

void test_quirk_does_not_invert_plain_string() {
    // The first unit has a zero high byte: the string is plain even with the quirk
    const wchar_t src[] = {'O', 'K', 0};
    char buf[8];
    DeviceStrings::toAscii(src, 32, true, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("OK", buf);
}

void test_non_ascii_replaced() {
    // wcstombs() stopped here with -1 and left the buffer unterminated
    const wchar_t src[] = {'U', (wchar_t)0x00E9, (wchar_t)0x4E2D, 'S', 0};
    char buf[8];
    memset(buf, 'X', sizeof(buf));
    TEST_ASSERT_EQUAL(4, DeviceStrings::toAscii(src, 32, false, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("U??S", buf);
}

void test_control_chars_dropped() {
    const wchar_t src[] = {'A', 0x01, '\n', 'B', 0};
    char buf[8];
    DeviceStrings::toAscii(src, 32, false, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("AB", buf);
}

void test_output_truncated_and_terminated() {
    const wchar_t src[] = {'A', 'B', 'C', 'D', 'E', 'F', 0};
    char buf[4];
    TEST_ASSERT_EQUAL(3, DeviceStrings::toAscii(src, 32, false, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("ABC", buf);
}

void test_unterminated_source_bounded() {
    const wchar_t src[] = {'A', 'B', 'C', 'D'}; // no NUL
    char buf[16];
    TEST_ASSERT_EQUAL(2, DeviceStrings::toAscii(src, 2, false, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("AB", buf);
}

void test_empty_and_null() {
    const wchar_t empty[] = {0};
    char buf[4] = {'X', 'X', 'X', 'X'};
    TEST_ASSERT_EQUAL(0, DeviceStrings::toAscii(empty, 32, false, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
    buf[0] = 'X';
    TEST_ASSERT_EQUAL(0, DeviceStrings::toAscii(nullptr, 32, false, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

// Issue #91: nut_usb_get_string() of usb-common.c, used when the enumeration read no string

void test_nut_langid_from_table() {
    const uint8_t table[] = {4, 0x03, 0x09, 0x04};
    TEST_ASSERT_EQUAL_HEX16(0x0409, DeviceStrings::nutLangId(table, 4));
    const uint8_t other[] = {4, 0x03, 0x10, 0x04}; // a LANGID that ESP-IDF rejects
    TEST_ASSERT_EQUAL_HEX16(0x0410, DeviceStrings::nutLangId(other, 4));
    const uint8_t zero[] = {4, 0x03, 0x00, 0x00}; // taken as it is, as upstream
    TEST_ASSERT_EQUAL_HEX16(0x0000, DeviceStrings::nutLangId(zero, 4));
}

void test_nut_langid_broken_is_en_us() {
    const uint8_t short_answer[] = {4, 0x03};
    TEST_ASSERT_EQUAL_HEX16(0x0409, DeviceStrings::nutLangId(short_answer, 2));
    const uint8_t wrong_type[] = {4, 0x02, 0x10, 0x04};
    TEST_ASSERT_EQUAL_HEX16(0x0409, DeviceStrings::nutLangId(wrong_type, 4));
    const uint8_t tiny_length[] = {2, 0x03, 0x10, 0x04};
    TEST_ASSERT_EQUAL_HEX16(0x0409, DeviceStrings::nutLangId(tiny_length, 4));
    TEST_ASSERT_EQUAL_HEX16(0x0409, DeviceStrings::nutLangId(nullptr, 4));
}

void test_nut_usb_string_mec0003() {
    const uint8_t desc[] = {16, 0x03, 'M', 0, 'E', 0, 'C', 0, '0', 0, '0', 0, '0', 0, '3', 0};
    char buf[64];
    TEST_ASSERT_EQUAL(7, DeviceStrings::nutUsbString(desc, sizeof(desc), buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("MEC0003", buf);
}

void test_nut_usb_string_as_upstream() {
    // A high byte gives '?'; spaces are kept (no trimming upstream)
    const uint8_t desc[] = {10, 0x03, 'A', 0, 0xE9, 0x00, 0x00, 0x20, ' ', 0};
    char buf[64];
    TEST_ASSERT_EQUAL(4, DeviceStrings::nutUsbString(desc, sizeof(desc), buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("A\xE9? ", buf);
    // Truncated to the buffer, always terminated
    char small[3];
    TEST_ASSERT_EQUAL(2, DeviceStrings::nutUsbString(desc, sizeof(desc), small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("A\xE9", small);
    // Nothing usable
    buf[0] = 'X';
    TEST_ASSERT_EQUAL(0, DeviceStrings::nutUsbString(desc, 1, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
    TEST_ASSERT_EQUAL(0, DeviceStrings::nutUsbString(desc, 2, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_nut_langid_from_table);
    RUN_TEST(test_nut_langid_broken_is_en_us);
    RUN_TEST(test_nut_usb_string_mec0003);
    RUN_TEST(test_nut_usb_string_as_upstream);
    RUN_TEST(test_plain_string);
    RUN_TEST(test_trailing_spaces_trimmed);
    RUN_TEST(test_inverted_string_autodetected);
    RUN_TEST(test_quirk_does_not_invert_plain_string);
    RUN_TEST(test_non_ascii_replaced);
    RUN_TEST(test_control_chars_dropped);
    RUN_TEST(test_output_truncated_and_terminated);
    RUN_TEST(test_unterminated_source_bounded);
    RUN_TEST(test_empty_and_null);
    return UNITY_END();
}
