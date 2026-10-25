#ifndef I_NET_LINK_H
#define I_NET_LINK_H

#include <Arduino.h>

/**
 * @brief One network link of the board: Wi-Fi station, Wi-Fi access point, Ethernet
 * (ADR 0010 §2-§3).
 *
 * The rest of the firmware sees the active link through this interface and never calls
 * WiFi or ETH directly. The adapters are the only place with platform calls.
 */
enum class NetLinkKind : uint8_t { STATION, ACCESS_POINT, ETHERNET };

class INetLink {
public:
    virtual ~INetLink() {}

    virtual NetLinkKind kind() const = 0;
    // Network type for the status APIs: "wifi" for station and access point, "ethernet"
    const char* typeName() const { return kind() == NetLinkKind::ETHERNET ? "ethernet" : "wifi"; }

    virtual void begin() = 0;
    virtual void end() = 0;
    virtual void loop(uint32_t now) = 0;
    virtual bool isConnected() const = 0;
    // Text of the web UI status bar
    virtual String statusText() const = 0;
    virtual String localIP() const = 0;
    virtual void macAddress(uint8_t mac[6]) const = 0;
};

#endif // I_NET_LINK_H
