#include "network/eth_link.h"
#include "core/app_logger.h"
#include "NetHostname.h"
#include <esp_mac.h>
#include <esp_eth.h>
#include <esp_netif.h>
#include <ETH.h>
#include <driver/spi_common.h>

// RMII needs the internal EMAC (ESP32, ESP32-P4). Images for chips without it (ESP32-S3)
// still build the RMII branch, but no profile of theirs asks for it. The W5500 (SPI, US-126)
// works on every chip: its pins come from the Network page, not from the profile.
#if CONFIG_ETH_USE_ESP32_EMAC
#define NUT_HAS_EMAC 1
#else
#define NUT_HAS_EMAC 0
#endif

EthLink::EthLink(const EthConfig& cfg, const String& saved_hostname)
    : m_cfg(cfg), m_savedHostname(saved_hostname), m_started(false), m_lastConnected(false), m_lastLinkUp(false),
      m_hasMac(false), m_mac{0, 0, 0, 0, 0, 0} {}

void EthLink::setMac(const uint8_t mac[6]) {
    if (m_cfg.type != EthType::SPI) return;
    memcpy(m_mac, mac, 6);
    m_hasMac = true;
}

// Right after ETH.begin(): the core has already given the W5500 the MAC derived from
// ESP_MAC_ETH. The link negotiation takes ~1 s, so the first DHCP request leaves with the
// new one. The driver filters the frames on its MAC register, lwIP writes the netif one
void EthLink::applyMac() {
    if (!m_hasMac) return;
    esp_err_t err = esp_eth_ioctl(ETH.handle(), ETH_CMD_S_MAC_ADDR, m_mac);
    if (err == ESP_OK) err = esp_netif_set_mac(ETH.netif(), m_mac);
    if (err == ESP_OK) {
        AppLogger::log("INFO", "[NETWORK] W5500 uses the Wi-Fi MAC %02X:%02X:%02X:%02X:%02X:%02X", m_mac[0], m_mac[1],
                       m_mac[2], m_mac[3], m_mac[4], m_mac[5]);
    } else {
        AppLogger::log("WARN", "[NETWORK] Could not give the W5500 the Wi-Fi MAC: %s", esp_err_to_name(err));
        m_hasMac = false;
    }
}

void EthLink::setDhcp(bool on) {
    if (!m_started) return;
    esp_netif_t* netif = ETH.netif();
    if (on) {
        const esp_err_t err = esp_netif_dhcpc_start(netif);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            AppLogger::log("WARN", "[NETWORK] Ethernet DHCP could not start: %s", esp_err_to_name(err));
        }
    } else {
        const esp_err_t err = esp_netif_dhcpc_stop(netif);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            AppLogger::log("WARN", "[NETWORK] Ethernet DHCP could not stop: %s", esp_err_to_name(err));
        }
    }
    // The "lost its IP address" line belongs to an active link, not to a standby
    m_lastConnected = isConnected();
}

