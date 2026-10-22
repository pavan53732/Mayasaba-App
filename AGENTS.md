# Mayasaba Agent Instructions

> **Current screen contract:** Screen 2 is the reusable project selector. First binding: Welcome → selector → Chat. Later launches and File → Switch Project return to the selector. See the [selection contract](docs/PROJECT-SELECTION.md), [three-screen implementation guide](docs/THREE-SCREEN-UX-IMPLEMENTATION.md) and [native validation record](native/LOCAL-VALIDATION.md). Implementation reports must retain incomplete acceptance checks and distinguish fixtures from live agent evidence.

This file defines the repository-wide operating rules for any coding agent working on Mayasaba. It is an implementation guide derived from [Mayasaba — Complete System Description (for AI agents).md](./docs/Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md).

## 1. Authority and precedence

Apply project guidance in this order:

1. The user's current, explicit instruction.
2. The complete system description linked above. It is the authoritative product and architecture specification.
3. `AGENTS.md` for repository working rules.
4. `docs/ARCHITECTURE.md` and `docs/UI-UX.md` as implementation-facing explanations of the authoritative specification.
5. Existing code, tests, schemas, and comments.

If derived documentation or implementation conflicts with the complete system description, follow the complete system description and report the conflict. Do not silently reconcile contradictory requirements or redefine established terms.

## 2. Product identity

Mayasaba is a fully native, Windows-only desktop control plane written in C++20. Its interface is WinUI 3 with XAML and C++/WinRT. Its deterministic local core coordinates exactly three externally installed coding CLIs:

- Hermes Agent CLI
- Kilo Code CLI
- Claude Code CLI

Mayasaba is not an AI model, provider, cloud IDE, hosted service, browser application, agent marketplace, or terminal wrapper. Agents provide intelligence; Mayasaba owns state, authority, safety, orchestration, verification, and proof.

Mayasaba has no separate application account or login. It supports software work and other authorized local-file tasks such as documents, research reports, data cleanup, and refactors. Apply only the lifecycle stages and acceptance oracles that fit the task; for example, a report does not require a software build gate.

The central invariant is: **one controller-owned project reality, three separate agent sessions**.

## 3. Required development environment

- Build and validate the product on Windows.
- The initial product target is Windows 11 version 25H2 (build 26200), native x64 Home, Pro, Education and Enterprise desktop editions outside S mode. Windows 10, other Windows 11 releases, Server, x86 and ARM64 are outside the initial matrix until the authoritative spec is explicitly amended and local proof passes. The MSI must reject unsupported OS/build/edition/mode/architecture before partial installation. Do not infer product support from Windows App SDK compatibility or x64 emulation.
- Use MSVC, the Windows SDK, MSBuild, Windows App SDK, C++/WinRT, CMake, and CTest as applicable.
- **Building on this machine requires clearing the proxy environment variables first.** With both `HTTPS_PROXY` and `https_proxy` (or the `http` pair) present in the environment, the .NET-based MSBuild tool task cannot launch `cl.exe`: it fails with `MSB6001` / "Item has already been added. Key in dictionary: 'HTTPS_PROXY' Key being added: 'https_proxy'". Remove `HTTPS_PROXY`, `HTTP_PROXY`, `https_proxy` and `http_proxy` before invoking MSBuild, or the build fails for a reason that looks like a compiler error but is not.
- `native/tests/validate_contract_registry.py` imports `jsonschema`, which is installed in the managed Python virtual environment (`~/.workbuddy-ai/binaries/python/envs/default`), **not** in the base interpreter that is first on `PATH`. Run the validator with the virtual environment's interpreter.
- Use WinUI 3 and native XAML for the product interface. Do not replace it with Electron, a webview, HTML/JavaScript UI, .NET UI, or another cross-platform frontend without an explicitly authorized architectural change.
- Use WiX for the mandatory MSI installer.
- Keep the deterministic core independent of WinUI so it can be tested without creating a window.
- Do not add hosted CI or cloud execution. Build and verification are local to the user's Windows machine.

Linux may be used only for work that does not claim to compile, run, package, or verify the Windows/WinUI product. A Linux-only result cannot satisfy a Windows acceptance gate.

## 4. Non-negotiable architecture invariants

### 4.1 Thirteen functional layers

Preserve these thirteen layers and their boundaries:

1. Control Room UI
2. Controller Application Services, including the Orchestrator
3. MCF-v2 Communication Fabric
4. Agent Gateway / Adapters
5. Council Engine
6. Context Synchronizer
7. Task/DAG Engine
8. Workspace Manager
9. Local Execution Kernel
10. Validation / Repair Engine
11. Evidence Engine
12. Policy Engine
13. SQLite Storage

Do not create a second orchestrator, an alternate state owner, a direct UI-to-agent channel, or a parallel persistence path. Lower layers must not depend on higher layers. Raw CLI protocols stay inside their dedicated adapters.

### 4.2 One owner per authoritative record

- Project service owns project identity, canonical folder binding, the project epoch, and the durable `ProjectClosure` record.
- Requirement service owns requirements, amendments, and requirement approvals.
- Message/Contribution service owns immutable `UserContribution` records and their message, question, and attachment links, plus Chat turn, independent response, draft, peer-review, and consolidated-reply records.
- Work-request authority service owns `WorkRequest`, request routing and authorization, research authorization, and pending change proposals.
- Decision service owns binding decisions, locks, dispositions, and supersession.
- Orchestrator owns cross-service scheduling, barriers, registered decision triggers, and phase transitions. It does not mutate the project epoch or another service's records.
- Council Engine owns council points, provisional positions, critiques, rounds, and syntheses. It cannot bind a decision.
- MCF-v2 owns envelopes, inbox/outbox delivery, retry, acknowledgement, dead letters, and replay.
- Agent Gateway owns session identity, health, and adapter-translated native traffic.
- Context Synchronizer owns immutable snapshots, context versions, and digests.
- Task/DAG Engine owns task graphs, attempts, leases, and fencing versions.
- Workspace Manager owns authorized workspace views, isolated staging, integration, and publication journals.
- Local Execution Kernel owns controller-issued process launch, observation, cancellation, and outcome records.
- Validation/Repair Engine owns validation verdicts, diagnostics, and repair dispositions.
- Evidence Engine owns evidence, hashes, citations, and provenance.
- Policy Engine owns material-action authorization and deny reasons.
- SQLite Storage alone executes SQL and persists owner-authorized transactions. It stores state; it does not originate authority.

### 4.3 Exactly twelve authoritative state machines

The authoritative state machines are:

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

Readiness, request routing, exploration, research, and decision-validity status are projections or workflows over these machines. Do not promote them into additional authoritative state machines.

Handoff machine #9 is the Orchestrator-owned, durable scheduling hold and transfer for replacement of one project-scoped CLI session by a distinct successor for the same CLI/project. It first fences affected old leases and verifies source effect cessation or revocation, then admits a probed successor, verifies current context/plan application and any affected all-three barrier, and only then releases dependent scheduling. It never transfers a lease, retargets a live session, mutates the project epoch or substitutes for Agent Session or Synchronization Barrier ownership. Follow the authoritative [Handoff transition table](./docs/Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md#handoff-machine-9-replacement-of-a-project-scoped-cli-session); missing proof or expired bounds blocks the handoff and preserves the hold.

### 4.4 Deterministic authority

- UI, agent text, classifier output, confidence, majority agreement, transport acknowledgement, and process exit codes are not authority.
- User instructions, policy, approved requirements, binding decisions, and valid task/lease commands define what Mayasaba may do.
- Current reproducible observations define what is factually true.
- A verified fact does not grant permission. A permitted action does not make a technical claim true.
- Unknown or unverifiable state fails closed and cannot satisfy a positive gate.

## 5. User-experience invariants

