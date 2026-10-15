# Mayasaba — implementation-readiness audit

**Audit status: DOCUMENTATION CLOSED; PARTIAL NATIVE IMPLEMENTATION AND LOCAL WINDOWS VALIDATION IN PROGRESS.**

This audit reconciles the authoritative specification, AGENTS, architecture, UI/UX, user-visible screens, C01–C11 contracts and v1 shared schemas. It closes the documentation-stage ambiguity inventory. A partial native C++20 / WinUI 3 desktop slice exists under native/ and is undergoing local Windows validation, but end-to-end orchestration, CLI adapters, publication, and MSI packaging remain to be completed.

## Closure meaning

Documentation closure means the same initial facts lead to one existing owner, allowed command, guard/result, durable ordering and recovery class. It does not mean every implementation payload schema or SQL layout already exists. Under C07, an implementation slice must add its closed command-specific schema and fixtures before that command can be enabled; accepting an unconstrained payload is prohibited.

Runtime truth remains evidence-based. A planned test, valid schema, agent statement, clean exit or successful build is not native proof.

## Final product decisions

The binding record is [C06 — Binding product decisions](./contracts/C06-BINDING-PRODUCT-DECISIONS.md).

| Decision | Binding result |
| --- | --- |
| Explicit authority and controls | Labeled UI actions bind approvals and material lifecycle actions. Exact standalone `/start`, `/pause`, `/resume`, `/stop` aliases are control-only; free text is not keyword-converted. |
| D-04 council triggers | Six finite material trigger classes, exclusions, canonical deduplication key and pre-authorization registration point are fixed. |
| D-11 CLI executable selection | A verified user-selected executable wins; otherwise exactly one verified candidate may be pinned. Multiple candidates require explicit choice and identity change requires reprobe. |
| D-15 evidence sufficiency | Criterion-specific minimum evidence, inadequate substitutes and unavailable dispositions are fixed. |
| D-16 storage policy | 10 GiB ceiling, 5 GiB reserve, 2–100 GiB user range, cleanup/backup cadence, protected drafts and attachment limits are fixed. Capacity blocks unsafe new storage and never deletes protected records or bytes. |
| English mixed content | Product prose and deliverables remain English. Only the smallest exact technical-identity token may remain unchanged when correctness requires it; this never admits unrestricted non-English agent prose. |
| B4 canonical bytes | `mayasaba-jcs-1`, RFC 8785/JCS-compatible I-JSON, UTF-8, rejection rules, canonical scalar strings and SHA-256 are fixed. |
| Keyboard Send | Enter sends and Shift+Enter inserts a line; the accessibility preference uses Enter for a line and Ctrl+Enter to send. |
| Operational bounds | Detection, startup, delivery, liveness, Stop, Chat, council, task, reconciliation and repair bounds are fixed. A Stop deadline never converts physical `UNKNOWN` into confirmed cessation. |
| Mayasaba updates | No autonomous updater. Exact signed MSI activation requires a separate explicit action after proposal, council, approval, backup and evidence. |

Previously approved Start retention also remains binding: A → B → A retains A's permission within the same application run only while A's identity, sessions, isolation and authority remain current. B inherits nothing. Stop, relaunch, unsafe uncertainty and closure preserve their fresh gates.

## D-01–D-20 reconciliation

