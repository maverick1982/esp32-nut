#include "board/board.h"

// ESP32-P4 with RMII Ethernet on the Espressif reference design: Waveshare ESP32-P4-ETH
// (also the PoE version) and its clones. IP101 PHY at address 1, MDC 31, MDIO 52, PHY
// reset 51, 50 MHz RMII clock in on GPIO 50. The RMII data pins are the fixed ones of
// the P4 EMAC. No Wi-Fi, no status LED.
// USB: the UPS goes on the USB 2.0 OTG High-Speed port (4-pin header J3 on the
// Waveshare board). The Full-Speed controller is not offered: the ESP-IDF 5.5 USB Host
// Library supports only the High-Speed one on the P4.
static const BoardConfig ESP32P4_ETH = {
    "esp32p4_eth",
    "ESP32-P4 Ethernet (Waveshare ESP32-P4-ETH)",
    false,  // has_wifi
    true,   // has_ethernet
    { EthType::RMII, EthPhy::IP101, 1, 31, 52, 50, 51, -1, -1, -1, -1, -1 },
    { -1, -1 },  // no status LED
    { 1, { { "hs", "USB High-Speed", 0 } } },  // 0 = ESP-IDF default, the High-Speed port
    { "", "", false, 3, 0, NetworkMode::ETHERNET },
};

const BoardConfig& board() {
    return ESP32P4_ETH;
}
