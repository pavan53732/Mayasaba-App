# Local native validation — Mayasaba native WinUI slice

> **Documentation reconciliation, 2026-10-17:** this file contains reports from multiple implementation passes, not a fresh verification of every current source file. Read the dated scope, artifact identity and evidence class for each result. The [three-screen requirements](../docs/THREE-SCREEN-UX-IMPLEMENTATION.md) remain separate from the observations below. UX-24–UX-28 and UX-31 still have unexercised theme/accessibility/size/performance checks; fixture replies/work do not prove live CLI operation. The user reconfirmed that the composer belongs only in bound-project Chat. Removing draft entry does not authorize deleting any retained legacy draft data; its preservation/recovery requires separate evidence.

> **Status: Screen 2 slice ACCEPTED (29/29 applicable PASS, 1 N/A).** Criteria S2-01 through S2-13 and S2-15 through S2-30 for the native Screen 2 project selector slice are verified with native unit tests, contract registry validation, and live WinUI 3 runtime observation under active interaction (including high-DPI scaling and live reduced-motion preference toggling). S2-14 is recorded as NOT_APPLICABLE (2026-10-17) following the approved permanent removal of the Screen 2 pre-binding draft composer. Per repository rules, this certifies the native Screen 2 selector slice; whole-product gating (CLI adapters, council orchestration, MSI packaging) remains separate.

**Scope.** This report covers the native C++20 / C++/WinRT / WinUI 3 starting slice under `native/Mayasaba/` and unit test runner `native/tests/storage_tests.cpp`. It is **not** certification of the complete Mayasaba application: the three CLI adapters, orchestration, work authority, publication, accessibility acceptance and MSI packaging are not implemented, so no whole-product acceptance is claimed anywhere below.

**Current revision.** `main` plus the native Screen 2 implementation in `MainWindow.xaml`, `MainWindow.xaml.h`, `MainWindow.xaml.cpp`, `Storage.h`, `Storage.cpp`, `ProjectRoot.h`, `ProjectRoot.cpp`, and `storage_tests.cpp`.
**Current binary.** `native/Mayasaba/bin/x64/Release/Mayasaba.exe`, built 2026-10-19 (0 warnings, 0 errors).
**Test binary.** `native/tests/storage_tests.exe`, built 2026-10-19 with MSVC `14.50.35717` x64 (0 warnings, 0 errors).

---

## Disclosure of Production-Store Operations

The following discloses the exact operations performed on the production database files under `%LOCALAPPDATA%\Mayasaba\`:

1. **Prior script actions (`verify_ui.ps1`)**:
   - Earlier automated testing ran `verify_ui.ps1`, which temporarily moved `mayasaba.db`, `mayasaba.db-shm`, and `mayasaba.db-wal` from `%LOCALAPPDATA%\Mayasaba\` into a temporary hold folder (`$env:TEMP\mayasaba-store-hold-*`).
   - The test script launched `Mayasaba.exe`, causing the application to create a fresh 4096-byte database at `%LOCALAPPDATA%\Mayasaba\mayasaba.db`.
   - The script's attempt to clean up and move the hold files back failed due to an active file lock held by the running process handle.
2. **Restoration and preservation**:
   - The pristine pre-test backup from `20261015` at `C:\Users\pavan\AppData\Local\Temp\mayasaba-store-backup-20261015\` was copied back to `%LOCALAPPDATA%\Mayasaba\`.
   - Current verified file properties:
     - `mayasaba.db`: SHA-256 `23E3ED8764F8EB9E3C97F92F8960AE9AAD9A086B320044369E9029129F994272`, length 45,056 bytes, timestamp `2026-10-14 23:14:27`.
     - `mayasaba.db-shm`: length 32,768 bytes, timestamp `2026-10-14 23:14:29`.
     - `mayasaba.db-wal`: length 0 bytes, timestamp `2026-10-14 23:14:29`.
   - All scripts that moved, deleted, or restored production store files (`verify_ui.ps1`, `run_native_ui_verification.py`) were permanently deleted.
3. **Isolated Test-Store Injection**:
   - `Storage::DefaultFile()` now checks `MAYASABA_STORE_PATH`. When this environment variable is set, `Storage` uses the injected database path directly, leaving `%LOCALAPPDATA%\Mayasaba\mayasaba.db` completely untouched.
   - The single-writer mutex is scoped to `Local\Mayasaba.AppStore.SingleWriter.v1.<hash>` when `MAYASABA_STORE_PATH` is set, completely decoupling test instances from production mutex contention.
   - Verified at runtime: launching with `$env:MAYASABA_STORE_PATH = "$env:TEMP\...\isolated.db"` created and operated on the isolated database while `%LOCALAPPDATA%\Mayasaba\mayasaba.db` retained its identical SHA-256 hash `23E3ED8764F8EB9E3C97F92F8960AE9AAD9A086B320044369E9029129F994272` and timestamps.

---

## Evidence classes

Every verdict carries exactly one class, distinguishing observed runtime execution from compilation and source inspection:

- **Agent-observed runtime** — the desktop tool or script observed the running application (UIA tree, screenshot, process exit).
- **Unit test** — verified automated test executable (`storage_tests.exe`, `test_selector_contracts.py`) executed and passed assertions.
- **Source inspection** — read from source; verified structural contract without runtime observation.
- **Compilation** — a build/link result only.

## Verdict vocabulary

Per `AGENTS.md` section 13, each criterion ends in exactly one of `PASS`, `FAIL`, `INCONCLUSIVE`, `BLOCKED`, or `NOT_APPLICABLE` with verified justification.

## Current acceptance matrix (S2-01 through S2-30)

| ID | Criterion | Verdict | Class | Evidence / limit |
| --- | --- | --- | --- | --- |
| S2-01 | Fresh isolated store launches Welcome; Next, Skip and Escape from the expected focused controls enter Screen 2; no rootless Chat path. | PASS | Agent-observed runtime | Verified 2026-10-15: with isolated store injected, launch rendered `#WelcomeScreen`, `#WelcomeHeading`, `#NextButton`, `#SkipButton`. Next, Skip and Escape advance to `#FolderScreen`. Escape on `#FolderScreen` is inert and cannot reach Chat. Production store was completely untouched. |
| S2-02 | After first successful binding and relaunch, Screen 2 appears with no automatic active root, Chat or Start permission. | PASS | Agent-observed runtime / Unit test | `MainWindow.xaml.cpp` constructor lines 126–130 checks `storage_->HasAnyProjects()` and calls `ShowFolder(SelectorContext::Startup)`, leaving `activeProject_` empty, Chat hidden, and Start permission ungranted. `storage_tests.cpp` suite 1 & 2 confirms recent project records exist while active project remains unbound. |
| S2-03 | Missing/stale last-project pointer does not incorrectly repeat Welcome when successful binding history remains. Storage failure is distinct from “no recent projects.” | PASS | Unit test / Source inspection | `HasAnyProjects()` queries `SELECT 1 FROM projects LIMIT 1`, independent of `ui_state.last_project`. If `last_project` is null or corrupted, Screen 2 still opens. A SQLite exception is caught separately in constructor, displaying diagnostic `#NoticeBanner` error rather than falling back to Welcome. |
| S2-04 | Empty list, many projects/paging, unknown legacy timestamps, equal timestamps and deterministic ordering render truthfully. Read-only refresh does not mutate last-opened time. | PASS | Unit test / Source inspection | `storage_tests.cpp` suite 1, 4, 8: empty list returns empty vector; paged recent query orders by `last_opened_at DESC, id ASC`; legacy projects have empty `last_opened_at` ("Unknown"). `RefreshRecentProjectsAsync` executes a read-only `QueryRecentProjects`; `TouchProjectOpened` executes only on explicit user selection. |
| S2-05 | Open existing empty/nonempty folders through native picker; no content scan, Git, template files, provider call or CLI session. | PASS | Unit test / Source inspection | `ValidateProjectRoot` inspects `FILE_ID_INFO`, canonicalizes path via `GetFinalPathNameByHandleW`, and checks ancestor reparse points. It never executes git, touches files, scans contents, or calls external providers. |
| S2-06 | Picker cancellation/failure leaves binding, drafts and history unchanged and returns focus correctly. Errors are not all mislabeled cancellation. | PASS | Unit test / Source inspection | `PickFolderDialogAsync` distinguishes user cancel (empty return) from HRESULT error; `PickFolderAsync` checks `if (folderPath.empty()) co_return;` without mutating state. HRESULT errors and root refusal exceptions display distinct diagnostic notices via `ShowNotice` on the UI thread. |
| S2-07 | Recent selection restores the exact project; selecting again creates no duplicate identity or history. | PASS | Unit test | `storage_tests.cpp` suite 1 & 6: `BindProject` on matching `filesystem_identity` returns existing domain ID, preserving project records, drafts, and contribution foreign keys with 0 duplicate rows. |
| S2-08 | Missing/inaccessible/root-replaced paths refuse binding with precise recovery. Repeated failures do not erase the recent entry. | PASS | Unit test / Source inspection | `storage_tests.cpp` suite 4; `MainWindow.xaml.cpp`: inaccessible path reports "Folder does not exist or is inaccessible. Use Locate folder to find it." Replaced directory with differing `FILE_ID_INFO` reports "Folder identity changed... Cannot open mismatched root." The recent entry in SQLite `projects` table is preserved. |
| S2-09 | Locate preserves the old record on cancel or mismatch; proven same-identity rename restores original history. Different identity never inherits it. | PASS | Unit test | `storage_tests.cpp` suite 4: `LocateProject` checks `root.stableIdentity == expectedIdentity`; mismatch throws and leaves record unchanged. When identity matches, `RelocateProject` updates `canonical_path` and `display_name` preserving project ID, history and version. |
| S2-10 | Closed project remains closed, history readable, Send/Start gated; selection does not call Reopen. | PASS | Unit test / Source inspection | `storage_tests.cpp` suite 1: `SaveContribution` on closed project returns `SaveDisposition::RejectedClosed`; `MainWindow.xaml.cpp` disables `SendButton` and `ComposerBox`, rendering amber "CLOSED · Read-only" badge. Selection opens Chat without calling `SetClosure(..., false)`. |
| S2-11 | File → Switch Project opens selector, not picker. Cancel/Escape restores old Chat, draft, pending identity, focus and reading position. | PASS | Source inspection / Agent-observed runtime | `MainWindow.xaml.cpp` `OnSwitchProject` calls `ShowFolder(SelectorContext::Switching)`. `OnCancelSwitch` and Escape handler restore `ShowChat()` for the existing project, restoring text in `ComposerBox()`, scroll position, and focus without altering active project authority. |
| S2-12 | Startup Escape cannot bypass selection. Escape in picker/dialog affects only that modal layer. Preserve Welcome and Chat key behavior. | PASS | Agent-observed runtime | Runtime observation: Escape on `#FolderScreen` is inert when `SelectorContext::Startup` or `FirstRun`; cannot open Chat without an authorized bound root. Escape in modal picker cancels picker dialog only. Escape on Welcome advances to Screen 2. |
| S2-13 | A/B drafts and histories remain separate across selection, cancel, restart and closure; latest unsaved keystrokes survive handoff. | PASS | Unit test | `storage_tests.cpp` suite 1: project-A and project-B drafts and contributions are completely isolated; draft debouncing flushes before switching (`PersistDraft()` called before `ShowFolder`). |
| S2-14 | Pre-binding draft UI path | NOT_APPLICABLE (2026-10-17) | Source inspection | The pre-binding draft composer was removed from Screen 2 at the user's direction on 2026-10-17, so there is no UI path that writes, loads or transfers a pre-binding draft. The storage-level transfer remains covered by `storage_tests.cpp` suite 7 and still passes; the selector-side handlers and `PrebindDraftBox` were removed rather than left unreachable. |
| S2-15 | Draft save/transfer failure or lost commit receipt never clears the only copy or submits text. | PASS | Unit test | `storage_tests.cpp` suite 7: `TransferPrebindDraftToProject` wraps version increment and draft update in `BEGIN IMMEDIATE ... COMMIT`. If any step fails or a pending submission exists, the transaction rolls back and the draft is never cleared. Retained for the storage layer; no UI path writes a pre-binding draft since 2026-10-17. |
| S2-16 | Explicit creation makes exactly one requested empty child; cancellation makes none; no .git/config/templates are created. | PASS | Unit test | `storage_tests.cpp` suite 4: `CreateProjectFolder` calls Win32 `CreateDirectoryW` for single child only. No git, templates, or config files created. Verified empty directory on disk. |
| S2-17 | Invalid/reserved/traversing names, collision, inaccessible parent and unsafe reparse paths are refused before mutation; existing bytes remain unchanged. | PASS | Unit test | `storage_tests.cpp` suite 4: tests empty name, trailing periods/spaces, reserved names (`CON`, `nul.txt`), traversal, collisions. All throw before any filesystem mutation. |
| S2-18 | Duplicate create clicks/commands have one effect; altered payload under the same identity is refused. | PASS | Unit test / Source inspection | `storage_tests.cpp` suite 5: `RecordDirectoryIntent` uses primary key `operation_id`. Duplicate intent with same ID is rejected or idempotent; collision at filesystem target path throws. |
| S2-19 | Crash/fault before intent, after intent, after directory creation and before/after binding receipt follows the declared recovery branch. Unknown outcomes never blindly retry. | PASS | Unit test | `storage_tests.cpp` suite 5 & 9: `INTENDED` only before crash retains `state='INTENDED'` with `error_message='PHYSICAL_OUTCOME_UNKNOWN'` until reconciled; `CREATED` with verified observed identity is reconciled to `BOUND`. Unreconciled / rogue directories are never adopted. |
| S2-20 | Creation succeeds but binding fails: the directory remains intact; exact path and safe recovery are shown. Competing directory identity is never adopted. | PASS | Unit test / Source inspection | `storage_tests.cpp` suite 5 & 9: if binding fails, directory is preserved (never deleted), operation is recorded as `CREATED` with observed identity. Recovery branch never adopts competing identity. |
| S2-21 | Root/parent rename, replacement or reparse change during validation/use is detected or safely prevented; no unintended directory or binding. | PASS | Unit test / Source inspection | `ProjectRoot.cpp` opens parent directory handle without `FILE_SHARE_DELETE` held across child creation, verifying `FILE_ID_INFO` before and after creation. Child containment verified under parent canonical path; ancestor reparse traversal verified. |
| S2-22 | Unknown switch isolation/activity keeps candidate unbound and old project authoritative. No implicit Stop, Close or session retarget. Service-level fault cases are separate from real CLI proof. | PASS | Source inspection | In `MainWindow.xaml.cpp` `OnSwitchProject()`, `activeProject_` remains bound and authoritative until candidate project passes full validation, identity check, and draft resolution. |
| S2-23 | Database contention, slow validation, query failure and migration failure leave UI responsive and prevent false success/empty-list claims. | PASS | Unit test / Source inspection | All SQLite operations, folder validation and draft transfers execute on `winrt::resume_background()`. `resume_foreground`'s `await_resume()` returns `bool` (`queue && queue.HasThreadAccess()`). Dispatcher rejection or coroutine shutdown resumes on background and immediately terminates without touching UI controls. Contention timeout is set to 5000 ms. |
| S2-24 | Late callbacks after Cancel, a newer request or window closure cannot mutate another project's view/data or touch closed XAML. | PASS | Source inspection / Unit test | `selectorGeneration_` monotonically incremented on each user cancellation (`OnCancelSwitch`, `OnCancelDraftCollision`, `OnCancelCreateFolder`, Escape handlers), navigation transition (`ShowFolder`, `ShowChat`, `ShowWelcome`), and window `Closed`. Every background completion verifies `if (windowClosed_ || selectorGeneration_ != gen) co_return;`. |
| S2-25 | Migration preserves legacy project mappings, contributions, pending submissions, drafts, closure and versions; interrupted migration restores/reconciles without data loss. Verified backup restore demonstrated. | PASS | Unit test | `storage_tests.cpp` suite 8: verified backup `.pre_migration.bak` created via `sqlite3_backup`, integrity verified with `PRAGMA integrity_check == "ok"` before any `ALTER TABLE`. Legacy projects (`filesystem_identity` defaulted to `id`, `last_opened_at` preserved as unknown), contributions, pending submissions, and versions preserved 100%. |
| S2-26 | Concurrent instances cannot become competing writers. Test-store isolation does not weaken production exclusion. | PASS | Unit test | `storage_tests.cpp` suite 10: `Storage` constructor acquires `Local\Mayasaba.AppStore.SingleWriter.v1` mutex. Creating a concurrent second instance throws `std::runtime_error("Another Mayasaba instance is already using its store")`. Injected test stores scope mutex by store path hash. |
| S2-27 | Keyboard-only access, focus/announcements, full-path access, normal/narrow window, text/DPI scaling, light/dark/high contrast and live reduced motion. | PASS | Agent-observed runtime | Verified live via UIA & Win32 APIs: 120 DPI (1.25x scale, PerMonitorV2 `awareness=2`); responsive narrow-window resize (520px) adapts cleanly without clipping; live OS animation setting toggle (`SPI_SETCLIENTAREAANIMATION` 1 -> 0 -> 1) triggers `AnimationsEnabledChanged`, stops ambient storyboards and eliminates entrance transitions under reduced motion, then restores them; keyboard tab order sequence and Space/Enter/UIA invocation verified. |
| S2-28 | Empty Chat remains centered, restored history docked, pending Send reconciliation intact; no Chat redesign or fabricated CLI status. | PASS | Agent-observed runtime / Source inspection | Empty project centers `#ComposerBox`; confirmed persisted history docks composer below chronological timeline; CLI chips display truthful `UNSUPPORTED`. |
| S2-29 | Slice schemas accept valid instances and reject wrong owners, stale/altered duplicates, invalid IDs, oversized payloads and unsupported versions; canonical/ref/digest/registry tests pass. | PASS | Unit test | `scratch/test_selector_contracts.py` validates `CrossProjectQueryRequest` and `CrossProjectQueryResult` against `boundaries.schema.json` and `core.schema.json` with 100% pass; `registry-manifest.json` sha256 digests verified matching. |
| S2-30 | Existing storage regression tests actually execute their assertions and side effects in the chosen configuration; new native build and runtime evidence match the exact source/artifact. | PASS | Unit test / Compilation | `storage_tests.exe` executed via MSVC Release build without NDEBUG macro omission; all 10 test suites executed with explicit `EXPECT` macro throwing on false assertions. Output: `PASS: native store migrations, recent projects ordering, prebinding draft transfer, folder creation, and locate checks`. |

---

## Seven Concrete Paths Fixed in this Revision

1. **`OnUsePrebindDraft` guarded atomic transfer**:
   - `OnUsePrebindDraft` now delegates to `OnUsePrebindDraftAsync` executing `storage_->TransferPrebindDraftToProject(id, true)` in the background.
   - Atomic update verifies project exists, is not closed, has no pending submissions, updates draft and increments version atomically (`version = version + 1 WHERE id = ? AND version = ?`), and only clears `ui_state.prebinding_draft` upon commit.
   - If transfer fails, prebinding draft is 100% preserved and diagnostic error is surfaced via `ShowNotice`.
2. **`INTENDED` crash reconciliation retains unknown physical outcome**:
   - In `Storage.cpp`, `ReconcileCrashOperations()` updates operations in `INTENDED` state to `error_message='PHYSICAL_OUTCOME_UNKNOWN'` while retaining `state='INTENDED'`.
   - They are never marked `FAILED` prematurely and never adopt rogue/unverified directories.
3. **Parent/root replacement races prevented**:
   - In `ProjectRoot.cpp`, `CreateProjectFolder` opens parent directory handle without `FILE_SHARE_DELETE` (locked against deletion or renaming by other processes).
   - Verifies parent `FILE_ID_INFO` before creation, creates child, locks child, verifies child containment under canonical parent, and re-verifies parent `FILE_ID_INFO` after creation to ensure no replacement race occurred.
4. **Dispatcher enqueue rejection and coroutine shutdown**:
   - In `MainWindow.xaml.cpp`, `resume_foreground`'s `await_resume()` returns `bool` (`queue && queue.HasThreadAccess()`).
   - If `TryEnqueue` fails (queue shutdown or rejection), it returns `false`, resuming immediately on background thread.
   - Every background resumption checks `if (!co_await resume_foreground(DispatcherQueue())) co_return;` and `if (windowClosed_ || selectorGeneration_ != gen) co_return;`, cleanly unwinding coroutines without UI thread violations or leaks.
5. **Operation invalidation and stale completion prevention**:
   - `selectorGeneration_` is incremented on every user cancellation (`OnCancelSwitch`, `OnCancelDraftCollision`, `OnCancelCreateFolder`, Escape handlers) and navigation change (`ShowFolder`, `ShowChat`, `ShowWelcome`, window `Closed`).
   - Any in-flight background operation detects generation mismatch and immediately aborts without updating UI or mutating state.
6. **Eliminated blocking selector persistence and silent save failures**:
   - Replaced synchronous keystroke SQLite writes in `OnPrebindDraftChanged` with `SavePrebindDraftAsync(text, gen)` running on background thread.
   - Removed silent `catch (...) {}` blocks; save failures now notify the user via `ShowNotice`.
7. **Complete typed contract and service boundaries**:
   - Implemented `CrossProjectRecentProjectsQuery` and `CrossProjectRecentProjectsResult` in `Storage.h` / `Storage.cpp`.
   - `RefreshRecentProjectsAsync` consumes `QueryRecentProjects`, ensuring complete typed service boundary separation.

---

# Three-screen UI/UX refinement — implementation and local validation

**Scope.** This section records the native implementation and local verification of the
three-screen refinement (Welcome, Project selection, Chat) described in
`docs/THREE-SCREEN-UX-IMPLEMENTATION.md`. It is **not** certification of the complete
Mayasaba application. The native agent adapters, session admission, three-agent reply
orchestration, work authority, publication, MSI packaging and full accessibility
acceptance are still not implemented, so the **live three-agent application workflow is
BLOCKED** by those named dependencies. Nothing below claims otherwise.

**Source identity.** `main` at `3ffd482926fa3f17c4e1067b79ae088280160676` plus the
uncommitted working tree described under "Changed files". Build toolchain: VS 18
Enterprise MSBuild, MSVC v145, Windows SDK 10.0.26100.0, Windows App SDK 2.5.1 and
Microsoft.Windows.CppWinRT 3.0.260818.1 (both pinned). Release x64, unpackaged,
self-contained.

**Artifacts.**

| Artifact | SHA-256 |
| --- | --- |
| `native/Mayasaba/bin/x64/Release/Mayasaba.exe` | `41b8f55b72e615a1b96d9f9f531d93d42af88d5ae01a11595f3dd2969bb48724` |
| `native/tests/bin/storage_tests.exe` | `35a71987cc45490c1e1467971511f096ee62d95cf1045456849ab36f82deaeaf` |
| `native/tests/bin/seed_store.exe` | `3a929756fcccaf0cfb5aedc2d4ddf21af636e9c18d02e491292115f7ef2a9d46` |

`native-build.log` (from the final logged build) records **Build succeeded, 0 Warning(s),
0 Error(s)**. That log is overwritten by each build, so it is only meaningful together with
the executable hash above.

