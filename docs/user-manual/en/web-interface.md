---
title: "Web Interface"
type: manual
tags: [user-manual]
---

# Web Interface

Open the address of the board in a browser and sign in (see [Initial Configuration](configuration.md#web-interface-login)). The sidebar lists the pages described below.

## Sidebar and Status Banner

The sidebar shows:

- the firmware version;
- the state of the network link (Wi-Fi or Ethernet) and of the UPS;
- the signed-in user, with **Log out** and **Log out of all devices**.

A yellow box at the top of the page appears when something needs your attention:

- the UPS is not answering and its data are stale;
- the last automatic restart and its cause, for example `too many USB port power cycles in an hour`;
- the last crash or watchdog reset, with the task that stopped responding;
- degraded mode, when automatic restarts are suspended.

See [Troubleshooting](troubleshooting.md#the-yellow-box-automatic-restarts) for what each message means.

## UPS Telemetry

The live data of the UPS:

- **UPS Status** (for example `OL` on line, `OB` on battery, `LB` low battery);
- **Battery Charge**, **UPS Load** and **Real Power**;
- a table with every variable the UPS reports, its value and its NUT description.

If the UPS is not recognized and uses the generic driver, a banner asks you to export the USB diagnostics and open an issue (see [Troubleshooting](troubleshooting.md#ups-recognized-but-values-are-missing)).

## UPS Commands

The commands the UPS supports, as with NUT's `usbhid-ups` driver:

- **Tests & beeper:** battery and panel tests and beeper commands. The result of the last battery test is shown at the top.
- **Power control:** load and shutdown commands (`load.off`, `shutdown.return`, `shutdown.stayoff`, …). They ask for a confirmation first.

The list depends on the UPS model. With no UPS connected the page says so.

## UPS Settings

The UPS parameters you can change (`upsrw` in NUT), with their current values: for example low battery thresholds, transfer voltages, battery date and shutdown delays.

- The values are read from the UPS when the page opens and after each **Apply**. Changes take effect immediately.
- Parameters that switch outlets, bypass or ECO mode ask for a confirmation first.
- Values kept on the board, such as `ups.delay.*` and outlet names, have a **Reset to default** button.
- A variable with a custom value (see [NUT Server](#nut-server)) is read-only here, with a link to the NUT Server page.

## Network

One section for each network link of the board:

- **Wi-Fi** (ESP32-S3): connection status, network name (SSID), signal, IP address, gateway, MAC address and DHCP. **Connection Setup** changes the Wi-Fi network: type the network name and its password, then press **Initialize Connection**. The board saves the settings and restarts.
- **Ethernet** (ESP32-P4): connection status, link speed, cable, IP address, gateway, MAC address and DHCP.
- **Hostname:** the name the board sends to your router, for both Wi-Fi and Ethernet. Use 1 to 32 letters, digits and hyphens, with no hyphen at the start or end. Leave it empty to use the default `esp32-nut-xxxxxx`. Press **Save Hostname**: the board restarts with the new name.

When the board stops answering, the last values stay on the page greyed out, with "No answer for N s".

## NUT Server

### NUT Server Credentials

The **UPS Name** and the NUT **Username** and **Password** used by NUT clients. See [NUT Server Settings](configuration.md#nut-server-settings). Saving restarts the board.

### Custom variables

Values that NUT clients see in place of the ones reported by the UPS, or as extra variables when the UPS does not report them. They work like `override.*` in NUT's `ups.conf`. Examples:

- set `ups.model` to the real model name when the UPS reports a wrong one;
- add `battery.date` with the date on the battery label;
- set `ups.mfr`, `ups.model` and `ups.serial` to fixed values, so Home Assistant keeps the same device and its history when you replace the UPS.

How it works:

- Up to 16 variables, each with a **Key**, a **Value** and an optional **Description**. The value and the description accept printable ASCII, up to 64 characters.
- Only NUT clients see them. The dashboard, the automatic shutdown and the rest of the board keep using the real values of the UPS.
- NUT clients also see `driver.parameter.override.<variable>`, as with NUT.
- A custom value for `ups.mfr`, `ups.model` or `ups.serial` also applies to `device.mfr`, `device.model` and `device.serial`.
- A writable variable with a custom value becomes read-only for `upsrw` and in the UPS Settings page.
- `ups.status`, `ups.status.*`, `device.type` and `driver.parameter.*` cannot be set.
- With no UPS connected, NUT clients get `ERR DATA-STALE` as usual, custom variables included.

Changes are saved on the board and applied at once, with no restart. They are kept across restarts and OTA updates.

## Board Settings

Settings of the board itself, kept after a restart. The top line shows the board model.

### Status LED (ESP32-S3)

Turn the LED off, or choose its brightness from 1 to 5. Changes apply at once. When off, the LED stays dark in every state. See [Status LED](status-led.md).

### Automatic shutdown

**Shut down the UPS on low battery**, off by default. With the option on, when the UPS is on battery and reports low battery (`OB LB`), the board sends `shutdown.default` to the UPS once, even with no NUT client connected. It does not send it again until the UPS is back on line (`OL`).

- Use it for UPSes that power devices without a NUT client, such as a TV or an audio system.
- The threshold is the one of the UPS, `battery.charge.low` / `battery.runtime.low`, in the UPS Settings page.
- The command and its outcome appear in the System Log as `[CMD] Automatic shutdown: ...`.
- If the UPS has no shutdown command, or no UPS is connected, the switch is disabled and the page says why.
- Whether the UPS turns its outlets back on when power returns depends on the model.

This is a board setting, not a UPS variable: NUT clients cannot see or change it, and `upsmon` keeps working as before.

### Time

- **Get the time from an NTP server** (on by default), the **Server** (default `pool.ntp.org`) and the **Time zone**, from a list with automatic daylight saving time (default UTC).
- The section shows whether the time is synchronized and the board time.
- Changes apply at once, with no restart.

The time is only shown and written in the logs: logins, sessions and timeouts do not depend on it. Without Internet access the board works as before, and the log lines just carry no time.

## Web Login

Change the username and password of the web interface. See [Change the credentials](configuration.md#change-the-credentials). In access point mode the page shows **Reset credentials**, which needs no current password.

## System Logs

The System Log of the board, in a terminal:

- **Auto-refresh** keeps it updated; **Refresh** reads it once; **Clear** empties the view.
- **Export** downloads the log as a file.
- **Export USB Diagnostics** downloads `usb_diagnostics.json`, with the USB descriptor of the UPS, the reports it answered and the state of the USB link. Before the download, the board reads once every FEATURE report of the UPS, including the ones it does not use for NUT, so one file gives a complete picture. Attach it when you report a problem with a UPS.

Once the board has the network time, each line shows the board time. Otherwise the terminal uses the time of your browser.

The board keeps only the most recent lines in memory, and a restart clears them. To keep a longer history, use remote logging.

### Remote logging (syslog)

Sends a copy of the System Log to a syslog server over UDP. It is off by default.

- **Server** and **UDP port** (default 514).
- **Format:** RFC 3164 (default) or RFC 5424.
- **Facility** (default `local0`), **Minimum level** (default INFO) and **Max lines per second** (default 10).

The panel shows the state and how many lines were sent, failed, rate-limited or dropped. The **Syslog** indicator next to Auto-refresh shows the state too.

- Lines are sent by a background task: they never delay the UPS or the NUT server.
- Lines written before the network is up are sent once the connection is ready. Nothing is sent in access point mode.
- With the time synchronized, each line carries the time it was written. RFC 5424 lines always carry the hostname; RFC 3164 lines carry it once the time is synchronized.
- Settings are saved on the board and apply without a restart.

> [!WARNING]
> UDP syslog is not encrypted and has no delivery receipt. Anyone on the network can read the lines, including the usernames of failed logins. If the server stops while the network is up, the state can keep showing it as active.

## Firmware OTA

Opens the update page: drag the release zip of your board, or its `firmware.bin`, onto the upload area. See [Updating the Firmware](installation.md#updating-the-firmware-ota).
