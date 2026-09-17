---
title: "Frequently Asked Questions"
type: manual
tags: [user-manual]
---

# Frequently Asked Questions (FAQ)

### Can I use a normal ESP32 or an ESP8266?
**No.** This project strictly requires an **ESP32-S3** microcontroller. Only the S3 (and S2) series features the internal hardware necessary to act natively as a USB Host (On-The-Go) and communicate directly with the UPS. Standard ESP32 models lack this functionality.

### Do I have to solder?
It depends on the board you purchase. If you buy a "Generic ESP32-S3 DevKit" development board with two USB-C ports, 99% of the time, **yes**. You must join two small pads (labeled `USB-OTG`) on the back of the board so it can supply power to the UPS. If you have advanced skills, you can bypass this using external GPIO pins and step-up modules, but soldering the pads is the recommended path.

### Why doesn't the captive portal open automatically?
Some Android smartphones and iOS versions block the automatic opening of captive portals for security reasons or if they detect specific proxy settings. In these cases, simply connect to the `NUT_ESP32_Config` network, ignore the "Internet unavailable" warning, open a browser, and navigate to `http://192.168.4.1`.

### Can I connect the ESP32-NUT via Ethernet (LAN) cable?
The firmware is currently written and optimized to work over a Wi-Fi connection. ESP32-S3 boards equipped with an integrated Ethernet chip (such as the WT32-ETH01 series) do not easily expose native USB Host support. Therefore, Ethernet is not supported.

### My UPS has an RJ45 or RJ11 port on the back, can I use that?
The RJ45/RJ11 ports on the back of UPS units (often labeled "Surge Protection" or "Data Port") are typically used to protect telephone or network lines from surges, or they use proprietary RS232 serial protocols (via special RJ45-to-USB cables). **ESP32-NUT requires a true Type-B USB port on the UPS** (the square one, like those on printers) as it only supports the native USB HID protocol.
