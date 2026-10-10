#ifndef AUTH_PLATFORM_H
#define AUTH_PLATFORM_H

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Credenziali persistite (ADR 0009): la password non viene mai salvata,
// solo l'hash PBKDF2 con il suo salt e il numero di iterazioni
struct CredentialRecord {
    String username;
    uint8_t salt[16];
    uint8_t hash[32];
    uint32_t iterations;
};

// Primitive crittografiche fornite dalla piattaforma (mbedTLS/esp_random sul device)
class IAuthCrypto {
public:
    virtual ~IAuthCrypto() = default;
    // PBKDF2-HMAC-SHA256 con output di 32 byte
    virtual bool deriveKey(const String& password, const uint8_t* salt, size_t salt_len,
                           uint32_t iterations, uint8_t out[32]) = 0;
    virtual void sha256(const uint8_t* data, size_t len, uint8_t out[32]) = 0;
    virtual void randomBytes(uint8_t* buf, size_t len) = 0;
};

// Archivio persistente delle credenziali (NVS sul device)
class IAuthStorage {
public:
    virtual ~IAuthStorage() = default;
    // false se non ci sono credenziali salvate
    virtual bool loadCredentials(CredentialRecord& record) = 0;
    virtual bool saveCredentials(const CredentialRecord& record) = 0;
};

#endif
