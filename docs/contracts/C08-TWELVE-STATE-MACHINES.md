# C08 — Twelve authoritative state-machine contracts

**Status: NORMATIVE DOCUMENTATION CONTRACT.** This pack completes the command, guard, transition, event, illegal-outcome and restart behavior for the twelve existing authoritative machines. It introduces no thirteenth machine. Machine-readable state, command and result values are registered in [`state-machines.schema.json`](./schemas/v1/state-machines.schema.json).

## Rules common to every machine

Every command carries `contract_version`, `command_id`, `operation_id`, `idempotency_key`, `project_id`, `expected_record_version`, current authority references, issued UTC time, and an active-time deadline. Project-scoped commands also carry the approved epoch. Task-, lease-, session- or artifact-scoped commands carry those exact identities.

One owner transaction performs the expected-version check, validates current authority and policy, writes the successor record/version, appends the domain event, and persists any outbound intent. Only then can a successful owner receipt be returned. An identical duplicate creates no mutation and returns a `DUPLICATE` linkage receipt with the exact original receipt ID and digest; the linked original remains the outcome record. The same idempotency key with different canonical payload bytes returns `IDEMPOTENCY_CONFLICT`. Stale versions, wrong projects/epochs, illegal states, expired authority and missing proof return typed refusals without mutation.

No SQLite commit proves a file/process/network effect. Commands that depend on physical effects first persist intent, then record observations in later owner transactions. An interrupted or contradictory physical observation is `UNKNOWN`; it may cause operational `BLOCKED` without being relabelled failure or success. Restart loads the last committed version, reconstructs holds/fences/outbox obligations, and reconciles before advancing.

`Pause` and `Stop` execute under their independently valid control identity. Pause preserves resumable state after fencing current effect admission. Stop revokes future admission and requests cancellation immediately; confirmed stopped presentation requires independent cessation evidence.

## 1. Project lifecycle — Project service

Lifecycle phase and operational condition are separate fields. The phase values are the approved lifecycle sequence; `PAUSED`, `STOPPED`, `BLOCKED`, and `RECOVERING` are conditions.

| Current phase/condition | Command and required guard | Next phase/condition and event | Refusal or recovery |
| --- | --- | --- | --- |
| No project identity | `BindProjectRoot`; user-selected canonical root passes identity, permission, alias/reparse and registration checks | Existing identity restored at its committed phase/closure, or new `PROJECT_CREATED`; `ProjectBound` | Invalid/unsafe root returns `PATH_IDENTITY_UNSAFE`; no partial project |
| Any open phase | `AdvanceProjectPhase`; approved requirements/decisions and predecessor exit criteria match expected epoch/version | Next declared phase; `ProjectPhaseAdvanced` | No skipping; unmet gate returns `PRECONDITION_FAILED` |
| Any open phase | `ReturnProjectPhase`; authorized material successor identifies invalidated affected work | Earlier applicable phase; epoch changes only through separate authorized Project command; `ProjectPhaseReturned` | Review request alone cannot advance epoch |
| Any open phase | `CommitProjectEpoch`; an authorized material successor requirement/decision names the current epoch and affected set, and affected leases/work are already fenced or invalidated | Same phase and condition with `approved_epoch + 1`; `ProjectEpochAdvanced`; dependent context and barriers become stale | A review request or candidate does not qualify; concurrent expected-epoch commands have one winner |
| Any open phase | `PauseProject`; current control authority | Same phase + `PAUSED`; `ProjectPaused` after affected effect admission is held | Existing material effects reconcile separately |
| `PAUSED` | `ResumeProject`; authority, Start, leases, context and safety revalidated | Prior phase + active condition; `ProjectResumed` | Missing proof remains `BLOCKED`/`RECOVERING` |
| Any open phase | `StopProject`; current control authority | Same phase + `STOPPED`; `ProjectStopRequested` | Cessation may remain `UNKNOWN`; no false stopped claim |
| `STOPPED` | `StartProjectSessions` is not a lifecycle transition | Phase unchanged; session commands proceed only after Start authorization | Start does not reopen, approve work or advance phase |
| Any open phase | `BeginCloseProject`; explicit version-bound Close action, no competing closure intent | Admission fence + closing intent; `ProjectClosureAdmissionStarted` | Duplicate returns original intent; competing payload conflicts |
| Closing intent | `CommitProjectClosure`; quiescence rechecked with no leases, effect-capable activity, pending publication, unresolved material `UNKNOWN`, or in-flight council round | Durable `ProjectClosure`; `ProjectClosed`; phase retained for history | Unmet guard returns `REFUSED` without mutation and retains the fence; uncertain commit reconciles before either branch |
| Closing intent after known closure refusal | `AbortProjectClosure`; exact refusal receipt, unchanged intent/version and current blocker set are bound | Closing intent superseded and admission fence removed; `ProjectClosureAborted` | This committed transition, rather than the refusal, reopens admission; changed intent/version must reconcile first |
| Closed | `ReopenProject`; explicit version-bound action and current retention state | Closure superseded, admission restored, retained phase + `STOPPED`; `ProjectReopened` | Retired history is disclosed, never reconstructed |
| Any | `RecordProjectBlock`/`BeginProjectRecovery` | Condition `BLOCKED` or `RECOVERING`; source-linked event | Does not rewrite phase or physical outcome |

