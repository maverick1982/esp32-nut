# Dynamic Polling and Quirks for USB STALL Mitigation

* **ADR ID:** 0005
* **Status:** Accepted
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

## Impact on Agent Implementation
* Agents modifying or implementing polling logic MUST NOT hardcode USB packet lengths for Control Transfers. They must parse the `HIDParser` structures.
* If a new brand or device is observed causing USB STALLs or WDT resets, the agent MUST first look for existing quirks in the `nut_repo/` upstream codebase and mirror them into `Quirks.h` rather than refactoring the global USB stack.