| ID | Documentation disposition | Governing contract |
| --- | --- | --- |
| D-01 | Closed: project-local atomic decision ordinal and replay-stable rotation | Authoritative FULL rules; C02; C08 machine 7; C09 B1 |
| D-02 | Closed: both non-chair reviews of the exact synthesis are mandatory | C02; C08 machine 7 |
| D-03 | Closed: ten-priority council outcome order and incomplete-round behavior | Authoritative spec; C02; C08 machine 7 |
| D-04 | Closed: material trigger registry, exclusions, dedup key and binding point | C06 D2 |
| D-05 | Closed: disposition authority and supersession | Authoritative spec; C07 authority references; C08/C09 |
| D-06 | Closed: interpretation never creates authority; disagreement routes through registered work/council gates | C07; C09 approval routing |
| D-07 | Closed: attachment identity, whole-draft hold, limits, change and timeout behavior | C06 D5; C07; C08 |
| D-08 | Closed: Windows path/file identity, alias/reparse recheck and fail-closed switch | C08 project lifecycle; C09 B1/B10 |
| D-09 | Closed: switching requires proven isolation or bounded pause/reconciliation and preserves the old view/draft on uncertainty | Authoritative spec; C03; C08 handoff/project; C09 |
| D-10 | Closed: session compatibility, Start retention, Stop/relaunch and replacement rules | C03; C06 D9; C08 session/handoff |
| D-11 | Closed: explicit verified pin, one-candidate discovery, multi-candidate user choice | C06 D3 |
| D-12 | Closed: immutable snapshot identity, filtered bytes, digest and application receipt | C07 artifact/context references; C08 context/barrier; C09 B8 |
| D-13 | Closed: capability observation stays separate from assignment/admission | C07 result grammar; C09 B7/B9 |
| D-14 | Closed: exhaustive publication recovery branches protect newer user edits | C09 B6 |
| D-15 | Closed: claim-class evidence matrix and fail-closed unavailable results | C06 D4 |
| D-16 | Closed: capacity, cleanup, backup, draft, attachment and optional-retention defaults | C06 D5 |
| D-17 | Closed at specification level: only separately authorized, probed native research tools; unavailable blocks affected work | Authoritative spec; C09 B7/B9; native proof pending |
| D-18 | Closed: durable at-least-once delivery, retry schedule, material dead letter and disposable expiry | C06 D9; C08 message delivery; C09 B5 |
| D-19 | Closed: review request does not advance epoch; authorized successor transaction does | Authoritative spec; C08 project/task/lease; C09 B1/B10 |
| D-20 | Closed: dependency-known affected work is fenced; demonstrably unaffected work continues; uncertainty blocks only the uncertain closure | C08 task/lease; C09 B8/B9 |

No row above claims the corresponding code or native evidence exists.

## B1–B10 reconciliation

| Contract | Selected documentation mechanism | Implementation evidence still required |
| --- | --- | --- |
| B1 state guards | Single-writer owner transaction with expected record/epoch/fence versions and canonical idempotency | Concurrent stale/duplicate/restart fault injection |
| B2 backups | Coordinated SQLite plus referenced-managed-byte snapshot boundary | Crash/restore tests and byte-integrity proof |
| B3 journal mode | WAL when local eligibility tests pass; rollback fallback; FULL durability for authority and publication commits; declared checkpoint/busy policy | Filesystem/store observations and recovery tests |
| B4 canonical bytes | `mayasaba-jcs-1` and golden accept/reject vectors | Independent implementation/vector execution |
| B5 delivery | Material, control, coordination and disposable classes with fixed retry/dead-letter/expiry rules | Queue/replay/fairness tests |
| B6 publication recovery | Before/after file identity and hash decision table: continue, safe restore or intervention | Native crash and concurrent-user-edit tests |
| B7 workspace/confinement | Git worktree only for suitable repositories; otherwise managed staging; neither substitutes for proven effect confinement | Per-CLI, descendant/service and reparse adversarial proof |
| B8 context retrieval | Dependency-aware snapshot with widening; uncertainty reports `PARTIAL`/`STALE` and blocks affected admission | Capacity, invalidation and application tests |
| B9 capability reassignment | Fresh successor identity, capability evidence, attempt and lease; old lease never transfers | Replacement and old-effect cessation proof |
| B10 closure admission | Durable Project-service intent plus atomic admission fence, quiescence checks and restart recovery | Race/restart tests |

The full algorithms and precedence are in [C09](./contracts/C09-SELECTION-TRANSACTION-RECOVERY.md).

## Twelve authoritative machines

[C08](./contracts/C08-TWELVE-STATE-MACHINES.md) specifies exactly these machines: Project Lifecycle, Agent Session, Message Delivery, Context, Task, Lease, Council Round, Synchronization Barrier, Handoff, Execution, Validation and Repair. Readiness, routing, exploration, research, decision validity and the three recovery/repair/improvement loops remain projections or workflows. No thirteenth state owner is introduced.

## Schema and transaction boundary

