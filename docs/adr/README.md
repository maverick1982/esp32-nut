---
type: reference
title: "Architecture Decision Records (ADRs)"
description: "Documento ADR: Architecture Decision Records (ADRs)"
tags: [adr, decision]
---
# Architecture Decision Records (ADRs)

This directory contains the Architectural Decision Records for this project.

| ID | Date | Title | Status | Summary |
|---|---|---|---|---|
| 0001 | 2026-09-05 | Record Architecture Decisions | Accepted | Establish the MADR format and Antigravity workflow for documenting architectural decisions. |
| 0002 | 2026-09-05 | Migrate to ESP-IDF usb_host_hid and Decouple Driver Tasks | Accepted | Refactor USB HID driver to use ESP-IDF usb_host_hid, a background task, and strict mutex locking. |
| 0003 | 2026-09-05 | Faithfully Mirror Official NUT Drivers and Subdrivers Behavior | Accepted | All drivers and subdrivers in esp32-nut must faithfully mirror upstream official NUT implementations (in nut_repo/). |
| 0004 | 2026-09-05 | Unify Sub-Driver Inheritance via GenericDriver | Accepted | All sub-drivers must inherit from GenericDriver to eliminate duplicate standard HID mappings and enforce DRY. |
| 0005 | 2026-09-11 | Dynamic Polling and Quirks for USB STALL Mitigation | Superseded | Dynamically calculate USB GET_REPORT sizes and apply targeted quirks to avoid WDT freezes. |
| 0006 | 2026-09-06 | Centralized Two-Tier HID Polling and Static Variable Lifecycle in GenericDriver | Accepted | Implement upstream NUT usbhid-ups 2s quick/30s full polling, static nominal lifecycle, and zero-filtering in GenericDriver. |
| 0007 | 2026-09-19 | Accurate HID Report Length Tracking and Memory Optimization | Accepted | Implement precise NUT-style exact byte length tracking for USB HID reports, truncate padded ESP-IDF responses, and eliminate String memory fragmentation. |
| 0008 | 2026-09-24 | Non-blocking HID Callbacks, Control Pipe Guarding and Crash Safety Net | Accepted | Defer hid_host callbacks to the loopTask, never touch an in-flight control URB, recover the link with backoff/restart ladder, Task WDT and guarded core dump (issue #47). |
