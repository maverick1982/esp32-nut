#include "web_config_server.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

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
        File file = LittleFS.open("/www/index.html", "r");
        if (file) {
            server.streamFile(file, "text/html");
            file.close();
        } else {
            server.send(404, "text/plain", "Not Found");
        }
    });

    // REST endpoints
    server.on("/api/wifi/scan", HTTP_GET, [this]() { handleScan(); });
    server.on("/api/wifi/connect", HTTP_POST, [this]() { handleConnect(); });

    // Serve static files from LittleFS with Cache-Control
    server.serveStatic("/", LittleFS, "/www/", "no-cache, no-store, must-revalidate");

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
    Serial.println("[WEB] Server avviato sulla porta 80");
}

void WebConfigServer::loop() {
    if (is_ap_mode) {
        dnsServer.processNextRequest();
    }
    server.handleClient();

    if (should_restart && millis() > restart_time) {
        ESP.restart();
    }
}

void WebConfigServer::handleScan() {
    int n = WiFi.scanComplete();

    if (n == WIFI_SCAN_FAILED) {
        // Run async scan, show hidden, passive mode to avoid dropping AP connections, 100ms per channel
        WiFi.scanNetworks(true, true, true, 100);
        server.send(202, "application/json", "{\"status\": \"scanning\"}");
        return;
    }

    if (n == WIFI_SCAN_RUNNING) {
        server.send(202, "application/json", "{\"status\": \"scanning\"}");
        return;
    }

    // Scan is complete
    JsonDocument doc;
    JsonArray networks = doc.to<JsonArray>();

    for (int i = 0; i < n; ++i) {
        JsonObject net = networks.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["sec"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "WPA/WPA2";
        
        int rssi = WiFi.RSSI(i);
        net["sig"] = rssi;
        if (rssi > -60) net["type"] = "excellent";
        else if (rssi > -80) net["type"] = "good";
        else net["type"] = "weak";
    }

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);

    // Free memory
    WiFi.scanDelete();
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
    if (config_mgr.save("/config.json")) {
        server.send(200, "application/json", "{\"success\": true}");
        
        // Asynchronous restart
        should_restart = true;
        restart_time = millis() + 1000;
    } else {
        server.send(500, "application/json", "{\"error\": \"Failed to save config\"}");
    }
}
