"""Run the five historical harness mutations in an isolated committed snapshot."""

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import uuid


@dataclass(frozen=True)
class Mutation:
    name: str
    path: str
    content: bytes
    test: str
    expected: str


def replace_once(data, needle, replacement):
    count = data.count(needle)
    if count != 1:
        raise ValueError(f"Mutation anchor must occur once; found {count}: {needle!r}")
    return data.replace(needle, replacement, 1)


def mutations(original):
    acceptance = "native/tests/test_full_ui_acceptance.py"
    cpp = "native/Mayasaba/MainWindow.xaml.cpp"
    xaml = "native/Mayasaba/App.xaml"
    guard = b"if returned or not (paging_bound or selection_attempted):"
    branch = (
        b"if (selectorContext_ == SelectorContext::FirstRun)\n"
        b"        {\n            ShowWelcome();\n            e.Handled(true);\n"
        b"            return;\n        }"
    )
    switching = (
        b"if (selectorContext_ == SelectorContext::Switching && !activeProject_.empty())\n"
        b"        {\n            OnCancelSwitch(nullptr, nullptr);\n"
        b"            e.Handled(true);\n            return;\n        }"
    )
    # git archive normally emits LF; keep the anchors usable on CRLF revisions too.
    if b"\r\n" in original[cpp]:
        branch = branch.replace(b"\n", b"\r\n")
        switching = switching.replace(b"\n", b"\r\n")
    first_run_removed = replace_once(original[cpp], branch, b"// mutation: remove FirstRun Escape")
    swapped = replace_once(
        original[cpp], switching,
        switching.replace(b"OnCancelSwitch(nullptr, nullptr);", b"ShowWelcome();"),
    )
    swapped = replace_once(
        swapped, branch, branch.replace(b"ShowWelcome();", b"OnCancelSwitch(nullptr, nullptr);"),
    )
    key = b'x:Key="MayasabaPrimaryAction"'
    if original[xaml].count(key) != 1:
        raise ValueError("Primary action style must have one declaration")
    key_at = original[xaml].index(key)
    start = original[xaml].rfind(b"<Style", 0, key_at)
    end = original[xaml].index(b"</Style>", key_at) + len(b"</Style>")
    style = original[xaml][start:end]
    disabled = re.compile(rb'<VisualState x:Name="Disabled">.*?</VisualState>', re.DOTALL)
    if len(disabled.findall(style)) != 1:
        raise ValueError("Primary action style must have exactly one Disabled block")
    changed_style = disabled.sub(b"<!-- mutation: omit Disabled state -->", style, count=1)
    return [
        Mutation("selection_attempt", acceptance,
                 replace_once(original[acceptance], guard, b"if returned or not paging_bound:"),
                 "test_phase3b_selection_attempt_unconfirmed_restoration_failure_preserves_rows",
                 "dependent phases must be stopped when restoration fails after selection attempt"),
        Mutation("return_to_alpha", acceptance,
                 replace_once(original[acceptance], guard, b"if True:"),
                 "test_phase3b_cleanup_releases_held_load_and_preserves_rows_on_alpha_failure",
                 "dependent phases must be stopped when alpha return failed"),
        Mutation("first_run_escape", cpp, first_run_removed,
                 "test_ux_journey_semantics_and_escape_preservation",
                 "Must have exactly 1 FirstRun branch"),
        Mutation("primary_disabled_state", xaml,
                 original[xaml][:start] + changed_style + original[xaml][end:],
                 "test_ux_shared_button_styles_and_focus_visuals",
                 "'MayasabaPrimaryAction' CommonStates missing required state 'Disabled'"),
        Mutation("escape_dispatch", cpp, swapped,
                 "test_ux_journey_semantics_and_escape_preservation",
                 "FirstRun Escape must call ShowWelcome()"),
    ]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def run_target(snapshot, output, test, phase, timeout):
    environment = os.environ.copy()
    environment.pop("PYTHONOPTIMIZE", None)
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    # Unique lookup prefixes prevent same-size mutations from reusing stale pyc.
    environment["PYTHONPYCACHEPREFIX"] = str(output / "unused-cache" / uuid.uuid4().hex)
    code = f"import test_harness_oracles as T; T.{test}()"
    try:
        process = subprocess.run(
            [sys.executable, "-B", "-c", code], cwd=snapshot / "native/tests",
            env=environment, capture_output=True, text=True, timeout=timeout,
        )
        result = {"exit_code": process.returncode, "timed_out": False}
        log = process.stdout + process.stderr
    except subprocess.TimeoutExpired as exc:
        result = {"exit_code": None, "timed_out": True}
        log = f"Test exceeded {timeout} seconds.\n{exc}"
    (output / f"{phase}.log").write_text(log, encoding="utf-8")
    return result, log


