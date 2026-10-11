# Mayasaba implementation-readiness ambiguity audit

## Status and purpose

**Documentation is not yet sufficient for an AI agent to build Mayasaba end to end without making product or architecture decisions.** This audit covers the five repository documents at `main` commit `3d51d9a5b3e2ccbe9fea45ad43f72751d2e40692`, plus the local documentation corrections made during this audit. It is a documentation deliverable, not application code, a generated contract registry, or a claim that any Windows behavior has been proved.

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

## Existing blocking contract gaps

The earlier P0/P1 audit remains substantially open. In particular, the repository still has no complete versioned machine-readable definitions for authoritative records, typed UI/application commands and queries, domain events, MCF payloads and errors, nor complete transition tables for the **twelve** state machines. The prose naming those objects is not their schema. The following are the most consequential missing chains:

| ID | Missing chain | Divergent implementation permitted by current prose |
| --- | --- | --- |
| P0-01/02 | Record schemas and all twelve state-machine tables: state, command, guard, transaction, event, illegal result, race precedence, recovery. | Two agents can implement different lease cancellation/expiry/late-response orders while claiming the same invariant. |
| P0-03/04 | Typed application boundary and cross-service transaction/outbox protocol. | One implementation can acknowledge a command before owner state and outbound event are durable; another can acknowledge after. |
| P0-05 | Local user/control authority and scoped approval identity. | A stale or cross-project approval may be accepted differently by two implementations. |
| P0-06 | Per-CLI probes, normalized streams, launch/session/cancellation results, task confinement acceptance. | Adapter authors must invent what `READY`, completion, cancellation and unsafe capability mean. |
| P0-08 | Project-owned closure-admission intent/event schema, idempotency and restart effects. | A crash between fencing admission and closure commit may reopen admission in one implementation. |
| P0-09 | Seven required selection fields for B1–B10, including design-time versus per-operation binding. | The current five-column summary does not identify when or on what evidence a fallback becomes lawful. |
| P1-02–P1-09 | Full MCF, effect idempotency, council equality, interpretation, oracle, publication, context and limits algorithms. | “Safe,” “sufficient,” “relevant,” and “bounded” remain subjective implementation choices. |
| P1-11/12 | Draft storage/privacy/recovery and retention protection for pending questions/approvals. | Restart and lawful retirement can lose user input or make reopening claims differ. |

No domain implementation should fill these gaps by inference. The required end product is a **complete contract set**, with source links to approved requirements, not merely an inventory of planned schemas.

## Additional findings from the deeper cross-document pass

“Resolution” below names the rule the documentation must contain. It does not silently select the answer. `Specification decision` means the present authoritative text does not establish a unique rule; the user or the existing Requirement/Decision authority must choose it. `Contract detail` means the established rule needs exact mechanics and negative cases.

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
| D-09 | UI/UX §5.4 says switching folders requires pause/recovery **when active work makes switching unsafe**; the authoritative spec says switching does not close, retarget or silently cancel work. | One UI blocks switching on every active task; another switches while a shared session still points at the old root. | Exact switch-admission conditions, session isolation check, and action on unsafe activity; specification decision. |
| D-10 | Spec §7 permits session reuse only when compatible and safe, but does not specify the compatibility tuple, idle cleanup trigger, or proof that stopped/old CLI-owned services cannot act later. | One adapter reuses a stale session; another always restarts and loses current work. | Session-reuse identity, expiry and stop/reconciliation table, including external CLI replacement; contract detail. |
| D-11 | Spec §7 allows discovery by validated explicit path **or** effective PATH, and each CLI may have multiple executables/installations. | Agents pick different binaries or silently switch after a PATH change. | Discovery precedence, identity pinning, user Locate action, ambiguity and change behavior; selection contract. |
| D-12 | Spec §2 says all three receive the same authorized context; §§2/7 also filter secrets and permit narrow CLI-specific runtime reads. The exact context boundary and digest after filtering are unspecified. | Peers receive different project bytes but report one digest, or secrets enter prompts. | Frozen project-context byte set, redaction/refusal rules, per-recipient runtime exception isolation, digest semantics and invalidation; contract detail. |
| D-13 | Spec §8 distinguishes `UNSUPPORTED` from `BLOCKED`, while adapter readiness also has `PROBE_FAILED`; a requested operation can encounter all three. | A permanent unsafe profile is shown as a transient probe failure, or a temporary dependency becomes permanent unsupported. | Per-observation classification and UI/result mapping, with retry/recheck eligibility; contract detail. |
| D-14 | Spec §8 publication permits “continue,” “safely restore,” or user intervention after partial mutation, but the before/after hash and identity combinations are not exhausted. | Two implementations take opposite actions on the same altered user file. | Per-file journal decision table, precedence, durable intent/observation points and no-overwrite negative cases; contract detail. |
| D-15 | Spec §9 gives broad oracle classes, “where safe and feasible” discrimination, and “relevant” regression checks. It does not state which properties demand which observations or who decides applicability. | A build-only check may certify behavior in one implementation; another marks it inconclusive. | Per-oracle eligibility/adequacy contracts and criterion-specific required cases, invalidation and uncertainty rules; specification decision for subjective acceptance. |
| D-16 | Spec §8 retention lists `PIN BEFORE BUILD` time defaults but leaves the storage ceiling, cleanup scheduling, backup count/durability and protected-reference fencing unpinned. | Implementations delete different data or stop accepting contributions at different thresholds. | Versioned local-storage policy with exact units, priorities, race behavior and user override authority; specification decision. |
| D-17 | Spec §7 permits native CLI web search/fetch for research but actual backend, URL/domain filtering, result provenance and “read-only” effects depend on installed tools. | A research adapter substitutes shell HTTP or calls a backend outside the approved scope. | Per-tool probe and permission matrix, source-observation contract, requested-domain failure disposition; contract detail and local proof later. |
| D-18 | Spec §6 names envelope fields and priority lanes without full payload/version grammar, ordering/fairness, gap and duplicate outcomes. | A recovery event can starve behind urgent traffic or arrive before its owner transaction. | Message catalog, lane scheduling and durable delivery state table; contract detail. |
| D-19 | Spec §5 reopens an existing decision with a Project-service epoch advance **before** a successor point; a Chat-originated material scope change advances the epoch **after** council and approval. The conditions separating these orderings are not specified. | A proposed reopening can invalidate current work immediately in one path but only after approval in another. | Explicit pre-council versus post-approval epoch rule, old-work effect and rollback/refusal behavior; specification decision. |
| D-20 | Spec §5 derives `DecisionValidityAssessment` on stale evidence but does not define the exact dependency traversal or when unaffected leases remain valid. | One controller fences every task; another lets a task rely on a stale lock. | Dependency/invalidation algorithm and conservative unknown-edge outcome; contract detail. |