Project switching changes the active UI binding through Project service but does not alter either project's lifecycle phase, closure, Start permission, sessions or histories. An unsafe candidate remains unbound while the current project/view/draft stays active.

## 2. Agent session — Agent Gateway

States: `ABSENT`, `STARTING`, `IDLE`, `BUSY`, `PAUSING`, `PAUSED`, `STOPPING`, `STOPPED`, `FAILED`, `UNKNOWN`, `RETIRED`.

| Current | Command and guard | Next/event | Refusal or recovery |
| --- | --- | --- | --- |
| `ABSENT`/known `FAILED`/`STOPPED` | `StartAgentSession`; current project-run Start permission, selected executable pin, supported probe, confinement profile and no unresolved prior effect | `STARTING`; `AgentSessionStartRequested`; Kernel launch intent follows | Unknown previous launch/Stop blocks; healthy/in-flight duplicate returns current attempt |
| `STARTING` | `RecordSessionStarted`; exact process/session identity and native handshake observed | `IDLE`; `AgentSessionStarted` | Deadline with no conclusive observation -> `UNKNOWN`, not `FAILED` |
| `IDLE` | `AssignSessionOperation`; current operation authority/context and compatible session | `BUSY`; `AgentSessionBusy` | Stale project/epoch/context or Stop hold refused |
| `BUSY` | `RecordSessionIdle`; assigned operation ended and attributable effects reconciled | `IDLE`; `AgentSessionIdle` | Unresolved effect -> `UNKNOWN`/blocked operation |
| `IDLE`/`BUSY` | `PauseAgentSession` | `PAUSING`; `AgentSessionPauseRequested` | New operation admission stops immediately |
| `PAUSING` | `ConfirmSessionPaused`; effect admission held and current operation safely suspended/fenced | `PAUSED`; `AgentSessionPaused` | Unknown effect stays `UNKNOWN` or handoff hold |
| `PAUSED` | `ResumeAgentSession`; Start permission and all identities/context revalidated | `IDLE`; `AgentSessionResumed` | Relaunch requires fresh Start; no inherited permission |
| Any nonretired | `StopAgentSession` | `STOPPING`; `AgentSessionStopRequested` | Idempotent duplicate returns same receipt |
| `STOPPING` | `ConfirmSessionStopped`; process tree plus attributable helpers/services ceased or effectively revoked | `STOPPED`; `AgentSessionStopped` | 20-second bound with missing proof -> `UNKNOWN` plus affected `BLOCKED` |
| Any active state | `RecordSessionFailure`; observed known failure and effects reconciled | `FAILED`; `AgentSessionFailed` | Unknown cause/effect uses `UNKNOWN` |
| Any | `RecordSessionUnknown` | `UNKNOWN`; `AgentSessionOutcomeUnknown` | No restart/reuse until reconciliation or Handoff #9 |
| `STOPPED`/`FAILED`/`UNKNOWN` | `RetireAgentSession`; no active leases/traffic/effects and provenance retained | `RETIRED`; `AgentSessionRetired` | Cannot erase unresolved effect |

