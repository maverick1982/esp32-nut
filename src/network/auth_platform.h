#ifndef NETWORK_AUTH_PLATFORM_H
#define NETWORK_AUTH_PLATFORM_H

#include <Arduino.h>
#include "AuthPlatform.h"

// Primitive crittografiche del firmware (ADR 0009): PBKDF2-HMAC-SHA256 e SHA-256
// di mbedTLS (accelerati in hardware sull'S3), RNG hardware con esp_fill_random
class EspAuthCrypto : public IAuthCrypto {
public:
    bool deriveKey(const String& password, const uint8_t* salt, size_t salt_len,
                   uint32_t iterations, uint8_t out[32]) override;
    void sha256(const uint8_t* data, size_t len, uint8_t out[32]) override;
    void randomBytes(uint8_t* buf, size_t len) override;

    // Abilita il log INFO della durata del PBKDF2: lo attiva il server solo attorno
    // alla creazione delle credenziali, per non riempire il log a ogni login
    void setLogTiming(bool enabled) { log_timing_ = enabled; }

private:
    bool log_timing_ = false;
};

// Credenziali nel namespace NVS "web_auth", separato da "nutos" (config_json)
class NvsAuthStorage : public IAuthStorage {
public:
    bool loadCredentials(CredentialRecord& record) override;
    bool saveCredentials(const CredentialRecord& record) override;
};

#endif
