#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include <Arduino.h>
#include "CredentialStore.h"
#include "SessionTable.h"
#include "LoginThrottle.h"

// Risposta delle API di autenticazione; il token viaggia solo in set_cookie
struct AuthResponse {
    int status;
    String body;
    String set_cookie;
    // Secondi per l'header Retry-After dei 429, 0 = assente
    uint32_t retry_after_s = 0;
};

// Logica delle API /api/auth/* (ADR 0009), indipendente dal web server
class AuthService {
public:
    static constexpr const char* kSessionCookie = "sid";
    static constexpr const char* kCookieAttributes = "Path=/; HttpOnly; SameSite=Strict";
    // Limite del body JSON, come WebApiJson::MAX_COMMAND_BODY_LENGTH
    static constexpr size_t kMaxBodyLength = 256;
    // Durata del cookie "Remember me": 30 giorni
    static constexpr uint32_t kRememberMaxAgeS = 2592000;

    AuthService(CredentialStore& credentials, SessionTable& sessions, LoginThrottle& throttle);

    // POST /api/auth/login {username, password[, remember]}: 400, 409, 429 con
    // retry_after (throttle attivo, senza verificare la password), 401 o 200
    AuthResponse login(const String& body, uint32_t now);
    // POST /api/auth/credentials {username, password} senza credenziali (TOFU)
    AuthResponse createInitialCredentials(const String& body, uint32_t now);
    // POST /api/auth/credentials {current_password?, username, password} (ADR 0009).
    // AP/STA è deciso solo da ap_mode, mai dal body:
    //  - body non valido → 400 invalid_request
    //  - nessuna credenziale → createInitialCredentials (TOFU, invariato)
    //  - AP → current_password ignorata, replace, chiusura di tutte le sessioni,
    //    200 senza cookie; 400 invalid_username/invalid_password, 500 storage_error
    //  - STA → sessione valida (401 unauthorized), throttle (429 senza PBKDF2),
    //    current_password mancante o errata → fallimento contato e 401
    //    invalid_current_password; replace (400/500); rotazione della sessione del
    //    chiamante (nuovo token dello stesso tipo nel cookie, Max-Age se persistente)
    //    e chiusura di tutte le altre, vecchio token compreso
    //  - recovery (finestra di recupero aperta, schede senza Wi-Fi, US-091): come in AP
    //    current_password ignorata e nessuna sessione richiesta, replace e chiusura di
    //    tutte le sessioni; poi una sessione nuova per chi ha creato le credenziali
    //    (cookie nel 200, come nella creazione iniziale)
    // Il 200 è {"success":true,"username":...,"sessions_closed":bool}: false se le
    // credenziali sono cambiate ma le sessioni non si sono potute chiudere (in STA
    // niente cookie nuovo e il vecchio token resta valido)
    AuthResponse updateCredentials(const String& body, const String& token, bool ap_mode, uint32_t now,
                                   bool recovery = false);
    // GET /api/auth/status: {configured, authenticated, ap_mode[, username]}, più
    // recovery e recovery_remaining_s solo con la finestra di recupero aperta (US-091)
    AuthResponse status(const String& token, bool ap_mode, uint32_t now, bool recovery = false,
                        uint32_t recovery_remaining_s = 0);
    // POST /api/auth/logout: idempotente, cancella sempre il cookie
    AuthResponse logout(const String& token);
    // POST /api/auth/logout-all: chiude tutte le sessioni, volatili e persistenti;
    // 500 storage_error (senza cookie) se lo storage non è stato svuotato
    AuthResponse logoutAll();
    bool isAuthenticated(const String& token, uint32_t now);

private:
    // Legge username e password (stringhe) e remember opzionale (bool, altrimenti
    // false) dal body; false se il body non è valido
    static bool parseCredentials(const String& body, String& username, String& password, bool& remember);
    // Come parseCredentials, con current_password opzionale (se presente deve essere
    // una stringa); has_current false se assente
    static bool parseUpdate(const String& body, String& current_password, bool& has_current,
                            String& username, String& password);
    static String sessionCookie(const String& token, bool remember = false);
    static String clearCookie();
    static String successBody(const String& username);
    static String updatedBody(const String& username, bool sessions_closed);
    static AuthResponse error(int status, const char* code);
    // 429 too_many_attempts con retry_after in secondi arrotondati per eccesso
    static AuthResponse tooManyAttempts(uint32_t wait_ms);
    static AuthResponse replaceError(CredentialStore::ReplaceResult result);

    CredentialStore& credentials_;
    SessionTable& sessions_;
    LoginThrottle& throttle_;
};

#endif
