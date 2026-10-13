# Mayasaba — Contract Completion and Implementation Entry Register

> **Status:** Documentation planning and acceptance criteria only. Governing product behavior remains in [the authoritative system description](./Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md); the current [readiness audit](./IMPLEMENTATION-READINESS-AUDIT.md) is the issue register. This document authorizes no application implementation, unsupported Windows proof, unapproved product selection, or new architecture owner.

## Decision boundary

Mayasaba is currently **not ready for an unrestricted autonomous whole-product implementation**. The thirteen layers, twelve state machines, three user-managed CLIs, FULL council, D1/D2/D3, project switching, scoped Start/Stop, and three governed loops are binding. Missing command schemas or native confinement proof must never be construed as permission to weaken them.

Classify every resolution as **P** (propagate settled rule), **C** (complete a contract with its authorized owner), **D** (record a genuinely new user/product choice), or **V** (obtain actual implementation-time proof). One item may require several types. Record the source authority and selected binding point before declaring a choice fixed. An audit fixture is not executable contract evidence; a documented acceptance test is not proof it passed.

## Dependency-ordered blocker register

| Order | ID | Minimum output before affected implementation | Classification and guard |
| --- | --- | --- | --- |
| 1 | Z-03 | Approval identity, exact user control recognition, revocation and condition/negation routing; deterministic refusal/clarification vectors | P/C; D only for unresolved policy; never infer consent |
| 1 | Z-07 | Registered D-04 materiality/trigger keys, phase map, normalized surviving-position equality, positive/negative D3 vectors | P/C/D; keep FULL and D1/D2/D3 |
| 1 | Z-09 | D-11 CLI path/PATH precedence, pinned executable identity, per-CLI probe and native event mapping | C/D/V; do not silently switch binaries |
| 1 | Z-10 | D-15 criterion-type/oracle adequacy and required evidence/subjective acceptance table | C/D/V; build cannot certify runtime |
| 1 | Z-11 | Versioned timeouts, monotonic/wall clock use, sleep/relaunch behavior, escalation, Pause/Resume, retry budget | C/D; no arbitrary endless retry |
| 1 | Z-12 | D-16 approved storage, backup, draft, deletion and attachment limits; snapshot/restore/reference fencing | P/C/D/V; keep existing retention defaults |
| 1 | Z-13 | U2 deterministic output admission for mixed-language code, names, source citations and history | C/D; preserve original source bytes |
| 2 | Z-01 | Versioned record/reference/command/query/event/error schemas and canonical bytes B4, with invalid-input vectors | C/D; a list of fields is not a schema |
| 3 | Z-02 | All twelve state-transition tables, guarded owner transactions, durable outbox causality, instance exclusion, crash/replay | P/C; single owner/controller, no new machine |
| 3 | Z-06 | MCF delivery class registry, sequence scope, replay/effect identity, expiry and dead-letter outcomes | C; never expire material obligation silently |
| 3 | Z-08 | Exact permitted context bytes, per-recipient filtering, digest/application receipt, invalidation and fresh independence | C; matching ACK cannot establish context equality |
| 4 | Z-04 | Per-CLI and per-operation pre-effect denial, credential exception, descendant/shared-service revocation and local Windows evidence | C/V; never treat staging/Job Objects as confinement |
| 4 | Z-05 | Exhaustive create/update/delete/rename publication journal/identity/hash recovery table with concurrent edits | P/C/V; preserve user edits and UNKNOWN |
| 4 | Z-14 | Pinned dependency/license/hash/SDK choices, native prototypes, MSI and explicitly authorized executable update path | C/D/V; native release remains unproven |

## Explicitly outstanding product selections

These are **not defaults** or silent engineering discretion: D-04 trigger/materiality, D-11 executable selection, D-15 acceptance sufficiency, D-16 quantitative storage/privacy/deletion/attachment policy, U2 mixed-content display, B4 canonical byte profile, initial keyboard Send behavior, and operational clock/deadline/attempt allowances where not derivable. Every decision records alternatives, scope, approving authority, timestamp/version, applicable evidence, failure result and binding point. Do not reopen previously approved retention periods, platform target, council membership/formula or Start-return permission.

