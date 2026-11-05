#ifndef APP_LOGGER_H
#define APP_LOGGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

struct LogMessage {
    uint32_t id;
    unsigned long time;
    String level;
    String msg;
};

class AppLogger {
public:
    static const int MAX_LOGS = 50;

    static void log(const String& level, const String& msg);
    static void log(const char* level, const char* format, ...);
    // Board time of a line (US-118): writes it in out and returns true, or false when the
    // board has no time. list_uptime_ms is millis() read under the logger mutex, so no
    // line is newer. Called under the logger mutex: it must not log nor wait for a lock
    // that a logging task may hold
    typedef bool (*BoardTimeFn)(void* ctx, uint32_t row_uptime_ms, uint32_t list_uptime_ms, char* out,
                                size_t len);
    // [{id, time, level, msg}], plus "board_time" on each line when fn gives it
    static String getLogsJSON(BoardTimeFn fn = nullptr, void* ctx = nullptr);

    // A change of the remote logging itself (enabled, minimum level changed): logged
    // like any line, and sent to the remote outputs past their minimum level and rate
    // limit, so the receiver always sees it (issue #69)
    static void logNotice(const char* level, const char* format, ...);

    // Remote outputs (issue #69): every line is also handed to fn, which must not block
    // (LogDispatcher::submit copies it into a queue). nullptr turns it off.
    // uptime_ms: millis() of the line, the same value kept in the buffer, so the System
    // Log page and the syslog server show the same time (US-118)
    typedef void (*Forwarder)(void* ctx, const char* level, const char* msg, bool notice, uint32_t uptime_ms);
    static void setForwarder(Forwarder fn, void* ctx);

private:
    static void write(const String& level, const String& msg, bool notice);
    static Forwarder forwarder;
    static void* forwarderCtx;
    static LogMessage logBuffer[MAX_LOGS];
    static int head;
    static int count;
    static uint32_t nextId;
};

#endif // APP_LOGGER_H
