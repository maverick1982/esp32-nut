---
title: "Introduction"
type: manual
tags: [user-manual]
---

# Introduction

Welcome to the ESP32-NUT User Manual. This document provides comprehensive guidelines and instructions for the installation, configuration, and integration of the ESP32-NUT system.

## What is ESP32-NUT?
ESP32-NUT is an open-source, smart bridge device designed to transform a standard, local USB Uninterruptible Power Supply (UPS) into a fully network-capable device. It leverages the ESP32-S3 microcontroller to act as a USB Host, communicating with the UPS and exposing its data over Wi-Fi using the standard Network UPS Tools (NUT) protocol. This eliminates the need for a dedicated PC or Raspberry Pi running 24/7 near your UPS.

## Key Features
- 🔌 **Plug & Play USB Host:** Direct support for standard USB UPS devices (e.g., CyberPower, APC) utilizing the native USB capabilities of the ESP32-S3.
- 🌐 **Wi-Fi Connectivity & Captive Portal:** Effortless initial setup via smartphone or computer without writing a single line of code.
- 🔗 **Built-in NUT Server:** 100% native compatibility with external clients such as Home Assistant, pfSense, TrueNAS, and Synology.
- 💡 **Diagnostic LED:** Immediate visual feedback regarding network and UPS connection status.

## Target Audience
This project is designed for:
- **Smart Home Users:** Ideal for integrating a remote UPS into Home Assistant when the UPS is physically far from the main server.
- **Homelab Enthusiasts:** Perfect for monitoring power consumption, load, and battery health over the local network with minimal hardware overhead.

## How to Use This Manual
To get started, we recommend following the chapters in order:

1. **Hardware Requirements:** Ensure you have the correct board and cables.
2. **Installation:** Flash the firmware onto the ESP32-S3.
3. **Configuration:** Connect the device to your Wi-Fi and configure the NUT server.
4. **Client Integration:** Add your UPS to your favorite monitoring software.

## Useful Links
- [GitHub Repository](https://github.com/maverick1982/esp32-nut): Source code and releases.
- [Issue Tracker](https://github.com/maverick1982/esp32-nut/issues): Report bugs or request new features.
