#include "network/wifi_links.h"
#include "core/app_logger.h"

WifiStaLink::WifiStaLink(const String& ssid, const String& password)
    : m_ssid(ssid),
      m_password(password),
      m_lastStatus(WL_IDLE_STATUS),
      m_lastConnectionAttempt(0),
      m_lastDisconnectTime(0) {}

void WifiStaLink::begin() {
    m_lastStatus = WL_IDLE_STATUS;

    AppLogger::log("INFO", "[NETWORK] Initializing Wi-Fi...");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true); // Consente all'ESP32 di gestire le riconnessioni a basso livello

    AppLogger::log("INFO", "[NETWORK] Connecting to SSID: %s...\n", m_ssid.c_str());
    WiFi.begin(m_ssid.c_str(), m_password.c_str());
    m_lastConnectionAttempt = millis();
    m_lastDisconnectTime = m_lastConnectionAttempt;
}

void WifiStaLink::end() {
    WiFi.disconnect();
}

void WifiStaLink::loop(uint32_t now) {
    wl_status_t currentStatus = WiFi.status();

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

    if (currentStatus == WL_CONNECTED) {
        m_lastConnectionAttempt = now; // Reset attempt timer while connected
    }

    // Gestione riconnessione e fallback
    if (currentStatus != WL_CONNECTED) {
        if (now - m_lastConnectionAttempt >= 15000) {
            m_lastConnectionAttempt = now;
            AppLogger::log("INFO", "[NETWORK] Reconnecting... Attempt on SSID: %s\n", m_ssid.c_str());
            // Forza una nuova connessione
            WiFi.disconnect();
            WiFi.begin(m_ssid.c_str(), m_password.c_str());
        }
    }
}

bool WifiStaLink::isConnected() const {
    return (WiFi.status() == WL_CONNECTED);
}

String WifiStaLink::statusText() const {
    return isConnected() ? WiFi.SSID() : String("Connecting");
}

String WifiStaLink::localIP() const {
    return WiFi.localIP().toString();
}

void WifiStaLink::macAddress(uint8_t mac[6]) const {
    WiFi.macAddress(mac);
}

WifiApLink::WifiApLink(const String& ssid, const String& password)
    : m_ssid(ssid), m_password(password) {}

void WifiApLink::begin() {
    AppLogger::log("INFO", "[NETWORK] Starting Access Point mode...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(m_ssid.c_str(), m_password.c_str());
    AppLogger::log("INFO", "[NETWORK] AP Started. IP: %s\n", WiFi.softAPIP().toString().c_str());
}

void WifiApLink::end() {
    WiFi.softAPdisconnect(true);
}

String WifiApLink::localIP() const {
    return WiFi.softAPIP().toString();
}

void WifiApLink::macAddress(uint8_t mac[6]) const {
    // Same MAC as before US-090: the device ID was always built from the station MAC
    WiFi.macAddress(mac);
}

uint32_t WifiApLink::stationCount() const {
    return WiFi.softAPgetStationNum();
}
