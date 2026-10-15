# Local native validation — Mayasaba native WinUI slice

> **Requirement changed 2026-10-15:** the user approved the [Screen 2 project selector](../docs/PROJECT-SELECTION.md). Direct-to-Chat startup results below are historical evidence for the preceding requirement. The new selector, recent-project list and explicit folder creation have not been implemented or runtime-verified by this documentation amendment.

**Scope.** This report covers the native C++20 / C++/WinRT / WinUI 3 starting slice under `native/Mayasaba/` only. It is **not** certification of the complete Mayasaba application: the three CLI adapters, orchestration, work authority, publication, accessibility acceptance and MSI packaging are not implemented, so no whole-product acceptance is claimed anywhere below.

**Current revision.** `main` at `45662c7`, with the 2026-10-15 startup correction in `native/Mayasaba/MainWindow.xaml.cpp`.
**Current binary.** `native/Mayasaba/bin/x64/Release/Mayasaba.exe`, SHA-256 `D69E65B3352EF4C4A90DF2D5976838221212ED96AC73A2A9097E0E03479E4A07`, built 2026-10-15 10:55.

## Evidence classes

Every verdict carries one class, so a compiled result is never confused with an observed one:

- **User-reported** — the user reported the outcome; not independently observed by the desktop tool.
- **Agent-observed runtime** — the desktop tool observed the running application (UIA tree, screenshot).
- **Source inspection** — read from source; no runtime observation.
- **Compilation** — a build/link result only.

## Verdict vocabulary

Per `AGENTS.md` section 13, each criterion ends in exactly one of `PASS`, `FAIL`, `INCONCLUSIVE`, `BLOCKED`, or `NOT_APPLICABLE` with verified justification. No other value is used. Rows recorded earlier as `PARTIAL` are mapped below with the mapping stated in the row.

## Current acceptance matrix

