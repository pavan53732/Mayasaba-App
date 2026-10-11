# Mayasaba UI/UX Specification

## Document status

This document translates the user-interface requirements in [Mayasaba — Complete System Description (for AI agents).md](./Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md) into an implementation-facing reference for product, design, WinUI, accessibility, and desktop-test work. The complete system description remains authoritative.

Mayasaba's interface is the native WinUI 3 **Control Room**. It is not a terminal multiplexer, a browser UI, a setup wizard, or a passive chat wrapper. Its job is to make authority, work, uncertainty, evidence, and recovery understandable without exposing private reasoning or raw protocol noise.

## 1. Experience goals

The Control Room must help the user:

1. open or restore a local Windows project without waiting for agents;
2. communicate through one continuous Chat from first idea through delivery;
3. understand what Mayasaba recorded, interpreted, authorized, ran, observed, and verified;
4. inspect agent contributions without confusing them with controller truth;
5. answer only questions that cannot be resolved from authorized evidence;
6. interrupt work at any time;
7. recover from missing agents, crashes, stale work, and partial publication; and
8. trust a completion claim because criterion-linked evidence is visible.

The interface should feel calm, native and direct. It is a conversation, not a dashboard: one strong vertical reading flow, generous whitespace for ordinary messages, and structured cards only when the controller has something consequential to report. Use Fluent styling and system materials only where they preserve readability and accessibility.

## 2. Product-wide interaction principles

### 2.1 Chat opens with a bound project

- Every application launch opens Chat as soon as a project root is bound; a user without one sees the dismissible first-run onboarding sequence (see §5.2).
- The project root is the only precondition for Chat. **CLI readiness never gates Chat** — with zero, one, two or three usable CLIs, the interface and its content behave identically apart from truthful status.
- CLI discovery runs quietly in the background and never blocks the shell.
- Local history and project recovery controls remain available when zero agents are usable.
- A draft may be typed on the open-folder screen; it is preserved when Chat opens and is never a submission before a root is bound.
- Send remains disabled until an authorized project root is bound.

### 2.2 One composer, one contribution path

- The first and every later message use the same composer, Send action, and `SubmitUserContribution` command.
- Conversation, requirements, questions, change requests, attachments, approvals, and control responses stay in the same Chat.
- There is no goal-intake wizard, special initial prompt, separate Explore composer, or mode switch that changes the meaning of Send.
- Each enabled Send attempts to persist exactly one immutable `UserContribution` before interpretation; a failed or uncertain persist preserves the draft, shows the precise storage failure with a recovery path, and is never displayed as saved.

### 2.3 Gate operations, not the interface

- Missing agents block completed three-agent Chat replies and may block FULL council, all-three plan synchronization, complete exploration/research, or tasks requiring that agent.
- Missing agents do not block Chat, Open Folder, project creation, project switching, history, drafts, existing evidence, or recovery.
- CLI readiness never gates Chat, folder selection, drafts, history or recovery; the only precondition for Chat is the bound project root.
- A persisted message is not automatically shown as interpreted, approved, running, answered, or completed.

### 2.4 Show provenance and uncertainty

- Distinguish user text, agent response, controller result, observed command outcome, validation result, and certification.
- Acknowledge receipt separately from successful execution.
- Label provisional streaming text as provisional until a recorded outcome exists.
- Say `unknown`, `partial`, `stale`, `inconclusive`, or `blocked` when that is the observed state.
- Do not invent progress percentages, token usage, capabilities, or successful cancellation.

### 2.5 Keep control visible

- Pause and Stop remain visible while work is active.
- Pending questions, approvals, and recovery actions link to their exact owning operation.
- No timeout or silence becomes approval.

## 3. Chat-only information architecture

Chat is the only persistent route and fills the application workspace. There is no side rail, tab bar, dashboard, alternate project screen or permanent details pane. The shell has three persistent regions:

1. **Compact Chat header** — project identity, inspectable canonical path, lifecycle phase, operational condition, the compact three-CLI status indicator at its top right, Pause/Stop and a small overflow menu.
2. **Conversation timeline** — user messages, agent responses and controller-owned contextual cards in chronological order.
3. **Chat footer** — lower-left Open Folder/current-project control plus the composer, pending attachments and Send.

Council, requirements, decisions, tasks, files/evidence, validation/repair, delivery and agent diagnostics run in the background. They become visible only when relevant as a compact timeline card, an inline expansion of that card, or a temporary details sheet over Chat. The details sheet is not a route and cannot be pinned as a replacement workspace.

### Desktop layout

```text
+--------------------------------------------------------------------------------------+
| Mayasaba | Project + path | phase + condition | (● Kilo Code  ● Claude Code  ● Hermes) [Pause] [Stop] [•••] |
+--------------------------------------------------------------------------------------+
|                                                                                      |
|                 User message                                                         |
|                                                                                      |
|                 Agent response                                                       |
|                                                                                      |
|                 ┌ Controller work card ───────────────────────────┐                  |
|                 │ Build failed · 1 blocking criterion             │                  |
|                 │ Clear summary and next action       [Show details]                 |
|                 └─────────────────────────────────────────────────┘                  |
|                                                                                      |
|                 Conversation continues                                               |
|                                                                                      |
+--------------------------------------------------------------------------------------+
| [Open Folder / Project ▾]  [Attach files] [ Message Mayasaba...              ] [Send] |
+--------------------------------------------------------------------------------------+
```

Open Folder/current project remains outside the text-entry field and must never look like Attach files. The conversation column is centered with a readable line length; structured controller cards may be wider when a matrix, file list or criterion table requires it.

### Contextual disclosure

| Presentation | Use |
| --- | --- |
| Inline summary card | A material milestone, blocker, choice, failure, recovery event or delivery result |
| Expanded card | Moderate detail such as task attempts, criteria, changed files, council rationale or source links |
| Temporary details sheet | Dense trees, comparison matrices, logs, per-file coverage, diagnostics or long evidence lists |
| Direct Chat query | The user asks “show current tasks,” “why is this blocked?” or another inspection question; Mayasaba returns a current source-linked card |

Routine heartbeats, queue operations, protocol envelopes and low-value progress events stay hidden. Presentation filtering changes only what is shown, never the underlying authoritative records.

### Responsive behavior

