---
title: "Installation"
type: manual
tags: [user-manual]
---

# Firmware Installation

Installing the ESP32-NUT firmware on your ESP32-S3 board can be done in two ways: a quick installation via a web browser (recommended for most users) or a manual installation via PlatformIO for advanced users.

## Method A: Quick Web Installer (Recommended)

This is the easiest and fastest method. It does not require downloading any software or source code to your computer.

> [!IMPORTANT]
> The Web Installer requires a Chromium-based browser, such as **Google Chrome, Microsoft Edge, or Brave**. Firefox and Safari currently do not support the required Web Serial APIs.

1. Connect the `COM` (or `UART`) port of your ESP32-S3 board to a USB port on your computer using a data cable.
2. Go to the official installation page: <a href="https://maverick1982.github.io/esp32-nut/" target="_blank">**ESP32-NUT Web Installer**</a>.
3. Click the **"Connect"** (or "Install") button.
4. The browser will prompt you to select a serial port. Choose the one corresponding to your ESP32 (it might be named "USB to UART Bridge" or "USB Serial").
5. Follow the on-screen instructions to start flashing. The process will automatically erase previous data (Erase Flash) and install the latest stable firmware release.
6. Once completed, the board will restart automatically.

## Method B: Manual Build via PlatformIO (Advanced)

If you wish to modify the code, test development versions (specific branches), or simply prefer compiling the firmware from scratch, you can use PlatformIO.

### Prerequisites
- [Visual Studio Code](https://code.visualstudio.com/) installed.
- **PlatformIO IDE** extension installed in VSCode.
- Git (optional, but recommended for cloning the repository).

### Procedure
1. Clone the official repository to your computer:
   ```bash
   git clone https://github.com/maverick1982/esp32-nut.git
   ```
2. Open the newly downloaded `esp32-nut` folder with Visual Studio Code. PlatformIO will automatically detect the project and download the necessary dependencies.
3. Connect the ESP32-S3 board to the computer via the `COM` (UART) port.
4. On the PlatformIO bottom bar (or from the alien ant side panel), click the **"Build"** checkmark button (to compile the code and verify there are no errors).
5. Next, click the right arrow **"Upload"** to flash the compiled firmware to the board.
6. (Optional) Click the electrical plug icon **"Upload File System image"** if you made changes to the Web UI files in the `data/` folder (although the main firmware generally includes compressed resources already).

---

Regardless of the method chosen, once the firmware is installed, the next step is to proceed to the [Initial Configuration](configuration.md) to connect the device to Wi-Fi and the UPS.
