#include <Arduino.h>
#include <unity.h>
#include <ArduinoJson.h>
#include <string.h>
#include <vector>
#include "AuthPlatform.h"
#include "CredentialStore.h"
#include "SessionTable.h"
#include "AuthPolicy.h"
#include "AuthService.h"
#include "LoginThrottle.h"

// Crypto finto e deterministico: non è sicuro, serve solo a rendere i risultati
// dipendenti dagli input e diversi dai byte in chiaro
class FakeCrypto : public IAuthCrypto {
public:
    uint32_t counter = 0;
    bool deriveOk = true;
    uint32_t deriveCalls = 0;

    bool deriveKey(const String& password, const uint8_t* salt, size_t salt_len,
                   uint32_t iterations, uint8_t out[32]) override {
        deriveCalls++;
        uint32_t h = 2166136261u ^ iterations;
        for (size_t i = 0; i < password.length(); i++) h = (h ^ (uint8_t)password[i]) * 16777619u;
        for (size_t i = 0; i < salt_len; i++) h = (h ^ salt[i]) * 16777619u;
        fill(h, out);
        return deriveOk;
    }

    void sha256(const uint8_t* data, size_t len, uint8_t out[32]) override {
        uint32_t h = 0x811C9DC5u;
        for (size_t i = 0; i < len; i++) h = (h ^ data[i]) * 16777619u;
        fill(h, out);
    }

    // Contatore: due chiamate consecutive danno sempre byte diversi
    void randomBytes(uint8_t* buf, size_t len) override {
        counter++;
        for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)((counter * 37u + i * 101u) ^ (counter >> 3));
    }

private:
    static void fill(uint32_t h, uint8_t out[32]) {
        for (size_t i = 0; i < 32; i++) {
            h = h * 1103515245u + 12345u;
            out[i] = (uint8_t)(h >> 16) | 0x80;  // mai ASCII: diverso da qualsiasi token esadecimale
        }
    }
};

class MemoryStorage : public IAuthStorage {
public:
    CredentialRecord record;
    bool hasRecord = false;
    bool failSave = false;
    int saveCalls = 0;

    bool loadCredentials(CredentialRecord& out) override {
        if (!hasRecord) return false;
        out = record;
        return true;
    }
    bool saveCredentials(const CredentialRecord& in) override {
        saveCalls++;
        if (failSave) return false;
        record = in;
        hasRecord = true;
        return true;
    }

    std::vector<PersistentSession> sessions;
    int sessionSaves = 0;
    bool failSessionSave = false;
    bool failSessionLoad = false;

    bool loadSessions(PersistentSession* out, size_t max, size_t& count) override {
        count = 0;
        if (failSessionLoad) return false;
        for (size_t i = 0; i < sessions.size() && i < max; i++) out[count++] = sessions[i];
        return true;
    }
    bool saveSessions(const PersistentSession* in, size_t count) override {
        sessionSaves++;
        if (failSessionSave) return false;
        sessions.assign(in, in + count);
        return true;
    }
};

static FakeCrypto crypto;
static MemoryStorage storage;

static String repeat(char c, size_t n) {
    String s;
    for (size_t i = 0; i < n; i++) s += c;
    return s;
}

// true se needle compare come sequenza di byte dentro haystack
static bool containsBytes(const void* haystack, size_t hay_len, const void* needle, size_t needle_len) {
    if (needle_len == 0 || needle_len > hay_len) return false;
    const uint8_t* h = (const uint8_t*)haystack;
    for (size_t i = 0; i + needle_len <= hay_len; i++) {
        if (memcmp(h + i, needle, needle_len) == 0) return true;
    }
    return false;
}

