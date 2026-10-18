# Mayasaba — UX and Capability Enhancement Contract

**Status:** Subordinate enhancement contract. Derives from the authoritative system description, AGENTS.md, ARCHITECTURE.md, UI-UX.md, USER-VISIBLE-SCREENS.md, THREE-SCREEN-UX-IMPLEMENTATION.md, PROJECT-SELECTION.md and C01–C11. It introduces no new architectural authority, owner, state machine or persistence path.

**Scope:** Proposed and approved enhancements to the native three-screen Chat control room. Implementation status of each capability is stated per feature; documentation completion is not runtime acceptance.

**Precedence:** User's current explicit instruction → Authoritative system description → AGENTS.md → ARCHITECTURE.md / UI-UX.md → this subordinate contract → code/tests.

---

## 1. Status vocabulary

Every capability or criterion uses exactly one of:

- `PROPOSED` — design only, no implementation commitment.
- `APPROVED` — product decision recorded; implementation not started.
- `IMPLEMENTED` — code exists in the repository.
- `VERIFIED` — runtime evidence exists on a native Windows build with a single honest verdict.

Runtime acceptance uses only `PASS`, `FAIL`, `INCONCLUSIVE`, `BLOCKED`, `NOT_APPLICABLE` (AGENTS.md §13).

---

## 2. Common architectural rules for all 12 capabilities

1. **Read-only projections.** Each capability is a projection over authoritative owner records accessed through typed queries (C07). None creates an approval, resolves a task, invents a failure, retries an operation, or marks completion.
2. **No new owners or state machines.** The 13 layers and 12 state machines are unchanged. Search, inspectors, Action Center and command palette introduce no new status machine.
3. **No new persistence authority.** Storage executes SQL and persists owner-authorized transactions only. Derived indexes must be rebuildable and non-authoritative.
4. **Project isolation.** Every projection is project-scoped. Records from project A never appear in project B.
5. **English-only presentation.** All labels, states, errors and accessibility text are English. Non-English source bytes remain protected and are presented only via English label/type/ordinal/digest (AGENTS.md §5).
6. **Failure honesty.** `UNKNOWN`, `MISSING`, `DENIED` and `EMPTY` are distinct visual states. Missing owner services render a truthful unavailable state, never a fabricated empty queue.
7. **Authority separation.** User action, authority, evidence, process observation and certified completion remain distinct. Agent claims, builds and majorities never certify.
8. **Contextual surfaces only.** No fourth screen, permanent navigation rail, dashboard, standalone Council/Task/Files page, IDE or terminal. Dense detail opens inline or in a temporary dismissible sheet that restores Chat focus and reading position.
9. **Keyboard/accessibility.** Every capability is fully keyboard operable, has stable UI Automation names/roles, supports Narrator, high contrast, reduced motion, DPI scaling and responsive layout.

---

## 3. Capability register

| ID | Capability | Priority | Status | Primary dependency |
|---|---|---|---|---|
| UXE-01 | Action Center in Chat | P0 | PROPOSED | Available owner records |
| UXE-02 | Universal Search | P0 | IMPLEMENTED (saved user messages subset; native acceptance incomplete) | Current project contributions; remaining scope PROPOSED |
| UXE-03 | Native Diff Inspector | P0 | PROPOSED | Staged-change records (Workspace Manager) |
| UXE-04 | Smart Project Explorer | P1 | IMPLEMENTED (Screen 2 filtering subset; native acceptance incomplete) | Local project registry; remaining scope PROPOSED |
| UXE-05 | Council Comparison View | P1 | PROPOSED | Council/Decision/Evidence records |
| UXE-06 | Task Dependency View | P1 | PROPOSED | Task/DAG Engine records |
| UXE-07 | Evidence Timeline | P1 | PROPOSED | Event/Evidence/owner records |
| UXE-08 | Source-linked Research Viewer | P1 | PROPOSED | Authorized research records |
| UXE-09 | Artifact Review | P1 | PROPOSED | Managed attachment/artifact records |
| UXE-10 | Recovery Inspector | P1 | PROPOSED | Recovery/evidence records |
| UXE-11 | Command Palette | P2 | PROPOSED | Typed application commands |
| UXE-12 | Project Evidence Export | P2 | PROPOSED | Evidence + authorization |

Status values above reflect documentation; runtime capability is `BLOCKED` until the named dependency and C10 gates are evidenced.

---

## 4. Capability specifications

### UXE-01 — Action Center in Chat (P0)

