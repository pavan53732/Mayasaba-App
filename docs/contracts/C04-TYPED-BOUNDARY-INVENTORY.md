# C04 — Typed owner-boundary inventory

**Status: NORMATIVE SEMANTIC INVENTORY.** This page records semantic obligations from the [authoritative specification](../Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md), [architecture](../ARCHITECTURE.md), and prior [C01–C03 traces](./README.md). [C07](./C07-VERSIONED-CONTRACT-REGISTRY.md) and its v1 schemas fix the shared property types, identity formats, canonical profile and result/error grammar. SQLite layout and command-specific payloads remain implementation-slice details constrained by C07–C09; they cannot create a new authority service.

**Legend:** S = explicit authoritative semantic fact; R = relationship needed to enforce an existing S fact; I = implementation-slice representation required by C07; V = physical proof still UNVERIFIED. A table entry is a semantic obligation and does not create a new owner.

## Record owner and minimum semantic obligations

| Record/family | Sole domain owner | Source-required semantic relationships (S/R) | Implementation-slice work/evidence (I/V) |
| --- | --- | --- | --- |
| Project identity, canonical root, epoch | Project service | Authorized bound canonical root, project-local identity/epoch, valid current scope | D-08 root alias/reparse identity, guard/version/record schema |
| ProjectClosure and Reopen | Project service | Explicit action provenance, durable admission fence, quiescence, closure/reopen predecessor links | B10 transaction, restart, conflict receipt |
| UserContribution and attachments | Message/Contribution | One submission identity, full immutable text/verified attachments, project/root and linked source/question | D-07 byte identity, storage commit receipt, attachment bounds |
| `ChatTurn`, `AgentChatResponse`, `ChatReplyDraft`, `ChatReplyReview`, `ConsolidatedChatReply` | Message/Contribution | Confirmed contribution, zero-based project-local turn ordinal, 3 agent responses, one writer, **both exact-final-digest reviews** | Typed record/revision/duplicate error; atomic allocation |
| InterpretationCandidate | Work-request authority validates agent proposal | Source spans, scoped intents/constraints, source contribution, current context and explicitly unsupported assumptions | D-06 structural negation/conditional verification and adjudication |
| WorkRequest, pending material change, WorkRequestAuthorized | Work-request authority | Source contribution/operation/scope, current project epoch and separate material admission | Typed authorization result, duplicate and revocation |
| Requirements/amendments/approvals | Requirement service | Approved source, predecessor, scope and explicit user gate | P0-05 approval key and expiry/revocation |
| Binding decisions, locks and supersession | Decision service | Current valid FULL council/evidence/user authority, predecessors; agent vote not binding | D-05 typed disposition/conflict contract |
| Council decision point/round/positions/critiques | Council Engine | Atomic **project-local d**, one-based round r, 3 proposals, 6 directed critiques, required rebuttals and exact dual synthesis reviews | D-04 trigger dedup, normalized equality, Council #7 transitions |
| MCF envelope/inbox/outbox | MCF-v2 fabric owns transport | Version, message/event IDs, sender/recipient, project/session/epoch, correlation/sequence, priority, type/phase/flags/payload/security; operation ID on material traffic | D-18/B5 vocabulary, field types, ACK/ordering/gap/replay |
| CLI session/native traffic | Agent Gateway | CLI identity, project/session association, observed readiness vs actual session result | D-10/11 CLI discovery and native profile proof V |
| Context snapshot/application receipt | Context Synchronizer | Project/epoch/graph/source versions, permitted context digest, verified **application** distinct from transport receipt | D-12 exact permitted/filter bytes, receipt grammar |
| `RepositoryManifest` | Workspace Manager | Permitted root/snapshot identity, normalized entries, byte identity, digest and access/exclusion status | B7 path/reparse proof and bounded traversal |
| `ExplorationAssignment` | Orchestrator | Equal frozen question/manifest/snapshot and independent three-session dispatch | Read-only admission, idempotency and bounded recovery |
| `ExplorationFinding`, `FileCoverage`, `ExplorationReport` | Evidence Engine | Attributed findings, controller-observed per-entry coverage, source links, current hashes and disclosed gaps | Coverage recomputation, stale invalidation and honest partial/block result |
| `ResearchAssignment` | Work-request authority service | Source contribution, exact research question/scope, current project/epoch and separately authorized native tools | Capability/policy admission and source-linked assignment result |
| `ResearchFinding`, `ResearchReport` | Evidence Engine | Attributed native-tool observations, URLs/digests/citations, disagreements and failed-tool gaps | Re-fetch/provenance validation and honest coverage result |
| Task attempt and lease | Task/DAG | Scope/contract, current epoch, lease fence/version, separate successor attempt | Task and Lease machine guards, concurrency, expiry |
| Workspace publication journal | Workspace Manager | Target/object identity, before/after hashes, intent, observed physical effect and competing user-edit preservation | B6 exhaustive physical/crash matrix V |
| Handoff #9 intent and hold | Orchestrator | Project/CLI/source/replacement identity, durable scheduling hold, successor/barrier proofs | Seven-command typed transitions, atomic release |
| `AcceptanceEvidenceMatrix`, validation check, failure diagnostic, repair | Validation/Repair | Criterion, appropriate oracle/adequacy, actual verdict, evidence, bounded attempts | D-15 applicability, exact bounds, tests V |
| Evidence, artifact and event chain | Evidence Engine | Independently observed bytes/source/tool references; retained expected head/boundary | B4 canonical bytes, backup/hash completeness |
| Material action permission/refusal | Policy Engine | User authority, current root/project/epoch, allowed scope, capability and explicit deny reason | Approval identity and real pre-effect containment V |

