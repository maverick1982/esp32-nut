---
name: archetipo-autopilot
description: Runs the full archetipo pipeline autonomously on backlog specs — for each TODO spec (by priority), spawns clean isolated subagents to plan and implement in sequence, verifying status transitions between steps. Use this skill when the user wants to "run everything", "implement all specs", "autopilot the backlog", "plan and implement everything", "batch process the entire backlog", "fai tutto in autonomia", "esegui tutto dal backlog", or any variation of fully autonomous end-to-end execution from backlog to working code. This skill differs from archetipo-loop because it chains multiple steps (plan → implement) per spec as an atomic pipeline unit, rather than running a single command repeatedly.
---

## Compatibility

This skill requires **isolated subagent/worker support** from your AI coding tool.

| Tool | Status |
|---|---|
| Claude Code (Agent tool) | Supported |
| Cursor (Task tool) | Supported |
| Gemini CLI (`create_sub_agent`) | Supported |
| Roo Code (`new_task` / Orchestrator) | Supported |
| Codex.ai | **Not supported** — lacks subagents |
| GitHub Copilot | **Not supported** — lacks subagents |
| OpenCode | **Not supported** — lacks subagents |

**If your tool is not supported**, run the pipeline manually:
1. `/archetipo-plan US-XXX` for each spec
2. `/archetipo-implement US-XXX` for each spec

# ARchetipo Autopilot — Autonomous Pipeline Execution

You are a **Direttore d'Orchestra** (orchestra conductor): you don't play any instrument, you coordinate the performers. For each spec in the backlog, you spawn isolated subagents to execute the pipeline steps (plan → implement), verify status transitions between steps, and move to the next spec. Your context stays lightweight — you never read source code, plans, or PRDs.

---

## Input Parameters

| Parameter | Format | Default | Example |
|---|---|---|---|
| **--epic** | Filter by epic code | all epics | `--epic EP-002` |
| **--priority** | Minimum priority level | all priorities | `--priority HIGH` |
| **--max-specs** | Maximum specs to process | 5 | `--max-specs 10` |
| **--stop-when** | Exit condition in natural language | all matching specs processed | `--stop-when "EP-001 completato"` |
| **--steps** | Pipeline steps to execute | `plan,implement` | `--steps plan` |
| **--on-error** | Error strategy | `ask` | `--on-error skip` |

**Argument parsing:**
- `--steps` accepts a comma-separated list: `plan`, `implement`, or `plan,implement`
- `--on-error` accepts: `ask` (default — prompt user), `skip` (log and continue), `stop` (halt immediately)
- `--priority` filters specs with priority >= the specified level (HIGH > MEDIUM > LOW)

**Invocation examples:**
```
/archetipo-autopilot
/archetipo-autopilot --epic EP-002 --max-specs 3
/archetipo-autopilot --priority HIGH --on-error skip --max-specs 10
/archetipo-autopilot --steps plan --max-specs 20
/archetipo-autopilot --stop-when "tutte le spec di EP-001 sono in REVIEW"
```

---

## Architecture

```
Autopilot Controller (main context, lightweight — never reads codebase)
  │
  ├─ Spec US-001
  │   ├─ Subagent A → /archetipo-plan US-001   → [context destroyed]
  │   └─ Subagent B → /archetipo-implement US-001 → [context destroyed]
  │
  ├─ Spec US-002
  │   ├─ Subagent C → /archetipo-plan US-002   → [context destroyed]
  │   └─ Subagent D → /archetipo-implement US-002 → [context destroyed]
  │
  └─ ...
```

**Context isolation is absolute.** Each subagent:
- Starts with an empty context — no residue from any previous subagent
- Receives ONLY: the command to execute, the working directory, and a 1-2 sentence summary of previous specs
- Reads the project context, config, backlog, and codebase from scratch — exactly as if the user typed the command in a new terminal session
- Terminates completely after execution — its context is destroyed
- Returns only a 1-3 sentence summary to the controller

This means subagent C (plan US-002) knows nothing about what subagent B (implement US-001) did. The controller never accumulates codebase knowledge.

---

## State File

Each autopilot run generates a **unique state file**:

```
.archetipo/autopilot-state-{unix_timestamp}.yaml
```

If the `.archetipo` folder does not exist, create it before writing the state file.

