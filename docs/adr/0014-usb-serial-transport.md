---
type: decision
title: "USB Serial UPS Connections: Second Host Class Driver (CDC-ACM), Device-Level Family Attach and Chip Adapters"
description: "Documento ADR: USB Serial UPS Connections: Second Host Class Driver (CDC-ACM), Device-Level Family Attach and Chip Adapters"
tags: [adr, decision, architecture, usb, cdc-acm, serial, driver]
---
# USB Serial UPS Connections: Second Host Class Driver (CDC-ACM), Device-Level Family Attach and Chip Adapters

* **ADR ID:** 0014
* **Status:** Proposed
* **Date:** 2026-11-27
* **Authors:** @josch0, Claude
* **Extends:** ADR 0013 (section 6, step 5: "CH340 and other USB-serial converters (#34) need a second host class driver next to `hid_host` and get their own ADR"), ADR 0010 (module map, no `#if` per board or family), ADR 0008 (callbacks only post to a queue, the poll task owns the USB work)
* **Related:** ADR 0002 (vendored `usb_host_hid`), ADR 0003 (upstream fidelity), US-132 ("not supported yet" path), issue #34

## Context and Problem Statement

Some UPS units have no HID interface: their USB port is a USB-to-UART bridge, and NUT drives them as a serial device (`port = /dev/ttyACM0` or `/dev/ttyUSB0`) with a serial driver such as `nutdrv_qx` or `blazer_ser`. The device that starts this work is an **APC SRV2KI-E** (Easy UPS On-Line). Its USB port is a MaxLinear/Exar **XR21V1410** bridge, USB ID **04e2:1410**. According to its datasheet it presents CDC-ACM interfaces: a Communication interface with an interrupt endpoint and a Data interface with bulk IN and OUT. Its UART line settings (baud rate, format, flow control) are not set with the CDC `SET_LINE_CODING` request but with vendor-specific register writes. The Linux kernel therefore drives the chip with its own `xr_serial` driver and makes `cdc-acm` ignore it when that driver is built. Issue #34 (CH340) is the same problem with a vendor-class bridge.

**What the current code does with such a device:** nothing visible.

