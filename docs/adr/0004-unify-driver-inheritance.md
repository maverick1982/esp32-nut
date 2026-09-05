# Unify Sub-Driver Inheritance via GenericDriver

* **ADR ID:** 0004
* **Status:** Accepted
* **Date:** 2026-09-05
* **Authors:** Antigravity / @maverick1982

## Context and Problem Statement
Currently in the esp32-nut project, PowercomDriver correctly inherits from GenericDriver, relying on the base class to parse standard Power Device Class (PDC) HID descriptors and only overriding what is necessary. However, APCDriver, EatonDriver, and CyberPowerDriver inherit directly from the naked IUPSDriver interface. 
Because of this, each of these three sub-drivers duplicates an extensive mapping array (mapping UPS.PowerSummary.PresentStatus.ACPresent, etc.) inside their respective decodeReport methods. This severe code duplication violates the DRY (Don't Repeat Yourself) principle, makes maintenance difficult, and increases the risk of inconsistent behavior if a new standard property needs to be added across all devices.

## Considered Options
* **Option 1: Keep drivers completely isolated.** Continue copying the base HID tables into every new sub-driver.
* **Option 2: Unify inheritance.** Make all sub-drivers (APCDriver, EatonDriver, CyberPowerDriver, etc.) inherit from GenericDriver. Let GenericDriver::decodeReport() handle the standard USB HID PDC parsing, and let the sub-drivers handle only vendor-specific quirks.

## Decision Outcome
Chosen option: "**Option 2: Unify inheritance**", because it immediately eliminates massive blocks of redundant code, centralizes the parsing of standard USB HID variables to a single point of failure/maintenance (GenericDriver), and enforces a consistent object-oriented hierarchy.

## Consequences
### Positive
* Significant reduction in code size (deletion of hundreds of lines of duplicated mappings).
* Adding support for a new standard USB HID variable now only requires a single modification in GenericDriver.
* Ensures all UPS devices fallback gracefully to standard HID parsing even if their specific sub-driver misses a mapping.

### Negative
* Sub-drivers now strictly depend on GenericDriver's behavior; if GenericDriver introduces a bug, it will affect all inherited sub-drivers. (Mitigated by our comprehensive native unit testing suite).

## Impact on Agent Implementation
* **Driver Scaffolding Rule**: Any new UPS driver created in the future MUST inherit from GenericDriver (not IUPSDriver directly) unless it relies on a completely non-HID proprietary protocol.
* **Override Pattern**: The decodeReport method in a sub-driver MUST call GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data); either before or after its custom parsing block, rather than reinventing standard mappings.
