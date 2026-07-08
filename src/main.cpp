#include "main.h"
#include "USBHostUPS.h"

USBHostUPS usb_ups;
ConfigManager config_mgr;

#ifndef UNIT_TEST
void setup() {
    // Inizializzazione della porta seriale per il debug diagnostico
    Serial.begin(MONITOR_BAUD_RATE);
    delay(1000); // Piccolo delay per stabilizzare la connessione seriale
    Serial.println("\n--- ESP32 NUT Server Initialized ---");

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
    }

    // Inizializzazione della libreria USBHostUPS (solo se la configurazione è valida)
    if (config_mgr.isValid()) {
        if (!usb_ups.begin()) {
            Serial.println("[MAIN] ERRORE: Inizializzazione USBHostUPS fallita!");
        } else {
            Serial.println("[MAIN] USBHostUPS inizializzato correttamente.");
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

    usb_ups.loop();

    uint32_t now = millis();
    static uint32_t last_print = 0;
    if (now - last_print >= 5000) {
        last_print = now;
        Serial.printf("[DIAG] UPS Info: Batteria = %d%% | Stato = %s | Tensione = %.1f V\n",
                      usb_ups.getBatteryCharge(),
                      usb_ups.getUPSStatus().c_str(),
                      usb_ups.getInputVoltage());
    }

    delay(10);
}
#endif


