#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>

class AppNetworkManager {
public:
    AppNetworkManager();
    
    // Avvia la connessione Wi-Fi
    void begin(const String& ssid, const String& password);
    
    // Gestisce il monitoraggio e la riconnessione
    void loop();
    
    // Ritorna true se la connessione è attiva
    bool isConnected() const;

private:
    String m_ssid;
    String m_password;
    wl_status_t m_lastStatus;
    uint32_t m_lastConnectionAttempt;
    bool m_isStarted;
};

#endif // NETWORK_MANAGER_H
