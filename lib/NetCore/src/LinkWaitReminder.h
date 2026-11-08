#ifndef LINK_WAIT_REMINDER_H
#define LINK_WAIT_REMINDER_H

#include <stdint.h>

/**
 * @brief When to repeat "no Ethernet link" in the log (issue #136).
 *
 * Without a cable a board with Ethernet only has no network at all, and the log said
 * just once "waiting for link". The link adapter logs a reminder after FIRST_MS without
 * link, then every REPEAT_MS, until the link comes up.
 *
 * Pure logic, so it runs in the native unit tests: the caller passes the link state and
 * millis(). Unsigned arithmetic: correct across the millis() wraparound.
 */
class LinkWaitReminder {
public:
    static const uint32_t FIRST_MS = 10000;   // 10 s: a cable that is plugged in links in ~3 s
    static const uint32_t REPEAT_MS = 60000;  // then once a minute

    LinkWaitReminder() : _down(false), _reminded(false), _downSince(0), _lastReminder(0) {}

    // Start of the wait (the adapter has just started, link still down)
    void begin(uint32_t now) {
        _down = true;
        _reminded = false;
        _downSince = now;
    }

    // Seconds without link when a reminder is due (at least 1), 0 otherwise
    uint32_t update(bool link_up, uint32_t now) {
        if (link_up) {
            _down = false;
            return 0;
        }
        if (!_down) begin(now);  // link lost: a new wait
        uint32_t waited = now - _downSince;
        bool due = _reminded ? (now - _lastReminder >= REPEAT_MS) : (waited >= FIRST_MS);
        if (!due) return 0;
        _reminded = true;
        _lastReminder = now;
        uint32_t seconds = waited / 1000;
        return seconds > 0 ? seconds : 1;
    }

private:
    bool _down;
    bool _reminded;
    uint32_t _downSince;
    uint32_t _lastReminder;
};

#endif  // LINK_WAIT_REMINDER_H
