#ifndef INPUT_SILENCE_GUARD_H
#define INPUT_SILENCE_GUARD_H

#include <stdint.h>

/**
 * A silent interrupt pipe on a UPS whose silence means frozen values (issue #138).
 *
 * The Back-UPS BX750MI sends about 18 INPUT reports per second. After tens of minutes or
 * hours it can stop sending them while it keeps answering GET_REPORT with the values of
 * that moment, until the cable is unplugged. Its continuous stream is never "periodic" for
 * the InputWatchdog (one endless burst), and the poll covers its INPUT values (US-123), so
 * nothing noticed: before v1.14.0 the restarts of issue #60 cleared the state.
 *
 * Here a silence of silenceMs after at least one INPUT report since the claim makes the
 * data stale and asks for an interface restart; if the reports are still missing retryMs
 * later, for a system restart (RestartPolicy still limits those). silenceMs 0 (every
 * other UPS) turns the guard off. Pure logic: the owner passes the time and acts.
 */
class InputSilenceGuard {
public:
    enum class Action : uint8_t { NONE, RECOVER, RESTART };

    static const uint32_t RETRY_MS = 60000;

    InputSilenceGuard() { reset(0); }

    // New device, or a claim: unarmed until its first INPUT report
    void reset(uint32_t now) {
        _armed = false;
        _lastInputMs = now;
        _recovered = false;
        _recoverAt = 0;
        _restartAsked = false;
    }

    // ts: when the HID task received the report
    void onInput(uint32_t ts) {
        _armed = true;
        _lastInputMs = ts;
        _recovered = false;
        _restartAsked = false;
    }

    // Set by the owner from the driver before each tick(); 0 = off
    void setSilenceMs(uint32_t ms) { _silenceMs = ms; }
    uint32_t silenceMs() const { return _silenceMs; }

    bool isSilent(uint32_t now) const {
        if (_silenceMs == 0 || !_armed) return false;
        // Signed: a report stamped by the HID task after the owner read the time
        int32_t quiet = (int32_t)(now - _lastInputMs);
        return quiet >= 0 && (uint32_t)quiet >= _silenceMs;
    }

    uint32_t silentForMs(uint32_t now) const {
        return isSilent(now) ? now - _lastInputMs : 0;
    }

    Action tick(uint32_t now) {
        if (!isSilent(now)) return Action::NONE;
        if (!_recovered) {
            _recovered = true;
            _recoverAt = now;
            return Action::RECOVER;
        }
        if (_restartAsked || (now - _recoverAt) < RETRY_MS) return Action::NONE;
        _restartAsked = true;
        return Action::RESTART;
    }

private:
    uint32_t _silenceMs = 0;
    bool _armed;
    uint32_t _lastInputMs;
    bool _recovered;
    uint32_t _recoverAt;
    bool _restartAsked;
};

#endif // INPUT_SILENCE_GUARD_H
