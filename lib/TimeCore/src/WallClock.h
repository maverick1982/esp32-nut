#ifndef WALL_CLOCK_H
#define WALL_CLOCK_H

#include <stdint.h>

/**
 * @brief Wall time from the uptime (US-117). Pure logic, native tests.
 *
 * At each NTP synchronization the clock keeps one reference pair: the Unix time in ms
 * and millis() at that moment. The time of any uptime follows from it, also for an
 * uptime before the reference: a log line written before the first synchronization and
 * sent after it gets the time it was written. The difference is taken as int32_t, so
 * the millis() wrap after 49.7 days is handled as long as the two uptimes are less than
 * 24.8 days apart. No locking here: NtpClock guards it.
 */
class WallClock {
public:
    void setSynced(int64_t epoch_ms, uint32_t uptime_ms) {
        _epoch_ms = epoch_ms;
        _uptime_ms = uptime_ms;
        _valid = true;
    }
    void clear() { _valid = false; }
    bool valid() const { return _valid; }

    // Unix time in ms of the given millis(); false while the clock has no reference
    bool epochMsAt(uint32_t uptime_ms, int64_t& out) const {
        if (!_valid) return false;
        out = _epoch_ms + (int32_t)(uptime_ms - _uptime_ms);
        return true;
    }

    // millis() of the last synchronization (valid() only)
    uint32_t syncedAt() const { return _uptime_ms; }

private:
    bool _valid = false;
    int64_t _epoch_ms = 0;
    uint32_t _uptime_ms = 0;
};

#endif // WALL_CLOCK_H
