#include "network/ntp_clock.h"
#include "network/network_manager.h"
#include "core/app_logger.h"
#include <esp_sntp.h>
#include <stdlib.h>
#include <string.h>

NtpClock* NtpClock::s_instance = nullptr;

void NtpClock::begin(const AppNetworkManager* network) {
    _network = network;
    s_instance = this;
}

void NtpClock::configure(const TimeSettings& settings, const String& who) {
    std::lock_guard<std::mutex> lock(_mutex);
    _pending = sanitize(settings);
    _pending_who = who;
    _has_pending = true;
}

bool NtpClock::networkUp() const {
    return _network && _network->isConnected() && !_network->isApMode();
}

void NtpClock::onSync(struct timeval* tv) {
    NtpClock* self = s_instance;
    if (!self || !tv) return;
    int64_t epoch_ms = (int64_t)tv->tv_sec * 1000 + tv->tv_usec / 1000;
    {
        std::lock_guard<std::mutex> lock(self->_mutex);
        self->_clock.setSynced(epoch_ms, millis());
    }
    // No AppLogger in the lwIP task: poll() writes the line
    self->_synced = true;
}

void NtpClock::apply(const TimeSettings& s, const String& who, uint32_t now) {
    if (_sntp_running) {
        esp_sntp_stop();
        _sntp_running = false;
    }
    const TimeZone* zone = findTimeZone(s.zone.c_str());
    setenv("TZ", zone ? zone->posix : "UTC0", 1);
    tzset();

    if (s.ntp) {
        strncpy(_server, s.server.c_str(), sizeof(_server) - 1);
        _server[sizeof(_server) - 1] = '\0';
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_servermode_dhcp(false);
        esp_sntp_setservername(0, _server);
        sntp_set_time_sync_notification_cb(&NtpClock::onSync);
        // A new server or time zone keeps the time already known; SNTP starts in poll()
        // once the network is up
        _policy.restart(now);
    } else {
        _policy.stop();
        std::lock_guard<std::mutex> lock(_mutex);
        _clock.clear();
    }
    _synced = false;
    _active = s;

    if (who.length() > 0) {
        if (s.ntp) {
            AppLogger::log("INFO", "[TIME] Settings changed by '%s': NTP on, %s, %s", who.c_str(), s.server.c_str(),
                           s.zone.c_str());
        } else {
            AppLogger::log("INFO", "[TIME] Settings changed by '%s': NTP off, %s", who.c_str(), s.zone.c_str());
        }
    }
}

void NtpClock::poll(uint32_t now) {
    TimeSettings next;
    String who;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_has_pending) {
            next = _pending;
            who = _pending_who;
            _has_pending = false;
            changed = true;
        }
    }
    if (changed) apply(next, who, now);

    bool up = networkUp();
    if (_active.ntp && up && !_sntp_running) {
        esp_sntp_init();
        _sntp_running = true;
    }

    if (_synced.exchange(false) && _policy.onSynced() == TimeLogPolicy::Line::Synchronized) {
        int64_t ms = 0;
        if (epochMsAt(millis(), ms)) {
            struct tm t;
            localTm(ms, t);
            char when[40];
            strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S %Z", &t);
            AppLogger::log("INFO", "[TIME] Synchronized with %s: %s", _active.server.c_str(), when);
        }
    }
    if (_active.ntp && _policy.tick(now, up) == TimeLogPolicy::Line::NoAnswer) {
        AppLogger::log("WARN", "[TIME] No answer from %s for %u s, retrying", _active.server.c_str(),
                       (unsigned)(TimeLogPolicy::NO_ANSWER_MS / 1000));
    }
    publishState();
}

void NtpClock::publishState() {
    std::lock_guard<std::mutex> lock(_mutex);
    _status.settings = _active;
    if (!_active.ntp) {
        _status.state = TimeState::Disabled;
    } else if (_clock.valid()) {
        _status.state = TimeState::Synchronized;
    } else if (_policy.noAnswer()) {
        _status.state = TimeState::NoAnswer;
    } else {
        _status.state = TimeState::Waiting;
    }
}

bool NtpClock::epochMsAt(uint32_t uptime_ms, int64_t& out) const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _clock.epochMsAt(uptime_ms, out);
}

bool NtpClock::reference(WallClock& out) const {
    std::lock_guard<std::mutex> lock(_mutex);
    out = _clock;
    return out.valid();
}

NtpClock::Status NtpClock::status() const {
    uint32_t now = millis();
    std::lock_guard<std::mutex> lock(_mutex);
    Status s = _status;
    s.has_time = _clock.epochMsAt(now, s.epoch_ms);
    s.since_sync_s = s.has_time ? (now - _clock.syncedAt()) / 1000 : 0;
    if (!s.has_time) s.epoch_ms = 0;
    return s;
}

void NtpClock::localTm(int64_t epoch_ms, struct tm& out) {
    time_t t = (time_t)(epoch_ms / 1000);
    localtime_r(&t, &out);
}

void NtpClock::utcTm(int64_t epoch_ms, struct tm& out) {
    time_t t = (time_t)(epoch_ms / 1000);
    gmtime_r(&t, &out);
}
