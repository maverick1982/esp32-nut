---
title: "NUT Client Integration"
type: manual
tags: [user-manual]
---

# NUT Client Integration

Once your ESP32-NUT is connected to the Wi-Fi network and the UPS has been successfully detected, the device will operate exactly like a traditional NUT server on the local network.

This means it is "plug & play" with any software client compatible with the standard NUT protocol.

## Connection Parameters

To configure your client (Home Assistant, pfSense, TrueNAS, or a generic Linux client), you will need these four parameters:

- **IP Address (Host):** The IP address assigned to the ESP32 by your router (DHCP). You can find this on your router's page or via the ESP32 Web UI.
- **Port:** `3493` (the standard NUT protocol port).
- **UPS Name:** The name you chose during the [Initial Configuration](configuration.md) (e.g., `living_room_ups`).
- **Username / Password:** The credentials set during configuration.

## Example 1: Home Assistant

The official Network UPS Tools integration in Home Assistant is the quickest way to get a dashboard.

1. In Home Assistant, go to **Settings > Devices & Services**.
2. Click **Add Integration** and search for "Network UPS Tools (NUT)".
3. Enter the IP Address (Host) of the ESP32 and leave the port as `3493`.
4. Enter the Username and Password.
5. Submit the form: Home Assistant will automatically detect the name of your UPS and start creating sensor entities (Battery, Load, Voltage, etc.).

## Example 2: Generic Linux Client (`upsc`)

If you are testing the server from a Linux machine (e.g., a Raspberry Pi or an Ubuntu server), you can use the standard command-line client `upsc`.

Install the client (e.g., `sudo apt install nut-client`), then run the command using the syntax `ups_name@ip_address`:

```bash
upsc living_room_ups@192.168.1.50
```

If the connection is successful, you will see the complete list of telemetry data provided by the UPS printed in the terminal.
