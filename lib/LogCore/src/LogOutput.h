#ifndef LOG_OUTPUT_H
#define LOG_OUTPUT_H

#include <stdint.h>
#include "LogLevel.h"
#include "LogOutputFilter.h"

/**
 * @brief One log line on its way to the remote outputs, and the output interface
 * (issue #69).
 *
 * The record has a fixed size, so the logger copies it into a queue without
 * allocating: AppLogger::log() runs in the loopTask and in the USB task and must never
 * wait on the network. Messages longer than the buffer are cut.
 */
static const size_t LOG_RECORD_MSG = 240;

struct LogRecord {
    uint32_t uptime_ms;
    LogLevel level;
    char msg[LOG_RECORD_MSG];
};

struct LogOutputStats {
    uint32_t sent;          // delivered to the transport
    uint32_t failed;        // the transport refused it (no route, socket error)
    uint32_t rate_limited;  // refused by the rate limit
    uint32_t not_ready;     // dropped while the output could not deliver (no network)
};

/**
 * A remote output: syslog today, MQTT later. Called only by the log task, so an
 * implementation needs no locking for its delivery state; settings changed from the
 * web server are applied by the output itself in poll().
 */
class ILogOutput {
public:
    virtual ~ILogOutput() {}

    virtual const char* name() const = 0;
    virtual bool enabled() const = 0;
    // Once per log task iteration: apply new settings, refresh name resolution
    virtual void poll(uint32_t now) = 0;
    // false while the output cannot deliver yet (network down, host not resolved)
    virtual bool ready() const = 0;
    // true when the transport accepted the line
    virtual bool deliver(const LogRecord& record) = 0;

    LogOutputFilter& filter() { return _filter; }
    LogOutputStats& stats() { return _stats; }
    const LogOutputStats& stats() const { return _stats; }

protected:
    LogOutputFilter _filter;
    LogOutputStats _stats = { 0, 0, 0, 0 };
};

#endif // LOG_OUTPUT_H
