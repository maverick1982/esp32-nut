---
title: "Initial Configuration"
type: manual
tags: [user-manual]
---

# Initial Configuration

This document outlines the steps to connect ESP32-NUT to your local Wi-Fi network and configure the NUT (Network UPS Tools) server parameters.

## 1. Entering Access Point (AP) Mode

To configure the device, it must be in **Access Point (AP) Mode**.

**On Very First Boot (after installation):**
Since there is no configuration saved in memory, the device will enter AP mode **automatically**. Simply connect power and wait a few moments.

**Manual Override (for subsequent configurations):**
If the device was previously configured but you need to change the Wi-Fi network or other parameters, the AP *will never activate on its own* (even if the signal is lost). You must trigger it manually using this procedure:
1. Connect power to the ESP32-S3 board via the `COM` or `UART` port.
2. **Within 3 seconds** of powering on, abruptly disconnect the power (unplug the cable).
3. Reconnect power to restart the board. 
4. On this consecutive boot, the device will ignore the saved configuration and enter AP Mode.

Once AP Mode is active:
1. Using a smartphone or computer, scan for available Wi-Fi networks.
2. Connect to the network named **`NUT_ESP32_Config`**.
3. When prompted, enter the default password: `12345678`.

## 2. Accessing the Captive Portal

On most modern operating systems, a browser window known as a "Captive Portal" will open automatically as soon as you connect to the device's network.

If the Captive Portal does not launch automatically:
- Open your web browser.
- Manually type the following address into the URL bar: `http://192.168.4.1`

## 3. Parameter Configuration

The web configuration interface is divided into intuitive sections:

### Wi-Fi Settings
- Select the name (SSID) of your home or business network from the dropdown list.
- Enter the password for your Wi-Fi network. Ensure the data is correct, otherwise, the device will fail to connect. Remember that in case of error, the device **will not return to AP Mode on its own**: you must repeat the Manual Override procedure (disconnecting power within 3 seconds) to re-enter the correct password.

### NUT Server Settings
- **UPS Name:** Assign an identifier to your UPS (e.g., `living_room_ups`). **Warning:** this field cannot contain spaces.
- **Username:** Choose a username you will use in external clients (e.g., Home Assistant, NAS) to access UPS data.
- **Password:** Set a secure password associated with the username.

> [!NOTE]
> The two configuration modules behave differently:
> - Saving the **NUT Server Settings** takes effect immediately and *does not reboot* the board.
> - Clicking the **"Initialize Connection"** button (to save Wi-Fi Settings), however, will **automatically reboot** the device to attempt connection to your local network.
