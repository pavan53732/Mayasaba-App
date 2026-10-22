"""AC-11 fault injection: make a CLI genuinely MISSING, then exercise its card.

AC-11 reads: "A healthy CLI produces no problem card; an affected one names its
cause and offers both actions." It has been INCONCLUSIVE since it was written,
for an honest reason: no CLI on this machine was in a card-producing state, so
the card, its buttons and their handlers were never observed. Reading the source
and declaring it working is exactly what this repository forbids.

The D-11 tie that this machine does have renders a card, but it is a *decision*
card offering "Choose the executable" -- not the problem card AC-11 names, and
not the "Locate executable" action AC-11 requires. So the tie does not discharge
AC-11 and must not be used as if it did.

This probe therefore INJECTS the condition rather than waiting for it:

    launch the application with a PATH from which one CLI's directory has been
    removed

`AgentGateway::Discover` resolves `hermes` / `kilo` / `claude` against the
process PATH and nothing else, so removing one directory makes exactly that CLI
resolve to nothing and report MISSING. The injection is:
  * controlled  - one named directory is removed, and the removed value is logged;
  * reversible  - it is an environment variable of a child process, so the
                  machine's PATH is untouched and nothing on disk is renamed;
  * honest      - MISSING here is the application's real observation of a real
                  PATH, not a stubbed state.

It then checks, against the rendered card:
  1. the affected CLI reads MISSING and the untouched CLIs still read READY
     (so the injection hit one CLI and not the detection pass);
  2. a card appears naming that CLI and its state;
  3. the card offers BOTH "Locate the executable for <CLI>" and "Recheck <CLI>";
  4. "Locate executable" is invoked and the native file picker actually opens,
     then the picker is dismissed;
  5. "Recheck" is invoked twice, under two different conditions, because
     "the card is still there" is equally what *nothing happening* looks like:
       a. with the CLI still absent, the card truthfully persists;
       b. with the CLI made present again, the entry must LEAVE MISSING. That
          transition is the only observation that discriminates a handler which
          re-ran detection from one that never ran.

Two oracles here were corrected after they produced readings about the probe
rather than about the application, and both corrections are load-bearing:

  * The picker is NOT a window of the application's own process. A WinRT
    `FileOpenPicker` is hosted by `C:\\Windows\\System32\\PickerHost.exe`, and it
    is created invisible and shown about half a second later. Counting visible
    top-level windows of the app's pid therefore cannot see it and reported a
    false failure. The oracle now enumerates every top-level window on the
    desktop through `EnumWindows`, including invisible ones and those owned by
    other processes, and records the owning image name.
  * A transient CHECKING during Recheck is not observable, and never was:
    `DetectReadinessAsync` publishes `readiness_` once, after the pass finishes
    (`MainWindow.xaml.cpp:1846`). Sampling for a transition that the code does
    not produce is an oracle-adequacy failure, not a product finding. The
    discriminator is a state that really changes, so the CLI is made to appear.

Verdicts are earned: PASS requires all five; a missing control is FAIL with the
specific absence named; an injection that did not take is INCONCLUSIVE, because
the card was then never exercised and nothing about it was learned.

Run with the base interpreter (pywinauto lives there, not in the managed venv):

    C:\\Python314\\python.exe probe_ac11_readiness_cards.py --exe <path> --store <path>
"""

import argparse
import ctypes
import json
import os
import shutil
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

user32 = ctypes.windll.user32
user32.GetForegroundWindow.restype = wintypes.HWND

COLD_START_TIMEOUT = 90.0
DETECTION_TIMEOUT = 60.0

STATES = ("CHECKING", "READY", "MISSING", "UNSUPPORTED", "PROBE_FAILED")

# The CLI whose directory is stripped, and the entry each CLI publishes.
STRIP_COMMAND = "hermes"
CHAT_ENTRIES = {
    "Hermes Agent CLI": "HermesStatus",
    "Kilo Code CLI": "KiloStatus",
    "Claude Code CLI": "ClaudeStatus",
}

LOG = None


def log(msg):
    line = f"[AC11] {msg}"
    print(line, flush=True)
    if LOG:
        LOG.write(line + "\n")
        LOG.flush()


def wait_for_window(pid, timeout):
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
    """Re-resolve on every call: the application rebuilds surfaces."""
    try:
        for d in parent.descendants():
            if d.element_info.automation_id == aid:
                return d
    except Exception:
        return None
    return None


def find_by_name(parent, name):
    try:
        for d in parent.descendants():
            if (d.element_info.name or "") == name:
                return d
    except Exception:
        return None
    return None


