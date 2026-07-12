#include "network_manager.h"

AppNetworkManager::AppNetworkManager() 
    : m_lastStatus(WL_IDLE_STATUS), 
      m_lastConnectionAttempt(0), 
      m_firstConnectionAttempt(0),
      m_isStarted(false),
      m_apFallbackActive(false),
      m_fallbackCallback(nullptr) {}

void AppNetworkManager::onFallback(std::function<void()> callback) {
    m_fallbackCallback = callback;
}

void AppNetworkManager::beginAP(const String& ap_ssid, const String& ap_password) {
    m_isStarted = true;
    m_apFallbackActive = true;
    Serial.println("[NETWORK] Avvio modalità Access Point...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());
    Serial.printf("[NETWORK] AP Avviato. IP: %s\n", WiFi.softAPIP().toString().c_str());
}

void AppNetworkManager::begin(const String& ssid, const String& password) {
    m_ssid = ssid;
    m_password = password;
    m_isStarted = true;
    m_apFallbackActive = false;
    m_lastStatus = WL_IDLE_STATUS;
    
    Serial.println("[NETWORK] Inizializzazione Wi-Fi in corso...");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true); // Consente all'ESP32 di gestire le riconnessioni a basso livello
    
    Serial.printf("[NETWORK] Connessione a SSID: %s...\n", m_ssid.c_str());
    WiFi.begin(m_ssid.c_str(), m_password.c_str());
    m_lastConnectionAttempt = millis();
    m_firstConnectionAttempt = m_lastConnectionAttempt;
}

void AppNetworkManager::loop() {
    if (!m_isStarted) {
        return;
    }
    
    wl_status_t currentStatus = WiFi.status();
    uint32_t now = millis();
    
    // Rileva cambiamenti di stato
    if (currentStatus != m_lastStatus) {
        if (currentStatus == WL_CONNECTED) {
            Serial.printf("[NETWORK] Connessione stabilita! Indirizzo IP: %s\n", WiFi.localIP().toString().c_str());
        } else if (currentStatus == WL_DISCONNECTED && m_lastStatus == WL_CONNECTED) {
            Serial.println("[NETWORK] Connessione Wi-Fi persa!");
        } else if (currentStatus == WL_NO_SSID_AVAIL) {
            Serial.println("[NETWORK] Rete SSID non trovata.");
        } else if (currentStatus == WL_CONNECT_FAILED) {
            Serial.println("[NETWORK] Connessione fallita.");
        } else if (currentStatus == WL_CONNECTION_LOST) {
            Serial.println("[NETWORK] Connessione persa (WL_CONNECTION_LOST).");
        }
        m_lastStatus = currentStatus;
    }
    
    // Gestione riconnessione e fallback
    if (currentStatus != WL_CONNECTED && !m_apFallbackActive) {
        if (now - m_firstConnectionAttempt >= 30000) {
            Serial.println("[NETWORK] Fallback Access Point attivato dopo 30s di fallimento.");
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP("NUT_ESP32_Config", "12345678");
            m_apFallbackActive = true;
            if (m_fallbackCallback) m_fallbackCallback();
        } else if (now - m_lastConnectionAttempt >= 15000) {
            m_lastConnectionAttempt = now;
            Serial.printf("[NETWORK] Riconnessione in corso... Tentativo su SSID: %s\n", m_ssid.c_str());
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
