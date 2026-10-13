# C09 — Selection, transaction, delivery and recovery contracts

**Status: NORMATIVE DOCUMENTATION CONTRACT.** This pack fixes B1–B10, owner transaction boundaries, MCF delivery classes, publication recovery and restart behavior. It describes implementation obligations; it does not claim that SQLite, Windows files, CLIs or MSI behavior have been tested.

## Owner transaction protocol

Every accepted owner command uses one SQLite write transaction through SQLite Storage:

1. Validate the v1 schema, canonical bytes and parser bounds before the transaction.
2. Read the owner record by exact project/record identity and compare `expected_record_version`.
3. Validate user/source authority, project epoch, policy, operation/idempotency identity and state guard.
4. If the idempotency key already exists and canonical command bytes match, perform no mutation and return a `DUPLICATE` linkage receipt containing the exact original receipt ID and digest; consumers retrieve and validate that linked original for its outcome. Different canonical bytes return `IDEMPOTENCY_CONFLICT`.
5. Write the successor owner record with `record_version + 1`, immutable predecessor reference and command identity.
6. Append the domain event and per-project integrity-chain entry.
7. Persist each causal outbox envelope in the same transaction.
8. Persist the command receipt with result kind and resulting record/event identities.
9. Commit. Only a confirmed commit may return `COMMITTED`. A lost result is queried by operation/idempotency identity; the command is not reinterpreted as new.

No cross-owner distributed transaction is invented. A causal receiving owner executes its own transaction after durable inbox admission. External process/file/network effects use durable intent before effect and a later observation transaction after effect. If either commit or physical outcome cannot be established, record `UNKNOWN`, preserve intent and block unsafe repetition.

One application instance owns the store through an OS-visible per-user store lock plus a SQLite ownership record containing instance ID, process identity, boot/session observation and heartbeat. A second instance opens a read-only explanatory result or activates the existing UI; it cannot create a second writer, lease reality or scheduler. Abandonment requires proof that the recorded owner process is gone, SQLite recovery/integrity succeeds, and all external effects are reconciled before a new ownership epoch is committed.

## B1 — State-transition guard

Default: atomic expected-version compare-and-swap inside the owner transaction above. The only eligible equivalent is a SQLite uniqueness/constraint or serialized single-writer operation that proves the same single-winner, stale-rejection, crash-consistency and receipt behavior in the B1 fixture set. SQLite Storage selects the mechanism per record family before migration; the schema registry records it. Failure or unproved equivalence returns `STALE_VERSION`/`GUARD_UNPROVEN` without mutation. No runtime fallback changes the guard silently.

## B2 — Backup boundary

Default: SQLite online backup coordinated with a frozen managed-byte reference manifest. Owning services briefly fence new reference-set mutations, Storage captures the database snapshot and manifest transaction, then each referenced immutable byte is copied or deduplicated by its recorded SHA-256. The backup is `VERIFIED` only after a clean restore to an isolated location, SQLite integrity checks, schema migration check, manifest completeness and byte digest verification.

An eligible alternative must produce the same database/reference point and restore observations; a database-only copy is ineligible. Storage binds the selected protocol before backups ship. A failed backup keeps the previous verified set and blocks migrations requiring a fresh backup.

## B3 — SQLite journal and durability mode

The store is supported only on a verified local fixed or local user volume with working file locking, atomic replacement observations and no remote/network/cloud-synchronized semantics. Default configuration is WAL, `synchronous=FULL`, a 5-second bounded busy policy, passive checkpoints after 1,000 WAL pages or 60 seconds, and a truncate checkpoint during clean idle shutdown after readers drain within 30 seconds.

If local tests show WAL ineligible, the declared fallback is rollback journal `DELETE` with `synchronous=FULL` and the same busy bound. The effective PRAGMA values are read back and recorded per store open. If neither mode passes crash/restart/integrity fixtures, authoritative writes are refused. Journal changes require the store closed to other writers, a verified backup, migration intent and recovery test; they never happen as an unrecorded fallback.

## B4 — Canonical bytes

Use `mayasaba-jcs-1` exactly as bound in C06 and the v1 schema README. SQLite Storage serializes authoritative contract objects; Evidence Engine independently verifies golden vectors before the first integrity-chain/hash write. A profile change is a material persistent-compatibility decision requiring D-04, a new profile ID and explicit migration; old hashes remain verified under their original profile.

## B5 — Delivery class, retry and expiry

