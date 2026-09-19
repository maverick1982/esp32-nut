---
type: decision
title: "Accurate HID Report Length Tracking and Memory Optimization"
description: "Documento ADR: Accurate HID Report Length Tracking and Memory Optimization"
tags: [adr, decision, hid, usb, memory]
---
# Accurate HID Report Length Tracking and Memory Optimization

* **ADR ID:** 0007
* **Status:** Accepted
* **Date:** 2026-09-19
* **Authors:** @maverick1982, Antigravity

## Context and Problem Statement
In earlier iterations (ADR 0005), we attempted to resolve USB STALLs and WDT freezes by dynamically calculating GET_REPORT sizes and introducing custom Quirks (like `QUIRK_MAX_REPORT_SIZE_1`). However, this approach failed to properly calculate the exact expected lengths for UPS devices with extensive padding in their HID descriptors. It led to regressions where devices like CyberPower failed to respond (`0x108 Invalid Response` or timeouts) when the requested length exceeded their internal strict payload limits, or when devices like APC returned padded buffers that mismatched the expected sizes.
Additionally, the dynamic tracking logic relied on C++ `String` objects for HID mapping paths, causing severe heap fragmentation across long-running polling loops.

How can we robustly calculate the exact expected byte length for any HID report across all vendors, gracefully handle vendor-specific padded responses, and eliminate memory fragmentation?

## Considered Options
* **Option 1: Continue Hardcoding Lengths and Quirks per Vendor**
  Maintain lists of expected lengths or specific quirks for every single UPS model.
* **Option 2: Faithful "NUT Way" Length Tracking with Truncation Tolerance**
  Emulate the upstream C++ NUT source code by precisely accumulating the bit lengths (including padding bits) during the initial HID descriptor parsing. Store these exact expected sizes mapped by Report ID and Report Type. Furthermore, tolerate padding discrepancies by configuring the underlying ESP-IDF `hid_host.c` to gracefully truncate responses that exceed the requested length, rather than failing the transfer entirely. Replace all `String` usage with fixed `char path[80]` arrays to solve fragmentation.

## Decision Outcome
Chosen option: "**Option 2: Faithful "NUT Way" Length Tracking with Truncation Tolerance**", because it guarantees universal compatibility by adhering exactly to the hardware's declared specifications rather than guessing or hardcoding sizes. It eliminates the need for arbitrary vendor quirks, provides a stable fix for `0x108` and timeout errors, and ensures long-term system stability by completely eradicating heap fragmentation.

## Consequences
### Positive
* Universal compatibility with strict (e.g. CyberPower) and padded (e.g. APC) UPS firmwares.
* Complete elimination of heap fragmentation in the `HIDParser` and drivers.
* Obsoletes manual quirks (e.g., `QUIRK_MAX_REPORT_SIZE_1`), reducing maintenance overhead.
* Significant reduction in TIMEOUT and Invalid Response errors during polling.

### Negative
* The codebase requires modifying the upstream ESP-IDF component `hid_host.c` to handle truncation, making future ESP-IDF updates slightly more complex.
* Transitioning from `String` to `const char*` and fixed arrays requires stricter memory management and string manipulation (e.g., `strcmp`, `strncat`).

## Impact on Agent Implementation
* Agents MUST NEVER use hardcoded report lengths (like `64`) when calling `requestReport`. They MUST ALWAYS use `host->getHIDParser()->getExpectedLength(report_id, report_type)`.
* Agents MUST NEVER use `String` objects for HID paths or map keys. Use `char[80]` arrays or `const char*` with proper custom comparators for `std::map`.
* Agents MUST assume that padding bits are part of the report length calculation, maintaining the "NUT Way" exact bit counting algorithm.
* This ADR effectively supersedes ADR 0005 regarding report length handling and quirks.
