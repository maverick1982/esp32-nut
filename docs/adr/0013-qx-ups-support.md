---
type: decision
title: "Q* UPS Support (nutdrv_qx): Protocol-Neutral Driver Core, Family-Aware Attach and Table-Driven Q* Engine"
description: "Documento ADR: Q* UPS Support (nutdrv_qx): Protocol-Neutral Driver Core, Family-Aware Attach and Table-Driven Q* Engine"
tags: [adr, decision, architecture, nutdrv_qx, megatec, usb, driver]
---
# Q* UPS Support (nutdrv_qx): Protocol-Neutral Driver Core, Family-Aware Attach and Table-Driven Q* Engine

* **ADR ID:** 0013
* **Status:** Accepted
* **Date:** 2026-11-07
* **Authors:** @maverick1982, Claude
* **Extends:** ADR 0010 (module map: "drivers see a transport, not the USB stack"; Q* drivers implement `IUPSDriver` directly), ADR 0008 (a bounded wait for Q* answers in the poll task)
* **Related:** ADR 0003 (upstream fidelity), ADR 0011 (one upstream-shaped table per subdriver), issues #28, #34, #91, #93, `docs/plans/nutdrv-qx-support.md`

## Context and Problem Statement

ESP32-NUT supports only USB HID UPS units (`usbhid-ups`). The analysis in `docs/plans/nutdrv-qx-support.md` estimates that the Q* family (`nutdrv_qx`, `blazer_usb`: Megatec and derived protocols, mostly Voltronic ODM units sold under many brands) is 20-30% of the UPS units connected to NUT over USB: the largest gap, ahead of any missing HID subdriver. Two open issues need it: #91 (VIDA Armour 1200VA, 0001:0000 "MEC0003", works with NUT using `protocol = hunnox`) and #28 (Safire 1500VA, 0001:0000 "MEC" / "MEC0003", fabula). #34 (CH340 USB-serial converter) needs a second USB host class driver and is out of scope here.

**How `nutdrv_qx` is built** (`nut_repo/drivers/nutdrv_qx.c`, `nutdrv_qx.h`). Two independent levels:

* **USB subdriver = transport.** A `*_command(cmd, buf)` function per bridge chip, selected from `qx_usb_id[]` by VID/PID and, for shared IDs, by the manufacturer and product strings (0001:0000 is shared by fabula, fabula_hunnox, fuji, krauler and snr). Most bridges are USB **HID class** devices with a vendor-defined or bogus report descriptor: cypress, phoenix, ippon, sgs and others send the command with SET_REPORT in 8-byte blocks and read the answer from the interrupt IN endpoint up to `\r`; krauler, fabula and hunnox encode the command as a **string descriptor index** read with GET_DESCRIPTOR (hunnox with a lock-step preamble and langid 0x0409); fuji needs interrupt OUT, snr a vendor control request.
* **Protocol subdriver = text protocol.** A `subdriver_t` with a `qx2nut` table of `item_t` rows (NUT variable, command such as `Q1\r`, expected answer length and leading character, `from`/`to` positions of the value, format, flags such as `QX_FLAG_STATIC`, `QX_FLAG_SEMI_STATIC`, `QX_FLAG_QUICK_POLL`, `QX_FLAG_CMD`, `QX_FLAG_SETVAR`, and optional `preprocess_*` functions). The protocol is **detected by probing**: `subdriver_matcher()` tries each protocol's `claim()` (which sends a command and checks the answer) in the fixed order of `subdriver_list[]`, up to `MAXTRIES` times, unless `protocol=` is set in `ups.conf`. The status bits and the shutdown/test commands shared by the Megatec-derived protocols are in `nutdrv_qx_blazer-common.c`. Most protocol files also carry a `testing_t` table of commands and answers (`#ifdef TESTING`).

