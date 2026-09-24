#include "core/main.h"
#include "USBHostUPS.h"
#include "network/web_config_server.h"
#include "core/app_logger.h"
#include "core/crash_diag.h"
#include <Preferences.h>
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "RestartPolicy.h"

USBHostUPS usb_ups;
ConfigManager config_mgr;
AppNetworkManager network_mgr;
NUTServer nut_server;
DiagnosticLED diagnostic_led;
WebConfigServer web_server(config_mgr);

Preferences boot_prefs;
bool is_ap_mode = false;
bool clear_ap_flag_pending = false;
uint32_t boot_time_ms = 0;

// Controlled restarts with growing delays, then degraded mode (review A7)
RestartPolicy restart_policy;

// Task watchdog on the loopTask (issue #47). It replaces a hw_timer whose ISR called
// esp_restart(): unsafe from an ISR, and unable to fire during a panic, when interrupts
// are masked. The TWDT panics instead, so a hang leaves a backtrace and a core dump.
static const uint32_t LOOP_WDT_TIMEOUT_MS = 30000;

static void setupLoopWatchdog() {
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = LOOP_WDT_TIMEOUT_MS,
        .idle_core_mask = (1 << 0), // keep the IDLE0 check enabled by the Arduino core
        .trigger_panic = true,
    };
    esp_err_t err = esp_task_wdt_reconfigure(&wdt_config);
    if (err == ESP_ERR_INVALID_STATE) {
        err = esp_task_wdt_init(&wdt_config);
    }
    if (err == ESP_OK) {
        err = esp_task_wdt_add(NULL);
    }
    if (err != ESP_OK) {
        AppLogger::log("ERROR", "[MAIN] Task watchdog setup failed: %s", esp_err_to_name(err));
    }
}