**Objective.** A compact, contextual "Needs your attention" surface inside Chat listing actionable obligations for the current project: pending user questions, pending approvals, blocked operations, failed applicable checks, unknown outcomes requiring reconciliation, and recovery choices authorized by the relevant owner.

**User experience.** Each item shows type, short English explanation, affected operation, grounded severity/urgency, and one safe next action. Selecting an item reveals source-linked details in Chat or a temporary sheet. The user can return to the prior reading position. Dismissing a visual item does not complete its underlying obligation.

**Source records.** Pending questions, approvals, blocked/failed/unknown operation records, recovery obligations, and their owner identities — all from existing owner services. Canonical owners: Requirement/Decision services (questions/approvals), Orchestrator/Validation/Repair Engine (blocked/failed checks), affected operation owners (unknown outcomes), Policy/owning service (recovery choices).

**Queries.** Typed, project-scoped, read-only queries returning projection records with owner identity, record version, projection time, completeness (`COMPLETE`/`PARTIAL`/`STALE`). No SQL from WinUI.

**Commands.** Any approval, response or recovery action uses the existing typed command boundary and its authorization/identity checks. The Action Center itself issues no command.

**WinUI.** A compact strip/region within the Chat layout (per directive §21), inline item cards, temporary detail sheet. No permanent panel consuming most of the window.

**States.** Empty (no items — truthful, not fabricated), loading, stale, unknown, denied, unavailable (owner service not implemented), failure. Project A items never appear in project B.

**Privacy/isolation.** Project-scoped only; no cross-project leakage; no credentials/private agent traffic.

**Keyboard/a11y.** Full keyboard traversal, stable automation names/roles, Narrator announcements, focus restoration on dismiss, high contrast, reduced motion, DPI, responsive.

**Acceptance.** Pending, multiple, stale, resolved, rejected, unauthorized and unknown cases; dismissal does not imply completion; project isolation verified.

**Dependencies.** Available owner records (M4 for most). Until then, render truthful unavailable state.

**Proof required.** Native runtime evidence of each state and isolation; single honest verdict per criterion.

---

### UXE-02 — Universal Search (P0)

**Objective.** Compact search within Chat over authorized, project-scoped records.

**Status & Implemented Scope (2026-10-18):** `IMPLEMENTED (native acceptance incomplete)` for current-project saved user messages. The native WinUI 3 interface provides `Ctrl+F` and `SearchMessagesButton` in the Chat header, opening an overlay `MessageSearchPanel` (`Canvas.ZIndex="20"`) with `MessageSearchBox`, clear action, `SearchResultsList`, neutral English provenance labels for all results, with original text available in its permitted conversation/history view, and BringIntoView navigation by durable submission identity with visual elevation highlight. Storage implements `SearchUserMessages` executing parameterized SQL (`LIKE ? ESCAPE '\'`) pre-pagination across all project contributions, deterministic ordering (`row_id ASC`), and input/result clamping. Other authorized owner-record scopes remain `PROPOSED`. Drafts, pending submissions, raw CLI output and private agent traffic remain excluded; this status does not authorize expanding search into them.

**Scope (enable only when records available).** Saved conversation; approved requirements/decisions; task identifiers/statuses; file/artifact metadata; evidence/validation references; controller events for the selected project.

**User experience.** Query input, scope filters, results with source type, title/snippet, current project, timestamps/provenance, keyboard selection, open-in-context (select/reveal original Chat record or authorized detail sheet). Empty-results and search-failure states.

**Backend.** Bounded, project-scoped typed query service. No arbitrary SQL or folder scanning from WinUI. Derived, rebuildable index only; not an authoritative store. Do not assume SQLite FTS without verifying the Windows SQLite build. Redact sensitive drafts, credentials, hidden private agent traffic, unauthorized sources.

**WinUI.** Compact search action/surface within Chat; temporary sheet or inline region; focus restoration.

**States.** Empty query, no results, loading, stale index, search failure, denied scope, unavailable source.

**Privacy/isolation.** Project-scoped; no leakage.

**Keyboard/a11y.** Keyboard selection, focus management, Narrator, high contrast, reduced motion, DPI, responsive.

**Acceptance.** Exact/partial matches, large histories, empty results, deleted/unavailable sources, project isolation, stale indexes, keyboard navigation, memory usage.

**Dependencies.** Typed query service + records; FTS availability proof.

**Proof required.** Native runtime evidence including large-history performance.

---

### UXE-03 — Native Diff Inspector (P0)

**Objective.** Inspect exact proposed file modifications before authorized publication.

