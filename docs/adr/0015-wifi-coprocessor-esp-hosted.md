---
type: decision
title: "Wi-Fi Through an ESP-Hosted Coprocessor: SDIO Pins in the Board Profile, Runtime Wi-Fi Capability and Built-In Ethernet Next to Wi-Fi"
description: "Documento ADR: Wi-Fi Through an ESP-Hosted Coprocessor: SDIO Pins in the Board Profile, Runtime Wi-Fi Capability and Built-In Ethernet Next to Wi-Fi"
tags: [adr, decision, architecture, network, wifi, esp-hosted, esp32-p4, board]
---
# Wi-Fi Through an ESP-Hosted Coprocessor: SDIO Pins in the Board Profile, Runtime Wi-Fi Capability and Built-In Ethernet Next to Wi-Fi

* **ADR ID:** 0015
* **Status:** Proposed
* **Date:** 2026-11-27
* **Authors:** @josch0, Claude
* **Extends:** ADR 0010 (section 3: "Runtime capabilities (detected at boot). Presence of a Wi-Fi coprocessor (try to start it)"), PDR 0002 (D3 one active link and the link choice, D4 SDIO pins fixed in the profile, D5 boards with Wi-Fi keep the access point recovery)
* **Related:** US-090, US-091, US-092 (P4 boards without Wi-Fi), US-126 and US-127 (W5500 next to Wi-Fi on the ESP32-S3), issue #94

## Context and Problem Statement

The ESP32-P4 has no radio. Boards such as the Waveshare **ESP32-P4-NANO** (and the ESP32-P4-WIFI6-POE-ETH, the Espressif Function EV board) add an **ESP32-C6** that runs Espressif's ESP-Hosted slave firmware and gives the P4 Wi-Fi over SDIO. Today every P4 profile has `has_wifi = false`: the README says of the P4-WIFI6-POE-ETH that "its ESP32-C6 Wi-Fi chip is not used". PDR 0002 already decided the product side for P4 boards with the coprocessor: Wi-Fi when the board has it (D1), one active link with the choice Wi-Fi / Ethernet / Automatic and Ethernet as the initial value on the P4 (D3), the SDIO pins fixed in the profile (D4), and the access point recovery of PDR 0001 kept on boards with Wi-Fi (D5).

**What the platform already provides.** The Arduino core 3.3.12 (pioarduino 55.03.312-1, ESP-IDF 5.5.5) ships ESP-Hosted 2.12.13 and `esp_wifi_remote` precompiled for the P4 (Apache-2.0), with SDIO as the host interface. The `WiFi` library routes over them: `WiFi.setPins(clk, cmd, d0, d1, d2, d3, rst)` sets the SDIO pins, the first `WiFi.mode()` starts the link to the coprocessor, and `esp32-hal-hosted.h` reports the host and coprocessor firmware versions and whether an update exists (`hostedGetHostVersion()`, `hostedGetSlaveVersion()`, `hostedHasUpdate()`, `hostedGetUpdateURL()`). The matching C6 firmware is part of the core package (`hosted/esp32c6-v2.12.13.bin`).

**Measured on a Waveshare ESP32-P4-NANO** (chip v3.x, factory C6 firmware, 2026-11-27, a test build on the `esp32-p4-eth` image): with CLK 18, CMD 19, D0-D3 14-17 and C6 reset 54 (the pins of Waveshare's own demo `08_eth2ap/sdkconfig` and the defaults of the core's `esp32p4` variant), ESP-Hosted starts, a Wi-Fi scan finds the networks around, and the whole test costs 3.4 KB of flash: the Wi-Fi stack is already in the P4 image. The C6 reports firmware version **0.0.0**: the factory firmware is older than the version query, and `hostedHasUpdate()` says an update exists. Joining a network and the access point were not part of the test.

**What does not fit today.**

1. `BoardConfig` has no place for the SDIO pins of a coprocessor (D4).
2. `has_wifi` is a compile-time constant. On a P4 it depends on a second chip and its firmware: a C6 that does not answer would leave a board that believes it has Wi-Fi, so the double reset would start an access point that never appears, and the recovery window of D5 (boards without Wi-Fi) would not open either. The user would be locked out, the outcome PDR 0002 D4 forbids.
3. `planNetwork()` knows Wi-Fi next to Ethernet only as a W5500 enabled by the user (`spi_eth`, US-126/127). A board with its own Ethernet port and Wi-Fi is planned as a Wi-Fi board: its port would never be used.
4. The link mode is saved with the W5500 settings (NVS namespace `eth`, key `mode`), offered only in the W5500 section of the Network page, and defaults to Automatic (US-127). `BoardDefaults::network_mode` exists but is not read.
5. The Web UI shows no chip revision, no installed image and nothing about the coprocessor, so "is the right image installed, is the C6 working?" has no answer on the board.

