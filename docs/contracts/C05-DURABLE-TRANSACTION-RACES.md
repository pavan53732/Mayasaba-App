# C05 — Durable transaction, ACK and recovery race matrix

**Status: NORMATIVE ORDERING AND ADVERSARIAL TRACE MATRIX.** This pack applies the [authoritative state/MCF/storage/Stop rules](../Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md) and [architecture](../ARCHITECTURE.md). [C06](./C06-BINDING-PRODUCT-DECISIONS.md) fixes product limits and clocks; [C07](./C07-VERSIONED-CONTRACT-REGISTRY.md) fixes shared representations; [C08](./C08-TWELVE-STATE-MACHINES.md) fixes transitions; and [C09](./C09-SELECTION-TRANSACTION-RECOVERY.md) fixes version guards, owner transactions, delivery and recovery. SQL statements and native confinement remain implementation details that must satisfy those contracts.

## Five separate facts

| Fact | Required source evidence | Does not prove |
| --- | --- | --- |
| Valid user authority | Project, operation, epoch, scoped consent and current Policy gates | A CLI was launched, a message was persisted or task succeeded |
| Owner commit | An owner-authorized, version-checked durable record/state/event/outbox transaction | External effect or agent completion |
| MCF delivery ACK | Valid envelope durably received in inbox | Work performed, plan applied, approval granted |
| Physical outcome | Independently observed process/tool/file state, scope and descendants | Test adequacy or full task success |
| Acceptance completion | Adequate current evidence for each applicable blocking criterion | Unchecked functionality or subjective consent |

Owner commitment and MCF inbox acknowledgement are **distinct durable boundaries**. ACK-before-durability is already prohibited, not an open alternative. Unknown commit/effect truth remains **UNKNOWN**; an operational **BLOCKED** state may coexist without converting the physical outcome to success or failure.

## Source-required happens-before gates

| Chain | Required ordering | Invalid shortcut |
| --- | --- | --- |
| PERSIST → INTERPRET | Complete full attachment set and immutable UserContribution durably confirmed before any associated CLI dispatch/turn routing | Composer presence, text preview or uncertain Send |
| AUTHORIZE → EFFECT | Current user/source/scope/epoch permission, current lease and proven pre-effect confinement before material execution | Start permission or agent recommendation alone |
| OWNER → MCF | Owner state plus outbound event/intent persisted atomically; sender outbox durable; receiver validates/stores inbox **before** ACK | Event emitted or ACK returned for uncommitted owner state |
| CONTEXT → LEASE | Verified current plan/snapshot application to required sessions and eligible barrier before dependent leases | Delivery ACK or self-claimed knowledge |
| FENCE → HANDOFF | All old affected leases fenced **and** source effect capabilities independently ceased/revoked; successor current context applied before hold release | Parent exit, timer expiry or new CLI readiness |
| JOURNAL → PUBLISH → CERTIFY | Durable pre-effect journal and guarded file effect; independently observed object identity/hash and relevant adequate post-publication oracles | SQLite intent as physical success |
| CLOSURE FENCE → CLOSE | Durable admission fence and current quiescence recheck before closing while racing work cannot enter | One stale read of “zero leases” |
| STOP → RECONCILE | Immediate authorized Stop independent of any Chat persistence; truthful observed cancellation and unresolved-effect handling | Waiting for reply, assuming cancelled helper |

These are *partial-order conditions*. They do not imply all services can atomically include physical file/process effects inside one SQLite transaction. C09 fixes owner transaction/outbox coupling, B1 guards, B3 journal selection and mixed physical recovery; the implementation must realize and test them without treating an external effect as part of a SQLite commit.

## Adversarial commit and crash decisions

