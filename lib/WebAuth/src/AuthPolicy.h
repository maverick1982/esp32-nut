#ifndef AUTH_POLICY_H
#define AUTH_POLICY_H

#include <Arduino.h>

enum class AuthDecision { ALLOW, REDIRECT_LOGIN, UNAUTHORIZED, SETUP_REQUIRED };

// Decisione di accesso per ogni rotta (ADR 0009, HTTP layer); logica pura
class AuthPolicy {
public:
    // AP → sempre ALLOW; rotte pubbliche e utenti autenticati → ALLOW;
    // pagine (GET fuori da /api/) → REDIRECT_LOGIN; il resto → SETUP_REQUIRED o UNAUTHORIZED
    static AuthDecision decide(const String& path, bool is_get, bool ap_mode, bool configured, bool authenticated);
    // Valore del cookie `name` nell'header Cookie, stringa vuota se assente
    static String extractCookie(const String& header, const char* name);

private:
    static bool isPublicRoute(const String& path, bool is_get);
};

#endif
