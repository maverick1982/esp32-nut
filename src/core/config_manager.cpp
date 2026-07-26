#include "core/config_manager.h"
#include <ArduinoJson.h>
#include "core/app_logger.h"

ConfigManager::ConfigManager() : is_valid(false) {}

bool ConfigManager::begin() {
    is_valid = false;
    
    preferences.begin("nutos", false);
    String config_json = preferences.getString("config_json", "");
    
    if (config_json == "") {
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, config_json);
    if (error) {
        AppLogger::log("ERROR", "[CONFIG] ERROR: JSON parsing from NVS failed. Details: %s\n", error.c_str());
        return false;
    }
    
    // Estrazione dei campi Wi-Fi
    if (doc["wifi"].is<JsonObject>()) {
        JsonObject wifi = doc["wifi"].as<JsonObject>();
        wifi_config.ssid = wifi["ssid"].as<String>();
        wifi_config.password = wifi["password"].as<String>();
    } else {
        AppLogger::log("ERROR", "[CONFIG] ERROR: 'wifi' section missing or invalid in JSON.");
        return false;
    }
    
    // Estrazione dei campi NUT
    if (doc["nut"].is<JsonObject>()) {
        JsonObject nut = doc["nut"].as<JsonObject>();
        nut_config.username = nut["username"].as<String>();
        nut_config.password = nut["password"].as<String>();
        nut_config.ups_name = nut["ups_name"].as<String>();
    } else {
        AppLogger::log("ERROR", "[CONFIG] ERROR: 'nut' section missing or invalid in JSON.");
        return false;
    }
    
    // Se siamo arrivati qui, la configurazione è valida
    is_valid = true;
    
    AppLogger::log("INFO", "[CONFIG] Configuration successfully loaded from NVS.");
    
    return true;
}

WifiConfig ConfigManager::getWifiConfig() const {
    return wifi_config;
}

NutConfig ConfigManager::getNutConfig() const {
    return nut_config;
}

bool ConfigManager::isValid() const {
    return is_valid;
}

void ConfigManager::setWifiConfig(const WifiConfig& config) {
    wifi_config = config;
}

void ConfigManager::setNutConfig(const NutConfig& config) {
    nut_config = config;
}

bool ConfigManager::save() {
    JsonDocument doc;
    
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"] = wifi_config.ssid;
    wifi["password"] = wifi_config.password;
    
    JsonObject nut = doc["nut"].to<JsonObject>();
    nut["username"] = nut_config.username;
    nut["password"] = nut_config.password;
    nut["ups_name"] = nut_config.ups_name;
    
    String jsonString;
    if (serializeJson(doc, jsonString) == 0) {
        AppLogger::log("ERROR", "[CONFIG] ERROR: Cannot serialize JSON.");
        return false;
    }
    
    preferences.begin("nutos", false);
    size_t written = preferences.putString("config_json", jsonString);
    if (written == 0) {
        AppLogger::log("ERROR", "[CONFIG] ERROR: Cannot write configuration to NVS.");
        return false;
    }
    
    AppLogger::log("INFO", "[CONFIG] Configuration successfully saved to NVS.");
    return true;
}