1. `lib/usb_host_hid/hid_host.c` is the only USB host class driver. On `USB_HOST_CLIENT_EVENT_NEW_DEV` it looks for an HID interface (`hid_interface_present()`) and ignores every device without one. `USBHostUPS` never receives an event, the dashboard stays on "Disconnected", the System Log is silent and the USB diagnostics export is empty.
2. The enumeration watchdog (`EnumWatchdog`, US-121, US-139) is not affected. Besides `_enum_seen`, which only the HID events set, `serviceEnumWatchdog()` counts every configured device on the bus (`configuredDeviceCount()`, issue #78), so a serial device that completed its enumeration already disarms it. This must stay so.
3. The event queue (`HidEvent`), the bound interface (`_hid_dev_handle`) and the attach (`claimInterface()`) are typed on `hid_host_device_handle_t`. `DriverRegistry::Family` knows only `HID` and `QX`, and both live behind `hid_host`.
4. `QxDriver` takes a `std::unique_ptr<QxUsbTransport>`, and `QxUsbTransport` is built on `IQxUsbPrimitives` (SET_REPORT, string descriptors, interrupt IN). The Q* engine itself only needs `IQxTransport::command()`, which is the shape a serial transport has too.

**Constraints.** One owner of the USB host and its recovery (ADR 0002, 0008). Callbacks only post to queues, there is no application lock across a transfer, and the Task WDT (30 s) watches the poll task (ADR 0008). Consumers see only `IUpsSource`, and there is no `#if` per board or family (ADR 0010). Protocols are ports of their NUT drivers (ADR 0003). NUT has no USB matching for serial drivers, because the kernel binds the bridge chip and NUT opens a tty. The matching of bridge chips therefore follows the kernel's device tables, not a NUT table. The project is GPL-3.0-or-later. The Linux USB serial drivers are GPL-2.0-only, so their code must not be copied. They are a behavioral reference only; the implementation follows the chip datasheets.

**Scope of this ADR.** The first delivery makes the serial device **visible and open**: the bridge is recognized, its interfaces are opened, and the result shows in the dashboard, the System Log and the diagnostics export, before any UPS protocol exists. The protocol on top (section 6) gets its own spec and, if it needs a new decision, an amendment of this ADR.

How do we add USB serial UPS connections so that the HID path keeps its behavior, a serial device is visible and diagnosable before a protocol is ported, and the later protocol reuses the Q* engine and the existing consumer side?

## Considered Options

* **A. Own CDC-ACM code on the raw USB host client API, inside `USBHostUPS`.** Claim the interfaces, submit bulk and control transfers directly. No new dependency, but it rewrites what Espressif maintains and tests (transfer handling, CDC descriptors, disconnection) inside the most timing-sensitive file of the project, and it leads to the family branches in the HID logic that ADR 0013 rejects.
* **B. Vendor Espressif's `usb_host_cdc_acm` as a second class driver next to `hid_host`.** One USB host, one USB/poll task and one recovery owner. The family is chosen at device level (HID interface present or not), and the line settings of each bridge chip sit behind a serial port interface. This is the same pattern as ADR 0002 for `usb_host_hid`.
* **C. Replace `hid_host`'s device handling with one own device-level client that dispatches to HID and serial.** The cleanest long-term structure, but it rewrites the HID attach that took months to stabilize (#47, #60, #78, #120, #97), only to add a family that B adds without touching it.

## Decision Outcome

Chosen option: **B**, because it reuses a maintained class driver, leaves the HID attach as it is, keeps one USB owner and one consumer API, and confines the serial code to new files. The HID event path is not touched: the serial family has its own event queue (section 3).

### 1. Second class driver

* **Vendored component.** `lib/usb_host_cdc_acm`: Espressif's `usb_host_cdc_acm` (esp-usb, `host/class/cdc/usb_host_cdc_acm`), Apache-2.0, with its `LICENSE`, pinned to version 2.4.1 after a build check against the platform of `platformio.ini` (pioarduino 55.03.312-1, ESP-IDF 5.5.5). It is vendored unchanged in a commit of its own, so the review sees the upstream files apart from our code. Local changes are marked `[esp32-nut]`, as in `lib/usb_host_hid`, and the component is listed in `NOTICE`.
* **One USB host.** `USBHostUPS::begin()` installs the CDC-ACM driver after `hid_host`, on the same `usb_host_install()` and the same `usb_host_events` task. Both class drivers are clients of the one USB Host Library, which allows several clients. A device is opened by both, but each claims only its own interfaces.
* **Callbacks only post (ADR 0008).** The driver's new-device, event and data callbacks run in its background task. They only copy what they are given and post it to the serial event queue (section 3), or count received bytes. No decision, no transfer and no application lock happen in a callback.
* **In every image.** S3, Arduino Nano ESP32 and P4 get the same code. There is no `#if` per board or family (ADR 0010); the USB port remains a board profile entry. The flash cost is measured on every image in the spec.

### 2. Device-level family attach

* **New family `USB_SERIAL`** in `DriverRegistry`. It is chosen at device level, before any interface is claimed:
  1. A device with an HID interface stays with `hid_host` and the existing attach (explicit HID rows, Q* rows, generic HID fallback, ADR 0013). For a composite device with both HID and CDC interfaces, HID wins and the serial path does not claim it.
  2. A device without an HID interface is classified by the serial rows: first explicit VID/PID rows naming the bridge adapter (04e2:1410 → `xr21v141x`), then a class fallback for a standard CDC-ACM device (Communication interface class 0x02, subclass 0x02, plus a Data interface class 0x0A) → adapter `cdc-acm`. The rows follow the device tables of the Linux USB serial drivers (IDs only, no code).
  3. A device that matches nothing is logged once with its VID:PID and interface classes ("USB device not supported") and left alone. Today it is silent.
* **Classification data copied in the callback.** The `usb_device_handle_t` the new-device callback receives is valid only during the callback (the driver closes it right after). The callback therefore copies what the classification needs into a fixed-size record: the address, VID, PID, device class, the manufacturer, product and serial strings, and the interface and endpoint layout parsed from the configuration descriptor by a pure parser. It posts that record and decides nothing.
* **Open in the poll task.** The classification and `cdc_acm_host_open_v2()` by device address run in the poll task with a bounded timeout, never in a callback. The result (ESP-IDF error name, interfaces, endpoints) is kept for the System Log and the export.
* **Own files.** The serial attach and its device state live in their own source files of `usb-transport`. The HID logic of `USBHostUPS.cpp` gets no `if (serial)` branch; it only hands the serial path its turn in the poll task.

### 3. An event queue of its own for the serial family

* The serial family gets its own small FreeRTOS queue: the new-device records of section 2 and the driver events of the opened device (disconnection, error, serial state). The poll task drains it in `processEvents()`, after the HID events.
* The HID event queue (`HidEvent`), its traits and the Q* backlog (`EventBacklog`) stay unchanged. The two families never share a device (section 2), so no order between their events matters. A Q* command waiting for an interrupt IN chunk never sees a serial event, and the HID path needs no refactoring.
* The enumeration watchdog keeps counting every configured device (`configuredDeviceCount()`). A serial new-device record also sets `_enum_seen`, as the HID `CONNECTED` event does.

### 4. Serial port and chip adapters

* **`ISerialPort`** (module `usb-transport`), introduced with the first protocol (section 6), is the only interface the layers above will see: line settings (baud rate, data bits, parity, stop bits), the control lines (DTR, RTS), write with a timeout, read with a timeout from the RX buffer, discard input, byte counters and the text of the last error. It is the serial counterpart of `IQxUsbPrimitives` and is implemented in the poll task with no application lock held.
* **One adapter per bridge chip**, named by the registry row:
  * `cdc-acm`: standard CDC-ACM, line settings with `SET_LINE_CODING` and `SET_CONTROL_LINE_STATE`.
  * `xr21v141x`: XR21V1410/1412/1414. The bulk data path is the CDC-ACM one, and the line settings are written as vendor register requests through the driver's custom request call, implemented from the MaxLinear datasheet.
  * Later and on request: CH34x (#34), CP210x, FTDI, PL2303, each from its datasheet or a compatibly licensed reference.
* **First delivery:** the registry row only names the adapter, and the System Log and the export report it. The device is opened through the CDC-ACM driver, which starts receiving; no line settings are applied, no control line is set and no byte is sent. Incoming bytes are counted and dropped. `ISerialPort`, the adapters and an RX buffer come with the first protocol (section 6), because only the protocol knows the line settings (for example 2400 8N1 for the Megatec family).

### 5. Visibility before a protocol (the US-132 pattern)

As long as no serial protocol driver exists, an opened serial device behaves like a recognized Q* UPS without a driver (US-132):

* **Dashboard and banner.** `unsupportedDevice()` returns `USB serial: XR21V141x (04E2:1410)`, or `USB serial: CDC-ACM (VID:PID)` for the class fallback. `WebApiJson::upsStatusJson()` already turns it into `"Not supported yet"` and the unsupported banner. `isConnected()` stays false, so the NUT server and the UPS pages see no UPS.
* **No supervision without a protocol.** The link monitor, the INPUT watchdog, the export read, port cycles and restarts do not run for a serial device without a driver, as on the US-132 path. A disconnection releases the device.
* **System Log.** One INFO line for the recognized device (VID:PID, adapter, interface classes), one for the opened port (endpoint addresses and max packet sizes), one for every open or transfer error, and one when the device disconnects.
* **USB diagnostics export.** A new `serial` object, present only when a serial device is attached: VID, PID and strings, the adapter, every interface (class, subclass, protocol, endpoints with address, type and max packet size), the open result, the RX and TX byte counters, the last driver event and the last error. The HID and Q* parts of the export stay byte-identical (`test_usb_diag_json`).

### 6. The protocol on top (direction, decided in its own spec)

* **Q* over serial.** An `IQxTransport` implementation on `ISerialPort`: a port of the serial path of `nut_repo/drivers/nutdrv_qx.c` (send the command, read up to `\r` with the upstream timeouts, flush). The Q* engine, protocols, probing, control and the `protocol` and `novendor` settings are reused unchanged. This requires that `QxDriver` takes a transport base that is not tied to `IQxUsbPrimitives`, which is a refactoring step of its own.
* **Other serial protocols** are `IUPSDriver` implementations ported from their NUT drivers (ADR 0003), selected like the Q* protocols.
* **Supervision of the serial family** (stale data after failed commands, recovery through port power cycle and restart) is defined with the first protocol, following the Q* rules of ADR 0013 section 2.
* Which protocol the APC SRV2KI-E speaks is not known yet. The visibility of section 5 is how it will be found out.

### 7. Tests

`USBHostUPS.cpp` is not built by the native environment (`lib_ignore = USBHostUPS`), and `test_usb_host` runs only on a board. "The existing tests stay green" therefore does not cover the event path by itself. The rule is the one `EnumWatchdog`, `RestartPolicy` and `PortCyclePolicy` already follow: **every decision is pure logic in a header-only class with native tests, and `USBHostUPS` only wires it to ESP-IDF.**

* **Native:**
  * **Configuration descriptor parser** on raw descriptors: a CDC-ACM device with an interface association, the XR21V1410 layout, a composite HID + CDC device, a truncated or malformed descriptor (parsed as far as valid, marked truncated), and more interfaces or endpoints than the record holds.
  * **Classification** on fake device and configuration descriptors: 04e2:1410, a standard CDC-ACM device, a composite HID + CDC device (HID wins, no serial claim), a vendor-class device with no row (not supported, logged once), and a device with only a Communication or only a Data interface (not claimed).
  * **Serial attach state:** open success, open failure, disconnection while open, a disconnection of another device, reconnection, the byte counters, and the `unsupportedDevice()` text for each adapter. No supervision action (port cycle, restart) for a serial device without a driver.
  * `ISerialPort`, the RX buffer and the adapters (including the `xr21v141x` register sequence) get their tests with the first protocol, when they are introduced.
  * **Export:** the `serial` object, and the HID and Q* export unchanged byte for byte (`test_usb_diag_json`). `WebApiJson::upsStatusJson()` with a serial device without a driver (`test_web_api`).
* **E2E:** the dashboard banner for a serial device without a driver (as `e2e/unsupported-ups.spec.ts` does for Q*).
* **Build:** all four images (`esp32-s3-standard`, `arduino-nano-esp32`, `esp32-p4-eth`, `esp32-p4-eth-v1`) build, as in CI, and the flash and RAM cost is noted per image.
* **Hardware**, with a written checklist in the PR:
  * The APC SRV2KI-E on a Waveshare ESP32-P4-NANO (`esp32-p4-eth` profile, USB 2.0 HS port): banner, log lines, `serial` export, no port cycle after boot, unplugging and replugging.
  * A board without a UPS: unchanged enumeration watchdog behavior.
  * A real HID UPS on the new build before release: the HID event path is unchanged, but the second class driver shares the USB host with `hid_host`. A tester may be needed if the authors own none.

### 8. Rollout

Each step is its own commit, and the PRs are cut as agreed with the maintainer:

1. This ADR (Proposed).
2. The vendored `usb_host_cdc_acm` 2.4.1, unchanged (with `NOTICE`).
3. The `USB_SERIAL` family with the 04e2:1410 row and the class fallback, the serial event queue, the open by address and the visibility of section 5, with native and e2e tests.
4. The protocol (section 6), after the device has been made visible on the real unit.

## Open Questions

* Whether the XR21V1410 of the SRV2KI-E presents exactly the interface layout of the datasheet. The `serial` export of step 3 answers this on the real unit.
* The behavior of the CDC-ACM driver on the P4 High-Speed port with a full-speed bridge (the hub limitation of #113 applies as for HID).

## Consequences

### Positive
* USB serial UPS units become visible and diagnosable with no change to the HID behavior: the device, its interfaces and the open result are in the dashboard, the System Log and the export before a single protocol line is ported.
* The serial-bridge family of #34 has a place: a new bridge is one registry row and one adapter.
* The Q* engine and the consumer side are reused, so a serial Q* UPS needs only a transport port, not a second engine.
* One USB owner, one poll task, one status computation, one consumer API (ADR 0002, 0008, 0010). The HID event path is not changed at all.

### Negative
* A second vendored component to keep up to date with ESP-IDF, as `usb_host_hid` already is.
* A second class driver task, its transfer buffers and a second event queue cost RAM and flash on every image.
* Two class drivers share the USB host: the HID path needs a test on real HID hardware although its code is unchanged.
* Every bridge adapter depends on a tester with real hardware. Line settings written from a datasheet can only be confirmed on a device.

## Impact on Agent Implementation

* Do not add serial branches inside the HID logic of `USBHostUPS`. Serial attach, state and diagnostics live in their own files, and family decisions belong to the registry and the device-level attach.
* Callbacks of `usb_host_cdc_acm` only copy into fixed-size records, post them to the serial event queue, or count bytes. Opening, line settings, writes and waits run in the poll task with no application lock held and with bounded timeouts (ADR 0008).
* A device with an HID interface is never claimed by the serial path.
* Do not copy code from the Linux USB serial drivers (GPL-2.0-only). Their device ID tables may be used as data, and their behavior may be cited, but the adapters are written from the chip datasheets.
* Bridge-chip specifics will live only in their adapter behind `ISerialPort`. Protocol code never sees a USB request.
* The first delivery sends no byte to the device and applies no line settings.
* The HID and Q* parts of the diagnostics export must stay byte-identical. The `serial` object is added only when a serial device is attached.
* The HID event queue, `HidEvent`, its traits and `EventBacklog` are not changed for the serial family; serial events use their own queue.
* The vendored component is committed unchanged and apart from our code; later local changes are separate commits.
* Local changes to `lib/usb_host_cdc_acm` are marked `[esp32-nut]` and the component stays listed in `NOTICE`.