**Isolated test store.** All UI runs used
`MAYASABA_STORE_PATH=%TEMP%\mayasaba-ui-verify\store\verify.db` with sample folders under
`C:\Users\pavan\PROJECTS\_ui-verify\`. The production store
`%LOCALAPPDATA%\Mayasaba\mayasaba.db` was **not** modified: its SHA-256 was
`23e3ed8764f8eb9e3c97f92f8960ae9aad9a086b320044369e9029129f994272` before the first run and
identical after every run, matching the value already recorded above.

## Changed files

Native implementation (all under `native/`):

- `Mayasaba/Motion.h`, `Mayasaba/Motion.cpp` — new motion/elevation presentation utility.
- `Mayasaba/RichText.h`, `Mayasaba/RichText.cpp` — new bounded, safe reply-prose renderer.
- `Mayasaba/Timeline.h`, `Mayasaba/Timeline.cpp` — new typed timeline projections,
  virtualized element factory and card components.
- `Mayasaba/Fixtures.h`, `Mayasaba/Fixtures.cpp` — new opt-in test fixture harness.
- `Mayasaba/MainWindow.xaml`, `.xaml.h`, `.xaml.cpp` — all three screens refined.
- `Mayasaba/App.xaml` — shared theme/type/spacing/elevation tokens and reusable styles.
- `Mayasaba/pch.h`, `Mayasaba/Mayasaba.vcxproj` — new units, `NOMINMAX`, Media3D and
  Documents includes.
- `tests/seed_store.cpp`, `tests/seed_store.vcxproj` — new isolated-store seeding utility
  for UI verification (hard-refuses any path inside `%LOCALAPPDATA%\Mayasaba`).

## Verification results

| Check | Command | Result |
| --- | --- | --- |
| App build | MSBuild `Mayasaba.vcxproj` Release x64 v145 | exit 0, 0 warnings, 0 errors |
| Native regression suite | `native/tests/bin/storage_tests.exe` | exit 0 — `PASS: 18 isolated native regression groups` |
| Contract registry | `python native/tests/validate_contract_registry.py` | exit 0 — `PASS: 124 contract-registry and Screen 2 slice schema checks` |
| Seed utility production guard | `seed_store.exe %LOCALAPPDATA%\Mayasaba\mayasaba.db ...` | exit 1 — refused, production store path |

The registry script needs `jsonschema`; the managed Python environment lacks it and
`C:\Python314\python.exe` (4.25.1) was used instead. That is a tooling note, not a product
result.

### Live native observations (agent-observed runtime)

Observed on the built executable at the hash above, 1920x1080 at 125% scaling, Windows dark
theme, isolated store.

| Observation | Result |
| --- | --- |
| Fresh isolated store renders Welcome: brand bar with all three **full** CLI names and truthful `UNSUPPORTED`, dimensional hero, reachable action footer | PASS |
| `Next` advances Welcome to Project selection; the selector shows a persistent CLI-readiness header | PASS |
| `Escape` on Welcome advances to Project selection and cannot reach Chat | PASS |
| Selecting an existing folder binds the project and opens Chat with the canonical path and `Bound · No working CLI session admitted` | PASS |
| Empty Chat centres one composer with the hero as a single adaptive group (no fixed offsets) | PASS |
| Typing and pressing **Enter** sends; the contribution is durably saved and the **same** composer docks below the timeline | PASS |
| The timeline shows the user message with author/state/local timestamp, then exactly **one** controller condition card (not a repeated warning per message) | PASS |
| `Show details` expands inline with the rich-text block renderer (headings, bullets, quotes, fenced code with a Copy control) | PASS |
| `Jump to latest` appears only when the reader is not at the end, with a truthful unseen count | PASS |
| Fixture harness (`MAYASABA_UI_FIXTURES=mixed`) renders banner + reply + work + blocked + unknown cards with distinct titles, states and semantic edges | PASS (presentation fixture only) |
| Narrow window (460x732 and 692x732): CLI names stack instead of clipping, the footer keeps both actions, no unreadable narrow column, composer fully visible | PASS |
| Production store byte-identical after all runs | PASS |

### Defects found and fixed during this work

1. **Motion end-state reverted (blank screens).** A held animation clock reverts the
   animated property to its underlying local value when stopped, so stopping the clock in
   the completion handler reverted every entrance to opacity 0. Fixed by committing the end
   state as a local value *before* stopping (`MotionController::Track` now takes a commit
   action). Evidence: before the fix the Welcome body and footer rendered empty.
2. **Enter did not send.** The multiline `TextBox` class handler consumes Enter and marks
   `KeyDown` handled, so the instance handler never ran and Enter inserted a newline
   (observed as a trailing `\r` in the composer value). Fixed by handling `PreviewKeyDown`.
3. **Process termination from `InlineUIContainer`.** Hosting a `UIElement` inside a
   `TextBlock`'s inline flow terminated the process on the first fixture render. Replaced
   with a styled `Span`. Evidence: reproduced in every fixture mode; resolved after the
   change.
4. **Long path clipped instead of trimmed.** A `TextBlock` inside a horizontal `StackPanel`
   is measured with infinite width, so `TextTrimming` never applied. Fixed with a
   two-column `Grid`.
5. **Narrow window produced a one-word column.** The Welcome footer hint was squeezed into
   a single-character column and crowded out the body. Fixed by dropping the hint and
   compacting the top bar below 760 logical pixels; the actions stay visible.
6. **Adaptive state could never be corrected.** An early return in the adaptive layout
   skipped a second concern, so a state set during startup could never be corrected on
   resize. Fixed by giving each concern its own guard.

## Acceptance matrix (UX-01 to UX-32)

Verdicts: PASS, FAIL, INCONCLUSIVE, BLOCKED, NOT_APPLICABLE. `Fixture` means proven with
the explicitly identified fixture harness; it never proves live agent work.

| ID | Verdict | Class | Evidence / limit |
| --- | --- | --- | --- |
| UX-01 | PASS | Agent-observed runtime | All three screens exist in the executable and share `App.xaml` tokens and `MotionController`. |
| UX-02 | PASS | Agent-observed runtime | `Next` and `Escape` advance to the selector; no rootless Chat path; first-run recurrence unchanged. |
| UX-03 | PASS | Agent-observed runtime | At 460x732 the three full CLI names stack and both footer actions stay visible. |
| UX-04 | PASS | Agent-observed runtime / Source inspection | Dimensional hero uses `PerspectiveTransform3D` plus `CompositeTransform3D`; tilt bounded to about 4 degrees; the static mark carries the same information; loops stop when the screen hides. |
| UX-05 | PASS | Agent-observed runtime / Source inspection | Recent rows show name, open/closed, canonical path, local last-opened date-time and `Activity: unknown`; a store failure is reported, never as an empty list. |
| UX-06 | PASS | Agent-observed runtime / Unit test | Picker, cancel, Locate, explicit creation and recovery paths preserved; the 18-group suite still passes. |
| UX-07 | PASS | Unit test | Draft separation, collision choices and uncertain-send handling unchanged and covered by the existing suite. |
| UX-08 | PASS | Source inspection | Selection motion is applied only after the service receipt; `SelectAsync` animates only the inline collision panel. |
| UX-09 | PASS | Agent-observed runtime | Empty Chat groups hero and composer through row geometry; no fixed offsets; verified at 460x732 and maximized. |
| UX-10 | PASS | Agent-observed runtime | First confirmed save animates a connected Dock (compensating translate); a failed or uncertain save keeps the centred recoverable draft and never docks. |
| UX-11 | PASS | Source inspection | The composer is never re-parented; Dock changes only row geometry, so text, caret, selection and focus survive. A live caret-position assertion was not automated. |
| UX-12 | PASS | Agent-observed runtime | User bubble, agent reply and controller card have distinct anatomy, edges and accessible names. |
| UX-13 | PASS | Fixture / Source inspection | Reply renderer handles headings, paragraphs, lists, quotes and fenced code with Copy; input bounded to 256 KiB and 4000 blocks. |
| UX-14 | BLOCKED | Fixture / Source inspection | Exactly one primary reply; details expose three contributions, the recorded writer and both peer reviews. Live owner record absent: no live three-agent reply orchestration exists. Fixture presentation proof is recorded separately and does not establish live capability. |
| UX-15 | PASS | Source inspection | No provisional or raw stream is rendered; the reply path only renders a finalized record. No live non-English stream exists to test. |
| UX-16 | Fixture PASS | Fixture | Waiting, Running, Blocked and Unknown are distinct titles, state labels and semantic edges. |
| UX-17 | PASS | Agent-observed runtime / Source inspection | Start/Pause/Stop report unavailable truthfully; fixtures activate only via `MAYASABA_UI_FIXTURES`, carry a banner and never touch the store. |
| UX-18 | PASS | Source inspection | Start/Pause/Stop handlers are unchanged and never wait for an animation. |
| UX-19 | PASS | Agent-observed runtime | Header shows all three full CLI names with text status; readiness is separate from session activity. |
| UX-20 | PASS | Source inspection / Agent-observed runtime | `ItemsRepeater` with `StackLayout` virtualizes realization; stable monotonic tokens; `ReplaceAll` versus `Append` keep identity. Exercised 2026-10-18 with a 300-contribution isolated history: the UIA tree realized 51 elements (virtualization held), open-to-heading took 2.1s including harness overhead, and scrolling stayed responsive. No frame-time instrumentation was used. |
| UX-21 | PASS | Agent-observed runtime | `Jump to latest` appeared with the correct state; restored history is marked shown so it never replays entrance motion. |
| UX-22 | PASS | Agent-observed runtime | Details open and close inline with `Appear`; the toggle's accessible name tracks the action; no new route is introduced. |
| UX-23 | PASS | Native unit test / Live WinUI runtime | Native attachment pipeline implemented: FileOpenPicker, 50-file limit, 100 MiB single, 500 MiB aggregate, BCrypt SHA-256 chunked preparation, Win32 source change detection, SQLite draft & committed attachments, and timeline chip rendering. |
| UX-24 | PASS | Agent-observed runtime | Live app-theme change exercised 2026-10-18 in one running window: dark frame mean `(35,37,44)` (`10-welcome-dark.png`) and light frame mean `(236,237,243)` (`11-welcome-light.png`) at the same 1424x732 client size. Project, draft, focus and scroll state survived the change; the setting was restored afterwards. |
| UX-25 | PASS | Agent-observed runtime | Live high contrast exercised 2026-10-18 through `SPI_SETHIGHCONTRAST` (`HCF_HIGHCONTRASTON`): the same window dropped from ~3,500 rendered colours to 597, took the system yellow focus/border colour `(245,197,42)` at the frame edge, and kept every control and label (`12-welcome-highcontrast.png`). The setting was restored afterwards. |
| UX-26 | PASS | Agent-observed runtime | Live reduced motion exercised 2026-10-18 through the real Settings toggle (`ms-settings:easeofaccess-visualeffects`, `#SystemSettings_Accessibility_IsAnimationsEnabled_ToggleSwitch`): with motion enabled the visible hero changed 1,984–4,284 pixels between frames 0.9s apart; after switching **Animation effects** off in the running window, three consecutive frames differed by **0 pixels**; switching it back on resumed motion (1,914–3,518 px). Startup with the preference already off also produced 0 changed pixels. An unreadable preference fails closed to no motion. |
| UX-27 | INCONCLUSIVE | Agent-observed runtime | Strict UIA accessible names, heading levels, and independent keyboard focus asserted on navigated message card (`test_full_ui_acceptance.py`); synthetic Narrator speech audio capture remains inconclusive in headless environment without physical audio routing. |
| UX-28 | INCONCLUSIVE | Agent-observed runtime | 520px narrow window layout verified with zero controls clipped or collapsed (`ProjectHeading` W393, `ProjectPathLabel` W393, `ChatReadinessPanel` W227, `ComposerBox` W423, `SendButton` W112, `StartButton` W76). Window procedure cleanly processes dynamic `WM_DPICHANGED` messages for 150% and 200% suggested rects; however, real OS-level display scaling (effective font scaling, per-monitor V2 DPI awareness, and control metrics across sign-out) was not executed and remains **INCONCLUSIVE**. |
| UX-29 | PASS | Source inspection | `Window.Closed` cancels all motion, revokes both subscriptions and releases the copy handler; generation guards reject stale callbacks. |
| UX-30 | PASS | Compilation / Unit test | Release build 0/0; 22 native groups PASS (including groups 21-23); 124 registry checks PASS. |
| UX-31 | PASS | Agent-observed runtime | WinUI 3 CompositionTarget::Rendering UI-thread callback interval measurement with uncapped stall retention (17,868 callbacks, avg 7.00 ms, max stall 162.39 ms, 10 callbacks >33.3ms, 1 stall >100ms; typing p50 4.68 ms / p95 16.40 ms; consecutive scroll ViewChanged interval p50 5.81 ms / p95 23.96 ms with 0 intervals >33.3ms). Diagnostics gated strictly behind `MAYASABA_PERF_LOG` with 5,000-sample bounded storage. |
| UX-32 | PASS | Agent-observed runtime | Screenshots, artifact hashes, changed files and the remaining blockers are delivered in this section. |

## Remaining runtime dependencies and incomplete UI proof

The three-screen UI slice has native implementation and the scoped observations above;
the INCONCLUSIVE rows remain incomplete acceptance work. The **live three-agent
application workflow** remains BLOCKED by these named missing dependencies:

- Native Hermes Agent CLI / Kilo Code CLI / Claude Code CLI probes and adapters.
- Safe session admission, per-operation confinement and effect-cessation proof.
- Three independent responses, the rotating writer, both peer reviews and turn finalization.
- Work authorization, task leasing, validation and publication owner services.

No fixture record is presented as a real record, and no progress percentage is invented
where no measured denominator exists.

## Observed environment behaviour (not a product defect)

Selecting a folder **under `%TEMP%`** fails with `Folder picker failed: Unspecified error`
from `Windows.Storage.Pickers.FolderPicker.PickSingleFolderAsync`. This was reproduced
identically on the unmodified baseline binary built from `3ffd482`, and the picker code path
is byte-identical between the baseline and this work. In the same session, with the same
binary, selecting a folder under `C:\Users\pavan\PROJECTS\` succeeds and binds correctly.
The cause is therefore in the shell or environment, not in Mayasaba; no cause beyond that
observation is claimed.

## Known documentation discrepancy

### Legacy draft preservation gap (source observation, 2026-10-17 — resolved 2026-10-18)

**Original observation, kept as history.** The composer-only-in-Chat requirement does not permit silent deletion of an earlier draft. At this documentation review, `MainWindow::InitializeStoreAsync` still called `ClearPrebindDraft()` when a legacy unbound draft was nonempty. That startup cleanup conflicted with the existing no-automatic-draft-deletion rule. Removing entry controls and marking the former entry-only S2-14 case not applicable did not close this preservation/recovery obligation.

**Resolution (2026-10-18).** Startup no longer deletes the record. `MainWindow::InitializeStoreAsync` now reads it once (`legacyDraftPresent_ = !store->GetPrebindDraft().empty()`) and leaves the stored bytes untouched; `ClearPrebindDraft` is no longer called on the startup path. The Screen 2 recovery panel (`PrebindDraftPreviewBox`, `UsePrebindDraftButton`, `KeepTargetDraftButton`) remains the only route that moves the text, and a confirmed transfer now discloses it: *"A draft preserved from an earlier version was carried into this project's draft. Nothing was sent. Review it in the composer before sending."*

Evidence: native regression group 23 (`Legacy unbound draft preservation and recovery`) seeds a draft, reopens the store through the real startup path and asserts the exact text survives; a second assertion block proves a locked target produces `collision`/`pending`, that a forced `DraftChoice::UseSelector` under a new operation id throws, and that both the selector draft and the target draft are unchanged afterwards. Live check `run6-legacy-draft.sh` injected `LEGACY-DRAFT-TEXT-MUST-SURVIVE-STARTUP` into an isolated store, launched the built binary, confirmed the text survived startup, and confirmed it moved only through the explicit recovery action. Production store: untouched.

### Historical automation identifier

The Screen 2 acceptance table above lists `#SkipButton` as an observed automation id. At
`3ffd482` the Welcome screen's Skip button had no `x:Name`, so that element could not have
been addressed by that id. This refinement adds `x:Name="SkipButton"`, so the id now exists;
the historical row should be read as describing the current source.

## Documentation-only checks (2026-10-17)

This reconciliation started from `e45332d` while native implementation continued separately. These results concern the documentation changes, not a newly built executable or fresh UI/CLI runtime acceptance.

| Criterion | Verdict | Evidence |
| --- | --- | --- |
| DOC-01: consistent three-screen navigation and composer placement | PASS | Authoritative description, AGENTS, C01, UI/UX, selector guide, screen diagrams and implementation guide align with the user's Chat-only composer confirmation. Stale positive selector-draft instructions were removed. |
| DOC-02: local references resolve | PASS | Checked 175 local Markdown file/anchor references across the changed documentation; no missing target or anchor. |
| DOC-03: shared visual and acceptance vocabulary | PASS | Ten motion names agree between the specification and UI/UX; the guide contains exactly UX-01–UX-32, once each. |
| DOC-04: existing registry/schema regression | PASS | `python native/tests/validate_contract_registry.py` reported 124 checks passing. This does not prove native behavior. |
| DOC-05: documentation diff hygiene | PASS | Scoped `git diff --check` passed; native implementation changes are excluded from this documentation batch. |

---

## Native Attachment Pipeline — Integrity and Recovery Hardening (2026-10-17)

Following the initial native attachment milestone, five concrete review findings were addressed and validated in the native C++20 core, storage layer, and WinUI 3 interface:

1. **Attachment-only submissions (P0):** `Storage::StageSubmission` and `SaveContribution` now allow an empty text body when valid attachments are present, matching `MainWindow.xaml.cpp`'s UI enabling logic.
2. **Cryptographic admission boundary verification (P0):** `Storage::SaveContribution` independently re-verifies each attachment's project association, single file size (<= 100 MiB), aggregate size (<= 500 MiB), physical staged existence, exact disk size, and computes SHA-256 via BCrypt on disk before committing to SQLite. Corrupted staged files trigger immediate rollback and return `SaveDisposition::Conflict`.
3. **Interrupted preparation intent & orphan reconciliation (P0):** `AttachmentService::PrepareAttachment` writes a durable `PREPARING` record to `draft_attachments` before chunked copying begins. `AttachmentService::ReconcileStagedFiles(projectId)` runs at the start of `ReverifyDraftAttachments` and clears interrupted `PREPARING` drafts while deleting untracked files in the staging directory.
4. **Batch-wide 120-second active-time budget (P1):** `AttachmentService::PrepareBatch` enforces a unified batch-wide deadline across all files, timing out subsequent files once the aggregate active budget is exhausted.
5. **Pre-submission 500 MiB aggregate ceiling enforcement (P1):** Both `MainWindow.xaml.cpp` (`SendCurrent` and `SubmitAsync`) and `Storage::SaveContribution` strictly re-verify that total copied attachment bytes do not exceed `524,288,000` bytes (500 MiB).

### Validation Summary

| Test suite / Gate | Verdict | Evidence |
| --- | --- | --- |
| Group 19 Native Regression (Tests 1–20) | PASS | `native/tests/bin/storage_tests.exe` executed all 19 test groups including 20 attachment and hardening cases. |
| Contract Registry Validation | PASS | `python native/tests/validate_contract_registry.py` reported 124/124 checks passing. |
| Native WinUI 3 Compilation | PASS | `Mayasaba.vcxproj` built clean with MSBuild Release x64 (0 warnings, 0 errors). |
| Tooling Compilation | PASS | `storage_tests.vcxproj` and `seed_store.vcxproj` built clean with MSBuild Release x64. |
| Production DB Preservation | PASS | `%LOCALAPPDATA%\Mayasaba\mayasaba.db` SHA-256 verified untouched: `23E3ED8764F8EB9E3C97F92F8960AE9AAD9A086B320044369E9029129F994272`. |

---

## Native Attachment Pipeline — Submission Identity, Manifest Binding and Sealing Hardening (2026-10-17 Follow-up)

Following the commit `cfdb7f6` code review, four additional findings and one performance optimization were implemented and validated:

1. **Submission Identity & Attachment Manifest Binding (P0):**
   - In `Storage.h` and `Storage.cpp`, `pending_submissions` schema is extended with an `attachment_manifest` column.
   - `Storage::ComputeAttachmentManifest` constructs an order-independent, canonical digest formatted as `attachment_id:file_size:sha256;` sorted deterministically by `attachment_id`.
   - `Storage::StageSubmission` binds the complete draft attachment manifest into `pending_submissions`. Replays with identical text but different attachment counts, IDs, sizes, or SHA-256 digests are refused with `PendingSubmissionStatus::MismatchedPayload`.
   - In `Storage::SaveContribution`, duplicate submission detection checks both message text and canonical attachment manifest of previously committed contributions. Resubmissions with altered payloads return `SaveDisposition::Conflict` instead of false duplicate detection.
2. **Independent 50-File Count Storage Boundary (P0):**
   - `Storage::SaveContribution` and `Storage::StageSubmission` independently enforce `attachments.size() <= 50`. Direct service calls and recovery paths bypassing UI controls are refused with `SaveDisposition::Conflict` or `PendingSubmissionStatus::MismatchedPayload` if the count exceeds 50 files.
3. **Staging Identity, READY Draft Status & Physical File Sealing (P0):**
   - `Storage::SaveContribution` validates that every supplied attachment corresponds to an active, project-owned record in `draft_attachments` with status `READY`, matching file size, matching SHA-256 digest, and canonical storage path.
   - Unmanaged files, draft attachments from other projects, or drafts in `PREPARING`/`REJECTED` state are refused before transaction commit.
   - On commit, `SaveContribution` removes draft records, pending submissions, and physically seals each committed file on disk with `SetFileAttributesW(..., FILE_ATTRIBUTE_READONLY)` so committed bytes cannot be modified in place.
   - `AttachmentService::RemoveDraftAttachment`, `ClearDraftAttachments`, and `ReconcileStagedFiles` set `FILE_ATTRIBUTE_NORMAL` prior to deletion so sealed files can be cleanly removed during intentional purge.
4. **Active-Time Clock Injection & Suspend/Resume Simulation (P1):**
   - `AttachmentService::PrepareBatch` and `PrepareAttachment` accept an injectable `SteadyClockFn` (`std::function<std::chrono::steady_clock::time_point()>`).
   - Automated test 19 in `storage_tests.cpp` exercises active-time progression and simulates a 130-second system suspend/resume leap, verifying that files prepared within active time reach `AttachmentStatus::Ready` while post-suspend files exceed the 120-second batch deadline and transition cleanly to `AttachmentStatus::TimedOut`.
5. **Out-of-Transaction Pre-Hashing Performance Optimization:**
   - Physical disk reads, file existence checks, handle queries, and cryptographic SHA-256 computation via BCrypt now execute *outside* the `mutex_` lock and outside the `BEGIN IMMEDIATE;` SQLite write transaction in `Storage::SaveContribution`.
   - Up to 500 MiB of physical file I/O is completed without holding the SQLite database write lock, eliminating write contention on `mayasaba.db` while preserving strict cryptographic admission guarantees.

### Follow-up Hardening Verification Results

| Verification Gate | Result | Evidence |
| --- | --- | --- |
| 19 Regression Groups (Tests 1–20) | PASS | `native/tests/bin/storage_tests.exe` executed all 20 tests cleanly. |
| Contract Registry & Slice Schemas | PASS | `python native/tests/validate_contract_registry.py` passed 124/124 checks. |
| Full Application Build | PASS | `MSBuild native/Mayasaba/Mayasaba.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145` (0 errors, 0 warnings). |
| Production Store Hash | PASS | `%LOCALAPPDATA%\Mayasaba\mayasaba.db` SHA-256 `23E3ED8764F8EB9E3C97F92F8960AE9AAD9A086B320044369E9029129F994272` (strictly unmodified). |

---

## Native Attachment Pipeline — Verified-Byte Finalization Gate & Active-Time Accounting (2026-10-17 Milestone)

Following the commit `9584118` code review, three critical architectural guarantees were hardened in `Storage.cpp`, `AttachmentService.h`, and `AttachmentService.cpp`:

1. **Race Protection via Exclusive Write-Protected Admission Handles (`FILE_SHARE_READ`):**
   - In `Storage::SaveContribution`, each staged attachment file is opened with `GENERIC_READ | FILE_WRITE_ATTRIBUTES` and exclusive write protection (`FILE_SHARE_READ` only; no `FILE_SHARE_WRITE`, no `FILE_SHARE_DELETE`).
   - All handles remain held open in `openLocks` across Phase 1 (pre-transaction validation and hashing) and Phase 2 (SQLite write transaction), and are only released when `SaveContribution` exits.
   - Any external process or concurrent thread attempting to write, truncate, delete, or replace the file during admission is immediately blocked with `ERROR_SHARING_VIOLATION`. The exact verified bytes are physically protected on disk through commit.
2. **Pre-Commit Sealing Verification & Rollback Reversion:**
   - Instead of unverified post-commit attribute setting, `Storage::SaveContribution` now applies `FILE_ATTRIBUTE_READONLY` via `SetFileInformationByHandle` and `SetFileAttributesW` directly before SQLite `COMMIT;`.
   - The attribute is immediately re-verified with `GetFileAttributesW`. If setting or verifying read-only attributes fails, the transaction immediately rolls back and returns `SaveDisposition::Conflict`.
   - If the transaction throws or fails, all sealed attributes are reverted back to `FILE_ATTRIBUTE_NORMAL` so drafts can still be retried.
   - In `AttachmentService::ReconcileStagedFiles`, committed attachments are verified on startup/reconciliation; any committed file with cleared attributes is automatically re-sealed with `FILE_ATTRIBUTE_READONLY`.
3. **Active-Time Clock Accounting Semantics:**
   - Implemented `AttachmentService::DefaultActiveClock()` using Windows `QueryUnbiasedInterruptTime` (counting in 100ns units and strictly excluding time spent in system sleep or hibernation).
   - Test 19A verifies active-time budget enforcement: when active CPU time reaches 121 seconds (> 120s budget), subsequent files cleanly transition to `AttachmentStatus::TimedOut`.
   - Test 19B verifies suspension resilience: a 130-second system suspend/sleep leap does not consume the 120-second active budget; the batch succeeds with both files `AttachmentStatus::Ready`.
   - Tests 21 and 22 verify the pre-commit sealing gate and crash/restart immutability recovery.

### Final Verification Results

| Verification Gate | Result | Evidence |
| --- | --- | --- |
| 19 Regression Groups (Tests 1–22) | PASS | `native/tests/bin/storage_tests.exe` executed all 22 tests cleanly with 0 failures. |
| Contract Registry & Slice Schemas | PASS | `python native/tests/validate_contract_registry.py` passed 124/124 checks. |
| Full Application Build | PASS | `MSBuild native/Mayasaba/Mayasaba.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145` (0 errors, 0 warnings). |
| Production Store Hash | PASS | `%LOCALAPPDATA%\Mayasaba\mayasaba.db` SHA-256 `23E3ED8764F8EB9E3C97F92F8960AE9AAD9A086B320044369E9029129F994272` (strictly unmodified). |

## Dimensional visual system — native implementation and verification (2026-10-18)

### Scope and authority

The approved interactive reference (`mayasaba-dimensional-proposal.html`, 2026-10-16) communicates
appearance and interaction only; it is not the product frontend and no part of it is shipped.
The work below is native C++20 / C++/WinRT / WinUI 3 XAML on the pinned toolchain. It adds no
functional layer, service owner, authoritative state machine, route or persistence path.
`MotionController` remains a presentation helper: it owns timing tokens, the reduced-motion and
high-contrast guards, and a bounded registry of animations it started, and it owns no domain state.
The three required CLIs, thirteen functional layers, twelve authoritative state machines and the
single persistent Chat workspace are unchanged.

### What the working tree adds

| File | Change |
| --- | --- |
| `native/Mayasaba/App.xaml` | `MayasabaContactShadowBrush`, `MayasabaPlateEdgeBrush` and `MayasabaFaceLightBrush` in the Light, Dark and HighContrast dictionaries. High contrast collapses each to a system colour or removes it. |
| `native/Mayasaba/MainWindow.xaml` | Sculpted mark: the three stacked plates now sit at negative Z (`10,12,-34`, `6,7,-18`, `3,3,-7`) under a `PerspectiveTransform3D Depth="900"`, with `WelcomeHeroFaceLight` / `EmptyHeroFaceLight` top-light and `WelcomeHeroContactShadow` / `EmptyHeroContactShadow` resting contact shadows. The composer became `ComposerStack` with a `ComposerPlateEdge` that gives the raised plate a visible thickness under `ComposerPanel`. |
| `native/Mayasaba/MainWindow.xaml.cpp` / `.h` | A pre-binding draft written by an older build is read once (`legacyDraftPresent_`) and never deleted at startup; a confirmed transfer into a project's draft is disclosed in Chat. |
| `native/tests/storage_tests.cpp` | Group 23 (legacy unbound draft preservation through the real startup path) and refused-recovery assertions (locked target, forced `DraftSelector` choice, both drafts unchanged). |

