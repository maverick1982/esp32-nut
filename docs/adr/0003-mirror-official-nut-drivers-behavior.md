# Faithfully Mirror Official NUT Drivers and Subdrivers Behavior

* **ADR ID:** 0003
* **Status:** Accepted
* **Date:** 2026-09-05
* **Authors:** Antigravity / @maverick1982

## Context and Problem Statement
When developing and maintaining drivers and subdrivers in esp32-nut (such as APC, CyberPower, Eaton, Powercom, etc.), how should we determine parsing logic, scaling factors, status mappings, report formats, and vendor-specific quirks? 
Inventing custom heuristics or proprietary interpretations leads to behavioral discrepancies, unexpected bugs with standard NUT clients (like pfSense, TrueNAS, Synology, nut-client), and fragile workarounds.

## Considered Options
* **Option 1: Custom heuristic implementations.** Deduce vendor protocols empirically based on isolated test logs and device samples.
* **Option 2: Mirror the official Network UPS Tools (NUT) project implementations as the single source of truth.** Look up the upstream NUT driver/subdriver source code (available locally in 
ut_repo/) and replicate their field mappings, conversion formulas, and quirks verbatim in C++.

## Decision Outcome
Chosen option: "**Option 2: Mirror official NUT drivers and subdrivers as the single source of truth**", because the upstream NUT project represents decades of real-world hardware reverse engineering, community validation, and protocol standardization. Faithfully reproducing their exact behavior guarantees full compatibility with existing NUT ecosystems and clients.

Specifically:
1. **Source of Truth**: The official NUT repository (mirrored locally in 
ut_repo/drivers/) is the authoritative reference for all HID usage tables, subdrivers (pc-hid.c, cps-hid.c, mge-hid.c, powercom-hid.c, etc.), and protocol conversions.
2. **Quirks Alignment**: Vendor quirks (such as Eaton beeper paths, Powercom string inversion, APC battery date encoding) must match the logic and naming found in upstream NUT subdrivers.
3. **Variable and Status Naming**: Variable keys (e.g. ups.status, attery.charge, ups.load) and status flags (OL, OB, LB, CHRG, DISCHRG, etc.) must strictly conform to official NUT variable definitions and state evaluation sequences.

## Consequences
### Positive
* High reliability and predictable behavior matching official NUT daemons (upsd/usbhid-ups).
* Seamless interoperability with standard NUT monitoring clients (Proxmox, pfSense, Home Assistant, etc.).
* Streamlined development and review: whenever a doubt arises about how a value should be parsed, the upstream C source in 
ut_repo/ provides an indisputable answer.

### Negative
* May inherit historical upstream complexities or quirks designed for legacy hardware revisions.

## Impact on Agent Implementation
* **Upstream First Rule**: When implementing or fixing any driver, subdriver, or quirk, the agent MUST first inspect the corresponding source code in 
ut_repo/ (e.g., 
ut_repo/drivers/usbhid-ups.c and 
ut_repo/drivers/*-hid.c).
* **No Invented Heuristics**: The agent MUST NOT invent custom formulas, arbitrary bit shifts, or synthetic status names if an established pattern exists in official NUT.
* **Traceability in Code**: Driver implementations and comments should explicitly cite the upstream NUT file and function being mirrored (e.g. // Mirrored from nut/drivers/apc-hid.c: apc_load_claim()).
