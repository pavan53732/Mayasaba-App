# Mayasaba — contract completion register

> **2026-10-15 project-selector amendment:** [Approved Screen 2 contract](PROJECT-SELECTION.md) replaces automatic last-project Chat startup with explicit selection on later launches, adds recent projects and explicit empty-folder creation, and routes File → Switch Project through the selector. This amendment is documentation/preview only: native UI, recent-list query metadata, create-folder typed command/fixtures and recovery proof remain pending. Earlier launch validation applies to the previous requirement and does not certify this amendment.

**Status: BASELINE DOCUMENTATION CONTRACT SET COMPLETE; PROJECT-SELECTOR COMMAND/QUERY EXTENSIONS AND IMPLEMENTATION GATES PENDING.**

This register records where the completed product choices and deterministic contracts live. It is subordinate to the authoritative system description and does not claim a working Windows application.

## Contract set

| Contract | Scope | Documentation result |
| --- | --- | --- |
| [C01](./contracts/C01-CHAT-SUBMISSION-CONTROLS.md) | Contribution persistence, immediate controls and turn admission | Normative trace |
| [C02](./contracts/C02-FULL-COUNCIL-D1-D3.md) | FULL council rotation, reviews and outcome precedence | Normative trace |
| [C03](./contracts/C03-HANDOFF-RECOVERY.md) | Handoff #9, Start/Stop and bounded recovery | Normative trace; native proof pending |
| [C04](./contracts/C04-TYPED-BOUNDARY-INVENTORY.md) | Owner records, commands and evidence boundaries | Normative semantic inventory |
| [C05](./contracts/C05-DURABLE-TRANSACTION-RACES.md) | T01–T22 crash, ACK, replay and concurrency traces | Normative adversarial matrix |
| [C06](./contracts/C06-BINDING-PRODUCT-DECISIONS.md) | Final ten approved product decisions | Binding product authority |
| [C07](./contracts/C07-VERSIONED-CONTRACT-REGISTRY.md) | Shared schemas, identities, receipts, errors and compatibility | Normative representation contract |
| [C08](./contracts/C08-TWELVE-STATE-MACHINES.md) | Exactly twelve state machines and transitions | Normative behavior contract |
| [C09](./contracts/C09-SELECTION-TRANSACTION-RECOVERY.md) | B1–B10, owner transactions, delivery and recovery | Normative algorithm contract |
| [C10](./contracts/C10-IMPLEMENTATION-ENTRY-GATE.md) | Documentation closure and I0–I8 implementation gates | Normative entry gate |
| [C11](./contracts/C11-COUNCIL-DECISION-QUALITY.md) | FULL Council substantive quality and C11 CQ01–CQ16 prospective oracles | Normative quality refinement; no native proof |

The [`schemas/v1`](./contracts/schemas/v1/README.md) registry contains the shared machine-readable representation and fixture indexes. Enabled implementation slices extend the registry with closed command-specific payload schemas under C07.

## Closed documentation decisions

The following are no longer implementation-agent choices: explicit approval/control identity, D-04 materiality/deduplication, D-11 executable precedence, D-15 evidence classes, D-16 limits/privacy/deletion, English technical-token handling, B4 bytes, keyboard Send behavior, operational bounds and Mayasaba update activation. The three safety clarifications are binding:

1. storage pressure blocks unsafe new storage after safe reclaim and never deletes protected history, drafts, pending data or required evidence;
2. Stop deadline expiry never turns an unknown physical effect into a confirmed stop; and
3. exact technical tokens do not authorize unrestricted non-English agent prose or deliverables.

## Adversarial fixture coverage

### C05 transaction fixtures

| Fixtures | Governed behavior |
| --- | --- |
| T01–T05 | Contribution/control lost receipts, altered duplicates and Stop races |
| T06–T08 | At-most-once material effects, replay authority and delivery gaps |
| T09–T12 | Dependency fencing, successor cessation, handoff replay and barrier application |
| T13–T16 | Publication recovery, user-edit conflict, closure/admission race and second-instance exclusion |
| T17–T22 | Suspend clocks, council independence, event-chain retention, late results, evidence adequacy and repair exhaustion |

Each T fixture uses C07 boundary results, C08 machine transitions and C09 transaction/recovery rules. Physical-effect fixtures remain blocked until C10 native evidence passes.

### Cross-cutting F01–F30 fixtures