**User experience.** Temporary native sheet showing: affected file identity and canonical project-relative path; original/baseline revision; proposed revision; added/removed/modified lines; unified or side-by-side comparison; originating task/attempt and requirement; applicable validation results; publication eligibility and blockers. Distinguish proposed/authorized/validated/published/certified. Never present passing checks as blanket certification.

**Technical.** Consume immutable, versioned diff records from Workspace Manager (or applicable authorized owner). No second editing/publication path. Bounded virtualization for large diffs. Binary files: metadata, digests, supported comparison info (not misleading text diffs). Untrusted file content treated as data; never execute embedded code/macros/instructions.

**States.** No changes, loading, stale baseline, changed baseline, conflicting user edits, inaccessible evidence, publication denied, binary/unsupported, large diff.

**Privacy/isolation.** Project-scoped; no cross-project.

**Keyboard/a11y.** Full keyboard, focus restoration, Narrator, high contrast, reduced motion, DPI, responsive.

**Acceptance.** New/modified/deleted/renamed/binary/large/stale files; conflicting edits; changed baseline; inaccessible evidence; publication denial. Diff review never bypasses approval/integration gates.

**Dependencies.** Staged-change records + Workspace Manager (M5).

**Proof required.** Native runtime evidence; single honest verdict per criterion.

---

### UXE-04 — Smart Project Explorer (P1)

**Objective.** Extend Project Selection with local filtering and path inspection.

**Status & Implemented Scope (2026-10-18):** `IMPLEMENTED (native acceptance incomplete)` for Screen 2 Recent Projects filtering. The native WinUI 3 interface provides `RecentProjectsFilterBox`, `RecentProjectsClearFilterButton`, and `RecentProjectsStatusFilter` (`All`, `Open`, `Closed`) on Screen 2. Filtering matches project names and canonical paths across the full authorized registry pre-pagination via `Storage::QueryRecentProjects` using parameterized SQL (`LIKE ? ESCAPE '\'`) with literal `%` and `_` escaping. Asynchronous queries are protected by `recentQueryGeneration_` against out-of-order execution, stale completions, and UI overwrites. Full-path tooltips and keyboard navigation are supported. Whole-PC discovery, external filesystem scanning, and folder tree exploration remain excluded; remaining scope remains `PROPOSED`.

**User experience.** Filter recorded recent projects by name, recorded canonical path, open/closed status. Easy full-path inspection via keyboard and pointer. Visually distinguish: open existing folder, explicitly create new project folder, open recorded recent project, locate moved project, resolve missing/mismatched identity. Distinguish missing path from deleted record.

**Backend.** Filter only Mayasaba's existing project records. No whole-PC discovery or folder scan. Revalidate filesystem identity on open per project-selection contract. No silent reopen of closed projects, identity replacement, draft transfer or session retargeting.

**WinUI.** Within existing Screen 2; no new screen.

**States.** Zero/one/dozens/hundreds of records, case-insensitive filtering, long paths, missing paths, identity conflicts, paging, cancellation.

**Privacy/isolation.** Local project registry only.

**Keyboard/a11y.** Keyboard filtering, focus, Narrator, high contrast, reduced motion, DPI, responsive.

**Acceptance.** Zero/one/dozens/hundreds; case-insensitive; long paths; missing paths; identity conflicts; paging; keyboard focus; cancellation.

**Dependencies.** Existing project records (available now).

**Proof required.** Native runtime evidence.

---

### UXE-05 — Council Comparison View (P1)

**Objective.** Display FULL Council structured results in one readable temporary sheet.

**User experience.** Structured comparison: three independent proposals; alternative approaches/tradeoffs; six directed peer critiques with targets and dispositions; rebuttals/revisions; shared assumptions and unresolved disagreements; source-linked evidence; current synthesis and binding-decision status. User understands why an approach was accepted/questioned/blocked.

**Technical.** Use only canonical Council, Evidence, Decision owner records. Preserve exact three-participant protocol, six directed critiques, writer/chair rules, round bounds, final-review requirements. Apply C11 substantive quality requirements. No private chain-of-thought. No inference of participation from missing records; no counting unsupported agreement as evidence. No majority-vote authority.

**States.** Incomplete critiques, stale proposal revisions, unavailable agents, contradicting evidence, disputed assumptions, missing synthesis review, fully valid rounds, safely blocked rounds.

**Privacy/isolation.** Project-scoped.

**Keyboard/a11y.** Full keyboard, focus restoration, Narrator, high contrast, reduced motion, DPI, responsive.

