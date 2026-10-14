# Local native validation — 2026-10-14

This report covers the native UI, motion and build-fix batch validated on 2026-10-14. It is not certification of the complete Mayasaba application.

## Environment and command

- Windows display version 25H2, build 26200.9457; x64 build target.
- VS 18 Enterprise MSBuild, MSVC 14.50.35717 (`v145` override).
- Windows SDK 10.0.26100.0.
- Project default remains `v143`. This machine's VS 2022 BuildTools installation lacks the Windows/XAML targets; its presence alone is insufficient.

Successful initial Release build and resource-fix rebuild:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe' native/Mayasaba/Mayasaba.vcxproj /restore /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /v:quiet /nologo '/flp:logfile=native-build.log;verbosity=normal'
```

The final UTF-8 rebuild uses the same compiler with `/p:OutDir=C:\Users\pavan\PROJECTS\Mayasaba-App\native\Mayasaba\bin\x64\Validation\` so the currently running Release executable is not overwritten. Build output and generated sources are ignored by Git.

## Findings and fixes

- Restored the exact pinned packages and their transitive dependencies.
- Kept the required Windows/XAML target declaration. A diagnostic desktop-only override skipped XAML generation and was reverted.
- Declared output and generated-code paths before imported package properties, with a shared XAML/C++ generated directory.
- Actual native launch initially crashed. The crash dump contained `Cannot find a Resource with the Name/Key TabViewButtonBackground`. Merging `XamlControlsResources` into App.xaml resolved startup; the native welcome screen then rendered.
- Observed garbled C++ title punctuation; added `/utf-8`. Runtime confirmation of the corrected title remains pending on the final binary.
- Temporary startup diagnostic message boxes were removed after diagnosis.

## Acceptance evidence matrix

| Criterion | Result | Evidence / limit |
| --- | --- | --- |
| Dependency restoration | PASS | Native MSBuild restore completed. |
| Release x64 compilation and linking after resource fix | PASS | Exit 0; 0 warnings and 0 errors. |
| Final UTF-8 source compilation and linking | PASS | Validation output build exited 0, with 0 warnings and 0 errors; this binary has not yet been launched. |
| Native window opens and renders | PASS | Computer Use observed the welcome screen and its accessibility tree. |
| Welcome advances to Open Folder | PASS | Next displayed FolderScreen with draft editor and disabled pre-binding Send. |
| Native folder picker opens | PASS | Observed Windows Select Folder dialog. |
| Folder selection opens Chat | PASS | User explicitly reported successfully selecting a folder and reaching Chat on 2026-10-14; not independently observed by the desktop tool. |
| Initial welcome entrance timing | INCONCLUSIVE | A settled screenshot cannot prove the initial transition. |
| Restored history versus new-entry animation | INCONCLUSIVE | User confirmed reaching Chat, but history restoration and new-entry animation have not been exercised and observed. |
| Live reduced motion | BLOCKED | Permission requested to toggle Windows Animation effects and restore its original value; no setting changed. |
| Queued callback behavior during close | INCONCLUSIVE | No controlled close-race observation was performed. |
| XML parsing and diff whitespace | PASS | Both XAML files parse; git diff --check passed. |
| Three-CLI operation and complete product acceptance | BLOCKED | Native adapters/orchestration and other documented implementation gates remain incomplete. |

Test folder created: `C:\Users\pavan\AppData\Local\Temp\Mayasaba-UI-validation-20261014`. The user subsequently reported successful folder selection and entry into Chat; the selected path was not independently verified. The assistant sent no application test messages. The app creates its local SQLite store during startup.

Final UTF-8 Validation executable SHA-256: `4666680CD19005B48DE06D05D837558A44AC4019FE2CFA02F49192E4CA12FF4B`. This identifies the compiled binary, not a runtime acceptance result.

## Dependency identities

SHA-256 of restored top-level NuGet packages:

| Package | SHA-256 |
| --- | --- |
| Microsoft.WindowsAppSDK 2.5.1 | `2E26DE1253E2E2A288A75387523E2F8B402C05F117D42013DF9875887A70933A` |
| Microsoft.Windows.CppWinRT 3.0.260818.1 | `A8993608AE9263A8288E1A1D9EE2684A5632F4ACC59E1E89DDD86B3B889A7367` |

NuGet's generated project.assets.json records resolved transitive identities. These hashes record the restored artifacts; they do not establish independent publisher verification or runtime safety.

The user authorized committing and pushing the complete batch to main after reporting the successful folder-to-Chat check. The remaining motion and product acceptance obligations above stay open.