def run(source, revision, output, timeout):
    root = Path(subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "--show-toplevel"], text=True,
    ).strip()).resolve()
    if root != source:
        raise ValueError("--source-root must name the repository root")
    commit = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "--verify", "--end-of-options", f"{revision}^{{commit}}"],
        text=True,
    ).strip()
    if output == root or output.is_relative_to(root / ".git"):
        raise ValueError("Choose a dedicated evidence directory outside .git")
    output.mkdir(parents=True, exist_ok=False)  # Never overwrite a prior run.
    snapshot = output / "snapshot"
    snapshot.mkdir()
    archive = output / "source.tar"
    subprocess.run(
        ["git", "-C", str(root), "archive", "--format=tar", f"--output={archive}", commit],
        check=True,
    )
    with tarfile.open(archive) as handle:
        handle.extractall(snapshot, filter="data")
    paths = ["native/tests/test_full_ui_acceptance.py", "native/Mayasaba/MainWindow.xaml.cpp",
             "native/Mayasaba/App.xaml"]
    original = {name: (snapshot / name).read_bytes() for name in paths}
    worktree_before = {name: sha256((root / name).read_bytes()) for name in paths}
    report = {
        "source_commit": commit, "source_root": str(root), "python": sys.executable,
        "runner_sha256": sha256(Path(__file__).read_bytes()),
        "python_version": sys.version, "snapshot": str(snapshot),
        "source_hashes": {name: sha256(data) for name, data in original.items()},
        "scope": "Five Python/source-structure harness discriminations; no native build or live UI run",
        "cases": [], "complete": False,
    }
    try:
        cases = mutations(original)  # Validate all anchors before any mutation.
        for case in cases:
            case_output = output / case.name
            case_output.mkdir()
            print(f"Checking {case.name}...", flush=True)
            clean_result, _ = run_target(snapshot, case_output, case.test, "clean", timeout)
            if clean_result["exit_code"] != 0:
                raise RuntimeError(f"Clean target failed: {case.name}; see {case_output / 'clean.log'}")
            try:
                (snapshot / case.path).write_bytes(case.content)
                mutant_result, mutant_log = run_target(snapshot, case_output, case.test, "mutant", timeout)
            finally:
                (snapshot / case.path).write_bytes(original[case.path])
            restored_result, _ = run_target(snapshot, case_output, case.test, "restored", timeout)
            restored = all((snapshot / name).read_bytes() == data for name, data in original.items())
            discriminates = (mutant_result["exit_code"] == 1 and "AssertionError" in mutant_log
                             and case.expected in mutant_log)
            passed = discriminates and restored_result["exit_code"] == 0 and restored
            report["cases"].append({
                "name": case.name, "target": case.test, "path": case.path,
                "expected_assertion": case.expected, "clean": clean_result,
                "mutant": mutant_result, "restored": restored_result,
                "byte_identical_restore": restored, "passed": passed,
            })
            print(f"  {'PASS' if passed else 'FAIL'}: {case.name}", flush=True)
        report["complete"] = len(report["cases"]) == 5 and all(c["passed"] for c in report["cases"])
    except Exception as exc:
        report["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        for name, data in original.items():
            (snapshot / name).write_bytes(data)
        report["snapshot_restored"] = all((snapshot / name).read_bytes() == data for name, data in original.items())
        report["working_tree_hashes_unchanged"] = all(
            sha256((root / name).read_bytes()) == digest for name, digest in worktree_before.items()
        )
        (output / "summary.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    passed = report["complete"] and report["snapshot_restored"]
    print(f"{'PASS' if passed else 'FAIL'}: {sum(c['passed'] for c in report['cases'])}/5 mutations discriminated")
    print(f"Evidence: {output / 'summary.json'}")
    return 0 if passed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--revision", default="HEAD", help="Committed revision; uncommitted changes are excluded")
    parser.add_argument("--output-dir", type=Path, required=True, help="New evidence directory; existing paths are refused")
    parser.add_argument("--timeout", type=float, default=60, help="Seconds per targeted test")
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("These harness targets require Windows and an interpreter with pywinauto/Pillow")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    try:
        return run(args.source_root.resolve(), args.revision, args.output_dir.resolve(), args.timeout)
    except Exception as exc:
        print(f"FAIL: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