How do we give P4 boards with an ESP-Hosted coprocessor Wi-Fi as PDR 0002 describes, without a way to lock the user out when the coprocessor fails, and without a second network code path next to the W5500 one?

## Considered Options

* **A. Wi-Fi as a compile-time capability of the profile only.** `has_wifi = true` in the profile, `WiFi.setPins()` at startup, nothing else. Smallest change, but a C6 that does not answer leaves the board without the access point and without the recovery window (point 2), and the built-in Ethernet port is still ignored by `planNetwork()` (point 3).
* **B. Profile data plus a runtime Wi-Fi capability, and the built-in port planned like the W5500.** The profile carries the coprocessor pins; at boot the board starts the coprocessor and only then counts as a Wi-Fi board. `planNetwork()` treats an Ethernet port of the board like an enabled W5500, so the existing Automatic / Ethernet / Wi-Fi logic, fallback and switching (US-126/127) serve both.
* **C. A separate network manager for coprocessor boards.** Keeps the W5500 code untouched, but duplicates the plan, the fallback and the switching that took US-126/127 to get right, against ADR 0010 (one network module).

## Decision Outcome

Chosen option: **B**, because it implements the capability ADR 0010 announced, keeps one network plan for every board, and turns a failing coprocessor into the already tested "board without Wi-Fi" case instead of a lockout.

### 1. Coprocessor pins in the board profile

`BoardConfig` gains a `WifiCoprocessor` entry: whether the board has one, and its SDIO pins (CLK, CMD, D0-D3) and reset pin. Boards whose radio is in the chip (ESP32-S3) have none. `has_wifi` keeps its meaning "the board can have Wi-Fi"; for a board with a coprocessor it is confirmed at runtime (section 2). The pins are profile data, never a setting (PDR 0002 D4), and are given to the `WiFi` library with `WiFi.setPins()` before any other Wi-Fi call, in the `net` adapter (ADR 0010: chip-specific libraries live in the adapters).

### 2. Runtime Wi-Fi capability

* At boot, before `planNetwork()`, a board with a coprocessor starts it once (bounded, under the Task WDT) and records the outcome: answered, or not, with the reason.
* **Answered:** the board is a Wi-Fi board: Wi-Fi settings, access point, captive portal and the access point recovery of PDR 0001 (PDR 0002 D5).
* **Not answered:** the board is planned exactly as a board without Wi-Fi (US-090..092): Ethernet, the recovery window after the double reset, no Wi-Fi page. The System Log says why, once, and the board page shows it. A broken coprocessor can therefore never lock the user out.
* The capability is decided once per boot. A coprocessor that fails later is a link failure like any Wi-Fi loss, handled by the existing link logic.

### 3. Built-in Ethernet next to Wi-Fi

* `NetBootInput` describes the Ethernet that exists independently of the user: the board's own port, or the W5500 enabled by the user. `planNetwork()` uses one rule for both: Ethernet first in Automatic and Ethernet mode, Wi-Fi (station, or the access point when Wi-Fi is not configured) as the fallback, switching between Ethernet and the station in Automatic (US-127), the double reset starting the access point.
* The decision table of `planNetwork()` for the ESP32-S3, with and without the W5500, and for P4 boards without Wi-Fi stays as it is; the native tests of `test_net_boot_plan` must pass unchanged, and new rows cover a board with both.
* An Ethernet port of the board cannot be "not answering" the way a wrongly wired W5500 can; a failed start of the PHY is handled like the W5500's NO_CHIP outcome, so the board still falls back to Wi-Fi in every mode.

### 4. Link mode as a board-level setting

* The link mode keeps its storage (NVS namespace `eth`, key `mode`) and its values, so existing ESP32-S3 boards keep their setting.
* When nothing is saved, the initial value is `BoardDefaults::network_mode` for a board with its own Ethernet port (Ethernet on the P4, PDR 0002 D3); the W5500 keeps Automatic (US-127).
* The Network page offers the choice wherever the board has both links: in the W5500 section as today, and in a link section for boards with a built-in port, served by the same API contract for the mode. A mode change restarts the board, as today.

