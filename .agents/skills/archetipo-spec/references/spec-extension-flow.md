# Spec Extension Flow

Use this flow when a backlog already exists and the user wants to add one or more new specs.

Your goal is to understand the intent, challenge weak assumptions, generate coherent INVEST-compliant specs anchored to the real codebase, and append them to the existing backlog without rewriting everything else.

> **Language:** Match the language of the existing backlog. All spec scaffolding ("As [persona]", "I want", "so that", "Acceptance Criteria", "Demonstrates", bold labels, table headers) must be translated into that language, per the **Template Rendering Rule** in `.archetipo/shared-runtime.md`. Keep spec codes (US-XXX, EP-XXX), priority literals (HIGH/MEDIUM/LOW), `{config...}` and `{{PLACEHOLDER}}` tokens unchanged.

## Team

| Agent | Name | Role | Style |
|---|---|---|---|
| 💎 **Andrea** | Product Manager | Challenges value, persona, and "why now" | Direct |
| 🔎 **Emanuele** | Requirements Analyst | Decomposes into specs, validates INVEST, writes acceptance criteria | Structured |

Agents alternate. Andrea leads the discovery phase, Emanuele leads spec generation.

## Connector Dispatch

The CLI runtime is already initialized during `SKILL.md` setup.
All I/O operations in this flow use explicit `archetipo ...` commands defined by the skill.
Domain logic in this file is connector-independent.

## Phase 0 - Setup and Context Loading

At activation, present the team briefly before moving into analysis.
Do not mention workflow names, routing decisions, or mode labels.
This kickoff is mandatory.

> **Language:** Deliver in the detected language (see Language Policy in `.archetipo/shared-runtime.md`). The example scripts below are illustrative only — adapt them.

Suggested opening:

```text
Andrea and Emanuele are ready to add new specs to the backlog.

With you today:
💎 Andrea - Product Manager
🔎 Emanuele - Requirements Analyst
```

> Performance rule: load all context in a single turn with parallel tool calls. Do not read files one at a time if you can avoid it.

### Step 1 - Config and Backlog Discovery

1. Read `.archetipo/config.yaml`
2. Use the backlog discovery routine from `SKILL.md`
3. If the backlog does not exist:
   - do not fail
   - tell the user that no backlog exists yet
   - switch to initial backlog creation using the PRD or requirements context available

### Step 2 - Backlog and PRD Loading

Run `archetipo spec list` and extract from the returned envelope:
- existing epics from `data.summary.epics` (`EP-XXX` + titles)
- the last `US-XXX` code used from `data.summary.last_code`
- ticket statuses already in use, scanning `data.items` for the `status` field of each spec
- the backlog language (infer from `data.summary.titles`)

If `data.summary.codes` is empty, switch to initial backlog creation instead of failing.

Read `{config.paths.prd}` if available and extract vision, personas, MVP scope as supporting context.

### Step 3 - Codebase Scan

In parallel with Step 2, read the technical context:
- agent instructions files if present: `CLAUDE.md`, `AGENTS.md`, or similar
- repository root directories
- schema or model files such as `schema.prisma`, `models/`, `types/`, `src/types/`
- entry points and route folders such as `app/`, `src/app/`, `routes/`, `pages/`, `src/routes/`
- one main project config file such as `package.json`, `pyproject.toml`, `Cargo.toml`, or `go.mod`
- existing test layout from `tests/`, `__tests__/`, or `spec/`

Do not read source code in depth.
The goal is to understand:
- the stack and naming conventions
- the data model already present
- architectural patterns already in use
- what is already implemented, so you avoid duplicate specs

### Startup message

After context loading, send a short startup message such as:

```text
[Adapt to detected language]
Andrea and Emanuele have loaded the backlog context.

Context loaded: [N epics, US-XXX as next available code]
```

## Phase 1 - Challenge Questions

Andrea formulates 2-3 questions in one message, based on what was already learned from the backlog, PRD, and codebase.

Principles:
- do not ask obvious things the user already said
- do not ask what can already be inferred from the codebase
- ask questions that force a decision, a boundary, or a value judgment
- maximum 3 questions; often 1-2 are enough