```yaml
autopilot:
  steps: [plan, implement]
  filters:
    epic: null
    priority: null
  max_specs: 5
  exit_condition: null
  on_error: ask
  current_spec_index: 1
  status: running  # running | completed | max_reached | error | stopped
  started_at: "2026-03-29T10:30:00"
  updated_at: "2026-03-29T11:15:30"

queue:
  - code: US-001
    title: "Login utente"
    epic: EP-001
    priority: HIGH
    points: 3
    pipeline:
      plan:
        status: success  # pending | success | error | skipped
        summary: "Piano creato con 8 task (5 impl, 2 test)"
        timestamp: "2026-03-29T10:35:00"
      implement:
        status: success
        summary: "Implementati 8 task, 12 test scritti, code review superata"
        timestamp: "2026-03-29T11:10:00"
    result: completed  # completed | partial | error | skipped | pending

  - code: US-002
    title: "Dashboard principale"
    epic: EP-001
    priority: HIGH
    points: 5
    pipeline:
      plan:
        status: pending
        summary: null
        timestamp: null
      implement:
        status: pending
        summary: null
        timestamp: null
    result: pending
```

The state file has two purposes:
1. **Resilience** — if the session is interrupted, the autopilot can resume from the exact step where it stopped
2. **Summaries for subagents** — each subagent receives only the summary strings from completed specs, never detailed content

---

## Workflow

### PHASE 0 — Initialization

1. Parse user arguments (steps, epic, priority, max-specs, stop-when, on-error)

2. Run `archetipo config show` and parse the stdout JSON envelope; keep `data` (SetupInfo) available.
   Parse stderr as the JSON error envelope and branch on `error.code`.
   This skill uses only these CLI operations directly:
   - `archetipo config show`
   - `archetipo spec list [--status STATUS]`

3. **Cleanup residual state files:** find all `.archetipo/autopilot-state-*.yaml` files with terminal status (`completed`, `max_reached`, `stopped`) and delete them.

4. **Active state detection:** find all `.archetipo/autopilot-state-*.yaml` files with `status: running` or `status: error`.

   **If none found:** proceed normally to step 5.

   **If one found:**
   - Check the `updated_at` field: if it is older than **2 hours**, flag it as a probable orphan:
     ```
     Trovato un autopilot in stato "running", ma l'ultima attività risale a {tempo_fa}.
     Probabilmente la sessione si è interrotta.
     - **Spec processate:** {N}/{total}
     - **Ultima spec:** {US-CODE}
     - **Ultimo step:** {plan/implement}

     Vuoi riprenderlo o scartarlo e avviarne uno nuovo?
     ```
   - If `updated_at` is recent (less than 2 hours), warn the user it may be active elsewhere.
   - If the user wants to **resume**: read the state file, find the first spec with `result: pending` or `result: error`, determine which pipeline step to resume from, and continue from PHASE 1.
   - If the user wants to **discard**: delete the state file and proceed normally.
   - If the user wants to **start an independent run**: proceed normally (new timestamp = new file).

   **If more than one found:** present a list and ask the user how to proceed.

5. **Build the spec queue.** Read the backlog once and select specs.

   Run `archetipo spec list` (no `--status` flag) and parse the JSON envelope to evaluate every item against the pipeline steps.

   **Spec selection rules:**
   - If `--steps` includes `plan`: select specs with `status: TODO`
   - If `--steps` is `implement` only: select specs with `status: PLANNED`
   - Apply `--epic` filter if provided
   - Apply `--priority` filter if provided (include the specified level and above)
   - Sort by: priority (HIGH > MEDIUM > LOW), then by spec number (US-001 before US-002)
   - Take at most `--max-specs` specs

   If no specs match the filters, inform the user and stop:
   ```
   Nessuna spec trovata con i filtri specificati.
   - Stato richiesto: {TODO/PLANNED}
   - Epic: {epic or "tutti"}
   - Priorità minima: {priority or "tutte"}
   ```

6. Generate the current unix timestamp as the autopilot ID and create the initial state file.

7. **Announce the queue:**