// Calcola lo stato diagnostico del sistema a partire dallo stato Wi-Fi e UPS
LedState computeSystemState(bool wifiConnected, bool upsConnected) {
    if (is_ap_mode) {
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
    // Prima di tutto: protegge il panic handler se manca la partizione coredump
    CrashDiag::begin();

    // Inizializzazione della porta seriale per il debug diagnostico
    Serial.begin(MONITOR_BAUD_RATE);
    delay(1000); // Piccolo delay per stabilizzare la connessione seriale
    AppLogger::log("INFO", "\n--- ESP32 NUT Server Initialized ---");
    CrashDiag::logBootInfo();
    restart_policy = RestartPolicy(CrashDiag::consecutiveRestarts());

    // Inizializzazione del LED diagnostico
    diagnostic_led.begin(LED_BUILTIN_PIN);

    // Task watchdog sul loopTask (30 secondi)
    setupLoopWatchdog();

    // Gestione NVS flag per AP manuale
    boot_prefs.begin("boot_state", false);
    bool manual_ap_triggered = boot_prefs.getBool("manual_ap", false);

    if (manual_ap_triggered) {
        boot_prefs.putBool("manual_ap", false);
        AppLogger::log("INFO", "[MAIN] Manual AP trigger detected!");
    } else {
        boot_prefs.putBool("manual_ap", true);
        clear_ap_flag_pending = true;
        boot_time_ms = millis();
    }

    // Inizializzazione di ConfigManager
    bool config_ok = config_mgr.begin() && config_mgr.isValid();
    
    if (!config_ok || manual_ap_triggered) {
        is_ap_mode = true;
        AppLogger::log("WARN", "[MAIN] Starting in AP mode...");
        network_mgr.beginAP("NUT_ESP32_Config", "12345678");
        web_server.setUPS(&usb_ups);
        web_server.begin(true);
    } else {
        is_ap_mode = false;
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

    // Inizializzazione della libreria USBHostUPS (sempre attiva)
    usb_ups.setLogCallback([](const char* level, const char* msg) {
        AppLogger::log(level, msg);
    });

    // Delay USB host initialization on cold boot to allow the UPS USB interface to stabilize
    // Many UPS devices (like APC) take a few seconds to properly handle USB requests after power on.
    uint32_t wait_until = 3000;
    while (millis() < wait_until) {
        delay(10);
    }

    if (!usb_ups.begin()) {
        AppLogger::log("ERROR", "[MAIN] ERROR: USBHostUPS initialization failed!");
    } else {
        AppLogger::log("INFO", "[MAIN] USBHostUPS initialized correctly.");
    }

    // Inizializzazione NUTServer (solo se la configurazione è valida)
    if (config_ok) {
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
    uint32_t now = millis();

    esp_task_wdt_reset();

    // Check timer per azzeramento flag manual_ap
    if (clear_ap_flag_pending && (now - boot_time_ms > 3000)) {
        boot_prefs.putBool("manual_ap", false);
        clear_ap_flag_pending = false;
        AppLogger::log("INFO", "[MAIN] Manual AP trigger window closed.");
    }

    web_server.loop();
    network_mgr.loop();
    // The UPS is served by its own task (USBHostUPS::begin, review A5b)

    // Recupero USB esaurito: riavvio controllato dal loopTask (mai da ISR), con attese
    // crescenti tra un riavvio e l'altro e modalità degradata oltre la soglia (review A7)
    static RestartPolicy::Decision last_decision = RestartPolicy::Decision::NONE;
    bool ups_healthy = usb_ups.isConnected() && !usb_ups.isDataStale();
    RestartPolicy::Decision decision = restart_policy.update(usb_ups.isRestartRequested(), ups_healthy, now);
    if (restart_policy.takeCleared()) {
        CrashDiag::clearConsecutiveRestarts();
        AppLogger::log("INFO", "[MAIN] UPS data healthy: consecutive restart counter cleared");
    }
    if (decision != last_decision) {
        if (decision == RestartPolicy::Decision::WAIT) {
            AppLogger::log("WARN", "[MAIN] Restart #%u in a row delayed by %u s: %s",
                           (unsigned)restart_policy.consecutive() + 1,
                           (unsigned)(restart_policy.msUntilRestart(now) / 1000), usb_ups.getRestartReason());
        } else if (decision == RestartPolicy::Decision::DEGRADED) {
            AppLogger::log("ERROR", "[MAIN] Degraded mode: %u restarts in a row, no more restarts (%s). "
                                    "Replug the UPS or reboot the board.",
                           (unsigned)restart_policy.consecutive(), usb_ups.getRestartReason());
        } else if (decision == RestartPolicy::Decision::NONE) {
            AppLogger::log("INFO", "[MAIN] Restart no longer needed");
        }
        CrashDiag::setDegraded(decision == RestartPolicy::Decision::DEGRADED);
        last_decision = decision;
    }
    if (decision == RestartPolicy::Decision::RESTART) {
        AppLogger::log("ERROR", "[MAIN] Controlled restart: %s", usb_ups.getRestartReason());
        CrashDiag::recordControlledRestart(usb_ups.getRestartReason());
        delay(200); // lascia uscire il log sulla seriale
        esp_restart();
    }

    // Se la configurazione non è valida, rimaniamo in modalità di attesa sicura
    if (!config_mgr.isValid()) {
        static uint32_t last_safe_print = 0;
        if (now - last_safe_print >= 5000) {
            last_safe_print = now;
            AppLogger::log("WARN", "[MAIN] WARNING: System in safe waiting mode. Configuration missing or invalid!");
        }
        
        diagnostic_led.setState(computeSystemState(network_mgr.isConnected(), usb_ups.isConnected()));
        diagnostic_led.update();
        delay(10);
        return;
    }

    nut_server.loop();

    static uint32_t last_print = 0;
    if (now - last_print >= 5000) {
        last_print = now;
        AppLogger::log("INFO", "[DIAG] UPS Info: Battery = %d%% | Status = %s | Voltage = %.1f V",
                      (int)usb_ups.getUPSData()->getFloat("battery.charge"),
                      usb_ups.getUPSStatusString().c_str(),
                      usb_ups.getUPSData()->getFloat("output.voltage"));
    }

    // Monitoraggio diagnostico stack (ogni 60 secondi)
    static uint32_t last_stack_log = 0;
    if (now - last_stack_log >= 60000) {
        last_stack_log = now;
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
        AppLogger::log("INFO", "[DIAG] Loop Task Stack High Water Mark: %u bytes", (uint32_t)hwm);
        AppLogger::log("INFO", "[DIAG] UPS Poll Task Stack High Water Mark: %u bytes",
                       (unsigned)usb_ups.getPollTaskStackHighWater());
        AppLogger::log("INFO", "[DIAG] Heap: free %u, min free %u, largest block %u bytes",
                       (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
                       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }

    // Aggiornamento stato LED diagnostico
    diagnostic_led.setState(computeSystemState(network_mgr.isConnected(), usb_ups.isConnected()));
    diagnostic_led.update();

    delay(10);
}
#endif