## 3. Message delivery — MCF-v2

States: `QUEUED`, `SENDING`, `RECEIVED`, `ACKNOWLEDGED`, `RETRY_WAIT`, `DEAD_LETTERED`, `EXPIRED_DISPOSABLE`, `BLOCKED`.

| Current | Command and guard | Next/event | Refusal or recovery |
| --- | --- | --- | --- |
| Absent | `EnqueueEnvelope`; valid schema, authority, class, project/epoch and operation identity; owner outbox intent already committed when causal | `QUEUED`; `EnvelopeQueued` | Malformed/oversized/unauthorized returns typed rejection |
| `QUEUED`/`RETRY_WAIT` | `BeginDelivery`; due, queue capacity and ordering prerequisites valid | `SENDING`; `EnvelopeDeliveryStarted` | Gap/blocked prerequisite holds without skipping |
| `SENDING` | `PersistInboxReceipt`; receiver validates and atomically stores inbox/dedup result before ACK | `RECEIVED`; `EnvelopeReceived` | Same key/different bytes -> conflict; invalid receiver scope rejected |
| `RECEIVED` | `AcknowledgeReceipt`; durable inbox receipt exists | `ACKNOWLEDGED`; `EnvelopeAcknowledged` | ACK never means application or success |
| `SENDING` | `RecordDeliveryTimeout`; no receipt by 15 active seconds and attempt allowance remains | `RETRY_WAIT`; `EnvelopeRetryScheduled` with 2/5/15-second schedule | Attempt budget persists across restart/sleep |
| `SENDING`/`RETRY_WAIT` | `ExhaustDelivery`; material class | `DEAD_LETTERED` and affected operation `BLOCKED`; `EnvelopeDeadLettered` | Obligation retained; no silent expiry |
| `SENDING`/`RETRY_WAIT` | `ExpireDisposable`; class is declared heartbeat/queue housekeeping and age >= 5 minutes | `EXPIRED_DISPOSABLE`; `DisposableEnvelopeExpired` | Cannot apply to approval, task, council, context, publication or recovery traffic |
| `DEAD_LETTERED` | `ReplayEnvelope`; explicit recovery, original effect identity and fresh current authority | New envelope linked to original; original remains terminal | Replay is delivery, never fresh material authority |

## 4. Context — Context Synchronizer

States: `REQUESTED`, `BUILDING`, `PARTIAL`, `READY`, `APPLYING`, `APPLIED`, `STALE`, `FAILED`, `BLOCKED`.