```
🎼 **ARchetipo Autopilot** — Avviato

**Spec in coda:** {N}
**Pipeline:** {steps}
**Max spec:** {max-specs}
**Epic:** {epic or "tutti"}
**Priorità minima:** {priority or "tutte"}
**Gestione errori:** {on-error}

| # | Spec | Epic | Priorità | SP |
|---|---|---|---|---|
| 1 | US-XXX: {title} | EP-XXX | HIGH | 3 |
| 2 | US-YYY: {title} | EP-YYY | MEDIUM | 5 |

Avvio pipeline sulla prima spec...
```

---

### PHASE 1 — Spec Pipeline Execution

For each spec in the queue, execute the pipeline steps **sequentially**. Each step is a separate subagent invocation.

#### Step A — Plan

**Condition:** `plan` is in `--steps` AND the spec status is `TODO`.

If the spec is already `PLANNED` (e.g., from a previous partial run), skip this step.

Spawn a subagent with this prompt:

```
## Operational Context

- **Working directory:** {absolute path to project root}
- **Autopilot Mode:** Spec {N} of {total} in an autonomous pipeline run.

### Previous Specs Summary
{summaries from completed specs in state file, or "First spec — no prior context."}

## Task

Execute /archetipo-plan {US-CODE}

## Instructions

1. Read the project context and configuration files if present (for example `.archetipo/config.yaml`, `CLAUDE.md`, `AGENTS.md`, or other agent-instructions files) to understand the project structure and conventions
2. Execute the planning skill for spec {US-CODE}
3. When done, return a concise summary (1-2 sentences) of the plan produced and whether it succeeded
```

**After the subagent returns:**

Trust the subagent's result — do not re-read the backlog. The plan skill already verifies the status transition internally.

1. If the subagent returns successfully (no error):
   - Record `plan.status: success` and the summary in the state file
   - Update `updated_at`
   - **Mockup artifact verification:** If the plan summary mentions UI work, mockups, or design:
     1. Check if `{config.paths.mockups}/{US-CODE}/` contains at least one file (use `ls` or glob)
     2. If mockup files are found: record `mockup_verified: true` in the state file and proceed
     3. If NO mockup files are found:
        - Spawn a dedicated mockup subagent: execute `/archetipo-design {US-CODE}` with the spec title and plan summary as context
        - Wait for completion (do NOT run in background)
        - Verify files exist after completion
        - If still no files: log `mockup_missing: true` in the state file and proceed anyway (do not block the pipeline)
   - Proceed to Step B
2. If the subagent returns an error or failure:
   - Record `plan.status: error` with the subagent's return as error detail
   - Apply error strategy (see Error Handling section)

#### Step B — Implement

**Condition:** `implement` is in `--steps` AND the spec status is `PLANNED`.

Spawn a subagent with this prompt:

```
## Operational Context

- **Working directory:** {absolute path to project root}
- **Autopilot Mode:** Spec {N} of {total} in an autonomous pipeline run.

### Previous Specs Summary
{summaries from completed specs in state file}

## Task

Execute /archetipo-implement {US-CODE}

## Instructions

1. Read the project context and configuration files if present (for example `.archetipo/config.yaml`, `CLAUDE.md`, `AGENTS.md`, or other agent-instructions files) to understand the project structure and conventions
2. Execute the implementation skill for spec {US-CODE}
3. When done, return a concise summary (2-3 sentences) of what was implemented, tests written, and code review result
```

**After the subagent returns:**

Trust the subagent's result — do not re-read the backlog. The implement skill already verifies status transitions internally.

1. If the subagent returns successfully (no error):
   - Record `implement.status: success` and the summary in the state file
   - Mark spec `result: completed`
   - Update `updated_at`
   - **E2E verification:** If the spec involves UI work and the implement summary does NOT mention e2e tests:
     1. Log `e2e_missing: true` in the spec's state entry
     2. Include a warning in the spec progress update: `⚠️ E2E test non scritti per questa spec`
     3. Do NOT block the pipeline — proceed to the next spec
2. If the subagent returns an error or failure:
   - Record `implement.status: error`
   - Mark spec `result: partial` (plan succeeded but implement failed)
   - Apply error strategy

#### Between specs

After completing a spec's pipeline, output a brief progress update:

```
### US-XXX completata ({N}/{total})
- **Plan:** {plan summary}
- **Implement:** {implement summary}

Prossima: US-YYY — {title}
```

Then proceed to PHASE 2 before starting the next spec.

---

### PHASE 2 — Exit Condition Evaluation

