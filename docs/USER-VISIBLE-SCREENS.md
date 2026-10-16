# Mayasaba — complete user-visible screen diagrams

> **2026-10-15 approved navigation change:** Screen 2 is the reusable project selector. First binding: Welcome → selector → Chat. Later launches: selector → Chat. File → Switch Project returns to the selector. See [project selection contract](PROJECT-SELECTION.md). Native implementation remains pending.

## Purpose and status

This is the single visual reference for what a person sees from first launch through a completed or blocked project. It describes the intended **native Windows interface**, not a working implementation. The [complete system description](./Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md) is authoritative; [UI-UX.md](./UI-UX.md) supplies the detailed behavior and acceptance rules. The [interactive preview](./CHAT-INTERFACE-PREVIEW.html) illustrates the three main screens and composer movement with sample data.

There are **three main screens**: Welcome, Project selection, and Chat. Welcome appears until the first root is bound; Screen 2 Project selection also appears on later launches and project switching. Chat is the only persistent workspace. The later diagrams are **states within Chat**, not extra pages, routes, tabs, or dashboards. Text in square brackets represents a control. Names, paths, statuses, messages, and evidence in the diagrams are illustrative; the product must show current observed values. `Show details` means inline expansion or a temporary, dismissible sheet when the content is too large.

## End-to-end journey

```text
FIRST BINDING: Screen 1 Welcome -- Next / Skip / Escape --> Screen 2
LATER LAUNCH: ------------------------------------------> Screen 2

SCREEN 2 PROJECT SELECTION
  Recent project -- revalidate identity and safe switching --+
  Open existing folder -- native picker -- validate ---------+--> Chat
  Create new project folder -- explicit empty creation -----+
  Cancel switch --------------------------------------------> Old Chat
  Missing/unsafe root --> exact blocker; no binding change

SCREEN 3 CHAT
  Empty project --> centered composer
  Confirmed saved contribution / restored history --> bottom composer
  Start --> explicitly initialize three project-scoped sessions
  File > Switch Project --> Screen 2 (old project/draft preserved)
  Exit --> leave app; next launch shows Screen 2
```

Opening a folder refreshes bounded, non-mutating detection of all three CLIs and shows their individual readiness on screen 3. It does not start working agent sessions, scan files, initialize Git, or approve work. The readiness entries never block folder selection or Chat. Screen 2 has no Next-to-Chat button. No path opens Chat without an authorized project root. After the first project binding, later launches show Screen 2 Project selection; only Welcome is not automatically repeated. Selecting **Exit** on Screen 3 safely persists the active draft and exits the application.

The native Windows application menu is outside all three screen content areas. Once a project is bound, **File → Switch Project** opens Screen 2 Project selection, whose Open existing folder action opens the native picker. It changes the active Chat only after Project service validates the chosen root and the existing safe-switching guards pass. Chat itself has no folder control.

## Screen 1 — Welcome

```text
+--------------------------------------------------------------------------+
| Mayasaba                         Hermes Agent CLI  ● CHECKING            |
|                                  Kilo Code CLI     ● CHECKING            |
|                                  Claude Code CLI   ● CHECKING            |
+--------------------------------------------------------------------------+
|                                                                          |
|       Welcome to Mayasaba                                                |
|                                                                          |
|       Mayasaba coordinates the coding CLIs already on your PC and        |
|       checks their work before calling it done.                          |
|                                                                          |
|       You install and sign in to Hermes Agent CLI, Kilo Code CLI,        |
|       and Claude Code CLI yourself. Mayasaba does not change them.       |
|                                                                          |
|       A missing CLI does not stop you from opening a folder.             |
|                                                                          |
|                                                 [Skip]       [Next]      |
+--------------------------------------------------------------------------+
```

`Next`, `Skip`, and Escape advance to screen 2. Skip dismisses the explanation only; it cannot skip folder binding. Each CLI entry shows its own observed `CHECKING`, `READY`, `MISSING`, `UNSUPPORTED`, or `PROBE_FAILED` status with name, text, dot, and accessible name. The illustrated `CHECKING` values are examples.

## Screen 2 — Project selection

