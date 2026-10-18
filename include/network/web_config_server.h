#ifndef WEB_CONFIG_SERVER_H
#define WEB_CONFIG_SERVER_H

#include <functional>
#include <WebServer.h>
#include <DNSServer.h>
#include "core/config_manager.h"
#include "USBHostUPS.h"
#include "AuthPolicy.h"
#include "AuthService.h"
#include "CredentialStore.h"
#include "LoginThrottle.h"
#include "SessionTable.h"
#include "network/auth_platform.h"

class WebConfigServer {
public:
    WebConfigServer(ConfigManager& config_mgr);
    void begin(bool isAPMode);
    void loop();
    void setUPS(USBHostUPS* ups);

private:
    WebServer server;
    DNSServer dnsServer;
    ConfigManager& config_mgr;
    bool is_ap_mode;

    // Autenticazione della Web UI (ADR 0009); l'ordine di dichiarazione è quello di costruzione
    EspAuthCrypto auth_crypto;
    NvsAuthStorage auth_storage;
    CredentialStore credentials;
    LoginThrottle throttle;
    // failures() del throttle all'ultimo WARN 429: un solo log per finestra di attesa
    uint32_t throttle_logged_failures = 0;
    SessionTable sessions;
    AuthService auth;

    void handleConnect();
    void handleLogs();
    void handleNutConfig();
    void handleGetConfig();
    void handleUpsVars();
    void handleSystemStatus();
    void handleUpsCommands();
    void handleUpsCommand();
    void handleUpsVariables();
    void handleUpsVariable();
    void handleUpsVariableReset();
    // Chi ha inviato la richiesta, per il log: in AP non c'è sessione, quindi "(AP mode)"
    String webUiUser();
    // WARN per una scrittura o un reset rifiutati prima di arrivare all'host (with_value: scrittura)
    void logVariableRejected(const String& body, bool with_value, const String& response);

    void handleLoginPage();
    void handleAuthLogin();
    void handleAuthStatus();
    void handleAuthCredentials();
    void handleAuthLogout();
    void handleAuthLogoutAll();

    void handleOTAPage();
    void handleOTAUpload();

    // Guard: ogni rotta registrata passa da qui (nessun server.on() diretto)
    WebServer::THandlerFunction guarded(WebServer::THandlerFunction handler);
    // Valore del cookie di sessione della richiesta corrente
    String sessionToken();
    // Decisione per la richiesta corrente (uri e metodo), senza inviare risposte
    AuthDecision evaluateRequest();
    // false se ha già risposto (302 verso /login o 401 JSON)
    bool authorize();
    // Stessa decisione di authorize() senza risposta (callback di upload OTA)
    bool authorizedQuiet();
    // 415 se il Content-Type non è application/json (anti-CSRF); false se ha risposto
    bool requireJson();
    void sendAuth(const AuthResponse& res);

    bool should_restart = false;
    unsigned long restart_request_time = 0;
    // Upload OTA rifiutato a UPLOAD_FILE_START: chunk e fine sono ignorati (il guard risponde 401)
    bool ota_rejected = false;
    // Un upload multipart autorizzato è partito: senza, il handler finale non dichiara successo
    bool ota_upload_started = false;
    // millis() a UPLOAD_FILE_START: durata dell'upload nel log di successo (US-085)
    uint32_t ota_start_ms = 0;
    USBHostUPS* usb_ups = nullptr;
};

#endif // WEB_CONFIG_SERVER_H
