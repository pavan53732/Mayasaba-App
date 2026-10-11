# Mayasaba

**One project reality. Three separate agent sessions.**

Mayasaba is a native Windows desktop **control plane** for AI coding agents. You point it at a folder on your own PC and state a request. Three externally installed coding CLIs — **Hermes Agent CLI**, **Kilo Code CLI**, and **Claude Code CLI** — deliberate and carry the work out in isolated workspaces, and Mayasaba declares the result done **only after it has verified the work itself, with evidence**.

The agents supply the intelligence. Mayasaba supplies **state, authority, safety, orchestration, verification, and proof**.

> Mayasaba is not an AI model, a provider, a cloud IDE, a hosted service, a browser application, an agent marketplace, or a terminal wrapper.

---

## Contents

- [Why it exists](#why-it-exists)
- [What it is — and what it refuses to be](#what-it-is--and-what-it-refuses-to-be)
- [The central invariant](#the-central-invariant)
- [The three agents](#the-three-agents)
- [How work flows](#how-work-flows)
- [Authority and truth: two questions](#authority-and-truth-two-questions)
- [The FULL council](#the-full-council)
- [Who owns what](#who-owns-what)
- [Verification: evidence decides "done"](#verification-evidence-decides-done)
- [User experience: Chat is the only surface](#user-experience-chat-is-the-only-surface)
- [Design principles](#design-principles)
- [Documentation](#documentation)
- [Working rules for contributors and agents](#working-rules-for-contributors-and-agents)

---

## Why it exists

Today, an AI coding agent's word is usually taken as truth: it says "done," and the task is treated as done. That is unsafe and unverifiable. A clean process exit, a successful build, an agent's confident summary, or even three agents agreeing — none of these is proof that the requested behaviour actually works.

Mayasaba exists to fix that. It is a **deterministic controller** that sits between a user and multiple agents, and it keeps separate four things that are normally blurred together:

| Concept | Who answers it |
| --- | --- |
| What the user **authorized** | User instructions, policy, approved requirements, binding decisions |
| What the controller **decided and scheduled** | The Orchestrator and the owning domain services |
| What an agent **proposed or reported** | The agent's messages — advisory, never authority |
| What the machine **actually did / what evidence proves** | Independently observed, reproducible facts |

A verified fact does not grant permission. A permitted action does not make a technical claim true. **Completion is controller-certified from evidence — never from an agent's claim.**

## What it is — and what it refuses to be

Mayasaba is a fully native, Windows-only desktop application with a deterministic local core. It has **no hosted component and no separate application account or login.**

**Hard boundaries:**

- **Windows only, fully local.** All controlled execution happens on the user's own PC. There is no hosted machine, remote executor, or cloud runner.
- **The opened folder is the boundary.** The folder the user opens is the sole canonical project root and the final publication destination. Mayasaba never scans the whole PC and never infers access to sibling, parent, or neighboring folders.
- **No unrelated side effects.** It does not send emails, publish content, submit forms, make purchases, change accounts, or control unrelated applications. Read-only web research happens only through each CLI's own user-configured tools, under a scoped assignment.
- **No hidden model management.** The model, provider, and authentication of each CLI belong to the user; Mayasaba never chooses, changes, or overrides them.

**It refuses to be** an AI model or inference provider, a cloud IDE, a chat wrapper, an agent marketplace, or a system where an agent has the final word. **An agent saying "done" never completes a task.**

Mayasaba supports software engineering **and** other authorized local-file work — documents, research reports, data cleanup, and refactors. Lifecycle stages and acceptance checks are **task-applicable**: a report is validated by its own criteria (structure, source and citation validity, record counts, integrity) and is never forced through a build gate that does not apply to it.

## The central invariant

> **One controller-owned project reality, three separate agent sessions.**

Mayasaba holds the facts: requirements, decisions, task ownership, context versions, workspace scope, evidence, and validation state. The agents never share a hidden brain, and **nothing an agent claims becomes true until Mayasaba confirms it.**

## The three agents

Mayasaba coordinates **exactly three** externally installed coding CLIs, and no others: **Hermes Agent CLI**, **Kilo Code CLI**, and **Claude Code CLI**. It does not replace them or add a model of its own.

- **The user owns each CLI's model, provider, authentication, tools, and updates.** Mayasaba always uses the CLI's existing selection and never chooses, changes, or overrides a model or provider. There is no model picker.
- **Compatibility is behavioural.** It is established by bounded behavioural and capability probes, never by release-number comparison or a version allow-list.
- **All three** participate in every FULL council decision point, in whole-repository exploration, and in three-agent research assignments.
- **Individual implementation and repair tasks** are leased to a specific agent — only the leased CLI is launched for that work.
- **A missing CLI blocks only the operations that require it** — never Chat, folder selection, history, drafts, or recovery. An all-three workflow is never silently reduced to two agents.

## How work flows

A software project passes through a lifecycle; other local-file tasks run only the stages their acceptance criteria require.

```text
PROJECT_CREATED → DISCOVERY → INDEPENDENT_ANALYSIS → PROPOSALS
→ CROSS_CRITIQUE → REBUTTAL_AND_REVISION → DISAGREEMENT_RESOLUTION
→ USER_INTERVIEW → PRODUCT_AND_UX_DESIGN → TECH_STACK_DEBATE
→ ARCHITECTURE_REVIEW → ARCHITECTURE_LOCKED → TASK_PLANNING
→ IMPLEMENTATION → INTEGRATION → BUILD → TEST → E2E
→ CROSS_AGENT_REVIEW → REPAIR (when needed) → FINAL_VALIDATION → PACKAGE → COMPLETE
```

`PAUSED`, `STOPPED`, `BLOCKED`, and `RECOVERING` are **orthogonal operational conditions**, not phases. User interruption has the highest operational priority.

**From a Chat request to files in the opened folder:**

1. The user opens a Windows folder and describes a task in Chat.
2. Mayasaba records an immutable `UserContribution` and validates a scoped work request for that operation.
3. Registered decisions go through a FULL council; approved work becomes a task graph with leases, allowed paths, and acceptance criteria.
4. Agents write inside isolated, controller-authorized workspaces — **never concurrently into the user's root**.
5. The controller verifies lease, scope, baseline hashes, and change provenance; stale, out-of-scope, or conflicting writes are rejected before the side effect.
6. Accepted changes are staged, validated against the applicable oracles, then published through a **durable, recoverable integration journal** that never blindly overwrites newer user edits.
7. Completion is reported through Chat cards **only** when all applicable blocking criteria carry valid, current evidence.

**Response versus implementation.** An agent's text reply is not a file change and not proof of completion. Building successfully, launching a process successfully, and behaving correctly at runtime are three different results — only the last is runtime correctness.

## Authority and truth: two questions

Two questions must never be conflated:

1. **What may Mayasaba do?** — answered by user instructions, policy, approved requirements, binding decisions, and valid task/lease commands.
2. **What is actually true?** — answered by current, independently checked, reproducible evidence about the world, the project, and running artifacts.

**Nothing that is not authority can create authority:** not an agent's text, a classifier's output, a confidence score, a majority agreement, a transport acknowledgement, or a process exit code. **Unknown or unverifiable state fails closed** and cannot satisfy a positive gate. A newer credible observation may supersede a stale factual claim, but it never grants permission by itself.

## The FULL council

**FULL is the only council mode.** Every registered decision point requires all three agents:

- independent proposals from all three, before any agent sees another's;
- a frozen proposal set;
- **six directed cross-critiques** — each agent critiques both peers, never itself;
- rebuttal and optional revision with immutable predecessor links;
- computed evidence grading with material disagreement preserved;
- a deterministic round-robin chair, and non-chair review of any synthesis;
- a default **five-round cap** for the entire decision point.

The externally reported outcomes are **`converged`**, **`synthesized`**, **`cap reached`**, **`escalated`**, and **`sealed with an open question`**. `CONTINUE` is an internal transition, not a positive outcome.

**Silence is never agreement.** An ACK, non-participation, a majority, or an agent's claim of agreement is never approval. Positive convergence requires two comparable completed FULL rounds with identical surviving positions, unless an evidence-backed, fully reviewed synthesis resolves the point earlier.

## Who owns what

Mayasaba is organized into **thirteen functional layers** with strict dependency direction — lower layers never depend on higher ones — and **exactly one owner per authoritative record**, so coordination never implies co-ownership.

A few examples of that rule: the Project service owns project identity and the project epoch; the Council Engine owns deliberation but **cannot bind a decision**; the Orchestrator schedules phases and barriers but never mutates another owner's records; the storage layer persists state but **does not originate authority**; the UI renders projections but never talks to an agent directly.

State is **never one giant status field.** Twelve independent state machines govern project lifecycle, agent session, message delivery, context, task, lease, council round, synchronization barrier, handoff, execution, validation, and repair. Readiness labels, request routing, exploration, research, and decision-validity status are projections over these machines — never additional state machines.

## Verification: evidence decides "done"

Every task and deliverable carries an acceptance-evidence matrix. Each criterion ends in exactly one verdict:

| Verdict | Meaning |
| --- | --- |
| `PASS` | The correct current observation was established and preserved |
| `FAIL` | A valid oracle observed the expectation was not met |
| `INCONCLUSIVE` | Evidence or oracle adequacy was insufficient |
| `BLOCKED` | The check could not run safely or with required authority |
| `NOT_APPLICABLE` | Verified justification shows the criterion does not apply |

**Completion is a matrix gate.** All blocking criteria must be `PASS` or validly `NOT_APPLICABLE`; no blocking `FAIL`, `INCONCLUSIVE`, or `BLOCKED` can be hidden by an aggregate score. Before any `PASS`, an oracle-adequacy review confirms the check actually measures the named requirement — a compile check cannot prove runtime behaviour, and an agent-authored passing test cannot prove its own relevance.

Evidence carries immutable provenance and content hashes. **A bare file path, an agent narrative, a clean exit, or a successful build is never proof by itself.** History is append-only: records are superseded explicitly, never rewritten to simplify the current view.

## User experience: Chat is the only surface

- **Every launch opens native Chat immediately, independent of CLI readiness.**
- Chat is the **only persistent route** — no project-navigation rail, tabs, dashboard, or standalone Council, Requirements, Decisions, Tasks, Files, Validation, Delivery, Settings, or Agents page.
- **Open Folder** sits at the lower-left of the Chat shell and is distinct from **Attach files**.
- Send is disabled **only** until an authorized project root is bound. After binding, every Send persists exactly one immutable `UserContribution` — even if no CLI is available.
- The first and later messages use the **same composer** and the same submission path. There is no goal-intake wizard and no special first-message mode.
- Opening a folder creates or restores project identity. It **does not** launch agents, scan the repository, initialize Git, approve work, or consume provider usage.
- The normal UI shows **only missing or unusable CLI warnings** (`MISSING`, `UNSUPPORTED`, `PROBE_FAILED`). Healthy `READY` agents do not occupy warning rows.
- Council, requirements, decisions, tasks, files/evidence, validation/repair, delivery, and diagnostics run in the **background** and surface as concise **Chat timeline cards** with an explicit **Show details** action — inline expansion, or a temporary dismissible sheet that restores Chat focus and reading position.
- An agent's private chain of thought, credentials, and authentication tokens are **never shown**.
- Keyboard navigation, visible focus, screen-reader semantics, high contrast, DPI scaling, and reduced-motion behaviour are first-class requirements.

## Design principles

- **Deterministic authority.** The controller, not a model, owns state and decisions.
- **Fail closed.** Unknown, stale, or inconclusive state cannot pass a positive gate.
- **Evidence over assertion.** Agreement is not verification; a green check is not certification.
- **One owner per record.** Coordination never implies co-ownership.
- **Least privilege.** Default-deny capabilities; block an operation when required containment cannot be proven.
- **Isolation by default.** Agents work in isolated workspaces; only the controller publishes to the user's root, through a recoverable journal.
- **Honest reporting.** Say `unknown`, `partial`, `stale`, `inconclusive`, or `blocked` when that is the observed state.
- **Append-only history.** Supersede records explicitly; never rewrite history to simplify the current view.

## Documentation

| Document | Role |
| --- | --- |
| Mayasaba — Complete System Description (for AI agents).md | **Authoritative product and architecture specification.** If any other document conflicts with it, this one wins. |
| AGENTS.md | Repository-wide operating rules for any coding agent working on Mayasaba. |
| ARCHITECTURE.md | Implementation-facing architectural reference derived from the specification. |
| UI-UX.md | Implementation-facing UI/UX, accessibility, and desktop-acceptance reference. |
| README.md | This file — product orientation and entry point. |

## Working rules for contributors and agents

- **Build and validate on Windows**, and keep verification **local** — there is no hosted CI or cloud execution.
- **Keep the deterministic core independent of the interface** so it can be tested without creating a window.
- **Do not** introduce a fourth agent, a second orchestrator, an alternate state owner, a direct UI-to-agent channel, or a thirteenth state machine.
- **Every task** requires an acceptance-evidence matrix (or its implementation-stage equivalent); each criterion ends in exactly one of `PASS`, `FAIL`, `INCONCLUSIVE`, `BLOCKED`, `NOT_APPLICABLE`.
- **Classify failures before repair** and give every repair attempt a falsifiable hypothesis; never repeat an identical failing strategy without new evidence.
- **Do not** commit, push, publish, or alter external accounts unless explicitly requested.

For the full rule set, read `AGENTS.md` before making changes.

---

**Mayasaba's success condition:** authority, observed execution, and current evidence remain **distinguishable from user input all the way through to final certification.**
