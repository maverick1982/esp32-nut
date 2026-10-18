#include "WebApiJson.h"
#include "AuthPolicy.h"
#include "AuthService.h"
#include "CommandCatalog.h"
#include "network/web_config_server.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include "core/app_logger.h"
#include "core/crash_diag.h"
#include "network/web_assets.h"
#include <Update.h>
#include "esp_task_wdt.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

WebConfigServer::WebConfigServer(ConfigManager& config_mgr) 
    : server(80), config_mgr(config_mgr), is_ap_mode(false),
      credentials(auth_crypto, auth_storage), sessions(auth_crypto, auth_storage),
      auth(credentials, sessions, throttle) {}

// Testo inviato dal client, solo per il log: caratteri non stampabili sostituiti e al
// massimo max_len caratteri, così il client non può sporcare il log
static String loggableText(const String& raw, size_t max_len) {
    String out;
    for (size_t i = 0; i < raw.length() && i < max_len; i++) {
        char c = raw[i];
        out += (c >= 0x20 && c <= 0x7E) ? c : '?';
    }
    return out;
}

// Username tentato in un body di login, solo per il log: caratteri non stampabili
// sostituiti e lunghezza limitata, così il client non può sporcare il log
static String loggableUsername(const String& body) {
    if (body.length() > AuthService::kMaxBodyLength) return String("?");
    JsonDocument filter;
    filter["username"] = true;
    JsonDocument req;
    if (deserializeJson(req, body, DeserializationOption::Filter(filter)) || !req["username"].is<const char*>()) {
        return String("?");
    }
    return loggableText(req["username"].as<String>(), 32);
}

