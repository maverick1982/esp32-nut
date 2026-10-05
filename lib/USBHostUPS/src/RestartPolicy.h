#ifndef RESTART_POLICY_H
#define RESTART_POLICY_H

#include <stdint.h>

/**
 * @brief Guard against boot loops of the controlled restarts (review A7).
 *
 * Pure logic, so it runs in the native unit tests. The number of consecutive
 * controlled restarts survives esp_restart() in RTC memory (CrashDiag).
 *
 * - The first restart runs at once: the USB recovery ladder has already spent minutes.
 * - The next ones wait 1, 5 and 15 minutes. A new enumeration of the UPS in the
 *   meantime clears the request and cancels the wait.
 * - After MAX_RESTARTS the board enters degraded mode: no more restarts, data stale,
 *   a banner in the web UI. Only a new enumeration (or a manual reboot) gets it out.
 * - Fresh data clear the counter, since the restart worked: PROVEN_RESET_MS (30 s) when
 *   the control pipe is proven too (a full poll answered by the UPS), HEALTHY_RESET_MS
 *   (2 min) otherwise, e.g. on a device that only sends INPUT reports.
 *   An APC Back-UPS BX locks EP0 from 1 minute to hours after a boot, and only a
 *   restart frees it (issue #60). With 10 minutes, then 2, a lockup soon after a restart
 *   delayed the next one by 1 or 5 minutes of stale data. A device that fails within
 *   30 s of every boot still walks the 1/5/15 min ladder up to degraded mode.
 */
class RestartPolicy {
public:
    enum class Decision : uint8_t { NONE, WAIT, RESTART, DEGRADED };

    static const uint8_t MAX_RESTARTS = 4;
    static const uint32_t HEALTHY_RESET_MS = 120000;
    static const uint32_t PROVEN_RESET_MS = 30000;

    static uint32_t delayMs(uint8_t consecutive) {
        static const uint32_t delays[MAX_RESTARTS] = {0, 60000, 300000, 900000};
        return consecutive < MAX_RESTARTS ? delays[consecutive] : 0;
    }

    explicit RestartPolicy(uint8_t consecutive = 0)
        : _consecutive(consecutive), _scheduled(false), _restartAt(0),
          _healthy(false), _healthySince(0), _cleared(false) {}

    /**
     * @param restartRequested USBHostUPS asks for a restart (recovery exhausted)
     * @param healthy          device attached and data fresh
     * @param proven           the control pipe answered a full poll since the claim
     */
    Decision update(bool restartRequested, bool healthy, uint32_t now, bool proven = false) {
        trackHealth(healthy, proven, now);

        if (!restartRequested) {
            _scheduled = false; // cancelled by a new enumeration
            return Decision::NONE;
        }
        if (isDegraded()) return Decision::DEGRADED;
        if (!_scheduled) {
            _scheduled = true;
            _restartAt = now + delayMs(_consecutive);
        }
        return ((int32_t)(now - _restartAt) >= 0) ? Decision::RESTART : Decision::WAIT;
    }

    bool isDegraded() const { return _consecutive >= MAX_RESTARTS; }
    uint8_t consecutive() const { return _consecutive; }
    uint32_t msUntilRestart(uint32_t now) const {
        return (_scheduled && (int32_t)(_restartAt - now) > 0) ? _restartAt - now : 0;
    }

    // True once after the counter was cleared: the owner resets the RTC copy
    bool takeCleared() {
        bool c = _cleared;
        _cleared = false;
        return c;
    }

private:
    void trackHealth(bool healthy, bool proven, uint32_t now) {
        if (!healthy) {
            _healthy = false;
            return;
        }
        if (!_healthy) {
            _healthy = true;
            _healthySince = now;
        }
        uint32_t needed = proven ? PROVEN_RESET_MS : HEALTHY_RESET_MS;
        if (_consecutive > 0 && (now - _healthySince) >= needed) {
            _consecutive = 0;
            _cleared = true;
        }
    }

    uint8_t _consecutive;
    bool _scheduled;
    uint32_t _restartAt;
    bool _healthy;
    uint32_t _healthySince;
    bool _cleared;
};

#endif // RESTART_POLICY_H
