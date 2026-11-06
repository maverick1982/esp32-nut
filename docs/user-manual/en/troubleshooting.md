---
title: "Troubleshooting"
type: manual
tags: [user-manual]
---

# Troubleshooting

This chapter helps you find the cause of the most common problems.

## Start From the Logs

1. Open the web interface and go to **System Logs**. The terminal shows what the firmware is doing: network connection, USB enumeration, errors from the UPS, NUT clients.
2. Use **Export** to save the log soon after a problem. The board keeps only the most recent lines, and a restart clears them.
3. To keep a longer history, or the lines written just before a restart, enable [remote logging](web-interface.md#remote-logging-syslog) to a syslog server.

For low-level USB problems a serial log helps too: connect the board to a computer and open a serial monitor at 115200 baud.

## The Yellow Box: Automatic Restarts

The board watches the USB link and recovers it by itself when the UPS stops answering. The steps go from the mildest to the strongest:

1. **Recovery of the USB interface**, without disconnecting the UPS.
2. **Power cycle of the USB port:** when a USB request stays stuck, the board switches its USB port off and on. The UPS comes back in a few seconds while the network, the web interface and the NUT server stay up. The System Log shows `[USB] Control transfer stuck: power-cycling the USB port (cycle N since boot)`, and the `[USB] Last 60 s` line counts the cycles.
3. **Automatic restart of the board:** if the recovery does not help, the board restarts. The first restart happens at once, the next ones after 1, 5 and 15 minutes.
4. **Degraded mode:** after 4 restarts in a row without getting the UPS back, the board stops restarting. NUT clients get `ERR DATA-STALE`. Only a new connection of the UPS or a manual restart gets the board out of it.

The yellow box at the top of the web interface shows the last automatic restart and its cause. The most common causes:

| Cause | Meaning |
| :--- | :--- |
| `control transfer stuck again after the USB port power cycle` | The power cycle did not unlock the UPS, so the board restarted. |
| `too many USB port power cycles in an hour` | More than 12 power cycles in an hour: the board restarted. |
| `UPS not back after the USB port power cycle` | The UPS did not come back within 30 s of the power cycle. |
| `INPUT reports stopped after recovery` | The UPS stopped sending its status messages, and the board does not read the same values with its own requests. |
| `UPS not enumerated after the automatic restart` | After a restart the UPS did not show up on the USB port, even after two power cycles. |

If the UPS still does not come back after the restarts, the yellow box asks you to **unplug and replug the UPS USB cable**.

A line `Last crash` or `Task watchdog: tasks not responding: ...` means the firmware crashed or froze. Export the System Log right away and open an issue with it and the text of the yellow box.

## The UPS Is Not Detected

If the web interface shows no UPS and the LED blinks fast red, check:

1. **ESP32-S3: USB-OTG pads.** On generic boards with two USB-C ports, the two `USB-OTG` pads on the back must be bridged with solder. Without them the `USB` port gives no power and the UPS interface stays off.
2. **ESP32-S3: OTG adapter.** Use a real **USB OTG** adapter. Charge-only adapters lack the internal resistor that tells the board to act as a host.
3. **ESP32-S3: ports.** Power goes to the `COM`/`UART` port, the UPS to the `USB` port.
4. **ESP32-P4: port.** The UPS goes on the USB 2.0 High-Speed port (header J3 on the Waveshare ESP32-P4-ETH).
5. **Hubs and boosted cables.** A long cable with a built-in booster contains a hub, which on the ESP32-P4 hides the UPS. See [USB Hubs and Active Extension Cables](hardware.md#usb-hubs-and-active-extension-cables).
6. **The USB cable.** Try another cable: some carry power only.

## The UPS Stops Answering From Time to Time

Some UPS models now and then stop answering a USB request:

- **APC Back-UPS BX series:** the APC firmware stops answering at irregular intervals, often several times an hour. NUT on Linux is affected too.
- **APC Smart-UPS:** a request sometimes stays stuck in the USB stack.

The board recovers by itself, usually with a power cycle of the USB port (see above). NUT clients may see the data as stale for a few seconds.

## Home Assistant Shows the UPS as Unavailable for a While

Short gaps happen when the board is recovering the USB link or restarting. Check the yellow box for the last automatic restart, and the System Log around that time. If the gaps come back often, open an issue with the System Log export (or the syslog lines) and the USB diagnostics export.

## UPS Recognized, but Values Are Missing

UPS makers fill the USB HID data in slightly different ways. If your UPS uses the generic driver or some values are missing, support can be added:

1. In **System Logs**, press **Export USB Diagnostics**. The board reads every report of the UPS once, then downloads `usb_diagnostics.json`.
2. Open an issue on GitHub with the [UPS Compatibility Report](https://github.com/maverick1982/esp32-nut/issues/new?template=ups_report.yml) form. Give the exact make and model and attach the file.

The same form is welcome when your UPS works well but is not in the list of tested models.

If the UPS reports a wrong value (for example a wrong model name), you can replace it for NUT clients with a [custom variable](web-interface.md#custom-variables).

## NUT Clients Cannot Connect

- **`ERR UNKNOWN-UPS`:** the UPS name in the client does not match the **UPS Name** of the NUT Server page (default `ups`).
- **`ERR ACCESS-DENIED`:** wrong NUT username or password.
- **`ERR DATA-STALE`:** the board has no fresh data from the UPS. See the yellow box and the System Log.
- **Connection refused:** 8 clients are already connected. The System Log lists their addresses. A client that disappeared frees its slot after about 90 seconds.
- **No answer at all:** check the address of the board, and that it is not in access point mode.

## The Web Interface Is Not Reachable

- Look for the board in your router's DHCP table as `esp32-nut-xxxxxx`, or with the hostname you set.
- **ESP32-S3:** a long yellow blink means the board is not on Wi-Fi. Use the double reset to start the setup access point and check the Wi-Fi settings (see [Initial Configuration](configuration.md)).
- **ESP32-P4:** check the Ethernet cable and the link LEDs of the port.
- **Forgotten password:** see [Forgotten password](configuration.md#forgotten-password).

## The ESP32-P4 Does Not Start After the Installation

The image for the wrong chip revision is refused by the bootloader. Install the other ESP32-P4 image (see [Chip revision](hardware.md#chip-revision-two-images)).

## My UPS Uses a Serial Connection

ESP32-NUT supports only UPSes that use the standard USB HID Power Device Class (`usbhid-ups` in NUT). UPSes that talk over a USB-to-serial converter (CH340, FT232, …), Megatec/Q1 or other serial protocols are not supported.
