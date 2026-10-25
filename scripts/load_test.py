#!/usr/bin/env python3
"""Load test for an esp32-nut board: NUT clients + web UI traffic, with heap monitoring.

Worst case of normal use: every NUT client slot taken by a polling client (upsmon,
Home Assistant, ...), the web UI open (telemetry, status, log) and a USB diagnostics
export now and then. The heap comes from /api/system-status (heap_free, heap_min_free,
heap_largest_block, the same values as the "[DIAG] Heap" log line).

Standard library only. Example (PowerShell or cmd):

    python scripts/load_test.py --host 192.168.1.50 --ups eaton-3s-700 --web-user admin

The web password is asked at the prompt when --web-pass is not given. NUT credentials
are optional: with them each client logs in like upsmon (USERNAME/PASSWORD/LOGIN).
"""
import argparse
import getpass
import http.cookiejar
import json
import socket
import sys
import threading
import time
import urllib.error
import urllib.request

NUT_PORT = 3493


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.nut_ok = {}
        self.nut_err = {}
        self.nut_reconnects = {}
        self.web_ok = 0
        self.web_err = 0
        self.dumps = 0
        self.heap_samples = []  # (elapsed s, free, min_free, largest)

    def add(self, field, key=None, n=1):
        with self.lock:
            if key is None:
                setattr(self, field, getattr(self, field) + n)
            else:
                d = getattr(self, field)
                d[key] = d.get(key, 0) + n


def nut_connect(host, timeout):
    s = socket.create_connection((host, NUT_PORT), timeout=timeout)
    return s, s.makefile("r", encoding="utf-8", newline="\n")


def nut_send(sock, line):
    sock.sendall((line + "\n").encode("utf-8"))


def nut_read_line(rf):
    line = rf.readline()
    if not line:
        raise ConnectionError("connection closed by the server")
    return line.rstrip("\n")


def nut_list_var(sock, rf, ups):
    nut_send(sock, f"LIST VAR {ups}")
    first = nut_read_line(rf)
    if first.startswith("ERR"):
        raise RuntimeError(first)
    count = 0
    while True:
        line = nut_read_line(rf)
        if line.startswith("END LIST VAR"):
            return count
        if line.startswith("ERR"):
            raise RuntimeError(line)
        count += 1


def nut_client(idx, args, stats, stop):
    """One persistent client, like upsmon or the Home Assistant integration."""
    name = f"nut{idx}"
    while not stop.is_set():
        try:
            sock, rf = nut_connect(args.host, args.timeout)
            if args.nut_user:
                for cmd in (f"USERNAME {args.nut_user}", f"PASSWORD {args.nut_pass}", f"LOGIN {args.ups}"):
                    nut_send(sock, cmd)
                    reply = nut_read_line(rf)
                    if not reply.startswith("OK"):
                        raise RuntimeError(f"{cmd.split()[0]}: {reply}")
            while not stop.is_set():
                nut_list_var(sock, rf, args.ups)
                nut_send(sock, f"GET VAR {args.ups} ups.status")
                reply = nut_read_line(rf)
                if not reply.startswith("VAR"):
                    raise RuntimeError(reply)
                stats.add("nut_ok", name)
                stop.wait(args.interval)
            try:
                nut_send(sock, "LOGOUT")
            except OSError:
                pass
            sock.close()
        except Exception as e:  # noqa: BLE001 - every failure is counted and retried
            stats.add("nut_err", name)
            stats.add("nut_reconnects", name)
            if args.verbose:
                print(f"[{name}] {type(e).__name__}: {e}", flush=True)
            stop.wait(2)


def extra_client_check(args):
    """A client beyond the slots must be rejected (US-084), not crash the board."""
    try:
        sock, rf = nut_connect(args.host, args.timeout)
        nut_send(sock, f"GET VAR {args.ups} ups.status")
        reply = rf.readline().strip()
        sock.close()
        return reply or "(connection closed without a reply)"
    except Exception as e:  # noqa: BLE001
        return f"{type(e).__name__}: {e}"


class Web:
    def __init__(self, args):
        self.base = f"http://{args.host}"
        self.timeout = args.timeout
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))

    def get(self, path):
        with self.opener.open(self.base + path, timeout=self.timeout) as r:
            return r.read()

    def login(self, user, password):
        body = json.dumps({"username": user, "password": password, "remember": False}).encode()
        req = urllib.request.Request(self.base + "/api/auth/login", data=body, method="POST",
                                     headers={"Content-Type": "application/json"})
        with self.opener.open(req, timeout=self.timeout) as r:
            return r.status


def web_worker(args, stats, stop, web, t0):
    """The web UI left open: telemetry and status every interval, log, periodic USB dump."""
    last_dump = 0.0
    while not stop.is_set():
        for path in ("/api/ups-vars", "/api/system-status", "/api/logs"):
            try:
                data = web.get(path)
                stats.add("web_ok")
                if path == "/api/system-status":
                    d = json.loads(data).get("diagnostics", {})
                    if "heap_free" in d:
                        with stats.lock:
                            stats.heap_samples.append((time.time() - t0, d["heap_free"],
                                                       d["heap_min_free"], d["heap_largest_block"]))
            except Exception as e:  # noqa: BLE001
                stats.add("web_err")
                if args.verbose:
                    print(f"[web] {path}: {type(e).__name__}: {e}", flush=True)
        if time.time() - last_dump >= args.dump_every:
            try:
                web.get("/api/usb/dump")
                stats.add("dumps")
                last_dump = time.time()
            except Exception as e:  # noqa: BLE001
                stats.add("web_err")
                if args.verbose:
                    print(f"[web] /api/usb/dump: {type(e).__name__}: {e}", flush=True)
        stop.wait(args.interval)


