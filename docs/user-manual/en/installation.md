---
title: "Installation"
type: manual
tags: [user-manual]
---

# Firmware Installation

There are three ways to install ESP32-NUT: the web installer (recommended), the release zip with `esptool`, or a build from source with PlatformIO. Later updates can be installed over the network from the web interface (OTA).

## Method A: Web Installer (Recommended)

No software to install: the browser flashes the board.

> [!IMPORTANT]
> The web installer needs a Chromium-based browser (**Google Chrome, Microsoft Edge, Brave**). Firefox and Safari do not support the Web Serial API it uses.

1. Connect the board to your computer with a data cable: the `COM` (or `UART`) port on an ESP32-S3, the USB-C port on an ESP32-P4.
2. Open the [**ESP32-NUT Web Installer**](https://maverick1982.github.io/esp32-nut/).
3. Press the button for your board:
   - the main button for an **ESP32-S3**;
   - in the **ESP32-P4** section, the button for your chip revision, **v3.1 or later** or **before v3** (see [Chip revision](hardware.md#chip-revision-two-images)).
4. Choose the serial port of the board (often named "USB to UART Bridge", "USB Serial" or "USB JTAG/serial debug unit").
5. When the installer asks **"Do you want to erase the device before installing?"**:
   - leave **"Erase device"** unchecked to keep the Wi-Fi, NUT and web login settings of a board that already runs ESP32-NUT;
   - check it for a clean install. An ESP32-S3 then starts as the `NUT_ESP32_Config` setup access point.
6. Wait for the end of the installation: the board restarts by itself.

The installer always installs the latest release.

## Method B: Release Zip and esptool

Every [release](https://github.com/maverick1982/esp32-nut/releases) has one zip per board:

| Zip | Board |
| :--- | :--- |
| `esp32-nut-vX.Y.Z.zip` | ESP32-S3 |
| `esp32-nut-vX.Y.Z-esp32-p4-eth.zip` | ESP32-P4 v3.1 or later |
| `esp32-nut-vX.Y.Z-esp32-p4-eth-v1.zip` | ESP32-P4 before v3 |

Inside, `firmware.factory.bin` is the complete image to write at address `0x0`:

```bash
esptool write-flash 0x0 firmware.factory.bin
```

With esptool v4 the command is `esptool.py write_flash 0x0 firmware.factory.bin`. The `README.txt` file in the zip explains the other files.

## Method C: Build with PlatformIO (Advanced)

To change the code or test a development branch:

1. Install [Visual Studio Code](https://code.visualstudio.com/) with the **PlatformIO IDE** extension, and Git.
2. Clone the repository:
   ```bash
   git clone https://github.com/maverick1982/esp32-nut.git
   ```
3. Open the `esp32-nut` folder in Visual Studio Code. PlatformIO downloads the toolchain and the libraries.
4. Pick the environment of your board:
   - `esp32-s3-standard` (ESP32-S3);
   - `esp32-p4-eth` (ESP32-P4 v3.1 or later);
   - `esp32-p4-eth-v1` (ESP32-P4 before v3).
5. Connect the board and press **Upload**. The web interface is built into the firmware, so there is no separate file system image to upload.

## Updating the Firmware (OTA)

Once the board runs ESP32-NUT, you can update it from the browser:

1. Download the zip of your board from the [releases](https://github.com/maverick1982/esp32-nut/releases) page.
2. In the web interface, open **Firmware OTA** in the sidebar.
3. Drag the zip, or the `firmware.bin` inside it, onto the upload area, or click it to choose the file.
4. Wait for the upload: the board restarts with the new version.

The settings are kept.

> [!WARNING]
> **Upgrading from v1.5.x or earlier?** Install v1.6.0 or later with the web installer, not with OTA. v1.6 added a flash partition (`coredump`) that keeps crash details, and an OTA update cannot change the partition table. Leave **"Erase device"** unchecked to keep your settings. After this one-time install, OTA works again.

---

Next step: the [Initial Configuration](configuration.md).
