#include "config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

ConfigManager::ConfigManager() : is_valid(false) {}

bool ConfigManager::begin(const char* filepath) {
    is_valid = false;
    
    // Inizializzazione LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("[CONFIG] ERRORE: Impossibile montare LittleFS!");
        return false;
    }
    
    // Apertura del file di configurazione
    File file = LittleFS.open(filepath, "r");
    if (!file) {
        Serial.printf("[CONFIG] ERRORE: Impossibile aprire il file %s!\n", filepath);
        return false;
    }
    
    // Parsing del file JSON usando la libreria ArduinoJson (v7)
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.printf("[CONFIG] ERRORE: Parsing JSON fallito per il file %s. Dettagli: %s\n", filepath, error.c_str());
        return false;
    }
    
    // Estrazione dei campi Wi-Fi
    if (doc["wifi"].is<JsonObject>()) {
        JsonObject wifi = doc["wifi"].as<JsonObject>();
        wifi_config.ssid = wifi["ssid"].as<String>();
        wifi_config.password = wifi["password"].as<String>();
    } else {
        Serial.println("[CONFIG] ERRORE: Sezione 'wifi' mancante o non valida nel file JSON.");
        return false;
    }
    
    // Estrazione dei campi NUT
    if (doc["nut"].is<JsonObject>()) {
        JsonObject nut = doc["nut"].as<JsonObject>();
        nut_config.username = nut["username"].as<String>();
        nut_config.password = nut["password"].as<String>();
        nut_config.ups_name = nut["ups_name"].as<String>();
    } else {
        Serial.println("[CONFIG] ERRORE: Sezione 'nut' mancante o non valida nel file JSON.");
        return false;
    }
    
    // Se siamo arrivati qui, la configurazione è valida
    is_valid = true;
    Serial.println("[CONFIG] Configurazione caricata correttamente da LittleFS.");
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