```text
+--------------------------------------------------------------------+
| Mayasaba                 Open a project                             |
|                                                                    |
| [Open existing folder]       [Create new project folder]             |
|                                                                    |
| Recent projects (illustrative values, not live activity)             |
| Northstar   C:\Projects\Northstar   Last opened: 15 Oct 2026          |
| OPEN        Activity: unknown                      [Open project]  |
| Atlas       D:\Work\Atlas           Last opened: 14 Oct 2026          |
| CLOSED      Retained history                       [View history]  |
| Missing folder: exact cause                         [Locate folder] |
|                                                                    |
| [Unsent pre-binding draft; never copies another project's draft]    |
| [Send disabled]                                                    |
| [Back: first introduction] / [Cancel: switching] / [Exit: startup]  |
+--------------------------------------------------------------------+
```

Recent rows query recorded projects, sorted by last successful open; they do not scan the PC. Selection revalidates the root and opens Chat only after safe binding. Closed history stays read-only. Missing or mismatched paths cannot inherit another identity. Creation explicitly creates only the requested empty folder; no Git, scans, installations or agent work follow. Preserve drafts on failure and cancellation. See [the complete selector contract](PROJECT-SELECTION.md) for recovery and authority rules.

### Native picker and rejected folder

```text
SCREEN 2 REMAINS VISIBLE             NATIVE WINDOWS DIRECTORY PICKER
[Open Folder] ────────────────────> [Select a folder] [Cancel]
     │                                      │
     │                           selected path is validated
     │                                      │
     ├── authorized ────────────────────────┴──> Screen 3 Chat
     │
     └── rejected / cancelled ─────────────────> Screen 2 remains
           Folder could not be opened: [exact reason]
           Draft preserved                         [Open Folder]
```

A rejected selection must name the path or authorization problem and retain the draft. Cancel does not create a project. A known folder restores its existing project identity and open or closed condition; it never silently reopens a closed project.

## Screen 3 — Chat, empty bound project

```text
NATIVE WINDOWS APPLICATION MENU (outside Chat)
[File] -> [Switch Project]

+--------------------------------------------------------------------------+
| Mayasaba · Project name + inspectable path                               |
| Phase + condition                Hermes Agent CLI  ● READY               |
| [Start] [Pause] [Stop] [•••] [Exit]  Kilo Code CLI ● READY               |
|                                 Claude Code CLI   ● READY                |
+--------------------------------------------------------------------------+
|                                                                          |
|                                                                          |
|                   What would you like to work on?                        |
|                                                                          |
|                 +--------------------------------------+                 |
|                 | Message Mayasaba...                   |                |
|                 | [Attach files]                [Send] |                 |
|                 +--------------------------------------+                 |
|                                                                          |
+--------------------------------------------------------------------------+
```

The one composer is centered while this project has no confirmed saved user contribution. Chat has **no folder control anywhere**, including the header overflow. `Attach files` selects message attachments and does not select or switch projects. The header shows the project and its canonical path, phase and condition, visible Start/Pause/Stop/Exit controls, overflow [•••], and exactly one truthful readiness entry per required CLI. Selecting [Exit] flushes the draft and closes the application; a later launch opens Screen 2 Project selection. `READY` detection does not mean a project session is active. There is no setup banner when all CLIs are ready.

### Switching projects from the native application menu

```text
NATIVE APP MENU, OUTSIDE CHAT: [File] -> [Switch Project]
                                   │
                   Screen 2 Project selection (modal presentation)
                   [Recent projects] [Open existing] [Create new]
                                   │
             +---------------------+----------------------+
             │                     │                      │
          Cancel             Invalid root          Authorized root
       or Escape           or unsafe switch            │
             │                     │                Project service
             │                     │                binds/restores
             ▼                     ▼                      ▼
       Current Chat       Current Chat + exact       Selected project's
       and draft stay     blocker/recovery          Chat, history, draft
```

The menu is keyboard accessible and adds no screen or route. Project service applies the existing root validation and safe-switching rules. Active old-project work continues only with proven isolation and supervision. If that proof is unknown, the selected folder remains an unbound candidate: the old project, view and draft stay active while Chat names the missing observation and shows bounded pause/reconciliation. Exhausting that bound produces a precise blocked-switch card with only demonstrated recovery actions. Nothing silently stops work, retargets a live CLI, merges histories, closes or reopens a project, or transfers Start permission. B never inherits A's Start. Returning to A in the same application run retains A's permission and healthy sessions after current validation; explicit Stop, application relaunch/crash recovery, unsafe/uncertain state and closure retain their separate gates. Selecting a folder does not start sessions. A known closed project remains closed until explicit **Reopen Project**.

