# Mayasaba — all three screens, motion, depth and Chat UX

**Derived implementation guide · visual scope approved 16 October 2026; composer rule reconciled 17 October 2026**

This guide specifies the native UI work and its acceptance criteria. It does not certify that the work is implemented. The [complete system description](./Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md) is authoritative; the [UI/UX reference](UI-UX.md), [screen diagrams](USER-VISIBLE-SCREENS.md) and [project selection contract](PROJECT-SELECTION.md) describe the same three-screen experience. UX-01–UX-32 below supplement existing contract and safety checks. Implementation reports must state the source revision and evidence category for every result.

## 1. Assignment

Implement a complete native UI/UX refinement across **all three screens**:

1. **Welcome** — first-run introduction, identity, dimensional hero and three-CLI status.
2. **Project selection** — recent projects, Open existing folder, explicit Create new project folder, project-draft preservation and recovery; no composer.
3. **Chat** — empty state, one composer, messages, consolidated agent replies, agent details, work progress, blockers and recovery.

The user explicitly wants richer, polished animations and 3D styling across the complete experience. Deliver working native C++20/WinUI 3 changes and local verification. A website, screenshot, concept, or stylesheet alone does not complete this assignment.

Use a cohesive **calm, dimensional control room** direction. Carry the same typography, spacing, materials, logo, motion and state semantics from Welcome through selection to Chat. Spend the strongest 3D expression on the Welcome/empty-Chat brand hero. Use surface elevation and connected transitions for project selection, messages, composer and details. Keep reading content stable.

Proceed with routine reversible implementation work within this scope without repeatedly asking permission. Preserve existing work. Report actual limitations instead of fabricating readiness, agent activity or completed work.

## 2. Scope and the meaning of completion

This assignment completes the **three-screen UI and its presentation wiring** to the native services that exist. It includes all required UI states, their typed presentation contracts, native interactions, accessibility, motion and regression proof.

At the reviewed baseline, native agent probes/adapters, real session startup, three-agent reply orchestration and agent execution are not implemented. They are separate substantial runtime dependencies. Do not quietly implement a new orchestrator, bypass containment, or fabricate live replies to make the design appear complete.

Where a backend exists, wire the actual native service. Where it does not, production must display a precise unavailable/waiting state. Exercise future reply/work states through an isolated, explicitly identified test fixture harness that cannot activate in normal production startup. Document the exact missing owner contract or runtime dependency. A fixture can prove the renderer and controls; it cannot prove real agent work.

Final reporting must distinguish:

- **Three-screen UI slice:** completed or incomplete, with its own evidence matrix.
- **Live three-agent application workflow:** implemented and verified, or explicitly blocked by named missing dependencies.

Do not describe the whole Mayasaba product as end-to-end complete just because the three screens look complete.

## 3. Repository, authority and starting evidence

Resolve paths below from the repository root.

Read before changing code:

1. `AGENTS.md` in full.
2. `docs/Mayasaba — Complete System Description (for AI agents).md`, particularly frontend/UI, three-agent Chat, authority, project selection and process safety.
3. `docs/UI-UX.md` in full for presentation, states, accessibility and acceptance.
4. `docs/PROJECT-SELECTION.md` and `docs/USER-VISIBLE-SCREENS.md`.
5. `docs/ARCHITECTURE.md`, applicable C06–C10 contracts, and schema registry definitions for affected commands/events.
6. [Native build instructions](../native/README.md), [historical validation report](../native/LOCAL-VALIDATION.md), current source and tests. Treat reports as historical claims until verified against current source and artifacts.
7. The selector regression requirements in section 7 below and PROJECT-SELECTION.md. Retain any existing S2-01–S2-30 case identities in the implementation evidence; that historical matrix is additional regression context, not a substitute for current source checks.

