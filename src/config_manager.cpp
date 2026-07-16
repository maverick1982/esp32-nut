#include "config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "app_logger.h"

ConfigManager::ConfigManager() : is_valid(false) {}

bool ConfigManager::begin(const char* filepath) {
    is_valid = false;
    
    // Inizializzazione LittleFS (ora incondizionata)
    if (!LittleFS.begin(true)) {
        AppLogger::log("ERROR", "[CONFIG] ERRORE: Impossibile montare LittleFS!");
        return false;
    }
    
    preferences.begin("nutos", false);
    String config_json = preferences.getString("config_json", "");
    
    JsonDocument doc;
    DeserializationError error;
    bool is_migration = false;
    
    if (config_json == "") {
        // Fallback to LittleFS (migrazione)
        if (LittleFS.exists(filepath)) {
            AppLogger::log("INFO", "[CONFIG] File legacy trovato, avvio migrazione...");
            File file = LittleFS.open(filepath, "r");
            if (!file) {
                AppLogger::log("ERROR", "[CONFIG] ERRORE: Impossibile aprire il file %s per la migrazione!", filepath);
                return false;
            }
            error = deserializeJson(doc, file);
            file.close();
            if (error) {
                AppLogger::log("ERROR", "[CONFIG] ERRORE: Parsing JSON fallito per il file legacy %s. Dettagli: %s\n", filepath, error.c_str());
                return false;
            }
            is_migration = true;
        } else {
            return false;
        }
    } else {
        error = deserializeJson(doc, config_json);
        if (error) {
            AppLogger::log("ERROR", "[CONFIG] ERRORE: Parsing JSON fallito da NVS. Dettagli: %s\n", error.c_str());
            return false;
        }
    }
    
    // Estrazione dei campi Wi-Fi
    if (doc["wifi"].is<JsonObject>()) {
        JsonObject wifi = doc["wifi"].as<JsonObject>();
        wifi_config.ssid = wifi["ssid"].as<String>();
        wifi_config.password = wifi["password"].as<String>();
    } else {
        AppLogger::log("ERROR", "[CONFIG] ERRORE: Sezione 'wifi' mancante o non valida nel file JSON.");
        return false;
    }
    
    // Estrazione dei campi NUT
    if (doc["nut"].is<JsonObject>()) {
        JsonObject nut = doc["nut"].as<JsonObject>();
        nut_config.username = nut["username"].as<String>();
        nut_config.password = nut["password"].as<String>();
        nut_config.ups_name = nut["ups_name"].as<String>();
    } else {
        AppLogger::log("ERROR", "[CONFIG] ERRORE: Sezione 'nut' mancante o non valida nel file JSON.");
        return false;
    }
    
    // Se siamo arrivati qui, la configurazione è valida
    is_valid = true;
    
    if (is_migration) {
        this->save(filepath);
        LittleFS.rename(filepath, String(filepath) + ".bak");
        AppLogger::log("INFO", "[CONFIG] Migrazione a NVS completata.");
    } else {
        AppLogger::log("INFO", "[CONFIG] Configurazione caricata correttamente da NVS.");
    }
    
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

bool ConfigManager::save(const char* filepath) {
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
        AppLogger::log("ERROR", "[CONFIG] ERRORE: Impossibile serializzare il JSON.");
        return false;
    }
    
    preferences.begin("nutos", false);
    size_t written = preferences.putString("config_json", jsonString);
    if (written == 0) {
        AppLogger::log("ERROR", "[CONFIG] ERRORE: Impossibile scrivere la configurazione in NVS.");
        return false;
    }
    
    AppLogger::log("INFO", "[CONFIG] Configurazione salvata correttamente in NVS.");
    return true;
}
