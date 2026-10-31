---
type: decision
title: "Dynamic Polling and Quirks for USB STALL Mitigation"
description: "Documento ADR: Dynamic Polling and Quirks for USB STALL Mitigation"
tags: [adr, decision]
---
# Dynamic Polling and Quirks for USB STALL Mitigation

* **ADR ID:** 0005
* **Status:** Partially superseded by ADR 0007 (report lengths, `QUIRK_MAX_REPORT_SIZE_1`) and by US-108 (`QUIRK_NO_GET_REPORT` replaced by `QUIRK_NO_INTERRUPT_PIPE`, see "Correction"). The freeze root cause was the deadlock addressed by ADR 0008.
* **Date:** 2026-09-11
* **Authors:** Antigravity (on behalf of Maverick1982)

## Context and Problem Statement
When transitioning to the official ESP-IDF `hid_host` component for USB communication (v1.5.0), the application began suffering from recurrent Watchdog Timer (`TG0WDT_SYS_RST`) resets. These resets occur when a connected UPS (particularly strict devices like APC SU750i) rejects a synchronous `GET_REPORT` command by issuing an Endpoint 0 STALL. 
Because the ESP-IDF `hid_class_request_get_report` API uses a blocking semaphore (`portMAX_DELAY`) that is prone to deadlocking upon certain anomalous STALLs, the generic polling loop inadvertently freezes the main task when blindly fetching 64 bytes for all report IDs. How can we robustly prevent these freezes without rewriting the underlying ESP-IDF USB framework?

## Considered Options
* **Option 1: Global Passive Polling for strict brands (APC)**
  Disable active polling (`requestReport`) globally for specific Vendor IDs (like `0x051D` APC) and rely solely on the asynchronous Interrupt IN endpoint to receive data changes.
* **Option 2: Dynamic Packet Sizing & Targeted Quirks (Align with NUT)**
  Calculate the exact expected payload size from the HID descriptor boundaries (`bit_offset + bit_size`) to avoid offending strict HID parsers. Add targeted `QUIRK_MAX_REPORT_SIZE_1` and `QUIRK_NO_GET_REPORT` configurations tailored exactly to the Product IDs known to have issues in the upstream NUT `apc-hid.c` driver.
* **Option 3: Low-Level Asynchronous Polling**
  Bypass the `hid_host` component entirely for control requests and interact directly with `usb_host_transfer_submit_control()` using custom Interrupt Request Packets (IRPs), semaphores, and manual RTOS timeout controls.

## Decision Outcome
Chosen option: "**Option 2: Dynamic Packet Sizing & Targeted Quirks (Align with NUT)**", because it resolves the immediate freezing condition gracefully by feeding the UPS strictly well-formed packets, maintains compliance with ADR 0003 (faithfully mirror NUT quirks), and avoids introducing complex, unmaintainable low-level raw USB code (unlike Option 3). It also prevents large regressions that Option 1 would have caused to models requiring active polling (e.g., APC 5G devices).

## Consequences
### Positive
* The polling cycle is now highly optimized and compliant with device capabilities, drastically reducing USB bus STALL noise.
* System stability is guaranteed without overriding built-in ESP-IDF RTOS behaviors.
* We establish a solid standard for implementing USB Quirks identical to the `QUIRKS` mapping technique found in NUT's `libhid.c`.

### Negative
* Additional computational overhead at runtime parsing the usage vector bounds per report during polling.
* Developer maintenance overhead: future unsupported UPS models that still freeze might require the manual introduction of new Product IDs to the `UPS_QUIRKS` table.

## Correction (US-108, 2026-10-31)
`QUIRK_NO_GET_REPORT` on 051d:0003 and 051d:0004 (issue #39, v1.5.0) was the opposite of upstream. `apc-hid.c` does not stop GET_REPORT on these PIDs: `disable_interrupt_pipe()` (apc-hid.c:51-62) sets `use_interrupt_pipe = FALSE`, because on the APC 5G the interrupt pipe carries a proprietary protocol, and usbhid-ups reads every value with a FEATURE GET_REPORT ("pollonly"). With the quirk the firmware read only the INPUT reports and missed the voltages, the load and the thresholds, and `battery.charge.low` / `battery.runtime.low` could not be set.

* `QUIRK_NO_GET_REPORT` is replaced by `QUIRK_NO_INTERRUPT_PIPE` (same bit) on both PIDs. `USBHostUPS::processInputReport()` drops the INPUT reports before the statistics and the InputWatchdog, which is never fed and never acts; `GenericDriver::buildPollLists()` leaves out the reports that are only INPUT (the 0x89 of the proprietary protocol, libusb1.c:1079); the full poll reads the semi-static reports every time, as usbhid-ups.c:2352, 2360 without the interrupt pipe (see ADR 0006).
* `LinkMonitor::setCarriesData()` is gone with `QUIRK_NO_GET_REPORT`: every device now carries its data on the control pipe.
* The freeze of #39 (GET_REPORT waiting forever after a STALL) is left to the timeouts, the interface recovery and the controlled restart of ADR 0008. Field proof on the Smart-UPS of #39 / #100 is pending before the release.

## Impact on Agent Implementation
* Agents modifying or implementing polling logic MUST NOT hardcode USB packet lengths for Control Transfers. They must parse the `HIDParser` structures. Known exception: the fixed Powercom poll lists use 8 bytes (see ADR 0007).
* If a new brand or device is observed causing USB STALLs or WDT resets, the agent MUST first look for existing quirks in the `nut_repo/` upstream codebase and mirror them into `Quirks.h` rather than refactoring the global USB stack.

