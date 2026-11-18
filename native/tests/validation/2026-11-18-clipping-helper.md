# Combined clipping helper: synchronization checkpoint

Date: 2026-11-18. Baseline: `0532fb7e6f45e1d2b24df85a95b6ebc37d6bef37`.

## Included scope

`test_full_ui_acceptance.py` adds rectangle intersection and permits the
reachability oracle to receive multiple clipping viewports. The oracle tests
the control against their shared visible area, rather than accepting unrelated
visible portions. Existing single-viewport calls remain supported.

`test_reachability_uses_combined_clipping_viewport` covers:

- Two separately sufficient viewports whose mutual visible width is only 10 px:
  rejected for a 200x40 px control requiring at least 40x20 px.
- Viewports with an empty shared area: rejected.
- Three clipping ancestors with a usable 160x30 px intersection: accepted, with
  the effective viewport and visible dimensions checked.

## Verification performed

Tests ran on a Windows source snapshot containing this checkpoint's two Python
files over the baseline. This avoids attributing concurrently edited runner
behavior to this checkpoint.

| Check | Interpreter / command | Result |
|---|---|---|
| Harness oracles | `C:\Users\pavan\.claude\skills\computer-use\.venv\Scripts\python.exe native/tests/test_harness_oracles.py` | 98 passing test functions; exit 0 |
| Contract registry | `C:\Users\pavan\.workbuddy-ai\binaries\python\envs\default\Scripts\python.exe native/tests/validate_contract_registry.py` | 124 checks; exit 0 |
| Controlling mutation | In the exported helper, replace combined intersection with the first viewport, then run the new regression | Mutant exits 1 because the expected rejection is missing |
| Restoration | Restore the exported helper byte-identically and rerun the regression | Exit 0; byte-identical restoration confirmed |
| Whitespace validation | `git diff --check` | Passed for checkpoint changes |

Durable execution logs and mutation source/output are outside the repository at
`C:\Users\pavan\.codex\visualizations\2026\10\16\01a14481-97c4-79a3-bd0c-39889505c645\sync-main-20261118\`.

## Explicitly incomplete scope

This is a helper checkpoint, not closure of review-0532fb7. The baseline focused
runner still makes separate reachability calls; wiring it to the combined
viewport and resolving run-context/evidence-directory ownership require their
own verification. Concurrent edits to `verify_phase23_native.py` are preserved
outside this commit. No fresh native application build, UI acceptance run,
physical Windows matrix, or live three-agent evidence is claimed here.

The application/XBF and production database were not modified by this checkpoint.
Experimental `scratch/` scripts are not included.
