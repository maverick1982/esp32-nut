#ifndef SYSLOG_OUTPUT_H
#define SYSLOG_OUTPUT_H

#include <Arduino.h>
#include <IPAddress.h>
#include <NetworkUdp.h>
#include <mutex>
#include "LogOutput.h"
#include "SyslogSettings.h"
#include "SyslogStatus.h"

class AppNetworkManager;

/**
 * @brief Syslog output over UDP, RFC 3164 or RFC 5424 (issue #69). Adapter of the net
 * module: the only code that sends syslog datagrams.
 *
 * Delivery runs in the log task only. configure() may be called from the web server
 * (loopTask): the new settings wait under a mutex and poll() applies them in the log
 * task. A host name is resolved in the log task too (DNS can take seconds), again
 * every 10 minutes, and after a failure every 30 seconds. State changes go to the
 * System Log; a failure is logged once, not for every line.
 */
class SyslogOutput : public ILogOutput {
public:
    SyslogOutput();

    // The network service tells whether a link is up; hostname is the HOSTNAME of RFC 5424
    void begin(const AppNetworkManager* network, const String& hostname);
    void configure(const SyslogSettings& settings);
    SyslogStatus status() const;

    const char* name() const override { return "syslog"; }
    bool enabled() const override { return _active.enabled; }
    void poll(uint32_t now) override;
    bool ready() const override { return _resolved && networkUp(); }
    bool deliver(const LogRecord& record) override;

private:
    bool networkUp() const;
    void apply(const SyslogSettings& s);
    void resolve(uint32_t now);
    // Updates the Web UI copy only when something changed
    void setState(SyslogState state, const String& error);

    const AppNetworkManager* _network = nullptr;
    String _hostname;

    // Written by configure(), read by poll(): guarded by _mutex
    mutable std::mutex _mutex;
    SyslogSettings _pending;
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
