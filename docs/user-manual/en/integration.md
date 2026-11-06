---
title: "NUT Client Integration"
type: manual
tags: [user-manual]
---

# NUT Client Integration

Once the board is on your network and reads the UPS, it works like a NUT server (`upsd`) with the `usbhid-ups` driver. Any NUT client can use it.

## Connection Parameters

- **Host:** the IP address of the board, or its hostname if your network resolves it. The **Network** page shows the address.
- **Port:** `3493`, the standard NUT port.
- **UPS name:** the **UPS Name** of the NUT Server page (default `ups`), not the model of the UPS.
- **Username / Password:** the NUT credentials of the NUT Server page, if you set them. They are needed for commands and settings; reading the data works without them.

Up to **8 NUT clients** can be connected at the same time. Beyond that, a new connection is refused and the System Log shows which addresses hold the connections. A client that disappears without closing its connection frees its slot after about 90 seconds.

## Home Assistant

1. Go to **Settings → Devices & Services**.
2. Press **Add Integration** and search for **Network UPS Tools (NUT)**.
3. Enter the address of the board and port `3493`.
4. Enter the NUT username and password, if you set them.
5. Confirm: Home Assistant finds the UPS and creates its sensors (battery, load, voltages, status…).

The commands of the UPS (battery test, beeper, …) are available as Home Assistant actions when the NUT credentials are set.

> [!TIP]
> Home Assistant identifies the UPS by its manufacturer, model and serial number. If you replace the UPS and want to keep the same device and its history, set `ups.mfr`, `ups.model` and `ups.serial` as [custom variables](web-interface.md#custom-variables).

## Linux: upsc, upscmd, upsrw

Install the NUT client tools (for example `sudo apt install nut-client`), then use `<ups name>@<address>`:

```bash
# All variables
upsc ups@192.168.1.50

# Commands supported by the UPS, then run one
upscmd -l ups@192.168.1.50
upscmd -u admin ups@192.168.1.50 test.battery.start.quick

# Writable variables, then change one
upsrw ups@192.168.1.50
upsrw -s battery.charge.low=20 -u admin ups@192.168.1.50
```

## Linux: upsmon (Automatic Shutdown of a Computer)

To shut a computer down when the battery runs low, add the board to `upsmon.conf` on that computer:

```
MONITOR ups@192.168.1.50 1 admin yourpassword secondary
```

The NUT username and password are those of the NUT Server page.

If no NUT client should handle the shutdown, the board can shut the UPS down by itself on low battery: see [Automatic shutdown](web-interface.md#automatic-shutdown).

## When the UPS Is Not Answering

When the board has no fresh data from the UPS (UPS disconnected, USB link being recovered), NUT clients get `ERR DATA-STALE`, as with NUT. Home Assistant shows the sensors as *Unavailable* until the data come back.
