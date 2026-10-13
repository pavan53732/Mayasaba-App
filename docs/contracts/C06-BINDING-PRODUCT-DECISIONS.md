# C06 — Binding product decisions for documentation completion

**Status: BINDING PRODUCT AUTHORITY — approved by the user on 2026-10-13.** This contract fixes the remaining D-04, D-11, D-15, D-16, U2, B4, keyboard, operational-bound and executable-update choices. It does not claim that the Windows application, the three CLI adapters, containment, MSI behavior, or any native test has been implemented or verified.

The authoritative system description remains controlling. If a derived document conflicts with this pack, implementation blocks until the conflict is corrected; it does not choose the weaker rule.

## D1 — Explicit authority and immediate controls

Material approval, Project Close/Reopen, retention opt-in, destructive retirement, and Mayasaba executable activation require an explicit labeled UI action bound to the exact project, source record, decision or proposal revision, project epoch, action kind, and expiry/revocation status. Free-form text may request one of these actions but cannot silently satisfy its approval command.

The permanent Chat-header controls are **Start**, **Pause**, and **Stop**. A contextual **Resume** action appears on the paused-status card after Pause. The exact case-insensitive, whitespace-trimmed standalone aliases `/start`, `/pause`, `/resume`, and `/stop` are also controls. A slash command is control-only only when it is the complete non-whitespace contribution. Plain-language or mixed text is not converted into a control by keyword matching. A mixed request can use the visible control independently and then persist its text through the ordinary contribution path; neither part grants authority to the other. Ambiguous text receives an English clarification request.

## D2 — D-04 FULL-council trigger registry

A registered FULL point is required before affected authorization when a proposed successor would create, modify, supersede, or select among materially different:

1. approved product requirements, scope, or blocking acceptance criteria;
2. user-visible product behavior or data-retention promises;
3. layer, owner, authoritative record, state-machine, dependency-direction, or cross-owner transaction responsibility;
4. authority, approval, isolation, confinement, credential, publication, recovery, or destructive-action boundary;
5. persistent compatibility format, canonical-byte profile, migration behavior, or externally visible contract; or
6. alternatives whose consequences differ materially in safety, user data, supported behavior, or irreversible cost.

The following do not create a new point when they stay inside the current approved contract: duplicate delivery, retry, reconciliation, evidence refresh, bounded self-recovery, bounded repair that restores already-approved behavior, mechanical schema/code generation, typo/link/format correction, and presentation-only adjustment with no semantic or accessibility change.

The deduplication key is `(project_id, approved_epoch, trigger_class, normalized_subject, affected_authority_ids, baseline_contract_digest)`. Normalization uses the canonical-byte profile; set order is sorted and duplicates are removed. One nonterminal point may exist for a key. A later materially changed baseline or authorized successor epoch creates a new key and predecessor link. The Orchestrator registers the point before affected plan authorization, lease issuance, publication, migration, or executable activation.

## D3 — D-11 executable selection

For each CLI, a user-selected executable that passes identity, access, native-communication, process-control, and required capability probes has priority and is pinned. Without a user selection, exactly one verified discovery candidate may be pinned automatically. Two or more verified candidates require an explicit **Choose executable** action; ordering in `PATH`, discovery time, filename, or version label never breaks the tie.

The pin contains CLI identity, canonical path, stable Windows file identity when available, SHA-256, signer observation when available, discovery source, probe profile/version, and observation time. A missing file, path retarget, identity/hash change, or externally replaced binary changes readiness to `CHECKING` and requires a fresh probe. It never silently switches to another installation. Release numbers are not compatibility gates.

## D4 — D-15 criterion-specific evidence sufficiency

| Claim class | Minimum adequate evidence | Insufficient by itself | Unavailable result |
| --- | --- | --- | --- |
| Compilation/linking | Clean invocation for the declared toolchain/configuration, captured diagnostics, exit observation, exact produced artifact identity/hash | Source inspection or an old artifact | `INCONCLUSIVE` or `BLOCKED` when required |
| Deterministic logic | Adequacy-reviewed automated test with positive, negative and boundary vectors where applicable, exact binary/source identity, observed result | Agent-authored assertion or test existence | `INCONCLUSIVE` or `BLOCKED` |
| Native runtime behavior | Controlled observation of the exact artifact on a supported Windows target with environment and effect evidence | Compilation, process launch, or exit zero | `INCONCLUSIVE` or `BLOCKED` |
| WinUI/accessibility behavior | Native UI Automation evidence plus a declared manual check only where automation cannot discriminate the criterion | HTML preview or screenshot alone | `INCONCLUSIVE`; blocking manual criteria require user evidence |
| Confinement/security denial | Adversarial pre-effect denial and revocation tests for the actual CLI, descendants/services, profile, paths and network/tool capabilities | Prompt policy, staging, Job Object membership, or a parent-process result | `BLOCKED` |
| Publication/data recovery | Crash/concurrency fixture plus file identity/hash/journal evidence covering the selected B6 branch | SQLite intent or file existence | `BLOCKED` while outcome is `UNKNOWN` |
| MSI lifecycle | Actual preflight, install, launch, repair, upgrade and uninstall observations on each applicable matrix class | MSI build success | `INCONCLUSIVE` or `BLOCKED` |
| Subjective appearance/preference | Explicit user acceptance bound to the exact build/screenshot/artifact and criterion | Agent consensus or automated aesthetic score | `BLOCKED` pending user acceptance |

