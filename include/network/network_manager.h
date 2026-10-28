#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include "board/board.h"
#include "NetBootPlan.h"
#include "NetLinkSelector.h"

class WifiStaLink;
class WifiApLink;
class EthLink;

// Network service of the net module (ADR 0010): creates the link chosen by the boot plan
// from the board profile and keeps it as the only active link (US-090)
class AppNetworkManager {
public:
    AppNetworkManager();
    ~AppNetworkManager();

    // Starts the link of the plan; ssid/password are used by the station only
    void begin(const BoardConfig& board, const NetBootPlan& plan, const String& ssid, const String& password);

    // Gestisce il monitoraggio e la riconnessione
    void loop();

    // Ritorna true se la connessione è attiva
    bool isConnected() const;
    bool isApMode() const;
    // Status bar text of the active link, "Disconnected" without one
    String statusText() const;
    // "wifi", "ethernet", or "none" without a link
    const char* typeName() const;
    String localIP() const;
    void macAddress(uint8_t mac[6]) const;
    // Stations connected to the configuration access point, 0 outside AP mode
    uint32_t apStationCount() const;

private:
    NetLinkSelector m_selector;
    WifiStaLink* m_sta;
    WifiApLink* m_ap;
    EthLink* m_eth;
};

#endif // NETWORK_MANAGER_H