| Current | Command and guard | Next/event | Refusal or recovery |
| --- | --- | --- | --- |
| Absent | `RequestContextSnapshot`; authorized purpose, recipients, project/epoch/graph and source roots | `REQUESTED`; `ContextRequested` | Cross-project or overbroad source refused |
| `REQUESTED` | `BeginContextBuild`; dependency policy and byte budget bound | `BUILDING`; `ContextBuildStarted` | No unbounded repository scan |
| `BUILDING` | `SealContextSnapshot`; permitted exact byte manifest, sources, filters and `mayasaba-jcs-1` digest complete | `READY`; `ContextSnapshotReady` | Incomplete or failed input uses its separate command below |
| `BUILDING` | `RecordPartialContext`; retrieval bound exhausted with declared missing coverage | `PARTIAL`; `ContextPartial` | Cannot claim complete/current |
| `BUILDING` | `RecordContextFailure`; known integrity/read/build failure with diagnostics | `FAILED`; `ContextFailed` | Unknown physical outcome uses `BlockContext` and preserves `UNKNOWN` |
| `READY`/`PARTIAL` | `BeginContextApplication`; exact recipient session, purpose and accepted sufficiency | `APPLYING`; `ContextApplicationStarted` | `PARTIAL` cannot satisfy a complete-context gate |
| `APPLYING` | `ConfirmContextApplied`; adapter/controller verifies exact digest and plan application, distinct from ACK | `APPLIED`; `ContextApplied` | Agent prose/ACK is insufficient |
| Any nonterminal/current | `InvalidateContext`; source/epoch/graph/dependency/executable/session change affects content | `STALE`; `ContextInvalidated` with dependency edges | Preserve unaffected snapshot history |
| `PARTIAL`/`FAILED`/`STALE` | `RebuildContext`; current authority and bounded widening rule | `BUILDING`; linked successor snapshot | No in-place rewrite |
| Any | `BlockContext`; missing isolation or fresh-independence proof is recorded | `BLOCKED`; `ContextBlocked` | Prior peer disclosure cannot count as independent proposal context |

## 5. Task — Task/DAG Engine

States: `PROPOSED`, `AUTHORIZED`, `WAITING_DEPENDENCY`, `READY`, `RUNNING`, `VERIFYING`, `COMPLETED`, `FAILED`, `BLOCKED`, `CANCELLED`, `INVALIDATED`.

| Current | Command and guard | Next/event | Refusal or recovery |
| --- | --- | --- | --- |
| Absent | `CreateTask`; source request, requirements/decisions, scope, outputs, criteria, dependencies and assigned capability specified | `PROPOSED`; `TaskProposed` | Proposal grants no execution |
| `PROPOSED` | `AuthorizeTask`; valid approved plan, current epoch/graph, adequate criteria and no conflict | `AUTHORIZED`; `TaskAuthorized` | Missing approval/oracle/conflict -> refusal/block |
| `AUTHORIZED` | `EvaluateTaskDependencies` | `READY` if satisfied, else `WAITING_DEPENDENCY`; corresponding event | Unknown affected edge holds conservatively |
| `READY` | `StartTaskAttempt`; current barrier/context and lease admission succeeds | `RUNNING`; `TaskAttemptStarted` | Task state and lease transaction are causally linked, not same owner mutation |
| `RUNNING` | `BeginTaskVerification`; attempt output complete and physical effects reconciled | `VERIFYING`; `TaskVerificationStarted` | Agent completion claim is insufficient |
| `VERIFYING` | `CompleteTask`; every blocking criterion current `PASS` or justified `NOT_APPLICABLE` | `COMPLETED`; `TaskCompleted` | Any blocking other verdict prevents completion |
| `RUNNING`/`VERIFYING` | `RecordTaskFailure`; known failed attempt and retry policy | `FAILED`; `TaskFailed` | Repair may create successor attempt; task identity remains |
| Any nonterminal | `BlockTask`; missing authority/evidence/capability or unresolved effect | `BLOCKED`; `TaskBlocked` | Unrelated tasks continue |
| Any nonterminal | `CancelTask`; explicit authority and affected effects fenced/reconciled | `CANCELLED`; `TaskCancelled` | Unknown effect remains linked and may block closure |
| Any nonterminal/completed | `InvalidateTask`; authorized successor epoch/plan/evidence invalidates this result | `INVALIDATED`; `TaskInvalidated` | Preserve unaffected outputs and history |

## 6. Lease — Task/DAG Engine

States: `REQUESTED`, `ACTIVE`, `EXPIRING`, `EXPIRED`, `REVOKING`, `REVOKED`, `RELEASED`, `UNKNOWN`, `BLOCKED`.

