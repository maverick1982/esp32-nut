#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <functional>

class AppNetworkManager {
public:
    AppNetworkManager();
    
    // Avvia la connessione Wi-Fi
    void begin(const String& ssid, const String& password);
    void beginAP(const String& ap_ssid, const String& ap_password);
    
    // Gestisce il monitoraggio e la riconnessione
    void loop();
    
    // Ritorna true se la connessione è attiva
    bool isConnected() const;
    bool isAPModeActive() const;
    void onFallback(std::function<void()> callback);

private:
    String m_ssid;
    String m_password;
    wl_status_t m_lastStatus;
    uint32_t m_lastConnectionAttempt;
    uint32_t m_lastDisconnectTime;
    bool m_isStarted;
    bool m_apFallbackActive;
    std::function<void()> m_fallbackCallback;
};

#endif // NETWORK_MANAGER_H
