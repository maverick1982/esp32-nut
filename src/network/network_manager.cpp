#include "network/network_manager.h"
#include "core/app_logger.h"

AppNetworkManager::AppNetworkManager() 
    : m_lastStatus(WL_IDLE_STATUS), 
      m_lastConnectionAttempt(0), 
      m_lastDisconnectTime(0),
      m_isStarted(false),
      m_apFallbackActive(false),
      m_fallbackCallback(nullptr) {}

void AppNetworkManager::onFallback(std::function<void()> callback) {
    m_fallbackCallback = callback;
}

void AppNetworkManager::beginAP(const String& ap_ssid, const String& ap_password) {
    m_isStarted = true;
    m_apFallbackActive = true;
    AppLogger::log("INFO", "[NETWORK] Starting Access Point mode...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());
    AppLogger::log("INFO", "[NETWORK] AP Started. IP: %s\n", WiFi.softAPIP().toString().c_str());
}

void AppNetworkManager::begin(const String& ssid, const String& password) {
    m_ssid = ssid;
    m_password = password;
    m_isStarted = true;
    m_apFallbackActive = false;
    m_lastStatus = WL_IDLE_STATUS;
    
    AppLogger::log("INFO", "[NETWORK] Initializing Wi-Fi...");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true); // Consente all'ESP32 di gestire le riconnessioni a basso livello
    
    AppLogger::log("INFO", "[NETWORK] Connecting to SSID: %s...\n", m_ssid.c_str());
    WiFi.begin(m_ssid.c_str(), m_password.c_str());
    m_lastConnectionAttempt = millis();
    m_lastDisconnectTime = m_lastConnectionAttempt;
}

void AppNetworkManager::loop() {
    if (!m_isStarted) {
        return;
    }
    
    wl_status_t currentStatus = WiFi.status();
    uint32_t now = millis();
    
    // Rileva cambiamenti di stato
    if (currentStatus != m_lastStatus) {
        if (m_lastStatus == WL_CONNECTED && currentStatus != WL_CONNECTED) {
            m_lastDisconnectTime = now;
        }
        
        if (currentStatus == WL_CONNECTED) {
            AppLogger::log("INFO", "[NETWORK] Connection established! IP Address: %s\n", WiFi.localIP().toString().c_str());
        } else if (currentStatus == WL_DISCONNECTED && m_lastStatus == WL_CONNECTED) {
            AppLogger::log("WARN", "[NETWORK] Wi-Fi connection lost!");
        } else if (currentStatus == WL_NO_SSID_AVAIL) {
            AppLogger::log("WARN", "[NETWORK] SSID network not found.");
        } else if (currentStatus == WL_CONNECT_FAILED) {
            AppLogger::log("ERROR", "[NETWORK] Connection failed.");
        } else if (currentStatus == WL_CONNECTION_LOST) {
            AppLogger::log("WARN", "[NETWORK] Connection lost (WL_CONNECTION_LOST).");
        }
        m_lastStatus = currentStatus;
    }
    
    // Gestione riconnessione e fallback
    if (currentStatus != WL_CONNECTED && !m_apFallbackActive) {
        if (now - m_lastDisconnectTime >= 30000) {
            AppLogger::log("WARN", "[NETWORK] Access Point fallback activated after 30s of failure.");
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP("NUT_ESP32_Config", "12345678");
            m_apFallbackActive = true;
            if (m_fallbackCallback) m_fallbackCallback();
        } else if (now - m_lastConnectionAttempt >= 15000) {
            m_lastConnectionAttempt = now;
            AppLogger::log("INFO", "[NETWORK] Reconnecting... Attempt on SSID: %s\n", m_ssid.c_str());
            // Forza una nuova connessione
            WiFi.disconnect();
            WiFi.begin(m_ssid.c_str(), m_password.c_str());
        }
    }
}

bool AppNetworkManager::isConnected() const {
    return (WiFi.status() == WL_CONNECTED);
}

bool AppNetworkManager::isAPModeActive() const {
    return m_apFallbackActive;
}
