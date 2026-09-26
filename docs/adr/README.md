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
| 0002 | 2026-09-05 | Migrate to ESP-IDF usb_host_hid and Decouple Driver Tasks | Accepted (amended by 0008) | Refactor USB HID driver to use ESP-IDF usb_host_hid and background tasks; data-only mutex, no lock across control transfers (see 0008). |
| 0003 | 2026-09-05 | Faithfully Mirror Official NUT Drivers and Subdrivers Behavior | Accepted | All drivers and subdrivers in esp32-nut must faithfully mirror upstream official NUT implementations (in nut_repo/). |
| 0004 | 2026-09-05 | Unify Sub-Driver Inheritance via GenericDriver | Accepted | All sub-drivers must inherit from GenericDriver to eliminate duplicate standard HID mappings and enforce DRY. |
| 0005 | 2026-09-11 | Dynamic Polling and Quirks for USB STALL Mitigation | Partially superseded by 0007 | Dynamically calculate USB GET_REPORT sizes and apply targeted quirks (`QUIRK_NO_GET_REPORT` still in force) to avoid WDT freezes. |
| 0006 | 2026-09-06 | Centralized Two-Tier HID Polling and Static Variable Lifecycle in GenericDriver | Accepted, partially implemented (amended by 0008) | usbhid-ups style 2 s quick poll of status reports / 30 s full poll in GenericDriver with per-driver hooks; static lifecycle only for strings, no zero filter. |
| 0007 | 2026-09-19 | Accurate HID Report Length Tracking and Memory Optimization | Accepted | Implement precise NUT-style exact byte length tracking for USB HID reports, truncate padded ESP-IDF responses, and eliminate String memory fragmentation. |
| 0008 | 2026-09-24 | Non-blocking HID Callbacks, Control Pipe Guarding and Crash Safety Net | Accepted | hid_host callbacks only post to a queue; USB service in the ups_poll task; guarded control pipe; LinkMonitor/InputWatchdog/RestartPolicy recovery; unified poll state machine; Task WDT and guarded core dump (issues #47, #36, #48). |
