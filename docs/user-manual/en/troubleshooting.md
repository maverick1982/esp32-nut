---
title: "Troubleshooting"
type: manual
tags: [user-manual]
---

# Troubleshooting

This section helps you diagnose and resolve the most common issues you might encounter while using ESP32-NUT.

## Integrated Diagnostics (System Logs)

The first step to resolving any anomaly is to consult the system logs.
Access the device's web interface by typing its IP address into your browser and navigate to the **System Logs** tab. Here you can read in real-time what the firmware is doing (e.g., Wi-Fi connection errors, USB handshake attempts).

## Issue: "UPS Not Detected" (Red LED or UI Error)

If the Web UI shows that no UPS is connected, or the status LED blinks rapidly in red, check the following points:

1. **USB-OTG pads not soldered:** If you are using a generic ESP32-S3 board with two USB-C ports, ensure you have bridged the two small rear pads labeled `USB-OTG` with solder. Without this bridge, the board does not send 5V power to the USB port, and the UPS will not turn on (logically).
2. **Incorrect Cable / Adapter:** Ensure you are using a true **USB OTG** (On-The-Go) adapter. Standard physical adapters (often sold just for charging) lack the necessary internal resistor (on the CC pins) to signal to the ESP32 that it should act as a "Host" rather than a peripheral. Without a specifically certified OTG cable/adapter, data communication will not initiate.
3. **Reversed Ports:** Check that the wall power is connected to the `COM`/`UART` port, and the UPS cable is connected to the port labeled `USB`.

## My UPS uses a Serial connection (RJ45 to USB, Megatec, etc.)

Currently, ESP32-NUT supports **only and exclusively** UPS models that present themselves as USB HID (Human Interface Device) devices compliant with the *USB HID Power Device Class* specification (`usbhid-ups` driver).

UPS units that communicate via Serial interfaces emulated over USB (e.g., ch340, ft232 converters) or proprietary serial protocols are not natively supported by this branch of the project.

## UPS recognized, but data is missing (e.g., Battery Level at 0%)

Various manufacturers (CyberPower, Eaton, APC) implement the HID tree slightly differently. If your UPS is recognized but uses the **Generic Driver** and vital data is missing, we can add specific support!

**How to request support for your UPS:**
1. Go to the ESP32 Web UI, in the **System Logs** tab.
2. Click the **Export USB Diagnostics** button. A file named `usb_diagnostics.json` containing your UPS's HID descriptor tree will be downloaded.
3. Open a new *Issue* on the project's GitHub page and attach this file, indicating the exact make and model of your UPS. It will be used to write a custom driver!
