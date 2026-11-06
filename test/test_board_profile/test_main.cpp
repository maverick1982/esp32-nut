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

void test_spi_eth_option(void) {
    // US-126: the S3 offers a user-wired W5500; the profile lists what it can never give it
    const SpiEthOption& o = board().spi_eth;
    TEST_ASSERT_TRUE(o.available);
    TEST_ASSERT_EQUAL_UINT8(48, o.gpio_max);
    TEST_ASSERT_EQUAL_UINT8(5, o.reserved_count);
    TEST_ASSERT_EQUAL_UINT8(19, o.reserved[0].first);
    TEST_ASSERT_EQUAL_UINT8(20, o.reserved[0].last);
    TEST_ASSERT_EQUAL_STRING("USB port of the UPS", o.reserved[0].reason);
    TEST_ASSERT_EQUAL_UINT8(43, o.reserved[4].first);
    TEST_ASSERT_EQUAL_UINT8(44, o.reserved[4].last);
    TEST_ASSERT_EQUAL_UINT8(4, o.strapping_count);
    TEST_ASSERT_EQUAL_UINT8(0, o.strapping[0]);
    TEST_ASSERT_EQUAL_UINT8(3, o.strapping[1]);
    TEST_ASSERT_EQUAL_UINT8(45, o.strapping[2]);
    TEST_ASSERT_EQUAL_UINT8(46, o.strapping[3]);
    TEST_ASSERT_EQUAL_INT8(-1, board().eth.rst);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_identity_and_capabilities);
    RUN_TEST(test_status_led_pins);
    RUN_TEST(test_usb_default_peripheral);
    RUN_TEST(test_initial_settings);
    RUN_TEST(test_spi_eth_option);
    return UNITY_END();
}
