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
