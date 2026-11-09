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
* After 60 s without an answer: interface restart (`hid_host_device_stop/start`, driver poll cycle reset), at most 2 times, then a controlled `esp_restart()` from the loopTask. The reason is kept in RTC memory and logged at the next boot. An IN transfer error restarts the interface directly (at most 3 times in a row), except with `QUIRK_NO_INTERRUPT_PIPE` (US-108).
* A device that issues no control request never accumulates failures and is never reset. (US-108: no device is in this case any more. The APC 5G, once read only from its INPUT reports (issue #39), reads every value with GET_REPORT under `QUIRK_NO_INTERRUPT_PIPE`, so the timeouts and the recovery ladder of this ADR guard the freeze of issue #39; its INPUT reports are dropped, `InputWatchdog` never acts and an INPUT transfer error is logged without a recovery or restart. `LinkMonitor::setCarriesData()` is gone.)

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
* **A1.** `isDataStale()` is also true with no device attached. Until issue #60 the first `USBUPS_NO_DEVICE_BOOT_GRACE_MS` (15 s) after boot were exempt; now the data is stale until the first full poll after a claim (see the #60 addendum). NUT clients get `ERR DATA-STALE` instead of an empty `Unknown` status, as with `usbhid-ups` + `upsd`. The web UI keeps showing "Disconnected" without the stale banner.

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
* **A7.** `RestartPolicy` (pure logic) with the count of consecutive controlled restarts in RTC memory (`CrashDiag`): the first restart runs at once, the next ones wait 1, 5 and 15 min, and after 4 the board stays up in degraded mode (data stale, web UI banner). A new enumeration of the UPS cancels the request; fresh data clear the count: 30 s with the control pipe proven, 2 min otherwise (10 min until issue #60, see its addendum). `isDataStale()` is true while a restart is requested.

### Addendum: USB layer review, phase 3 (2026-09-24)
Architecture from `docs/plans/usb-layer-review.md` (§4, phase 3).

**Threading model (A5b), replacing "everything else runs in the loopTask":** the USB service (events, polling, decoding, recovery) runs in the `ups_poll` task: priority 3, above the loopTask and below the HID task (5) and WiFi, under the Task WDT. The loopTask only serves NUT and the web UI, so a UPS that does not answer no longer delays them. Requests from other tasks (`setBeeper()`) wait for the current poll step on `_op_mutex` (at most 3 s). Lock order: `_op_mutex`, then `_mutex`. `AppLogger` has its own mutex.

**More `hid_host.c` deviations** (marked `[esp32-nut, review Mx]`):
* **M3.** Claim: the interface is released if the IN transfer cannot be allocated. Release: no `ESP_ERROR_CHECK` on the free, and `in_xfer` is cleared.
* **M5.** `usb_class_request_get_descriptor()` takes the recipient. New `hid_host_device_get_string_descriptor()` and `hid_host_device_get_string_indices()`.

**Application changes:**
* **A6 / S3.** One poll state machine in `GenericDriver`: quick poll of the status reports every 2 s, full poll (missing strings, then every report, then the strings found during the cycle) every 30 s, as `usbhid-ups` and ADR 0006. Drivers change the policy through hooks. `DriverRegistry` picks the driver by VID/PID.
* **M1.** Parser: signed fields when Logical Minimum < 0, fields limited to 32 bits, long items, Usage Minimum/Maximum.
* **S2.** `UsageMapIndex` precomputes the usage → mapping match (removed with the hand-written mappings by US-079: the NUT table rows are bound once per device).

### Field validation (2026-09-26)
Firmware `fix-issue-47-v3` (`dd2a740`) on the two CyberPower units of issue #47: 13.9 h and 19.6 h of continuous uptime without reboots, panics or control transfer timeouts, ~96,600 GET_REPORT with 0 failures, 11 UPS-initiated USB resets recovered in ~0.4 s. Details in `docs/plans/usb-layer-review.md` (phase 4). The same deadlock also explains issue #36 (Powercom) and #48 (CyberPower CP1600, APC Back-UPS CS 750).

### Addendum: stuck control transfer and fewer requests (issue #60, 2026-09-30)
Logs from an APC Back-UPS BX750MI (v1.6.1): after 1-2 minutes of polling a GET_REPORT times out and the callback never comes, so every later request gets `ESP_ERR_INVALID_STATE`. INPUT reports keep flowing (status, charge). The two interface restarts of the ladder fail at the first request, and only the controlled restart (bus reset) brings the UPS back, about 3.5 minutes after the first error.

**More `hid_host.c` deviations** (marked `[esp32-nut, issue 60]`):
* New `hid_host_device_ctrl_stuck()`: `ctrl_inflight` read outside a request, i.e. a timed-out URB still owned by the stack. `USBHostUPS::service()` calls it under `_op_mutex`, so none of its own requests is in progress.

**Application changes:**
* `LinkMonitor::tick(now, ctrlStuck)`: with a stuck transfer, after `stuckRestartMs` (30 s) the monitor asks for the restart directly and skips the interface restarts, which cannot free EP0.
* `GenericDriver` skips a report whose values all came in an INPUT report younger than `maxReportAgeMs()` (2 s), like `refresh_report_buffer()` in `libhid.c`. A FEATURE report counts only if its INPUT twin (same ID) carries all its usages.
* `APCDriver`: for `ups.model` starting with "Back-UPS BX", quick poll and report age are 10 s, like the `pollinterval = 10` their users set in `usbhid-ups`. GET_REPORT asks at least 8 bytes instead of the declared length (`GenericDriver::requestLength()`), like the `usbhid-ups` `maxreport` flag, a tweak "for buggy APC Back-UPS firmware" that returns a wrong report size. With it `libhid.c` asks `sizeof(rbuf->data[id])` bytes, i.e. the size of a pointer. ADR 0007 exact lengths stay the rule for every other device.

**Second round (2026-10-01).** With these changes the BX750MI ran 15-90 minutes between lockups instead of 1-2 (102 GET_REPORT/min), and each controlled restart brought it back, 16 times in a day. Two gaps were left:
* `RestartPolicy` cleared its count after 10 min of fresh data. A lockup 5 minutes after a restart waited 1 or 5 more minutes with stale data (the 60 s and 300 s gaps seen in Home Assistant), and four close lockups would have left the board in degraded mode. The count is now cleared after 2 min (`HEALTHY_RESET_MS`): a restart followed by 2 minutes of fresh data worked, so the next one runs at once. A device that fails again within 2 minutes still walks the 1/5/15 min ladder.
* On the BX the full poll read 42 reports, half of them static (nominal values, limits, dates, string indices, vendor usages). With `GenericDriver::pollStaticReportsOnce()` (APCDriver: "Back-UPS BX" only) a report with no dynamic usage (`isDynamicUsage()`: status, measures, timers, test, beeper) is left out of the full poll once decoded, like `HU_FLAG_STATIC` in `usbhid-ups` with the interrupt pipe. A new enumeration reads it again, and one never answered is asked at every full poll. On the BX750MI descriptor: 50 GET_REPORT/min.
* `EVENT_QUEUE_LEN` 16 → 48: during the 1.5 s of a timed-out GET_REPORT the BX sent ~27 INPUT reports, 29 were dropped.

**Third round (2026-10-05).** With 50 GET_REPORT/min the BX750MI still locked up about 1.4 times an hour (17 in 12.5 h, from 65 s to 4 h after a boot): the lockup rate does not follow the request rate. On Linux the kernel cancels the timed-out URB and NUT goes on; here only a restart frees EP0. The goal became a recovery NUT clients barely see:
* `LinkMonitor` asks for the stuck restart `stuckRestartMs` (3 s, was 30 s) after the *first failure*, not after the last answer. That is before `staleAfterMs` (20 s): meanwhile the INPUT reports keep status, charge and LB current, so clients only miss the reboot.
* `RestartPolicy::update(..., proven)`: `USBHostUPS::isControlPipeProven()` (a GET_REPORT answered and the first full poll ended since the claim, no link failure) clears the count after `PROVEN_RESET_MS` (30 s) of fresh data. Without it (INPUT-only devices) `HEALTHY_RESET_MS` (2 min) stays. A device failing within 30 s of every boot still reaches degraded mode.
* `isDataStale()` is true until the first full poll after a claim (`IUPSDriver::initialPollDone()`, plus a known status), at most `FIRST_DATA_MAX_MS` (30 s), like `upsd` while `usbhid-ups` runs `upsdrv_initinfo()`. Before, NUT served an empty status right after the boot (Home Assistant: `unknown`). The 15 s boot grace of A1 is gone, so without a device the data is always stale. The web UI hides the stale banner while the first poll runs (`isWaitingFirstData()`).
* `NUTServer` publishes `device.type` ("ups") and `device.mfr/model/serial` copied from `ups.*`, as `drivers/main.c` does: the OMV NUT plugin and an Android app did not show the model.

### Addendum: device strings (issue #76, 2026-10-08)
`hid_host_dev_info_t` cut every device string at 31 characters: an APC Back-UPS ES 700G showed `ups.model = "Back-UPS ES 700G FW:871.O4 .I U"`.

**More `hid_host` deviations** (marked `[esp32-nut, issue 76]`):
* `HID_STR_DESC_MAX_LENGTH` 32 → 64, like the model buffer of `apc-hid.c`. `wchar_t` is 2 bytes on the S3 toolchain: 192 more bytes on the poll task stack during the claim.

**Application changes:**
* New `IUPSDriver::formatDeviceStrings()`, called after the strings are read at claim time and after every string descriptor, like the `format_model` hooks of the NUT subdrivers. It must be idempotent.
* `APCDriver` splits the product string like `apc_format_model()`: `ups.model` before `FW:`, `ups.firmware` up to `USB FW:`, `ups.firmware.aux` after it. `device.model` follows `ups.model`, and `isBackUpsBX()` still matches the split model.

### Addendum: on-demand read of the diagnostics export (US-119, issue #123, 2026-11-05)
The USB diagnostics export (`/api/usb/dump`) carried only the reports the poll had cached. When the user asks for an export, the board now reads once every FEATURE report of the descriptor that is not cached, so the export also shows the reports no row reads (US-120 will drop them from the full poll).

* **Where it runs.** In the `ups_poll` task, never in the loopTask: `POST /api/usb/dump/read` only raises a flag (`requestDiagRead()`), `service()` asks one report per iteration after `_driver->loop()`, so the poll goes first when both are due. The page follows the read with `GET /api/usb/dump/read` and then downloads the export.
* **Spacing.** `noteControlResult()` records the end of every control request. A step of the read starts only `requestSpacingMs()` (the poll's 50 ms) after it, and right after a step of the read `_driver->loop()` waits for the same spacing: two requests are never closer than in the poll.
* **No decoding, no cache.** `readReportRaw()` runs the GET_REPORT without any application lock, counts it in the GET statistics and passes it to `noteControlResult()`, so the thresholds and the recovery ladder above see it as any other request. The answer is neither decoded nor written to the report cache or `UPSData`. An empty answer is recorded as empty, with no ERROR line and not as a failure: the `[USB] Last 60 s` line counts it apart (`, N empty (export read)`, only when N > 0). A failure gets at most a DEBUG line.
* **Exclusions.** The reports the driver never polls (`acceptPollReport()`: CyberPower 6 and >= 130, Eaton 254/255) are not read either (`IUPSDriver::acceptDiagReport()`).
* **Stop.** The read becomes `interrupted` with the reports read so far when the link no longer allows requests (`!canPoll()`, interface restart pending, restart requested); a disconnection or a new claim forgets it. One INFO line at the end: `[USB] Export read: ...`.

### Addendum: UPS not enumerated after an automatic restart (US-121, issues #78 and #120, 2026-11-05)
Two Smart-UPS 750 (#78) stayed absent for hours after a controlled restart: an export with VID 0, a single restart in the banner. A later software restart (OTA) found the UPS at once, and so does replugging the cable.

* **Cause (ESP-IDF 5.5.5).** A failed enumeration stage moves the enum driver to `ENUM_STAGE_CANCEL`. `ENUM_EVENT_CANCELED` (`usb_host.c:401`) calls `hub_port_disable()` → `root_port_disable()` (`hub.c:504`): the HCD port becomes `DISABLED`, while the hub keeps `ROOT_PORT_STATE_ENABLED` and the device tree node. Only a `HCD_PORT_EVENT_DISCONNECTION`/`ERROR` recovers the port (`hub.c:412-437`), so nothing retries. Nothing asks `RestartPolicy` for a restart either, since no device is attached to fail: the 1/5/15 min ladder stops.
* **`EnumWatchdog` (pure logic).** `main.cpp` passes the consecutive restart count to `USBHostUPS::setAutomaticRestartCount()` before `begin()`. Above 0, `begin()` arms the watchdog. If no device enumerates, `service()` (poll task) power-cycles the root port after 1 and 2 minutes (`FIRST_CYCLE_MS`, `CYCLE_INTERVAL_MS`, `MAX_PORT_CYCLES = 2`), with a WARN line each time: `[USB] UPS not enumerated N min after the automatic restart: power-cycling the USB port (attempt n of 2)`. One minute after the last cycle it calls `requestRestart("UPS not enumerated after the automatic restart")`, and `RestartPolicy` applies its usual delays and degraded mode, unchanged. Enumerated means a `CONNECTED` or `OPEN_FAILED` event of the HID driver since the boot, or `usb_host_lib_info().num_devices > 0`. The first enumeration disarms the watchdog for good, and `claimInterface()` cancels a pending request as before. With a count of 0 the watchdog only power-cycles the port, without a restart: see the US-139 addendum.
* **Port power cycle.** `usb_host_lib_set_root_port_power(false)`, 500 ms, `usb_host_lib_set_root_port_power(true)`, as in the IDF host tests (`test_usb_host_plugging.c`, `msc_client_async_enum.c`). With the hub state `NOT_POWERED`, a disconnection frees the device node (`port_has_device`), and once powered again the port is `DISCONNECTED`, so an attached device raises a new connection, a reset and an enumeration. The power bit does not switch VBUS on these boards: the UPS stays powered, as across `esp_restart()`. An error of either call is logged as WARN, and there is no power on after a failed power off (`hub_root_start()` requires `NOT_POWERED`).
* **Web UI.** `CrashDiag::setDegraded(degraded, upsNotEnumerated)` with `USBHostUPS::isNotEnumeratedRestart()`. `/api/system-status` adds `diagnostics.ups_not_enumerated` in degraded mode only, and the banner then says that the UPS did not come back after the automatic restart and asks to unplug and replug the USB cable.
* **Why a failed enumeration is not in the System Log.** The public USB Host API has no event for it: `ENUM_EVENT_CANCELED` stays inside `usb_host.c`, the library flags are only `NO_CLIENTS` and `ALL_FREE`, and the `ESP_LOGE(ENUM_TAG, ...)` lines of `enum.c` go to the serial port only. Catching them would need a global `esp_log_set_vprintf()` hook that parses the text of every IDF log line from any context: rejected as fragile and invasive. The System Log shows the observable effect instead, the WARN lines above.
* **Open risks.** It is not proven that the port cycle alone brings the UPS back: the restarts stay as the safety net. If powering the port off did not raise the disconnection, the node of the cancelled device would stay in the device tree. A non-HID device counts as enumerated through `num_devices`. The Eaton 3S on the bench does not reproduce the lockup: the real case is for the #78 reporter to confirm with a test build.
* **Test build.** `PLATFORMIO_BUILD_FLAGS=-DNUT_TEST_ENUM_WATCHDOG=1` arms the watchdog of an automatic restart on a normal boot (count 0) and ignores the enumerations. With a UPS attached, every cycle detaches and reattaches it, and after 3 minutes the board restarts; the next boot (count 1) follows the normal rules, so the loop ends by itself.

### Addendum: stuck control transfer, USB port power cycle instead of a restart (US-122, issue #120, 2026-11-05)
The serial log of a Smart-UPS 1500 (FW 653.13.I, board v1.13.0, #120) shows 13 controlled restarts in about 2 hours, from 28 s to 30 min apart. Each time one FEATURE GET_REPORT times out (a different report every time: 9, 11, 29, 30, 32, 42, 44), the next request finds `Control pipe not available`, and 3 s later `LinkMonitor` asks for the stuck restart. Right after the restart the UPS answers normally. The same UPS worked with NUT on a PC, where libusb abandons the timed-out request.
* **Device gone frees the URB (ESP-IDF 5.5.5).** When an opened device goes away, `usbh.c:911-918` sets `EPn_HALT_FLUSH | EP0_FLUSH | EP0_DEQUEUE | PROP_GONE_EVT`. The actions run in that order (`usbh.c:745-760`): `handle_ep0_dequeue()` (`usbh.c:537-560`) hands every URB of EP0 back as a `USBH_EVENT_CTRL_XFER`, and only then `DEV_GONE` is propagated. `usb_host_client_handle_events()` keeps the order: it runs the done control transfer callbacks first (`usb_host.c:940-951`) and the client events after (`usb_host.c:953-959`). The callback of our `hid_host.c` (~line 1062) clears `ctrl_inflight` and gives the semaphore, so at `DEV_GONE` the `Control transfer still in flight on device removal, leaking device context` branch (~line 1516) is not expected to run. Switching the root port off is enough to make the device go away (US-121 showed it on the Eaton 3S: disconnected, connected again, data served in about 6 s).
* **`PortCyclePolicy` (pure logic).** When `LinkMonitor::tick()` returns `RESTART` with `ctrlStuck`, `USBHostUPS::recoverStuckTransfer()` asks `onStuck()`:
  * `CYCLE_PORT`: WARN `[USB] Control transfer stuck: power-cycling the USB port (cycle N since boot)`, `_is_ready_to_poll = false` under the data mutex (data stale at once, no request to a device that is going away), `_link.reset()`, then `cycleRootPort()` (off, 500 ms, on). The disconnection and the new claim follow the normal path. When the control pipe is proven again (`isControlPipeProven()`), INFO `[USB] UPS back after the USB port power cycle in N ms`.
  * `RESTART`, the safety net, through `requestRestart()` and `RestartPolicy` as before: stuck again before the pipe was proven after the last cycle (`control transfer stuck again after the USB port power cycle`), or 12 cycles already in the last hour (`too many USB port power cycles in an hour`). A UPS not proven within 30 s of the cycle (`BACK_TIMEOUT_MS`) also asks for the restart (`UPS not back after the USB port power cycle`); if it does not enumerate after that restart, US-121 takes over.
  * The 12 per hour leave room for the #120 log (at most 10 lockups in an hour), replayed in the native tests.
  * `RESTART` without `ctrlStuck` (pipe not answering after the interface restarts) is unchanged, and so is `LinkMonitor`.
* **Counters.** The `[USB] Last 60 s` line adds `, N USB port power cycles` only when there were some in the period, and the diagnostics export adds `link.port_cycles` (cycles since the boot).
* **Test build.** `PLATFORMIO_BUILD_FLAGS=-DNUT_TEST_STUCK_CYCLE=1`: 90 s after the control pipe is proven, the poll task logs `[TEST] Simulated stuck control transfer`, records a link failure, reports `ctrlStuck` and stops polling, so 3 s later the real port cycle starts. With a cycle every ~100 s, the 13th within the hour gets the restart, which tests the safety net too.
* **Open risks.** The test build does not hold a real URB in flight: the order above comes from the sources and is for the #120 reporter to confirm. The heap after repeated cycles is checked on the bench. A UPS that sometimes does not re-enumerate after the cycle falls back to the restart.

### Addendum: INPUT watchdog only for values the poll does not read (US-123, issue #111, 2026-11-06)
An MGE Protection Center (#111, v1.13.0) sends one INPUT report a minute while it answers every GET_REPORT (`[USB] Last 60 s: 1 INPUT reports (1 packets), 295 GET_REPORT ok, 0 failed`). The watchdog of A2 took it for periodic (60 s). After 300 s without a report it made the data stale (Home Assistant "unavailable"), recovered the interface twice and restarted the board: `INPUT reports stopped after recovery`, about 3 minutes without data each time. Every INPUT field of that descriptor has a FEATURE twin in the poll, and the poll requests a report again once its INPUT twin is older than `maxReportAgeMs()`: nothing froze. `usbhid-ups` has no INPUT watchdog at all (ADR 0003).
* **Coverage in the driver.** `IUPSDriver::inputValuesPolled()` (default false) is computed by `GenericDriver::computeInputCoverage()` after every `buildPollLists()` (first build and the pruned lists of US-120). The readers are those of `featureReportsRead()`: the fields (usage and twin) of each bound row that is not `HU_FLAG_ABSENT` (nor, after the INIT walk, a command or DEAD row), the active beeper decoded outside the rows, `readsFieldOutsideRows()`. A reader depends on the interrupt pipe when it has an INPUT field and none of its fields, INPUT or FEATURE, is in a report of the quick or full list. Covered = no reader depends on it; false while the rows are not bound. The lists count, not the skipping of the semi-static and static reports: those values do not change at run time.
* **Watchdog.** `USBHostUPS::service()` passes the coverage with `InputWatchdog::setValuesPolled()` before each `tick()`. Covered: `isStale()` is false, `tick()` returns `NONE`, and `takeSilenceNotice()` gives one INFO line per silence: `[USB] No INPUT report for N s (period N ms), values still read with GET_REPORT`. The period is still learned. Not covered: the ladder of A2, unchanged. `reset()` (new device) clears the coverage until the new driver computes it.
* **Fixtures.** On all 31 fixtures, with the registry's driver and with `GenericDriver`, the values are covered, so the watchdog never acts on the known devices. The CS500 reads its INPUT-only report 51 with GET_REPORT INPUT. The one exception is the CyberPower UT850EG, on which `battery.runtime.low` comes from vendor report 130: `CyberPowerDriver::acceptPollReport()` never requests it, so only INPUT report 130 carries the value and the watchdog stays active there.
* **Export.** `link.input_watchdog`: true when an INPUT silence would make the data stale and recover, then restart; false when covered or with `QUIRK_NO_INTERRUPT_PIPE`.
* **Rejected.** Removing `InputWatchdog`: it guards devices with values only in INPUT reports the driver does not request (the UT850EG). A single recovery without escalation when covered: on the v1.12 test build of #111 that recovery left the MGE disconnected until the cable was replugged.

### Addendum: UPS not enumerated after a boot that was not automatic (US-139, issue #78, 2026-11-08)
On v1.14.0 a Smart-UPS 750 (#78) did not show up for 2 h 50 min after a boot with `rst:0x1 (POWERON)`: no ENUM error, no recovery line, since `EnumWatchdog` was armed only with a consecutive restart count above 0. Replugging the cable brought it back in about 3 s. The cause is the one of US-121 (a cancelled enumeration disables the root port and nothing retries); only the kind of boot differs.
* **Two modes.** `begin()` now arms the watchdog at every boot, with `EnumWatchdog::modeFor(count)`: `AFTER_AUTO_RESTART` above 0 (US-121, unchanged), `AFTER_NORMAL_BOOT` at 0 (power on, EN pin, OTA, settings saved). In `AFTER_NORMAL_BOOT` the same two root port cycles run at 1 and 2 minutes, and the second disarms the watchdog: no `REQUEST_RESTART`, so no restart, no degraded mode, no `diagnostics.ups_not_enumerated` banner and no new diagnostics field. Each cycle logs an INFO line, `[USB] No UPS enumerated N min after boot: power-cycling the USB port (n/2)`. The first enumeration disarms it as before.
* **Why no restart and no banner.** A board powered on without a UPS goes through these two cycles every time, and a cycle on an empty port has no visible effect: an INFO line is all it should leave. This refines the decision of 2026-11-06 (no cycles and no restarts for a UPS unplugged on purpose), which is about detaches while running: a UPS that goes away after it enumerated still re-arms nothing.
* **Test build.** `PLATFORMIO_BUILD_FLAGS=-DNUT_TEST_ENUM_WATCHDOG=2` arms the `AFTER_NORMAL_BOOT` watchdog on a normal boot and ignores the enumerations: with a UPS attached, it detaches and comes back twice, and the board does not restart.
* **Open risks.** A root port cycle with nothing attached relies on the IDF sources (`hub_root_stop()` from a powered port, `hub_root_start()` back to `DISCONNECTED`); on the bench only a cycle with the UPS attached is checked. Whether the cycle brings a Smart-UPS back after a cancelled enumeration is for the #78 reporter to confirm.

### Addendum: frozen values of a Back-UPS BX (issue #138, 2026-11-09)
A Back-UPS BX750MI on v1.14.0 (#138) kept serving the same values for hours: `[USB] Last 60 s: 0 INPUT reports, 58 GET_REPORT ok, 0 failed`, while the same UPS normally sends about 1110 INPUT reports a minute (#60). The reporter confirmed that every value stays frozen while the board keeps talking to the UPS, and that only unplugging the cable clears it. The UPS answers GET_REPORT with the values of the moment its interrupt pipe went silent. Neither guard saw it: its continuous stream (one report every ~55 ms, under `burstGapMs`) is one endless burst, never periodic for A2, and its INPUT values are covered by the poll (US-123). Before v1.14.0 the restarts of #60 cleared the state; US-122 replaced most of them.
* **Guard.** `InputSilenceGuard` (pure logic) with the silence from `IUPSDriver::inputSilenceFaultMs()` (default 0 = off). `APCDriver` returns `BX_INPUT_SILENCE_MS` (60 s) for a model that `isBackUpsBX()` and not `isBackUpsBXR()`: the BXR1200MI (#123) keeps sending INPUT reports and its reporter sees no freeze. Armed by the first INPUT report after the claim (`reset()` in `claimInterface()`), so a UPS whose interrupt pipe never carried data, or `QUIRK_NO_INTERRUPT_PIPE`, never triggers it.
* **Ladder.** After 60 s of silence the data is stale (`isDataStale()`), the interface restarts (with CLEAR_FEATURE(ENDPOINT_HALT)) with `[USB] No INPUT report for N s: UPS values likely frozen`; if no report arrives in the next `RETRY_MS` (60 s), `requestRestart("INPUT reports stopped, values frozen")`, still limited by `RestartPolicy`. An INPUT report ends the silence and the ladder.
* **Rejected.** Stopping BX support at v1.13 (the reporter's suggestion): v1.13 restarted the board about 16 times a day on these models. A root port cycle before the restart: on the S3 it does not cut VBUS, and whether it clears this state is unknown, while the restarts of v1.13 did.

## Consequences
### Positive
* Removes the root cause of the timeouts: the HID task can always deliver control completions.
* A timed-out transfer can no longer corrupt memory. At worst the pipe is refused until the stack returns the URB.
* The board always leaves a panic (reboot), and a hang in the loopTask becomes a panic with a backtrace.
* Stale data is never served as current to NUT clients.
* Crash details reach the web UI without a serial console.

### Negative
* More deviations from the upstream `usb_host_hid` component (see also ADR 0007), to carry over on every update.
* A control URB that never comes back cannot be cancelled on its own. Until US-122 only a restart cleared it; now a root port power cycle does, with the restart as the safety net (see its addendum). A cancelled enumeration is a different case: since US-121 the root port is power-cycled first as well.
* The `coredump` partition needs a one-time full flash with the web installer, because OTA does not update the partition table. Until then, core dumps are skipped (but panics no longer hang). The web installer and the release package also write `boot_app0.bin` (empty otadata) at `0xe000`: otherwise a board left on `app1` by an odd number of OTA updates keeps booting the old firmware after the full flash (`9723a3d`).
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
* Agents MUST NOT make an INPUT silence stale the data or trigger a recovery or a restart when `inputValuesPolled()` is true (US-123), and MUST recompute the coverage after every rebuild of the poll lists. The one exception is a driver whose `inputSilenceFaultMs()` is not 0 (Back-UPS BX, issue #138): agents MUST keep it off by default and justify each model it turns on with a log of frozen values.
