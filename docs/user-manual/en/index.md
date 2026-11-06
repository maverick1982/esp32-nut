---
title: "Introduction"
type: manual
tags: [user-manual]
---

# Introduction

Welcome to the ESP32-NUT User Manual. It explains how to choose the hardware, install the firmware, configure the board and connect your NUT clients to it. It describes firmware v1.14.0.

## What is ESP32-NUT?

ESP32-NUT is an open-source firmware that turns a USB UPS into a network UPS. The board acts as a USB host for the UPS, reads its data and publishes it on your local network with the standard Network UPS Tools (NUT) protocol, on port 3493. Home Assistant, Synology, TrueNAS, PeaNUT, `upsmon` and any other NUT client can use it as if it were a NUT server running on a PC, without a Raspberry Pi or a computer left on next to the UPS.

It runs on two families of boards:

- **ESP32-S3**, connected over **Wi-Fi**;
- **ESP32-P4** with an **Ethernet** port (PoE on the boards that have it).

## Key Features

- 🔌 **USB host for the UPS:** reads the standard USB HID Power Device data, with brand-specific drivers for Eaton/MGE, APC, CyberPower, Powercom and more, and a generic driver for the others.
- 🔗 **Built-in NUT server:** standard NUT protocol on port 3493, up to 8 clients at the same time, with the same variable names as NUT's `usbhid-ups` driver.
- ⚡ **UPS commands and settings:** battery tests, beeper, load and shutdown commands, and the writable UPS parameters, from the web interface or with `upscmd` and `upsrw`.
- 🏷️ **Custom NUT variables:** replace a wrong value or add a missing one for NUT clients, like `override.*` in NUT's `ups.conf`.
- 🔋 **Automatic shutdown:** the board can shut the UPS down by itself on low battery, also with no NUT client connected.
- 🖥️ **Web interface with login:** live data, commands, settings, network, logs and firmware updates, protected by a username and password.
- 🌐 **Network:** Wi-Fi with a setup access point (ESP32-S3) or Ethernet (ESP32-P4), with a hostname you choose.
- 📜 **Logs and diagnostics:** System Log in the browser, remote syslog, network time, and a USB diagnostics export for bug reports.
- 🔄 **OTA updates:** install new versions from the browser.
- 💡 **Status LED** on the ESP32-S3 boards.

## Target Audience

- **Smart home users** who want a remote UPS in Home Assistant when the UPS is far from the server.
- **Homelab users** who want to monitor load, battery and power events of one or more UPSes over the network with minimal hardware.

## How to Use This Manual

Follow the chapters in order the first time:

1. **[Hardware Requirements](hardware.md):** the board, the cables and the UPS.
2. **[Installation](installation.md):** flash the firmware and keep it up to date.
3. **[Initial Configuration](configuration.md):** connect the board to your network and secure the web interface.
4. **[Web Interface](web-interface.md):** what each page does.
5. **[Client Integration](integration.md):** add the UPS to Home Assistant, a NAS or a Linux client.

The **[Status LED](status-led.md)**, **[Troubleshooting](troubleshooting.md)** and **[FAQ](faq.md)** chapters help when something does not work as expected.

## Useful Links

- [GitHub Repository](https://github.com/maverick1982/esp32-nut): source code, releases and the full list of tested UPS models.
- [Web Installer](https://maverick1982.github.io/esp32-nut/): install the firmware from the browser.
- [Issue Tracker](https://github.com/maverick1982/esp32-nut/issues): report a bug, request a feature or send a UPS compatibility report.
