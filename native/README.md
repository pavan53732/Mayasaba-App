# Mayasaba — real native Windows implementation

> **2026-10-15 project-selector amendment:** [Approved Screen 2 contract](../docs/PROJECT-SELECTION.md) replaces automatic last-project Chat startup with explicit selection on later launches, adds recent projects and explicit empty-folder creation, and routes File → Switch Project through the selector.
>
> **2026-10-16 three-screen refinement:** the Welcome, Project selection and Chat screens and their presentation wiring have native implementation and recorded local observations — see [the validation record](LOCAL-VALIDATION.md) for the artifacts, observations and UX-01–UX-32 matrix, including incomplete theme, accessibility, size/DPI and performance checks. The [three-screen guide](../docs/THREE-SCREEN-UX-IMPLEMENTATION.md) defines the required scope; source presence or fixture rendering does not close every criterion. New native units: `Motion.*` (motion and elevation), `RichText.*` (bounded reply renderer), `Timeline.*` (typed projections and the virtualized timeline), `Fixtures.*` (opt-in test fixture harness). `tests/seed_store.*` seeds an isolated store for UI verification and refuses the production store path.

**Implementation status: INCOMPLETE.** This directory contains a **real native C++20 / C++/WinRT / WinUI 3 unpackaged application starting slice**, not a renamed browser preview. Windows App SDK 2.5.1 and Microsoft.Windows.CppWinRT 3.0.260818.1 are pinned in the `.vcxproj`. Initial x64 support targets the previously approved Windows 11 system class. A local Release x64 build and native Welcome/Open Folder launch were verified on 2026-10-14 using VS 18 Enterprise with the v145 toolset. See [local validation](LOCAL-VALIDATION.md) for commands, evidence and outstanding runtime checks. This does not establish full application acceptance.

## What's implemented in this slice

- Native XAML Welcome → required Open Folder → Chat. Welcome contains **no composer**, and neither does the Open Folder screen: the single composer appears in Chat once a project root is bound. The pre-binding draft composer was removed on 2026-10-17 at the user's direction.
- Real Windows folder-picker API, initialized with the native window handle, used for first selection and `File → Switch Project` (outside Chat). Cancellation never changes the active project.
- Local fixed-drive ordinary-directory root validation, native volume/file identity and canonical path, with no file scan, Git operation or provider invocation from binding.
- Windows per-user SQLite store using the OS `winsqlite3` library, a single-writer instance mutex, WAL and `synchronous=FULL`. Project identity and version-guarded closed/reopened flag, per-project unsent drafts, and immutable keyed contributions are persisted.
- Exactly one native Chat composer moves from the empty center to a docked position once history is confirmed. Restoration shows project-specific history; no invented agent reply is displayed.
- Persistent Start/Pause/Stop header controls and visible per-CLI `UNSUPPORTED` readiness, with truthful explanations instead of simulated native CLI actions. Exact standalone slash controls are recognized but **not dispatched** until the adapters are proven safe.
- Dimensional presentation across all three screens: three-plate sculpted mark at negative Z under a 900-depth perspective, face light and contact shadows, a raised composer plate, and the ten named motions from `Motion.h`. Depth, elevation and ambient loops are presentation only; they never carry status, authority or domain state. See [LOCAL-VALIDATION.md](LOCAL-VALIDATION.md#dimensional-visual-system--native-implementation-and-verification-2026-10-18) for the criterion verdicts and evidence.

### Fixture harness

`MAYASABA_UI_FIXTURES=mixed` (with an isolated `MAYASABA_STORE_PATH`) renders the reply, running-task, blocked and unknown presentation states so the renderer can be exercised without any agent runtime. The cards carry a visible fixture banner, are inserted above restored history (so in a project with history they sit at the top of the timeline), are never written to the store, and are never read by ordinary startup. A fixture proves the renderer and its controls only; it is not evidence of live agent work.

## Still missing — NOT an end-to-end application

The three-screen UI slice is implemented; the following runtime dependencies are not, so
the **live three-agent application workflow is blocked**. Nothing in the UI pretends
otherwise: every dependent surface shows a precise unavailable or waiting state, and the
fixture harness that exercises reply/work states is opt-in and clearly labelled.

- Native Hermes/Kilo Code/Claude Code capability probes, persistent CLI supervision, per-operation confinement, effective descendant/service revocation, automatic project-safe Start/Stop, safe handoff and real controller session evidence.
- Full approved `SubmitUserContribution` schema/owner/event/outbox/idempotency receipt and complete unknown-commit reconciliation with durable original submission identity. The 2026-10-17 attachment milestone adds native selection, bounded preparation/hash checks, persisted draft/submission attachments and timeline chips; its reported tests do not by themselves establish every draft-protection, authorization or recovery requirement.
- Three-agent reply orchestration and exact peer reviews, FULL council execution, approved requirement/task/lease machines, independent verification, guarded publication, backup/retention and user-governed MSI activation.
- Full accessibility/UI Automation acceptance, packaging as a signed MSI, system matrix and all physical Windows tests. A successful compile, when achieved, will still not establish any of these.

**No material CLI work is launched** by this slice. Buttons are intentionally fail-closed because the repository's actual native confinement/adapter proofs have not been implemented.

## Windows build

On an eligible Windows x64 developer machine, install VS 2022 Desktop development with C++, WinUI 3/Windows application tooling and Windows SDK 10.0.26100.0. From a VS Developer PowerShell:

```powershell
msbuild native/Mayasaba/Mayasaba.vcxproj /restore /p:Configuration=Release /p:Platform=x64
```

The local VS 2022 BuildTools installation has v143 but lacks the Windows/XAML targets. On this machine, use VS 18 Enterprise MSBuild with `/p:PlatformToolset=v145`; this is an explicit build override and leaves the project default at v143. Do not remove `ApplicationType` to bypass missing targets: doing so skips required XAML compilation. Deployment of this unpackaged self-contained app via a user-approved MSI is future work, **not** a completed installer.

Verified working invocation on this machine (Git Bash; `MSYS2_ARG_CONV_EXCL='*'` keeps the `/p:` switches intact, and the lowercase `*_proxy` duplicates must be unset because Windows environment keys are case-insensitive and CL.exe then fails with `MSB6001`):

```bash
export MSYS2_ARG_CONV_EXCL='*' && unset https_proxy http_proxy
mb=(/c/Program\ Files/Microsoft\ Visual\ Studio/18/Enterprise/M*/Current/Bin/M*.exe)
"${mb[0]}" native/Mayasaba/Mayasaba.vcxproj /restore /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /v:quiet /nologo "/flp:logfile=native-build.log;verbosity=normal"
```

Last build from this command: exit 0, 0 warnings, 0 errors, Release x64, output `native/Mayasaba/bin/x64/Release/Mayasaba.exe`. See [local validation](LOCAL-VALIDATION.md) for executable hashes and runtime evidence.

## Source-of-truth and safety

Read [AGENTS.md](../AGENTS.md), [the complete system description](../docs/Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md), [C06 decisions](../docs/contracts/C06-BINDING-PRODUCT-DECISIONS.md) and [C10 entry gate](../docs/contracts/C10-IMPLEMENTATION-ENTRY-GATE.md). Existing 13 layers and 12 authoritative state machines remain binding. Native implementation code does not override their safety rules or permit unproven action.

**Correctness caveats still to resolve:** The current UI implementation synchronously accesses local SQLite and so has not proven nonblocking behavior under a five-second busy limit; interrupted/unknown command identity reconciliation has not been integrated end-to-end; project root alias and shared-service isolation require adversarial Windows proof. Treat those as blockers, not as completed invariants.
