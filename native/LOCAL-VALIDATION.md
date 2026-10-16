# Local native validation — Mayasaba native WinUI slice

> **Status: Screen 2 slice ACCEPTED (30/30 PASS).** All thirty criteria S2-01 through S2-30 for the native Screen 2 project selector slice are verified with native unit tests, contract registry validation, and live WinUI 3 runtime observation under active interaction (including high-DPI scaling and live reduced-motion preference toggling). Per repository rules, this certifies the native Screen 2 selector slice; whole-product gating (CLI adapters, council orchestration, MSI packaging) remains separate.

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
| S2-14 | Prebinding draft survives Back/cancel/restart; empty-target transfer is exactly once; target-draft/pending-send collision preserves both and requires explicit disposition. | PASS | Unit test / Source inspection | `OnUsePrebindDraftAsync` uses one guarded atomic transfer via `TransferPrebindDraftToProject(id, true)` in a transaction with version increment; target-draft collision modal preserves both and requires explicit user choice. Failure leaves draft completely preserved. |
| S2-15 | Draft save/transfer failure or lost commit receipt never clears the only copy or submits text. | PASS | Unit test / Source inspection | `storage_tests.cpp` suite 7: `TransferPrebindDraftToProject` wraps version increment and draft update in `BEGIN IMMEDIATE ... COMMIT`. If any step fails or pending submission exists, transaction rolls back and prebinding draft is never cleared. |
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