### 5. Coprocessor firmware: shown, not updated (first delivery)

* The System Log and the board page show the coprocessor target, its ESP-Hosted firmware version ("unknown" when it answers 0.0.0, as the factory firmware of the measured C6 does) and the host version, and say when the host reports a newer firmware for the coprocessor.
* This is a notice, not an error: the measured factory firmware scans with host 2.12.13.
* **Updating the coprocessor is out of scope** and gets its own decision. The candidates are an upload of the C6 image from the Web UI through the ESP-Hosted update API, or a download from Espressif's server. Both write the firmware of a second chip and need their own failure analysis.

### 6. Board page: chip revision and image

The board page shows the chip model and revision and the PlatformIO environment the image was built for (`NUT_BUILD_ENV`), with the coprocessor of section 5. A user can tell from the board whether the installed image matches the chip revision (PDR 0002 D2) and whether the coprocessor works.

### 7. First board: Waveshare ESP32-P4-NANO

* Profile `waveshare_p4_nano`: IP101 PHY at address 1, MDC 31, MDIO 52, RMII clock in on 50, PHY reset 51 (as `esp32p4_eth`); ESP32-C6 on SDIO CLK 18, CMD 19, D0-D3 14-17, reset 54; the UPS on the USB 2.0 High-Speed port (Type-A); no status LED (only a power LED); initial link mode Ethernet.
* Two images as for the other P4 boards: `esp32-p4-nano` (chip v3.1+) and `esp32-p4-nano-v1` (chip v1.x), with release zips, web installer manifests, CI and README entries. The v1 image is built but untested until a tester confirms it.
* The other P4 boards with a C6 (P4-WIFI6-POE-ETH, Function EV board) can follow with their own profile once their pins are checked against their schematics.

### 8. Tests

* **Native:** `test_net_boot_plan` gains the rows of a board with built-in Ethernet and Wi-Fi in every mode, with and without a configured Wi-Fi network, with the double reset, and with a coprocessor that did not answer (planned as a board without Wi-Fi); the existing rows stay unchanged. The board profile test checks the P4-NANO data. The JSON of the board page and of the link mode get `test_web_api` cases.
* **E2E:** the link section of the Network page on a board with built-in Ethernet and Wi-Fi, and the board page with chip revision, image and coprocessor.
* **Build:** all images, as in CI.
* **Hardware** (Waveshare ESP32-P4-NANO, chip v3.x): Ethernet as today; Automatic, Ethernet and Wi-Fi modes; joining a Wi-Fi network; switching in Automatic when the cable is unplugged and plugged; the double reset starting the access point; the coprocessor information on the board page.

## Consequences

### Positive

* P4 boards with an ESP32-C6 get the Wi-Fi PDR 0002 planned for them, through the core's own ESP-Hosted support and for 3.4 KB of flash.
* A coprocessor that does not answer degrades to the tested "board without Wi-Fi" behavior: no lockout.
* One network plan for the W5500 and for built-in ports; the Automatic mode and its switching are reused, not duplicated.
* The board page answers "right image?" and "is the coprocessor working?" without a serial cable.

### Negative

* The boot of a coprocessor board waits for the coprocessor once (about 3 s in the test build).
* The coprocessor firmware is a second firmware the project does not update yet; a C6 whose factory firmware cannot join networks would need the update path of section 5 first.
* `planNetwork()` and the Network page change; the S3 and W5500 behavior must be shown unchanged by the existing native and e2e tests.
* The Wi-Fi MAC address of a coprocessor board is the C6's, not derived from the P4: the "same address on both links" of US-126 has to be checked on the hardware.

## Impact on Agent Implementation

* Coprocessor pins are profile data (`board().wifi_coprocessor`), passed to `WiFi.setPins()` in the `net` adapter; never literals elsewhere, never a setting, no `#if` per board.
* Code that offers Wi-Fi (routes, pages, the access point, the plan) asks the runtime capability, not `board().has_wifi` alone, on boards with a coprocessor.
* `planNetwork()` stays pure and keeps every existing row of `test_net_boot_plan` unchanged; new behavior comes as new rows.
* Do not write to the coprocessor's flash and do not download its firmware in this delivery; show its version only.
* Keep the link mode storage (namespace `eth`, key `mode`) and its values: existing boards must keep their saved mode.
* The coprocessor start runs once at boot, bounded, and its failure is logged once; never retry it in a tight loop.
