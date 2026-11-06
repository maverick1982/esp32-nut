#ifndef NET_AUTO_SWITCH_H
#define NET_AUTO_SWITCH_H

#include <stdint.h>

/**
 * @brief Decides when the AUTO link mode switches between Ethernet and Wi-Fi (US-127).
 *
 * Pure logic, so it runs in the native unit tests. The network manager calls step() from
 * its loop with eth_ready = the W5500 has the cable link and a DHCP address, then carries
 * out the action and calls switched(). step() never changes active() by itself.
 * - On Ethernet: eth_ready false without interruption for more than ETH_DOWN_MS →
 *   TO_WIFI, only if a Wi-Fi network is configured (never towards the access point).
 * - On Wi-Fi: eth_ready true without interruption for more than ETH_UP_MS → TO_ETHERNET.
 * - Never within HOLD_MS of begin() or of the last switch, so a flapping cable does not
 *   make the board bounce between the two links.
 * Worst case, measured from the cable change: right after a switch the hold is still
 * running, so the board waits for its end (unplugged 1 s after a switch → TO_WIFI 14 s
 * later); otherwise ETH_DOWN_MS or ETH_UP_MS. At most HOLD_MS = 15 s, plus the time the
 * new link needs to connect: inside the 30 s of the acceptance criteria.
 * Times are millis(): unsigned subtraction survives the wrap.
 */
enum class AutoLink : uint8_t { ETHERNET, WIFI };
enum class AutoAction : uint8_t { NONE, TO_WIFI, TO_ETHERNET };

class NetAutoSwitch {
public:
    static const uint32_t ETH_DOWN_MS = 10000;
    static const uint32_t ETH_UP_MS = 5000;
    static const uint32_t HOLD_MS = 15000;

    NetAutoSwitch()
        : _since(0), _changedAt(0), _active(AutoLink::ETHERNET), _wifi(false), _waiting(false) {}

    void begin(uint32_t now, AutoLink active, bool wifi_available) {
        _active = active;
        _wifi = wifi_available;
        _changedAt = now;
        _waiting = false;
        _since = now;
    }

    AutoAction step(uint32_t now, bool eth_ready) {
        // The condition that would make the board switch: Ethernet lost while on it,
        // Ethernet ready while on Wi-Fi
        const bool wanted = _active == AutoLink::ETHERNET ? !eth_ready : eth_ready;
        if (!wanted) {
            _waiting = false;
            return AutoAction::NONE;
        }
        if (!_waiting) {
            _waiting = true;
            _since = now;
        }
        if ((uint32_t)(now - _changedAt) < HOLD_MS) return AutoAction::NONE;
        if (_active == AutoLink::ETHERNET) {
            if (!_wifi || (uint32_t)(now - _since) <= ETH_DOWN_MS) return AutoAction::NONE;
            return AutoAction::TO_WIFI;
        }
        return (uint32_t)(now - _since) > ETH_UP_MS ? AutoAction::TO_ETHERNET : AutoAction::NONE;
    }

    void switched(uint32_t now, AutoLink active) {
        _active = active;
        _changedAt = now;
        _waiting = false;
        _since = now;
    }

    AutoLink active() const { return _active; }

private:
    uint32_t _since;      // start of the uninterrupted condition
    uint32_t _changedAt;  // begin() or last switch
    AutoLink _active;
    bool _wifi;
    bool _waiting;
};

#endif // NET_AUTO_SWITCH_H
