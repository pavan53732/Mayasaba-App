# Mayasaba implementation-readiness ambiguity audit

## Status and purpose

**Documentation is not yet sufficient for an AI agent to build Mayasaba end to end without making product or architecture decisions.** The original audit baseline was `main` commit `3d51d9a5b3e2ccbe9fea45ad43f72751d2e40692`; this file now tracks later approved documentation decisions and the still-open gaps against the current repository documents. It is a documentation deliverable, not application code, a generated contract registry, or a claim that any Windows behavior has been proved.

The [complete system description](./Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md) is authoritative. [AGENTS.md](../AGENTS.md), [ARCHITECTURE.md](./ARCHITECTURE.md), [UI-UX.md](./UI-UX.md), and [README.md](../README.md) must not weaken it. This audit records where the authoritative text itself leaves two implementations possible, where derived text diverges, and where a named contract still lacks its actual definition. An item remains open until the rule is written into the authoritative description, propagated where relevant, and exercised by a discriminating scenario. A list of names or empty schemas cannot close it.

The word **“or” is not itself a defect**. A closed set of result values, an explicit user choice, and a conditional mechanism with a deterministic selection rule are valid. The defect is an alternative whose eligibility, selector, evidence, failure result, or binding time is left to an implementation agent. Likewise, a safe-sounding adjective such as “bounded,” “sufficient,” or “current” is not an executable rule until its limit and observation procedure are fixed.

## Corrections already justified by existing rules

These corrections remove contradictions; they do **not** complete their typed contracts.

| Item | Current documentation correction | Still needed |
| --- | --- | --- |
| Lease reassignment | Preserve the valid approved task contract, fence the old lease, and create a successor attempt and lease after required synchronization. | Exact concurrency transition table and durable effects. |
| Workspace prototype | Staging and the publication journal control integration; operation admission independently needs proven pre-effect mediation or an effective access boundary. | A documented test procedure and accepted/failing observations for each CLI execution profile. |
| Configuration precedence | Safety invariants and approved authority outrank layered Mayasaba settings; unspecified project/user overrides cannot widen task policy or process access. | A per-setting type, default, merge, allowed-layer, and rejection contract. |
| Onboarding | A successful folder binding opens Chat immediately. **Next** advances only from welcome to open folder; screen 2 has no second Next-to-Chat action. | Remaining first-run UI contract details and scenario review. |

## Contract-completion execution register (documentation only)

The dependency-ordered [contract completion and implementation entry register](./CONTRACT-COMPLETION-REGISTER.md) maps the current Z-01–Z-14 blocker families, genuinely open decisions, four additional adversarial subcases and the minimum executable owner-command/evidence template. It records **planning and acceptance requirements, not completed schemas or tested Windows capabilities**. The original D/B/P0/P1 inventory below remains open until its individual closure proof is supplied.

## Derived contract trace tranche (2026-10-13)

The [C01–C03 source-linked trace corpus](./contracts/README.md) supplements the authoritative rules with adversarial cases for persistence/Stop independence, FULL council completeness and Handoff #9 source-fencing/restart. It remains **documentation-only**: no JSON Schema registry, typed error catalog, atomic cross-owner protocol, numerical limit, containment test or native Windows implementation has been completed. The historical ambiguity inventory remains historical; the D-/B-/P0/Z implementation contracts stay OPEN until actual closure evidence exists.

## Existing blocking contract gaps

The earlier P0/P1 audit remains substantially open. In particular, the repository still has no complete versioned machine-readable definitions for authoritative records, typed UI/application commands and queries, domain events, MCF payloads and errors, nor complete transition tables for the **twelve** state machines. The prose naming those objects is not their schema. The following are the most consequential missing chains:

| ID | Missing chain | Divergent implementation permitted by current prose |
| --- | --- | --- |
| P0-01/02 | Record schemas and all twelve state-machine tables: state, command, guard, transaction, event, illegal result, race precedence, recovery. | Two agents can implement different lease cancellation/expiry/late-response orders while claiming the same invariant. |
| P0-03/04 | Typed application boundary and cross-service transaction/outbox protocol. | Premature acknowledgment is **already forbidden** by the authoritative atomic durability and persist-before-ACK rules. What remains divergent is the exact owner transaction, completion receipt, uncertain-commit result and retry/replay reconciliation contract. |
| P0-05 | Local user/control authority and scoped approval identity. | A stale or cross-project approval may be accepted differently by two implementations. |
| P0-06 | Per-CLI probes, normalized streams, launch/session/cancellation results, task confinement acceptance. | Adapter authors must invent what `READY`, completion, cancellation and unsafe capability mean. |
| P0-08 | Project-owned closure-admission intent/event schema, idempotency and restart effects. The authoritative spec now fixes ownership, purpose, fencing and unknown-commit direction; typed fields/atomic boundary are still missing. | A crash between fencing admission and closure commit may reopen admission in one implementation unless the remaining contract is completed. |
| P0-09 | The seven selection fields are now explicit for B1–B10, including design-time versus per-operation binding, but several exact evidence procedures, defaults and limits are marked `OPEN`. | An implementation cannot lawfully select an open alternative merely because the table has seven columns. |
| P1-02–P1-09 | Full MCF, effect idempotency, council equality, interpretation, oracle, publication, context and limits algorithms. | “Safe,” “sufficient,” “relevant,” and “bounded” remain subjective implementation choices. |
| P1-11/12 | Draft storage/privacy/recovery and retention protection for pending questions/approvals. | Restart and lawful retirement can lose user input or make reopening claims differ. |

