# ARchetipo Implement — Output Templates

> **Language rule:** All structural labels, headers, and user-facing messages in these templates must be rendered in the detected project language (see Language Policy in `.archetipo/shared-runtime.md`).  
> Exception: agent names and role identifiers (Ugo, Mina, Cesare) are part of the ARchetipo brand voice and remain unchanged across languages.

---

## Error Messages (Phase 0)

### No backlog

```text
❌ **Ugo:** No backlog found. A backlog is required to know what to implement.
   Run `/archetipo-spec` to create one, then `/archetipo-plan` to plan the first spec.
```

### No planned specs

```text
❌ **Ugo:** No specs in status {config.workflow.statuses.planned} found in the backlog.
   Run `/archetipo-plan` to plan a spec, or pass a different spec as argument.
```

### No implementation plan

```text
❌ **Ugo:** No implementation plan found for this spec.
   The spec has not been planned yet. Run first:
   `/archetipo-plan {US-CODE}`
```

---

## Session Announcement (Phase 0, Step 10)

```text
⚡ ARCHETIPO — SPEC IMPLEMENTATION

The delivery team is ready.

**Team:**
🔧 Ugo — Full-Stack Developer
🧪 Mina — Test Architect
🔍 Cesare — Code Reviewer

**Spec:** {US-CODE}: {title}
**Epic:** {EP-CODE} | **Priority:** {PRIORITY} | **Points:** {N}
**Tasks to complete:** {N}

Starting implementation...
```

---

## Wave Execution Plan (Phase 1, Step 6)

```text
🔧 **Ugo:** I've analyzed the tasks from the plan. Here is how we will execute them:

**Execution context:** Worker-backed preferred | In-context fallback

**Wave 1 — Sequential workers**
- 🔧 Ugo: TASK-01 [title or body excerpt]
- 🧪 Mina: TASK-02 [title or body excerpt]

**Reason for sequential scheduling:** [dependencies | shared files | unstable interfaces]

**Wave 2 — Concurrent workers**
- 🔧 Ugo: TASK-03 [title or body excerpt]
- 🧪 Mina: TASK-04 [title or body excerpt]

**Fallback to current context:** [only if workers are unavailable or unreliable]

Proceeding.
```

---

## Wave Completion Report (Phase 2)

```text
✅ **Wave N complete**

**Completed:**
- TASK-01: [title] ✅
- TASK-02: [title] ✅

**Next wave:** [N+1]
```

---

## Code Review Output (Phase 3)

**Review criteria (English labels):**

1. plan adherence
2. code quality
3. architecture adherence
4. security
5. test quality
6. mockup adherence when UI work exists
7. completeness vs. tasks and acceptance criteria

**Output format:**

```text
🔍 **Cesare:** Code review complete.

**Summary:** [N] issues found ([N] critical, [N] improvements)

**🔴 CRITICAL — [Title]**
**File:** `path/to/file.ts:NN`
**Problem:** [description]
**Why it matters:** [rationale]
**Suggested fix:** [fix]

**🟡 IMPROVEMENT — [Title]**
**File:** `path/to/file.ts:NN`
**Problem:** [description]
**Suggestion:** [improvement]

**✅ Positive notes:**
- [positive observation]
```

**Severity labels:**

- `🔴 CRITICAL` — must fix before completion
- `🟡 IMPROVEMENT` — should fix, but may be skipped with user approval

---

## Completion Summary (Phase 5)

```text
✅ Implementation complete!

**Spec:** {US-CODE}: {title}
**Status:** {config.workflow.statuses.review}

**Implementation summary:**
- Tasks completed: {N}/{N}
- Tests written/executed: {N}
- Code review: passed ✅
- Review cycles: {N}

**Files created/modified:**
- `path/to/new-file.ts`
- `path/to/modified-file.ts`
- `path/to/test-file.test.ts`

**Optional improvements left open:**
- [Improvement title] — `path/to/file.ts:NN` — [brief suggestion]

⚠️ The spec is in Review. Run `/archetipo-review {US-CODE}` to accept it or send it back with feedback.
```