| ID | Concurrent or interrupted facts | Required safe conclusion | Precise missing contract |
| --- | --- | --- | --- |
| T01 | UserContribution committed, UI lost ACK | Reconcile original Send identity, prevent duplicate; no false saved claim | Owner result/idempotency lookup |
| T02 | Same Send ID, different content or attachments | Never mutate original or persist second identity-equivalent contribution | Exact payload conflict test/error |
| T03 | Stop arrives during uncertain mixed Send | Stop acts independently; no new non-control dispatch before confirmed commit | Cross-owner command/event ordering |
| T04 | Stop effect invoked, durability unknown | Preserve observed result and investigate before retry; no fabricated persistent receipt | Control result/physical provenance |
| T05 | Bus ACK would precede owner state/outbox commit | **Forbidden**; such an ACK does not satisfy admission | Exact atomic owner/outbox/inbox protocol |
| T06 | Same material envelope delivered again | Process at-most-once effect under stable original material operation | Dedup payload/conflict and result |
| T07 | Controller replays historical material traffic as new envelope | Replay alone grants no new material authority | Replay identity and current user/Policy guard |
| T08 | Recipient observes a sequence gap | Report/hold; never silently fabricate or skip prerequisite message | Sequence scope, gap and queue recovery |
| T09 | A material successor epoch commits after original task authorization | Fence/reevaluate affected work, preserve unrelated verified work | Epoch compare and dependency traversal |
| T10 | Old lease expires while old child remains effect-capable | No overlapping unsafe successor execution from time alone | Physical source revocation and lease CAS |
| T11 | Handoff COMPLETED commit succeeded but caller ACK lost | Reconcile original handoff, never release hold twice | Hold commit and result receipt |
| T12 | Successor plan transport ACK exists, actual application not verified | Keep barrier/hold; no dependent release | Current applied-context proof |
| T13 | Crash between file mutation and journal follow-up | Preserve UNKNOWN until physical object/identity/hash observation | B6 exhaustive create/update/delete/rename matrix |
| T14 | User modifies destination after before-hash check | Block unsafe overwrite; never erase newer edit | Per-file competing-edit guard and timing |
| T15 | Closure quiescence read races task admission | Only one guarded outcome; no new unauthorized work after closed | Project service B10 admission transaction |
| T16 | Two Mayasaba app controllers target same store | Prevent competing store/lease owner realities | Process/store ownership exclusion and abandonment |
| T17 | Windows sleep or wall-clock change during finite bound | Never reset retries or authority; reconcile honest deadline | Versioned time and suspend/restart policy |
| T18 | CLI saw peer proposals, then “independent” new council first proposal requested | Do not count contaminated response as independently produced | Context/session isolation and provenance |
| T19 | Remaining event hashes valid but expected retained head missing | Do not claim complete retained history | Expected-head/anchor/checkpoint proof |
| T20 | A's late CLI response arrives while B is visible | Attribute to A, reject cross-project current effect | Exact session/root/epoch envelope validation |
| T21 | Build passes but a blocking login/runtime criterion remains unobserved | Record validation `INCONCLUSIVE`, refuse completion for inadequate evidence, and keep the dependent operation `BLOCKED`; never claim runtime PASS | D-15 oracle sufficiency and tests |
| T22 | Repeated repair same failure signature exhausts allowed attempts | Stop bounded repair; preserve diagnostics and affected BLOCKED | Numeric attempt/clock and repair identity |

All entries are **expected outcomes**, not executed tests. A refusal of unsafe progress is not proof that the unknown physical outcome was harmless or reversed.

## Owner-boundary review protocol

For each T case, C07–C09 determine the owner, command/result grammar, authority/epoch/version identity, owner record/event/outbox boundary, ACK meaning, duplicate handling, restart reconciliation, physical-outcome separation and safe recovery class. Each enabled implementation slice supplies its exact payload schema, SQL mapping and fixtures. For an unsafe effect, the implementation names the unavailable proof rather than selecting a more permissive profile.

## Completion boundary

C06–C09 close the documentation choices previously tracked by Z-01/Z-02/Z-03/Z-06/Z-11/Z-12, D-04/D-11/D-15/D-16/U2 and B1–B10. C10 keeps the affected implementation gates closed until schemas for enabled payloads, transaction and fault-injection tests, per-CLI physical confinement, publication safety and Windows evidence pass.

**Result: normative transaction-order coverage.** The v1 shared schemas exist, but no native Windows experiment, SQL transaction test or application implementation is claimed.
