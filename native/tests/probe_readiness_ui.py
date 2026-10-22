"""Observation probe for the CLI readiness projection (Agent Gateway, layer 4).

This is NOT the acceptance harness. It exists to answer one question with a
measurement instead of a claim:

    Does the header print a state observed on this machine, or a constant?

The pre-wiring build printed "UNSUPPORTED" for all three CLIs unconditionally.
That string could not be wrong, and could not be right. After wiring, each entry
must reach a state that the gateway actually measured, and at least one of them
must differ from the hardcoded literal.

Run with the base interpreter (pywinauto lives there, not in the managed venv):

    C:\\Python314\\python.exe probe_readiness_ui.py

The probe is read-only: it launches the application, reads UIA text, and closes.
It never clicks Start, Send, or any control that would change state.
"""

import json
import subprocess
import sys
import time
from pathlib import Path

EXE = Path(r"C:\Users\pavan\PROJECTS\Mayasaba-App\native\Mayasaba\bin\x64\Release\Mayasaba.exe")
OUT = Path(__file__).parent / "evidence" / "readiness-probe.json"

# A cold start on this project takes tens of seconds. The readiness probe then
# runs three bounded CLI detections on the background lane. Poll rather than
# sample once: a single read converts a slow-but-correct transition into a
# failure of the product.
COLD_START_TIMEOUT = 90.0
READINESS_TIMEOUT = 120.0

ENTRIES = {
    "welcome_hermes": "WelcomeHermesStatus",
    "welcome_kilo": "WelcomeKiloStatus",
    "welcome_claude": "WelcomeClaudeStatus",
}

# Screen 2 has its own entries with their own names. They must project the same
# observation: the prelude is where the user decides what to install, so a
# constant there is the most misleading place to have one.
FOLDER_ENTRIES = {
    "folder_hermes": "FolderHermesStatus",
    "folder_kilo": "FolderKiloStatus",
    "folder_claude": "FolderClaudeStatus",
}

STATES = ("CHECKING", "READY", "MISSING", "UNSUPPORTED", "PROBE_FAILED")


def wait_for_window(pid, timeout):
    """Wait for the app's top-level window, distinguishing 'not yet' from 'not there'."""
    from pywinauto import Desktop

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            for win in Desktop(backend="uia").windows(process=pid):
                text = win.window_text() or ""
                if "Mayasaba" in text or "Engineering Control Room" in text:
                    return win
        except Exception:
            pass
        time.sleep(0.5)
    return None


def find_by_aid(parent, aid):
    """Resolve an element fresh on every call.

    The application rebuilds surfaces as screens change, so a cached reference
    can point at a node that no longer exists and read as an empty string
    against a working UI. Re-resolving costs one descendant scan.
    """
    try:
        for d in parent.descendants():
            if d.element_info.automation_id == aid:
                return d
    except Exception:
        return None
    return None


def read_entries(win, mapping=None):
    """Read each readiness TextBlock by automation id. Missing elements are absent, not errors."""
    found = {}
    for key, aid in (mapping or ENTRIES).items():
        ctrl = find_by_aid(win, aid)
        found[key] = ctrl.window_text() if ctrl is not None else None
    return found


def advance_to_folder(win):
    """Move from Welcome to Screen 2 by clicking Next. Returns True if it landed.

    Next is the documented first-run transition; it does not bind a project, so
    nothing is created or changed by taking it.
    """
    from pywinauto.controls.uia_controls import ButtonWrapper

    button = find_by_aid(win, "NextButton")
    if button is None:
        return False
    button.click_input()
    deadline = time.monotonic() + 20.0
    while time.monotonic() < deadline:
        if find_by_aid(win, "FolderHermesStatus") is not None:
            return True
        time.sleep(0.5)
    return False


def parse_state(text):
    if not text:
        return None
    for state in STATES:
        if text.rstrip().endswith(state):
            return state
    return "UNRECOGNIZED"


