#include "main.h"
#include "USBHostUPS.h"

USBHostUPS usb_ups;
ConfigManager config_mgr;
AppNetworkManager network_mgr;
NUTServer nut_server;
DiagnosticLED diagnostic_led;

// Calcola lo stato diagnostico del sistema a partire dallo stato Wi-Fi e UPS
LedState computeSystemState(bool wifiConnected, bool upsConnected) {
    if (!wifiConnected) {
        return LedState::CONNECTING;
    }
    if (!upsConnected) {
        return LedState::ERROR;
    }
    return LedState::OPERATIONAL;
}

#ifndef UNIT_TEST
void setup() {
    // Inizializzazione della porta seriale per il debug diagnostico
    Serial.begin(MONITOR_BAUD_RATE);
    delay(1000); // Piccolo delay per stabilizzare la connessione seriale
    Serial.println("\n--- ESP32 NUT Server Initialized ---");

    // Inizializzazione del LED diagnostico
    diagnostic_led.begin(LED_BUILTIN_PIN);

    // Inizializzazione di ConfigManager
    if (!config_mgr.begin()) {
        Serial.println("[MAIN] ERRORE: Caricamento configurazione fallito! Avvio in modalità sicura...");
    } else {
        Serial.println("[MAIN] Configurazione caricata con successo.");
        // Stampa parametri per verifica
        WifiConfig wifi = config_mgr.getWifiConfig();
        NutConfig nut = config_mgr.getNutConfig();
        Serial.printf("[MAIN] Wi-Fi SSID: %s\n", wifi.ssid.c_str());
        Serial.printf("[MAIN] NUT UPS Name: %s\n", nut.ups_name.c_str());
        
        // Inizializzazione di AppNetworkManager
        network_mgr.begin(wifi.ssid, wifi.password);
    }

    // Inizializzazione della libreria USBHostUPS e NUTServer (solo se la configurazione è valida)
    if (config_mgr.isValid()) {
        if (!usb_ups.begin()) {
            Serial.println("[MAIN] ERRORE: Inizializzazione USBHostUPS fallita!");
        } else {
            Serial.println("[MAIN] USBHostUPS inizializzato correttamente.");
        }

        // Inizializzazione NUTServer
        NutConfig nut_config = config_mgr.getNutConfig();
        NUTServerConfig nut_server_config = {nut_config.username, nut_config.password, nut_config.ups_name};
        if (!nut_server.begin(nut_server_config, &usb_ups)) {
            Serial.println("[MAIN] ERRORE: Inizializzazione NUTServer fallita!");
        } else {
            Serial.println("[MAIN] NUTServer avviato correttamente sulla porta 3493.");
        }
    }
}

void loop() {
    // Se la configurazione non è valida, rimaniamo in modalità di attesa sicura
    if (!config_mgr.isValid()) {
        static uint32_t last_safe_print = 0;
        uint32_t now = millis();
        if (now - last_safe_print >= 5000) {
            last_safe_print = now;
            Serial.println("[MAIN] ATTENZIONE: Sistema in attesa sicura. Configurazione mancante o non valida!");
        }
        delay(100);
        return;
    }

    network_mgr.loop();
    usb_ups.loop();
    nut_server.loop();

    uint32_t now = millis();
    static uint32_t last_print = 0;
    if (now - last_print >= 5000) {
        last_print = now;
        Serial.printf("[DIAG] UPS Info: Batteria = %d%% | Stato = %s | Tensione = %.1f V\n",
                      usb_ups.getUPSData().remainingCapacity,
                      usb_ups.getUPSStatusString().c_str(),
                      (float)usb_ups.getUPSData().outputVoltage);
    }

    // Aggiornamento stato LED diagnostico
    diagnostic_led.setState(computeSystemState(network_mgr.isConnected(), usb_ups.isConnected()));
    diagnostic_led.update();

    delay(10);
}
#endif