No domain implementation should fill these gaps by inference. The required end product is a **complete contract set**, with source links to approved requirements, not merely an inventory of planned schemas.

## H-01 and W-01 decisions selected (2026-10-12)

The user authorized choosing and documenting these two decisions. The [authoritative handoff transition table](./Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md#handoff-machine-9-replacement-of-a-project-scoped-cli-session) selects **CLI session replacement** for machine #9. Orchestrator owns the durable dependent-scheduling hold, with explicit source fencing, successor attachment, context/barrier verification, release and blocked recovery. Agent Gateway, Task/DAG, Context Synchronizer, Kernel and Policy retain their established record and effect authority. Task reassignment, project switching, phase changes and human escalation do not become handoffs. Thus the missing *meaning and owner* of H-01 are closed; the versioned machine-readable record/command/event/error schemas, atomic cross-owner protocol and race fixtures remain open under P0-01/02/03/04.

The [authoritative platform rule](./Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md#platform) selects **Windows 11 version 25H2 (build 26200), native x64 Home/Pro/Education/Enterprise outside S mode** as the sole initial product support target. The MSI rejects other OS/build/edition/mode/architectures before partial installation. Windows 10, other Windows 11 releases, Server, x86 and ARM64 are excluded until an explicit matrix amendment and matching proof; SDK backward compatibility and x64 emulation do not silently expand scope. Thus the missing *selected matrix* of W-01 is closed. A supported release cannot be claimed until the pinned toolchain, installer lifecycle, native UI, three CLI adapters and effective containment/recovery are actually verified on the selected target. This local Windows proof is still open.

| Decision criterion | Verdict | Evidence or remaining limit |
| --- | --- | --- |
| Handoff purpose, owner, phase/guard/event and blocked recovery directions selected | `PASS` | Spec §4 handoff table, AGENTS §4.3, ARCHITECTURE §5, README and UI/UX scenario 73 agree. |
| Initial Windows OS/build/edition/architecture and exclusion behavior selected | `PASS` | Spec §8 Platform, AGENTS §3, ARCHITECTURE §18, README and UI/UX scenario 74 agree. |
| Executable Handoff schemas and cross-service race/transaction contract complete | `BLOCKED` | P0-01/02/03/04 still require versioned definitions and discriminating replay/concurrency fixtures. |
| Supported Windows deployment and CLI safety demonstrated | `NOT_APPLICABLE` | Documentation-only decision; no MSI, application or local three-CLI containment test exists yet. |

**Discriminating documentation scenarios.** (H1) If the old session still has an unfenced lease or an unresolved affected physical outcome, `ConfirmSourceFenced` cannot pass; bounded investigation ends in `BLOCKED` with the scheduling hold intact. (H2) A healthy successor with only a transport ACK or a stale project digest cannot reach `READY_TO_RELEASE`. (H3) After current source revocation, current successor application evidence and barrier proof, `CompleteSessionHandoff` records completion and releases that hold in one Orchestrator transaction; it never reuses the source lease. (H4) A replacement in Project B does not change a Project A session or hold. (W1) An MSI on Windows 11 25H2 native x64 outside S mode may proceed to the remaining release proofs even if a CLI is missing; absence is reported after launch and blocks only dependent operations. (W2) Windows 10, Windows 11 24H2/26H2, Windows 11 ARM64 running x64 emulation, x86 and S mode fail installer platform admission with an English reason. These are specified acceptance vectors, not executed Windows test results.

**Control-only turn exception.** The writer ordinal and first-writer rule are now explicit across the five implementation documents. A control-only Chat contribution attempts persistence without creating a turn or advancing writer rotation; immediate authorized control execution does not await contribution persistence, while failed/uncertain persistence cannot notify a CLI. Mixed-intent routing is now fixed: the valid control executes under its own stable identity, the complete submission attempts one contribution persist, and non-control text enters its normal turn/authority path only after confirmed persistence; the control supplies no authority for that path, and recovery cannot duplicate the control. The precise deterministic control-only recognition predicate and versioned command/error contract remain open under P0-01/03 and P1-05. Do not turn this prose exception into an agent-chosen classifier.

| Documentation amendment criterion | Verdict | Evidence or limit |
| --- | --- | --- |
| Writer order, zero ordinal and separate council counter agree across implementation guides | `PASS` | Authoritative Chat and council procedures, AGENTS, README, architecture and UI/UX name Hermes → Kilo Code → Claude Code and independent counters. |
| Control-only turn exception and persistence/notification boundary are consistent | `PASS` | Broad three-agent Chat statements now exclude control-only contributions; immediate controller action, no turn/ordinal, and no CLI notification after failed/uncertain persistence are stated in the spec and derived guides. |
| Governance and discriminating first-turn scenario recorded | `PASS` | The dated control-only refinement and UI/UX scenario 43 distinguish a preceding control-only submission from the first Hermes-written reply-eligible turn. |
| Handoff purpose/transitions and Windows support matrix selected in the writer/control amendment | `NOT_APPLICABLE` | That earlier amendment did not choose them; the later H-01/W-01 decision section above records their selection without changing the earlier amendment's scope. |
| Runtime writer/control behavior proved | `NOT_APPLICABLE` | Documentation-only change; no application implementation or Windows test exists. |

## U2 — English-only product language

**Approved rule.** English is the sole language for Mayasaba-owned UI/UX, installer text, accessibility output, agent-authored workflow content and replies, and generated/published deliverables. No localization, language picker, multilingual UI or non-English output fallback is permitted. Authorized non-English source material may be read and retained under the existing scope and evidence rules, but its raw text is not reproduced in Mayasaba-generated presentation or deliverables. External CLI-owned and Windows-owned surfaces do not select Mayasaba's language. The authoritative specification and UI/UX acceptance scenario 72 carry this rule.

**Binding agent-reply consequence.** A non-English reply from any of the three CLIs is never shown, even as provisional streaming, in the primary answer, Show details, notifications, accessibility output or history. The affected turn has an English blocker and controlled recovery; it cannot silently translate the response, omit that agent, or finalize with only the other two. Raw diagnostic/evidence bytes stay outside user-facing presentation.

**Enforcement contract still open.** Before implementation, define a deterministic and testable English-only admission check for mixed-language prose, code, proper names, identifiers, file names, citations, images containing text, accessibility output and generated artifacts; name the owning boundary, evidence, error/result and recovery behavior. Specify how historic or user-authored non-English text is represented in the composer and immutable Chat history without altering its source bytes or falsely claiming an English rendering. A coding agent may not choose a heuristic, silently translate source, or claim the product is fully English solely because its static strings are English.

**U2 documentation amendment evidence matrix** (scope: the approved language rule, not runtime acceptance):

| Criterion | Verdict | Evidence or limit |
| --- | --- | --- |
| English-only authority covers UI, workflow text, all reply stages and deliverables | `PASS` | The authoritative §3 rule and agent-reply display gate cover provisional streams, primary replies, Show details and accessible output, with no localization or non-English fallback. |
| Supporting UI/UX, architecture, README and agent instructions agree | `PASS` | Each document states English-only output; UI/UX replaces translated-text layout guidance and adds scenario 72. |
| Authorized non-English sources are readable without raw generated display | `PASS` | The authoritative rule and attachment preview conditions distinguish retained source bytes from user-facing output. |
| Complete language verification and historic/user-authored input contract exists | `BLOCKED` | The exact classifier, mixed-content boundary and own-input rendering rule require further contract/authority work before a full build. |
| Windows and installed-CLI language behavior has been proved | `NOT_APPLICABLE` | This change edits documentation only and makes no runtime proof claim. |

## Direct corrections propagated in this documentation batch

**Empty-Chat composer placement (2026-10-13).** The user selected a centered composer for an empty bound project's Chat and bottom docking of that same composer after the first confirmed persisted user contribution. Restored conversations open docked; failed or uncertain first persistence keeps the unsaved draft centered; CLI problem cards alone do not move it. The authoritative specification, UI/UX wireframe and scenario 75, architecture, AGENTS and README carry the same presentation rule. This is a documentation-level UX selection, not a new composer, route, state machine or runtime proof. The implementation-readiness gaps below remain open.

**Three-screen preview and Chat folder-control correction (2026-10-13).** The user requires the complete visible sequence: screen 1 welcome, screen 2 mandatory Open Folder, and screen 3 Chat. Screen 3 has no folder control, visible or hidden, including in its overflow menu. The standalone preview and UI/UX wireframes show all three screens. The later project-switching entry is now approved as **File → Switch Project** in the native Windows application menu outside Chat; it opens the native picker and uses Project-service safeguards. This closes the placement ambiguity without a new screen, route, owner or state machine. Native keyboard, picker and safe-switching behavior still require Windows runtime proof.

**Unknown-isolation switch disposition (2026-10-13).** When a selected project's safe-switch proof lacks a required isolation, supervision or effect-cessation observation, the selection remains an unbound candidate and the old project, view and draft remain active. The controller records the missing observation and runs bounded pause/reconciliation; proof permits binding, while exhaustion records a blocked switch with a precise reason and demonstrated recovery actions. This fixes the disposition without creating a new state machine. The detailed D-09 evidence table and native runtime proof remain open.

**Three-CLI detection and explicit Start/Stop (2026-10-13).** The user requires fresh detection of Hermes Agent CLI, Kilo Code CLI and Claude Code CLI after screen-2 folder binding, plus visible Start and Stop on screen 3. Detection remains bounded and non-mutating; folder binding does not launch working sessions. Start explicitly requests controller-owned initialization of all three project sessions and shows each actual session outcome separately from readiness; Stop acts immediately and prevents restart until a later Start. Send remains persistable before Start or after Stop, but its agent-dependent processing waits. The Start permission is now scoped to the current application run and project: relaunch restores Chat without automatic session startup; prior effects must be reconciled and the user selects Start again. Repeated Start during an in-flight or healthy attempt makes no duplicate launch; after a known partial failure, a later Start revalidates and retries only failed/missing sessions. An uncertain launch or Stop outcome blocks affected restart until bounded reconciliation establishes safety. The screen diagrams, preview, authoritative description, UI/UX, architecture, AGENTS and README carry this UX contract. Exact typed command fields, idempotency-key construction, startup bounds and crash/replay proof remain implementation-readiness work; the preview is illustrative and does not launch CLIs.

**Project-scoped Start retention across switching (2026-10-13).** The user approved Start permission as scoped to a project and current application run rather than current Chat visibility. In A → B → A, B inherits nothing; A retains permission and may reuse its healthy sessions after current identity, isolation and authority validation without another Start or duplicate launch. Explicit Stop requires another Start for that project. Relaunch/crash recovery requires fresh Start per project after reconciliation. Unsafe or uncertain state blocks affected reuse/restart, and closure still requires Reopen Project. Start continues to authorize no task, scan, decision or file effect. Exact typed command fields and concurrency/replay fixtures remain D-09/D-10 implementation-readiness work.

### Native light/dark theme correction

The former UI/UX §4 specified only a light reference palette. The approved presentation rule now follows the Windows light/dark app setting at launch and on live changes; Windows high contrast takes precedence. UI/UX §4 supplies a dark reference palette, and desktop scenario 36 plus the authoritative specification test the full shell, status and details surfaces, readable contrast, and preservation of draft/focus/reading position. Theme selection is presentation state and cannot alter project or controller authority. This closes the missing dark-theme instruction in prose; actual WinUI rendering still requires local Windows proof.

| Criterion | Verdict | Evidence or limit |
| --- | --- | --- |
| Authoritative light/dark and high-contrast precedence stated | `PASS` | Spec §3 feedback/accessibility and §8 presentation name launch, live changes and high-contrast override. |
| UI/UX dark palette and affected surfaces specified | `PASS` | UI/UX §4 includes dark tokens and the complete shell/details coverage. |
| Desktop acceptance distinguishes dark and live switching | `PASS` | UI/UX scenario 36 and the authoritative native-theme test cover readability and state preservation. |
| Windows/WinUI runtime behavior verified | `NOT_APPLICABLE` | This is a documentation amendment; no application code or local Windows theme test exists yet. |

The authoritative specification and relevant derived text now state: (1) a question answer attempts persistence and only a confirmed contribution can be dispatched or interpreted; (2) architecture dispatch has the same gate; (3) restoring a known folder differs from authorized Reopen Project; (4) the closed-project exception applies to Send and its test; (5) an explicitly authorized whole-repository inspection follows the mandatory three-CLI read-only workflow or reports a blocker; (6) Project service owns a durable, idempotent closure-admission intent tied to `ProjectClosure`; (7) B1–B10 show all seven required selection fields with unpinned evidence/binding details identified; (8) a CLI research capability observation `UNAVAILABLE` can lead to a `BLOCKED` assignment; (9) expiry applies only to disposable queue housekeeping, never a queued material operation; and (10) pending questions/approvals protect the records and bytes needed to resume them. These corrections establish direction, not complete P0-08/P0-09 schemas or the whole implementation contract set.

## Additional findings from the deeper cross-document pass

The next table preserves the findings **as discovered at the initial audit baseline**; its source descriptions are historical, not claims that the current spec still has every discrepancy. The status table immediately after it is current and supersedes each historical resolution label. `Specification decision` meant that the earlier authoritative text lacked a unique rule; `Contract detail` meant an established direction needed exact mechanics and negative cases.

| ID | Source and ambiguity | Different behavior an agent could build | Resolution owner/type |
| --- | --- | --- | --- |
| D-01 | Spec §5, “Assign roles and chair” precedes proposal collection but places the skeptic away from the **leading proposal's author**. No leading proposal exists yet. | Assign skeptic before proposals, or wait and assign after disclosure, changing independence and role duties. | Council procedure: specify role assignment timing and what “leading” means; specification decision. |
| D-02 | Spec §5 rotates the chair using decision-point creation ordinal and round number without zero/one indexing, then requires **a non-chair** synthesis reviewer without choosing which one. | Different chair/reviewer after restart or concurrent points; one or both peers may review. | Exact chair formula and reviewer selector, including unavailable reviewer; contract detail. |
| D-03 | Spec §5 gives five externally reported round outcomes, an internal `CONTINUE`, and separate pause/block conditions, but no precedence when cap, missing contribution, user question and invalid evidence coincide. | Same round becomes `cap reached`, `escalated`, paused or blocked depending on check order. | Total termination precedence and complete round state table; specification decision for precedence. |
| D-04 | Spec §5 calls lifecycle decision triggers “including” certain gates; “material” plan/design change is not defined. | A new app or changed plan can enter different numbers of FULL points, or bypass one. | Enumerated trigger registry, exact materiality predicate, deduplication key and affected-phase map; specification decision. |
| D-05 | Spec §5 distinguishes `HARD_LOCK`, `SOFT_DECISION`, `ASSUMPTION`, `OPEN`, but only excludes the last two from implementation authority. | An agent may treat a soft decision as binding or advisory, and invalidate tasks differently. | Per-disposition authority, supersession and invalidation table; specification decision. |
| D-06 | Spec §2 says three independent `InterpretationCandidate`s inform routing, while the controller has no language model and must verify user meaning. “Consequential uncertainty” lacks a structural predicate. | One controller accepts a two-of-three interpretation; another asks a question; another creates multiple work requests. | Source-span/negation/condition verification, conflict and clarification precedence, and explicit deny cases; contract detail plus user choice where semantics remain unproved. |
| D-07 | Spec §2 and UI/UX §8 define outside-root attachment consent and managed copy but not what happens if Send occurs while an attachment is preparing, changes during copy, or copy/persistence partly succeeds. | A contribution can reference different bytes, silently omit an attachment, or duplicate a managed copy. | Attachment identity, consent, snapshot, readiness-at-Send, atomic association, change and recovery table; contract detail. |
| D-08 | Spec §2 and UI/UX §5 require one project per canonical Windows root but do not define identity across aliases, case-only names, junction/target changes, moved folders or inaccessible roots. | The same physical folder can become two projects, or a changed target can inherit old authority. | Canonical identity and rebinding algorithm with race and failure outcomes; contract detail. |
| D-09 | UI/UX §5.4 and the authoritative spec fix the unknown-isolation disposition: retain the old view/draft and unbound candidate through bounded pause/reconciliation, then bind only with proof or block precisely. The exact proof predicate remains open. | Incomplete evidence could still be treated differently by adapters or fail to detect a shared session pointing at the old root. | Exact switch-admission evidence, session-isolation observation and bounded proof table; contract detail. |
| D-10 | Spec §7 now retains project-scoped Start across A → B → A in one run when sessions remain healthy and current, while Stop/relaunch/closure retain their gates. It does not yet specify the full compatibility tuple, idle cleanup trigger, or proof that stopped/old CLI-owned services cannot act later. | One adapter can still misclassify a stale session as reusable or expire a healthy retained session inconsistently. | Session-reuse identity, compatibility, expiry and stop/reconciliation table, including external CLI replacement; contract detail. |
| D-11 | Spec §7 allows discovery by validated explicit path **or** effective PATH, and each CLI may have multiple executables/installations. | Agents pick different binaries or silently switch after a PATH change. | Discovery precedence, identity pinning, user Locate action, ambiguity and change behavior; selection contract. |
| D-12 | Spec §2 says all three receive the same authorized context; §§2/7 also filter secrets and permit narrow CLI-specific runtime reads. The exact context boundary and digest after filtering are unspecified. | Peers receive different project bytes but report one digest, or secrets enter prompts. | Frozen project-context byte set, redaction/refusal rules, per-recipient runtime exception isolation, digest semantics and invalidation; contract detail. |
| D-13 | Spec §8 distinguishes `UNSUPPORTED` from `BLOCKED`, while adapter readiness also has `PROBE_FAILED`; a requested operation can encounter all three. | A permanent unsafe profile is shown as a transient probe failure, or a temporary dependency becomes permanent unsupported. | Per-observation classification and UI/result mapping, with retry/recheck eligibility; contract detail. |
| D-14 | Spec §8 publication permits “continue,” “safely restore,” or user intervention after partial mutation, but the before/after hash and identity combinations are not exhausted. | Two implementations take opposite actions on the same altered user file. | Per-file journal decision table, precedence, durable intent/observation points and no-overwrite negative cases; contract detail. |
| D-15 | Historically, Spec §9 gave broad oracle classes, “where safe and feasible” discrimination, and “relevant” regression checks without criterion-specific applicability. The current authoritative rules expressly reject build-only runtime certification. | Different **adequate** behavior-specific oracles may be selected for the same criterion, or missing proof may be classified differently; build-only runtime certification is **not** a permitted current outcome. | Per-oracle eligibility/adequacy contracts and criterion-specific required cases, invalidation and uncertainty rules; specification decision for subjective acceptance. |
| D-16 | Spec §8 retention lists `PIN BEFORE BUILD` time defaults but leaves the storage ceiling, cleanup scheduling, backup count/durability and protected-reference fencing unpinned. | Implementations delete different data or stop accepting contributions at different thresholds. | Versioned local-storage policy with exact units, priorities, race behavior and user override authority; specification decision. |
| D-17 | Spec §7 permits native CLI web search/fetch for research but actual backend, URL/domain filtering, result provenance and “read-only” effects depend on installed tools. | A research adapter substitutes shell HTTP or calls a backend outside the approved scope. | Per-tool probe and permission matrix, source-observation contract, requested-domain failure disposition; contract detail and local proof later. |
| D-18 | Spec §6 names envelope fields and priority lanes without full payload/version grammar, ordering/fairness, gap and duplicate outcomes. | A recovery event can starve behind urgent traffic or arrive before its owner transaction. | Message catalog, lane scheduling and durable delivery state table; contract detail. |
| D-19 | Spec §5 reopens an existing decision with a Project-service epoch advance **before** a successor point; a Chat-originated material scope change advances the epoch **after** council and approval. The conditions separating these orderings are not specified. | A proposed reopening can invalidate current work immediately in one path but only after approval in another. | Explicit pre-council versus post-approval epoch rule, old-work effect and rollback/refusal behavior; specification decision. |
| D-20 | Spec §5 derives `DecisionValidityAssessment` on stale evidence but does not define the exact dependency traversal or when unaffected leases remain valid. | One controller fences every task; another lets a task rely on a stale lock. | Dependency/invalidation algorithm and conservative unknown-edge outcome; contract detail. |

### Status after the 2026-10-12 binding D-01/D-03 approval

`APPROVED RULE` means the user settled the stated product rule, while its typed records/transitions can still be missing. `APPROVED DIRECTION` means the product principle is settled but the listed algorithmic contract remains. `DERIVABLE` means an existing invariant fixes the safety direction but still needs a discriminating contract. `OPEN DECISION` means at least one behavior or parameter still requires explicit product authority. None of these labels asserts that the complete executable contract or Windows proof exists.

| Finding | Status | Fixed direction or exact remaining obligation |
| --- | --- | --- |
| D-01 | APPROVED RULE; typed allocation contract OPEN | Bind the zero-based **project-local** `d` (independent sequence starting at 0 in each project), one-based `r`, and `i = (d + r - 1) mod 3` pre-proposal rotation below. Durable concurrent allocation/replay schema remains. |
| D-02 | APPROVED RULE; typed review contract OPEN | Both non-chair agents review the exact synthesis revision; either missing/stale review prevents a positive synthesis. The chair index follows D-01. |
| D-03 | APPROVED RULE; full transition table OPEN | The ten-priority outcome order and both tie-breakers below are binding. The twelve-machine registry still needs the council-round commands, state/guard/event/error table. |
| D-04 | OPEN DECISION | Exact trigger registry, materiality predicate, duplicate key and phase map remain unspecified. |
| D-05 | APPROVED DIRECTION; typed contract OPEN | `SOFT_DECISION` is approved scoped revisable authority, subordinate to hard locks/safety; `ASSUMPTION`/`OPEN` do not authorize. Persisted disposition/supersession table remains. |
| D-06 | DERIVABLE; contract OPEN | Unsupported agent interpretations cannot become authority; exact structural checks and disagreement routing remain. |
| D-07 | APPROVED DIRECTION; limits/contract OPEN | One Send identity, complete attachment set, bounded verification, preserved draft, no duplicate or silent omission; exact limits and recovery table remain. |
| D-08 | DERIVABLE; contract OPEN | Canonical identity and safe alias/reparse handling are mandatory; exact rebinding algorithm remains. |
| D-09 | APPROVED DIRECTION; evidence contract OPEN | Switch while isolated old work safely continues; unknown proof preserves the old view/draft and candidate through bounded pause/recovery, then succeeds with proof or blocks precisely. Exact evidence table remains. |
| D-10 | APPROVED DIRECTION; compatibility contract OPEN | A → B → A retains A's Start within the run when current/safe; B inherits nothing; Stop/relaunch/closure retain their gates. Compatibility tuple and stop/expiry proof remain. |
| D-11 | OPEN DECISION | Discovery precedence among multiple eligible executables and path-change binding still need a selection contract. |
| D-12 | DERIVABLE; contract OPEN | Three CLIs receive one authorized project-context snapshot; exact filtered byte set and digest remain. |
| D-13 | DERIVABLE; contract OPEN | Capability observation and assignment disposition are separate; full readiness/result matrix remains. |
| D-14 | DERIVABLE; contract OPEN | No overwrite of newer user edits or blind retry; exhaustive per-file recovery table remains. |
| D-15 | OPEN DECISION | Criterion-specific oracle adequacy and applicability remain subjective without an approved table. |
| D-16 | APPROVED DIRECTION; parameters OPEN DECISION | 30-day diagnostics, 7-day staging/unreferenced-blob grace, three verified backups and opt-in-only 365-day historical policy are initial defaults. Storage ceiling/reserve, schedules, draft/attachment limits and privacy/deletion remain. |
| D-17 | DERIVABLE; contract/proof OPEN | Only authorized probed native read-only web tools; exact per-tool test matrix and local observation remain. |
| D-18 | DERIVABLE; contract OPEN | Durable at-least-once delivery and disposable-only expiry are fixed; full payload, ordering, fairness and replay tables remain. |
| D-19 | APPROVED DIRECTION; invalidation contract OPEN | Review request alone leaves approved epoch unchanged; Project service advances it only after material successor authorization, with unsafe affected work quarantined. |
| D-20 | DERIVABLE; contract OPEN | Preserve demonstrably unaffected work and fence affected unsafe work; dependency traversal/unknown-edge table remains. |

**Binding D-01/D-02 index and scope.** For a durably recorded zero-based **project-local** decision-point creation ordinal `d` and one-based round number `r`, compute `i = (d + r - 1) mod 3` over Hermes, Kilo Code, Claude Code. Each project's first decision point has `d = 0`; creating a point in another project never advances this project's sequence. Chair and proposer duty use `i`, skeptic `(i + 1) mod 3`, verifier `(i + 2) mod 3`. Allocate `d` atomically with one winner under concurrent creation within that project; preserve it on restart, replay and reopening; assign roles before proposals without inspecting proposal content. All three still propose and each critiques both peers. The authoritative spec supplies role and cross-project vectors; the exact schema/transaction remains open P0-01/02 work.

**Binding D-03 precedence.** The authoritative spec now fixes this order: (1) execute authorized emergency Pause/Stop immediately and preserve actual progress; (2) block invalid authority, stale epoch, unsafe physical effect or non-overridable policy failure; (3) keep missing proposals, critiques, required rebuttals or synthesis reviews incomplete until a declared bound, then record the missing obligations and `BLOCKED` disposition without calling the round complete (an explicit Pause remains `PAUSED`); (4) for a complete round, request a material missing user fact as `sealed with an open question`; (5) route an unresolved user preference/choice as `escalated`; (6) investigate insufficient evidence only within authorization and bounds, otherwise record its blocker; (7) accept a fully reviewed, eligible synthesis as `synthesized`; (8) accept a comparable two-round fixpoint as `converged` only absent an already accepted synthesis; (9) report `cap reached` for a completed unresolved round with exhausted allowance; (10) otherwise `CONTINUE` only with a registered cause and remaining authority/bounds, else `BLOCKED`. A missing fact that could change offered choices precedes the trade-off packet; pending user approval never turns into approval by timeout. This is binding behavior; only its complete machine-readable transition/command/error contract remains open.

Discriminating D-03 fixture results: Pause plus a missing critique executes Pause immediately and preserves incompleteness; invalid authority plus cap blocks; a missing critique plus factual question at round five remains incomplete and becomes `BLOCKED` at its bound; a complete round with both a material missing fact and preference question asks for the fact first; insufficient evidence prevents an otherwise stable fixpoint from converging; an eligible accepted synthesis wins over an eligible fixpoint; a complete unresolved round at cap reports `cap reached`; and `CONTINUE` requires a complete round, registered cause, current authority and remaining bounds. These are required expectations for the later typed trace fixtures, not proof those fixtures already exist.

**D-01/D-03 documentation amendment evidence matrix** (scope: this wording amendment, not application acceptance):

| Criterion | Verdict | Evidence or limit |
| --- | --- | --- |
| Authoritative D-01 formula, project-local scope, role ownership and restart vectors recorded | `PASS` | Spec FULL-round procedure names `d`, `r`, all three duty indices, per-project zero start, pre-proposal assignment, cross-project independence and example vectors. |
| Authoritative D-03 precedence and tie-breakers recorded | `PASS` | Spec contains priorities 1–10, incomplete-round handling, missing-fact-before-choice and no timeout approval. |
| Derived documents do not call D-01/D-03 proposals | `PASS` | AGENTS, architecture, UI/UX, README and this status table were checked for stale open-decision language. |
| Machine-readable council-round transition/command/error contract exists | `NOT_APPLICABLE` | This amendment binds D-01/D-03 prose and does not produce the P0-01/02 registry; that separate global build-readiness blocker remains open. |
| Local Windows runtime behavior proved | `NOT_APPLICABLE` | This change edits documentation only and makes no runtime capability claim. |

The onboarding control inconsistency found during this pass was resolved from the already authoritative “binding opens Chat immediately” rule and propagated into the relevant documents. It is not an open item.

## Selection contracts that still need exact evidence and binding mechanics

The ten alternatives in spec §9 now explicitly show all seven required fields and distinguish design-time from per-operation selection. An `OPEN` entry is not resolved by having a column: its exact applicability observation, evidence procedure, limit, transaction or binding event still needs a contract. Examples that matter most:

| Selection | Missing decision that can change the shipped program |
| --- | --- |
| B1 transition guard | What exact owner transaction proves equivalence to compare-and-swap, and when is that mechanism selected? |
| B2 backup | What snapshot boundary ties SQLite rows to managed bytes under concurrent writers and what restore observation accepts it? |
| B3 journal mode | What local-filesystem observation chooses WAL versus rollback and what exact `synchronous`/busy/checkpoint policy is bound? |
| B4 canonical bytes | Which single byte algorithm and numeric/Unicode reject vectors are binding before the first hash? |
| B5 delivery expiry | Which exact message classes may expire, after what bound, and how is an expired operation reported? |
| B6 publication recovery | Which hash/identity combinations select continue, restore or intervention? |
| B7 workspace | What proves Git-worktree suitability and **independent pre-effect mediation or effective confinement** for this root, staged-copy/worktree mechanism and actual CLI profile? Staging itself proves no containment. |
| B8 context retrieval | What dependency uncertainty widens retrieval and what exact bound ends it as `PARTIAL`/`STALE`? |
| B9 capability reassignment | What exact command/evidence admits a successor attempt under the **already fixed** reassign → block → user-intervention ordering, where a block and intervention request may coexist? The ordering itself is not open. |
| B10 closure restart | Which durable Project-service intent/event and compare-and-swap check re-establish admission fencing? |

These contracts are documentation work. Actual CLI compatibility, Windows containment and MSI behavior will still require local evidence later; the documentation must already specify the tests, accepted results and fail-closed disposition so an agent cannot improvise them.

## Open product-policy parameters

The approved retention periods and draft-recovery direction do not authorize an implementation agent to select these remaining product defaults. Each row needs a versioned value/rule, selection authority, failure result and test before the affected feature is implementable.

| Policy | Required decision before implementation |
| --- | --- |
| Local application storage | Default ceiling or sizing rule, safety reserve, measurement boundary and permitted user override. |
| Cleanup execution | Trigger/cadence, batch/stop limit, exact start of the unreferenced-blob grace, interrupted-plan reconciliation and behavior when protected bytes remain over ceiling. |
| Backup creation | Cadence, capacity ceiling, coordinated database/managed-byte snapshot boundary and verification/restore schedule; three retained verified copies alone do not answer these. |
| Draft privacy | Windows-user-profile protection, local draft-byte and selected attachment-copy handling, and visibility when switching projects/users. |
| Draft deletion | Explicit Discard effects, whether unsent drafts survive Close Project and when abandoned drafts expire; no expiration may silently remove a pending submission with uncertain persistence. |
| Attachment preparation | Maximum count, per-file and aggregate byte limits, parser/type limits, finite preparation deadline and behavior after a changed source file. |
| Optional historical retention | User-visible opt-in/withdrawal contract for the initial 365-day period and exact protection of still-required authority/recovery evidence. No default opt-in. |

## Three-loop documentation direction and open operational contracts (2026-10-13)

**Approved behavior, not an implemented self-updater.** The user specified three bounded loops: **self-recovery** of previously approved work through detection, fencing, independent effect reconciliation and safe resumption; **self-repair** of failing acceptance criteria through evidence-linked diagnoses, falsifiable limited attempts and rechecking; and **user-governed self-improvement** through evidence-linked proposals, FULL council and explicit approval for material changes, with no silent alteration or activation of Mayasaba's executable, policy or authority. The authoritative §9 governs these through existing Orchestrator, Kernel, Gateway, Handoff #9, Task/DAG, Workspace, SQLite, Validation/Repair, Evidence and Policy services, adding no state machine or service owner.

**Still OPEN at contract level:** Exact versioned per-operation deadlines and heartbeat/stall criteria, observable independent CLI/child/service liveness, finite retry and recovery bounds, cancellation escalation, idempotency keys, reconciliation proof for source effect cessation, cross-service command/event/error identities, routing between recovery and repair, improvement-proposal triggers/deduplication, and the separately user-authorized Mayasaba release/update verification and activation procedure. These are not coding-agent defaults. Existing P0/P1 chains and D-09/D-10/D-14/D-15/D-18 remain open. The three-loop names and scenarios below cannot substitute for schemas, exhaustive traces or local Windows proof.

## Divergence scenarios the completed documents must settle

For each scenario, write initial durable records and observed facts, then the **one allowed controller action**, owner command, event, user projection and recovery result. Replay it against the full contract set. Some cases now have an approved safety direction while their exact typed traces remain incomplete:

1. Lease cancellation, expiry, late CLI response and successor issuance race in one project epoch.
2. A user presses Stop while a Chat contribution commit is uncertain and a prior task remains leased.
3. Council round five has a missing critique, a user factual question and an exhausted time bound. D-03 priority 3 wins: the round stays incomplete with its missing critique and `BLOCKED` disposition; it cannot seal `cap reached` or an open-question outcome. The typed event/transaction trace remains open.
4. The chair writes a synthesis after both non-chair agents disagree; both must review the exact revision. The positive/negative outcome still needs evidence, coverage and D-03 precedence checks.
5. Folder alias binds an existing project, then its reparse target changes before Send.
6. An outside-root attachment changes after consent but before Send; the copy finishes after Send. One submission identity/draft remains, no incomplete contribution is persisted; the byte-identity and deadline table is still open.
7. Switching projects while a CLI-owned shared service and publication journal remain active. Continue only with proven isolation/supervision, otherwise pause/reconcile before switching; the proof and failure table is still open.
8. Publication crashes between two files; the user edits the first file before restart.
9. The database reports an uncertain contribution commit while the draft and managed attachment copy exist.
10. A CLI update changes the executable while a session and plan-application result are cached.
11. A research tool returns a search snippet, a fetched page and a forbidden upload-capable tool event.
12. A closed project with retention opt-in has a pending approval linked to history approaching expiration. Protect required linked records/bytes; the exact reference-eligibility table remains open.

13. An unresponsive CLI has no heartbeat but may have launched an effect-capable helper and has an outstanding lease. Record a bounded stall, refuse to infer cessation, fence and reconcile through Handoff #9; no successor launch/lease from timeout alone, and Stop/relaunch gates still apply.
14. A repair attempt fails with the identical criterion failure signature and no objective improvement. Preserve diagnostic and attempt evidence; stop the repeated strategy at the declared bound, show `BLOCKED` and do not modify the check to claim `PASS`.
15. Repeated observed adapter failures produce a proposed change to Mayasaba's code or process. Record only a nonbinding proposal unless approved; material change follows FULL council and user approval, and candidate code/executable cannot silently replace or activate the installed Mayasaba application.

Where current approved rules cannot determine the action, the fixture must remain **unresolved** and the affected implementation path blocked until an authorized specification decision is recorded. A test that asserts an invented answer would conceal the ambiguity.

## Documentation completion standard

Before telling AI coding agents to build the entire application, every material behavior must have the chain **user authority → canonical owner → typed command → preconditions/transition/transaction/event → permitted effect and recovery → observable result and adequate acceptance oracle**. The twelve machines, commands, records, MCF catalog, errors, compatibility rules, limits and ten selection contracts must be actual versioned definitions, checked for drift against the prose. Two independent trace readers given the same records and observations must derive the same permitted action. Any disagreement is an open specification issue, not an opportunity for an agent to choose.

**Documentation readiness and implementation proof are separate.** The documents can specify an exact local Windows test and fail-closed result before the installed CLIs or containment are tested. Later implementation may claim that capability only after the stated observation passes. This audit neither runs nor claims those proofs.
