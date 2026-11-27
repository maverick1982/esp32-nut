#include "board/board.h"

// Waveshare ESP32-P4-NANO (ADR 0015). Ethernet as the Espressif reference design and the
// ESP32-P4-ETH: IP101GRI PHY at address 1, MDC 31, MDIO 52, PHY reset 51, 50 MHz RMII clock
// in on GPIO 50. The UPS goes on the USB 2.0 High-Speed port (the Type-A socket); the USB-C
// port is power and UART flashing only. No status LED (only a power LED).
// ESP32-C6-MINI-1 Wi-Fi coprocessor on SDIO 4-bit: CLK 18, CMD 19, D0-D3 14-17, reset 54, as
// in Waveshare's ESP32-P4-NANO demo (ESP-IDF/08_eth2ap/sdkconfig) and the esp32p4 variant of
// the Arduino core. The TF card slot (GPIO 39-44) is a different SDIO bus.
static const BoardConfig WAVESHARE_P4_NANO = {
    "waveshare_p4_nano",
    "Waveshare ESP32-P4-NANO",
    false,  // has_wifi: the coprocessor is not used yet (ADR 0015, next step)
    true,   // has_ethernet
    { EthType::RMII, EthPhy::IP101, 1, 31, 52, 50, 51, -1, -1, -1, -1, -1, -1 },
    { false, 0, 0, {}, 0, {} },  // no W5500: the board has its own Ethernet port
    { -1, -1 },  // no status LED
    { 1, { { "hs", "USB High-Speed (Type-A)", 0 } } },  // 0 = ESP-IDF default, the High-Speed port
    { "NUT_ESP32_Config", "12345678", false, 3, 0, NetworkMode::ETHERNET },
    { true, 18, 19, 14, 15, 16, 17, 54 },  // ESP32-C6 on SDIO (ADR 0015)
};

const BoardConfig& board() {
    return WAVESHARE_P4_NANO;
}
