#include "web_config_server.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include "app_logger.h"
#include "web_assets.h"

WebConfigServer::WebConfigServer(ConfigManager& config_mgr) 
    : server(80), config_mgr(config_mgr), is_ap_mode(false) {}

void WebConfigServer::begin(bool isAPMode) {
    is_ap_mode = isAPMode;
    
    if (is_ap_mode) {
        // Start DNS Server for Captive Portal
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(53, "*", WiFi.softAPIP());
    }

    // Serve explicitly the index.html on root
    server.on("/", HTTP_GET, [this]() {
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.sendHeader("Pragma", "no-cache");
        server.sendHeader("Expires", "-1");
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, "text/html", (const char*)web_asset_index_html, web_asset_index_html_len);
    });

    server.on("/index.html", HTTP_GET, [this]() {
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, "text/html", (const char*)web_asset_index_html, web_asset_index_html_len);
    });

    server.on("/app.js", HTTP_GET, [this]() {
        server.sendHeader("Cache-Control", "public, max-age=31536000");
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, "application/javascript", (const char*)web_asset_app_js, web_asset_app_js_len);
    });

    server.on("/shared.css", HTTP_GET, [this]() {
        server.sendHeader("Cache-Control", "public, max-age=31536000");
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, "text/css", (const char*)web_asset_shared_css, web_asset_shared_css_len);
    });

    // REST endpoints
    server.on("/api/wifi/connect", HTTP_POST, [this]() { handleConnect(); });
    server.on("/api/nut/config", HTTP_POST, [this]() { handleNutConfig(); });
    server.on("/api/logs", HTTP_GET, [this]() { handleLogs(); });
    server.on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });

    // Catch-all for Captive Portal (redirect to captive page if not found)
    server.onNotFound([this]() {
        if (is_ap_mode && server.hostHeader() != WiFi.softAPIP().toString()) {
            server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
            server.send(302, "text/plain", "");
        } else {
            server.send(404, "text/plain", "Not found");
        }
    });

    server.begin();
    AppLogger::log("INFO", "[WEB] Server avviato sulla porta 80");
}

void WebConfigServer::loop() {
    if (is_ap_mode) {
        dnsServer.processNextRequest();
    }
    server.handleClient();

    if (should_restart && (millis() - restart_request_time >= 1000)) {
        ESP.restart();
    }
}

void WebConfigServer::handleConnect() {
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
    server.send(200, "application/json", AppLogger::getLogsJSON());
}

void WebConfigServer::handleNutConfig() {
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
