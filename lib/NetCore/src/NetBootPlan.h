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
    // Save the default configuration before starting (boards without Wi-Fi, US-092)
    bool default_config;
};

inline NetBootPlan planNetwork(BoardCaps caps, bool config_valid, bool manual_trigger) {
    NetBootPlan plan = { BootLink::NONE, false, false, false, false };
    if (caps.has_wifi) {
        if (!config_valid || manual_trigger) {
            plan.link = BootLink::WIFI_ACCESS_POINT;
            plan.captive_portal = true;
            plan.ap_idle_timeout = manual_trigger && config_valid;
        } else {
            plan.link = BootLink::WIFI_STATION;
        }
        return plan;
    }
    if (caps.has_ethernet) plan.link = BootLink::ETHERNET;
    plan.recovery_window = manual_trigger;
    plan.default_config = !config_valid;
    return plan;
}

#endif // NET_BOOT_PLAN_H
