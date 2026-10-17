# Local native validation — Mayasaba native WinUI slice

> **Documentation reconciliation, 2026-10-17:** this file contains reports from multiple implementation passes, not a fresh verification of every current source file. Read the dated scope, artifact identity and evidence class for each result. The [three-screen requirements](../docs/THREE-SCREEN-UX-IMPLEMENTATION.md) remain separate from the observations below. UX-24–UX-28 and UX-31 still have unexercised theme/accessibility/size/performance checks; fixture replies/work do not prove live CLI operation. The user reconfirmed that the composer belongs only in bound-project Chat. Removing draft entry does not authorize deleting any retained legacy draft data; its preservation/recovery requires separate evidence.

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
| UX-27 | INCONCLUSIVE | Agent-observed runtime | The UIA tree exposes stable automation ids, heading levels and accessible names; a full keyboard-only and Narrator walkthrough was **not** completed. |
| UX-28 | INCONCLUSIVE | Agent-observed runtime | Short/narrow extremes are now exercised: 460x732 and 692x732 previously, plus 2026-10-18 at a 720x416 logical client size (900x520 physical at 125% DPI) where the composer, Send, Start, Pause and Stop stayed reachable with no clipping. Exact 520x640 and 640x480 logical client sizes and 150/200% DPI scaling were **not** exercised: the session ran at 125% (`GetDpiForWindow` = 120) and changing DPI requires a sign-out. |
| UX-29 | PASS | Source inspection | `Window.Closed` cancels all motion, revokes both subscriptions and releases the copy handler; generation guards reject stale callbacks. |
| UX-30 | PASS | Compilation / Unit test | Release build 0/0; 20 native groups PASS (2026-10-18 rebuild, including group 23); 124 registry checks PASS. |
| UX-31 | INCONCLUSIVE | Agent-observed runtime | Coarse resource observations only (2026-10-18, `GetProcessTimes` over 10s windows on the built Release binary): visible Welcome hero with ambient motion 3.9% of one core, the same process on Project selection 0.8%, Welcome again 1.6%, and 0.0% when the app started with animation effects already off. One earlier run of the same script reported ~28%, so the figure is not yet stable enough to certify. No frame-time instrumentation (ETW/present statistics) exists, so transition smoothness is unmeasured. |
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
| Frame timing / present statistics | INCONCLUSIVE | No ETW or present-statistics instrumentation exists, so transition smoothness is not measured. |
| 150% and 200% DPI | INCONCLUSIVE | The session ran at 125%; changing DPI needs a sign-out that was not authorised. |
| Narrator speech walkthrough | INCONCLUSIVE | Accessible names, heading levels and automation ids were read from the UIA tree; Narrator speech itself was not driven. |
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
| D-12 | Frame timing and 150/200% DPI | INCONCLUSIVE | Not instrumented; DPI change needs a sign-out. |

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

### What this does not establish

The visual refinement is implemented and verified as a native presentation slice. It does not
implement, and must not be read as evidence for, the missing three-agent orchestration, task
leasing, validation, publication or the twelve proposed productivity capabilities. The live reply
and work rows above stay BLOCKED until those owner services exist.
