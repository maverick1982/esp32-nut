#ifndef NTP_CLOCK_H
#define NTP_CLOCK_H

#include <Arduino.h>
#include <atomic>
#include <mutex>
#include <sys/time.h>
#include <time.h>
#include "TimeSettings.h"
#include "TimeStatus.h"
#include "WallClock.h"

class AppNetworkManager;

/**
 * @brief Network time from an NTP server, with the time zone of Board Settings (US-117).
 * Adapter of the net module: the only code that calls esp_sntp_* and sets TZ.
 *
 * The time is only shown and written in the logs (ADR 0009): sessions, login limits and
 * the AP timeout keep using millis(). configure() may be called from the web server: the
 * new settings wait under a mutex and poll() applies them in the loopTask, without a
 * restart. SNTP is asynchronous (lwIP), so nothing ever waits for it: not the USB task,
 * not the NUT server, not the loop. The synchronization callback runs in the lwIP task:
 * it only stores the reference pair of WallClock under the mutex and raises a flag that
 * poll() turns into the System Log lines (TimeLogPolicy). Same code for Wi-Fi and
 * Ethernet, on every board (ADR 0010).
 *
 * DHCP option 42 stays off: lwIP writes the servers it gets from DHCP from index 0 and
 * would replace the server chosen by the user.
 */
class NtpClock {
public:
    struct Status {
        TimeState state;
        TimeSettings settings;
        bool has_time;
        int64_t epoch_ms;       // now, when has_time
        uint32_t since_sync_s;  // since the last synchronization, when has_time
    };

    void begin(const AppNetworkManager* network);
    // who: the Web UI user who changed the settings, empty for the saved ones at boot
    void configure(const TimeSettings& settings, const String& who = String());
    // loopTask, once per loop()
    void poll(uint32_t now);

    // Unix time in ms of a millis() value, also of one before the synchronization; false
    // without the time. Any task
    bool epochMsAt(uint32_t uptime_ms, int64_t& out) const;
    Status status() const;

    // Broken-down time of a Unix time in ms: local (time zone of the settings) and UTC
    static void localTm(int64_t epoch_ms, struct tm& out);
    static void utcTm(int64_t epoch_ms, struct tm& out);

private:
    static void onSync(struct timeval* tv);
    static NtpClock* s_instance;

    bool networkUp() const;
    void apply(const TimeSettings& s, const String& who, uint32_t now);
    void publishState();

    const AppNetworkManager* _network = nullptr;

    // Written by configure(), read by poll(); _clock written by onSync(): all under _mutex
    mutable std::mutex _mutex;
    TimeSettings _pending = timeDefaults();
    String _pending_who;
    bool _has_pending = false;
    WallClock _clock;
    Status _status = { TimeState::Disabled, timeDefaults(), false, 0, 0 };
    std::atomic<bool> _synced{false};

    // loopTask only
    TimeSettings _active = timeDefaults();
    TimeLogPolicy _policy;
    bool _sntp_running = false;
    // lwIP keeps the pointer given to esp_sntp_setservername(): the name must stay here
    char _server[NTP_SERVER_MAX + 1] = {0};
};

#endif // NTP_CLOCK_H
