#ifndef INPUT_ERROR_BACKOFF_H
#define INPUT_ERROR_BACKOFF_H

#include <stdint.h>

/**
 * @brief Spacing of the interface restarts after errors on the interrupt IN pipe (issue #36).
 *
 * Pure logic, so it runs in the native unit tests. A bus error on the IN pipe leaves it
 * stopped until the interface is restarted. The restarts used to follow each other with
 * no wait, so four errors in 0.4 s restarted the board, while the UPS was dropping off the
 * bus and would have come back by itself about a second later (Powercom SPD-750U).
 *
 * As the Linux usbhid driver (hid_io_error() in hid-core.c: retries at 13, 26, 52, 104 ms
 * for 1 s, then a port reset):
 * - each restart waits FIRST_DELAY_MS, doubled after every error up to MAX_DELAY_MS;
 * - the board restart is asked only when the errors go on for ESCALATE_AFTER_MS and after
 *   MIN_RECOVERIES restarts, which leaves the time for a disconnection to arrive;
 * - an INPUT report (onInput()), a new claim or a disconnection (reset()) end the streak,
 *   and so does QUIET_RESET_MS without errors: an error after that is a new one.
 *
 * Issue #163: a Vertiv Edge plugged in while the board runs gets its IN pipe stuck, and
 * only the board restart (a new enumeration) brings it back. Before the restart the first
 * escalation asks for a USB port power cycle (CYCLE_PORT), which enumerates the UPS again
 * while network, web UI and NUT keep running. One cycle per PORT_CYCLE_GAP_MS: an
 * escalation closer to the last cycle asks for the restart. reset() does not forget the
 * cycle, since the cycle itself ends the streak with a disconnection.
 */
class InputErrorBackoff {
public:
    static const uint32_t FIRST_DELAY_MS = 100;
    static const uint32_t MAX_DELAY_MS = 2000;
    static const uint32_t ESCALATE_AFTER_MS = 10000;
    static const uint8_t MIN_RECOVERIES = 3;
    static const uint32_t QUIET_RESET_MS = 30000;
    static const uint32_t PORT_CYCLE_GAP_MS = 600000;

    enum class Action : uint8_t { RECOVER, CYCLE_PORT, RESTART };

    InputErrorBackoff() : _cycled(false), _cycledAt(0) { reset(); }

    /**
     * @brief The IN pipe reported an error.
     * @return RECOVER: restart the interface when due(); CYCLE_PORT: the errors went on too
     *         long, power-cycle the USB port; RESTART: they went on too long again within
     *         PORT_CYCLE_GAP_MS of the last cycle
     */
    Action onError(uint32_t now) {
        if (_recoveries > 0 && now - _lastErrorAt >= QUIET_RESET_MS) reset();
        _lastErrorAt = now;
        if (_recoveries == 0) {
            _streakStart = now;
            _delay = FIRST_DELAY_MS;
        } else {
            if (_recoveries >= MIN_RECOVERIES && now - _streakStart >= ESCALATE_AFTER_MS) {
                _pending = false;
                if (_cycled && now - _cycledAt < PORT_CYCLE_GAP_MS) return Action::RESTART;
                _cycled = true;
                _cycledAt = now;
                return Action::CYCLE_PORT;
            }
            _delay = (_delay * 2 > MAX_DELAY_MS) ? MAX_DELAY_MS : _delay * 2;
        }
        if (_recoveries < 255) _recoveries++;
        _pending = true;
        _recoverAt = now + _delay;
        return Action::RECOVER;
    }

    // An interface restart is waiting for its time
    bool pending() const { return _pending; }
    bool due(uint32_t now) const { return _pending && (int32_t)(now - _recoverAt) >= 0; }
    // The restart is starting: the streak stays until reset()
    void onRecoveryStarted() { _pending = false; }
    // An INPUT report proves the pipe works and ends the streak. One queued before the error
    // does not cancel the restart that is waiting: the pipe stays stopped without it.
    void onInput() {
        if (!_pending) reset();
    }

    uint8_t recoveries() const { return _recoveries; }
    uint32_t delayMs() const { return _delay; }
    uint32_t streakMs(uint32_t now) const { return _recoveries ? now - _streakStart : 0; }

    void reset() {
        _pending = false;
        _recoveries = 0;
        _delay = 0;
        _streakStart = 0;
        _lastErrorAt = 0;
        _recoverAt = 0;
    }

private:
    bool _cycled;       // a CYCLE_PORT was returned once, at _cycledAt (kept by reset())
    uint32_t _cycledAt;
    bool _pending;
    uint8_t _recoveries;
    uint32_t _delay;
    uint32_t _streakStart;
    uint32_t _lastErrorAt;
    uint32_t _recoverAt;
};

#endif // INPUT_ERROR_BACKOFF_H
