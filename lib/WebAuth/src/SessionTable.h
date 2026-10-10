#ifndef SESSION_TABLE_H
#define SESSION_TABLE_H

#include <Arduino.h>
#include "AuthPlatform.h"

// Sessioni volatili in RAM (ADR 0009, PDR D2): si salva solo SHA-256 del token,
// scadenza dopo 30 minuti senza richieste, tutte perse al riavvio
class SessionTable {
public:
    static constexpr size_t kMaxSessions = 8;
    static constexpr uint32_t kIdleTimeoutMs = 30UL * 60UL * 1000UL;
    static constexpr size_t kTokenBytes = 16;
    static constexpr size_t kTokenLength = kTokenBytes * 2;

    explicit SessionTable(IAuthCrypto& crypto);

    // Nuova sessione: restituisce il token esadecimale (32 caratteri)
    String create(uint32_t now);
    // Valida e aggiorna last_seen; una sessione scaduta viene liberata
    bool validate(const String& token, uint32_t now);
    void remove(const String& token);
    void clear();
    size_t activeCount(uint32_t now) const;

private:
    struct Slot {
        uint8_t hash[32];
        uint32_t last_seen_ms;
        bool used;
    };

    // Indice dello slot con quell'hash, -1 se assente (scansione completa)
    int findSlot(const String& token);
    bool isExpired(const Slot& slot, uint32_t now) const;

    IAuthCrypto& crypto_;
    Slot slots_[kMaxSessions];
};

#endif
