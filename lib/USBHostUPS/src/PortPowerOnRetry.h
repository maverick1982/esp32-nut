#ifndef PORT_POWER_ON_RETRY_H
#define PORT_POWER_ON_RETRY_H

#include <stdint.h>

/**
 * @brief Power the root port on again after a power cycle, retrying until ESP-IDF accepts it
 *        (issue #78).
 *
 * Pure logic, so it runs in the native unit tests, like PortCyclePolicy.
 *
 * usb_host_lib_set_root_port_power(false) sets the hub state to NOT_POWERED at once, but with a
 * device attached the disconnection moves the HCD port to RECOVERY (hcd_dwc.c), and only the
 * USB Host library task brings it back to NOT_POWERED (hcd_port_recover()), once the HID
 * driver has closed the device and the USBH has freed it. Until then the power on fails with
 * ESP_ERR_INVALID_STATE (_port_cmd_power_on()). Before this class a single attempt followed
 * the 500 ms pause: had that chain taken longer, the port would have stayed off until a
 * restart, or for good after a normal boot (US-139: no restart there).
 *
 * The owner calls start() right after the power off, then onResult() for each attempt when
 * due(): every FAST_RETRY_MS within FAST_WINDOW_MS (the owner waits there, blocking), then
 * every SLOW_RETRY_MS from the poll loop, without limit. A port left off is worse than a
 * call that keeps failing.
 */
class PortPowerOnRetry {
public:
    enum class Outcome : uint8_t { DONE, RETRY, STILL_OFF };

    static const uint32_t OFF_MS = 500;          // pause before the first attempt
    static const uint32_t FAST_WINDOW_MS = 3000; // after OFF_MS, retries every FAST_RETRY_MS
    static const uint32_t FAST_RETRY_MS = 50;
    static const uint32_t SLOW_RETRY_MS = 1000;

    PortPowerOnRetry() : _pending(false), _warned(false), _offAt(0), _nextAt(0), _attempts(0) {}

    void start(uint32_t now) {
        _pending = true;
        _warned = false;
        _offAt = now;
        _nextAt = now + OFF_MS;
        _attempts = 0;
    }

    bool pending() const { return _pending; }
    bool due(uint32_t now) const { return _pending && (int32_t)(now - _nextAt) >= 0; }
    // Still within the blocking part: OFF_MS plus FAST_WINDOW_MS from the power off
    bool inFastWindow(uint32_t now) const { return _pending && (now - _offAt) < OFF_MS + FAST_WINDOW_MS; }
    // Time until the next attempt, 0 when due
    uint32_t msUntilDue(uint32_t now) const {
        if (!_pending || due(now)) return 0;
        return _nextAt - now;
    }

    /**
     * @param ok the power on was accepted
     * @return DONE, RETRY, or STILL_OFF once, at the first failure past the fast window
     *         (the owner logs a WARN: the port stays off until a later attempt succeeds)
     */
    Outcome onResult(bool ok, uint32_t now) {
        if (!_pending) return Outcome::DONE;
        if (_attempts < 0xFFFF) _attempts++;
        if (ok) {
            _pending = false;
            return Outcome::DONE;
        }
        if ((now - _offAt) < OFF_MS + FAST_WINDOW_MS) {
            _nextAt = now + FAST_RETRY_MS;
            return Outcome::RETRY;
        }
        _nextAt = now + SLOW_RETRY_MS;
        if (!_warned) {
            _warned = true;
            return Outcome::STILL_OFF;
        }
        return Outcome::RETRY;
    }

    uint16_t attempts() const { return _attempts; }
    uint32_t offSince() const { return _offAt; }

private:
    bool _pending;
    bool _warned;
    uint32_t _offAt;
    uint32_t _nextAt;
    uint16_t _attempts;
};

#endif // PORT_POWER_ON_RETRY_H