void WebConfigServer::begin(bool isAPMode) {
    this->is_ap_mode = isAPMode;
    credentials.begin();
    // Sessioni "Remember me" salvate nel blob NVS "sessions" (ADR 0009)
    sessions.begin();
    AppLogger::log("INFO", credentials.hasCredentials()
        ? "[AUTH] Web UI credentials configured"
        : "[AUTH] No Web UI credentials: first-use setup required");
    if (sessions.persistentCount() > 0) {
        AppLogger::log("INFO", "[AUTH] Persistent sessions restored: %u", (unsigned)sessions.persistentCount());
    }
    
    if (is_ap_mode) {
        // Start DNS Server for Captive Portal
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(53, "*", WiFi.softAPIP());
    }

    static bool _handlers_registered = false;
    if (_handlers_registered) return;
    _handlers_registered = true;

    // Ogni rotta passa dal guard (ADR 0009): in AP è tutto aperto, in STA rispondono
    // senza sessione solo le rotte pubbliche elencate in AuthPolicy

    // Serve explicitly the index.html on root
    server.on("/", HTTP_GET, guarded([this]() {
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.sendHeader("Pragma", "no-cache");
        server.sendHeader("Expires", "-1");
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, "text/html", (const char*)web_asset_index_inlined_html, web_asset_index_inlined_html_len);
    }));

    server.on("/index.html", HTTP_GET, guarded([this]() {
        server.sendHeader("Location", "/", true);
        server.send(301, "text/plain", "");
    }));


    // REST endpoints
    server.on("/api/wifi/connect", HTTP_POST, guarded([this]() { handleConnect(); }));
    server.on("/api/nut/config", HTTP_POST, guarded([this]() { handleNutConfig(); }));
    server.on("/api/logs", HTTP_GET, guarded([this]() { handleLogs(); }));
    server.on("/api/config", HTTP_GET, guarded([this]() { handleGetConfig(); }));
    server.on("/api/ups-vars", HTTP_GET, guarded([this]() { handleUpsVars(); }));
    server.on("/api/system-status", HTTP_GET, guarded([this]() { handleSystemStatus(); }));
    server.on("/api/beeper", HTTP_POST, guarded([this]() { handleBeeper(); }));
    server.on("/api/ups/commands", HTTP_GET, guarded([this]() { handleUpsCommands(); }));
    server.on("/api/ups/command", HTTP_POST, guarded([this]() { handleUpsCommand(); }));
    server.on("/api/ups/variables", HTTP_GET, guarded([this]() { handleUpsVariables(); }));
    server.on("/api/ups/variable", HTTP_POST, guarded([this]() { handleUpsVariable(); }));
    server.on("/api/ups/variable/reset", HTTP_POST, guarded([this]() { handleUpsVariableReset(); }));
    server.on("/api/usb/dump", HTTP_GET, guarded([this]() {
        if (!usb_ups) {
            server.send(503, "application/json", "{\"error\": \"UPS non inizializzato\"}");
            return;
        }
        server.sendHeader("Content-Disposition", "attachment; filename=\"usb_diagnostics.json\"");
        server.send(200, "application/json", usb_ups->dumpUSBDiagnostics());
    }));

    // Rotte pubbliche dell'autenticazione (elencate in AuthPolicy)
    server.on("/login", HTTP_GET, guarded([this]() { handleLoginPage(); }));
    server.on("/api/auth/login", HTTP_POST, guarded([this]() { handleAuthLogin(); }));
    server.on("/api/auth/status", HTTP_GET, guarded([this]() { handleAuthStatus(); }));
    server.on("/api/auth/credentials", HTTP_POST, guarded([this]() { handleAuthCredentials(); }));
    server.on("/api/auth/logout", HTTP_POST, guarded([this]() { handleAuthLogout(); }));
    // Non pubblica: il guard richiede una sessione valida (in STA)
    server.on("/api/auth/logout-all", HTTP_POST, guarded([this]() { handleAuthLogoutAll(); }));

    // OTA endpoints
    server.on("/update", HTTP_GET, guarded([this]() { handleOTAPage(); }));
    // Il controllo che conta è nella callback di upload (UPLOAD_FILE_START), che gira
    // prima di questo handler e prima di qualsiasi scrittura nel flash
    server.on("/update", HTTP_POST,
        guarded([this]() {
            // Nessun file ricevuto (body non multipart): niente "OK" e niente riavvio
            if (!ota_upload_started) {
                server.sendHeader("Connection", "close");
                server.send(400, "application/json", "{\"error\":\"multipart upload required\"}");
                return;
            }
            ota_upload_started = false;
            server.sendHeader("Connection", "close");
            server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
            if (!Update.hasError()) {
                should_restart = true;
                restart_request_time = millis();
            }
        }),
        [this]() { handleOTAUpload(); }
    );

    // Catch-all for Captive Portal (redirect to captive page if not found).
    // In AP il guard restituisce sempre ALLOW: il redirect captive resta invariato
    server.onNotFound([this]() {
        if (!authorize()) return;
        if (is_ap_mode && server.hostHeader() != WiFi.softAPIP().toString()) {
            server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
            server.send(302, "text/plain", "");
        } else {
            server.send(404, "text/plain", "Not found");
        }
    });

    // Content-Type per requireJson (anti-CSRF), Cookie per la sessione (ADR 0009)
    static const char* collectedHeaders[] = {"Content-Type", "Cookie"};
    server.collectHeaders(collectedHeaders, 2);

    server.begin();
    AppLogger::log("INFO", "[WEB] Server started on port 80");
}

