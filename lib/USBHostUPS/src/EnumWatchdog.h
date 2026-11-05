#ifndef ENUM_WATCHDOG_H
#define ENUM_WATCHDOG_H

#include <stdint.h>

/**
 * @brief Retry the enumeration when the UPS does not come back after an automatic
 *        restart (US-121, issue #78).
 *
 * Pure logic, so it runs in the native unit tests, like RestartPolicy.
 *
 * ESP-IDF does not retry a cancelled enumeration: ENUM_EVENT_CANCELED disables the root
 * port (usb_host.c, hub_port_disable), and only a detach or a port error recovers it. A
 * UPS still busy right after a controlled restart can fail its enumeration and then stay
 * absent for hours, while RestartPolicy waits for a restart request that never comes,
 * since nothing is attached to ask for one.
 *
 * The owner arms the watchdog at boot only after a controlled restart (consecutive
 * restart counter > 0 in RTC memory). If no device enumerates:
 * - after FIRST_CYCLE_MS, and then every CYCLE_INTERVAL_MS, it asks to power-cycle the
 *   root port, MAX_PORT_CYCLES times;
 * - one CYCLE_INTERVAL_MS after the last cycle it asks once for a restart, and
 *   RestartPolicy applies its usual delays (1/5/15 min) and degraded mode.
 * The first enumeration disarms it for good: whatever happens later is the job of the
 * link recovery.
 */
class EnumWatchdog {
public:
    enum class Action : uint8_t { NONE, CYCLE_PORT, REQUEST_RESTART };

    static const uint32_t FIRST_CYCLE_MS = 60000;
    static const uint32_t CYCLE_INTERVAL_MS = 60000;
    static const uint8_t MAX_PORT_CYCLES = 2;

    EnumWatchdog() : _armed(false), _armedAt(0), _cycles(0), _restartRequested(false) {}

    void arm(uint32_t now) {
        _armed = true;
        _armedAt = now;
        _cycles = 0;
        _restartRequested = false;
    }

    /**
     * @param enumerated a device enumerated since the boot
     */
    Action tick(bool enumerated, uint32_t now) {
        if (!_armed) return Action::NONE;
        if (enumerated) {
            _armed = false;
            return Action::NONE;
        }
        uint32_t elapsed = now - _armedAt; // unsigned: millis() wrap-safe
        if (_cycles < MAX_PORT_CYCLES) {
            if (elapsed >= FIRST_CYCLE_MS + (uint32_t)_cycles * CYCLE_INTERVAL_MS) {
                _cycles++;
                return Action::CYCLE_PORT;
            }
            return Action::NONE;
        }
        if (!_restartRequested && elapsed >= FIRST_CYCLE_MS + (uint32_t)MAX_PORT_CYCLES * CYCLE_INTERVAL_MS) {
            _restartRequested = true;
            return Action::REQUEST_RESTART;
        }
        return Action::NONE;
    }

    bool armed() const { return _armed; }
    uint8_t cycles() const { return _cycles; }
    bool restartRequested() const { return _restartRequested; }

private:
    bool _armed;
    uint32_t _armedAt;
    uint8_t _cycles;
    bool _restartRequested;
};

#endif // ENUM_WATCHDOG_H