| Current | Command and guard | Next/event | Refusal or recovery |
| --- | --- | --- | --- |
| Absent | `RequestLease`; task ready, attempt distinct, project/epoch/graph/context current, agent capable, conflict locks clear | `REQUESTED`; `LeaseRequested` | Missing confinement or barrier refuses admission |
| `REQUESTED` | `GrantLease`; single-winner expected version and new fencing version | `ACTIVE`; `LeaseGranted` | Concurrent loser receives `STALE_VERSION` |
| `ACTIVE` | `RenewLease`; before bound, current progress/effect evidence and authority | `ACTIVE` with incremented version/bound; `LeaseRenewed` | No silent indefinite renewal; task max applies |
| `ACTIVE` | `BeginLeaseExpiry` | `EXPIRING`; `LeaseExpiryStarted` | Time alone does not stop effects |
| `EXPIRING` | `ConfirmLeaseExpired`; admission revoked and attributable effects ceased/reconciled | `EXPIRED`; `LeaseExpired` | Missing physical proof -> `UNKNOWN`/`BLOCKED` |
| `ACTIVE`/`EXPIRING` | `RevokeLease`; Stop, invalidation, handoff or policy cause | `REVOKING`; `LeaseRevocationRequested`; increment fence immediately | Stale actor cannot pass new fence |
| `REVOKING` | `ConfirmLeaseRevoked`; effects ceased or effective pre-effect revocation proven | `REVOKED`; `LeaseRevoked` | Unknown physical effect remains `UNKNOWN` |
| `ACTIVE` | `ReleaseLease`; attempt ended and all material outcomes known | `RELEASED`; `LeaseReleased` | Cannot release away unresolved publication/process effect |
| Any active branch | `RecordLeaseUnknown`/`BlockLease` | `UNKNOWN` or `BLOCKED`; linked evidence | Successor lease cannot overlap unsafe source capability |

## 7. Council round — Council Engine

States: `ALLOCATED`, `COLLECTING_PROPOSALS`, `PROPOSALS_FROZEN`, `COLLECTING_CRITIQUES`, `COLLECTING_REBUTTALS`, `SYNTHESIZING`, `AWAITING_SYNTHESIS_REVIEWS`, `COMPLETE`, `BLOCKED`, `PAUSED`.

| Current | Command and guard | Next/event | Refusal or recovery |
| --- | --- | --- | --- |
| Absent | `CreateCouncilPoint`; D-04 trigger key current and no nonterminal duplicate | Point plus round `ALLOCATED`; atomically allocate project-local `d` and roles; `CouncilPointCreated` | Nonmaterial trigger refused; duplicate returns existing point |
| `ALLOCATED` | `BeginProposalCollection`; synchronized fresh independent context for all three | `COLLECTING_PROPOSALS`; `CouncilProposalCollectionStarted` | Prior peer disclosure contaminating baseline blocks independence |
| `COLLECTING_PROPOSALS` | `FreezeProposals`; exactly one current independent proposal from each CLI | `PROPOSALS_FROZEN`; `CouncilProposalsFrozen` | Missing/duplicate/stale proposal remains incomplete |
| `PROPOSALS_FROZEN` | `BeginCritiques` | `COLLECTING_CRITIQUES`; six directed assignments persisted; `CouncilCritiquesAssigned` | Self-critique or omitted edge invalid |
| `COLLECTING_CRITIQUES` | `CloseCritiques`; six distinct actual authored, current, frozen-target critiques with C11 substantive challenge/no-defect checks | `COLLECTING_REBUTTALS`; `CouncilCritiquesCompleted` | ACK, hollow agreement, self/duplicate edge or stale target leaves a required slot incomplete |
| `COLLECTING_REBUTTALS` | `CloseRebuttals`; required rebuttals/revisions linked immutably | `SYNTHESIZING`; `CouncilRebuttalsCompleted` | Missing required rebuttal stays incomplete |
| `SYNTHESIZING` | `SubmitSynthesis`; assigned chair, exact frozen inputs and complete source-checked C11 six-slot claim-to-critique coverage with immutable revision digest | `AWAITING_SYNTHESIS_REVIEWS`; `CouncilSynthesisSubmitted` | Missing/false coverage withholds synthesis; changed synthesis/coverage digest invalidates reviews |
| `AWAITING_SYNTHESIS_REVIEWS` | `SealCouncilRound`; both nonchair reviews of exact synthesis/coverage revision, complete mandatory participation and current C11 evidence/quality assessment | D-03 outcome; normally `COMPLETE`; `CouncilRoundSealed` | Preserve first-applicable ten-priority D-03 result; failed quality/evidence cannot certify positive synthesis/convergence |
| Any nonterminal | `PauseCouncilRound` | `PAUSED`; `CouncilRoundPaused` | No positive outcome |
| `PAUSED` | `ResumeCouncilRound`; same baseline or authorized successor context with invalidation | Earliest incomplete phase; `CouncilRoundResumed` | Five-round budget never resets |
| Any nonterminal | `BlockCouncilRound`; bound expires with mandatory contribution missing | `BLOCKED`; `CouncilRoundBlockedIncomplete` | Never `cap reached` |