```text
CHAT HEADER OVERFLOW [•••]
  [Diagnostics]
  [Close Project]       when the project is open
  [Reopen Project]      when the project is closed
  No Open Folder or Switch Folder action
```

### Start all three sessions and show their status

```text
FOLDER BOUND: READINESS DETECTED       USER CLICKS [Start]
Hermes Agent CLI  READY                    │
Kilo Code CLI     READY                    ▼
Claude Code CLI   READY           Controller attempts all three
                                            │
                                            ▼
| Three CLI sessions · starting                    [Show details]          |
| Hermes Agent CLI  STARTING                                                |
| Kilo Code CLI     STARTING                                                |
| Claude Code CLI   STARTING                                                |
                                            │
                                            ▼
| Three CLI sessions · observed result             [Show details]          |
| Hermes Agent CLI  READY / IDLE                                           |
| Kilo Code CLI     READY / IDLE                                           |
| Claude Code CLI   READY / IDLE                                           |
```

This is a Chat status card, not another screen. It updates each CLI independently from actual session observations; a failed startup shows the named CLI and exact reason rather than claiming all three started. Start does not send the draft or authorize a scan, council, task, research, or file write. The readiness indicator stays separate from session activity. Before Start, a Send can still save a contribution, but its agent-dependent processing waits for Start. After explicit Stop, a new Send can be saved but agent-dependent work waits until a later Start.

Repeated Start while startup is in progress or all three sessions are healthy shows the existing status without launching duplicates. Start belongs to its project for the current application run, even while another project's Chat is visible. Returning to that project reuses its permission and healthy sessions after current identity, isolation and authority validation. Another project never inherits it. After a completed partial failure, a later Start may retry only known failed or missing sessions after revalidation. Explicit Stop requires another Start for that project. An uncertain start, stop, session or isolation outcome blocks affected reuse/restart until reconciled. On application relaunch or crash recovery, Chat and history return, but every project requires fresh Start after prior effects are reconciled. A closed project requires Reopen Project, and Start never replays earlier material work or authorizes new work by itself.

### First Send and composer movement

```text
EMPTY CHAT, CENTERED COMPOSER
    │ Send
    ▼
Sending: same draft and submission identity remain visible
    │
    ├── confirmed persisted ──> saved user message enters timeline;
    │                          the SAME composer docks at the bottom
    │
    └── failed or uncertain ──> no saved message appears;
                               draft stays in the centered composer;
                               exact failure and recovery appear
```

CLI readiness and problem cards do not move the composer. A restored project with retained conversation starts with the composer docked. The change preserves draft text, selected attachments, focus, and reading position; reduced-motion settings are honored.

## Screen 3 — Chat with a conversation

```text
+--------------------------------------------------------------------------+
| Mayasaba · Project name + inspectable path                               |
| Phase + condition                Hermes Agent CLI  ● READY               |
| [Start] [Pause] [Stop] [•••] [Exit]  Kilo Code CLI ● READY               |
|                                 Claude Code CLI   ● READY                |
+--------------------------------------------------------------------------+
| You · 10:42 · Saved                                                      |
| Help me understand what this project needs next.                         |
|                                                                          |
| Mayasaba · one reviewed reply                         [Show details]     |
| Summary written by the rotating CLI writer and reviewed by both peers.   |
|                                                                          |
| Controller card · current operation                    [Show details]    |
| Status, consequence, timestamp, and next permitted action.               |
|                                                                          |
|                                               [Jump to latest]           |
+--------------------------------------------------------------------------+
| [ Message Mayasaba...                                      ] [Send]      |
| [Attach files] · selected attachment chips                               |
+--------------------------------------------------------------------------+
```

Messages and cards are chronological. Every submitted contribution separates **saved** status from interpretation, authorization, execution, and completion. A successfully finalized conversational turn has one primary reply, with all three responses, the selected writer, both peer reviews, and material disagreement available through `Show details`. A missing response or review shows waiting or a bounded blocker; it never produces a one- or two-agent final reply. A greeting grants no file scan or write permission. Long history preserves the reader's position and offers `Jump to latest`.

