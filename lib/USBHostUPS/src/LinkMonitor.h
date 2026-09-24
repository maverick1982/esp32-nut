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

/**
 * @brief Liveness check of the interrupt IN pipe (review A2).
 *
 * LinkMonitor only sees EP0. If INPUT reports stop without any error, the values they
 * carry stay frozen and never become stale: the worst case is QUIRK_NO_GET_REPORT
 * (APC 5G, Smart-UPS), where INPUT reports are the only source of data.
 *
 * The watchdog learns the period of the device. Reports closer than burstGapMs form one
 * burst (a device sends several report IDs at once), and the intervals between bursts
 * are kept. The device is *periodic* once `samples` intervals agree within 1/4 of their
 * median. Only then a silence longer than silenceFactor x median (at least minSilenceMs)
 * makes the data stale and asks for an interface recovery, then a restart, with the same
 * ladder as LinkMonitor. Devices that send only on changes are never periodic, so the
 * watchdog never acts on them.
 */
class InputWatchdog {
public:
    typedef LinkMonitor::Action Action;
    static const uint8_t SAMPLES = 8;

    struct Config {
        uint32_t burstGapMs;
        uint32_t silenceFactor;
        uint32_t minSilenceMs;
        uint32_t retryMs;
        uint8_t maxRecoveries;
    };

    static Config defaultConfig() {
        return Config{500, 5, 30000, 60000, 2};
    }

    InputWatchdog() : InputWatchdog(defaultConfig()) {}
    explicit InputWatchdog(const Config& cfg) : _cfg(cfg) { reset(0); }

    // New device: forget the learned period
    void reset(uint32_t now) {
        _count = 0;
        _next = 0;
        _haveBurst = false;
        _lastBurstMs = now;
        _lastInputMs = now;
        _silent = false;
        _recoveries = 0;
        _windowStartMs = now;
    }

    // ts: when the HID task received the report, not when the loopTask processed it
    void onInput(uint32_t ts) {
        bool periodic = isPeriodic();
        uint32_t threshold = silenceThresholdMs();
        _silent = false;
        _recoveries = 0;

        if (_haveBurst && (ts - _lastInputMs) < _cfg.burstGapMs) {
            _lastInputMs = ts; // same burst
            return;
        }
        if (_haveBurst) {
            uint32_t interval = ts - _lastBurstMs;
            // The gap of a silence that was detected is not a period: keep the learned one
            if (!(periodic && interval >= threshold)) {
                _samples[_next] = interval;
                _next = (_next + 1) % SAMPLES;
                if (_count < SAMPLES) _count++;
            }
        }
        _haveBurst = true;
        _lastBurstMs = ts;
        _lastInputMs = ts;
    }

    bool isPeriodic() const {
        if (_count < SAMPLES) return false;
        uint32_t med = median();
        if (med == 0) return false;
        uint32_t lo = _samples[0], hi = _samples[0];
        for (uint8_t i = 1; i < SAMPLES; i++) {
            if (_samples[i] < lo) lo = _samples[i];
            if (_samples[i] > hi) hi = _samples[i];
        }
        uint32_t tol = med / 4;
        return (med - lo) <= tol && (hi - med) <= tol;
    }

    uint32_t periodMs() const { return _count ? median() : 0; }

    uint32_t silenceThresholdMs() const {
        uint32_t t = periodMs() * _cfg.silenceFactor;
        return t > _cfg.minSilenceMs ? t : _cfg.minSilenceMs;
    }

    bool isStale(uint32_t now) const {
        return isPeriodic() && (now - _lastInputMs) >= silenceThresholdMs();
    }

    Action tick(uint32_t now) {
        if (!isStale(now)) return Action::NONE;
        if (!_silent) {
            _silent = true;
        } else if ((now - _windowStartMs) < _cfg.retryMs) {
            return Action::NONE;
        }
        _windowStartMs = now;
        if (_recoveries >= _cfg.maxRecoveries) return Action::RESTART;
        _recoveries++;
        return Action::RECOVER;
    }

    uint8_t recoveries() const { return _recoveries; }

private:
    uint32_t median() const {
        uint32_t s[SAMPLES];
        uint8_t n = _count;
        for (uint8_t i = 0; i < n; i++) s[i] = _samples[i];
        for (uint8_t i = 1; i < n; i++) {
            uint32_t v = s[i];
            int8_t j = i - 1;
            while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; }
            s[j + 1] = v;
        }
        return s[n / 2];
    }

    Config _cfg;
    uint32_t _samples[SAMPLES];
    uint8_t _count;
    uint8_t _next;
    bool _haveBurst;
    uint32_t _lastBurstMs;
    uint32_t _lastInputMs;
    bool _silent;
    uint8_t _recoveries;
    uint32_t _windowStartMs;
};

#endif // LINK_MONITOR_H
