#include <unity.h>
#include <string.h>
#include "board/board.h"

// ADR 0015: the Waveshare ESP32-P4-NANO profile (env native-waveshare-p4-nano)

void setUp(void) {}
void tearDown(void) {}

void test_identity_led_and_usb(void) {
    TEST_ASSERT_EQUAL_STRING("waveshare_p4_nano", board().name);
    TEST_ASSERT_EQUAL_STRING("Waveshare ESP32-P4-NANO", board().label);
    TEST_ASSERT_TRUE(board().has_ethernet);
    // Wi-Fi through the coprocessor (ADR 0015); confirmed at boot by activeBoard()
    TEST_ASSERT_TRUE(board().has_wifi);
    TEST_ASSERT_EQUAL_INT8(-1, board().led.pin);
    TEST_ASSERT_FALSE(board().defaults.led_enabled);
    TEST_ASSERT_EQUAL_UINT8(1, board().usb.port_count);
    TEST_ASSERT_EQUAL_STRING("hs", board().usb.ports[0].id);
    TEST_ASSERT_EQUAL_UINT32(0, board().usb.ports[0].peripheral_map);
    TEST_ASSERT_FALSE(board().spi_eth.available);
}

// Same Ethernet wiring as the Espressif reference design and the ESP32-P4-ETH
void test_ethernet_pins(void) {
    const EthConfig& eth = board().eth;
    TEST_ASSERT_TRUE(eth.type == EthType::RMII);
    TEST_ASSERT_EQUAL_UINT8(EthPhy::IP101, eth.phy_type);
    TEST_ASSERT_EQUAL_INT8(1, eth.phy_addr);
    TEST_ASSERT_EQUAL_INT8(31, eth.mdc);
    TEST_ASSERT_EQUAL_INT8(52, eth.mdio);
    TEST_ASSERT_EQUAL_INT8(50, eth.clk);
    TEST_ASSERT_EQUAL_INT8(51, eth.power);
}

// ESP32-C6 on SDIO, as in Waveshare's demo (08_eth2ap/sdkconfig)
void test_wifi_coprocessor_pins(void) {
    const WifiCoprocessor& c = board().wifi_coprocessor;
    TEST_ASSERT_TRUE(c.present);
    TEST_ASSERT_EQUAL_INT8(18, c.clk);
    TEST_ASSERT_EQUAL_INT8(19, c.cmd);
    TEST_ASSERT_EQUAL_INT8(14, c.d0);
    TEST_ASSERT_EQUAL_INT8(15, c.d1);
    TEST_ASSERT_EQUAL_INT8(16, c.d2);
    TEST_ASSERT_EQUAL_INT8(17, c.d3);
    TEST_ASSERT_EQUAL_INT8(54, c.reset);
}

// PDR 0002 D3: Ethernet is the initial link on P4 boards
void test_defaults(void) {
    TEST_ASSERT_TRUE(board().defaults.network_mode == NetworkMode::ETHERNET);
    TEST_ASSERT_EQUAL_STRING("NUT_ESP32_Config", board().defaults.ap_ssid);
    TEST_ASSERT_EQUAL_UINT8(0, board().defaults.usb_port);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_identity_led_and_usb);
    RUN_TEST(test_ethernet_pins);
    RUN_TEST(test_wifi_coprocessor_pins);
    RUN_TEST(test_defaults);
    return UNITY_END();
}
