#ifndef ENUM_WATCHDOG_H
#define ENUM_WATCHDOG_H

#include <stdint.h>

/**
 * @brief Retry the enumeration when the UPS does not show up after a boot (US-121 and
 *        US-139, issue #78).
 *
 * Pure logic, so it runs in the native unit tests, like RestartPolicy.
 *
 * ESP-IDF does not retry a cancelled enumeration: ENUM_EVENT_CANCELED disables the root
 * port (usb_host.c, hub_port_disable), and only a detach or a port error recovers it. A
 * UPS still busy when the USB host starts can fail its enumeration and then stay absent
 * for hours: nothing is attached to fail, so nothing asks for a recovery.
 *
 * The owner arms the watchdog at every boot, in the mode modeFor() picks from the
 * consecutive restart counter in RTC memory. If no device enumerates, after FIRST_CYCLE_MS
 * and then every CYCLE_INTERVAL_MS it asks to power-cycle the root port, MAX_PORT_CYCLES
 * times. Then:
 * - AFTER_AUTO_RESTART (counter > 0, US-121): one CYCLE_INTERVAL_MS after the last cycle
 *   it asks once for a restart, and RestartPolicy applies its usual delays (1/5/15 min)
 *   and degraded mode.
 * - AFTER_NORMAL_BOOT (counter 0: power on, EN pin, OTA, settings saved; US-139): it
 *   disarms right after the last cycle. A board powered on without a UPS must see no
 *   restart and no banner, and a cycle on an empty port has no visible effect. In #78 a
 *   Smart-UPS 750 stayed absent for almost 3 hours after a POWERON boot.
 * The first enumeration disarms it for good: whatever happens later is the job of the
 * link recovery.
 */
class EnumWatchdog {
public:
    enum class Action : uint8_t { NONE, CYCLE_PORT, REQUEST_RESTART };
    enum class Mode : uint8_t { AFTER_AUTO_RESTART, AFTER_NORMAL_BOOT };

    static const uint32_t FIRST_CYCLE_MS = 60000;
    static const uint32_t CYCLE_INTERVAL_MS = 60000;
    static const uint8_t MAX_PORT_CYCLES = 2;

    EnumWatchdog()
        : _armed(false), _armedAt(0), _cycles(0), _restartRequested(false),
          _mode(Mode::AFTER_AUTO_RESTART) {}

    static Mode modeFor(uint8_t consecutiveRestarts) {
        return consecutiveRestarts > 0 ? Mode::AFTER_AUTO_RESTART : Mode::AFTER_NORMAL_BOOT;
    }

    void arm(uint32_t now, Mode mode = Mode::AFTER_AUTO_RESTART) {
        _armed = true;
        _armedAt = now;
        _cycles = 0;
        _restartRequested = false;
        _mode = mode;
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
                // After a boot that was not automatic the cycles are all there is
                if (_mode == Mode::AFTER_NORMAL_BOOT && _cycles >= MAX_PORT_CYCLES) _armed = false;
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
    Mode mode() const { return _mode; }

private:
    bool _armed;
    uint32_t _armedAt;
    uint8_t _cycles;
    bool _restartRequested;
    Mode _mode;
};

#endif // ENUM_WATCHDOG_H