## Chat state — a CLI needs attention

```text
+--------------------------------------------------------------------------+
| Project name                     Hermes Agent CLI  ● READY               |
|                                  Kilo Code CLI     ● MISSING             |
|                                  Claude Code CLI   ● READY               |
+--------------------------------------------------------------------------+
| ! Kilo Code CLI · MISSING                             [Show details]     |
|   Executable not found at the checked location.                          |
|   Three-agent replies and other operations requiring Kilo must wait.     |
|   Install or repair the CLI outside Mayasaba.                            |
|   [Locate executable] [Recheck]                                          |
|                                                                          |
| Other saved Chat and recoverable drafts remain available.                |
|                                               [composer and Send]        |
+--------------------------------------------------------------------------+
```

An `UNSUPPORTED` or `PROBE_FAILED` card names its own exact cause and operation impact. Healthy CLIs have no problem card. The status indicator is selectable for a read-only diagnostics sheet; it never becomes an Agents or Settings page. Locating and rechecking do not install, update, authenticate, or reconfigure a CLI. A missing CLI does not disable Send for an open bound project.

## Chat state — attachments and submission

```text
| [ Message with supporting files...                         ] [Send]      |
| [Attach files]                                                          |
| [design.png · image · preparing] [Remove]                                |
| [notes.txt  · text  · ready]     [Remove] [Preview]                      |
| [other file · rejected: exact reason] [Remove]                           |
```

Selected files show name or English-safe metadata, detected type, size, preparation state, and a removal action. A supported English-safe preview is read-only; otherwise show an English preview-unavailable explanation. Selecting a file never submits it. One submission accepts at most 20 files, 100 MiB per file, 500 MiB aggregate, with 120 active seconds for preparation. Archives remain opaque unless a separately authorized operation performs bounded extraction. When Send occurs while a selected file is preparing, keep the same complete draft and submission identity through bounded verification. A changed, rejected, timed-out, or unverified file prevents a partial contribution; no selected file is silently omitted. After persistence, the message links each submitted attachment to provenance and its separate context-inclusion status. An outside-root file needs explicit authorization for that file and managed copy only.

### Failed or uncertain persistence

```text
| Message not saved                                                        |
| The submission could not be confirmed: [precise storage reason].        |
| Your text and valid selections remain in the composer.                  |
| [Recheck submission outcome] [Show details]                              |
|                                                                          |
| [ Original draft...                                        ] [Send held] |
```

An uncertain commit is reconciled using the same submission identity before another attempt. The UI must not show a saved message, interpretation, reply, or duplicate contribution until persistence is confirmed. Recovery controls depend on the actual failure; the diagram's `Recheck submission outcome` illustrates a permitted reconciliation action, not a universal button.

## Chat state — questions, choices, and approvals

```text
| ? Clarification needed for this request               [Show details]    |
|   One batch of unanswered questions, each tied to the affected work.    |
|   [Answer in the same Chat composer]                                     |
|                                                                          |
| ? Choice required for decision D-…                    [Show details]    |
|   Option A: [effect]    Option B: [effect]                               |
|   Constraints, evidence, uncertainty, and impact are inspectable.       |
|   Advisory recommendation is labelled advisory.                         |
|                                                                          |
| ! Approval requested for [exact proposed effect]      [Show details]    |
|   [Approve this effect] [Decline]                                        |
```

A factual question asks for missing information; a choice packet asks which eligible path the user wants. A proven conflict shows both acceptance obligations and the evidence of contradiction. Approval names its exact effect and remains linked to its source. Silence, timeout, an unrelated “yes,” or an agent recommendation is never approval. Answers and control-only messages use the same Chat composer and contribution path, while immediate Pause/Stop does not wait for a conversational reply.

### Immediate controls

```text
[Pause] or [Stop] ──> Controller acts immediately
                     │
                     ├── observed effect: status card names the result
                     └── uncertain effect: status card names the blocker

A standalone /start, /pause, /resume, or /stop is a control-only message.
Plain-language or mixed text is never converted into a control by keywords.
A separately submitted control-only message is saved as one contribution
when persistence succeeds; it creates no conversational turn or writer step.
After Stop, sessions do not silently restart on a later Send; [Start]
requests fresh validation and three project-scoped session starts.
```