static bool isLowerHex(const String& s) {
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static bool contains(const String& haystack, const char* needle) {
    return haystack.indexOf(needle) >= 0;
}

// Token dal Set-Cookie "sid=<token>; ..."
static String cookieToken(const String& set_cookie) {
    int eq = set_cookie.indexOf('=');
    int semi = set_cookie.indexOf(';');
    if (eq < 0 || semi < 0) return String();
    return set_cookie.substring(eq + 1, semi);
}

void setUp(void) {
    crypto = FakeCrypto();
    storage = MemoryStorage();
}

void tearDown(void) {}

// ---------------------------------------------------------------------------
// CredentialStore
// ---------------------------------------------------------------------------

void test_username_length_limits(void) {
    TEST_ASSERT_FALSE(CredentialStore::validateUsername(""));
    TEST_ASSERT_TRUE(CredentialStore::validateUsername("a"));
    TEST_ASSERT_TRUE(CredentialStore::validateUsername(repeat('u', 32)));
    TEST_ASSERT_FALSE(CredentialStore::validateUsername(repeat('u', 33)));
}

void test_username_printable_only(void) {
    TEST_ASSERT_TRUE(CredentialStore::validateUsername(" ~admin"));
    String low = "adm";
    low += (char)0x1F;
    TEST_ASSERT_FALSE(CredentialStore::validateUsername(low));
    String del = "adm";
    del += (char)0x7F;
    TEST_ASSERT_FALSE(CredentialStore::validateUsername(del));
    String high = "adm";
    high += (char)0xC3;
    TEST_ASSERT_FALSE(CredentialStore::validateUsername(high));
}

void test_password_length_limits(void) {
    TEST_ASSERT_FALSE(CredentialStore::validatePassword(repeat('p', 7)));
    TEST_ASSERT_TRUE(CredentialStore::validatePassword(repeat('p', 8)));
    TEST_ASSERT_TRUE(CredentialStore::validatePassword(repeat('p', 64)));
    TEST_ASSERT_FALSE(CredentialStore::validatePassword(repeat('p', 65)));
}

void test_create_ok_and_reload(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    TEST_ASSERT_FALSE(store.hasCredentials());

    TEST_ASSERT_TRUE(store.create("admin", "password1") == CredentialStore::CreateResult::OK);
    TEST_ASSERT_TRUE(store.hasCredentials());
    TEST_ASSERT_EQUAL_STRING("admin", store.username().c_str());
    TEST_ASSERT_EQUAL_UINT32(CredentialStore::kDefaultIterations, storage.record.iterations);

    // Una nuova istanza sullo stesso storage ricarica le credenziali
    CredentialStore reloaded(crypto, storage);
    TEST_ASSERT_FALSE(reloaded.hasCredentials());
    reloaded.begin();
    TEST_ASSERT_TRUE(reloaded.hasCredentials());
    TEST_ASSERT_EQUAL_STRING("admin", reloaded.username().c_str());
    TEST_ASSERT_TRUE(reloaded.verify("admin", "password1"));
}

void test_create_invalid_inputs(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    TEST_ASSERT_TRUE(store.create("", "password1") == CredentialStore::CreateResult::INVALID_USERNAME);
    TEST_ASSERT_TRUE(store.create("admin", "short") == CredentialStore::CreateResult::INVALID_PASSWORD);
    TEST_ASSERT_FALSE(store.hasCredentials());
    TEST_ASSERT_EQUAL(0, storage.saveCalls);
}

void test_create_twice_already_configured(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    TEST_ASSERT_TRUE(store.create("admin", "password1") == CredentialStore::CreateResult::OK);
    TEST_ASSERT_TRUE(store.create("other", "password2") == CredentialStore::CreateResult::ALREADY_CONFIGURED);
    TEST_ASSERT_EQUAL(1, storage.saveCalls);
    TEST_ASSERT_EQUAL_STRING("admin", storage.record.username.c_str());
    TEST_ASSERT_TRUE(store.verify("admin", "password1"));
    TEST_ASSERT_FALSE(store.verify("other", "password2"));
}

void test_create_storage_error(void) {
    storage.failSave = true;
    CredentialStore store(crypto, storage);
    store.begin();
    TEST_ASSERT_TRUE(store.create("admin", "password1") == CredentialStore::CreateResult::STORAGE_ERROR);
    TEST_ASSERT_FALSE(store.hasCredentials());
    TEST_ASSERT_FALSE(store.verify("admin", "password1"));
}

void test_verify(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    TEST_ASSERT_TRUE(store.create("admin", "password1") == CredentialStore::CreateResult::OK);
    TEST_ASSERT_TRUE(store.verify("admin", "password1"));
    TEST_ASSERT_FALSE(store.verify("admin", "password2"));
    TEST_ASSERT_FALSE(store.verify("admin", ""));
    TEST_ASSERT_FALSE(store.verify("Admin", "password1"));
    TEST_ASSERT_FALSE(store.verify("admin2", "password1"));
    TEST_ASSERT_FALSE(store.verify("adm", "password1"));
}

void test_verify_without_credentials(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    TEST_ASSERT_FALSE(store.verify("", ""));
    TEST_ASSERT_FALSE(store.verify("admin", "password1"));
}

void test_verify_uses_record_iterations(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    TEST_ASSERT_TRUE(store.create("admin", "password1") == CredentialStore::CreateResult::OK);

    // Record con un numero di iterazioni diverso dal default
    CredentialRecord rec = storage.record;
    rec.iterations = 1234;
    crypto.deriveKey("password1", rec.salt, sizeof(rec.salt), rec.iterations, rec.hash);
    MemoryStorage other;
    other.record = rec;
    other.hasRecord = true;
    CredentialStore custom(crypto, other);
    custom.begin();
    TEST_ASSERT_TRUE(custom.verify("admin", "password1"));
}

void test_salts_differ(void) {
    MemoryStorage other;
    CredentialStore a(crypto, storage);
    CredentialStore b(crypto, other);
    a.begin();
    b.begin();
    TEST_ASSERT_TRUE(a.create("admin", "password1") == CredentialStore::CreateResult::OK);
    TEST_ASSERT_TRUE(b.create("admin", "password1") == CredentialStore::CreateResult::OK);
    TEST_ASSERT_FALSE(memcmp(storage.record.salt, other.record.salt, 16) == 0);
    TEST_ASSERT_FALSE(memcmp(storage.record.hash, other.record.hash, 32) == 0);
}

void test_record_has_no_plaintext_password(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    const char* pwd = "secretpassword";
    TEST_ASSERT_TRUE(store.create("admin", pwd) == CredentialStore::CreateResult::OK);
    const CredentialRecord& rec = storage.record;
    TEST_ASSERT_FALSE(contains(rec.username, pwd));
    TEST_ASSERT_FALSE(containsBytes(rec.salt, sizeof(rec.salt), pwd, 4));
    TEST_ASSERT_FALSE(containsBytes(rec.hash, sizeof(rec.hash), pwd, 4));
}

void test_constant_time_equals(void) {
    uint8_t a[4] = {1, 2, 3, 4};
    uint8_t b[4] = {1, 2, 3, 4};
    uint8_t c[4] = {1, 2, 3, 5};
    TEST_ASSERT_TRUE(CredentialStore::constantTimeEquals(a, b, 4));
    TEST_ASSERT_FALSE(CredentialStore::constantTimeEquals(a, c, 4));
    TEST_ASSERT_TRUE(CredentialStore::constantTimeEquals(a, c, 3));
}

// ---------------------------------------------------------------------------
// SessionTable
// ---------------------------------------------------------------------------

static const uint32_t MIN = 60UL * 1000UL;

void test_session_token_format_and_validity(void) {
    SessionTable table(crypto, storage);
    String token = table.create(1000);
    TEST_ASSERT_EQUAL(32, token.length());
    TEST_ASSERT_TRUE(isLowerHex(token));
    TEST_ASSERT_TRUE(table.validate(token, 1000));
    TEST_ASSERT_EQUAL(1, table.activeCount(1000));

    String other = table.create(1000);
    TEST_ASSERT_FALSE(token == other);
}

void test_session_invalid_tokens(void) {
    SessionTable table(crypto, storage);
    String token = table.create(1000);
    TEST_ASSERT_FALSE(table.validate("", 1000));
    TEST_ASSERT_FALSE(table.validate(repeat('0', 32), 1000));
    TEST_ASSERT_FALSE(table.validate(token.substring(0, 31), 1000));
    TEST_ASSERT_FALSE(table.validate(token + "0", 1000));
    TEST_ASSERT_TRUE(table.validate(token, 1000));
}

void test_session_exact_expiry(void) {
    SessionTable table(crypto, storage);
    String a = table.create(1000);
    String b = table.create(1000);
    TEST_ASSERT_TRUE(table.validate(a, 1000 + SessionTable::kIdleTimeoutMs - 1));
    TEST_ASSERT_FALSE(table.validate(b, 1000 + SessionTable::kIdleTimeoutMs));
    // La sessione scaduta resta non valida anche se richiesta prima
    TEST_ASSERT_FALSE(table.validate(b, 1000));
    TEST_ASSERT_EQUAL(30UL * MIN, SessionTable::kIdleTimeoutMs);
}

void test_session_refresh_keeps_alive(void) {
    SessionTable table(crypto, storage);
    String token = table.create(0);
    for (uint32_t t = 10 * MIN; t <= 60 * MIN; t += 10 * MIN) {
        TEST_ASSERT_TRUE(table.validate(token, t));
    }
    TEST_ASSERT_TRUE(table.validate(token, 60 * MIN + 30 * MIN - 1));
    TEST_ASSERT_FALSE(table.validate(token, 60 * MIN + 30 * MIN - 1 + 30 * MIN));
}

void test_session_rollover(void) {
    SessionTable table(crypto, storage);
    String token = table.create(0xFFFFFF00u);
    TEST_ASSERT_TRUE(table.validate(token, 0x00000100u));
    TEST_ASSERT_EQUAL(1, table.activeCount(0x00000100u));
}

void test_session_lru_replacement(void) {
    SessionTable table(crypto, storage);
    String tokens[SessionTable::kMaxSessions];
    for (size_t i = 0; i < SessionTable::kMaxSessions; i++) {
        tokens[i] = table.create(1000 + i * 1000);
    }
    // Il token 0 è il più vecchio per creazione, ma viene usato: diventa il 1 il meno recente
    TEST_ASSERT_TRUE(table.validate(tokens[0], 20000));
    String ninth = table.create(21000);

    TEST_ASSERT_EQUAL(SessionTable::kMaxSessions, table.activeCount(21000));
    TEST_ASSERT_FALSE(table.validate(tokens[1], 21000));
    TEST_ASSERT_TRUE(table.validate(ninth, 21000));
    TEST_ASSERT_TRUE(table.validate(tokens[0], 21000));
    for (size_t i = 2; i < SessionTable::kMaxSessions; i++) {
        TEST_ASSERT_TRUE(table.validate(tokens[i], 21000));
    }
}

void test_session_expired_slot_reused(void) {
    SessionTable table(crypto, storage);
    String old_token = table.create(0);
    String tokens[SessionTable::kMaxSessions - 1];
    for (size_t i = 0; i < SessionTable::kMaxSessions - 1; i++) tokens[i] = table.create(1000);
    // A "later" solo la prima sessione è scaduta
    uint32_t later = SessionTable::kIdleTimeoutMs + 500;
    TEST_ASSERT_EQUAL(SessionTable::kMaxSessions - 1, table.activeCount(later));
    String fresh = table.create(later);
    TEST_ASSERT_EQUAL(SessionTable::kMaxSessions, table.activeCount(later));
    for (size_t i = 0; i < SessionTable::kMaxSessions - 1; i++) TEST_ASSERT_TRUE(table.validate(tokens[i], later));
    TEST_ASSERT_TRUE(table.validate(fresh, later));
    TEST_ASSERT_FALSE(table.validate(old_token, later));
}

void test_session_remove_and_clear(void) {
    SessionTable table(crypto, storage);
    String a = table.create(1000);
    String b = table.create(1000);
    String c = table.create(1000);
    table.remove(b);
    TEST_ASSERT_FALSE(table.validate(b, 1000));
    TEST_ASSERT_TRUE(table.validate(a, 1000));
    TEST_ASSERT_TRUE(table.validate(c, 1000));
    TEST_ASSERT_EQUAL(2, table.activeCount(1000));

    table.remove("");
    table.remove(repeat('f', 32));
    TEST_ASSERT_EQUAL(2, table.activeCount(1000));

    table.clear();
    TEST_ASSERT_FALSE(table.validate(a, 1000));
    TEST_ASSERT_FALSE(table.validate(c, 1000));
    TEST_ASSERT_EQUAL(0, table.activeCount(1000));
}

void test_session_token_not_stored(void) {
    SessionTable table(crypto, storage);
    String token = table.create(1000);
    // Né il token esadecimale né una sua parte compaiono nella memoria della tabella
    TEST_ASSERT_FALSE(containsBytes(&table, sizeof(table), token.c_str(), 8));
    uint8_t hash[32];
    crypto.sha256((const uint8_t*)token.c_str(), token.length(), hash);
    TEST_ASSERT_TRUE(containsBytes(&table, sizeof(table), hash, sizeof(hash)));
}

// Sessioni persistenti ("Remember me")

static const uint32_t DAY = 24UL * 60UL * MIN;

void test_persistent_survives_reload(void) {
    String token;
    {
        SessionTable table(crypto, storage);
        table.begin();
        token = table.create(1000, true);
        TEST_ASSERT_EQUAL(32, token.length());
        TEST_ASSERT_TRUE(isLowerHex(token));
        TEST_ASSERT_EQUAL(1, table.persistentCount());
        TEST_ASSERT_EQUAL(1, table.activeCount(1000));
    }
    SessionTable reloaded(crypto, storage);
    reloaded.begin();
    TEST_ASSERT_EQUAL(1, reloaded.persistentCount());
    TEST_ASSERT_TRUE(reloaded.validate(token, 5));
}

void test_persistent_no_idle_expiry(void) {
    SessionTable table(crypto, storage);
    table.begin();
    String token = table.create(1000, true);
    uint32_t later = 1000 + 31 * DAY;
    TEST_ASSERT_TRUE(table.validate(token, later));
    TEST_ASSERT_TRUE(table.validate(token, 1000 + SessionTable::kIdleTimeoutMs));
    TEST_ASSERT_EQUAL(1, table.activeCount(later));
}

void test_volatile_not_persisted(void) {
    String token;
    {
        SessionTable table(crypto, storage);
        table.begin();
        token = table.create(1000);
        TEST_ASSERT_TRUE(table.validate(token, 1000));
        TEST_ASSERT_EQUAL(0, table.persistentCount());
    }
    SessionTable reloaded(crypto, storage);
    reloaded.begin();
    TEST_ASSERT_FALSE(reloaded.validate(token, 1000));
    TEST_ASSERT_EQUAL(0, reloaded.activeCount(1000));
    TEST_ASSERT_EQUAL(0, storage.sessions.size());
}

void test_persistent_oldest_replaced(void) {
    const size_t n = SessionTable::kMaxPersistentSessions;
    String tokens[n + 1];
    {
        SessionTable table(crypto, storage);
        table.begin();
        for (size_t i = 0; i <= n; i++) tokens[i] = table.create(1000 + i, true);
        TEST_ASSERT_EQUAL(n, table.persistentCount());
        TEST_ASSERT_FALSE(table.validate(tokens[0], 2000));
        for (size_t i = 1; i <= n; i++) TEST_ASSERT_TRUE(table.validate(tokens[i], 2000));
    }
    TEST_ASSERT_EQUAL(n, storage.sessions.size());

    // Dopo il ricaricamento seq continua: la prossima sostituisce tokens[1]
    SessionTable reloaded(crypto, storage);
    reloaded.begin();
    TEST_ASSERT_FALSE(reloaded.validate(tokens[0], 3000));
    for (size_t i = 1; i <= n; i++) TEST_ASSERT_TRUE(reloaded.validate(tokens[i], 3000));
    String extra = reloaded.create(3000, true);
    TEST_ASSERT_TRUE(reloaded.validate(extra, 3000));
    TEST_ASSERT_FALSE(reloaded.validate(tokens[1], 3000));
    for (size_t i = 2; i <= n; i++) TEST_ASSERT_TRUE(reloaded.validate(tokens[i], 3000));
}

void test_persistent_remove(void) {
    String keep, gone;
    {
        SessionTable table(crypto, storage);
        table.begin();
        keep = table.create(1000, true);
        gone = table.create(1000, true);
        table.remove(gone);
        TEST_ASSERT_FALSE(table.validate(gone, 1000));
        TEST_ASSERT_TRUE(table.validate(keep, 1000));
    }
    SessionTable reloaded(crypto, storage);
    reloaded.begin();
    TEST_ASSERT_FALSE(reloaded.validate(gone, 1000));
    TEST_ASSERT_TRUE(reloaded.validate(keep, 1000));
    TEST_ASSERT_EQUAL(1, reloaded.persistentCount());
}

void test_persistent_clear(void) {
    SessionTable table(crypto, storage);
    table.begin();
    String vol = table.create(1000);
    String per = table.create(1000, true);
    table.clear();
    TEST_ASSERT_FALSE(table.validate(vol, 1000));
    TEST_ASSERT_FALSE(table.validate(per, 1000));
    TEST_ASSERT_EQUAL(0, table.activeCount(1000));
    TEST_ASSERT_EQUAL(0, storage.sessions.size());

    SessionTable reloaded(crypto, storage);
    reloaded.begin();
    TEST_ASSERT_EQUAL(0, reloaded.activeCount(1000));
    TEST_ASSERT_FALSE(reloaded.validate(per, 1000));
}

void test_persistent_clear_save_fails(void) {
    SessionTable table(crypto, storage);
    table.begin();
    String vol = table.create(1000);
    String per = table.create(1000, true);
    storage.failSessionSave = true;
    TEST_ASSERT_FALSE(table.clear());
    // Storage non svuotato: le sessioni restano valide per riprovare
    TEST_ASSERT_TRUE(table.validate(vol, 1000));
    TEST_ASSERT_TRUE(table.validate(per, 1000));
    TEST_ASSERT_EQUAL(1, storage.sessions.size());
    storage.failSessionSave = false;
    TEST_ASSERT_TRUE(table.clear());
    TEST_ASSERT_EQUAL(0, storage.sessions.size());
}

void test_persistent_remove_save_fails(void) {
    SessionTable table(crypto, storage);
    table.begin();
    String vol = table.create(1000);
    String per = table.create(1000, true);
    storage.failSessionSave = true;
    // Volatile e token sconosciuto: nessuna scrittura, true
    TEST_ASSERT_TRUE(table.remove(vol));
    TEST_ASSERT_TRUE(table.remove(repeat('a', 32)));
    TEST_ASSERT_FALSE(table.remove(per));
    TEST_ASSERT_FALSE(table.validate(per, 1000));
}

void test_persistent_load_fails(void) {
    SessionTable seed(crypto, storage);
    seed.begin();
    String old = seed.create(1000, true);
    storage.failSessionLoad = true;
    SessionTable table(crypto, storage);
    table.begin();
    TEST_ASSERT_EQUAL(0, table.persistentCount());
    TEST_ASSERT_FALSE(table.validate(old, 1000));
    String vol = table.create(1000);
    String per = table.create(1000, true);
    TEST_ASSERT_TRUE(table.validate(vol, 1000));
    TEST_ASSERT_TRUE(table.validate(per, 1000));
    TEST_ASSERT_EQUAL(1, table.persistentCount());
}

// Voce di storage per un token noto
static PersistentSession storedSession(const String& token, uint32_t seq) {
    PersistentSession s;
    crypto.sha256((const uint8_t*)token.c_str(), token.length(), s.hash);
    s.seq = seq;
    return s;
}

void test_persistent_seq_renumbered(void) {
    String a = repeat('a', 32), b = repeat('b', 32), c = repeat('c', 32), d = repeat('d', 32);
    storage.sessions.push_back(storedSession(a, 0xFFFFFFFFUL));
    storage.sessions.push_back(storedSession(b, 5));
    storage.sessions.push_back(storedSession(c, 5));
    storage.sessions.push_back(storedSession(d, 3));

    SessionTable table(crypto, storage);
    table.begin();
    // Rinumerate da 1 nell'ordine di seq (stabile) e salvate una volta
    TEST_ASSERT_EQUAL(1, storage.sessionSaves);
    TEST_ASSERT_EQUAL(4, storage.sessions.size());
    TEST_ASSERT_EQUAL_UINT32(4, storage.sessions[0].seq);
    TEST_ASSERT_EQUAL_UINT32(2, storage.sessions[1].seq);
    TEST_ASSERT_EQUAL_UINT32(3, storage.sessions[2].seq);
    TEST_ASSERT_EQUAL_UINT32(1, storage.sessions[3].seq);

    String fresh = table.create(1000, true);
    TEST_ASSERT_EQUAL(5, storage.sessions.size());
    uint32_t fresh_seq = storage.sessions[4].seq;
    for (size_t i = 0; i < 4; i++) TEST_ASSERT_TRUE(fresh_seq > storage.sessions[i].seq);

    // Slot pieni: la prossima sostituisce la più vecchia (d), poi b
    const size_t free_slots = SessionTable::kMaxPersistentSessions - 5;
    for (size_t i = 0; i < free_slots; i++) table.create(1000, true);
    table.create(1000, true);
    TEST_ASSERT_FALSE(table.validate(d, 1000));
    TEST_ASSERT_TRUE(table.validate(a, 1000));
    TEST_ASSERT_TRUE(table.validate(b, 1000));
    TEST_ASSERT_TRUE(table.validate(c, 1000));
    TEST_ASSERT_TRUE(table.validate(fresh, 1000));
    table.create(1000, true);
    TEST_ASSERT_FALSE(table.validate(b, 1000));
    TEST_ASSERT_TRUE(table.validate(c, 1000));
    TEST_ASSERT_TRUE(table.validate(a, 1000));
}

void test_persistent_seq_not_renumbered_when_valid(void) {
    String a = repeat('a', 32), b = repeat('b', 32);
    storage.sessions.push_back(storedSession(a, 7));
    storage.sessions.push_back(storedSession(b, 2));
    SessionTable table(crypto, storage);
    table.begin();
    TEST_ASSERT_EQUAL(0, storage.sessionSaves);
    TEST_ASSERT_EQUAL_UINT32(7, storage.sessions[0].seq);
}

void test_persistent_storage_writes_only_when_expected(void) {
    SessionTable table(crypto, storage);
    table.begin();
    TEST_ASSERT_EQUAL(0, storage.sessionSaves);
    String per = table.create(1000, true);
    TEST_ASSERT_EQUAL(1, storage.sessionSaves);

    String vol = table.create(1000);
    for (int i = 0; i < 5; i++) table.create(1000 + i);
    TEST_ASSERT_EQUAL(1, storage.sessionSaves);

    for (int i = 0; i < 100; i++) {
        TEST_ASSERT_TRUE(table.validate(per, 2000 + i));
        TEST_ASSERT_TRUE(table.validate(vol, 2000 + i));
        table.validate(repeat('0', 32), 2000 + i);
    }
    TEST_ASSERT_EQUAL(1, storage.sessionSaves);

    // remove di una volatile o di un token sconosciuto: nessuna scrittura
    table.remove(vol);
    table.remove(repeat('f', 32));
    TEST_ASSERT_EQUAL(1, storage.sessionSaves);

    table.remove(per);
    TEST_ASSERT_EQUAL(2, storage.sessionSaves);
    table.clear();
    TEST_ASSERT_EQUAL(3, storage.sessionSaves);
}

void test_persistent_token_not_in_storage(void) {
    SessionTable table(crypto, storage);
    table.begin();
    String token = table.create(1000, true);
    TEST_ASSERT_EQUAL(1, storage.sessions.size());
    const PersistentSession& saved = storage.sessions[0];
    TEST_ASSERT_FALSE(containsBytes(&saved, sizeof(saved), token.c_str(), 8));
    uint8_t hash[32];
    crypto.sha256((const uint8_t*)token.c_str(), token.length(), hash);
    TEST_ASSERT_EQUAL_MEMORY(hash, saved.hash, sizeof(hash));
}

// ---------------------------------------------------------------------------
// AuthPolicy
// ---------------------------------------------------------------------------

struct DecideCase {
    const char* path;
    bool is_get;
    bool ap_mode;
    bool configured;
    bool authenticated;
    AuthDecision expected;
};

void test_decide_table(void) {
    const DecideCase cases[] = {
        // AP: tutto passa, con o senza sessione
        {"/", true, true, true, false, AuthDecision::ALLOW},
        {"/api/config", true, true, true, true, AuthDecision::ALLOW},
        {"/update", false, true, false, false, AuthDecision::ALLOW},
        // Rotte pubbliche
        {"/login", true, false, true, false, AuthDecision::ALLOW},
        {"/api/auth/login", false, false, true, false, AuthDecision::ALLOW},
        {"/api/auth/status", true, false, true, false, AuthDecision::ALLOW},
        {"/api/auth/credentials", false, false, false, false, AuthDecision::ALLOW},
        {"/api/auth/logout", false, false, true, false, AuthDecision::ALLOW},
        // Pagine senza sessione
        {"/", true, false, true, false, AuthDecision::REDIRECT_LOGIN},
        {"/update", true, false, true, false, AuthDecision::REDIRECT_LOGIN},
        {"/", true, false, false, false, AuthDecision::REDIRECT_LOGIN},
        // API senza sessione
        {"/api/config", true, false, true, false, AuthDecision::UNAUTHORIZED},
        {"/api/config", true, false, false, false, AuthDecision::SETUP_REQUIRED},
        // POST /update: mai redirect
        {"/update", false, false, true, false, AuthDecision::UNAUTHORIZED},
        {"/update", false, false, false, false, AuthDecision::SETUP_REQUIRED},
        // Metodo sbagliato su rotte pubbliche
        {"/login", false, false, true, false, AuthDecision::UNAUTHORIZED},
        {"/api/auth/login", true, false, true, false, AuthDecision::UNAUTHORIZED},
        {"/api/auth/status", false, false, true, false, AuthDecision::UNAUTHORIZED},
        {"/api/auth/credentials", true, false, false, false, AuthDecision::SETUP_REQUIRED},
        // Varianti dei path pubblici: confronto esatto e case-sensitive (la query
        // string la toglie già WebServer)
        {"/login/", true, false, true, false, AuthDecision::REDIRECT_LOGIN},
        {"/LOGIN", true, false, true, false, AuthDecision::REDIRECT_LOGIN},
        {"/login.html", true, false, true, false, AuthDecision::REDIRECT_LOGIN},
        {"/api/auth/loginX", false, false, true, false, AuthDecision::UNAUTHORIZED},
        {"/api/auth/login/", false, false, true, false, AuthDecision::UNAUTHORIZED},
        {"/api/auth/status/x", true, false, true, false, AuthDecision::UNAUTHORIZED},
        {"/API/AUTH/STATUS", true, false, true, false, AuthDecision::REDIRECT_LOGIN},
        {"/api", true, false, true, false, AuthDecision::REDIRECT_LOGIN},
        {"/api/", true, false, true, false, AuthDecision::UNAUTHORIZED},
        // HEAD/PUT/DELETE arrivano come non-GET: mai ALLOW senza sessione
        {"/", false, false, true, false, AuthDecision::UNAUTHORIZED},
        {"/index.html", false, false, true, false, AuthDecision::UNAUTHORIZED},
        // Con sessione
        {"/", true, false, true, true, AuthDecision::ALLOW},
        {"/api/config", true, false, true, true, AuthDecision::ALLOW},
        {"/update", false, false, true, true, AuthDecision::ALLOW},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const DecideCase& c = cases[i];
        AuthDecision d = AuthPolicy::decide(c.path, c.is_get, c.ap_mode, c.configured, c.authenticated);
        char msg[96];
        snprintf(msg, sizeof(msg), "case %u: %s %s", (unsigned)i, c.is_get ? "GET" : "POST", c.path);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)c.expected, (int)d, msg);
    }
}

