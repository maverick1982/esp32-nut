#ifndef NETWORK_WIFI_COPROCESSOR_H
#define NETWORK_WIFI_COPROCESSOR_H

#include <stddef.h>
#include <stdint.h>
#include "board/board.h"

/**
 * @brief The Wi-Fi coprocessor of the board at boot (ADR 0015, sections 2 and 5).
 *
 * `net` adapter of the ESP-Hosted support of the Arduino core: gives the SDIO pins of the
 * profile to WiFi.setPins() and starts the link to the coprocessor once, in a task of its own,
 * waiting at most timeout_ms (the caller's tick resets the watchdog meanwhile). Only the
 * transport starts: no Wi-Fi mode, no network interface. A later WiFi.mode() reuses it.
 * A coprocessor that does not answer, or does not answer in time, leaves the board without
 * Wi-Fi for this boot: the caller plans it as a board without Wi-Fi (no lockout).
 */
struct WifiCoprocessorStatus {
    bool present = false;    // the profile has a coprocessor
    bool answered = false;   // ESP-Hosted connected to it
    bool timed_out = false;  // no answer within the timeout (its task may still be blocked)
    char target[16] = "";    // "esp32c6"
    uint8_t host[3] = {0, 0, 0};   // ESP-Hosted version of the host side (the core)
    uint8_t slave[3] = {0, 0, 0};  // firmware version of the coprocessor, 0.0.0 = unknown
    bool update_available = false; // the host has a newer firmware for the coprocessor
    char error[64] = "";
    uint32_t duration_ms = 0;
    bool slaveKnown() const { return slave[0] || slave[1] || slave[2]; }
};

namespace WifiCoprocessorProbe {

// Once per boot, before planNetwork(). tick(now) is called about every 100 ms while waiting
bool run(const WifiCoprocessor& c, uint32_t timeout_ms, void (*tick)(uint32_t now));

// The outcome of run(); present false before run() or on a board without a coprocessor
const WifiCoprocessorStatus& status();

}  // namespace WifiCoprocessorProbe

/**
 * @brief Writes a new firmware to the coprocessor (ADR 0015, section 5): the ESP-Hosted update
 * API of the core. The coprocessor writes it to its other OTA partition, so an interrupted
 * update leaves the old firmware running; activate() makes it boot the new one. Only while
 * the coprocessor answers; the caller checks the image first (CoprocessorImage). loopTask.
 */
namespace WifiCoprocessorUpdate {

bool begin();
bool write(const uint8_t* data, size_t len);
bool end();
bool activate();

}  // namespace WifiCoprocessorUpdate

#endif  // NETWORK_WIFI_COPROCESSOR_H
