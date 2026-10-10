#ifndef CREDENTIAL_STORE_H
#define CREDENTIAL_STORE_H

#include <Arduino.h>
#include "AuthPlatform.h"

// Credenziali della Web UI con PBKDF2 (ADR 0009); logica pura, testata in nativo
class CredentialStore {
public:
    static constexpr uint32_t kDefaultIterations = 10000;

    enum class CreateResult { OK, INVALID_USERNAME, INVALID_PASSWORD, ALREADY_CONFIGURED, STORAGE_ERROR };

    CredentialStore(IAuthCrypto& crypto, IAuthStorage& storage);

    // Carica il record dallo storage
    void begin();
    bool hasCredentials() const { return configured_; }
    const String& username() const { return record_.username; }

    // 1-32 caratteri ASCII stampabili (0x20-0x7E)
    static bool validateUsername(const String& username);
    // 8-64 byte
    static bool validatePassword(const String& password);

    // Prima creazione (TOFU): non sovrascrive mai credenziali esistenti
    CreateResult create(const String& username, const String& password);
    // Ricalcola l'hash con salt e iterazioni del record e confronta in tempo costante
    bool verify(const String& username, const String& password);

    // Confronto senza uscita anticipata, riusato da SessionTable
    static bool constantTimeEquals(const uint8_t* a, const uint8_t* b, size_t len);

private:
    IAuthCrypto& crypto_;
    IAuthStorage& storage_;
    CredentialRecord record_;
    bool configured_;
};

#endif
