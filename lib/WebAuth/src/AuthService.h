#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include <Arduino.h>
#include "CredentialStore.h"
#include "SessionTable.h"

// Risposta delle API di autenticazione; il token viaggia solo in set_cookie
struct AuthResponse {
    int status;
    String body;
    String set_cookie;
};

// Logica delle API /api/auth/* (ADR 0009), indipendente dal web server
class AuthService {
public:
    static constexpr const char* kSessionCookie = "sid";
    static constexpr const char* kCookieAttributes = "Path=/; HttpOnly; SameSite=Strict";
    // Limite del body JSON, come WebApiJson::MAX_COMMAND_BODY_LENGTH
    static constexpr size_t kMaxBodyLength = 256;

    AuthService(CredentialStore& credentials, SessionTable& sessions);

    // POST /api/auth/login {username, password}
    AuthResponse login(const String& body, uint32_t now);
    // POST /api/auth/credentials {username, password} senza credenziali (TOFU)
    AuthResponse createInitialCredentials(const String& body, uint32_t now);
    // GET /api/auth/status: {configured, authenticated, ap_mode[, username]}
    AuthResponse status(const String& token, bool ap_mode, uint32_t now);
    // POST /api/auth/logout: idempotente, cancella sempre il cookie
    AuthResponse logout(const String& token);
    bool isAuthenticated(const String& token, uint32_t now);

private:
    // Legge username e password (stringhe) dal body; false se il body non è valido
    static bool parseCredentials(const String& body, String& username, String& password);
    static String sessionCookie(const String& token);
    static String clearCookie();
    static String successBody(const String& username);
    static AuthResponse error(int status, const char* code);

    CredentialStore& credentials_;
    SessionTable& sessions_;
};

#endif