def parse_state(text):
    if not text:
        return None
    for state in STATES:
        if text.rstrip().endswith(state):
            return state
    return "UNRECOGNIZED"


def read_entries(win):
    out = {}
    for cli_name, aid in CHAT_ENTRIES.items():
        el = find_by_aid(win, aid)
        out[cli_name] = el.window_text() if el is not None else None
    return out


def scan_cards(win):
    """Return (card names, action labels) as the user would encounter them."""
    cards, actions = [], {"locate": [], "choose": [], "recheck": []}
    try:
        for d in win.descendants():
            try:
                name = d.element_info.name or ""
            except Exception:
                continue
            if (" problem: " in name) or (" needs a decision: " in name):
                cards.append(name)
            elif name.startswith("Locate the executable for "):
                actions["locate"].append(name)
            elif name.startswith("Choose the executable for "):
                actions["choose"].append(name)
            elif name.startswith("Recheck "):
                actions["recheck"].append(name)
    except Exception as exc:
        log(f"card scan raised {type(exc).__name__}: {exc}")
    return cards, actions


def directory_of_command(command):
    """The PATH directory that currently resolves `command`, or None."""
    for ext in (".cmd", ".exe", ".bat", ".com", ""):
        found = shutil.which(command + ext)
        if found:
            return str(Path(found).parent)
    return None


PICKER_CLASS = "#32770"  # the Win32 common dialog class
EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def enum_top_level_windows():
    """Every top-level window on the desktop, visible or not, with its owner.

    Deliberately NOT filtered by pid: the picker is hosted by PickerHost.exe, so a
    pid filter is the blind spot this function exists to remove.
    """
    out = []

    def callback(hwnd, _):
        try:
            owner = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            cls = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, cls, 256)
            length = user32.GetWindowTextLengthW(hwnd)
            title = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, title, length + 1)
            out.append({
                "hwnd": int(hwnd),
                "class": cls.value,
                "title": title.value,
                "visible": bool(user32.IsWindowVisible(hwnd)),
                "pid": owner.value,
            })
        except Exception:
            pass
        return True

    user32.EnumWindows(EnumWindowsProc(callback), 0)
    return out


def image_name(pid):
    """The executable behind a pid, so the picker's host is named, not guessed."""
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return None
    try:
        buf = ctypes.create_unicode_buffer(1024)
        size = wintypes.DWORD(1024)
        if kernel32.QueryFullProcessImageNameW(handle, 0, buf, ctypes.byref(size)):
            return buf.value
        return None
    finally:
        kernel32.CloseHandle(handle)


def watch_for_picker(known_hwnds, timeout, poll=0.5):
    """Poll for a NEW common-dialog window and follow its visibility.

    Polled, not sampled once: the dialog is created invisible and shown roughly
    half a second later, so a single read at the wrong instant reports "no
    picker" for a picker that is working. Visibility is tracked over time and the
    window is NOT forced shown -- forcing it would make the observation a
    statement about this probe instead of about the application.
    """
    deadline = time.monotonic() + timeout
    seen = []
    while time.monotonic() < deadline:
        fresh = [w for w in enum_top_level_windows()
                 if w["hwnd"] not in known_hwnds and w["class"] == PICKER_CLASS]
        if fresh:
            seen.append(fresh[0])
            if fresh[0]["visible"]:
                return {"opened": True, "ever_visible": True,
                        "window": fresh[0], "samples": len(seen)}
        time.sleep(poll)
    if seen:
        return {"opened": True, "ever_visible": any(w["visible"] for w in seen),
                "window": seen[-1], "samples": len(seen)}
    return {"opened": False, "ever_visible": False, "window": None, "samples": 0}


