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
