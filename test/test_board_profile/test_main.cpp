#include <unity.h>
#include <string.h>
#include "board/board.h"

// The esp32s3_devkit profile carries the values that used to be literals in the code (US-090)

void setUp(void) {}
void tearDown(void) {}

void test_identity_and_capabilities(void) {
    TEST_ASSERT_EQUAL_STRING("esp32s3_devkit", board().name);
    TEST_ASSERT_EQUAL_STRING("ESP32-S3 DevKitC-1", board().label);
    TEST_ASSERT_TRUE(board().has_wifi);
    TEST_ASSERT_FALSE(board().has_ethernet);
    TEST_ASSERT_TRUE(board().eth.type == EthType::NONE);
}

void test_status_led_pins(void) {
    // DevKitC-1 NeoPixel and its power switch, before in DiagnosticLED.h
    TEST_ASSERT_EQUAL_INT8(48, board().led.pin);
    TEST_ASSERT_EQUAL_INT8(38, board().led.power_pin);
}

void test_usb_default_peripheral(void) {
    TEST_ASSERT_EQUAL_UINT8(1, board().usb.port_count);
    TEST_ASSERT_EQUAL_STRING("otg", board().usb.ports[0].id);
    TEST_ASSERT_EQUAL_UINT32(0, board().usb.ports[0].peripheral_map);
    TEST_ASSERT_EQUAL_UINT8(0, board().defaults.usb_port);
}

void test_initial_settings(void) {
    // Access point of the first setup and of the double reset, before in main.cpp
    TEST_ASSERT_EQUAL_STRING("NUT_ESP32_Config", board().defaults.ap_ssid);
    TEST_ASSERT_EQUAL_STRING("12345678", board().defaults.ap_password);
    TEST_ASSERT_TRUE(board().defaults.led_enabled);
    TEST_ASSERT_EQUAL_UINT8(3, board().defaults.led_level);
    TEST_ASSERT_TRUE(board().defaults.network_mode == NetworkMode::WIFI);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_identity_and_capabilities);
    RUN_TEST(test_status_led_pins);
    RUN_TEST(test_usb_default_peripheral);
    RUN_TEST(test_initial_settings);
    return UNITY_END();
}
