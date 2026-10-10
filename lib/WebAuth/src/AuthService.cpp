#include "AuthService.h"
#include <ArduinoJson.h>
#include <string>

AuthService::AuthService(CredentialStore& credentials, SessionTable& sessions, LoginThrottle& throttle)
    : credentials_(credentials), sessions_(sessions), throttle_(throttle) {}

bool AuthService::parseCredentials(const String& body, String& username, String& password, bool& remember) {
    remember = false;
    // Body lunghi rifiutati subito e filtro sui soli campi usati
    if (body.length() > kMaxBodyLength) return false;
    JsonDocument filter;
    filter["username"] = true;
    filter["password"] = true;
    filter["remember"] = true;
    JsonDocument req;
    DeserializationError err = deserializeJson(req, body.c_str(), DeserializationOption::Filter(filter));
    if (err) return false;
    if (!req["username"].is<const char*>() || !req["password"].is<const char*>()) return false;
    username = req["username"].as<const char*>();
    password = req["password"].as<const char*>();
    // Solo un vero booleano attiva il cookie persistente
    remember = req["remember"].is<bool>() && req["remember"].as<bool>();
    return true;
}

String AuthService::sessionCookie(const String& token, bool remember) {
    String cookie = kSessionCookie;
    cookie += "=";
    cookie += token;
    cookie += "; ";
    cookie += kCookieAttributes;
    if (remember) {
        cookie += "; Max-Age=";
        cookie += String((unsigned long)kRememberMaxAgeS);
    }
    return cookie;
}

String AuthService::clearCookie() {
    String cookie = kSessionCookie;
    cookie += "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict";
    return cookie;
}

String AuthService::successBody(const String& username) {
    JsonDocument doc;
    doc["success"] = true;
    doc["username"] = username.c_str();
    std::string out;
    serializeJson(doc, out);
    return String(out.c_str());
}

AuthResponse AuthService::error(int status, const char* code) {
    AuthResponse res;
    res.status = status;
    res.body = "{\"error\":\"";
    res.body += code;
    res.body += "\"}";
    return res;
}

AuthResponse AuthService::login(const String& body, uint32_t now) {
    String username, password;
    bool remember = false;
    if (!parseCredentials(body, username, password, remember)) return error(400, "invalid_request");
    if (!credentials_.hasCredentials()) return error(409, "setup_required");

    // Throttle attivo: 429 senza PBKDF2 e senza contare un nuovo fallimento
    uint32_t wait_ms = throttle_.retryAfterMs(now);
    if (wait_ms > 0) {
        uint32_t wait_s = wait_ms / 1000 + (wait_ms % 1000 ? 1 : 0);
        AuthResponse res;
        res.status = 429;
        res.body = "{\"error\":\"too_many_attempts\",\"retry_after\":";
        res.body += String((unsigned long)wait_s);
        res.body += "}";
        res.retry_after_s = wait_s;
        return res;
    }

    if (!credentials_.verify(username, password)) {
        throttle_.recordFailure(now);
        return error(401, "invalid_credentials");
    }
    throttle_.recordSuccess();

    AuthResponse res;
    res.status = 200;
    res.body = successBody(credentials_.username());
    res.set_cookie = sessionCookie(sessions_.create(now, remember), remember);
    return res;
}

AuthResponse AuthService::createInitialCredentials(const String& body, uint32_t now) {
    String username, password;
    bool remember = false;
    if (!parseCredentials(body, username, password, remember)) return error(400, "invalid_request");

    switch (credentials_.create(username, password)) {
        case CredentialStore::CreateResult::OK:
            break;
        case CredentialStore::CreateResult::ALREADY_CONFIGURED:
            return error(409, "already_configured");
        case CredentialStore::CreateResult::INVALID_USERNAME:
            return error(400, "invalid_username");
        case CredentialStore::CreateResult::INVALID_PASSWORD:
            return error(400, "invalid_password");
        case CredentialStore::CreateResult::STORAGE_ERROR:
        default:
            return error(500, "storage_error");
    }

    // Chi crea le credenziali resta autenticato
    AuthResponse res;
    res.status = 200;
    res.body = successBody(credentials_.username());
    res.set_cookie = sessionCookie(sessions_.create(now));
    return res;
}

AuthResponse AuthService::status(const String& token, bool ap_mode, uint32_t now) {
    bool authenticated = isAuthenticated(token, now);
    JsonDocument doc;
    doc["configured"] = credentials_.hasCredentials();
    doc["authenticated"] = authenticated;
    doc["ap_mode"] = ap_mode;
    if (authenticated) doc["username"] = credentials_.username().c_str();
    std::string out;
    serializeJson(doc, out);

    AuthResponse res;
    res.status = 200;
    res.body = String(out.c_str());
    return res;
}

AuthResponse AuthService::logout(const String& token) {
    // Idempotente: 200 anche se il salvataggio di una persistente fallisce
    if (token.length() > 0) sessions_.remove(token);
    AuthResponse res;
    res.status = 200;
    res.body = "{\"success\":true}";
    res.set_cookie = clearCookie();
    return res;
}

AuthResponse AuthService::logoutAll() {
    // Storage non svuotato: 500 senza cookie, la sessione resta valida per riprovare
    if (!sessions_.clear()) return error(500, "storage_error");
    AuthResponse res;
    res.status = 200;
    res.body = "{\"success\":true}";
    res.set_cookie = clearCookie();
    return res;
}

bool AuthService::isAuthenticated(const String& token, uint32_t now) {
    if (!credentials_.hasCredentials() || token.length() == 0) return false;
    return sessions_.validate(token, now);
}
