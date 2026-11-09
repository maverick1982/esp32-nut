#include "network/network_manager.h"
#include "network/wifi_links.h"
#include "network/eth_link.h"
#include "core/app_logger.h"
#include "NetHostname.h"
#include <esp_task_wdt.h>
#include <esp_mac.h>

AppNetworkManager::AppNetworkManager()
    : m_sta(nullptr), m_ap(nullptr), m_eth(nullptr), m_ethCfg(), m_hasEthCfg(false), m_hasWifi(false),
      m_ethProbe(EthProbeOutcome::WAITING), m_autoSwitch(false), m_ethStandby(false), m_linkChanges(0) {}

AppNetworkManager::~AppNetworkManager() {
    m_selector.activate(nullptr);
    if (m_ethStandby && m_eth) m_eth->end();
    delete m_sta;
    delete m_ap;
    delete m_eth;
}

String AppNetworkManager::ethHostnameSeed(bool has_wifi) const {
    if (!has_wifi) return m_savedHostname;
    // From eFuse, as WifiStaLink: the station need not be running
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return m_savedHostname;
    return NetHostname::effective(m_savedHostname, mac);
}

EthLink* AppNetworkManager::newEthLink(const EthConfig& cfg, bool has_wifi) const {
    EthLink* eth = new EthLink(cfg, ethHostnameSeed(has_wifi));
    // Issue #64: on a board with Wi-Fi the W5500 takes the station MAC, so the router sees
    // one device on both links and gives it the same address (a DHCP reservation works).
    // Safe only because the two never hold an address together (setDhcp, switchLink)
    uint8_t mac[6];
    if (has_wifi && esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) eth->setMac(mac);
    return eth;
}

void AppNetworkManager::begin(const BoardConfig& board, const NetBootPlan& plan, const String& ssid,
                              const String& password, const String& saved_hostname) {
    m_savedHostname = saved_hostname;
    m_hasWifi = board.has_wifi;
    // Kept for a station created later by the automatic switch (US-127)
    m_ssid = ssid;
    m_password = password;
    switch (plan.link) {
    case BootLink::WIFI_STATION:
        if (!m_sta) m_sta = new WifiStaLink(ssid, password, saved_hostname);
        // A W5500 in standby (AUTO mode) is outside the selector and keeps running
        m_selector.activate(m_sta, m_ethStandby);
        break;
    case BootLink::WIFI_ACCESS_POINT:
        if (!m_ap) m_ap = new WifiApLink(board.defaults.ap_ssid, board.defaults.ap_password);
        m_selector.activate(m_ap);
        break;
    case BootLink::ETHERNET: {
        // A W5500 set by the user (US-126) or the board's own port. After probeEthernet()
        // the link is already active and activate() leaves it as it is
        const EthConfig& cfg = m_hasEthCfg ? m_ethCfg : board.eth;
        if (cfg.type != EthType::NONE) {
            if (!m_eth) m_eth = newEthLink(cfg, board.has_wifi);
            m_selector.activate(m_eth);
            break;
        }
    }
        // fall through: a profile with has_ethernet but no Ethernet port data
    case BootLink::NONE:
        AppLogger::log("WARN", "[NETWORK] No network link available on this board (%s)", board.name);
        m_selector.activate(nullptr);
        break;
    }
    // US-127: only between Ethernet and the station, with the W5500 started
    m_autoSwitch = m_autoSwitch && m_eth && m_eth->started() &&
                   (plan.link == BootLink::ETHERNET || plan.link == BootLink::WIFI_STATION);
    if (m_autoSwitch) {
        // A cable that gave no address at startup is not tried again until it is replugged
        m_autoSwitcher.begin(millis(), plan.link == BootLink::ETHERNET ? AutoLink::ETHERNET : AutoLink::WIFI,
                             m_ssid.length() > 0, m_ethProbe == EthProbeOutcome::NO_IP);
    }
    // A missing cable leads to Wi-Fi after 10 s: no "No Ethernet link" reminder before it.
    // Without a Wi-Fi network, or in Ethernet mode, the reminder is all the log says
    if (m_eth) m_eth->setLinkReminder(!(m_autoSwitch && m_ssid.length() > 0));
}

void AppNetworkManager::setEthernetConfig(const EthConfig& cfg) {
    m_ethCfg = cfg;
    m_hasEthCfg = true;
}

EthProbeOutcome AppNetworkManager::probeEthernet(const String& saved_hostname, void (*tick)(uint32_t now)) {
    m_savedHostname = saved_hostname;
    m_hasWifi = board().has_wifi;
    if (!m_eth) m_eth = newEthLink(m_hasEthCfg ? m_ethCfg : EthConfig(), m_hasWifi);
    m_selector.activate(m_eth);
    EthBootProbe probe;
    probe.begin(millis(), m_eth->started());
    EthProbeOutcome outcome = probe.step(millis(), m_eth->linkUp(), m_eth->hasIP());
    while (outcome == EthProbeOutcome::WAITING) {
        m_selector.loop(millis());
        if (tick) tick(millis());
        esp_task_wdt_reset();
        delay(50);
        outcome = probe.step(millis(), m_eth->linkUp(), m_eth->hasIP());
    }
    m_selector.loop(millis());  // logs the link and the address before going on
    if (outcome != EthProbeOutcome::CONNECTED) {
        // The caller decides whether to fall back (needsFallback(), US-127)
        AppLogger::log("WARN", "[NETWORK] Ethernet (W5500) not available (%s)", ethProbeReason(outcome));
    }
    m_ethProbe = outcome;
    return outcome;
}

