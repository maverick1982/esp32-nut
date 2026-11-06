#ifndef ETH_BOOT_PROBE_H
#define ETH_BOOT_PROBE_H

#include <stdint.h>

/**
 * @brief Decides at startup whether the W5500 Ethernet link is usable (US-126).
 *
 * Pure logic, so it runs in the native unit tests. The network manager starts the module,
 * then calls step() until the outcome is no longer WAITING:
 * - the module did not start (wrong pins, no module): NO_CHIP at once;
 * - no cable link within ETH_LINK_TIMEOUT_MS of begin(): NO_LINK;
 * - link but no DHCP address within ETH_DHCP_TIMEOUT_MS of the first link: NO_IP.
 * A link that drops restarts the DHCP wait, but the link wait is measured from begin()
 * and is never restarted. So the probe lasts at most 5 s without a link, about 20 s with
 * a link and no address. Times are millis(): unsigned subtraction survives the wrap.
 */
enum class EthProbeOutcome : uint8_t { WAITING, CONNECTED, NO_CHIP, NO_LINK, NO_IP };

class EthBootProbe {
public:
    static const uint32_t ETH_LINK_TIMEOUT_MS = 5000;
    static const uint32_t ETH_DHCP_TIMEOUT_MS = 15000;

    EthBootProbe() : _start(0), _linkAt(0), _started(false), _hasLink(false) {}

    void begin(uint32_t now, bool started) {
        _start = now;
        _started = started;
        _hasLink = false;
        _linkAt = 0;
    }

    EthProbeOutcome step(uint32_t now, bool link_up, bool has_ip) {
        if (!_started) return EthProbeOutcome::NO_CHIP;
        if (link_up && has_ip) return EthProbeOutcome::CONNECTED;
        if (link_up) {
            if (!_hasLink) {
                _hasLink = true;
                _linkAt = now;
            }
            return (uint32_t)(now - _linkAt) > ETH_DHCP_TIMEOUT_MS ? EthProbeOutcome::NO_IP
                                                                  : EthProbeOutcome::WAITING;
        }
        _hasLink = false;
        return (uint32_t)(now - _start) > ETH_LINK_TIMEOUT_MS ? EthProbeOutcome::NO_LINK
                                                              : EthProbeOutcome::WAITING;
    }

private:
    uint32_t _start;
    uint32_t _linkAt;
    bool _started;
    bool _hasLink;
};

// Short reason for the web UI and the API ("" while waiting or once connected)
inline const char* ethProbeReason(EthProbeOutcome outcome) {
    switch (outcome) {
        case EthProbeOutcome::NO_CHIP: return "no_chip";
        case EthProbeOutcome::NO_LINK: return "no_link";
        case EthProbeOutcome::NO_IP: return "no_ip";
        default: return "";
    }
}

#endif // ETH_BOOT_PROBE_H