After each spec pipeline completes, run these checks in order:

**Check A — Exit condition met:**
If `--stop-when` was specified, verify the condition. This requires re-reading the backlog to check current statuses.

Re-run `archetipo spec list` to get current statuses.

Evaluate the `--stop-when` condition against the current state (e.g., "EP-001 completato" → check if all EP-001 specs are in REVIEW or DONE).

If the condition is met → set `status: completed` → go to PHASE 3.

**Check B — Queue exhausted:**
If all specs in the queue have been processed → set `status: completed` → go to PHASE 3.

**Check C — Max specs reached:**
If the number of processed specs equals `--max-specs` → set `status: max_reached` → go to PHASE 3.

**If no exit condition is satisfied** → return to PHASE 1 with the next spec.

---

### PHASE 3 — Closure

1. Update the state file with the final status.

2. Present the final summary:

```
## Autopilot {completed/max_reached/error/stopped}

{closing message — see below}

### Riepilogo spec

| # | Spec | Plan | Implement | Risultato |
|---|---|---|---|---|
| 1 | US-XXX: {title} | ✅ | ✅ | completata |
| 2 | US-YYY: {title} | ✅ | ❌ | parziale |
| 3 | US-ZZZ: {title} | ⏭️ | ⏭️ | saltata |

**Spec completate:** {N}/{total}
**Spec con errori:** {N}
**Spec saltate:** {N}
```

**Result icons:**
- ✅ success
- ❌ error
- ⏭️ skipped
- ⏸️ pending (not reached)

**Closing messages by status:**

- `completed`: *"Tutte le spec in coda sono state processate."* — or if stop-when was used: *"La condizione di uscita è stata raggiunta: \"{stop-when}\""*
- `max_reached`: Include the suggestion to continue with a realistic estimate:
  ```
  Raggiunte {max-specs} spec senza soddisfare la condizione di uscita: "{stop-when}".

  **Per proseguire**, riesegui:
  /archetipo-autopilot {original filters} --max-specs {suggested value} --stop-when "{stop-when}"
  ```
  The suggested value should be based on remaining work — if 7 specs remain, suggest `--max-specs 7`.
- `error`: *"L'autopilot è stato interrotto a causa di un errore sulla spec {US-CODE}."*
- `stopped`: *"L'autopilot è stato fermato dall'utente alla spec {US-CODE}."*

3. **Delete the state file.** The summary has been communicated and there is no need to keep it.

---

## Error Handling

When a pipeline step fails (plan or implement), the behavior depends on `--on-error`:

### `ask` (default)

Prompt the user:
```
⚠️ **Errore sulla spec {US-CODE}** — Step: {plan|implement}

{error summary from subagent}

Come vuoi procedere?
- **Riprova** — riesegui lo step {plan|implement} su {US-CODE}
- **Salta** — passa alla prossima spec
- **Ferma** — termina l'autopilot
```

Record the user's choice in the state file.

### `skip`

Log the error, mark the spec's result as `error` (or `partial` if plan succeeded), and proceed to the next spec. No user interaction.

### `stop`

Log the error, set `status: error`, and go to PHASE 3 (Closure). No user interaction.

### Partial failure

If plan succeeds but implement fails, the spec is marked as `partial`. On resumption, the controller sees the spec is already PLANNED and only retries the implement step — it does not re-run plan.

---

## Resumption Logic

When the controller finds an existing `running` or `error` state file and the user chooses to resume:

1. Load the state file (queue, summaries, pipeline statuses)
2. Find the first spec with `result: pending` or `result: error`
3. Determine which pipeline step to resume from:
   - `plan.status: pending` → start from plan
   - `plan.status: error` → retry plan
   - `plan.status: success` + `implement.status: pending` → start from implement
   - `plan.status: success` + `implement.status: error` → retry implement
4. Continue the normal loop from that spec onward

Specs with `result: completed` or `result: skipped` are never re-processed.

---

## Requirements

This skill requires an AI coding tool that supports these capabilities:
- Isolated subagents or worker contexts
- Passing a working directory and a short task prompt to each subagent
- Independent execution of sequential pipeline steps without shared residual context
- Reading project context, config, backlog, and repository files from the subagent context itself

Any agentic IDE or CLI that provides these capabilities is compatible; the skill logic must remain capability-based rather than vendor-specific.
