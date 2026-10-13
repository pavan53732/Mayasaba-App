# C05 — Durable transaction, ACK and recovery race matrix

**Status: SOURCE-DERIVED ORDERING TRACES; EXACT OWNER TRANSACTION CONTRACT OPEN.** This pack applies the [authoritative state/MCF/storage/Stop rules](../Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md) and [architecture](../ARCHITECTURE.md). It does **not** choose SQLite SQL statements, version-guard algorithm, process sandbox, distributed transaction scheme, canonical bytes, clocks, or error-code values.

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

These are *partial-order conditions*. They do not imply all services can atomically include physical file/process effects inside one SQLite transaction. The precise owner transaction/outbox coupling, B1 transition guard, B3 SQLite journal choices and mixed physical recovery algorithms remain OPEN.

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
| T21 | Build passes but login/runtime criterion remains unobserved | Do not claim runtime PASS; INCONCLUSIVE/BLOCKED as applicable | D-15 oracle sufficiency and tests |
| T22 | Repeated repair same failure signature exhausts allowed attempts | Stop bounded repair; preserve diagnostics and affected BLOCKED | Numeric attempt/clock and repair identity |

All entries are **expected outcomes**, not executed tests. A refusal of unsafe progress is not proof that the unknown physical outcome was harmless or reversed.

## Owner-boundary review protocol

A future versioned executable implementation contract must answer, for each T case: which existing owner receives which valid command; project/epoch/user authority; stable operation and expected-version identity; one precise accepted/refused/unknown result; owner records/events/outbox in the atomic boundary; inbox ACK semantics; how identical and altered duplicates differ; crash/replay query; physical effect and children/service attribution; actual cancellation/Stop precedence; and user-visible evidence. For an unsafe effect, explicitly name the unavailable proof rather than silently selecting a more permissive profile.

## Open freeze gates

**Z-01/Z-02/P0-01–04:** schemas, transaction and idempotency, error and uncertain-commit grammar. **Z-06/D-18/B5:** sequence, delivery, expiry/dead letter and fairness. **Z-04/Z-05/D-09/D-10/B6:** physical CLI confinement, handoff and user-edit-safe publication. **Z-11/Z-12/B2/B3/B4:** clocks/limits, consistent backup and canonical bytes. **D-04/D-11/D-15/D-16/U2:** unresolved genuine product/acceptance choices.

**Result: source-derived transaction-order coverage only.** No executable JSON schema, native Windows experiment, SQL transaction test, implementation or closed P0 finding is claimed.
