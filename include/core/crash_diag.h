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
 */
namespace CrashDiag {
    // Call first in setup(): arms the core dump guard and collects the boot diagnostics
    void begin();
    // Writes the boot diagnostics to the application log
    void logBootInfo();
    // Stores the reason of a controlled restart in RTC memory; the caller then restarts
    void recordControlledRestart(const char* reason);
    bool hasCoredumpPartition();
    void fillJson(JsonObject obj);
}

#endif // CRASH_DIAG_H
