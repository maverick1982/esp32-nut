---
type: decision
title: "Centralized Two-Tier HID Polling and Static Variable Lifecycle in GenericDriver"
description: "Documento ADR: Centralized Two-Tier HID Polling and Static Variable Lifecycle in GenericDriver"
tags: [adr, decision]
---
# Centralized Two-Tier HID Polling and Static Variable Lifecycle in GenericDriver

* **ADR ID:** 0006
* **Status:** Accepted, partially implemented. Amended by [ADR 0008](0008-non-blocking-hid-callbacks-and-control-pipe-recovery.md) (phase 3 addendum).
* **Date:** 2026-09-06
* **Authors:** Antigravity / @maverick1982

## Context and Problem Statement
In `esp32-nut`, how should HID report polling be scheduled across all UPS drivers, how should static configuration parameters be handled versus dynamic telemetries, and how should spurious zero/corrupted packets be filtered?

Currently, `GenericDriver::loop()` iterates through all available HID Feature Report IDs every 2 to 5 seconds, transmitting a burst of `GET_REPORT` requests over Endpoint 0 (Control Transfer). On microcontrollers with constrained or slow USB FIFO engines (e.g. Powercom SPD-750U, Cypress USB controllers), this relentless control traffic causes:
1. **Endpoint 0 buffer exhaustion and starvation**: The UPS microcontroller spends over 60% of its CPU time servicing USB control transfers, starving its internal analog-to-digital converter (ADC) sampling routines.
2. **Telemetry drop to 0**: The UPS begins returning empty or zeroed data bytes (`0x00`), which `GenericDriver::decodeReport()` unconditionally writes into `UPSData` as `0.0`, wiping out real voltage, load, and temperature readings.
3. **Inefficient bus utilization**: Static parameters (e.g. `input.voltage.nominal`, `output.voltage.nominal`, `battery.type`, descriptor strings) never change during operation, yet they are queried repeatedly every few seconds.

In official upstream Network UPS Tools (`nut_repo/drivers/usbhid-ups.c` and `nut_repo/drivers/libhid.c`), all subdrivers (APC, CyberPower, Eaton, Powercom, TrippLite, etc.) share a unified polling architecture:
- **`HU_WALKMODE_INIT`**: Ran once on startup, fetches all variables including static configuration.
- **`HU_WALKMODE_QUICK_UPDATE`**: Ran every `pollinterval` (default 2s), queries only items flagged with `HU_FLAG_QUICK_POLL` (essential status/alarms). Dynamic analog features are completely skipped.
- **`HU_WALKMODE_FULL_UPDATE`**: Ran every `pollfreq` (default 30s, or 12s for CPS), fetches dynamic analog telemetries (voltages, load, temperature), while explicitly skipping all items flagged with `HU_FLAG_STATIC` (`usbhid-ups.c:2352`).
- **Report Caching and Resilience**: `libhid.c` caches reports by `ReportID` and discards failed/corrupted transfers without overwriting the last known good values in `dstate`.

## Considered Options
* **Option 1: Driver-specific ad-hoc polling hacks in `PowercomDriver`.** Keep `GenericDriver` polling all feature reports every 2 seconds, and implement custom timers and workarounds only inside `PowercomDriver`.
* **Option 2: Centralize Two-Tier Polling and Static Lifecycle in `GenericDriver` mirroring official NUT.** Implement the upstream NUT `usbhid-ups` scheduling engine directly in `GenericDriver` for all subdrivers:
  1. Quick Update (2s default): only handles interrupt events and essential keepalive/status.
  2. Full Update (30s default, or subdriver override): steps through dynamic Feature Reports.
  3. Static Lifecycle: nominal voltages and device properties are fetched once at connection/init and never polled again.
  4. Spurious Zero Filter: do not overwrite established valid readings with zero when the device status is online (`OL`).
  5. Subdriver Hook: allow subdrivers to adjust pacing (`getPollPacingMs()`) and full-poll frequency (`getFullPollIntervalMs()`) when hardware quirks require it.

