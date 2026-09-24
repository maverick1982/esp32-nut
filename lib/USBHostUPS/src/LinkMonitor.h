#ifndef LINK_MONITOR_H
#define LINK_MONITOR_H

#include <stdint.h>

/**
 * @brief Health tracker for the USB control pipe (issue #47, ADR 0008).
 *
 * Pure logic, no hardware dependency, so it runs in the native unit tests.
 *
 * - "Alive": the device answered a control request, even with a STALL.
 *   A device that rejects some reports is healthy and must never be reset.
 * - "Link failure": no answer at all (timeout, pipe still busy, bus error).
 *
 * After a link failure, polling backs off exponentially. If the pipe does not come
 * back within linkTimeoutMs, the monitor first asks for an interface recovery
 * (up to maxRecoveries times), then for a controlled restart.
 * Devices that only stream INPUT reports never issue control requests, so they
 * never accumulate failures and are never reset.
 */
class LinkMonitor {
public:
    enum class Action : uint8_t { NONE, RECOVER, RESTART };

    struct Config {
        uint32_t backoffBaseMs;
        uint32_t backoffMaxMs;
        uint32_t staleAfterMs;
        uint32_t linkTimeoutMs;
        uint8_t maxRecoveries;
    };

    static Config defaultConfig() {
        return Config{2000, 30000, 20000, 60000, 2};
    }

    LinkMonitor() : LinkMonitor(defaultConfig()) {}
    explicit LinkMonitor(const Config& cfg) : _cfg(cfg) { reset(0); }

    void reset(uint32_t now) {
        _failures = 0;
        _recoveries = 0;
        _lastAliveMs = now;
        _windowStartMs = now;
        _backoffUntilMs = now;
        _backoffActive = false;
    }

    void onAlive(uint32_t now) {
        _failures = 0;
        _recoveries = 0;
        _lastAliveMs = now;
        _windowStartMs = now;
        _backoffActive = false;
    }

    void onLinkFailure(uint32_t now) {
        if (_failures < 255) _failures++;
        uint32_t backoff = _cfg.backoffBaseMs;
        for (uint8_t i = 1; i < _failures && backoff < _cfg.backoffMaxMs; i++) backoff *= 2;
        if (backoff > _cfg.backoffMaxMs) backoff = _cfg.backoffMaxMs;
        _backoffUntilMs = now + backoff;
        _backoffActive = true;
    }

    bool canPoll(uint32_t now) const {
        return !_backoffActive || (int32_t)(now - _backoffUntilMs) >= 0;
    }

    bool isStale(uint32_t now) const {
        return _failures > 0 && (now - _lastAliveMs) >= _cfg.staleAfterMs;
    }

    /**
     * With no device attached nothing is current (review A1): like usbhid-ups + upsd,
     * NUT clients must get ERR DATA-STALE, not an empty "Unknown" status. A UPS that
     * resets its USB port when it goes on battery would otherwise never show OB.
     * Only the first bootGraceMs after boot are exempt, while the UPS enumerates.
     */
    static bool isStaleWithoutDevice(bool deviceSeen, uint32_t now, uint32_t bootGraceMs) {
        return deviceSeen || now >= bootGraceMs;
    }

    Action tick(uint32_t now) {
        if (_failures == 0 || (now - _windowStartMs) < _cfg.linkTimeoutMs) return Action::NONE;
        if (_recoveries >= _cfg.maxRecoveries) return Action::RESTART;
        _recoveries++;
        // Give the recovered interface a full window before escalating again.
        // _lastAliveMs is left untouched: data stays stale until the device answers.
        _windowStartMs = now;
        _backoffActive = false;
        return Action::RECOVER;
    }

    uint8_t failures() const { return _failures; }
    uint8_t recoveries() const { return _recoveries; }

private:
    Config _cfg;
    uint8_t _failures;
    uint8_t _recoveries;
    uint32_t _lastAliveMs;
    uint32_t _windowStartMs;
    uint32_t _backoffUntilMs;
    bool _backoffActive;
};

#endif // LINK_MONITOR_H
