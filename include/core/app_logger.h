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
    static String getLogsJSON();

    // Remote outputs (issue #69): every line is also handed to fn, which must not block
    // (LogDispatcher::submit copies it into a queue). nullptr turns it off.
    typedef void (*Forwarder)(void* ctx, const char* level, const char* msg);
    static void setForwarder(Forwarder fn, void* ctx);

private:
    static Forwarder forwarder;
    static void* forwarderCtx;
    static LogMessage logBuffer[MAX_LOGS];
    static int head;
    static int count;
    static uint32_t nextId;
};

#endif // APP_LOGGER_H
