#include "network/web_config_server.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include "core/app_logger.h"
#include "network/web_assets.h"
#include <Update.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

WebConfigServer::WebConfigServer(ConfigManager& config_mgr) 
    : server(80), config_mgr(config_mgr), is_ap_mode(false) {}

void WebConfigServer::begin(bool isAPMode) {
    this->is_ap_mode = isAPMode;
    
    if (is_ap_mode) {
        // Start DNS Server for Captive Portal
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(53, "*", WiFi.softAPIP());
    }

    static bool _handlers_registered = false;
    if (_handlers_registered) return;
    _handlers_registered = true;

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
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, "application/javascript", (const char*)web_asset_app_js, web_asset_app_js_len);
    });

    server.on("/fflate.min.js", HTTP_GET, [this]() {
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, "application/javascript", (const char*)web_asset_fflate_min_js, web_asset_fflate_min_js_len);
    });

    server.on("/shared.css", HTTP_GET, [this]() {
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, "text/css", (const char*)web_asset_shared_css, web_asset_shared_css_len);
    });

    server.on("/ups.css", HTTP_GET, [this]() {
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, "text/css", (const char*)web_asset_ups_css, web_asset_ups_css_len);
    });

    server.on("/mobile.css", HTTP_GET, [this]() {
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, "text/css", (const char*)web_asset_mobile_css, web_asset_mobile_css_len);
    });

    server.on("/logo.png", HTTP_GET, [this]() {
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, "image/png", (const char*)web_asset_logo_png, web_asset_logo_png_len);
    });

    server.on("/favicon.ico", HTTP_GET, [this]() {
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, "image/x-icon", (const char*)web_asset_favicon_ico, web_asset_favicon_ico_len);
    });


    // REST endpoints
    server.on("/api/wifi/connect", HTTP_POST, [this]() { handleConnect(); });
    server.on("/api/nut/config", HTTP_POST, [this]() { handleNutConfig(); });
    server.on("/api/logs", HTTP_GET, [this]() { handleLogs(); });
    server.on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });
    server.on("/api/ups-vars", HTTP_GET, [this]() { handleUpsVars(); });
    server.on("/api/system-status", HTTP_GET, [this]() { handleSystemStatus(); });
    server.on("/api/beeper", HTTP_POST, [this]() { handleBeeper(); });
    server.on("/api/usb/dump", HTTP_GET, [this]() {
        if (!usb_ups) {
            server.send(503, "application/json", "{\"error\": \"UPS non inizializzato\"}");
            return;
        }
        server.sendHeader("Content-Disposition", "attachment; filename=\"usb_diagnostics.json\"");
        server.send(200, "application/json", usb_ups->dumpUSBDiagnostics());
    });

    // OTA endpoints
    server.on("/update", HTTP_GET, [this]() { handleOTAPage(); });
    server.on("/update", HTTP_POST, 
        [this]() { 
            server.sendHeader("Connection", "close");
            server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
            if (!Update.hasError()) {
                should_restart = true;
                restart_request_time = millis();
            }
        },
        [this]() { handleOTAUpload(); }
    );

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
    AppLogger::log("INFO", "[WEB] Server started on port 80");
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

void WebConfigServer::setUPS(USBHostUPS* ups) {
    usb_ups = ups;
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

void WebConfigServer::handleUpsVars() {
    if (!usb_ups) {
        server.send(503, "application/json", "{\"error\": \"UPS non inizializzato\"}");
        return;
    }
    
    const UPSData& data = usb_ups->getUPSData();
    JsonDocument doc;
    
    doc["ups.status"] = usb_ups->getUPSStatusString();
    doc["ups.mfr"] = data.manufacturer.length() > 0 ? data.manufacturer : "Unknown";
    doc["ups.model"] = data.product.length() > 0 ? data.product : "Unknown";
    doc["ups.serial"] = data.serialNumber.length() > 0 ? data.serialNumber : "Unknown";
    doc["battery.charge"] = data.remainingCapacity;
    if (data.remainingCapacityLimit > 0) doc["battery.charge.low"] = data.remainingCapacityLimit;
    if (data.designCapacity > 0) doc["battery.capacity"] = data.designCapacity;
    if (data.fullChargeCapacity > 0) doc["battery.capacity.full"] = data.fullChargeCapacity;
    if (data.runTimeToEmpty > 0) doc["battery.runtime"] = data.runTimeToEmpty;
    doc["output.voltage"] = data.outputVoltage;
    doc["input.voltage"] = data.inputVoltage;
    doc["battery.voltage"] = data.batteryVoltage;
    if (data.highVoltageTransfer > 0) doc["input.transfer.high"] = data.highVoltageTransfer;
    if (data.lowVoltageTransfer > 0) doc["input.transfer.low"] = data.lowVoltageTransfer;
    if (data.configApparentPower > 0) doc["ups.power.nominal"] = data.configApparentPower;
    if (data.configActivePower > 0) doc["ups.realpower.nominal"] = data.configActivePower;
    if (data.configFrequency > 0) doc["input.frequency.nominal"] = data.configFrequency;
    if (data.configVoltage > 0) doc["input.voltage.nominal"] = data.configVoltage;
    if (data.outputVoltageNominal > 0) doc["output.voltage.nominal"] = data.outputVoltageNominal;
    if (data.outputFrequencyNominal > 0) doc["output.frequency.nominal"] = data.outputFrequencyNominal;
    doc["ups.load"] = data.load;
    if (data.configApparentPower > 0 || data.configActivePower > 0) doc["ups.realpower"] = data.realPower;
    if (data.batteryTemperature > 0.0f) doc["battery.temperature"] = serialized(String(data.batteryTemperature, 1));
    if (data.delayShutdown >= 0) doc["ups.delay.shutdown"] = data.delayShutdown;
    if (data.delayStart >= 0) doc["ups.delay.start"] = data.delayStart;
    if (data.timerStart >= 0) doc["ups.timer.start"] = data.timerStart;
    if (data.timerShutdown >= 0) doc["ups.timer.shutdown"] = data.timerShutdown;
    if (data.batteryType.length() > 0) doc["battery.type"] = data.batteryType;
    if (data.upsType.length() > 0) doc["ups.type"] = data.upsType;
    doc["ups.beeper.status"] = data.beeperEnabled ? "enabled" : "disabled";
    doc["outlet.1.switch"] = data.outlet1Switch ? 1 : 0;
    doc["outlet.2.switch"] = data.outlet2Switch ? 1 : 0;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void WebConfigServer::handleSystemStatus() {
    JsonDocument doc;
    
    doc["version"] = FIRMWARE_VERSION;
    
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
        const UPSData& data = usb_ups->getUPSData();
        String model = data.product.length() > 0 ? data.product : "Unknown Model";
        ups_status_str = model;
    } else {
        ups_status_str = "Disconnected";
    }
    doc["ups"]["status"] = ups_status_str;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void WebConfigServer::handleBeeper() {
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

void WebConfigServer::handleOTAPage() {
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", (const char*)web_asset_update_html, web_asset_update_html_len);
}

void WebConfigServer::handleOTAUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        AppLogger::log("INFO", String("[OTA] Upload started: ") + upload.filename);
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace, U_FLASH)) { //start with max available size
            Update.printError(Serial);
        }
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
            AppLogger::log("INFO", String("[OTA] Success: ") + String(upload.totalSize) + " bytes");
        } else {
            Update.printError(Serial);
            AppLogger::log("ERROR", "[OTA] Error at the end");
        }
    }
}
