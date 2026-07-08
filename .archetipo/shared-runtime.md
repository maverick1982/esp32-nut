# ARchetipo Shared Runtime

This file contains runtime rules shared by all ARchetipo skills.
Load this file once at activation time, before loading any flow reference.

## CLI Runtime Contract

ARchetipo skills use `archetipo` as the only backend for PRD, backlog, plan, task, and workflow-status operations.

Common rules:

- Run `archetipo config show` at the start of every skill that needs project metadata or configured paths.
- Parse stdout as a JSON success envelope:

```json
{"schema":"archetipo/v1","kind":"<kind>","data":{...}}
```

- Parse stderr as a JSON error envelope:

```json
{"schema":"archetipo/v1","kind":"error","error":{"code":"E_*","message":"...","hint":"..."}}
```

- Error envelopes MAY include an optional `error.details` field with machine-readable corrective data. Skills must tolerate its absence and must never branch on its shape alone — always branch on `error.code` first, then use `details` only as corrective instructions.

- `archetipo validate ...` commands return a normal stdout envelope with `kind:"validation_result"`. Structural validation outcomes are reported in `data.ok` and `data.findings`; error envelopes are reserved for process failures such as unreadable input, missing files, config errors, or internal failures.

- Branch on `error.code`, never on `error.message`.
- Treat exit codes as stable:
  - `0`: success
  - `1`: generic error
  - `2`: invalid input
  - `3`: connector/backend failure
  - `4`: missing precondition
- When `.archetipo/config.yaml` is absent, the CLI applies its built-in defaults for connector, paths, and workflow statuses.
- Command-specific invocation forms, payloads, and semantics belong in each skill that uses them. Do not infer CLI operations from documentation files.
- `archetipo config show` returns `data.project_root`: the ABSOLUTE project root containing `.archetipo/config.yaml` (or the current directory when defaults are used). Run connector/backlog commands from this root unless a command-specific rule says otherwise.

## Worktree Working Directory

Specs may be implemented inside a per-spec git worktree (worktree workflow). To make every skill operate on the right files **deterministically** — never depending on the model remembering to prefix paths — the spec envelope carries the resolved working directory.

`archetipo spec show <US-CODE>` and `archetipo spec next` return `data.workdir`: the ABSOLUTE directory for that spec — the spec's git worktree when one exists on disk, the project root otherwise. It is always populated, and the CLI derives it from the actual filesystem state (not from a stored field that could drift). After resolving a spec, treat `data.workdir` as the single root for ALL of that spec's file work:

- every file you read, edit, search or create for the spec must live under `data.workdir`;
- run every shell/git/test command for the spec with `data.workdir` as the working directory.

Connector commands (`archetipo spec plan`, `archetipo task done`, `archetipo spec review`, etc.) still operate on backlog/config state and must be run from `data.project_root` from `config show`. Work on the codebase for a spec happens under `data.workdir`.

When the spec has no worktree, `data.workdir` is just the project root and nothing changes. Branch only on this value — never on connector type. (`data.spec.worktree` is the raw, project-root-relative field; always prefer `data.workdir`, which is absolute and filesystem-checked.) If a command such as `archetipo spec start` may create a worktree, run `archetipo spec show <US-CODE>` again afterwards and replace the in-memory spec/tasks/workdir with that post-start envelope before touching files.

## Language Policy

Detect the output language from the strongest available source, in priority order:

1. Language of the backlog (if a backlog exists and is readable)
2. Language of the PRD (if no backlog is available)
3. Language of the user's current conversation

Apply the detected language to all user-facing output: messages, document section headers, error messages, and opening announcements.

### Template Rendering Rule

Templates and example text in skill files are **structural guides written in English**. When generating the final artifact, render every static element in the detected language. This includes:

- Document titles and section headings (e.g. "Elevator Pitch", "Vision", "User Personas")
- Table headers (e.g. "Phase | Action | Thought | Emotion | Opportunity")
- Bold inline labels (e.g. "**Author:**", "**Role:**", "**Goals:**", "**Pain Points:**")
- Connective phrases and sentence scaffolding (e.g. "For **X**, who has the problem of **Y**, **Z** is a **C** that..." → translate the connectives "For", "who has the problem of", "is a", "that", "Unlike", "our product")
- Enumerations, captions, footers, and any hard-coded prose around placeholders
- Agent role captions (e.g. "Proposed by:")

Rules:

1. Keep every `{{PLACEHOLDER}}` token **unchanged** — do not translate placeholder names.
2. Keep code blocks, file paths, CLI commands, and identifiers unchanged.
3. Keep technical terms that have no natural translation (e.g. "MVP", "ADR", "CI/CD", "ORM") unchanged unless the target language has a standard equivalent already used in the existing artifact.
4. Keep consistency with any existing artifact language (PRD → backlog → specs must all use the same language).
5. If the detected language is English, render the template as-is.

The final output must read as a single coherent document in the detected language — never a mix of English scaffolding and localized content.

## Assumptions and Questions

Ask the user only when all these conditions are true:

1. The missing information is critical to generate a correct output
2. The information cannot be reasonably inferred from the rest of the context
3. Proceeding would likely create a materially wrong result

If questions are needed:

- ask at most 3
- group them in one message
- allow the user to skip them

For non-critical gaps:

- infer a reasonable assumption
- continue
- record the assumption or open question in the final artifact

## Small Model Discipline

ARchetipo artifacts must be usable by smaller or lower-cost models during later phases. Prefer explicit contracts over broad interpretation:

- Keep generated specs small, independently demonstrable, and testable.
- Make `Demonstrates` concrete enough to become a review script.
- In implementation plans, split work into small tasks with local context, clear allowed changes, verification commands, done criteria, and blockers.
- Do not leave architectural choices implicit for implementation. If a decision matters, put it in the plan.
- Before persisting generated specs or plans, run the relevant `archetipo validate ...` command and repair blocking issues instead of saving malformed artifacts.
- Treat warnings as quality feedback. They do not block persistence, but fix them when the repair is straightforward.

## Conversation Rules

- Each agent speaks in character
- Never mention internal mode names, workflow names, or routing decisions in the conversation

## Agent Persona

When an agent speaks, always render the speaker as `icon + name`, for example:

```text
💎 Andrea: [content]

🧭 Costanza: [content]
```

This rule applies to any skill that defines named agents with personas.

## File Output Rules

- Use the configured output path whenever present
- Create parent directories if they do not exist
- Overwrite the target generated artifact for the current run unless the active flow explicitly says otherwise
- When a connector overrides write-output behavior, follow that connector for I/O and keep the domain logic unchanged
