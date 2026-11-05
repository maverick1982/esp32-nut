#ifndef PORT_CYCLE_POLICY_H
#define PORT_CYCLE_POLICY_H

#include <stdint.h>

/**
 * @brief Recover a stuck control transfer with a root port power cycle instead of a
 *        restart (US-122, issue #120).
 *
 * Pure logic, so it runs in the native unit tests, like LinkMonitor and RestartPolicy.
 *
 * Some UPS (APC Smart-UPS, issues #78 and #120) now and then leave a GET_REPORT
 * unanswered. libusb on a PC abandons it; the ESP-IDF stack keeps the URB in flight on
 * EP0, and until US-122 only a restart freed it. Switching the root port off makes the
 * device go away: the USBH halts and flushes EP0 and hands the stuck URB back with its
 * callback before it reports DEV_GONE (usbh.c, ADR 0008), so the UPS re-enumerates in a
 * few seconds while network, web UI and NUT keep running.
 *
 * The owner asks onStuck() when LinkMonitor wants a restart for a stuck control transfer:
 * - CYCLE_PORT: power-cycle the port and wait until the control pipe is proven again
 *   (onProven), at most BACK_TIMEOUT_MS (backTimedOut);
 * - RESTART, the safety net, when the cycle did not help: stuck again while still
 *   waiting for the UPS, or MAX_CYCLES_PER_WINDOW cycles already in the last WINDOW_MS.
 *   lastRestartReason() tells which.
 */
class PortCyclePolicy {
public:
    enum class Decision : uint8_t { CYCLE_PORT, RESTART };

    static const uint32_t BACK_TIMEOUT_MS = 30000;
    static const uint8_t MAX_CYCLES_PER_WINDOW = 12;
    static const uint32_t WINDOW_MS = 3600000;

    // Restart reasons, at most 64 bytes (recordRestartReason)
    static constexpr const char* STUCK_AGAIN_REASON =
        "control transfer stuck again after the USB port power cycle";
    static constexpr const char* TOO_MANY_REASON = "too many USB port power cycles in an hour";
    static constexpr const char* NOT_BACK_REASON = "UPS not back after the USB port power cycle";

    PortCyclePolicy()
        : _count(0), _next(0), _waiting(false), _timeoutReported(false), _cycleAt(0),
          _cyclesSinceBoot(0), _lastRestartReason(nullptr) {}

    Decision onStuck(uint32_t now) {
        if (_waiting) {
            _lastRestartReason = STUCK_AGAIN_REASON;
            return Decision::RESTART;
        }
        // _next is the oldest stamp once the ring is full; unsigned difference: wrap-safe
        if (_count == MAX_CYCLES_PER_WINDOW && (now - _stamps[_next]) < WINDOW_MS) {
            _lastRestartReason = TOO_MANY_REASON;
            return Decision::RESTART;
        }
        _stamps[_next] = now;
        _next = (uint8_t)((_next + 1) % MAX_CYCLES_PER_WINDOW);
        if (_count < MAX_CYCLES_PER_WINDOW) _count++;
        if (_cyclesSinceBoot < 0xFFFFFFFFu) _cyclesSinceBoot++;
        _waiting = true;
        _timeoutReported = false;
        _cycleAt = now;
        return Decision::CYCLE_PORT;
    }

    // The control pipe answered again after the cycle
    void onProven() { _waiting = false; }

    // True once per cycle, when the UPS is not back within BACK_TIMEOUT_MS
    bool backTimedOut(uint32_t now) {
        if (!_waiting || _timeoutReported) return false;
        if ((now - _cycleAt) < BACK_TIMEOUT_MS) return false;
        _timeoutReported = true;
        return true;
    }

    bool waiting() const { return _waiting; }
    uint32_t cycleStartedAt() const { return _cycleAt; }
    uint32_t cyclesSinceBoot() const { return _cyclesSinceBoot; }
    const char* lastRestartReason() const { return _lastRestartReason; }

private:
    uint32_t _stamps[MAX_CYCLES_PER_WINDOW];
    uint8_t _count;
    uint8_t _next;
    bool _waiting;
    bool _timeoutReported;
    uint32_t _cycleAt;
    uint32_t _cyclesSinceBoot;
    const char* _lastRestartReason;
};

#endif // PORT_CYCLE_POLICY_H
