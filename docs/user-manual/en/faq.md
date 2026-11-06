---
title: "Frequently Asked Questions"
type: manual
tags: [user-manual]
---

# Frequently Asked Questions (FAQ)

### Can I use a standard ESP32 or an ESP8266?
**No.** ESP32-NUT needs a chip with a USB host controller: it runs on **ESP32-S3** and on supported **ESP32-P4** boards. Standard ESP32, ESP32-C3 and ESP8266 boards cannot talk to the UPS.

### Can I connect ESP32-NUT with an Ethernet cable?
**Yes, with an ESP32-P4 board** such as the Waveshare ESP32-P4-ETH, also with PoE. The ESP32-S3 boards use Wi-Fi. See [Hardware Requirements](hardware.md).

### Do I have to solder?
On a generic ESP32-S3 board with two USB-C ports, almost always **yes**: you bridge the two `USB-OTG` pads on the back so the board can power the UPS interface. The ESP32-P4 boards need no soldering.

### Which ESP32-P4 image do I need?
It depends on the chip revision: **v3.1 or later** for current boards, **before v3** for the early v1.x boards. Run `esptool chip-id` to find it. A wrong image is simply refused, so you can install the other one. See [Chip revision](hardware.md#chip-revision-two-images).

### Why does the captive portal not open by itself?
Some phones and operating systems do not open captive portals automatically. Connect to the `NUT_ESP32_Config` network, ignore the "no Internet" warning, open a browser and go to `http://192.168.4.1`.

### How do I change the Wi-Fi network?
From the **Network** page while the board is on your network, or, if it can no longer connect, from the setup access point after a double reset (power off within 3 seconds of power-on, then on again). See [Initial Configuration](configuration.md).

### Are the web login and the NUT credentials the same?
**No.** The web login protects the web interface (**Web Login** page). The NUT credentials are used by NUT clients for commands and settings (**NUT Server** page). Changing one does not change the other.

### Is my data private?
The board does not send anything to the Internet, except the NTP time requests (which you can turn off in **Board Settings → Time**) and the syslog lines to the server you choose. The web interface uses plain HTTP, so use it on a trusted network.

### How many NUT clients can connect?
Up to 8 at the same time.

### Can the board shut the UPS down without a NUT client?
**Yes.** Turn on **Board Settings → Automatic shutdown**: on low battery the board sends `shutdown.default` to the UPS by itself. See [Automatic shutdown](web-interface.md#automatic-shutdown).

### My UPS reports a wrong model or no battery date. Can I fix it?
**Yes, for NUT clients.** Add a [custom variable](web-interface.md#custom-variables) in the **NUT Server** page, for example `ups.model` or `battery.date`.

### Do I lose my settings when I update?
**No.** OTA updates keep all settings. With the web installer, leave **"Erase device"** unchecked to keep them.

### My UPS has an RJ45 or RJ11 port on the back. Can I use it?
**No.** Those ports protect telephone or network lines, or use serial protocols. ESP32-NUT needs the **USB port** of the UPS (usually type B, the square one found on printers).