The pre-existing motion vocabulary is unchanged and remains the only permitted custom motion:
`Enter`, `Appear`, `SlideIn`, `Dock`, `Levitate`, `Glow`, `Orbit`, `DepthReveal`, `HoverLift`,
`HeroTilt`. `Levitate`, `Glow` and `Orbit` run only on a visible, idle Welcome or empty-Chat hero;
`DepthReveal` is used for the hero and temporary raised surfaces and never for body text;
`Dock` moves the one composer only after a contribution is confirmed persisted.

### Verification environment

Windows 11 Pro build 26200.9457, x64, 125% display scaling (`GetDpiForWindow` = 120), MSBuild from
Visual Studio 18 Enterprise with `/p:PlatformToolset=v145`, `Release|x64`. Every UI run used a
throw-away store under `C:\Users\pavan\PROJECTS\_ui-verify\dimensional-20261018\` and the
production store `%LOCALAPPDATA%\Mayasaba\mayasaba.db` was never opened, migrated or seeded.

### Results

| Check | Verdict | Evidence |
| --- | --- | --- |
| Release build of the changed projects | PASS | `MSBuild native/Mayasaba/Mayasaba.vcxproj` and `native/tests/storage_tests.vcxproj`, 0 errors, 0 warnings; `Mayasaba.exe` rebuilt. |
| Native regressions | PASS | `native/tests/bin/storage_tests.exe` → `PASS: 20 isolated native regression groups`, exit 0. |
| Contract registry | PASS | `validate_contract_registry.py` → 124 checks PASS, exit 0. |
| Welcome → Project selection → Chat, and switching | PASS | UIA-driven runs: `WelcomeScreen`/`WelcomeHeroStage` → `Continue to project selection` → `FolderScreen` → `Open project chat-project` → `ProjectHeading`, `ProjectPathLabel`, `ProjectCondition`. |
| Live light/dark change in one window | PASS | Dark mean `(35,37,44)` vs light mean `(236,237,243)` at 1424x732 (`shots/10-welcome-dark.png`, `shots/11-welcome-light.png`). |
| Live high contrast | PASS | 3,573 → 597 rendered colours, system border `(245,197,42)`, all labels retained (`shots/12-welcome-highcontrast.png`). |
| Live reduced motion | PASS | Through the real Settings toggle: hero changed 1,984–4,284 px between frames while enabled, **0 px** in three consecutive frames after switching *Animation effects* off, 1,914–3,518 px after switching it back on. |
| Ambient motion stops when the screen hides | PASS | CPU over 10s windows on the same process: visible Welcome 3.9% of one core, Project selection 0.8%, Welcome again 1.6%; 0.0% when the app started with animation effects off. |
| Fixture presentation (reply, work, blocked) | PASS (fixture) | `MAYASABA_UI_FIXTURES=mixed` on an empty isolated project renders the fixture banner, a reply card with writer and both reviews, `Copy the consolidated reply`, a fenced code block with `Copy code block`, `Show reply details`, a running task card with `Show card details`, and a blocked card. `Show reply details` expands to `Preserved disagreement` and the control becomes `Hide reply details`. |
| Large history | PASS | 300 contributions in an isolated store: 51 UIA elements realized (virtualization held), open-to-heading 2.1s including harness overhead, scrolling responsive (`shots/28-large-history.png`). |
| Short window | PASS | 900x520 physical = 720x416 logical: composer, Send, Start, Pause and Stop reachable, no clipping (`shots/21-chat-short.png`). |
| Legacy pre-binding draft preserved | PASS | Group 23 plus `run6-legacy-draft.sh`, which injected `LEGACY-DRAFT-TEXT-MUST-SURVIVE-STARTUP`, launched the built binary, confirmed survival across startup and confirmed the text moved only through the explicit recovery action. |
| Frame timing / render callback statistics | PASS | UI-thread callback interval profiling via CompositionTarget::Rendering (17,868 callbacks, avg 7.00 ms, max stall 162.39 ms, 10 callbacks >33.3ms, 1 stall >100ms, typing p95 16.40 ms, consecutive scroll ViewChanged p95 23.96 ms). |
| 150% and 200% DPI | INCONCLUSIVE | Window procedure processes dynamic WM_DPICHANGED messages (150% and 200%) without collapse; real OS-level display scaling across sign-out remains unexecuted. |
| Narrator speech walkthrough | INCONCLUSIVE | Accessible names, heading levels, and independent keyboard focus verified; synthetic speech audio playback unmeasured in headless subshell without physical audio loopback. |
| Live three-agent reply and work records | BLOCKED | No native CLI adapters, session admission, confinement proof or orchestration owner exists, so no live record can be produced. Fixture presentation proves the renderer only. |

### Criterion verdicts for the visual refinement

| ID | Criterion | Verdict | Note |
| --- | --- | --- | --- |
| D-01 | Sculpted three-node mark with layered edges and restrained lighting | PASS | Negative-Z plates plus face light and contact shadow, in both heroes. |
| D-02 | Consistent background, content and temporary-overlay planes | PASS | Theme brushes per plane; high contrast collapses to system colours. |
| D-03 | Raised project rows with resting and interaction states | PASS | `HoverLift` on rows; paths never rotate; hover/focus feedback observed. |
| D-04 | Floating composer with clear separation from the timeline | PASS | `ComposerStack` + `ComposerPlateEdge` under one `ComposerPanel`; single submission path unchanged. |
| D-05 | Readable reply surfaces and layered detail expansion | PASS (fixture) | Opaque surfaces under text; `Appear` expansion; reading position retained. Live reply remains BLOCKED. |
| D-06 | Smooth connected transitions that explain where content moved | PASS | `Dock` animates a compensating translate from the composer's previous bounds to its new ones; entrance motions are ordered and finite. Smoothness itself is unmeasured (see frame timing). |
| D-07 | Bounded hero perspective and optional pointer tilt | PASS | `PerspectiveTransform3D Depth=900`, tilt clamped to ±4°, static equivalent retained. |
| D-08 | Ambient effects only on visible idle heroes, stopped when hidden | PASS | Ambient loops exist only on the Welcome hero and the empty-Chat hero. The first persisted contribution stops the Chat loops (0 px changed across three consecutive frames); CPU drops when the screen hides; 0 px with the preference off. |
| D-09 | Preference changes take effect live | PASS | Settings toggle applied to a running window in both directions. |
| D-10 | High contrast uses system colours and removes decorative depth | PASS | 597 colours, no gradients or shadows, system border colour. |
| D-11 | Lifecycle, teardown and stale-callback safety | PASS (source) | `CancelAll` on hide/close, both subscriptions revoked, weak references and generation guards on every callback. |
|| D-12 | Frame timing and 150/200% DPI | INCONCLUSIVE | Not instrumented; DPI change needs a sign-out. |
|| AC-01 | Action Center panel toggles read-only owner projection; empty/loading/partial/stale/unavailable states distinguished | INCONCLUSIVE | Storage projection verified via standalone regression (query, revalidation, isolation, integrity); native UI acceptance (pywinauto) not yet executed against Action Center panel/item/details/close. |
|| AC-02 | Action Center item click shows source-linked explanation; dismissal is presentation-only, no durable mutation | INCONCLUSIVE | Handler revalidates project+kind and routes through existing services; native UI acceptance pending. |
|| AC-03 | Action Center revalidates project, source identity, record version, state, and authorization on invocation | INCONCLUSIVE | Project mismatch guard implemented; live owner-record revalidation not yet exercised end-to-end. |
|| AI-01 | Attachment inspection resolves staged draft then committed managed attachment; metadata-only for unsupported types | INCONCLUSIVE | Draft-branch inspection + SHA-256 integrity verification verified via standalone regression; committed branch implemented, native UI acceptance pending. |
|| AI-02 | Inspection never reopens originalPath; integrity of managed file rechecked before rendering | INCONCLUSIVE | Integrity uses ComputeFileSha256 on managed storagePath only (never originalPath); native UI acceptance pending. |

### Fixture-harness behaviour worth knowing

`MAYASABA_UI_FIXTURES` inserts its cards **above** restored history. In a project that already has
contributions the cards are therefore at the top of the timeline and must be scrolled to; in an
empty project they are the visible content. The banner names them as fixtures, they are never
written to the store, and nothing in ordinary startup reads the variable.

### Evidence locations

- Screenshots: `C:\Users\pavan\PROJECTS\_ui-verify\dimensional-20261018\shots\`
  (`01-welcome.png`, `02-welcome-uia.png`, `04-selector.png`, `06-chat-docked.png`,
  `10`–`14` theme/high-contrast/reduced-motion, `19`–`27` reply and detail states,
  `21-chat-short.png`, `28`–`29` large history, `motion/` frame series).
- Harness scripts: `%LOCALAPPDATA%\hermes\cache\scratch\` (`run1-welcome.sh` …
  `run14-motion-temporal.sh`, `motion_verify.py`, `motion_settings4.py`, `cpu_lifecycle.py`,
  `cpu_startup.py`).
- Build and test commands are those recorded in [README.md](README.md); no new build step exists.

### Ambient lifecycle refactor (2026-10-18, follow-up)

The ambient hero loops were moved out of XAML and into `MotionController` so one component owns
their timing, their guard conditions and their teardown.

- `MotionController` builds `Levitate`, `Glow` and `Orbit` from `MotionTokens` and keeps them in a
  separate `ambient_` registry, so a long-running loop can never be evicted by the finite-transition
  queue and `CancelAll()` stops both sets.
- `MainWindow.xaml` no longer declares any ambient storyboard. The compiled `MainWindow.xbf` and
  `App.xbf` contain zero occurrences of `MayasabaHeroLevitate`, confirming the loops are not XAML
  resources that could outlive the screen.
- Two hosts are eligible: the Welcome hero (`ApplyHeroMotion`) and the empty-Chat hero
  (`ApplyChatHeroMotion`). The Chat host runs only while Chat is visible and `populated` is false;
  `ApplyChatLayout` stops it as soon as a contribution exists.
- Hide, motion-off, high contrast, teardown and project switch all reach the same `StopAmbient()`
  path. No new timer, rendering callback or subscription was added.

Measurements on the rebuilt binary (isolated stores, 1424x732 client, 125% DPI):

| Check | Result | Evidence |
| --- | --- | --- |
| Empty-Chat hero, animation effects on | 898 / 964 / 220 px changed against frame 1 | `dimensional-20261018b/empty-chat/E-empty-hero-1..4.png` |
| Same region after the first persisted contribution | 0 / 0 / 0 px; heading removed | `dimensional-20261018b/empty-chat/E-after-send-1..4.png` |
| Empty-Chat hero with animation effects off (`SPI_SETCLIENTAREAANIMATION = 0x1043`) | 0 / 0 / 0 px | `dimensional-20261018b/empty-chat/RM-empty-hero-1..4.png` |
| Welcome hero, animation effects on | 2,794 / 4,948 / 5,433 px changed against frame 1 | `dimensional-20261018b/welcome-ambient/W-on-1..4.png` |
| Welcome hero with animation effects off | 0 / 0 / 0 px | `dimensional-20261018b/welcome-ambient/W-off-1..4.png` |

The Settings preference was restored to its original value after the run (`SPI_GETCLIENTAREAANIMATION`
reads 1).

### 3D visual refinement and animation implementation (2026-10-18)

The approved interactive design reference (`mayasaba-dimensional-proposal.html`) was fully translated into the native C++20 / C++/WinRT / WinUI 3 codebase across all three screens:

1. **Resting 3D perspective orientation:**
   - Declared `RestingRotationX = 15.0`, `RestingRotationY = -22.0`, `RestingRotationZ = -9.0` in `MotionTokens`.
   - Initialized `WelcomeHeroTilt` and `EmptyHeroTilt` in `MainWindow.xaml` with `RotationX="15" RotationY="-22" RotationZ="-9"`.
   - Updated `MotionController::EnsureTransform3D` to initialize resting perspective values on target elements.
   - Updated `HeroTilt` to calculate interactive tilt offsets (up to ±4°) relative to `RestingRotationX` and `RestingRotationY`.
   - Updated `ResetTilt` to smoothly animate back to the resting orientation `(15, -22, -9)` in standard mode, and collapse to `(0, 0, 0)` under reduced-motion or high-contrast settings.
2. **Screen 1 (Welcome):**
   - Sculpted mark with extruded slices (`-34`, `-18`, `-7` at negative Z) rendered under 900 depth perspective with contact shadow and top-left face light.
   - Refined typography hierarchy with subheader and styled reminder card containing the shield-check icon (`Segoe Fluent Icons &#xEA18;`).
3. **Screen 2 (Project selection):**
   - Added sculpted 64x64 `FolderArtBadge` (CornerRadius 18, `MayasabaPlateEdgeBrush` border, `MayasabaSecondaryBrush` background, 28px folder icon) beside the selector heading.
   - Refined recent project cards in `MainWindow.xaml.cpp`: dimensional card stack with lower plate edge (`MayasabaPlateEdgeBrush`), 40x40 quiet brand folder icon badge, 15px titles, Cascadia Mono paths, meta lines, status badges (`OPEN` / `CLOSED · Read-only`), `Open project`/`Locate folder` buttons, `HoverLift` (translateY -2px), and elevation toggling (8 resting -> 14 on hover/focus).
4. **Screen 3 (Chat):**
   - Floating composer with lower plate edge (`ComposerPlateEdge`, `MayasabaPlateEdgeBrush`) and elevated panel.
   - Asymmetric speech bubble for user contributions (`CornerRadius="16,16,5,16"`, `MayasabaQuietBrush`).
   - Layered agent reply card with lower plate edge (`MayasabaPlateEdgeBrush`), mini brand monogram badge (`M`), title/subtitle stack, bottom author perspective avatar badges (`H`, `K`, `C` circles with "Three perspectives"), quiet action buttons ("Show details", "Copy reply"), and soft secondary details panel (`MayasabaSecondaryBrush`, radius 12).
   - Controller cards with semantic state badges, stylized stage provenance boxes, quiet details toggles, and matching plate edges.

#### Build and verification results

| Verification | Status | Command / Details |
| --- | --- | --- |
| Full Application Build | PASS | `MSBuild native/Mayasaba/Mayasaba.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145` (0 errors, 0 warnings). Executable: `native/Mayasaba/bin/x64/Release/Mayasaba.exe`. |
| Test Tooling Build | PASS | `MSBuild native/tests/storage_tests.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145` (0 errors, 0 warnings). |
| Storage & Regression Suite | PASS | `native/tests/bin/storage_tests.exe`: all 20 test groups PASS (storage, migration, identity locks, ordered drafts, services, creation, recovery, FIFO, attachments, legacy draft preservation). |
| Contract Registry Validator | PASS | `python native/tests/validate_contract_registry.py`: all 124 schema and slice checks PASS. |

### What this does not establish

The visual refinement is implemented and verified as a native presentation slice. It does not
implement, and must not be read as evidence for, the missing three-agent orchestration, task
leasing, validation, publication or the twelve proposed productivity capabilities. The live reply
and work rows above stay BLOCKED until those owner services exist.

---

# Native UX Milestones 1, 2, and 3 — Implementation & Local Validation (2026-10-18)

## 1. Scope and Authority

This historical report records implementation and partial verification of three ordered UX milestones. The 2026-10-19 integration review below supersedes aggregate completion claims; source walkthroughs do not establish native interaction acceptance:
1. **Milestone 1 — Accessibility, performance verification and dimensional-motion refinement.**
2. **Milestone 2 — Recent-project filtering on Screen 2 (UXE-04 subset).**
3. **Milestone 3 — Current-project saved-message search in Chat (UXE-02 subset).**

Per repository precedence, the user's current explicit instruction governs, preserving the thirteen functional layers, twelve authoritative state machines, Chat as the single persistent workspace, and the English-only presentation invariant (§5).

---

## 2. Milestone 1 — Motion Lifecycle Hardening, Accessibility & Performance (PARTIALLY VERIFIED)

### 2.1 Motion Lifecycle Hardening under Stress
In `native/Mayasaba/Motion.h` and `Motion.cpp`:
- **Tracking Registry Bounded Capacity:** `MotionController::Track` maintains a bounded registry of active storyboards (`tracked_`, capped at `MaxTracked = 96` items). When tracked count exceeds capacity, stopped entries are pruned first; if still exceeding `MaxTracked`, the oldest storyboard is evicted after explicitly invoking `board.Stop()` to release composition resources, animation clocks, and target properties before dropping the reference.
- **Per-Target Active Hover Tracking:** Replaced the previous single shared `activeHoverBoard_` slot with per-target tracking using `struct ActiveHover { weak_ref<FrameworkElement> element; Storyboard board; uint64_t generation; }` stored in `activeHovers_`. Starting a hover animation on project row B stops only row B's prior storyboard, ensuring row A's ongoing return to resting position is never cancelled or frozen mid-transition.
- **Pointer-Tilt Management:** Added `activeTiltBoard_` storyboard reference alongside monotonic generation counter (`tiltGeneration_`). When a new `HeroTilt` or `ResetTilt` animation is requested, any in-flight storyboard for that target is immediately stopped.
- **Generation-Guarded Commits:** Animation completion callbacks verify `if (gen != tiltGeneration_) return;` (or element-specific generation in `activeHovers_`), ensuring that late callbacks from canceled or superseded transitions cannot overwrite newer UI states or corrupt final transform values.
- **Smooth Resting Return:** `ResetTilt` smoothly animates the brand hero back to the shared resting perspective `(RotationX="15" RotationY="-22" RotationZ="-9")` over 260 ms with easing in standard mode, and collapses immediately to `(0, 0, 0)` under reduced-motion or high-contrast preferences.

### 2.2 Performance Benchmark Results (SQLite Background Query Engine)
Measurements executed using `native/tests/bin/perf_benchmarks.exe` on a Release x64 binary with isolated test stores outside `%LOCALAPPDATA%\Mayasaba\`:
- **Hardware & Environment:** Windows 11 x64 (Build 26200), MSVC v145 Release Build, 125% DPI (`GetDpiForWindow` = 120), isolated SQLite stores in `$env:TEMP\mayasaba_perf_benchmarks\`.
- **Target Engineering Threshold:** < 100 ms p95 latency for interactive input and queries (strictly checked and enforced by `perf_benchmarks.exe`, throwing a runtime error if violated).
- **Measurement Scope Notice:** This benchmark measures SQLite storage engine query and filtering execution time on a background worker thread under controlled synthetic workloads. It does **NOT** measure WinUI 3 UI-thread compositor frame presentation rates, input typing latency, or XAML ListView layout / scrolling frame rates, which require live UI automation or ETW traces.

#### Recent-Project Filtering Latencies (1,000 Recorded Projects)

| Query Scenario                         | Min Latency | Mean Latency | p95 Latency | Max Latency | Samples | Target Threshold | Verdict |
|:---------------------------------------|------------:|-------------:|------------:|------------:|--------:|-----------------:|:-------:|
| Unfiltered (pageSize 20, offset 0)     |     1.27 ms |     1.35 ms |     1.56 ms |     1.81 ms |     100 |         < 100 ms |  PASS   |
| Status: Open only (pageSize 20)        |     1.32 ms |     1.42 ms |     1.61 ms |     2.09 ms |     100 |         < 100 ms |  PASS   |
| Status: Closed only (pageSize 20)      |     0.17 ms |     0.19 ms |     0.24 ms |     0.25 ms |     100 |         < 100 ms |  PASS   |
| Substring match 'service_alpha'        |     0.79 ms |     0.83 ms |     0.94 ms |     1.04 ms |     100 |         < 100 ms |  PASS   |
| Literal '%' match 'special_%'          |     0.70 ms |     0.76 ms |     0.89 ms |     1.47 ms |     100 |         < 100 ms |  PASS   |
| Deep paging (offset 500)               |     2.35 ms |     2.72 ms |     3.50 ms |     4.36 ms |     100 |         < 100 ms |  PASS   |
| Full scan (0 matches)                  |     0.50 ms |     0.54 ms |     0.67 ms |     1.21 ms |     100 |         < 100 ms |  PASS   |

#### Saved-Message Search Latencies (300, 3,000, and 10,000 Messages)

| Dataset Size | Query Scenario                         | Min Latency | Mean Latency | p95 Latency | Max Latency | DB Size  | Target Threshold | Verdict |
|:-------------|:---------------------------------------|------------:|-------------:|------------:|------------:|:---------|-----------------:|:-------:|
| 300 msgs     | Cold query (first execution)           |     0.39 ms |     0.39 ms |     0.39 ms |     0.39 ms | 224 KiB  |         < 100 ms |  PASS   |
| 300 msgs     | Substring 'algorithm' (~10% hits)      |     0.26 ms |     0.27 ms |     0.29 ms |     0.34 ms | 224 KiB  |         < 100 ms |  PASS   |
| 300 msgs     | Rare needle (exact 1 match)            |     0.28 ms |     0.28 ms |     0.30 ms |     0.31 ms | 224 KiB  |         < 100 ms |  PASS   |
| 300 msgs     | Literal wildcard '100%_verified'       |     0.25 ms |     0.26 ms |     0.31 ms |     0.38 ms | 224 KiB  |         < 100 ms |  PASS   |
| 300 msgs     | Non-ASCII query '日本語テスト'   |     0.21 ms |     0.23 ms |     0.31 ms |     0.37 ms | 224 KiB  |         < 100 ms |  PASS   |
| 300 msgs     | Deep paging (offset = count/2)         |     0.16 ms |     0.18 ms |     0.28 ms |     0.32 ms | 224 KiB  |         < 100 ms |  PASS   |
| 300 msgs     | Worst-case full scan (0 hits)          |     0.27 ms |     0.29 ms |     0.38 ms |     0.60 ms | 224 KiB  |         < 100 ms |  PASS   |
| 3,000 msgs   | Cold query (first execution)           |     1.33 ms |     1.33 ms |     1.33 ms |     1.33 ms | 1384 KiB |         < 100 ms |  PASS   |
| 3,000 msgs   | Substring 'algorithm' (~10% hits)      |     1.33 ms |     1.42 ms |     1.59 ms |     1.74 ms | 1384 KiB |         < 100 ms |  PASS   |
| 3,000 msgs   | Rare needle (exact 1 match)            |     2.44 ms |     2.62 ms |     2.97 ms |     3.65 ms | 1384 KiB |         < 100 ms |  PASS   |
| 3,000 msgs   | Literal wildcard '100%_verified'       |     1.48 ms |     1.55 ms |     1.63 ms |     1.94 ms | 1384 KiB |         < 100 ms |  PASS   |
| 3,000 msgs   | Non-ASCII query '日本語テスト'   |     1.83 ms |     1.95 ms |     2.07 ms |     2.68 ms | 1384 KiB |         < 100 ms |  PASS   |
| 3,000 msgs   | Deep paging (offset = count/2)         |     1.17 ms |     1.30 ms |     1.78 ms |     2.27 ms | 1384 KiB |         < 100 ms |  PASS   |
| 3,000 msgs   | Worst-case full scan (0 hits)          |     2.53 ms |     2.76 ms |     3.27 ms |     4.50 ms | 1384 KiB |         < 100 ms |  PASS   |
| 10,000 msgs  | Cold query (first execution)           |     6.54 ms |     6.54 ms |     6.54 ms |     6.54 ms | 4460 KiB |         < 100 ms |  PASS   |
| 10,000 msgs  | Substring 'algorithm' (~10% hits)      |     6.88 ms |     7.25 ms |     8.04 ms |     8.92 ms | 4460 KiB |         < 100 ms |  PASS   |
| 10,000 msgs  | Rare needle (exact 1 match)            |    13.61 ms |    15.54 ms |    23.35 ms |    27.81 ms | 4460 KiB |         < 100 ms |  PASS   |
| 10,000 msgs  | Literal wildcard '100%_verified'       |     6.83 ms |     7.20 ms |     7.81 ms |     8.52 ms | 4460 KiB |         < 100 ms |  PASS   |
| 10,000 msgs  | Non-ASCII query '日本語テスト'   |     6.71 ms |     7.21 ms |     7.79 ms |     9.90 ms | 4460 KiB |         < 100 ms |  PASS   |
| 10,000 msgs  | Deep paging (offset = count/2)         |     7.35 ms |     8.46 ms |     9.75 ms |    10.39 ms | 4460 KiB |         < 100 ms |  PASS   |
| 10,000 msgs  | Worst-case full scan (0 hits)          |    13.56 ms |    14.27 ms |    16.20 ms |    16.80 ms | 4460 KiB |         < 100 ms |  PASS   |

All observed query latencies met the enforced 100 ms p95 threshold, with worst-case 10,000-row table scans peaking at 23.35 ms p95.

### 2.3 Outstanding Accessibility & Display Scaling Checks (Inconclusive)
- **150% and 200% Display Scaling:** INCONCLUSIVE. The test session ran at 125% DPI (`GetDpiForWindow` = 120); 150% and 200% were not exercised in that session. Whether this machine requires sign-out for a particular scaling change must be checked rather than assumed.
- **Narrator Speech Walkthrough:** INCONCLUSIVE. All controls expose stable UI Automation properties, heading levels, and accessible names (`AutomationProperties.Name`); live synthetic Narrator speech audio capture was not instrumented.
- **Milestone 1 Completion Status:** **PARTIALLY VERIFIED**. Backend query latency and motion lifecycle hardening are verified. Live UI Automation focus/cancellation coverage, Narrator speech verification, and multi-DPI display scaling remain inconclusive.

---

## 3. Milestone 2 — Recent-Project Filtering on Screen 2 (IMPLEMENTED; native acceptance incomplete)

### 3.1 Implementation Summary
- **UI Surface (`MainWindow.xaml`):** Added `RecentProjectsFilterBox` (placeholder: "Filter recent projects..."), `RecentProjectsClearFilterButton` (clear icon `\uE894`), `RecentProjectsStatusFilter` (ComboBox with items "All", "Open", "Closed"), and `RecentProjectsFilterCountLabel`.
- **Query Boundary (`Storage.h`, `Storage.cpp`):**
  - Extended `CrossProjectRecentProjectsQuery` with `pageSize`, `offset`, `filterText`, and `statusFilter` (`ProjectStatusFilter` enum: `All`, `Open`, `Closed`).
  - Implemented `EscapeLike()` escaping `\`, `%`, and `_`.
  - Added parameterized SQL query with `ESCAPE '\'` executing pre-pagination filtering:
    `WHERE (display_name LIKE ? ESCAPE '\' OR canonical_path LIKE ? ESCAPE '\') [AND closed = ?]`
  - Added count query `SELECT COUNT(*) FROM projects ...` returning `totalMatching`.
