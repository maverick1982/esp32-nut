#ifndef AP_IDLE_TIMEOUT_H
#define AP_IDLE_TIMEOUT_H

#include <stdint.h>

/**
 * @brief Timeout of the manual access point mode (US-064, PDR 0001 D7).
 *
 * Pure logic, so it runs in the native unit tests: the caller passes the number of
 * stations connected to the AP and millis().
 *
 * - Enabled only when the AP was started by the double reset and the configuration is
 *   valid: there is a normal mode to go back to. With a missing or invalid configuration
 *   the AP stays up.
 * - While at least one station is connected the idle period starts again, so the
 *   timeout counts from the moment the last station leaves.
 * - update() reports the expiry once; the caller logs the reason and restarts.
 * - Unsigned arithmetic: correct across the millis() wraparound.
 */
class ApIdleTimeout {
public:
    static const uint32_t DEFAULT_TIMEOUT_MS = 600000; // 10 minutes

    explicit ApIdleTimeout(bool enabled = false, uint32_t timeout_ms = DEFAULT_TIMEOUT_MS)
        : _enabled(enabled), _timeoutMs(timeout_ms), _idleSince(0), _fired(false) {}

    // Start of the first idle period (the AP has just started, no station yet)
    void begin(uint32_t now) {
        _idleSince = now;
        _fired = false;
    }

    // true only once, when the AP has had no station for timeout_ms
    bool update(uint32_t stations, uint32_t now) {
        if (!_enabled || _fired) return false;
        if (stations > 0) {
            _idleSince = now;
            return false;
        }
        if (now - _idleSince >= _timeoutMs) {
            _fired = true;
            return true;
        }
        return false;
    }

    bool isEnabled() const { return _enabled; }

    // Time left before the restart; 0 when disabled or already expired
    uint32_t msRemaining(uint32_t now) const {
        if (!_enabled || _fired) return 0;
        uint32_t idle = now - _idleSince;
        return idle >= _timeoutMs ? 0 : _timeoutMs - idle;
    }

private:
    bool _enabled;
    uint32_t _timeoutMs;
    uint32_t _idleSince;
    bool _fired;
};

#endif // AP_IDLE_TIMEOUT_H
