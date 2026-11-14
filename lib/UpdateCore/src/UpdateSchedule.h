#ifndef UPDATE_SCHEDULE_H
#define UPDATE_SCHEDULE_H

#include <stdint.h>

// When the automatic update check runs, and how often a manual one is allowed
class UpdateSchedule {
public:
    static constexpr uint32_t FIRST_CHECK_MS = 2UL * 60UL * 1000UL;
    static constexpr uint32_t INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;
    static constexpr uint32_t RETRY_MS = 60UL * 60UL * 1000UL;
    static constexpr uint32_t MANUAL_COOLDOWN_MS = 30UL * 1000UL;

    void setAuto(bool enabled) { _auto = enabled; }
    bool autoEnabled() const { return _auto; }

    // First call with the network up schedules the first check
    bool due(uint32_t now, bool network_up) {
        if (!network_up) return false;
        if (!_scheduled) {
            _scheduled = true;
            _next = now + FIRST_CHECK_MS;
            return false;
        }
        return _auto && (int32_t)(now - _next) >= 0;
    }

    // Any check, automatic or manual
    void checked(uint32_t now, bool ok) {
        _scheduled = true;
        _next = now + (ok ? INTERVAL_MS : RETRY_MS);
    }

    bool manualAllowed(uint32_t now) const {
        return !_manual_used || now - _last_manual >= MANUAL_COOLDOWN_MS;
    }

    void manualStarted(uint32_t now) {
        _manual_used = true;
        _last_manual = now;
    }

private:
    bool _auto = true;
    bool _scheduled = false;
    uint32_t _next = 0;
    bool _manual_used = false;
    uint32_t _last_manual = 0;
};

#endif // UPDATE_SCHEDULE_H
