#ifndef TIME_STATUS_H
#define TIME_STATUS_H

#include <stdint.h>

/**
 * @brief State of the network time for the Web UI, and the System Log lines about it
 * (US-117). Pure logic, native tests.
 *
 * Disabled: NTP turned off. Waiting: NTP on, no time yet (no network, or the first
 * answer has not come). Synchronized: the board has the time. NoAnswer: the network is
 * up but the server has not answered for NO_ANSWER_MS; SNTP keeps retrying.
 */
enum class TimeState : uint8_t { Disabled, Waiting, Synchronized, NoAnswer };

inline const char* timeStateName(TimeState s) {
    switch (s) {
    case TimeState::Waiting: return "waiting";
    case TimeState::Synchronized: return "synchronized";
    case TimeState::NoAnswer: return "no_answer";
    default: return "disabled";
    }
}

/**
 * Decides when the System Log gets a line: one at the first synchronization after the
 * start of SNTP (boot or new settings), one only per failure period (no answer within
 * NO_ANSWER_MS with the network up), one again at the synchronization that ends it. The
 * periodic synchronizations of SNTP (every 3 h) write nothing.
 */
class TimeLogPolicy {
public:
    enum class Line : uint8_t { None, Synchronized, NoAnswer };

    static const uint32_t NO_ANSWER_MS = 60000;

    // SNTP (re)started: boot, new settings
    void restart(uint32_t now) {
        _waiting = true;
        _started = now;
        _no_answer = false;
        _synced_once = false;
    }

    // NTP turned off: no lines until the next restart()
    void stop() {
        _waiting = false;
        _no_answer = false;
    }

    // Once per loop. Without network the wait does not count: it starts again when the
    // network comes back
    Line tick(uint32_t now, bool network_up) {
        if (!_waiting) return Line::None;
        if (!network_up) {
            _started = now;
            return Line::None;
        }
        if (_no_answer || (uint32_t)(now - _started) < NO_ANSWER_MS) return Line::None;
        _no_answer = true;
        return Line::NoAnswer;
    }

    // An answer from the server
    Line onSynced() {
        bool log = !_synced_once || _no_answer;
        _waiting = false;
        _synced_once = true;
        _no_answer = false;
        return log ? Line::Synchronized : Line::None;
    }

    // A failure period is open (for TimeState::NoAnswer)
    bool noAnswer() const { return _no_answer; }

private:
    bool _waiting = false;
    bool _no_answer = false;
    bool _synced_once = false;
    uint32_t _started = 0;
};

#endif // TIME_STATUS_H
