#ifndef LOG_OUTPUT_FILTER_H
#define LOG_OUTPUT_FILTER_H

#include <stdint.h>
#include "LogLevel.h"

/**
 * @brief Minimum level and rate limit of one log output (issue #69).
 *
 * Pure logic, so it runs in the native unit tests. Each output (syslog today, MQTT
 * later) has its own filter, so a burst of USB errors cannot flood the receiver.
 * Token bucket: `rate` lines per second on average, bursts up to `burst()` lines (the
 * boot log arrives all at once when the network comes up). Lines refused by the rate
 * limit are counted; takeSuppressed() returns the count once, so the output can send
 * one summary line instead of the missing ones.
 */
class LogOutputFilter {
public:
    enum class Decision : uint8_t { SEND, BELOW_LEVEL, RATE_LIMITED };

    static constexpr uint16_t MIN_RATE = 1;
    static constexpr uint16_t MAX_RATE = 100;
    static constexpr uint16_t DEFAULT_RATE = 10;

    LogOutputFilter() { configure(LogLevel::INFO, DEFAULT_RATE); }

    void configure(LogLevel min_level, uint16_t rate) {
        if (rate < MIN_RATE) rate = MIN_RATE;
        if (rate > MAX_RATE) rate = MAX_RATE;
        _min_level = min_level;
        _rate = rate;
        _tokens_milli = (uint32_t)burst() * 1000u;
        _last_ms = 0;
        _started = false;
    }

    LogLevel minLevel() const { return _min_level; }
    uint16_t rate() const { return _rate; }
    // Four seconds of traffic, at least 20 lines
    uint16_t burst() const { return _rate * 4 < 20 ? 20 : _rate * 4; }

    Decision check(LogLevel level, uint32_t now_ms) {
        if ((uint8_t)level < (uint8_t)_min_level) return Decision::BELOW_LEVEL;
        refill(now_ms);
        if (_tokens_milli < 1000u) {
            _suppressed++;
            return Decision::RATE_LIMITED;
        }
        _tokens_milli -= 1000u;
        return Decision::SEND;
    }

    // Lines refused by the rate limit since the last call
    uint32_t takeSuppressed() {
        uint32_t n = _suppressed;
        _suppressed = 0;
        return n;
    }

    uint32_t pendingSuppressed() const { return _suppressed; }

private:
    void refill(uint32_t now_ms) {
        if (!_started) {
            _started = true;
            _last_ms = now_ms;
            return;
        }
        uint32_t elapsed = now_ms - _last_ms;  // wraps correctly with millis()
        _last_ms = now_ms;
        uint64_t add = (uint64_t)elapsed * _rate;  // tokens * 1000 per ms = rate
        uint64_t cap = (uint64_t)burst() * 1000u;
        uint64_t t = _tokens_milli + add;
        _tokens_milli = (uint32_t)(t > cap ? cap : t);
    }

    LogLevel _min_level = LogLevel::INFO;
    uint16_t _rate = DEFAULT_RATE;
    uint32_t _tokens_milli = 0;
    uint32_t _last_ms = 0;
    bool _started = false;
    uint32_t _suppressed = 0;
};

#endif // LOG_OUTPUT_FILTER_H
