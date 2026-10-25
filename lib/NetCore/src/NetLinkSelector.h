#ifndef NET_LINK_SELECTOR_H
#define NET_LINK_SELECTOR_H

#include "INetLink.h"

/**
 * @brief Keeps exactly one network link active (ADR 0010 §3, PDR 0002 D3).
 *
 * Pure logic, so it runs in the native unit tests. activate() stops the previous link
 * before starting the new one: the NUT server, the web UI and the hostname always
 * refer to a single address. Without a link every query answers "not connected".
 */
class NetLinkSelector {
public:
    NetLinkSelector() : _active(nullptr) {}

    void activate(INetLink* link) {
        if (link == _active) return;
        if (_active) _active->end();
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
