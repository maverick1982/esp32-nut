---
title: "Hardware Requirements"
type: manual
tags: [user-manual]
---

# Hardware Requirements

ESP32-NUT uses the USB host controller built into some Espressif chips to talk to the UPS. The choice of board matters.

## Supported Boards

| Board | Network | Status LED | Notes |
| :--- | :--- | :--- | :--- |
| **ESP32-S3** development board (e.g. ESP32-S3 DevKitC-1 and compatible boards with two USB-C ports) | Wi-Fi | Yes (RGB LED on GPIO 48, if present) | The most common setup. |
| **Waveshare ESP32-P4-ETH** (also the PoE version) | Ethernet | No | UPS on the USB 2.0 High-Speed port. |
| **Waveshare ESP32-P4-WIFI6-POE-ETH** | Ethernet | No | Its ESP32-C6 Wi-Fi chip is not used: the board runs on Ethernet. |
| Other ESP32-P4 boards following the Espressif reference design (IP101 PHY) | Ethernet | No | A board wired differently needs its own profile: open an issue. |

> [!WARNING]
> Standard ESP32 boards (e.g. ESP32-WROOM-32), ESP32-C3 and ESP8266 **will not work**: they have no USB host controller.

## ESP32-S3: Wiring and Power

Generic ESP32-S3 boards with two USB-C ports (labeled `COM`/`UART` and `USB`) need a small preparation, with no wires on the GPIO pins:

1. **Enable host mode ("USB-OTG" pads):** on the back of the board, bridge the two pads labeled `USB-OTG` with a drop of solder. This sends 5 V to the `USB` port, so it can power the USB interface of the UPS.
2. **Enable the status LED ("RGB" pads, optional):** if the board has an RGB LED, bridge the `RGB` pads to turn it on.

Then connect:

- **Power:** a USB wall charger to the `COM` (or `UART`) port.
- **UPS:** a **USB-C OTG** adapter in the `USB` port, and the UPS USB cable into the adapter.

## ESP32-P4: Wiring and Power

- **UPS:** on the USB 2.0 High-Speed port (on the Waveshare ESP32-P4-ETH, the 4-pin header J3).
- **Network:** an Ethernet cable. The board gets its address from your router (DHCP). There is no Wi-Fi and no setup access point.
- **Power:** from the board's USB-C port, or from PoE on the PoE versions.

### Chip revision: two images

The ESP32-P4 comes in different chip revisions, and each one needs its own firmware image:

| Image | Chip revision | Hardware tests |
| :--- | :--- | :--- |
| ESP32-P4 v3.1 or later | v3.1 and later (current production) | Tested on a v3.2 board, with a UPS |
| ESP32-P4 before v3 | v1.x (early boards) | Tested on a v1.3 board, with a UPS |

To find the revision, run `esptool chip-id` (`esptool.py chip_id` with esptool v4): the output says, for example, `Chip is ESP32-P4 (revision v1.3)`. The serial boot log shows it too, as `chip revision: v1.3`. Revisions v2.x and v3.0 are not supported by either image.

An image for the wrong revision is refused, by the bootloader on a full install and by the board on an OTA update, so you can simply install the other one.

## USB Hubs and Active Extension Cables

A long USB cable with a built-in booster contains a USB hub.

- **ESP32-P4:** on the High-Speed port, a high-speed hub hides the UPS (a full-speed device): the USB host stack cannot reach a full-speed device behind a high-speed hub. Use a passive cable, or put a full-speed-only USB isolator (based on the ADuM3160) between the board and the cable.
- **ESP32-S3:** its USB port is full-speed only, so such a cable should work without the isolator (not tested).

## The UPS

ESP32-NUT supports UPSes that use the standard **USB HID Power Device Class**, the ones NUT handles with the `usbhid-ups` driver. Many models from Eaton/MGE, APC, CyberPower, Powercom, Legrand, Powerwalker, WalleCube and others are tested: the full list is in the [README](https://github.com/maverick1982/esp32-nut#-supported-ups-devices). A UPS that is not recognized uses a generic driver that reads the standard values.

UPSes that talk over a USB-to-serial converter or a serial protocol (Megatec/Q1, USB CDC, RS-232 through an RJ45 cable) are not supported.

The UPS needs a real USB port (usually type B, the square one found on printers).

## 3D Printed Case (Optional)

A compact case for the ESP32-S3 board is available for free:

- [**Printables**](https://www.printables.com/model/1794471-case-esp32-nut-server-bridge)
- [**Thingiverse**](https://www.thingiverse.com/thing:7389257)

The source models and CAD files are in the [`hardware/case`](https://github.com/maverick1982/esp32-nut/tree/main/hardware/case) folder of the repository.
