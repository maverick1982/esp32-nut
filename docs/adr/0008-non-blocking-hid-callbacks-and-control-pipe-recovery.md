---
type: decision
title: "Non-blocking HID Callbacks, Control Pipe Guarding and Crash Safety Net"
description: "Documento ADR: Non-blocking HID Callbacks, Control Pipe Guarding and Crash Safety Net"
tags: [adr, decision, usb, hid, watchdog, coredump, issue-47]
---
# Non-blocking HID Callbacks, Control Pipe Guarding and Crash Safety Net

* **ADR ID:** 0008
* **Status:** Accepted
* **Date:** 2026-09-24
* **Authors:** @maverick1982, Claude

## Context and Problem Statement
Serial logs from two CyberPower UPSes (issue #47, fix v2) show the same chain in 6 out of 6 events (analysis in `docs/plans/issue-47-usb-ctrl-timeout-crash.md`):

1. **Deadlock.** The loopTask held `_mutex` while blocked in a GET_REPORT. An INPUT report arrived, and its callback, running in the HID host task, blocked on the same `_mutex`. That task is also the one that delivers the control transfer completion, so the GET_REPORT timed out after 5 s.
2. **Corruption.** `hid_control_transfer()` returned on timeout while the URB was still owned by the stack. The late callback left a token in the binary semaphore, so the next requests "completed" immediately with stale data, and the code rewrote the setup packet and buffer of a URB the HCD was still processing. The USB ISR then crashed.
3. **No recovery.** The Arduino core writes a core dump to flash, but the partition table had no `coredump` partition: the panic re-entered itself and sometimes never ended. The `hw_timer` "watchdog" cannot fire during a panic (interrupts masked) and called `esp_restart()` from an ISR.

Constraints verified on the platform (ESP-IDF 5.1 in the pioarduino 51.03.04 Arduino libraries, cross-checked with the IDF 5.5 sources):
* `usb_host_endpoint_halt/flush/clear` reject EP0 (`check_ep_addr()`: *"EP0 is owned/managed by USBH"*), so a client cannot cancel an in-flight control transfer.
* `usb_transfer_t::timeout_ms` is *"currently not supported"*: the HCD never times out a URB by itself.
* `usb_host_transfer_free()` frees a URB without checking whether it is in flight.
* There is no root port reset or power API in IDF 5.1.
* `sdkconfig.defaults` has no effect with `framework = arduino`, because the core is precompiled.

## Considered Options
* **Option 1: Longer timeouts and retries.** It treats the symptom: the deadlock stays, and each overlap still costs 5 s.
* **Option 2: Deferred callbacks, a guarded control pipe, and an explicit recovery ladder.** Fix the cause (the blocking callback), make the vendored `hid_host.c` refuse to touch a URB it does not own, and add bounded application-level recovery.
* **Option 3: Upgrade the platform / hybrid `arduino, espidf` build** to get a custom sdkconfig and newer USB host APIs. Invasive, long builds, and it still would not allow EP0 halt.

## Decision Outcome
Chosen option: "**Option 2**".

**Threading rule.** hid_host callbacks never block. They copy the event (INPUT report bytes, connect, disconnect, transfer error) into a FreeRTOS queue with timeout 0. Two slots are reserved for non-INPUT events, and INPUT reports are dropped and counted when the queue is full. Everything else runs in the loopTask (in the `ups_poll` task since the phase 3 addendum). No application lockis held during a blocking control transfer. `_mutex` only protects `UPSData` and the report cache against readers.

**`hid_host.c` deviations** (marked `[esp32-nut, ADR 0008]`):
* `ctrl_inflight` flag, set on submit and cleared in `ctrl_xfer_done`. `hid_device_lock_ctrl()` rejects any request with `ESP_ERR_INVALID_STATE` while it is set, so neither the setup packet write, the resubmit, nor the realloc in `usb_class_request_get_descriptor` can touch an in-flight URB. The pipe heals by itself when the late callback arrives.
* The semaphore is drained before each submit.
* The transfer status is checked: STALL → `ESP_ERR_INVALID_RESPONSE`, overflow or short → `ESP_ERR_INVALID_SIZE`, other errors → `ESP_FAIL`. Before, a failed transfer with `actual_num_bytes < 8` made `hid_class_request_get()` `memcpy` a negative (huge) length.
* Request lengths are checked against the control buffer size.
* A failed resubmit of the IN transfer raises `HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR` instead of silently stopping INPUT reports.
* `hid_host_device_start()` restores `READY` if the submit fails, so it can be retried.
* `hid_host_uninstall_device()` waits for the request in progress, detaches the interfaces awaiting user deletion (`parent = NULL`, now checked by the getters), and leaks the device context rather than freeing a URB that is still in flight.

**Recovery ladder** (`LinkMonitor`, pure logic, unit tested):
* An answered request, including a STALL, counts as *alive*. No answer (timeout, pipe busy, bus error) counts as a *link failure*.
* After a link failure, Feature Report polling backs off exponentially from 2 s to 30 s, through `isControlPending()`, which every driver already honours (renamed `isPollingPaused()` in phase 3).
* After 20 s without an answer, the data is *stale*: the NUT server returns `ERR DATA-STALE` (like `upsd`) and the web UI flags it.
* After 60 s without an answer: interface restart (`hid_host_device_stop/start`, driver poll cycle reset), at most 2 times, then a controlled `esp_restart()` from the loopTask. The reason is kept in RTC memory and logged at the next boot. An IN transfer error restarts the interface directly (at most 3 times in a row).
* Devices that never issue control requests (`QUIRK_NO_GET_REPORT`) never accumulate failures and are never reset. (Since phases 2-3 they may still issue string descriptor requests, at most once per failing index, and their INPUT pipe is guarded by `InputWatchdog`. `LinkMonitor::setCarriesData(false)` keeps control pipe failures from making their data stale or climbing the recovery ladder.)

**Crash safety net:**
* Task WDT on the loopTask (30 s, panic) instead of the `hw_timer` ISR; since phase 3 also on the `ups_poll` task.
* `coredump` partition at `0x7F0000` (64 KB, fills the 8 MB flash exactly).
* `-Wl,--wrap=esp_core_dump_write`: the wrapper calls the real writer only if the partition exists. This covers boards updated via OTA, which keep the old partition table: the panic completes and the board reboots instead of hanging with the watchdogs disabled.
* At boot: reset reason, last controlled restart cause, and the core dump summary (task, PC, backtrace, only after a crash reset) go to the log and to `/api/system-status`.
* `sdkconfig.defaults` removed.

### Addendum: USB layer review, phase 1 (2026-09-24)
Critical fixes from `docs/plans/usb-layer-review.md` (§4, phase 1).

**More `hid_host.c` deviations** (marked `[esp32-nut, review Cx]`):
* **C2.** `hid_host_device_init_attempt()` no longer closes an uninitialized `dev_hdl` when `usb_host_device_open()` fails, and no longer aborts the HID task through `ESP_ERROR_CHECK`: enumeration errors are logged and cleaned up (interfaces not yet notified are removed, then the device). The `fail:` path of `hid_host_install_device()` frees only its own allocations, because the device is not in the list yet and `dev_hdl` belongs to the caller.
* **C3.** `report_desc_len` keeps the bytes of the report descriptor actually received, and `hid_host_get_report_descriptor()` returns that length instead of `wReportDescriptorLength`. An empty answer is an error.
* **C4.** `hid_host_string_descriptor_copy()` ignores a string descriptor with `bLength < 2` (the length went negative).

**Application changes:**
* **C1.** `setBeeper()` writes the report back only if it was read back up to the beeper field, or if the report carries no other usage (`BeeperLogic::canWriteBack()`). Otherwise the zeros of a blind SET_REPORT could reach `DelayBeforeShutdown` (an immediate `load.off`).
* **C4.** Device strings are converted by `DeviceStrings::toAscii()` (always terminated, non-ASCII → `?`, trailing spaces trimmed, inverted strings detected or forced by `QUIRK_INVERT_STRINGS`) instead of `wcstombs()`.
* **A1.** `isDataStale()` is also true with no device attached, except in the first `USBUPS_NO_DEVICE_BOOT_GRACE_MS` (15 s) after boot while no UPS has been seen yet. NUT clients get `ERR DATA-STALE` instead of an empty `Unknown` status, as with `usbhid-ups` + `upsd`. The web UI keeps showing "Disconnected" without the stale banner.

### Addendum: USB layer review, phase 2 (2026-09-24)
Link robustness from `docs/plans/usb-layer-review.md` (§4, phase 2).

**More `hid_host.c` deviations** (marked `[esp32-nut, review Ax]`):
* **A3.** New `hid_host_device_clear_ep_in_halt()`: CLEAR_FEATURE(ENDPOINT_HALT) on the IN endpoint through EP0, with the same `ctrl_inflight` guard as the class requests.
* **A4.** New `hid_host_device_get_ep_in_mps()`. The IN transfer stays one packet long.
* **A5a.** GET/SET class requests time out after `USBUPS_CTRL_TIMEOUT_MS` (1500 ms) instead of 5 s. String descriptor requests and CLEAR_FEATURE use the same 1500 ms timeout; report descriptor requests and lock waits keep `DEFAULT_TIMEOUT_MS` (5 s). `setBeeper()` waits up to 4 s for the current poll step (language ID + string).

**Application changes:**
* **A2.** `InputWatchdog` (in `LinkMonitor.h`, pure logic) learns the INPUT period from the intervals between bursts. Only for a periodic device, a silence of 5 × period (at least 30 s) makes the data stale and climbs the same recover → recover → restart ladder. Change-driven devices are never periodic and never trigger it.
* **A3.** A recovery after an IN transfer error or an INPUT silence runs `stop`, then CLEAR_FEATURE(ENDPOINT_HALT), then `start`. The IDF HCD does not reset the host data toggle on a pipe clear: the first packet after it may be lost.
* **A4.** `InputReassembler` (pure logic) rebuilds INPUT reports longer than MPS from the per-packet events, using the lengths declared in the report descriptor. A larger IN transfer was rejected: an interrupt IN transfer only ends on a short packet or a full buffer, so a shorter report that is a multiple of MPS would be glued to the next one.
* **A7.** `RestartPolicy` (pure logic) with the count of consecutive controlled restarts in RTC memory (`CrashDiag`): the first restart runs at once, the next ones wait 1, 5 and 15 min, and after 4 the board stays up in degraded mode (data stale, web UI banner). A new enumeration of the UPS cancels the request; 10 min of fresh data clear the count. `isDataStale()` is true while a restart is requested.

### Addendum: USB layer review, phase 3 (2026-09-24)
Architecture from `docs/plans/usb-layer-review.md` (§4, phase 3).

**Threading model (A5b), replacing "everything else runs in the loopTask":** the USB service (events, polling, decoding, recovery) runs in the `ups_poll` task: priority 3, above the loopTask and below the HID task (5) and WiFi, under the Task WDT. The loopTask only serves NUT and the web UI, so a UPS that does not answer no longer delays them. Requests from other tasks (`setBeeper()`) wait for the current poll step on `_op_mutex` (at most 3 s). Lock order: `_op_mutex`, then `_mutex`. `AppLogger` has its own mutex.

**More `hid_host.c` deviations** (marked `[esp32-nut, review Mx]`):
* **M3.** Claim: the interface is released if the IN transfer cannot be allocated. Release: no `ESP_ERROR_CHECK` on the free, and `in_xfer` is cleared.
* **M5.** `usb_class_request_get_descriptor()` takes the recipient. New `hid_host_device_get_string_descriptor()` and `hid_host_device_get_string_indices()`.

**Application changes:**
* **A6 / S3.** One poll state machine in `GenericDriver`: quick poll of the status reports every 2 s, full poll (missing strings, then every report, then the strings found during the cycle) every 30 s, as `usbhid-ups` and ADR 0006. Drivers change the policy through hooks. `DriverRegistry` picks the driver by VID/PID.
* **M1.** Parser: signed fields when Logical Minimum < 0, fields limited to 32 bits, long items, Usage Minimum/Maximum.
* **S2.** `UsageMapIndex` precomputes the usage → mapping match.

### Field validation (2026-09-26)
Firmware `fix-issue-47-v3` (`6a8e871`) on the two CyberPower units of issue #47: 13.9 h and 19.6 h of continuous uptime without reboots, panics or control transfer timeouts, ~96,600 GET_REPORT with 0 failures, 11 UPS-initiated USB resets recovered in ~0.4 s. Details in `docs/plans/usb-layer-review.md` (phase 4). The same deadlock also explains issue #36 (Powercom) and #48 (CyberPower CP1600, APC Back-UPS CS 750).

## Consequences
### Positive
* Removes the root cause of the timeouts: the HID task can always deliver control completions.
* A timed-out transfer can no longer corrupt memory. At worst the pipe is refused until the stack returns the URB.
* The board always leaves a panic (reboot), and a hang in the loopTask becomes a panic with a backtrace.
* Stale data is never served as current to NUT clients.
* Crash details reach the web UI without a serial console.

### Negative
* More deviations from the upstream `usb_host_hid` component (see also ADR 0007), to carry over on every update.
* A control URB that never comes back can only be cleared by a restart. IDF 5.1 has no way to reset the port.
* The `coredump` partition needs a one-time full flash with the web installer, because OTA does not update the partition table. Until then, core dumps are skipped (but panics no longer hang). The web installer and the release package also write `boot_app0.bin` (empty otadata) at `0xe000`: otherwise a board left on `app1` by an odd number of OTA updates keeps booting the old firmware after the full flash (`2a1cea0`).
* INPUT reports are dropped (and counted) if the task draining the event queue (the loopTask, since phase 3 `ups_poll`) stalls for more than ~40 s.The Task WDT fires before that.

## Impact on Agent Implementation
* Agents MUST NOT block, take `_mutex`, log through `AppLogger`, or issue control transfers inside hid_host callbacks (`handle_driver_event`, `handle_interface_event`). They only post to `_event_queue`.
* Agents MUST NOT hold `_mutex` (or any other application lock) across `hid_class_request_*` calls. Take it only to read or modify `UPSData` and the report cache.
* Agents MUST route every control request result through `noteControlResult()` so the `LinkMonitor` sees it, and MUST NOT bypass `isPollingPaused()` (formerly `isControlPending()`) in drivers.
* Agents MUST NOT call `esp_restart()` from an ISR or from a callback. Restarts go through `USBHostUPS::requestRestart()` → `RestartPolicy` in `main.cpp` → `CrashDiag::recordControlledRestart()` → `esp_restart()`.
* Agents MUST NOT free, reallocate or modify `ctrl_xfer` in `hid_host.c` while `ctrl_inflight` is set, and MUST keep the `[esp32-nut, ADR 0008]` markers when updating the vendored component.
* Agents MUST NOT reintroduce `sdkconfig.defaults` expecting it to affect the Arduino build.
* Agents MUST NOT issue a SET_REPORT with bytes that were not read back from the device unless the report holds no other usage (`BeeperLogic::canWriteBack()`), and MUST keep the `[esp32-nut, review Cx]` markers in `hid_host.c`.
* Agents MUST NOT serve UPS data as current with no device attached: `isDataStale()` covers that case.
* Agents MUST decode INPUT reports only after `InputReassembler`, never straight from the per-packet events, and MUST NOT enlarge the IN transfer beyond one packet.
* Agents MUST route controlled restarts through `RestartPolicy` in `main.cpp`: never call `esp_restart()` on `isRestartRequested()` directly.
* Agents MUST NOT call `USBHostUPS` methods that issue control transfers from tasks other than `ups_poll` without `_op_mutex` (see `setBeeper()`), and MUST NOT hold the data lock (`getUPSData()`, `lock()`) while calling them.
* Agents MUST add poll policies to drivers through the `GenericDriver` hooks, never with a new poll state machine.