Authority: current explicit user instruction → complete system description → AGENTS.md → derived documentation → code/tests/comments. The current request authorizes the visual refinement of all three screens, including Chat; the earlier Screen 2-only instruction is not a prohibition on this new UI assignment. Existing architecture, safety, three-agent and persistence requirements remain in force.

The 2026-10-16 amendment in the complete system description approves the expanded ten-motion set and three-screen depth treatment. UI-UX.md holds its shared tokens. Keep changes consistent with that amendment and its reduced-motion/high-contrast guards. This does not authorize changes to orchestration, filesystem authority or unrelated product requirements.

Reviewed baseline was `3ffd482926fa3f17c4e1067b79ae088280160676`; repository status was clean during the review. Recheck HEAD, status and diff yourself. Do not assume that baseline is still current. The user reports that Screen 2 works in their manual testing.

The previously inspected executable was `native/Mayasaba/bin/x64/Release/Mayasaba.exe`, SHA-256 `998B025F8C63CD8B27E765D6FC65C861C4677949BE3BCA2E85922C069572D38B`. This identifies a historical inspected binary and is not a substitute for building your final source.

Preserve staged, unstaged and untracked work. Do not reset, replace the entire main window from an older revision, clean the repository, rewrite a production store, or terminate an app another person is using. A different process committed the prior batch during earlier work; inspect changes rather than assuming ownership. Follow repository commit/push rules; the standing documentation-only authorization does not authorize committing a mixed native implementation batch.

## 4. Historical starting facts and current evidence

Primary paths:

- `native/Mayasaba/MainWindow.xaml`, `.xaml.h`, `.xaml.cpp`.
- `native/Mayasaba/App.xaml` for theme resources.
- `native/Mayasaba/ProjectService.h`, `Storage.h/.cpp`, `ProjectRoot.h/.cpp`, `SerialQueue.h`.
- `native/Mayasaba/NativeIds.h`, `ProtectedDraft.h`.
- `native/tests/storage_tests.cpp`, `storage_tests.vcxproj`, `validate_contract_registry.py`.

Historical findings at `3ffd482` (before the subsequent native refinement and attachment milestone):

- Welcome and selector are tall scrollable layouts. Welcome actions were partly below the visible area of an approximately 1139 × 586 captured native window.
- Screen 2 supports real selection/create/recents/recovery wiring. Preserve its behavior while refining layout.
- Screen 2 readiness is below the draft; it should remain readily visible in a consistent header region.
- Empty Chat positions the hero and composer using independent fixed margins in the same grid row. Rework this for small heights, long text and text scaling.
- `RenderHistory()` creates user TextBlocks and a repeated hard-coded integration-waiting card. No complete agent-reply/details/evidence renderer exists there.
- Chat history is a ScrollViewer containing a StackPanel, without virtualization or Jump to latest.
- Composer docking changes grid row/margins directly; there is no actual connected Dock animation.
- Attach files is disabled because verified attachment handling is absent.
- Start deliberately reports unavailable native adapters/context/confinement; Pause/Stop report that no working sessions were started.
- CLI names in Chat are shortened and statuses are hard-coded UNSUPPORTED.
- Theme dictionaries and reduced-motion observation exist. Ambient motion has no explicit high-contrast guard.
- Documentation contains stale “native pending” statements and a historical 30/30 acceptance claim. Reconcile only with evidence; do not inherit or erase those claims without explanation.

These are historical findings, not a claim that concurrent implementation work still has each defect. Inspect the current source before changing it.

## 5. Shared visual system

### Identity and materials

- Preserve Mayasaba's indigo/violet brand and existing three-node mark. Use the same mark in all three screens, at appropriate sizes.
- Reuse the light/dark token relationships in UI-UX.md: light canvas `#F3F4F8`, surface `#FFFFFF`, ink `#1D2440`, brand `#5957C7`; dark canvas `#111318`, surface `#1E2230`, ink `#F0F2FF`, brand `#A5A1FF`. Use theme resources, not scattered literals.
- Windows high contrast uses system brushes. Remove decorative gradients, shadows, perspective and ambient motion there.
- Use a quiet background plane, a readable content plane and a raised temporary-details plane. Keep the existing resting/overlay shadow hierarchy.
- Use opaque backing under body text. Restrict gradients to the existing permitted brand locations; do not animate gradients behind conversation content.
- Keep one vector icon family; replace inconsistent icon treatment gradually within affected controls. No network-loaded icons or new dependencies are needed.