def main():
    global LOG
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--store", required=True)
    ap.add_argument("--out", default=None)
    ap.add_argument("--strip-command", default=STRIP_COMMAND)
    ap.add_argument("--appear-dir", default=None,
                    help="an empty directory placed ahead of the stripped one on the "
                         "child PATH; a working shim written here later makes the CLI "
                         "appear, which is the Recheck transition discriminator")
    ap.add_argument("--keep-picker-open", action="store_true",
                    help="do not dismiss the file picker (for manual inspection)")
    args = ap.parse_args()

    out_path = Path(args.out) if args.out else Path(__file__).parent / "evidence" / "ac11-readiness-cards.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    LOG = open(str(out_path) + ".log", "w", encoding="utf-8")

    strip_dir = directory_of_command(args.strip_command)
    if not strip_dir:
        log(f"INCONCLUSIVE: '{args.strip_command}' does not resolve on this machine's "
            f"PATH, so there is no directory to remove and no injection to make.")
        LOG.close()
        return 1

    original_path = os.environ.get("PATH", "")
    kept = [p for p in original_path.split(";")
            if p and os.path.normcase(os.path.normpath(p)) != os.path.normcase(os.path.normpath(strip_dir))]
    if len(kept) == len([p for p in original_path.split(";") if p]):
        log(f"INCONCLUSIVE: '{strip_dir}' is not a PATH entry, so the injection "
            f"would not change resolution.")
        LOG.close()
        return 1

    log(f"injection: removing '{strip_dir}' from the child PATH "
        f"({len(original_path.split(';'))} -> {len(kept)} entries)")
    log(f"the machine's own PATH is not modified; only the launched child's is")

    env = os.environ.copy()
    child_path = ";".join(kept)
    if args.appear_dir:
        # Ahead of everything, so a shim written into it wins resolution. Empty
        # for the first half of the run, which is what keeps the CLI MISSING then.
        os.makedirs(args.appear_dir, exist_ok=True)
        for stale in Path(args.appear_dir).glob(args.strip_command + ".*"):
            stale.unlink()
        child_path = args.appear_dir + ";" + child_path
        log(f"probe directory {args.appear_dir} placed first on the child PATH "
            f"(empty, so resolution still fails there)")
    env["PATH"] = child_path
    env["MAYASABA_STORE_PATH"] = args.store
    env.pop("MAYASABA_PERF_LOG", None)

    result = {
        "exe": args.exe,
        "store": args.store,
        "stripped_directory": strip_dir,
        "stripped_command": args.strip_command,
        "checks": {},
        "verdict": "INCONCLUSIVE",
        "reason": None,
    }

    proc = subprocess.Popen([args.exe], cwd=os.path.dirname(args.exe), env=env)
    log(f"launched pid={proc.pid} with the injected PATH")
    try:
        win = wait_for_window(proc.pid, COLD_START_TIMEOUT)
        if win is None:
            result["reason"] = f"no window within {COLD_START_TIMEOUT}s"
            log(f"INCONCLUSIVE: {result['reason']}")
            return 1

        # Reach Chat. With a bound store the application starts on Screen 2, and
        # Screen 2 has no card host, so the card is only observable in Chat.
        opened = False
        deadline = time.monotonic() + 30.0
        while time.monotonic() < deadline:
            button = find_by_name(win, "Open project alpha")
            if button is not None:
                try:
                    button.iface_invoke.Invoke()
                    opened = True
                except Exception as exc:
                    log(f"opening alpha raised {type(exc).__name__}: {exc}")
                break
            time.sleep(0.5)
        if not opened:
            result["reason"] = "Screen 2 did not offer 'Open project alpha', so Chat was never reached"
            log(f"INCONCLUSIVE: {result['reason']}")
            return 1

        # Poll for detection to settle: the injected CLI must reach MISSING.
        states, raw = {}, {}
        deadline = time.monotonic() + DETECTION_TIMEOUT
        while time.monotonic() < deadline:
            raw = read_entries(win)
            states = {k: parse_state(v) for k, v in raw.items()}
            if all(v is not None for v in raw.values()) and any(
                    s not in (None, "CHECKING") for s in states.values()):
                time.sleep(1.2)
                again = {k: parse_state(v) for k, v in read_entries(win).items()}
                if again == states:
                    break
            time.sleep(1.0)

        log(f"readiness after injection: {json.dumps(raw, ensure_ascii=False)}")
        result["entries"] = raw
        result["states"] = states

        affected = [name for name, s in states.items() if s == "MISSING"]
        healthy = [name for name, s in states.items() if s == "READY"]
        result["checks"]["injection_took_effect"] = bool(affected)
        if not affected:
            result["reason"] = (
                "the injected CLI did not reach MISSING, so no problem card was owed "
                "and the card path was never exercised")
            log(f"INCONCLUSIVE: {result['reason']}")
            log(f"  states read: {json.dumps(states)}")
            return 1
        log(f"PASS: injection took effect; MISSING={affected} READY={healthy}")

        # The card must name the cause.
        cards, actions = scan_cards(win)
        log(f"cards: {json.dumps(cards, ensure_ascii=False)}")
        log(f"actions: {json.dumps(actions, ensure_ascii=False)}")
        result["cards"] = cards
        result["actions"] = actions

        failures = []
        target = affected[0]
        card_for_target = [c for c in cards if target in c and " problem: " in c]
        result["checks"]["card_present"] = bool(card_for_target)
        if not card_for_target:
            failures.append(f"no problem card names {target} although it reads MISSING")

        locate = [a for a in actions["locate"] if target in a]
        recheck = [a for a in actions["recheck"] if target in a]
        result["checks"]["locate_offered"] = bool(locate)
        result["checks"]["recheck_offered"] = bool(recheck)
        if not locate:
            failures.append(f"the {target} card offers no 'Locate executable' action")
        if not recheck:
            failures.append(f"the {target} card offers no 'Recheck' action")

        # A healthy CLI must NOT have a problem card. This is the other half of
        # AC-11 and the half that a card-only assertion would miss.
        stray = [c for c in cards if " problem: " in c
                 and any(h in c for h in healthy)]
        result["checks"]["healthy_has_no_card"] = not stray
        if stray:
            failures.append(f"a READY CLI shows a problem card: {stray}")

        if failures:
            for f in failures:
                log(f"FAIL: {f}")
            result["verdict"] = "FAIL"
            result["reason"] = "; ".join(failures)
            return 1

        # --- Exercise Recheck -------------------------------------------------
        # Part one: the CLI is still absent, so the honest outcome is that the
        # card persists. A card that disappeared would claim health it did not
        # observe. But "the card is still there" is ALSO what a handler that never
        # ran looks like, so this half alone proves nothing -- part two follows.
        recheck_btn = find_by_aid(win, "CliRecheckButton") or None
        if recheck_btn is None:
            # The buttons are built imperatively, so they are located by their
            # accessible name rather than by an automation id that does not exist.
            recheck_btn = None
            try:
                for d in win.descendants():
                    try:
                        if (d.element_info.name or "").startswith("Recheck ") \
                                and target in (d.element_info.name or ""):
                            recheck_btn = d
                            break
                    except Exception:
                        continue
            except Exception:
                pass
        if recheck_btn is None:
            log(f"FAIL: the Recheck action for {target} is not invokable")
            result["verdict"] = "FAIL"
            result["reason"] = "the Recheck control was listed but not invokable"
            return 1

        try:
            recheck_btn.iface_invoke.Invoke()
        except Exception as exc:
            log(f"FAIL: invoking Recheck raised {type(exc).__name__}: {exc}")
            result["verdict"] = "FAIL"
            result["reason"] = f"Recheck could not be invoked: {exc}"
            return 1

        # Poll for the card to still be there after the pass settles.
        deadline = time.monotonic() + 30.0
        persisted = False
        while time.monotonic() < deadline:
            cards_after, _ = scan_cards(win)
            if any(target in c and " problem: " in c for c in cards_after):
                persisted = True
                break
            time.sleep(1.0)
        result["checks"]["card_persists_after_recheck"] = persisted
        if not persisted:
            log(f"FAIL: the {target} card disappeared after Recheck while the CLI is "
                f"still absent from PATH")
            result["verdict"] = "FAIL"
            result["reason"] = "Recheck cleared a card for a CLI that is still missing"
            return 1
        log(f"PASS: Recheck ran and the {target} card truthfully persisted (still MISSING).")

        # --- Exercise Locate executable ---------------------------------------
        # The picker is hosted out-of-process and created invisible, so the
        # observable is a NEW common-dialog window anywhere on the desktop, polled
        # until it is actually shown. See the module docstring for why the earlier
        # process-filtered, visible-only count was a blind oracle.
        known = {w["hwnd"] for w in enum_top_level_windows()}
        locate_btn = None
        try:
            for d in win.descendants():
                try:
                    if (d.element_info.name or "").startswith("Locate the executable for ") \
                            and target in (d.element_info.name or ""):
                        locate_btn = d
                        break
                except Exception:
                    continue
        except Exception:
            pass
        if locate_btn is None:
            log(f"FAIL: the Locate action for {target} is not invokable")
            result["verdict"] = "FAIL"
            result["reason"] = "the Locate control was listed but not invokable"
            return 1

        try:
            locate_btn.iface_invoke.Invoke()
        except Exception as exc:
            log(f"NOTE: invoking Locate raised {type(exc).__name__}: {exc}")
        picker = watch_for_picker(known, timeout=20.0)
        result["checks"]["locate_opens_picker"] = picker["ever_visible"]
        result["picker"] = {
            "opened": picker["opened"],
            "ever_visible": picker["ever_visible"],
            "window": picker["window"],
            "host_image": (image_name(picker["window"]["pid"])
                           if picker["window"] else None),
        }
        log(f"picker: {json.dumps(result['picker'], ensure_ascii=False)}")
        if picker["ever_visible"]:
            log(f"PASS: Locate executable opened a usable native picker "
                f"({result['picker']['host_image']}, "
                f"class {picker['window']['class']!r}, title {picker['window']['title']!r}).")
            if not args.keep_picker_open:
                # Dismiss with Escape so the run leaves no modal surface behind.
                user32.PostMessageW(picker["window"]["hwnd"], 0x0100, 0x1B, 0)
                time.sleep(0.8)
        elif picker["opened"]:
            failures.append(
                "a common-dialog window appeared after Locate but never became "
                "visible, so the picker was created without being usable")
            log(f"FAIL: {failures[-1]}")
        else:
            failures.append(
                "Locate executable was invoked but no common-dialog window "
                "appeared anywhere on the desktop within 20s")
            log(f"FAIL: {failures[-1]}")

        # --- Recheck part two: a state that actually changes -------------------
        # The only observation that distinguishes a handler which re-ran detection
        # from one that did nothing is a state TRANSITION. The child PATH already
        # carries an empty probe directory ahead of the stripped one, so writing a
        # working shim into it makes the CLI genuinely appear; Recheck must then
        # report it. Nothing outside that directory is touched, and the machine's
        # PATH is still untouched.
        #
        # This runs LAST because it destroys the setup it depends on: once the CLI
        # is healthy its problem card is correctly withdrawn, so anything that needs
        # the card -- Locate above all -- must be exercised before it.
        if args.appear_dir:
            shim = Path(args.appear_dir) / (args.strip_command + ".cmd")
            try:
                shim.write_text("@echo off\r\necho mayasaba-ac11-standin 1.0.0\r\nexit /b 0\r\n",
                                encoding="ascii", newline="")
                log(f"made the CLI appear: wrote {shim}")
            except Exception as exc:
                log(f"NOTE: could not write the stand-in shim: {type(exc).__name__}: {exc}")

            if shim.exists():
                recheck_btn = None
                try:
                    for d in win.descendants():
                        try:
                            n = d.element_info.name or ""
                            if n.startswith("Recheck ") and target in n:
                                recheck_btn = d
                                break
                        except Exception:
                            continue
                except Exception:
                    pass
                if recheck_btn is not None:
                    try:
                        recheck_btn.iface_invoke.Invoke()
                        log("Recheck invoked with the CLI now present on PATH")
                    except Exception as exc:
                        log(f"NOTE: second Recheck invoke raised {type(exc).__name__}: {exc}")

                appeared = False
                deadline = time.monotonic() + 45.0
                while time.monotonic() < deadline:
                    entry = find_by_aid(win, CHAT_ENTRIES[target])
                    now_state = parse_state(entry.window_text() if entry is not None else None)
                    if now_state not in (None, "CHECKING", "MISSING"):
                        appeared = True
                        log(f"PASS: Recheck observed the change -- {target} left MISSING "
                            f"and now reads {now_state}.")
                        result["checks"]["recheck_observed_transition"] = True
                        result["checks"]["state_after_appear"] = now_state
                        break
                    time.sleep(1.0)
                if not appeared:
                    entry = find_by_aid(win, CHAT_ENTRIES[target])
                    last = entry.window_text() if entry is not None else None
                    log(f"FAIL: Recheck did not observe the CLI appearing; the entry "
                        f"still reads {last!r} after the shim was written and Recheck "
                        f"invoked. A handler that re-ran detection would have found it.")
                    result["checks"]["recheck_observed_transition"] = False
                    result["checks"]["state_after_appear"] = last
                    failures.append(
                        f"Recheck did not observe {target} appearing on PATH; the "
                        f"handler did not re-run detection")
        else:
            log("NOTE: no --appear-dir given, so the Recheck transition was not "
                "exercised and 'the card persisted' is not evidence that it ran.")
            result["checks"]["recheck_observed_transition"] = None

        if failures:
            result["verdict"] = "FAIL"
            result["reason"] = "; ".join(failures)
        else:
            result["verdict"] = "PASS"
            result["reason"] = (
                f"{target} reads MISSING, its card names the cause and offers both "
                f"Locate executable and Recheck; Recheck preserves the card while the "
                f"CLI is absent and observes it when the CLI appears; Locate opens a "
                f"usable native picker; READY CLIs have no card")
        log(f"{result['verdict']}: {result['reason']}")
        return 0 if result["verdict"] == "PASS" else 1
    finally:
        result["stripped_directory"] = strip_dir
        out_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
        log(f"evidence: {out_path}")
        try:
            proc.terminate()
            proc.wait(timeout=10)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass
        log("terminated")
        LOG.close()


if __name__ == "__main__":
    sys.exit(main())