Only Decision service can bind an eligible result after authority, evidence, policy and user gates. Comparable-position equality uses sorted canonical position IDs, normative claims, dispositions, affected authority IDs and evidence digests; narrative wording and confidence are excluded. Two consecutive complete rounds must have identical canonical surviving-position digests unless a fully reviewed synthesis resolves earlier.


**C11 quality guards refine existing commands, not machine states.** Check the original six directed edges H→K, H→C, K→H, K→C, C→H, C→K; actual authored substance; accurate frozen source claim/critique/revision links; adequate independent observations and inference premises; justified challenge dispositions; common-assumption tests or documented bounds; fair comparisons and current binding-decision validity. A substantive critique failure is incomplete participation; material unresolved fact/choice/evidence maps to the original D-03 priorities. Exact two-peer synthesis/coverage digest matching is mandatory when evaluated. No extra critique, event, owner, positive outcome, round or authorization is introduced. [C11](./C11-COUNCIL-DECISION-QUALITY.md) and CQ01–CQ16 are prospective semantic oracles.

## 8. Synchronization barrier — Orchestrator

States: `CREATED`, `WAITING`, `SATISFIED`, `INVALIDATED`, `BLOCKED`, `RELEASED`.

| Current | Command and guard | Next/event | Refusal or recovery |
| --- | --- | --- | --- |
| Absent | `CreateSynchronizationBarrier`; exact project/epoch/graph/snapshot/plan and required participant set | `CREATED`; `SynchronizationBarrierCreated` | Participant set is exactly the applicable three-CLI set |
| `CREATED` | `OpenSynchronizationBarrier` | `WAITING`; `SynchronizationBarrierOpened` | — |
| `WAITING` | `RecordBarrierApplication`; Context owner supplies exact session/digest application receipt | `WAITING`; participant receipt appended | ACK or prose understanding rejected |
| `WAITING` | `SatisfySynchronizationBarrier`; all required current receipts match | `SATISFIED`; `SynchronizationBarrierSatisfied` | One stale/missing receipt blocks release |
| `SATISFIED` | `ReleaseSynchronizationBarrier`; dependent admission rechecks current version | `RELEASED`; `SynchronizationBarrierReleased` | Release occurs once; duplicate returns receipt |
| Any nonterminal/satisfied | `InvalidateSynchronizationBarrier`; epoch/graph/context/session/plan changed | `INVALIDATED`; `SynchronizationBarrierInvalidated` | Dependent leases not yet granted; affected leases fenced if change follows release |
| Any nonterminal | `BlockSynchronizationBarrier`; bound/proof failure is recorded | `BLOCKED`; `SynchronizationBarrierBlocked` | Chat and unrelated work remain available |

## 9. Handoff — Orchestrator