## Chat state — exploration, plan, tasks, and evidence

```text
| Repository exploration · partial/complete            [Show details]     |
|   Requested root; inventory, read, analyzed, excluded, unreadable,      |
|   unsupported, too-large and stale counts; three named CLI results.     |
|                                                                          |
| Reviewed plan · proposed/authorized                    [Show details]    |
|   Requirements, decisions, assignments, dependencies, acceptance.       |
|                                                                          |
| Plan application · waiting for [named CLI]            [Show details]    |
|   All three must apply the exact authorized plan before dependent work. |
|                                                                          |
| Task · assigned to [one CLI] · running                 [Show details]    |
|   Expected artifact, bounded scope, blocker or observed progress.       |
|                                                                          |
| Evidence · [artifact and exact version]                [Show details]    |
|   Source, hash, environment, check, result, and limitations.             |
```

These are contextual cards within the same timeline, not a Council, Tasks, Files, or Validation page. Complete repository exploration requires three independent current-context results and verified file coverage; missing evidence is labelled partial or blocked. A proposed plan is never displayed as authorized. A plan-delivery acknowledgement is never displayed as proof that all three sessions applied it. Individually leased implementation tasks name their assigned CLI and dependencies. Task details can show attempts, changed paths, criteria, and evidence without inventing progress percentages.

### Mid-project change

```text
| Pending change · source message                     [Show details]     |
|   Proposed effect and affected requirements, decisions, tasks, files.    |
|   Current approved scope remains in force where still safe.             |
|   FULL review, exact user approval, and plan resynchronization follow.  |
```

A request to change direction is a new Chat contribution. The proposal is not displayed as approved scope until the relevant owners authorize it. Unaffected safe work may continue; affected unsafe work is held. On-demand requirements and decision cards show approved versions, pending amendments, source contributions, decision dispositions, affected artifacts, and the current project epoch as source-linked detail, rather than opening separate pages.

### Council and decision detail

```text
TEMPORARY DETAILS SHEET OVER CHAT                         [Close]
+--------------------------------------------------------------------------+
| Decision: [exact question] · Round [current / declared cap]              |
| Three independent proposals: Hermes · Kilo Code · Claude Code            |
| Six directed peer critiques: [actual completion per assignment]          |
| Chair synthesis: [revision] · both non-chair reviews: [status]           |
| Evidence and surviving disagreement: [source-linked summary]             |
| Outcome: [waiting / question / choice / eligible decision / blocked]     |
+--------------------------------------------------------------------------+
```

The sheet is opened by a relevant `Show details` action and returns to the same Chat focus and reading position when closed. It shows concise positions and evidence, never private chain of thought. Missing mandatory participation remains incomplete, not approval. A decision card distinguishes `HARD_LOCK`, `SOFT_DECISION`, `ASSUMPTION`, and `OPEN`, and shows an authorized successor when a prior decision changes.

## Chat state — validation, publication, and recovery

```text
| Validation · criterion [ID]                          [Show details]     |
|   Expectation: [observable result] · Oracle: [method]                    |
|   Verdict: PASS / FAIL / INCONCLUSIVE / BLOCKED / NOT_APPLICABLE         |
|   Staged result and published-folder result are distinct.               |
|                                                                          |
| Publication · reconciling / blocked / complete        [Show details]    |
|   Files applied, pending, conflicted; journal and post-check status.     |
|                                                                          |
| Unknown physical outcome · affected operation        [Show details]    |
|   Cannot yet prove what changed. Safe unrelated work may continue.      |
|   [Recheck outcome] [Inspect affected files] [Resolve conflict]         |
```

The card offers only actions currently proven safe for its operation. `Retry safely` appears only after reconciliation rules out duplication; unresolved material effects never get a generic Retry. A blocked outcome can remain physically unknown. Validation distinguishes schema checks, build, tests, process launch, runtime behavior, accessibility, publication, and final certification. A clean process exit or one passing check is never shown as final proof. Repair cards show the failing criterion, observed signature, current hypothesis, bounded attempt, and rerun result. User edits that conflict with publication remain visible and are never silently overwritten.

