#include "SessionTable.h"
#include "CredentialStore.h"
#include <string.h>

SessionTable::SessionTable(IAuthCrypto& crypto) : crypto_(crypto) {
    clear();
}

bool SessionTable::isExpired(const Slot& slot, uint32_t now) const {
    // Sottrazione uint32_t: corretta anche al rollover di millis()
    return (uint32_t)(now - slot.last_seen_ms) >= kIdleTimeoutMs;
}

String SessionTable::create(uint32_t now) {
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

    String token(hex);
    memset(hex, 0, sizeof(hex));
    return token;
}

int SessionTable::findSlot(const String& token) {
    if (token.length() != kTokenLength) return -1;
    uint8_t hash[32];
    crypto_.sha256((const uint8_t*)token.c_str(), token.length(), hash);
    int found = -1;
    for (size_t i = 0; i < kMaxSessions; i++) {
        bool match = CredentialStore::constantTimeEquals(hash, slots_[i].hash, sizeof(hash));
        if (slots_[i].used && match) found = (int)i;
    }
    return found;
}

bool SessionTable::validate(const String& token, uint32_t now) {
    int idx = findSlot(token);
    if (idx < 0) return false;
    Slot& slot = slots_[idx];
    if (isExpired(slot, now)) {
        memset(&slot, 0, sizeof(slot));
        return false;
    }
    slot.last_seen_ms = now;
    return true;
}

void SessionTable::remove(const String& token) {
    int idx = findSlot(token);
    if (idx >= 0) memset(&slots_[idx], 0, sizeof(Slot));
}

void SessionTable::clear() {
    memset(slots_, 0, sizeof(slots_));
}

size_t SessionTable::activeCount(uint32_t now) const {
    size_t count = 0;
    for (size_t i = 0; i < kMaxSessions; i++) {
        if (slots_[i].used && !isExpired(slots_[i], now)) count++;
    }
    return count;
}
