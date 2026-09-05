# Architecture Decision Records (ADRs)

This directory contains the Architectural Decision Records for this project.

| ID | Date | Title | Status | Summary |
|---|---|---|---|---|
| 0001 | 2026-09-05 | Record Architecture Decisions | Accepted | Establish the MADR format and Antigravity workflow for documenting architectural decisions. |
| 0002 | 2026-09-05 | Migrate to ESP-IDF usb_host_hid and Decouple Driver Tasks | Accepted | Refactor USB HID driver to use ESP-IDF usb_host_hid, a background task, and strict mutex locking. |
| 0003 | 2026-09-05 | Faithfully Mirror Official NUT Drivers and Subdrivers Behavior | Accepted | All drivers and subdrivers in esp32-nut must faithfully mirror upstream official NUT implementations (in nut_repo/). |
| 0004 | 2026-09-05 | Unify Sub-Driver Inheritance via GenericDriver | Accepted | All sub-drivers must inherit from GenericDriver to eliminate duplicate standard HID mappings and enforce DRY. |