- Wide: keep one centered conversation column; a temporary details sheet may use available width while Chat remains visible or dimmed behind it.
- Medium: expand details inline when readable; otherwise use a modal sheet.
- Narrow: cards become single-column and the details sheet becomes full-width but remains dismissible back to the same Chat position.
- Keep Pause/Stop reachable while work is active.
- Restore scroll position, selected card and keyboard/screen-reader focus after closing details.

## 4. Visual direction

The visual idea is a **quiet conversation with an evidence spine**. Ordinary messages remain light and spacious. Controller cards carry a narrow semantic edge and precise status text, so authority and proof are recognizable without turning Chat into a grid of equal boxes.

### Default light-theme tokens

| Token | Value | Role |
| --- | --- | --- |
| `Canvas` | `#F7F8FA` | Application and timeline background |
| `Surface` | `#FFFFFF` | Composer, cards and temporary sheets |
| `Ink` | `#202124` | Primary text |
| `Muted` | `#62666D` | Secondary metadata |
| `Authority` | `#4057C8` | Controller provenance and active controls |
| `Attention` | `#A65D00` | Waiting, warning and unresolved state |
| `Critical` | `#B4232C` | Blocking failure and destructive-stop emphasis |

Use Windows theme resources and high-contrast overrides in implementation; these values define the intended default-light relationships, not hard-coded inaccessible colors. Certification uses a distinct success semantic token but never color alone.

### Typography and shape

- Use **Segoe UI Variable** for interface and conversation text.
- Use **Cascadia Mono** only for code, paths, hashes and command fragments.
- Keep prose lines below roughly 80 characters where window width allows.
- Prefer small-radius message/card containers and quiet one-pixel separators over repeated shadows.
- Use the evidence spine, not decorative gradients, as the memorable visual motif.
- Use motion only for user-triggered expansion, collapse, insertion and confirmed state change; respect reduced motion.

## 5. Startup and folder selection

### 5.1 Initial launch

Render immediately:

- the native application shell;
- for a user with no bound project, the first-run onboarding sequence (§5.2) starting at screen 1; for a bound project, the Chat welcome/empty state;
- an editable draft composer on the open-folder screen and in Chat;
- disabled Send with a clear "Open a folder to send" explanation; and
- lower-left Open Folder in the Chat shell.

Start bounded, non-mutating CLI checks in the background. The compact CLI status indicator renders in the Chat header as soon as each first observed result arrives; while a check is still running it shows that CLI as `CHECKING` (see §6.2).

A user with no bound project is first shown the dismissible first-run onboarding sequence (see §5.2) and remains on it until a project root is bound: **Chat does not open without a bound project root**, while CLI readiness never gates anything.

Do not show:

- a blocking agent-setup page;
- an authentication or provider form;
- a model picker;
- a requirement intake wizard;
- a spinner that prevents folder selection;
- any CLI status presented as a gate on Chat, folder selection or Send; or
- fabricated readiness, averages, percentages or progress meters.

### 5.2 First-run onboarding

A user with no bound project sees a dismissible onboarding sequence — **screen 1 welcome** and **screen 2 open folder** — and remains on it until a project root is bound, at which point **Chat** opens. It is never re-shown after a root is bound, and introduces no route, state machine, record or owner. Every screen keeps the three-CLI status indicator visible in the header region.

**Screen 1 — welcome.** Explains what Mayasaba is: a native Windows control plane that coordinates the coding CLIs already on the PC and verifies their work before anything is called done. It reminds the user that:

- the three coding CLIs — **Hermes Agent CLI**, **Kilo Code CLI**, **Claude Code CLI** — are installed, authenticated and updated by the user outside Mayasaba;
- Mayasaba never installs, updates or reconfigures a CLI, and holds no credentials or model picker;
- a missing CLI only limits the operations that need it — it never blocks opening the project folder, and once a folder is open it never blocks Chat;
- the header shows each CLI's current readiness, and problem cards offer **Locate executable** and **Recheck**.

**Screen 2 — open folder.** Shows native **Open Folder** and explains that opening a folder only creates or restores project identity: it launches no agent session, scans no repository, initializes no Git repository, approves nothing, consumes no model-provider usage, and **installs nothing** — the three CLIs stay machine-level tools the user installs and signs into outside Mayasaba, and Mayasaba never installs, updates or configures one, in the selected folder or elsewhere. The editable draft composer is available here; a draft typed on this screen is preserved when Chat opens and is never a submission before a root is bound.

**Chat — the destination.** Binding a folder opens Chat's welcome view with the composer, the lower-left project control and the header indicator. This is the persistent route for every later launch.

Controls:

- **Next** — bottom-right of each screen; advances welcome → open folder, and open folder → Chat once a root is bound.
- **Back** — bottom-left; returns to the previous screen.
- **Skip** and **Escape** — skip the explanation and advance to the mandatory open-folder screen. **There is no route to Chat without a bound project root.**

Rules:

- One mandatory precondition. No screen disables or gates folder selection, and CLI readiness never gates Chat, folder selection, drafts, history, recovery or project switching; the single required step is binding a project root, which is the same root every Send, task and publication already requires — with the sole exception of a closed project, which stays readable but requires explicit reopen authorization before any new submission.
- Presentation only. Onboarding progress is UI state; it creates no authoritative record, state machine, owner, route, goal-intake, brief or scope record.
- No side effects. No installation, download, credential collection, model/provider change, working agent session, repository scan, Git initialization or provider usage results from these screens, and nothing is installed into the selected folder.

```text
+--------------------------------------------------------------------------------------+
| (● Kilo Code  ● Claude Code  ● Hermes)                                               |
+--------------------------------------------------------------------------------------+
|                                                                                      |
|     Welcome to Mayasaba                                                              |
|     A native Windows control plane that coordinates the coding CLIs you already       |
|     have installed and verifies their work before anything is called done.           |
|                                                                                      |
|     Mayasaba works with three CLIs you install and sign into yourself:               |
|       Hermes Agent CLI · Kilo Code CLI · Claude Code CLI                              |
|     It never installs or changes them. A missing CLI never blocks opening your       |
|     folder, and once it is open it never blocks Chat or your history.                |
|                                                                                      |
|     Next step: open the project folder you want to work in.                          |
|                                                                                      |
|                                                    [Skip]              [Next >]      |
+--------------------------------------------------------------------------------------+
```

Accessibility: focus moves to each screen's heading when it opens; **Next**, **Back** and **Skip** are keyboard-reachable with **Escape** mapped to Skip; the screen change is announced to screen readers; the layout honors high contrast, DPI scaling and reduced motion; and the status indicator keeps its accessible names on every screen.

