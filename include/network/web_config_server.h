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
    SessionTable sessions;
    AuthService auth;

    void handleConnect();
    void handleLogs();
    void handleNutConfig();
    void handleGetConfig();
    void handleUpsVars();
    void handleSystemStatus();
    void handleBeeper();
    void handleUpsCommands();
    void handleUpsCommand();

    void handleLoginPage();
    void handleAuthLogin();
    void handleAuthStatus();
    void handleAuthCredentials();
    void handleAuthLogout();

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
    USBHostUPS* usb_ups = nullptr;
};

#endif // WEB_CONFIG_SERVER_H
