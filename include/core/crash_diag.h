#ifndef CRASH_DIAG_H
#define CRASH_DIAG_H

#include <Arduino.h>
#include <ArduinoJson.h>

/**
 * Post-mortem diagnostics (issue #47, ADR 0008).
 *
 * - Guards the core dump writer called by the panic handler: when the partition table
 *   has no "coredump" partition (boards updated only via OTA), the dump is skipped so
 *   the panic completes and the board reboots, instead of re-entering the panic handler
 *   with the watchdogs disabled.
 * - Logs the reset reason, the cause of the last controlled restart and the summary of
 *   the last core dump, and exposes them to the web UI.
 * - After a task watchdog reset, logs the tasks that did not reset the watchdog in time,
 *   saved in RTC memory by the TWDT interrupt hook before the panic (US-110).
 */
namespace CrashDiag {
    // Call first in setup(): arms the core dump guard and collects the boot diagnostics
    void begin();
    // Writes the boot diagnostics to the application log
    void logBootInfo();
    // Stores the reason of a controlled restart in RTC memory and counts it as one more
    // consecutive restart; the caller then restarts
    void recordControlledRestart(const char* reason);
    // Stores only the reason, shown after the boot as the last controlled restart, without
    // counting it as a USB recovery restart (manual AP timeout, US-064)
    void recordRestartReason(const char* reason);
    // Cause of a restart asked from the Web UI (US-142)
    constexpr const char* MANUAL_RESTART_CAUSE = "Manual restart from the Web UI";
    // Stores MANUAL_RESTART_CAUSE like recordRestartReason(): the counter of USB recovery
    // restarts is neither incremented nor cleared. After the boot the web UI gets
    // "last_restart_manual", so it is not shown as an automatic restart
    void recordManualRestart();
    // Controlled restarts in a row since the data was last healthy (review A7)
    uint8_t consecutiveRestarts();
    void clearConsecutiveRestarts();
    // Cause of the first controlled restart of the current series, "" when the counter is 0
    // (issue #78): the later restarts overwrite the last cause, so in degraded mode this is
    // the only record of what started it. Logged at boot and sent as first_restart_cause.
    const char* firstRestartReason();
    // Degraded mode: restarts suspended after too many in a row (shown in the web UI).
    // upsNotEnumerated: the UPS never enumerated after the restarts (US-121), so the web
    // UI asks to replug the USB cable
    void setDegraded(bool degraded, bool upsNotEnumerated = false);
    bool hasCoredumpPartition();
    void fillJson(JsonObject obj);
}

#endif // CRASH_DIAG_H
