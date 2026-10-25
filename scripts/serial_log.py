#!/usr/bin/env python3
"""Serial logger for an esp32-nut board: every line with the PC time, optional reset at start.

Used for the memory measurements of US-095 (docs/plans/custom-sdkconfig.md): flash with
esptool, then capture the boot and the "[DIAG] Heap" lines from the UART port of the board
(on the ESP32-S3-DevKitC the port with the USB-serial bridge; the UPS stays on the native USB).

    python scripts/serial_log.py COM5 serial.log 660 --reset

--reset pulses EN through RTS (IO0 kept high through DTR) so the boot lines are captured.
Needs pyserial (it is in the PlatformIO penv: ~/.platformio/penv/Scripts/python.exe).
"""
import sys
import time

import serial

port, out, seconds = sys.argv[1], sys.argv[2], float(sys.argv[3])
reset = "--reset" in sys.argv

s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 0.5
s.dtr = False  # IO0 high: normal boot
s.rts = False  # EN high
s.open()
if reset:
    s.rts = True
    time.sleep(0.2)
    s.rts = False

end = time.time() + seconds
buf = b""
with open(out, "a", encoding="utf-8") as f:
    f.write(f"--- serial log start {time.strftime('%Y-%m-%d %H:%M:%S')} reset={reset}\n")
    while time.time() < end:
        chunk = s.read(4096)
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace").rstrip("\r")
            f.write(f"{time.strftime('%H:%M:%S')} {text}\n")
            f.flush()
    f.write(f"--- serial log end {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
s.close()
