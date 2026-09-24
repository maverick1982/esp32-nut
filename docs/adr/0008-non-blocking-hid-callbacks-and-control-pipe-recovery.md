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

**Threading rule.** hid_host callbacks never block. They copy the event (INPUT report bytes, connect, disconnect, transfer error) into a FreeRTOS queue with timeout 0. Two slots are reserved for non-INPUT events, and INPUT reports are dropped and counted when the queue is full. Everything else runs in the loopTask. No application lock is held during a blocking control transfer. `_mutex` only protects `UPSData` and the report cache against readers.

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
* After a link failure, Feature Report polling backs off exponentially from 2 s to 30 s, through `isControlPending()`, which every driver already honours.
* After 20 s without an answer, the data is *stale*: the NUT server returns `ERR DATA-STALE` (like `upsd`) and the web UI flags it.
* After 60 s without an answer: interface restart (`hid_host_device_stop/start`, driver poll cycle reset), at most 2 times, then a controlled `esp_restart()` from the loopTask. The reason is kept in RTC memory and logged at the next boot. An IN transfer error restarts the interface directly (at most 3 times in a row).
* Devices that never issue control requests (`QUIRK_NO_GET_REPORT`) never accumulate failures and are never reset.

**Crash safety net:**
* Task WDT on the loopTask (30 s, panic) instead of the `hw_timer` ISR.
* `coredump` partition at `0x7F0000` (64 KB, fills the 8 MB flash exactly).
* `-Wl,--wrap=esp_core_dump_write`: the wrapper calls the real writer only if the partition exists. This covers boards updated via OTA, which keep the old partition table: the panic completes and the board reboots instead of hanging with the watchdogs disabled.
* At boot: reset reason, last controlled restart cause, and the core dump summary (task, PC, backtrace, only after a crash reset) go to the log and to `/api/system-status`.
* `sdkconfig.defaults` removed.

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
* The `coredump` partition needs a one-time full flash with the web installer, because OTA does not update the partition table. Until then, core dumps are skipped (but panics no longer hang).
* INPUT reports are dropped (and counted) if the loopTask stalls for more than ~40 s. The Task WDT fires before that.

## Impact on Agent Implementation
* Agents MUST NOT block, take `_mutex`, log through `AppLogger`, or issue control transfers inside hid_host callbacks (`handle_driver_event`, `handle_interface_event`). They only post to `_event_queue`.
* Agents MUST NOT hold `_mutex` (or any other application lock) across `hid_class_request_*` calls. Take it only to read or modify `UPSData` and the report cache.
* Agents MUST route every control request result through `noteControlResult()` so the `LinkMonitor` sees it, and MUST NOT bypass `isControlPending()` in drivers.
* Agents MUST NOT call `esp_restart()` from an ISR or from a callback. Restarts go through `USBHostUPS::requestRestart()` → `main.cpp` → `CrashDiag::recordControlledRestart()`.
* Agents MUST NOT free, reallocate or modify `ctrl_xfer` in `hid_host.c` while `ctrl_inflight` is set, and MUST keep the `[esp32-nut, ADR 0008]` markers when updating the vendored component.
* Agents MUST NOT reintroduce `sdkconfig.defaults` expecting it to affect the Arduino build.