void test_extract_cookie(void) {
    TEST_ASSERT_EQUAL_STRING("abc", AuthPolicy::extractCookie("sid=abc", "sid").c_str());
    TEST_ASSERT_EQUAL_STRING("abc", AuthPolicy::extractCookie("a=1; sid=abc; b=2", "sid").c_str());
    TEST_ASSERT_EQUAL_STRING("abc", AuthPolicy::extractCookie("a=1;sid=abc;b=2", "sid").c_str());
    TEST_ASSERT_EQUAL_STRING("abc", AuthPolicy::extractCookie("  a=1 ;  sid = abc  ; b=2", "sid").c_str());
    TEST_ASSERT_EQUAL_STRING("abc", AuthPolicy::extractCookie("xsid=1; sid=abc", "sid").c_str());
    TEST_ASSERT_EQUAL_STRING("", AuthPolicy::extractCookie("xsid=1; sidx=2", "sid").c_str());
    TEST_ASSERT_EQUAL_STRING("", AuthPolicy::extractCookie("", "sid").c_str());
    TEST_ASSERT_EQUAL_STRING("", AuthPolicy::extractCookie("a=1; b=2", "sid").c_str());
    TEST_ASSERT_EQUAL_STRING("", AuthPolicy::extractCookie("sid=", "sid").c_str());
}

