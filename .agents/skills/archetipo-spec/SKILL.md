---
name: archetipo-spec
description: Creates the initial product backlog from a PRD, or appends new specs to an existing one. Use whenever the user asks for a backlog, epics, specs, user stories, or wants to add a single feature — even if the backlog does not yet exist.
---

# ARchetipo - Spec Skill

You are the public entry point for ARchetipo backlog and spec work. A spec is the unit of work in the backlog; its body is written as a user story.

Your job is to understand whether the user needs to create the first backlog or extend an existing one, load only the references that matter for that case, and execute the correct flow without making the user choose between overlapping skills.

Treat routing as an internal implementation detail.

## Shared Runtime

Read `.archetipo/shared-runtime.md` for Language Policy, Assumptions and Questions, Conversation Rules, and File Output Rules.

## Core Principle

Keep the working context lean:
- Load this file first
- Load exactly one main flow reference at activation time
- Connector logic lives in the `archetipo` CLI; this skill never interprets a connector file

## Supported Modes

### `mode: bootstrap-backlog`

Use this mode when:
- the user asks to generate a backlog from an existing PRD or requirements artifact
- no backlog exists yet
- the user asks for the first epics or specs of the project

In this mode:
1. Read this file
2. Read `./references/backlog-bootstrap-flow.md`
3. Use the PRD as the primary source and create the initial backlog

### `mode: extend-backlog`

Use this mode when:
- a backlog already exists
- the user asks to add, refine, split, or append specs
- the user wants to extend the backlog without regenerating it from scratch

In this mode:
1. Read this file
2. Read `./references/spec-extension-flow.md`
3. Use the existing backlog as the primary source and PRD/codebase as supporting context
4. Append or create only the requested items

## Config Loading & Connector Dispatch

1. Run `archetipo config show` and parse the stdout JSON envelope. The `data` field is a `SetupInfo` object.
2. On failure, parse stderr as the JSON error envelope and branch on `error.code`.
3. This skill uses only these CLI operations:
   - `archetipo config show`
   - `archetipo spec list`
   - `archetipo validate spec --file <path|->`
   - `archetipo spec add --file <path|->`

Extract and keep available from `data`:
- `connector`
- `paths.prd`
- `paths.backlog`
- `paths.planning`
- `paths.mockups`
- `workflow.statuses`

## Backlog Discovery

Use this routine whenever the skill must decide whether it is extending an existing backlog or creating the first one.

Run `archetipo spec list` and parse the JSON envelope. The CLI returns `data.items` (full Spec objects) and `data.summary` with codes, last code, epics, and titles for the existing backlog.

If `data.summary.codes` is non-empty, use the existing specs as the source of truth for backlog extension.
If `data.summary.codes` is empty, treat the project as backlog-less and route to initial backlog creation.

## PRD Discovery

Use this routine whenever initial backlog creation needs a PRD or when spec extension needs extra product context:

1. Try to read `{config.paths.prd}`
2. Only if that fails with file not found:
   - search markdown files in `docs/`
   - prefer files whose name or content indicates they are a PRD
3. Only if still not found:
   - search for `PRD*` files anywhere in the project

If a PRD is not found and the active flow needs one, ask the user for one of these:
- the file path
- the content pasted directly
- confirmation that they want to run product inception first

## Intent Routing

Use these routing rules before producing any substantive output.

1. Load this file
2. Run `archetipo config show` and use `data.paths` / `data.workflow.statuses` as the project metadata source
3. Run backlog discovery
4. Decide the flow

Prefer `mode: bootstrap-backlog` when:
- the backlog does not exist
- the user explicitly asks to generate the backlog from a PRD or requirements
- the repository has a PRD but no backlog yet

Prefer `mode: extend-backlog` when:
- the backlog already exists
- the request is about one or more incremental specs, a new feature slice, a refinement, or a split

If a backlog already exists but the user explicitly asks to regenerate it from the PRD:
- ask for confirmation before overwriting or recreating the initial backlog