`NOT_APPLICABLE` requires a source-linked applicability proof; a validator or agent cannot self-declare it to avoid a failing criterion. A successful check certifies only its exact current criterion and artifact/environment identity.

## D5 — D-16 storage, cleanup, backup, draft and attachment policy

### Capacity and measurement

- Default local Mayasaba application-storage ceiling: **10 GiB per Windows user**.
- Required free-volume reserve after an admitted new storage operation: **5 GiB**.
- User-adjustable ceiling: **2–100 GiB**, subject to the reserve and current protected usage.
- Measured bytes include SQLite database/WAL/journal files, Mayasaba-managed evidence/attachments, staging, backups, diagnostics, logs and caches. The user's project-root files are excluded.
- At 80% of the effective ceiling, cleanup is requested. At the ceiling or reserve boundary, reclaim eligible disposable data first and then block unsafe new storage operations.
- Storage pressure never deletes or rewrites protected history, pending or uncertain submissions, unsent drafts, required attachments, active/recovery state, pending approval/question dependencies, or evidence supporting a current acceptance claim. If protected bytes keep usage above the limit, show an exact blocker and user controls to raise the ceiling or explicitly retire eligible data.

### Cleanup and backup

Cleanup is evaluated at application startup, once per changed 24-hour period, and after crossing 80% capacity. One batch processes at most 500 eligible objects or 30 seconds, whichever comes first, then yields. It uses the existing transactional plan, reference recheck and restart reconciliation rules.

A coordinated backup is requested before each schema migration and at most once per changed 24-hour period. The database and every referenced managed byte form one recorded snapshot boundary. Keep three recent verified restorable backups. Backup bytes count toward the ceiling. If three verified copies cannot coexist while preserving protected data and the reserve, block the migration or other operation that requires a new backup and explain the required capacity action; never silently discard the last required verified set.

### Drafts and attachments

Unsent draft text, draft metadata, and temporary selected-attachment copies are protected to the current Windows user using Windows user-bound protection and access controls. They are isolated by project and are not visible to another Windows account. A draft survives project switching, application restart and Project Close until successful Send or explicit **Discard draft**. There is no automatic draft deletion. Discard reconciles uncertain persistence first and then removes only that draft's controller-managed bytes.

One submission may contain at most **20 files**, **100 MiB per file**, and **500 MiB aggregate**. Preparation has **120 active seconds**. Archives are accepted only as opaque files unless an operation separately authorizes bounded extraction; preview/parser limits never change accepted source bytes. If identity, length, or last-write observation changes during copy, preparation fails for that attachment, the whole draft remains unsent, and the UI offers **Reselect** or **Remove**. It never substitutes earlier bytes or silently omits the file.

Optional 365-day closed-project history retirement remains off by default. Opt-in identifies the project, policy version, start clock and estimated effect. The user receives warnings 30 days and 7 days before eligibility. Withdrawal before the retirement commit prevents future retirement; it cannot restore already lawfully retired records. Authority, recovery, pending approval/question, and current acceptance dependencies remain protected regardless of age.

## D6 — U2 English-only mixed-content policy

Mayasaba-owned prose, agent-authored replies, proposals, critiques, plans, questions, narrative evidence, accessibility announcements, notifications, and generated/published deliverables must be English. A technical-identity exception never admits unrestricted non-English agent prose or deliverables.

Exact tokens may remain byte-for-byte unchanged only when altering them would break identity or executable correctness: source-code identifiers and literals required to preserve existing code behavior, canonical paths/filenames, URLs, hashes, CLI/product names, externally assigned legal/source identifiers, and user-provided proper names. Mayasaba labels and explains them in English, does not translate them, and records their source/provenance. The exception applies only to the smallest exact token span; surrounding narrative remains English.

Non-English natural-language source passages remain protected source bytes. User-facing presentation refers to them with an English source label, media/type, ordinal and digest. The user's own submitted Chat contribution remains byte-faithfully visible only in that contribution's user-message bubble and history/detail view so the immutable input can be recognized and audited; its status, labels and accessibility description remain English. Mayasaba does not repeat that prose in summaries, notifications, generated output or deliverables. Language admission is deterministic, versioned and auditable; uncertainty blocks the affected generated output and preserves the original bytes. It never silently edits user data or substitutes machine translation.

