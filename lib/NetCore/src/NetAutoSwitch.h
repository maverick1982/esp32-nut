#ifndef NET_AUTO_SWITCH_H
#define NET_AUTO_SWITCH_H

#include <stdint.h>

/**
 * @brief Decides when the AUTO link mode switches between Ethernet and Wi-Fi (US-127).
 *
 * Pure logic, so it runs in the native unit tests. The network manager calls step() from
 * its loop with the W5500 cable link and whether it has a DHCP address, then carries out
 * the action and calls switched(). step() never changes active() by itself.
 * - On Ethernet: link and address not both there, without interruption for more than
 *   ETH_DOWN_MS → TO_WIFI, only if a Wi-Fi network is configured (never towards the
 *   access point).
 * - On Wi-Fi: cable link without interruption for more than ETH_UP_MS → TO_ETHERNET.
 *   The link alone (issue #64): the W5500 has the station MAC and asks for its address
 *   only once the station is off, so the router never sees that MAC on both interfaces.
 * - Never within HOLD_MS of begin() or of the last switch, so a flapping cable does not
 *   make the board bounce between the two links.
 * - A cable with link and no DHCP address (the board left Ethernet, or the startup probe
 *   gave up, with the link still up) is not tried again until the link goes down, or
 *   until RETRY_MS have passed: otherwise the board would bounce every ~30 s.
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
    static const uint32_t RETRY_MS = 300000;

    NetAutoSwitch()
        : _since(0), _changedAt(0), _noAddressAt(0), _active(AutoLink::ETHERNET), _wifi(false), _waiting(false),
          _linkUp(false), _noAddress(false) {}

    /**
     * @param eth_no_address the startup probe saw the link but got no DHCP address
     */
    void begin(uint32_t now, AutoLink active, bool wifi_available, bool eth_no_address = false) {
        _active = active;
        _wifi = wifi_available;
        _changedAt = now;
        _waiting = false;
        _since = now;
        _linkUp = eth_no_address;
        _noAddress = active == AutoLink::WIFI && eth_no_address;
        _noAddressAt = now;
    }

    AutoAction step(uint32_t now, bool link_up, bool has_address) {
        _linkUp = link_up;
        if (_noAddress && (!link_up || (uint32_t)(now - _noAddressAt) >= RETRY_MS)) _noAddress = false;
        // The condition that would make the board switch: Ethernet lost while on it,
        // the cable back (and worth a try) while on Wi-Fi
        const bool wanted = _active == AutoLink::ETHERNET ? !(link_up && has_address) : (link_up && !_noAddress);
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
        // Leaving Ethernet with the link still up: the address is what failed
        _noAddress = active == AutoLink::WIFI && _active == AutoLink::ETHERNET && _linkUp;
        _noAddressAt = now;
        _active = active;
        _changedAt = now;
        _waiting = false;
        _since = now;
    }

    AutoLink active() const { return _active; }
    // A cable with link and no address, not tried again for now
    bool waitingForReplug() const { return _noAddress; }

private:
    uint32_t _since;        // start of the uninterrupted condition
    uint32_t _changedAt;    // begin() or last switch
    uint32_t _noAddressAt;  // when the cable without address was given up
    AutoLink _active;
    bool _wifi;
    bool _waiting;
    bool _linkUp;     // link at the last step()
    bool _noAddress;  // cable with link and no DHCP address, not tried again yet
};

#endif // NET_AUTO_SWITCH_H
