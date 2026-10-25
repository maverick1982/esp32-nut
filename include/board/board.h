#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

/**
 * @brief Board profile: pins, capabilities and initial settings of one board (ADR 0010 §3).
 *
 * Plain data, compiled into the image: each PlatformIO environment builds exactly one
 * src/board/<board>.cpp (build_src_filter), so the code that uses a pin or a capability
 * asks board() instead of testing the target with #if. Pins whose wrong value would cut
 * the network are fixed here; what a user can safely get wrong is a setting whose
 * initial value comes from `defaults` (PDR 0002 D4). A pin of -1 means "not present".
 */

enum class EthType : uint8_t { NONE, RMII, SPI };

// Network mode offered when a board has both links (PDR 0002 D3)
enum class NetworkMode : uint8_t { WIFI, ETHERNET, AUTO };

struct EthConfig {
    EthType type;
    uint8_t phy_type;   // ESP-IDF eth_phy type of the adapter, meaningful only with RMII/SPI
    int8_t phy_addr;
    // RMII management and clock
    int8_t mdc;
    int8_t mdio;
    int8_t clk;
    int8_t power;
    // SPI module
    int8_t cs;
    int8_t irq;
    int8_t sck;
    int8_t miso;
    int8_t mosi;
};

struct LedConfig {
    int8_t pin;        // addressable status LED (NeoPixel)
    int8_t power_pin;  // driven high before the LED is used
};

struct UsbConfig {
    uint32_t peripheral_map;  // usb_host_config_t::peripheral_map, 0 = ESP-IDF default
};

struct BoardDefaults {
    const char* ap_ssid;
    const char* ap_password;
    bool led_enabled;
    NetworkMode network_mode;
};

struct BoardConfig {
    const char* name;
    bool has_wifi;
    bool has_ethernet;
    EthConfig eth;
    LedConfig led;
    UsbConfig usb;
    BoardDefaults defaults;
};

// The profile of the board this image was built for
const BoardConfig& board();

#endif // BOARD_H
