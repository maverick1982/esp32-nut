#ifndef LOG_FANOUT_H
#define LOG_FANOUT_H

#include <stdint.h>
#include <stdio.h>
#include "LogOutput.h"

/**
 * @brief Delivers each log line to the enabled outputs, each with its own minimum
 * level and rate limit (issue #69). Pure logic, native tests with fake outputs.
 *
 * - Boot or a new configuration: the first lines are logged before the output can
 *   deliver (no network yet, host not resolved). While an enabled output is not ready,
 *   for up to HOLD_MS after it was enabled (at boot or from the Web UI), shouldHold()
 *   asks the log task to keep the lines in the queue, so the boot log and the
 *   "remote logging enabled" notice still arrive. After that, lines for an output that
 *   is not ready are dropped and counted.
 * - Notices (LogRecord::notice, changes of the remote logging itself) skip the minimum
 *   level and the rate limit.
 * - Rate limit: before the first line an output accepts after a refusal, it gets one
 *   summary line with the number of lines it missed.
 */
class LogFanout {
public:
    static constexpr uint8_t MAX_OUTPUTS = 4;
    static constexpr uint32_t HOLD_MS = 60000;

    bool add(ILogOutput* out) {
        if (!out || _count >= MAX_OUTPUTS) return false;
        _outputs[_count++] = out;
        return true;
    }

    uint8_t count() const { return _count; }

    void poll(uint32_t now) {
        for (uint8_t i = 0; i < _count; i++) _outputs[i]->poll(now);
    }

    bool shouldHold(uint32_t now) const {
        for (uint8_t i = 0; i < _count; i++) {
            const ILogOutput* out = _outputs[i];
            if (out->enabled() && !out->ready() && now - out->enabledAt() < HOLD_MS) return true;
        }
        return false;
    }

    void deliver(const LogRecord& record, uint32_t now) {
        for (uint8_t i = 0; i < _count; i++) {
            ILogOutput* out = _outputs[i];
            if (!out->enabled()) continue;
            if (!out->ready()) {
                out->stats().not_ready++;
                continue;
            }
            if (record.notice) {
                send(out, record);
                continue;
            }
            LogOutputFilter::Decision d = out->filter().check(record.level, now);
            if (d == LogOutputFilter::Decision::BELOW_LEVEL) continue;
            if (d == LogOutputFilter::Decision::RATE_LIMITED) {
                out->stats().rate_limited++;
                continue;
            }
            uint32_t missed = out->filter().takeSuppressed();
            if (missed > 0) {
                LogRecord summary;
                summary.uptime_ms = record.uptime_ms;
                summary.level = LogLevel::WARN;
                summary.notice = false;
                snprintf(summary.msg, sizeof(summary.msg), "[LOG] %lu lines not sent (rate limit %u/s)",
                         (unsigned long)missed, (unsigned)out->filter().rate());
                send(out, summary);
            }
            send(out, record);
        }
    }

private:
    static void send(ILogOutput* out, const LogRecord& record) {
        if (out->deliver(record)) {
            out->stats().sent++;
        } else {
            out->stats().failed++;
        }
    }

    ILogOutput* _outputs[MAX_OUTPUTS] = { nullptr, nullptr, nullptr, nullptr };
    uint8_t _count = 0;
};

#endif // LOG_FANOUT_H
