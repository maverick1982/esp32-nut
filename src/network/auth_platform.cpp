#include "AuthPlatform.h"
#include "network/auth_platform.h"
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/sha256.h>
#include "core/app_logger.h"

static const char* kNamespace = "web_auth";
static const char* kKeyUser = "user";
static const char* kKeySalt = "salt";
static const char* kKeyHash = "hash";
static const char* kKeyIter = "iter";
static const char* kKeySessions = "sessions";
// Voce del blob "sessions": 32 byte di hash + seq uint32 little-endian
static const size_t kSessionRecordSize = 36;

bool EspAuthCrypto::deriveKey(const String& password, const uint8_t* salt, size_t salt_len,
                              uint32_t iterations, uint8_t out[32]) {
    unsigned long start = millis();
    int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
                                            (const unsigned char*)password.c_str(), password.length(),
                                            salt, salt_len, iterations, 32, out);
    if (ret != 0) {
        AppLogger::log("ERROR", "[AUTH] PBKDF2 failed: %d", ret);
        return false;
    }
    if (log_timing_) {
        AppLogger::log("INFO", "[AUTH] PBKDF2 %u iterations: %lu ms",
                       (unsigned)iterations, (unsigned long)(millis() - start));
    }
    return true;
}

void EspAuthCrypto::sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    mbedtls_sha256(data, len, out, 0);
}

void EspAuthCrypto::randomBytes(uint8_t* buf, size_t len) {
    esp_fill_random(buf, len);
}

bool NvsAuthStorage::loadCredentials(CredentialRecord& record) {
    Preferences prefs;
    // Sola lettura: se il namespace non esiste ancora begin() fallisce (stato TOFU)
    if (!prefs.begin(kNamespace, true)) return false;

    bool ok = prefs.isKey(kKeyHash)
           && prefs.getBytesLength(kKeyHash) == sizeof(record.hash)
           && prefs.getBytesLength(kKeySalt) == sizeof(record.salt);
    if (ok) {
        record.username = prefs.getString(kKeyUser, "");
        record.iterations = prefs.getUInt(kKeyIter, 0);
        ok = prefs.getBytes(kKeySalt, record.salt, sizeof(record.salt)) == sizeof(record.salt)
          && prefs.getBytes(kKeyHash, record.hash, sizeof(record.hash)) == sizeof(record.hash)
          && record.username.length() > 0
          && record.iterations > 0;
    }
    prefs.end();
    return ok;
}

bool NvsAuthStorage::saveCredentials(const CredentialRecord& record) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return false;

    // hash per ultimo: la sua presenza indica un record completo
    bool ok = prefs.putString(kKeyUser, record.username) == record.username.length()
           && prefs.putBytes(kKeySalt, record.salt, sizeof(record.salt)) == sizeof(record.salt)
           && prefs.putUInt(kKeyIter, record.iterations) == sizeof(uint32_t)
           && prefs.putBytes(kKeyHash, record.hash, sizeof(record.hash)) == sizeof(record.hash);
    prefs.end();
    return ok;
}

bool NvsAuthStorage::loadSessions(PersistentSession* out, size_t max, size_t& count) {
    count = 0;
    Preferences prefs;
    // Namespace assente (nessuna credenziale né sessione salvata): nessuna sessione
    if (!prefs.begin(kNamespace, true)) return true;
    if (!prefs.isKey(kKeySessions)) {
        prefs.end();
        return true;
    }

    size_t len = prefs.getBytesLength(kKeySessions);
    // Blob corrotto o con più voci di quante ne servono: si riparte senza sessioni
    if (len == 0 || len % kSessionRecordSize != 0 || len / kSessionRecordSize > max) {
        prefs.end();
        return true;
    }

    uint8_t* buf = (uint8_t*)malloc(len);
    if (!buf) {
        prefs.end();
        return false;
    }
    bool ok = prefs.getBytes(kKeySessions, buf, len) == len;
    prefs.end();
    if (ok) {
        size_t n = len / kSessionRecordSize;
        for (size_t i = 0; i < n; i++) {
            const uint8_t* rec = buf + i * kSessionRecordSize;
            memcpy(out[i].hash, rec, sizeof(out[i].hash));
            out[i].seq = (uint32_t)rec[32]
                       | ((uint32_t)rec[33] << 8)
                       | ((uint32_t)rec[34] << 16)
                       | ((uint32_t)rec[35] << 24);
        }
        count = n;
    }
    free(buf);
    return ok;
}

bool NvsAuthStorage::saveSessions(const PersistentSession* in, size_t count) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return false;

    if (count == 0) {
        // Nessuna sessione persistente: niente chiave (chiave già assente = successo)
        bool removed = !prefs.isKey(kKeySessions) || prefs.remove(kKeySessions);
        prefs.end();
        return removed;
    }

    size_t len = count * kSessionRecordSize;
    uint8_t* buf = (uint8_t*)malloc(len);
    if (!buf) {
        prefs.end();
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        uint8_t* rec = buf + i * kSessionRecordSize;
        memcpy(rec, in[i].hash, sizeof(in[i].hash));
        rec[32] = (uint8_t)(in[i].seq & 0xFF);
        rec[33] = (uint8_t)((in[i].seq >> 8) & 0xFF);
        rec[34] = (uint8_t)((in[i].seq >> 16) & 0xFF);
        rec[35] = (uint8_t)((in[i].seq >> 24) & 0xFF);
    }
    bool ok = prefs.putBytes(kKeySessions, buf, len) == len;
    prefs.end();
    free(buf);
    return ok;
}
