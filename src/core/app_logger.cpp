#include "core/app_logger.h"
#include <stdarg.h>

LogMessage AppLogger::logBuffer[AppLogger::MAX_LOGS];
int AppLogger::head = 0;
int AppLogger::count = 0;
uint32_t AppLogger::nextId = 1;

void AppLogger::log(const String& level, const String& msg) {
    // Print to serial
    Serial.printf("[%lu] [%s] %s\n", millis(), level.c_str(), msg.c_str());

    // Add to buffer
    logBuffer[head].id = nextId++;
    logBuffer[head].time = millis();
    logBuffer[head].level = level;
    logBuffer[head].msg = msg;

    head = (head + 1) % MAX_LOGS;
    if (count < MAX_LOGS) {
        count++;
    }
}

void AppLogger::log(const char* level, const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    log(String(level), String(buffer));
}

String AppLogger::getLogsJSON() {
    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();

    int startIdx = (count < MAX_LOGS) ? 0 : head;
    for (int i = 0; i < count; i++) {
        int idx = (startIdx + i) % MAX_LOGS;
        JsonObject obj = array.add<JsonObject>();
        obj["id"] = logBuffer[idx].id;
        obj["time"] = logBuffer[idx].time;
        obj["level"] = logBuffer[idx].level;
        obj["msg"] = logBuffer[idx].msg;
    }

    String output;
    serializeJson(doc, output);
    return output;
}
