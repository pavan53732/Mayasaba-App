# Mayasaba — Derived contract trace corpus

**Documentation only. No executable schema, app implementation, test run or Windows/CLI proof is claimed.** The [authoritative system description](../Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md) takes precedence over these derived documents. The [readiness audit](../IMPLEMENTATION-READINESS-AUDIT.md) and [completion register](../CONTRACT-COMPLETION-REGISTER.md) remain the sources for open contract blockers.

| Pack | Approved invariants made traceable | Still OPEN |
| --- | --- | --- |
| [C01 — Chat submission and immediate controls](./C01-CHAT-SUBMISSION-CONTROLS.md) | Persist-before-dispatch, one Send identity, mixed Stop + request, control-only turn and three-agent reply gate | Versioned schemas, control-recognition predicates, SQLite/outbox transaction and bounds |
| [C02 — FULL council D1–D3](./C02-FULL-COUNCIL-D1-D3.md) | Project-local D1 rotation, six critiques, both nonchair reviews and ten-level D3 precedence | D-04 triggers/materiality, round machine transaction, normalized position equality |
| [C03 — Handoff #9 recovery](./C03-HANDOFF-RECOVERY.md) | Source fencing, successor proof, Stop/relaunch and safe project switching | Per-CLI effective isolation, session compatibility, physical cessation, timing and guard schemas |

## Interpretation and completion rules

Each scenario records a source-derived **expected owner action or refusal** given specified durable facts. The tables are not machine-executable fixtures or observed results. Names such as UserContribution, BLOCKED, UNKNOWN, handoff phases and council outcomes refer to already-approved concepts; these packs add no service owner, state machine, model/provider, authorization path or independently selectable fallback.

Where the source fixes an outcome but omits an exact field type, command/error schema, transaction, idempotency-key construction or proof, **both statements remain true:** the prohibited behavior is already forbidden, **and** the executable contract remains incomplete. No Z-/D-/B- finding closes until its required schema, concurrency/crash behavior, selected bounds and verifiable evidence are present.

If a derived trace disagrees with the authoritative specification, do **not** silently select either behavior for application code: report the conflict and block affected implementation pending reconciliation. Scope-based preparatory documentation and source inspection are distinct from a full autonomous product build.
