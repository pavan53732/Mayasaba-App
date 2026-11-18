# Historical scratch reproductions

These `.py.txt` files preserve original source bytes cited by local validation.
They are historical evidence, not maintained runnable tests. Original paths,
identities, setup assumptions, failure messages and limitations are retained;
their comments do not establish current behavior or an operating-system cause.
The local attributes preserve original line endings and whitespace for these
two text archives; maintained Python files retain normal whitespace checks.

| Archived source | Original role | Current treatment |
|---|---|---|
| `test_mutation.py.txt` | Five cleanup/journey/style mutations cited by LOCAL-VALIDATION sections 30–31 | Superseded for execution by `native/tests/run_harness_mutations.py`, which mutates an isolated committed snapshot and checks exact failure assertions. |
| `test_real_explorer_drop9.py.txt` | Explorer-origin drag/drop experiment cited by LOCAL-VALIDATION section 29 | Preserved for provenance. It is not proof of successful attachment acceptance and does not close C11/C12. |

The original mutation script rewrites the caller's working checkout. The
Explorer script performs top-level desktop/input operations, launches processes,
and deletes rows in a fixed test store. It uses machine-specific paths and old
Win32 bindings. Retaining its bytes does not endorse running it as a current
acceptance harness. This promotion did not execute it or any other Explorer
scratch experiment.

`inventory.json` records all 29 original scratch scripts, their SHA-256 hashes,
and disposition at promotion time. The original local files remain untouched.
Source patchers, transcript readers, superseded oracle sketches and additional
desktop experiments remain local; their changes or useful assertions are already
represented in versioned source/tests or require their own review. None is an
application build input.
