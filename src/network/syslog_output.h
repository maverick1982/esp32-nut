#ifndef SYSLOG_OUTPUT_H
#define SYSLOG_OUTPUT_H

#include <Arduino.h>
#include <IPAddress.h>
#include <NetworkUdp.h>
#include <mutex>
#include "LogOutput.h"
#include "SyslogSettings.h"
#include "SyslogStatus.h"
#include "SyslogChange.h"

class AppNetworkManager;

/**
 * @brief Syslog output over UDP, RFC 3164 or RFC 5424 (issue #69). Adapter of the net
 * module: the only code that sends syslog datagrams.
 *
 * Delivery runs in the log task only. configure() may be called from the web server
 * (loopTask): the new settings wait under a mutex and poll() applies them in the log
 * task. A host name is resolved in the log task too (DNS can take seconds), again
 * every 10 minutes, and after a failure every 30 seconds. State changes go to the
 * System Log; a failure is logged once, not for every line. Changes of the settings
 * are told to the syslog server too, whatever the minimum level (SyslogChange.h).
 * The lines it writes about itself start with tag() and stay in the System Log
 * (ADR 0012). Same code for Wi-Fi and Ethernet, on every board (ADR 0010).
 */
class SyslogOutput : public ILogOutput {
public:
    SyslogOutput();

    // The network service tells whether a link is up and gives the HOSTNAME of the
    // messages: the name of the Network page (US-113), read once the link is up
    void begin(const AppNetworkManager* network);
    // who: the Web UI user who changed the settings, empty for the saved ones at boot
    void configure(const SyslogSettings& settings, const String& who = String());
    SyslogStatus status() const;

    const char* name() const override { return "syslog"; }
    // Its own lines in the System Log are not sent to the syslog server (ADR 0012)
    const char* tag() const override { return "[SYSLOG]"; }
    bool enabled() const override { return _active.enabled; }
    void poll(uint32_t now) override;
    bool ready() const override { return _resolved && networkUp(); }
    bool deliver(const LogRecord& record) override;

private:
    // A station or Ethernet link is up; nothing is sent in AP mode
    bool networkUp() const;
    void apply(const SyslogSettings& s, const String& who);
    // A notice straight to the current server, outside the queue (before a change)
    bool sendNow(const char* msg);
    // One datagram; sendDatagram() tries a refused one again a few times, outside a
    // failure period (ARP still pending right after the link comes up)
    bool sendOnce(const char* buf, size_t n);
    bool sendDatagram(const char* buf, size_t n);
    void resolve(uint32_t now);
    // Updates the Web UI copy only when something changed
    void setState(SyslogState state, const String& error);

    const AppNetworkManager* _network = nullptr;
    // Set by poll() the first time the link is up. A new hostname restarts the board
    // (handleSetHostname), so the value never gets stale
    String _hostname;

    // Written by configure(), read by poll(): guarded by _mutex
    mutable std::mutex _mutex;
    SyslogSettings _pending;
    String _pending_who;
    bool _has_pending = false;
    // Copy for the Web UI: guarded by _mutex
    SyslogStatus _status;

    // Log task only
    SyslogSettings _active;
    NetworkUDP _udp;
    IPAddress _ip;
    bool _resolved = false;
    bool _is_literal = false;
    bool _resolve_failing = false;
    uint32_t _next_resolve = 0;
    bool _send_failing = false;
    SyslogState _shown_state = SyslogState::Disabled;
    String _shown_error;
};

#endif // SYSLOG_OUTPUT_H