## D7 — B4 canonical bytes

Authoritative hashes use profile `mayasaba-jcs-1`: RFC 8785/JCS-compatible canonical JSON over the I-JSON subset, encoded as UTF-8 without a byte-order mark. Object keys use JCS ordering; insignificant whitespace is absent. Duplicate keys, unpaired surrogates, invalid UTF-8, NaN, infinities and negative zero are rejected. Strings are hashed as their exact Unicode scalar sequence with **no silent normalization**.

Security-relevant integers outside `[-9007199254740991, 9007199254740991]`, exact decimals, byte lengths, monotonic counters and identifiers that could lose JSON-number precision are schema-defined canonical decimal strings. Timestamps use UTC RFC 3339 with exactly three fractional digits and `Z`; UUIDs are lowercase canonical text. SHA-256 digests are lowercase 64-character hexadecimal. Every hashed record carries the profile identifier. No authoritative hash is written until the golden accept/reject vectors in the v1 registry pass across independent runs.

## D8 — Keyboard behavior

Default composer behavior is **Enter sends** and **Shift+Enter inserts a newline**. The per-Windows-user accessibility preference switches to **Enter inserts a newline** and **Ctrl+Enter sends**. Both modes expose their shortcut in the Send control's accessible help text. Changing the preference never submits or alters the current draft.

## D9 — Operational clocks, deadlines and finite attempts

All elapsed bounds use monotonic active time in-process; UTC wall time is recorded for audit. Windows suspend does not consume active time and never resets attempts, authority or an absolute wall bound. Resume first records the discontinuity and reconciles current authority, process/session identity and physical effects.

| Operation | Initial bound | Retry/terminal rule |
| --- | --- | --- |
| CLI detection/probe | 30 active seconds | At most two attempts; changed identity starts a new probe operation |
| Project-session startup | 120 active seconds per CLI | At most two attempts, and only after a known failure; `UNKNOWN` cannot retry |
| MCF receipt acknowledgement | 15 active seconds | Three redeliveries after 2, 5 and 15 seconds; material obligation dead-letters and blocks, disposable heartbeat may expire after five minutes |
| Liveness/stall assessment | 60 active seconds without qualifying progress | Two independent liveness observations 15 seconds apart before recovery; silence alone does not prove cessation |
| Stop | Issue immediately; 10-second graceful window; controlled termination; 20-second reconciliation window | Expiry ends waiting only. If physical cessation is not independently known, retain `UNKNOWN` evidence and affected `BLOCKED`; never report confirmed stop |
| One agent Chat contribution/review | 10 active minutes | Missing mandatory participation blocks that contribution; no reduced-agent fallback |
| Complete Chat turn | 30 active minutes | Preserve saved contribution and resumable `BLOCKED` turn |
| FULL council round | 30 active minutes | Existing five-round point cap; incomplete participation remains incomplete and blocked at its bound |
| Task execution | 60 active minutes by default | Task contract may set a shorter value or up to 8 active hours; longer requires renewed explicit user authorization |
| Recovery reconciliation | Three attempts within 10 active minutes | Preserve `UNKNOWN` and record affected `BLOCKED` when unresolved |
| Same-signature repair | Three attempts without objective improvement | Record diagnostics and `BLOCKED`; changed hypothesis/evidence creates a linked strategy, not a reset by relabeling |

An operation also has a 24-hour wall-age reauthorization point. Reaching it does not cancel or classify a physical effect; it prevents new dependent effects until current authority and observations are reconciled. No automatic retry is permitted for an unresolved material `UNKNOWN` outcome.

## D10 — Governed Mayasaba executable updates

Mayasaba has no autonomous updater. An evidence-linked self-improvement proposal may lead to isolated candidate work, applicable FULL council, and explicit user approval. Activation is a separate **Install approved Mayasaba update** action bound to the exact signed MSI identity, SHA-256, approved change decision, supported-platform preflight, verified backup and acceptance matrix.

The running application never overwrites itself. Activation closes or safely holds controller activity, exits, invokes the verified MSI path, and records the separately observed install and relaunch results. Failed activation preserves the prior verified installation when the MSI transaction or an independently proven recovery branch makes that safe; otherwise it reports `UNKNOWN`/`BLOCKED` and does not claim rollback. No improvement proposal, council result, downloaded package, timer, or startup check activates an executable without the explicit action.

## Documentation and proof boundary

These ten decisions are product authority. The C07–C10 contracts translate them and existing invariants into schemas, transitions, algorithms and entry gates. They do not turn planned tests into passed native evidence. Windows, CLI, confinement, MSI and runtime claims remain unverified until their implementation-stage evidence matrices pass.