Note that `hunnox` uses `blazer_claim` with `Q1` and `FW?`, exactly like `zinto`, and `megatec` uses `blazer_claim` with `Q1` and `I`; both come earlier in `subdriver_list[]`. Upstream itself does not detect `hunnox`, so #91 needs `protocol = hunnox`.

**What the current code already provides** (ADR 0010 step 4, US-103..US-105):

* The NUT server and the Web UI see only `IUpsSource`: a Q* driver that writes the usual keys of `UPSData` (`ups.status.*` flags, `battery.charge`, …) is indistinguishable from an HID one, and `UPSData::computeUPSStatusString()` builds `ups.status` for both.
* Commands and SET VAR go through `IUPSDriver::control()` → `IUpsControl`, which receives the protocol-neutral `IUpsControlPort`; the HID part is reachable only through `hidPort()`.
* `DriverRegistry` already selects a driver by VID/PID plus strings (US-097).
* `hid_host` already offers the raw primitives: `hid_class_request_set_report()`, `hid_host_device_get_string_descriptor()` with an explicit langid, and the interrupt IN chunks reach the poll task through the event queue.

**What is still HID-shaped:**

1. `IUPSDriver` mixes what the host needs from every driver (setup, poll, first poll done, device strings, control, delays, driver-side values) with HID-only hooks: `loop(IHidTransport*)`, `decodeReport`/`postDecode`, `planSet(HIDUsageDef…)`, `requestLength`, `acceptDiagReport`, `invalidateReport`, `inputValuesPolled`, `parseStringDescriptor`, the beeper encodings, `commandTable` (HID rows), `delayPaths`. A Q* driver would implement them as empty stubs while the host keeps treating it as HID. Concretely, the INPUT watchdog stays active for any driver that does not return `inputValuesPolled() == true` (ADR 0008, US-123): a Q* driver that forgets it would have its data marked stale and trigger port recoveries for no reason.
2. `USBHostUPS::claimInterface()` drops every interface whose report descriptor has no UPS (0x84) or Battery (0x85) usage page, which is exactly the case of most Q* bridges. The report cache, the INPUT watchdog, the descriptor retry and the diagnostics sweep (US-119) assume HID. Adding Q* there directly would scatter `if (qx)` branches over a 1600-line file, the outcome ADR 0010 rejects.
3. `IHidTransport::requestStringDescriptor()` remembers failed indices and never asks them again (review M5): right for HID strings, wrong for a krauler-family command, which is a string index by design.
4. The USB diagnostics export and the record/replay fixtures are HID only. We own no Q* hardware: every transport will be verified by testers, so an export of the Q* exchanges and a text replay are as necessary as the driver.

