#ifndef ETH_LINK_H
#define ETH_LINK_H

#include <Arduino.h>
#include "INetLink.h"
#include "board/board.h"

// Ethernet adapter of the net module (ADR 0010, US-092): the only code that calls ETH.*
// Pins and PHY come from the board profile. Address by DHCP; the ESP-IDF driver and the
// DHCP client reconnect by themselves, the link only reports and logs the changes.
class EthLink : public INetLink {
public:
    explicit EthLink(const EthConfig& cfg);

    NetLinkKind kind() const override { return NetLinkKind::ETHERNET; }
    void begin() override;
    void end() override;
    void loop(uint32_t now) override;
    // Link up with an IPv4 address
    bool isConnected() const override;
    // "192.168.1.50 (100 Mbps)", "Connecting" (link up, no address yet) or "Disconnected"
    String statusText() const override;
    String localIP() const override;
    void macAddress(uint8_t mac[6]) const override;

private:
    EthConfig m_cfg;
    bool m_started;
    bool m_lastConnected;
    bool m_lastLinkUp;
};

#endif // ETH_LINK_H