def main():
    p = argparse.ArgumentParser(description="esp32-nut load test: NUT clients + web UI, heap monitoring")
    p.add_argument("--host", required=True, help="board IP address or host name")
    p.add_argument("--ups", required=True, help="UPS name (NUT tab of the web UI)")
    p.add_argument("--clients", type=int, default=8, help="NUT clients kept connected (default 8, all slots)")
    p.add_argument("--duration", type=int, default=600, help="test length in seconds (default 600)")
    p.add_argument("--interval", type=float, default=2.0, help="polling period of every client in s (default 2)")
    p.add_argument("--dump-every", type=int, default=60, help="USB diagnostics export period in s (default 60)")
    p.add_argument("--nut-user", help="NUT username (optional: clients then LOGIN like upsmon)")
    p.add_argument("--nut-pass", help="NUT password (asked if --nut-user is given without it)")
    p.add_argument("--web-user", help="web UI username (without it the web load is skipped)")
    p.add_argument("--web-pass", help="web UI password (asked at the prompt if missing)")
    p.add_argument("--threshold", type=int, default=100_000, help="minimum heap_min_free in bytes to pass")
    p.add_argument("--timeout", type=float, default=10.0, help="socket/HTTP timeout in s")
    p.add_argument("--no-extra-client", action="store_true", help="skip the check of a client beyond the slots")
    p.add_argument("--verbose", action="store_true", help="print every error")
    args = p.parse_args()

    if args.nut_user and args.nut_pass is None:
        args.nut_pass = getpass.getpass("NUT password: ")
    web = None
    if args.web_user:
        if args.web_pass is None:
            args.web_pass = getpass.getpass("Web UI password: ")
        web = Web(args)
        try:
            web.login(args.web_user, args.web_pass)
        except urllib.error.HTTPError as e:
            sys.exit(f"Web login failed: HTTP {e.code} (check username/password; 429 = too many attempts)")
        except Exception as e:  # noqa: BLE001
            sys.exit(f"Web UI not reachable: {e}")

    stats = Stats()
    stop = threading.Event()
    t0 = time.time()
    threads = [threading.Thread(target=nut_client, args=(i + 1, args, stats, stop), daemon=True)
               for i in range(args.clients)]
    if web:
        threads.append(threading.Thread(target=web_worker, args=(args, stats, stop, web, t0), daemon=True))
    for t in threads:
        t.start()
        time.sleep(0.2)  # stagger the connections, as real clients do

    extra = None
    if not args.no_extra_client:
        time.sleep(3)
        extra = extra_client_check(args)

    print(f"Running for {args.duration} s: {args.clients} NUT clients"
          f"{', web UI' if web else ''}. Ctrl+C stops early.", flush=True)
    try:
        while time.time() - t0 < args.duration:
            time.sleep(10)
            with stats.lock:
                h = stats.heap_samples[-1] if stats.heap_samples else None
                ok, err = sum(stats.nut_ok.values()), sum(stats.nut_err.values())
            heap = (f"heap free {h[1]}, min free {h[2]}, largest {h[3]}" if h else "heap n/a (no web login)")
            print(f"[{int(time.time() - t0):4d} s] NUT ok {ok}, errors {err} | web ok {stats.web_ok}, "
                  f"errors {stats.web_err}, dumps {stats.dumps} | {heap}", flush=True)
    except KeyboardInterrupt:
        print("Stopped by the user.")
    stop.set()
    for t in threads:
        t.join(timeout=args.timeout + 2)

    print("\n=== Summary ===")
    for i in range(args.clients):
        name = f"nut{i + 1}"
        print(f"{name}: {stats.nut_ok.get(name, 0)} polls ok, {stats.nut_err.get(name, 0)} errors")
    if extra is not None:
        print(f"Client beyond the {args.clients} slots: {extra}")
    print(f"Web: {stats.web_ok} requests ok, {stats.web_err} errors, {stats.dumps} USB dumps")
    if stats.heap_samples:
        lowest_free = min(s[1] for s in stats.heap_samples)
        min_free = min(s[2] for s in stats.heap_samples)
        lowest_block = min(s[3] for s in stats.heap_samples)
        print(f"Heap: lowest free {lowest_free}, min free {min_free}, smallest largest block {lowest_block}")
        verdict = "PASS" if min_free >= args.threshold else "FAIL"
        print(f"{verdict}: heap_min_free {min_free} vs threshold {args.threshold}")
        return 0 if verdict == "PASS" and stats.web_err == 0 and not any(stats.nut_err.values()) else 1
    print("Heap not measured (use --web-user to read /api/system-status).")
    return 1 if any(stats.nut_err.values()) else 0


if __name__ == "__main__":
    sys.exit(main())