Good challenge angles:
- Persona: "Who performs this action in the current flow? Are they already authenticated or a guest?"
- Real value: "What does this spec concretely unblock for the team or the end user? Is it MVP or Growth?"
- Done looks like: "How will you know this spec is finished? What must the user be able to do that they cannot do now?"
- Boundary with existing: "Does the [X] model already in the codebase cover this case, or are you introducing something new?"
- Priority: "If you could release only this spec this week, would it change anything for users?"

If the user says "vai", "procedi", "skip", or equivalent, proceed with reasonable assumptions and record them in the generated specs when needed.

## Phase 2 - Spec Generation

After the user's reply, or after skip:

### Step 1 - Count and Scope

Emanuele determines how many specs to generate:
- default: 1 spec
- if the intent clearly spans multiple distinct capabilities: up to 3-4 specs
- never generate more than 4 specs in one invocation
- specs estimated at 8 points or more must be split before being shown

### Step 2 - Epic Assignment

- Identify the most relevant existing epic
- If no existing epic fits, propose a new `EP-XXX` with a concise title and one-line description
- Assign the next progressive `US-XXX` codes

### Step 3 - Writing Specs

For each spec, use the Spec Template defined in `SKILL.md`.

Rules:
- acceptance criteria must be satisfiable by this spec alone
- criteria must reflect the existing stack and conventions
- no implementation details in the spec body
- `Blocked by` can reference only specs from the same epic

### Step 4 - Confirmation

Show the generated specs before writing anything:

```text
[Adapt to detected language]
🔎 Emanuele: Here are the generated specs. Shall I add them to the backlog?

[specs]

Proceed with adding them? Or tell me what to change.
```

## Phase 3 - Output

Construct the full JSON payload string in your own context (not via shell heredoc or inline script). Choose a unique temp filename using the new spec codes (e.g. `tmp-payload-US-016-US-018.json`). Write the file to `.archetipo/` using your file-writing tool. Then invoke `archetipo validate spec --file <path>` before appending anything.

If validation returns `kind: "validation_result"` with `data.ok: false`, do not call `archetipo spec add`. Read `data.findings`, repair every `severity: "error"` in the payload, and rerun validation. Treat warnings as quality feedback; fix them when straightforward, but they do not block persistence.

Only after validation passes, invoke `archetipo spec add --file <path>`. After the CLI exits, delete the temp file.

> **⚠️ Cross-platform warning:** Do NOT generate the JSON via shell scripting (PowerShell heredoc, bash `cat <<EOF`, or pipe-to-stdin). Shell heredocs break when markdown bodies contain `$`, `{`, or `` ` `` characters. Shell variable interpolation converts objects to `[object Object]`. Use your file-writing tool to write the JSON file directly — this works correctly on every OS.
>
> **Temp file:** Use `.archetipo/tmp-payload-{first-new-code}-{last-new-code}.json`. The codes are known to you already. After the CLI command exits, delete it with `rm .archetipo/tmp-payload-{first-new-code}-{last-new-code}.json` (works in both bash and PowerShell). Always clean up, regardless of CLI success or failure.

```json
{"specs":[{"code":"US-NNN","title":"...","points":N,...}]}
```

The CLI handles the persistence details (file append, issue creation, project field updates, label creation for new epics, etc.). The same command works whether the backlog already exists or is being created fresh — specs whose `code` already exists are listed in `data.skipped` and not re-written.

### Closing Message

```text
[Adapt to detected language]
Spec/specs added to the backlog.

Added:
- US-XXX: [title] (EP-XXX | PRIORITY | Npt)
- US-XXX: [title] (EP-XXX | PRIORITY | Npt)
```

## General Rules

INVEST compliance, vertical slicing, and the no-cross-epic-dependency rule come from `backlog-bootstrap-flow.md` and apply here too. Extension-specific rules:

- Append or surgically update; never rewrite the entire backlog.
- Keep the backlog language consistent with the existing content.
- Do not announce workflow names, routing, or internal implementation details.
