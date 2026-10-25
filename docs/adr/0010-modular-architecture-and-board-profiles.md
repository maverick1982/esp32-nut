---
type: decision
title: "Modular Architecture: Single-Responsibility Modules, Board Profiles and Capability-Driven Runtime"
description: "Documento ADR: Modular Architecture: Single-Responsibility Modules, Board Profiles and Capability-Driven Runtime"
tags: [adr, decision, architecture, modularity, board, network, refactoring]
---
# Modular Architecture: Single-Responsibility Modules, Board Profiles and Capability-Driven Runtime

* **ADR ID:** 0010
* **Status:** Accepted
* **Date:** 2026-10-21
* **Authors:** @maverick1982, Claude
* **Product decisions:** [PDR 0002](../pdr/0002-supporto-multi-scheda-esp32-p4-ethernet.md)
* **Amends:** ADR 0004 (scope of the `GenericDriver` inheritance rule)
* **Related:** ADR 0002 and 0008 (task ownership), ADR 0003 (upstream fidelity), ADR 0009 (recovery), issue #94, `docs/plans/platform-upgrade-idf55.md`, `docs/plans/nutdrv-qx-support.md`, `docs/plans/mqtt-support.md`

## Context and Problem Statement

The firmware targets one board today (ESP32-S3 DevKit, Wi-Fi, USB HID UPS). Several upcoming changes add new axes of variation:

* **New boards.** Issue #94 brings an ESP32-P4 port with wired Ethernet (fork by rtadams89). The fork selects Ethernet at build time (`NUT_NET_ETHERNET`) and spreads `#if` blocks over `main.cpp`, `network_manager`, `web_config_server`, `DiagnosticLED`, `USBHostUPS` and `crash_diag`. Some P4 boards also have an ESP32-C6 Wi-Fi coprocessor (ESP-Hosted over SDIO), so "P4 = Ethernet only" is not true either.
* **New protocols.** `nutdrv_qx` support (Megatec/Q*) needs non-HID transports (string descriptors, raw SET_REPORT, later CDC serial) and drivers that do not fit the HID model.
* **New outputs.** MQTT adds a second consumer of UPS data and logs next to the NUT server and the web UI.
* **New platform.** The planned ESP-IDF 5.5 upgrade (pioarduino 55.03.x) also removes the S3/P4 platform split.

The code is already partly modular: libraries in `lib/`, the `IUPSDriver` / `IUSBHostUPS` interfaces, pure logic with native tests (`RestartPolicy`, `LinkMonitor`, `AuthPolicy`, `SessionTable`, `HIDParser`, `NutTable`; 557 native tests), log callbacks instead of a global logger inside the libraries, and a platform port in `WebAuth` (`AuthPlatform`). But several units mix responsibilities (line counts on `main`, 2026-10-21):

| Unit | Lines | Responsibilities mixed today |
|---|---|---|
| `lib/USBHostUPS/src/USBHostUPS.cpp` | 1187 | USB stack and `hid_host`, poll task, recovery ladder, commands, writable variables, diagnostics JSON |
| `lib/NUTServer/src/NUTServer.cpp` | 1066 | TCP sockets and client slots, protocol parsing, command dispatch |
| `lib/USBHostUPS/src/GenericDriver.cpp` | 748 | HID mappings, poll state machine, NUT table binding |
| `src/network/web_config_server.cpp` | 638 | HTTP routing, auth guard, OTA, configuration, system status |
| `src/core/main.cpp` | 298 | wiring, plus AP idle timeout, double reset, restart decisions, LED state |
| `data/www/bundle.js` | ~1800 | the whole web UI |

Two coupling points matter most:
* **`IUSBHostUPS` serves two audiences.** The NUT server and the web API use it as "the UPS" (data, status, commands, variables), but it also exposes HID internals (`getUsages()`, `getHIDParser()`, `getActiveBeeperPath()`, `writeUsage()`, `requestReport()`). Every consumer therefore depends on HID types, and a non-HID driver cannot implement it cleanly.
* **Globals in `main.cpp`.** Modules are created as globals and reach each other through them instead of receiving their dependencies.

How should the code be organized so that a new board, a new transport, a new protocol or a new output is an additive change confined to one module, and a change in one module cannot cause regressions in unrelated ones?