## Four newly isolated adversarial subcases

| Case | Initial facts | Required safe principle | Missing executable trace |
| --- | --- | --- | --- |
| X1 — Competing app instances | Two controllers address the same SQLite/application store and project with independent leases | Only one admitted controller reality; never issue competing writes, effects or leases | Store/instance exclusion transaction, restart/abandonment and second-instance user result — Z-02 |
| X2 — Clock discontinuity | Operation deadline is active when Windows sleeps, wall time shifts, or the app restarts | No sleep or time change grants fresh effects, consent or unlimited retries | Durable budget basis, clock observation, resume and retry accounting — Z-11 |
| X3 — Reused-session council independence | A CLI saw peer proposals from the previous context, then a changed baseline demands fresh independent proposals | Prior disclosure must not be mistaken for independent new analysis | Session/context contamination exclusion and application proof — Z-08 |
| X4 — Valid truncated evidence chain | Remaining chain links hash correctly but the expected durable head or historical boundary is absent | Internal hash consistency alone is not whole-history completeness | Expected head, retained checkpoint/anchor and verification result — Z-01/Z-12 |

## F01–F30 discriminating source-level trace register

This register derives the **safe outcome already required** by the authoritative specification for each adversarial ordering and explicitly identifies the **missing executable contract**. These are documentation scenarios, not executed tests, typed schemas or proof that any installed CLI behaves safely. An unresolved outcome stays an implementation blocker; the listed owner is the existing owner, never a new state machine.

| ID | Initial observation | Existing owner(s) | Already required result | Open schema, trace or native proof |
| --- | --- | --- | --- | --- |
| F01 | Two Sends reuse an identity but bytes differ | Contribution/Storage | No overwrite and no duplicate contribution | Collision error and payload identity |
| F02 | Contribution committed; response/ACK lost | Contribution/Storage | Reconcile original Send ID; do not dispatch or save twice | Durable commit/outbox/result boundary |
| F03 | MCF duplicate arrives after later sequence | MCF/recipient | No repeat effect; record ordering gap | Sequence scope and gap release |
| F04 | Inbox/outbox commit is uncertain | MCF/Storage | Keep unknown until durable observation resolves it | Unknown-commit/replay transaction |
| F05 | Replayed dead letter may already have effected work | MCF/effect owner | New delivery cannot create duplicate material effect | Replay-to-original effect-ID mapping |
| F06 | Cross-project correlation or stale epoch | Policy/recipient | Refuse and preserve project isolation | Exact versioned error and audit event |
| F07 | Lease expiry, late response and reassign race | Task/DAG/Workspace | Reject stale effect; successor must use new fence | CAS/result/successor total ordering |
| F08 | Cancellation receipt but child still writes | Kernel/Gateway/Policy | Never report physical cessation or unsafe reuse | Descendant/service cessation proof |
| F09 | Revoked session with unresolved effect | Task/DAG/Gateway | Reconcile; block affected resumption while UNKNOWN | Owner-specific effect identity/timeout result |
| F10 | Handoff source not conclusively fenced | Orchestrator | ConfirmSourceFenced fails; hold persists | Typed command/guard/event transaction |
| F11 | Successor has only ACK and stale digest | Orchestrator/Context | Withhold READY_TO_RELEASE | Applied-baseline evidence schema |
| F12 | Missing required plan-application result | Orchestrator/Task/DAG | Withhold dependent lease | Barrier atomic release/replay |
| F13 | FULL round lacks mandatory critique at bound | Council Engine | Incomplete/BLOCKED; never cap-completed | Missing-obligation and seal events |
| F14 | Approval references stale point/wrong project | Decision/Requirement/Policy | Reject mismatched approval | Exact identity/scope/revocation grammar |
| F15 | Agent consensus/exit zero offered as approval/PASS | Decision/Validation | Reject that authority or certification claim | Objective oracle/result evidence |
| F16 | Malformed, duplicate or non-English response | Gateway/Contribution | No counterfeit participation or disallowed output | Parser, dedup and mixed-language error mapping |
| F17 | CLI native helper attempts forbidden read/write | Policy/Kernel | Prevent before effect or refuse whole profile | Proven per-CLI mediation/confinement |
| F18 | Root/reparse target changes after validation | Workspace/Policy | Refuse stale path/object authority | Identity and TOCTOU guard |
| F19 | Crash between publication mutations | Workspace/Storage | Journal/inspect; no false complete claim | Per-operation restart decision table |
| F20 | User-edited file matches neither journal digest | Workspace/Evidence | Preserve competing edit; no blind restore | Conflict/UNKNOWN disposition |
| F21 | Material effect remains physically UNKNOWN | Effect owner/Orchestrator | Operational BLOCKED with UNKNOWN evidence | Safe release/reconciliation predicate |
| F22 | Artifact differs from certified digest | Evidence/Validation | Invalidate stale evidence | Version-bound invalidation event |
| F23 | Build passes, runtime criterion untested | Validation/Evidence | No complete/PASS claim for runtime | Criterion-class evidence matrix |
| F24 | Test passes but does not test requirement | Validation/Evidence | Refuse unsupported adequacy claim | Independent oracle adequacy vectors |
| F25 | Stale or inconclusive criterion at delivery | Validation/Evidence | Refuse certification | Current per-criterion matrix transaction |
| F26 | Installer invoked on excluded Windows platform | Installer/Policy | Refuse before partial installation | Real platform observation and MSI evidence |
| F27 | Closure admission fenced; commit uncertain | Project/Storage | Keep admission fenced; reconcile commit | Project intent/CAS atomic protocol |
| F28 | Restart with pending approval and unsent draft | Contribution/UI/Storage | Protect references; recover draft, do not submit | Draft privacy/expiry/backup policy |
| F29 | Switch while old CLI isolation unproven | Project/Orchestrator | Keep old view/draft; candidate unbound | Isolation/effect cessation proof |
| F30 | Non-English stream headed for Chat/a11y | Gateway/UI/Contribution | Withhold output, English blocker; no reduced-agent reply | Deterministic language admission policy |

