#include "core/app_logger.h"
#include <stdarg.h>
#include <mutex>

// Called by the loopTask and by the USB task (review A5b)
static std::recursive_mutex s_log_mutex;

LogMessage AppLogger::logBuffer[AppLogger::MAX_LOGS];
int AppLogger::head = 0;
int AppLogger::count = 0;
uint32_t AppLogger::nextId = 1;
AppLogger::Forwarder AppLogger::forwarder = nullptr;
void* AppLogger::forwarderCtx = nullptr;

void AppLogger::setForwarder(Forwarder fn, void* ctx) {
    std::lock_guard<std::recursive_mutex> lock(s_log_mutex);
    forwarder = fn;
    forwarderCtx = ctx;
}

void AppLogger::log(const String& level, const String& msg) {
    write(level, msg, false);
}

void AppLogger::logNotice(const char* level, const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    write(String(level), String(buffer), true);
}

void AppLogger::write(const String& level, const String& msg, bool notice) {
    std::lock_guard<std::recursive_mutex> lock(s_log_mutex);
    // One millis() for the serial line, the buffer and the remote outputs (US-118)
    const uint32_t now = millis();
    // Print to serial
    Serial.printf("[%lu] [%s] %s\n", (unsigned long)now, level.c_str(), msg.c_str());

    // Add to buffer
    logBuffer[head].id = nextId++;
    logBuffer[head].time = now;
    logBuffer[head].level = level;
    logBuffer[head].msg = msg;

    head = (head + 1) % MAX_LOGS;
    if (count < MAX_LOGS) {
        count++;
    }

    // Remote outputs: a non-blocking copy into the log queue (issue #69)
    if (forwarder) forwarder(forwarderCtx, level.c_str(), msg.c_str(), notice, now);
}

void AppLogger::log(const char* level, const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    log(String(level), String(buffer));
}

String AppLogger::getLogsJSON(BoardTimeFn fn, void* ctx) {
    std::lock_guard<std::recursive_mutex> lock(s_log_mutex);
    // After the lock: every line is written under it, so none is newer (US-118)
    const uint32_t list_uptime = millis();
    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();
    char board_time[40];

    int startIdx = (count < MAX_LOGS) ? 0 : head;
    for (int i = 0; i < count; i++) {
        int idx = (startIdx + i) % MAX_LOGS;
        JsonObject obj = array.add<JsonObject>();
        obj["id"] = logBuffer[idx].id;
        obj["time"] = logBuffer[idx].time;
        obj["level"] = logBuffer[idx].level;
        obj["msg"] = logBuffer[idx].msg;
        if (fn && fn(ctx, (uint32_t)logBuffer[idx].time, list_uptime, board_time, sizeof(board_time))) {
            // A copy: the buffer is reused for the next line
            obj["board_time"] = String(board_time);
        }
    }

    String output;
    serializeJson(doc, output);
    return output;
}
