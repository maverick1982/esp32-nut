---
title: "Initial Configuration"
type: manual
tags: [user-manual]
---

# Initial Configuration

This chapter connects the board to your network and secures the web interface. The steps differ between the ESP32-S3 (Wi-Fi) and the ESP32-P4 (Ethernet).

## ESP32-S3: Wi-Fi Setup

### 1. Start the setup access point

The board offers its own Wi-Fi network, the **setup access point**, to receive your Wi-Fi settings.

- **First boot:** with no Wi-Fi settings saved, the access point starts **automatically**.
- **Later:** the access point never starts by itself, not even when the Wi-Fi signal is lost. To start it, use a **double reset**:
  1. power the board;
  2. **within 3 seconds**, unplug the power;
  3. plug it in again: the board starts in access point mode.

Then, from a phone or a computer:

1. connect to the Wi-Fi network **`NUT_ESP32_Config`**;
2. enter the password **`12345678`**.

### 2. Open the setup page

A captive portal page usually opens by itself. If it does not, open a browser and go to `http://192.168.4.1`.

In access point mode the web interface is open **without login** and gives full access (credentials, settings, OTA, UPS commands). Keep the access point session short.

### 3. Enter your Wi-Fi network

1. Open the **Network** page.
2. In **Connection Setup**, type the name of your Wi-Fi network in **Network Name (SSID)** and its password in **Security Key**.
3. Press **Initialize Connection**. The board saves the settings and restarts on your network.

If the name or the password is wrong, the board does not go back to access point mode by itself: use the double reset again.

With valid settings saved, the board leaves access point mode on its own when no device has been connected to it for 10 minutes. While a device stays connected, it stays in access point mode.

> [!NOTE]
> While the board is in access point mode it is not on your home network, so Home Assistant and other NUT clients cannot reach it. With valid settings the NUT server stays on, but only for devices connected to the access point.

### 4. Find the board on your network

The board gets its address from your router (DHCP). In the router's DHCP table it appears as **`esp32-nut-xxxxxx`** (the last 3 bytes of its MAC address), or with the hostname you choose later in the **Network** page. To always find it at the same address, reserve one for its MAC address in the router.

## ESP32-P4: Ethernet Setup

There is no access point and no Wi-Fi setup:

1. plug in the Ethernet cable;
2. find the board in the router's DHCP table as **`esp32-nut-xxxxxx`**;
3. open its address in the browser and continue with the web login below.

The NUT server starts with the default settings: UPS name `ups` and no NUT credentials.

## Web Interface Login

The web interface is protected by a username and a password.

### Create the credentials

The first visit to the web interface from your network shows only the **Create credentials** form. Whoever creates them first becomes the administrator, so do it right after the installation.

- **Username:** 1 to 32 printable characters.
- **Password:** 8 to 64 bytes. Plain ASCII characters take 1 byte each, accented letters and emoji 2 to 4.

The password is stored as a salted PBKDF2-SHA256 hash, never in clear text.

### Sessions

- Every page and every API needs the login, firmware updates included.
- A session ends after 30 minutes without requests and at every restart. An open page keeps polling the board, so it stays signed in while it is open.
- Tick **Remember me** when you sign in to stay signed in for 30 days, across restarts and firmware updates. At most 8 devices can be remembered: signing in on a 9th signs out the oldest one.
- **Log out** in the sidebar signs out this device. **Log out of all devices** closes every session, remembered ones included.
- After 5 wrong passwords in a row, each further attempt waits 1 s, then 2, 4… up to 60 s. There is no permanent lockout, and the NUT server keeps running.

> [!WARNING]
> The web interface is served over plain HTTP: passwords travel unencrypted on the network. Use it on a trusted network and do not reuse a password you use elsewhere.

### Change the credentials

Open the **Web Login** page, enter the current password and the new username and password, then press **Save credentials**. Saving signs out every other device; this one stays signed in.

The web credentials are separate from the NUT credentials (**NUT Server** page): changing one does not change the other.

### Forgotten password

**ESP32-S3:**

1. Use the double reset to start the `NUT_ESP32_Config` access point.
2. Connect to it, open the web interface and go to the **Web Login** page.
3. Set the new credentials with **Reset credentials**: no current password is needed. All sessions are closed.
4. Restart the board to go back to your network. It also goes back by itself 10 minutes after the last device disconnects from the access point.

**ESP32-P4:** a double reset (power off and on again within 3 seconds) opens a 10-minute window in which the **Web Login** page lets you set new credentials without the current password.

Entering access point mode does not erase the web credentials, and changing only the Wi-Fi settings keeps them too.

## NUT Server Settings

Open the **NUT Server** page to set what NUT clients use to connect:

- **UPS Name:** the name clients use to address the UPS (default `ups`), as in `upsc ups@192.168.1.50`. It cannot be empty and accepts printable characters without spaces, `"`, `\`, `#` and `@`.
- **Username** and **Password:** the NUT credentials. They are needed to run commands and change settings from NUT clients (`upscmd`, `upsrw`, Home Assistant commands). Reading the data does not need them, as with NUT. With no credentials set, every client is allowed.

Press **Save Credentials**: the board saves the settings and restarts.

---

Next step: a tour of the [Web Interface](web-interface.md), or go straight to [Client Integration](integration.md).