## Decision Outcome
Chosen option: "**Option 2: Centralize Two-Tier Polling and Static Lifecycle in `GenericDriver` mirroring official NUT**", because:
1. It adheres strictly to **ADR 0003** (Faithfully Mirror Official NUT Behavior) and **ADR 0004** (Unify Sub-Driver Inheritance via GenericDriver).
2. Upstream NUT implements this exact scheduling inside `usbhid-ups.c` as a shared engine for all subdrivers, rather than duplicating polling logic per vendor.
3. It protects all supported UPS hardware from USB control pipe exhaustion and eliminates unnecessary bus traffic.
4. It cleanly separates vendor quirks (e.g. Powercom 800ms pacing and custom `0xA4` text report) from core HID scheduling.

### Implementation status (2026-09-26)
The first implementation (`7dbde3e`) never reached `main`: until 1.5.x every driver polled all reports every 2 s. The two-tier engine was implemented with the USB layer review, phase 3 (ADR 0008 addendum), in a different form:
* **Implemented:** points 1 and 2.
  * Quick poll every 2 s of the reports that hold status usages (PresentStatus, RemainingCapacity, RunTimeToEmpty, PercentLoad, ...).
  * Full poll every 30 s: missing strings, then every report, then the strings found during the cycle.
  * 50 ms between requests, and none while `isPollingPaused()`.
  * Report ID 0 included.
* **Implemented differently:** point 5. The driver hooks are `quickPollMs()` (0 = no quick poll), `fullPollMs()`, `acceptPollReport()`, `pollInputReports()`, `buildPollLists()`, `collectStringRequests()`, `upsTypeName()` and `onLoop()`. The request spacing is fixed (`STEP_SPACING_MS`): there is no `getPollPacingMs()` and no Powercom 800 ms pacing (that exists only on the unmerged `feature/issue-36` branch). The drivers use them as follows:
  * **CyberPower:** no quick poll; FEATURE reports only, every 30 s, excluding IDs 4, 6 and >= 130.
  * **Eaton:** excludes reports 254 and 255.
  * **Powercom:** fixed lists, 0x0A every 2 s and 0x0A, 0x1D, 0x21, 0x1F every 30 s. The `0xA4` report is decoded if it arrives but is not requested.
* **Not implemented:**
  * Point 3: only string descriptors are static. Nominal values are re-read at every full poll.
  * Point 4: `GenericDriver` writes every decoded value as received; there is no zero filter. The zeros of issue #36 and #48 turned out to be responses shifted after a control transfer timeout (ADR 0008), not UPS glitches.

## Consequences
### Positive
* **Hardware Stability**: Eliminates USB control transfer saturation, preventing microcontroller lockup and ADC starvation on devices like Powercom SPD-750U.
* **100% Upstream Alignment**: Matches the exact 2s Quick / 30s Full poll intervals and static variable handling of upstream NUT `usbhid-ups`.
* **DRY & Maintainable**: All subdrivers (APCDriver, CyberPowerDriver, EatonDriver, PowercomDriver) inherit optimal polling behavior without duplicated scheduling code.
* **Data Integrity**: Established analog readings remain stable and resilient against transient communication glitches or empty zero packets.

### Negative
* Dynamic telemetry values (such as input/output voltage, load percentage, temperatures) will update every 30 seconds rather than every 2 seconds, which is standard NUT behavior. (Fast status changes like power outages are still received instantly via Interrupt IN transfers).

## Impact on Agent Implementation
* **`GenericDriver` State Machine**:
  - Poll timing comes from the hooks `quickPollMs()` (default 2000 ms, 0 disables the quick poll) and `fullPollMs()` (default 30000 ms, matching NUT `DEFAULT_POLLFREQ`). Agents MUST change a driver's policy only through the hooks listed in "Implementation status", never with a new state machine.
  - String descriptors are static: they are requested only while the key is missing (`collectStringRequests()`), and an index that failed is not asked again for that device. Nominal voltages are not static yet (see "Implementation status").
* **Spurious Zero Protection** (not implemented, see "Implementation status"):
  - In `GenericDriver::decodeReport()`, ensure analog telemetry fields (e.g. `input.voltage`, `output.voltage`, `battery.temperature`, `ups.temperature`) do not overwrite existing valid positive values with `0.0` when the UPS is reported as `OL` (Online).
* **Vendor Quirks Preservation**:
  - Retain `PowercomDriver` custom decoding (`0xA4` report, beeper inverted logic) and its fixed poll lists (`buildPollLists()`).