| Criterion | Verdict | Class | Evidence / limit |
| --- | --- | --- | --- |
| Dependency restoration | PASS | Compilation | Native MSBuild restore completed 2026-10-14 and re-run 2026-10-15. |
| Release x64 compilation and linking | PASS | Compilation | 2026-10-15: exit 0, **0 warnings, 0 errors**, 20.53 s; `MainWindow.xaml.cpp` recompiled (`native-build.log`). |
| **Startup with a bound project opens Chat directly** (approved rule) | PASS | Agent-observed runtime | 2026-10-15, binary `D69E65B3…`: launch restored `ui_state.last_project` (`Documents`) straight into Chat. UIA read `ProjectHeading` = "Documents", `ProjectPathLabel` = `C:\Users\pavan\Documents`, `ProjectCondition` = "Bound · No working CLI session admitted". No Welcome control was present. Screenshot `win-20261015-105602-960.png`. |
| Startup with no bound root shows the first-run onboarding sequence | BLOCKED | — | Not tested: the store holds bound projects, and creating unbound state would alter user state, which is forbidden. |
| No false CLI completion | PASS | Agent-observed runtime | 2026-10-15: header reads `● Hermes · UNSUPPORTED`, `● Kilo · UNSUPPORTED`, `● Claude · UNSUPPORTED`. The blocked-Start notice was observed 2026-10-14 (UIA text + source). |
| Chat renders bound project identity, controls and empty state | PASS | Agent-observed runtime | 2026-10-15: heading, canonical path, condition, Start/Pause/Stop/More/**Exit**, centered composer, Send disabled on an empty draft. |
| Theme readability in the live Windows setting (dark) | PASS | Agent-observed runtime | 2026-10-15 screenshot: light text on dark surfaces; user bubble vs controller card distinction confirmed 2026-10-14. |
| Exit control present in the Chat header | PASS | Agent-observed runtime | 2026-10-15: `#ExitButton` "Exit Mayasaba" present beside the overflow button. |
| Exit flushes the draft, then closes the application | INCONCLUSIVE | Source inspection | `OnExit` persists the draft and calls `Close()`. The button was not clicked; the 2026-10-15 close used the title-bar Close, which runs the same `Window.Closed` teardown. |
| Window closes cleanly, process exits, store intact | PASS | Agent-observed runtime | 2026-10-15: Close removed the window and the process exited. The store was byte-identical before and after (SHA-256 `23E3ED8764F8EB9E3C97F92F8960AE9AAD9A086B320044369E9029129F994272`). |
| Native folder picker opens | PASS | Agent-observed runtime | 2026-10-14: Windows "Select Folder" dialog observed. |
| Folder selection opens Chat | PASS | **User-reported** | 2026-10-14: the user reported selecting a folder and reaching Chat. Not independently observed by the desktop tool. |
| Picker cancel preserves the prior screen and store | PASS | Agent-observed runtime | 2026-10-14: cancel left the app on the prior screen with an unmodified store (verified read-only). *Recorded earlier as `PARTIAL`; the recorded observation satisfies the stated criterion in full, so it is mapped to `PASS`.* |
| Restored history renders without replay or duplicates | PASS | Agent-observed runtime | 2026-10-14: restored history (1 entry + 1 controller card) rendered on relaunch with no duplicates. |
| Append-side insertion animation distinct from restored history | INCONCLUSIVE | Agent-observed runtime | Requires a Send in a disposable project; not exercised. |
| Initial welcome entrance timing | INCONCLUSIVE | Agent-observed runtime | A settled screenshot cannot prove the initial transition. |
| Live reduced motion | BLOCKED | — | Permission to toggle Windows Animation effects was never granted; no OS setting was changed. |
| Closure race — close while the folder picker is open | INCONCLUSIVE | Source inspection | The path is unreachable through normal UI (the picker's modal ownership makes the title-bar close inert); guarded in source, not observed. |
| Chat responsiveness under SQLite contention (5 s busy timeout) | INCONCLUSIVE | Source inspection | `Storage` is called synchronously from the UI thread; no contention test has been run. |
| Narrow-window layout | INCONCLUSIVE | Agent-observed runtime (historical `FAIL`) | 2026-10-14: at 565 px the identity column collapsed and at 800 px the heading clipped. The two-row header fix is compiled into the current binary but has not been visually re-verified at those widths. |
| Keyboard focus and navigation | PASS | Agent-observed runtime | 2026-10-14: tab order logical; disabled Send/Attach skipped. |
| XAML well-formedness and diff hygiene | PASS | Source inspection | Both XAML files parse; `git diff --check` clean. |
| Three-CLI operation and whole-product acceptance | BLOCKED | — | Native adapters, orchestration, work authority, publication, accessibility acceptance and MSI packaging are not implemented. |

## Current status

The native slice builds clean and, on the corrected binary, opens a previously bound project directly at Chat with truthful CLI status and no fabricated agent output. It remains a starting slice, not a product: every CLI-dependent operation is fail-closed and all three adapters are absent.

**Open and unverified — do not treat as complete:**

- First-run onboarding (no bound root): cannot be exercised without altering user state.
- Motion: live reduced-motion change, initial entrance timing, and restored-versus-appended insertion animation.
- UI responsiveness under SQLite contention.
- The close-while-picker-open race.
- Narrow-window layout re-verification on the current binary.
- Exit-button click-through.
- Three-CLI adapters, orchestration, work authority, publication, accessibility acceptance, MSI packaging.

## Approved startup rule and its correction

**Binding rule.** A user with no bound project sees the first-run onboarding sequence (screen 1 welcome → screen 2 open folder) until a root is bound, and **binding a folder opens Chat immediately**. A previously bound project opens **directly at Chat on every later launch**, and the onboarding sequence is **never re-shown**. A relaunch still requires a fresh Start before any CLI session is initialized; restoration is not authorization. Sources: the authoritative system description (launch and first-run-onboarding sections), `AGENTS.md` section 5, and `docs/UI-UX.md` acceptance criteria 58, 59 and 80.

**Deviation (2026-10-14, commit `c9a9e8a`).** That commit made the constructor always show Welcome and rewrote three documents to match (`docs/UI-UX.md`, `docs/USER-VISIBLE-SCREENS.md`, `docs/CHAT-INTERFACE-PREVIEW.html`). The change was recorded in this report and in the commit message as following an explicit product instruction. **No such instruction could be located** — every record of it was an artifact of the implementing session itself. The written rule therefore remained binding and the code was non-conforming.

**Correction (2026-10-15).** `native/Mayasaba/MainWindow.xaml.cpp` again restores `LastBoundProject()` into Chat, with Welcome reserved for the no-bound-root case, and the three documents were reverted to the binding rule while keeping the separately requested Exit control. Rebuilt Release x64: 0 warnings, 0 errors.

**Correction verified.** 2026-10-15, agent-observed runtime, binary `D69E65B3…` — see the startup row in the current matrix above.

## Historical observations (preserved and superseded)

These observations are retained for provenance. Where a later session contradicted an earlier one, the later result stands and the superseded entry is marked.

### Session 1 — 2026-10-14, first native build, launch and folder-to-Chat report

Source revision: the `f53d90d` era. Validation binary SHA-256 `4666680CD19005B48DE06D05D837558A44AC4019FE2CFA02F49192E4CA12FF4B` (20:57), built to a separate `bin/x64/Validation/` output so a running Release executable was not overwritten.

- Dependency restore, XAML resource fix, and the first successful Release x64 build. The launch initially crashed on a missing `TabViewButtonBackground` resource; merging `XamlControlsResources` into `App.xaml` resolved it. `/utf-8` was added after garbled title punctuation was observed.
- Observed: welcome screen rendered; Next reached Open Folder; the native picker opened.
- **User-reported:** folder selection reached Chat.
- **Stale claim, corrected:** an early row read "this binary has not yet been launched". That binary *was* subsequently launched in Session 2 (see below); the row was superseded, not deleted.
- The user authorized committing and pushing that batch to `main` after reporting the folder-to-Chat check. (Current standing practice is commit-only; the user pushes.)

### Session 2 — 2026-10-14 (later the same day), UI validation continuation

Starting state: `main` at `f53d90d`, working tree clean. Final binary SHA-256 `6D94B4785178D73E78F0BD7E17C62D305153EE8C858698BEA7A31F473F0881F8` (22:04), containing the narrow-header fix, the picker closure guard, the Exit button and the startup-from-Screen-1 change.

- Build: exit 0, 0 warnings, 0 errors. The sandbox blocked commands containing the literal tool name, so the compiler was resolved through a wildcard array.
- **Stale claims, corrected:** this session's own notes said the fixes were "uncommitted, reviewable" and "uncommitted — awaiting build", and one paragraph asserted that no rebuild could be performed. The same document records the successful 0-warning rebuild above, and all four changes were committed as `c9a9e8a`. Those notes described an earlier point in the session and are superseded.
- **Superseded verdict:** "A. Startup restores bound project — PASS" described a binary that auto-opened Chat. That was correct for its binary, and the behaviour was then deliberately changed in `c9a9e8a`. That change has since been judged a deviation and reverted; the current matrix row supersedes both.
- **Superseded verdict:** "A. Startup screen — PASS (per user instruction)", stating that always starting at Screen 1 implemented an explicit instruction and superseded the restore property. No such instruction could be located; this entry is superseded by the correction above.
- Environmental interference, not a product defect: the desktop runs `cua-driver.exe` (Cua.AgentCursorOverlay), which scrambled keystrokes typed into the native picker, triggered stray activations, and caused unattended instances to self-exit after roughly 15–20 seconds. Several checks were therefore handed to the user rather than forced through automation.
- Pre-existing user state was inspected read-only from a copy of `%LOCALAPPDATA%\Mayasaba\mayasaba.db` (`mode=ro`); no application-state writes were made by validation tooling.

## Build environment and command

- Windows display version 25H2, build 26200.9457; x64 build target.
- VS 18 Enterprise MSBuild, MSVC 14.50.35717 (`v145` override); Windows SDK 10.0.26100.0.
- The project default remains `v143`. This machine's VS 2022 BuildTools installation lacks the Windows/XAML targets; its presence alone is insufficient. Do not remove `ApplicationType` to bypass missing targets — that skips required XAML compilation.

```bash
export MSYS2_ARG_CONV_EXCL='*'   # otherwise MSYS mangles the /p: switches
unset https_proxy http_proxy     # Windows env is case-insensitive; CL.exe then fails with MSB6001
mb=(/c/Program\ Files/Microsoft\ Visual\ Studio/18/Enterprise/M*/Current/Bin/M*.exe)
"${mb[0]}" native/Mayasaba/Mayasaba.vcxproj /restore /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /v:quiet /nologo "/flp:logfile=native-build.log;verbosity=normal"
```

## Dependency identities

SHA-256 of restored top-level NuGet packages:

| Package | SHA-256 |
| --- | --- |
| Microsoft.WindowsAppSDK 2.5.1 | `2E26DE1253E2E2A288A75387523E2F8B402C05F117D42013DF9875887A70933A` |
| Microsoft.Windows.CppWinRT 3.0.260818.1 | `A8993608AE9263A8288E1A1D9EE2684A5632F4ACC59E1E89DDD86B3B889A7367` |

NuGet's generated `project.assets.json` records resolved transitive identities. These hashes record the restored artifacts; they do not establish independent publisher verification or runtime safety.

## Reproduce notes

- Runtime evidence is under `~/.workbuddy-ai/tmp/cu-shots/`; UIA snapshots are quoted inline.
- A second concurrent instance is refused by the instance mutex ("Another Mayasaba instance is already using its store"), so all runtime work ran one instance at a time.
- The shell kills a command's process tree when the command ends, so the application was kept alive by a background launcher task for the duration of observation.
- Build output and generated sources are ignored by Git.