A complete fixture must record the original immutable input, exact project/root/epoch, current approved source records, expected version and lease, relevant byte/object digests, which physical effects are independently observed, the controlling typed command and owner, one committed/refused transaction plus events/outbox, precise Chat projection, and the permitted recovery action. Repeat each material case with duplicate delivery, stale agent/session, interrupted commit and competing user edit where relevant. **No scenario passes merely because the required safe outcome is written here.**

## Executable-contract acceptance template

For each owned command or material external effect, the versioned contract must state **all** of the following:

1. Source requirement, owner, operation, user authority and scope; stable project/epoch/attempt/session identities.
2. Typed command/query/event/error definitions with requiredness, validation, compatibility, idempotency and forbidden inputs.
3. Preconditions, expected version/fence, legal state transitions, atomic storage/owner/outbox boundaries and exact illegal-command results.
4. Physical effect admission proof and observable before/after identity; cancellation, stale/revoked actor behavior and `UNKNOWN` classification.
5. Deadline, heartbeat, bounded retry, queue and cancellation escalation with clock semantics and crash/restart reconciliation.
6. One unique result for each competing-command, duplicate, stale-context, lost-ACK, interrupted-effect and altered-file trace.
7. User-visible projection and adequacy-checked acceptance oracle bound to real evidence. An unexecuted fixture remains `NOT VERIFIED`.

**Done for one scoped slice** requires two independent trace readers deriving the same owner command, permit/deny result, durable effect and recovery from the same initial records; coverage of its participating machines and applicable B1–B10; and actual native proof for any admitted physical CLI effects. **Done for unrestricted whole-product build** requires all twelve machine contracts and every relevant open product choice settled. This is stricter than a schema stub and narrower than demanding an entire MSI before writing documents.

## Documentation readiness status

- **Completed now:** Q-01/Q-02 correction propagation on `main`; a dependency-ordered Z-01–Z-14 register; explicit four novel subcases and acceptance template. These are documentation artifacts only.
- **Not complete:** open product choices, typed schemas, full state tables, transaction protocols, B-contract algorithms, per-CLI safety profiles, actual Windows proofs and native application implementation.
- **No false closure:** retain open D/B/P0/P1 entries until their required evidence exists; do not label planned tests as passed or an advisory product proposal as approved.
