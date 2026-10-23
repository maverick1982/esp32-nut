#!/usr/bin/env python3
"""Temporary memory instrumentation for src/core/main.cpp (US-095). Never commit the result.

    python scripts/mem_probe_patch.py src/core/main.cpp

Adds "[MEM] <stage>: total, free, largest" lines during setup() (setup start, after Wi-Fi
begin, before USB begin, after NUT server begin) and, every 5 minutes, the minimum free stack
of every FreeRTOS task ("[MEM] task <name>: min free stack N"). With FreeRTOS trace facility
off (e.g. the v1.8.0 libraries) it lists the known system tasks by name. Works on the v1.8.0
tag and on the multi-board branch (same anchors). Undo with: git restore src/core/main.cpp
"""
import sys

path = sys.argv[1]
s = open(path, encoding="utf-8", newline="").read()
nl = "\r\n" if "\r\n" in s else "\n"
if "[US-095 TASK-01" in s:
    sys.exit("already patched")

helpers = """
// [US-095 TASK-01, temporaneo: non committare]
static void diagMem(const char* stage) {
    AppLogger::log("INFO", "[MEM] %s: total %u, free %u, largest %u", stage,
                   (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
                   (unsigned)esp_get_free_heap_size(),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
static void diagStacks() {
#if configUSE_TRACE_FACILITY
    UBaseType_t n = uxTaskGetNumberOfTasks() + 4;
    TaskStatus_t* st = (TaskStatus_t*)malloc(sizeof(TaskStatus_t) * n);
    if (!st) return;
    n = uxTaskGetSystemState(st, n, NULL);
    for (UBaseType_t i = 0; i < n; ++i) {
        AppLogger::log("INFO", "[MEM] task %s: min free stack %u", st[i].pcTaskName,
                       (unsigned)st[i].usStackHighWaterMark);
    }
    free(st);
#else
    // Senza TRACE_FACILITY (librerie della v1.8.0): task di sistema noti, per nome
    static const char* const names[] = {"esp_timer", "ipc0", "ipc1", "Tmr Svc", "sys_evt", "tiT",
        "wifi", "usb_host_events", "USB HID Host", "ups_poll", "loopTask", "IDLE0", "IDLE1"};
    AppLogger::log("INFO", "[MEM] tasks: %u", (unsigned)uxTaskGetNumberOfTasks());
    for (const char* name : names) {
        TaskHandle_t h = xTaskGetHandle(name);
        if (h) AppLogger::log("INFO", "[MEM] task %s: min free stack %u", name,
                              (unsigned)uxTaskGetStackHighWaterMark(h));
    }
#endif
}

void setup() {"""

edits = [
    ("\nvoid setup() {", helpers),
    ("CrashDiag::logBootInfo();", 'CrashDiag::logBootInfo();\n    diagMem("setup start");'),
    ("network_mgr.begin(wifi.ssid, wifi.password);",
     'network_mgr.begin(wifi.ssid, wifi.password);\n        diagMem("after WiFi begin");'),
    ("if (!usb_ups.begin()) {", 'diagMem("before USB begin");\n    if (!usb_ups.begin()) {'),
    ('AppLogger::log("INFO", "[MAIN] NUTServer started correctly on port 3493.");',
     'AppLogger::log("INFO", "[MAIN] NUTServer started correctly on port 3493.");\n'
     '            diagMem("after NUT server begin");'),
]
for old, new in edits:
    old_n, new_n = old.replace("\n", nl), new.replace("\n", nl)
    if s.count(old_n) != 1:
        sys.exit(f"anchor not found exactly once: {old!r}")
    s = s.replace(old_n, new_n)

# Stack list every 5 minutes, after the existing 60 s [DIAG] Heap line
anchor = '"[DIAG] Heap: free'
i = s.find(anchor)
if i < 0:
    sys.exit("anchor [DIAG] Heap not found")
end = s.find(";", i) + 1
block = (nl + "        static uint32_t last_mem_stacks = 0;" + nl +
         "        if (now - last_mem_stacks >= 300000) { last_mem_stacks = now; diagStacks(); }")
s = s[:end] + block + s[end:]

open(path, "w", encoding="utf-8", newline="").write(s)
print("patched", path)