// ---------------------------------------------------------------------------
// AuthService
// ---------------------------------------------------------------------------

static const char* kCreds = "{\"username\":\"admin\",\"password\":\"password1\"}";

static bool bodyHasNoSecrets(const AuthResponse& r, const String& token) {
    if (contains(r.body, "password1")) return false;
    if (token.length() > 0 && contains(r.body, token.c_str())) return false;
    return true;
}

void test_service_tofu_ok(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    SessionTable sessions(crypto, storage);
    LoginThrottle throttle;
    AuthService service(store, sessions, throttle);

    AuthResponse r = service.createInitialCredentials(kCreds, 1000);
    TEST_ASSERT_EQUAL(200, r.status);
    TEST_ASSERT_EQUAL_STRING("{\"success\":true,\"username\":\"admin\"}", r.body.c_str());
    TEST_ASSERT_TRUE(r.set_cookie.startsWith("sid="));
    TEST_ASSERT_TRUE(contains(r.set_cookie, "Path=/"));
    TEST_ASSERT_TRUE(contains(r.set_cookie, "HttpOnly"));
    TEST_ASSERT_TRUE(contains(r.set_cookie, "SameSite=Strict"));
    TEST_ASSERT_FALSE(contains(r.set_cookie, "Max-Age"));

    String token = cookieToken(r.set_cookie);
    TEST_ASSERT_EQUAL(32, token.length());
    TEST_ASSERT_TRUE(service.isAuthenticated(token, 2000));
    TEST_ASSERT_TRUE(bodyHasNoSecrets(r, token));
    TEST_ASSERT_TRUE(store.hasCredentials());
}

