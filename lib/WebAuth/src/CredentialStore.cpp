#include "CredentialStore.h"
#include <string.h>

CredentialStore::CredentialStore(IAuthCrypto& crypto, IAuthStorage& storage)
    : crypto_(crypto), storage_(storage), configured_(false) {
    memset(record_.salt, 0, sizeof(record_.salt));
    memset(record_.hash, 0, sizeof(record_.hash));
    record_.iterations = 0;
}

void CredentialStore::begin() {
    CredentialRecord loaded;
    loaded.iterations = 0;
    if (storage_.loadCredentials(loaded) && loaded.iterations > 0) {
        record_ = loaded;
        configured_ = true;
    } else {
        configured_ = false;
    }
}

bool CredentialStore::validateUsername(const String& username) {
    size_t len = username.length();
    if (len < 1 || len > 32) return false;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)username[i];
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

bool CredentialStore::validatePassword(const String& password) {
    size_t len = password.length();
    return len >= 8 && len <= 64;
}

CredentialStore::CreateResult CredentialStore::create(const String& username, const String& password) {
    // Credenziali esistenti: nessuna validazione né sovrascrittura
    if (configured_) return CreateResult::ALREADY_CONFIGURED;
    if (!validateUsername(username)) return CreateResult::INVALID_USERNAME;
    if (!validatePassword(password)) return CreateResult::INVALID_PASSWORD;

    CredentialRecord rec;
    rec.username = username;
    rec.iterations = kDefaultIterations;
    crypto_.randomBytes(rec.salt, sizeof(rec.salt));
    if (!crypto_.deriveKey(password, rec.salt, sizeof(rec.salt), rec.iterations, rec.hash)) {
        return CreateResult::STORAGE_ERROR;
    }
    if (!storage_.saveCredentials(rec)) return CreateResult::STORAGE_ERROR;

    // Lo stato cambia solo dopo un salvataggio riuscito
    record_ = rec;
    configured_ = true;
    return CreateResult::OK;
}

CredentialStore::ReplaceResult CredentialStore::replace(const String& username, const String& password) {
    if (!validateUsername(username)) return ReplaceResult::INVALID_USERNAME;
    if (!validatePassword(password)) return ReplaceResult::INVALID_PASSWORD;

    // Nuovo record con nuovo salt: quello in memoria resta intatto fino al salvataggio
    CredentialRecord rec;
    rec.username = username;
    rec.iterations = kDefaultIterations;
    crypto_.randomBytes(rec.salt, sizeof(rec.salt));
    if (!crypto_.deriveKey(password, rec.salt, sizeof(rec.salt), rec.iterations, rec.hash)) {
        memset(rec.hash, 0, sizeof(rec.hash));
        return ReplaceResult::STORAGE_ERROR;
    }
    if (!storage_.saveCredentials(rec)) {
        memset(rec.hash, 0, sizeof(rec.hash));
        return ReplaceResult::STORAGE_ERROR;
    }

    record_ = rec;
    configured_ = true;
    return ReplaceResult::OK;
}

bool CredentialStore::verify(const String& username, const String& password) {
    if (!configured_) return false;

    // Username: differenza accumulata sulla lunghezza del record, senza uscita anticipata
    const String& stored = record_.username;
    size_t stored_len = stored.length();
    size_t given_len = username.length();
    uint8_t diff = (stored_len == given_len) ? 0 : 1;
    for (size_t i = 0; i < stored_len; i++) {
        uint8_t given = i < given_len ? (uint8_t)username[i] : 0;
        diff |= (uint8_t)stored[i] ^ given;
    }

    // L'hash viene calcolato sempre, anche con username errato
    uint8_t computed[32];
    bool derived = crypto_.deriveKey(password, record_.salt, sizeof(record_.salt), record_.iterations, computed);
    bool hash_ok = constantTimeEquals(computed, record_.hash, sizeof(computed));
    memset(computed, 0, sizeof(computed));

    return derived && (diff == 0) && hash_ok;
}

bool CredentialStore::constantTimeEquals(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}
