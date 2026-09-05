# Record Architecture Decisions

* **ADR ID:** 0001
* **Status:** Accepted
* **Date:** 2026-09-05
* **Authors:** Antigravity / @maverick1982

## Context and Problem Statement
We need to track architectural decisions made for the ESP32 NUT project. Without a formal record, future agents or developers might unknowingly violate past design constraints, re-introduce rejected dependencies, or lack context on why certain implementations were chosen.

## Considered Options
* Do not record decisions formally.
* Use a wiki or external documentation tool.
* Use Markdown Any Decision Record (MADR) directly in the repository.

## Decision Outcome
Chosen option: "Use Markdown Any Decision Record (MADR) directly in the repository". MADR provides a lean, text-based structure that is perfectly suited for Git version control and agentic workflows (Antigravity can read, parse, and enforce these decisions via workspace rules).

## Consequences
### Positive
* Decisions are versioned alongside the code.
* AI agents can automatically enforce rules derived from accepted ADRs.
* New contributors have a historical log of "why" things are built the way they are.

### Negative
* Requires discipline to create a record for every significant architectural choice.

## Impact on Agent Implementation
* Agents MUST read docs/adr/README.md before planning tasks.
* Agents MUST NOT violate the decisions recorded here.
* Agents MUST use the /new-adr workflow to propose new architectural changes.