void test_service_tofu_repeated(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    SessionTable sessions(crypto, storage);
    LoginThrottle throttle;
    AuthService service(store, sessions, throttle);

    TEST_ASSERT_EQUAL(200, service.createInitialCredentials(kCreds, 1000).status);
    AuthResponse r = service.createInitialCredentials("{\"username\":\"evil\",\"password\":\"password2\"}", 1000);
    TEST_ASSERT_EQUAL(409, r.status);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"already_configured\"}", r.body.c_str());
    TEST_ASSERT_EQUAL_STRING("", r.set_cookie.c_str());
    TEST_ASSERT_EQUAL(1, sessions.activeCount(1000));
}

void test_service_tofu_invalid_fields(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    SessionTable sessions(crypto, storage);
    LoginThrottle throttle;
    AuthService service(store, sessions, throttle);

    AuthResponse r = service.createInitialCredentials("{\"username\":\"\",\"password\":\"password1\"}", 1000);
    TEST_ASSERT_EQUAL(400, r.status);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"invalid_username\"}", r.body.c_str());

    r = service.createInitialCredentials("{\"username\":\"admin\",\"password\":\"short\"}", 1000);
    TEST_ASSERT_EQUAL(400, r.status);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"invalid_password\"}", r.body.c_str());
    TEST_ASSERT_EQUAL_STRING("", r.set_cookie.c_str());
    TEST_ASSERT_FALSE(store.hasCredentials());
}

