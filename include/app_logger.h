#ifndef APP_LOGGER_H
#define APP_LOGGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

struct LogMessage {
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

private:
    static LogMessage logBuffer[MAX_LOGS];
    static int head;
    static int count;
};

#endif // APP_LOGGER_H