void AppNetworkManager::leaveEthernet(bool standby) {
    if (standby && m_eth && m_eth->started() && m_selector.active() == m_eth) {
        // Issue #64: no address on the standby W5500 while the station runs (same MAC)
        m_eth->setDhcp(false);
        m_selector.activate(nullptr, true);
        m_ethStandby = true;
        AppLogger::log("INFO", "[NETWORK] W5500 kept on standby: back to Ethernet when the cable is connected");
        return;
    }
    m_selector.activate(nullptr);
}

// Carries out an action of NetAutoSwitch: one link in the selector at a time, the W5500
// stays started (standby) while the board is on Wi-Fi. Issue #64: the W5500 has the
// station MAC, so the old link lets go of its address before the new one asks for it
void AppNetworkManager::switchLink(AutoAction action) {
    const uint32_t now = millis();
    if (action == AutoAction::TO_WIFI) {
        AppLogger::log("WARN", "[NETWORK] Ethernet lost: switching to Wi-Fi");
        m_eth->setDhcp(false);
        if (!m_sta) m_sta = new WifiStaLink(m_ssid, m_password, m_savedHostname);
        m_selector.activate(m_sta, true);
        m_ethStandby = true;
        m_autoSwitcher.switched(now, AutoLink::WIFI);
        if (m_autoSwitcher.waitingForReplug()) {
            AppLogger::log("WARN", "[NETWORK] No DHCP address on the cable: Ethernet tried again once the cable is "
                                   "replugged, or in %u min",
                           (unsigned)(NetAutoSwitch::RETRY_MS / 60000));
        }
    } else if (action == AutoAction::TO_ETHERNET) {
        AppLogger::log("INFO", "[NETWORK] Ethernet back: switching from Wi-Fi to Ethernet");
        // Stops the station (radio off) first; the W5500 is already running, its begin()
        // does nothing. Only then its DHCP asks for the address
        m_selector.activate(m_eth);
        m_eth->setDhcp(true);
        m_ethStandby = false;
        m_autoSwitcher.switched(now, AutoLink::ETHERNET);
    } else {
        return;
    }
    ++m_linkChanges;
}

void AppNetworkManager::loop() {
    m_selector.loop(millis());
    if (m_autoSwitch && m_eth && m_eth->started()) {
        switchLink(m_autoSwitcher.step(millis(), m_eth->linkUp(), m_eth->hasIP()));
    }
}

bool AppNetworkManager::isConnected() const {
    return m_selector.isConnected();
}

bool AppNetworkManager::isApMode() const {
    const INetLink* link = m_selector.active();
    return link && link->kind() == NetLinkKind::ACCESS_POINT;
}

String AppNetworkManager::statusText() const {
    const INetLink* link = m_selector.active();
    return link ? link->statusText() : String("Disconnected");
}

const char* AppNetworkManager::typeName() const {
    const INetLink* link = m_selector.active();
    return link ? link->typeName() : "none";
}

String AppNetworkManager::localIP() const {
    const INetLink* link = m_selector.active();
    return link ? link->localIP() : String("0.0.0.0");
}

void AppNetworkManager::macAddress(uint8_t mac[6]) const {
    const INetLink* link = m_selector.active();
    if (link) {
        link->macAddress(mac);
    } else {
        memset(mac, 0, 6);
    }
}

void AppNetworkManager::linkInfo(NetLinkInfo& out) const {
    const INetLink* link = m_selector.active();
    if (link) {
        link->linkInfo(out);
    } else {
        out = NetLinkInfo();
    }
}

String AppNetworkManager::hostname() const {
    const INetLink* link = m_selector.active();
    if (!link) return String();
    String name = link->hostname();
    if (name.length() > 0) return name;
    uint8_t mac[6];
    link->macAddress(mac);
    return NetHostname::effective(m_savedHostname, mac);
}

String AppNetworkManager::defaultHostname() const {
    const INetLink* link = m_selector.active();
    if (!link) return String();
    uint8_t mac[6];
    // US-127: on a board with Wi-Fi the default name comes from the station MAC on every
    // link, so cable and Wi-Fi give the board the same name
    if (!m_hasWifi || esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) link->macAddress(mac);
    return NetHostname::defaultFor(mac);
}

uint32_t AppNetworkManager::apStationCount() const {
    return isApMode() ? m_ap->stationCount() : 0;
}