### 5.3 Open Folder

Open Folder launches the native Windows directory picker. The selected path is a candidate until Workspace Manager and Policy Engine validate locality, existence, permissions, aliases, and scope. Project service then creates or restores the project identity.

Success behavior:

- update the persistent header;
- replace Open Folder with the folder/project name and an inspectable canonical path;
- enable Send;
- restore Chat plus the authoritative background requirements, decisions, tasks and evidence needed to reconstruct its cards; and
- retain any pre-selection draft.

Rejection behavior:

- explain the exact path or authorization problem;
- preserve the draft;
- keep Open Folder available; and
- do not create a partial project identity or imply access was granted.

Opening a folder must not visibly or invisibly:

- launch working agent sessions;
- scan or index the repository;
- consume model-provider usage;
- initialize Git;
- alter files;
- approve a request; or
- grant attachment access outside the selected root.

### 5.4 Project switching

The current-project control offers Open Folder or Switch Folder. Switching uses Project service and must:

- preserve each project's independent Chat and state;
- never retarget an active CLI session to the new root;
- never merge projects or rebind tasks;
- require controlled pause/recovery when active work makes switching unsafe; and
- restore the selected project's projections after binding succeeds.

**Closing and reopening are separate, explicit actions.** `Close Project` and `Reopen Project` live in the same project control area and are never triggered by switching, inactivity or closing the application. Closing requires safe quiescence: the composer explains exactly which operations block closure — active leases, running processes, a pending publication journal, an unresolved outcome or an in-flight council round — and offers only the permitted recovery or stop actions. When closure proceeds, a summary card states what closure does and does not do: it preserves the folder, history, decisions and evidence; it does not delete, rewrite, certify unfinished work, or cancel anything silently; and outstanding questions and approvals are listed and restored on reopening. A closed project remains readable — its history, decisions and evidence stay inspectable and CLI readiness is irrelevant to reading — while every new submission explicitly requires reopen authorization. Reopening is blocked from silently resuming work: it restores the same project identity and, where history was lawfully retired under an opt-in policy, discloses exactly what was removed rather than claiming recovered evidence.

## 6. Agent readiness UX

### 6.1 Exception-only notices

Normal UI warns only about agents needing attention:

- `MISSING`: executable not found;
- `UNSUPPORTED`: observed behavior or safety requirements cannot be supported; or
- `PROBE_FAILED`: a bounded readiness check failed for a concrete reason.

`READY` agents are omitted from warning lists. When every agent is ready, no setup banner appears. `CHECKING` may use a small nonblocking indicator.

Each problem row includes:

- exact CLI name;
- exact observed status;
- concise cause;
- effect on current operations;
- guidance to install, authenticate, configure, or repair it externally;
- **Locate executable** where appropriate; and
- **Recheck**.

Do not label an installed but incompatible CLI as missing. Do not disguise a later provider-login or task-tool failure as an installation problem.

### 6.2 CLI status indicator

The compact Chat header permanently displays one small status entry per required CLI at its top right, and the same indicator is shown on every first-run onboarding screen. Normal UI warnings still appear only for agents needing attention; this indicator reports each CLI's observed readiness at all times without turning Chat into a monitoring screen.

Each entry contains:

- the exact CLI name (**Hermes Agent CLI**, **Kilo Code CLI**, **Claude Code CLI**);
- its current observed readiness as short status text — `CHECKING`, `READY`, `MISSING`, `UNSUPPORTED` or `PROBE_FAILED`; and
- a status dot: neutral/amber `CHECKING`, green `READY`, red `MISSING`, `UNSUPPORTED` or `PROBE_FAILED`.

Rules:

- **Truthful only.** The entry is a projection of the adapter's current record. Never fabricate, average, hide or upgrade a state, and never show a percentage, countdown or progress meter. An entry is never green without the corresponding observed readiness evidence.
- **Never a gate.** Chat, Open Folder, Send after binding, drafts, history, recovery and project switching stay available regardless of what is shown, and the indicator adds no condition to any operation.
- **Never color-only.** Status text plus CLI name is always present, with a UI Automation accessible name on each entry; verify high-contrast, DPI-scaling and reduced-motion rendering.
- **Quiet and compact.** Small, unemphasized control-row typography. It never occupies a warning row, never animates continuously, and never interrupts the conversation with a setup banner.
- **Detail on demand.** Selecting or expanding an entry opens the existing read-only diagnostics sheet (§6.3). Nothing here is a standalone Agents/Settings page, dashboard, setup wizard, version manager, automatic installer/updater, model selector or credential collector.
- **Actionable problems still get cards.** A `MISSING`, `UNSUPPORTED` or `PROBE_FAILED` CLI additionally receives the §6.1 problem card with **Locate executable** and **Recheck**; the card disappears once that specific problem is verified resolved.

### 6.3 Technical diagnostics

An explicitly opened temporary diagnostics sheet may show all three adapters, probe time, executable identity, supported observed capabilities, session status, failure details, and recovery history. It opens over Chat, is read-only except for scoped controls such as Locate and Recheck, and returns focus to its invoking control when dismissed.

It must not expose credentials, tokens, private prompts, or model configuration controls.

### 6.4 Operation-specific status

Active work shows attributed session state in contextual Chat cards and their details even when installations are healthy. Distinguish:

- ready;
- starting;
- synchronizing;
- running;
- idle;
- waiting;
- blocked;
- cancelling;
- failed;
- recovering; and
- stopped.

Installation health and task/session activity are different concepts and should not share a misleading badge.

## 7. Chat experience

### 7.1 Conversation timeline

The timeline contains visually distinct entries for:

- user contributions;
- agent-attributed responses;
- controller interpretations and outcomes;
- clarifying question batches;
- approval/choice requests;
- task and decision summaries;
- observed execution/validation updates;
- recovery notices; and
- final certification.

Every submitted user message displays author, timestamp, persistence state, interpretation/route state, and links to relevant `UserContribution`, request, question, decision, task, or evidence records.

Each successfully finalized conversational turn displays one consolidated primary reply with unobtrusive attribution to Hermes, Kilo Code and Claude Code. Show details exposes the three initial contributions, selected writer, both reviews and preserved disagreements. Individual raw drafts do not appear as three competing primary answers. Controller status/control-result cards are distinct from the CLI-written reply and are not styled as a fourth agent.

