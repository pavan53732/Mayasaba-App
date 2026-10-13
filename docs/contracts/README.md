# Mayasaba — Derived contract trace corpus

**Normative documentation contracts; no app implementation or Windows/CLI proof is claimed.** The [authoritative system description](../Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md) takes precedence. C01–C05 preserve source-derived traces; C06–C10 bind the approved product choices, representation, transitions, algorithms and implementation-entry gates. The [readiness audit](../IMPLEMENTATION-READINESS-AUDIT.md) and [completion register](../CONTRACT-COMPLETION-REGISTER.md) distinguish documentation closure from later native proof.

| Pack | Approved invariants made traceable | Remaining implementation evidence |
| --- | --- | --- |
| [C01 — Chat submission and immediate controls](./C01-CHAT-SUBMISSION-CONTROLS.md) | Persist-before-dispatch, one Send identity, mixed Stop + request, exact control-only aliases and three-agent reply gate | Command-specific schemas, SQLite/outbox code and fault-injection results |
| [C02 — FULL council D1–D3](./C02-FULL-COUNCIL-D1-D3.md) | Project-local D1 rotation, six critiques, both nonchair reviews, D-04 and ten-level D3 precedence | Council implementation and contaminated/replay fault-injection results |
| [C03 — Handoff #9 recovery](./C03-HANDOFF-RECOVERY.md) | Source fencing, successor proof, Stop/relaunch and safe project switching | Per-CLI effective isolation, session compatibility, physical cessation, timing and guard schemas |
| [C04 — typed owner-boundary inventory](./C04-TYPED-BOUNDARY-INVENTORY.md) | Existing records, owner commands and distinguishable result evidence | Enabled-slice payload schemas and runtime proof |
| [C05 — durable transactions and race handling](./C05-DURABLE-TRANSACTION-RACES.md) | Persist/ACK ordering and 22 crash/concurrency/replay outcomes | Transaction code, fault injection and native confinement proof |
| [C06 — binding product decisions](./C06-BINDING-PRODUCT-DECISIONS.md) | Ten approved decisions covering authority, triggers, executable selection, evidence, storage, language, canonical bytes, keyboard, bounds and updates | Native implementation and proof |
| [C07 — versioned contract registry](./C07-VERSIONED-CONTRACT-REGISTRY.md) | JSON Schema 2020-12 shared types, commands, events, records, errors, evidence, policies and compatibility | Command-specific implementation payload schemas and runtime validator proof |
| [C08 — twelve state machines](./C08-TWELVE-STATE-MACHINES.md) | Commands, guards, transitions, events, illegal outcomes and restart/UNKNOWN behavior for exactly twelve machines | Implementation and fault-injection evidence |
| [C09 — selection, transaction and recovery](./C09-SELECTION-TRANSACTION-RECOVERY.md) | B1–B10, owner transactions, instance exclusion, MCF classes, publication recovery, approval/context/evidence receipts | Native SQLite/filesystem/CLI proof |
| [C10 — implementation entry gate](./C10-IMPLEMENTATION-ENTRY-GATE.md) | Documentation closure criteria and staged I0–I8 implementation/proof gates | Passing implementation evidence |
| [v1 machine-readable registry](./schemas/v1/README.md) | Core, boundary, machine and policy schemas plus canonical and adversarial vectors | Generated/runtime conformance evidence |

## Interpretation and completion rules

Each scenario records a source-derived **expected owner action or refusal** given specified durable facts. The tables are not machine-executable fixtures or observed results. Names such as UserContribution, BLOCKED, UNKNOWN, handoff phases and council outcomes refer to already-approved concepts; these packs add no service owner, state machine, model/provider, authorization path or independently selectable fallback.

Where the source fixes an outcome, C06–C09 now provide the documentation-stage product selection and common executable contract. **Both statements remain true:** prohibited behavior is already forbidden, and native proof is still required before the implementation may claim capability. A command remains disabled until its command-specific payload schema and implementation fixtures satisfy C07; this is an implementation slice gate rather than an open product decision.

If a derived trace disagrees with the authoritative specification, do **not** silently select either behavior for application code: report the conflict and block affected implementation pending reconciliation. Scope-based preparatory documentation and source inspection are distinct from a full autonomous product build.
