# Migrate to ESP-IDF usb_host_hid and Decouple Driver Tasks

* **ADR ID:** 0002
* **Status:** Accepted
* **Date:** 2026-09-05
* **Authors:** Antigravity / @maverick1982

## Context and Problem Statement
The legacy implementation of the USB HID Host driver suffered from critical stability issues. Concurrent access from the Web API, the NUT server, and the USB polling loop caused race conditions. In addition, interacting directly with low-level USB transfers often led to deadlocks, unresponsive Web UI, or complete freezing of the ESP32 when the UPS was hot-plugged or when the bus stalled. How should we manage USB HID communication to ensure absolute stability and responsiveness?

## Considered Options
* **Option 1: Patch the legacy manual USB stack.** Add more timeouts and checks to the existing code. (Cons: Fragile, doesn't fix underlying architecture flaws, hard to maintain).
* **Option 2: Purely synchronous polling on the main loop.** (Cons: USB timeouts would block the main loop, causing the Web Server and NUT server to drop client connections).
* **Option 3: Migrate to ESP-IDF usb_host_hid with a decoupled background task and thread-safe shared state.** Rely on the official Espressif HID component for low-level handling, move USB event polling to a background FreeRTOS task, and protect all shared data with mutexes. (Pros: highly stable, leverages official drivers, frees up the main loop).

## Decision Outcome
Chosen option: "**Option 3: Migrate to ESP-IDF usb_host_hid with a decoupled background task**", because it provides a robust, asynchronous event-driven model for USB communication while maintaining high responsiveness for the web and NUT servers.

Specifically, the refactoring established these patterns:
1. **Background Task**: usb_host_client_handle_events runs in its own FreeRTOS task to keep the USB stack fed without blocking loop().
2. **Thread Safety**: All reads/writes to UPSData and interactions with the HID handle are protected by a std::recursive_mutex.
3. **Synchronous Control Transfers**: Operations like setBeeper (which use hid_class_request_set_report) are executed synchronously on the main thread (not the background task) to prevent stalling the event loop.
4. **Native Testability**: Hardware-independent logic (like bit-masking in BeeperLogic and JSON payload generation in WebApiJson) was extracted into pure functions to allow native TDD without requiring physical USB hardware.

## Consequences
### Positive
* The ESP32 no longer deadlocks during aggressive concurrent polling from the Web UI and NUT clients.
* Hot-plugging the USB cable is handled gracefully without crashing.
* Codebase is highly testable (native unit tests cover payload manipulation and API generation).

### Negative
* Increased memory footprint due to the dedicated FreeRTOS task.
* Increased complexity in USBHostUPS.cpp regarding mutex lifecycle management.

## Impact on Agent Implementation
* **Mutex Rule**: Agents MUST NEVER access UPSData or invoke usb_host_hid APIs from outside the class without acquiring the _mutex.
* **Task Boundary Rule**: Agents MUST NOT place blocking control transfers (e.g., hid_class_request_get_report) inside the asynchronous background event loop, as this will cause deadlocks.
* **Testability Rule**: Agents MUST extract byte-level parsing, bit-masking, and string formatting into pure static functions (e.g., BeeperLogic, WebApiJson) and write native unit tests for them, avoiding direct hardware dependencies in business logic.
