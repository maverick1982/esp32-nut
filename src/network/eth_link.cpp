#include "network/eth_link.h"
#include "core/app_logger.h"
#include <esp_mac.h>
#include <ETH.h>

// RMII needs the internal EMAC (ESP32, ESP32-P4). Images for chips without it (ESP32-S3)
// still build this adapter, but no profile of theirs asks for it.
#if CONFIG_ETH_USE_ESP32_EMAC
#define NUT_HAS_EMAC 1
#else
#define NUT_HAS_EMAC 0
#endif

EthLink::EthLink(const EthConfig& cfg)
    : m_cfg(cfg), m_started(false), m_lastConnected(false), m_lastLinkUp(false) {}

void EthLink::begin() {
    m_lastConnected = false;
    m_lastLinkUp = false;
    AppLogger::log("INFO", "[NETWORK] Initializing Ethernet...");

    uint8_t mac[6];
    macAddress(mac);
    char hostname[24];
    snprintf(hostname, sizeof(hostname), "esp32-nut-%02x%02x%02x", mac[3], mac[4], mac[5]);

    if (m_cfg.type != EthType::RMII) {
        AppLogger::log("ERROR", "[NETWORK] Ethernet type of this board profile not supported yet");
        return;
    }
#if NUT_HAS_EMAC
    eth_phy_type_t phy;
    switch (m_cfg.phy_type) {
    case EthPhy::IP101:
        phy = ETH_PHY_IP101;
        break;
    default:
        AppLogger::log("ERROR", "[NETWORK] Ethernet PHY %u of this board profile not supported",
                       (unsigned)m_cfg.phy_type);
        return;
    }
#ifdef ETH_RMII_CLK
    // The core takes the RMII clock GPIO from the variant (ETH_RMII_CLK), not from the
    // profile: a profile with another clock pin would be ignored without this warning
    if (m_cfg.clk >= 0 && m_cfg.clk != ETH_RMII_CLK) {
        AppLogger::log("WARN", "[NETWORK] Board profile RMII clock GPIO %d ignored: the core uses GPIO %d",
                       (int)m_cfg.clk, (int)ETH_RMII_CLK);
    }
#endif
    m_started = ETH.begin(phy, m_cfg.phy_addr, m_cfg.mdc, m_cfg.mdio, m_cfg.power, EMAC_CLK_EXT_IN);
    if (!m_started) {
        AppLogger::log("ERROR", "[NETWORK] Ethernet initialization failed: check the PHY pins of the board profile");
        return;
    }
    // The netif exists only after ETH.begin(): before it setHostname() fails. DHCP starts
    // when the link comes up, so the first request already carries the name
    if (ETH.setHostname(hostname)) {
        AppLogger::log("INFO", "[NETWORK] Ethernet started, hostname %s, waiting for link and DHCP", hostname);
    } else {
        AppLogger::log("WARN", "[NETWORK] Could not set the Ethernet hostname %s", hostname);
        AppLogger::log("INFO", "[NETWORK] Ethernet started, waiting for link and DHCP");
    }
#else
    AppLogger::log("ERROR", "[NETWORK] RMII Ethernet not available on this chip");
#endif
}

void EthLink::end() {
    if (m_started) ETH.end();
    m_started = false;
}

void EthLink::loop(uint32_t) {
    if (!m_started) return;
    bool link_up = ETH.linkUp();
    bool connected = isConnected();
    if (link_up != m_lastLinkUp) {
        if (link_up) {
            AppLogger::log("INFO", "[NETWORK] Ethernet link up: %u Mbps, %s duplex", (unsigned)ETH.linkSpeed(),
                           ETH.fullDuplex() ? "full" : "half");
        } else {
            AppLogger::log("WARN", "[NETWORK] Ethernet link down (cable unplugged?)");
        }
        m_lastLinkUp = link_up;
    }
    if (connected != m_lastConnected) {
        if (connected) {
            AppLogger::log("INFO", "[NETWORK] Connection established! IP Address: %s\n", ETH.localIP().toString().c_str());
        } else if (link_up) {
            AppLogger::log("WARN", "[NETWORK] Ethernet lost its IP address, waiting for DHCP");
        }
        m_lastConnected = connected;
    }
}

bool EthLink::isConnected() const {
    return m_started && ETH.linkUp() && ETH.hasIP();
}

String EthLink::statusText() const {
    if (!m_started || !ETH.linkUp()) return "Disconnected";
    if (!ETH.hasIP()) return "Connecting";
    return ETH.localIP().toString() + " (" + String(ETH.linkSpeed()) + " Mbps)";
}

String EthLink::localIP() const {
    return m_started ? ETH.localIP().toString() : String("0.0.0.0");
}

void EthLink::macAddress(uint8_t mac[6]) const {
    // From eFuse: valid before ETH.begin() too, so the hostname and the device id match
    if (esp_read_mac(mac, ESP_MAC_ETH) != ESP_OK) memset(mac, 0, 6);
}
