# C03 — Handoff #9, Start/Stop and bounded recovery traces

**Status: NORMATIVE TRACE CONTRACT; WINDOWS CLI PROOF PENDING.** Source: [authoritative Handoff machine #9](../Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md#handoff-machine-9-replacement-of-a-project-scoped-cli-session), approved run-scoped Start rules and bounded self-recovery. [C06](./C06-BINDING-PRODUCT-DECISIONS.md) fixes operational bounds; [C08](./C08-TWELVE-STATE-MACHINES.md) fixes Handoff transitions; [C09](./C09-SELECTION-TRANSACTION-RECOVERY.md) fixes guarded transaction and recovery selection. This does not add a thirteenth machine or independent controller.

## Ownership stays separated

- **Orchestrator** owns handoff identity, persisted scheduling hold, phase and its guarded release.
- **Task/DAG** fences existing attempts/leases; any successor task requires an independently admitted new attempt and new lease.
- **Agent Gateway** observes real old/successor project-scoped session identity and health.
- **Execution Kernel and Policy** prove effective external child/tool/shared-service cessation or revocation, rather than treating process exit or ACK as proof.
- **Context Synchronizer and existing barrier** prove exact current epoch, graph, digest and plan *application*, not merely delivery.
- **Workspace/SQLite Storage** preserve file journal, durable uncertain outcomes and crash-recovery truth.
- **Project and user** retain root authority, project closure and explicit Start/Stop permissions.

## Existing seven guarded Handoff transitions

| State | Command | Required observation | Permitted next |
| --- | --- | --- | --- |
| Absent | BeginSessionHandoff | Current project/CLI/source/replacement authority; hold durably recorded before dependent actions | FENCING_SOURCE |
| FENCING_SOURCE | ConfirmSourceFenced | Task/DAG fences all affected leases; Gateway source retired; attributable *material* effects physically ceased/revoked and no affected UNKNOWN remains | AWAITING_SUCCESSOR |
| AWAITING_SUCCESSOR | AttachSuccessor | Distinct, same project and required CLI identity; native probe and source fence verified | SYNCHRONIZING |
| SYNCHRONIZING | ConfirmSuccessorApplied | Current epoch, graph, snapshot digest, plan applied; relevant all-three barrier re-satisfied | READY_TO_RELEASE |
| READY_TO_RELEASE | CompleteSessionHandoff | Guarded recheck source fencing, new session health, policy, context, barrier, lease state | COMPLETED; release **only this** Orchestrator hold atomically with completion |
| Any nonterminal | BlockSessionHandoff | Defect, unsafe uncertainty, expired bound, lost authority | BLOCKED; preserve hold/phase/recovery |
| BLOCKED | ResumeSessionHandoff | Independent current missing proof; authority rechecked | Earliest phase still needing proof, never direct completed |

Only one replacement for an identity is active. Same-key duplicate Begin returns existing handoff; a conflicting replacement intent for that project/CLI is refused. A replacement session never inherits or retargets the source lease. An authorized Pause/Stop acts immediately, without implying source effect cessation.

## Adversarial case matrix

| Case | Observed situation | Required safe action | Implementation evidence/fixture |
| --- | --- | --- | --- |
| H01 | Duplicate Begin with identical key | Return same handoff and hold, not second replacement | Idempotent typed result |
| H02 | Competing different replacement for same project/CLI | Conflict, no parallel hold transfer | Admission transaction |
| H03 | Lease fenced but old helper remains effect-capable | Refuse ConfirmSourceFenced, hold, eventually BLOCKED | Native effective revocation proof |
| H04 | Old parent exited, private shared service not observed | Do not infer effect cessation | Gateway/Kernel shared-service attribution |
| H05 | Publication journal physically UNKNOWN | Refuse release until independent reconciliation | B6 per-file hash/identity table |
| H06 | Successor ACK received but wrong context/graph | Do not mark applied/READY_TO_RELEASE | Exact context-application proof |
| H07 | Successor becomes unhealthy after synchronization | Revalidate, withhold release, recover/block | Guarded current-health race |
| H08 | Completion commit may have succeeded, ACK lost | Reconcile same handoff ID; never release again blindly | Storage/outbox result semantics |
| H09 | Explicit Stop issued during replacement | Act immediately; retain actual hold and unknown effects | Typed Stop/handoff precedence |
| H10 | App relaunch during handoff with unresolved effects | Restore durable truth and hold; require **fresh user Start** for renewed CLI sessions | Restart and run identity |
| H11 | A → B → A in one application run | B gets no A permission; reuse A only if still isolated, authorized and healthy | D-09/D-10 compatibility evidence |
| H12 | Switch to B, old A shared-service isolation unknown | Keep A project view/draft, B unbound candidate; bounded reconcile/block | Switch proof predicate |
| H13 | Existing project is CLOSED | No session/handoff can bypass explicit Reopen Project | Project closure admission |
| H14 | Old source sends late result after fence | Preserve provenance, reject as current successor/task result | Stale-result typed rejection |

## Recovery, repair and improvement separation

**Self-recovery** may resume *only already approved* behavior if physical effects, leases, context, Start/Stop and current permission are independently known safe. UNKNOWN is never automatically failure or success and can coexist with operational BLOCKED. Relaunch cannot reset attempt budget, silently restart sessions, or double-dispatch material work. Failed acceptance after recovery follows bounded **Validation/Repair** with falsifiable hypothesis and regression checks. Repeated system failures may generate only **user-governed self-improvement proposals**, not silent replacement of Mayasaba's executable, authority or policies.

## Implementation-readiness boundary

Handoff commands use the C07 wrapper and enabled-slice payload rule; C08 fixes legal phases, guards and results; C09 fixes transaction/replay, successor eligibility and B6 recovery; C06 fixes deadlines and suspend/restart behavior. The implementation must still supply the command-specific payload schema and prove native process/service confinement, cessation, Start/Stop observation and publication recovery under C10. No Windows runtime proof is claimed.
