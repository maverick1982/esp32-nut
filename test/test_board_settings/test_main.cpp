#include <unity.h>
#include "board/board_settings.h"

// Board settings: defaults, LED brightness scale and sanitize (US-094)

// A P4-like profile with two USB ports and no status LED
static const BoardConfig P4_LIKE = {
    "p4_like", "P4 test board", false, true,
    { EthType::RMII, 0, 1, 31, 52, 50, 51, -1, -1, -1, -1, -1 },
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
    BoardSettings s = { true, 0, 0 };
    TEST_ASSERT_EQUAL_UINT8(3, sanitize(board(), s).led_level);
    s.led_level = 6;
    TEST_ASSERT_EQUAL_UINT8(3, sanitize(board(), s).led_level);
    s.led_level = 255;
    TEST_ASSERT_EQUAL_UINT8(3, sanitize(board(), s).led_level);
    s.led_level = 5;
    TEST_ASSERT_EQUAL_UINT8(5, sanitize(board(), s).led_level);
}

void test_sanitize_usb_port(void) {
    BoardSettings s = { true, 3, 1 };
    TEST_ASSERT_EQUAL_UINT8(0, sanitize(board(), s).usb_port);   // the S3 has one port
    TEST_ASSERT_EQUAL_UINT8(1, sanitize(P4_LIKE, s).usb_port);   // second port of two
    s.usb_port = 2;
    TEST_ASSERT_EQUAL_UINT8(0, sanitize(P4_LIKE, s).usb_port);
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
    RUN_TEST(test_capabilities);
    return UNITY_END();
}
