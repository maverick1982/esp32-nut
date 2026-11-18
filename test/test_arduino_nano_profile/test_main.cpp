#include <unity.h>
#include <string.h>
#include "board/board.h"

void setUp(void) {}
void tearDown(void) {}

void test_identity_usb_and_led(void) {
    TEST_ASSERT_EQUAL_STRING("arduino_nano_esp32", board().name);
    TEST_ASSERT_EQUAL_STRING("Arduino Nano ESP32", board().label);
    TEST_ASSERT_TRUE(board().has_wifi);
    TEST_ASSERT_FALSE(board().has_ethernet);
    TEST_ASSERT_EQUAL_INT8(-1, board().led.pin);
    TEST_ASSERT_EQUAL_UINT8(1, board().usb.port_count);
    TEST_ASSERT_EQUAL_STRING("otg", board().usb.ports[0].id);
    TEST_ASSERT_FALSE(board().defaults.led_enabled);
}

void test_w5500_pin_rules(void) {
    const SpiEthOption& option = board().spi_eth;
    TEST_ASSERT_TRUE(option.available);
    TEST_ASSERT_EQUAL_UINT8(48, option.gpio_max);
    TEST_ASSERT_EQUAL_UINT8(4, option.reserved_count);
    TEST_ASSERT_EQUAL_UINT8(19, option.reserved[0].first);
    TEST_ASSERT_EQUAL_UINT8(20, option.reserved[0].last);
    TEST_ASSERT_EQUAL_UINT8(26, option.reserved[1].first);
    TEST_ASSERT_EQUAL_UINT8(44, option.reserved[3].last);
    TEST_ASSERT_EQUAL_UINT8(4, option.strapping_count);
    TEST_ASSERT_EQUAL_UINT8(0, option.strapping[0]);
    TEST_ASSERT_EQUAL_UINT8(46, option.strapping[3]);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_identity_usb_and_led);
    RUN_TEST(test_w5500_pin_rules);
    return UNITY_END();
}