Constraints: upstream fidelity (ADR 0003); the USB/poll task owns the USB host, no application lock across a control transfer, Task WDT on the poll task with 30 s (ADR 0008); 4 MB flash on the classic ESP32 boards (#93); refactoring steps must not change behavior (ADR 0010).

How do we add the Q* family so that the HID path keeps its behavior, the Q* code follows `nutdrv_qx`, and the timing-sensitive USB layer is not destabilized?

## Considered Options

* **A. Q* as one more `IUPSDriver` on the current interfaces.** `QxDriver` stubs the HID hooks, the Q* primitives are added to `IHidTransport`, `claimInterface()` gets a Q* branch. Cheapest start, but every HID assumption of the host (INPUT watchdog, report cache, diagnostics sweep, beeper paths) must be neutralized case by case inside the driver, and each new HID feature risks breaking Q* silently.
* **B. A separate Q* stack.** A second `IUpsSource` implementation with its own task and attach logic, sharing only `lib/usb_host_hid`. Clean separation of the code, but two owners of one USB host and of its event queue (against ADR 0002/0008), and a duplicate recovery ladder, link monitor and restart policy that took months to stabilize (#47, #60, #78, #120).
* **C. One host, a protocol-neutral driver core with per-family extensions, family-aware attach, and a port of the `nutdrv_qx` engine.** Split `IUPSDriver` the way US-105 split the control port; let the registry pick the protocol family before any HID check; keep HID-only supervision on the HID path; add a Q* transport interface built on the primitives `hid_host` already has; port the `qx2nut` tables and the probing as they are upstream.

## Decision Outcome

Chosen option: **C**, because it reuses the single USB owner, the recovery ladder and the consumer side that already work, confines Q* to new code, and lets each step be verified on its own: the two refactoring steps against the existing native, replay and e2e tests, the Q* steps against upstream's own `testing_t` tables and then on real units by the testers.

### 1. Protocol-neutral driver core

`IUPSDriver` keeps only what the host needs from every driver, with no HID type in it: name, `setup()`, the periodic poll, `initialPollDone()`, `formatDeviceStrings()`, `control()`, default delays and whether `ups.delay.*` exist, writable variables and driver-side values (`setDriverSideValue`, `resetDriverSideValue`, `noteHostValue`), `noteDataChanged()`, `requestSpacingMs()`.

The HID hooks move to an HID extension (working name `IHidDriver`) reached through `hid()`, which returns `nullptr` for a driver of another family, mirroring `IUpsControlPort::hidPort()` (US-105). The host calls HID hooks only through `hid()`, so a Q* driver cannot be reached by HID-only logic by mistake. A driver receives the transport of its own family (`IHidTransport` for HID, the Q* transport of section 3); the core interface names neither. The exact distribution of the borderline methods (beeper capabilities, `commandTable`, `delayPaths`) is set by the refactoring spec, with one rule: a method stays in the core only if the host needs it for every family.

This step is a pure refactoring: all HID drivers keep their behavior and the existing native, replay and e2e tests stay green without changes to their expectations.

### 2. Family-aware attach and supervision

* **Selection order.** `DriverRegistry` decides the family before the report descriptor check, in this order: (1) the explicit HID rows (VID/PID, plus strings for shared IDs, as today); (2) the Q* rows, a faithful port of `qx_usb_id[]` with its strings and its order; (3) the generic HID fallback for any interface with UPS or Battery pages. The explicit HID rows and the Q* rows do not overlap today (06da:ffff vs 06da:0002…0601; 1cb0:0032/0038 vs 1cb0:0035). Putting the Q* rows before the generic fallback is what fixes #91, whose interface exposes a Power Device page and is attached today by `GenericDriver`, which receives no data ("Not Detected").
* **HID path unchanged.** The usage page check, the report cache, the INPUT watchdog, the descriptor retry and the diagnostics sweep run only for the HID family, with today's behavior.
* **Shared supervision.** The link monitor, the recovery ladder (port cycle, restart policy) and the stale/first-data states apply to both families, fed by the control transfer results as today. On the Q* path the data become stale after consecutive failed polls, as `upsdrv_updateinfo()` does with `dstate_datastale()`, and `isWaitingFirstData()` is true until the protocol has been detected and the first full walk has completed.

This step touches the attach of the USB layer. Although HID behavior must not change, it gets a test build on real HID units before release, as ADR 0010 step 4 did.

### 3. Q* transport

* **Host primitives** (module `usb-transport`), on the bound interface, with no application lock held (ADR 0008): raw SET_REPORT of arbitrary bytes; GET_DESCRIPTOR of a string with an explicit index and langid, **without** the failed-index memory of `requestStringDescriptor()`; read of interrupt IN chunks with a timeout; VID/PID and device strings. Interrupt OUT (fuji) and vendor control requests (snr) are added later, when a tester needs them.
* **USB subdrivers** (module `ups-protocol`, Q* part): one class per upstream `*_command()` (cypress, phoenix, ippon, sgs, krauler, fabula, hunnox, …), a faithful port of its framing, chunking, retries, langid handling and timeouts, built only on the host primitives above. They are plain C++ with native tests on a fake primitive interface.
* **Synchronous command, bounded wait.** A Q* command is request/response, as in NUT. It runs inside the poll task: the control transfers block as `requestReport()` already does; for the interrupt IN transports, the primitive takes the chunks of the bound interface from the event queue until the terminator or the upstream timeout. Other events read meanwhile (disconnection, errors) are kept in order and handled as soon as the command returns. One poll iteration therefore blocks for at most a few upstream timeouts, well inside the 30 s Task WDT. This extends ADR 0008 for the Q* path only; the HID poll keeps its timing.

### 4. Q* engine and protocols

* **Faithful port of the tables** (ADR 0003, and the analogue of ADR 0011 for Q*). Each protocol is its upstream `qx2nut` table, row by row, with its `preprocess_*` functions, flags and formats. A new protocol starts from its upstream file. Keys and names are the upstream ones.
* **Constant tables, per-driver state.** Upstream `item_t` rows carry runtime fields (`answer`, `value`). Here the table rows are `static const` data and the runtime fields live in the driver instance, indexed by row, so the tables keep the static lifetime that `IUpsControl` relies on (US-105: the control may run after the poll task deleted the driver).
* **Engine.** The walk of `nutdrv_qx.c` (`qx_ups_walk()`, `qx_process()`, `ups_infoval_set()`, the status bits and `update_status()`), with the same poll classes: static items at init, semi-static every `pollfreq`, quick items every `pollinterval`. The status bits are written into the same `ups.status.*` flags the HID drivers use, so `ups.status` keeps one computation. `nutdrv_qx_blazer-common.c` (status, shutdown and test commands) is ported once and shared.
* **Control.** A Q* `IUpsControl` implementation ports `instcmd()` and `setvar()`, including the blazer-common commands (`beeper.toggle`, `test.battery.*`, `shutdown.*`, `load.*`), using only `IUpsControlPort`, never `hidPort()`.
* **Protocol detection as upstream.** After attach, the protocols are tried in the order of `subdriver_list[]` with `MAXTRIES`; a protocol not yet ported is skipped, never reordered. The probing runs in the poll state machine, at most one claim attempt per iteration. When no protocol claims the device, the System Log says so once ("Device not supported", as upstream) and probing is retried later with a back-off, not in a tight loop. The detected protocol and transport are written to the System Log and to the diagnostics export; in the NUT variables they follow what the HID drivers publish today (`driver.name`).
* **Upstream options as UPS settings.** `protocol` (needed by #91, since `hunnox` is not detectable upstream either) comes with the first Q* delivery, with the upstream name and values; `langid_fix`, `novendor`, `noscanlangid` and `subdriver` follow when a tester needs them. They satisfy the rule of ADR 0010: a wrong value leaves the UPS without data, it cannot lock the user out of the board.
  *Note (US-138, 2026-11-07):* `protocol` and `novendor` landed in **Board Settings**, not UPS Settings: like `ups.conf`, they configure the board, they are not variables of the UPS (decision of the maintainer). `novendor` came with `protocol` because the hunnox USB transport cannot encode `FW?`, so `blazer_claim` needs it for #91 as with NUT. `langid_fix`, `noscanlangid` and `subdriver` still follow when a tester needs them.

### 5. Diagnostics and tests

* **USB diagnostics export, Q* section.** Transport, protocol, probing outcome and the last exchanges (command, raw answer as text and hex, length, time, error), so a tester's file is enough to debug a transport we cannot run.
* **Native tests.** Each ported protocol is tested with its upstream `testing_t` table, replayed through a fake transport; each USB subdriver with fake primitives (cypress chunking, krauler indices, hunnox lock-step). The replay fixtures gain a text variant for captured Q* exchanges.

### 6. Rollout

Each step in its own spec and PR, behavior-preserving steps first:

1. **Driver core split** (section 1). No behavior change.
2. **Family-aware attach** (section 2). No behavior change for HID devices; test build on real HID units.
3. **Q* infrastructure:** host primitives, engine, blazer-common, Q* control, the protocols `megatec`, `mustek`, `megatec-old`, `zinto`, `innovatae` and the fallback `q1`, diagnostics section, native tests.
4. **First transports for the open issues:** fabula and hunnox (krauler family) with the `hunnox` protocol and the `protocol` setting, for #91 and #28. Delivered as a **test build** (OTA zip) for the reporters of the two issues, not as a release; the Q* support goes into a release only after at least one of them confirms it on the real unit. Then cypress and the other SET_REPORT transports for the wider market.
5. Further protocols and options on user request. CH340 and other USB-serial converters (#34) need a second host class driver next to `hid_host` and get their own ADR.

Flash is measured at each Q* step on the classic ESP32 image (#93). The Q* code is in every image: no `#if` per board or per protocol family (ADR 0010).

## Consequences

### Positive
* The largest coverage gap is addressed with code that mirrors upstream, so protocol fixes and new protocols can be ported from NUT row by row.
* HID drivers become independent from Q* and vice versa: a new HID feature cannot reach a Q* device through a forgotten hook, and the INPUT watchdog hazard disappears by construction.
* One USB owner, one recovery ladder, one status computation, one consumer API: the NUT server, the Web UI, syslog and the future MQTT output need no change.
* Upstream's own `testing_t` tables give the Q* protocols native tests from day one.
* #91 stops being attached by the wrong driver as soon as step 2 and the Q* rows land.

### Negative
* Two refactoring steps with no visible feature before the first Q* unit works.
* Step 2 touches the attach of the USB layer and needs a round of tests on real HID units.
* The synchronous Q* command is a new kind of wait in the poll task; its bound depends on the upstream timeouts of each transport and must be checked when a transport is ported.
* Every Q* transport depends on a tester with real hardware: progress follows the testers' availability.
* More flash on the 4 MB boards; the margin must be watched at each step.

## Resolved Questions

Decided on 2026-11-07 by @maverick1982:
* **First protocols:** besides `megatec`, `q1` and `hunnox`, the ones that cost little and carry no risk of their own: `mustek`, `megatec-old`, `zinto` and `innovatae` (section 4). They are almost only table data on the blazer-common functions and have upstream `testing_t` tables. Their probes (`QS`, `D`, `Q1` + `FW?`, `Q1`) and their positions in the probing order are the upstream ones, so a device is claimed by the same protocol as in NUT, among the ported ones.
* **First Q* delivery:** a test build for the reporters of #91 (tda2806) and #28 only, not a release (section 6).

## Impact on Agent Implementation

* Do not add HID-only calls on the driver outside `hid()` once step 1 exists; new hooks needed by every family go into the core, the others into their family extension.
* Do not add `if (qx)` / family branches inside the HID logic of `USBHostUPS`; family decisions belong to the attach (registry) and to the family's own path.
* Q* protocol tables and USB subdrivers are ports of the upstream files in `nut_repo/drivers/nutdrv_qx*.c`: keep their order, keys, commands, positions, flags and timeouts; cite the upstream lines when a behavior is not obvious. Do not write a Q1 parser by hand.
* Keep Q* tables `static const`; runtime values go into the driver instance.
* Do not reorder the protocol probing nor add heuristics upstream does not have; a protocol upstream cannot detect is selected with the `protocol` setting, as in `ups.conf`.
* Q* commands run only in the poll task, with no application lock held, and with the upstream timeouts; never wait for a Q* answer in the loopTask or in a `hid_host` callback.
* The control of a Q* driver uses `IUpsControlPort` only; do not call `hidPort()` from Q* code.
* Every new Q* transport or protocol comes with native tests (upstream `testing_t` and fake primitives) and with its fields in the diagnostics export.
* Refactoring steps 1 and 2 must not change HID behavior and must not be mixed with Q* features in the same commit.