- English is the only Mayasaba product language. Use English for every Mayasaba-owned UI string, card, control, question, error, recovery message, help item, notification, installer-owned string and accessibility announcement; agent-authored replies, proposals, critiques, plans, questions and narrative evidence accepted into the workflow, plus generated/published deliverables, must also be English. Do not add localization resources, a language picker, multilingual UI, or a non-English fallback. Exact technical identities may remain unchanged only where alteration would break identity or executable correctness: the smallest necessary code identifier/literal, canonical path/filename, URL, hash, CLI/product name, assigned source/legal identifier or user proper name, always with English labels and provenance. This exception never admits unrestricted non-English agent prose or deliverables. Other authorized non-English source prose remains protected bytes and is presented only through an English source label/type/ordinal/digest. The user's own immutable Chat contribution may remain byte-faithfully visible only in its user-message bubble and history/detail view, with English labels/status/accessibility text; generated summaries, notifications, replies and deliverables must not repeat its non-English prose. If English-only output cannot be verified, block that output rather than certify it.
- Never show a non-English AI-agent reply as provisional streaming, the primary answer, an individual Show details contribution/review, a notification, an accessibility announcement or history. Keep unverified stream text hidden; a required non-English response blocks the affected turn with an English reason and controlled recovery, never a translated substitute or two-agent fallback. Protected raw diagnostic bytes are not user-facing text.
- A bound project root is required before Chat opens; Chat is never gated on CLI readiness, but there is no route to Chat without an authorized root. The permitted prelude is Screen 2 Project selection on every later launch, preceded by the first-run introduction until the first successful binding: screen 1 welcome (what Mayasaba is, the user-owned CLI reminder, live status indicator) and screen 2 open folder, then Chat. **Next** advances from welcome to open folder; **Back** returns to welcome during first-run introduction only; switching offers Cancel instead; **Skip** and Escape skip only the explanation and advance to the mandatory open-folder screen. Binding the folder opens Chat immediately, so screen 2 has no separate Next-to-Chat action. Welcome is not automatically repeated after the first binding; Screen 2 returns on subsequent launches and project switching, never gates folder selection, drafts, history, recovery, project switching or CLI readiness, installs nothing (never into the selected folder), collects no credentials, and introduces no route, state machine or record.
- Chat is the only persistent user-facing workspace and the only top-level route. Do not add a project-navigation rail, tabs, dashboard, or standalone Council, Requirements, Decisions, Tasks, Files, Validation, Delivery, Settings, or Agents page.
- **Project selection** is mandatory on screen 2: select a recent project, Open existing folder, or explicitly Create new project folder. See docs/PROJECT-SELECTION.md. Bound-project Chat (screen 3) has no folder control, visible or hidden; its header names the project. **File → Switch Project** in the native Windows application menu, outside Chat content, opens Screen 2 Project selection, whose Open existing folder action opens the native picker for later switching. Project service validates and binds the selected root under the existing safe-switching rules. Switching never silently stops active work, retargets a running CLI, merges project histories, closes or reopens a project. Folder selection remains distinct from **Attach files** and adds no screen, route, service or state machine.
- Send is disabled only until an authorized project root is bound, and a closed project is the sole exception: every submission to a closed project additionally requires an explicit, authorized **Reopen Project** action, independent of CLI readiness. For an open bound project, every Send creates one submission identity and attempts to persist exactly one immutable `UserContribution`, regardless of CLI readiness. A selected attachment that is still preparing holds the same identity and complete draft for bounded verification; rejection, change or timeout never silently omits the attachment or creates a contribution from an incomplete set. If persistence fails or is uncertain, never represent the contribution as saved, accepted, interpreted, or answered: preserve the recoverable draft where possible, display the precise failure with an explicit recovery path, and reconcile through the same idempotency identity before retrying to prevent duplicates. Limits are 50 files, 100 MiB each, 500 MiB aggregate and 120 active seconds preparation; changed source identity blocks the whole draft until reselected or removed.
- The composer exists only in bound-project Chat; Welcome and Screen 2 contain no composer, attachment-entry control or Send control (user confirmation, 2026-10-17). First and later messages use the same composer and `SubmitUserContribution` path. Removing draft entry never authorizes silent deletion or transfer of legacy unbound draft data. There is no initial-goal wizard or special first-message mode. Current scope is derived through read-only queries over approved requirements and binding decisions, linked to immutable Chat contributions and approvals; do not introduce a separate project brief, stored intent summary, scope owner, or approval checkpoint.
- In an empty bound project's Chat, center that one composer in the conversation area. After the first confirmed persisted user contribution, dock the same composer below the chronological timeline and keep it there for subsequent messages; restored conversation opens docked. A failed or uncertain first Send keeps the unsaved draft centered with its error and recovery path. CLI problem cards alone do not dock it. Preserve attachments, focus and reading position during the move and respect reduced motion. This visual transition creates no second input path or route.
- Opening a folder creates or restores project identity and triggers a fresh bounded, non-mutating detection of all three CLIs. It does not launch working agent sessions, scan the repository, initialize Git, approve work, or consume provider usage. Chat screen 3 exposes **Start**, **Pause**, and **Stop**. Start explicitly requests controller-owned initialization of all three project-scoped CLI sessions and shows each observed session outcome in Chat; Stop acts immediately and holds stopped sessions until a later explicit Start. Start permission lasts only for the current application run and that project, independent of whether its Chat is currently visible. Switching A → B never grants B A's permission. Returning to A in the same run retains A's permission and reuses its healthy sessions without another Start only after current project/session identity, isolation and authority revalidate; unsafe or uncertain state blocks affected reuse/restart pending reconciliation. Explicit Stop of A requires a later Start for A, and relaunch/crash recovery requires a fresh Start per project after prior effects are reconciled. Project closure still requires explicit Reopen Project before any work and Start never bypasses it. Repeated Start during an active attempt does not duplicate sessions, and a later attempt may retry only known failed/missing sessions after revalidation. Readiness detection and session activity are distinct status projections. Start alone grants no scan, research, file-write, council, or task authority.
- The compact Chat header permanently carries a small status indicator with exactly one entry per required CLI (Hermes Agent CLI, Kilo Code CLI, Claude Code CLI) showing the CLI name, current observed readiness (`CHECKING`, `READY`, `MISSING`, `UNSUPPORTED`, `PROBE_FAILED`) and a colored dot. It is a truthful read-only projection: never fabricated, averaged, hidden, percentage-bearing, color-only (name + status text + accessible name are required), and never a gate on Chat, Open Folder, Send after binding, drafts, history, recovery or project switching. Missing, unsupported or probe-failed CLIs additionally receive inline problem cards with the exact cause, operation impact, **Locate executable** and **Recheck**; a healthy CLI generates no problem card and no setup banner.
- Council, requirements, decisions, tasks, files/evidence, validation/repair, delivery and diagnostics run in the background. Surface only user-relevant changes as concise Chat timeline cards. Dense details expand inline or in a temporary dismissible sheet that restores Chat focus and reading position.
- Every controller card that has additional inspectable information uses an explicit **Show details** action; it must not navigate to a standalone page.
- Gate agent-dependent operations, not Chat, project selection, history, drafts, or recovery controls.
- Never show an agent's private chain of thought, credentials, authentication tokens, or unrelated process output.
- Follow the current Windows light/dark app setting on launch and live changes; Windows high contrast overrides both standard palettes. Keep every Chat and onboarding surface readable in dark mode and preserve the project, draft, scroll position, focus and controller state on a theme change. Preserve keyboard navigation, visible focus, screen-reader semantics, DPI scaling, and reduced-motion behavior.

- **A surface that must stay reachable cannot be a spanning child of a content-driven `Grid`.** A `Grid` gives a `Grid.RowSpan` child only the height its rows leave over; where the rows are sized by their content, that is nothing, and the spanning child collapses — taking its controls out of reach with no error anywhere. Declare such a surface on the screen that owns the height, give it its own bounded row or a `*` row, and let exactly one row flex. The saved-message search overlay was moved out of the conversation grid for this reason and its results list made the only `*` row, so a short window shrinks and scrolls the list instead of pushing the paging action below the fold. A list in a `*` row must also stretch: aligned to the top it takes the height of its whole content instead, overflows the surface, and has nothing left to scroll. Where the spanning child's own height feeds back into the rows it spans, XAML can raise `E_LAYOUTCYCLE` (`0x802B0014`) from the layout pass, and with no handler the process fail-fasts to `0xC000027B` and the window simply stops answering — so the symptom presents as an unrelated control "never opening" rather than as a layout fault. **That symptom does not identify the spanning child as its cause.** It is the shape to suspect first, not a diagnosis: in this application the attachment-preview panel was moved off the content-driven grid as a structural correction and the fail-fast reproduced unchanged, at the identical fault offset in `Microsoft.UI.Xaml.dll`, so the move restored reachability and nothing else. Attribute a layout cycle only to a measured element, and when the measurement contradicts the suspected cause, record the refutation instead of keeping the tidier story.

See `docs/UI-UX.md` before changing any user-facing flow.

**Three-screen visual refinement (2026-10-16).** Apply the shared native motion/depth system to Welcome, Project selection and Chat. Strong perspective belongs to Welcome/empty-Chat heroes; paths, conversation text, code and evidence remain stable. Use the ten named custom motions in UI-UX.md with lifecycle cancellation, immediate controls, and static reduced-motion/high-contrast alternatives. UX-01–UX-32 supplement existing contract checks. A presentation helper adds no service owner, authoritative state machine, work permission or route. Report renderer fixtures, native interaction evidence and live three-agent proof separately; the visual amendment cannot authorize fabricated replies or activity.

## 6. Agent and model ownership

