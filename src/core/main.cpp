#include "core/main.h"
#include "USBHostUPS.h"
#include "network/web_config_server.h"
#include "core/app_logger.h"

USBHostUPS usb_ups;
ConfigManager config_mgr;
AppNetworkManager network_mgr;
NUTServer nut_server;
DiagnosticLED diagnostic_led;
WebConfigServer web_server(config_mgr);

// Calcola lo stato diagnostico del sistema a partire dallo stato Wi-Fi e UPS
LedState computeSystemState(bool wifiConnected, bool upsConnected) {
    if (network_mgr.isAPModeActive()) {
        return LedState::AP_MODE; // AP mode = AP_MODE
    }
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
    AppLogger::log("INFO", "\n--- ESP32 NUT Server Initialized ---");

    // Inizializzazione del LED diagnostico
    diagnostic_led.begin(LED_BUILTIN_PIN);

    // Setup fallback AP trigger
    network_mgr.onFallback([]() {
        AppLogger::log("INFO", "[MAIN] Fallback detected, starting WebConfigServer in AP mode.");
        web_server.setUPS(&usb_ups);
        web_server.begin(true);
    });

    // Inizializzazione di ConfigManager
    if (!config_mgr.begin()) {
        AppLogger::log("ERROR", "[MAIN] ERROR: Configuration load failed! Starting in safe mode...");
        network_mgr.beginAP("NUT_ESP32_Config", "12345678");
        web_server.begin(true);
    } else {
        AppLogger::log("INFO", "[MAIN] Configuration loaded successfully.");
        // Stampa parametri per verifica
        WifiConfig wifi = config_mgr.getWifiConfig();
        NutConfig nut = config_mgr.getNutConfig();
        AppLogger::log("INFO", "[MAIN] Wi-Fi SSID: %s\n", wifi.ssid.c_str());
        AppLogger::log("INFO", "[MAIN] NUT UPS Name: %s\n", nut.ups_name.c_str());
        
        // Inizializzazione di AppNetworkManager
        network_mgr.begin(wifi.ssid, wifi.password);
        web_server.setUPS(&usb_ups);
        web_server.begin(false);
    }

    // Inizializzazione della libreria USBHostUPS e NUTServer (solo se la configurazione è valida)
    if (config_mgr.isValid()) {
        usb_ups.setLogCallback([](const char* level, const char* msg) {
            AppLogger::log(level, msg);
        });
        if (!usb_ups.begin()) {
            AppLogger::log("ERROR", "[MAIN] ERROR: USBHostUPS initialization failed!");
        } else {
            AppLogger::log("INFO", "[MAIN] USBHostUPS initialized correctly.");
        }

        // Inizializzazione NUTServer
        NutConfig nut_config = config_mgr.getNutConfig();
        NUTServerConfig nut_server_config = {nut_config.username, nut_config.password, nut_config.ups_name};
        if (!nut_server.begin(nut_server_config, &usb_ups)) {
            AppLogger::log("ERROR", "[MAIN] ERROR: NUTServer initialization failed!");
        } else {
            AppLogger::log("INFO", "[MAIN] NUTServer started correctly on port 3493.");
        }
    }
}

void loop() {
    web_server.loop();
    network_mgr.loop();

    // Se la configurazione non è valida, rimaniamo in modalità di attesa sicura
    if (!config_mgr.isValid()) {
        static uint32_t last_safe_print = 0;
        uint32_t now = millis();
        if (now - last_safe_print >= 5000) {
            last_safe_print = now;
            AppLogger::log("WARN", "[MAIN] WARNING: System in safe waiting mode. Configuration missing or invalid!");
        }
        
        diagnostic_led.setState(computeSystemState(network_mgr.isConnected(), false));
        diagnostic_led.update();
        delay(10);
        return;
    }

    usb_ups.loop();
    nut_server.loop();

    uint32_t now = millis();
    static uint32_t last_print = 0;
    if (now - last_print >= 5000) {
        last_print = now;
        AppLogger::log("INFO", "[DIAG] UPS Info: Battery = %d%% | Status = %s | Voltage = %.1f V\n",
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