Controller cards use a consistent anatomy: plain-language title, source-linked status, one-sentence consequence, timestamp, affected operation, primary next action when one exists, and **Show details**. A narrow evidence spine distinguishes controller authority from conversational messages. Do not emit a new card for every heartbeat or low-level event; update the existing source-linked card when that preserves truthful chronology, and append a new card when the authoritative outcome materially changes.

### 7.2 Composer

The single native composer provides:

- multiline text;
- configurable keyboard send behavior;
- Attach files;
- drag-and-drop;
- removable attachment chips/cards;
- one visible Send action; and
- context for a linked reply when answering a question or choice packet.

The composer never writes requirements, decisions, or project epochs directly. Users describe and change the project through Chat and linked inline controls; no separate brief editor, scope document, or goal-approval screen exists.

Draft rules:

- preserve text and valid attachments after a rejected submission;
- selecting files does not submit the draft;
- attachment rejection does not discard valid selections;
- project switching must not accidentally send a draft to another project; and
- application restart restores a draft only according to an explicit local draft policy, never as a submitted contribution.

### 7.3 Message and request states

Folder state and message state remain separate.

Chat Send states:

- draft;
- sending;
- persisted; and
- rejected.

Derived operation states shown beneath or beside the contribution may include:

- commentary or answer;
- interpretation pending;
- clarification needed;
- pending authorization;
- authorized;
- waiting for an agent/capability;
- running;
- rejected; and
- completed with evidence.

A persisted contribution can legitimately have no completed interpretation yet. The UI must not merge those states into one checkmark.

### 7.4 Streaming and long histories

- Virtualize long Chat histories and dense event/evidence lists.
- Apply bounded live-update batches on the UI dispatcher.
- Preserve the user's reading position while new content streams.
- Show a clear Jump to latest action when the user is not at the end.
- Do not force-scroll during reading, selection, or assistive-technology navigation.
- Keep streaming content provisional and distinguish it from persisted controller outcomes.

## 8. Attachments

### 8.1 Selection and preview

Users may select one or more local files with the native picker or drag them onto the composer. Each pending file displays:

- file name;
- detected type;
- size;
- preparation state;
- removal control;
- image thumbnail when supported;
- read-only text/document preview when supported; or
- explicit preview-unavailable fallback.

Declare and enforce count, size, type, and parser limits. Place an actionable error beside the rejected file.

### 8.2 Attachment lifecycle

Visually distinguish:

1. selected;
2. preparing;
3. ready;
4. rejected;
5. submitted/persisted; and
6. included in a particular context snapshot.

A file displayed in Chat is not automatically reported as read, synchronized to every agent, accepted as a requirement, or verified as evidence.

Submitted attachments appear with the originating contribution. Expanding the attachment card shows provenance, digest, supported preview, and context-inclusion status without opening a Files page.

### 8.3 Outside-root files

Selecting a file outside the project root requires explicit authorization for that file and its managed copy. It does not authorize the parent directory or neighboring content. The UI must explain this boundary before the copy is accepted.

Attachment processing never executes the file or treats embedded text as controller authority.

## 9. Conversation, exploration, and work-request UX

### 9.1 Ordinary conversation

Every submitted message, including “Hi”, goes to all three authorized project-scoped CLIs for independent responses and one consolidated reply written by a rotating CLI and reviewed by both peers. Questions and brainstorming do not automatically create implementation tasks, lock requirements, or open a FULL council. The main timeline stays concise; Show details exposes individual contributions and reply provenance. Peer review is not factual certification or user approval.

While collecting responses or reviews, show one source-linked turn status without fabricated percentages or simulated agent agreement. The compact header indicator continues to report each CLI's observed installation readiness independently of turn progress; per-agent session and task state stay on the work cards. With any missing response/review, keep the persisted contribution and explain what is waiting/blocked; never show a one-/two-agent reply as complete. Bound retries and draft revision, preserve reading position, and mark any displayed draft provisional. Finalization displays one primary answer; replay must not duplicate it, and corrections are explicit supersessions. A disagreement may be stated in the reviewed answer rather than hidden.

Explicit Pause/Stop controls execute immediately without waiting for the three-agent exchange. Show the observed control outcome separately; do not restart stopped agents to obtain a conversational answer. Send, history, drafts and folder controls remain available with missing agents. A greeting grants no scan, web research or file-write authority. For an engineering request, distinguish independent input, FULL-council plan review, required user choices, verified all-three plan application, individually assigned background tasks and evidence-backed delivery. Do not require an extra finalized conversational reply before authorized planning can proceed. No model/provider settings page is added.

### 9.2 Repository exploration

When the user asks to inspect the entire project, show a bounded exploration card within Chat:

- requested canonical root;
- inventory/manifest status;
- three named independent assignments;
- per-agent status;
- source verification status;
- inventory, content-inspected, analyzed, excluded, unreadable, unsupported, too-large, and stale counts;
- verified facts, disagreements, unknowns, and limitations; and
- per-file coverage/citation drill-down.

Do not flood the main conversation with a raw file listing. If any agent is unavailable or any readable file lacks current inspection evidence, label the report partial or blocked rather than complete.

### 9.3 Engineering request

For requested implementation, show the source-linked request's progression:

- recorded;
- independent requirements, questions and proposed plans from all three;
- clarification needed;
- pending authorization;
- authorized;
- FULL planning/council at registered material decision points;
- authorized plan with source requirement/decision links;
- synchronizing the exact plan to all three sessions;
- queued/leased only after the required application barrier;
- active;
- integration/validation;
- repair/recovery; and
- evidence-backed completion.

