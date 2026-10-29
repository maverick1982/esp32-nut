---
type: decision
title: "Single HID-to-NUT Table per Subdriver, Converging on Upstream hid_info_t"
description: "Documento ADR: Single HID-to-NUT Table per Subdriver, Converging on Upstream hid_info_t"
tags: [adr, decision, drivers, nut, hid, upstream-fidelity]
---
# Single HID-to-NUT Table per Subdriver, Converging on Upstream hid_info_t

* **ADR ID:** 0011
* **Status:** Accepted
* **Date:** 2026-10-29
* **Authors:** @maverick1982, Claude
* **Extends:** ADR 0003 (upstream fidelity becomes verifiable row by row)
* **Amends:** ADR 0004, once US-079 is done (`GenericDriver` stays the base class for the poll state machine and the hooks, its standard mappings become the `genericHid2Nut()` table)
* **Related:** ADR 0007 (String fragmentation), ADR 0010 (`ups-protocol` module, consumers without HID types), specs US-066 (table and binding engine, RW rows), US-073…079 and US-106 (migrations)

## Context and Problem Statement

Two mechanisms turn HID usages into NUT variables today:

* **Hand-written mappings.** `GenericDriver` and every derived driver have `Mapping` tables (HID path + function that writes UPSData keys), indexed by `UsageMapIndex`. They were written by reading the upstream subdrivers, then corrected one issue at a time (for example CyberPower scales and variables in #43, the Legrand and Liebert drivers of US-087 and US-097).
* **The NUT table** (`lib/USBHostUPS/src/NutTable.h`, US-066). Rows with the same shape as upstream `hid_info_t` (`info_type`, `info_flags`, `info_len`, HID path, format, `hidflags`, conversion), copied in upstream order with the source line in a comment, bound to the device usages by a small engine. Since US-066 it holds only the writable rows (`ST_FLAG_RW`) of mge-hid, apc-hid, cps-hid and ecoflow-hid, plus the `ups.delay.*` paths of powercom-hid, liebert-hid and legrand-hid.

The two overlap: `postDecode()` writes the table rows last, so that the table has the final word over a hand-written mapping of the same key. Every value therefore has two possible sources, a difference from usbhid-ups can only be found by reading code side by side, and each new driver written by hand adds work to any later alignment.

On 2026-10-15 the convergence on the upstream tables was chosen as "approach C" in US-066. On 2026-10-29, after EP-006 (US-103…105) and the new Legrand and Liebert drivers, the migration specs were reviewed and three open questions were decided:

1. what to do with the keys we publish that the upstream subdriver does not;
2. what to do with the `unmapped.*` rows of the upstream tables;
3. how new drivers are written from now on.

## Considered Options

* **Option 1: keep the hand-written mappings** and align them to upstream issue by issue.
* **Option 2: rewrite everything at once**, copying all upstream tables and deleting the mappings in one change.
* **Option 3: incremental convergence**, one subdriver per spec, each migration verified on the record/replay fixtures, ending with a generic table and the removal of the hand-written mappings.

## Decision Outcome

Chosen option: **Option 3, incremental convergence**, because each step is small enough to be checked against its fixtures and, where possible, on real hardware, while option 1 keeps the double source forever and option 2 changes every brand at once on hardware we mostly do not have.

### Rule 1: one table per subdriver, upstream shape and order

* Each subdriver has one table `xxxHid2Nut()` in `NutTable.h`, with the rows of `xxx_hid2nut` of upstream in the same order, each with its source line (`// mge-hid.c:2013`).
* A row we do not take stays in the table as a comment with the reason (for example a conversion that cannot be ported, or a choice made in a spec such as US-087's `divide_by_10`).
* The engine handles read-only rows with their formats and `info_lkp` conversions, `BOOL` rows that set the internal `ups.status.*` keys, writable rows, `HU_FLAG_ABSENT` rows and, through `HidControl`, `HU_TYPE_CMD` rows.
* The engine writes a key to UPSData only when its value changes, not at every report: read-only rows would otherwise allocate many Strings per report (ADR 0007).
* What is not a table row stays a driver hook: quirks, poll policy, string descriptors, beeper encoding, model and country context.

### Rule 2: keys that only we publish (mixed rule)

For every key the firmware publishes for a device and the upstream subdriver of that device does not:

* **Same name, different meaning from upstream: replace it with the upstream row.** Known case: `outlet.1.switch` / `outlet.2.switch` of `EatonDriver`, both set to `1`/`0` from a single usage, become the `outlet.N.status` rows of `mge_hid2nut`, one per outlet, with `on`/`off`. It is a visible change, listed in the release notes.
* **Standard NUT name (`docs/nut-names.txt`) with the right meaning, not read by the upstream subdriver: keep it** as a documented extension. Known cases: `battery.mfr.date`, `ups.mfr.date`.
* **Name not in `docs/nut-names.txt`: remove it.** Known case: `battery.capacity.full` (upstream uses it only in `idowell-hid`).

The full list for each subdriver comes out of its migration, by comparing the replay of its fixtures with the upstream rows, and is written in the spec and in the replay test.

### Rule 3: `unmapped.*` rows are copied, never built

Upstream keeps the rows generated by `gen-usbhid-subdriver.sh` for usages without a standard name (`unmapped.*`) inside `#if WITH_UNMAPPED_DATA_POINTS`, which is off in production builds ("Production driver builds must not include any non-standard names", `docs/hid-subdrivers.txt`). We copy those rows as they are, inside the same `#if`, and never define the macro. No `unmapped.*` key is published, and the table stays comparable line by line with the upstream file. It concerns cps-hid (5 rows), powercom-hid (11), liebert-hid (11) and ecoflow-hid (19); mge-hid, apc-hid, legrand-hid and openups-hid have none. The developer need behind these rows is covered by the USB diagnostics export, which carries the whole descriptor and the raw values.

### Rule 4: new drivers start from their upstream table

Once the engine handles read-only and `BOOL` rows (end of US-073), a new driver is born with the table of its upstream subdriver and without hand-written mappings; hooks only for what is not a table row. A driver requested before US-073 is done is planned after it or, if urgent, written by hand with a migration spec already in the backlog (as US-106 does for Legrand and Liebert).

### Rule 5: every migration declares its differences

* The replay of every fixture of the subdriver is compared before and after the migration. Each difference is either motivated by an upstream row or removed.
* The differences (keys added, removed, renamed, formats) are listed in the replay test and in the release notes.
* `ups.status` and `LIST CMD` do not change unless an upstream row explains it.
* A migration of a brand we do not have is tested by a user with that UPS before the release.

### Migration order

US-073 and US-074 (Eaton, mge-hid; US-073 also builds the engine), then US-075 (CyberPower), US-076 (APC), US-077 (Powercom), US-078 (EcoFlow and OpenUPS), US-106 (Legrand and Liebert), and last US-079: `genericHid2Nut()` for unknown brands, removal of the hand-written mappings, of `UsageMapIndex` if unused and of `CommandCatalog.h`.

## Consequences

### Positive

* One source per value: a difference from usbhid-ups is found by comparing a row with the upstream line, not by reading two code paths.
* Upstream fixes reach the firmware by copying rows, in the order of the upstream file.
* Commands, writable variables and read-only values share one description of the device.
* The Eaton outlet status becomes correct for each outlet.

### Negative

* About 43 story points of migrations, most of them on brands we cannot test in house.
* Visible changes for users: `outlet.N.switch` becomes `outlet.N.status`, `battery.capacity.full` disappears; each migration may show more such differences, to be explained in the release notes.
* Until US-079 the two mechanisms coexist, and `postDecode()` keeps giving the table the final word.
* The copied `unmapped.*` rows and the commented exclusions make the tables longer than the code that runs.

## Impact on Agent Implementation

* Do not add new hand-written `Mapping` entries for a subdriver that already has, or is being given, its table: add or fix the row in `xxxHid2Nut()`. After US-073, a new driver gets its table first.
* Copy rows from `nut_repo/drivers/<subdriver>.c` in the upstream order, with the `// file.c:line` comment. Do not reorder, merge or rename rows; exclude a row only with a comment that gives the reason.
* Never define `WITH_UNMAPPED_DATA_POINTS`, and never publish a key whose name is not in `docs/nut-names.txt`, except the documented extensions of rule 2.
* Before inventing a key, look it up in `docs/nut-names.txt`; if the concept has no name, leave it unpublished.
* Keep `NutTable.h` inside the `ups-protocol` module (ADR 0010): the NUT server, the web API and the web UI see only `IUpsSource` and `UpsCommands.h`.
* In every migration, run the replay of all fixtures of the subdriver before and after, and write the differences in the replay test and in the release notes.
