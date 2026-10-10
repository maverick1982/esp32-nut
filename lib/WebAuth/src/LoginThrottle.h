#ifndef LOGIN_THROTTLE_H
#define LOGIN_THROTTLE_H

#include <stdint.h>

// Limite globale ai tentativi di login falliti (PDR D9): dopo il 5° fallimento
// consecutivo bisogna attendere 1 s, 2 s, 4 s … fino a 60 s dall'ultimo
// fallimento. Nessuna attesa bloccante: il chiamante risponde 429 finché
// retryAfterMs() non torna 0. Aritmetica uint32_t sicura al rollover di millis()
class LoginThrottle {
public:
    static constexpr uint32_t kFreeAttempts = 5;
    static constexpr uint32_t kBaseDelayMs = 1000;
    static constexpr uint32_t kMaxDelayMs = 60000;

    // Millisecondi da attendere prima di un nuovo tentativo, 0 se è consentito
    uint32_t retryAfterMs(uint32_t now) const;
    void recordFailure(uint32_t now);
    void recordSuccess();
    // Fallimenti consecutivi: ogni finestra di attesa ne ha un valore diverso
    uint32_t failures() const { return failures_; }

private:
    uint32_t failures_ = 0;
    uint32_t last_failure_ms_ = 0;
};

#endif