For **“Build a native Windows task manager app”**, follow the [six-step contract](./Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md#engineering-request-six-step-contract). Show necessary clarification in Chat, including whether unresolved “task manager” scope means personal tasks or Windows processes. Show the reviewed plan as a concise source-linked card, with requirements, choices, task assignments, dependencies and acceptance checks in Show details. Label a proposed plan as proposed until its owning services authorize it; do not add a separate brief or approval page.

Distinguish “plan delivered” from “plan applied by all three”. A pending application result names the affected agent without claiming the plan was applied. No dependent implementation is displayed as started before that barrier passes. A replaced session or revised plan shows resynchronization; a stale delivery ACK cannot turn the card green.

One finalized primary reply may summarize the plan. Necessary question batches, approved-plan updates, progress and delivery cards can follow throughout the same work request without becoming duplicate conversational answers. Each subsequent user answer remains a new contribution linked to the pending operation. Reply-review failure is shown on that reply; independently eligible work may continue. Ask only for details needed by the affected operation. A blocked request must not freeze unrelated Chat or authorized work.

### 9.4 Mid-project changes

A feature addition, removal, or direction change remains a normal Chat contribution. If material, display:

- a source-linked pending change proposal;
- affected requirements, decisions, tasks, and artifacts;
- the FULL council decision point;
- required user approval;
- epoch/plan invalidation after authorization; and
- unaffected work that may continue.

Never display the proposed change as approved scope before the owning services commit it.

## 10. Questions, choices, and approvals

### 10.1 Clarification batch

Agents' unresolved questions are normalized, deduplicated, checked against available evidence, and presented in one batch. Each question includes enough context to answer and identifies the affected operation.

### 10.2 Choice packet

An `escalated` council result presents:

- competing options;
- applicable hard constraints;
- user preferences already on record;
- strongest supporting and contradicting evidence;
- measured costs, risks, reversibility, and downstream impact;
- unresolved uncertainty;
- a conflict matrix tied to requirement IDs; and
- an advisory recommendation clearly labeled advisory.

A conflicting-acceptance-criteria blocker presents the same packet from the blocking task's matrix: both obligations verbatim, the demonstrated contradiction with its evidence, the affected scope and work, and the resolution choices (narrow, re-scope, or supersede one criterion). It never silently resolves, never hides either obligation, and is never shown as a repair request. The card distinguishes an ambiguity that needs clarification from a proven contradiction.

### 10.3 Open factual question

A `sealed with an open question` result requests missing facts or clarification, not a preference. Its response contract and labeling must differ from a choice packet.

### 10.4 Approval semantics

- Approval controls name the exact proposed effect.
- A response is linked to the exact question/operation/decision point.
- Silence, timeout, an unrelated "yes," or an agent recommendation is never approval.
- After a material answer changes project truth, show that a new context/epoch is being prepared and affected agents must resynchronize.

## 11. Council cards and details

Council remains a background subsystem. Insert a concise council card only when a decision begins, materially changes, needs the user, blocks work or reaches an outcome. **Show details** prioritizes structured inspection over conversational theater.

For each decision point, show:

- stable identity and triggering event/gate;
- precise question and affected records;
- current epoch/context digest;
- round budget and current round;
- all three required participants;
- deterministic chair and rotating duties;
- independent proposals;
- the six directed critique assignments and completion state;
- rebuttals and revisions with predecessor links;
- evidence grades and source links;
- surviving material disagreements;
- synthesis plus non-chair review and coverage result;
- round outcome and continuation reason; and
- linked user question, choice packet, or binding decision.

Do not expose private chain of thought. Concise rationale, claims, evidence, alternatives, and objections are sufficient.

Visually communicate that:

- chair is a synthesis role, not a superior agent;
- majority is not authority;
- repeated citations to one observation are one evidence source;
- an absent critique is incomplete, not an abstention; and
- stable agreement is deliberative stability, not proof of universal optimality.

## 12. Requirements and decision cards

### 12.1 Requirements

An on-demand requirements card, generated by a material change or direct Chat request, shows:

- current scope queried from approved requirements and binding decisions, linked to their source contributions and approvals;
- approved requirements and versions;
- amendments and pending changes;
- approval state;
- originating contributions;
- linked decisions/tasks/acceptance criteria; and
- affected epoch.

The scope shown in a card is a read-only view of existing approved records, not a separately stored or versioned summary, user-authored brief, or approval checkpoint. Pending proposals remain distinguishable from approved scope. If no build requirements are approved yet, Chat and separately authorized read-only exploration still work.

### 12.2 Decisions

A decision card and its expanded details show:

- decision question and resolution;
- disposition: `HARD_LOCK`, `SOFT_DECISION`, `ASSUMPTION`, or `OPEN`;
- alternatives and shared evaluation criteria;
- supporting/contradicting evidence;
- authoritative preferences and hard constraints;
- residual risks and uncertainty;
- affected records and artifacts;
- originating council rounds and positions;
- predecessor/successor records; and
- explicit reopening action.

When evidence challenges a decision, show the scoped validity concern and affected downstream work. Do not silently portray the historical lock as reversed.

## 13. Task cards and details

Tasks run in the background. Chat receives a compact task card for meaningful assignment, blocker, retry, repair, integration or completion changes. Expanded card details or a temporary sheet may show graph and list representations appropriate to scale. Every task projection exposes:

- objective and expected artifacts;
- source contribution/work request;
- dependencies;
- assigned CLI;
- allowed read/write scope in concise form;
- attempt history;
- active lease/fencing version where technically useful;
- progress derived from observed records;
- blockers and retry/repair status;
- acceptance criteria and required oracles; and
- changed paths and evidence after execution.

Retry and reassignment are controller-authorized actions, not direct agent controls. Avoid false numeric percentages when the underlying work is not measurable.

## 14. Files and evidence cards

Provide linked Chat cards and on-demand detail sheets for:

- selected project files;
- managed submitted attachments;
- generated artifacts;
- source citations;
- repository exploration coverage;
- observed command/process output references;
- hashes and environment identity;
- producing task/attempt/command; and
- acceptance criteria that use the evidence.

The internal evidence/blob store is not presented as a second editable project tree. A missing or corrupted managed blob shows an integrity/availability failure and cannot satisfy a criterion.

Diagnostic records may expose protocol or database details only in a temporary read-only, redacted technical sheet over Chat. Inspecting evidence never expands filesystem authority.

## 15. Validation, repair, and delivery cards

### 15.1 Criterion accountability

For every criterion display:

- stable ID and source requirement;
- observable expectation;
- oracle or review procedure;
- blocking/nonblocking designation;
- artifact/hash and environment evaluated;
- staged versus published-location result;
- one verdict: `PASS`, `FAIL`, `INCONCLUSIVE`, `BLOCKED`, or `NOT_APPLICABLE`;
- oracle-adequacy status and known gaps; and
- evidence links.

Text and iconography must communicate outcomes in addition to color.

### 15.2 Separate validation domains

Do not collapse these into one green state:

- contract/schema validation;
- formatting/static analysis;
- compilation/build;
- unit/integration/regression tests;
- process launch;
- runtime behavior;
- E2E behavior;
- accessibility/UX review;
- cross-agent review;
- publication/reconciliation;
- packaging/install lifecycle; and
- final certification.

### 15.3 Repair

Show the failure class, failing criterion, observed signature, current hypothesis, bounded repair scope, responsible task/agent, attempt history, targeted rerun, and regression outcome.

When the same failure repeats without progress, explain that the strategy stopped and new evidence or a new hypothesis is required. Toolchain, provider, policy, and external-environment failures must not look like application-code defects.

### 15.4 Publication and recovery

During integration show:

- staged candidate status;
- conflict/freshness checks;
- pre-publication validation;
- publication journal status;
- files pending/applied/conflicted;
- restart reconciliation;
- post-publication checks; and
- whether the original folder is restored, modified, certified, or awaiting user action.

Never show partial multi-file publication as completed. Never hide preserved user-edit conflicts.

### 15.5 Delivery

Completion appears only after all applicable blocking criteria have fresh evidence and the controller certifies the exact deliverables in the selected root. The final delivery card lists final paths, hashes where useful, certification evidence, nonblocking warnings, and any explicitly accepted limitations.

## 16. Feedback and visual semantics

Use a consistent semantic vocabulary across messages, cards and temporary sheets:

| Semantic state | Meaning | UI treatment |
| --- | --- | --- |
| Neutral | Recorded or available; no outcome implied | Plain label and timestamp |
| Provisional | Streaming or proposed, not authoritative | Explicit provisional styling/text |
| Active | Observed work is currently progressing | Activity indicator plus descriptive verb |
| Waiting | Awaiting a declared dependency, user input, or agent | Name the dependency and available action |
| Blocked | Cannot proceed safely or with current authority/capability | Strong text label, reason, and recovery path |
| Warning | Nonblocking risk or uncovered case | Text and icon; preserve at delivery |
| Failed | A valid operation or oracle observed failure | Exact failed item and next action |
| Unknown | Physical outcome cannot yet be established | Never style as success or ordinary retry |
| Recovered | Reconciliation established a safe current state | Describe what was recovered and revalidated |
| Certified | Controller proved all applicable completion gates | Reserved final-success treatment |

Avoid using the same green check for persistence, ACK, process exit, passing one test, and final certification.

### Recovery actions

An `UNKNOWN` outcome is followed by contextual actions, never a generic Retry. Each action appears only when the owning service can demonstrate that it is permitted for that specific unknown:

- **Recheck outcome** — re-observe the physical outcome from existing durable records;
- **Inspect affected files** — read-only comparison of journal expectations against actual identities and hashes;
- **Resolve conflict** — route a competing user version to a scoped conflict resolution;
- **Retry safely** — available only after idempotency reconciliation has ruled out a duplicate; and
- **Resume** — requires verified fencing and current lease/epoch validation.

A generic Retry button MUST NOT appear for an unresolved material side effect. While reconciliation runs, the affected operation shows a bounded reconciling state that names the dependency. If no safe path is found, it becomes Blocked with its outcome explicitly unresolved and a precise reason — never styled as success, never as an ordinary retry, and never relabelled as a known failure — while unrelated work continues.

## 17. Empty, loading, error, and disconnected states

Every card type and temporary sheet defines:

- first-use empty state;
- no matching records state;
- bounded loading/checking state;
- partial-data state;
- unavailable dependency state;
- recoverable error state;
- corrupted/integrity-failure state; and
- restart reconciliation state.

Each state explains what happened, what remains trustworthy, and the next available user action. Preserve existing readable state during transient failures instead of blanking the interface.

After restart, render persisted records as persisted and mark interrupted physical outcomes as reconciling or unknown until observed. Never assume interrupted work completed.

**Storage and cleanup states.** When local application storage reaches its declared ceiling, reclaim disposable data first and keep Chat usable. If protected data still exceeds capacity, show a precise storage blocker naming what is full, what was reclaimed, what remains protected, and the available recovery or cleanup controls; it never silently deletes authoritative records, never touches the user's project files, and leaves unrelated conversation and work available **wherever their own persistence and resource requirements can still be met** — a genuinely full storage volume cannot guarantee further writes, so an operation whose own write would fail is blocked with its precise reason rather than falsely attempted. A failed or uncertain persist keeps the recoverable draft, states that the message is **not** saved, and offers retry after idempotency reconciliation. Retention that removed historical records under an explicit user policy is disclosed wherever a claim references the missing history — an abbreviated chain is never presented as complete.

## 18. Accessibility

Accessibility is a release requirement, not polish.

### Keyboard

- Every interactive element is reachable in a logical order.
- Visible focus is always present.
- Chat, cards, inline details, question batches, tables, temporary sheets and dialogs have predictable focus entry/return.
- Pause and Stop remain keyboard reachable during streaming.
- Configurable Send behavior avoids trapping multiline entry users.

### Screen readers and semantics

- Give every control an accessible name, role, state, and useful description.
- Announce meaningful state changes without reading every streamed token or event.
- Identify agent attribution and controller attribution in accessible text.
- Tables, trees, graphs, and evidence relationships have linear alternatives.
- Do not encode status solely through position, color, animation, or icon.

### Visual accessibility

- Support system text scaling, DPI scaling, high contrast, and color themes.
- Maintain readable contrast over system materials.
- Respect reduced-motion preferences.
- Keep focus, selection, error, warning, and certification visually distinct.
- Avoid dense fixed-size panels that clip translated or scaled text.

### Performance accessibility

- Keep the UI thread free from agent I/O, parsing, database work, and long validation operations.
- Virtualize long lists.
- Batch frequent updates.
- Preserve assistive-technology focus during refresh.
- Do not replace stable controls unnecessarily as status changes.

## 19. Privacy and information boundaries

The ordinary interface may show authorized source files, submitted attachments, generated artifacts, relevant sanitized output, and evidence. It must not show:

- provider credentials or authentication tokens;
- private chain of thought;
- unrelated environment variables or filesystem content;
- raw secrets found in source or web content;
- unrestricted CLI logs;
- sensitive internal database/protocol records without redaction; or
- data from another project or session.

When a provider or research tool may receive project context, the user-facing flow must preserve the governing authorization and provider policy. A UI preview or evidence link never widens access.

## 20. Native implementation boundaries

- Use WinUI 3 controls, XAML layouts, and C++/WinRT view models.
- View models submit typed commands/queries and render immutable projections.
- Implement one persistent Chat route only. Council, requirements, decisions, tasks, files/evidence, validation/repair, delivery and diagnostics are card/detail projections, not pages or navigation destinations.
- Dispatch UI updates explicitly and in bounded batches.
- UI code does not write SQLite, launch executables, manage worktrees, parse raw CLI protocols, authorize material actions, or compute certification.
- Keep view-only formatting and selection state separate from durable domain state.
- Use Windows UI Automation properties and stable automation IDs for critical flows.

## 21. Desktop acceptance scenarios

At minimum, native automated and manual acceptance must cover:

### Startup and readiness

1. First launch with no bound project shows onboarding screen 1, then the mandatory open-folder screen, with zero, one, two and three usable CLIs; the sequence is identical apart from truthful status, and binding a folder opens Chat in every case.
2. Only `MISSING`, `UNSUPPORTED`, and `PROBE_FAILED` agents appear in the normal warning UI.
3. All agents ready produces no setup banner.
4. Recheck updates observed status without installing, updating, or changing a model/provider.
5. A later provider/task-tool failure is attributed to the affected operation, not mislabeled as missing.

### Folder and project identity

6. Draft before folder selection; Send is disabled for root absence only.
7. Valid empty and populated folders create/restore projects.
8. Invalid, denied, aliased, and reparse-escape roots fail with exact feedback.
9. Opening a folder launches no working agent and performs no implicit scan.
10. Switching projects preserves identity and never retargets a live session.

### Chat and requests

11. First and later messages attempt to persist through the identical contribution path.
12. Missing agents do not prevent contribution persistence.
13. Normal conversation does not create implementation tasks or a council.
14. A multi-intent message yields distinct linked operation statuses.
15. Work-specific clarification blocks only affected work.
16. A material mid-project change remains pending until council and approvals finish.
17. Rejected submission preserves the draft and valid attachments.

### Attachments

18. Select, remove, drag/drop, preview, reject, submit, and inspect supported/unsupported files.
19. Selecting an attachment never sends the draft.
20. Outside-root attachment authorization remains file-scoped.
21. Missing/corrupt managed bytes display an integrity failure and cannot appear included/verified.

### Exploration and council

22. Three-agent exploration shows independent assignment and honest coverage counts.
23. Unreadable, stale, oversized, and excluded files prevent false full-coverage claims.
24. Council renders three proposals and six real critiques; absence cannot look complete.
25. First-round stability without synthesis shows continuation, not convergence.
26. Choice and factual-question outcomes use distinct packets and response handling.

### Work, evidence, and recovery

27. One-agent task starts only its assigned CLI; FULL workflows start all three headlessly.
28. Start, stream, cancel, timeout, crash, and recover without visible terminal windows.
29. Build, launch, runtime, E2E, and certification remain separate results.
30. Every criterion shows one of the five allowed verdicts and linked evidence.
31. Partial publication and concurrent user edits show recovery/conflict, never success.
32. Restart reconciles interrupted process and publication outcomes before positive status.
33. Final completion appears only with fresh criterion-linked published-artifact evidence.

### Accessibility and responsiveness

34. Complete critical flows using keyboard only.
35. Verify screen-reader names, roles, states, reading order, and useful announcements.
36. Verify high contrast, text/DPI scaling, reduced motion, and non-color status cues.
37. Sustain large Chat, task, council, and evidence streams without UI-thread stalls or reading-position loss.
38. Verify there is no project-navigation rail, tab bar, dashboard or standalone project page at any window size.
39. Verify every background domain is reachable through a relevant Chat card, direct Chat query or temporary details sheet without creating another route.
40. Verify card expansion and sheet dismissal restore timeline position, keyboard focus and screen-reader context.

### Current scope through Chat

41. Verify users describe, refine, and change the project entirely through Chat and linked inline controls, without a separate brief editor, scope-summary record, or initial goal approval.
42. Verify the current scope card derives from approved requirement/decision versions with source links, excludes pending proposals, handles an empty approved scope, and refreshes after authorized changes without treating stale views as authority.

### Three-agent conversation and consolidated replies

43. Send “Hi”: observe three independent responses, one rotating writer, both peer reviews of the final draft and one concise primary reply; no scan, work lease or FULL council starts.
44. Inspect Show details for the three contributors, writer, both reviews, source/context references and preserved uncertainty without exposing private reasoning.
45. With zero, one or two usable CLIs, attempt to persist the contribution — a failed or uncertain persist follows scenario 62 — and show waiting/blocked status for a saved one; do not substitute a reduced-participant final answer or disable Chat.
46. Exercise one revised draft, missing/stale/self reviews, contradictory interpretations and bound exhaustion; require both complete reviews delivered to the writer before any revision, exact final-draft reviews, a recorded `BLOCKED` outcome with precise reason and resumable recovery when bounds are exhausted, and truthful unresolved status rather than an indefinite spin or fabricated consensus.
47. Restart or redeliver a completed turn: show one finalized reply; changed context invalidates stale drafts/reviews, and a later message cannot be mixed into an earlier turn.
48. Pause or Stop while replies are pending: execute the authorized control immediately, show its actual outcome, and do not restart stopped sessions for discussion.
49. Request a native Windows app: show independent input, full council with six critiques at material decision points, authorized plan synchronization to all three, individual task assignments and verified results.
50. Reject cross-project responses, unauthorized attachment/provider context and attempts to turn conversational synthesis into work authorization or factual certification; show no Mayasaba model/provider settings page.

### Engineering workflow and synchronization

51. For the task-manager request, inspect three independent requirements/questions/plans and all six routed critiques, rebuttals and revisions; no direct CLI-to-CLI channel or material new-app council bypass is allowed.
52. Delay a conversational reply review while authorized planning is eligible: planning proceeds and its status remains distinct; a missing mandatory council contribution still blocks its decision.
53. Deliver the plan to all three but withhold one context-application result, supply a wrong graph revision/digest, or replace a session: dependent leases remain withheld until the exact current baseline is applied.
54. Ask and answer a necessary scope question, then change an authorized requirement during execution: retain source links, preserve unaffected work, and show affected invalidation plus successor-plan synchronization before resuming.
55. Display one finalized conversational reply alongside later question, authorized-plan, progress and delivery cards; replay creates no duplicate reply, and current drafts/reviews preserve historical source attribution.
56. First launch with no bound project: screen 1 explains Mayasaba and the user-owned CLIs with the truthful status indicator; **Next** reaches screen 2, and **Skip** or Escape skip the explanation to screen 2 — no control reaches Chat without a bound project root.
57. Screen 2 with no folder bound: **Back** returns to screen 1, **Open Folder** binds a native folder and updates the header without launching any session, scan, Git initialization or provider usage, and binding opens Chat's welcome view immediately.
58. Relaunch and replay with no bound project: the sequence stays on screen 2 with no dead-end, fabricated state or duplicate introduction; no path, shortcut, deep link or replay opens Chat without a bound root.
59. After a folder is bound, every later launch, restart and project switch opens directly at Chat; onboarding is never repeated and no duplicate introduction appears.
60. Zero CLIs installed: screen 2 still opens the folder, Chat opens on binding with truthful waiting/blocked status, and no installation prompt, credential form, model picker, version manager or blocked state appears — and nothing is installed into the selected folder. Keyboard focus moves to each screen's heading with **Next**/**Back**/**Escape** mapped, the screen change is announced to screen readers, all three status entries keep accessible names, and high-contrast, DPI-scaling and reduced-motion rendering hold. No authoritative record, state machine or route is created.
61. Storage pressure: fill local application storage to the declared ceiling. Verify disposable data is reclaimed first while Chat stays usable; when protected data still exceeds capacity a precise storage blocker appears naming what is full, what was reclaimed, what remains protected and the recovery/cleanup controls; the database is not corrupted and the user's project files are untouched. Verify unrelated conversation and work stay available wherever their own persistence and resource requirements can still be met, and that an operation whose own write would fail is blocked with its precise reason rather than falsely attempted.
62. Persistence failure: simulate a failed or uncertain `UserContribution` commit (full device, SQLite failure, interrupted commit). Verify the message is never displayed as saved, accepted, interpreted or answered; the recoverable draft is preserved where possible; the precise storage failure and an explicit recovery path are shown; retry is reconciled through the existing idempotency identity so no duplicate contribution is created; and storage pressure never reports provider usage as Mayasaba budget state.
63. Cleanup eligibility: create a managed blob, reference it from an authoritative record, then age it past every disposable schedule. Verify it is not deleted while referenced, and that no blob is deleted on age alone. Interrupt a cleanup run mid-plan and verify restart reconciliation records outcomes and never deletes active-lease, unresolved-operation, pending-journal, protected-evidence or recovery-dependency bytes. Attempt a concurrent new reference during the deletion window and verify transactional fencing prevents the eligibility race.
64. Finite retention opt-in: enable an explicit per-project retention policy and verify immutable log segments, verified retention checkpoints, retained hash-chain boundary anchors and a durable deletion manifest; verify any historical claim referencing removed records discloses exactly what was removed, never presents the abbreviated chain as complete, and never treats expired evidence as independently verifiable. With no opt-in, verify preservation is absolute and nothing expires automatically.
65. Conflicting acceptance criteria: approve two individually valid but jointly unsatisfiable criteria and verify the task is blocked before execution leasing with a truthful card that names both obligations and the demonstrated contradiction; verify unrelated authorized work continues, that the controller-verified clarification path is offered for a scope ambiguity, and that resolution requires a binding decision with re-materialized criteria. Attempt the agent-side escapes — editing or deleting the oracle or test, self-declaring a criterion `NOT_APPLICABLE`, or satisfying one criterion at the expense of the other — and verify every attempt fails to clear the blocking condition, with neither obligation weakened and no silent resolution.
66. Closure preconditions: with an active lease, a running process, a pending publication journal and an in-flight council round in turn, verify Close Project is refused with the exact blocking operations named and only permitted recovery or stop actions offered; verify a paused or stopped round qualifies only after its outstanding work is fenced or reconciled and the incomplete round is durably recorded. Verify switching folders, application exit, inactivity and lifecycle `COMPLETE` never close a project, and that closure is a durable Project-service record rather than a phase or condition.
67. Closure during unresolved publication: crash multi-file publication with `UNKNOWN` outcomes, then request Close Project and verify closure is refused as a recovery workaround, the blocking publication is named, and only the contextual recovery actions are offered. Verify that an explicit Stop does not clear the unresolved obligation, and that the project simply stays openable rather than becoming unusable.
68. Concurrent work admission: attempt to start a new task lease, submit work, or open a new council round while closure is finalizing and verify the admission barrier refuses all of them, revalidates quiescence and either commits the closure or refuses it — with no second lease-fencing authority and no lease silently mutated.
69. Questions, approvals and closure history: with an unanswered question and a pending approval, verify closure proceeds and preserves both in the closure summary; verify they are restored intact on reopening and do not block it. Verify closure never deletes or rewrites the folder and never implies unfinished work was certified.
70. Retention clock and retirement: with an explicit opt-in, verify the clock starts at the later of the durable closure commit and the opt-in commit — including opt-in granted after closure, which must not trigger retroactive expiry — and that destructive retirement rechecks current closure and policy eligibility. Verify reopening before expiration suspends expiry, reopening during a cleanup run prevents further eligible cleanup, and reopening after lawful retirement discloses exactly what was removed without claiming recovered evidence.
71. Closed-project Chat: verify a closed project's history, decisions and evidence remain readable with CLI readiness irrelevant, that every new submission requires explicit Reopen Project authorization regardless of CLI status, and that a Chat-requested closure follows the ordinary persisted-contribution rules while the Close control itself dispatches nothing to any CLI.

## 22. UX anti-patterns

Do not introduce:

- a blocking, mandatory or readiness-gated startup/setup wizard;
- a model/provider/API-key selector;
- a fabricated, averaged, hidden, percentage-bearing or color-only CLI status, or a CLI status used as a gate on Chat, folder selection or Send;
- a status indicator that becomes a dashboard, navigation destination or standalone Agents/Settings page;
- a project-navigation rail, project tab bar, dashboard or standalone Council/Requirements/Decisions/Tasks/Files/Validation/Delivery/Settings page;
- a separate goal intake or Explore input mode;
- agent terminals the user must launch or manage;
- a folder picker inside the attachment control;
- a global progress percentage inferred from agent text;
- a generic success check that conflates receipt, execution, test, and certification;
- a two-agent fallback council;
- a majority vote display as the decision rule;
- hidden automatic approval or timeout-to-consent;
- editable internal evidence/database state as a second project truth;
- raw protocol traffic in ordinary Chat;
- private chain-of-thought display;
- false token counts or a credential/token display; or
- a visual claim of full repository analysis without current per-file evidence.

The best Mayasaba interface is not the one that makes automation look effortless. It is the one that makes the exact boundary between request, authority, execution, evidence, and completion easy to see.