The onboarding control inconsistency found during this pass was resolved from the already authoritative “binding opens Chat immediately” rule and propagated into the relevant documents. It is not an open item.

## Selection contracts that still need seven fields

The ten alternatives in spec §9 currently have a default, some eligibility text, an authority label and a failure note. Each still needs **applicability, required evidence and binding point made exact**, with a design-time or runtime selection classification. Examples that matter most:

| Selection | Missing decision that can change the shipped program |
| --- | --- |
| B1 transition guard | What exact owner transaction proves equivalence to compare-and-swap, and when is that mechanism selected? |
| B2 backup | What snapshot boundary ties SQLite rows to managed bytes under concurrent writers and what restore observation accepts it? |
| B3 journal mode | What local-filesystem observation chooses WAL versus rollback and what exact `synchronous`/busy/checkpoint policy is bound? |
| B4 canonical bytes | Which single byte algorithm and numeric/Unicode reject vectors are binding before the first hash? |
| B5 delivery expiry | Which exact message classes may expire, after what bound, and how is an expired operation reported? |
| B6 publication recovery | Which hash/identity combinations select continue, restore or intervention? |
| B7 workspace | What proves Git-worktree suitability and staged-copy containment for this root and CLI? |
| B8 context retrieval | What dependency uncertainty widens retrieval and what exact bound ends it as `PARTIAL`/`STALE`? |
| B9 capability reassignment | When is a successor attempt possible, and how do operational blocking and user intervention coexist? |
| B10 closure restart | Which durable Project-service intent/event and compare-and-swap check re-establish admission fencing? |

These contracts are documentation work. Actual CLI compatibility, Windows containment and MSI behavior will still require local evidence later; the documentation must already specify the tests, accepted results and fail-closed disposition so an agent cannot improvise them.

## Divergence scenarios the completed documents must settle

For each scenario, write initial durable records and observed facts, then the **one allowed controller action**, owner command, event, user projection and recovery result. Replay it against the full contract set. The following cases currently expose unresolved choices:

1. Lease cancellation, expiry, late CLI response and successor issuance race in one project epoch.
2. A user presses Stop while a Chat contribution commit is uncertain and a prior task remains leased.
3. Council round five has a missing critique, a user factual question and an exhausted time bound.
4. The chair writes a synthesis after both non-chair agents disagree; identify the exact reviewer and outcome.
5. Folder alias binds an existing project, then its reparse target changes before Send.
6. An outside-root attachment changes after consent but before Send; the copy finishes after Send.
7. Switching projects while a CLI-owned shared service and publication journal remain active.
8. Publication crashes between two files; the user edits the first file before restart.
9. The database reports an uncertain contribution commit while the draft and managed attachment copy exist.
10. A CLI update changes the executable while a session and plan-application result are cached.
11. A research tool returns a search snippet, a fetched page and a forbidden upload-capable tool event.
12. A closed project with retention opt-in has a pending approval linked to history approaching expiration.

Where current approved rules cannot determine the action, the fixture must remain **unresolved** and the affected implementation path blocked until an authorized specification decision is recorded. A test that asserts an invented answer would conceal the ambiguity.

## Documentation completion standard

Before telling AI coding agents to build the entire application, every material behavior must have the chain **user authority → canonical owner → typed command → preconditions/transition/transaction/event → permitted effect and recovery → observable result and adequate acceptance oracle**. The twelve machines, commands, records, MCF catalog, errors, compatibility rules, limits and ten selection contracts must be actual versioned definitions, checked for drift against the prose. Two independent trace readers given the same records and observations must derive the same permitted action. Any disagreement is an open specification issue, not an opportunity for an agent to choose.

**Documentation readiness and implementation proof are separate.** The documents can specify an exact local Windows test and fail-closed result before the installed CLIs or containment are tested. Later implementation may claim that capability only after the stated observation passes. This audit neither runs nor claims those proofs.