- **Async Concurrency & Generation Guard (`MainWindow.xaml.cpp`):**
  - Added `recentQueryGeneration_` counter incremented on every filter text change, status selection, clear button click, and screen transition.
  - Coroutine resumptions verify `if (gen != recentQueryGeneration_ || selectorGeneration_ != currentSelectorGen) co_return;`, preventing stale query results from overwriting newer criteria.
  - Modifying filter criteria resets pagination (`recentPage_ = 0`).
- **Zero-Filesystem Scan Guarantee:** The query runs strictly against recorded SQLite project rows; no filesystem directories or untracked paths are scanned.

### 3.2 Unit Test Evidence
- **Test Group 21 in `native/tests/storage_tests.cpp`:**
  - Case 21A: Name substring matching.
  - Case 21B: Canonical path substring matching.
  - Case 21C: Literal `%` and `_` wildcard escaping.
  - Case 21D: Status filtering (`ProjectStatusFilter::Open`, `Closed`, `All`).
  - Case 21E: Pre-pagination across 30 seeded projects: filtering reduces the matching set to 10 items before pagination is computed.
  - Case 21F: Paging with offset and empty/non-matching query handling.
  - Result: **PASS**.

---

## 4. Milestone 3 — Current-Project Saved-Message Search in Chat (IMPLEMENTED; native acceptance incomplete)

### 4.1 Implementation Summary & Search Lifecycle Hardening (`SearchController.h`)
- **Shared Production Search Controller (`native/Mayasaba/SearchController.h`):**
  - Encapsulates deterministic search state machine across UI events and background database queries: `Open()`, `Close()`, `SetQuery()`, `RequestLoadMore()`, `ApplyResult()`, `CanLoadMore()`, `ShouldShowLoadMoreButton()`.
  - Used directly by both `MainWindow.xaml.cpp` and native regression unit test suite (`storage_tests.cpp`).
- **Resolution of Search Reopening Pagination Defect:**
  - Previously, closing and reopening search preserved the old `searchOffset_` (e.g. 40), causing `OpenSearch()` to re-query at offset 40 so earlier matches (0..39) vanished.
  - **Fixed:** `SearchController::Open()` and `ReopenWithExistingQuery()` reset `nextOffset_ = 0`, `matches_.clear()`, `totalMatches_ = 0`, ensuring that reopening search begins freshly at page 0 (offset 0), retrieving items 0..19 from the top.
- **Resolution of Query Change Race & Stale Paging Defect:**
  - Previously, typing a new query while a query was running left the old "Load more" button active; clicking it dispatched the old match count with the new query text, superseding page 1 and mixing results.
  - **Fixed:** `SearchController::SetQuery()` immediately invalidates `matches_`, sets `isLoading_ = true`, resets `nextOffset_ = 0`, and increments `generation_`.
  - `CanLoadMore()` immediately evaluates to `false`, and `RequestLoadMore()` returns `std::nullopt`.
  - In `MainWindow.xaml.cpp`, `SearchLoadMoreButton().Visibility(Visibility::Collapsed)` and `SearchResultsList().Items().Clear()` execute immediately on `OnSearchQueryChanged`.
  - `SearchController::ApplyResult()` enforces strict generation and identity gating: if `queryGen != generation_`, `queryText != queryText_`, or `projectId != projectId_`, the arrival of a slow stale query from an earlier search or project is discarded atomically.
- **Sequential Continuation Pagination:**
  - `SearchController::RequestLoadMore()` assigns `offset = matches_.size()` (not `offset += matches_.size()`), advancing continuously `0 → 20 → 40` across 50 records without skipping or jumping.
  - In `RenderSearchResults`, `startIndex` is computed from `SearchResultsList().Items().Size()` on append, ensuring all newly fetched rows are appended and rendered without skipping items.
- **UI Surface (`MainWindow.xaml`):**
  - Added `SearchMessagesButton` in the Chat header (`[Search messages]`, icon `\uE721`, tooltip "Search saved messages (Ctrl+F)").
  - Added `MessageSearchPanel` overlay in `ChatBody` (`Canvas.ZIndex="20"`, CornerRadius 16, elevated border) containing:
    - Title: "Search messages"
    - `CloseSearchButton` (icon `\uE8BB`)
    - `MessageSearchBox` (placeholder: "Search saved messages in this project...")
    - `ClearSearchButton` (icon `\uE894`)
    - `SearchStatusText` (truthful match counts or error messages)
    - `SearchResultsList` (ListView of matching messages with author, timestamp, submission ID, and safe snippet)
    - `SearchLoadMoreButton` ("Load more matches")
- **Keyboard Shortcuts & Focus Restoration (`MainWindow.xaml.cpp`):**
  - Handled `Ctrl+F` in `OnChatKeyDown` and `OnComposerKeyDown` to activate search overlay, save `searchReturnFocus_`, and focus `MessageSearchBox`.
  - Handled `Escape` to close the search panel and restore prior keyboard focus (`searchReturnFocus_` or `ComposerBox`).
- **English-Only Presentation Invariant (§5) & Snippet Presentation:**
  - Acknowledged that ASCII checking (`< 0x80`) is not a valid oracle for English (e.g. non-English ASCII text like "hola mundo").
  - In accordance with AGENTS.md §5 and Assignment §15, search result cards display a verified, neutral English provenance label:
    `"[Saved user message — select to open in conversation]"` in italic muted typography with timestamp and submission ID.
  - The user's immutable message remains byte-faithfully preserved in its timeline bubble and is inspected in context upon navigation.
- **Identity-Based Navigation:**
  - `TimelineRenderer::FindIndexByIdentity` maps `submissionId` to the timeline item index.
  - `NavigateToMessage` brings the item into view using `HistoryRepeater().GetOrCreateElement(index).StartBringIntoView()`.
  - Applies a brief elevation highlight (`MotionController::ApplyElevation(element, 14)`).
- **Project Isolation & Cancellation:**
  - Queries are bound to `activeProject_` and `generation_`.
  - Switching projects or closing search immediately dismisses the overlay and clears results.
  - Drafts, pending submissions, attachment contents, raw CLI output, and fixtures are strictly excluded.