- Use exactly the three named CLIs; do not introduce a fourth model or hidden interpreter service.
- The user owns each CLI's model, provider, authentication, account, native tools, and update settings.
- Mayasaba must not add model/provider flags, environment overrides, alternate endpoints, configuration writes, silent fallbacks, credential collection, or a model picker.
- Compatibility is established by bounded behavioral and capability probes, never by release-number comparison or a version allow-list.
- Mayasaba launches installed CLIs headlessly only after explicit Chat **Start** authorizes initialization of all three project-scoped sessions. A Send before Start still attempts to persist one contribution, but agent-dependent processing waits for Start. All three are required for every conversational response/interpretation, FULL council, complete repository exploration, or three-agent research; only the leased agent is required for an individual implementation/verification task after its required plan synchronization. After Start, necessary sessions may be resumed on demand for authorized operations; an explicit Stop holds them until a later Start. Every launch remains controller-owned, bounded, and truthfully observed.
- Every reply-eligible Chat message, including a greeting, is assigned to all three authorized project-scoped CLIs for independent responses; a control-only contribution is the immediate-control exception and creates no conversational turn. A deterministic rotating CLI writer drafts one consolidated reply, cycling Hermes → Kilo Code → Claude Code by project-local turn ordinal modulo three (ordinal zero selects Hermes, so a project's first reply is written by Hermes); both peers review the exact final draft. Mayasaba has no synthesis model or provider-settings page of its own.
- Persist one finalized primary reply per turn with all three response references, writer and both reviews; expose contributions through Show details. Chat consolidation is not a council mode, decision, approval, or proof of factual correctness. Material decisions still require FULL council and six actual critiques.
- Missing participants/reviews or exhausted recorded time and byte bounds leave the turn waiting, and a turn that cannot complete within its declared bounds reaches a recorded `BLOCKED` status with a precise reason and a controlled, resumable recovery path — never an indefinite spin — without a one-/two-agent fallback. Both peer reviews reach the writer in full before any revision; no peer feedback is hidden from its intended recipient. Provider token, cost and usage budgets are the user's concern and are never metered, capped or reported by Mayasaba as its own budget state. Chat persistence remains available. Explicit Pause/Stop controls execute immediately through the controller without waiting for agents, contribution persistence or restarting stopped sessions for discussion; a control-only message creates no turn and consumes no turn ordinal, so it never advances the writer rotation. In a mixed-intent submission, execute the valid immediate control under its own stable identity, but dispatch the non-control text only after the complete contribution is confirmed persisted and through its ordinary turn and authority gates. The control grants no authority to the other intent; failed or uncertain contribution persistence creates no turn, CLI notification or non-control work and recovery must not duplicate the control. CLI notification of a submitted control requires confirmed contribution persistence and current authorization.
- Follow the [six-step engineering workflow](./docs/Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md#engineering-request-six-step-contract): shared request/context, independent requirements/questions/plans, routed FULL discussion, authorized plan, all-three plan application, then individually leased execution and verification. New-app material decisions cannot bypass FULL.
- An authorized plan is the Task/DAG graph and contracts linked to Requirement/Decision approvals, not a new brief or owner. Orchestrator owns the synchronization barrier; Context Synchronizer supplies the exact snapshot and session application results. Require matching project/session, epoch, graph revision and digest from all three before granting dependent implementation leases. A transport ACK or prose “understood” is insufficient. Replaced sessions and affected successor plans must resynchronize.
- Reply finalization is not a work-authorization or scheduling gate. Missing mandatory interpretations/council/plan results block dependent work; missing conversational reviews block that reply. Keep one primary reply while allowing linked question, plan, progress and delivery cards. Intake proposals cannot replace FULL proposals unless their registered point, baseline, contract and independence already match.
- Assign individual tasks through scoped leases; do not make every agent write every task. Preserve source links and unaffected work when an authorized plan change invalidates affected leases.
- A missing CLI blocks only operations that require it. Never reduce an all-three workflow to two agents.


**C11 documentation-only quality refinement:** [C11](docs/contracts/C11-COUNCIL-DECISION-QUALITY.md) requires evidence-adequate load-bearing claims, exactly six source-linked substantive critique/disposition rows, shared-assumption adversarial checking, fair comparison of all viable alternatives, explicit historical decision validity and concise choice packs. A hollow rubber stamp cannot count as the actual required critique; a chair may not erase unresolved objections with synthesis prose. All previous D-03/approval/owner/state and five-round bounds remain in force.

## 7. FULL council rules

FULL is the only council mode. Every registered council decision point requires:

- independent current-context proposals from all three agents before disclosure;
- a frozen proposal set;
- six actual directed cross-critiques, each agent critiquing both peers and never itself;
- rebuttal and optional revision with immutable predecessor links;
- evidence grading and explicit preservation of material disagreement;
- deterministic round-robin chair selection;
- both non-chair agents' reviews of the exact synthesis revision;
- a default five-round cap for the entire decision point;
- controller and Decision-service gates before a decision becomes binding.

Silence, an ACK, non-participation, a majority, or an agent's claim of agreement is never approval. `CONTINUE` is an internal transition, not a positive council outcome. Positive convergence requires two comparable completed FULL rounds with identical surviving positions unless an evidence-backed, fully reviewed synthesis resolves the point earlier.

Assign rotating duties before proposals, independent of proposal content; do not use a pre-proposal “leading author.” Council Engine durably and atomically allocates a zero-based project-local decision-point ordinal `d` (starting at 0 independently per project); each point's round number `r` is one-based. In Hermes → Kilo Code → Claude Code order, `i = (d + r - 1) mod 3` assigns chair/proposer `i`, skeptic `(i + 1) mod 3`, and verifier `(i + 2) mod 3`. Persist the assignment before proposals and preserve it on replay. Creating a point in another project never advances this project's `d`; concurrent creation within one project has a single winner for each ordinal, and restart, replay or reopening preserves existing ordinals. Apply the authoritative D-03 precedence in order: immediate authorized Pause/Stop; invalid authority/safety; missing mandatory participation; completed-round missing user fact; completed-round user choice; insufficient evidence; eligible reviewed synthesis; eligible two-round convergence; completed unresolved cap; eligible registered `CONTINUE`, otherwise block. Missing mandatory contributions keep a round incomplete even when time or round allowance expires; an incomplete round cannot become completed `cap reached`. No timeout or silence grants approval. A `SOFT_DECISION` is approved, scoped and revisable authority subordinate to `HARD_LOCK` and safety, while `ASSUMPTION` and `OPEN` authorize no implementation. A request to reopen a decision does not itself advance the approved project epoch; Project service advances it only after the material successor is authorized. Quarantine independently unsafe affected work while review is pending.

## 8. Project, workspace, and filesystem rules

- The Windows folder chosen by the user is the sole canonical project root and final publication destination.
- Project closure is an explicit, authorized user action recorded durably by Project service after race-free quiescence; switching folders, inactivity, application exit and lifecycle `COMPLETE` never imply closure. Rebinding a known folder restores its identity, not an explicit Reopen Project action. Closure never cancels work silently, bypasses unresolved physical recovery, deletes the user's project files, or certifies unfinished work.
- Closure requires no active leases, running processes, pending publication journal, unresolved `UNKNOWN` outcome or in-flight council round; pending questions and approvals are preserved and listed but do not independently block it. The existing Project service and Orchestrator boundaries provide the race-free admission barrier — no second lease-fencing authority is introduced.
- Optional closed-project retention starts at the later of the durable closure commit and the explicit retention opt-in commit, never retroactively; destructive retirement rechecks current closure and policy eligibility. Reopening supersedes the closure record, suspends pending expiration, and discloses lawfully retired history without claiming recovered evidence.
- Switching projects may leave old-project authorized work running only with proven project-scoped isolation and continued safe supervision. If that proof is unknown, keep the old project, view and draft active; hold the selected root as an unbound candidate; record the exact missing isolation, supervision or effect-cessation observation; and run bounded pause/reconciliation. Complete the switch only after proof succeeds. Exhausted reconciliation records a precise blocked switch and demonstrated recovery actions; never retarget a live session or silently cancel work.
- Canonicalize and validate paths, file identity, permissions, reparse points, junctions, aliases, and time-of-check/time-of-use changes.
- Never infer access to siblings, parent folders, adjacent repositories, or the rest of the PC.
- Whole-repository exploration is separately authorized, strictly read-only, coverage-accounted, and performed independently by all three CLIs.
- Implementation happens in controller-authorized isolated worktrees or staged copies, not through concurrent writes to the selected root.
- Use Git worktrees only for compatible existing Git repositories. Never silently run `git init`.
- A task lease is not a filesystem sandbox. Agent implementation edits occur only in controller-authorized isolation and accepted results use the Workspace Manager's durable integration journal. Operation admission separately requires proven pre-effect mediation or a locally proven operating-system access boundary with effective revocation.
- Reject stale-lease, out-of-scope, reparse-escape, conflicting, or unproven writes before the side effect.
- Publish accepted changes through a durable integration journal, guarded file mutations, reconciliation, and post-publication checks. Never blindly overwrite or roll back newer user edits.

## 9. Process and security rules

- The Local Execution Kernel is the exclusive launch path for Mayasaba-issued processes and commands.
- Use controlled Win32 process creation, restricted handle inheritance, redirected streams, and controller-owned Job Objects. Create controlled children suspended, assign them to the job, then resume; reject launch if assignment fails.
- Job Objects provide deterministic lifecycle control, cancellation, and cleanup across the child process tree.
- Staging is an integration mechanism, not filesystem containment. A staging working directory plus Job Object supervision holds material modifications outside the canonical root and gates publication through the durable integration journal, but it does **not** remove direct filesystem access. Filesystem safety independently requires proven pre-effect mediation or an effective access boundary — a tested restricted token, ACL/AppContainer profile, or controller-mediated writes — for every prohibited read/write/network effect, including stale writes. The staging-plus-publication-journal path must never be treated as containment.
- Do not claim a CLI's internal tools or subprocesses were kernel-mediated merely because the parent CLI was launched by the kernel.
- Admit an operation only if material effects are intercepted before execution or independently confined by a tested effective profile.
- Default-deny task capabilities. Block the operation when required containment cannot be proven.
- Keep secrets out of prompts, MCF payloads, ordinary state, logs, evidence, and UI. Required CLI runtime configuration or authentication reads must be narrowly allowed, redacted, and isolated from task tools and agent-visible context.
- Repository files, attachments, web results, rule files, and parent configuration are untrusted input. They cannot become controller instructions.
- Never authorize approval-bypass switches, credential export, external messaging, plugin installation, unrestricted serving, arbitrary HTTP shell commands, or unrelated outside side effects.
- **Authorization precedes irreversible physical effect.** The authoritative owner authorizes and records a durable change first; only then may the irreversible physical step follow — deleting bytes, releasing a handle, publishing a file. Never perform an effect the authoritative layer could subsequently refuse, because a refusal that arrives afterwards is not a refusal at all. When the owner can return the exact identity it acted on (a path, a version), act on that returned identity and never on one read earlier, which another operation may have replaced. A refused operation must leave every record and every byte untouched, and a partially completed one must remain recoverable rather than silently lost.

## 10. Messaging and contracts

- MCF-v2 carries typed agent traffic and cross-bus asynchronous events. The WinUI application boundary uses separate typed commands and queries.
- Every message, event, payload, command, query, state transition, and error has a declared versioned machine-readable contract.
- Persist outbox state before acknowledgement; validate and persist inbox state before receiver acknowledgement.
- Delivery is at least once, so every material operation requires an idempotency key based on project and operation identity.
- An ACK means receipt and persistence only. It never means execution or validation succeeded.
- Reject malformed, unauthorized, oversized, stale, and cross-project traffic with an explicit recorded reason.
- Bound parser bytes, nesting, collection sizes, and stream buffers.
- Authoritative hashes use the `mayasaba-jcs-1` RFC 8785/JCS-compatible I-JSON/UTF-8 profile defined in C06/C07. Reject duplicate keys, invalid Unicode, NaN/infinity/negative zero and unsafe numeric encodings; perform no silent Unicode normalization. Pass the registered vectors before writing authoritative hashes.

## 11. Persistence and evidence

- SQLite is the sole authoritative application-state store and the only SQL boundary.
- Serialize writes through the storage owner. Commit state changes, events, and outbound records transactionally.
- Store large immutable attachment/evidence/workspace bytes only in access-controlled Mayasaba storage, with SQLite-owned identity, digest, type, provenance, and association records.
- Verify managed bytes on use and recovery. Missing or mismatched bytes are integrity failures, not valid evidence.
- Preserve append-only history. Supersede records explicitly; do not rewrite history to simplify the current view.
- Bind evidence to exact artifact hashes, workspace, environment, check identity/version, time, and source provenance.
- A bare file path, an agent narrative, a clean exit, or a successful build is insufficient proof by itself.
- Retention has two separate promises that must both hold: *audit integrity* (authenticity and ordering of every retained record) and *storage boundedness* (no unbounded local disk consumption without a declared policy or safe stopping behavior). Append-only means retained history cannot be silently altered — not that every temporary byte lives forever.
- Authoritative records — active requirements, decisions, approvals, contributions, open task/council/lease/recovery records, and evidence substantiating current acceptance claims — are retained while the project exists. Reclaimable data — completed operation logs and diagnostics, temporary staging, regenerable caches, and unreferenced managed blobs — is reclaimed automatically on declared schedules. Never delete a managed blob because it is old; age is not proof it is unreferenced.
- Initial reclaimable-data defaults are 30 days for completed logs/diagnostics, 7 days after verified publication/recovery for staging, a 7-day safety grace beginning at a durable verified-unreferenced scan for blobs, and three recent verified backups. Authoritative-history expiry is off unless a project explicitly opts in; the optional initial history period is 365 days from the approved later-of-closure-and-opt-in clock with 30-day and 7-day warnings. Default storage is 10 GiB per Windows user with a 5 GiB free-volume reserve and 2–100 GiB user override; cleanup evaluates at startup, each changed 24-hour period and 80%, in 500-item/30-second batches. Back up before migration and at most once per changed 24-hour period. Drafts are Windows-user protected and do not auto-expire. Storage pressure blocks unsafe new writes after eligible cleanup; it never deletes protected history, pending drafts or required evidence.
- Automatic expiration of authoritative history requires an explicit per-project user opt-in under a verifiable retention policy; preservation is the default. Under finite retention keep immutable log segments, verified retention checkpoints, retained hash-chain boundary anchors, and a durable deletion manifest, and always disclose what was intentionally removed — an abbreviated chain is never complete, and expired evidence is never independently verifiable.
- Cleanup must be crash-safe: eligibility computed from authoritative SQLite references; active leases, unresolved operations, pending publication journals, protected evidence, and recovery dependencies excluded; a transactional cleanup plan recorded; references rechecked immediately before deletion; only controller-managed eligible bytes deleted; outcomes recorded and interrupted cleanup reconciled on restart. Eligibility and deletion are transactionally fenced so a blob cannot become newly referenced after eligibility is finalized.
- SQLite Storage remains the only component executing SQL; owning services authorize retirement of their own records and the Policy Engine validates destructive actions. Internal cleanup never deletes the user's project files, never corrupts the database, and never reports a contribution as persisted when it was not.
- **Every storage mutation is admitted before it touches SQLite.** Each `Storage` mutation entry point calls the single admission point (`AdmitWrite_locked`) before any statement is prepared, and admission covers both the quarantine check and the free-space reserve. No mutation may rely on having begun with `BEGIN IMMEDIATE`, and no new entry point may assume a caller checked. The SQLite authorizer installed at open is the backstop, not the mechanism: it denies write actions at the SQLite layer on a quarantined connection (reads stay permitted so inspection can describe the unresolved state), so an entry point that forgets admission still fails closed. A new mutation entry point without admission is incomplete. A denial must be translated to the quarantine explanation at **every** failure path — statement prepare, step and `sqlite3_exec` alike — because SQLite itself reports only `not authorized`; a backstop refusal and an admission refusal must be indistinguishable to a caller.
- **A rollback is proven, never assumed.** After a failed mutation, establish the transaction state with `sqlite3_get_autocommit`; a connection still inside a transaction is quarantined and presented as unusable, never as healthy. Quarantine is cleared only by reopening the store, where the unknown transaction dies with the connection.
- **The UI thread must never block on a contended database.** Storage work runs on the serial background lane (`co_await selectorQueue_.Background()`), and the UI thread only awaits its completion. A store wait must be bounded (`sqlite3_busy_timeout` is 5 s here) and a wait that exhausts its bound must surface a truthful error rather than hang. Note that in WAL mode an outside writer holding a write transaction blocks other *writers*; readers read a snapshot and do not wait, so contention can delay a save but cannot by itself freeze the message pump. Do not attribute a UI stall to database contention without measuring the pump.
- **A harness that mutates the store under a live application must not measure the application while it holds the lock.** The acceptance harness issues DDL and writes (`ALTER TABLE`, seeding, version injection, `DELETE`) against the same SQLite file the application has open. Timestamp those operations in the run log so a correlation between a held lock and an application delay can be checked rather than assumed — and so that a delay which is *not* correlated is not written up as contention. `native/LOCAL-VALIDATION.md` §22.2 records a measured refutation of exactly that assumption.
- **An attachment identity is owned by the project that first recorded it.** A write naming an identity another project owns — in `draft_attachments` or in committed `attachments` — is refused inside the same transaction as the write it guards, so a rejected write leaves no partial update and no silently rewritten owner row. Upserts must never match on identity alone while authorization reads the supplied project.
- Monitor local application storage — never provider usage — against a declared ceiling: reclaim disposable data first, and if protected data still exceeds capacity, fail closed with a precise storage blocker and recovery/cleanup controls in Chat.
- Unknown physical outcomes fail closed. For every `UNKNOWN` outcome, identify the affected operation and authority boundary, preserve durable intent and evidence, perform bounded independent reconciliation, and permit only a demonstrably safe next action. Never blindly retry, roll back, repeat, or certify an uncertain material side effect.
- Reconciliation is bounded; if no safe path is established the operation becomes `BLOCKED` with its physical outcome explicitly unresolved and a precise user-facing reason, while unrelated authorized work continues. Reaching `BLOCKED` must not relabel an unknown outcome as a known failure: `UNKNOWN` is an evidence status, `BLOCKED` an operational disposition, and an operation can be blocked because its outcome remains unknown — no thirteenth state machine is introduced.
- Recovery actions are contextual (`Recheck outcome`, `Inspect affected files`, `Resolve conflict`, `Retry safely`, `Resume`) and appear only when the owning service can demonstrate they are permitted. A generic Retry must never appear for an unresolved material side effect.

**Bounded recovery, repair, and governed self-improvement (not additional machines).** Self-recovery may restore only existing authorized operations after observed stall/deadline, durable cancellation/fencing and independent effect reconciliation; a hung CLI is replaced only through Handoff #9, and explicit Stop, closure, uncertain effects and fresh Start after relaunch still gate any resumption. Self-repair requires an evidence-linked `FailureDiagnostic`, falsifiable bounded successor attempts under existing Task/DAG leases, rechecking the failing oracle and regressions; three identical-signature attempts without objective progress end in precise `BLOCKED`. Self-improvement is advisory: material changes require applicable FULL council and explicit approval, and activation requires the separately explicit signed/hash-bound MSI action in C06 D10. Apply C06 D9 clocks: expiry ends waiting, never proves cessation; Stop's 20-second reconciliation can leave physical `UNKNOWN` with operational `BLOCKED`. No loop creates a new owner or thirteenth machine.

**Contract corpus (not native proof).** Read [C01–C10 and the v1 registry](./docs/contracts/README.md) alongside the authoritative specification. C01–C05 preserve source-derived traces; C06 binds approved choices; C07–C09 define representation, transitions, transactions and recovery; C10 controls implementation entry. No derived contract overrides the complete system description or proves Windows/CLI behavior.

## 12. Task and implementation workflow

Before editing:

The [implementation-readiness audit](./docs/IMPLEMENTATION-READINESS-AUDIT.md) records documentation closure and remaining implementation-proof gates. Do not treat a planned test, shared wrapper schema or unproved native profile as permission to invent behavior or claim capability. Follow C10's staged implementation entry.

1. Read the authoritative system description and the relevant derived document.
2. Inspect the current repository and local Windows toolchain; do not assume dependencies exist.
3. Identify the owning layer/service and confirm that the change does not duplicate authority.
4. State the affected requirements, paths, contracts, acceptance criteria, and validation method.
5. Treat any material architecture replacement, new service owner, new state machine, frontend-stack change, or weakened safety boundary as a proposed architectural change, not routine implementation.
6. Every normative implementation alternative must have a declared selection contract: default mechanism, eligible alternatives, selection authority, applicability conditions, required verification evidence, failure disposition, and binding point. An unpinned alternative must not be silently chosen by an implementation agent, and a failed alternative must never trigger an unverified fallback.

While editing:

- Keep changes inside the authorized repository scope.
- Prefer small vertical slices with real behavior and criterion-linked tests over placeholder-only scaffolding.
- Keep UI code free of SQL, process launch, direct agent communication, scheduling, and certification logic.
- Keep platform code and CLI-native wire formats behind narrow interfaces.
- Use RAII, WIL wrappers, standard ownership types, and explicit boundary results. Avoid owning raw pointers and manual handle cleanup in ordinary service code.
- Translate platform/library exceptions into registered boundary errors; do not silently discard them.
- Keep agent activity and blocking I/O off the UI thread.
- Preserve cancellation, deadlines, bounded queues, and deterministic replay behavior.
- Do not install, update, or download dependencies merely because the specification lists a candidate. Obtain authorization and pin exact revision, license, and hash first.

After editing:

1. Review the diff for boundary violations and unrelated changes.
2. Run the narrowest relevant checks, then applicable regression checks.
3. Record the exact commands, environment, outcomes, limitations, and artifact hashes when the evidence system exists.
4. Distinguish compilation, process launch, runtime behavior, and acceptance. Do not collapse them into one success claim.
5. Report changed files, verified behavior, failed or unavailable checks, remaining proof obligations, and the next coherent milestone.

**Standing user authorization for Mayasaba Git integration (2026-10-19; supersedes the documentation-only authorization of 2026-10-12):** when an agent completes an authorized assignment or coherent implementation milestone in this repository, review the actual diff, run the applicable checks, update affected documentation and acceptance evidence, then commit the assignment changes to `main` and push to `origin/main` **without asking for another approval or confirmation**. This covers native implementation, tests, schemas, project rules, documentation, and other files necessary for the authorized assignment. It grants Git integration permission, not permission to expand the implementation scope or certify unfinished work.

- Use a descriptive commit subject and a detailed body explaining the problem, actual behavior changed, relevant contract or owner boundaries, checks run and their outcomes, and remaining failures, limitations or incomplete acceptance. Do not put unsupported completion claims in commit messages.
- Stage only reviewed changes belonging to the assignment. Preserve unrelated staged, unstaged and untracked work; exclude generated build logs, binaries, secrets and production user data unless their inclusion is explicitly required and authorized. Do not use a blanket staging command to absorb another agent's work.
- Do not stop after implementation with an unexplained uncommitted assignment diff. Integrate completed, adequately checked changes, or report the exact failed gate, unresolved conflict or other blocker preventing integration. A failed check cannot be hidden by pushing or changing the PASS summary. Partial work must be explicitly identified and must not be certified complete.
- Check local and remote state before integration. Preserve concurrent commits and edits, resolve conflicts deliberately, and rerun affected checks after integration changes. Never force-push, reset away user work, rewrite published history, or silently replace newer edits. If the current checkout is not on `main`, preserve its work and integrate through a safe, reviewed path rather than blindly switching or retargeting it.
- After pushing, verify that local `main` and remote `origin/main` resolve to the same commit. Report the commit hash, synchronization result, and any remaining working-tree changes accurately; a clean tree is not required when unrelated work has been preserved.
- Older memory notes or agent-specific instructions such as "Never push" or "commit only" do not override this explicit user authorization. A newer explicit user instruction to hold, review only, or avoid committing/pushing takes precedence. Reviewing another agent's report alone does not authorize taking ownership of or committing that agent's unfinished changes.

**Detect concurrent work before editing, and stop before editing into it.** More than one agent can work in this checkout at the same time. Before starting an assignment and again before staging anything, run `git status --short` and `git diff --stat` and compare each modified path against the files the assignment intends to touch. A path modified that the assignment did not touch belongs to someone else. Do not build over it, format it, stage it, or commit it, and do not assume a build reflects source you are reading: compare the binary's mtime against the modified files' mtimes, and record the executable and XBF hashes actually exercised. When the assignment requires editing a file another agent is actively changing, ask the user how to proceed rather than deciding alone; when the assignment can be completed without touching that file, scope the work to exclude it and record the untouched defect as an open finding with its file and symbol.

This standing authorization is limited to Git commits and pushes for authorized Mayasaba work. Deployments, releases, unrelated publication and other external account changes still require their own applicable authorization.

**A harness that seeds a row the application validates must seed every field that validation reads.** The application's recent-project selection resolves a row through `domain_id` and compares its `filesystem_identity` against the folder's real Windows file identity (`ProjectService.h:73, :121`; `ProjectRoot.cpp:ValidateProjectRoot`). A harness that inserts only `id`, `display_name` and `canonical_path` gets a row that *looks* right in a `SELECT` and is refused at open with "Recent selection requires a project identity and version". That refusal is correct fail-closed behavior, and it presents as "the harness's UI leg never got where it was going" rather than as a seeding defect. Seed the identity by computing it (`GetFileInformationByHandleEx(FileIdInfo)`, formatted `%x:` + 16 zero-padded hex bytes), never by inventing a string.

**A harness must leave the application on a project whose record it intends to keep.** `NavigateAsync` preserves the current draft through `storage_->SaveDraft(id, draft)` before it will leave a project, and `SaveDraft` throws when no row matches. Deleting a probe project's row *before* switching away therefore makes every later navigation in the run fail, and the failure surfaces in unrelated phases as missing controls and `NOT_RUN` criteria. Move the application off a temporary project first, then delete the row.

**Check for a stale application instance before starting a run.** A leftover `Mayasaba.exe` from an earlier probe holds the single-instance lock, so a fresh launch hands its arguments to the running process and the harness connects to a window bound to a *different* store — every Screen 2 assertion then reads empty labels and the run reports 20 `NOT_RUN` for a reason that has nothing to do with the binary. Kill any existing `Mayasaba.exe` before a run and confirm the task list is empty.

**`pywinauto` is installed for the base interpreter here, not the managed virtual environment.** `C:\Python314\python.exe` resolves `pywinauto`; `~/.workbuddy-ai/binaries/python/envs/default/Scripts/python.exe` does not, and that environment is only needed for `validate_contract_registry.py`'s `jsonschema`. Do not run the acceptance harness with `-I` or through the venv; both make `pywinauto` unimportable.

## 13. Validation and definition of done

**A green acceptance suite is a statement about the harness, not about the product.** This is the single most important reading rule in this repository, and it is stated first because the opposite mistake has already been made here more than once. The native acceptance harness drives a built executable through UI Automation and reports PASS for a set of criteria. Those criteria are *observations of a starting slice*: three screens, project binding, the composer, search, the Action Center projection and a motion system. They say nothing about the thirteen architectural layers, the twelve authoritative state machines, the agent adapters, the council engine, the execution kernel or the MSI, because none of those exist yet. A run reporting "17 PASS, 0 FAIL" is evidence that the harness's own assertions held against that binary on that machine. It is not evidence that Mayasaba works, is complete, or is deployable, and it must never be summarized as if it were. `native/LOCAL-VALIDATION.md` carries an explicit "what is built and what is not" section; read it before drawing any conclusion from a run result.

Every task requires an `AcceptanceEvidenceMatrix` or its implementation-stage equivalent. Each criterion must end in exactly one of:

- `PASS`
- `FAIL`
- `INCONCLUSIVE`
- `BLOCKED`
- `NOT_APPLICABLE`, with verified justification

Completion requires all applicable blocking criteria to be `PASS` or validly `NOT_APPLICABLE`. A blocking `FAIL`, `INCONCLUSIVE`, `BLOCKED` or **unresolved material conflict** cannot be hidden by an aggregate score.

Before execution leasing, and whenever approved requirements or acceptance criteria materially change, check for conflicting acceptance obligations — criteria whose satisfaction conditions are jointly unsatisfiable within the same scope, epoch and evidence rules. An unresolved material conflict prevents completion certification and blocks only affected work; it is a matrix-level condition, not a sixth verdict.

A conflict is proven from the approved obligations, mutually exclusive required artifact states, or recorded oracle results — never from difficulty, estimated effort, or an agent's assertion. Check scope first: a genuine distinction is an ambiguity for clarification, not a contradiction. The Validation/Repair Engine records an evidence-linked `AcceptanceConflictAssessment` (a derived projection, not a new lock or state machine).

Only the owning Requirement and Decision services may resolve a conflict, through the existing authority, evidence, FULL-council and user-choice gates, with affected-work invalidation and matrix re-materialization. No agent or validator may silently weaken, discard, reinterpret, or mark either conflicting criterion satisfied — including by changing its oracle or test, self-declaring it `NOT_APPLICABLE`, or optimizing one at the expense of the other.

**Artifact identity.** A native result is evidence only for the artifact that produced it. Record the SHA-256 of the executable **and** of the compiled XAML (`MainWindow.xbf`): a XAML-only change rebuilds the XBF without relinking the executable, so the executable hash alone does not identify the UI that was tested. Never carry an observation forward to a later build, and never let a harness report a result without the identity it ran against.

**Cold-start timing.** Do not report "no window" as a failure or a regression until the probe has waited long enough for the binary to present one; on this project a cold start takes tens of seconds, not a few. Distinguish "not yet" from "not there", and re-test the baseline with the same wait before attributing a cause. A probe that measured a shell wrapper rather than the process is not evidence at all.

**A UI assertion must poll for its postcondition, never sample it once.** Any check of an asynchronous transition — a screen appearing, a panel opening, focus moving, a list paging — is written as a bounded poll of the real postcondition, with a timeout long enough to pass a correct-but-slow transition and short enough to fail a genuine hang. A fixed `time.sleep()` followed by a single read converts scheduler and layout jitter into failures of the *product*, and it does so intermittently, which is worse than failing consistently because the failure cannot be reproduced on demand. When one assertion in a harness already polls, treat every other assertion that does not as suspect rather than assuming the file's conventions are uniform.

**A focus assertion must first establish, and then record, that the window is foreground.** UIA reports `CurrentHasKeyboardFocus` false for *every* element when the top-level window is not the OS foreground window. A focus check taken without comparing `GetForegroundWindow()` to the window handle therefore cannot distinguish an application that failed to move focus from a harness that lost the foreground, and will report the product as defective for a harness condition. When such an assertion fails, log the foreground state and every element in the tree that holds focus, so the two causes remain separable in the evidence.

**A wheel notification reaches a control only when its window is foreground and the pointer is over that control.** Windows delivers `WM_MOUSEWHEEL` to the foreground window and routes it by pointer position, so a posted message to a top-level handle while another process is foreground — or while the pointer sits outside the target's own rect — is not a wheel over the target. A null result from such a probe is a statement about the probe, not about the application. Establish `GetForegroundWindow() == window handle`, place the pointer inside the control's UIA rect, and then confirm the control's own scroll state moved. This is how the reported "results list does not scroll at 640 × 480" defect was investigated and found not to reproduce: with the window foreground and the pointer over the list, `UIA_ScrollPattern` reported twelve distinct monotonic offsets, the realized item identity changed, and the list reached 48.67 %. Record the readings, not only the conclusion.

**A realized item's UIA rect is clipped to the viewport, so geometry cannot arbitrate a small scroll.** When the viewport is shorter than one item, the single realized item's rect fills the viewport exactly at every offset and a small scroll is invisible in `BoundingRectangle`. Use `CurrentVerticalScrollPercent` and `CurrentVerticalViewSize` (in the generated typelib the booleans are `CurrentVerticallyScrollable` / `CurrentHorizontallyScrollable`; the documented singular form raises `AttributeError`) or compare the realized item's identity. A control reporting `CurrentVerticalViewSize == 100.00 %` and `CurrentVerticalScrollPercent == -1.00 %` is not scrollable at that size — a different finding from a control whose offset does not move, and the two must not be written up as the same defect.

**A layout figure must be read from a settled layout, and the state that produced it logged with it.** A width, height or scroll extent read in the same pass that changed the layout reports the previous pass. This applies equally to a status projection the application publishes about itself: a `compact`/size/root figure that contradicts the window it describes is a measurement-timing observation to record, never a defect to attribute. The measurements behind these three rules, tied to the executable and XBF hashes that produced them, are in `native/LOCAL-VALIDATION.md`.

**Layout-affecting property mutations inside `SizeChanged` event handlers must be deferred to the dispatcher queue.** In WinUI 3, `SizeChanged` fires during layout. Mutating layout-affecting properties of related elements (such as `MaxHeight`, `Height`, `Padding`, `Margin`, or widths on ancestors, siblings, or header grids) synchronously inside a `SizeChanged` callback dirties layout while the layout pass is still executing. If that mutation alters the container's available height or triggers sibling re-measure, it forms a cyclic feedback loop that exhausts the convergence limit and throws `E_LAYOUTCYCLE` (`0x802B0014`), failing fast to `0xC000027B`. Guard the callback with a previous-vs-new size equality check and defer responsive re-computation to `DispatcherQueue().TryEnqueue` using a boolean debounce flag (e.g. `responsiveLayoutQueued_`). This lets the active layout pass settle and reads truthful geometry on the next tick without re-entering layout.

**A fail-fast is not evidence that the cause is unknowable — make the framework record what it threw.** An unhandled WinRT exception surfaces only as `0xC000027B` in `Microsoft.UI.Xaml.dll`, which names the framework and never the code, and every UIA call afterwards fails or returns `None`; the acceptance run can only observe that the window stopped answering. Register `Application::UnhandledException` and write `args.Exception()` and `args.Message()` to a file beside the store before the process dies. Leave `Handled` false: absorbing an exception XAML could not attribute hands the run a half-initialised window and measures that instead of the defect. Note that `UnhandledExceptionEventArgs::Exception()` returns a `winrt::hresult`, not a string — format `static_cast<unsigned>(error.value)`. This is what turned a speculative attribution into the measured `hresult=0x802B0014 | message=Layout cycle detected.  Layout could not complete.` record for the attachment-preview fail-fast. Symbolising a WER-style minidump is not a substitute: the stowed-exception array lives on the faulting thread's stack, which such a dump does not capture, and the linker map is too sparse to resolve a return address inside a static-library region.

**A fallback path that cannot reach its target is worse than no fallback.** Before writing `if X: invoke() else: fallback()`, prove the fallback works in the state the `else` branch actually represents. A keyboard fallback such as `send_keys("%(FS)")` requires an `AccessKey` on the target; this application's `MenuBarItem` and `MenuFlyoutItem` declare none, so the fallback silently guaranteed failure and made an unreachable control indistinguishable from a broken one. When the primary path fails, report the specific reason and fail, rather than substituting a path that was never verified.

**A criterion that was previously BLOCKED or unmeasurable does not inherit any prior verdict when it becomes measurable.** When a crash or environmental block prevented a phase from running, its criterion was BLOCKED — not PASS, not FAIL. Once the block is removed and the criterion runs for the first time, its verdict is determined entirely by the actual outcome of that first measurable run. "The criterion is no longer BLOCKED" and "the criterion PASSED" are separate claims; make only the first if the run did not satisfy the criterion's threshold. This was demonstrated by Criterion 15 (CompositionTarget Performance Profiling): the layout-cycle fix in `da376ed0` unblocked the phase, but the run itself produced `FAIL: Typing p95 latency exceeded 100ms: 104.767 ms`. The commit message incorrectly stated "Criterion 15 PASS"; the correction is in `native/LOCAL-VALIDATION.md` §20.5–20.6 and §21.

**Select a UIA element by control type as well as by text.** WinUI exposes both a `MenuItem` and a `Text` child carrying the same string for a menu entry, and only the former has `InvokePattern`. Text-only matching happens to work while element order is stable and breaks when it is not; match `control_type` too so the selection does not depend on ordering that no contract guarantees.

**Check artifact identity per run, not once per assignment.** Another agent working the same checkout can relink the executable mid-assignment: this happened here, where `MainWindow.xaml.cpp` was edited at 12:41:43 and the binary relinked at 12:43:15 between two batches of runs. Read the SHA-256 from each run's own `artifact-identity.json` and compare it before attributing any difference to a change under test. When the binary moved, either re-run the earlier batch against the new artifact or record the comparison as crossing a binary boundary and scope every claim to the artifact that actually produced it. `git diff` the concurrent edit against the code paths under test to bound the confound, and record that inspection — "it probably didn't matter" is not a bound. A binary's modification time is the cheap check that catches this before it corrupts a conclusion.

**A mutation for an intermittent defect must be deterministic.** AGENTS.md §13 requires a controlling mutation for a corrected regression test. When the original defect is timing-dependent, reverting the fix and hoping to lose the race proves nothing: a green run is indistinguishable from a lucky one. Mutate the *postcondition* so it cannot be satisfied by construction — point the lookup at a name that does not exist — and confirm the test fails at the assertion that encodes the defect, with the specific cause logged rather than a generic error. Restore byte-identically and verify the restore by hash. State plainly that the mutation proves the assertion discriminates *that* failure and does not reproduce the original race.

**A UIA walk is not a UI-thread latency probe.** `find_by_aid` enumerates every descendant over cross-process COM, so its duration is dominated by enumeration cost and grows with the tree; a slow walk and a blocked message pump produce the same number. Never read a walk duration as evidence that the application's UI thread is stalled. Measure the pump directly with `SendMessageTimeoutW(WM_NULL)` under `SMTO_ABORTIFHUNG`, and only call a stall when that probe times out. The "11.399 s UI-thread stall" that drove an entire investigation was a walk; the pump answered in well under a millisecond at the same moment, and the same run logged a 7.88 s walk for an ordinary notice read. See `native/LOCAL-VALIDATION.md` §22.2–22.3.

**Prove a latency probe discriminates before using it to make a claim.** A probe that returns "responsive" for a genuinely blocked target cannot support any conclusion. Suspend the owning process's threads (`GetWindowThreadProcessId`, not the pid you launched — a packaged app's window belongs to a different process) and confirm the probe reports a timeout, then resume and confirm it recovers. The first attempt at this control here suspended the wrong process and produced a false "does not discriminate" verdict; the ownership check is what exposed it. See §22.3.

**A watchdog must not fire before the safeguard it is meant to back up.** `ole_drop.cpp` force-dropped at 2500 ms while its watchdog killed the process at 2400 ms, so the guard could never run and every attempt exited as a timeout blamed on the OS. Order the budgets so the in-process safeguard settles well inside the watchdog, and make the watchdog report the measurement that distinguishes its causes (here: the drag-loop iteration count) rather than naming an environment.

**A synthetic-input helper that fails against a known-good control target is measuring itself.** Before attributing an OLE drag failure to the application, run the same helper against a target that certainly accepts the operation. `ole_drop.exe` reported `QueryContinueDrag calls: 0` against both Mayasaba and a plain text editor, which establishes a helper/session limit and says nothing about the application. An INCONCLUSIVE criterion whose cause is a broken stimulus stays INCONCLUSIVE, but its stated reason must be the measured one.

**A test helper's failure message is a guess, not a finding.** "WATCHDOG: ... under OS environment" asserted a cause the helper never measured. When a test artifact reports a cause, check that it observed it; otherwise the message launders an assumption into evidence.

**A control-target failure settles the question; do not re-litigate it every milestone.** When the same helper fails identically against a target that certainly accepts the operation, the stimulus is established as broken and the criterion's INCONCLUSIVE is permanent until the stimulus changes — not until someone tries again. Criteria 11 and 12 are recorded in `native/LOCAL-VALIDATION.md` §24.4 on exactly this basis. Re-running them under the same launcher costs a milestone and produces no new information; the condition that would make them meaningful again is a session that already owns the foreground, and that is the thing to check first, not the application.

**A property published through `AutomationProperties` on an element with no `AutomationPeer` is unreadable by every UIA client, and the Raw view does not help.** `AutomationProperties::SetItemStatus` on a plain `<Grid>` writes nothing observable: a `Grid` creates no peer, so the Raw view — which walks the peer tree, not the XAML tree — has nothing to visit. `MainWindow.xaml.cpp:597-607` publishes the hero rotation this way onto `WelcomeHeroMark` and `EmptyHeroMark`, both plain `<Grid>`s, so the declared `15, -22, -9` can never be read back by a harness. Before writing a criterion that reads a published value, confirm the element has a peer by finding it in the Raw view first; when it is absent from both views, the finding is a reachability limit and the criterion stays INCONCLUSIVE rather than being quietly passed on an unread value. Note that `pywinauto`'s `descendants()` walks only the Control view, so it cannot distinguish "no peer" from "filtered out" — use `IUIAutomation.RawViewWalker` and `ControlViewWalker` explicitly.

Before a `PASS`, check oracle adequacy: the oracle must actually discriminate the requested property. Where safe and applicable, include positive, negative, boundary, regression, or controlled fault-injection cases. Agent-authored tests remain untrusted until their relevance and assertions are checked. **A regression test written for a corrected defect must include the controlling mutation: revert the fix, rebuild, and record that the test fails at the assertion that encodes the defect, then restore byte-identically and record the pass.** The mutation result is acceptance evidence for the test, not an optional extra; a green run alone cannot show the test would have caught the defect.

Classify failures before repair:

- `CODE_OR_TEST`
- `INTEGRATION_CONFLICT`
- `STALE_CONTEXT`
- `DEPENDENCY_OR_TOOLCHAIN`
- `CLI_OR_PROVIDER`
- `POLICY_OR_SCOPE`
- `EXTERNAL_ENVIRONMENT`
- `UNKNOWN`

Every repair needs a falsifiable hypothesis and must rerun the affected oracle plus relevant regressions. Do not repeat an identical failing strategy without new evidence.

## 14. Required pre-implementation prototypes

Do not claim the architecture is deployable until local Windows evidence exists for all three:

1. A responsive WinUI Control Room under sustained CLI/event streaming.
2. Process launch, cancellation, and crash cleanup across an owned process tree.
3. Enforceable workspace access and stale-write rejection for every admitted agent execution profile, proven by pre-effect mediation or a tested effective operating-system access boundary for all prohibited reads, writes, network effects, and stale writes. Staging and integration-journal verification are separately required for controlled publication; they do not prove containment.

A successful build or an opened window does not satisfy these gates.

## 15. Prohibited shortcuts

Do not:

- add an AI model or hosted backend to Mayasaba;
- introduce an agent other than Hermes, Kilo Code, or Claude Code;
- implement a reduced council or skip any of the six critiques;
- equate agent agreement with verification;
- let the UI, Orchestrator, Council Engine, or storage layer mutate another owner's authoritative records;
- create a thirteenth authoritative state machine;
- gate Chat or folder selection on agent readiness;
- add persistent project-section navigation or standalone project pages outside Chat;
- show fabricated, averaged, hidden, percentage-bearing or color-only CLI status, or use any CLI status to gate Chat or folder selection;
- select or rewrite a CLI's model/provider configuration;
- use release numbers as compatibility gates;
- grant broad filesystem or network access through prompt wording;
- treat Job Objects, worktrees, or permission maps as untested sandboxes;
- let agents concurrently write the user's selected root;
- certify work from an exit code, build result, file existence, agent assertion, or stale evidence;
- silently weaken an acceptance criterion, delete a failing test, or mark an unexecuted check as passed;
- silently redefine, erase, or overwrite historical requirements, decisions, or evidence.

When a safe or authoritative path is unavailable, fail closed, preserve the exact reason, and identify the next action that could establish the missing authority or evidence.

## 16. User preference: reviewing another implementation agent

**Project-specific persistent preference (2026-10-19).** When the user shares another AI agent's implementation report for review, act as the user's reviewer. Deliver direct, actionable feedback addressed to the implementation agent, ready for the user to copy and send. Do not merely summarize the report or reassure the user that work is complete.

- Inspect the available current source, diff, contracts, tests, and Git state before endorsing claims. State exactly what was inspected or executed and what remains unverified.
- Give a clear acceptance verdict supported by evidence. Distinguish confirmed defects, missing assigned scope, insufficient verification, and genuine external blockers. Do not invent findings or broaden the assignment merely to keep the review open.
- For each material finding, explain what the agent did wrong, where it occurs, its practical consequence, the required correction, and the regression test or native observation needed to establish that it is fixed. Use current file/line references where available.
- Separate successful compilation, unit/contract coverage, native interaction acceptance, and live agent evidence. A changed PASS message, stored hash, screenshot, or successful push does not independently prove the claimed behavior.
- Tell the implementation agent to continue the existing authorized assignment, fix confirmed defects, complete omitted scope, and submit actual evidence. Revised report wording alone does not fix implementation defects.
- Request a corrected submission listing root cause, correction, verification result, remaining limitation, and actual Git synchronization state. Keep applicable unexecuted checks INCONCLUSIVE, demonstrated failures FAIL, and genuine dependency blocks BLOCKED; use NOT_APPLICABLE only with a valid scope justification.
- Preserve valid existing results and acknowledge fixes once supported. Close the review when the scoped corrections are complete, retaining any genuinely outstanding acceptance items accurately.
- Reviewing a report does not authorize taking over implementation or contacting another agent. Provide the guidance in this chat unless the user explicitly requests edits or delivery to another agent.

This preference applies to Mayasaba reviews and changes no product authority, implementation scope, or acceptance requirement.

## 17. User preference: close out every work item in AGENTS.md, then commit and push

**Project-specific persistent preference (2026-10-21).** Every completed unit of work ends with three steps, in this order. A work item is not finished until all three are done.

### 17.1 Update this file

Before committing, update `AGENTS.md` so the next agent inherits what this one learned:

- Add or amend any **durable working rule** the work established — a convention, a prohibition, a required ordering, a boundary that must be respected. State it as a rule, not as a narrative of what happened.
- Record any **correction of a previously stated rule**, marking the old rule superseded and saying why. Do not silently rewrite history.
- Record any **environment or toolchain fact** that cost real time and would cost it again (a build prerequisite, a harness timing requirement, a path that does not work).

Keep this file an operating-rules document. Do **not** duplicate the evidence tables, hashes, test counts or per-run observations that belong in `native/LOCAL-VALIDATION.md`; point to that file instead. Volatile figures such as commit SHAs and pass counts must not be written here, because they are wrong the moment the next commit lands.

**When appending to a file, match that file's existing line endings.** Read the bytes first and count them (`data.count(b"\r\n")` versus the bare-`\n` remainder); do not assume a convention from the file's name, from what another file in the repo uses, or from what the working copy happens to show today. Appending with the wrong convention leaves a mixed-ending file that presents as a whole-file diff the next time anything rewrites it. Note that `core.autocrlf=true` here stores LF in the index but materializes CRLF in a fresh checkout, so the working copy is not a stable source of truth — the committed bytes are, and they stay whatever you stage. Preserve UTF-8-without-BOM: do not re-encode to "fix" a console that renders `â€”` or `Â§`, which is a Windows-1252 display artefact rather than corruption.

Never record an unverified acceptance claim in this file. If a criterion was not observed, say it is not observed.

**A timeout that fires before the thing it waits for has finished destroys the evidence.** The budget-ordering rule already applied inside `ole_drop.cpp` (safeguard inside watchdog) also governs the boundary between a test helper and the harness that drives it. The harness waited exactly `timeout=4` on a helper whose watchdog fired at 4000 ms and then spent 400 ms sending an escape and printing what it observed, so Python killed the helper at the instant its escape began: the diagnostic never printed, and the run recorded a bare "reached bounded timeout" that named no cause. Derive the harness timeout from the helper's declared budgets plus margin, name the constant on both sides, and have the helper print its own budget total on startup so a mismatch is visible in the log. The same applies to any bounded wait around a subprocess.

**`ExitProcess` does not flush iostream buffers, so a helper that dies via its watchdog silently loses every line it wrote.** A `std::wcout` line followed by `ExitProcess(4)` is not evidence — it is a measurement that was made and then discarded at the kill boundary. Flush explicitly after any line that must survive the watchdog, or write through a handle you flush. This cost a whole run's `foreground=` reading, which then looked like the probe had never measured the foreground at all.

**A probe whose child output is captured only on clean exit measures nothing when the child hangs.** `subprocess.run(capture_output=True)` discards everything when it raises `TimeoutExpired`, so a hanging target produces a timeout with no evidence in it. Have the child write each stage to its own log file, flushed as it goes, and read that file whether the child exited or was killed. Two consecutive probe runs here returned four timeouts and zero measurements before this was fixed.

**A synthetic input probe run from a non-foreground child cannot establish the foreground.** `SetForegroundWindow` is refused for a process that does not already own the foreground, and the refusal is silent. Measured here: a Python parent set a control window foreground successfully (`GetForegroundWindow()` matched), and every C++ child launched from it then read `fg_before_press` as a *different* window and never matched the target. If a drag or click probe depends on the target being foreground, the child must be given the foreground by a mechanism that works across processes, or the probe must record the refusal and stop — not proceed and report a null result as a finding about the target.

### 17.2 Commit with a complete message

Commit only reviewed, verified changes, under the workflow in §12. The message must stand on its own for a reader who has only the commit:

- the root cause or the requirement, with the file and symbol where it lives;
- what changed and why that shape is correct, including any ordering or concurrency argument;
- how it was verified — the exact commands and the actual results, including counts that were executed rather than assumed;
- what was **not** verified, named explicitly;
- any test that had to change, and why that is a contract change rather than a silenced failure.

Never claim a pass for a check that did not run. Keep unexecuted checks `INCONCLUSIVE`, demonstrated failures `FAIL`, and genuine dependency blocks `BLOCKED`.

### 17.3 Push and verify synchronization

Push the branch to `origin` and then **prove** the synchronization rather than assuming it:

```
git push origin main
git fetch origin
git rev-parse HEAD
git rev-parse origin/main
git status --short
```

Report the resulting commit SHA and confirm that local `HEAD` equals `origin/main` with a clean working tree. If the push is rejected because the remote advanced, integrate deliberately — never force-push and never discard the remote's work.

Report the SHA, the files changed, the executed results and the remaining unverified criteria back to the user in the same turn. A push is not evidence that the work is correct; it only records what was verified.

**Standing exception.** This preference authorizes pushing. The older instruction to commit and leave pushing to the user is superseded for this repository by this section. It does not authorize force-pushing, pushing to a different branch, or pushing unverified work.

### 17.4 Durable rules established by the navigation-wedge and criterion-19 work (2026-10-22)

- **A fail-closed failure path must not be terminal when its cause cannot be retried.** Refusing an operation is correct when a later attempt could succeed. When the cause is that the record the operation depends on no longer exists, every retry fails identically and the refusal becomes a wedge: the user cannot navigate, cannot exit, and cannot close the window. Classify the failure by whether it is retryable, and recover from the unretryable case by dropping the invalid binding rather than by abandoning the operation. Keep the notice and keep the unsaved text; never report the save as having happened.
- **A window coroutine that both navigation and the close path call must not have an early return that the close path cannot escape.** `AppWindow().Closing` and the in-app Exit route through the same navigation coroutine. Any `co_return` added to it is a potential "window will not close" defect, so a refusal added there needs a recovery branch, not only a notice.
- **A failure classification belongs in a pure header, not inline in a window method.** `NavigationRecovery.h` is a function over a value with no IO, lock or window state, so the decision is testable without creating a window. A decision that lives only inside a coroutine cannot be regression-tested without a UI harness, which is why the wedge survived as long as it did.
- **Extract the retryable/unretryable distinction into the store, not the caller.** `TrySaveDraft` reports the missing-record case and `SaveDraft` delegates to it while still throwing, so every existing caller's contract is unchanged and a refusal can still never look like a save. Do not make the one caller that can recover infer the cause by catching and parsing an exception message.
- **A XAML panel gains an AutomationPeer only when it declares `AutomationProperties.Name`; that is the whole rule.** A `<Grid>` or `<StackPanel>` without one is invisible to UIA in *both* the Control and Raw views, so anything set on it through `AutomationProperties` is unreadable rather than merely filtered. When an observable property has to be published for a client, publish it on an element that carries a name — and prefer the container that outlives layout modes over a child that a compact layout collapses (`EmptyHeroArt` is removed below a 430 px body, `EmptyChatHero` is not).
- **"Not observable" is a disposition, not a permanent verdict, until the carrier has been checked.** §24.3 recorded criterion 19 INCONCLUSIVE as an environment limit; the limit was the application's choice of carrier and the correction was two lines. Before recording a reachability limit, confirm that no peer-bearing element could carry the observation. If the carrier is the obstacle, fix the carrier.
- **A rebuild of byte-identical source can produce a different executable hash.** MSBuild is not reproducible here: restoring a mutation byte-identically and rebuilding yielded `fe43f1bd…` where the pre-mutation build was `8f73b204…`, with the XBF unchanged. Evidence taken against the earlier build must not be carried onto the later one, even though the source is identical — re-run and use a fresh evidence directory.
- **An XML comment in XAML cannot contain `--`.** The XAML compiler fails with `WMC9997` at parse time, which is a build failure rather than a rendering problem. Reword instead of escaping.
- **Build toolchain on this machine (re-confirmed, and it costs time every session):** the app project builds only with the **Visual Studio 18 Enterprise** MSBuild at `C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\` with `/p:PlatformToolset=v145`. The VS 2022 BuildTools MSBuild fails the app with `MSB8036` ("Windows SDK version 10.0.17134.0 (or later) was not found") because its VC targets register no toolset matching the project. The **test** projects (`verify_ac_ai.vcxproj` and the other `native/tests` projects) build with either, and use `v143`. The Bash tool rejects command text containing the literal `MSBuild.exe`, so invoke it from the PowerShell tool with the executable name assembled from parts.
- **The acceptance harness reports `source_commit` from `git rev-parse HEAD`, which is the last commit and not the working tree.** A run against uncommitted changes therefore names the previous commit in its `artifact-identity.json`. Treat that field as "the commit the run started from" and rely on the executable and XBF hashes for artifact identity.

### 17.5 Durable rules established by the I1 delivery work (2026-10-22)

- **An append-only attempt log must not carry a uniqueness constraint on the identity it logs.** `command_receipts` is keyed by `receipt_id` and holds *every* attempt for an `operation_id`, because a DUPLICATE or an `IDEMPOTENCY_CONFLICT` refusal is itself a recorded fact. Adding `UNIQUE(operation_id)` made the second attempt fail the constraint instead of being recorded, and the failure looked like a storage bug rather than a schema contradiction. Decide first whether a table is current state (one row per identity) or history (one row per attempt), then constrain accordingly.
- **A read that picks one row out of several for one identity needs an explicit order.** "The prior receipt" is not a row SQLite is obliged to return first. Use `ORDER BY rowid ASC LIMIT 1` and say in the schema comment *why* the earliest row is authoritative, because that reasoning is the invariant, not the query plan. Note that removing the `ORDER BY` alone may change nothing observable — SQLite happened to return rowid order — so a passing mutation does not license removing it.
- **A comment that describes a behaviour the code does not implement is a defect of its own.** `ReceiptForOperation` said the retry kind was "read back as its text rather than mapped" while assigning `Never` unconditionally, which would silently convert an `AFTER_RECONCILIATION` policy into a permanent refusal. When a stored enum value cannot be parsed, carry an explicit `…Unreadable` flag: defaulting to one member of the enum is a claim, and an unreadable value supports no claim.
- **Recompute a digest you are about to trust; compare the supplied one.** A validator that stores a caller's digest beside a caller's bytes without recomputing it records envelopes whose recipients cannot verify what they received. The supplied value is a claim to check, never the source of truth.
- **A receipt's `result_kind` has conditional requirements, and the validator must enforce all four.** `COMMITTED` needs a record identity, a version and at least one event; `REFUSED` and `UNKNOWN` need an error code and message; `DUPLICATE` needs the original receipt id *and* its digest; and each kind forbids the others' fields. A `REFUSED` receipt carrying a `result_record_id` is refused, because a reader would take it as evidence of a commit.
- **A fault-injection test whose subject can fail for two reasons proves nothing.** The "tampered envelope is refused" case first passed under the mutation that removed digest verification, because the same commit was *also* being refused for having no record identity. A controlling mutation that does not fail means the assertion does not discriminate: supply the other required field so the subject is the only possible cause, then re-run the mutation. Do this before claiming the test covers the behaviour.
- **A migration test should reconstruct the previous shape rather than assume it.** Dropping exactly the tables the new version adds and resetting `PRAGMA user_version` produces the old schema from the real one, which exercises the actual migration path instead of a hand-written fixture that can drift from it.
- **A build of a different configuration is a different artifact.** A Release run and a Debug run of the same source produce different executable hashes; evidence from one does not describe the other. Re-run in the configuration the baseline used before comparing tables, and check the XBF hash to confirm whether XAML moved at all.
- **The acceptance harness needs a seeded store and a fresh directory per run.** It expects three seeded projects (`3 projects recorded`) and 50 seeded messages in `alpha`; an empty store makes Phase 1 fail and leaves all 20 criteria `NOT_RUN`. Copy `C:\Users\pavan\PROJECTS\_ui-verify\multi_test\store.db.fixture-backup` to `<evidence-dir>\store.db`, delete any `-wal`/`-shm` siblings, and pass `--store` and `--evidence-dir` as Windows paths. Do not let a run mutate the fixture backup itself. The harness takes its own `.harness.lock`, so two runs must not share a store.
- **Do not drive the harness from the Bash tool.** Path values containing backslashes survive Bash quoting badly enough to produce `WinError 123` on a mangled directory name. Use the PowerShell tool for any command whose arguments are Windows paths.

### 17.6 Durable rules established by the harness close-out (2026-10-22)

- **Two acceptance runs must never share one store, and the exclusion belongs to the harness, not to the operator.** Each run injects DDL faults (`ALTER TABLE contributions -> contributions_test_broken`), seeds rows, and deletes them again while the application has the same SQLite file open. Two runs against one file interleave those writes, and afterwards neither run's log describes the store it was actually driving. `acquire_store_lock()` takes an exclusive `msvcrt.locking` byte range on `<store>.harness.lock` **before the first write and before the launch**; a run that cannot take it logs `ABORTED BEFORE LAUNCH` with the lock path, emits the table with every criterion `NOT_RUN`, and exits non-zero. It does not start, and it does not report a partial pass.
- **The exclusion lock is a sidecar file and must stay one.** It is opened on `<store>.harness.lock`, never on the store, so it takes no byte-range lock inside the database and cannot contend with the application's own writers or with SQLite's own locking. This is a property to demonstrate, not to assume: the oracle `test_acquire_store_lock_excludes_a_second_holder` proves only that a second *harness* holder is refused. That the application is unaffected has to be shown from the run — the app opened the store, wrote its own rows to it during the run, and every store-dependent criterion was measured. A future change that locks the store file itself would silently make the harness a contender for the application's writes while every oracle still passed.
- **A cross-process UIA enumeration is a request the application's own thread answers, so a pump-latency sample taken inside one is not evidence about the application's own work.** `install_uia_walk_tracker()` patches `BaseWrapper.descendants` on the pywinauto base class, because the walks are spread over a dozen call sites and an un-instrumented one would leave samples unattributable. Each walk records its own start and end into `UI_UIA_WINDOWS`; `ui_sample_summary()` then reports slow samples split into "inside a harness UIA walk" and "outside every harness UIA walk", and the raw windows are written to `ui-thread-samples.json`. On the close-out run all three samples over 250 ms fell inside a walk and none outside, over 20.4 % walk coverage — the split is what makes that a reading rather than an interpretation.
- **The sampler, the store-write markers and the walk windows must share one time origin.** `start_ui_sampler` previously offset its samples from its own `started` while `mark_store_write`, `mark_event` and the walk windows offset from `_RUN_T0`, so every cross-series comparison carried an unstated skew. All four now subtract `_RUN_T0`. A marker series that cannot be compared with the sample series without a correction term is not a correlation.
- **A UIA element resolved before a panel is destroyed refers to a node that no longer exists, and reads as an empty string against a panel that is working.** The saved-message search panel is rebuilt on close-and-reopen. `read_status_text()` therefore re-resolves the element on every attempt instead of caching one, and returns the last text it saw so a genuine failure reports what the panel actually said. Do not hoist that lookup out of the loop to save a descendant scan: the scan is the correctness property.
- **`git checkout -- <path>` discards another agent's uncommitted work with no recovery, and "I have read the diff" is not a backup.** This cost a 431-line harness diff in this session. Before any operation that can revert a modified working-tree file, copy it aside and record its SHA-256; `git stash` is not a substitute when the work belongs to someone else and must not be mixed into your own stash. When the work has already been lost, it can sometimes be reconstructed: a `.pyc` in `native/tests/__pycache__/` holds the pre-loss bytecode, and a reconstruction can be checked against it structurally (module and nested code object names, `co_firstlineno`, non-code constants, `co_names`) as well as by size. Prefer that over trusting a memory of the diff.
