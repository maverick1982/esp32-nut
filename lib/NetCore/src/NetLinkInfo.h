#ifndef NET_LINK_INFO_H
#define NET_LINK_INFO_H

#include <Arduino.h>

// State of a network link as the Network page shows it (US-112)
enum class NetLinkState : uint8_t { CONNECTED, CONNECTING, DISCONNECTED, ACCESS_POINT };

/**
 * @brief Read-only status of the active link for /api/system-status (US-112).
 *
 * Filled by the link adapters (the only code with WiFi / ETH calls, ADR 0010) and turned
 * into JSON by WebApiJson::networkStatusJson(). Empty strings and zero values mean "the
 * link does not have it now": the JSON leaves those fields out.
 */
struct NetLinkInfo {
    const char* type = "none";        // "wifi", "ethernet", "none" without a link
    NetLinkState state = NetLinkState::DISCONNECTED;
    String ip;
    String gateway;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    bool dhcp = false;                 // address from DHCP (station and Ethernet)
    String ssid;                       // Wi-Fi: network joined or being joined; AP: its own network
    int32_t rssi = 0;                  // dBm, valid with has_rssi
    bool has_rssi = false;
    uint16_t link_speed_mbps = 0;      // Ethernet, 0 = unknown
    bool full_duplex = false;
};

#endif // NET_LINK_INFO_H
