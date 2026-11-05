---
type: decision
title: "Remote Log Outputs: Non-blocking Queue, log_out Task and Per-Output Fanout"
description: "Documento ADR: Remote Log Outputs: Non-blocking Queue, log_out Task and Per-Output Fanout"
tags: [adr, decision, logging, syslog, mqtt, tasks]
---
# Remote Log Outputs: Non-blocking Queue, log_out Task and Per-Output Fanout

* **ADR ID:** 0012
* **Status:** Accepted
* **Date:** 2026-11-04
* **Authors:** @maverick1982, @rtadams89, Claude
* **Related:** ADR 0008 (non-blocking USB task, Task WDT), ADR 0009 (protected routes), ADR 0010 (module map, task ownership), issue #69, PR #104, US-116, US-117 (time in the messages), `docs/plans/mqtt-support.md`

## Context and Problem Statement

The System Log of the Web UI is a ring buffer of 50 lines in RAM: after a few hours the lines about a rare USB disconnection or a UPS state change are gone. Users want a copy of the log on their own servers (issue #69): a syslog receiver today (rsyslog, syslog-ng, a NAS), MQTT later.

`AppLogger::log()` runs in the loopTask (web server, NUT server) **and** in the USB/poll task, under a recursive mutex. Sending from there would make those tasks wait on the network: a DNS lookup can take seconds, and ADR 0008 forbids the USB task from waiting. An output that logs its own failures would also feed them back to itself in a loop. How do the log lines reach one or more remote outputs without ever delaying the UPS or the NUT server?

## Considered Options

* **A. Send from `AppLogger::log()`**, in the caller's task.
* **B. Drain a queue from the loopTask**, next to the web and NUT servers.
* **C. One queue filled without waiting, drained by a dedicated low-priority task that hands each line to every enabled output (fanout)**, with a minimum level and a rate limit per output.

## Decision Outcome

Chosen option: **C**, the backend of PR #104 by @rtadams89, with one added rule for the lines an output writes about itself.

* **Queue.** `AppLogger` keeps the serial line and the System Log buffer as before; after the buffer, under the same mutex, it calls a forwarder (`AppLogger::setForwarder`). `LogDispatcher::submit()` copies the line into a FreeRTOS queue of 40 fixed-size `LogRecord` (message up to 240 bytes) with `xQueueSend(..., 0)`: it never waits and never allocates. A full queue drops the line and counts it; the log task then sends `[LOG] N lines lost: log queue full`. The queue is created first thing in `setup()`, so the boot lines are kept until the network is up.
* **Task.** `log_out` (4096 bytes of stack, priority 1, no core affinity) belongs to the `system` module of the ADR 0010 map, owned by `LogDispatcher` in `src/core/log_dispatcher.*`. It is started after the outputs have their settings. It is **not** under the Task WDT: a slow DNS lookup or a stuck socket blocks only this task, never the USB task or the loopTask.
* **Outputs are adapters.** Each output implements `ILogOutput` (`lib/LogCore/src/LogOutput.h`) and lives in the module of its transport: syslog over UDP in `net` (`src/network/syslog_output.*`), MQTT in the future `mqtt` module, which will deliver from the `log_out` task with `esp_mqtt_client_enqueue()` (no wait on the broker). An output applies new settings and refreshes its name resolution in `poll()`, called by the log task; the web server only hands it the settings under a mutex.
* **Fanout, filter and limit** (`LogFanout`, pure logic in `lib/LogCore`, native tests). Up to 4 outputs, each with its own minimum level (INFO, WARN, ERROR) and rate limit (1–100 lines per second, default 10). The first line accepted after a refusal is preceded by one summary line with the number of lines missed. While an enabled output is not ready (no network, host not resolved), the task keeps the lines in the queue for up to 60 s after the output was enabled (at boot or from the Web UI); after that they are dropped and counted (`not_ready`).
* **Notices.** A change of the remote logging itself (enabled, disabled, moved, level changed) is logged with `AppLogger::logNotice()`: it skips the minimum level and the rate limit, so the receiver always learns about it. Before a move or a disable, the previous server is told directly (`SyslogChange.h`).
* **Own lines.** `ILogOutput::tag()` is the prefix of the lines an output writes about itself in the System Log (`"[SYSLOG]"` for syslog: send failed, host not resolved). A line that is not a notice and starts with an output's tag is not delivered to that output and does not touch its statistics; the other outputs still receive it, so a future MQTT output gets the `[SYSLOG]` lines and syslog gets the `[MQTT]` ones. Failures are written once per failure period, not once per line.
* **Settings** are stored in their own NVS namespace (`logout`), separate from `config_json`: they survive an OTA update, apply without a restart and are available in AP mode. The API (`GET`/`POST /api/logging`) is registered with `guarded()` and is not public in `AuthPolicy` (ADR 0009).
* **Network.** The syslog output sends only when a station or Ethernet link is up (never in AP mode), with the hostname of the Network page (US-113) as HOSTNAME, on every board without `#if` (ADR 0010). Without the network time the receiver timestamps each line.
* **Time in the messages (US-117).** With the SNTP time of Board Settings → Time (`NtpClock`, ADR 0009: logs only), each message carries the time its line was written, computed from `LogRecord.uptime_ms` and the reference pair of the last synchronization, so a line held since boot gets its own time, not the sending time. RFC 3164: `<PRI>Mmm dd hh:mm:ss HOSTNAME TAG: MSG`, local time of the chosen time zone, with the hostname of the Network page. RFC 5424: `<PRI>1 YYYY-MM-DDThh:mm:ss.mmmZ HOST APP - - - MSG`, in UTC. Without the time (NTP off, no answer yet, LAN without Internet) the format stays the one of US-116: RFC 3164 without TIMESTAMP and HOSTNAME, RFC 5424 with `-` (`SyslogFormat.h`, native tests).
* **Board time on the System Logs page (US-118).** With the same time, `GET /api/logs` adds `board_time` to each line (`2026-11-05T09:12:03.007+01:00`, ISO 8601 with the offset of the chosen time zone), counted back from the moment the list is built with an unsigned difference of the uptimes (`BoardTime.h`, native tests). The terminal shows its `hh:mm:ss` and Export downloads it; `id`, `time`, `level`, `msg` and `X-Uptime-Ms` are unchanged, and without the time the field is missing and the page uses the browser clock (issue #60). AppLogger reads `millis()` once and hands the same value to the log queue, so the page and the syslog server show the same second.
* **Security.** Syslog over UDP is not encrypted: anyone on the LAN can read the lines, including the usernames of failed logins (ADR 0009 forbids only passwords and tokens in the log). The Web UI says so next to the settings. TCP and TLS are out of scope.

## Consequences

### Positive
* The USB task and the NUT server never wait on the network for logging; the change to `AppLogger` is one non-blocking call.
* New outputs (MQTT) plug into the same queue, task, filter and limit: they write only the transport adapter.
* The boot log and the configuration changes reach the receiver even when the network comes up late.
* The decisions (hold, level, limit, own lines, message format) are pure logic with native tests.

### Negative
* About 14 KB of heap (queue of 40 records, about 10 KB, and the 4 KB task stack), even with remote logging off.
* Lines are lost when the queue overflows or an output is unreachable for more than a minute; they are counted, not retried.
* A second consumer of `AppLogger` lines means the message text is now an interface: the tag prefixes must stay stable.

## Impact on Agent Implementation

* Never send log lines to the network from `AppLogger`, from the USB task or from the loopTask: add an `ILogOutput` and register it with `LogDispatcher::addOutput()` before `start()`.
* `LogDispatcher::submit()` must stay non-blocking (`xQueueSend` with timeout 0); do not add locks or allocations to it.
* A new output must return a unique `tag()` and prefix with it every line it logs about itself; use `AppLogger::logNotice()` only for changes of its own settings.
* Keep the filter, limit and message logic in `lib/LogCore` (no platform calls, no `AppLogger`), with native tests; the transport stays in the module of that transport.
* Do not put `log_out` under the Task WDT, and do not raise its priority above the USB tasks.
* Remote logging routes are protected with `guarded()` and never added to the public list of `AuthPolicy`.
