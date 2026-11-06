#ifndef NET_LINK_SELECTOR_H
#define NET_LINK_SELECTOR_H

#include "INetLink.h"

/**
 * @brief Keeps exactly one network link active (ADR 0010 §3, PDR 0002 D3).
 *
 * Pure logic, so it runs in the native unit tests. activate() stops the previous link
 * before starting the new one: the NUT server, the web UI and the hostname always
 * refer to a single address. Without a link every query answers "not connected".
 *
 * keep_previous (US-127) leaves the previous link running outside the selector: the
 * W5500 stays started while the board is on Wi-Fi in AUTO mode, so the network manager
 * can see the cable come back. Only the selected link counts for the state, the name and
 * the servers. Activating that link again later calls its begin(), which must then be
 * harmless on a link that is already running.
 */
class NetLinkSelector {
public:
    NetLinkSelector() : _active(nullptr) {}

    void activate(INetLink* link, bool keep_previous = false) {
        if (link == _active) return;
        if (_active && !keep_previous) _active->end();
        _active = link;
        if (_active) _active->begin();
    }

    INetLink* active() const { return _active; }

    void loop(uint32_t now) {
        if (_active) _active->loop(now);
    }

    bool isConnected() const { return _active && _active->isConnected(); }

private:
    INetLink* _active;
};

#endif // NET_LINK_SELECTOR_H