**Loop-specific recovery and improvement projections.** A hung CLI or stalled effect shows actual observed liveness, cancellation/fence status, affected operation and bounded reconciliation; uncertain effects stay `UNKNOWN` and cannot support a green result. A failed repair shows the real failing criterion, current hypothesis and finite attempt/exhaustion reason, never an endless animated spinner. Repeated evidence may produce a nonbinding **Proposed Mayasaba improvement** card with Show details and required user approval; it cannot secretly modify the installed application, policy or the user's CLI configuration. All appear within the existing Chat timeline, not a new screen. Explicit Stop and fresh Start after relaunch remain binding.

## Chat state — certified delivery

```text
| Certified delivery                                  [Show details]       |
|   [Deliverable] is present at [final path in selected project].          |
|   Applicable blocking criteria: passed with current evidence.           |
|   Source-linked hashes/checks: [inspectable references].                |
|   Nonblocking warnings and accepted limits: [if any].                   |
|   Final project path and linked evidence remain inspectable.            |
```

The final card appears only after the applicable acceptance checks and post-publication evidence support the exact deliverable. A document or research task uses its relevant checks; it does not require a software build merely because Mayasaba can build software. If completion cannot be proved, the UI names the remaining criterion and blocker instead of showing this card.

## Chat state — explicit project closure and reopening

```text
OPEN PROJECT                                    CLOSED PROJECT
[•••] → [Close Project]                       [•••] → [Reopen Project]
         │                                               │
         ▼                                               ▼
Closure check: active work, unknown outcomes,    Exact reopen effect and
publication and council round must be safe.      authority are checked.
         │                                               │
         ▼                                               ▼
Closure summary card: retained history,          Retained Chat is readable;
questions, approvals, and protected evidence.    new Send waits for reopen.
```

Closing is explicit and never follows from switching folders, inactivity, application exit, or a lifecycle label. A closed project's retained Chat remains readable. `Reopen Project` requires explicit authorization before any new submission and never silently resumes earlier work. The UI discloses any history lawfully retired under an opt-in retention policy. **File → Switch Project** remains available in the native application menu outside Chat and follows the picker and project-isolation flow above.

## Shared behavior on every visible surface

| Concern | What the user sees |
| --- | --- |
| Loading or unavailable data | Existing readable state stays visible; a bounded status names the dependency and next action. |
| Corrupted or missing managed evidence | An integrity or availability failure; that evidence cannot support a passing criterion. |
| Restart after interrupted work | Persisted history returns; uncertain physical effects show reconciling or unknown until checked. |
| Full local application storage | At 10 GiB or below the 5 GiB free-space reserve, reclaim only eligible disposable bytes and block unsafe new storage; never delete protected history, drafts, pending data, or required evidence. |
| Light, dark, and Windows high contrast | Readable surfaces and status text; changing theme preserves project, draft, focus, and scroll position. |
| Keyboard and screen reader | Logical focus, visible focus ring, named controls and states, meaningful announcements, and return focus from details. |
| Privacy and English-only output | No credentials, tokens, private reasoning, unrelated process output, or unverified non-English agent prose. Required non-English generated prose is blocked with an English explanation. The user's own submitted contribution remains exact only in its user-message/history view with English labels; generated output does not repeat it. The smallest exact technical-identity token may remain unchanged when correctness requires it, with English labels and provenance. |

The permanent Chat-header controls are **Start**, **Pause**, **Stop**, and **Exit**. After Pause, the paused-status card exposes a contextual **Resume** action; exact standalone `/start`, `/pause`, `/resume`, and `/stop` aliases follow the same controller path. The default composer shortcut is **Enter to send** and **Shift+Enter for a new line**. An accessibility preference changes this to **Enter for a new line** and **Ctrl+Enter to send** without changing or submitting the current draft. If Stop cannot prove physical cessation after its 20-second reconciliation window, the UI keeps the affected outcome `UNKNOWN` and operation `BLOCKED`; it never shows a confirmed stop merely because the deadline elapsed.

All cards with more inspectable information expose `Show details`. Dense content uses a temporary sheet over Chat and returns to the invoking control. No illustration here authorizes new filesystem access, provider changes, credentials, a fourth agent, a separate top-level route, or a shortcut around approval and evidence requirements.