## Considered Options

* **A. Keep the structure, add `#if` per target.** What the P4 fork does. Cheap now, but every new board, network or protocol multiplies the conditional paths across files, and the native tests only see the default branch.
* **B. Rewrite into a new architecture in one go.** Clean result, but a long period without releases and a very high risk of USB regressions, which were hard to fix (#47, #60) and are not fully covered by tests.
* **C. Incremental modular architecture.** Define a module map, dependency rules, board profiles and a capability-driven runtime; move the code towards it in small behavior-preserving steps, each verified by the existing tests and replays.

## Decision Outcome

Chosen option: **C**, because it gives the structure needed by the P4, `nutdrv_qx` and MQTT without a rewrite, and each step can be verified on its own.

### 1. Module map

Each module has one responsibility and one reason to change.

| Module | Responsibility | Owns | Today |
|---|---|---|---|
| `app` | Composition root: creates the modules, injects dependencies, runs the setup sequence | loopTask entry (`setup()`, `loop()`) | `src/core/main.cpp` (also holds logic, to be moved out) |
| `board` | Board profile and capabilities (section 3) | — | new |
| `net` | Network links (Wi-Fi STA/AP, Ethernet RMII or SPI) and the choice of the active link | link state | `src/network/network_manager.*`, AP logic in `main.cpp` |
| `usb-transport` | USB host stack, `hid_host`, primitives (control, interrupt IN, string descriptors), link monitoring and the recovery ladder | the USB/poll task (ADR 0002, 0008) | part of `USBHostUPS.cpp`, `lib/usb_host_hid`, `LinkMonitor` |
| `ups-protocol` | Drivers: turn transport bytes into UPS variables and commands. HID drivers today, Q* drivers later | driver state | `GenericDriver` and subclasses, `HIDParser`, `NutTable`, `CommandCatalog`, `DriverRegistry` |
| `ups-model` | UPS data and status computation, the consumer-facing UPS API (data, status, commands, writable variables) | the data lock | `UPSData`, parts of `USBHostUPS` and `IUSBHostUPS` |
| `nut-server` | upsd protocol: client sessions, parser and dispatch | NUT sockets | `lib/NUTServer` |
| `web` | HTTP routing, API handlers, OTA; uses `web-auth` | web sockets | `src/network/web_config_server.cpp`, `lib/WebApi` |
| `web-auth` | Credentials, sessions, throttling (ADR 0009) | — | `lib/WebAuth` (already modular) |
| `config` | Persistence of settings (Wi-Fi, NUT, network mode, driver-side variables, credentials store) | NVS namespaces | `src/core/config_manager.*`, `driver_var_store.*`, `CredentialStore` |
| `system` | Logging, crash diagnostics, Task WDT, restart policy, status LED, AP idle timeout | — | `src/core/app_logger.*`, `crash_diag.*`, `RestartPolicy`, `DiagnosticLED`, `ApIdleTimeout` |
| future: `mqtt` | MQTT publisher of UPS data and logs | MQTT task (esp-mqtt) | `docs/plans/mqtt-support.md` |

Not every row must become a separate library on day one: a module may start as a folder with a clear public header and be split into a library when it gains a second implementation or a second consumer.

### 2. Dependency rules

* **One direction only.** `app` → output modules (`nut-server`, `web`, `mqtt`) → `ups-model` → `ups-protocol` → `usb-transport`. `net`, `config`, `board` and `system` are services: any module may use them through their interfaces, they never depend on the modules above.
* **Consumers see the UPS, not the transport.** `nut-server`, `web` and `mqtt` depend only on a consumer-facing UPS interface (working name `IUpsSource`: data snapshot, status string, connected/stale, supported commands, execute command, writable variables, set/reset variable). HID types (`HIDUsageDef`, `HIDParser`, usage paths) do not cross this boundary. `IUSBHostUPS` is split accordingly: the driver-facing part stays with `usb-transport` / `ups-protocol`.
* **Drivers see a transport, not the USB stack.** `ups-protocol` uses a transport interface (report requests, raw SET_REPORT, string descriptors, VID/PID), so the same driver logic can be tested with fakes and a Q* driver can reuse the primitives.
* **Dependencies are injected.** Modules receive interfaces in their constructor or `begin()`; no module reads another module's global. Libraries keep reporting through a log callback, as they already do.
* **Pure logic stays separate from hardware.** Decisions (AP entry, recovery, network mode selection, restart policy, status computation) are plain C++ with native tests; thin adapters call the ESP-IDF / Arduino APIs.
* **Physical enforcement where cheap.** A module that becomes a library declares its dependencies (`library.json`), so a forbidden include fails to build. `src/` keeps only `app` and the Arduino-bound adapters.
* **Task ownership is explicit.** Each task belongs to one module (loopTask: `app`, `web`, `nut-server`; USB/poll task: `usb-transport` and the drivers it calls; MQTT task: `mqtt`). Cross-task access goes only through the existing data lock and queues (ADR 0002, 0008).

### 3. Board profiles and capabilities

Variation between targets is handled at three levels. The rule that decides between the last two: **a parameter may be a user setting only if a wrong value cannot lock the user out of the board.**

**Separate images (build time, forced by the hardware).** One image per chip family, plus one per incompatible silicon revision: ESP32-S3; ESP32-P4 v3.1+; ESP32-P4 before v3. Both P4 images come from the implementation contributed in #94: its author tested the pre-v3 image on a v1.3 dev board, while the v3.1+ image has not been tested on hardware and is published with a note in the README that says so (corrected on 2026-10-25: the first version of this ADR had the two images the other way round). The image header carries the chip and the revision range (`scripts/chip_rev.py` from the fork), so OTA refuses a wrong image. The web installer manifest lists one build per `chipFamily`, so the user picks no image by hand.

**Board profile (compiled into the image).** A small data file per supported board, selected by the PlatformIO environment:

```
include/board/board.h          struct BoardConfig { name; has_wifi; has_ethernet;
                               eth (RMII or SPI, PHY type/address, pins); led (pin, power pin);
                               usb peripheral map; defaults for the user settings }
                               const BoardConfig& board();
src/board/<board>.cpp          one BoardConfig, ~30 lines of data
```

Pins whose wrong value would cut the network (Ethernet RMII on a board without Wi-Fi, SDIO of a Wi-Fi coprocessor) are fixed in the profile. A board with different wiring gets its own profile and environment.

**Runtime capabilities (detected at boot).** Presence of a Wi-Fi coprocessor (try to start it), Ethernet link state, the attached UPS and its driver.

**One active network link.** On a board with both Wi-Fi and Ethernet, only one link is up at a time. There is no routing between two interfaces, and the NUT server, the web UI and the hostname always refer to a single address.

**User settings (web UI, NVS).** Everything a user can safely get wrong: network mode (Wi-Fi / Ethernet / automatic, which picks Ethernet when the cable has a link and Wi-Fi otherwise) when both exist, Wi-Fi credentials, the USB port used for the UPS on the P4, LED on/off, an optional SPI Ethernet module on the S3 (Wi-Fi stays as fallback), and the existing NUT and UPS settings. The profile provides the defaults.

Code outside `board` never tests the target with `#if`; it asks `board()` or the runtime capabilities (for example `if (board().has_wifi)` instead of `#if NUT_NET_ETHERNET`). The remaining `#if` blocks are limited to: a library that exists only on some chips (adapters in `net`), the CPU architecture (`crash_diag` backtrace: Xtensa vs RISC-V), and the ESP-IDF version while the S3 and P4 platforms still differ.

### 4. Migration

Behavior-preserving steps, each in its own spec and PR, with the native tests, the record/replay fixtures and the e2e tests green at every step:

1. **ESP-IDF 5.5 first** (`docs/plans/platform-upgrade-idf55.md`), platform only and no refactoring, so that a regression can be attributed to the platform.
2. **`board` and `net`:** board profile for the S3 DevKit, network links behind an interface, network mode as a user setting. This is the base for the P4 PR (#94).
3. **`app` as composition root:** move AP entry, double reset, AP idle timeout, restart decision and LED state out of `main.cpp` into testable units; replace the globals with injected instances.
4. **Split `IUSBHostUPS`** into the consumer-facing `IUpsSource` and the driver-facing transport interface; move commands and variables behind `ups-model`. The USB layer is timing-sensitive and not fully covered by tests (EP0 stalls, in-flight URBs, ADR 0008): this step gets a test build for the testers before release.
5. **`web` and `nut-server` internal splits** (routing / handlers / OTA; sessions / parser / dispatch) when they are next touched.
6. **The P4 port** (#94) rebased on steps 1-2, then `nutdrv_qx` and MQTT as additional modules.

Out of scope: micro-modules for their own sake, an event bus, dynamic plugin loading. The web UI front end (`bundle.js`) follows the same principles but is planned separately.

### 5. Relation to other ADRs

* **ADR 0004** is amended: "every sub-driver inherits from `GenericDriver`" applies to the USB HID drivers. A driver for another protocol family (Q*) implements `IUPSDriver` directly and reuses only the shared parts (status computation, command catalog interface).
* **ADR 0002 and 0008** stay in force: the USB/poll task, the callback-to-queue rule and the guarded control pipe move into `usb-transport` unchanged.
* **ADR 0003** stays in force for the driver mappings.
* **ADR 0009** is extended for boards without Wi-Fi, which have no AP recovery. The fork erases the web credentials on a double reset, which lets anyone on the LAN create the administrator, with no time limit, until the owner does. Instead:
  * the double reset opens a **recovery window of a few minutes** (the exact value is set in the implementation spec; the manual AP idle timeout of ADR 0009 uses 10 minutes);
  * during the window the first-use page accepts new web credentials, which replace the existing ones and revoke all sessions;
  * the existing credentials are **not** erased by the double reset: if nobody creates new ones before the window closes, they stay valid and the board is as before;
  * opening and closing of the window, and the replacement of the credentials, are written to the System Log.
  Boards with Wi-Fi keep the AP recovery of ADR 0009.

## Consequences

### Positive
* A new board is a profile file plus a build environment; a new network link, transport, protocol or output is a new module instead of edits across the code base.
* Consumers (NUT, web, MQTT) no longer depend on HID internals, which is a prerequisite for `nutdrv_qx`.
* Decisions that are now buried in `main.cpp` and in `#if` branches become pure functions with native tests; the tests also cover the non-default boards through fake profiles.
* Smaller units with one reason to change reduce regressions in unrelated areas and make reviews of external PRs (such as #94) easier.

### Negative
* Refactoring time with no visible feature, spread over several releases.
* More files and interfaces to navigate; a thin indirection on some calls (negligible in flash and CPU on the S3/P4).
* Step 4 touches the USB layer again, which needs a round of tests with real UPS units.
* The P4 PR waits for steps 1-2, and its author must adapt it.

## Resolved Questions

Decided on 2026-10-21 by @maverick1982:
* **Recovery on boards without Wi-Fi:** new web credentials can be created only during a time-limited window after the double reset (section 5, ADR 0009 bullet).
* **ESP32-P4 images:** both images from #94 are published; the v3.1+ one is marked in the README as not tested on hardware, the pre-v3 one is tested by the author on a v1.3 board (section 3, corrected on 2026-10-25).
* **Wi-Fi and Ethernet at the same time:** one active link at a time (section 3).

## Impact on Agent Implementation

* Do not add target `#if` blocks (`NUT_NET_ETHERNET`, board names, chip names) outside `board` and the `net` adapters; query `board()` or a runtime capability instead.
* New pins, capabilities and defaults go into the board profile, never as literals in the code that uses them.
* Do not include HID types (`HIDUsageDef`, `HIDParser`, `NutTable`, `CommandCatalog`) in `nut-server`, `web`, `mqtt` or `app` code written after this ADR; go through the consumer-facing UPS interface (until step 4 exists, do not add new HID dependencies to those modules).
* Do not create new globals; pass dependencies to constructors or `begin()`. Libraries log through a callback, not through `AppLogger`.
* Put new decision logic in a hardware-free unit with native tests, with a thin adapter for the platform calls.
* Refactoring steps must not change behavior: same NUT output, same web API, same native/replay/e2e results. Mixing a refactoring with a behavior change in the same commit is not allowed.
* Do not touch the USB layer's timing behavior (ADR 0008) as part of a refactoring; any such change needs its own ADR or spec and a tester round.
