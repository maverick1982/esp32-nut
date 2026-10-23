#!/usr/bin/env python3
"""Minimal NUT client that writes what upsc, upscmd -l and upsrw would print.

For comparing a firmware with a saved baseline (docs/plans/idf55-baseline/*-v1.8.0.txt) on a
PC without the NUT client tools. Read only, no login:

    python scripts/nut_snapshot.py <board-ip> <ups-name> out/v2

writes out/v2-upsc.txt, out/v2-upscmd.txt and out/v2-upsrw.txt. Compare with
diff --strip-trailing-cr (the baseline files have CRLF line endings).
"""
import shlex
import socket
import sys

host, ups, prefix = sys.argv[1], sys.argv[2], sys.argv[3]
sock = socket.create_connection((host, 3493), timeout=10)
rf = sock.makefile("r", encoding="utf-8", newline="\n")


def cmd(line):
    sock.sendall((line + "\n").encode())
    return rf.readline().rstrip("\n")


def list_cmd(line):
    first = cmd(line)
    if not first.startswith("BEGIN LIST"):
        raise RuntimeError(f"{line}: {first}")
    rows = []
    while True:
        r = rf.readline().rstrip("\n")
        if r.startswith("END LIST"):
            return rows
        rows.append(shlex.split(r))


def get(line):
    reply = cmd(line)
    parts = shlex.split(reply)
    if parts and parts[0] == "ERR":
        return None
    return parts[-1]


with open(prefix + "-upsc.txt", "w", encoding="utf-8") as f:
    for r in list_cmd(f"LIST VAR {ups}"):
        f.write(f"{r[2]}: {r[3]}\n")

with open(prefix + "-upscmd.txt", "w", encoding="utf-8") as f:
    f.write(f"Instant commands supported on UPS [{ups}]:\n\n")
    for r in list_cmd(f"LIST CMD {ups}"):
        f.write(f"{r[2]} - {get(f'GET CMDDESC {ups} {r[2]}')}\n")

with open(prefix + "-upsrw.txt", "w", encoding="utf-8") as f:
    for r in list_cmd(f"LIST RW {ups}"):
        name, value = r[2], r[3]
        f.write(f"[{name}]\n{get(f'GET DESC {ups} {name}')}\n")
        types = shlex.split(cmd(f"GET TYPE {ups} {name}"))[3:]
        maxlen = next((t.split(":")[1] for t in types if t.startswith("STRING:")), None)
        if "ENUM" in types:
            f.write("Type: ENUM STRING\n")
            if maxlen:
                f.write(f"Maximum length: {maxlen}\n")
            for e in list_cmd(f"LIST ENUM {ups} {name}"):
                sel = " SELECTED" if e[3] == value else ""
                f.write(f'Option: "{e[3]}"{sel}\n')
        else:
            f.write("Type: STRING\n")
            if maxlen:
                f.write(f"Maximum length: {maxlen}\n")
            f.write(f"Value: {value}\n")
        f.write("\n")

cmd("LOGOUT")
sock.close()
print("written", prefix + "-upsc.txt", prefix + "-upscmd.txt", prefix + "-upsrw.txt")