[C07](./contracts/C07-VERSIONED-CONTRACT-REGISTRY.md) and [`schemas/v1`](./contracts/schemas/v1/README.md) fix JSON Schema 2020-12, shared identities/references, owner records, boundary commands/results, errors, policies, machine records, canonical vectors and T/F fixture references. [C09](./contracts/C09-SELECTION-TRANSACTION-RECOVERY.md) fixes owner commit/event/outbox ordering, instance exclusion, idempotency, ACK meaning and physical-outcome separation.

Command-specific payloads are closed when their implementation slice is prepared. Before enablement they must have a bounded versioned schema, owner/transition mapping, duplicate behavior and positive/negative/stale/restart/unknown fixtures. This controlled extension point is part of the specification; it is not permission to invent product behavior.

## Documentation acceptance matrix

| Check | Required result for closure |
| --- | --- |
| Product decisions | C06 decisions and safety clarifications propagated without contradictory open wording |
| Registry | Every JSON file parses; every local `$ref` resolves; canonical expected hashes match exact UTF-8 bytes |
| Machines | Exactly twelve numbered contracts with owners, commands, guards, results, illegal/restart/unknown behavior |
| Transactions and alternatives | C09 fixes B1–B10, owner durability, MCF, publication, approval routing and clocks |
| Adversarial fixtures | C05 T01–T22 and register F01–F30 map to governing contracts and expected dispositions |
| User experience | Welcome → Open Folder → Chat plus Start/Pause/Stop, bottom composer, project switch, storage and recovery agree across UI documents and preview |
| Structure | Relative Markdown links/anchors resolve and numbered UI acceptance scenarios are consecutive |
| Fresh reader | Independent readers can derive the owner, command/guard, result and recovery without adding product authority |

The repository validation record is completed in the companion [contract completion register](./CONTRACT-COMPLETION-REGISTER.md). Any later contradictory semantic change reopens only its affected documentation scope and follows D-04 when material.

**C11 refinement (2026-10-14):** [C11](./contracts/C11-COUNCIL-DECISION-QUALITY.md) augments the FULL Council's decision-quality specification with six-slot critique dispositions, classified evidence, adversarial common-assumption checks, fair nonbinary alternatives and decision-validity review. Its typed v1 subdocuments and CQ01–CQ16 are documentation-stage expected scenarios only. Existing thirteen layers, twelve authoritative machines, D-03 priority and the five-round bound do not change; this makes no Windows/CLI or I5 runtime claim.

## Remaining implementation and proof obligations

These items are deliberately not documentation ambiguities:

- select, pin and record the current local MSVC, Windows SDK, Windows App SDK, C++/WinRT, SQLite, JSON, WiX and test dependencies under C10;
- implement command-specific schemas, C++ domain types, migrations, owner transactions and fault injection slice by slice;
- prove each installed CLI's supported native protocol, process tree, descendant/service effects and effective pre-effect confinement on the supported Windows target;
- implement and test native WinUI accessibility, responsiveness and theme behavior;
- prove publication, backup/restore, cleanup, retention, closure and update recovery;
- run MSI preflight/install/launch/repair/upgrade/uninstall tests on the declared Windows matrix; and
- assemble current criterion-linked evidence before any release claim.

Follow [C10 — Documentation closure and implementation entry gate](./contracts/C10-IMPLEMENTATION-ENTRY-GATE.md). Missing proof yields `INCONCLUSIVE`, `UNSUPPORTED`, `UNKNOWN` or `BLOCKED` as defined; it never silently weakens the contract.

## Verdict

The Mayasaba documentation stage was recorded as closed under C10's mechanical and fresh-reader rules. Since then, a **partial native C++20 / WinUI 3 source slice** has been added under [native/](../native/README.md), with project root validation, local SQLite contribution/draft persistence, three native screen definitions, and an in-app Exit control. The slice has **not** completed I1–I8 and no complete Windows certification is asserted. In accordance with AGENTS.md §3 ("Do not add hosted CI or cloud execution. Build and verification are local to the user's Windows machine"), hosted GitHub Actions workflows have been removed; all compilation, verification, and tests are conducted locally on Windows. This audit remains an implementation gate, not an assertion that Mayasaba is operational end to end.
