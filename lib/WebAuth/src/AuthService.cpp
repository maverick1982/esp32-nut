#include "AuthService.h"
#include <ArduinoJson.h>
#include <string>

AuthService::AuthService(CredentialStore& credentials, SessionTable& sessions)
    : credentials_(credentials), sessions_(sessions) {}

bool AuthService::parseCredentials(const String& body, String& username, String& password) {
    // Body lunghi rifiutati subito e filtro sui soli campi usati
    if (body.length() > kMaxBodyLength) return false;
    JsonDocument filter;
    filter["username"] = true;
    filter["password"] = true;
    JsonDocument req;
    DeserializationError err = deserializeJson(req, body.c_str(), DeserializationOption::Filter(filter));
    if (err) return false;
    if (!req["username"].is<const char*>() || !req["password"].is<const char*>()) return false;
    username = req["username"].as<const char*>();
    password = req["password"].as<const char*>();
    return true;
}

String AuthService::sessionCookie(const String& token) {
    String cookie = kSessionCookie;
    cookie += "=";
    cookie += token;
    cookie += "; ";
    cookie += kCookieAttributes;
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
    if (!parseCredentials(body, username, password)) return error(400, "invalid_request");
    if (!credentials_.hasCredentials()) return error(409, "setup_required");
    if (!credentials_.verify(username, password)) return error(401, "invalid_credentials");

    AuthResponse res;
    res.status = 200;
    res.body = successBody(credentials_.username());
    res.set_cookie = sessionCookie(sessions_.create(now));
    return res;
}

AuthResponse AuthService::createInitialCredentials(const String& body, uint32_t now) {
    String username, password;
    if (!parseCredentials(body, username, password)) return error(400, "invalid_request");

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
    if (token.length() > 0) sessions_.remove(token);
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