void EthLink::begin() {
    // US-127: a W5500 kept on standby while the board was on Wi-Fi becomes the active link
    // again without restarting: its link is already there, the network manager turns its
    // DHCP back on (issue #64)
    if (m_started) return;
    m_lastConnected = false;
    m_lastLinkUp = false;
    AppLogger::log("INFO", "[NETWORK] Initializing Ethernet...");

    uint8_t mac[6];
    macAddress(mac);
    // The saved name (US-113) or esp32-nut-xxxxxx from the Ethernet MAC
    m_hostname = NetHostname::effective(m_savedHostname, mac);
    const char* hostname = m_hostname.c_str();

    if (m_cfg.type == EthType::SPI && m_cfg.phy_type == EthPhy::W5500) {
        beginW5500(hostname);
        return;
    }
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
    m_linkWait.begin(millis());
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

void EthLink::beginW5500(const char* hostname) {
#if CONFIG_ETH_SPI_ETHERNET_W5500
    // Without INT the driver reads the chip every 10 ms instead of waiting for the interrupt
    const bool polling = m_cfg.irq < 0;
    if (polling) ETH.setPollPeriod(10);
    m_started = ETH.begin(ETH_PHY_W5500, m_cfg.phy_addr, m_cfg.cs, m_cfg.irq, m_cfg.rst, SPI2_HOST, m_cfg.sck,
                          m_cfg.miso, m_cfg.mosi);
    if (!m_started) {
        AppLogger::log("ERROR", "[NETWORK] W5500 not responding: check the pins in the Network page");
        releaseW5500();
        return;
    }
    AppLogger::log("INFO", "[NETWORK] W5500 started: SCK %d, MOSI %d, MISO %d, CS %d, INT %d, RST %d (%s)",
                   (int)m_cfg.sck, (int)m_cfg.mosi, (int)m_cfg.miso, (int)m_cfg.cs, (int)m_cfg.irq, (int)m_cfg.rst,
                   polling ? "polling" : "interrupt");
    applyMac();
    if (ETH.setHostname(hostname)) {
        AppLogger::log("INFO", "[NETWORK] Ethernet started, hostname %s, waiting for link and DHCP", hostname);
    } else {
        AppLogger::log("WARN", "[NETWORK] Could not set the Ethernet hostname %s", hostname);
        AppLogger::log("INFO", "[NETWORK] Ethernet started, waiting for link and DHCP");
    }
#else
    (void)hostname;
    AppLogger::log("ERROR", "[NETWORK] W5500 driver not available in this image");
#endif
}

// ETH.end() stops the driver and deletes the W5500 device, but leaves the SPI bus
// initialized and its GPIOs routed to it: free it, so the fallback to Wi-Fi starts from a
// clean state. Also after a failed ETH.begin(), which already deleted the device but not
// the bus. ESP_ERR_INVALID_STATE = bus not initialized, nothing to free
void EthLink::releaseW5500() {
#if CONFIG_ETH_SPI_ETHERNET_W5500
    ETH.end();
    const esp_err_t err = spi_bus_free(SPI2_HOST);
    if (err == ESP_OK) {
        AppLogger::log("INFO", "[NETWORK] W5500 stopped, SPI bus released");
    } else if (err != ESP_ERR_INVALID_STATE) {
        AppLogger::log("WARN", "[NETWORK] Could not release the W5500 SPI bus: %s", esp_err_to_name(err));
    }
#endif
}

void EthLink::end() {
    if (m_cfg.type == EthType::SPI && m_cfg.phy_type == EthPhy::W5500) {
        if (m_started) releaseW5500();
    } else if (m_started) {
        ETH.end();
    }
    m_started = false;
}

void EthLink::loop(uint32_t now) {
    if (!m_started) return;
    bool link_up = ETH.linkUp();
    bool connected = isConnected();
    // Link down from the start never changes state: without this the log stays silent
    if (uint32_t waited_s = m_linkWait.update(link_up, now)) {
        AppLogger::log("WARN", "[NETWORK] No Ethernet link for %lu s: check the cable and the switch port",
                       (unsigned long)waited_s);
    }
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
    return m_started && ETH.linkUp() && hasIP();
}

String EthLink::statusText() const {
    if (!m_started || !ETH.linkUp()) return "Disconnected";
    if (!hasIP()) return "Connecting";
    return ETH.localIP().toString() + " (" + String(ETH.linkSpeed()) + " Mbps)";
}

String EthLink::localIP() const {
    return m_started ? ETH.localIP().toString() : String("0.0.0.0");
}

void EthLink::linkInfo(NetLinkInfo& out) const {
    out = NetLinkInfo();
    out.type = typeName();
    macAddress(out.mac);
    if (!m_started || !ETH.linkUp()) {
        out.state = NetLinkState::DISCONNECTED;
        return;
    }
    out.link_speed_mbps = (uint16_t)ETH.linkSpeed();
    out.full_duplex = ETH.fullDuplex();
    out.dhcp = true; // address by DHCP only
    if (!hasIP()) {
        out.state = NetLinkState::CONNECTING;
        return;
    }
    out.state = NetLinkState::CONNECTED;
    out.ip = ETH.localIP().toString();
    out.gateway = ETH.gatewayIP().toString();
}

void EthLink::macAddress(uint8_t mac[6]) const {
    // Issue #64: the station MAC given by the network manager, as the router sees it
    if (m_hasMac) {
        memcpy(mac, m_mac, 6);
        return;
    }
    // From eFuse: valid before ETH.begin() too, so the hostname and the device id match
    if (esp_read_mac(mac, ESP_MAC_ETH) != ESP_OK) {
        memset(mac, 0, 6);
        return;
    }
    // US-126: an SPI module has no MAC of its own. The core gives it the locally
    // administered address derived from the Ethernet MAC (ETHClass::beginSPI, ETH index 0):
    // the same one here, so the Network page shows the MAC the router sees. Only the first
    // byte changes, so the default hostname (last 3 bytes) stays the same
    if (m_cfg.type == EthType::SPI) {
        uint8_t universal[6];
        memcpy(universal, mac, 6);
        if (esp_derive_local_mac(mac, universal) != ESP_OK) memcpy(mac, universal, 6);
    }
}

bool EthLink::linkUp() const {
    return m_started && ETH.linkUp();
}

bool EthLink::hasIP() const {
    return m_started && ETH.hasIP() && (uint32_t)ETH.localIP() != 0;
}
