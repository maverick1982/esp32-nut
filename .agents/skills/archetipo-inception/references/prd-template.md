# PRD Template

When the inception flow has gathered the minimum required information, generate the PRD using exactly this structure and save it to `{config.paths.prd}`.

> **Language:** The template below is an English scaffold. Before writing the file, translate every static element (headings, table headers, bold labels, connective phrases like "For **X**, who has the problem of **Y**...") into the detected language, per the **Template Rendering Rule** in `.archetipo/shared-runtime.md`. Keep `{{PLACEHOLDER}}` tokens unchanged.

> **📌 Marker contract:** The HTML comments `<!-- archetipo:prd section=<id> required=true -->` below are stable, machine-readable markers used by `archetipo validate prd`. **Do not translate, modify, or remove them.** They are part of the validation contract and must appear verbatim in every generated PRD.

```markdown
# {{PROJECT_NAME}} - Product Requirements Document

**Author:** ARchetipo
**Date:** {{DATE}}
**Version:** 1.0

---

## Elevator Pitch

<!-- archetipo:prd section=elevator_pitch required=true -->

> {{ELEVATOR_PITCH}}
>
> For **{{TARGET_SEGMENT}}**, who has the problem of **{{PROBLEM}}**, **{{PRODUCT_NAME}}** is a **{{CATEGORY}}** that **{{KEY_BENEFIT}}**. Unlike **{{MAIN_ALTERNATIVE}}**, our product **{{DIFFERENTIATOR}}**.

---

## Vision

<!-- archetipo:prd section=vision required=true -->

{{VISION_STATEMENT}}

### Product Differentiator

{{PRODUCT_DIFFERENTIATOR}}

---

## User Personas

<!-- archetipo:prd section=user_personas required=true -->

### Persona 1: {{PERSONA_1_NAME}}

**Role:** {{ROLE_1}}
**Age:** {{AGE_1}} | **Background:** {{BACKGROUND_1}}

**Goals:**
{{PERSONA_1_GOALS}}

**Pain Points:**
{{PERSONA_1_PAIN_POINTS}}

**Behaviors & Tools:**
{{PERSONA_1_BEHAVIORS}}

**Motivations:** {{PERSONA_1_MOTIVATIONS}}
**Tech Savviness:** {{TECH_SAVVINESS_1}}

#### Customer Journey - {{PERSONA_1_NAME}}

| Phase | Action | Thought | Emotion | Opportunity |
|---|---|---|---|---|
| Awareness | {{AWARENESS_1}} | {{AWARENESS_THOUGHT_1}} | {{AWARENESS_EMOTION_1}} | {{AWARENESS_OPPORTUNITY_1}} |
| Consideration | {{CONSIDERATION_1}} | {{CONSIDERATION_THOUGHT_1}} | {{CONSIDERATION_EMOTION_1}} | {{CONSIDERATION_OPPORTUNITY_1}} |
| First Use | {{FIRST_USE_1}} | {{FIRST_USE_THOUGHT_1}} | {{FIRST_USE_EMOTION_1}} | {{FIRST_USE_OPPORTUNITY_1}} |
| Regular Use | {{REGULAR_USE_1}} | {{REGULAR_USE_THOUGHT_1}} | {{REGULAR_USE_EMOTION_1}} | {{REGULAR_USE_OPPORTUNITY_1}} |
| Advocacy | {{ADVOCACY_1}} | {{ADVOCACY_THOUGHT_1}} | {{ADVOCACY_EMOTION_1}} | {{ADVOCACY_OPPORTUNITY_1}} |

---

### Persona 2: {{PERSONA_2_NAME}}

**Role:** {{ROLE_2}}
**Age:** {{AGE_2}} | **Background:** {{BACKGROUND_2}}

**Goals:**
{{PERSONA_2_GOALS}}

**Pain Points:**
{{PERSONA_2_PAIN_POINTS}}

**Behaviors & Tools:**
{{PERSONA_2_BEHAVIORS}}

**Motivations:** {{PERSONA_2_MOTIVATIONS}}
**Tech Savviness:** {{TECH_SAVVINESS_2}}

#### Customer Journey - {{PERSONA_2_NAME}}

| Phase | Action | Thought | Emotion | Opportunity |
|---|---|---|---|---|
| Awareness | {{AWARENESS_2}} | {{AWARENESS_THOUGHT_2}} | {{AWARENESS_EMOTION_2}} | {{AWARENESS_OPPORTUNITY_2}} |
| Consideration | {{CONSIDERATION_2}} | {{CONSIDERATION_THOUGHT_2}} | {{CONSIDERATION_EMOTION_2}} | {{CONSIDERATION_OPPORTUNITY_2}} |
| First Use | {{FIRST_USE_2}} | {{FIRST_USE_THOUGHT_2}} | {{FIRST_USE_EMOTION_2}} | {{FIRST_USE_OPPORTUNITY_2}} |
| Regular Use | {{REGULAR_USE_2}} | {{REGULAR_USE_THOUGHT_2}} | {{REGULAR_USE_EMOTION_2}} | {{REGULAR_USE_OPPORTUNITY_2}} |
| Advocacy | {{ADVOCACY_2}} | {{ADVOCACY_THOUGHT_2}} | {{ADVOCACY_EMOTION_2}} | {{ADVOCACY_OPPORTUNITY_2}} |

---

## Brainstorming Insights

<!-- archetipo:prd section=brainstorming_insights required=true -->

> Key discoveries and alternative directions explored during the inception session.

### Assumptions Challenged

{{ASSUMPTIONS_CHALLENGED}}

### New Directions Discovered

{{NEW_DIRECTIONS_DISCOVERED}}

### Assumptions to Validate

{{ASSUMPTIONS_TO_VALIDATE}}

### Key Risks

{{KEY_RISKS}}

---

## Product Scope

<!-- archetipo:prd section=product_scope required=true -->

### MVP - Minimum Viable Product

{{MVP_SCOPE}}

### Growth Features (Post-MVP)

{{GROWTH_FEATURES}}

### Vision (Future)

{{VISION_FEATURES}}

---

## Technical Architecture

<!-- archetipo:prd section=technical_architecture required=true -->

> **Proposed by:** Leonardo (Architect)

### System Architecture

{{HIGH_LEVEL_ARCHITECTURE}}

**Architectural Pattern:** {{ARCHITECTURE_PATTERN}}

**Main Components:**
{{ARCHITECTURE_COMPONENTS}}

### Technology Stack

| Layer | Technology | Version | Rationale |
|---|---|---|---|
| Language | {{LANGUAGE}} | {{LANGUAGE_VERSION}} | {{LANGUAGE_RATIONALE}} |
| Backend Framework | {{BACKEND_FRAMEWORK}} | {{BACKEND_VERSION}} | {{BACKEND_RATIONALE}} |
| Frontend Framework | {{FRONTEND_FRAMEWORK}} | {{FRONTEND_VERSION}} | {{FRONTEND_RATIONALE}} |
| Database | {{DATABASE}} | {{DB_VERSION}} | {{DB_RATIONALE}} |
| ORM | {{ORM}} | {{ORM_VERSION}} | |
| Auth | {{AUTH_LIB}} | | |
| Testing | {{TESTING_FRAMEWORK}} | | |

### Project Structure

**Organizational pattern:** {{CODE_ORGANIZATION_PATTERN}}

```text
{{DIRECTORY_LAYOUT}}
```

### Development Environment

{{DEVELOPMENT_ENVIRONMENT}}

**Required tools:** {{REQUIRED_DEV_TOOLS}}

### CI/CD & Deployment

**Build tool:** {{BUILD_TOOL}}

**Pipeline:** {{BUILD_PIPELINE}}

**Deployment:** {{DEPLOYMENT_STRATEGY}}

**Target infrastructure:** {{TARGET_INFRASTRUCTURE}}

### Architecture Decision Records (ADR)

{{ARCHITECTURE_DECISIONS}}

---

## Functional Requirements

<!-- archetipo:prd section=functional_requirements required=true -->

{{FUNCTIONAL_REQUIREMENTS}}

---

## Non-Functional Requirements

<!-- archetipo:prd section=non_functional_requirements required=true -->

### Security

{{SECURITY_REQUIREMENTS}}

### Integrations

{{INTEGRATION_REQUIREMENTS}}

---

## Next Steps

<!-- archetipo:prd section=next_steps required=true -->

1. **Backlog** - Run `/archetipo-spec` to transform this PRD into a backlog
2. **Design** - Run `/archetipo-design` for UI mockups (when applicable)
3. **Validation** - Review with stakeholders and test the riskiest assumptions

---

_PRD generated via ARchetipo Product Inception - {{DATE}}_
_Session conducted by: {{USER_NAME}} with the ARchetipo team_
```