| Class | Examples | Ordering scope | Exhaustion |
| --- | --- | --- | --- |
| `EMERGENCY_CONTROL` | Pause/Stop notification after persisted contribution; cancellation/fence signal | Project + control operation | Dead-letter immediately after configured attempts; control itself already acts through application boundary; affected notification remains visible |
| `SYNCHRONIZATION` | Context/plan application request/result, barrier event, handoff proof | Project + epoch + barrier/handoff | Dead-letter and block dependent release |
| `TASK_CONTROL` | Assignment, lease/fence, attempt result | Project + task + attempt | Dead-letter and block affected task/lease |
| `RECOVERY` | Reconciliation observation/request | Project + recovery operation | Dead-letter and keep original outcome `UNKNOWN`/affected `BLOCKED` |
| `COUNCIL` | Proposal, critique, rebuttal, synthesis review | Project + point + round + directed obligation | Dead-letter and keep round incomplete/blocked |
| `PROGRESS` | Material progress milestone | Project + operation | Dead-letter without losing underlying operation; projection shows stale/missing progress |
| `HEARTBEAT_DISPOSABLE` | Replaceable liveness sample | Session + operation | May expire after five minutes; never establishes cessation or success |
| `BULK` | Reproducible nonauthoritative detail chunks | Project + transfer | Dead-letter; parent operation reports incomplete transfer |

Every material envelope has an original `operation_id`, `effect_id`, class and monotonic sequence within its ordering scope. Duplicates are deduplicated by stable envelope ID and canonical payload digest. A gap holds later dependent envelopes and requests replay; it never fabricates or skips a prerequisite. Replay creates a new envelope ID linked to the original delivery/effect identity and rechecks current authority. Queue fairness reserves capacity for higher classes and uses bounded round-robin within one class so bulk cannot starve control. Parser maximums are 1 MiB envelope bytes, 64 nesting levels, 10,000 collection members and 64 KiB per ordinary string unless a referenced managed blob contract is used.

## B6 — Interrupted publication recovery

The journal records one operation and one entry per create/update/delete/rename with canonical root, stable target identity when present, expected absence/presence, before hash, intended after hash, staged source hash, temp/backup identities, lease fence and user-edit observation. Each entry advances `PREPARED → EFFECT_STARTED → EFFECT_OBSERVED → VERIFIED`, or `CONFLICT`/`UNKNOWN`; the operation is complete only when every entry is `VERIFIED` and post-publication criteria pass.

| Reconciled destination observation | Safe decision |
| --- | --- |
| Intended after identity/hash exists and no competing edit is observed | Record effect observed; run verification; never repeat mutation |
| Original before identity/hash still exists and no temp/intended object appeared | Effect did not occur; retry only with current authority, lease and unchanged guard |
| Target absent where create was intended, with no ambiguous alias/temp | Known not-created; retry may be admitted under current guards |
| Target absent where delete was intended and journal proves the exact before object was removed | Record delete observed; verify surrounding invariants |
| Source and destination identities prove the intended rename completed exactly once | Record rename observed; verify |
| Destination matches before hash but a temp/intended object exists | Inspect journal-owned temp; continue only if identity and hashes prove the next idempotent step |
| Destination has a third-party/newer identity/hash, alias changed, or both old and new names contain divergent data | `CONFLICT`; preserve every user object; no overwrite/restore; offer inspection/resolution |
| Required identity/hash/USN/handle evidence is missing or contradictory | Preserve `UNKNOWN`; affected publication `BLOCKED`; no generic retry |

Restore is allowed only from a journal-owned exact before-image when the destination is absent or still the exact journal-produced after object and no newer user edit can be lost. Otherwise user intervention chooses among preserved artifacts. Multi-file recovery evaluates every entry before any continuation and never rolls the whole set back blindly.

## B7 — Workspace selection and confinement

Default workspace for a suitable existing Git repository is a controller-created worktree at the authorized baseline. Suitability requires: valid worktree-capable repository, resolved baseline commit, no unsupported nested repository/submodule state for the task, controller-owned destination, no root mutation, and cleanup/recovery proof. Non-Git or unsuitable Git roots use a controller-managed staged copy with exact manifest/digests. Mayasaba never runs `git init`.

Worktree and staged-copy selection is independent of confinement. Before launch, the actual CLI/profile/operation must prove pre-effect mediation or a tested effective OS access boundary for prohibited root/out-of-scope/network/tool effects and revocation. If workspace preparation succeeds but confinement does not, execution is `BLOCKED`. Workspace Manager binds the mechanism per task before the lease; it never switches after edits begin without a new attempt and baseline.

