#include "network/syslog_output.h"
#include "network/network_manager.h"
#include "core/app_logger.h"
#include <Network.h>

static const char* kAppName = "esp32-nut";
static const uint32_t RESOLVE_REFRESH_MS = 10UL * 60UL * 1000UL;
static const uint32_t RESOLVE_RETRY_MS = 30UL * 1000UL;

SyslogOutput::SyslogOutput() : _pending(syslogDefaults()), _active(syslogDefaults()) {
    _status.state = SyslogState::Disabled;
    _status.stats = { 0, 0, 0, 0 };
}

void SyslogOutput::begin(const AppNetworkManager* network, const String& hostname) {
    _network = network;
    _hostname = hostname;
}

void SyslogOutput::configure(const SyslogSettings& settings) {
    std::lock_guard<std::mutex> lock(_mutex);
    _pending = sanitize(settings);
    _has_pending = true;
}

SyslogStatus SyslogOutput::status() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _status;
}

bool SyslogOutput::networkUp() const {
    return _network && _network->isConnected();
}

void SyslogOutput::setState(SyslogState state, const String& error) {
    if (state == _shown_state && error == _shown_error) return;
    _shown_state = state;
    _shown_error = error;
    std::lock_guard<std::mutex> lock(_mutex);
    _status.state = state;
    _status.error = error;
}

void SyslogOutput::apply(const SyslogSettings& s) {
    _active = s;
    _filter.configure(s.min_level, s.rate);
    _resolved = false;
    _resolve_failing = false;
    _send_failing = false;
    _next_resolve = millis();
    _is_literal = _ip.fromString(s.host.c_str());
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _status.target = s.enabled ? s.host + ":" + String(s.port) : String("");
    }
    if (!s.enabled) {
        setState(SyslogState::Disabled, "");
        AppLogger::log("INFO", "[SYSLOG] Remote logging off");
        return;
    }
    setState(SyslogState::Waiting, "");
    AppLogger::log("INFO", "[SYSLOG] Sending to %s:%u (%s, facility %s, %s and above, max %u lines/s)",
                   s.host.c_str(), (unsigned)s.port, s.protocol == SyslogProtocol::RFC5424 ? "RFC 5424" : "RFC 3164",
                   syslogFacilityName(s.facility), logLevelName(s.min_level), (unsigned)s.rate);
}

void SyslogOutput::resolve(uint32_t now) {
    if (_is_literal) {
        _resolved = true;
        return;
    }
    if ((int32_t)(now - _next_resolve) < 0) return;  // not due yet

    IPAddress ip;
    if (Network.hostByName(_active.host.c_str(), ip) == 1 && ip != IPAddress((uint32_t)0)) {
        if (!_resolved || ip != _ip) {
            std::lock_guard<std::mutex> lock(_mutex);
            _status.target = _active.host + " (" + ip.toString() + "):" + String(_active.port);
        }
        _ip = ip;
        _resolved = true;
        _resolve_failing = false;
        _next_resolve = now + RESOLVE_REFRESH_MS;
        return;
    }
    _next_resolve = now + RESOLVE_RETRY_MS;
    // A failed refresh keeps the last address: the receiver is probably still there
    if (_resolved || _resolve_failing) return;
    _resolve_failing = true;
    setState(SyslogState::Waiting, String("Cannot resolve ") + _active.host);
    AppLogger::log("WARN", "[SYSLOG] Cannot resolve %s, retrying every %u s", _active.host.c_str(),
                   (unsigned)(RESOLVE_RETRY_MS / 1000));
}

void SyslogOutput::poll(uint32_t now) {
    SyslogSettings next;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_has_pending) {
            next = _pending;
            _has_pending = false;
            changed = true;
        }
        _status.stats = _stats;
    }
    if (changed) apply(next);
    if (!_active.enabled) return;
    if (!networkUp()) {
        if (_shown_state != SyslogState::Error) setState(SyslogState::Waiting, "No network connection");
        return;
    }
    resolve(now);
}

bool SyslogOutput::deliver(const LogRecord& record) {
    char buf[SYSLOG_MAX_MESSAGE];
    size_t n = formatSyslog(buf, sizeof(buf), _active.protocol, _active.facility, record.level, _hostname.c_str(),
                            kAppName, record.msg);
    bool ok = n > 0 && _udp.beginPacket(_ip, _active.port) == 1 && _udp.write((const uint8_t*)buf, n) == n &&
              _udp.endPacket() == 1;
    if (ok) {
        _send_failing = false;
        setState(SyslogState::Active, "");
        return true;
    }
    if (!_send_failing) {
        // Logged once per failure period, not for every line
        _send_failing = true;
        setState(SyslogState::Error, String("Send to ") + _ip.toString() + " failed");
        AppLogger::log("WARN", "[SYSLOG] Send to %s:%u failed", _ip.toString().c_str(), (unsigned)_active.port);
    }
    return false;
}
