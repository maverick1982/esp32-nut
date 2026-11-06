#ifndef NET_BOOT_PLAN_H
#define NET_BOOT_PLAN_H

#include <stdint.h>

/**
 * @brief Which network link the board starts with (ADR 0010 §3, PDR 0001 D7, PDR 0002 D5).
 *
 * Pure logic, so it runs in the native unit tests for boards with and without Wi-Fi.
 *
 * - With Wi-Fi: the access point (with the captive portal) when the configuration is
 *   missing or invalid, or after the double reset; otherwise the station. The AP idle
 *   timeout runs only after the double reset with a valid configuration: there is a
 *   normal mode to go back to.
 * - Without Wi-Fi there is no access point: Ethernet when the board has it. The double
 *   reset asks for the recovery window of the web credentials instead (US-091). A missing
 *   or invalid configuration has no setup page to fill in either: the board saves the
 *   default NUT settings and joins the network with DHCP (US-092).
 * - With Wi-Fi and a W5500 enabled by the user (US-126): Ethernet first, with Wi-Fi as
 *   the fallback (the station when it is configured, otherwise the access point) if the
 *   module does not answer or gets no address (EthBootProbe). Only one link at a time
 *   (PDR 0002 D3). The double reset still starts the access point.
 */
struct BoardCaps {
    bool has_wifi;
    bool has_ethernet;
};

enum class BootLink : uint8_t { WIFI_STATION, WIFI_ACCESS_POINT, ETHERNET, NONE };

struct NetBootPlan {
    BootLink link;
    bool captive_portal;
    bool ap_idle_timeout;
    bool recovery_window;
    // Save the default configuration before starting (boards without Wi-Fi, US-092; a
    // W5500 board that ends up on Ethernet without a configuration, US-126)
    bool default_config;
    // Link to start if `link` (the W5500) does not come up, NONE = no fallback (US-126)
    BootLink fallback;
};

struct NetBootInput {
    bool has_wifi;
    bool has_ethernet;
    bool spi_eth;        // W5500 enabled by the user (US-126)
    bool config_valid;
    bool wifi_ssid_set;  // the saved configuration names a Wi-Fi network
    bool manual_trigger; // double reset
};

inline NetBootPlan planNetwork(const NetBootInput& in) {
    NetBootPlan plan = { BootLink::NONE, false, false, false, false, BootLink::NONE };
    if (in.has_wifi) {
        if (in.spi_eth && !in.manual_trigger) {
            plan.link = BootLink::ETHERNET;
            plan.fallback = (in.config_valid && in.wifi_ssid_set) ? BootLink::WIFI_STATION
                                                                  : BootLink::WIFI_ACCESS_POINT;
            plan.default_config = !in.config_valid;
        } else if (!in.config_valid || in.manual_trigger) {
            plan.link = BootLink::WIFI_ACCESS_POINT;
            plan.captive_portal = true;
            plan.ap_idle_timeout = in.manual_trigger && in.config_valid;
        } else if (!in.wifi_ssid_set) {
            // A valid configuration without a network name (saved for Ethernet): no station
            // to join, so the access point asks for it, with nothing to go back to
            plan.link = BootLink::WIFI_ACCESS_POINT;
            plan.captive_portal = true;
        } else {
            plan.link = BootLink::WIFI_STATION;
        }
        return plan;
    }
    if (in.has_ethernet) plan.link = BootLink::ETHERNET;
    plan.recovery_window = in.manual_trigger;
    plan.default_config = !in.config_valid;
    return plan;
}

inline NetBootPlan planNetwork(BoardCaps caps, bool config_valid, bool manual_trigger) {
    const NetBootInput in = { caps.has_wifi, caps.has_ethernet, false, config_valid, config_valid, manual_trigger };
    return planNetwork(in);
}

// The plan to follow when the first link did not come up: the access point of a fallback
// has the captive portal and no idle timeout (there is no working link to go back to)
inline NetBootPlan fallbackPlan(const NetBootPlan& plan) {
    NetBootPlan next = plan;
    next.link = plan.fallback;
    next.fallback = BootLink::NONE;
    next.default_config = false;
    next.captive_portal = plan.fallback == BootLink::WIFI_ACCESS_POINT;
    next.ap_idle_timeout = false;
    return next;
}

#endif // NET_BOOT_PLAN_H