void test_service_tofu_storage_error(void) {
    storage.failSave = true;
    CredentialStore store(crypto, storage);
    store.begin();
    SessionTable sessions(crypto, storage);
    LoginThrottle throttle;
    AuthService service(store, sessions, throttle);

    AuthResponse r = service.createInitialCredentials(kCreds, 1000);
    TEST_ASSERT_EQUAL(500, r.status);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"storage_error\"}", r.body.c_str());
    TEST_ASSERT_EQUAL_STRING("", r.set_cookie.c_str());
    TEST_ASSERT_EQUAL(0, sessions.activeCount(1000));
}

void test_service_invalid_bodies(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    SessionTable sessions(crypto, storage);
    LoginThrottle throttle;
    AuthService service(store, sessions, throttle);

    String big = "{\"username\":\"admin\",\"password\":\"password1\",\"pad\":\"";
    big += repeat('x', 256);
    big += "\"}";
    const String bodies[] = {
        "not json",
        "",
        "{\"username\":\"admin\"}",
        "{\"password\":\"password1\"}",
        "{\"username\":1,\"password\":\"password1\"}",
        "{\"username\":\"admin\",\"password\":null}",
        big,
    };
    for (size_t i = 0; i < sizeof(bodies) / sizeof(bodies[0]); i++) {
        AuthResponse r = service.createInitialCredentials(bodies[i], 1000);
        TEST_ASSERT_EQUAL_MESSAGE(400, r.status, bodies[i].c_str());
        TEST_ASSERT_EQUAL_STRING("{\"error\":\"invalid_request\"}", r.body.c_str());
        r = service.login(bodies[i], 1000);
        TEST_ASSERT_EQUAL_MESSAGE(400, r.status, bodies[i].c_str());
        TEST_ASSERT_EQUAL_STRING("", r.set_cookie.c_str());
    }
    TEST_ASSERT_FALSE(store.hasCredentials());

    // Esattamente 256 byte è accettato
    String exact = "{\"username\":\"admin\",\"password\":\"password1\",\"pad\":\"";
    String tail = "\"}";
    exact += repeat('x', 256 - exact.length() - tail.length());
    exact += tail;
    TEST_ASSERT_EQUAL(256, exact.length());
    TEST_ASSERT_EQUAL(200, service.createInitialCredentials(exact, 1000).status);
}

void test_service_login_without_credentials(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    SessionTable sessions(crypto, storage);
    LoginThrottle throttle;
    AuthService service(store, sessions, throttle);

    AuthResponse r = service.login(kCreds, 1000);
    TEST_ASSERT_EQUAL(409, r.status);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"setup_required\"}", r.body.c_str());
    TEST_ASSERT_EQUAL_STRING("", r.set_cookie.c_str());
}

void test_service_login(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    SessionTable sessions(crypto, storage);
    LoginThrottle throttle;
    AuthService service(store, sessions, throttle);
    TEST_ASSERT_EQUAL(200, service.createInitialCredentials(kCreds, 1000).status);
    sessions.clear();

    AuthResponse r = service.login("{\"username\":\"admin\",\"password\":\"wrongpass\"}", 2000);
    TEST_ASSERT_EQUAL(401, r.status);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"invalid_credentials\"}", r.body.c_str());
    TEST_ASSERT_EQUAL_STRING("", r.set_cookie.c_str());

    r = service.login("{\"username\":\"root\",\"password\":\"password1\"}", 2000);
    TEST_ASSERT_EQUAL(401, r.status);
    TEST_ASSERT_EQUAL_STRING("", r.set_cookie.c_str());
    TEST_ASSERT_EQUAL(0, sessions.activeCount(2000));

    r = service.login(kCreds, 3000);
    TEST_ASSERT_EQUAL(200, r.status);
    TEST_ASSERT_EQUAL_STRING("{\"success\":true,\"username\":\"admin\"}", r.body.c_str());
    TEST_ASSERT_TRUE(contains(r.set_cookie, "HttpOnly"));
    TEST_ASSERT_TRUE(contains(r.set_cookie, "SameSite=Strict"));
    String token = cookieToken(r.set_cookie);
    TEST_ASSERT_TRUE(service.isAuthenticated(token, 4000));
    TEST_ASSERT_TRUE(bodyHasNoSecrets(r, token));
}

void test_service_status(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    SessionTable sessions(crypto, storage);
    LoginThrottle throttle;
    AuthService service(store, sessions, throttle);

    AuthResponse r = service.status("", false, 1000);
    TEST_ASSERT_EQUAL(200, r.status);
    TEST_ASSERT_EQUAL_STRING("{\"configured\":false,\"authenticated\":false,\"ap_mode\":false}", r.body.c_str());

    r = service.status("", true, 1000);
    TEST_ASSERT_EQUAL_STRING("{\"configured\":false,\"authenticated\":false,\"ap_mode\":true}", r.body.c_str());

    TEST_ASSERT_EQUAL(200, service.createInitialCredentials(kCreds, 1000).status);
    sessions.clear();

    // Configurato ma non autenticato: nessuno username
    r = service.status("", false, 1000);
    TEST_ASSERT_EQUAL_STRING("{\"configured\":true,\"authenticated\":false,\"ap_mode\":false}", r.body.c_str());
    r = service.status(repeat('a', 32), false, 1000);
    TEST_ASSERT_FALSE(contains(r.body, "username"));

    String token = cookieToken(service.login(kCreds, 2000).set_cookie);
    r = service.status(token, false, 3000);
    TEST_ASSERT_EQUAL_STRING("{\"configured\":true,\"authenticated\":true,\"ap_mode\":false,\"username\":\"admin\"}",
                             r.body.c_str());
    TEST_ASSERT_TRUE(bodyHasNoSecrets(r, token));
    TEST_ASSERT_EQUAL_STRING("", r.set_cookie.c_str());

    r = service.status(token, true, 3000);
    TEST_ASSERT_EQUAL_STRING("{\"configured\":true,\"authenticated\":true,\"ap_mode\":true,\"username\":\"admin\"}",
                             r.body.c_str());

    // Sessione scaduta
    r = service.status(token, false, 3000 + SessionTable::kIdleTimeoutMs);
    TEST_ASSERT_EQUAL_STRING("{\"configured\":true,\"authenticated\":false,\"ap_mode\":false}", r.body.c_str());
}