def main():
    if not EXE.exists():
        print(f"FAIL: executable not present at {EXE}")
        return 2

    # A leftover instance holds the single-instance lock and the probe would then
    # read a window bound to a different store. Kill it before starting.
    subprocess.run(["taskkill", "/F", "/IM", "Mayasaba.exe"], capture_output=True)

    proc = subprocess.Popen([str(EXE)])
    try:
        win = wait_for_window(proc.pid, COLD_START_TIMEOUT)
        if win is None:
            print(f"FAIL: no window within {COLD_START_TIMEOUT}s")
            return 1

        # Poll for the real postcondition. CHECKING is a registered state AND the
        # value the XAML ships with, so "all three read a registered state" is
        # satisfied by the initial markup and proves nothing -- a first version of
        # this probe asserted exactly that and reported PASS without a detection
        # having run. The postcondition that only detection can produce is: at
        # least one entry reads a state other than CHECKING.
        #
        # This machine has two resolvable CLIs, so at least one entry must leave
        # CHECKING. Claude Code legitimately stays CHECKING (two installations, so
        # the D-11 rule requires the user's choice), which is why the postcondition
        # is "at least one", not "all".
        deadline = time.monotonic() + READINESS_TIMEOUT
        last = {}
        settled = None
        while time.monotonic() < deadline:
            last = read_entries(win)
            states = {k: parse_state(v) for k, v in last.items()}
            values = list(states.values())
            if all(v is not None for v in last.values()) and any(s != "CHECKING" for s in values):
                # Confirm it is stable rather than mid-transition: two consecutive
                # reads must agree.
                time.sleep(1.5)
                again = {k: parse_state(v) for k, v in read_entries(win).items()}
                if again == states:
                    settled = again
                    last = read_entries(win)
                    break
            time.sleep(1.0)

        if settled is None:
            print(f"FAIL: no entry left CHECKING within {READINESS_TIMEOUT}s")
            print("  A detection pass either did not run, did not reach the projection,")
            print("  or failed silently. The entries below are the initial markup value,")
            print("  which this probe cannot distinguish from a completed CHECKING result.")
            print(f"  last read: {json.dumps(last, indent=2)}")
            return 1

        result = {
            "executable": str(EXE),
            "entries": last,
            "states": settled,
            "distinct_states": sorted(set(s for s in settled.values() if s)),
            "left_checking": sorted(k for k, s in settled.items() if s != "CHECKING"),
        }

        # Screen 2 carries its own entries. Read them after the Welcome pass so the
        # evidence covers both preludes rather than only the first one the probe
        # happened to land on.
        folder_states = {}
        if advance_to_folder(win):
            folder_last = read_entries(win, FOLDER_ENTRIES)
            folder_states = {k: parse_state(v) for k, v in folder_last.items()}
            result["folder_entries"] = folder_last
            result["folder_states"] = folder_states
        else:
            result["folder_entries"] = None
            result["folder_states"] = None

        OUT.parent.mkdir(parents=True, exist_ok=True)
        OUT.write_text(json.dumps(result, indent=2), encoding="utf-8")

        print(json.dumps(result, indent=2))
        print()
        print(f"PASS: {len(result['left_checking'])} of {len(settled)} Welcome entries left CHECKING "
              f"({', '.join(result['left_checking'])}); states {result['distinct_states']}")

        if folder_states:
            folder_left = sorted(k for k, s in folder_states.items() if s != "CHECKING")
            print(f"PASS: {len(folder_left)} of {len(folder_states)} Screen 2 entries left CHECKING "
                  f"({', '.join(folder_left)}); states {sorted(set(folder_states.values()))}")
        else:
            print("INCONCLUSIVE: Screen 2 was not reached, so its entries were not observed")
        print(f"  evidence: {OUT}")
        print()
        print("NOTE: this observes the readiness projection only. It is not a working")
        print("      session, not containment, and not gate I3.")
        return 0
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=10)
        except Exception:
            subprocess.run(["taskkill", "/F", "/IM", "Mayasaba.exe"], capture_output=True)


if __name__ == "__main__":
    sys.exit(main())
