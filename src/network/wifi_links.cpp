#include "network/wifi_links.h"
#include "core/app_logger.h"
#include "NetHostname.h"
#include <esp_mac.h>

WifiStaLink::WifiStaLink(const String& ssid, const String& password, const String& saved_hostname)
    : m_ssid(ssid),
      m_password(password),
      m_savedHostname(saved_hostname),
      m_lastStatus(WL_IDLE_STATUS),
      m_lastConnectionAttempt(0),
      m_lastDisconnectTime(0) {}

void WifiStaLink::begin() {
    m_lastStatus = WL_IDLE_STATUS;

    AppLogger::log("INFO", "[NETWORK] Initializing Wi-Fi...");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true); // Consente all'ESP32 di gestire le riconnessioni a basso livello

    // US-113: on the STA netif, after WiFi.mode() (which writes the core default name) and
    // before WiFi.begin(), so the first DHCP request carries it. WiFi.STA.setHostname()
    // takes up to 32 characters, WiFi.setHostname() only 31 (NetHostname.h)
    // MAC from eFuse: right after WiFi.mode() the STA netif has not started yet and
    // WiFi.macAddress() gives 00:00:00:00:00:00 (seen on the S3, core 3.3.12). Same value
    // as WiFi.macAddress() once the station runs, so name and device ID match
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) WiFi.macAddress(mac);
    m_hostname = NetHostname::effective(m_savedHostname, mac);
    if (WiFi.STA.setHostname(m_hostname.c_str())) {
        AppLogger::log("INFO", "[NETWORK] Wi-Fi hostname %s", m_hostname.c_str());
    } else {
        AppLogger::log("WARN", "[NETWORK] Could not set the Wi-Fi hostname %s", m_hostname.c_str());
    }

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

void WifiStaLink::linkInfo(NetLinkInfo& out) const {
    out = NetLinkInfo();
    out.type = typeName();
    macAddress(out.mac);
    if (!isConnected()) {
        out.state = NetLinkState::CONNECTING;
        out.ssid = m_ssid; // the network it is joining
        return;
    }
    out.state = NetLinkState::CONNECTED;
    out.ip = WiFi.localIP().toString();
    out.gateway = WiFi.gatewayIP().toString();
    out.dhcp = true; // no static address on the station
    out.ssid = WiFi.SSID();
    out.rssi = WiFi.RSSI();
    out.has_rssi = true;
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

void WifiApLink::linkInfo(NetLinkInfo& out) const {
    out = NetLinkInfo();
    out.type = typeName();
    out.state = NetLinkState::ACCESS_POINT;
    out.ssid = m_ssid; // the board's own network
    out.ip = localIP();
    macAddress(out.mac);
}

uint32_t WifiApLink::stationCount() const {
    return WiFi.softAPgetStationNum();
}