### Typography and geometry

- Segoe UI Variable for product text; Cascadia Mono only for code, paths and technical identities.
- Suggested logical-size scale: hero 36–44, screen title 28–32, section 18–20, conversation 15–16, metadata 12–13. Scale adaptively; Windows text scaling takes precedence.
- Keep prose approximately 60–80 characters per line on wide windows. Allow evidence/code details wider when needed.
- Base spacing: 4/8/12/16/24/32. Reuse documented panel/control/composer radii (18/11/17) unless a named visual amendment explains a change.
- Support short and narrow windows: test at 520 × 640 and 640 × 480 logical client sizes, then ordinary desktop widths. Stack controls and reduce decorative space before sacrificing content or actions.
- No critical control may require horizontal scrolling. Long names/paths must wrap or offer a keyboard-accessible full-value disclosure.

### Shared interaction rules

- Every primary action has immediate pressed/busy feedback, duplicate-action protection and an exact result.
- Loading, empty, error, unavailable, blocked and uncertain are separate states.
- Keep focus visible. Announce material state changes, not animation frames or every token.
- Theme, DPI and motion preference changes preserve current project, draft, pending submission, focus, selection and reading position.
- Implement stable controls/view models; do not rebuild a whole screen on every status event.

## 6. Screen 1 — Welcome

### Layout and content

Use a responsive brand introduction with a generous hero on large windows, compact hero on small ones, concise copy, and a reliably reachable action area.

```text
Mayasaba                         Three named CLI readiness entries

                 Dimensional three-node logo
                  Welcome to Mayasaba
        One concise explanation of the control room
        User-owned CLI installation/configuration reminder

Skip introduction                                      Next
```

- No composer on this screen.
- Show exactly Hermes Agent CLI, Kilo Code CLI and Claude Code CLI, each with its current observed readiness, text and accessible name. In the current build, explain unavailable probes truthfully.
- Keep Next and Skip visible at small heights using an action footer outside the scrollable explanatory body.
- Shorten oversized decorative space before wrapping important copy into an unreadable narrow column.
- Focus the heading on entry through a proper accessible heading/focus strategy; keep keyboard progression predictable.

### Motion and depth

- Create a native dimensional logo treatment with perspective, layered edges and restrained shadow. Reuse vector geometry; avoid adding a rendering engine or external asset dependency.
- One coordinated Enter sequence on first display. Preserve a finite, comprehensible order: identity → message → actions.
- Optional existing slow Levitate/Glow/Orbit stays confined to the visible Welcome hero and stops when the screen hides or animations are disabled.
- Hero pointer tilt is optional and limited to approximately ±4 degrees; keyboard/touch users receive the same information without it. No essential meaning depends on tilt.
- Never animate all the explanatory text independently or run per-frame layout work on the UI thread.

### Required behavior

- Next, Skip and Welcome Escape lead to Screen 2, never rootless Chat.
- Welcome is not repeated automatically after the first successful binding.
- No CLI install, credential collection, provider call, repository scan, Git operation or working session starts from this screen.

## 7. Screen 2 — Project selection

### Layout

Prioritize finding or opening a project. Keep decoration smaller than Welcome and leave space for real records.

```text
Back / Cancel / Exit             Three named CLI readiness entries
Open a project
[Open existing folder] [Create new project folder]

Recent projects
Name · open/closed · observed activity
Canonical path · last opened                [Open] [Locate]
...virtualized/paged records...

No composer, attachment entry or Send control on this screen
```

