#ifndef CORE_LOG_DISPATCHER_H
#define CORE_LOG_DISPATCHER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "LogFanout.h"

/**
 * @brief Logging core of the system module (issue #69): one queue, one task, several
 * remote outputs.
 *
 * AppLogger hands every line to submit(), which copies it into a fixed-size queue and
 * never waits: a full queue drops the line and counts it. The log task drains the queue
 * into the LogFanout, so the USB task and the NUT server never wait on the network.
 * The serial line and the System Log of the Web UI stay synchronous in AppLogger, as
 * before. The decisions (hold at boot, level, rate limit) are in LogFanout (native
 * tests); this class only owns the FreeRTOS queue and task.
 */
class LogDispatcher {
public:
    static const UBaseType_t QUEUE_LENGTH = 40;

    // Creates the queue: call first in setup(), so the boot lines are kept
    bool init();
    bool addOutput(ILogOutput* out) { return _fanout.add(out); }
    // Starts the task, after the outputs have their settings
    bool start();

    // From AppLogger, any task: never blocks. notice: a change of the remote logging
    // itself, which skips the minimum level and the rate limit (LogRecord::notice)
    void submit(const char* level, const char* msg, bool notice);

    // Lines lost because the queue was full
    uint32_t queueDrops() const { return _queue_drops; }

    // Adapter for AppLogger::setForwarder()
    static void forward(void* self, const char* level, const char* msg, bool notice) {
        static_cast<LogDispatcher*>(self)->submit(level, msg, notice);
    }

private:
    static void taskEntry(void* arg);
    void run();

    QueueHandle_t _queue = nullptr;
    TaskHandle_t _task = nullptr;
    LogFanout _fanout;
    volatile uint32_t _queue_drops = 0;
    uint32_t _reported_drops = 0;
};

#endif // CORE_LOG_DISPATCHER_H
