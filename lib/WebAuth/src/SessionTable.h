#ifndef SESSION_TABLE_H
#define SESSION_TABLE_H

#include <Arduino.h>
#include "AuthPlatform.h"

// Sessioni della Web UI (ADR 0009, PDR D2); di ogni token si salva solo SHA-256.
// - volatili: in RAM, scadenza dopo 30 minuti senza richieste, perse al riavvio
// - persistenti ("Remember me"): salvate tramite IAuthStorage, senza scadenza per
//   inattività lato server (i 30 giorni li applica il browser con Max-Age); se gli
//   slot sono pieni si sostituisce quella con seq più basso, cioè la più vecchia
// Lo storage si scrive solo in create persistente, remove di una persistente, clear
// e rotateExclusive (quando cambia l'insieme delle persistenti).
class SessionTable {
public:
    static constexpr size_t kMaxSessions = 8;
    static constexpr size_t kMaxPersistentSessions = 8;
    static constexpr uint32_t kIdleTimeoutMs = 30UL * 60UL * 1000UL;
    static constexpr size_t kTokenBytes = 16;
    static constexpr size_t kTokenLength = kTokenBytes * 2;

    SessionTable(IAuthCrypto& crypto, IAuthStorage& storage);

    // Carica le sessioni persistenti dallo storage; se le seq caricate sono
    // esaurite (UINT32_MAX) o duplicate le rinumera da 1 nello stesso ordine
    void begin();
    // Nuova sessione: restituisce il token esadecimale (32 caratteri)
    String create(uint32_t now, bool persistent = false);
    // Valida il token; per le volatili aggiorna last_seen e libera quelle scadute.
    // Non scrive mai sullo storage
    bool validate(const String& token, uint32_t now);
    // false solo se si è rimossa una persistente e il salvataggio è fallito
    bool remove(const String& token);
    // Chiude tutte le sessioni, volatili e persistenti, e svuota lo storage;
    // false (sessioni invariate) se lo storage non è stato svuotato
    bool clear();
    // Rotazione esclusiva: crea un nuovo token dello stesso tipo di quello dato
    // (persistent in uscita) e chiude tutte le altre sessioni, vecchio token
    // compreso. Tutto o niente come clear(): un solo salvataggio dello storage
    // prima della RAM; se fallisce ok = false, restituisce "" e non cambia nulla.
    // Token vuoto o sconosciuto: come clear() (ok = esito), restituisce ""
    String rotateExclusive(const String& token, uint32_t now, bool& ok, bool& persistent);
    // Volatili non scadute più persistenti
    size_t activeCount(uint32_t now) const;
    size_t persistentCount() const;

private:
    struct Slot {
        uint8_t hash[32];
        uint32_t last_seen_ms;
        bool used;
    };

    struct PersistentSlot {
        uint8_t hash[32];
        uint32_t seq;
        bool used;
    };

    // Indice dello slot con quell'hash, -1 se assente (scansione completa di tutti
    // gli slot). 0..kMaxSessions-1 volatili, poi le persistenti
    int findSlot(const String& token);
    // Token esadecimale casuale (32 caratteri) e il suo SHA-256
    void generateToken(char hex[kTokenLength + 1], uint8_t hash[32]);
    bool isExpired(const Slot& slot, uint32_t now) const;
    size_t pickPersistentSlot() const;
    bool savePersistent();

    IAuthCrypto& crypto_;
    IAuthStorage& storage_;
    Slot slots_[kMaxSessions];
    PersistentSlot persistent_[kMaxPersistentSessions];
    uint32_t next_seq_;
};

#endif