- First-run uses Back; later startup uses Exit; switching uses Cancel. Preserve their distinct semantics.
- On wide windows, align primary actions together and provide a readable recent list. On narrow windows, stack them.
- Show actual project name, inspectable canonical path, known last-opened time, open/closed status and genuine activity. Unknown activity stays unknown.
- Empty lists should explain the first useful action. Avoid emphasizing inactive pagination controls when no second page exists.
- Keep create and recovery forms inline or in existing temporary surfaces. Every form has a clear heading, action, cancellation and precise error.
- Recovery controls name the operation and only offer demonstrated safe actions.
- Message entry belongs only in bound-project Chat (user confirmation, 2026-10-17). Welcome and Screen 2 have no composer, attachment-entry control or Send control. Preserve the old project's draft while selecting; never submit or transfer it.

### Motion and depth

- Recent row hover/focus can use a subtle surface/elevation change; avoid rotating or tilting rows containing paths.
- Expand create/recovery/collision detail with Appear; preserve the scroll anchor and focus.
- A successful selection may use a short connected transition into Chat after the durable bind succeeds. It must not delay action availability or impersonate success before commit.
- Cancel restores the previous Chat view without replaying the whole conversation animation.

### Preserve all existing Screen 2 safety behavior

Retain applicable S2-01–S2-30 regressions from the [native validation record](../native/LOCAL-VALIDATION.md#current-acceptance-matrix-s2-01-through-s2-30). S2-14 is historical for removed pre-binding draft entry; project-draft isolation and uncertain-send safeguards still apply. In particular:

- Explicit selection every later launch; no auto last-project binding.
- Native picker cancellation/failure preserves the active binding and drafts.
- Recent/Open/Locate bind the correct domain identity and validate filesystem identity; aliases or replacement directories must not inherit unrelated history.
- Closed projects stay closed and readable until explicit Reopen.
- Safe switching never retargets running sessions or silently stops work; unknown isolation/supervision blocks the switch.
- A/B drafts stay separate and pending uncertain sends cannot be overwritten. Screen 2 creates, loads and transfers no unbound draft. Legacy unbound draft data stays protected until explicit recovery/discard; removing its entry control does not authorize deletion.
- Explicit creation creates exactly one requested empty child under a validated parent, with durable intent before effect and original idempotency identity on replay.
- Unknown create outcomes are reconciled, never blindly retried, adopted or deleted.
- No selected-folder scan, Git initialization, template files, CLI configuration writes or provider use.
- Store failures are not presented as an empty recent list. Preserve backup/migration, single-writer locking, capacity guards and Windows-user draft protection.

## 8. Screen 3 — Chat

### Shell and empty state

- Chat remains the only persistent workspace. No new dashboard, permanent project rail, tabs or standalone Agents/Tasks pages.
- Keep File → Switch Project in the native application menu outside Chat.
- Compact header: project identity and inspectable path, lifecycle/condition, three full CLI readiness entries, Start/Pause/Stop, overflow and Exit. Keep Stop reachable during every operation.
- Separate CLI readiness from session activity. Do not use a green READY dot to imply a working session or approved task.
- Group the empty-state hero, invitation and the one composer through adaptive layout. Eliminate the independent 190/135 fixed-offset arrangement.
- Use a smaller dimensional logo than Welcome. Stop ambient hero motion as soon as conversation/work replaces the empty state.
- Copy should invite action: “What would you like to work on?” with a short explanation of current capabilities or waiting state when relevant.

### Composer and saved messages

- Preserve one composer instance and one SubmitUserContribution path for first and later messages.
- On a confirmed first saved contribution, animate the same composer from center to the bottom. Preserve text, caret, selection, focus and any prepared attachments. Failed/uncertain persistence keeps the recoverable draft and its original identity; do not visually acknowledge a saved message.
- Render user messages with clear author/time/persistence state and selectable text. Use subtle brand tint and comfortable line length.
- Provide multiline input, clear Send feedback, the documented Enter behavior and alternate accessibility preference. Changing that preference never submits text.
- When real attachment service is available, show file chips with preparing/verified/rejected states and removal. Enforce its all-or-nothing submission rules and bounds. Otherwise explain unavailability visibly and accessibly; never accept or silently omit files through a decorative button.
- Drafts and histories must remain project-specific across navigation and restart.

### One consolidated agent reply

Implement native reusable presentation for:

- readable paragraphs, headings, lists, quotes and safe code blocks;
- Copy actions and inspectable, source-linked file/evidence references;
- one finalized primary answer, with unobtrusive attribution;
- provisional, waiting, blocked and final states that remain distinct;
- expandable agent contributions/reviews and preserved disagreement.

The actual workflow requires three independent responses, the recorded rotating writer, and both peer reviews of the exact final revision. The UI displays those records; it never manufactures them. Do not reveal private chain of thought, raw unverified streams, credentials, unrelated output or non-English generated replies. Preserve the system's precise English-only exceptions for technical identities and user contributions.

**Show details** reveals three contributions, writer, both peer reviews, source/context references and unresolved disagreement. Use an inline expansion for moderate content and a temporary accessible sheet for dense detail. Dismissal restores the invoking control and reading position.

Do not turn an ordinary consolidated reply into a controller decision, authorized work order or completed validation claim.

### Work, messages and agent activity

Create presentation components for these owner-provided states:

| Presentation | Necessary information |
|---|---|
| Message saved / waiting | Saved contribution identity; why processing waits; next available action |
| Three-agent response/review | Each named participant's observed state; writer/review stage; exact blocker |
| Work proposal or approval | Scope, affected files/effects, relevant choice and explicit authorization action |
| Running task | Task/operation title, real stage, material last event, linked detail and Stop access |
| Paused or stopped | Requested control versus observed effect outcome; remaining unknown effects |
| Validation | Criterion-linked result and evidence, separate from process exit or agent agreement |
| Delivery/publication | Actual published artifacts and validation outcome; unresolved criteria disclosed |
| Recovery | Exact affected operation, uncertainty, preserved data and demonstrated safe next action |

Use a compact card with a semantic edge, plain-language title, consequence, timestamp, primary permitted action and Show details. Update routine progress in place; append meaningful outcomes without rewriting historical records. Do not add a new card for each heartbeat.

No fabricated percentages or animated “thinking” that runs forever. If measured progress has no valid denominator, show the current stage and last observed event. A deadline produces the owner's blocked/unknown state with recovery. Pause/Stop must not depend on an animation finishing or a reply arriving.

### Reading stability and long histories

- Replace the nonvirtualized StackPanel history with an appropriate WinUI virtualized list/repeater and stable item identities.
- Page history through bounded queries. Batch live updates; keep storage/parsing/agent I/O off the UI thread.
- Preserve selected text, caret and reading anchor while new events arrive.
- Follow the end only when the user was already following it. Otherwise show Jump to latest with a truthful unseen-entry count.
- Restored history appears immediately without replaying entrance effects.
- Theme changes, details expansion and project-switch cancellation preserve reading position.

## 9. Native motion and 3D contract

Implement motion as a reusable **UI presentation utility**, not a new service owner or authoritative state machine. Use the existing Windows/WinUI toolchain and native composition/XAML APIs; keep all animation state subordinate to the actual service state.

| Motion | Target and behavior | Default |
|---|---|---|
| Enter | New screen/card; fade and vertical 10 → 0 | 260 ms |
| SlideIn | Newly inserted timeline entry; horizontal 20 → 0 + fade | 260 ms |
| Appear | Inline detail/content reveal | 260 ms |
| Dock | Same composer, connected position and size | 260 ms |
| DepthReveal | Hero/temporary raised surface; small perspective/elevation settle | 260 ms, no repeated text tilt |
| HoverLift | Interactive surface elevation cue; focus-equivalent treatment | 120–160 ms |
| Press feedback | Native pressed response; immediate cancellation-safe feedback | Native control behavior |
| HeroTilt | Welcome/empty hero only, maximum approximately ±4° | 260 ms settling |
| Levitate / Glow / Orbit | Existing hero-only long-form effects | Retain documented 5–6 s / 52 s / 74 s bounds |

Use the existing base easing `cubic-bezier(.18,.78,.2,1)` where supported by the selected native animation API. Keep one shared timing/token source. Document any API-specific mapping instead of asserting an exact curve that was not implemented.

Rules:

1. OS animations disabled or preference unknown → no animation. Dock becomes an instant layout change.
2. High contrast → system colors, no decorative depth/gradients/shadows/ambient motion. Observe live changes explicitly.
3. Theme/focus changes and window teardown cancel obsolete animations and release handlers.
4. Hidden screens stop loops. Timeline content and active work never have ambient orbits/glows behind them.
5. Animation never delays Stop, changes a domain state, clears a draft or confirms persistence.
6. Keep text/code on stable reading planes. Use native compositor transforms/opacity where practical; avoid layout-invalidating animation every frame.
7. Bound all event queues, animation instances and transient visuals. Do not retain recycled list items through animation callbacks.
8. At busy moments, reduce decorative effects before allowing input latency. Record actual frame timing/resource observations on the test machine; no unmeasured 60-FPS claims.

Microsoft implementation references (verify API availability against the pinned SDK):

- https://learn.microsoft.com/en-us/windows/apps/design/signature-experiences/motion
- https://learn.microsoft.com/en-us/windows/apps/design/signature-experiences/layering
- https://learn.microsoft.com/en-us/windows/apps/develop/composition/composition-tailoring

Check API availability against the repository's pinned SDK before use. Do not install/update packages just to access an effect.

## 10. Native structure and boundaries

- Keep XAML/views responsible for presentation and user intent. No SQL, subprocess launch, raw CLI traffic, scheduling or completion certification in UI code.
- Extract reusable styles/resources, motion helpers and timeline components from the main-window monolith where that improves ownership and testability.
- Use typed, versioned view projections with stable project, turn, contribution and operation references. These are projections of existing owners, not new domain machines.
- Validate bounds and enum values at the boundary; stale project/generation callbacks cannot update the current view.
- Use the existing serial service lane and lifetime/generation protections. Preserve cancellation and explicit error propagation.
- For missing reply/work services, document the expected owner interface and test renderer fixtures separately. Do not introduce a second persistence system or let fixture data leak into the real store.
- Native rich-content rendering must safely handle long text, code, invalid markup and source links; rendered text never becomes executable commands or authority.
- Retain existing UUID/domain/filesystem identity distinctions and immutable historical contribution keys.

## 11. Implementation order

1. Inspect current changes, authority, source and available build tools. Record the baseline and scoped acceptance matrix.
2. Establish shared theme/type/spacing/elevation/motion resources and responsive shell rules.
3. Complete Welcome, including reachable footer and native dimensional hero.
4. Refine Screen 2 using its existing services. Verify selector/draft/create/recovery regressions before proceeding.
5. Refactor Chat layout and virtualized timeline; preserve the same composer and contribution path.
6. Add reply/details/work/recovery components and typed projections. Connect existing services; explicitly identify absent runtime interfaces.
7. Add Dock and other finite transitions, then hero depth. Implement preference changes, cancellation and focus return together with each effect.
8. Complete accessibility, responsive/theming checks and targeted native regressions.
9. Review the complete diff and update implementation/validation documentation to match observed reality.
10. Deliver an accurate matrix and evidence package. Finish all authorized independent work before declaring a dependency blocker.

Do not stop after a design plan, preview, or first screen. Complete all three screens and all applicable checks in the agreed UI slice.

## 12. Build and isolated verification

Use Windows x64 and the existing native project. At the reviewed baseline, the available working compiler path was:

`C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe`

MSVC v145 and Windows SDK 10.0.26100.0 were available; project package versions were pinned. Verify current availability. Do not perform an unsolicited restore/download/upgrade.

Representative PowerShell commands, after inspecting the current project:

```powershell
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild native\Mayasaba\Mayasaba.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /v:minimal /nologo
& $msbuild native\tests\storage_tests.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /v:minimal /nologo
& .\native\tests\bin\storage_tests.exe
python .\native\tests\validate_contract_registry.py
```

Check and record each exit independently. Resolve exact current paths rather than assuming this historical command list proves anything. The previous observed suite reported 18 native regression groups and 124 registry checks; additions or current outputs may differ. Registry checks do not prove live native UI, a canonicalizer implementation or agent behavior.

Use a new isolated `MAYASABA_STORE_PATH` and temporary sample folders for UI tests. Never reset, migrate experimentally, seed fixtures into, or swap the user's production store. Use purpose-built test fixtures for many records, long histories, errors and simulated reply/work events. Do not silently send real prompts to installed CLIs or incur provider usage during UI-only verification.

Capture native screenshots from the actual built executable. Identify source revision plus uncommitted-diff/source hashes, executable hash, configuration, OS/SDK/toolchain, theme, dimensions/DPI, test-store identity and check outcome. Browser concepts are design aids only.

## 13. Required acceptance matrix

Create and maintain a matrix using PASS, FAIL, INCONCLUSIVE, BLOCKED or justified NOT_APPLICABLE. These are individual criteria; no aggregate score overrides a blocking item.

| ID | Required result |
|---|---|
| UX-01 | All three screens exist in the native executable and share the documented visual system. |
| UX-02 | Welcome Next/Skip/Escape navigate correctly; no rootless Chat; first-run recurrence rules remain correct. |
| UX-03 | Welcome actions and three CLI names remain usable at short/narrow sizes and increased text size. |
| UX-04 | Dimensional Welcome hero is native, has bounded lifecycle and static accessible alternatives. |
| UX-05 | Screen 2 handles recents, empty/loading/error states, paging, full paths and closed status truthfully. |
| UX-06 | Picker, cancel, Locate, explicit creation and recovery preserve all applicable S2-01–S2-30 invariants. |
| UX-07 | A/B drafts and uncertain sends survive navigation, restart and failures; no composer exists before binding, and legacy unbound draft data remains protected for explicit recovery/discard. |
| UX-08 | Screen 2 motion never reports selection success before the service receipt or changes creation effects. |
| UX-09 | Empty Chat has one unobscured composer with adaptive hero layout; no fixed-offset collision at tested sizes. |
| UX-10 | First confirmed save performs connected Dock; failed/unknown save retains the correct recoverable draft/identity. |
| UX-11 | Caret, selection, attachment state where available and focus survive Dock and preference/theme changes. |
| UX-12 | User message, agent reply and controller result have distinct, readable and accessible anatomy. |
| UX-13 | Rich replies handle long prose/code/lists/links safely and provide appropriate Copy/disclosure controls. |
| UX-14 | Exactly one primary reply; details expose the three contributions, recorded writer and both exact-final reviews. |
| UX-15 | Unverified/non-English generated streams stay hidden; errors/blockers use English and safe recovery. |
| UX-16 | Waiting, running, paused, stopped, blocked, unknown, validation and delivery projections are distinct and honest. |
| UX-17 | Missing runtime features remain truthfully unavailable; fixture records cannot appear in normal production use. |
| UX-18 | Start/Pause/Stop project scope and command semantics remain intact; animations never delay immediate controls. |
| UX-19 | Header shows all three full CLI names with text/accessible status; readiness is separate from session activity. |
| UX-20 | Virtualized long history, bounded update batches and stable identities work with representative large fixtures. |
| UX-21 | New content preserves reading/selection; Jump to latest appears appropriately; restored history does not replay motion. |
| UX-22 | Details open/close, keyboard dismissal and focus/scroll return work; no new persistent route is introduced. |
| UX-23 | All actual attachment actions use verified preparation and whole-submission rules; unavailable implementation is explicit. |
| UX-24 | Light, dark and live theme transitions preserve state and readability across all three screens. |
| UX-25 | High contrast uses system colors and disables decorative depth/ambient motion explicitly. |
| UX-26 | Reduced motion disables every transition/loop, including Dock, during startup and live preference changes. |
| UX-27 | Keyboard-only and Narrator walkthroughs cover headings, actions, errors, states and details without token/event noise. |
| UX-28 | 520 × 640 / 640 × 480 logical sizes, ordinary desktop widths, 125/150/200% DPI and increased text size remain usable. |
| UX-29 | Window close/navigation/theme change cancels stale callbacks and releases motion/event subscriptions; no wrong-project update. |
| UX-30 | Native Release build, applicable native/schema regressions and Screen 2 regressions pass with current evidence. |
| UX-31 | Frame/input responsiveness and resource use are measured during transitions and representative event/history load; limitations recorded. |
| UX-32 | Final screenshots, source/artifact identity, matrix, changed files and precise remaining runtime blockers are delivered. |

For UX-14–UX-18, retain separate rows or subchecks for **presentation fixture proof** and **live service integration proof**. If the runtime is absent, the latter is BLOCKED; it must not be converted to PASS based on sample data. State precisely whether that dependency blocks the UI-slice completion or only the broader product milestone.

Use fault/negative cases where they discriminate actual risk: lost save receipt, stale selection callback, duplicate create, pending-send collision, blocked participant, opening details while updates arrive and disabling motion mid-transition. Do not add trivial tests that merely repeat style literals.

## 14. Deliverables and final response

Deliver:

1. Native code/resources for all three refined screens and reusable UI components.
2. Explicitly scoped visual-contract updates for the requested motion/depth extension.
3. Typed presentation contracts/fixtures and real service wiring where available.
4. Current native build/regression results and complete acceptance matrix.
5. Native screenshots covering Welcome, selector with recents/create/collision/recovery, empty Chat, saved messages, reply/details/work/blocked fixture states, and representative narrow/theme variants.
6. A truthful implementation/validation document with exact source/artifact identity and outstanding runtime dependencies.

Final response should say what changed on **each of the three screens**, what was actually tested, what remains unavailable, and where the evidence is. Explicitly separate preview/fixture proof from native UI proof and live agent proof. Do not claim success from a build, screenshot, process exit or previous report alone.

## 15. Design references and evidence status

The repository [interactive preview](CHAT-INTERFACE-PREVIEW.html) is an illustrative navigation/composer reference with sample data. It predates this visual refinement and may not demonstrate the new depth or motion requirements. Preview screenshots and fixture content are not evidence of running agents or completed native acceptance.

The latest preview selection is the Conversation direction; the user also requested richer native animation and 3D across all three screens. Keep the readable Conversation layout and apply the bounded depth treatment specified here. The implementation requirements are fully described here and in UI-UX.md; no external personal file is required to interpret this guide. Use the readable conversation flow with hero depth and restrained elevation throughout all three screens. Keep the actual Mayasaba mark and one unified native implementation.

This documentation amendment records requirements only. Native implementation and fixture/runtime observations were subsequently recorded in [LOCAL-VALIDATION.md](../native/LOCAL-VALIDATION.md); UX-24–UX-28 and UX-31 still include unexercised visual/accessibility/performance checks in that report. This documentation reconciliation does not rerun or certify those checks. Record actual PASS, FAIL, INCONCLUSIVE, BLOCKED or justified NOT_APPLICABLE outcomes in a source-identified native validation report. A missing runtime dependency can block live-work proof while the renderer is verified with isolated fixtures; report those results separately.