void test_service_logout(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    SessionTable sessions(crypto, storage);
    LoginThrottle throttle;
    AuthService service(store, sessions, throttle);
    String first = cookieToken(service.createInitialCredentials(kCreds, 1000).set_cookie);
    String second = cookieToken(service.login(kCreds, 1000).set_cookie);

    AuthResponse r = service.logout(first);
    TEST_ASSERT_EQUAL(200, r.status);
    TEST_ASSERT_EQUAL_STRING("{\"success\":true}", r.body.c_str());
    TEST_ASSERT_EQUAL_STRING("sid=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict", r.set_cookie.c_str());
    TEST_ASSERT_FALSE(service.isAuthenticated(first, 1000));
    TEST_ASSERT_TRUE(service.isAuthenticated(second, 1000));

    // Idempotente, anche senza token
    r = service.logout("");
    TEST_ASSERT_EQUAL(200, r.status);
    TEST_ASSERT_TRUE(contains(r.set_cookie, "Max-Age=0"));
    r = service.logout(first);
    TEST_ASSERT_EQUAL(200, r.status);
    TEST_ASSERT_TRUE(contains(r.set_cookie, "Max-Age=0"));
    TEST_ASSERT_TRUE(service.isAuthenticated(second, 1000));
}

void test_service_is_authenticated(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    SessionTable sessions(crypto, storage);
    LoginThrottle throttle;
    AuthService service(store, sessions, throttle);
    TEST_ASSERT_FALSE(service.isAuthenticated("", 1000));

    String token = cookieToken(service.createInitialCredentials(kCreds, 1000).set_cookie);
    TEST_ASSERT_FALSE(service.isAuthenticated("", 1000));
    TEST_ASSERT_FALSE(service.isAuthenticated(repeat('0', 32), 1000));
    TEST_ASSERT_TRUE(service.isAuthenticated(token, 1000));
}

// ---------------------------------------------------------------------------
// LoginThrottle
// ---------------------------------------------------------------------------

void test_throttle_free_attempts(void) {
    LoginThrottle t;
    TEST_ASSERT_EQUAL(0, t.retryAfterMs(1000));
    for (int i = 0; i < 4; i++) t.recordFailure(1000);
    TEST_ASSERT_EQUAL(0, t.retryAfterMs(1000));
    t.recordFailure(1000);
    TEST_ASSERT_EQUAL(1000, t.retryAfterMs(1000));
    TEST_ASSERT_EQUAL(1, t.retryAfterMs(1999));
    TEST_ASSERT_EQUAL(0, t.retryAfterMs(2000));
}

void test_throttle_backoff_and_cap(void) {
    LoginThrottle t;
    for (int i = 0; i < 5; i++) t.recordFailure(1000);
    t.recordFailure(5000);
    TEST_ASSERT_EQUAL(2000, t.retryAfterMs(5000));
    t.recordFailure(9000);
    TEST_ASSERT_EQUAL(4000, t.retryAfterMs(9000));

    LoginThrottle many;
    for (int i = 0; i < 20; i++) many.recordFailure(1000);
    TEST_ASSERT_EQUAL(LoginThrottle::kMaxDelayMs, many.retryAfterMs(1000));
    TEST_ASSERT_EQUAL(60000, many.retryAfterMs(1000));
    for (int i = 0; i < 100; i++) many.recordFailure(1000);
    TEST_ASSERT_EQUAL(60000, many.retryAfterMs(1000));
    TEST_ASSERT_EQUAL(0, many.retryAfterMs(61000));
}

void test_throttle_success_resets(void) {
    LoginThrottle t;
    for (int i = 0; i < 8; i++) t.recordFailure(1000);
    TEST_ASSERT_TRUE(t.retryAfterMs(1000) > 0);
    t.recordSuccess();
    TEST_ASSERT_EQUAL(0, t.retryAfterMs(1000));
    for (int i = 0; i < 4; i++) t.recordFailure(1000);
    TEST_ASSERT_EQUAL(0, t.retryAfterMs(1000));
}

void test_throttle_rollover(void) {
    LoginThrottle t;
    for (int i = 0; i < 5; i++) t.recordFailure(0xFFFFFF00u);
    TEST_ASSERT_EQUAL(1000, t.retryAfterMs(0xFFFFFF00u));
    // 0x100 ms dopo il fallimento, oltre il rollover
    TEST_ASSERT_EQUAL(1000 - 0x200, t.retryAfterMs(0x00000100u));
    TEST_ASSERT_EQUAL(0, t.retryAfterMs(0xFFFFFF00u + 1000u));
}

// ---------------------------------------------------------------------------
// AuthService: remember, 429, logoutAll
// ---------------------------------------------------------------------------

void test_service_login_remember(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    SessionTable sessions(crypto, storage);
    sessions.begin();
    LoginThrottle throttle;
    AuthService service(store, sessions, throttle);
    TEST_ASSERT_EQUAL(200, service.createInitialCredentials(kCreds, 1000).status);
    sessions.clear();

    AuthResponse r = service.login("{\"username\":\"admin\",\"password\":\"password1\",\"remember\":true}", 2000);
    TEST_ASSERT_EQUAL(200, r.status);
    TEST_ASSERT_TRUE(contains(r.set_cookie, "Max-Age=2592000"));
    TEST_ASSERT_TRUE(contains(r.set_cookie, "HttpOnly"));
    TEST_ASSERT_TRUE(contains(r.set_cookie, "SameSite=Strict"));
    String token = cookieToken(r.set_cookie);
    TEST_ASSERT_EQUAL(32, token.length());
    TEST_ASSERT_EQUAL(1, sessions.persistentCount());
    TEST_ASSERT_TRUE(service.isAuthenticated(token, 2000 + 31 * DAY));
    TEST_ASSERT_EQUAL(AuthService::kRememberMaxAgeS, 2592000);

    const char* no_remember[] = {
        "{\"username\":\"admin\",\"password\":\"password1\"}",
        "{\"username\":\"admin\",\"password\":\"password1\",\"remember\":false}",
        "{\"username\":\"admin\",\"password\":\"password1\",\"remember\":\"true\"}",
        "{\"username\":\"admin\",\"password\":\"password1\",\"remember\":1}",
    };
    for (size_t i = 0; i < sizeof(no_remember) / sizeof(no_remember[0]); i++) {
        r = service.login(no_remember[i], 3000);
        TEST_ASSERT_EQUAL_MESSAGE(200, r.status, no_remember[i]);
        TEST_ASSERT_FALSE_MESSAGE(contains(r.set_cookie, "Max-Age"), no_remember[i]);
    }
    TEST_ASSERT_EQUAL(1, sessions.persistentCount());
}

void test_service_login_throttled(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    SessionTable sessions(crypto, storage);
    sessions.begin();
    LoginThrottle throttle;
    AuthService service(store, sessions, throttle);
    TEST_ASSERT_EQUAL(200, service.createInitialCredentials(kCreds, 1000).status);
    sessions.clear();

    const char* wrong = "{\"username\":\"admin\",\"password\":\"wrongpass\"}";
    for (int i = 0; i < 5; i++) {
        AuthResponse r = service.login(wrong, 2000);
        TEST_ASSERT_EQUAL(401, r.status);
        TEST_ASSERT_EQUAL(0, r.retry_after_s);
    }

    // Il 6° tentativo immediato, anche con la password giusta: 429 senza PBKDF2
    uint32_t derives = crypto.deriveCalls;
    AuthResponse r = service.login(kCreds, 2000);
    TEST_ASSERT_EQUAL(429, r.status);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"too_many_attempts\",\"retry_after\":1}", r.body.c_str());
    TEST_ASSERT_EQUAL(1, r.retry_after_s);
    TEST_ASSERT_EQUAL_STRING("", r.set_cookie.c_str());
    r = service.login(wrong, 2500);
    TEST_ASSERT_EQUAL(429, r.status);
    TEST_ASSERT_TRUE(r.retry_after_s > 0);
    TEST_ASSERT_TRUE(contains(r.body, "\"retry_after\":1"));
    TEST_ASSERT_EQUAL(derives, crypto.deriveCalls);
    TEST_ASSERT_EQUAL(0, sessions.activeCount(2500));

    // Il body non valido e il setup restano prioritari
    TEST_ASSERT_EQUAL(400, service.login("not json", 2500).status);

    // Dopo l'attesa: login corretto, poi un errore torna a essere 401
    r = service.login(kCreds, 3000);
    TEST_ASSERT_EQUAL(200, r.status);
    TEST_ASSERT_TRUE(service.isAuthenticated(cookieToken(r.set_cookie), 3000));
    r = service.login(wrong, 3000);
    TEST_ASSERT_EQUAL(401, r.status);
    TEST_ASSERT_EQUAL(0, r.retry_after_s);
}

