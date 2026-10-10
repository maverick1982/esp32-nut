#include "LoginThrottle.h"

uint32_t LoginThrottle::retryAfterMs(uint32_t now) const {
    if (failures_ < kFreeAttempts) return 0;
    // Esponente limitato: oltre 6 lo shift supera comunque kMaxDelayMs
    uint32_t exponent = failures_ - kFreeAttempts;
    uint32_t delay = kMaxDelayMs;
    if (exponent < 16) {
        uint32_t shifted = kBaseDelayMs << exponent;
        if (shifted < kMaxDelayMs) delay = shifted;
    }
    uint32_t elapsed = now - last_failure_ms_;
    if (elapsed >= delay) return 0;
    return delay - elapsed;
}

void LoginThrottle::recordFailure(uint32_t now) {
    if (failures_ < UINT32_MAX) failures_++;
    last_failure_ms_ = now;
}

void LoginThrottle::recordSuccess() {
    failures_ = 0;
    last_failure_ms_ = 0;
}
