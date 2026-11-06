#ifndef ETH_SPI_SETTINGS_H
#define ETH_SPI_SETTINGS_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "board/board.h"

/**
 * @brief Settings of a W5500 Ethernet module wired by the user (US-126, issue #64).
 *
 * Pure logic, so it runs in the native unit tests. There is no common pinout among the
 * boards with a W5500 (external modules, Waveshare ESP32-S3-ETH, LilyGO T-ETH-Lite S3),
 * so the pins are always set by the user from the Network page, never preset in the
 * firmware. PDR 0002 D4 allows a pin in the web UI only if a mistake cannot lock the
 * user out: checkEthSpiSettings() refuses the GPIOs the board reserves (flash, PSRAM,
 * USB towards the UPS, serial console, status LED) and a module that does not answer
 * at startup falls back to Wi-Fi or the access point (NetBootPlan, EthBootProbe).
 * A pin of -1 means "not set"; INT and RST are optional.
 */
struct EthSpiSettings {
    bool enabled;
    int8_t sck;
    int8_t mosi;
    int8_t miso;
    int8_t cs;
    int8_t irq;
    int8_t rst;
};

inline EthSpiSettings ethSpiDefaults() {
    EthSpiSettings s = { false, -1, -1, -1, -1, -1, -1 };
    return s;
}

inline bool operator==(const EthSpiSettings& a, const EthSpiSettings& b) {
    return a.enabled == b.enabled && a.sck == b.sck && a.mosi == b.mosi && a.miso == b.miso &&
           a.cs == b.cs && a.irq == b.irq && a.rst == b.rst;
}

inline bool operator!=(const EthSpiSettings& a, const EthSpiSettings& b) {
    return !(a == b);
}

struct EthPinCheck {
    bool ok;
    char message[96];  // the first problem found, for the web UI and the API (400)
};

// Number of pin fields and their names, in the order the checks and the web UI use
static const uint8_t ETH_SPI_PIN_COUNT = 6;

inline const char* ethSpiPinName(uint8_t index) {
    static const char* const NAMES[ETH_SPI_PIN_COUNT] = { "SCK", "MOSI", "MISO", "CS", "INT", "RST" };
    return index < ETH_SPI_PIN_COUNT ? NAMES[index] : "";
}

inline int8_t ethSpiPin(const EthSpiSettings& s, uint8_t index) {
    switch (index) {
        case 0: return s.sck;
        case 1: return s.mosi;
        case 2: return s.miso;
        case 3: return s.cs;
        case 4: return s.irq;
        case 5: return s.rst;
        default: return -1;
    }
}

// SCK, MOSI, MISO and CS are required to enable the module; INT and RST are optional
inline bool ethSpiPinRequired(uint8_t index) {
    return index < 4;
}

inline bool isStrappingGpio(const BoardConfig& b, int pin) {
    for (uint8_t i = 0; i < b.spi_eth.strapping_count && i < 4; i++) {
        if (b.spi_eth.strapping[i] == pin) return true;
    }
    return false;
}

/**
 * Checks the pins in field order (SCK, MOSI, MISO, CS, INT, RST). Every set pin is checked
 * even when the module is off, so a forbidden GPIO is never saved; the required fields are
 * checked only when enabling. Returns false with the first problem in out.message.
 */
inline bool checkEthSpiSettings(const BoardConfig& b, const EthSpiSettings& s, EthPinCheck& out) {
    out.ok = false;
    out.message[0] = '\0';
    if (s.enabled && !b.spi_eth.available) {
        snprintf(out.message, sizeof(out.message), "No W5500 support on this board");
        return false;
    }
    for (uint8_t i = 0; i < ETH_SPI_PIN_COUNT; i++) {
        const int pin = ethSpiPin(s, i);
        const char* name = ethSpiPinName(i);
        if (pin == -1) continue;
        if (pin < -1 || pin > b.spi_eth.gpio_max) {
            snprintf(out.message, sizeof(out.message), "GPIO %d does not exist on this board", pin);
            return false;
        }
        for (uint8_t r = 0; r < b.spi_eth.reserved_count && r < 6; r++) {
            const ReservedGpio& rg = b.spi_eth.reserved[r];
            if (pin >= rg.first && pin <= rg.last) {
                snprintf(out.message, sizeof(out.message), "GPIO %d cannot be used for %s: %s", pin, name,
                         rg.reason);
                return false;
            }
        }
        if (pin == b.led.pin || pin == b.led.power_pin) {
            snprintf(out.message, sizeof(out.message), "GPIO %d cannot be used for %s: status LED", pin, name);
            return false;
        }
        for (uint8_t j = 0; j < i; j++) {
            if (ethSpiPin(s, j) == pin) {
                snprintf(out.message, sizeof(out.message), "GPIO %d is used for both %s and %s", pin,
                         ethSpiPinName(j), name);
                return false;
            }
        }
    }
    if (s.enabled) {
        for (uint8_t i = 0; i < ETH_SPI_PIN_COUNT; i++) {
            if (ethSpiPinRequired(i) && ethSpiPin(s, i) < 0) {
                snprintf(out.message, sizeof(out.message), "%s is required to enable the W5500", ethSpiPinName(i));
                return false;
            }
        }
    }
    out.ok = true;
    return true;
}

// Set pins that are strapping pins, each once: allowed, with a warning in the web UI
inline uint8_t strappingWarnings(const BoardConfig& b, const EthSpiSettings& s, int8_t out[ETH_SPI_PIN_COUNT]) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < ETH_SPI_PIN_COUNT; i++) {
        const int8_t pin = ethSpiPin(s, i);
        if (pin < 0 || !isStrappingGpio(b, pin)) continue;
        bool seen = false;
        for (uint8_t j = 0; j < n; j++) {
            if (out[j] == pin) seen = true;
        }
        if (!seen) out[n++] = pin;
    }
    return n;
}

// Ethernet configuration of the network module for the pins set by the user
inline EthConfig toEthConfig(const EthSpiSettings& s) {
    EthConfig c = { EthType::SPI, EthPhy::W5500, 1, -1, -1, -1, -1, s.cs, s.irq, s.sck, s.miso, s.mosi, s.rst };
    return c;
}

// Link to use on a board with Wi-Fi and the W5500 (US-127): the name saved in NVS and
// sent by the API. parseNetworkMode() accepts only these exact lower-case names
inline const char* networkModeName(NetworkMode mode) {
    switch (mode) {
        case NetworkMode::ETHERNET: return "ethernet";
        case NetworkMode::WIFI: return "wifi";
        default: return "auto";
    }
}

inline bool parseNetworkMode(const char* s, NetworkMode& out) {
    if (s == nullptr) return false;
    if (strcmp(s, "auto") == 0) {
        out = NetworkMode::AUTO;
    } else if (strcmp(s, "ethernet") == 0) {
        out = NetworkMode::ETHERNET;
    } else if (strcmp(s, "wifi") == 0) {
        out = NetworkMode::WIFI;
    } else {
        return false;
    }
    return true;
}

// PDR 0002 D4: saved settings the board does not allow (a corrupted NVS entry, a profile
// that changed) turn the module off; the pins are kept so the user can fix them
inline EthSpiSettings sanitizeEthSpi(const BoardConfig& b, EthSpiSettings s) {
    EthPinCheck check;
    if (s.enabled && !checkEthSpiSettings(b, s, check)) s.enabled = false;
    return s;
}

#endif // ETH_SPI_SETTINGS_H
