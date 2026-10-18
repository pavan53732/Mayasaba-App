# Local diagnostic helpers

These helpers are preserved from the native UX implementation work. They are
developer experiments, not product components or release acceptance gates.

- `diag_run.*`: debugger-assisted launch and stack inspection.
- `inspect_win.ps1`, `monitor_win.ps1`, `test_start_proc.ps1`,
  `test_uia_window.ps1`, `test_win32_window.ps1`: process/window observations.
- `resolve_symbol*.ps1`: symbol lookup for a specific historical address.
- `test_launch_cu.py`: an experiment using a separately installed computer-use script.
- `walkthrough_run.py`: partial UI smoke-test scaffolding using pywinauto.
- `perf_benchmarks.*`: isolated SQLite query measurements with a p95 limit;
  these do not measure frame presentation, typing latency or accessibility.

The diagnostic launchers contain machine-specific executable and test-store paths.
Review those paths and prepare an isolated store before invoking them. Do not point
them at production data. Some helpers focus windows or terminate the process they
launch; do not run them against an existing user session. Dependencies are not
installed automatically. The symbol lookup address is not valid for every build.

The walkthrough's printed PASS messages are not sufficient evidence: some steps
observe only status text or invoke an action without asserting the final focus,
scroll target, animation state or absence of every failure. Its sleeps do not
force a query-completion race. Full native acceptance still requires discriminating
assertions and recorded observations for those properties. The integration review
did not execute these interactive helpers; see `../LOCAL-VALIDATION.md`.