States and transitions are exactly `FENCING_SOURCE`, `AWAITING_SUCCESSOR`, `SYNCHRONIZING`, `READY_TO_RELEASE`, `COMPLETED`, and `BLOCKED`, using `BeginSessionHandoff`, `ConfirmSourceFenced`, `AttachSuccessor`, `ConfirmSuccessorApplied`, `CompleteSessionHandoff`, `BlockSessionHandoff`, and `ResumeSessionHandoff` as defined in the authoritative Handoff #9 table. All use the common expected-version/idempotency/error rules above.

The source fence requires lease fencing plus verified cessation or effective revocation of attributable effect-capable children, tools and services. The successor must be distinct, behaviorally probed and project/CLI-scoped. Application requires the exact current epoch, graph revision, snapshot digest and plan plus any affected all-three barrier. `COMPLETED` atomically releases only the Orchestrator hold. Any missing observation or deadline produces `BLOCKED` while retaining the hold. Stop acts immediately but does not prove the source fence.

## 10. Execution — Local Execution Kernel

States: `REQUESTED`, `ADMITTED`, `STARTING`, `RUNNING`, `CANCELLING`, `EXITED`, `FAILED_TO_START`, `UNKNOWN`, `BLOCKED`.

| Current | Command and guard | Next/event | Refusal or recovery |
| --- | --- | --- | --- |
| Absent | `RequestExecution`; valid task/lease fence, command capability, paths, environment and proven confinement profile | `REQUESTED`; `ExecutionRequested` | Missing pre-effect denial returns `CONFINEMENT_UNPROVEN` |
| `REQUESTED` | `AdmitExecution`; Policy permit, expected versions and resource bounds current | `ADMITTED`; `ExecutionAdmitted` | Permit is not launch evidence |
| `ADMITTED` | `BeginExecution`; controlled child created suspended and launch intent committed before the physical launch boundary | `STARTING`; `ExecutionStartRequested` | Commit is not launch success |
| `STARTING` | `RecordExecutionStartFailure`; known creation/Job assignment/resume failure and proof that no child remains effect-capable | `FAILED_TO_START`; `ExecutionFailedToStart` | Uncertain launch uses `RecordExecutionUnknown` |
| `STARTING` | `ConfirmExecutionRunning`; exact process identity/handles/streams observed | `RUNNING`; `ExecutionRunning` | Uncertain launch -> `UNKNOWN` |
| `RUNNING` | `RequestExecutionCancellation`; Stop/lease/policy/task cause | `CANCELLING`; `ExecutionCancellationRequested` | New material effects denied immediately |
| `RUNNING` | `RecordExecutionExit`; process exit observed and attributable effects reconciled | `EXITED`; `ExecutionExited` with exit value | Exit does not prove task success |
| `CANCELLING` | `ConfirmExecutionCancelled`; process tree and attributable helpers/services ceased or effectively revoked | `EXITED`; `ExecutionCancelled` | Missing proof after bound -> `UNKNOWN`/`BLOCKED` |
| Any | `RecordExecutionUnknown` | `UNKNOWN`; `ExecutionOutcomeUnknown` | No blind retry/rollback/certification |
| `UNKNOWN` | `ReconcileExecution`; bounded independent observations | `EXITED` with `ExecutionExited`, `FAILED_TO_START` with `ExecutionFailedToStart`, or remain `UNKNOWN` + `BLOCKED` | Outcome never guessed |

## 11. Validation — Validation/Repair Engine

States: `PENDING`, `RUNNING`, `PASS`, `FAIL`, `INCONCLUSIVE`, `BLOCKED`, `NOT_APPLICABLE`, `STALE`.