| IDs | Initial condition | Required disposition | Governing contract |
| --- | --- | --- | --- |
| F01–F03 | Altered duplicate Send, lost contribution receipt, delivery gap | Reject collision or reconcile the original identity; hold dependent delivery without duplicate effect | C01, C07, C09 B5 |
| F04–F06 | Unknown commit, replay after effect, cross-project command | Preserve `UNKNOWN`, link the original effect, and reject the mismatched project identity | C07, C09 owner transaction/B5 |
| F07–F09 | Stale lease, cancellation uncertainty, session effect uncertainty | Reject stale effect and block resumption until cessation/outcome proof | C08 lease/execution/session |
| F10–F12 | Restarted handoff, ACK without application, missing barrier application | Preserve hold; ACK never releases it; dependent lease remains blocked | C08 handoff/barrier |
| F13–F16 | Incomplete council, stale authority, inadequate evidence, non-English generated output | Block or refuse without manufacturing completion, approval, evidence adequacy or an admissible reply | C06 D4/D6, C08 council, C09 approval routing |
| F17–F20 | Unproved confinement, unsafe path identity, interrupted publication, conflicting user edit | Refuse effect or reconcile through B6; never overwrite newer user data | C09 B6/B7 |
| F21–F25 | Unknown physical result, stale validation, runtime claim without proof, inadequate oracle, certification request | Preserve `UNKNOWN`/`BLOCKED` or `INCONCLUSIVE`; refuse certification | C06 D4, C08 validation/common rules |
| F26–F30 | Unsupported installer target, closure race, protected draft/reference, unsafe switch, non-English output | Refuse preflight/closure/switch/output as applicable while preserving protected data | C06 D5/D6, C08 project, C09 B10, C10 |

The machine-readable [`trace-fixtures.json`](./contracts/schemas/v1/trace-fixtures.json) indexes T01–T22 and F01–F30 to the expected result label and governing contract. The governing C07–C09 branch supplies the separate boundary result, physical outcome and operational disposition; a fixture label never collapses those dimensions.

## Documentation closure checks

| Check | Evidence recorded for this documentation change |
| --- | --- |
| C06 decisions propagated | **PASS** — authoritative specification, AGENTS, architecture, UI/UX, screens, README and C01–C10 contain the ten decisions and three safety clarifications without a conflicting open choice |
| JSON registry | **HISTORICAL PASS, additional validation pending** — prior eight v1 files and five schemas were checked at original closure; C11 adds one schema and one CQ fixture file whose JSON shape, local references and SHA-256 manifest entries were checked in this refinement. A fresh complete Draft 2020-12 metaschema/canonical-vector suite has not yet been executed |
| Fixed ownership | **PASS (rechecked 2026-10-14)** — current boundary schema contains 123 commands, 125 events and 56 records, each with exactly one owner match; prior documented 115/122 counts were stale and are corrected in the manifest; all twelve machine owners remain |
| Twelve machines | **PASS** — C08 contains exactly twelve consecutively numbered authoritative machines and no new state owner |
| B1–B10 | **PASS** — C09 contains one selected documentation mechanism, evidence requirement and fail-closed rule for each alternative |
| Fixture mapping | **PASS** — T01–T22 and F01–F30 form 52 unique, schema-valid fixtures with separate boundary, physical and operational results |
| UX scenarios | **PASS** — 85 numbered desktop scenarios are consecutive |
| Links and tables | **PASS** — 178 Markdown links, including 136 local targets and 49 local anchors, resolve; 70 Markdown tables are structurally consistent |
| Preview syntax | **PASS** — the illustrative HTML preview's JavaScript parses successfully |
| Reader test | **PASS** — independent fresh-context reviews found the requested UI, authority, language, storage, Stop and proof boundaries deterministic after their reported issues were corrected |

These are documentation checks over the committed sources and fixtures. They do not substitute for any C10 implementation or native-evidence gate.

**C11 documentation addendum, 2026-10-14:** The historical numerical PASS rows above describe the earlier C01–C10 documentation-closure check and have **not** all been re-run in a fresh environment for this C11 update. C11 adds one schema, a 16-case expected-fixture catalog and exact digest entries in the v1 registry. The CQ cases are **not** executed native/semantic tests; Gate I5 remains pending for actual evidence. The C02/C07/C08/C09/C10, architecture and Chat UI contracts reference C11 without new modes, states or authority.

## Implementation gates

| Gate | Scope | Current status |
| --- | --- | --- |
| I0 | Documentation contracts and consistency | Passed by this documentation closure change |
| I1 | Core schemas, canonical bytes, SQLite, replay | Not implemented |
| I2 | Native WinUI shell and responsiveness | Not implemented |
| I3 | Per-CLI process and containment proof | Not implemented |
| I4 | First vertical owner workflow | Not implemented |
| I5 | Three-CLI Chat, FULL council, barrier and handoff | Not implemented |
| I6 | Lease, workspace, validation, publication and recovery | Not implemented |
| I7 | Storage, backup, retention, closure and MSI update | Not implemented |
| I8 | Supported Windows release certification | Not implemented |

Only [C10](./contracts/C10-IMPLEMENTATION-ENTRY-GATE.md) defines advancement. Documentation closure permits implementation planning and staged work; it grants no runtime capability claim.

## Final classification

- **Closed:** remaining product choices, shared representation, twelve machine behavior, B1–B10 selection, owner transaction ordering, bounded recovery and acceptance-oracle classes.
- **Controlled per slice:** command-specific payload schemas, SQL layout, source organization and dependency revisions within C07/C10 constraints.
- **Pending evidence:** application code, native CLI compatibility/confinement, Windows UI, storage/publication recovery, MSI lifecycle and release certification.

This classification prevents both failure modes: coding agents cannot invent product behavior, and documentation cannot be mistaken for executed proof.
