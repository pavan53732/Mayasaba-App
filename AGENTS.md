# Mayasaba Agent Instructions

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
- A bound project root is required before Chat opens; Chat is never gated on CLI readiness, but there is no route to Chat without an authorized root. The only permitted prelude is the first-run onboarding sequence shown to a user with no bound project until one is bound: screen 1 welcome (what Mayasaba is, the user-owned CLI reminder, live status indicator) and screen 2 open folder, then Chat. **Next** advances from welcome to open folder; **Back** returns to welcome; **Skip** and Escape skip only the explanation and advance to the mandatory open-folder screen. Binding the folder opens Chat immediately, so screen 2 has no separate Next-to-Chat action. The sequence is never re-shown after a project is bound, never gates folder selection, drafts, history, recovery, project switching or CLI readiness, installs nothing (never into the selected folder), collects no credentials, and introduces no route, state machine or record.
- Chat is the only persistent user-facing workspace and the only top-level route. Do not add a project-navigation rail, tabs, dashboard, or standalone Council, Requirements, Decisions, Tasks, Files, Validation, Delivery, Settings, or Agents page.
- **Open Folder** is the mandatory action on onboarding screen 2. Bound-project Chat (screen 3) has no folder control, visible or hidden; its header names the project. **File → Switch Project** in the native Windows application menu, outside Chat content, opens the native folder picker for later switching. Project service validates and binds the selected root under the existing safe-switching rules. Switching never silently stops active work, retargets a running CLI, merges project histories, closes or reopens a project. Folder selection remains distinct from **Attach files** and adds no screen, route, service or state machine.
- Send is disabled only until an authorized project root is bound, and a closed project is the sole exception: every submission to a closed project additionally requires an explicit, authorized **Reopen Project** action, independent of CLI readiness. For an open bound project, every Send creates one submission identity and attempts to persist exactly one immutable `UserContribution`, regardless of CLI readiness. A selected attachment that is still preparing holds the same identity and complete draft for bounded verification; rejection, change or timeout never silently omits the attachment or creates a contribution from an incomplete set. If persistence fails or is uncertain, never represent the contribution as saved, accepted, interpreted, or answered: preserve the recoverable draft where possible, display the precise failure with an explicit recovery path, and reconcile through the same idempotency identity before retrying to prevent duplicates. Limits are 20 files, 100 MiB each, 500 MiB aggregate and 120 active seconds preparation; changed source identity blocks the whole draft until reselected or removed.
- The first and later messages use the same composer and `SubmitUserContribution` path. There is no initial-goal wizard or special first-message mode. Current scope is derived through read-only queries over approved requirements and binding decisions, linked to immutable Chat contributions and approvals; do not introduce a separate project brief, stored intent summary, scope owner, or approval checkpoint.
- In an empty bound project's Chat, center that one composer in the conversation area. After the first confirmed persisted user contribution, dock the same composer below the chronological timeline and keep it there for subsequent messages; restored conversation opens docked. A failed or uncertain first Send keeps the unsaved draft centered with its error and recovery path. CLI problem cards alone do not dock it. Preserve attachments, focus and reading position during the move and respect reduced motion. This visual transition creates no second input path or route.
- Opening a folder creates or restores project identity and triggers a fresh bounded, non-mutating detection of all three CLIs. It does not launch working agent sessions, scan the repository, initialize Git, approve work, or consume provider usage. Chat screen 3 exposes **Start**, **Pause**, and **Stop**. Start explicitly requests controller-owned initialization of all three project-scoped CLI sessions and shows each observed session outcome in Chat; Stop acts immediately and holds stopped sessions until a later explicit Start. Start permission lasts only for the current application run and that project, independent of whether its Chat is currently visible. Switching A → B never grants B A's permission. Returning to A in the same run retains A's permission and reuses its healthy sessions without another Start only after current project/session identity, isolation and authority revalidate; unsafe or uncertain state blocks affected reuse/restart pending reconciliation. Explicit Stop of A requires a later Start for A, and relaunch/crash recovery requires a fresh Start per project after prior effects are reconciled. Project closure still requires explicit Reopen Project before any work and Start never bypasses it. Repeated Start during an active attempt does not duplicate sessions, and a later attempt may retry only known failed/missing sessions after revalidation. Readiness detection and session activity are distinct status projections. Start alone grants no scan, research, file-write, council, or task authority.
- The compact Chat header permanently carries a small status indicator with exactly one entry per required CLI (Hermes Agent CLI, Kilo Code CLI, Claude Code CLI) showing the CLI name, current observed readiness (`CHECKING`, `READY`, `MISSING`, `UNSUPPORTED`, `PROBE_FAILED`) and a colored dot. It is a truthful read-only projection: never fabricated, averaged, hidden, percentage-bearing, color-only (name + status text + accessible name are required), and never a gate on Chat, Open Folder, Send after binding, drafts, history, recovery or project switching. Missing, unsupported or probe-failed CLIs additionally receive inline problem cards with the exact cause, operation impact, **Locate executable** and **Recheck**; a healthy CLI generates no problem card and no setup banner.
- Council, requirements, decisions, tasks, files/evidence, validation/repair, delivery and diagnostics run in the background. Surface only user-relevant changes as concise Chat timeline cards. Dense details expand inline or in a temporary dismissible sheet that restores Chat focus and reading position.
- Every controller card that has additional inspectable information uses an explicit **Show details** action; it must not navigate to a standalone page.
- Gate agent-dependent operations, not Chat, project selection, history, drafts, or recovery controls.
- Never show an agent's private chain of thought, credentials, authentication tokens, or unrelated process output.
- Follow the current Windows light/dark app setting on launch and live changes; Windows high contrast overrides both standard palettes. Keep every Chat and onboarding surface readable in dark mode and preserve the project, draft, scroll position, focus and controller state on a theme change. Preserve keyboard navigation, visible focus, screen-reader semantics, DPI scaling, and reduced-motion behavior.

See `docs/UI-UX.md` before changing any user-facing flow.

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

Do not commit, push, publish, or alter external accounts unless the user explicitly requests it. **Standing user authorization for this repository's documentation work (2026-10-12):** after completing documentation edits and the necessary diff/check review, immediately commit those changes on `main` with a descriptive subject and detailed commit body, then push `main` to `origin/main` **without asking the user for another approval or confirmation**. Exclude unrelated changes; never force-push. Report the commit and sync result. This standing authorization does not apply to non-documentation work or other external changes.

## 13. Validation and definition of done

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

Before a `PASS`, check oracle adequacy: the oracle must actually discriminate the requested property. Where safe and applicable, include positive, negative, boundary, regression, or controlled fault-injection cases. Agent-authored tests remain untrusted until their relevance and assertions are checked.

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
