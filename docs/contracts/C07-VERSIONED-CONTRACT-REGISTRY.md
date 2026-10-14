# C07 — Versioned contract registry and boundary catalog

**Status: NORMATIVE DOCUMENTATION CONTRACT.** The v1 registry supplies machine-readable shared definitions for authoritative owner records, commands, events, MCF envelopes, state-machine records, policies, errors and evidence. C04 remains the semantic ownership inventory; this pack binds its representation and compatibility rules.

## Registry roots

- [`core.schema.json`](./schemas/v1/core.schema.json)
- [`boundaries.schema.json`](./schemas/v1/boundaries.schema.json)
- [`state-machines.schema.json`](./schemas/v1/state-machines.schema.json)
- [`policy.schema.json`](./schemas/v1/policy.schema.json)
- [`canonical-vectors.json`](./schemas/v1/canonical-vectors.json)
- [`trace-fixtures.json`](./schemas/v1/trace-fixtures.json)
- [`trace-fixtures.schema.json`](./schemas/v1/trace-fixtures.schema.json)
- [`registry-manifest.json`](./schemas/v1/registry-manifest.json)

The dialect, limits, compatibility policy and canonical profile are defined in the [v1 registry README](./schemas/v1/README.md). The registry is authoritative only for representation. Service ownership, allowed behavior and safety precedence remain in the complete system description and C06–C10.

## Identity and reference rules

- Persistent IDs are lowercase canonical UUID text. IDs are opaque and never encode authority, time or ownership.
- Security-relevant counters, versions, epochs, byte sizes and elapsed milliseconds use canonical decimal strings to avoid cross-runtime number loss.
- Every owner record has `project_id`, `record_id`, `record_version`, approved epoch, owner, type, predecessor when applicable, body digest and UTC timestamps.
- Every material command has a unique command ID plus stable operation and idempotency identities. Operation identity survives lost receipts and replay; command identity identifies one delivery/attempt to invoke it.
- Every approval/authority reference identifies its exact object/revision and scope digest. Stale, revoked, expired or mismatched authority fails closed.
- Every artifact reference includes SHA-256, exact byte length, media type and provenance identity. A path alone is not an artifact identity.

## Owner command families

The `BoundaryCommand.command_type` catalog maps to existing owners. It includes project binding/lifecycle/closure; contribution/draft/action; session; MCF; context; task/lease; council; barrier/handoff; execution; validation/repair; publication/backup/cleanup; and executable update commands. An implementation enables a command only when its command-specific payload schema, C08 transition, owner transaction, error mapping and fixtures are present.

Command wrappers do not allow a generic `owner` to become authority. The implementation registry maps each command type to one fixed owner and rejects any wire object whose claimed owner differs. The same applies to domain event and record types.

## Error and result grammar

Owner receipts have exactly four boundary results:

- `COMMITTED`: the owner transaction is durably confirmed; it proves no external effect.
- `REFUSED`: no owner mutation occurred and a registered error explains why.
- `DUPLICATE`: identical canonical command bytes produce no owner mutation and return a linkage receipt containing the exact original receipt ID and digest. The linked original receipt remains the sole source of its committed, refused or unknown outcome; the duplicate wrapper cannot replace or reinterpret its fields.
- `UNKNOWN`: commit truth is not established; the operation identity is reconciled before any retry.

Physical outcome is independent: `NOT_STARTED`, `KNOWN_SUCCEEDED`, `KNOWN_FAILED`, or `UNKNOWN`. Operational disposition is independent: `ACTIVE`, `WAITING`, `PAUSED`, `STOPPED`, `BLOCKED`, `RECOVERING`, or `COMPLETED`. A record may therefore be operationally `BLOCKED` while its physical outcome remains `UNKNOWN`.

