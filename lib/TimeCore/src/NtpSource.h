#ifndef NTP_SOURCE_H
#define NTP_SOURCE_H

#include <stdint.h>
#include <stdio.h>

/**
 * @brief Which NTP server the board uses: the one of DHCP option 42 or the one of Board
 * Settings (US-145, issue #135). Pure logic, native tests.
 *
 * The DHCP server is used only with the option on and a lease that carries it. When it
 * does not answer (TimeLogPolicy NoAnswer) the configured server takes over. The DHCP
 * server is tried again only on a new connection (onLinkDown), a lease with a different
 * server, or new settings (reset): never on a timer, so the two servers do not take turns.
 */
enum class NtpOrigin : uint8_t { Configured, Dhcp };

// Off: option off. None: no server from DHCP. Used: the DHCP server is in use. NoAnswer:
// the DHCP server did not answer, the configured one is in use
enum class DhcpNtpState : uint8_t { Off, None, Used, NoAnswer };

inline const char* ntpOriginName(NtpOrigin o) {
    return o == NtpOrigin::Dhcp ? "dhcp" : "configured";
}

inline const char* dhcpNtpStateName(DhcpNtpState s) {
    switch (s) {
    case DhcpNtpState::None: return "none";
    case DhcpNtpState::Used: return "used";
    case DhcpNtpState::NoAnswer: return "no_answer";
    default: return "off";
    }
}

static const size_t IPV4_TEXT_MAX = 16;

// "a.b.c.d" of an lwIP ip4_addr_t.addr: network order, so the first octet is the low byte
inline void formatIpv4(uint32_t lwip_addr, char out[IPV4_TEXT_MAX]) {
    snprintf(out, IPV4_TEXT_MAX, "%u.%u.%u.%u", (unsigned)(lwip_addr & 0xFF), (unsigned)((lwip_addr >> 8) & 0xFF),
             (unsigned)((lwip_addr >> 16) & 0xFF), (unsigned)((lwip_addr >> 24) & 0xFF));
}

class NtpSourcePolicy {
public:
    // Boot or new settings: forgets a failed DHCP server, keeps the lease
    void reset(bool use_dhcp) {
        _use = use_dhcp;
        _failed = 0;
    }

    // The network went down: the lease is gone
    void onLinkDown() {
        _dhcp = 0;
        _failed = 0;
    }

    // Server of the current lease (0: none). true when the server in use changes
    bool onDhcpServer(uint32_t addr) {
        if (addr == _dhcp) return false;
        NtpOrigin before = origin();
        uint32_t before_server = before == NtpOrigin::Dhcp ? _dhcp : 0;
        _dhcp = addr;
        if (addr != _failed) _failed = 0;
        NtpOrigin after = origin();
        uint32_t after_server = after == NtpOrigin::Dhcp ? _dhcp : 0;
        return before != after || before_server != after_server;
    }

    // No answer within TimeLogPolicy::NO_ANSWER_MS. true when it leaves the DHCP server
    // for the configured one
    bool onNoAnswer() {
        if (origin() != NtpOrigin::Dhcp) return false;
        _failed = _dhcp;
        return true;
    }

    NtpOrigin origin() const {
        return (_use && _dhcp != 0 && _dhcp != _failed) ? NtpOrigin::Dhcp : NtpOrigin::Configured;
    }

    DhcpNtpState state() const {
        if (!_use) return DhcpNtpState::Off;
        if (_dhcp == 0) return DhcpNtpState::None;
        if (_dhcp == _failed) return DhcpNtpState::NoAnswer;
        return DhcpNtpState::Used;
    }

    // Server of the current lease, 0 without (also with the option off)
    uint32_t dhcpServer() const { return _dhcp; }

private:
    bool _use = false;
    uint32_t _dhcp = 0;
    uint32_t _failed = 0;
};

#endif // NTP_SOURCE_H
