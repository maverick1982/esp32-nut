---
title: "Status LED (Colors and Blinking)"
type: manual
tags: [user-manual]
---

# Status LED (Colors and Blinking)

The RGB status LED of the ESP32-S3 boards (on GPIO 48, enabled by the `RGB` pads) shows the state of the network and of the UPS at a glance. The ESP32-P4 boards have no status LED: use the web interface instead.

## State Legend

| Color | Pattern | State | Meaning |
| :--- | :--- | :--- | :--- |
| 🔵 **Blue** / 🔴 **Red** | Fast alternation for 4 s, then 4 s off | **Setup access point** | The board is in access point mode (`NUT_ESP32_Config`) and waits for you to connect and enter the Wi-Fi settings. |
| 🟡 **Yellow** | Slow blink (1 s on, 1 s off) | **Connecting** | The board is not connected to the Wi-Fi network yet. It is normal for a few seconds after power-on. If it lasts, the Wi-Fi network is out of reach or the saved settings are wrong. |
| 🔴 **Red** | Fast blink | **No UPS** | The board is on the network, but no UPS is connected or recognized on the USB port. |
| 🟢 **Green** | Short flash every 5 s | **Operational** | Everything works: the board is on the network and reads the UPS. The flash is short so it does not disturb a dark room. |

## LED Settings

In **Board Settings → Status LED** you can turn the LED off or choose its brightness from 1 to 5. When off, it stays dark in every state, also after a restart.

> [!TIP]
> With a fast red or a long yellow blink, open the web interface and check the **System Logs** page, or see [Troubleshooting](troubleshooting.md).
