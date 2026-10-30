#include "core/log_dispatcher.h"
#include "core/app_logger.h"
#include <string.h>

bool LogDispatcher::init() {
    if (_queue) return true;
    _queue = xQueueCreate(QUEUE_LENGTH, sizeof(LogRecord));
    return _queue != nullptr;
}

bool LogDispatcher::start() {
    if (!_queue) return false;
    if (_task) return true;
    // Below the HID task (5) and the USB library task (2): logging never delays the UPS
    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "log_out", 4096, this, 1, &_task, tskNO_AFFINITY);
    if (ok != pdPASS) {
        _task = nullptr;
        AppLogger::log("ERROR", "[LOG] Log task creation failed: remote logging off");
        return false;
    }
    return true;
}

void LogDispatcher::submit(const char* level, const char* msg) {
    if (!_queue) return;
    LogRecord r;
    r.uptime_ms = millis();
    r.level = logLevelOf(level);
    strncpy(r.msg, msg ? msg : "", sizeof(r.msg) - 1);
    r.msg[sizeof(r.msg) - 1] = '\0';
    if (xQueueSend(_queue, &r, 0) != pdTRUE) {
        _queue_drops = _queue_drops + 1;
    }
}

void LogDispatcher::taskEntry(void* arg) {
    static_cast<LogDispatcher*>(arg)->run();
}

void LogDispatcher::run() {
    LogRecord r;
    for (;;) {
        uint32_t now = millis();
        _fanout.poll(now);

        // Lines lost on a full queue: one line with the count, once per batch
        uint32_t drops = _queue_drops;
        if (drops != _reported_drops) {
            LogRecord lost;
            lost.uptime_ms = now;
            lost.level = LogLevel::WARN;
            snprintf(lost.msg, sizeof(lost.msg), "[LOG] %lu lines lost: log queue full",
                     (unsigned long)(drops - _reported_drops));
            _reported_drops = drops;
            _fanout.deliver(lost, now);
        }

        // Boot: keep the lines while an enabled output waits for the network
        if (_fanout.shouldHold(now)) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (xQueueReceive(_queue, &r, pdMS_TO_TICKS(1000)) == pdTRUE) {
            _fanout.deliver(r, millis());
        }
    }
}
