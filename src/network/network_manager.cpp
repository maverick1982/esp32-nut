#include "network/network_manager.h"
#include "network/wifi_links.h"
#include "network/eth_link.h"
#include "core/app_logger.h"
#include "NetHostname.h"

AppNetworkManager::AppNetworkManager() : m_sta(nullptr), m_ap(nullptr), m_eth(nullptr) {}

AppNetworkManager::~AppNetworkManager() {
    m_selector.activate(nullptr);
    delete m_sta;
    delete m_ap;
    delete m_eth;
}

void AppNetworkManager::begin(const BoardConfig& board, const NetBootPlan& plan, const String& ssid,
                              const String& password, const String& saved_hostname) {
    m_savedHostname = saved_hostname;
    switch (plan.link) {
    case BootLink::WIFI_STATION:
        if (!m_sta) m_sta = new WifiStaLink(ssid, password, saved_hostname);
        m_selector.activate(m_sta);
        break;
    case BootLink::WIFI_ACCESS_POINT:
        if (!m_ap) m_ap = new WifiApLink(board.defaults.ap_ssid, board.defaults.ap_password);
        m_selector.activate(m_ap);
        break;
    case BootLink::ETHERNET:
        if (board.eth.type != EthType::NONE) {
            if (!m_eth) m_eth = new EthLink(board.eth, saved_hostname);
            m_selector.activate(m_eth);
            break;
        }
        // fall through: a profile with has_ethernet but no Ethernet port data
    case BootLink::NONE:
        AppLogger::log("WARN", "[NETWORK] No network link available on this board (%s)", board.name);
        m_selector.activate(nullptr);
        break;
    }
}

void AppNetworkManager::loop() {
    m_selector.loop(millis());
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
    link->macAddress(mac);
    return NetHostname::defaultFor(mac);
}

uint32_t AppNetworkManager::apStationCount() const {
    return isApMode() ? m_ap->stationCount() : 0;
}
