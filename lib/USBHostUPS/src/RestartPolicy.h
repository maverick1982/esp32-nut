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
 * - HEALTHY_RESET_MS of fresh data clear the counter.
 */
class RestartPolicy {
public:
    enum class Decision : uint8_t { NONE, WAIT, RESTART, DEGRADED };

    static const uint8_t MAX_RESTARTS = 4;
    static const uint32_t HEALTHY_RESET_MS = 600000;

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
     */
    Decision update(bool restartRequested, bool healthy, uint32_t now) {
        trackHealth(healthy, now);

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
    void trackHealth(bool healthy, uint32_t now) {
        if (!healthy) {
            _healthy = false;
            return;
        }
        if (!_healthy) {
            _healthy = true;
            _healthySince = now;
        }
        if (_consecutive > 0 && (now - _healthySince) >= HEALTHY_RESET_MS) {
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
