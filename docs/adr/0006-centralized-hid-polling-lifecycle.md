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
  * Full poll every 30 s: missing strings, then every report, then the strings found during the cycle. Since US-120 only the FEATURE reports something reads: see "Reports with NUT rows only".
  * 50 ms between requests, and none while `isPollingPaused()`.
  * Report ID 0 included.
* **Implemented differently:** point 5. The driver hooks are `quickPollMs()` (0 = no quick poll), `fullPollMs()`, `acceptPollReport()`, `pollInputReports()`, `buildPollLists()`, `readsFieldOutsideRows()` (US-120), `collectStringRequests()`, `upsTypeName()` and `onLoop()`. The request spacing is fixed (`STEP_SPACING_MS`): there is no `getPollPacingMs()` and no Powercom 800 ms pacing (that exists only on the unmerged `feature/issue-36` branch). The drivers use them as follows:
  * **CyberPower:** no quick poll; FEATURE reports only, every 30 s, excluding IDs 6 and >= 130, and ID 4 on the layouts with vendor reports (#40; read on the others for battery.mfr.date, US-111).
  * **Eaton:** excludes reports 254 and 255.
  * **Powercom:** fixed lists, 0x0A every 2 s and 0x0A, 0x1D, 0x21, 0x1F every 30 s. The `0xA4` report is decoded if it arrives but is not requested.
* **Not implemented:**
  * Point 3: only string descriptors are static. Nominal values are re-read at every full poll. (Since US-072 the semi-static reports are read once: see "Semi-static reports".)
  * Point 4: `GenericDriver` writes every decoded value as received; there is no zero filter. The zeros of issue #36 and #48 turned out to be responses shifted after a control transfer timeout (ADR 0008), not UPS glitches.

### Semi-static reports (US-072, 2026-10-30)
Point 3, as `hid_ups_walk(HU_WALKMODE_FULL_UPDATE)` does it (usbhid-ups.c:2343-2362), once every value comes from a table row (US-079):
* **Classification per report.** A report (type, ID) is semi-static when at least one field of it is read by a bound row (the row's usage or its twin) and every such row has `HU_FLAG_STATIC` or `HU_FLAG_SEMI_STATIC`. Command rows, `HU_FLAG_ABSENT` rows and the rows the INIT rule dropped (`DEAD`) do not count, since upstream never reads them. A report with only such rows, or with no row at all, is not in the full poll since US-120 (see "Reports with NUT rows only"). The active beeper decoded outside the rows (`decodeBeeperStatus()`) keeps its report dynamic. A row still undecided (a value deferred until the conversion inputs are known, as the Eaton nominal values before `mge_type`, or a row waiting behind it) keeps its report dynamic until a read decides it: upstream knows `mge_type` before its INIT walk. `GenericDriver::classifySemiStatic()` runs once the first full poll has ended and `finishInit()` has been applied, again at each full poll while a row is undecided, and after a new bind (new device).
* **Full poll.** `startCycle()` leaves out of the full poll the semi-static reports already decoded. The quick poll, the reports `pollSoon()` puts at the head (Powercom) and the existing `pollStaticReportsOnce()` skip (APC Back-UPS BX, Powercom) are not touched. A report invalidated after a failed read back is read again.
* **After a change.** `IUPSDriver::noteDataChanged()`, called by `USBHostUPS` after a successful HID write of SET VAR (`writePlannedValue()`) and after an INSTCMD that returned OK (`executeCommand()`), is `data_has_changed` (usbhid-ups.c:1157, 1239): the next cycle is a full poll right away, even before 30 s, and reads the semi-static reports too (usbhid-ups.c:1534-1543). Driver-side values (`ups.delay.*`, `HU_FLAG_ABSENT` rows) write nothing to the UPS and do not set it, as upstream.
* **Deviation:** `HU_FLAG_STATIC` and `HU_FLAG_SEMI_STATIC` rows are treated alike, so after a change the static ones are read again too (one extra read per report, only after a SET or an INSTCMD). Upstream reads only the semi-static ones again.
* **Accepted effect, as upstream:** a semi-static value changed outside NUT (front panel, the manufacturer's software) shows only after the next SET VAR, INSTCMD or reconnection.
* **Without the interrupt pipe** (`QUIRK_NO_INTERRUPT_PIPE`, APC 5G 051d:0003/0004, US-108): "pollonly" upstream, where usbhid-ups.c:2352, 2360 skip the `HU_FLAG_STATIC` / `HU_FLAG_SEMI_STATIC` rows only with `use_interrupt_pipe` and use their reads to detect stale data. `startCycle()` then reads the semi-static reports at every full poll.
* **Effect:** over the 24 fixtures the full poll at steady state goes from 827 to 773 reports; on the Eaton 3S from 24 to 16 (16 requests a minute fewer). The replay never reads again the reports of the Eaton rows deferred at INIT (16 and 18); on the device a later read decides them and the full poll drops to 14 reports. The Back-UPS BX already skipped its static reports: unchanged.

### Reports with NUT rows only (US-120, 2026-11-05)
`hid_ups_walk()` (usbhid-ups.c:2232-2340) reads the table row by row. `HIDGetDataValue()` asks for the report of the row, and libhid keeps one buffer per report, so a report that no row reads is never requested. The INIT walk reads every row found in the descriptor except the `HU_FLAG_ABSENT` ones. It reads the command rows too, to see whether they exist. It drops a row whose variable an earlier row already set (`hiddata = NULL`). The full update skips `HU_FLAG_ABSENT | HU_TYPE_CMD`. Issue #123: the APC Back-UPS BXR1200MI answered FEATURE report 227 empty at every full poll. That report holds only the vendor field `UPS.0xFFFF00FF.0xFFFF00EF`, which no row reads, and each request logged `requestReport FAILED: type=3, id=227, empty response`.
* **Rule.** `GenericDriver::buildPollLists()` puts a FEATURE report in the full poll only when it has a reader. A reader is one of:
  * a field (the usage or its twin) of a bound row that is not `HU_FLAG_ABSENT`. Until `finishInit()` every other bound row counts, command rows included: that is the INIT walk. After `finishInit()`, the command rows and the `DEAD` rows no longer count;
  * the active beeper decoded outside the rows (`decodeBeeperStatus()`), when the driver's table has no `ups.beeper.status` row;
  * a field the driver declares with `readsFieldOutsideRows()`. Eaton declares iModel (`UPS.PowerSummary.0xFFFF00F0`) and `UPS.Flow.[4].ConfigApparentPower`, which `EatonDriver::decodeReport()` reads for `mge_type`.
* **When.** `loop()` binds the rows before the first lists. Before, they were bound at the first decoded report. Once `finishInit()` has run, `pruneFullPoll()` builds the lists once more and logs the reports left out once (debug level): `Full poll: N FEATURE reports left out (no NUT row reads them): <id> ...`. A report with only `DEAD` or command rows counts as unread, like one without rows. Without bound rows, every report is read as before.
* **Reads outside the rows, driver by driver.** GenericDriver and every driver: the active beeper. Eaton: the hook above. CyberPower and OpenUPS read no field outside the rows, only scales from the product ID. `APCDriver::onLoop()` reads `ups.model`. Powercom reads report 0x0A raw and the text of 0xA4, but builds its own fixed lists (`buildPollLists()` without the base), which are unchanged. Legrand, Liebert and EcoFlow: only the beeper of the base. The "Kept" rows of ADR 0011 rule 2 are rows of the generic tail, so they are bound rows like the others.
* **Unchanged:** the quick poll (a status report without readers stays in it), the INPUT reports from the interrupt pipe and the GET_REPORT INPUT of `pollInputReports()`, the exclusions of `acceptPollReport()` (CyberPower, Eaton), the semi-static reports (US-072), `pollStaticReportsOnce()` and the quick poll every 10 s of the Back-UPS BX (#60), the APC 5G without the interrupt pipe (US-108), `pollSoon()`, and the on-demand read of the USB diagnostics export (US-119, `acceptDiagReport()`), which still reads every FEATURE report. SET VAR, beeper and INSTCMD writes read their report themselves before writing (read-modify-write), so they do not depend on the poll.
* **Deviation:** upstream keeps `hiddata` for a row whose INIT read failed and reads it again at every full update. Here such a row is `DEAD` for good (the ADR 0011 INIT rule: `First::FAIL` never changes), so reading its report again would publish nothing, and the report leaves the full poll like the other `DEAD` rows. The same holds for a row whose report did not answer during the first full poll (`finishInit()` turns it into `FAIL`).
* **Effect** (`docs/test-results/US-120/full-poll-reports.md`, golden `[requests]`): only the request lines of the golden files change. The snapshots, LIST CMD and LIST RW of every fixture are the same. With the registry's driver, the full poll at steady state goes from 1000 to 515 requests over the 31 fixtures, and the requests in the 60 s window from 6142 to 5168. On the BXR1200MI, report 227 is gone, as are 14 and 120. On the Eaton 3S the full poll goes from 16 to 11 reports.

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
  - String descriptors are static: they are requested only while the key is missing (`collectStringRequests()`), and an index that failed is not asked again for that device. Nominal values and limits are read once as semi-static reports (US-072, see "Semi-static reports"): a driver changes nothing to get it, the flags of its table rows decide.
* **Spurious Zero Protection** (not implemented, see "Implementation status"):
  - In `GenericDriver::decodeReport()`, ensure analog telemetry fields (e.g. `input.voltage`, `output.voltage`, `battery.temperature`, `ups.temperature`) do not overwrite existing valid positive values with `0.0` when the UPS is reported as `OL` (Online).
* **Vendor Quirks Preservation**:
  - Retain `PowercomDriver` custom decoding (`0xA4` report, beeper inverted logic) and its fixed poll lists (`buildPollLists()`).

