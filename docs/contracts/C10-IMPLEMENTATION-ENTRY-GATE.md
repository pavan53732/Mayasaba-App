# C10 — Documentation closure and implementation entry gate

**Status: NORMATIVE GATE.** Mayasaba's documentation stage may close when the product decisions, representation contracts, state transitions, selection algorithms, recovery branches and acceptance oracles are internally complete. Documentation closure does not assert that any native implementation or proof exists.

## Documentation-closure criteria

All of the following must pass in the repository:

1. The ten C06 product decisions and three safety clarifications appear in the authoritative system description and relevant derived documents without contradictory `OPEN` wording.
2. C07 and the v1 schema registry parse successfully, all local `$ref` files exist, shared types are bounded, and canonical vectors contain verified expected hashes/rejections.
3. C08 names exactly twelve machines, one owner per machine, every state, registered command, guard, event/result, illegal transition and restart/unknown branch.
4. C09 fixes B1–B10, owner commit/outbox/receipt ordering, instance exclusion, MCF class/replay/expiry, approval routing, context application, publication recovery and clocks.
5. Every C05 T01–T22 and completion-register F01–F30 fixture maps to one expected result and governing contract.
6. C04 owner inventory, contract index, authoritative specification, architecture, UI/UX, user-visible screens, README, AGENTS, readiness audit and completion register agree.
7. Relative file links and heading anchors resolve; JSON parses; Markdown tables are structurally complete; all numbered UI scenarios remain consecutive and new scenarios append consecutively.
8. A fresh-context reader can determine one owner, command, guard/result and recovery path for the adversarial questions without inventing product behavior.

If any criterion fails, documentation remains open for that scope. Passing closes specification ambiguity, not runtime evidence.

## Dependency and toolchain selection

The product technology choices remain C++20, WinUI 3/C++/WinRT, Windows App SDK, SQLite, CMake/MSBuild/CTest and WiX. Exact dependency revisions, licenses and hashes are selected immediately before the first build in a reviewed offline-restorable manifest because available supported revisions change over time. This is a deterministic implementation setup transaction, not product discretion:

1. Use the latest stable release compatible with the pinned MSVC/Windows SDK/Windows App SDK baseline at selection time.
2. Prefer official upstream source/package, permissive compatible license and reproducible SHA-256.
3. Record name, version, source URL, license/SPDX, artifact hash, dependency graph and restoration procedure.
4. Run schema-dialect/parser/canonical-vector tests before accepting JSON components; run SQLite configuration/recovery tests before accepting store configuration; run unit/UI harness discrimination tests before accepting test tooling.
5. If a candidate fails, evaluate the declared alternative under the same evidence. Do not silently download or fall back.
6. A dependency update later is a versioned manifest change; material compatibility/persistent-format/security changes invoke D-04.

Candidate names in the architecture are not authorization to download. The implementation agent must obtain the user's normal dependency acquisition authorization when the action will modify the machine or contact external services.

## Implementation sequence and gates

### Gate I0 — Documentation closed

Required evidence: all documentation-closure criteria above pass. Permits: repository/toolchain inspection and implementation planning. Does not permit a runtime capability claim.

### Gate I1 — Local contract/core slice

Implement schema loading/validation, canonical bytes, IDs/errors/receipts, single-writer SQLite transaction/outbox, instance exclusion and deterministic replay in the UI-independent core. Required evidence includes canonical vectors, stale/duplicate/unknown-commit fault injection, migration/backup restore and event-chain completeness. Only passing slices may be used by higher layers.

### Gate I2 — Native shell and responsiveness prototype

Implement the WinUI 3 shell, onboarding, bound-project Chat, theme/accessibility and sustained projection/event rendering without CLI-dependent authority. Required local Windows evidence is a responsive UI under sustained events and the documented UI scenarios applicable to this slice. The HTML preview remains illustrative.

### Gate I3 — Process and containment prototypes

For each CLI and each admitted operation profile, prove controlled suspended launch/Job assignment/streams/cancellation plus independent pre-effect prevention or effective OS confinement for prohibited reads, writes, network/tools, reparse escapes, stale lease effects, descendants and services. Staging alone cannot pass. A failed profile is `UNSUPPORTED`/`BLOCKED`; it does not weaken policy.

### Gate I4 — One vertical owner workflow

Implement one bounded end-to-end path, beginning with contribution persistence or another separately authorized low-effect slice, using the exact owner schemas/transitions/transactions and evidence. Run duplicate, lost-ACK, crash/restart, cross-project and Stop races. Do not enable unconstrained generic payloads.

### Gate I5 — Three-CLI coordination and FULL council

Implement verified context application, all-three Chat response/review, D1/D2/D3 FULL rounds, barrier and Handoff #9. Evidence must cover missing participants, contaminated independence, changed synthesis revision, five-round/incomplete bounds, replacement and current-plan application. No two-agent fallback.

### Gate I6 — Isolated execution, publication and recovery

Implement task/lease fences, authorized workspaces, validation/repair, durable B6 publication and user-edit-safe recovery. Pass all applicable T/F fixtures plus native concurrent edit/crash tests before writing the canonical root.

### Gate I7 — Retention, backup, closure and governed update

Implement C06 storage/draft/attachment policy, B2/B3 backup/store behavior, closure/reopen/retirement and signed MSI update activation. Pass capacity/protected-data, restore, interrupted cleanup, closure race, install/repair/upgrade/uninstall and failed-update tests.

### Gate I8 — Supported release certification

Run the complete criterion matrix on Windows 11 25H2 build 26200 native x64 for the declared editions outside S mode. Every blocking criterion must be current `PASS` or justified `NOT_APPLICABLE`. Missing platform/CLI/containment/MSI evidence is `INCONCLUSIVE`/`BLOCKED`; it cannot be converted into documentation success.

## Required evidence packet per gate

Each gate packet records source requirements/decisions, exact code/artifact digests, environment/toolchain/dependency manifest, commands and observed outputs, fault injections, criterion-to-oracle adequacy, verdicts, unresolved physical outcomes and limitations. Agent narrative and clean exit are supporting context only.

## Stop conditions

Implementation stops for the affected slice when it encounters an actual contradiction in the authoritative documents, an unregistered material behavior, an unconstrained material payload, ineffective confinement, inadequate oracle, stale authority, protected-storage capacity blocker, unknown material effect, or unsupported platform. It records the exact blocker and continues only unrelated authorized work.

## Meaning of “documentation closed”

Documentation closed means two independent readers can derive the same permitted command, owner transaction, observable result and recovery from the same initial facts. It does not mean the application exists, the architecture is feasible on every installed CLI, Windows tests passed, or a release can be shipped. Those claims become available only through Gates I1–I8.
