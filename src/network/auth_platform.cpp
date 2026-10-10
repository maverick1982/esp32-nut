#include "AuthPlatform.h"
#include "network/auth_platform.h"
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/sha256.h>
#include "core/app_logger.h"

static const char* kNamespace = "web_auth";
// Record delle credenziali in un solo blob: [versione][iter LE 4][salt 16][hash 32][len 1][username]
static const char* kKeyCred = "cred";
static const uint8_t kCredVersion = 1;
static const size_t kCredHeaderSize = 1 + 4 + 16 + 32 + 1;
static const size_t kCredMaxSize = kCredHeaderSize + 32;  // username: al massimo 32 byte
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

    uint8_t buf[kCredMaxSize];
    size_t len = prefs.isKey(kKeyCred) ? prefs.getBytesLength(kKeyCred) : 0;
    bool ok = len >= kCredHeaderSize + 1 && len <= sizeof(buf)
           && prefs.getBytes(kKeyCred, buf, len) == len;
    prefs.end();
    if (!ok || buf[0] != kCredVersion) return false;

    // [versione][iter LE 4][salt 16][hash 32][len username 1][username]
    size_t user_len = buf[kCredHeaderSize - 1];
    if (user_len == 0 || kCredHeaderSize + user_len != len) return false;
    record.iterations = (uint32_t)buf[1] | ((uint32_t)buf[2] << 8)
                      | ((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 24);
    memcpy(record.salt, buf + 5, sizeof(record.salt));
    memcpy(record.hash, buf + 5 + sizeof(record.salt), sizeof(record.hash));
    record.username = "";
    for (size_t i = 0; i < user_len; i++) record.username += (char)buf[kCredHeaderSize + i];
    return record.iterations > 0;
}

bool NvsAuthStorage::saveCredentials(const CredentialRecord& record) {
    size_t user_len = record.username.length();
    if (user_len == 0 || kCredHeaderSize + user_len > kCredMaxSize) return false;

    uint8_t buf[kCredMaxSize];
    buf[0] = kCredVersion;
    for (int i = 0; i < 4; i++) buf[1 + i] = (uint8_t)(record.iterations >> (8 * i));
    memcpy(buf + 5, record.salt, sizeof(record.salt));
    memcpy(buf + 5 + sizeof(record.salt), record.hash, sizeof(record.hash));
    buf[kCredHeaderSize - 1] = (uint8_t)user_len;
    memcpy(buf + kCredHeaderSize, record.username.c_str(), user_len);

    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return false;
    // Un solo blob: NVS scrive una chiave in modo atomico (il valore precedente resta
    // finché il nuovo non è completo), quindi un'interruzione non lascia mai username,
    // salt e hash di versioni diverse (review US-063)
    size_t total = kCredHeaderSize + user_len;
    bool ok = prefs.putBytes(kKeyCred, buf, total) == total;
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
