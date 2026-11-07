#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "board/board_settings.h"

// Board settings: defaults, LED brightness scale and sanitize (US-094)

// A P4-like profile with two USB ports and no status LED
static const BoardConfig P4_LIKE = {
    "p4_like", "P4 test board", false, true,
    { EthType::RMII, 0, 1, 31, 52, 50, 51, -1, -1, -1, -1, -1, -1 },
    { false, 0, 0, {}, 0, {} },
    { -1, -1 },
    { 2, { { "hs", "High-Speed", 1 }, { "fs", "Full-Speed", 2 } } },
    { "", "", false, 3, 0, NetworkMode::ETHERNET },
};

void setUp(void) {}
void tearDown(void) {}

void test_defaults_from_the_s3_profile(void) {
    BoardSettings s = defaultsFor(board());
    TEST_ASSERT_TRUE(s.led_on);
    TEST_ASSERT_EQUAL_UINT8(3, s.led_level);
    TEST_ASSERT_EQUAL_UINT8(0, s.usb_port);
    // Level 3 keeps the brightness used before US-094
    TEST_ASSERT_EQUAL_UINT8(10, ledBrightness(s.led_level));
}

void test_brightness_scale(void) {
    TEST_ASSERT_EQUAL_UINT8(3, ledBrightness(1));
    TEST_ASSERT_EQUAL_UINT8(5, ledBrightness(2));
    TEST_ASSERT_EQUAL_UINT8(10, ledBrightness(3));
    TEST_ASSERT_EQUAL_UINT8(20, ledBrightness(4));
    TEST_ASSERT_EQUAL_UINT8(40, ledBrightness(5));
    // Out of range: the default level
    TEST_ASSERT_EQUAL_UINT8(10, ledBrightness(0));
    TEST_ASSERT_EQUAL_UINT8(10, ledBrightness(6));
}

void test_sanitize_led_level(void) {
    BoardSettings s = { true, 0, 0, false };
    TEST_ASSERT_EQUAL_UINT8(3, sanitize(board(), s).led_level);
    s.led_level = 6;
    TEST_ASSERT_EQUAL_UINT8(3, sanitize(board(), s).led_level);
    s.led_level = 255;
    TEST_ASSERT_EQUAL_UINT8(3, sanitize(board(), s).led_level);
    s.led_level = 5;
    TEST_ASSERT_EQUAL_UINT8(5, sanitize(board(), s).led_level);
}

void test_sanitize_usb_port(void) {
    BoardSettings s = { true, 3, 1, false };
    TEST_ASSERT_EQUAL_UINT8(0, sanitize(board(), s).usb_port);   // the S3 has one port
    TEST_ASSERT_EQUAL_UINT8(1, sanitize(P4_LIKE, s).usb_port);   // second port of two
    s.usb_port = 2;
    TEST_ASSERT_EQUAL_UINT8(0, sanitize(P4_LIKE, s).usb_port);
}

void test_auto_shutdown_off_by_default_and_kept(void) {
    // US-114: off on every profile, and sanitize() keeps the saved value
    TEST_ASSERT_FALSE(defaultsFor(board()).auto_shutdown);
    TEST_ASSERT_FALSE(defaultsFor(P4_LIKE).auto_shutdown);
    BoardSettings s = { true, 3, 0, true };
    TEST_ASSERT_TRUE(sanitize(board(), s).auto_shutdown);
    s.led_level = 0;
    TEST_ASSERT_TRUE(sanitize(board(), s).auto_shutdown);
}

// US-138: the Q* driver detects the protocol by itself and asks the vendor by default
void test_qx_settings_default_automatic(void) {
    BoardSettings s = defaultsFor(board());
    TEST_ASSERT_EQUAL_STRING("", s.qx_protocol);
    TEST_ASSERT_FALSE(s.qx_novendor);
    TEST_ASSERT_EQUAL_STRING("", defaultsFor(P4_LIKE).qx_protocol);
}

// A name the firmware does not know becomes automatic, a known one its canonical form;
// novendor is kept as saved
void test_sanitize_qx_protocol(void) {
    BoardSettings s = defaultsFor(board());
    s.qx_novendor = true;
    snprintf(s.qx_protocol, sizeof(s.qx_protocol), "%s", "voltronic");
    TEST_ASSERT_EQUAL_STRING("", sanitize(board(), s).qx_protocol);
    snprintf(s.qx_protocol, sizeof(s.qx_protocol), "%s", "HUNNOX");
    TEST_ASSERT_EQUAL_STRING("hunnox", sanitize(board(), s).qx_protocol);
    snprintf(s.qx_protocol, sizeof(s.qx_protocol), "%s", "Megatec/Old");
    TEST_ASSERT_EQUAL_STRING("megatec/old", sanitize(board(), s).qx_protocol);
    snprintf(s.qx_protocol, sizeof(s.qx_protocol), "%s", "q1");
    TEST_ASSERT_EQUAL_STRING("q1", sanitize(board(), s).qx_protocol);
    snprintf(s.qx_protocol, sizeof(s.qx_protocol), "%s", "megatec-old");
    TEST_ASSERT_EQUAL_STRING("", sanitize(board(), s).qx_protocol);
    TEST_ASSERT_TRUE(sanitize(board(), s).qx_novendor);
}

// A corrupted value without NUL (the array full) is not read past its end
void test_sanitize_qx_protocol_without_terminator(void) {
    BoardSettings s = defaultsFor(board());
    memset(s.qx_protocol, 'z', sizeof(s.qx_protocol));
    BoardSettings out = sanitize(board(), s);
    TEST_ASSERT_EQUAL_STRING("", out.qx_protocol);
}

void test_capabilities(void) {
    TEST_ASSERT_TRUE(hasLed(board()));
    TEST_ASSERT_FALSE(hasUsbChoice(board()));
    TEST_ASSERT_FALSE(hasLed(P4_LIKE));
    TEST_ASSERT_TRUE(hasUsbChoice(P4_LIKE));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_from_the_s3_profile);
    RUN_TEST(test_brightness_scale);
    RUN_TEST(test_sanitize_led_level);
    RUN_TEST(test_sanitize_usb_port);
    RUN_TEST(test_auto_shutdown_off_by_default_and_kept);
    RUN_TEST(test_qx_settings_default_automatic);
    RUN_TEST(test_sanitize_qx_protocol);
    RUN_TEST(test_sanitize_qx_protocol_without_terminator);
    RUN_TEST(test_capabilities);
    return UNITY_END();
}
