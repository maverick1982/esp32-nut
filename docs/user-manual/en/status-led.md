---
title: "Status LED (Colors and Blinking)"
type: manual
tags: [user-manual]
---

# Status LED (Colors and Blinking)

This document provides a quick guide to interpreting the behavior of the RGB status LED (typically a WS2812 or similar, integrated on the board or connected externally). 

The LED provides immediate diagnostic feedback on the system's vital status (Wi-Fi and USB connection with the UPS).

## State Legend

| Color | Blinking Pattern | Meaning (State) | Technical Description |
| :--- | :--- | :--- | :--- |
| 🔵 **Blue** / 🔴 **Red** | Fast alternating (4s cycles) | **Setup Mode (AP_MODE)** | The device is in Access Point mode. It waits for the user to connect to the temporary Wi-Fi network for initial configuration. The animation lasts 4 seconds, followed by 4 seconds off. |
| 🟡 **Yellow** | Continuous slow blink | **Connecting (CONNECTING)** | The device is attempting to establish a connection with the configured Wi-Fi network. This state is typically visible for a few seconds during startup. |
| 🟢 **Green** | Short pulse every 5 seconds | **Operational (OPERATIONAL)** | Normal operation. The device is successfully connected to Wi-Fi, the NUT server is listening, and the UPS has been detected and is communicating on the USB port. (The pulse is short to avoid disturbance in dark environments). |
| 🔴 **Red** | Continuous fast blink | **Error (ERROR)** | An anomaly condition has occurred. Common causes include failure to connect to the saved Wi-Fi network, NUT server initialization failure, or the UPS USB cable is unplugged/unrecognized. |

> [!TIP]
> If the LED indicates an Error state (Fast blinking red), we recommend consulting the dedicated [Troubleshooting](troubleshooting.md) section or checking the system logs via the web interface to identify the exact cause of the anomaly.
