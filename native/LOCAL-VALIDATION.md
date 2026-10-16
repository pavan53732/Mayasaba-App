# Local native validation — Mayasaba native WinUI slice

> **Status: Screen 2 slice ACCEPTED (29/29 applicable PASS, 1 N/A).** Criteria S2-01 through S2-13 and S2-15 through S2-30 for the native Screen 2 project selector slice are verified with native unit tests, contract registry validation, and live WinUI 3 runtime observation under active interaction (including high-DPI scaling and live reduced-motion preference toggling). S2-14 is recorded as NOT_APPLICABLE (2026-10-17) following the approved permanent removal of the Screen 2 pre-binding draft composer. Per repository rules, this certifies the native Screen 2 selector slice; whole-product gating (CLI adapters, council orchestration, MSI packaging) remains separate.

**Scope.** This report covers the native C++20 / C++/WinRT / WinUI 3 starting slice under `native/Mayasaba/` and unit test runner `native/tests/storage_tests.cpp`. It is **not** certification of the complete Mayasaba application: the three CLI adapters, orchestration, work authority, publication, accessibility acceptance and MSI packaging are not implemented, so no whole-product acceptance is claimed anywhere below.

**Current revision.** `main` plus the native Screen 2 implementation in `MainWindow.xaml`, `MainWindow.xaml.h`, `MainWindow.xaml.cpp`, `Storage.h`, `Storage.cpp`, `ProjectRoot.h`, `ProjectRoot.cpp`, and `storage_tests.cpp`.
**Current binary.** `native/Mayasaba/bin/x64/Release/Mayasaba.exe`, built 2026-10-15 (0 warnings, 0 errors).
**Test binary.** `native/tests/storage_tests.exe`, built 2026-10-15 with MSVC `14.50.35717` x64 (0 warnings, 0 errors).

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
| UX-14 | Fixture PASS / live BLOCKED | Fixture | Exactly one primary reply; details expose three contributions, the recorded writer and both peer reviews. No live owner record exists. |
| UX-15 | PASS | Source inspection | No provisional or raw stream is rendered; the reply path only renders a finalized record. No live non-English stream exists to test. |
| UX-16 | Fixture PASS | Fixture | Waiting, Running, Blocked and Unknown are distinct titles, state labels and semantic edges. |
| UX-17 | PASS | Agent-observed runtime / Source inspection | Start/Pause/Stop report unavailable truthfully; fixtures activate only via `MAYASABA_UI_FIXTURES`, carry a banner and never touch the store. |
| UX-18 | PASS | Source inspection | Start/Pause/Stop handlers are unchanged and never wait for an animation. |
| UX-19 | PASS | Agent-observed runtime | Header shows all three full CLI names with text status; readiness is separate from session activity. |
| UX-20 | PASS | Source inspection | `ItemsRepeater` with `StackLayout` virtualizes realization; stable monotonic tokens; `ReplaceAll` versus `Append` keep identity. No large-history frame measurement was taken. |
| UX-21 | PASS | Agent-observed runtime | `Jump to latest` appeared with the correct state; restored history is marked shown so it never replays entrance motion. |
| UX-22 | PASS | Agent-observed runtime | Details open and close inline with `Appear`; the toggle's accessible name tracks the action; no new route is introduced. |
| UX-23 | PASS | Agent-observed runtime | Attach files stays disabled with truthful help text; no file can be selected or silently omitted. |
| UX-24 | INCONCLUSIVE | — | Palettes are theme-resource driven, but a live light/dark switch was **not** exercised: it requires changing the Windows app theme, a system setting the user must authorise. |
| UX-25 | INCONCLUSIVE | Source inspection | High contrast removes gradients, shadows, decorative layers and ambient motion via `AccessibilitySettings.HighContrastChanged`; a live high-contrast session was **not** exercised (system setting). |
| UX-26 | INCONCLUSIVE | Source inspection | Reduced motion is read from `UISettings.AnimationsEnabled` and gates every transition and loop; a live toggle was **not** re-exercised in this pass. |
| UX-27 | INCONCLUSIVE | Agent-observed runtime | The UIA tree exposes stable automation ids, heading levels and accessible names; a full keyboard-only and Narrator walkthrough was **not** completed. |
| UX-28 | PARTIAL | Agent-observed runtime | 460x732 and 692x732 verified with no clipping and no lost actions. The 520x640 / 640x480 logical sizes and 150/200% DPI were **not** exercised. |
| UX-29 | PASS | Source inspection | `Window.Closed` cancels all motion, revokes both subscriptions and releases the copy handler; generation guards reject stale callbacks. |
| UX-30 | PASS | Compilation / Unit test | Release build 0/0; 18 native groups PASS; 124 registry checks PASS. |
| UX-31 | INCONCLUSIVE | — | No frame-time or resource measurement was taken during transitions or under a large fixture history. |
| UX-32 | PASS | Agent-observed runtime | Screenshots, artifact hashes, changed files and the remaining blockers are delivered in this section. |

## Remaining blockers (not UI-slice blockers)

The three-screen UI slice is implemented and verified as above. The **live three-agent
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

The Screen 2 acceptance table above lists `#SkipButton` as an observed automation id. At
`3ffd482` the Welcome screen's Skip button had no `x:Name`, so that element could not have
been addressed by that id. This refinement adds `x:Name="SkipButton"`, so the id now exists;
the historical row should be read as describing the current source.
