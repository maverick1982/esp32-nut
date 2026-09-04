# <img src="docs/images/logo.png" width="40" height="40" align="left" style="margin-right: 15px;"> ESP32 NUT Server Bridge

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

*🇮🇹 [Leggi in Italiano](README.it.md)*

An open-source firmware for ESP32-S3 that acts as a standalone **Network UPS Tools (NUT) Server bridge** over Wi-Fi. It connects to an Eaton UPS (like the Eaton 3S 700) via USB Host and exposes its data to your local network using the standard NUT protocol (port 3493) and a built-in Web Interface.

This allows you to easily integrate your USB-only UPS into Home Assistant, TrueNAS, Synology, or any other NUT-compatible client without needing a dedicated Raspberry Pi or PC running 24/7.

## 🌟 Features

- **Native USB Host Support**: Directly reads HID data from USB-compatible UPS devices (tested with Eaton, APC, CyberPower, Powercom).
- **NUT Server Protocol**: Implements the standard NUT protocol, making it instantly compatible with existing NUT clients.
- **Web UI & Captive Portal**: Easy configuration of Wi-Fi and NUT credentials via a modern, responsive web interface.
- **Over-The-Air (OTA) Updates**: Flash new firmware versions directly from the web browser without touching the board.
- **Diagnostic LED**: Visual feedback for Wi-Fi and UPS connection status.

## 🔌 Supported UPS Devices

The firmware uses the official Network UPS Tools (NUT) HID mappings and supports multiple vendors automatically:

<details>
<summary><b>Eaton</b> (Click to expand tested models)</summary>

- Eaton 3S series
- Eaton 5P 1U
</details>

<details>
<summary><b>APC</b> (Click to expand tested models)</summary>

- APC Back-UPS CS 500
- APC BackUPS Pro 900
- APC Back-UPS XS 700U
- APC Smart-UPS 750
</details>

<details>
<summary><b>CyberPower</b> (Click to expand tested models)</summary>

- CyberPower UT650EG
- CyberPower CP 1350C / CP1350AVRLCD
- CyberPower CP1500AVRLCDa
</details>

<details>
<summary><b>Powercom</b> (Click to expand tested models)</summary>

- Powercom SPD-750U
</details>

<br>

- **Generic / Other Brands**: If an UPS is not recognized, the system will fall back to a generic stub driver. Currently, this driver cannot read dynamic stats (like Battery Level). If your UPS falls into this category, you can use the **Export USB Diagnostics** button in the Web UI (System Logs tab) to download a diagnostic file (`usb_diagnostics.json`). Please attach this file when opening an issue on GitHub to request dedicated support or report bugs on existing drivers.

> [!NOTE]
> **USB HID Protocol Requirement:** This firmware currently supports only UPS models using the standard **USB HID Power Device Class** (`usbhid-ups`). UPS units communicating via USB-to-Serial protocols (such as Megatec/Q1, USB CDC/serial bridges, or proprietary serial protocols) are not currently supported.

## 📸 Screenshots

| UPS Telemetry | Wi-Fi Config |
| :---: | :---: |
| [![UPS Telemetry](docs/images/ui-ups.png)](docs/images/ui-ups.png) | [![Wi-Fi Config](docs/images/ui-wifi.png)](docs/images/ui-wifi.png) |

| NUT Server | System Logs |
| :---: | :---: |
| [![NUT Server](docs/images/ui-nut.png)](docs/images/ui-nut.png) | [![System Logs](docs/images/ui-logs.png)](docs/images/ui-logs.png) |

| Firmware OTA | |
| :---: | :---: |
| [![Firmware OTA](docs/images/ui-ota.png)](docs/images/ui-ota.png) | |

## 🛠 Hardware Requirements & Wiring

- **ESP32-S3 Board**: An ESP32-S3 development board is **required** because it features native USB OTG Host capabilities. Standard ESP32 or ESP32-C3 will not work.
- **USB OTG Cable**: An adapter to plug the UPS USB cable into the ESP32.

### Typical Dual USB-C Board Setup (e.g., Generic ESP32-S3 DevKit)
Many generic ESP32-S3 boards with two USB-C ports have hidden solder pads on the back to enable Host mode. To achieve a clean setup without manually soldering wires to GPIOs:
1. **Solder the "USB-OTG" pads**: Find the two pads labeled `USB-OTG` on the back of the board and bridge them with solder. This routes 5V power to the `USB` port, enabling it to act as a Host to power the UPS USB interface.
2. **Solder the "RGB" pads (Optional)**: Bridge the `RGB` pads if you want to enable the built-in status LED.
3. **Power**: Connect a USB wall charger to the port labeled `COM` (or `UART`).
4. **Data**: Connect a USB-C OTG adapter to the port labeled `USB`, and plug the UPS into it.

### 3D Printed Case (Optional)

Turn your bare ESP32-S3 board into a finished product! We have designed a sleek 3D-printable case for this project.

<img src="hardware/case/images/case-front-green-light.jpg" width="400" alt="3D Printed Case Front">

Download the ready-to-print files from your favorite platform:
- [**Printables**](https://www.printables.com/model/1794471-case-esp32-nut-server-bridge)
- [**Thingiverse**](https://www.thingiverse.com/thing:7389257)

Source `.3mf` models, original `.f3d` CAD files, and additional photos are available in the [`hardware/case`](hardware/case) directory.

## 🚀 Getting Started

### 1. Flashing the Firmware

**Option A: Zero-Click Install via Browser (Recommended)**
You can install the firmware directly from your browser without downloading any tools!
👉 <a href="https://maverick1982.github.io/esp32-nut/" target="_blank">**Install via Browser**</a>

**Option B: Manual Build via PlatformIO**
1. Clone this repository.
2. Open the project folder in VSCode with the PlatformIO extension.
3. Connect your ESP32-S3 to your PC (using the UART/UART-Prog USB port).
4. Click **Build** and then **Upload**.

### 2. First Boot & Configuration
1. On first boot (or if no valid Wi-Fi configuration is saved), the ESP32 will create an Access Point named **`NUT_ESP32_Config`** (Password: `12345678`).
   > **Note:** To manually force Access Point mode (e.g., to change Wi-Fi settings), plug in the ESP32 and unplug it within 3 seconds. On the next boot, the ESP32 will start in Access Point mode. (The AP no longer activates automatically upon signal loss).
2. Connect your phone or PC to this Wi-Fi network.
3. A captive portal should automatically open. If not, navigate to `http://192.168.4.1`.
4. Enter your home Wi-Fi credentials and set your preferred NUT Server username/password.
5. Save and reboot.

### 3. Usage
- Once connected to your home Wi-Fi, the ESP32 will grab an IP address from your DHCP server.
- **Web Dashboard**: Navigate to the ESP32's IP address in your browser to see real-time UPS stats (Battery level, Voltage, Load, etc.).
- **NUT Client**: Configure your Home Assistant or NAS to connect to the ESP32's IP on port `3493` using the credentials you defined.

## 🛠 Support the Project

This project is entirely open-source and free to use. 
If it helped you integrate your UPS into your smart home or server rack, please consider supporting its ongoing development! 

Donations will be explicitly used to purchase new UPS models for testing, ESP32 development boards, and other hardware tools necessary to improve the code and expand compatibility with a wider range of devices.

You can support the project via:

[![Ko-fi](https://img.shields.io/badge/Ko--fi-F16061?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/maverick1982)
[![PayPal](https://img.shields.io/badge/PayPal-00457C?style=for-the-badge&logo=paypal&logoColor=white)](https://paypal.me/pasqualecuorpo)

Thank you to everyone who supports this project! 🚀

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
