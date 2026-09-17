---
title: "Hardware Requirements"
type: manual
tags: [user-manual]
---

# Hardware Requirements

This project is specifically designed to leverage the native USB Host capabilities of Espressif microcontrollers. Therefore, hardware selection is crucial.

## Supported Board: ESP32-S3

To use ESP32-NUT, **an ESP32-S3 development board is strictly required**.

> [!WARNING]
> Standard ESP32 boards (e.g., ESP32-WROOM-32), ESP8266, or ESP32-C3 **will not work**, as they lack the native USB OTG (On-The-Go) controller required to communicate directly with the UPS.

## Wiring and Power

The most common and recommended setup involves using a generic ESP32-S3 development board equipped with two USB-C ports (typically labeled `COM`/`UART` and `USB`).

To achieve a clean setup without soldering external wires to GPIO pins, you simply need to bridge a few pads on the back of the board:

1. **Enable Host Mode ("USB-OTG" Pad)**: Locate the two small solder pads on the back of the board labeled `USB-OTG`. Bridge them with a drop of solder. This step routes 5V power to the `USB` port, allowing it to act as a "Host" and power the UPS USB interface.
2. **Enable Status LED ("RGB" Pad - Optional)**: If your board has an integrated RGB LED (e.g., WS2812), locate the `RGB` pads on the back and bridge them. This enables visual feedback for the system status.

### Final Connections

Once the board is prepared, make the connections as follows:

- **Power:** Connect a standard USB wall charger to the port labeled `COM` (or `UART`). This port will provide main power to the ESP32.
- **Data (UPS):** Connect a **USB-C OTG** adapter to the port labeled `USB`, and plug the USB cable from your UPS into this adapter.

## 3D Printed Case (Optional)

If you have access to a 3D printer, you can turn your bare ESP32-S3 board into a finished product. We designed a compact, custom-fit case for this project.

You can download the ready-to-print STL/3MF files for free from the following links:
- [**Printables**](https://www.printables.com/model/1794471-case-esp32-nut-server-bridge)
- [**Thingiverse**](https://www.thingiverse.com/thing:7389257)