void test_service_login_throttle_seconds_round_up(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    SessionTable sessions(crypto, storage);
    sessions.begin();
    LoginThrottle throttle;
    AuthService service(store, sessions, throttle);
    TEST_ASSERT_EQUAL(200, service.createInitialCredentials(kCreds, 1000).status);

    const char* wrong = "{\"username\":\"admin\",\"password\":\"wrongpass\"}";
    for (int i = 0; i < 5; i++) TEST_ASSERT_EQUAL(401, service.login(wrong, 2000).status);
    TEST_ASSERT_EQUAL(401, service.login(wrong, 3000).status);  // 6°: attesa 2 s
    AuthResponse r = service.login(wrong, 3001);
    TEST_ASSERT_EQUAL(429, r.status);
    TEST_ASSERT_EQUAL(2, r.retry_after_s);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"too_many_attempts\",\"retry_after\":2}", r.body.c_str());
}

void test_service_logout_all(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    SessionTable sessions(crypto, storage);
    sessions.begin();
    LoginThrottle throttle;
    AuthService service(store, sessions, throttle);
    String vol = cookieToken(service.createInitialCredentials(kCreds, 1000).set_cookie);
    String per = cookieToken(
        service.login("{\"username\":\"admin\",\"password\":\"password1\",\"remember\":true}", 1000).set_cookie);
    TEST_ASSERT_TRUE(service.isAuthenticated(vol, 1000));
    TEST_ASSERT_TRUE(service.isAuthenticated(per, 1000));

    AuthResponse r = service.logoutAll();
    TEST_ASSERT_EQUAL(200, r.status);
    TEST_ASSERT_EQUAL_STRING("{\"success\":true}", r.body.c_str());
    TEST_ASSERT_EQUAL_STRING("sid=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict", r.set_cookie.c_str());
    TEST_ASSERT_FALSE(service.isAuthenticated(vol, 1000));
    TEST_ASSERT_FALSE(service.isAuthenticated(per, 1000));
    TEST_ASSERT_EQUAL(0, sessions.activeCount(1000));
    TEST_ASSERT_EQUAL(0, storage.sessions.size());
}

void test_service_logout_all_storage_error(void) {
    CredentialStore store(crypto, storage);
    store.begin();
    SessionTable sessions(crypto, storage);
    sessions.begin();
    LoginThrottle throttle;
    AuthService service(store, sessions, throttle);
    String vol = cookieToken(service.createInitialCredentials(kCreds, 1000).set_cookie);
    String per = cookieToken(
        service.login("{\"username\":\"admin\",\"password\":\"password1\",\"remember\":true}", 1000).set_cookie);

    storage.failSessionSave = true;
    AuthResponse r = service.logoutAll();
    TEST_ASSERT_EQUAL(500, r.status);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"storage_error\"}", r.body.c_str());
    TEST_ASSERT_EQUAL(0, r.set_cookie.length());
    TEST_ASSERT_TRUE(service.isAuthenticated(vol, 1000));
    TEST_ASSERT_TRUE(service.isAuthenticated(per, 1000));

    // logout resta 200 anche se il salvataggio fallisce
    AuthResponse out = service.logout(per);
    TEST_ASSERT_EQUAL(200, out.status);
    TEST_ASSERT_FALSE(service.isAuthenticated(per, 1000));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_username_length_limits);
    RUN_TEST(test_username_printable_only);
    RUN_TEST(test_password_length_limits);
    RUN_TEST(test_create_ok_and_reload);
    RUN_TEST(test_create_invalid_inputs);
    RUN_TEST(test_create_twice_already_configured);
    RUN_TEST(test_create_storage_error);
    RUN_TEST(test_verify);
    RUN_TEST(test_verify_without_credentials);
    RUN_TEST(test_verify_uses_record_iterations);
    RUN_TEST(test_salts_differ);
    RUN_TEST(test_record_has_no_plaintext_password);
    RUN_TEST(test_constant_time_equals);
    RUN_TEST(test_session_token_format_and_validity);
    RUN_TEST(test_session_invalid_tokens);
    RUN_TEST(test_session_exact_expiry);
    RUN_TEST(test_session_refresh_keeps_alive);
    RUN_TEST(test_session_rollover);
    RUN_TEST(test_session_lru_replacement);
    RUN_TEST(test_session_expired_slot_reused);
    RUN_TEST(test_session_remove_and_clear);
    RUN_TEST(test_session_token_not_stored);
    RUN_TEST(test_persistent_survives_reload);
    RUN_TEST(test_persistent_no_idle_expiry);
    RUN_TEST(test_volatile_not_persisted);
    RUN_TEST(test_persistent_oldest_replaced);
    RUN_TEST(test_persistent_remove);
    RUN_TEST(test_persistent_clear);
    RUN_TEST(test_persistent_clear_save_fails);
    RUN_TEST(test_persistent_remove_save_fails);
    RUN_TEST(test_persistent_load_fails);
    RUN_TEST(test_persistent_seq_renumbered);
    RUN_TEST(test_persistent_seq_not_renumbered_when_valid);
    RUN_TEST(test_persistent_storage_writes_only_when_expected);
    RUN_TEST(test_persistent_token_not_in_storage);
    RUN_TEST(test_decide_table);
    RUN_TEST(test_extract_cookie);
    RUN_TEST(test_service_tofu_ok);
    RUN_TEST(test_service_tofu_repeated);
    RUN_TEST(test_service_tofu_invalid_fields);
    RUN_TEST(test_service_tofu_storage_error);
    RUN_TEST(test_service_invalid_bodies);
    RUN_TEST(test_service_login_without_credentials);
    RUN_TEST(test_service_login);
    RUN_TEST(test_service_status);
    RUN_TEST(test_service_logout);
    RUN_TEST(test_service_is_authenticated);
    RUN_TEST(test_throttle_free_attempts);
    RUN_TEST(test_throttle_backoff_and_cap);
    RUN_TEST(test_throttle_success_resets);
    RUN_TEST(test_throttle_rollover);
    RUN_TEST(test_service_login_remember);
    RUN_TEST(test_service_login_throttled);
    RUN_TEST(test_service_login_throttle_seconds_round_up);
    RUN_TEST(test_service_logout_all);
    RUN_TEST(test_service_logout_all_storage_error);
    return UNITY_END();
}
