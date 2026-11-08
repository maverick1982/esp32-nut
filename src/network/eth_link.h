#ifndef ETH_LINK_H
#define ETH_LINK_H

#include <Arduino.h>
#include "INetLink.h"
#include "LinkWaitReminder.h"
#include "board/board.h"

// Ethernet adapter of the net module (ADR 0010, US-092): the only code that calls ETH.*
// RMII port with pins and PHY from the board profile, or a W5500 on SPI with the pins set
// by the user in the Network page (US-126). Address by DHCP; the ESP-IDF driver and the
// DHCP client reconnect by themselves, the link only reports and logs the changes.
class EthLink : public INetLink {
public:
    // saved_hostname: the name chosen in the Network page, "" for the default (US-113)
    EthLink(const EthConfig& cfg, const String& saved_hostname);

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
    void linkInfo(NetLinkInfo& out) const override;
    String hostname() const override { return m_hostname; }
    // ETH.begin() succeeded: false when the PHY or the W5500 did not answer
    bool started() const { return m_started; }
    // Link up, with or without an address (startup probe, US-126)
    bool linkUp() const;
    // A real IPv4 address: ETH.hasIP() alone stays true after setDhcp(false), which posts
    // no IP-lost event
    bool hasIP() const;
    // Issue #64: the MAC the W5500 uses instead of the one derived from ESP_MAC_ETH. Set
    // before begin(); only for an SPI module, which has no MAC of its own
    void setMac(const uint8_t mac[6]);
    // Issue #64: DHCP client of the running link. Off on a W5500 kept on standby, so it
    // never holds an address (with the station MAC) while the station is on; on again
    // when the board comes back to Ethernet. Off clears the address at once
    void setDhcp(bool on);

private:
    void beginW5500(const char* hostname);
    void releaseW5500();
    void applyMac();

    EthConfig m_cfg;
    String m_savedHostname;
    String m_hostname;
    bool m_started;
    bool m_lastConnected;
    bool m_lastLinkUp;
    // "No Ethernet link" repeated while the cable is missing (issue #136)
    LinkWaitReminder m_linkWait;
    bool m_hasMac;
    uint8_t m_mac[6];
};

#endif // ETH_LINK_H