The **Orchestrator** coordinates but does not write another owner's authoritative records. **SQLite Storage** physically persists owner-authorized state; it does not become another semantic owner. Status labels, readiness queries, route interpretations and loop names do not introduce a thirteenth state machine.

## Command, event and read-only query family inventory

Code-form names below are already present in the authoritative text. Descriptive names in prose are **slots only**, not newly selected wire API symbols.

| Existing boundary | Source name / operation | Required distinction | Required C07–C09 result mapping |
| --- | --- | --- | --- |
| UI → Message/Contribution | `SubmitUserContribution` | Persist full contribution before any dependent CLI dispatch; one ID; unknown commit is not saved | Typed payload, atomic result/ACK reconciliation and duplicate conflict |
| UI/control → existing owner | Immediate Pause/Stop | Valid authorized control acts now, independent of message persistence; actual cessation requires proof | Natural-language control predicate, effect-status and error |
| Work-request service | `WorkRequestAuthorized` event | Scoped current admission after checks, not project-wide permission | Event schema/command receipt |
| Project service | Bind, switch, close and reopen | Folder identity and closure fences; Start never reopens | Root, closure, guard and unknown receipt schema |
| Council Engine | Create point, assign FULL round, critique/rebuttal/review and seal | D1/D2/D3 and D-04 admission fixed | Point/round machine #7 commands, events and errors |
| Orchestrator/Handoff | `BeginSessionHandoff`, `ConfirmSourceFenced`, `AttachSuccessor`, `ConfirmSuccessorApplied`, `CompleteSessionHandoff`, `BlockSessionHandoff`, `ResumeSessionHandoff` | Source cessation and successor plan application required; no lease transfer | Version/hold transaction, typed illegal transition |
| Gateway/Context/Orchestrator | Start and synchronize barrier | Project+application-run Start; current actual plan applied, not transport ACK | Native result, compatibility and application receipt |
| Task/DAG | Lease and successor-attempt admission | Current task/scope/version/fence; distinct successor attempt | CAS, old-lease and late-result errors |
| Workspace/Validation | Material publication and acceptance evidence | Journal/physical observation/adequate oracle distinct | B6/D-15 branching and typed unknown |
| MCF-v2 | Envelope send/ACK/replay | Delivery at least once; ACK is **persisted receipt only**; replay not fresh authority | D-18/B5 causal ordering, idempotency, expiry |
| Application query boundary | Owner projections and evidence details | Read-only immutable projection, no made-up missing fields | Query names, paging, snapshot-consistent errors |

## Results that must not collapse into one status

| Observation | Actually establishes | Must never be treated as |
| --- | --- | --- |
| Owner command committed durably | Known owner state/event/outbox transition | Material filesystem/process effect success |
| Transport ACK | Valid envelope persisted in receiving inbox | Agent applied plan, agreed, completed work, or approved |
| Process exit or timeout | Process observed exited, or bound elapsed | Proven effect-capable descendants stopped |
| UNKNOWN commit or physical effect | Proof unavailable; reconcile | Retry-safe failure, successful completion, or reason to erase lease |
| Validation PASS for one adequate criterion | Evidence for **that** current criterion | Universal task/product correctness or user choice |

## Completion boundary

C06–C09 close the documentation choices for the JSON Schema dialect, shared types, B4 bytes, guard/result grammar, owner transaction/outbox boundary, explicit approval/control identity and bounded clocks. Each enabled command still requires its closed payload schema and fixtures under C07. Per-CLI process/confinement observations, parser implementation, SQLite code and Windows results remain C10 implementation evidence.

This inventory is traceable specification work and is neither a released library nor a runtime proof.
