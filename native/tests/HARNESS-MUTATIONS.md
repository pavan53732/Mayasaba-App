# Harness mutation checks

`run_harness_mutations.py` maintains the five discrimination checks previously
run from the untracked `scratch/test_mutation.py`. It checks uncertain-selection
cleanup, verified return before cleanup, FirstRun Escape, the primary Disabled
visual state and FirstRun/Switching action dispatch.

Run on Windows using Python 3.12 or later with pywinauto and Pillow installed:

```powershell
& 'C:\Users\pavan\.claude\skills\computer-use\.venv\Scripts\python.exe' `
  native/tests/run_harness_mutations.py `
  --output-dir native/tests/evidence/harness-mutations-NEW-RUN
```

Choose a new output directory for every run. `--source-root` defaults to this
script's repository; `--revision` defaults to HEAD. The runner deliberately tests
**committed source**, excluding uncommitted changes, binaries, scratch files and
production data. It records the resolved commit and source hashes in summary.json.

Git exports that revision into the evidence directory. All five anchors are
validated before mutations begin. Each case requires a clean pass, a mutant
exit of 1 with its expected assertion, a restored pass and byte-identical source
restoration. Missing dependencies, timeouts, import failures and unrelated
assertions cannot count as successful discrimination. Each child uses a new
bytecode-cache lookup prefix to prevent same-size/same-timestamp source swaps
from loading a stale mutant. The original checkout is never mutated.

Per-case clean/mutant/restored logs, source.tar, snapshot and summary.json remain
in the run directory. Existing output paths are refused. Child tests have a
bounded timeout, and cleanup restores the snapshot on failure. An anchor change
requires reviewing the affected check rather than silently skipping it.

These are Python helper and source-structure tests. They do not compile C++,
launch WinUI, exercise real Explorer drops or establish live three-agent proof.
Keep native interaction acceptance and physical P01–P06 separate.

Historical scripts and their disposition are documented in
`historical/scratch/README.md`. Original validation entries remain historical;
use the maintained runner for new execution evidence.