**Acceptance.** As above. No live Council capability claimed from fixture-only demonstrations.

**Dependencies.** Real Council/Decision/Evidence records (M4).

**Proof required.** Native runtime evidence with real records.

---

### UXE-06 — Task Dependency View (P1)

**Objective.** Inspectable task graph in a temporary sheet.

**User experience.** Task identity/objective, dependency relationships, assigned CLI, observed state, blockers/prerequisites, task attempt and current lease info where authorized, linked requirements/validation/evidence. Compact graph + accessible list/tree representations. Graph must not be the only way to understand dependencies.

**Technical.** Task/DAG Engine remains sole owner of task state, graph revision, leases. UI cannot assign leases, change dependencies or mark tasks complete via graph manipulation.

**States.** Cycles/invalid graphs, blocked dependencies, stale leases, canceled work, superseded tasks, many-node performance, screen-reader equivalents.

**Privacy/isolation.** Project-scoped.

**Keyboard/a11y.** Full keyboard, focus restoration, Narrator, high contrast, reduced motion, DPI, responsive; non-graph alternative always available.

**Acceptance.** As above.

**Dependencies.** Task/DAG Engine records (M4).

**Proof required.** Native runtime evidence.

---

### UXE-07 — Evidence Timeline (P1)

**Objective.** Source-linked investigation view ("Why did this happen?") for one request/task/operation.

**User experience.** Recorded progression: (1) user contribution persisted, (2) interpretation and authorized scope established, (3) relevant decision/approval recorded, (4) work attempt and agent participation observed, (5) validation executed, (6) publication outcome established, (7) final certification or unresolved blocker recorded. Each step shows actual owner, identity, timestamp, evidence reference, known/unknown status.

**Technical.** Read only from authoritative event/evidence/owner records. No fictional chronological story from agent prose. Differentiate observation time, event time, successful commit time where relevant.

**States.** Missing evidence, uncertain commits, stale context, failed validation, partial publication, superseded outcomes, complete verified chains. Absent step explicitly marked unavailable/unverified.

**Privacy/isolation.** Project-scoped.

**Keyboard/a11y.** Full keyboard, focus restoration, Narrator, high contrast, reduced motion, DPI, responsive.

**Acceptance.** As above.

**Dependencies.** Event/evidence/owner records (M4).

**Proof required.** Native runtime evidence.

---

### UXE-08 — Source-linked Research Viewer (P1)

**Objective.** Comparison of research contributions and attributed sources.

**User experience.** Display research question, authorized scope, which CLI produced each finding, source title/origin, evidence/citation identity, freshness/verification status, contradictory findings, remaining uncertainty. Group by question/claim.

**Technical.** Do not conflate three agents repeating one claim with three independent sources. Research results attributed to actual producer and source. No browsing/download/network access from viewer. New research requests go through existing policy/work-request owners.

**States.** Stale sources, inaccessible citations, unsupported claims, conflicting evidence, duplicate sources, incomplete participation.

**Privacy/isolation.** Project-scoped.

**Keyboard/a11y.** Full keyboard, focus restoration, Narrator, high contrast, reduced motion, DPI, responsive.

**Acceptance.** As above.

**Dependencies.** Authorized research records (M4).

**Proof required.** Native runtime evidence.

---

### UXE-09 — Artifact Review (P1)

**Objective.** Expand native attachment inspection into safe generated-artifact review.

**User experience.** Display supported artifacts (text, code, images, generated documents) via safe read-only viewers when supported. Show name/type, size, content digest, originating contribution/task, current managed-file identity, available preview, verification/publication status, applicable validation evidence.

**Safety.** No preview executes scripts/macros/binaries/active document content. Bounded preview size/memory/parsing limits. Unknown/unsupported/corrupt/potentially unsafe files get metadata-only card + explicit preview-unavailable explanation. Honor English-only presentation including restrictions on unverified non-English source text. A submitted attachment is not implied interpreted by all three agents.

**States.** Valid, unsupported, oversized-preview, corrupt, binary, multilingual, missing, tampered.

**Privacy/isolation.** Project-scoped.

**Keyboard/a11y.** Full keyboard, focus restoration, Narrator, high contrast, reduced motion, DPI, responsive.

**Acceptance.** As above; opening a preview never modifies source files.

**Dependencies.** Managed attachment/artifact records (available for attachments; M4 for generated artifacts).

**Proof required.** Native runtime evidence.

---

### UXE-10 — Recovery Inspector (P1)