void WebConfigServer::loop() {
    if (is_ap_mode) {
        dnsServer.processNextRequest();
    }
    server.handleClient();

    if (should_restart && (millis() - restart_request_time >= 1000)) {
        if (usb_ups) {
            usb_ups->end();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        ESP.restart();
    }
}

void WebConfigServer::setUPS(USBHostUPS* ups) {
    usb_ups = ups;
}

void WebConfigServer::handleConnect() {
    if (!requireJson()) return;
    if (server.hasArg("plain") == false) {
        server.send(400, "application/json", "{\"error\": \"Body not received\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));

    if (error) {
        server.send(400, "application/json", "{\"error\": \"Invalid JSON\"}");
        return;
    }

    String ssid = doc["ssid"].as<String>();
    String pwd = doc["password"].as<String>();

    WifiConfig wc;
    wc.ssid = ssid;
    wc.password = pwd;

    config_mgr.setWifiConfig(wc);
    if (config_mgr.save()) {
        server.send(200, "application/json", "{\"success\": true}");
        
        // Asynchronous restart
        should_restart = true;
        restart_request_time = millis();
    } else {
        server.send(500, "application/json", "{\"error\": \"Failed to save config\"}");
    }
}

void WebConfigServer::handleLogs() {
    String logs = AppLogger::getLogsJSON();
    // La UI converte il campo "time" (millis) nell'ora del browser (issue 60)
    server.sendHeader("X-Uptime-Ms", String(millis()));
    server.send(200, "application/json", logs);
}

void WebConfigServer::handleNutConfig() {
    if (!requireJson()) return;
    if (server.hasArg("plain") == false) {
        server.send(400, "application/json", "{\"error\": \"Body not received\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));

    if (error) {
        server.send(400, "application/json", "{\"error\": \"Invalid JSON\"}");
        return;
    }

    // ups_name not a string or not valid → 400, nothing saved and no restart (US-082)
    String rejected;
    if (int status = WebApiJson::checkNutConfigBody(doc, rejected)) {
        server.send(status, "application/json", rejected);
        return;
    }

    NutConfig nc = config_mgr.getNutConfig();
    if (doc["username"].is<String>()) {
        nc.username = doc["username"].as<String>();
    }
    if (doc["password"].is<String>()) {
        nc.password = doc["password"].as<String>();
    }
    if (doc["ups_name"].is<String>()) {
        nc.ups_name = doc["ups_name"].as<String>();
    }

    config_mgr.setNutConfig(nc);
    if (config_mgr.save()) {
        server.send(200, "application/json", "{\"success\": true}");
        AppLogger::log("INFO", "[WEB] NUT configuration updated");
        should_restart = true;
        restart_request_time = millis();
    } else {
        server.send(500, "application/json", "{\"error\": \"Failed to save config\"}");
    }
}

void WebConfigServer::handleGetConfig() {
    JsonDocument doc;
    
    JsonObject wifiObj = doc["wifi"].to<JsonObject>();
    wifiObj["ssid"] = config_mgr.getWifiConfig().ssid;
    wifiObj["mode"] = is_ap_mode ? "AP" : "STA";
    
    JsonObject nutObj = doc["nut"].to<JsonObject>();
    nutObj["username"] = config_mgr.getNutConfig().username;
    nutObj["ups_name"] = config_mgr.getNutConfig().ups_name;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void WebConfigServer::handleUpsVars() {
    if (!usb_ups) {
        server.send(503, "application/json", "{\"error\": \"UPS non inizializzato\"}");
        return;
    }
    String response = WebApiJson::generateUpsVars(usb_ups);
    server.send(200, "application/json", response);
}

void WebConfigServer::handleSystemStatus() {
    JsonDocument doc;
    
    doc["version"] = FIRMWARE_VERSION;
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char dev_id[32];
    // Lo standard convenzionale per i dispositivi ESP è usare la seconda metà del MAC (gli ultimi 3 byte)
    // poiché i primi 3 byte rappresentano l'OUI di Espressif.
    snprintf(dev_id, sizeof(dev_id), "%s-%02X%02X%02X", ESP.getChipModel(), mac[3], mac[4], mac[5]);
    doc["device_id"] = String(dev_id);
    
    // Wi-Fi status
    wl_status_t wifi_status = WiFi.status();
    String wifi_status_str = "Disconnected";
    if (is_ap_mode) {
        wifi_status_str = "AP Mode Active";
    } else if (wifi_status == WL_CONNECTED) {
        wifi_status_str = WiFi.SSID();
    } else {
        wifi_status_str = "Connecting";
    }
    doc["wifi"]["status"] = wifi_status_str;
    
    // UPS status
    String ups_status_str = "Disconnected";
    if (usb_ups && usb_ups->isConnected()) {
        auto data = usb_ups->getUPSData();
        String model = data->hasKey("ups.model") ? data->get("ups.model") : "Unknown Model";
        ups_status_str = model;
    } else {
        ups_status_str = "Disconnected";
    }
    doc["ups"]["status"] = ups_status_str;
    // A missing UPS is already "Disconnected": the stale banner is for an attached one,
    // and not for the first seconds after the claim, while the first full poll runs
    if (usb_ups && usb_ups->isConnected() && usb_ups->isDataStale() && !usb_ups->isWaitingFirstData()) {
        doc["ups"]["stale"] = true;
    }

    CrashDiag::fillJson(doc["diagnostics"].to<JsonObject>());

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void WebConfigServer::handleBeeper() {
    if (!requireJson()) return;
    if (server.hasArg("plain") == false) {
        server.send(400, "application/json", "{\"error\": \"Body not received\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));

    if (error) {
        server.send(400, "application/json", "{\"error\": \"Invalid JSON\"}");
        return;
    }

    if (doc["enable"].is<bool>()) {
        bool enable = doc["enable"].as<bool>();
        if (usb_ups) {
            usb_ups->setBeeper(enable);
            server.send(200, "application/json", "{\"success\": true}");
            return;
        }
    }
    
    server.send(500, "application/json", "{\"error\": \"Failed to set beeper\"}");
}

// Catalogo dei comandi supportati dall'UPS (US-059), uguale in STA e in AP (US-068)
void WebConfigServer::handleUpsCommands() {
    server.send(200, "application/json", WebApiJson::generateUpsCommands(usb_ups));
}

// Esegue un comando: la logica e gli status sono in WebApiJson::runUpsCommand. Nessun
// blocco dei distruttivi in AP (US-068): la conferma la chiede la pagina
void WebConfigServer::handleUpsCommand() {
    if (!requireJson()) return;
    String body = server.hasArg("plain") ? server.arg("plain") : String();
    String response;
    int status = WebApiJson::runUpsCommand(usb_ups, body, response);
    if (status == 200) {
        // Il nome è quello del catalogo (stringa costante), non quello del client
        JsonDocument filter;
        filter["name"] = true;
        JsonDocument req;
        if (!deserializeJson(req, body, DeserializationOption::Filter(filter))) {
            const UPSCommandInfo* info = CommandCatalog::find(req["name"].as<const char*>());
            if (info && info->destructive) {
                AppLogger::log("WARN", String("[CMD] Destructive command '") + info->name +
                               "' run from the Web UI by '" + webUiUser() + "'");
            }
        }
    }
    server.send(status, "application/json", response);
}

// Variabili scrivibili dell'UPS (US-068), uguale in STA e in AP
void WebConfigServer::handleUpsVariables() {
    server.send(200, "application/json", WebApiJson::generateUpsVariables(usb_ups));
}

// Scrive una variabile: la logica e gli status sono in WebApiJson::runUpsVariable
void WebConfigServer::handleUpsVariable() {
    if (!requireJson()) return;
    String body = server.hasArg("plain") ? server.arg("plain") : String();
    String response;
    bool host_called = false;
    int status = WebApiJson::runUpsVariable(usb_ups, body, response, &host_called);
    // L'esito di setVariable() lo registra già l'host: una sola riga per richiesta
    if (status != 200 && !host_called) logVariableRejected(body, true, response);
    server.send(status, "application/json", response);
}

// Riporta al default una variabile lato driver: logica in WebApiJson::resetUpsVariable
void WebConfigServer::handleUpsVariableReset() {
    if (!requireJson()) return;
    String body = server.hasArg("plain") ? server.arg("plain") : String();
    String response;
    bool host_called = false;
    int status = WebApiJson::resetUpsVariable(usb_ups, body, response, &host_called);
    // L'esito di resetVariable() lo registra già l'host
    if (status != 200 && !host_called) logVariableRejected(body, false, response);
    server.send(status, "application/json", response);
}

String WebConfigServer::webUiUser() {
    // In AP non c'è sessione: l'utente configurato non è chi ha inviato la richiesta
    return is_ap_mode ? String("(AP mode)") : credentials.username();
}

// Nome e valore riletti dal body, che può essere JSON non valido: "?" se mancano, testo
// del client ripulito e troncato. Il motivo è il campo "error" della risposta
void WebConfigServer::logVariableRejected(const String& body, bool with_value, const String& response) {
    String name("?");
    String value("?");
    if (body.length() <= WebApiJson::MAX_COMMAND_BODY_LENGTH) {
        JsonDocument filter;
        filter["name"] = true;
        if (with_value) filter["value"] = true;
        JsonDocument req;
        if (!deserializeJson(req, body, DeserializationOption::Filter(filter))) {
            if (req["name"].is<const char*>()) {
                String raw = req["name"].as<String>();
                if (raw.length() > 0) name = loggableText(raw, 48);
            }
            if (with_value && req["value"].is<const char*>()) {
                String raw = req["value"].as<String>();
                value = loggableText(raw, 32);
                if (raw.length() > 32) value += "...";
            }
        }
    }
    String reason("rejected");
    JsonDocument res;
    if (!deserializeJson(res, response) && res["error"].is<const char*>()) {
        reason = res["error"].as<String>();
    }
    String msg = String("Variable ") + name;
    if (with_value) msg += String(" = \"") + value + "\"";
    else msg += " reset";
    msg += String(" from the Web UI by '") + webUiUser() + "': " + reason;
    AppLogger::log("WARN", msg);
}

void WebConfigServer::handleOTAPage() {
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", (const char*)web_asset_update_inlined_html, web_asset_update_inlined_html_len);
}

void WebConfigServer::handleOTAUpload() {
    // WebServer chiama questa callback anche per un POST non multipart (percorso "raw",
    // RequestHandlersImpl::canRaw), prima del guard e senza HTTPUpload: server.upload()
    // dereferenzierebbe un puntatore nullo. Stesso test che il core usa per isForm
    if (!server.header("Content-Type").startsWith("multipart/")) return;

    // WebServer legge tutto l'upload dentro handleClient(): loop() non riparte fino alla
    // fine e con un Wi-Fi lento si superano i 30 s del watchdog del loopTask (US-085).
    // Un reset a ogni blocco ricevuto non lo disattiva: un upload fermo viene comunque
    // abbandonato da WebServer dopo HTTP_MAX_DATA_WAIT (5 s)
    esp_task_wdt_reset();

    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        // uri, metodo e header raccolti sono già disponibili: WebServer analizza gli
        // header prima del body multipart. Senza sessione Update.begin() non parte
        ota_rejected = !authorizedQuiet();
        ota_upload_started = !ota_rejected;
        if (ota_rejected) {
            AppLogger::log("WARN", "[OTA] Upload rejected: not authenticated");
            return;
        }
        AppLogger::log("INFO", String("[OTA] Upload started: ") + upload.filename);
        ota_start_ms = millis();
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace, U_FLASH)) { //start with max available size
            Update.printError(Serial);
        }
    } else if (ota_rejected) {
        // Upload rifiutato: chunk ignorati, il guard del handler finale risponde 401
        if (upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED) ota_rejected = false;
        return;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        // Validazione magic byte E9 sul primo chunk
        if (upload.totalSize == 0 && upload.currentSize > 0) {
            if (upload.buf[0] != 0xE9) {
                AppLogger::log("ERROR", "[OTA] Invalid magic byte");
                Update.abort();
                return;
            }
        }
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
            AppLogger::log("INFO", String("[OTA] Success: ") + String(upload.totalSize) + " bytes in " +
                                   String(millis() - ota_start_ms) + " ms");
        } else {
            Update.printError(Serial);
            AppLogger::log("ERROR", "[OTA] Error at the end");
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        // Il handler finale non viene chiamato: il flag non deve valere per la richiesta dopo
        Update.abort();
        ota_upload_started = false;
        AppLogger::log("ERROR", "[OTA] Upload aborted");
    }
}

// --- Autenticazione (ADR 0009) ---

WebServer::THandlerFunction WebConfigServer::guarded(WebServer::THandlerFunction handler) {
    return [this, handler]() {
        if (!authorize()) return;
        handler();
    };
}

String WebConfigServer::sessionToken() {
    return AuthPolicy::extractCookie(server.header("Cookie"), AuthService::kSessionCookie);
}

AuthDecision WebConfigServer::evaluateRequest() {
    // In AP la policy consente tutto: il token non viene nemmeno validato
    bool authenticated = !is_ap_mode && auth.isAuthenticated(sessionToken(), millis());
    return AuthPolicy::decide(server.uri(), server.method() == HTTP_GET, is_ap_mode,
                              credentials.hasCredentials(), authenticated);
}

bool WebConfigServer::authorize() {
    switch (evaluateRequest()) {
        case AuthDecision::ALLOW:
            return true;
        case AuthDecision::REDIRECT_LOGIN:
            server.sendHeader("Location", "/login", true);
            server.sendHeader("Cache-Control", "no-store");
            server.send(302, "text/plain", "");
            return false;
        case AuthDecision::SETUP_REQUIRED:
            server.send(401, "application/json", "{\"error\":\"setup_required\"}");
            return false;
        case AuthDecision::UNAUTHORIZED:
        default:
            server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
            return false;
    }
}

bool WebConfigServer::authorizedQuiet() {
    return evaluateRequest() == AuthDecision::ALLOW;
}

bool WebConfigServer::requireJson() {
    // Solo application/json: una pagina di un'altra origine non può inviarlo
    // senza preflight CORS, che il server non gestisce (niente CSRF con text/plain)
    String contentType = server.header("Content-Type");
    contentType.toLowerCase();
    if (!contentType.startsWith("application/json")) {
        server.send(415, "application/json", "{\"error\":\"Unsupported content type\"}");
        return false;
    }
    return true;
}

void WebConfigServer::sendAuth(const AuthResponse& res) {
    if (res.set_cookie.length() > 0) {
        server.sendHeader("Set-Cookie", res.set_cookie);
    }
    if (res.retry_after_s > 0) {
        server.sendHeader("Retry-After", String(res.retry_after_s));
    }
    server.sendHeader("Cache-Control", "no-store");
    server.send(res.status, "application/json", res.body);
}

void WebConfigServer::handleLoginPage() {
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", (const char*)web_asset_login_inlined_html, web_asset_login_inlined_html_len);
}

void WebConfigServer::handleAuthLogin() {
    if (!requireJson()) return;
    String body = server.hasArg("plain") ? server.arg("plain") : String();
    AuthResponse res = auth.login(body, millis());
    if (res.status == 200) {
        throttle_logged_failures = 0;  // il successo azzera anche il throttle
        AppLogger::log("INFO", String("[AUTH] Login succeeded for user '") + credentials.username() + "'");
    } else if (res.status == 401) {
        AppLogger::log("INFO", String("[AUTH] Login failed for user '") + loggableUsername(body) + "'");
    } else if (res.status == 429 && throttle.failures() != throttle_logged_failures) {
        // Solo la prima richiesta respinta di ogni finestra (niente flood nel log)
        throttle_logged_failures = throttle.failures();
        AppLogger::log("WARN", String("[AUTH] Login throttled for user '") + loggableUsername(body) +
                       "': retry after " + String(res.retry_after_s) + " s");
    }
    sendAuth(res);
}

void WebConfigServer::handleAuthStatus() {
    sendAuth(auth.status(sessionToken(), is_ap_mode, millis()));
}

void WebConfigServer::handleAuthCredentials() {
    if (!requireJson()) return;
    String body = server.hasArg("plain") ? server.arg("plain") : String();
    // Prima creazione (TOFU), reset in AP o cambio in STA (US-063): AP/STA solo da is_ap_mode
    const char* action = !credentials.hasCredentials() ? "created" : (is_ap_mode ? "reset" : "changed");
    // Durata del PBKDF2 nel log solo alla creazione (verifica dell'obiettivo dell'ADR)
    bool creating = !credentials.hasCredentials();
    auth_crypto.setLogTiming(creating);
    AuthResponse res = auth.updateCredentials(body, sessionToken(), is_ap_mode, millis());
    auth_crypto.setLogTiming(false);
    if (res.status == 200) {
        AppLogger::log("INFO", String("[AUTH] Web UI credentials ") + action + " for user '" + credentials.username() + "'");
        if (res.body.indexOf("\"sessions_closed\":false") >= 0) {
            AppLogger::log("ERROR", "[AUTH] Credentials changed but other sessions could not be closed");
        }
    } else if (res.status != 429 || throttle.failures() != throttle_logged_failures) {
        // 429: solo la prima richiesta respinta di ogni finestra, come per il login
        if (res.status == 429) throttle_logged_failures = throttle.failures();
        AppLogger::log("WARN", String("[AUTH] Credentials ") + (creating ? "creation" : "change") +
                       " refused (HTTP " + String(res.status) + ")");
    }
    sendAuth(res);
}

void WebConfigServer::handleAuthLogout() {
    if (!requireJson()) return;
    sendAuth(auth.logout(sessionToken()));
}

void WebConfigServer::handleAuthLogoutAll() {
    if (!requireJson()) return;
    // In AP la policy non valida le sessioni: chiunque sia sull'AP potrebbe chiuderle
    if (is_ap_mode) {
        server.send(403, "application/json", "{\"error\":\"Not available in Access Point mode\"}");
        return;
    }
    AuthResponse res = auth.logoutAll();
    if (res.status == 200) {
        AppLogger::log("INFO", String("[AUTH] All sessions closed by '") + credentials.username() + "'");
    } else {
        AppLogger::log("ERROR", "[AUTH] Logout of all sessions failed: storage not cleared");
    }
    sendAuth(res);
}

