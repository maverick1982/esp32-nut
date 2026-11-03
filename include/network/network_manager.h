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

    // Starts the link of the plan; ssid/password are used by the station only.
    // saved_hostname: the name chosen in the Network page, "" for the default (US-113)
    void begin(const BoardConfig& board, const NetBootPlan& plan, const String& ssid, const String& password,
               const String& saved_hostname);

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
    // Status of the active link for the Network page; type "none" without a link (US-112)
    void linkInfo(NetLinkInfo& out) const;
    // Name in use on the active link (US-113). The access point has none: then the name
    // the station will use (same MAC). "" without a link
    String hostname() const;
    // esp32-nut-xxxxxx from the MAC of the active link, "" without a link
    String defaultHostname() const;
    // Stations connected to the configuration access point, 0 outside AP mode
    uint32_t apStationCount() const;

private:
    NetLinkSelector m_selector;
    WifiStaLink* m_sta;
    WifiApLink* m_ap;
    EthLink* m_eth;
    String m_savedHostname;
};

#endif // NETWORK_MANAGER_H