Do not expose mode names, routing decisions, or workflow labels in user-facing messages.

## Runtime Rules

- Ask clarifying questions only when critical information is missing and cannot be inferred responsibly
- Group clarifying questions in a single message when possible
- Optimize specs for downstream low-cost implementation models: keep each spec small, independently demonstrable, and free of hidden architectural decisions.
- Every spec body must include a concrete `Demonstrates` section and checklist acceptance criteria. If the increment is foundational, `Demonstrates` must describe what a developer or reviewer can actually run or inspect.

## Output Boundaries

- Initial backlog creation belongs to this skill, not to `archetipo-inception`

## CLI payload shape

`archetipo spec add` expects a JSON or YAML payload with this shape:

```json
{"specs": [
  {"code": "US-001", "title": "...", "epic": {"code": "EP-001", "title": "..."},
   "priority": "HIGH", "points": 3, "status": "TODO", "body": "...markdown..."}
]}
```

The `body` field carries the spec content rendered as a user story (see Spec Template below). `points` is the canonical field name (no `story_` prefix).

## Spec Template

Use this shape for every spec, in both initial backlog generation and spec extension. The body follows the user-story agile format; the container is a spec.

```markdown
#### US-XXX: [Concise action-oriented title]

**Epic:** EP-XXX | **Priority:** HIGH | **Points:** N | **Status:** {config.workflow.statuses.todo}
**Blocked by:** -

**User Story**
As [persona name or role],
I want [specific action or capability],
so that [concrete benefit tied to a PRD goal].

**Demonstrates**
After implementing this spec, [describe what can be concretely observed or verified — e.g. for a feature: "the user can open the reports page, click Export, and download a CSV"; for a foundational spec: "a developer can run the test suite and see all checks pass"]

**Acceptance Criteria**
- [ ] [Primary happy path]
- [ ] [Validation or error case]
- [ ] [Relevant edge case]
```

## Non-Feature Specs (Refactoring / Tech Debt)

Not every valuable slice is user-facing. Refactoring, dependency upgrades, performance work, and debt repayment are legitimate specs — but they need different acceptance criteria, or they become unverifiable "improve the code" wishes.

Rules for non-feature specs:

- **The persona is a developer or operator**, not an end user. "As a developer maintaining the billing module..." is a valid user story opening.
- **The benefit must still be concrete**: faster builds, safer changes, lower latency, fewer production incidents — never "cleaner code" without an observable consequence.
- **`Demonstrates` must be observable without the feature changing**: a measurement, a passing regression suite, a removed dependency in the lockfile, a profiler trace. If nothing observable changes, the spec is not ready.
- **Acceptance criteria are regression + target**: at least one criterion pins existing behavior ("the full test suite passes unchanged"), and at least one states the measurable goal of the work.
- **Same INVEST rules apply**: a refactoring spec too large to verify in one review must be split by module or by seam, not delivered as a big-bang rewrite.
- **No demo video**: per the implement skill's e2e policy, purely technical specs skip video recording; expect the evidence to be test output and measurements instead.

Template variant (only the marked fields differ from the standard template):

```markdown
#### US-XXX: [Refactor/upgrade goal, action-oriented]

**Epic:** EP-XXX | **Priority:** MEDIUM | **Points:** N | **Status:** {config.workflow.statuses.todo}
**Blocked by:** -

**User Story**
As [developer/operator role],
I want [the structural change — e.g. "the payment client extracted behind an interface"],
so that [the observable consequence — e.g. "providers can be swapped without touching checkout code"].

**Demonstrates**
After implementing this spec, [observable, feature-neutral evidence — e.g. "the full test suite passes unchanged and the new module has no import from the legacy package", or "p95 latency of /search drops below 200ms in the benchmark"]

**Acceptance Criteria**
- [ ] [Regression guard: existing behavior is preserved — name the suite or contract that proves it]
- [ ] [The measurable target of the work — metric, structure, or dependency state]
- [ ] [Cleanup boundary: what is explicitly out of scope, when relevant]
```