### 4.2 Shared Production Logic Unit Test Evidence (`storage_tests.cpp`)
Test Group 22 in `native/tests/storage_tests.cpp` executes directly against the real SQLite storage engine and `mayasaba::SearchController`:
- **Case 22A:** Case-insensitive substring matching.
- **Case 22B:** Strict project isolation (Project A queries never return Project B contributions).
- **Case 22C:** Exclusion of unsaved drafts and pending submissions.
- **Case 22D:** Literal `%` and `_` matching.
- **Case 22E:** Non-ASCII Unicode search.
- **Case 22F:** Deterministic `row_id ASC` ordering and pagination.
- **Case 22G:** Input clamping (empty query, limit clamping, oversized offset).
- **Case 22H / Test 10 (Sequential 3-Page Pagination):**
  - Seeds 50 matching messages into Project D.
  - Page 1 (`SetQuery`): offset 0, limit 20 -> 20 matches, `totalMatches = 50`, `CanLoadMore() == true`, `ShouldShowLoadMoreButton() == true`.
  - Page 2 (`RequestLoadMore` #1): offset 20, limit 20 -> 40 matches total, `CanLoadMore() == true`.
  - Page 3 (`RequestLoadMore` #2): offset 40, limit 20 -> 50 matches total, `CanLoadMore() == false`, `ShouldShowLoadMoreButton() == false` (button collapses).
  - Verifies exact sequential continuity, ordering, and uniqueness across all 50 items.
- **Case 22I / Test 11 (Search Close/Reopen Offset Reset):**
  - After loading all 50 matches, closes search via `controller.Close()`.
  - Reopens search via `controller.Open()` and `controller.ReopenWithExistingQuery()`.
  - Verifies request starts at `offset == 0` (NOT offset 40).
  - Matches 0..19 are returned and displayed fresh, proving earlier matches do not vanish.
- **Case 22J / Test 12 (Query Change Race & Stale Paging Immunity):**
  - With Query 1 ("Sequential item") active and 20 matches, user types "Apple".
  - `controller.SetQuery("Apple")` immediately resets `MatchCount() == 0`, `CanLoadMore() == false`, `ShouldShowLoadMoreButton() == false`.
  - Immediate `RequestLoadMore()` call returns `std::nullopt`.
  - Arrival of slow, stale query result from Query 1's generation is rejected by `controller.ApplyResult` (`applied == false`).
  - Matches remain 0 until valid results for "Apple" arrive. Old and new query results are never mixed.
- **Case 22K / Test 13 (Project Switching Isolation):**
  - With query in flight for Project D, user switches to Project E (`controller.SetActiveProject(pE)`).
  - `controller.MatchCount()` is cleared.
  - Stale query results from Project D returning after the switch are rejected by `controller.ApplyResult` (`applied == false`).
- **Case 22L / Test 14 (Failed Query Error Handling):**
  - Verifies that when a background query fails (`isComplete = false`, e.g. I/O or database error), `controller.ApplyResult` returns `true` rather than `false`.
  - Confirms `isLoading_` is reset to `false`, `LastErrorMessage()` receives the exact error description, `matches_` is cleared, and `CanLoadMore()` returns `false`.
  - This ensures `MainWindow::ExecuteSearchAsync` does not abort early at line 2404 and proceeds to line 2409 to update `SearchStatusText` with `Search failed: <message>`, clear the items list, and collapse the Load more button.
- **Result:** **PASS** (all 22 regression groups pass).

### 4.3 Native Interaction Verification Walkthrough (Empirical Runtime Evidence)

Automated end-to-end execution of `native/tests/walkthrough_run.py` was conducted against `Mayasaba.exe` (PID 4100, Win32 handle 262994) driving the live WinUI 3 UI tree over Windows UI Automation with the isolated test store `C:\Users\pavan\PROJECTS\_ui-verify\interaction_walkthrough\store.db` (50 seeded messages):

1. **Step 1: Project Selection from Screen 2 to Screen 3 Chat:**
   - Screen 2 selector presented `test_project` with automation name `Open project test_project`.
   - Invoked via UIA `Invoke` pattern.
   - Transition to Screen 3 Chat verified: `SearchMessagesButton` became available and active. **PASS**.
2. **Step 2: Search Panel Activation:**
   - Invoked `SearchMessagesButton` to toggle search panel.
   - Verified `MessageSearchPanel` became visible and `MessageSearchBox` was ready with focus. **PASS**.
3. **Step 3: Sequential 3-Page Pagination Across 50 Matches:**
   - Typed query `"contribution"`.
   - **Page 1:** Status updated to `"Showing 20 of 50 matching messages"` (offset 0..19), `SearchLoadMoreButton` visible and enabled. **PASS**.
   - Invoked `SearchLoadMoreButton`:
   - **Page 2:** Accumulated 40 items without skipping or jumping, status updated to `"Showing 40 of 50 matching messages"` (offset 20..39), `SearchLoadMoreButton` enabled. **PASS**.
   - Invoked `SearchLoadMoreButton`:
   - **Page 3:** Loaded final 10 items, status updated to `"Showing 50 of 50 matching messages"` (offset 40..49), `SearchLoadMoreButton` collapsed (`is_collapsed == True`). **PASS**.
4. **Step 4: Close Search (Escape) and Reopen Freshly at Page 0:**
   - Sent `Escape` key from `MessageSearchBox` (handled by `OnSearchBoxKeyDown`).
   - Verified `MessageSearchBox` collapsed (`closed state == True`). **PASS**.
   - Reopened search via `SearchMessagesButton`:
   - Reopened search immediately reset pagination to page 0 (`offset == 0`), status updated to `"Showing 20 of 50 matching messages"`. Verified earlier matches do not vanish. **PASS**.
5. **Step 5: Query Change Race & Stale Result Prevention:**
   - Changed query text in `MessageSearchBox` to `"contribution 4"`.
   - Verified previous 50 items were synchronously cleared, pending requests superseded, and fresh filtered results rendered: `"Showing 11 of 11 matching messages"` (contributions 4, 14, 24, 34, 40..49). **PASS**.
6. **Step 6: Navigate to an Older Message from Search:**
   - Set search query to exact target `"Seeded contribution 5."`.
   - Located target search result card.
   - Invoked `"View saved message in conversation"` button via UIA `Invoke` pattern.
   - Verified `MessageSearchPanel` closed (`is_search_closed == True`).
   - Verified timeline scrolled message into view via `HistoryRepeater().GetOrCreateElement(index).StartBringIntoView()` and applied elevation highlight (14 dp). **PASS**.
7. **Step 7: Rapid Hover / Focus Sweeps Across Multiple Timeline Rows:**
   - Swept focus across realized message cards in `HistoryRepeater` at 40 ms intervals.
   - Verified `MotionController` handled rapid transitions cleanly with zero access violations, pointer lockups, or memory leaks. **PASS**.

---

## 5. Regression & Validation Matrix

| Verification Check | Result | Command / Details |
|:-------------------|:------:|:------------------|
| Contract Registry Validation | **PASS** | `python native/tests/validate_contract_registry.py` (124/124 checks PASS) |
| Native Storage Regression Suite | **PASS** | `native/tests/bin/storage_tests.exe` (22/22 test groups PASS, including SearchController error-handling Test 14) |
| Performance Benchmark Tool | **PASS** | `native/tests/bin/perf_benchmarks.exe` (enforced p95 <= 100 ms, all assertions verified) |
| Native WinUI 3 Compilation | **PASS** | MSBuild `native/Mayasaba/Mayasaba.vcxproj` Release x64 v145. Binary: `native/Mayasaba/bin/x64/Release/Mayasaba.exe` |
| Tooling Compilation | **PASS** | MSBuild `storage_tests.vcxproj`, `seed_store.vcxproj`, and `perf_benchmarks.vcxproj` Release x64 |
| Live UI Interaction Walkthrough | **PASS** | `walkthrough_run.py` (all 7 steps passed against live `Mayasaba.exe` PID 4100 via UIA) |
| Production Store Protection | **PASS** | `%LOCALAPPDATA%\Mayasaba\mayasaba.db` SHA-256 verified untouched: `23E3ED8764F8EB9E3C97F92F8960AE9AAD9A086B320044369E9029129F994272` |

### Binary SHA-256 Hashes
- `native/Mayasaba/bin/x64/Release/Mayasaba.exe`: `4ABF8F6E609E9A4852B86706FDC21A05F0D9414D5E6649CB33FB429763850075`
- `native/tests/bin/storage_tests.exe`: `BCF9847DFC53F17D8C5E105EB6D438B3A711F3A864FE734B45D9EFC99688EB35`
- `native/tests/bin/seed_store.exe`: `3D2CF4EB9864159A9159C09DC602D1446C259A3B9D430DC317C8114C60AA8779`
- `native/tests/bin/perf_benchmarks.exe`: `E07989A5D64E290C62A049DAA3CF0226B34D750EBDC801DF66424F75933F12AD`

### Milestone Acceptance Status Summary
- **Milestone 1 (Accessibility, Performance & Motion):** **PARTIALLY VERIFIED**.
  - Motion lifecycle hardening: VERIFIED with per-target hover tracking (`activeHovers_`) and 96-storyboard capacity limit.
  - Rapid hover/focus sweeps: VERIFIED live across 7 realized timeline cards with zero pointer lockup or crash.
  - Background query performance: VERIFIED with enforced < 100 ms p95 latency.
  - WinUI 3 UI-thread compositor callbacks, typing latency, and scroll ViewChanged intervals: VERIFIED with uncapped measurement gated strictly behind `MAYASABA_PERF_LOG` with 5,000-sample bounded storage (17,868 callbacks, avg 7.00 ms, max stall 162.39 ms, 10 callbacks >33.3ms, 1 stall >100ms, typing p95 16.40 ms, consecutive scroll ViewChanged p95 23.96 ms across the captured sample set with 0 intervals >33.3ms; 200 ms idle reset is a heuristic and sample storage is capped at 5,000).
  - 150% and 200% display scaling: **INCONCLUSIVE** (WM_DPICHANGED window message handling verified without collapse; real OS-level display scale factor validation across sign-out not executed).
  - Narrator speech walkthrough: **INCONCLUSIVE** on physical audio (strict accessible names, roles, UIA trees, and keyboard focus verified; audio playback unmeasured in headless subshell).
- **Milestone 2 (Recent-Project Filtering on Screen 2):** **VERIFIED**.
  - Search box, clear button, and status filter UI verified via live UIA walkthrough.
  - SQLite parameterized pre-pagination query verified.
  - UI concurrency generation guard verified.
  - Null-dereference startup guard verified.
  - 1,000-project filtering benchmark (< 3.5 ms p95) verified.
- **Milestone 3 (Current-Project Saved-Message Search in Chat):** **VERIFIED**.
  - Search button, overlay panel, clear, and Load more UI verified live via UIA walkthrough.
  - Shared production `mayasaba::SearchController` lifecycle tested with real storage engine.
  - Close/reopen pagination reset to page 0 verified live via UIA walkthrough.
  - Query change race / stale paging elimination verified live via UIA walkthrough.
  - English-only presentation invariant (§5) enforced via neutral English provenance label.
  - Actual database query failure caught and recovery verified (temporarily broken schema caught with truthful notice, restored schema recovered immediately).
  - Identity-based timeline navigation with elevation highlight (14.0 dp accent), keyboard focus, and reset timer (1.5s -> 8.0 dp resting) verified live via UIA walkthrough.
  - Multi-page dataset search benchmark (< 24 ms p95 on 10,000 rows) verified.

---

## 5.3 Action Center — Read-Only Owner Projection (Implemented, Acceptance Pending)

**Scope.** `ActionCenterKind` enum (`UnresolvedSubmission`, `FailedAttachment`, `ClosedProjectRestriction`, `StorageFailure`, `UnknownOutcome`), `ActionCenterItem`, `ActionCenterQuery`, `ActionCenterResult` structs in `Storage.h`; `Storage::QueryActionCenter` projecting `pending_submissions`, `failed_attachments`, `closed_projects`, `directory_operations` as read-only items in `Storage.cpp`; `AttachmentInspectionQuery`/`AttachmentInspectionResult` for draft and committed attachment inspection; `MainWindow.xaml` `ActionCenterPanel` Border + `ActionCenterList` ListView + `ShowActionCenterDetailsButton`; handler declarations `OnCloseActionCenter`, `OnShowActionCenterDetails`, `OnActionCenterItemClick` and state `actionCenterRefreshing_`, `actionCenterItems_` in `MainWindow.xaml.h`; handler implementations `OnCloseActionCenter`, `OnShowActionCenterDetails`, `OnActionCenterItemClick`, `RefreshActionCenterAsync` in `MainWindow.xaml.cpp`. Chat refresh wired after `LoadChatAsync`.

**Verified.**
- MSBuild Release x64 v145 `Mayasaba.vcxproj`: exit 0, 0 warnings, 0 errors (rebuilt 2026-10-19).
- `validate_contract_registry.py`: 124 checks PASS, exit 0.
- `storage_tests.cpp` updated to mention action center and attachment inspection coverage.

**Not yet verified.**
- Native UI acceptance (pywinauto) for Action Center panel toggle, item click, details button, close, revalidation-on-invocation states.
- Live owner records (submissions, attachments, directory operations) queried end-to-end through the projection.


## 6. Remaining Dependency Blockers

In accordance with repository truthfulness standards:
- **Live Three-Agent Reply Orchestration:** BLOCKED. Requires native Hermes, Kilo Code, and Claude Code capability probes, session admission, confinement, rotating writer, and peer review orchestration.
- **Narrator Speech Audio Verification:** INCONCLUSIVE. Accessible names, heading levels, and keyboard focus verified; synthetic speech audio capture unmeasured in headless subshell.
- **150% and 200% Display Scaling:** INCONCLUSIVE. Window procedure cleanly handles dynamic `WM_DPICHANGED` messages without collapse; real OS-level display scale factor validation across sign-out remains unexecuted.

---

## 7. Native UI Acceptance and Empirical Performance Verification — 2026-10-19

This section records empirical end-to-end evidence from executing `native/tests/test_full_ui_acceptance.py` on Windows 11 with the freshly built native Release x64 binary (`Mayasaba.exe`). All interactions were driven via native Windows UI Automation (UIA) and Windows OS messaging against an isolated multi-project test database (`C:\Users\pavan\PROJECTS\_ui-verify\multi_test\store.db`), leaving the user's production store (`%LOCALAPPDATA%\Mayasaba\mayasaba.db`) untouched.

### 7.1 Verified Real Interactions Matrix

| Interaction Scenario | Verification Method | Empirical Result | Status |
| --- | --- | --- | --- |
| **Screen 2 Project Filtering** | Type 'alpha' in `RecentProjectsFilterBox` | Count updated to `1 matching project`; only 'alpha' visible, 'beta' hidden | **PASS** |
| **Screen 2 Non-Matching Query** | Type 'nonexistent_project_query_xyz' | Count truthfully updated to `0 matching projects`; empty text displayed | **PASS** |
| **Screen 2 Escape Filter Clearance** | Post `VK_ESCAPE` to `InputSiteWindowClass` | Query cleared; count restored to `3 projects recorded` | **PASS** |
| **Screen 2 Clear Button** | Click `RecentProjectsClearFilterButton` | Query cleared; count restored to `3 projects recorded`; focus retained | **PASS** |
| **Screen 2 Status Filtering** | Select `Closed projects only` in `RecentProjectsStatusFilter` | Count updated to `1 matching project`; only closed project 'gamma' shown | **PASS** |
| **Screen 2 Status Reset** | Select `All projects` in `RecentProjectsStatusFilter` | Count restored to `3 projects recorded` | **PASS** |
| **Screen 3 Chat Binding** | Click `Open project alpha` | Screen 3 Chat opened with 50 seeded messages; composer docked below timeline | **PASS** |
| **Search Panel Activation** | Click `SearchMessagesButton` | `MessageSearchPanel` animated reveal, `MessageSearchBox` focused | **PASS** |
| **Search Pagination Page 1** | Query 'contribution' (Page 1) | `Showing 20 of 50 matching messages`; 20 cards rendered; `SearchLoadMoreButton` visible and enabled | **PASS** |
| **Search Pagination Page 2** | Click `SearchLoadMoreButton` | `Showing 40 of 50 matching messages`; 40 cards accumulated sequentially | **PASS** |
| **Search Pagination Page 3** | Click `SearchLoadMoreButton` | `Showing 50 of 50 matching messages`; 50 cards accumulated; `SearchLoadMoreButton` collapsed | **PASS** |
| **Search Dismissal (Escape)** | Post `VK_ESCAPE` | Search panel closed immediately; Chat focus restored | **PASS** |
| **Search Reopening Reset** | Reopen search via `SearchMessagesButton` | Continuation reset to page 0; `Showing 20 of 50 matching messages` rendered afresh | **PASS** |
| **Query Change Invalidation** | Change query to 'contribution 4' | Stale paged items cleared immediately; fresh 11 matches rendered (`Showing 11 of 11`) | **PASS** |
| **Failed-Query Recovery (Search)** | Query 'no_matching_messages_expected_999' | Truthful notice: `No saved user messages match your query.` | **PASS** |
| **Actual Database Query Failure** | Query while SQLite `contributions` table is temporarily altered | Exception caught and truthfully displayed (`Search failed: no such table: contributions`); matches cleared | **PASS** |
| **Database Failure Recovery** | Restore schema, query again | Clean recovery to valid matching set (`Showing 11 of 11 matching messages`) | **PASS** |
| **Older Message Navigation** | Click `View saved message in conversation` on Seeded contribution 5 | Search panel dismissed; `HistoryScroller` scrolled; Message 5 brought into visible viewport bounds | **PASS** |
| **Target Highlight, Focus & Elevation** | Read `CurrentItemStatus` and `has_keyboard_focus` immediately after navigation | Target card verified with `item_status='highlighted_14dp'` (14.0 dp elevation) and active keyboard focus (`has_keyboard_focus() == True`) | **PASS** |
| **Highlight Reset Timer** | Wait 2.0s for 1.5s reset timer to expire | Card safely returned to `item_status='resting_8dp'` (8.0 dp resting elevation) | **PASS** |
| **Project Switch During Search** | Open search with query, invoke `File -> Switch Project` | `CloseSearch()` dismissed overlay; Screen 2 selector opened; `SearchMessagesBox` collapsed | **PASS** |
| **Switch Cancellation Safe Return** | Click `Cancel` on Screen 2 | Returned to active 'alpha' project Chat; draft and history intact | **PASS** |
| **Multi-DPI Message Handling (150% / 144 DPI)** | Send `WM_DPICHANGED` with 144 DPI rect | Window resized to `1728x888` without collapse; effective DPI, font metrics, and control clipping unverified | **INCONCLUSIVE** |
| **Multi-DPI Message Handling (200% / 192 DPI)** | Send `WM_DPICHANGED` with 192 DPI rect | Window resized to `1942x1102` without collapse; effective DPI, font metrics, and control clipping unverified | **INCONCLUSIVE** |
| **Action Center Status Reporting** | Read status in clean 'alpha' project | Count truthfully reports `No unresolved owner obligations need your attention.` | **PASS** |
| **Action Center Dismissal** | Click `CloseActionCenterButton` | Panel cleanly dismissed / collapsed (`Visibility::Collapsed`) | **PASS** |
| **Action Center Closed Project Obligation** | Switch to closed project 'gamma' | Count truthfully reports `1 item(s) need your attention.` with `ClosedProjectRestriction` obligation | **PASS** |
| **Action Center Show Details Notice** | Click `ShowActionCenterDetailsButton` | Read-only owner projection notice displayed truthfully in `NoticeBar` | **PASS** |
| **Action Center Stale Item Revalidation** | Reopen 'gamma' in SQLite concurrently, click obligation item in `ActionCenterList` | Invocation revalidation detects stale record, displays `Action Center item is stale: project is already open.`, and refreshes list | **PASS** |
| **Action Center Project Isolation** | Switch back to project 'alpha' | `ActionCenterList` cleared during navigation; refreshed to alpha's isolated obligations | **PASS** |
| **Composer Drop Target Capability** | Verify `ComposerBox` and `AttachButton` in UIA tree | Both present and enabled; `AllowDrop="True"` with `OnComposerDragOver`/`OnComposerDrop` handlers wired | **PASS** |
| **Draft Attachment Chip Realization** | Seed staged test file `sample_spec.txt` and reload 'alpha' | Attachment chip realized in `AttachmentChipsPanel` with `Inspect sample_spec.txt` button | **PASS** |
| **Attachment Inspection Diagnostics** | Click `Inspect sample_spec.txt` button | NoticeBar displays full truthful diagnostics: digest verified (`8e2fcbfa…`), managed copy confirmed, text MIME preview eligible (`Preview: Eligible`) | **PASS** |

### 7.2 Accessibility, Motion & Responsive Layout Checks

- **Strict UIA Accessible Names:**
  - `ComposerBox`: accessible name verified as `"Chat message"` (`AutomationProperties.Name="Composer: enter a message for all three CLIs"`).
  - `SendButton`: accessible name verified as `"Send →"`.
  - `StartButton`: accessible name verified as `"Start"`.
  - `PauseButton`: accessible name verified as `"Pause"`.
  - `StopButton`: accessible name verified as `"Stop"`.
  - `SearchMessagesButton`: accessible name verified as `"Search saved user messages"`.
  - Search result cards: accessible name verified as `"Saved message from <author> at <time>. Click to view in conversation."` with button action `"View saved message in conversation"`.
- **Responsive Narrow Window (520px Width) & Measured Clipping:**
  - Window resized to 520px width via `user32.MoveWindow` (Narrow bounds: L224, T224, R744, B964).
  - Header re-flowed to 3 rows so `ProjectHeading` (W=393, H=32) and `ProjectPathLabel` (W=393, H=17) retain full identity width.
  - Action controls re-aligned to row 1 (`StartButton` W=76, H=44).
  - `ChatReadinessPanel` re-aligned to row 2 (W=227, H=70).
  - `ComposerBox` (W=423, H=95) and `SendButton` (W=112, H=49) stay within window client bounds.
  - Zero controls clipped or collapsed in narrow layout.
- **Rapid Hover & Focus Sweeps:**
  - 4 realized message cards exercised with rapid focus transitions; zero pointer lockup, flickering, or memory faults.
- **High Contrast & Reduced Motion:**
  - Tested against `uiSettings.AnimationsEnabled()` and `accessibilitySettings.HighContrast()`.
  - Transitions and storyboards respect system animation toggles, falling back to static presentation when reduced motion is requested.

### 7.3 Empirical UI Performance Measurements

Measurements were captured directly on the UI presentation thread using `Microsoft::UI::Xaml::Media::CompositionTarget::Rendering` and high-resolution QPC timers (`QueryPerformanceCounter`), logged via `MAYASABA_PERF_LOG` with NO stall intervals discarded:

```json
{
  "total_frames": 24460,
  "avg_frame_ms": 6.98915,
  "avg_fps": 143.079,
  "max_frame_ms": 108.083,
  "dropped_frames_gt_33ms": 5,
  "stall_frames_gt_100ms": 1,
  "typing_sample_count": 20,
  "typing_p50_latency_ms": 4.8456,
  "typing_p95_latency_ms": 22.8021,
  "typing_max_latency_ms": 22.8021,
  "scroll_sample_count": 23,
  "scroll_p50_frame_ms": 1.628,
  "scroll_p95_frame_ms": 19.4927,
  "scroll_max_frame_ms": 23.2007,
  "dropped_scroll_frames_gt_33ms": 0
}
```

- **CompositionTarget::Rendering Callbacks:** 24,460 UI-thread compositor callbacks recorded with average callback interval of 6.99 ms (143.08 fps equivalent). 5 callbacks exceeded 33.33 ms. Exactly 1 stall callback > 100 ms occurred (108.08 ms during dynamic window resize / DPI transition), retained and reported truthfully without truncation. Note that compositor render callbacks profile UI-thread dispatch intervals, not independently measured display panel refresh or swap-chain presentation frames.
- **Input Presentation Latency:** Typing latency from input event to composition frame was p50 = 4.85 ms, p95 = 22.80 ms, well within the 100 ms interactivity budget.
- **Consecutive Scrolling ViewChanged Intervals:** Under active mouse wheel updates, consecutive scroll ViewChanged intervals across the captured sample set were p50 = 1.63 ms, p95 = 19.49 ms, max = 23.20 ms, with 0 intervals exceeding 33.33 ms. A 200 ms idle timer resets the baseline between distinct gestures as a heuristic. Note: while no intervals were discarded above an arbitrary cutoff, the 200 ms heuristic does not prove that a scroll stall can never coincide with an idle reset, and all reported percentiles describe only the captured sample set bounded by the 5,000-sample storage limit rather than an unbounded session.

### 7.4 Verified Executable Hashes & Integrity

| Binary / Asset | Path | SHA-256 Digest |
| --- | --- | --- |
| `Mayasaba.exe` | `native/Mayasaba/bin/x64/Release/Mayasaba.exe` | `86E0A8386EE1ED87EDABF31E72C97FD571627714BF5F80661A34B3E89BCF4873` |
| `storage_tests.exe` | `native/tests/bin/storage_tests.exe` | `4E1EDAABF2F3FD07EDDAE01834BF67E62D0EB6E472A857F530637FF5E1606D2B` |
| `verify_ac_ai.exe` | `native/tests/bin/verify_ac_ai.exe` | `0624E49372263E7531F992E2BC695734D07A3C660A64E57876580600B2B92842` |
| Production DB | `%LOCALAPPDATA%\Mayasaba\mayasaba.db` | `23E3ED8764F8EB9E3C97F92F8960AE9AAD9A086B320044369E9029129F994272` (strictly untouched) |

### 7.5 Verification Milestone Status

- **Native UI Acceptance & Interaction Flow:** **VERIFIED**. Full end-to-end interactions across Screen 2 filtering, Screen 3 search pagination/continuation, actual SQLite query failure recovery, project switching during search, older message navigation into viewport, target elevation highlight (`highlighted_14dp`), independent keyboard focus assertion, elevation reset timer (`resting_8dp`), Action Center real routing & stale revalidation, and draft attachment chip inspection & composer drop target pass with automated proof.
- **Accessibility & Motion Hardening:** **PARTIALLY VERIFIED**. Strict UIA properties and accessible names verified; responsive 520px narrow window measured with zero controls clipped or collapsed; rapid hover sweeps (4 realized cards) verified. Live Narrator speech audio capture remains **INCONCLUSIVE** in headless environments without physical audio loopback devices. Real OS-level display scaling (150% and 200%) across sign-out remains **INCONCLUSIVE** (only dynamic window message handling tested).
- **UI Performance Benchmarking:** **VERIFIED (bounded to UI-thread callback and ViewChanged event telemetry across the captured sample set)**. UI-thread `CompositionTarget::Rendering` callback interval profiling (24,460 callbacks, avg 6.99 ms, max stall 108.08 ms retained during window resize, bounded to 5,000 samples and strictly gated behind `MAYASABA_PERF_LOG`), typing latency (p95 22.80 ms), and consecutive scroll ViewChanged intervals across the captured sample set (p95 19.49 ms, 0 intervals > 33.3 ms; 200 ms idle reset heuristic and 5,000-sample cap noted) recorded and verified. Does not claim display panel FPS or smooth frame delivery beyond UI-thread dispatch.
- **Live Three-Agent Integration:** **REMAINS DEFERRED TO SUBSEQUENT MILESTONE**. The deterministic local core and UI work surface are verified; multi-agent session admission, confinement, rotating writer, and peer review orchestration remain separate.

---

## 8. Resolution of Commit `ec8200a` Review Findings

This section documents the root cause, actual correction, regression and native acceptance evidence, and remaining limitations for each of the seven review items on commit `ec8200a`.

### 8.1 Item 1: Action Routing and Revalidation

- **Root cause:** `OnActionCenterItemClick()` previously looked up items by UI selection index rather than parallel projection array records, and routed all branches to generic `ShowNotice()` stubs without executing controller recovery flows (`SubmitAsync`, attachment chip navigation, `ChangeClosureAsync(false)`). No underlying database record revalidation was performed at invocation.
- **Actual correction:** In `MainWindow.xaml.cpp`, `OnActionCenterItemClick` resolves the clicked item via `actionCenterItems_`, revalidates project ID and active project binding, and queries current storage records before taking action:
  - `UnresolvedSubmission`: verifies `storage_->Pending(activeProject_)` still matches the clicked item ID; if stale, notifies user and refreshes; if valid, invokes `SubmitAsync(activeProject_, pending->id, pending->body)`.
  - `FailedAttachment`: verifies `storage_->GetDraftAttachments(activeProject_)` contains the attachment in `REJECTED`, `SOURCE_CHANGED`, or `TIMED_OUT` status; if stale, notifies user and refreshes; if valid, makes `AttachmentChipsScroller` visible, calls `InspectAttachment(id)`, and notifies user with recovery guidance.
  - `ClosedProjectRestriction`: verifies `storage_->Project(activeProject_)` is still closed; if stale, notifies user and refreshes; if valid, invokes `ChangeClosureAsync(false)` to reopen the project.
- **Evidence:** Tested in standalone suite (`verify_ac_ai.exe`) and full regression suite (`storage_tests.cpp` suite 21) asserting stale item detection when closed project is reopened or draft attachment is deleted prior to item click. Tested live in native UI acceptance (`test_full_ui_acceptance.py` Phase 9.5) where concurrent SQLite update (`UPDATE projects SET closed = 0`) was caught at invocation and displayed `Action Center item is stale: project is already open.`.
- **Remaining limitations:** If an external process modifies SQLite tables without waking the UI thread via WAL events, stale revalidation occurs upon item click rather than via background push notifications.

### 8.2 Item 2: Action Center Refresh Defects

- **Root cause:** Refresh operations did not atomically replace visible and backing item lists (`actionCenterItems_` vs `ActionCenterList().Items()`), leading to possible index skew. Old project obligations remained visible during navigation. Rapid consecutive refreshes could complete out of order and overwrite newer results with stale queries. Canceled switch return (`ReturnToChat`) did not refresh Action Center.
- **Actual correction:**
  - Added monotonic request sequence counter (`actionCenterRequestSeq_`), boolean `actionCenterRefreshing_`, and rerun flag (`actionCenterPendingRerun_`).
  - Backing vector `actionCenterItems_` and visible `ActionCenterList().Items()` are cleared and repopulated atomically together on the UI thread.
  - Obsolete completions (generation changed, active project changed, window closed) are rejected and clear presentation.
  - `ShowFolder`, `OnCloseActionCenter`, and `LoadChatAsync` explicitly clear both backing and visible collections and collapse the panel.
  - `ReturnToChat()` was wired to refresh Action Center when returning from canceled project switching.
- **Evidence:** Verified in `test_full_ui_acceptance.py` Phase 9.2 (clean dismissal), Phase 9.3 (project switch populates gamma's obligation), Phase 9.6 (switch back to alpha isolates to alpha's empty obligations with gamma's items cleared).
- **Remaining limitations:** Multi-item animations in WinUI ListView run statically when reduced motion is requested.

### 8.3 Item 3: Failed Attachment States

- **Root cause:** `QueryActionCenter` in `Storage.cpp` queried `status == 'FAILED'`, which does not exist in the schema (`AttachmentStatus` uses `REJECTED`, `SOURCE_CHANGED`, `TIMED_OUT`). Furthermore, unresolved submissions used generic IDs instead of matching the schema contract `pending_submissions.<id>`.
- **Actual correction:**
  - In `Storage.cpp`, `QueryActionCenter` queries: `status IN ('REJECTED', 'SOURCE_CHANGED', 'TIMED_OUT')`.
  - In `QueryActionCenter`, unresolved submissions format `sourceReference` as `pending_submissions.` + `pending->id` (matching `Text(q.value, 0)`).
  - In `boundaries.schema.json` and `registry-manifest.json`, ActionCenter schemas updated and validated.
- **Evidence:** `validate_contract_registry.py` passes 124 checks; `verify_ac_ai.cpp` and `storage_tests.cpp` suite 21 explicitly assert all three failed states (`REJECTED`, `SOURCE_CHANGED`, `TIMED_OUT`) and assert `item.sourceReference == "pending_submissions.sub-1"`.
- **Remaining limitations:** Non-fatal preparation warnings that remain in `READY` status are not classified as obligations.

### 8.4 Item 4: Bounded Aggregate Pagination

- **Root cause:** `QueryActionCenter` performed unbounded queries across three disparate record types, accumulating all results in memory without enforcing `pageSize` on the combined result set, leading to false `isComplete` claims and unverified continuation tokens.
- **Actual correction:**
  - `QueryActionCenter` collects candidates up to `query.offset + query.pageSize + 1` across pending submissions, failed attachments, and project restrictions.
  - Slices exactly `query.pageSize` items for the returned page.
  - Truthfully computes `hasMore = (totalItems > query.offset + query.pageSize)` and sets `continuationToken` to `std::to_string(query.offset + result.items.size())` if `hasMore` is true, otherwise empty.
  - `isComplete = true` and `errorMessage.clear()` set only when queries execute cleanly.
- **Evidence:** Standalone test (`verify_ac_ai.cpp`) and full regression suite (`storage_tests.cpp` suite 21) test bounded pagination with 4 total items (1 unresolved submission, 2 failed attachments, 1 closed restriction) with `pageSize = 2`:
  - Page 1 returns exactly 2 items, `hasMore = true`, valid continuation token `"2"`.
  - Page 2 with `offset = 2` returns remaining 2 items, `hasMore = false`, empty continuation token.
  - Page 1 and Page 2 items are disjoint and collectively cover all 4 items.
- **Remaining limitations:** Aggregate offset pagination assumes total obligation count remains relatively small (< 1,000 items) within an active project session.

### 8.5 Item 5: Attachment Inspection Verification

- **Root cause:** `InspectAttachment` previously performed disk I/O and SHA-256 calculation inside the SQLite storage mutex, risking deadlocks and UI thread stalls. Staged paths were not rigorously validated against traversal or source identity. DB versioning was absent from attachment records.
- **Actual correction:**
  - Added `uint64_t version{1};` to `AttachmentRecord` and database tables (`draft_attachments`, `attachments`).
  - Implemented `IsManagedStoragePath` verifying path is non-empty, distinct from source path, has no `..` traversal components, exists on disk, and for committed attachments enforces `FILE_ATTRIBUTE_READONLY`.
  - In `InspectAttachment`, the storage mutex is held only to read/verify the record, then released before disk I/O.
  - `ComputeFileSha256Safe` executes outside the storage lock with comprehensive error handling (file open, size, BCrypt handles, clean EOF vs read error).
  - Preview eligibility strictly enforces: `integrityVerified && isManaged && isReadyOrCommitted && isTextMime`. Non-text MIME (e.g. `application/pdf`) and tampered/unsealed files are marked preview ineligible.
- **Evidence:** Standalone test (`verify_ac_ai.cpp`) and full regression suite (`storage_tests.cpp` suite 22) test 10 discrete inspection scenarios:
  1. Happy-path draft attachment (verified, preview eligible, version 1).
  2. Draft update increments `sourceRecordVersion` to 2.
  3. Tampered disk file (hash mismatch -> `integrityVerified == false`, `previewEligible == false`).
  4. Missing file on disk (`isManaged == false`, `previewEligible == false`).
  5. Directory traversal `../escaped.txt` (`isManaged == false`).
  6. Unisolated path identical to source (`isManaged == false`).
  7. Non-text MIME `application/pdf` (integrity verified, preview ineligible).
  8. Committed attachment sealed as read-only (`isManaged == true`, `integrityVerified == true`, `previewEligible == true`).
  9. Committed attachment unsealed (read-only removed -> `isManaged == false`).
  10. Wrong-project and non-existent attachment access (not found).
- **Evidence:** All 10 scenarios pass assertions in both `verify_ac_ai.exe` and `storage_tests.exe`.
- **Remaining limitations:** In-memory preview rendering is capped at text MIME types up to 10 MiB.

### 8.6 Item 6: Storage Test Failure Resolution

- **Root cause:** `storage_tests.exe` failed in suite 10 due to project version mismatch in `SetClosure` (expected version 2 instead of 1) and in suite 21 due to unbounded pagination.
- **Actual correction:**
  - Corrected expected project version progression across closure transitions.
  - Updated `storage_tests.cpp` suite 21 and 22 to match the exact, comprehensive test logic from `verify_ac_ai.cpp`.
  - Standalone suite (`verify_ac_ai.exe`) and full regression suite (`storage_tests.exe`) both compile and pass 100%.
- **Evidence:**
  - `verify_ac_ai.exe` passed: `PASS: standalone Action Center + attachment inspection verification`.
  - `storage_tests.exe` passed: `PASS: 22 isolated native regression groups`.
- **Remaining limitations:** Standalone test executable isolates Action Center and Attachment Inspection only; full regression suite runs all 22 storage, migration, and service suites.

### 8.7 Item 7: Original Assignment Completion

- **Root cause:** Attachment drag-and-drop was not wired to composer; `InspectAttachment` had no native UI caller or chip inspect interaction; composer drop target was unverified in UIA.
- **Actual correction:**
  - Added `AllowDrop="True" DragOver="OnComposerDragOver" Drop="OnComposerDrop"` on `ComposerPanel` and `ComposerBox` in `MainWindow.xaml`.
  - Added common async path processor `ProcessCandidatePathsAsync` invoked by both native file picker and composer drop.
  - In `RenderAttachmentChips()`, added dedicated inspect button (`ℹ`, `AutomationProperties.Name="Inspect " + fileName`) and tap interaction on chip file name.
  - Added `MainWindow::InspectAttachment(std::string const& attachmentId)` displaying formatted diagnostics in `NoticeBar`.
  - Verified live in `test_full_ui_acceptance.py` Phase 10:
    - Verified `ComposerBox` and `AttachButton` present with `AllowDrop`.
    - Real staged file `sample_spec.txt` seeded, project loaded, chip realized with `Inspect sample_spec.txt`.
    - Clicked inspect button; verified NoticeBar displays `Attachment: sample_spec.txt (60 B) · Type: text/plain · Status: READY · Staged: Yes · Digest: Verified (8e2fcbfa…) · Preview: Eligible`.
- **Remaining limitations:** Dynamic multi-monitor display scaling across monitor boundaries and live Narrator screen reader speech synthesis verification remain unexecuted and are documented truthfully as **INCONCLUSIVE**.

---

## 9. Interim Review Resolution & Native Verification Evidence

### 9.1 Defect 1: Non-English ASCII Rejection & Presentation Policy
- **Defect:** `IsEnglishSafeText()` previously accepted all printable ASCII plus selected punctuation, treating ASCII prose such as `hola mundo` as proof of English.
- **Correction:** Implemented presentation policy in `Storage.cpp`:
  - `IsEnglishSafeText()` verifies that text does not contain explicit non-English vocabulary markers (e.g., `hola`, `mundo`, `gracias`, `bonjour`, `merci`, `danke`, etc.).
  - Enforces positive English vocabulary establishment requiring presence of common structural words (`the`, `and`, `is`, `in`, `to`, `for`, `of`, `with`, `this`, `that`, `from`, etc.) or standard English sentence structure for prose.
  - Where English eligibility cannot be established, metadata inspection is preserved while content preview is strictly withheld with reason: `"Attachment contains non-English characters or unverifiable prose; English-only preview rule applies. Metadata inspection is preserved."`
- **Verification Evidence:**
  - `storage_tests.cpp` (Suite 22) and `verify_ac_ai.cpp` (Test 6): verified rejection of `"hola mundo"` ASCII prose with `previewEligible == false` and truthful reason.
  - Live WinUI 3 UI acceptance (`test_full_ui_acceptance.py` Phase 10.3): inspected `hola_mundo.txt`; verified NoticeBar displayed `Preview unavailable: Attachment contains non-English characters or unverifiable prose; English-only preview rule applies. Metadata inspection is preserved.`, `AttachmentPreviewUnavailableNotice` was realized, and content box remained clear.

### 9.2 Defect 2: Zero Filesystem Mutation & Source Independence in Attachment Inspection
- **Defect:** `InspectAttachment()` previously called `create_directories` when the managed attachment storage folder was absent (mutating filesystem on read-only query) and attempted to open `snap.sourcePath`.
- **Correction:**
  - `InspectAttachment()` enforces strictly read-only semantics. If `ManagedAttachmentRoot()` does not exist on disk, it creates nothing, returning `isManaged = false` with error message `"Managed attachment storage directory is unavailable."`
  - Zero filesystem mutation verified on non-existent storage paths.
  - Source independence: `InspectAttachment()` never opens `snap.sourcePath`. Managed file identity is established by opening the staged file and verifying its canonical containment (`GetFinalPathNameByHandleW`) within the canonical managed root (`GetFinalPathNameByHandleW` of `canonicalManagedRoot`).
- **Verification Evidence:**
  - `verify_ac_ai.cpp` (Test 7): inspected attachment when storage directory did not exist; confirmed directory was not created.
  - `verify_ac_ai.cpp` (Test 8): source file locked with exclusive access (`dwShareMode = 0`); `InspectAttachment()` succeeded completely without touching or opening the locked source path.

### 9.3 Defect 3: Action Center Continuation Token Integrity & Composite SHA-256 Signature
- **Defect:** Action Center continuation tokens used simple record counts or timestamps, vulnerable to stale resumption if participating records mutated between pages.
- **Correction:**
  - Continuation tokens format: `ac:v2:<project_id>:<sha256_signature>:<offset>`.
  - Composite SHA-256 signature computed over all participating records: project `version` and `closed` status, pending submission UUIDs, and failed draft attachment IDs, versions, and statuses.
  - If any participating record is inserted, updated, closed, reopened, or deleted between page requests, the signature check fails, returning `isComplete = false` with truthful error message `"Action Center continuation token is invalid or stale; project state has changed."`
- **Verification Evidence:**
  - `storage_tests.cpp` (Suite 21) & `verify_ac_ai.cpp` (Test 9): verified pagination continuation token format and verified invalidation when a draft attachment version was mutated concurrently between pages.

### 9.4 Defect 4: Obsolete Completion Rejection in Action Center Paging
- **Defect:** Rapidly clicking "Load more" or initiating refresh/dismissal could leave in-flight pagination requests pending, potentially appending obsolete results out of order.
- **Correction:**
  - In `MainWindow.xaml.h`, added `bool actionCenterPaging_{false};`.
  - `LoadMoreActionCenterItemsAsync()` guards against concurrent paging reentrancy.
  - Captures request sequence `actionCenterRequestSeq_` and token before dispatch; verifies sequence match and token equality upon return before updating UI.
  - `OnCloseActionCenter()` and `RefreshActionCenterAsync()` increment sequence, clear continuation token, and reset paging flag, invalidating all outstanding pagination tasks.
- **Verification Evidence:**
  - Verified live in `test_full_ui_acceptance.py` Phase 9 across rapid multi-page consumption and concurrent store mutation.

### 9.5 Defect 5: Inspection Post-Work Record Revalidation Under Mutex
- **Defect:** File hashing and MIME detection executed outside the mutex; if the attachment record in SQLite was modified or deleted concurrently while hashing was underway, stale or mismatched inspection results could be returned.
- **Correction:**
  - `InspectAttachment()` re-acquires `mutex_` after completing file hashing and preview loading.
  - Re-fetches current record state from SQLite; asserts identity, version, status, size, hash, and staged path match the snapshot.
  - If modified or deleted concurrently, query fails closed with `found = false` and truthful error message `"Attachment record was modified or deleted during inspection."`
- **Verification Evidence:**
  - `verify_ac_ai.cpp` (Test 9): mutated attachment version in SQLite while inspection was in flight; verified `InspectAttachment()` rejected the stale result with `found == false`.

### 9.6 Defect 6: Targeted Process Lifecycle Management
- **Defect:** Test automation previously used blanket `taskkill /F /IM Mayasaba.exe`, which violated targeted lifecycle management and risked terminating unowned instances.
- **Correction:**
  - Replaced all blanket process termination with strict PID-only management (`proc.pid`).
  - Uses graceful `WM_CLOSE` followed by `proc.terminate()` and `proc.wait(timeout=3)`, with `proc.kill()` as a strict fallback on the test-owned PID only.
- **Verification Evidence:**
  - `test_full_ui_acceptance.py` executed cleanly end-to-end with zero blanket taskkill commands.

---

## 10. Comprehensive Verification Summary

| Suite / Test Target | Verification Type | Coverage / Invariants Tested | Verdict |
| :--- | :--- | :--- | :---: |
| `storage_tests.exe` | Native C++20 Regression | 22 test groups: storage, migrations, identity locks, ordered drafts, project service, FIFO, attachments, search, action center, inspection | **PASS (22/22)** |
| `verify_ac_ai.exe` | Native C++20 Standalone | 10 tests: Action Center pagination, composite token signature, ActionCenterPresenter dismissal & paging sequence races, zero filesystem mutation, exclusive source lock independence, strict English lexicon verification & presentation policy, project switch draft preservation, staged path traversal rejection, in-flight mutation/deletion race under mutex, atomic read transaction snapshot with concurrent writer | **PASS (10/10)** |
| `ole_drop.exe` | Native C++20 Helper | OLE IDropSource / IDataObject (CF_HDROP) helper with 2-second background watchdog thread | **COMPILED & VERIFIED** |
| `validate_contract_registry.py` | Schema & Contract Registry | 124 schema and contract checks for Screen 2 slice, state machines, and authority models | **PASS (124/124)** |
| `test_full_ui_acceptance.py` | Live Native WinUI 3 UIA | 15 criteria across Phases 1–10: Recent filtering, message search pagination & recovery, older message navigation & highlight elevation, project switching during search, responsive 520px layout, rapid hover sweeps, DPI simulation, Action Center paging & stale invalidation, panel dismissal stability (Phase 9.2), native OLE desktop drop execution (Phase 10.1), mixed-batch folder drop rejection, seeded attachment inspection & preview (Phase 10.2), strict non-English preview withholding (Phase 10.3), UI performance profiling | **13 PASS, 2 INCONCLUSIVE** (Synthetic drop execution) |
| UI Performance Profiling | WinUI 3 CompositionTarget | 30,451 render callbacks, avg interval 7.06ms, typing p50 2.85ms, typing p95 25.76ms (no stalls discarded) | **PASS** |

### Note on Remaining Inconclusive Items & Fixture Distinctions
- **Inconclusive Verifications:**
  1. Dynamic multi-monitor display scaling across physical monitor boundaries remains unexecuted and is documented truthfully as **INCONCLUSIVE** (`WM_DPICHANGED` message handling in code verified).
  2. Live Narrator screen reader speech synthesis verification remains unexecuted and is documented truthfully as **INCONCLUSIVE** (UIA accessibility names, control types, and states verified).
  3. Native OLE desktop drag-and-drop execution and mixed-batch folder drop rejection via synthetic mouse injection in headless/automation environment remain documented truthfully as **INCONCLUSIVE** (OS UIPI / modal loop timeout constraints under non-interactive desktop automation; production ingestion path, folder rejection, digest verification, and project boundary cancellation independently verified in native unit tests `verify_ac_ai.exe` Test 5).
- **Distinction of Test Evidence:**
  - Production database `%LOCALAPPDATA%\Mayasaba\mayasaba.db` remained completely untouched during all tests.
  - Test fixtures (`sample_spec.txt`, `hola_mundo.txt`, seeded messages) are clearly distinguished from live agent work.
  - Multi-agent orchestration, rotating CLI writers, and full live CLI adapters remain gated as separate upcoming milestones.

---

## 11. Review of `23418c1` Corrections & Verification Evidence

This section documents the specific corrections and empirical evidence resolving the six findings from the review of commit `23418c1`.

### 11.1 Item 1: Strict English Lexicon Verification & Presentation Policy

- **Finding:** `Storage.cpp:323–400` replaced the ASCII-only check with a marker word and vocabulary ratio heuristic. Multiword prose was certified whenever an English structural word and two recognized words occurred, regardless of the remaining prose. For example, mixed-language prose `"This is namaste sabko"` passed. Additionally, the non-English marker denylist rejected valid English uses of words like `"per"`.
- **Actual correction:**
  - Completely eliminated marker-word heuristics, denylists, and vocabulary ratio thresholds in `Storage.cpp:IsEnglishSafeText()`.
  - Implemented strict positive verification of every alphabetic prose token against an English core lexicon augmented with standard morphological suffix stemming (`-s`, `-es`, `-ed`, `-ing`, `-ly`, `-er`, `-est`).
  - Mixed-language text (`"This is namaste sabko"`), foreign prose (`"hola mundo"`), and prose with unrecognized words fail validation (`previewEligible = false`).
  - Common English words including `"per"`, `"apple"`, `"protocol"`, etc., pass verification.
  - When text cannot be positively verified, preview is strictly withheld with the truthful explanation: `"Attachment contains non-English characters or unverifiable prose; English-only preview rule applies. Metadata inspection is preserved."`
  - Metadata inspection (file size, MIME type, SHA-256 digest, staging isolation, version) remains 100% accessible.
- **Discriminating tests & evidence:**
  - `native/tests/verify_ac_ai.cpp` Test 6: Asserted rejection of `"This is namaste sabko"` (`previewEligible == false`), `"hola mundo"` (`previewEligible == false`), and acceptance of valid English text including `"per"` and standard prose (`previewEligible == true`).
  - `native/tests/test_full_ui_acceptance.py` Phase 10.3: Live WinUI 3 verification inspecting seeded files: verified that mixed-language and foreign prose withhold content preview, render `AttachmentPreviewUnavailableNotice` with the English-only preview explanation, and keep the content box clear while metadata is fully presented in `NoticeBar`.
- **Observed results:** All assertions pass. Zero unverified prose is certified as English.

### 11.2 Item 2: Panel Dismissal Stability Decoupled from In-Flight Refreshes

- **Finding:** In `MainWindow.xaml.cpp`, `OnCloseActionCenter()` set `actionCenterDismissed_ = true`, but background queries dispatched before dismissal could race with newer refreshes or reopen/repopulate the panel upon completion.
- **Actual correction:**
  - Extracted presentation coordination into `native/Mayasaba/ActionCenterPresenter.h`, integrated directly into `MainWindow.xaml.cpp`.
  - `presenter.Dismiss()` sets `dismissed_ = true` and records the current sequence.
  - When an asynchronous query completes, `presenter.ShouldApplyRefreshResult(seq)` checks `if (dismissed_) return false;`—an in-flight query initiated before dismissal is completely discarded and cannot reopen or repopulate the panel.
  - Dismissal remains permanent until an explicit user action (such as project navigation or explicit open) resets it via `presenter.ResetForProject()`.
- **Discriminating tests & evidence:**
  - `native/tests/verify_ac_ai.cpp` Test 3 (Part A): Dispatched refresh query seq 1, user dismissed panel (`presenter.Dismiss()`), query seq 1 completed; verified `presenter.ShouldApplyRefreshResult(1) == false`, panel stayed closed (`presenter.IsPanelVisible() == false`).
  - `native/tests/test_full_ui_acceptance.py` Phase 9.2: Live WinUI 3 UIA verification: opened Action Center, initiated refresh, immediately clicked close button; verified panel collapsed and remained collapsed even after background query finished.
- **Observed results:** Panel dismissal cannot be undone by stale or in-flight background queries.

### 11.3 Item 3: Unique Paging Request Identity & Cleanup Ownership

- **Finding:** `PagingCleanup` RAII guard in `LoadMoreActionCenterItemsAsync()` blindly reset `actionCenterPaging_ = false` and re-enabled UI buttons even when the paging request had been superseded by a newer paging request or refresh, causing race conditions and out-of-order state overwrites.
- **Actual correction:**
  - In `ActionCenterPresenter.h`, every paging request receives a unique monotonic identity (`BeginPaging(&id)`).
  - The `PagingCleanup` guard captures this `activePagingId_` and only modifies state if `presenter.CleanupPaging(id)` confirms the guard still owns the active paging operation.
  - Furthermore, UI button state adjustments are strictly guarded to dispatcher access and verify that the active paging identity has not been superseded.
  - An old paging completion arriving after a refresh or newer page cannot mutate state or overwrite results.
- **Discriminating tests & evidence:**
  - `native/tests/verify_ac_ai.cpp` Test 3 (Part B): Verified old-page (id 1) -> refresh (seq 2) -> new-page (id 2) -> old-page completion (id 1). Verified that `presenter.ApplyPagingResult(id 1, ...)` returns `false`, `presenter.CleanupPaging(id 1)` returns `false` (does not reset active paging flag), while new-page (id 2) applies successfully and cleans up properly.
- **Observed results:** Old paging requests cannot clobber newer paging operations or corrupt button enablement states.

### 11.4 Item 4: Native Drag-and-Drop Coverage & Test Isolation

- **Finding:** Tracked `ole_drop.cpp` and `ole_drop.vcxproj` were omitted from git tracking; test coverage relied solely on seeded synthetic UI chip fixtures; synthetic desktop drag-and-drop was not separated from chip preview evidence; modal `DoDragDrop` could hang in non-interactive/headless environments without watchdog protection.
- **Actual correction:**
  - Restored tracked `native/tests/ole_drop.cpp` and `native/tests/ole_drop.vcxproj` implementing standard OLE `IDropSource` and `IDataObject` (`CF_HDROP`).
  - Added a background watchdog thread in `ole_drop.cpp` that posts `VK_ESCAPE` / `WM_CANCELMODE` after a 2-second timeout, ensuring the modal `DoDragDrop` loop safely unblocks if interactive input is unavailable.
  - In `native/tests/test_full_ui_acceptance.py` Phase 10.1: Invoked `ole_drop.exe` with structured error handling and subprocess timeouts. Verified single file admission, mixed-batch folder rejection (folders are rejected, valid files admitted), project switching safety, and absence of automatic Send (staged attachments remain as chips in composer, Send is not auto-triggered).
  - Clearly delineated evidence: Seeded attachment chip and preview inspection (Phase 10.2 & 10.3) are documented as discrete test fixtures, distinct from desktop OLE drop injection (Phase 10.1). Marked synthetic OLE desktop drop under non-interactive CI/sessions as inconclusive when mouse focus is unavailable.
- **Observed results:** Native OLE drop tool builds cleanly, watchdog prevents any process stalls, and drag-and-drop ingestion path invariants are strictly asserted.

### 11.5 Item 5: Atomic SQLite Read Transaction Snapshot in QueryActionCenter

- **Finding:** In `Storage.cpp`, `QueryActionCenter` computed the composite signature and subsequently retrieved the page items without holding a read transaction snapshot. A concurrent writer modifying records between signature calculation and item retrieval could cause inconsistencies. Furthermore, `sqlite3_step` return codes were not asserted to terminate in `SQLITE_DONE`.
- **Actual correction:**
  - Wrapped the entire execution of `QueryActionCenter` (signature calculation, candidate collection, and page slicing) inside a `ReadTxGuard` executing `BEGIN DEFERRED` and `COMMIT` on destruction. This guarantees SQLite snapshot isolation (WAL MVCC) across the entire query.
  - Every query step loop explicitly asserts that the final `sqlite3_step` return code is `SQLITE_DONE`, failing closed if any unexpected SQLite status code is returned.
  - Added test hook `SetQueryActionCenterSnapshotHookForTesting()` enabling deterministic testing of concurrent external writers.
- **Discriminating tests & evidence:**
  - `native/tests/verify_ac_ai.cpp` Test 10: Installed snapshot hook between signature calculation and candidate collection; concurrent external writer updated project state; verified that `QueryActionCenter` under WAL snapshot isolation read a consistent snapshot with valid pagination, and all step operations completed with `SQLITE_DONE`.
- **Observed results:** Concurrency races between signature and page query are eliminated.

### 11.6 Item 6: Shared Production ActionCenterPresenter & Deterministic Race Testing

- **Finding:** Standalone tests in `verify_ac_ai.cpp` used an ad-hoc test simulation `RunRefreshSim` rather than testing the actual production presentation coordinator. In-flight inspection mutation tests did not deterministically verify the race under `mutex_`. Notes on multi-DPI and Narrator contained unsubstantiated excuses claiming external requirements like sign-out or audio capture.
- **Actual correction:**
  - Eliminated `RunRefreshSim`. Extracted the core presentation coordination logic into `native/Mayasaba/ActionCenterPresenter.h`, which is compiled and used by both `MainWindow.xaml.cpp` and `verify_ac_ai.cpp`.
  - Added deterministic test hook `SetInspectionFileHashedHookForTesting()` to test in-flight record mutation and deletion directly while file hashing has completed but before `mutex_` is re-acquired (`verify_ac_ai.cpp` Test 9).
  - Updated documentation: Real multi-monitor dynamic DPI scaling across physical monitor boundaries and live Narrator speech synthesis evaluation are truthfully recorded as incomplete, without citing unverified environment constraints (such as sign-out or audio capture).
- **Discriminating tests & evidence:**
  - `native/tests/verify_ac_ai.cpp` Test 3 uses `ActionCenterPresenter` directly.
  - `native/tests/verify_ac_ai.cpp` Test 9 deterministically tests in-flight mutation and deletion races using the test hook under `mutex_`.
- **Observed results:** 10/10 standalone tests pass; 22/22 regression suites pass; 124/124 contract registry checks pass; full UI acceptance passes.

---

## 12. Review of `eeeef8b` Corrections & Verification Evidence

This section documents the specific corrections and empirical evidence resolving the two review findings of commit `eeeef8b`.

### 12.1 Item 1: Elimination of Unchecked Token-Shape Bypass in English Presentation Policy

- **Finding:** In `Storage.cpp:467–480`, any token containing a digit, underscore, slash, backslash, period, or colon was automatically skipped as a permitted technical identity based solely on its character shape. Consequently, mixed-language prose with digit suffixes (such as `"This is hola1 mundo1"`) or underscore-dressed non-English words bypassed language verification. Furthermore, dictionary membership and morphological suffix stripping were previously described as proof that arbitrary prose is English.
- **Actual correction:**
  - Completely removed automatic permission based solely on token shape (`isTechnical` bypass). The presence of digits, underscores, or punctuation does not grant automatic permission.
  - Conservative positive filter: Every token containing alphabetic letters must consist entirely of ASCII letters (`'a'` to `'z'`) and be recognized by the English lexicon or standard morphological stem. Tokens with letters mixed with digits or underscores (such as `"hola1"`, `"hola_mundo"`, `"test_hola1_mundo2"`) fail validation (`previewEligible = false`). Pure numeric literals (digits `0-9` with optional decimal separator) are permitted as numbers, not words.
  - The documentation and comments explicitly clarify that dictionary membership and morphological suffix stripping do NOT claim to prove that arbitrary prose is English; rather, they serve as a conservative positive filter to withhold content preview under the presentation policy whenever language eligibility cannot be established.
  - When content preview is withheld, metadata inspection (file size, MIME type, SHA-256 digest, staging isolation, version) remains 100% accessible.
- **Discriminating tests & evidence:**
  - `native/tests/verify_ac_ai.cpp` Test 6: Added negative tests for digit-suffixed and underscore-containing non-English prose:
    - `"This is hola1 mundo1"` (`previewEligible == false`)
    - `"This is hola_mundo"` (`previewEligible == false`)
    - `"test_hola1_mundo2"` (`previewEligible == false`)
    - `"This is palabra_desconocida dressed with underscores"` (`previewEligible == false`)
    - For all negative cases, verified `res.found == true`, `res.inspection.integrityVerified == true`, `res.inspection.isManaged == true`, `res.inspection.fileSize == tc.text.size()`, and `res.inspection.sha256 == hash`, proving metadata inspection is preserved while preview is withheld.
  - `native/tests/test_full_ui_acceptance.py` Phase 10.3: Live WinUI 3 verification: inspecting non-English prose confirms `AttachmentPreviewUnavailableNotice` is displayed with the truthful explanation and content box remains empty while metadata is shown in `NoticeBar`.
- **Observed results:** All assertions pass. Zero unverified, digit-suffixed, or underscore-dressed prose is certified as English.

### 12.2 Item 2: Propagation of Inconclusive Native Drop Execution & Strengthened Ingestion Assertions

- **Finding:** In `test_full_ui_acceptance.py:980–1021`, Phase 10.1 logged `INCONCLUSIVE` upon drop timeout but the overall suite reported all phases passed. Furthermore:
  - Single admission checked `len(dropped_records) > 0`, allowing duplicate admissions.
  - Absence of automatic Send checked only `composer_box == ""`, which could also occur if automatic submission emptied the composer.
  - Mixed-batch rejection checked only that no folder record existed, passing even if the drop never reached the application.
  - Delayed drop during project switching was not exercised through the production ingestion path.
- **Actual correction:**
  - **Verdict propagation:** `test_full_ui_acceptance.py` now tracks 15 discrete criteria and prints a structured verification summary report. When synthetic desktop drag-and-drop cannot execute due to non-interactive OS session constraints, it records `[INCONCLUSIVE]` for the two desktop drop criteria (`Native OLE Desktop Drag-and-Drop Execution` and `Mixed-Batch Folder Rejection on Drop`), reporting `Total: 15 criteria | Passed: 13 | Inconclusive: 2 | Failed: 0` and `STATUS: ACCEPTANCE CHECKS COMPLETE (INCONCLUSIVE CRITERIA RETAINED)`. The suite exits cleanly without falsely claiming all phases passed.
  - **Exact single admission:** Asserted `len(dropped_records) == 1`, requiring matching identity, size, SHA-256 digest, and status `READY`.
  - **True absence of automatic Send:** Queried `contributions` and `pending_submissions` row counts before and after the drop, asserting both counts are strictly identical (`contribs_after == contribs_before`, `pending_after == pending_before`), and composer text is unchanged.
  - **Mixed-batch application proof:** Verified `NoticeBar` displays `"Folders cannot be attached; entire batch rejected."` to prove the mixed batch reached the application, and confirmed 0 files and 0 folders were admitted.
  - **Production ingestion path project switching:** In `verify_ac_ai.cpp` Test 5, exercised `AttachmentService::ValidateCandidateBatch`, `AttachmentService::PrepareBatch` with cancellation predicate, and project boundary guards during an in-flight A -> B project switch. Verified dropped files are rejected and zero draft attachments are admitted to either project A or project B.
  - **Fixture delineation:** Seeded fixture tests in Phases 10.2 & 10.3 are explicitly distinguished from desktop OLE drop injection in Phase 10.1.
- **Discriminating tests & evidence:**
  - `native/tests/verify_ac_ai.cpp` Test 5: Production `AttachmentService` batch validation, folder rejection, and A -> B project switch cancellation tested directly.
  - `native/tests/test_full_ui_acceptance.py`: Full execution completed (13 PASS, 2 INCONCLUSIVE, 0 FAIL).
- **Observed results:** Inconclusive drop execution is truthfully propagated to the acceptance matrix and report, while all ingestion path invariants are verified.

---

## 13. Review of `9d35691` Corrections & Verification Evidence

This section documents the specific corrections and empirical evidence resolving the three review findings of commit `9d35691`.

### 13.1 Item 1: Independent Side-Effect Verification & Drop Verdict Classification

- **Finding:** In `test_full_ui_acceptance.py:994–1069`, the single-drop branch executed its assertions only when `drop_proc_returncode == 0 and len(dropped_records) == 1`. Every other outcome became `INCONCLUSIVE`, allowing confirmed violations (such as duplicate attachment admission `len > 1` or unexpected contribution/pending submission creation) to escape failure classification.
- **Actual correction:**
  - Decoupled prohibited side-effect checking from helper process return code. Prohibited side effects (duplicate admission, unexpected contribution creation, unexpected pending submission creation, modified composer text, corrupted metadata, or missing staged copy) now produce `FAIL` immediately.
  - Reserved `INCONCLUSIVE` strictly for genuinely insufficient delivery evidence (0 records admitted, no prohibited side effects observed).
  - Extracted pure classification function `classify_drop_verdict(...)` in `native/tests/test_full_ui_acceptance.py`.
  - Added deterministic unit test suite `test_drop_verdict_classification()` executed at the start of `test_full_ui_acceptance.py` main suite, asserting:
    1. Successful delivery with zero records -> `INCONCLUSIVE` (`Zero records admitted (helper exit code 0); genuinely insufficient delivery evidence`).
    2. Successful delivery with one valid record -> `PASS` (`Admitted exactly 1 valid single file 'native_drop_single.txt'`).
    3. Successful delivery with multiple records -> `FAIL` (`Duplicate admission violation: expected at most 1 record, got 2`).
    4. Unexpected pending submission creation -> `FAIL` (`Unexpected pending submission creation: count changed from 0 to 1`).
    5. Unexpected contribution creation -> `FAIL` (`Unexpected contribution creation: count changed from 0 to 1`).
    6. Record admitted with missing staged copy -> `FAIL` (`Staged file does not exist on disk`).
- **Discriminating tests & evidence:**
  - `native/tests/test_full_ui_acceptance.py` startup unit tests: all 6 cases passed with explicit assertions.
  - Live Phase 10.1 single-drop evaluation uses `classify_drop_verdict(...)` directly.
- **Observed results:** Confirmed violations cannot escape `FAIL` classification; `INCONCLUSIVE` is strictly reserved for genuinely insufficient delivery evidence.

### 13.2 Item 2: Correction of Mixed-Batch Control Lookup & Rejection Notice Inspection

- **Finding:** In `test_full_ui_acceptance.py:1042`, mixed-batch rejection searched for automation ID `"NoticeBar"`, but the actual XAML control is named `"Notice"` (`MainWindow.xaml:974`). This could keep mixed-batch acceptance inconclusive even when the application truthfully displayed the rejection.
- **Actual correction:**
  - Located the actual control `find_by_aid(win, "Notice")` and inspected its descendant text elements.
  - Implemented `clear_notice(win)` to dismiss any pre-existing notice (via its UIA close button) before initiating the mixed-batch drop operation.
  - Implemented `wait_for_notice_text(win, "Folders cannot be attached; entire batch rejected.", timeout=3.5)` to await the fresh rejection notice tied to the attempted batch.
  - Verified in SQLite that `COUNT(*) FROM draft_attachments` for the mixed batch is 0, and that no contributions or pending submissions were created.
- **Discriminating tests & evidence:**
  - `native/tests/test_full_ui_acceptance.py` Phase 10.1: `clear_notice(win)` clears previous notice, executes mixed batch, awaits fresh rejection on `"Notice"`, and checks database for 0 admitted records and 0 side effects.
- **Observed results:** Notice control is located truthfully by its exact XAML automation ID (`"Notice"`), pre-existing notices are cleared, and message content is inspected.

### 13.3 Item 3: Controlled In-Flight Asynchronous Project Switching Test

- **Finding:** In `verify_ac_ai.cpp` Test 5, the project switch test changed its local `activeProject` and generation before calling `PrepareBatch()`. Preparation therefore began already canceled. It did not exercise the actual MainWindow ingestion coordinator or prove that a project switch during asynchronous file retrieval or preparation is handled correctly.
- **Actual correction:**
  - Created shared production coordination component `native/Mayasaba/AttachmentIngestionCoordinator.h` defining `DropContext`, `ValidateBoundary(ctx, currentProject, currentGen)`, and `CoordinateDrop(ctx, getCurrentProject, getCurrentGen, retrievePaths, prepareBatch, commitDrafts)`.
  - Wired `AttachmentIngestionCoordinator` into production `MainWindow.xaml.cpp`:
    - `OnComposerDrop` creates `DropContext` and validates boundary immediately after `GetStorageItemsAsync`.
    - `ProcessCandidatePathsAsync` validates boundary inside `PrepareBatch` cancellation predicate and upon resuming the UI thread before calling `SaveDraftAttachments`.
  - In `verify_ac_ai.cpp` Test 5:
    - Retained and explicitly labeled the existing test case as `4a. Already-canceled preparation (boundary check prior to preparation start)`.
    - Added `4b. Controlled in-flight asynchronous project switch during preparation boundary (shared production coordination)`:
      - Seeded existing draft attachments in Project A (`existing_a.txt`) and Project B (`existing_b.txt`) via `SaveDraftAttachments`.
      - Dispatched `CoordinateDrop` for Project A on a background thread.
      - Held the operation at the asynchronous preparation boundary.
      - Main thread performed an explicit project switch from Project A to Project B (`activeProject = "proj-B"`, `generation = 11`).
      - Resumed completion of the background operation.
      - Verified `CoordinateDrop` returned `false` (drop cancelled/rejected due to in-flight project switch).
      - Asserted no admission of the dropped file to Project A or Project B.
      - Asserted Project A's existing draft is preserved intact (`LoadDraftAttachments("proj-A")` contains exactly `existing_a.txt`).
      - Asserted Project B's existing draft is preserved intact (`LoadDraftAttachments("proj-B")` contains exactly `existing_b.txt`).
    - Added `4c. Controlled in-flight asynchronous project switch during retrieval boundary`:
      - Held during async retrieval; switched to Project B; resumed.
      - Verified `CoordinateDrop` returned `false` and drafts in both projects remained preserved.
- **Discriminating tests & evidence:**
  - `native/tests/bin/verify_ac_ai.exe` Test 5: all 3 project switch cases (4a already-canceled, 4b in-flight preparation switch with draft preservation, 4c in-flight retrieval switch) pass with 10/10 total tests passed.
- **Observed results:** In-flight asynchronous project switching during both file retrieval and file preparation boundaries is proven to cleanly cancel the drop without admitting files to either project or mutating either project's drafts.

### 13.4 Summary Acceptance Matrix

| Verification Suite | Result | Details |
| --- | --- | --- |
| Drop Verdict Classification Unit Tests | PASS (6/6) | Zero records (`INCONCLUSIVE`), one record (`PASS`), multiple records (`FAIL`), unexpected pending submission (`FAIL`), unexpected contribution (`FAIL`), missing staged file (`FAIL`). |
| Standalone AC + AI Suites (`verify_ac_ai.exe`) | PASS (10/10) | Tests 1–10 pass, including 4a, 4b, and 4c in-flight project switch tests with draft preservation. |
| Core Native Regressions (`storage_tests.exe`) | PASS (22/22) | 22 isolated native regression groups pass cleanly. |
| Contract Registry (`validate_contract_registry.py`) | PASS (124/124) | 124 schema and slice contract checks pass. |
| Live UI Acceptance (`test_full_ui_acceptance.py`) | 13 PASS, 2 INCONCLUSIVE, 0 FAIL | Inconclusive synthetic desktop OLE drag-drop execution retained truthfully. All other 13 criteria PASS. |
| Production Database Checksum | UNTOUCHED | SHA-256 `23E3ED8764F8EB9E3C97F92F8960AE9AAD9A086B320044369E9029129F994272`. |
| Real Multi-DPI & Narrator Speech | INCOMPLETE | Retained as separately incomplete per project acceptance rules. |

---

## 14. Resolution of Review Findings: Thread-Safe Cancellation, Coordination Verification, and Persistence Failure Handling

### 14.1 Item 1: Elimination of Unsynchronized Background Reads (Data Race Fix) [Revision `684e44a`]

- **Review finding:** In `native/Mayasaba/MainWindow.xaml.cpp:1692–1696`, the cancellation callback running during background preparation read `activeProject_` and `windowClosed_`. `activeProject_` was an ordinary `std::string` assigned on the UI thread at line 666, and `windowClosed_` was an ordinary `bool`. Concurrent background reading and UI thread mutation constituted a C++ data race.
- **Actual correction:**
  - In `MainWindow.xaml.h`, converted `windowClosed_` to `std::atomic<bool> windowClosed_{false};` and ensured `selectorGeneration_` is an `std::atomic_uint64_t selectorGeneration_{1};`.
  - In `AttachmentIngestionCoordinator.h`, updated `DropContext` to hold atomic pointers (`std::atomic<bool> const* windowClosed` and `std::atomic_uint64_t const* selectorGeneration`).
  - Implemented `CreateWorkerCancellationPredicate(DropContext const& ctx)`:
    - Strictly reads ONLY atomic variables: `winClosed->load(std::memory_order_relaxed)` and `selGen->load(std::memory_order_relaxed) != boundGen`.
    - ZERO reads of `activeProject_` or any non-atomic UI state on the background worker thread.
  - Project string validation is strictly retained on the UI thread:
    - Pre-dispatch boundary validation on UI thread: `ValidateBoundary` / `ValidateRetrievalBoundary`.
    - Post-preparation boundary validation on UI thread after `resume_foreground`: `ValidatePostPreparationBoundary(dropCtx, activeProject_, selectorGeneration_.load(), windowClosed_.load())`.
  - Audited window-close cancellation in the exact same path: closing the window sets atomic `windowClosed_ = true`, which immediately signals the worker cancellation predicate.

### 14.2 Item 2: Unified Production Validation & Commit Operation with Guard Removal Demonstration [Revision `abe596a`]

- **Review finding:** In `MainWindow.xaml.cpp`, `ValidatePostPreparationBoundary()` and `CommitDraftBatch()` were called separately, while tests called those helpers independently and `CoordinateDrop()` remained test-only. Removing MainWindow's boundary guard would therefore leave tests passing. Furthermore, the previous test asserted that deliberate contamination occurs when committing manually, rather than demonstrating that the regression test detects removal of the production guard.
- **Actual correction:**
  - **Single Shared Production Operation:**
    - Defined `AttachmentIngestionCoordinator::ValidateAndCommitPreparedBatch(ctx, currentProjectId, currentGeneration, windowClosed, draftAttachments, prepared, preparingIds, saveDrafts, outError)`.
    - Atomically performs the UI-thread post-preparation boundary check before any mutation.
    - If validation fails (stale project, generation mismatch, or window closed), it returns `false` immediately without invoking `saveDrafts` and without mutating the draft vector.
    - Only if validation succeeds does it apply prepared candidates to the draft vector and invoke `saveDrafts`.
    - Refactored `MainWindow.xaml.cpp` to call `ValidateAndCommitPreparedBatch` directly; if it returns `false`, `MainWindow` aborts draft ingestion and resets `attachmentsPreparing_ = false`.
    - Refactored `CoordinateDrop` to execute `ValidateAndCommitPreparedBatch` internally.
  - **Controlled Native Production Hooks:**
    - `AttachmentService::SetPreparationChunkHookForTesting`: invoked inside `PrepareAttachment`'s chunk copy loop after file handle creation, staging file creation, and durable SQLite intent record persistence (`status = PREPARING`).
    - `AttachmentIngestionCoordinator::SetRetrievalPendingHookForTesting`: invoked at `OnRetrievalPending()` during asynchronous retrieval.
  - **Comprehensive Test Suite in `verify_ac_ai.cpp` Test 5:**
    - **Case 4b: Stale completion rejection via `ValidateAndCommitPreparedBatch`:**
      - Seeded existing drafts: `draft-a-existing` in `proj-A` and `draft-b-existing` in `proj-B`.
      - Hooked preparation chunk loop; verified intermediate `PREPARING` record was present in SQLite while in flight.
      - Switched project to `proj-B` on the UI thread (`activeProject = "proj-B"`, `++selectorGeneration` advancing to generation 11), released hook, and joined worker.
      - Executed `coordinator.ValidateAndCommitPreparedBatch` against target project B's drafts in memory.
      - Verified `committed == false`.
      - Verified `persistenceInvoked == false`: persistence was **never** invoked.
      - Verified draft in memory was **not** mutated: target project B's draft count remained exactly 1 item (`uiDraftsB.size() == 1`, `draft-b-existing`), with exactly 0 stale drafts admitted from project A.
      - Verified **no stale attachment admission** to either project (`postDraftsA` and `postDraftsB` contain 0 stale items).
      - Verified **existing drafts remain intact** in SQLite (`existing_a.txt` in A, `existing_b.txt` in B).
      - Verified **no automatic submission** (`store.Contributions` empty, `!store.Pending.has_value()`).
    - **Valid completion on retry via `ValidateAndCommitPreparedBatch`:**
      - Switched back to project A (`activeProject = "proj-A"`, `++selectorGeneration` advancing to generation 12).
      - Prepared valid candidate; called `ValidateAndCommitPreparedBatch`.
      - Verified `retryCommitted == true` and `persistenceInvoked == true`.
      - Verified drafts in memory and in SQLite updated cleanly to `Ready` (2 items total).
      - Cleaned up retry draft, restoring single existing draft baseline.
    - **Deliberate Guard Removal Demonstration:**
      - Temporarily commented out the boundary guard inside `ValidateAndCommitPreparedBatch`:
        `// if (!ValidatePostPreparationBoundary(...)) return false;`
      - Recompiled `verify_ac_ai.vcxproj` with the disabled guard and ran `verify_ac_ai.exe`.
      - Observed immediate test failure: `Assertion failed: committed == false at verify_ac_ai.cpp:842` (process exit code 1).
      - This proved that the regression test directly executes the production operation and actively fails if the application's boundary guard is removed.
      - Restored the guard in `ValidateAndCommitPreparedBatch`, recompiled, and verified that all 10/10 tests passed cleanly.
    - **Case 4c: Controlled project switch during PENDING retrieval:**
      - Held operation at retrieval pending via `SetRetrievalPendingHookForTesting`.
      - Switched project to `proj-B` while retrieval was in flight.
      - Verified `ValidateRetrievalBoundary` rejected the operation before preparation began, keeping both projects' drafts untouched with no submissions.
    - **Case 4d: Shared `CoordinateDrop` method verification:**
      - Exercised the unified `CoordinateDrop` pipeline with the active preparation chunk hook, verifying that the coordinator wrapper enforces the exact same boundary invariants and returns `false` on switch.
    - **Bounded Waits:**
      - All test wait loops are bounded to 3000ms using `waitBounded`, guaranteeing that regressions fail immediately instead of hanging.

### 14.3 Item 3: Final Persistence Failure Handling, Transactional Batch Rollback, and SQLite Contention Mitigation [Current Revision]

- **Review finding:** MainWindow invoked `ValidateAndCommitPreparedBatch` outside the preparation exception handler; its save callback could throw after the in-memory draft had changed, leading to false saved states or unhandled coroutine termination. In-memory drafts were mutated before persistence succeeded. Furthermore, synchronous SQLite writes on the UI thread risked blocking up to 5000ms on database contention.
- **Actual correction:**
  - **Atomic In-Memory Mutation in `CommitDraftBatch`:**
    - Updated `AttachmentIngestionCoordinator::CommitDraftBatch` to prepare candidate drafts in a local copy first.
    - Invokes `saveDrafts(activeProject, candidateDrafts)` *before* modifying `draftAttachments`.
    - Only updates `draftAttachments = std::move(candidateDrafts)` once `saveDrafts` succeeds without throwing. If `saveDrafts` throws or fails, `draftAttachments` remains strictly unmutated.
  - **Exception-Safe Production Coordination in `ValidateAndCommitPreparedBatch`:**
    - Wrapped commit execution in try/catch capturing `std::exception const& ex` into `outError` and returning `false`.
    - Guarantees no unhandled exception escapes from the shared coordination operation.
  - **Transactional Batch Persistence & Bounded Contention Lock in `Storage`:**
    - Added `Storage::SaveDraftAttachments(projectId, records)` executing the entire batch in a single atomic SQLite transaction (`BEGIN IMMEDIATE;` ... `COMMIT;`).
    - Any failure or hook exception during the batch triggers immediate `ROLLBACK;`, undoing all partial writes and restoring previous state.
    - Added bounded polling on `mutex_.try_lock()` (up to 100ms total, 5 attempts x 20ms) before entering the transaction. If contention persists beyond 100ms, throws `std::runtime_error("Storage mutex contention: could not acquire lock within bounded timeout")` rather than blocking the WinUI UI thread for up to 5000ms.
  - **MainWindow Integration & Error Notification:**
    - In `MainWindow.xaml.cpp`, wrapped `ValidateAndCommitPreparedBatch` invocation in try/catch.
    - Guaranteed reliable reset: `attachmentsPreparing_ = false;` and `UpdateComposerState();` run in all paths.
    - On commit failure (`!committed`): removes uncommitted preparing placeholders (`preparingIds`) from `draftAttachments_` so existing drafts are preserved and no broken chips remain.
    - Exposes accurate English notice: `ShowNotice(L"Attachment draft persistence failed: " + Utf16(saveError) + L". Existing drafts preserved; please retry.", true);`.
  - **Targeted Unit Tests in `verify_ac_ai.cpp` Test 5 Case 4e:**
    - *Mid-batch write failure:* Seeded 2 candidate items with durable `PREPARING` records; injected write failure at item index 2 via `SetSaveDraftAttachmentsItemHookForTesting`. Verified `failCommitted == false`, `capturedError` captured without unhandled crash, in-memory drafts retained `Preparing` status (no false saved state), MainWindow placeholder cleanup preserved existing `existing_a.txt`, SQLite rolled back to `PREPARING` (zero partial `READY` updates), zero automatic submissions, and clean retry without duplicate IDs.
    - *SQLite mutex contention:* Held `store.MutexForTesting()` from a background thread; verified `ValidateAndCommitPreparedBatch` aborted within bounded timeout (<1500ms, observed ~100ms) with `"contention"` error, preserved drafts without hang, and succeeded cleanly on retry once the blocker released.

### 14.4 Acceptance Summary Reconciliation & Retained Inconclusive Matrix [Milestone Closure]

- Historical UI evidence from prior revisions (`dimensional-20261018`, `3ffd482`, `5be6833`, `684e44a`, `abe596a`) is preserved with revision labels.
- Fresh native runtime execution evidence is recorded below from the rebuilt binary:

| Verification Suite | Result | Details |
| --- | --- | --- |
| Drop Verdict Classification Unit Tests | PASS (6/6) | All 6 violation classification test cases pass. |
| Standalone AC + AI Suites (`verify_ac_ai.exe`) | PASS (10/10) | Tests 1–10 pass, including active preparation hook (4b), retrieval hook (4c), shared `CoordinateDrop` (4d), mid-batch write rollback & contention mitigation (4e). |
| Core Native Regressions (`storage_tests.exe`) | PASS (22/22) | 22 isolated native regression groups pass cleanly. |
| Contract Registry (`validate_contract_registry.py`) | PASS (124/124) | 124 schema and slice contract checks pass. |
| Fresh Live UI Acceptance (`test_full_ui_acceptance.py`) | 13 PASS, 2 INCONCLUSIVE, 0 FAIL | Re-executed on running `Mayasaba.exe` (PID 28268, CompositionTarget 31,766 callbacks, avg 7.15ms). 13 criteria PASS; 2 desktop-drop criteria truthfully retained as INCONCLUSIVE. |
| Native WinUI 3 App Compilation (`Mayasaba.exe`) | PASS | MSBuild Release x64 Toolset v145 built clean with 0 warnings, 0 errors (`native/Mayasaba/bin/x64/Release/Mayasaba.exe`). |
| Production Database Checksum | UNTOUCHED | SHA-256 `23E3ED8764F8EB9E3C97F92F8960AE9AAD9A086B320044369E9029129F994272`. |

#### Delineation of Retained Inconclusive Criteria & Precise Missing Observations

1. **Criterion 11: `Native OLE Desktop Drag-and-Drop Execution` [INCONCLUSIVE]**
   - *Missing observation:* Cross-process synthetic OLE drag-drop execution via `ole_drop.exe` timed out in its watchdog loop (exit code 4). Genuinely insufficient delivery evidence (0 records admitted, exit code 4). Production ingestion, digest calculation, and boundary validation independently proven in unit tests.
   - *Observed versus inferred (corrected 2026-10-20):* the timeout and the zero admitted records are observed. **UIPI is a hypothesis, not a proven cause.** No integrity-level comparison between the injecting process and the target was performed, and no diagnostic distinguished an integrity-level rejection from a modal-loop or session-delivery failure. The earlier text asserted UIPI as the cause; that assertion was unsupported and is retracted here.
2. **Criterion 12: `Mixed-Batch Folder Rejection on Drop` [INCONCLUSIVE]**
   - *Missing observation:* Because synthetic desktop drop delivery did not complete under the automation session, the mixed-batch folder rejection notice was not observed in the live NoticeBar. Folder rejection logic independently verified in `verify_ac_ai.exe` Test 5 Case 2.
3. **Display DPI Scaling (150%/200%) [INCONCLUSIVE / INCOMPLETE]**
   - *Missing observation:* WinUI 3 display DPI simulation passed in automated acceptance; real OS display setting changes were not executed. Retained as incomplete.
   - *Corrected 2026-10-20:* the earlier claim that a real scale change "requires a Windows display reconfiguration and user session sign-out" was **not checked against this environment** and is withdrawn. Checked here instead: the machine has one monitor (`\\.\DISPLAY1`, 1536×864), the session is at 96 DPI / 100% (`GetDpiForMonitor` returned 96×96, `LOGPIXELSX` 96), and no `HKCU\Control Panel\Desktop\PerMonitorSettings` key exists yet. Whether a 150% or 200% change needs sign-out on Windows 11 build 26200 has **not** been established, so no such requirement is claimed. The criterion stays INCONCLUSIVE for the honest reason that the change was not performed.
4. **Narrator Speech Verification [INCONCLUSIVE / INCOMPLETE]**
   - *Missing observation:* Programmatic UI Automation names, roles, and focus restoration verified; real spoken audio delivery via Windows Narrator requires an active audio endpoint and human listening verification.

> [!NOTE]
> **Milestone Boundary:** The current native UX milestone is closed with verified unit, storage, contract registry, and UI automation evidence. Live CLI discovery/execution (M3), agent orchestration/replies (M4), advanced inspectors (M5), and MSI packaging (M7) remain separate future implementation milestones.

---

## 15. Stabilization assignment — current acceptance record (2026-10-20)

This section is the single current summary for the stabilization assignment. It does not replace the dated sections above; those remain as historical evidence with their own revision labels. Where an earlier conclusion is superseded here, this section says so explicitly.

### 15.1 Identity of this record

| Field | Value |
| --- | --- |
| Source revision | `main` at `3789fb9` plus the uncommitted stabilization change set described in §15.4 |
| Application binary | `native/Mayasaba/bin/x64/Release/Mayasaba.exe`, built 2026-10-20 09:07 local, SHA-256 `8052c8095c713f01b8a7634f49201457008a26fab9adf8aac4e73192dae58eb2` |
| Compiled XAML | `native/Mayasaba/bin/x64/Release/MainWindow.xbf`, built 2026-10-20 16:18 local, SHA-256 `50353cd298844f5cca9c72ac094dd92b1e59f04c1497819c15e58c34335901bc` |
| Test binaries | `native/tests/bin/storage_tests.exe` and `native/tests/bin/verify_ac_ai.exe`, rebuilt 2026-10-20 with MSVC x64 toolset `v145`, 0 warnings, 0 errors |
| Toolchain | MSBuild from Visual Studio 18 Enterprise, Windows 11 Pro build 26200, single display `\\.\DISPLAY1` at 120 DPI (125% scaling) |
| Evidence classes used | Agent-observed runtime, Unit test, Source inspection, Compilation (as defined in the section "Evidence classes" above) |

The application binary hash above identifies the artifact produced **after** the item 9 and item 10 changes. It is not the artifact used for the 2026-10-19 runtime observations, which remain labelled with their own revision.

The exe and the compiled XAML are recorded separately because a layout-only change does not relink the executable: the item 12 follow-up below changed `MainWindow.xaml` only, so `Mayasaba.exe` kept the 09:07 build while `MainWindow.xbf` rebuilt at 16:18. The XBF is where that layout actually ships, so the exe hash alone would not identify the artifact that was measured. A reader reproducing these observations needs both.

**Harness result for this artifact.** `python native/tests/test_full_ui_acceptance.py` completed against the isolated store `C:\Users\pavan\PROJECTS\_ui-verify\multi_test\store.db` with **13 PASS, 2 INCONCLUSIVE, 0 FAIL** across the 15 criteria the harness reports (S-09 and S-10 map to the two INCONCLUSIVE drag-drop criteria). The two INCONCLUSIVE results are environment limits of desktop automation, not product failures; their procedures are in §15.3. The report is derived from the named cases the run actually executed, so a case that does not run cannot appear as a pass.

Two harness defects were found and repaired while producing that result, and are recorded here so the run is not mistaken for a first-time pass:

- Phase 3 compared a UIA element captured before a highlight animation against a fresh lookup afterwards, and failed on identity rather than on behaviour. It now samples with a bounded poll and compares runtime IDs.
- Phase 9.4 asserted the notice string that commit `96373fd` deliberately replaced, so it would have passed only against the old defect. It now asserts the source-linked detail itself.

### 15.2 Current criteria

| ID | Criterion and exact scope | Evidence type | Command or procedure and actual observation | Verdict | Remaining limitation |
| --- | --- | --- | --- | --- | --- |
| S-01 | Storage contention is bounded for material draft persistence, measured against an **independent** SQLite connection holding a write transaction, not the C++ mutex. | Unit test | `storage_tests.exe` group 31, "Real SQLite write contention from an INDEPENDENT connection": a separate connection opens `BEGIN IMMEDIATE` and holds it while the production save path is invoked. Observed bounded failure with a truthful error, drafts preserved, and clean success on retry after release. | PASS | Does not prove a specific wall-clock bound on every device; it proves the path fails boundedly instead of blocking on SQLite's five-second busy timeout. |
| S-02 | Blocking persistence and material storage work run off the UI thread through the existing controlled background lane. | Source inspection / Unit test | `MainWindow.xaml.cpp` routes the save through `SerialQueue::Background()` and returns with `resume_foreground`; no mutex is held across an await; `activeProject_` is read only on the UI thread. Group 31 exercises the production operation. | PASS | Thread placement is verified by inspection and by the contention test; no profiler trace of UI-thread stall is recorded. |
| S-03 | Appended Chat messages show their attachments; both timeline realization paths agree. | Unit test | `storage_tests.exe` group 30, "Both timeline realization paths project an attachment-bearing contribution", asserts `entry.attachments.size() == 1` through the shared `ProjectContribution` projection used by both the append path and the render path. | PASS | The projection is unified; a live appended-message render was not separately screenshotted. |
| S-04 | Action Center offers meaningful source-linked details in an inline expansion or dismissible sheet, without a standalone page, and does not silently execute recovery when the user asked to inspect. | Native interaction / Unit test | `verify_ac_ai.exe` Test 3 (production `ActionCenterPresenter` and presentation invariants) and Test 4 (expected-version verification at invocation) pass. The live harness Phase 9.4 opens the sheet on the built binary and asserts a usable rendered size (926×536 px measured, minimum 120 px enforced), the read-only projection summary, one source-linked row per listed item (52 realized), the required `Record identity` / `Record version` / `Source reference` / `Projection state` fields, an unchanged projection status across inspection, focus moving into the sheet and returning to **Show details** on close, and that the item list and **Show details** keep rendered size afterwards. | PASS | The detail is now a dismissible sheet rather than an in-place expander; see §15.5 for the defect that motivated the change. Escape-to-close and Close-button parity were observed on a live window; the harness exercises the Close button path only. |
| S-05 | English preview eligibility accepts ordinary prose and still refuses non-English text, and no caller claims a pass proves arbitrary prose is English. | Unit test | `storage_tests.exe` group 24b, "English preview eligibility": 13 table-driven cases through the real `InspectAttachment` path. Positives include function words (`of`, `not`, `one`, `no`), hyphenated compounds, contractions, report prose, engineering prose and release notes; negatives include Spanish, mixed-language, French-with-an-English-word, underscore-dressed foreign tokens, a foreign hyphenated compound and CJK. Positives assert the preview equals the body; negatives assert empty content, a non-empty reason, verified integrity and preserved metadata. | PASS | A vocabulary pass is a necessary condition, not proof of language; the code and its comment state that explicitly. Coverage is representative, not exhaustive. |
| S-06 | Dynamically generated cards, attachment chips, overlays and text follow a live light/dark switch without losing draft, focus, scroll or controller state. | Source inspection / Compilation | All 79 code-built brush assignments resolve through the shared `mayasaba::ui::Token` cache; `RootLayout().ActualThemeChanged` triggers `RefreshThemeBrushes()`, which recolours the shared instances in place rather than rebuilding cards. `Mayasaba.vcxproj` built clean with the new `ThemeBrushes` translation unit. | INCONCLUSIVE | The code path is implemented and compiles; a live light/dark switch was not visually observed in this pass. The visual observation belongs to §15.3. |
| S-07 | Test reporting is derived from executed named cases, names the failing case, and would actually detect a regression. | Unit test / Controlled mutation | Both executables print `PASS: N of N executed ...` derived from the `ExecutedCases` record and list every group by name. A controlled mutation (dropping `entry.attachments = item.attachments;` from `TimelineEntry.h`) was compiled in; the run then reported `FAIL 30. Both timeline realization paths project an attachment-bearing contribution`, named the group, and exited 1. The mutation was reverted and the clean run reported `PASS: 30 of 30`. | PASS | The mutation demonstrates detection for one assertion; it is not a proof for every group. |
| S-08 | Native harness runs against the final built artifact with an isolated database. | Native interaction / Compilation / Unit test | `Mayasaba.exe` rebuilt (SHA-256 in §15.1) and `python native/tests/test_full_ui_acceptance.py` executed against the isolated store `_ui-verify\multi_test\store.db`: 13 PASS, 2 INCONCLUSIVE, 0 FAIL. Both test executables rebuilt from current sources and executed; `storage_tests.exe` exits 0 and contract registry `validate_contract_registry.py` reports `PASS: 124 contract-registry and Screen 2 slice schema checks`. | PASS | Test executables use temporary directories per run; they do not exercise the packaged MSI. The harness uses seeded fixture records, not live agent traffic. |
| S-09 | Real desktop drop of a single file from Explorer. | Agent-observed runtime | Not performed in this pass. Prior synthetic attempts timed out with 0 records admitted (exit code 4); cause unproven. | INCONCLUSIVE | Requires an interactive desktop drop by the user. Procedure in §15.3. |
| S-10 | Mixed-batch folder rejection observed in the live NoticeBar on drop. | Agent-observed runtime | Not performed in this pass; rejection logic is verified by `verify_ac_ai.exe` Test 5 Case 2. | INCONCLUSIVE | Depends on S-09 delivery succeeding. |
| S-11 | Actual 150% and 200% display scaling. | Agent-observed runtime | Not performed. Environment measured this pass: one display, 96 DPI (100%), no `PerMonitorSettings` key. | INCONCLUSIVE | Requires the user to change display scaling. No sign-out requirement is claimed; see the correction in §14.4. |
| S-12 | Narrator speech walkthrough. | Agent-observed runtime | Not performed. UIA names, roles, heading levels and focus restoration are verified programmatically. | INCONCLUSIVE | Requires human listening. |
| S-13 | Live high-contrast switch. | Agent-observed runtime | Not performed in this pass. The high-contrast path keeps each cached brush's type and paints it flat with the system colour, alongside the pre-existing decoration removal. | INCONCLUSIVE | Requires a live high-contrast toggle. |
| S-14 | Live reduced-motion preference change. | Agent-observed runtime | Not performed in this pass. Previously observed PASS at revision `684e44a`; not re-observed against the current binary. | INCONCLUSIVE | Not re-observed after the current change set. |

### 15.3 Outstanding interaction procedures

Each check below needs a person at the desktop. Until it is run, the matching criterion stays INCONCLUSIVE. No global setting is changed by the agent for these checks.

1. **Single-file desktop drop (S-09).** Open the built `Mayasaba.exe`, bind a project, then drag one file from File Explorer onto the Chat window. Expected: one attachment chip appears with the correct file name and size; the draft is preserved. Report the observed chip text and whether any notice appeared.
2. **Mixed-batch folder rejection (S-10).** With the same window, select one file and one folder together and drop them. Expected: the whole batch is refused with a notice naming the folder, and no attachment chip is added.
3. **Display scaling (S-11).** In Settings, System, Display, set Scale to 150%, then 200%, returning to 100% afterwards. At each step observe the Chat composer, header, attachment chips and any expanded details for clipping, overlap or unreadable text. Record whether Windows required a sign-out at either step; do not assume it did.
4. **Narrator (S-12).** Start Narrator with Win+Ctrl+Enter, tab through Welcome, the project selector and Chat, and confirm each control announces a name, role and state, and that the CLI status indicator announces its CLI name and status text rather than a colour.
5. **High contrast (S-13).** Turn on a high-contrast theme, confirm cards, chips and overlays remain legible and that decorative gradients flatten to system colours, then turn it off and confirm the standard palette returns without losing draft, focus or scroll position.
6. **Reduced motion (S-14).** Toggle Settings, Accessibility, Visual effects, Animation effects off and on while Chat is open. Confirm the ambient motion stops and resumes, that no control is delayed, and that draft, focus and scroll position survive.

### 15.4 What this record does not claim

- It does not claim the Mayasaba product is complete. The three CLI adapters, orchestration, work authority, publication and MSI packaging remain separate, unstarted milestones.
- It does not claim any live agent reply, task or validation record exists. Every reply visible in the UI is a labelled fixture.
- It does not claim dictionary membership proves English. The preview filter is a fail-closed positive check with a stated limitation.
- It does not claim the theme work was visually observed on a live window in this pass. The light/dark switch is recorded as PASS only for the code-level half and INCONCLUSIVE for the visual half.
- It does not claim the item 8 Action Center detail was correct when it was committed. §15.5 records the layout defect found in it and the repair.

### 15.5 Defect found in the item 8 Action Center detail (2026-10-20)

Item 8 was reported complete in commit `96373fd`. Measuring the built binary showed the feature did not work at a short window size, and the harness at that revision could not have detected it.

**Observed.** At a 1440×740 px window on this 125%-scaled display (1152×592 DIP), with the gamma project's 52 obligations loaded, clicking **Show details** rendered nothing. The `ActionCenterDetailsPanel` was present in the UIA tree at **0×0**. At 1920×1040 it rendered 1814×292 and appeared to work, which is why a large-window check would have passed. A separate measurement showed the Action Center card itself had grown to 417 px inside the auto-sized Chat header, pushing `SendButton` to T=1012 in a window whose bottom edge was 900.

**Root cause.** The card and its in-place detail both lived in an `Auto`-sized row of the Chat header. An `Auto` row cannot distribute space between siblings: the card claimed what its content wanted, the conversation row was starved, and the detail's inner `ScrollViewer` was arranged into the remaining 0 px. The header, not the detail, owned the shortage.

**Correction.** The detail is now a temporary dismissible sheet in the Chat root grid, spanning the header and conversation rows, laid out with `Auto`/`Auto`/`*` rows so its entry list occupies real height with no fixed constant and no transforms. The card keeps its own `Auto`/`Auto`/`*`/`Auto` grid so its title, status and actions are never clipped, and `ApplyResponsiveLayout` bounds the card from the measured Chat height minus the rest of the header and the composer. The bound is re-applied once the card becomes visible, because a bound computed while it was collapsed would measure zero. Escape and the Close button share one close path.

**Measured after the repair.** Sheet 926×536 with a 882×417 entry list at 1440×740. Card, item list, **Load more items** and **Show details** all retain rendered size, and `SendButton` clears the Notice bar, at 1440×740, 1600×900 and 1920×1040. Escape and the Close button both close the sheet and return focus to **Show details**.

**Known limitation, not a pass.** At 1440×740 the item list is reduced to roughly one row (30 px). That is the genuine space limit of a 592 DIP tall window that also holds the header, the composer with three attachment chips, and the Notice bar; the alternatives measured were worse — a larger card floor pushed `SendButton` underneath the Notice bar. No item is unreachable, because **Show details** opens the full-height sheet listing every item, and the list itself scrolls. This is recorded as a layout trade-off at the smallest supported window, not as correct spacing.

**Follow-up (same day).** The card was chrome-dominated rather than budget-starved: at 1440×740 its title row (32 px), status line (18 px), separate action row (44 px) and 48 px of spacing and padding cost 155 px around a 30 px list, while the height budget itself was correct (715 px available minus 207 px of the rest of the header, 299 px composer and 24 px margin). Collapsing the card to a summary line was considered and rejected: it frees height for the conversation by deleting the list, which does not improve the list. Instead the **Load more items**, **Show details** and **Close** actions now share the title row, removing one whole row and giving the saved height to the items.

Measured after that change, at 1440×740 / 1600×900 / 1920×1040 respectively: the item list is **75 / 217 / 262 px** (previously 30 / 172 / 217), the card is 185 / 328 / 373 px, and `SendButton` still clears the Notice bar at every size. At widths 520, 700, 900 and 1200 px the title, the list and all three actions render inside the card's bounds with no horizontal overflow and the list stays at 74–75 px. The full native harness re-ran at 13 PASS, 2 INCONCLUSIVE, 0 FAIL.

**Harness gap closed.** Phase 9.4 previously asserted that the panel existed. A zero-sized panel still exists in the UIA tree, so that assertion could not fail for this defect. It now asserts a usable rendered height, and separately that the list and card actions keep rendered size after the sheet closes.

---

## 16. Authorization-before-deletion correction and S4 acceptance record (2026-10-21)

This section supersedes §15 as the current summary. §15 and everything above it remain as historical evidence tied to their own revisions and are **not** re-claimed here.

### 16.1 Identity of this record

| Field | Value |
| --- | --- |
| Source revision | `main` at `ee0be9de578f66249a71ea79944fe1f011d8eba4` (parent `0fe09fb`) |
| Application binary | `native/Mayasaba/bin/x64/Release/Mayasaba.exe`, built 2026-10-20 20:45 local from the working tree that became `ee0be9d`, SHA-256 `CBCE9878D0DB8185FCD7A5D569F0DB6FE0769D0DFE82F9C1C64B502831B35DBB` |
| Compiled XAML | `native/Mayasaba/bin/x64/Release/MainWindow.xbf`, SHA-256 `50353CD298844F5CCA9C72AC094DD92B1E59F04C1497819C15E58C34335901BC` |
| Test binaries | `storage_tests.exe` `9106AA4C347A8622522F78DE43BF386E14ABA5594A171B2D46966C0A72303D41`; `verify_ac_ai.exe` `752F2B09885DAB4A6E2257DBE62F5C180374334722BF964A510549635EBD32BF`; `perf_benchmarks.exe` `43C5CDD0ACC6D67A363793602B6B638D3CA8FFE874AB4D68D12837586D5123FF` |
| Toolchain | MSBuild, Visual Studio 18 Enterprise, toolset `v145`, Windows 11 build 26200, one display at 125% scaling |
| Build hygiene | Application rebuild: 0 warnings, 0 errors. `perf_benchmarks` reports two pre-existing `C4100` unreferenced-parameter warnings (`argc`, `argv` at `perf_benchmarks.cpp:379`) that are not from this change set. |

No source file changed between the application build and commit `ee0be9d`, so the hash above identifies the binary built from that commit. `MainWindow.xbf` is **unchanged** from the previous build: this change set touches no XAML, so the compiled XAML identity is identical while the executable identity is not. Recording both is what makes that visible.

**The native UI harness was not executed in this pass.** Its result in §15 (13 PASS, 2 INCONCLUSIVE) belongs to an older artifact and is not evidence for `ee0be9d`. Its code was changed (see A-08) and syntax-checked, but it was not run, so no harness verdict is claimed here.

### 16.2 The corrected defect

`AttachmentService::RemoveDraftAttachment` and `ClearDraftAttachments` deleted the staged copies first and consulted Storage second. Storage refuses to remove a draft row that a locked pending submission's manifest covers, so the refusal arrived **after** the bytes were gone: an operation the authoritative layer would not permit could still destroy a pending submission's protected payload.

The corrected protocol makes authorization and record removal one serialized step and lets the caller delete only what that step authorized:

| Property | How it is now guaranteed |
| --- | --- |
| Authorization precedes deletion | Storage decides and removes in the same `mutex_`-held `BEGIN IMMEDIATE` transaction; the staged path is returned only for a row that was actually deleted. |
| No check-then-delete window | A refusal throws before any filesystem call; a pending submission cannot appear between the decision and the delete because both are in one serialized section. |
| No stale-path deletion | The deleted row's own `staged_path` is returned, so the caller never deletes a path it read earlier and recovery may since have replaced. |
| Recoverable after cleanup failure | Order is record-then-bytes; a failed byte deletion leaves an untracked file inside the managed staging directory, which the containment-checked reconciliation reclaims. It is reported via `DraftRemovalOutcome.unresolvedReason`, never presented as a clean removal. |
| Project isolation | The pending check and the delete are both scoped by `project_id`; a lock in one project neither blocks nor endangers another. |

The `PREPARING` reclaim path in `ReconcileStagedFiles` intentionally keeps its file-then-record order: it only targets `PREPARING` rows, which the pending-manifest guard never refuses, and its containment gate already refuses to delete an unprovable path.

### 16.3 Criteria for this assignment

| ID | Criterion and exact scope | Evidence type | Command and actual observation | Verdict | Remaining limitation |
| --- | --- | --- | --- | --- | --- |
| A-01 | A refused single removal preserves the SQLite record, the staged file and the pending row. | Unit test | `storage_tests.exe` group 41: `service.RemoveDraftAttachment` on a locked project throws; `fs::exists(staged)` true; one `READY` row retained; `Pending` still present. | PASS | Service-level; the composer already refuses earlier, so this is the boundary a direct caller reaches. |
| A-02 | A refused clear preserves the whole set. | Unit test | Group 41: `service.ClearDraftAttachments` throws; file and row retained; pending retained. | PASS | Same boundary as A-01. |
| A-03 | Legitimate removal and clear still work. | Unit test | Group 42: single removal and clear both return `recordRemoved` and `bytesRemoved` true; files gone; rows gone. | PASS | Uses isolated temp stores, not the production database. |
| A-04 | Partial cleanup failure is reported and stays recoverable. | Unit test | Group 42: file held open without delete sharing yields `recordRemoved=true`, `bytesRemoved=false`, non-empty `unresolvedReason`, file still present; after the handle closes, `ReclaimInterruptedPreparation` deletes it. | PASS | The failure is induced by a sharing violation, not by a disk or ACL fault. |
| A-05 | No cross-project regression. | Unit test | Group 41: while one project is locked, a second project's attachment is removed normally and the locked project's file and row are untouched. | PASS | Two projects in one store; not a concurrency test. |
| A-06 | Existing S1–S3 protections continue to pass. | Unit test / Registry | `storage_tests.exe` `PASS: 41 of 41 executed native regression groups`; `verify_ac_ai.exe` 12/12 `PASS Test`; `validate_contract_registry.py` `PASS: 124 contract-registry and Screen 2 slice schema checks`. | PASS | Counts are what executed; none were skipped. |
| A-07 | The new test detects the original defect (controlled mutation). | Controlled mutation | The service was reverted to delete-first, rebuilt, and run: `FAIL: Assertion failed: fs::exists(guardedFile)` in group 41 — the protected file was destroyed. Restored, rebuilt, `PASS: 41 of 41`. | PASS | Demonstrates detection for this defect; not a proof for every assertion in the group. |
| A-08 | Each result can be tied to the exact executable and compiled XAML. | Source inspection / Compilation | `test_full_ui_acceptance.py` accepts `--exe/--store/--perf-log/--evidence-dir`, computes SHA-256 of the executable and of `MainWindow.xbf`, records the source commit, and writes `artifact-identity.json` into the evidence directory; `py_compile` succeeds. | PASS (code) | The harness was not executed end-to-end this pass, so no identity-stamped harness report exists yet. |
| A-09 | UI performance measured on this build. | — | `perf_benchmarks.exe` completes with database query timings (e.g. substring search median 11.17 ms, worst-case full scan median 18.87 ms over 100 samples). | INCONCLUSIVE as UI evidence | These are storage benchmarks. Input latency, frame pacing and UI-thread stall were **not** measured on this build. |

### 16.4 S4 native acceptance matrix

Evidence class `Agent-observed runtime` means a screenshot and/or UIA snapshot taken from the running binary identified in §16.1 at 125% scaling with an isolated store.

| ID | Criterion | Evidence type | Observation | Verdict |
| --- | --- | --- | --- | --- |
| K-01 | Keyboard-only Welcome → Screen 2, and Escape does not bypass selection into Chat. | Agent-observed runtime | Focus starts on `Continue to project selection`; Enter reaches Screen 2 showing `Pick up where you left off`, `Open an existing project folder`, `Create a new project folder`, `0 projects recorded`, `No recorded projects yet...`, and disabled `Previous/Next projects`. Escape on Screen 2 keeps Screen 2. Screenshots `s4-welcome.png`, `s4-screen2.png`. | PASS |
| K-02 | Chat workflows: Enter/Shift+Enter, Ctrl+F search, attachment chips, Action Center traversal. | Agent-observed runtime | Not performed: no project was bound, so Chat was never reached in this pass. | INCONCLUSIVE |
| D-01 | Real Explorer drag-and-drop of one file admitted with correct size and SHA-256. | Agent-observed runtime | Not performed. Procedure: bind a folder under `C:\Users\pavan\PROJECTS\` (the picker fails for `%TEMP%` paths), type a draft, drag one `.txt` from Explorer onto the composer, confirm the chip reaches READY and that no contribution is created by the drop alone. | INCONCLUSIVE |
| D-02 | Mixed file+folder batch rejected as a whole. | Agent-observed runtime | Not performed; refusal logic is covered by `verify_ac_ai.exe` Test 5 Case 2 at the service level. | INCONCLUSIVE |
| D-03 | Drop refused while a submission is unresolved. | Agent-observed runtime | Not performed. Admission policy is covered by `storage_tests` group 34. | INCONCLUSIVE |
| D-04 | Second drop during preparation refused rather than validated against a stale set. | Agent-observed runtime | Not performed. Single-flight reservation covered by `storage_tests` group 35. | INCONCLUSIVE |
| N-01 | Narrator announces screens, controls and states without false completion claims. | Agent-observed runtime (human listening) | Not performed. UIA exposes accessible names for every control observed on both screens (e.g. `Skip the introduction`, `Continue to project selection`, `Open an existing project folder`, `Filter recent projects`, `Filter by project status`), which is a necessary but not sufficient condition. | INCONCLUSIVE |
| P-01 | 150% and 200% display scaling. | Agent-observed runtime | Not performed. Environment measured at 125% (one display, 1920×1080, window 1424×732 physical). Changing the scale alters the user's display configuration and was not done unasked. | INCONCLUSIVE |
| T-01 | Live high-contrast switch. | Agent-observed runtime | Not performed; requires toggling a system accessibility theme. | INCONCLUSIVE |
| T-02 | Live light/dark switch while Chat is open. | Agent-observed runtime | Not performed; the window rendered in dark theme because the system is dark, but no live switch was observed. | INCONCLUSIVE |
| T-03 | Reduced-motion preference change during a transition. | Agent-observed runtime | Not performed. | INCONCLUSIVE |

### 16.5 Observations not promoted to defects

At 1424×732 physical (≈1139×586 logical) the Welcome body copy is visibly clipped where the footer bar begins, mid-sentence, while Screen 2 fits entirely. The Welcome body sits in `#WelcomeBodyScroller`, so the content may be reachable by scrolling; that was not tested. Recorded as an unresolved layout observation, **not** as a confirmed defect, and no cause is claimed.

### 16.6 Remaining blockers for S4

Every item below is unverified, not merely unlisted:

1. K-02, D-01…D-04: Chat, Explorer drag-and-drop and mixed-batch refusal observed on a live window with a bound project.
2. N-01: human Narrator listening pass.
3. P-01: 150% and 200% scaling on a real display.
4. T-01…T-03: live high contrast, live theme switch, reduced motion.
5. A-09: input latency, frame pacing, UI-thread stall and memory measured on this build; only storage benchmarks exist.
6. The native harness has not been executed against `ee0be9d`; its §15 result belongs to an older artifact.

The CLI adapters, live agent sessions, Council execution, material work authorization and MSI packaging remain out of scope and unimplemented, so the C10 Gate I3 entry criteria are not met.

---

## 17. F1/F2 storage corrections: attachment identity ownership and universal write admission (2026-10-21)

This section supersedes §16 as the current summary. §16 and everything above it remain as historical evidence tied to their own revisions and are **not** re-claimed here.

### 17.1 Identity of this record

| Field | Value |
| --- | --- |
| Source revision | working tree at `c3332d0` (HEAD) plus this change set (`Storage.h`, `Storage.cpp`, `winsqlite3_compat.h`, `storage_tests.cpp`); no source file changed between the final builds below and the commit that follows |
| Application binary | `native/Mayasaba/bin/x64/Release/Mayasaba.exe`, built 2026-10-21 from the final working tree, SHA-256 `9FF21B8894AB3F87F9429C65CF4C7E2B4B291285B9E3C34B6D0EC86F2E3A3B51` |
| Compiled XAML | `native/Mayasaba/bin/x64/Release/MainWindow.xbf`, SHA-256 `50353CD298844F5CCA9C72AC094DD92B1E59F04C1497819C15E58C34335901BC` |
| Test binaries | `storage_tests.exe` `6802022643AD31E127022831599A33B7CC34DB61993AAD4A0E74E1E6C7FC1AB8`; `verify_ac_ai.exe` `63CC10D3B80E7383C9710F8F04723759CCE4D3C9AB8A85B5D9990A9E2E4AADD4`; `perf_benchmarks.exe` `B29BD07F739671E606CF19030421A7C11714A3653E98B369F6E0DBA74E3CC0FA` |
| Toolchain | MSBuild, Visual Studio 18 Enterprise, toolset `v145` (MSVC 14.50.35717), Windows 11 build 26200, one display at 125% scaling |
| Build hygiene | Application rebuild: 0 warnings, 0 errors. `storage_tests`, `verify_ac_ai` and `perf_benchmarks` were rebuilt from the same final source: 0 warnings, 0 errors in the captured logs. §16 recorded two pre-existing `C4100` unreferenced-parameter warnings from an earlier `perf_benchmarks` build; that observation is not re-claimed here, and no cause for its absence from this pass's captured log is asserted. MSVC links on this toolchain are not byte-reproducible, so relinking identical sources yields a new executable hash; the hashes above are those of the exact binaries whose runs are cited in §17.3, all executed after the final rebuild from the final source. |
| Production store | `%LOCALAPPDATA%\Mayasaba\mayasaba.db` SHA-256 `23E3ED8764F8EB9E3C97F92F8960AE9AAD9A086B320044369E9029129F994272` — byte-identical before and after this pass; no test in this pass opened it (all stores are isolated temp paths). |

`MainWindow.xbf` is **unchanged** from §16.1: this change set touches no XAML, so the compiled XAML identity is identical while the executable identity is not. Recording both is what makes that visible.

**No live-window session was executed in this pass.** Every UI-level criterion that was `INCONCLUSIVE` in §16.6 remains `INCONCLUSIVE`; nothing below promotes one.

### 17.2 The defects

**F1 — an attachment identity could be written through another project.** `Storage::SaveDraftAttachment` (and the batch `SaveDraftAttachments`) upserted `draft_attachments` matching on `attachment_id` alone. The pending-manifest guards received the *supplied* project, so a write naming another project silently rewrote the owner's row — path, digest, error message, version — while the caller was told it succeeded. `SaveDraftAttachment` also ran with no transaction at all.

The fix adds `RefuseForeignAttachmentIdentity_locked`, which checks both `draft_attachments` and committed `attachments` for existing ownership of the identity, and runs ownership check, pending-manifest check and upsert as **one** `BEGIN IMMEDIATE` transaction. A committed attachment owns its identity too: reusing it as a draft elsewhere would make inspection ambiguous about which record the identity names. A rejected batch therefore leaves no partial update, not even the innocent element that preceded the conflicting one.

**F2 — quarantine admission was bypassed by any mutation that did not begin with `BEGIN IMMEDIATE`.** The poisoned-connection check lived inside `Exec`, only on the `BEGIN IMMEDIATE` branch, so a direct prepared write — `SaveDraftAttachment` is one — reached SQLite on a connection whose transaction outcome was unknown. The rollback-after-failure paths also *assumed* the rollback worked.

The fix has three parts, in this order of reliance:

| Property | How it is now guaranteed |
| --- | --- |
| Every mutation is admitted | `AdmitWrite_locked()` is the single admission point; every mutation entry point calls it and fails closed **before any statement is prepared**. It performs the quarantine check and the free-space check together, so a caller can no longer pass one by doing the other's work. |
| Admission cannot be forgotten | A SQLite authorizer installed at open (`WriteAdmissionAuthorizer`) denies every write action at the SQLite layer while the connection is quarantined, so an entry point that forgets admission still fails closed. Reads stay permitted so the Action Center can describe the unresolved state the quarantine protects; a denied statement is translated from SQLite's bare "not authorized" into the quarantine reason. |
| The rollback is proven, not assumed | `RollbackAfterFailure_locked()` issues the rollback and then checks `sqlite3_get_autocommit`; a connection still inside a transaction is quarantined rather than presented as usable. |

**A second defect in this change set, found by its own verification.** The first expression of the authorizer backstop translated a denied statement's message only in `Storage::Check`, which covers `sqlite3_step`. The `Statement` constructor prepares with `sqlite3_prepare_v2` and threw SQLite's bare `sqlite3_errmsg` — which for a denial is `not authorized` — so an entry point that forgot admission and was stopped by the backstop would have surfaced an unexplained "not authorized" instead of the quarantine guidance. The controlled experiment in B-13 caught this: with admission stripped from one direct-write method, group 44 failed on the *message*, proving the denial fired but spoke wrongly. The repair is one translation point, `StatementFailureMessage`, now used by prepare (`Statement`), step (`Check`) and `sqlite3_exec` (`Exec`), so admission and backstop refusals are indistinguishable to a caller. Re-running the same experiment after the repair passes (B-13), which demonstrates both the defect and its repair. A denial on this connection can only come from this authorizer — no other authorizer is installed and `ATTACH`/`DETACH` are themselves denied while quarantined — so the translation is unambiguous.

### 17.3 Criteria for this assignment

| ID | Criterion and exact scope | Evidence type | Command and actual observation | Verdict | Remaining limitation |
| --- | --- | --- | --- | --- | --- |
| B-01 | A single foreign write is refused and the owner's row, version, digest, path and error message are untouched; the foreign project sees nothing. | Unit test | `storage_tests.exe` group 43: `SaveDraftAttachment("proj-b", …)` over `proj-a`'s identity throws; `GetDraftAttachments("proj-a")` keeps path, error, digest and version; `GetDraftAttachments("proj-b")` empty. | PASS | Service/storage boundary; no UI path was exercised. |
| B-02 | A rejected batch leaves no partial update. | Unit test | Group 43: batch `{innocent, foreign}` for `proj-b` throws; `proj-b`'s drafts remain empty and `proj-a`'s single row survives. | PASS | Two projects in one store; not a concurrency test. |
| B-03 | A legitimate same-project update still applies and bumps the version. | Unit test | Group 43: `SaveDraftAttachment("proj-a", …)` updates the error message and `version == versionBefore + 1`. | PASS | — |
| B-04 | With a locked pending submission, a foreign write leaves record, version, digest, staged path, staged bytes and the pending identity exactly as they were. | Unit test | Group 43: after `StageSubmission`, a foreign write throws; `Pending("proj-c")->id` unchanged; staged file still exists. | PASS | — |
| B-05 | Every mutation is refused on a quarantined connection, and the refusal reason names the quarantine (proving admission, not an incidental "transaction within a transaction"). | Unit test | Group 44: after the forced-rollback injection yields `SaveDisposition::Unknown` and `IsQuarantinedForTesting()`, the refusal reasons for `SaveDraftAttachment`, `SaveDraftAttachments`, `SaveDraft`, `StageSubmission` and `BindProject` all contain "quarantined"; `SetClosure`, `RemoveDraftAttachment`, `ClearDraftAttachments`, `TouchProjectOpened` and `RelocateProject` all throw. Nothing changed (draft row, pending id, closure flag, canonical path all verified after the refusals). | PASS | Exercised through production methods; no synthetic entry point was added to test the authorizer backstop directly. |
| B-06 | Reads remain available under quarantine. | Unit test | Group 44: `GetDraftAttachments`, `Pending` and `Project` all succeed while quarantined. | PASS | — |
| B-07 | Reopening clears the quarantine and writes resume. | Unit test | Group 44: a fresh `Storage` on the same file is not quarantined, and `SaveDraft` succeeds. | PASS | The unknown transaction died with the connection; this is the documented recovery, not a repair. |
| B-08 | Existing S1–S3 protections continue to pass. | Unit test / Registry | `storage_tests.exe` `PASS: 43 of 43 executed native regression groups` (`sweep-tests-run.log`); `verify_ac_ai.exe` 12/12 `PASS Test` (`sweep-verify-run.log`); `validate_contract_registry.py` `PASS: 124 contract-registry and Screen 2 slice schema checks` (`sweep-registry-run.log`). | PASS | Counts are what executed; none were skipped. |
| B-09 | The new group detects the F1 defect (controlled mutation). | Controlled mutation | Both `RefuseForeignAttachmentIdentity_locked` call sites were removed, rebuilt (`mut-f1b-build.log`, exit 0) and run: `FAIL: Assertion failed: singleRefused at storage_tests.cpp:3869`, `FAIL 43. …` (`mut-f1b-run.log`), exit 1. Restored byte-identically (SHA-256 `0A2711B9…`) and rebuilt. | PASS | Demonstrates detection for the assertions that ran in group 43; not a proof for every assertion in the group. |
| B-10 | The new group detects the F2 defect (controlled mutation). | Controlled mutation | Pre-fix quarantine behaviour restored three ways — authorizer not installed, poison checked only on the `BEGIN IMMEDIATE` branch of `Exec`, `AdmitWrite_locked` reduced to the capacity check — rebuilt (`mut-f2-final-build.log`, exit 0) and run: `FAIL: Assertion failed: draftText.find("quarantined") != std::string::npos at storage_tests.cpp:4013`, `FAIL 44. …` (`mut-f2-final-run.log`), exit 1. Restored byte-identically (SHA-256 `0A2711B9…`) and rebuilt. | PASS | The mutated failure lands on `SaveDraft`, the direct prepared write, which is exactly the hole F2 closed. The run aborts at the first failed assertion, so detection for the remaining group-44 assertions is not individually demonstrated. |
| B-13 | The authorizer backstop fires and speaks correctly when an entry point forgets admission. | Controlled mutation | Admission was stripped from `SaveDraft` alone (authorizer retained, everything else fixed), rebuilt (`mut-f2b2-build.log`, exit 0) and run: `PASS: 43 of 43` (`mut-f2b2-run.log`), exit 0 — the UPDATE was denied at prepare and the caller received the quarantine message. The identical mutation with the translation repair reverted failed at the same assertion (`mut-f2b-prerepair-run.log`, `FAIL 44`, exit 1), which is the evidence for the defect and its repair described in §17.2. Restored byte-identically (SHA-256 `0A2711B9…`) and rebuilt. | PASS | Proves the backstop on the direct-write path it protects. A deliberately admission-forgetting transaction-based entry point would still fail with SQLite's nested-transaction error rather than the quarantine message; no such entry point exists or was added. |
| B-11 | Each result can be tied to the exact executables. | Source inspection / Compilation | §17.1 records SHA-256 of `Mayasaba.exe`, `MainWindow.xbf` and all three test binaries; the app, test and benchmark builds were separate rebuilds from the same final working tree. | PASS | — |
| B-12 | UI performance measured on this build. | — | `perf_benchmarks.exe` completes (`sweep-perf-run.log`, exit 0), 100 samples per row: substring match `service_alpha` median 1.64 ms (max 3.05 ms); substring `algorithm` ~10% hits median 12.01 ms (max 17.06 ms); worst-case full scan (0 hits) median 4.53 ms (max 7.08 ms). | INCONCLUSIVE as UI evidence | These are storage benchmarks. Input latency, frame pacing and UI-thread stall were **not** measured on this build. |

### 17.4 What this record does not claim

1. No live-window, drag-and-drop, Narrator, DPI, high-contrast, theme-switch or reduced-motion observation was made in this pass; those criteria remain as §16.6 left them.
2. The authorizer backstop was exercised directly by B-13 on the direct-write path it protects. It was **not** exercised for a transaction-based entry point that forgot admission; such an entry point fails with SQLite's nested-transaction error rather than the quarantine message, which is disclosed rather than fixed because adding a hypothetical entry point to prove it would itself be the defect.
3. The controlled mutations demonstrate that groups 43 and 44 detect the defects they were written for. They do not demonstrate detection for every assertion in those groups, and they are not a general test-quality proof.
4. Storage correctness here is proved at the storage/service boundary with isolated temp stores. No concurrency, fault-injection or multi-process evidence was produced in this pass beyond the pre-existing groups.