The registered error catalog distinguishes invalid shape/version/size, missing or stale authority, policy denial, project/epoch/version mismatch, illegal state, idempotency collision, deadline expiry, unproved confinement, integrity/path/capability/evidence/storage/language/delivery/guard/user-edit/instance conflicts and internal error. Error messages shown by Mayasaba are English and name a demonstrated recovery action only when one is safe.

## Record-family payload completion rule

The shared `OwnerRecord.body` and `BoundaryCommand.payload` slots allow the catalog to describe every owner without combining their authority. Before application implementation enables a particular record or command, that slice must add a versioned schema under the same v1 package that:

1. fixes every property, type, required field and bound;
2. uses only registered identity/reference types;
3. maps to one C08 transition or read-only query;
4. maps to one owner transaction and event/outbox result;
5. defines identical-duplicate versus altered-duplicate behavior;
6. supplies positive, negative, stale, restart and unknown-outcome fixtures; and
7. passes canonical and parser vectors.

This is an implementation slice gate, not an open product decision. A disabled unimplemented command is compliant; accepting an unconstrained material payload is not.


## C11 typed quality subdocuments and fixture expectations

[`council-quality.schema.json`](./schemas/v1/council-quality.schema.json) defines closed `ClaimQuality`, `DirectedCritiqueCoverage`, `SynthesisQualityPacket` and `ChoiceExplanationPacket` typed subdocuments inside existing Council/Evidence/Decision owner bodies or exact-digest references. They create no new `OwnerRecord.record_type`, command/event owner, twelfth-plus-one state machine or wire receipt family. Each eventual enabled command still needs its own versioned C07 payload and runtime evidence. The v1 manifest registers the C11 schema SHA-256 and [`council-quality-fixtures.json`](./schemas/v1/council-quality-fixtures.json) CQ01–CQ16 expectations separately from the unchanged original 52 T/F cases.

Cross-record semantic validators must independently enforce six unique directed reviewer→author assignments, authentic substantive current authored work, frozen target IDs, adequate observation and inference premises, source deduplication, justified critique resolution, exact synthesis/coverage and both nonchair reviews, prior decision compatibility and original first-applicable D-03 priority. Schema validity by itself never certifies an observation or an LLM's assertion. This documentation refinement does not alter existing v1 wrapper accepted instances/canonical bytes or implement a native validator.

## Query contract

Read-only application queries carry contract version, query ID, project, requested projection, snapshot consistency token, page size (1–200) and opaque continuation token. Results carry the owner record versions used, projection time, completeness (`COMPLETE`, `PARTIAL`, `STALE`), next token and typed error. Queries never authorize actions or fill absent facts with defaults. A UI projection may combine read-only results but cannot create an aggregate authority record.

## Event-chain completeness

Each project event-chain entry includes event ID, owner record/version, operation, canonical profile, previous hash and chain hash. A durable project chain-head record identifies the expected latest event. Retention checkpoints record the pre-retirement boundary, retained first event, expected head and deletion-manifest digest. Verification reports `COMPLETE`, `ABBREVIATED_DISCLOSED`, or `INTEGRITY_FAILURE`; valid remaining links without the expected head/boundary can never report complete.

## Registration and drift control

The checked-in `registry-manifest.json` lists every schema `$id` and file digest, identifies the complete command/event/record enum and fixed-owner mapping registries by JSON Pointer, fixes all twelve machine owners, and lists both fixture sets. The mapping registries are not duplicated in the manifest: their canonical `allOf` rules live in `boundaries.schema.json`, and validation must prove that every declared type is accepted under exactly one owner. The build must verify or regenerate this manifest and refuse stale digests, count drift, missing types or zero/multiple-owner matches. Documentation/implementation drift is a blocking validation criterion. Schema changes follow D-04 when material and always preserve migration/history rules.

## What this closes and what it does not

This pack closes the documentation choice of dialect, shared field encodings, canonical profile, result dimensions, error catalog and compatibility rules. Native parser behavior, chosen C++ library revision, owner transactions, CLI safety and Windows runtime remain implementation evidence obligations. Schema validity never certifies those claims.