**Objective.** One contextual view of recoverable/uncertain operations.

**User experience.** For each recovery obligation: operation identity, project identity, last known durable state, last observed physical state, why result is uncertain, available recovery evidence, safely permitted next action, consequences. Examples: unresolved Send, interrupted attachment preparation, failed validation, CLI session uncertainty, publication reconciliation.

**Authority.** Projection only. No retry/delete/overwrite/restore/terminate solely because inspector opened. Each recovery button invokes canonical owner command with expected versions, identity, authorization checks. `UNKNOWN` is neither success nor failure; never converted to automatic retry.

**States.** Restart, duplicated recovery requests, competing user action, stale identity, unavailable store, changed project files. Unsafe operations remain blocked until required evidence exists.

**Privacy/isolation.** Project-scoped.

**Keyboard/a11y.** Full keyboard, focus restoration, Narrator, high contrast, reduced motion, DPI, responsive.

**Acceptance.** As above.

**Dependencies.** Recovery/evidence records (M3/M4).

**Proof required.** Native runtime evidence.

---

### UXE-11 — Command Palette (P2)

**Objective.** Keyboard-oriented launcher for available native application commands.

**User experience.** Quick access to: search, open authorized detail view, Switch Project, inspect current project info, open pending Action Center items, open supported evidence/artifact views, execute currently permitted typed application commands. One documented accessible keyboard shortcut with alternatives where conflicts arise with active text input or assistive technology. Show current project, command name, availability, denial reason.

**Architecture.** A palette choice invokes the exact same application command as its visible native button. No new authority, no Start-permission bypass, no silent CLI launch, no hidden filesystem operations.

**States.** Keyboard-only use, focus restoration, unauthorized commands, changed project selection, modal cancellation, conflicting shortcut behavior.

**Privacy/isolation.** Project-scoped.

**Keyboard/a11y.** Full keyboard, focus restoration, Narrator, high contrast, reduced motion, DPI, responsive.

**Acceptance.** As above.

**Dependencies.** Typed application commands (available).

**Proof required.** Native runtime evidence.

---

### UXE-12 — Project Evidence Export (P2)

**Objective.** Export a portable, authorized record of project decisions and completion evidence.

**Export content.** Project/export identity; approved requirements/decisions; relevant saved contributions; council decisions/critique references; task/attempt records; validation results; artifact identities/digests; publication evidence; known omissions, redactions, unresolved outcomes.

**Security.** Deliberate user-authorized operation. Redaction before writing. No passwords, tokens, private agent reasoning, hidden CLI output, unrelated projects, protected data without authorization. Bounded deterministic export format with explicit manifest, source versions, integrity digests. Not described as certified if evidence incomplete.

**States.** Unauthorized export, redaction, large history, tampered evidence, changed source data, interrupted file writing, destination failure, deterministic output integrity.

**Privacy/isolation.** Project-scoped; redaction enforced.

**Keyboard/a11y.** Full keyboard, focus restoration, Narrator, high contrast, reduced motion, DPI, responsive.

**Acceptance.** As above.

**Dependencies.** Complete evidence + authorization (M6).

**Proof required.** Native runtime evidence.

---

## 5. Implementation milestones (summary)

- **M0** Baseline + documentation (this contract + sync). Done when docs consistent and every feature has owner, interaction model, dependency, oracle.
- **M1** Native UX verification (live theme/contrast/motion/keyboard/Narrator/DPI/performance).
- **M2** Low-dependency productivity: UXE-04, saved-Chat search subset of UXE-02, UXE-01 for available records, attachment drag-drop/preview (UXE-09 subset), keyboard improvements.
- **M3** CLI discovery + sessions.
- **M4** Real agent orchestration (replies, council, tasks, validation, evidence, publication).
- **M5** Advanced inspectors (UXE-03, UXE-05, UXE-06, UXE-07, UXE-08, UXE-09, UXE-10).
- **M6** Productivity/export completion (UXE-11, UXE-12, advanced search, redaction, performance).
- **M7** Packaging and release certification.

No milestone may weaken an existing safety or authority contract to make a UI feature appear complete.

---

## 6. Quality gates

Every implemented capability requires unit, integration, negative, crash/restart, WinUI acceptance, resource, security tests and evidence provenance (commands, outputs, environment, source commit, binary identity, results). Use exactly one verdict per criterion.

---

## 7. Change control

Material new behavior, persistent format changes or additional authorization require documentation + approval through existing governance (C06/C10) before enabling. Record unresolved contradictions instead of guessing.
