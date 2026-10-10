#include "SessionTable.h"
#include "CredentialStore.h"
#include <string.h>

SessionTable::SessionTable(IAuthCrypto& crypto, IAuthStorage& storage)
    : crypto_(crypto), storage_(storage), next_seq_(1) {
    // Solo memoria: lo storage si legge in begin() e non si scrive qui
    memset(slots_, 0, sizeof(slots_));
    memset(persistent_, 0, sizeof(persistent_));
}

void SessionTable::begin() {
    memset(persistent_, 0, sizeof(persistent_));
    next_seq_ = 1;

    PersistentSession loaded[kMaxPersistentSessions];
    size_t count = 0;
    if (!storage_.loadSessions(loaded, kMaxPersistentSessions, count)) count = 0;
    if (count > kMaxPersistentSessions) count = kMaxPersistentSessions;

    uint32_t max_seq = 0;
    bool duplicate = false;
    for (size_t i = 0; i < count; i++) {
        memcpy(persistent_[i].hash, loaded[i].hash, sizeof(persistent_[i].hash));
        persistent_[i].seq = loaded[i].seq;
        persistent_[i].used = true;
        if (loaded[i].seq > max_seq) max_seq = loaded[i].seq;
        for (size_t j = 0; j < i; j++) {
            if (loaded[j].seq == loaded[i].seq) duplicate = true;
        }
    }
    memset(loaded, 0, sizeof(loaded));

    if (max_seq == UINT32_MAX || duplicate) {
        // Rinumerazione da 1 nell'ordine di seq attuale (stabile: a parità vale
        // la posizione), così la più vecchia resta la prima a essere sostituita
        bool done[kMaxPersistentSessions] = {};
        for (uint32_t n = 1; n <= count; n++) {
            size_t pick = count;
            for (size_t i = 0; i < count; i++) {
                if (done[i]) continue;
                if (pick == count || persistent_[i].seq < persistent_[pick].seq) pick = i;
            }
            done[pick] = true;
            persistent_[pick].seq = n;
        }
        max_seq = (uint32_t)count;
        savePersistent();
    }
    next_seq_ = max_seq + 1;
}

bool SessionTable::isExpired(const Slot& slot, uint32_t now) const {
    // Sottrazione uint32_t: corretta anche al rollover di millis()
    return (uint32_t)(now - slot.last_seen_ms) >= kIdleTimeoutMs;
}

size_t SessionTable::pickPersistentSlot() const {
    // Slot libero, altrimenti quello con seq più basso (la sessione più vecchia)
    for (size_t i = 0; i < kMaxPersistentSessions; i++) {
        if (!persistent_[i].used) return i;
    }
    size_t target = 0;
    for (size_t i = 1; i < kMaxPersistentSessions; i++) {
        if (persistent_[i].seq < persistent_[target].seq) target = i;
    }
    return target;
}

bool SessionTable::savePersistent() {
    PersistentSession out[kMaxPersistentSessions];
    size_t count = 0;
    for (size_t i = 0; i < kMaxPersistentSessions; i++) {
        if (!persistent_[i].used) continue;
        memcpy(out[count].hash, persistent_[i].hash, sizeof(out[count].hash));
        out[count].seq = persistent_[i].seq;
        count++;
    }
    bool ok = storage_.saveSessions(out, count);
    memset(out, 0, sizeof(out));
    return ok;
}

String SessionTable::create(uint32_t now, bool persistent) {
    uint8_t raw[kTokenBytes];
    crypto_.randomBytes(raw, sizeof(raw));

    static const char kHex[] = "0123456789abcdef";
    char hex[kTokenLength + 1];
    for (size_t i = 0; i < kTokenBytes; i++) {
        hex[i * 2] = kHex[raw[i] >> 4];
        hex[i * 2 + 1] = kHex[raw[i] & 0x0F];
    }
    hex[kTokenLength] = '\0';
    memset(raw, 0, sizeof(raw));

    if (persistent) {
        PersistentSlot& slot = persistent_[pickPersistentSlot()];
        crypto_.sha256((const uint8_t*)hex, kTokenLength, slot.hash);
        slot.seq = next_seq_++;
        slot.used = true;
        savePersistent();
    } else {
        // Slot libero o scaduto, altrimenti quello usato meno di recente
        size_t target = kMaxSessions;
        for (size_t i = 0; i < kMaxSessions; i++) {
            if (!slots_[i].used || isExpired(slots_[i], now)) {
                target = i;
                break;
            }
        }
        if (target == kMaxSessions) {
            uint32_t oldest_age = 0;
            target = 0;
            for (size_t i = 0; i < kMaxSessions; i++) {
                uint32_t age = now - slots_[i].last_seen_ms;
                if (age >= oldest_age) {
                    oldest_age = age;
                    target = i;
                }
            }
        }

        Slot& slot = slots_[target];
        crypto_.sha256((const uint8_t*)hex, kTokenLength, slot.hash);
        slot.last_seen_ms = now;
        slot.used = true;
    }

    String token(hex);
    memset(hex, 0, sizeof(hex));
    return token;
}

int SessionTable::findSlot(const String& token) {
    if (token.length() != kTokenLength) return -1;
    uint8_t hash[32];
    crypto_.sha256((const uint8_t*)token.c_str(), token.length(), hash);
    // Nessuna uscita anticipata: si confrontano sempre tutti gli slot
    int found = -1;
    for (size_t i = 0; i < kMaxSessions; i++) {
        bool match = CredentialStore::constantTimeEquals(hash, slots_[i].hash, sizeof(hash));
        if (slots_[i].used && match) found = (int)i;
    }
    for (size_t i = 0; i < kMaxPersistentSessions; i++) {
        bool match = CredentialStore::constantTimeEquals(hash, persistent_[i].hash, sizeof(hash));
        if (persistent_[i].used && match) found = (int)(kMaxSessions + i);
    }
    return found;
}

bool SessionTable::validate(const String& token, uint32_t now) {
    int idx = findSlot(token);
    if (idx < 0) return false;
    // Le persistenti non scadono per inattività e non si scrive sullo storage
    if ((size_t)idx >= kMaxSessions) return true;
    Slot& slot = slots_[idx];
    if (isExpired(slot, now)) {
        memset(&slot, 0, sizeof(slot));
        return false;
    }
    slot.last_seen_ms = now;
    return true;
}

bool SessionTable::remove(const String& token) {
    int idx = findSlot(token);
    if (idx < 0) return true;
    if ((size_t)idx >= kMaxSessions) {
        memset(&persistent_[idx - kMaxSessions], 0, sizeof(PersistentSlot));
        return savePersistent();
    }
    memset(&slots_[idx], 0, sizeof(Slot));
    return true;
}

bool SessionTable::clear() {
    // Prima lo storage: se non si svuota le sessioni restano tutte valide e si può
    // riprovare (altrimenti al riavvio tornerebbero le persistenti)
    if (!storage_.saveSessions(nullptr, 0)) return false;
    memset(slots_, 0, sizeof(slots_));
    memset(persistent_, 0, sizeof(persistent_));
    return true;
}

size_t SessionTable::activeCount(uint32_t now) const {
    size_t count = 0;
    for (size_t i = 0; i < kMaxSessions; i++) {
        if (slots_[i].used && !isExpired(slots_[i], now)) count++;
    }
    return count + persistentCount();
}

size_t SessionTable::persistentCount() const {
    size_t count = 0;
    for (size_t i = 0; i < kMaxPersistentSessions; i++) {
        if (persistent_[i].used) count++;
    }
    return count;
}