| Current | Command and guard | Next/event | Refusal or recovery |
| --- | --- | --- | --- |
| Absent | `CreateValidationCheck`; criterion, claim class, applicability and D-15 oracle specified | `PENDING`; `ValidationCheckCreated` | Missing adequate oracle blocks criterion |
| `PENDING` | `RunValidationCheck`; exact artifact/environment/check identity and authority | `RUNNING`; `ValidationStarted` | Stale artifact refused |
| `RUNNING` | `RecordValidationResult`; independently observed evidence satisfies one allowed verdict | Verdict state; `ValidationResultRecorded` | Agent narrative/exit/build alone cannot elevate |
| Any terminal | `InvalidateValidation`; artifact/environment/check/requirement changed | `STALE`; `ValidationInvalidated` | Stale PASS cannot certify |
| `STALE`/`FAIL`/`INCONCLUSIVE` | `RerunValidation`; current artifact and adequate oracle | `RUNNING`; linked successor execution | Preserve prior result history |

`NOT_APPLICABLE` requires exact source authority and applicability proof. An unresolved conflict among approved criteria blocks the affected acceptance matrix and cannot be repaired by weakening a criterion.

## 12. Repair — Validation/Repair Engine

States: `DIAGNOSED`, `PLANNED`, `AUTHORIZED`, `RUNNING`, `REVALIDATING`, `SUCCEEDED`, `FAILED`, `BLOCKED`, `CANCELLED`.

| Current | Command and guard | Next/event | Refusal or recovery |
| --- | --- | --- | --- |
| Failed/inconclusive validation | `CreateFailureDiagnostic`; exact criterion, evidence, signature, classification and falsifiable hypothesis | `DIAGNOSED`; `FailureDiagnosed` | Unknown cause remains explicit |
| `DIAGNOSED` | `PlanRepair`; bounded change scope, task/lease needs, oracle and regressions | `PLANNED`; `RepairPlanned` | Cannot edit criterion/oracle to claim success |
| `PLANNED` | `AuthorizeRepair`; existing approved scope or separately approved material successor, attempt allowance current | `AUTHORIZED`; `RepairAuthorized` | New material choice invokes D-04 before authorization |
| `AUTHORIZED` | `StartRepairAttempt`; distinct task attempt/lease and confinement | `RUNNING`; `RepairAttemptStarted` | No lease reuse or direct root write |
| `RUNNING` | `BeginRepairRevalidation`; physical outcomes known and candidate artifact identified | `REVALIDATING`; `RepairRevalidationStarted` | Unknown publication/execution blocks |
| `REVALIDATING` | `CompleteRepair`; failed criterion and relevant regressions all current PASS | `SUCCEEDED`; `RepairSucceeded` | One pass cannot hide regression failure |
| `REVALIDATING` | `RecordRepairFailure`; known result, diagnostics preserved | `FAILED`; `RepairFailed` | Same signature/no objective improvement counts toward three-attempt cap |
| `FAILED` | `AuthorizeRepair` with new evidence/hypothesis and allowance | `AUTHORIZED`; linked next attempt | Relabeling identical strategy cannot reset count |
| Any nonterminal/failed | `BlockRepair`; cap exhausted or unresolved required effect | `BLOCKED`; `RepairBlocked` | Self-improvement may propose, never self-authorize |
| Any nonterminal/failed | `CancelRepair`; authorized Stop or invalidation prevents continuation and no unresolved material effect is discarded | `CANCELLED`; `RepairCancelled` | Cancellation cannot relabel an unknown physical outcome |

## Cross-machine release order

For material execution, the minimum order is: Project open/current epoch → authorized WorkRequest/Task → current Context application and required Synchronization Barrier → active Lease fence → Policy permit and Execution admission → observed attempt output → Validation → guarded Workspace publication/reconciliation → current post-publication Validation → task completion. Handoff, Stop, closure, invalidation or an `UNKNOWN` physical outcome can hold this chain at the owning boundary. No aggregate status bypasses an owner command.

## Documentation acceptance

All commands, events, states, results and common errors in this document must exist in the v1 schemas. Fixtures must cover one successful path, one stale-version refusal, one identical duplicate, one altered duplicate conflict, one restart, and every relevant `UNKNOWN`/Stop/concurrent-user-edit branch. Native proof remains an implementation-stage gate.
