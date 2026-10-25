#ifndef RECOVERY_WINDOW_H
#define RECOVERY_WINDOW_H

#include <stdint.h>

/**
 * @brief Recovery window of the web credentials on boards without Wi-Fi (US-091,
 * PDR 0002 D5, ADR 0010 §5).
 *
 * Pure logic, so it runs in the native unit tests. Those boards have no AP recovery:
 * the double reset opens this window instead, and while it is open new web credentials
 * can be created without the current password.
 *
 * - The double reset does not erase anything: if nobody creates new credentials, the
 *   window expires and the current ones stay valid.
 * - It closes when it expires (update() reports it once) or after use (markUsed()).
 * - RAM only: a restart closes it.
 * - Unsigned arithmetic: correct across the millis() wraparound.
 */
class RecoveryWindow {
public:
    // Same value as the manual AP idle timeout (ApIdleTimeout)
    static const uint32_t DEFAULT_DURATION_MS = 600000; // 10 minutes

    enum class Event { NONE, EXPIRED };

    explicit RecoveryWindow(uint32_t duration_ms = DEFAULT_DURATION_MS)
        : _durationMs(duration_ms), _openedAt(0), _open(false) {}

    void open(uint32_t now) {
        _openedAt = now;
        _open = true;
    }

    bool isOpen(uint32_t now) const {
        return _open && now - _openedAt < _durationMs;
    }

    uint32_t remainingMs(uint32_t now) const {
        return isOpen(now) ? _durationMs - (now - _openedAt) : 0;
    }

    // New credentials were created: close without an expiry event
    void markUsed() { _open = false; }

    // EXPIRED once, when the window runs out unused
    Event update(uint32_t now) {
        if (_open && now - _openedAt >= _durationMs) {
            _open = false;
            return Event::EXPIRED;
        }
        return Event::NONE;
    }

    uint32_t durationMs() const { return _durationMs; }

private:
    uint32_t _durationMs;
    uint32_t _openedAt;
    bool _open;
};

#endif // RECOVERY_WINDOW_H
