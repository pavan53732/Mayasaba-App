# Mayasaba

**One project reality. Three separate agent sessions.**

Mayasaba is a fully native, Windows-only desktop **control plane** for AI coding agents. You point it at a folder on your own PC and state a request. Three externally installed coding CLIs — **Hermes Agent CLI**, **Kilo Code CLI**, and **Claude Code CLI** — deliberate and carry the work out in isolated workspaces, and Mayasaba declares the result done **only after it has verified the work itself, with evidence**.

The agents supply the intelligence. Mayasaba supplies **state, authority, safety, orchestration, verification, and proof**.

> Mayasaba is not an AI model, a provider, a cloud IDE, a hosted service, a browser app, an agent marketplace, or a terminal wrapper.

---

## Contents

- [Why it exists](#why-it-exists)
- [What it is — and what it refuses to be](#what-it-is--and-what-it-refuses-to-be)
- [The central invariant](#the-central-invariant)
- [The three agents](#the-three-agents)
- [How work flows](#how-work-flows)
- [Architecture at a glance](#architecture-at-a-glance)
- [Authoritative state: twelve state machines](#authoritative-state-twelve-state-machines)
- [The two questions: authority vs. truth](#the-two-questions-authority-vs-truth)
- [The FULL council](#the-full-council)
- [User experience: Chat is the only surface](#user-experience-chat-is-the-only-surface)
- [Technology stack](#technology-stack)
- [Repository contents](#repository-contents)
- [Project status](#project-status)
- [Design principles](#design-principles)
- [Working rules for contributors and agents](#working-rules-for-contributors-and-agents)

---

## Why it exists

Today, an AI coding agent's word is usually taken as truth: it says "done," and the task is treated as done. That is unsafe and unverifiable. A clean process exit, a successful build, an agent's confident summary, or three agents agreeing — none of these is proof that the requested behaviour actually works.

Mayasaba exists to fix that. It is a **deterministic controller** that sits between a user and multiple agents, and it enforces a strict separation between four things that are normally blurred together:

| Concept | Who answers it |
| --- | --- |
| What the user **authorized** | User instructions, policy, approved requirements, binding decisions |
| What the controller **decided and scheduled** | The Orchestrator and the owning domain services |
| What an agent **proposed or reported** | The agent's messages — advisory, never authority |
| What the machine **actually did / what evidence proves** | Independently observed, reproducible facts |

A verified fact does not grant permission. A permitted action does not make a technical claim true. **Completion is controller-certified from evidence — never from an agent's claim.**

## What it is — and what it refuses to be

Mayasaba is a **native C++20 Windows desktop application** with a **WinUI 3 / XAML / C++/WinRT** interface and a deterministic local core. It has **no hosted component and no separate application account or login.**

**Hard boundaries:**

- **Windows only.** All controlled execution happens on the user's own PC. There is no hosted machine, remote executor, or cloud runner.
- **The project root is the boundary.** The folder the user opens is the sole canonical project root and final publication destination. Mayasaba never scans the whole PC and never infers access to sibling or parent folders.
- **No unrelated side effects.** It does not send emails, publish content, submit forms, make purchases, change accounts, or control unrelated applications. Read-only web research is allowed only through each CLI's own configured tools, under a scoped assignment.
- **Installs from an MSI.** SQLite exclusively holds authoritative application state; large artifact/evidence bytes live in access-controlled local storage, verified against database-stored digests.
- **It is not** an AI model or inference provider, a cloud IDE, a chat wrapper, an agent marketplace, or a system where an agent has the final word. **An agent saying "done" never completes a task.**

Mayasaba supports software engineering **and** other authorized local-file work — documents, research reports, data cleanup, and refactors. Lifecycle stages and acceptance oracles are **task-applicable**: a report does not require a software build gate.

## The central invariant

> **One controller-owned project reality, three separate agent sessions.**

Mayasaba holds the facts: requirements, decisions, task ownership, context versions, workspace scope, evidence, and validation state. The agents never share a hidden brain, and **nothing an agent claims becomes true until Mayasaba confirms it.**

## The three agents

Mayasaba coordinates **exactly three** externally installed coding CLIs, and no others:

| Agent | Transport | Notable controls |
| --- | --- | --- |
| **Hermes Agent CLI** | Streamed JSON over standard I/O | Suppresses outside-workspace rule/memory/skill injection; forbids approval-bypass, upgrades, outbound messaging, credential export, and service control |
| **Kilo Code CLI** | Native JSON events (local ACP only when verified) | Absolute working directory; private controller-owned session; default-deny permissions; no cloud/public sessions, plugin/import surfaces, or codebase indexing |
| **Claude Code CLI** | Headless print mode (`-p`) with native JSON events | Establishes the installed CLI's supported JSON mode via behavioural probe; forbids `--dangerously-skip-permissions`, background listeners, plugin installation, and credential export |

**Mayasaba is model-agnostic and does not manage model selection.** The user configures the model, provider, and authentication **separately inside each CLI**; Mayasaba always uses that CLI's existing selection and **never chooses, changes, or overrides a model or provider**. There is no model picker in Mayasaba. Compatibility is established by **bounded behavioural and capability probes, never by release-number comparison.**

- All three participate in every **FULL council** decision point.
- All three participate in whole-repository exploration and three-agent research.
- Individual implementation/repair tasks are leased to a specific agent (or agents).
- A missing CLI blocks **only** the operations that require it — never Chat, folder selection, history, drafts, or recovery.

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
2. Mayasaba records an immutable `UserContribution` and validates a scoped `WorkRequest`.
3. Registered decisions go through a FULL council; approved work becomes a task graph with leases.
4. Agents write inside isolated, controller-authorized workspaces — **never concurrently into the user's root**.
5. The controller verifies lease, scope, baseline hashes, and change provenance.
6. Accepted changes are staged, validated against applicable oracles, then published through a **durable integration journal**.
7. Completion is reported through Chat cards **only** when all applicable blocking criteria carry valid, current evidence.

**Response versus implementation.** An agent's text reply is not a file change and not proof of completion. Coding requires an authorized task, a current lease, observed file changes, and criterion-linked evidence.

## Architecture at a glance

The system is organised into **thirteen functional layers** with strict dependency direction (lower layers never depend on higher ones) and **exactly one owner per authoritative record**.

| # | Layer | Owns |
| --- | --- | --- |
| 1 | Control Room UI | Renders one persistent Chat; calls only typed commands/queries |
| 2 | Controller Application Services (+ Orchestrator) | Domain records and cross-service coordination |
| 3 | MCF-v2 Communication Fabric | Routing, ACK, retry, dedup, dead letters, replay |
| 4 | Agent Gateway / Adapters | One adapter per CLI: detect, probe, launch, stream, interrupt, translate |
| 5 | Council Engine | Structured FULL deliberation |
| 6 | Context Synchronizer | Immutable snapshots, versions, digests, stale rejection |
| 7 | Task/DAG Engine | Task graph, leases, attempts, scheduling, recovery |
| 8 | Workspace Manager | Boundaries, isolated worktrees/staging, integration, publication journal |
| 9 | Local Execution Kernel | The only launch path for controller-issued processes |
| 10 | Validation / Repair Engine | Oracles, diagnostics, bounded hypothesis-driven repair |
| 11 | Evidence Engine | Artifacts, hashes, citations, provenance |
| 12 | Policy Engine | Material-action authorization and deny reasons |
| 13 | SQLite Storage | The only SQL boundary; stores owner-authorized transactions |

**One owner per authoritative record.** For example: Project service owns project identity, `ProjectIntent`, and the project epoch; Requirement service owns requirements and approvals; Message/Contribution service owns immutable `UserContribution` records; Decision service owns binding decisions and locks; the Orchestrator schedules but never mutates another service's records.

## Authoritative state: twelve state machines

State is **never one giant status field.** Twelve independent, authoritative state machines each govern one concern:

1. Project lifecycle
2. Agent session
3. Message delivery
4. Context
5. Task
6. Lease
7. Council round
8. Synchronization barrier
9. Handoff
10. Execution
11. Validation
12. Repair

Readiness labels, request routing, exploration, research, and decision-validity are **projections or workflows** over these machines — not additional state machines. Three cross-cutting rules apply: a **cross-machine rule**, a **fail-closed rule** (unverifiable state cannot satisfy a positive gate), and a **recovery rule**.

## The two questions: authority vs. truth

Two questions must never be conflated:

1. **What may Mayasaba do?** — answered by user instructions, policy, committed requirements, and valid task/lease commands.
2. **What is actually true?** — answered by current, independently checked evidence about the world, project, and running artifacts.

**Nothing that is not authority can create authority:** not an agent's text, a classifier's output, a confidence score, a majority agreement, a transport acknowledgement, or a process exit code. **Unknown or unverifiable state fails closed** and cannot satisfy a positive gate.

**Evidence-backed completion.** A feature is not complete because code was written or because an agent said so. Completion requires the requested local artifact, the checks appropriate to the task, and the evidence that connects the two — with immutable provenance and content hashes. **A bare file path can never be cited as evidence.**

## The FULL council

**FULL is the only council mode.** Every registered decision point requires all three agents:

- independent proposals from all three, before any agent sees another's;
- a frozen proposal set;
- **six directed cross-critiques** (each agent critiques both peers, never itself);
- rebuttal and optional revision with immutable predecessor links;
- computed evidence grading and preserved material disagreement;
- a deterministic round-robin chair;
- non-chair review of any synthesis, plus a controller coverage check;
- a default **five-round cap** for the entire decision point.

The five externally reported round outcomes are **`converged`**, **`synthesized`**, **`cap reached`**, **`escalated`**, and **`sealed with an open question`**. `CONTINUE` is an internal transition, not a positive outcome.

**Silence is never agreement.** An ACK, non-participation, a majority, or an agent's claim of agreement is never approval. A decision is valid only for its documented scope and evidence, and every decision stays linked to the work it authorized.

## User experience: Chat is the only surface

- **Every launch opens native Chat immediately, independent of CLI readiness.**
- Chat is the **only persistent route**. There is no project-navigation rail, tab bar, dashboard, or standalone Council/Requirements/Decisions/Tasks/Files/Validation/Delivery/Settings page.
- **Open Folder** sits at the lower-left of the Chat shell and is distinct from **Attach files**.
- Send is disabled **only** until an authorized project root is bound. After binding, every Send persists exactly one immutable `UserContribution` — even if no CLI is available.
- The first and later messages use the same composer and `SubmitUserContribution` path. There is no goal-intake wizard.
- Opening a folder creates or restores project identity. It **does not** launch agents, scan the repository, initialize Git, approve work, or consume provider usage.
- Council, requirements, decisions, tasks, files/evidence, validation/repair, delivery, and diagnostics run in the background. They surface only as concise **Chat timeline cards**, inline expansion, or a **temporary dismissible details sheet**.
- The normal UI shows **only missing or unusable CLI warnings.** Healthy `READY` agents do not occupy warning rows.

## Technology stack

| Area | Baseline |
| --- | --- |
| Language | **C++20** |
| UI | **WinUI 3**, XAML, C++/WinRT |
| Platform APIs | Win32 + Windows Runtime; Microsoft WIL for resource wrappers |
| Core build/test | CMake, CTest, GoogleTest/GoogleMock *(pin before build)* |
| UI build | MSBuild, Windows SDK, Windows App SDK |
| JSON / schema | `nlohmann/json` and `pboettch/json-schema-validator` *(candidates — pin and validate)* |
| Hashing | Windows CNG SHA-256 (canonical byte profile must be tested) |
| Persistence | Embedded SQLite C API, single serialized writer |
| Process control | `CreateProcessW`, redirected streams, Job Objects, overlapped I/O / IOCP |
| Workspace isolation | Git worktrees or controller-owned isolated staging |
| Packaging | **WiX-authored MSI** (mandatory end-user installer) |
| UI verification | Windows UI Automation |

**Deliberately absent:** no cloud service, remote database, hosted component, application account, managed .NET application runtime, embedded browser UI, or JavaScript application runtime. Verification is **local** — there is no hosted CI.

## Repository contents

| File | Role |
| --- | --- |
| [`Mayasaba — Complete System Description (for AI agents).md`](./Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md) | **Authoritative product and architecture specification.** If any other document conflicts with it, this one wins. |
| [`AGENTS.md`](./AGENTS.md) | Repository-wide operating rules for any coding agent working on Mayasaba. |
| [`ARCHITECTURE.md`](./ARCHITECTURE.md) | Implementation-facing architectural reference derived from the specification. |
| [`UI-UX.md`](./UI-UX.md) | Implementation-facing UI/UX, accessibility, and desktop-acceptance reference. |
| `README.md` | This file — orientation and entry point. |

**Authority and precedence:** (1) the user's current explicit instruction → (2) the complete system description → (3) `AGENTS.md` → (4) `ARCHITECTURE.md` and `UI-UX.md` → (5) existing code, tests, schemas, and comments.

## Project status

Mayasaba is at the **specification and architecture stage**. The repository currently contains the authoritative system description and its derived implementation guides; the native C++20 / WinUI 3 implementation is not yet present.

Before production implementation relies on the design, **local Windows evidence is required** for three bounded prototypes:

1. A responsive WinUI Control Room under sustained CLI/event streaming.
2. Controlled process launch, cancellation, crash cleanup, and process-tree ownership.
3. Enforceable workspace access and stale-write rejection for every admitted agent execution profile — proven via controller-mediated staging and integration-journal verification, or a verified OS sandbox boundary.

**A successful build or an opened window does not satisfy these gates.**

## Design principles

- **Deterministic authority.** The controller, not a model, owns state and decisions.
- **Fail closed.** Unknown, stale, or inconclusive state cannot pass a positive gate.
- **Evidence over assertion.** Agreement is not verification; a green check is not certification.
- **One owner per record.** Coordination never implies co-ownership.
- **Least privilege.** Default-deny capabilities; block an operation when required containment cannot be proven.
- **Isolation by default.** Agents work in isolated workspaces; only the controller publishes to the user's root, through a recoverable journal.
- **Honest reporting.** Say `unknown`, `partial`, `stale`, `inconclusive`, or `blocked` when that is the observed state. Never invent progress percentages or token counts.
- **Append-only history.** Supersede records explicitly; never rewrite history to simplify the current view.

## Working rules for contributors and agents

- **Build and validate on Windows.** Use MSVC, the Windows SDK, MSBuild, Windows App SDK, C++/WinRT, CMake, and CTest. A Linux-only result cannot satisfy a Windows acceptance gate.
- **Keep the deterministic core independent of WinUI** so it can be tested without creating a window.
- **Do not** replace the native UI with Electron, a webview, HTML/JavaScript, .NET UI, or another cross-platform frontend without an explicitly authorized architectural change.
- **Do not add hosted CI or cloud execution.**
- **Do not** introduce a fourth agent, a second orchestrator, an alternate state owner, a direct UI-to-agent channel, or a thirteenth state machine.
- **Do not** commit, push, publish, or alter external accounts unless the user explicitly requests it.
- **Every task** requires an `AcceptanceEvidenceMatrix` (or its implementation-stage equivalent); each criterion ends in exactly one of `PASS`, `FAIL`, `INCONCLUSIVE`, `BLOCKED`, or `NOT_APPLICABLE`.
- **Classify failures before repair** and give every repair attempt a falsifiable hypothesis; do not repeat an identical failing strategy without new evidence.

For the full rule set, read [`AGENTS.md`](./AGENTS.md) before making changes.

---

**Mayasaba's success condition:** authority, observed execution, and current evidence remain **distinguishable from user input all the way through to final certification.**