## B8 — Context retrieval and dependency uncertainty

Default retrieval begins from explicitly authorized source records, task inputs, dependency edges and changed artifacts. Unknown edges widen in this order: direct declared dependencies, reverse dependents, containing component, then authorized repository scope. Each widening step records why, exact paths/records, bytes and coverage.

The default snapshot limit is 64 MiB or 100,000 source lines of permitted material per recipient, whichever is reached first. A task contract may approve a smaller bound; a larger bound requires an explicit source-linked authorization. At the boundary, produce `PARTIAL` with omitted dependency identities. Any change to a source record, epoch, graph revision, executable/session identity affecting application, or included artifact digest makes the snapshot `STALE`. Complete-context gates reject `PARTIAL` and `STALE`; they never truncate silently.

## B9 — Capability reassignment

When the assigned CLI lacks a required capability, Task/DAG first evaluates another named CLI only if the approved task contract permits that assignee and all current context/barrier, confinement and capability probes pass. It fences/revokes the old lease, creates a new attempt and fencing version, and preserves the original task contract and evidence. No session or lease is retargeted.

If no eligible successor exists, the task is `BLOCKED`. If the missing capability is material to the approved plan, a user-intervention question may coexist with the block. User intervention cannot declare an incapable agent capable or bypass safety. Task/DAG binds the successor before granting its lease.

## B10 — Closure admission and restart

`BeginCloseProject` atomically writes a Project-owned closure intent with operation/idempotency identity, expected project version/epoch and an admission-fence generation. Orchestrator reads that fence before new scheduling; existing leases remain Task/DAG-owned. Project service then queries each owner at a snapshot boundary and revalidates immediately before `ProjectClosure` commit.

If any blocker exists, `CommitProjectClosure` returns one `REFUSED` receipt listing current owner records and performs no mutation, so the admission fence remains. The controller then submits `AbortProjectClosure` bound to that exact refusal, unchanged closure intent/version and blocker set; its committed Project-service transaction supersedes the intent, emits `ProjectClosureAborted` and removes the fence. If either commit result is lost, admission remains fenced while the original operation receipt is queried. Restart resumes the same intent; it never starts another closure or reopens admission until the commit/refusal and any required abort are known. `ReopenProject` is a separate explicit action after a known closure and uses expected-version CAS.

## Approval, negation and condition routing

Approval commands come only from labeled controls bound to the exact decision/proposal/action revision. The receipt records user/session identity supplied by Windows application context, project, epoch, source contribution when applicable, action, object/revision, UTC time and policy version. Approval expires when its bound object is superseded, scope/epoch changes materially, the user revokes it, or the action's declared expiry passes. Expiry never cancels an already unknown physical effect.

Agent/classifier interpretation is advisory. Deterministic routing separates quoted text, negation, condition and hypothetical language. “Do not approve,” “if approved,” quoted approval wording, questions about approval, and agent-authored approval never produce an approval command. Free-form user text proposing approval creates a pending exact confirmation card. Immediate slash controls follow C06's complete-message grammar; all other text follows normal persistence and interpretation.

## Executable discovery, context and evidence receipts

C06 D3 supplies executable precedence. The pinned identity and probe result are included in every session launch and evidence record. Context snapshots use a manifest of exact source record versions and managed-byte digests; per-recipient filtering excludes secrets, unrelated files, peer proposals before independence gates, and unapproved external material. Application receipts contain recipient session identity, snapshot/profile/digest, plan/graph/epoch, adapter observation method and result; ACK is not an application receipt.

Acceptance evidence follows C06 D4. Each `PASS` references the criterion, oracle/check version, artifact/environment digests, execution/observation identities and adequacy assessment. Subjective user acceptance uses the same explicit version-bound action grammar as other approvals.

## Restart and clock rules

Active elapsed budgets persist consumed duration and attempt count at each material transition. UTC timestamps aid audit and wall-age checks but never replace monotonic elapsed observations. Suspend records a discontinuity; resume reconciles processes, locks, leases, sessions, queues and publication journals before new effects. A 24-hour wall-age requires renewed authority but never classifies an external effect. No retry counter resets on crash, replay, suspend, project switch, relaunch or handoff.

## Documentation acceptance

The C05 T01–T22 and completion-register F01–F30 cases must map to one branch in this contract and one typed v1 result. Any case lacking a unique branch keeps the affected documentation finding open. Passing these documentation checks does not establish native confinement or Windows runtime behavior.
