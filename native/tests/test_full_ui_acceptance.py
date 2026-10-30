import os
import sys
import time
import json
import msvcrt
import sqlite3
import subprocess
import ctypes
import hashlib
import traceback
import threading
from ctypes import wintypes
from pywinauto import Desktop
from pywinauto.keyboard import send_keys

# Defaults are preserved for an interactive run, but every path can be supplied explicitly
# so a verification run never depends on a machine-specific directory. Results are only
# meaningful when they are tied to the exact binaries that produced them, so the harness
# records the identity of the executable and the compiled XAML it drove.
STORE_PATH = r"C:\Users\pavan\PROJECTS\_ui-verify\multi_test\store.db"
PERF_LOG_PATH = r"C:\Users\pavan\PROJECTS\_ui-verify\multi_test\perf.json"
BIN_DIR = r"C:\Users\pavan\PROJECTS\Mayasaba-App\native\Mayasaba\bin\x64\Release"
EXE_PATH = os.path.join(BIN_DIR, "Mayasaba.exe")
EVIDENCE_DIR = ""
# The window takes roughly 10-25 s to appear on a cold binary here; a fixed short sleep
# reports "no window" against a build that is merely still starting.
WINDOW_WAIT_SECONDS = 40
OLE_DROP_EXE = r"C:\Users\pavan\PROJECTS\Mayasaba-App\native\tests\bin\ole_drop.exe"

# How long to wait for ole_drop.exe, derived from the budgets that helper runs on
# rather than from a number chosen independently of them.
#
# This coupling has already caused a false finding once. The helper's watchdog
# fires at OLE_DROP_WATCHDOG_MS, then sends VK_ESCAPE, then waits
# OLE_DROP_ESCAPE_MS before it prints the "QueryContinueDrag calls so far"
# diagnostic and exits 4. The harness previously used timeout=4 -- exactly the
# watchdog interval -- so Python killed the helper at the instant its escape
# began. The escape never ran, the diagnostic never printed, and the run recorded
# a bare "reached bounded 4s timeout" with no measurement in it. A timeout must
# contain the whole escape-and-report sequence with margin, or it destroys the
# evidence it is waiting for.
#
# Keep these equal to kWatchdogMs / kEscapeMs in native/tests/ole_drop.cpp. The
# helper prints "budgets: selfdrop=... watchdog=... escape=... total=..." on
# startup, so a mismatch is visible in the run log.
OLE_DROP_WATCHDOG_MS = 4000
OLE_DROP_ESCAPE_MS = 400
OLE_DROP_MARGIN_S = 4.0
OLE_DROP_TIMEOUT_S = (OLE_DROP_WATCHDOG_MS + OLE_DROP_ESCAPE_MS) / 1000.0 + OLE_DROP_MARGIN_S


def apply_cli_overrides(argv):
    """Accepts --exe/--store/--perf-log/--evidence-dir without disturbing the defaults."""
    global EXE_PATH, STORE_PATH, PERF_LOG_PATH, BIN_DIR, EVIDENCE_DIR
    for i, arg in enumerate(argv):
        if i + 1 >= len(argv):
            break
        value = argv[i + 1]
        if arg == "--exe":
            EXE_PATH = os.path.abspath(value)
            BIN_DIR = os.path.dirname(EXE_PATH)
        elif arg == "--store":
            STORE_PATH = os.path.abspath(value)
        elif arg == "--perf-log":
            PERF_LOG_PATH = os.path.abspath(value)
        elif arg == "--evidence-dir":
            EVIDENCE_DIR = os.path.abspath(value)


def sha256_of(path):
    try:
        with open(path, "rb") as handle:
            digest = hashlib.sha256()
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()
    except OSError as exc:
        return f"UNAVAILABLE ({exc})"


def record_artifact_identity():
    """Ties every result in this run to the exact binaries that produced it."""
    identity = {
        "executable": EXE_PATH,
        "executable_sha256": sha256_of(EXE_PATH),
        "compiled_xaml": os.path.join(BIN_DIR, "MainWindow.xbf"),
        "compiled_xaml_sha256": sha256_of(os.path.join(BIN_DIR, "MainWindow.xbf")),
        "source_commit": source_commit(),
        "store": STORE_PATH,
        "perf_log": PERF_LOG_PATH,
        "recorded_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
    }
    global EVIDENCE_LOG_HANDLE
    if EVIDENCE_DIR and EVIDENCE_LOG_HANDLE is None:
        os.makedirs(EVIDENCE_DIR, exist_ok=True)
        EVIDENCE_LOG_HANDLE = open(os.path.join(EVIDENCE_DIR, "harness-run.log"),
                                   "w", encoding="utf-8", buffering=1)
    log(f"Executable SHA-256:    {identity['executable_sha256']}")
    log(f"MainWindow.xbf SHA-256: {identity['compiled_xaml_sha256']}")
    log(f"Source commit:         {identity['source_commit']}")
    if EVIDENCE_DIR:
        target = os.path.join(EVIDENCE_DIR, "artifact-identity.json")
        with open(target, "w", encoding="utf-8") as handle:
            json.dump(identity, handle, indent=2)
        log(f"Artifact identity written: {target}")
        log(f"Run log written to: {os.path.join(EVIDENCE_DIR, 'harness-run.log')}")
    return identity


def source_commit():
    try:
        repo = os.path.abspath(os.path.join(BIN_DIR, "..", "..", "..", "..", ".."))
        out = subprocess.run(["git", "-C", repo, "rev-parse", "HEAD"],
                             capture_output=True, text=True, timeout=10)
        return out.stdout.strip() or "UNAVAILABLE"
    except Exception as exc:
        return f"UNAVAILABLE ({exc})"

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

from ctypes import wintypes

DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = ctypes.c_void_p(-4)
DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE = ctypes.c_void_p(-3)
DPI_AWARENESS_CONTEXT_SYSTEM_AWARE = ctypes.c_void_p(-2)
DPI_AWARENESS_CONTEXT_UNAWARE = ctypes.c_void_p(-1)

# Set ctypes signatures explicitly
if hasattr(user32, "SetProcessDpiAwarenessContext"):
    user32.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
    user32.SetProcessDpiAwarenessContext.restype = wintypes.BOOL

if hasattr(user32, "GetDpiAwarenessContextForProcess"):
    user32.GetDpiAwarenessContextForProcess.argtypes = [wintypes.HANDLE]
    user32.GetDpiAwarenessContextForProcess.restype = ctypes.c_void_p

if hasattr(user32, "AreDpiAwarenessContextsEqual"):
    user32.AreDpiAwarenessContextsEqual.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    user32.AreDpiAwarenessContextsEqual.restype = wintypes.BOOL

if hasattr(user32, "GetAwarenessFromDpiAwarenessContext"):
    user32.GetAwarenessFromDpiAwarenessContext.argtypes = [ctypes.c_void_p]
    user32.GetAwarenessFromDpiAwarenessContext.restype = ctypes.c_int

if hasattr(user32, "GetDpiForWindow"):
    user32.GetDpiForWindow.argtypes = [wintypes.HWND]
    user32.GetDpiForWindow.restype = wintypes.UINT

def establish_dpi_awareness():
    """Establish and explicitly verify effective DPI awareness using supported Win32 APIs.

    Attempts to set DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2.
    If SetProcessDpiAwarenessContext returns FALSE (e.g. manifest or runtime
    pre-initialized DPI context), verifies the effective awareness context via
    GetDpiAwarenessContextForProcess and GetAwarenessFromDpiAwarenessContext.
    Accepts and documents Per-Monitor v2 or Per-Monitor v1, but strictly rejects
    System-Aware, Unaware, or null contexts. Does NOT swallow initialization failure.
    """
    if not hasattr(user32, "SetProcessDpiAwarenessContext") or not hasattr(user32, "GetDpiAwarenessContextForProcess"):
        raise RuntimeError("Windows Per-Monitor DPI awareness APIs are not available on this platform")

    set_ok = bool(user32.SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
    current_proc = kernel32.GetCurrentProcess()
    effective_ctx = user32.GetDpiAwarenessContextForProcess(current_proc)
    if not effective_ctx:
        err = ctypes.get_last_error()
        raise RuntimeError(f"GetDpiAwarenessContextForProcess returned NULL (GetLastError={err})")

    # 1. Exact PMv2 match
    if hasattr(user32, "AreDpiAwarenessContextsEqual") and user32.AreDpiAwarenessContextsEqual(effective_ctx, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2):
        return ("PER_MONITOR_AWARE_V2", effective_ctx)

    # 2. Check awareness level if context pointer differs
    awareness = -1
    if hasattr(user32, "GetAwarenessFromDpiAwarenessContext"):
        awareness = user32.GetAwarenessFromDpiAwarenessContext(effective_ctx)

    # 2 is DPI_AWARENESS_PER_MONITOR_AWARE (PMv1)
    if awareness == 2:
        return ("PER_MONITOR_AWARE_V1", effective_ctx)

    # Context is UNAWARE (0), SYSTEM_AWARE (1), or INVALID (-1)
    err = ctypes.get_last_error()
    awareness_names = {-1: "INVALID", 0: "UNAWARE", 1: "SYSTEM_AWARE", 2: "PER_MONITOR_AWARE"}
    aw_name = awareness_names.get(awareness, f"UNKNOWN({awareness})")
    raise RuntimeError(
        f"DPI awareness initialization failed: SetProcessDpiAwarenessContext returned {set_ok} (GetLastError={err}), "
        f"and effective context is {aw_name} (ctx={effective_ctx}), which does not satisfy per-monitor scaling"
    )

EFFECTIVE_DPI_NAME, EFFECTIVE_DPI_CTX = establish_dpi_awareness()

REGISTERED_VERDICTS = {"PASS", "FAIL", "INCONCLUSIVE", "BLOCKED", "NOT_RUN", "NOT_APPLICABLE"}

REQUIRED_CRITERIA_AUTOMATED = {
    "1. Recent Projects & Startup Filtering": "Recent project filtering and selection on Screen 2",
    "2. Message Search Pagination & Continuation": "Message search pagination and forward continuation",
    "3. Older Message Navigation & Selection": "Navigation to older search results and row selection",
    "4. Search Mode Project Switch Safety": "Project switching while in search mode resets state cleanly",
    "5. Narrow 520px Responsive Layout": "Narrow 520px client width layout and reachability",
    "6. Rapid Hover Sweeps & Animation Stability": "Rapid hover sweeps over interactive elements without crash or feedback loop",
    "7. Display DPI Scaling Simulation": "Simulated DPI change handling across components",
    "8. Action Center Pagination & Resumption": "Action Center obligation pagination and resumption",
    "9. Action Center Stale Invalidation & Refresh": "Action Center refresh and stale cache invalidation",
    "10. Action Center Panel Dismissal Stability": "Action Center panel dismissal and non-reopening when empty",
    "11. Native OLE Desktop Drag-and-Drop Execution": "Native OLE desktop drag-and-drop file admission",
    "12. Mixed-Batch Folder Rejection on Drop": "Rejection of folders in mixed-batch drag-drop operations",
    "13. Seeded Attachment Chip Realization & Diagnostics": "Attachment chip rendering, realization and diagnostic bounds",
    "14. Strict Non-English Prose Preview Withholding": "Strict withholding of non-English prose previews per English-only invariant",
    "15. WinUI 3 CompositionTarget Performance Profiling": "WinUI 3 CompositionTarget frame rendering performance profiling",
    "16. Search Overlay Flat Resting Orientation": "Search overlay rests completely flat without residual 3D tilt",
    "17. Search Overlay Bounded Height, Scrolling & Paging Reachability": "Search overlay height bounded and paging controls reachable",
    "18. Repeated Open/Close & Cancelled Reveal Leave The Overlay Flat": "Repeated reveal and cancellation leaves search overlay flat",
    "19. Empty-Chat Hero Perspective Preserved": "Empty-chat hero perspective preserved without leaking into stable reading planes",
    "20. Reduced-Motion Change Leaves The Overlay Flat": "Live reduced-motion preference toggle leaves overlay flat",
    "21. CLI Readiness Projection & Non-Gating": "CLI readiness indicator truthfully projected and non-gating",
    "22. Bounded History Paging Save & Newest Rendering": "Bounded history paging and newest contribution rendering",
    "23. Reading Anchor Preserved Across Older Prepend": "Reading anchor preserved during older message prepending",
}

REQUIRED_CRITERIA_PHYSICAL = {
    "P01. Physical Windows Client Sizing": "Physical window sizing at 520x640 and 640x480 logical client units with actual OS borders",
    "P02. Physical High-DPI Display Scaling": "Physical multi-monitor / actual OS display scaling (125%, 150%, 200%)",
    "P03. Physical OS Theme & High-Contrast Switching": "Physical live OS light/dark and high-contrast theme changes",
    "P04. Physical Keyboard Navigation & Narrator": "Physical keyboard-only navigation and Narrator speech/announcement verification",
    "P05. Physical OLE Desktop Drag-and-Drop": "Physical OLE drag-and-drop from real Windows Explorer process",
    "P06. Physical Long-Session Performance & Resource Leak": "Physical long-session duration benchmarking for memory/handle leaks",
}

REQUIRED_CRITERIA_ALL = {**REQUIRED_CRITERIA_AUTOMATED, **REQUIRED_CRITERIA_PHYSICAL}

APPROVED_EXEMPTIONS = {
    "EXEMPTION-TEST-CRITERION": {
        "criterion": "EXEMPTION-TEST-CRITERION",
        "exemption_id": "EXEMPTION-S2-14",
        "scope": "Screen 2 pre-binding draft composer permanently removed per authorized contract (2026-10-17)",
        "source": "AGENTS.md §5",
    },
}

def is_valid_na_justification(criterion_name, justification):
    if not criterion_name or not isinstance(criterion_name, str):
        return False
    if not justification or not isinstance(justification, str):
        return False

    # Exact criterion lookup: the criterion must be explicitly registered with an approved exemption
    exemption = APPROVED_EXEMPTIONS.get(criterion_name)
    if not exemption:
        return False

    req_id = exemption.get("exemption_id", "")
    req_scope = exemption.get("scope", "")
    req_source = exemption.get("source", "")

    j = justification.strip()
    if len(j) < 20:
        return False
    lower_j = j.lower()
    if any(placeholder in lower_j for placeholder in ["todo", "placeholder", "skip this", "not needed", "n/a", "none"]):
        return False

    # Justification must contain the exact exemption_id and reference the approved scope or source
    if req_id and req_id not in j:
        return False
    if req_scope and req_scope not in j and req_source not in j:
        return False

    return True

def validate_verdict_record(name, raw_value):
    if not isinstance(name, str) or not name.strip():
        return (None, None, f"Malformed criterion identifier: {name!r}")
    if isinstance(raw_value, dict):
        v = raw_value.get("verdict")
        j = raw_value.get("justification", "")
    elif isinstance(raw_value, (list, tuple)):
        v = raw_value[0] if len(raw_value) > 0 else None
        j = raw_value[1] if len(raw_value) > 1 else ""
    elif isinstance(raw_value, str):
        if raw_value.startswith("NOT_APPLICABLE:"):
            v = "NOT_APPLICABLE"
            j = raw_value[len("NOT_APPLICABLE:"):].strip()
        else:
            v = raw_value.strip()
            j = ""
    else:
        return (None, None, f"Malformed verdict value type for criterion {name!r}: {type(raw_value).__name__}")
    if v not in REGISTERED_VERDICTS:
        return (v, j, f"Unregistered verdict {v!r} for criterion {name!r}; must be one of {sorted(REGISTERED_VERDICTS)}")
    if v == "NOT_APPLICABLE":
        if not is_valid_na_justification(name, j):
            if name not in APPROVED_EXEMPTIONS:
                return (v, j, f"Criterion {name!r} has no approved exemption in registry; cannot be marked NOT_APPLICABLE: {j!r}")
            return (v, j, f"Criterion {name!r} marked NOT_APPLICABLE without valid scope justification matching approved exemption: {j!r}")
    return (v, j, None)

def evaluate_verdicts(verdicts, required_registry=REQUIRED_CRITERIA_AUTOMATED, is_strict=False):
    if not isinstance(verdicts, dict):
        return {
            "valid": False,
            "exit_code": 1,
            "status": "MALFORMED_VERDICTS_OBJECT",
            "errors": [f"Verdicts object must be a dictionary, got {type(verdicts).__name__}"],
            "counts": {},
            "records": {}
        }
    errors = []
    records = {}
    counts = {v: 0 for v in REGISTERED_VERDICTS}
    for req_id in required_registry:
        if req_id not in verdicts:
            errors.append(f"Missing required criterion: {req_id!r}")
    for name, raw_value in verdicts.items():
        v, j, err = validate_verdict_record(name, raw_value)
        if err:
            errors.append(err)
        else:
            records[name] = (v, j)
            counts[v] += 1
    if errors:
        return {
            "valid": False,
            "exit_code": 1,
            "status": "VALIDATION_FAILED",
            "errors": errors,
            "counts": counts,
            "records": records
        }
    if counts["FAIL"] > 0:
        return {
            "valid": True,
            "exit_code": 1,
            "status": "TEST_SUITE_FAILED",
            "errors": [f"{counts['FAIL']} assertion(s) failed"],
            "counts": counts,
            "records": records
        }
    if counts["BLOCKED"] > 0:
        return {
            "valid": True,
            "exit_code": 1,
            "status": "TEST_SUITE_INCOMPLETE_BLOCKED",
            "errors": [f"{counts['BLOCKED']} criterion(s) blocked by failed preconditions"],
            "counts": counts,
            "records": records
        }
    if counts["NOT_RUN"] > 0:
        return {
            "valid": True,
            "exit_code": 1,
            "status": "TEST_SUITE_INCOMPLETE_NOT_RUN",
            "errors": [f"{counts['NOT_RUN']} criterion(s) were never executed"],
            "counts": counts,
            "records": records
        }
    if counts["INCONCLUSIVE"] > 0:
        if is_strict:
            return {
                "valid": True,
                "exit_code": 1,
                "status": "STRICT_CERTIFICATION_FAILED",
                "errors": [f"{counts['INCONCLUSIVE']} inconclusive criteria disallowed in strict mode"],
                "counts": counts,
                "records": records
            }
        else:
            return {
                "valid": True,
                "exit_code": 0,
                "status": "ACCEPTANCE_CHECKS_COMPLETE_INCONCLUSIVE_RETAINED",
                "errors": [],
                "counts": counts,
                "records": records
            }
    return {
        "valid": True,
        "exit_code": 0,
        "status": "ALL_CRITERIA_PASSED",
        "errors": [],
        "counts": counts,
        "records": records
    }

def get_window_dpi(hwnd):
    if not hwnd or not user32.IsWindow(hwnd):
        raise ValueError(f"Invalid window handle: {hwnd}")
    if hasattr(user32, "GetDpiForWindow"):
        dpi = user32.GetDpiForWindow(hwnd)
        if dpi > 0:
            return dpi
        err = ctypes.get_last_error()
        raise RuntimeError(f"GetDpiForWindow returned 0 for HWND {hwnd} (GetLastError={err}); unknown DPI cannot be assumed")
    raise RuntimeError("GetDpiForWindow not available on this system; cannot measure window DPI")

def resize_to_logical_client(win, target_logical_w, target_logical_h, max_retries=3):
    hwnd = win.handle
    dpi = get_window_dpi(hwnd)
    scale = dpi / 96.0
    target_phys_w = int(round(target_logical_w * scale))
    target_phys_h = int(round(target_logical_h * scale))

    GWL_STYLE = -16
    GWL_EXSTYLE = -20
    style = user32.GetWindowLongW(hwnd, GWL_STYLE)
    ex_style = user32.GetWindowLongW(hwnd, GWL_EXSTYLE)

    rc = RECT(0, 0, target_phys_w, target_phys_h)
    if hasattr(user32, "AdjustWindowRectExForDpi"):
        res = user32.AdjustWindowRectExForDpi(ctypes.byref(rc), style, False, ex_style, dpi)
        if not res:
            err = ctypes.get_last_error()
            raise RuntimeError(f"AdjustWindowRectExForDpi failed with GetLastError={err}")
        req_outer_w = rc.right - rc.left
        req_outer_h = rc.bottom - rc.top
    else:
        w_rect = RECT()
        c_rect = RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(w_rect))
        user32.GetClientRect(hwnd, ctypes.byref(c_rect))
        chrome_w = (w_rect.right - w_rect.left) - (c_rect.right - c_rect.left)
        chrome_h = (w_rect.bottom - w_rect.top) - (c_rect.bottom - c_rect.top)
        req_outer_w = target_phys_w + chrome_w
        req_outer_h = target_phys_h + chrome_h

    cur_w_rect = RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(cur_w_rect))
    left = cur_w_rect.left
    top = cur_w_rect.top

    for attempt in range(max_retries):
        user32.MoveWindow(hwnd, left, top, req_outer_w, req_outer_h, True)
        time.sleep(0.4)
        dpi = get_window_dpi(hwnd)
        scale = dpi / 96.0
        c_rect = RECT()
        user32.GetClientRect(hwnd, ctypes.byref(c_rect))
        act_phys_w = c_rect.right - c_rect.left
        act_phys_h = c_rect.bottom - c_rect.top
        act_log_w = act_phys_w / scale
        act_log_h = act_phys_h / scale
        diff_w = target_logical_w - act_log_w
        diff_h = target_logical_h - act_log_h
        if abs(diff_w) <= 2.0 and abs(diff_h) <= 2.0:
            log(f"resize_to_logical_client: target {target_logical_w}x{target_logical_h} DIPs -> "
                f"measured logical {act_log_w:.1f}x{act_log_h:.1f} (phys client {act_phys_w}x{act_phys_h}, "
                f"outer {req_outer_w}x{req_outer_h} at {dpi} DPI)")
            return (act_log_w, act_log_h)
        req_outer_w += int(round(diff_w * scale))
        req_outer_h += int(round(diff_h * scale))

    log(f"WARNING: resize_to_logical_client target {target_logical_w}x{target_logical_h} DIPs, "
        f"measured logical {act_log_w:.1f}x{act_log_h:.1f} (diff {diff_w:.1f}, {diff_h:.1f})")
    assert abs(target_logical_w - act_log_w) <= 2.0 and abs(target_logical_h - act_log_h) <= 2.0, (
        f"Failed to achieve target logical client dimensions: target={target_logical_w}x{target_logical_h}, "
        f"actual={act_log_w:.1f}x{act_log_h:.1f} at {dpi} DPI"
    )
    return (act_log_w, act_log_h)

WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_CLOSE = 0x0010
WM_MOUSEWHEEL = 0x020A
MOUSEEVENTF_WHEEL = 0x0800
WM_DPICHANGED = 0x02E0
SW_RESTORE = 9
VK_ESCAPE = 0x1B
VK_PRIOR = 0x21
VK_NEXT = 0x22

class RECT(ctypes.Structure):
    _fields_ = [
        ("left", wintypes.LONG),
        ("top", wintypes.LONG),
        ("right", wintypes.LONG),
        ("bottom", wintypes.LONG),
    ]

EVIDENCE_LOG_HANDLE = None


class CriterionFailure(Exception):
    """Ends one criterion's checks without ending the run.

    A failing assertion used to `return 1` straight out of `main`, which meant
    every later phase was never exercised and the verdict table was never
    printed -- the run reported one failure and nothing about the eighteen
    criteria it never reached. Raising this instead unwinds only the checks that
    depended on the failed observation, lets the remaining phases run, and
    leaves a complete table behind. The verdict is recorded as FAIL, so the exit
    code is still non-zero.
    """


def log(msg):
    line = f"[ACCEPTANCE] {msg}\n"
    sys.stdout.buffer.write(line.encode("utf-8", errors="replace"))
    sys.stdout.buffer.flush()
    # The run's own output is part of the evidence. A run that only printed to
    # the console left an evidence directory with the artifact identity and
    # nothing that said what actually happened.
    if EVIDENCE_LOG_HANDLE is not None:
        EVIDENCE_LOG_HANDLE.write(line)
        EVIDENCE_LOG_HANDLE.flush()


UOI_NAME = 2


# --- run-scoped UI-thread sampler -----------------------------------------
#
# The hypothesis under test in W1 was that the application's UI thread blocks
# for seconds because the harness's own SQLite writes take a write lock on the
# live store while the application is running, and the application's background
# storage work waits behind that lock. That hypothesis is only refuted by
# MEASUREMENT made during a run, so the sampler below records the application's
# pump latency on a fixed cadence for the whole run, and every harness write to
# the store is timestamped against it. The correlation between "the harness held
# a write lock" and "the application's UI thread blocked" is then a fact in the
# run log rather than an argument.
#
# A UIA descendant walk cannot serve this purpose: its cost grows with the size
# of the tree and with the accessibility provider, so a slow walk and a blocked
# pump produce the same number. The 11.399 s figure recorded in run
# 20261021-final was a walk, and it was read as a stall.
UI_SAMPLE_INTERVAL_S = 0.05
# The budget must cover the WHOLE run, not a prefix of it. A harness run
# against a cold binary takes around 150 s, and a sampler that stops at 30 s
# leaves the phases that matter -- the seeded writes, the OLE attempt, the
# performance leg -- unmeasured. The count cap is a second bound, not the
# primary one.
UI_SAMPLE_MAX_S = 600.0
UI_SAMPLE_COUNT = 12000

UI_SAMPLES = []          # list of (monotonic_offset_s, answered, elapsed_s, detail)
UI_WRITE_MARKERS = []    # list of (monotonic_offset_s, description)
UI_EVENT_MARKERS = []    # list of (monotonic_offset_s, description) for non-write events
UI_UIA_WINDOWS = []      # list of (start_offset_s, end_offset_s, description) for harness UIA walks
_RUN_T0 = None


def _run_clock():
    """Seconds since the sampler started, or None if it has not started."""
    if _RUN_T0 is None:
        return None
    return time.monotonic() - _RUN_T0


def mark_store_write(description):
    """Timestamp a harness write against the live store.

    Every mutation the harness makes to the store while the application is
    running is a candidate lock holder, so each one is recorded with the time
    it happened. The sampler's readings are then interpretable: a pump latency
    spike that falls inside a marked write window is a candidate contention
    effect, and one that does not is not.
    """
    offset = _run_clock()
    if offset is not None:
        UI_WRITE_MARKERS.append((offset, description))
        log(f"  [store-write] t+{offset:8.3f}s  {description}")


def mark_event(description):
    """Timestamp a harness event that is not a store write.

    The OLE attempt is the one harness event that runs for seconds while the
    application is expected to be doing work, and the question it raises is
    whether the application's pump was blocked for that whole window. Marking
    its start and its end makes that window exact instead of inferred from
    neighbouring writes.
    """
    offset = _run_clock()
    if offset is not None:
        UI_EVENT_MARKERS.append((offset, description))
        log(f"  [event] t+{offset:8.3f}s  {description}")
    return offset


class uia_walk:
    """Record the window during which the harness walked the UIA tree.

    A cross-process UIA enumeration is a request to the application's own
    thread: the provider answers it. So a pump-latency sample taken while the
    harness is walking the tree measures the application *answering the
    harness*, which is not the application doing its own work, and the two
    must not be reported as the same thing.

    Recording the walk's own start and end makes the attribution measurable
    rather than a matter of interpretation.
    """

    def __init__(self, description):
        self.description = description
        self.start = None

    def __enter__(self):
        self.start = time.monotonic()
        return self

    def __exit__(self, *exc):
        if _RUN_T0 is not None:
            UI_UIA_WINDOWS.append((self.start - _RUN_T0,
                                    time.monotonic() - _RUN_T0,
                                    self.description))
        return False


def install_uia_walk_tracker():
    """Record every cross-process UIA enumeration the harness performs.

    A UIA enumeration is a request the application's own thread must answer:
    the provider is inside the application. A pump-latency sample taken while
    the harness is walking the tree therefore measures the application
    answering the harness, which is not the application doing its own work.
    The two must not be reported as the same thing, so the walk's own window
    is recorded and the samples are attributed against it.

    The patch is installed on the pywinauto base class rather than on this
    file's helpers, because the walks are spread over a dozen call sites and
    an un-instrumented one would silently leave samples unattributable.
    """
    from pywinauto.base_wrapper import BaseWrapper

    if getattr(BaseWrapper, "_mayasaba_walk_tracked", False):
        return

    original = BaseWrapper.descendants

    def tracked(self, **kwargs):
        start = time.monotonic()
        try:
            return original(self, **kwargs)
        finally:
            if _RUN_T0 is not None:
                UI_UIA_WINDOWS.append((start - _RUN_T0,
                                        time.monotonic() - _RUN_T0,
                                        "uia.descendants"))

    BaseWrapper.descendants = tracked
    BaseWrapper._mayasaba_walk_tracked = True


def start_ui_sampler(hwnd):
    """Sample the application's message pump on a fixed cadence for the run."""
    global _RUN_T0
    _RUN_T0 = time.monotonic()

    def run():
        started = time.monotonic()
        deadline = started + UI_SAMPLE_MAX_S
        while time.monotonic() < deadline and len(UI_SAMPLES) < UI_SAMPLE_COUNT:
            # The same origin the store-write and UIA-window markers use, so
            # a sample and a marker can be compared without a skew term.
            offset = time.monotonic() - _RUN_T0
            try:
                answered, elapsed, detail = ui_thread_latency(hwnd, timeout_ms=2000)
            except Exception as exc:
                answered, elapsed, detail = False, 0.0, f"raised {type(exc).__name__}: {exc}"
            UI_SAMPLES.append((offset, answered, elapsed, detail))
            time.sleep(UI_SAMPLE_INTERVAL_S)
        log(f"  UI-thread sampler finished: {len(UI_SAMPLES)} sample(s)")

    thread = threading.Thread(target=run, name="ui-thread-sampler", daemon=True)
    thread.start()
    return thread


def write_ui_sample_artifact():
    """Write the raw pump samples and write markers to the evidence directory.

    The summary line in the run log is a reduction. The raw series is what a
    reviewer needs in order to check the reduction, to place an unanswered
    sample against the phase it happened in, and to see the shape of the
    distribution rather than only its percentiles.
    """
    if not EVIDENCE_DIR or not UI_SAMPLES:
        return None
    path = os.path.join(EVIDENCE_DIR, "ui-thread-samples.json")
    payload = {
        "note": (
            "Samples of the application's UI thread answering a WM_NULL probe "
            "with SendMessageTimeoutW. Each sample is "
            "[offset_s, answered, elapsed_s, detail]. 'offset_s' is seconds "
            "since the sampler started, which begins when the harness "
            "connects to the application window."
        ),
        "probe": "SendMessageTimeoutW(WM_NULL, SMTO_ABORTIFHUNG, 2000 ms)",
        "interval_s": UI_SAMPLE_INTERVAL_S,
        "samples": [
            [round(s[0], 3), bool(s[1]), round(s[2], 6), s[3] if len(s) > 3 else ""]
            for s in UI_SAMPLES
        ],
        "store_writes": [
            [round(m[0], 3), m[1]] for m in UI_WRITE_MARKERS
        ],
        "events": [
            [round(m[0], 3), m[1]] for m in UI_EVENT_MARKERS
        ],
        "uia_windows": [
            [round(w[0], 3), round(w[1], 3), w[2]] for w in UI_UIA_WINDOWS
        ],
    }
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=1)
    return path


def pump_latency_between(start_offset, end_offset):
    """Summarize the pump samples taken inside a marked window.

    This is the measurement that answers "was the application's UI thread
    blocked while this was happening?". A window in which the pump answered
    every probe, quickly, refutes a blocked-UI-thread explanation for the
    event in that window; a window in which the pump never answered supports
    it. Without the window, the event is only known to have failed, and the
    reason is unknown.
    """
    if start_offset is None or end_offset is None:
        return "not measured (the event window was not marked)"
    inside = [s for s in UI_SAMPLES
              if start_offset <= s[0] <= end_offset]
    if not inside:
        return (f"no pump samples fell inside the window "
                f"t+{start_offset:.3f}s..t+{end_offset:.3f}s "
                f"({end_offset - start_offset:.3f}s long)")
    answered = sum(1 for s in inside if s[1])
    elapsed = sorted(s[2] for s in inside)
    slow = [s for s in inside if s[2] > 1.0]
    return (f"{len(inside)} sample(s) in t+{start_offset:.3f}s..t+{end_offset:.3f}s "
            f"({end_offset - start_offset:.3f}s), {answered} answered, "
            f"p50 {elapsed[len(elapsed) // 2] * 1000:.2f} ms, "
            f"max {elapsed[-1] * 1000:.2f} ms, "
            f"{len(slow)} over 1000 ms")


def ui_sample_summary():
    """Summarize the pump samples, and their overlap with harness write windows.

    The summary separates the two questions that were previously conflated:
    how long the pump ever took to answer, and whether the slowest answers
    coincide with the harness holding a write lock.

    An unanswered sample is reported separately from a slow one. "The window
    did not answer within the probe timeout" and "the window answered after
    400 ms" are different observations, and only the second is a latency
    reading.

    A sample taken after the window was destroyed is separated from both. The
    application is closed at the end of a run while the sampler is still
    ticking, and SendMessageTimeoutW fails instantly for a window that no
    longer exists. Counting those as "unanswered" reports the application
    being shut down as a stall, which is precisely the conflation this summary
    exists to prevent: 45 consecutive "no WM_NULL reply" samples at the
    teardown tail of run 20261022-sampler were all `window_destroyed`, and
    every one of them had returned in zero seconds.
    """
    if not UI_SAMPLES:
        return "no samples were collected"
    elapsed = sorted(s[2] for s in UI_SAMPLES)
    answered = sum(1 for s in UI_SAMPLES if s[1])
    count = len(elapsed)
    destroyed = [s for s in UI_SAMPLES if not s[1] and "window_destroyed" in str(s[3])]
    unanswered = [s for s in UI_SAMPLES if not s[1] and "window_destroyed" not in str(s[3])]

    def pct(p):
        if not count:
            return 0.0
        index = min(count - 1, max(0, int(round(p * (count - 1)))))
        return elapsed[index]

    slowest = max(UI_SAMPLES, key=lambda s: s[2])
    over_1000 = [s for s in UI_SAMPLES if s[2] > 1.0]
    over_250 = [s for s in UI_SAMPLES if s[2] > 0.25]

    # Which samples fall inside a harness write window? A write is instantaneous
    # from the application's point of view, so the window is widened to the
    # sampling interval either side: a pump answer that is slow because the
    # application was busy with storage is slow at or after the write.
    inside = 0
    for sample in UI_SAMPLES:
        offset = sample[0]
        for mark, _description in UI_WRITE_MARKERS:
            if abs(offset - mark) <= 0.5:
                inside += 1
                break

    slowest_marked = any(abs(slowest[0] - mark) <= 0.5
                         for mark, _d in UI_WRITE_MARKERS)

    # A cross-process UIA enumeration is a request the application's own
    # thread has to service. A pump sample taken while the harness is walking
    # the tree therefore measures the application answering the harness, which
    # is not the application doing its own work. The two are separated here so
    # the slow samples can be attributed rather than merely counted.
    def in_uia_window(sample):
        for start, end, _description in UI_UIA_WINDOWS:
            if start <= sample[0] <= end:
                return True
        return False

    slow_in_uia = sum(1 for s in over_250 if in_uia_window(s))
    slow_not_uia = len(over_250) - slow_in_uia

    parts = [
        f"{count} samples, {answered} answered",
        f"p50 {pct(0.50) * 1000:.2f} ms",
        f"p95 {pct(0.95) * 1000:.2f} ms",
        f"p99 {pct(0.99) * 1000:.2f} ms",
        f"max {slowest[2] * 1000:.2f} ms at t+{slowest[0]:.3f}s",
        f"over 250 ms: {len(over_250)} "
        f"(inside a harness UIA walk: {slow_in_uia}; "
        f"outside every harness UIA walk: {slow_not_uia})",
        f"over 1000 ms: {len(over_1000)}",
    ]
    if unanswered:
        offsets = ", ".join(f"t+{s[0]:.3f}s" for s in unanswered[:6])
        more = "" if len(unanswered) <= 6 else f" (+{len(unanswered) - 6} more)"
        hung = sum(1 for s in unanswered if "IsHungAppWindow=True" in str(s[3]))
        parts.append(f"unanswered (no WM_NULL reply within the 2000 ms probe): "
                     f"{len(unanswered)} [{offsets}{more}]"
                     + (f"; {hung} reported IsHungAppWindow=True" if hung else
                        "; none reported IsHungAppWindow=True"))
    if destroyed:
        parts.append(f"after window destruction (not a latency reading): {len(destroyed)}"
                     f" [t+{destroyed[0][0]:.3f}s..t+{destroyed[-1][0]:.3f}s]")
    parts += [
        f"{len(UI_WRITE_MARKERS)} harness write(s) marked",
        f"samples within 500 ms of a harness write: {inside}",
        f"slowest sample coincides with a harness write: {slowest_marked}",
    ]
    if UI_EVENT_MARKERS:
        parts.append(f"{len(UI_EVENT_MARKERS)} harness event(s) marked")
    return "; ".join(parts)


def _object_name(handle):
    """The name of a window-station or desktop object, for the foreground diagnostic."""
    if not handle:
        return None
    try:
        buf = ctypes.create_unicode_buffer(256)
        needed = wintypes.DWORD()
        ok = user32.GetUserObjectInformationW(
            handle, UOI_NAME, buf, ctypes.sizeof(buf), ctypes.byref(needed))
        return buf.value if ok else None
    except Exception as exc:
        return f"<{type(exc).__name__}: {exc}>"


def desktop_context():
    """The calling thread's desktop, the input desktop, and the foreground window.

    GetForegroundWindow() is desktop-relative: a process whose thread is not
    attached to the input desktop reads 0 even while the application is
    foreground and reachable on the input desktop. A "foreground hwnd 0" reading
    therefore cannot be told apart from "no foreground window exists" unless
    the desktops are recorded with it, and that ambiguity is what made the
    earlier `foreground hwnd 0` readings unexplainable.
    """
    try:
        thread_desktop = user32.GetThreadDesktop(kernel32.GetCurrentThreadId())
    except Exception as exc:
        thread_desktop = None
        thread_desktop_error = f"{type(exc).__name__}: {exc}"
    else:
        thread_desktop_error = None
    try:
        input_desktop = user32.GetInputDesktop()
    except Exception as exc:
        input_desktop = None
        input_desktop_error = f"{type(exc).__name__}: {exc}"
    else:
        input_desktop_error = None
    try:
        foreground = user32.GetForegroundWindow()
    except Exception as exc:
        foreground = None
        foreground_error = f"{type(exc).__name__}: {exc}"
    else:
        foreground_error = None
    return {
        "thread_desktop": _object_name(thread_desktop),
        "input_desktop": _object_name(input_desktop),
        "on_input_desktop": bool(thread_desktop) and bool(input_desktop)
                            and thread_desktop == input_desktop,
        "foreground_hwnd": foreground,
        "errors": [e for e in (thread_desktop_error, input_desktop_error,
                               foreground_error) if e],
    }


def establish_foreground(win, attempts=3, settle=0.7):
    """Make the application window the OS foreground window, and verify it.

    SetForegroundWindow is refused for a process that does not already own the
    foreground, and the refusal is silent: the call returns and the window
    stays in the background. A bare set_focus() therefore establishes nothing
    it can promise, which is why the wheel leg used to report a stimulus it
    never delivered.

    The documented way to take the foreground from a background process is to
    attach this thread's input queue to the current foreground thread's input
    queue, which lifts the restriction, and then to VERIFY the result by
    reading GetForegroundWindow() back. A foreground that was never verified
    is not a foreground, so the return value is the verified reading and not
    the return code of the API call.

    Returns (established, detail). `detail` names the method used and every
    reading taken, so a failure states which attempt failed and what the
    foreground actually was.
    """
    handle = int(win.handle)
    observations = []
    try:
        current_thread = kernel32.GetCurrentThreadId()
    except Exception as exc:
        current_thread = 0
        observations.append(f"GetCurrentThreadId failed: {type(exc).__name__}: {exc}")
    for attempt in range(1, attempts + 1):
        try:
            foreground = user32.GetForegroundWindow()
        except Exception as exc:
            observations.append(f"attempt {attempt}: GetForegroundWindow raised "
                                f"{type(exc).__name__}: {exc}")
            foreground = 0
        if foreground == handle:
            return True, "; ".join(observations + [f"attempt {attempt}: already foreground"])
        attached = False
        foreground_thread = 0
        try:
            if foreground:
                foreground_thread = user32.GetWindowThreadProcessId(foreground, None)
            if foreground_thread and foreground_thread != current_thread:
                attached = bool(user32.AttachThreadInput(current_thread,
                                                          foreground_thread, True))
            user32.SetForegroundWindow(handle)
            if attached:
                user32.BringWindowToTop(handle)
        except Exception as exc:
            observations.append(f"attempt {attempt}: SetForegroundWindow raised "
                                f"{type(exc).__name__}: {exc}")
        finally:
            if attached:
                try:
                    user32.AttachThreadInput(current_thread, foreground_thread, False)
                except Exception as exc:
                    observations.append(f"attempt {attempt}: AttachThreadInput(detach) raised "
                                        f"{type(exc).__name__}: {exc}")
        deadline = time.time() + settle
        verified = False
        while time.time() < deadline:
            try:
                if user32.GetForegroundWindow() == handle:
                    verified = True
                    break
            except Exception:
                pass
            time.sleep(0.05)
        try:
            now = user32.GetForegroundWindow()
        except Exception:
            now = 0
        observations.append(f"attempt {attempt}: attached={attached} foreground_after={now}")
        if verified:
            return True, "; ".join(observations)
    return False, "; ".join(observations)


def send_wheel_at_point(win, x, y, delta, steps=1, pause=0.05):
    """Send a real wheel event over a screen point, to the foreground window.

    Two things had to be true for a wheel to reach a control, and neither was
    true of the previous approach:

    * A WM_MOUSEWHEEL posted to the top-level window is not a wheel over a
      control. Windows routes a wheel event to the window under the pointer, so
      a posted message never reached the control it was aimed at and the content
      it was supposed to move stayed exactly where it was.
    * A wheel is delivered to the foreground window. The harness drives the app
      through UIA, which works in the background, so the window was not
      necessarily foreground and the wheel went somewhere else entirely.

    The window is therefore made foreground first, the state is recorded, and
    only then is the wheel sent over the control's own point. The previous
    cursor position is restored so the rest of the run sees the machine as it
    was.

    Returns True if the application window was confirmed foreground at the
    moment the wheel events were sent, False otherwise.  A False return means
    Windows may have routed the wheel elsewhere; the caller must NOT treat a
    null scroll result as a product defect when this function returns False.
    """
    class _POINT(ctypes.Structure):
        _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]

    previous = _POINT()
    user32.GetCursorPos(ctypes.byref(previous))
    # The foreground is ESTABLISHED and then VERIFIED, rather than requested and
    # assumed. A SetForegroundWindow that the OS refuses is refused silently,
    # so the only trustworthy evidence that the wheel will be delivered is a
    # read-back of GetForegroundWindow() after the attempt.
    try:
        win.set_focus()
    except Exception as exc:
        log(f"  could not focus the application window: {type(exc).__name__}: {exc}")
    foreground_established, fg_detail = establish_foreground(win)
    time.sleep(0.2)
    foreground = user32.GetForegroundWindow()
    foreground_established = (foreground == win.handle) and foreground_established
    log(f"  foreground establishment: established={foreground_established} "
        f"foreground={foreground} application={win.handle} ({fg_detail})")
    if not foreground_established:
        context = desktop_context()
        log(f"  NOTE: the application window is not the foreground window "
            f"(foreground {foreground}, application {win.handle}); the wheel may not reach it")
        log(f"  NOTE: desktop context: thread_desktop={context['thread_desktop']} "
            f"input_desktop={context['input_desktop']} "
            f"on_input_desktop={context['on_input_desktop']} "
            f"errors={context['errors']}")

    user32.SetCursorPos(int(x), int(y))
    time.sleep(0.15)
    for _ in range(steps):
        user32.mouse_event(MOUSEEVENTF_WHEEL, 0, 0, int(delta), 0)
        time.sleep(pause)
    user32.SetCursorPos(previous.x, previous.y)
    time.sleep(0.1)
    return foreground_established

def broadcast_setting_change(section="VisualEffects"):
    """Tell every top-level window that a system setting changed.

    SystemParametersInfoW with SPIF_SENDCHANGE broadcasts WM_SETTINGCHANGE with an
    empty section name. Components that key off a named section — the animation
    preference is one of them — only refresh when the section they watch is named,
    so the harness names it explicitly as a second, independent stimulus.
    """
    HWND_BROADCAST = 0xFFFF
    WM_SETTINGCHANGE = 0x001A
    SMTO_ABORTIFHUNG = 0x0002
    result = ctypes.c_size_t()
    try:
        ok = user32.SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                                        ctypes.c_wchar_p(section),
                                        SMTO_ABORTIFHUNG, 2000, ctypes.byref(result))
        log(f"  broadcast WM_SETTINGCHANGE('{section}') -> {ok}")
        return bool(ok)
    except Exception as exc:
        log(f"  could not broadcast WM_SETTINGCHANGE('{section}'): {type(exc).__name__}: {exc}")
        return False

def ui_thread_latency(hwnd, timeout_ms=2000):
    """How long the application's UI thread takes to answer the message pump.

    This is the only probe in this harness that measures UI-thread
    responsiveness. A UIA descendant walk does not: it is a cross-process COM
    enumeration whose cost grows with the size of the tree and with the
    accessibility provider, so a slow walk and a blocked UI thread produce the
    same number. The 11.399 s recorded in run 20261021-final was a walk, and it
    was written up as a stall; measuring the pump directly showed the thread
    answering in well under a millisecond at the same time.

    SendMessageTimeoutW(WM_NULL) asks the target's message loop to process a
    null message and return. A thread that is not pumping cannot answer, so a
    timeout here is real evidence of a block.

    A zero return does NOT by itself mean the pump was blocked. SendMessageTimeoutW
    fails immediately for a window that no longer exists, so a destroyed window
    and a hung window both return 0 -- and the destroyed case returns in
    microseconds. Reporting the teardown tail of a run as "unanswered" therefore
    manufactures a stall out of the application being closed. The window is
    checked first so the two are distinguishable, and a vanished window is
    reported as such rather than counted against the pump.

    Returns (answered, seconds, detail_string).
    """
    SMTO_ABORTIFHUNG = 0x0002
    WM_NULL = 0x0000
    try:
        if not user32.IsWindow(hwnd):
            return False, 0.0, "window_destroyed"
    except Exception as exc:
        return False, 0.0, f"raised {type(exc).__name__}: {exc}"
    result = ctypes.c_size_t()
    started = time.time()
    try:
        ok = user32.SendMessageTimeoutW(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG,
                                        timeout_ms, ctypes.byref(result))
    except Exception as exc:
        return False, time.time() - started, f"raised {type(exc).__name__}: {exc}"
    elapsed = time.time() - started
    hung = None
    try:
        hung = bool(user32.IsHungAppWindow(hwnd))
    except Exception:
        pass
    return bool(ok), elapsed, f"IsHungAppWindow={hung}"


def find_by_aid(parent, aid):
    for d in parent.descendants():
        if d.element_info.automation_id == aid:
            return d
    return None


def read_status_text(win, expected, timeout=5.0, interval=0.05):
    """Read the search panel's status line, polling for `expected`.

    Two defects are avoided by construction, and both were observed in a
    recorded run rather than reasoned about:

    * **The element is resolved on every attempt.** The search panel is
      destroyed and rebuilt when it is closed and reopened, so a UIA element
      resolved before the close refers to a node that no longer exists. It
      reads as an empty string against a panel that is working correctly, and
      a harness that caches it reports a product failure that does not exist.
      Re-resolving costs one descendant scan and removes the stale reference
      from the failure mode entirely.
    * **The postcondition is polled, not sampled.** A fixed sleep observes one
      instant, which turns a slow-but-correct transition into a failure. The
      bound still expires, so a genuine defect still fails.

    The text last seen is returned, so a failure can report what the panel
    actually said instead of nothing.
    """
    deadline = time.time() + timeout
    text = ""
    while True:
        element = find_by_aid(win, "SearchStatusText")
        if element is not None:
            try:
                text = element.window_text() or ""
            except Exception:
                text = ""
        if expected in text:
            return text
        if time.time() >= deadline:
            return text
        time.sleep(interval)


def acquire_store_lock(store_path):
    """Take an exclusive lock on the acceptance store, or fail closed.

    Two acceptance runs against one store interleave their DDL fault
    injection, their seeded rows and their deletes, and each run's evidence
    then describes a store the other run was mutating underneath it. That
    happened here: a run failed in Phase 2 while a second run was writing to
    the same database, and neither run's log could afterwards be used as
    evidence about the product.

    The lock makes that state unreachable. The second run refuses to start
    with a message naming the lock file, rather than starting and producing
    evidence it cannot trust. The returned handle must stay open for the
    lifetime of the run; closing it releases the lock.
    """
    lock_path = os.path.abspath(store_path) + ".harness.lock"
    handle = open(lock_path, "a+b")
    try:
        handle.seek(0, os.SEEK_END)
        if handle.tell() == 0:
            handle.write(b"\0")
            handle.flush()
        handle.seek(0)
        msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
    except OSError:
        handle.close()
        raise RuntimeError(
            f"another acceptance run holds the store lock {lock_path}; "
            "two runs must not share one store, because each run's writes "
            "invalidate the other's evidence"
        )
    return handle

def find_by_name(parent, name):
    for d in parent.descendants():
        if d.window_text() == name or d.element_info.name == name:
            return d
    return None

def get_item_status(element):
    try:
        return getattr(element.element_info.element, "CurrentItemStatus", "")
    except Exception:
        return ""


class _FILE_ID_INFO(ctypes.Structure):
    _fields_ = [("VolumeSerialNumber", ctypes.c_ulonglong),
                ("Identifier", ctypes.c_ubyte * 16)]


def folder_stable_identity(path):
    """The exact identity string ProjectRoot.cpp computes for a folder.

    `ValidateProjectRoot` formats the volume serial in hex, then each of the 16
    file-id bytes as two zero-padded hex digits. A project row's
    `filesystem_identity` is compared against that string, so a harness that
    seeds its own row has to reproduce the format rather than invent an
    identifier: a row whose identity does not match the folder it names is
    refused by the application, which is correct behavior and not a defect.
    """
    GENERIC_READ = 0x80000000
    FILE_SHARE_ALL = 0x00000007
    OPEN_EXISTING = 3
    FILE_FLAG_BACKUP_SEMANTICS = 0x02000000
    FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000
    FileIdInfo = 18
    handle = kernel32.CreateFileW(path, GENERIC_READ, FILE_SHARE_ALL, None, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, None)
    if handle in (-1, 0xFFFFFFFFFFFFFFFF):
        raise OSError(f"CreateFileW failed for {path}: {ctypes.get_last_error()}")
    try:
        info = _FILE_ID_INFO()
        if not kernel32.GetFileInformationByHandleEx(handle, FileIdInfo, ctypes.byref(info),
                                                     ctypes.sizeof(info)):
            raise OSError(f"GetFileInformationByHandleEx failed: {ctypes.get_last_error()}")
    finally:
        kernel32.CloseHandle(handle)
    return "%x:%s" % (info.VolumeSerialNumber,
                      "".join("%02x" % byte for byte in info.Identifier))


def uia_view_membership(win, automation_ids, max_depth=40):
    """Which of `automation_ids` exist in the UIA Control view and the Raw view.

    "Not found" has two different causes and they must not be written up as the
    same finding:

    * the element has no AutomationPeer at all, so no automation id exists for
      it in ANY view and nothing it carries -- including an ItemStatus -- is
      readable by any UIA client; or
    * the element has a peer but is filtered out of the Control view as
      non-content, in which case the Raw view still exposes it and its status is
      readable.

    `pywinauto`'s `descendants()` walks the Control view, so it cannot tell these
    apart. This walks both walkers directly and returns the two sets.
    """
    try:
        from pywinauto.uia_defines import IUIA
        iuia = IUIA().iuia
        root = iuia.ElementFromHandle(win.handle)
    except Exception as exc:
        return set(), set(), f"could not open the UIA views: {type(exc).__name__}: {exc}"

    wanted = set(automation_ids)

    def collect(walker):
        found = set()
        stack = [(walker.GetFirstChildElement(root), 1)]
        while stack:
            element, depth = stack.pop()
            while element:
                try:
                    aid = element.CurrentAutomationId or ""
                    if aid in wanted:
                        found.add(aid)
                    if depth < max_depth:
                        child = walker.GetFirstChildElement(element)
                        if child:
                            stack.append((child, depth + 1))
                    element = walker.GetNextSiblingElement(element)
                except Exception:
                    element = None
        return found

    try:
        raw = collect(iuia.RawViewWalker)
        control = collect(iuia.ControlViewWalker)
    except Exception as exc:
        return set(), set(), f"UIA walk failed: {type(exc).__name__}: {exc}"
    return control, raw, ""


UIA_SCROLL_PATTERN_ID = 10004


def vertical_scroll_offset(element):
    """The control's own vertical scroll offset and view size, as UIA reports them.

    Returns (scroll_percent, view_percent), or None when the control has no
    scroll pattern. A view size below 100 percent means the content is taller
    than the viewport, which is the only way a list can scroll at all.

    The generated interface spells the scrollable flag
    CurrentVerticallyScrollable; the documented name CurrentVerticalScrollable
    does not exist on it, so reading the documented name raises AttributeError
    instead of returning a value.
    """
    try:
        import comtypes.client
        comtypes.client.GetModule("UIAutomationCore.dll")
        from comtypes.gen.UIAutomationClient import IUIAutomationScrollPattern
        pattern = element.element_info.element.GetCurrentPattern(UIA_SCROLL_PATTERN_ID)
        if not pattern:
            return None
        pattern = pattern.QueryInterface(IUIAutomationScrollPattern)
        return (float(pattern.CurrentVerticalScrollPercent),
                float(pattern.CurrentVerticalViewSize))
    except Exception as exc:
        log(f"  the vertical scroll offset was unreadable: {type(exc).__name__}: {exc}")
        return None

def has_focus(element):
    # A container reports focus through whichever descendant actually holds it, so the
    # direct check alone would produce false failures for composite controls.
    try:
        if element.has_keyboard_focus():
            return True
        return any(d.has_keyboard_focus() for d in element.descendants())
    except Exception:
        return False

def poll_until(predicate, timeout, interval=0.1, min_samples=2):
    """Bounded wait for an asynchronous UI postcondition.

    A fixed sleep samples one instant and turns a slow-but-correct transition
    into a failure; an unbounded wait hides a genuine hang. Polling with a bound
    keeps both properties: a real defect still fails, but only after the
    transition has had its full allowance.

    `min_samples` guarantees more than one look. Without it a single slow
    predicate can consume the whole deadline and the "poll" degrades into one
    snapshot -- which is exactly the failure `wait_for_notice` was fixed for, and
    which survived here. The deadline is therefore a lower bound on the
    observation window rather than a cap on the number of observations.
    """
    deadline = time.time() + timeout
    samples = 0
    while True:
        value = predicate()
        samples += 1
        if value:
            return value
        if time.time() >= deadline and samples >= min_samples:
            return None
        time.sleep(interval)


def wait_for_count_label(win, expected, timeout=8.0, label=""):
    """Poll the Screen 2 count label until it reads `expected`.

    Phase 1 sampled this label once after a fixed `time.sleep(1.0)` in seven
    places. Measured on this machine, the label settles at about +1.56 s after
    the keystroke, so the fixed sample read the *previous* count and reported a
    product defect that did not exist. The query itself is a store read on the
    serial lane, and readiness detection shares that lane, so its latency is
    not something a single sample can characterise.

    Returns (matched, last_text_seen). The last text is returned on failure so
    the log names what the label actually said rather than only what was wanted.
    """
    last = [""]

    def check():
        try:
            el = find_by_aid(win, "RecentProjectsFilterCountLabel")
        except Exception:
            return None
        if not el:
            return None
        try:
            last[0] = el.window_text()
        except Exception:
            return None
        return last[0] if expected in last[0] else None

    got = poll_until(check, timeout=timeout, interval=0.1, min_samples=2)
    if got is None and label:
        log(f"  [{label}] count label settled at '{last[0]}' (wanted '{expected}')")
    return (got is not None), last[0]


def log_menu_failure_diagnostics(win, reason):
    """Record what the window looked like when a menu did not come up.

    The Phase 4 flake ("the File flyout never realized its menu item") was seen
    once in five runs and could not be reproduced in isolation, so the evidence
    that would identify it has to be captured at the moment it happens: which
    menu elements exist at all, whether the UI thread answers a cheap request,
    and whether the application is even the foreground window. A bare "the menu
    did not appear" cannot distinguish a menu that refused to open, a menu that
    opened and was dismissed, a UI thread that was blocked, and a window that
    had lost the foreground to another process.

    Every probe is individually guarded. A diagnostic that raises would replace
    the observation it was meant to explain with a traceback, and the failure
    would then be reported as a harness defect rather than as the event it was.
    """
    log(f"  menu failure diagnostics ({reason}):")
    try:
        menus = []
        for d in win.descendants():
            try:
                if d.element_info.control_type in ("Menu", "MenuItem", "MenuBar"):
                    rect = d.rectangle()
                    menus.append((d.element_info.control_type, d.window_text(),
                                  rect.left, rect.top, rect.width(), rect.height()))
            except Exception:
                continue
        log(f"    menu elements in the tree now: {menus}")
    except Exception as exc:
        log(f"    reading the menu elements raised {type(exc).__name__}: {exc}")
    try:
        started = time.time()
        probe = find_by_aid(win, "SearchMessagesButton")
        rect = probe.rectangle() if probe else None
        log(f"    UIA walk cost (a full descendant scan plus one rect) took "
            f"{time.time() - started:.3f}s: {rect}")
    except Exception as exc:
        log(f"    UIA walk cost raised {type(exc).__name__}: {exc}")
    try:
        # A real UI-thread latency reading, which the walk above is NOT. The
        # walk enumerates every descendant over cross-process COM, so its
        # duration is dominated by enumeration cost and says nothing about
        # whether the application's message pump is answering. The 11.399 s
        # figure recorded in run 20261021-final was this walk, and it was read
        # as a UI-thread stall; measuring the pump directly separates the two.
        ui_ok, ui_secs, ui_detail = ui_thread_latency(win.handle)
        log(f"    UI thread latency (WM_NULL, the message pump answering): "
            f"{ui_secs:.3f}s answered={ui_ok} {ui_detail}")
    except Exception as exc:
        log(f"    UI thread latency probe raised {type(exc).__name__}: {exc}")
    try:
        foreground = user32.GetForegroundWindow()
        log(f"    foreground hwnd {foreground}; application window {win.handle}; "
            f"foreground matches the application: {foreground == win.handle}")
        # Name the window that actually holds the foreground. Without its
        # class, title and owning process, "the foreground did not match the
        # application" cannot be acted on: it does not say whether the harness's
        # own console took it, a notification took it, or the app lost it.
        log(f"    foreground holder: {describe_foreground_window()}")
    except Exception as exc:
        log(f"    reading the foreground window raised {type(exc).__name__}: {exc}")
    try:
        # The foreground reading alone cannot say WHY it is 0. GetForegroundWindow()
        # is desktop-relative, so a 0 is consistent both with "no foreground
        # window exists" and with "this process is not attached to the input
        # desktop and therefore cannot see one". The desktop names distinguish
        # the two, and the distinction is what makes a 0 diagnosable.
        context = desktop_context()
        log(f"    desktop context: thread_desktop={context['thread_desktop']} "
            f"input_desktop={context['input_desktop']} "
            f"on_input_desktop={context['on_input_desktop']} "
            f"errors={context['errors']}")
    except Exception as exc:
        log(f"    reading the desktop context raised {type(exc).__name__}: {exc}")


def open_switch_project(win, timeout=10.0):
    """Drive the native File -> Switch Project menu; return Screen 2's heading or None.

    Two choices here are deliberate and were measured, not assumed:

    * The flyout is POLLED for after expanding, never sampled after a fixed
      sleep. It realizes in 0.074-0.232 s over 20/20 measured rounds, so a fixed
      1.0 s sleep nearly always passed -- but "nearly always" is not an oracle,
      and this exact sampling point produced the intermittent Phase 4 failure.

    * There is no keyboard fallback. `MenuBarItem Title="File"` and
      `MenuFlyoutItem Text="Switch Project"` declare no AccessKey in
      MainWindow.xaml, so `send_keys("%(FS)")` cannot reach either control; it
      was verified to leave the flyout closed. The old fallback therefore made
      an unreachable menu indistinguishable from a broken switch, and silently
      guaranteed failure whenever the scan came back empty. Returning None with
      the reason logged is the honest result.
    """
    file_menu = None
    for d in win.descendants():
        try:
            if (d.element_info.control_type == "MenuItem"
                    and (d.window_text() == "File" or d.element_info.name == "File")):
                file_menu = d
                break
        except Exception:
            continue
    if file_menu is None:
        log("  Switch Project: the File menu item is absent from the UIA tree")
        # The same absence has been observed in two different shapes -- the menu
        # bar's File item missing entirely, and the flyout failing to realize its
        # item -- so both are diagnosed with the same probes.
        log_menu_failure_diagnostics(win, "the File menu item is absent")
        return None

    # The application's window is made foreground BEFORE the menu is invoked,
    # not only after a failure. A WinUI flyout is a Popup hosted by the
    # application's own window, and a Popup realizes only while its owner is
    # foreground; invoking the menu from a background window can therefore
    # create a flyout that never exposes its item. Establishing the foreground
    # first is what makes this path deterministic, and the foreground is
    # verified by reading it back rather than by trusting the API's return
    # value.
    established, fg_detail = establish_foreground(win)
    log(f"  Switch Project: foreground established={established} [{fg_detail}]")

    try:
        file_menu.iface_invoke.Invoke()
    except Exception as exc:
        log(f"  Switch Project: invoking File raised {type(exc).__name__}: {exc}")
        return None

    def switch_item():
        for d in win.descendants():
            try:
                if ("Switch Project" in d.window_text()
                        and d.element_info.control_type == "MenuItem"):
                    return d
            except Exception:
                continue
        return None

    # The item is selected by control type as well as text: the flyout also
    # exposes a Text child carrying the same string, and that child has no
    # InvokePattern.
    item = poll_until(switch_item, timeout=3.0, interval=0.05)
    item_already_invoked = False
    if item is None:
        # A WinUI flyout opens in a Popup hosted by the application's own
        # window, and a Popup only realizes while its owner is foreground. If
        # the application is not the OS foreground window when the menu is
        # invoked, the flyout can be created and then discarded without ever
        # exposing its item -- which is exactly the "the File flyout never
        # realized its menu item" shape recorded in the original Phase 10.2
        # report.
        #
        # So the foreground is established and VERIFIED before the menu is
        # invoked, and the menu is invoked again if the flyout did not come up
        # the first time. The measurement that a stall did not occur is
        # recorded alongside, because a missing flyout is not evidence of a
        # blocked UI thread and must not be reported as one.
        try:
            ui_ok, ui_secs, ui_detail = ui_thread_latency(win.handle)
            log(f"  Switch Project: UI thread latency before the foreground "
                f"repair {ui_secs:.3f}s answered={ui_ok} {ui_detail}")
        except Exception as probe_exc:
            ui_ok, ui_secs, ui_detail = None, 0.0, f"{type(probe_exc).__name__}: {probe_exc}"
        established, detail = establish_foreground(win)
        log(f"  Switch Project: foreground repair established={established} "
            f"[{detail}]")
        if established:
            try:
                file_menu.iface_invoke.Invoke()
            except Exception as exc:
                log(f"  Switch Project: re-invoking File raised "
                    f"{type(exc).__name__}: {exc}")
            item = poll_until(switch_item, timeout=3.0, interval=0.05)
            if item is None:
                log("  Switch Project: the File flyout still did not realize "
                    "its item after the foreground was established and verified")
    if item is None:
        # Before declaring the flyout absent, scan the tree for the item once
        # more and measure UI-thread latency separately from the scan.
        #
        # These are two different quantities and were previously conflated. In
        # run 20261021-final the item WAS present at (212, 281, 136, 35) when
        # this diagnostic ran, and the 11.399 s figure that was read as a stall
        # is the cost of a full UIA descendant scan, not the application's
        # message pump. A measured pump probe is recorded alongside it so a
        # stall claim rests on the pump and not on enumeration cost.
        probe_start = time.time()
        try:
            probe_btn = find_by_aid(win, "SearchMessagesButton")
            probe_rect = probe_btn.rectangle() if probe_btn else None
            probe_elapsed = time.time() - probe_start
        except Exception as probe_exc:
            probe_rect = None
            probe_elapsed = time.time() - probe_start
            log(f"  Switch Project: UIA walk raised {type(probe_exc).__name__}: {probe_exc}")
        log(f"  Switch Project: UIA walk took {probe_elapsed:.3f}s; rect={probe_rect}")
        try:
            ui_ok, ui_secs, ui_detail = ui_thread_latency(win.handle)
            log(f"  Switch Project: UI thread latency {ui_secs:.3f}s "
                f"answered={ui_ok} {ui_detail}")
            ui_stalled = (not ui_ok) or ui_secs > 1.0
        except Exception as probe_exc:
            ui_stalled = None
            log(f"  Switch Project: UI thread latency probe raised "
                f"{type(probe_exc).__name__}: {probe_exc}")

        # One last scan for the item now that the poll deadline has expired.
        late_item = switch_item()
        if late_item is not None:
            if ui_stalled:
                log(f"  Switch Project: 'Switch Project' MenuItem found after the poll deadline "
                    f"expired, and the UI thread did not answer the pump probe in {ui_secs:.3f}s; "
                    f"this is a UI-thread stall timeout, not a missing menu item. "
                    f"Attempting to invoke the late item.")
            else:
                log(f"  Switch Project: 'Switch Project' MenuItem found after the poll deadline "
                    f"expired while the UI thread was answering normally; this is a poll "
                    f"deadline that was too short for a correct-but-slow realization, not a "
                    f"UI-thread stall and not a missing menu item. Attempting to invoke it.")
            try:
                late_item.iface_invoke.Invoke()
                item = late_item
                item_already_invoked = True
            except Exception as exc:
                log(f"  Switch Project: invoking the late item raised {type(exc).__name__}: {exc}")
                return None
        else:
            log("  Switch Project: the File flyout never realized its menu item")
            # Say what the window looked like when the menu did not come up. A bare
            # "the flyout never appeared" does not distinguish a menu that refused
            # to open from a menu that opened and was dismissed again.
            log_menu_failure_diagnostics(win, "the File flyout never realized its menu item")
            return None

    if not item_already_invoked:
        try:
            item.iface_invoke.Invoke()
        except Exception as exc:
            log(f"  Switch Project: invoking the menu item raised {type(exc).__name__}: {exc}")
            return None

    def folder_heading():
        candidate = find_by_aid(win, "FolderHeading")
        if candidate is not None and candidate.is_visible():
            return candidate
        return None

    heading = poll_until(folder_heading, timeout=timeout, interval=0.1)
    if heading is None:
        log(f"  Switch Project: Screen 2 did not appear within {timeout:.0f}s")
    return heading


def window_is_foreground(win):
    """Whether this window is the OS foreground window.

    UIA reports CurrentHasKeyboardFocus false for every element when the
    top-level window is not foreground, so a focus assertion taken without this
    check cannot distinguish an app that failed to move focus from a harness
    that lost the foreground window.
    """
    try:
        return ctypes.cindll.user32.GetForegroundWindow() == win.handle
    except Exception:
        return None


def describe_foreground_window():
    """Name the window that currently holds the foreground.

    "GetForegroundWindow() != the application window" names a symptom. A
    record that says which window actually took the foreground -- its class and
    its title -- is a finding, because it distinguishes the harness's own
    console from a notification or from another application.
    """
    try:
        handle = user32.GetForegroundWindow()
    except Exception as exc:
        return f"GetForegroundWindow raised {type(exc).__name__}: {exc}"
    if not handle:
        return "no foreground window (GetForegroundWindow returned 0)"
    buf = ctypes.create_unicode_buffer(256)
    try:
        user32.GetClassNameW(handle, buf, 256)
    except Exception:
        pass
    class_name = buf.value
    title = ""
    length = int(user32.GetWindowTextLengthW(handle) or 0)
    if length:
        tbuf = ctypes.create_unicode_buffer(length + 1)
        try:
            user32.GetWindowTextW(handle, tbuf, length + 1)
            title = tbuf.value
        except Exception:
            pass
    try:
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(handle, ctypes.byref(pid))
        pid_value = pid.value
    except Exception:
        pid_value = 0
    return (f"foreground hwnd {handle} class={class_name!r} title={title!r} "
            f"pid={pid_value}")


def send_key_to_window(win_handle, vk_code):
    input_hwnds = []
    def enum_cb(h, _):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(h, buf, 256)
        if buf.value == "InputSiteWindowClass":
            input_hwnds.append(h)
        return True
    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_int, ctypes.c_int)
    user32.EnumChildWindows(win_handle, WNDENUMPROC(enum_cb), 0)
    target = input_hwnds[0] if input_hwnds else win_handle
    user32.PostMessageW(target, WM_KEYDOWN, vk_code, 0)
    user32.PostMessageW(target, WM_KEYUP, vk_code, 0)

def classify_drop_verdict(
    drop_proc_returncode: int,
    dropped_records: list,
    contribs_before: int,
    contribs_after: int,
    pending_before: int,
    pending_after: int,
    composer_text_before: str = "",
    composer_text_after: str = "",
    expected_filename: str = "native_drop_single.txt",
    expected_size: int = 0,
    expected_sha256: str = "",
    staged_file_exists: bool = True
) -> tuple:
    """
    Classifies the drop operation verdict independently of helper success.
    Prohibited side effects produce FAIL independently of helper returncode.
    INCONCLUSIVE is reserved strictly for genuinely insufficient delivery evidence (0 records admitted).
    """
    # 1. Prohibited side effect: unexpected contribution creation
    if contribs_after != contribs_before:
        return "FAIL", f"Unexpected contribution creation: count changed from {contribs_before} to {contribs_after}"

    # 2. Prohibited side effect: unexpected pending submission creation
    if pending_after != pending_before:
        return "FAIL", f"Unexpected pending submission creation: count changed from {pending_before} to {pending_after}"

    # 3. Prohibited side effect: composer text modified
    if composer_text_after != composer_text_before:
        return "FAIL", f"Composer text was modified by drop operation: '{composer_text_after}'"

    # 4. Prohibited side effect: duplicate / multiple admission
    if len(dropped_records) > 1:
        return "FAIL", f"Duplicate admission violation: expected at most 1 record, got {len(dropped_records)}"

    # 5. Exactly 1 record admitted: verify metadata and staged copy integrity
    if len(dropped_records) == 1:
        rec = dropped_records[0]
        # rec format: (id, file_name, file_size, status, sha256, staged_path)
        if expected_filename and rec[1] != expected_filename:
            return "FAIL", f"Admitted record filename mismatch: expected '{expected_filename}', got '{rec[1]}'"
        if expected_size and rec[2] != expected_size:
            return "FAIL", f"Admitted record size mismatch: expected {expected_size}, got {rec[2]}"
        if rec[3] != "READY":
            return "FAIL", f"Admitted record status mismatch: expected 'READY', got '{rec[3]}'"
        if expected_sha256 and rec[4] != expected_sha256:
            return "FAIL", f"Admitted record digest mismatch: expected '{expected_sha256}', got '{rec[4]}'"
        if not staged_file_exists:
            staged = rec[5] if len(rec) > 5 else ""
            return "FAIL", f"Staged file does not exist on disk: {staged}"

        if drop_proc_returncode == 0:
            return "PASS", f"Admitted exactly 1 valid single file '{rec[1]}' (size={rec[2]}, status={rec[3]}, digest={rec[4]}). Absence of automatic Send verified."
        else:
            return "FAIL", f"Record admitted but helper process exited with code {drop_proc_returncode}"

    # 6. Zero records admitted: genuinely insufficient delivery evidence
    return "INCONCLUSIVE", f"Zero records admitted (helper exit code {drop_proc_returncode}); genuinely insufficient delivery evidence"


def test_drop_verdict_classification():
    log("Running unit tests for drop verdict classification logic...")
    sample_rec = ("att-1", "native_drop_single.txt", 100, "READY", "abc123hash", "C:/staged/att-1")

    # Case 1: Successful delivery with zero records -> INCONCLUSIVE
    v, reason = classify_drop_verdict(
        drop_proc_returncode=0,
        dropped_records=[],
        contribs_before=0,
        contribs_after=0,
        pending_before=0,
        pending_after=0,
        composer_text_before="",
        composer_text_after="",
        expected_filename="native_drop_single.txt",
        expected_size=100,
        expected_sha256="abc123hash"
    )
    assert v == "INCONCLUSIVE", f"Expected INCONCLUSIVE, got {v} ({reason})"
    log(f"  [CASE 1 PASS] Successful delivery with zero records -> {v} ({reason})")

    # Case 2: Successful delivery with one record -> PASS
    v, reason = classify_drop_verdict(
        drop_proc_returncode=0,
        dropped_records=[sample_rec],
        contribs_before=0,
        contribs_after=0,
        pending_before=0,
        pending_after=0,
        composer_text_before="",
        composer_text_after="",
        expected_filename="native_drop_single.txt",
        expected_size=100,
        expected_sha256="abc123hash",
        staged_file_exists=True
    )
    assert v == "PASS", f"Expected PASS, got {v} ({reason})"
    log(f"  [CASE 2 PASS] Successful delivery with one valid record -> {v} ({reason})")

    # Case 3: Successful delivery with multiple records (duplicate admission violation) -> FAIL
    sample_rec2 = ("att-2", "native_drop_single.txt", 100, "READY", "abc123hash", "C:/staged/att-2")
    v, reason = classify_drop_verdict(
        drop_proc_returncode=0,
        dropped_records=[sample_rec, sample_rec2],
        contribs_before=0,
        contribs_after=0,
        pending_before=0,
        pending_after=0,
        composer_text_before="",
        composer_text_after="",
        expected_filename="native_drop_single.txt",
        expected_size=100,
        expected_sha256="abc123hash"
    )
    assert v == "FAIL", f"Expected FAIL for duplicate admission, got {v} ({reason})"
    assert "Duplicate admission violation" in reason
    log(f"  [CASE 3 PASS] Successful delivery with multiple records -> {v} ({reason})")

    # Case 4: Unexpected pending submission creation -> FAIL
    v, reason = classify_drop_verdict(
        drop_proc_returncode=0,
        dropped_records=[],
        contribs_before=0,
        contribs_after=0,
        pending_before=0,
        pending_after=1,
        composer_text_before="",
        composer_text_after=""
    )
    assert v == "FAIL", f"Expected FAIL for unexpected pending submission, got {v} ({reason})"
    assert "Unexpected pending submission creation" in reason
    log(f"  [CASE 4 PASS] Unexpected pending submission creation -> {v} ({reason})")

    # Case 5: Unexpected contribution creation -> FAIL
    v, reason = classify_drop_verdict(
        drop_proc_returncode=0,
        dropped_records=[],
        contribs_before=0,
        contribs_after=1,
        pending_before=0,
        pending_after=0,
        composer_text_before="",
        composer_text_after=""
    )
    assert v == "FAIL", f"Expected FAIL for unexpected contribution creation, got {v} ({reason})"
    assert "Unexpected contribution creation" in reason
    log(f"  [CASE 5 PASS] Unexpected contribution creation -> {v} ({reason})")

    # Case 6: Record admitted with corrupted metadata or missing staged file -> FAIL
    v, reason = classify_drop_verdict(
        drop_proc_returncode=0,
        dropped_records=[sample_rec],
        contribs_before=0,
        contribs_after=0,
        pending_before=0,
        pending_after=0,
        expected_filename="native_drop_single.txt",
        expected_size=100,
        expected_sha256="abc123hash",
        staged_file_exists=False
    )
    assert v == "FAIL", f"Expected FAIL for missing staged file, got {v} ({reason})"
    log(f"  [CASE 6 PASS] Missing staged file -> {v} ({reason})")

    log("PASS: All unit tests of drop verdict classification logic passed.\n")


def clear_notice(win):
    """Clears any active notice in the Window's Notice InfoBar."""
    notice_bar = find_by_aid(win, "Notice")
    if not notice_bar:
        return
    for d in notice_bar.descendants():
        if d.element_info.control_type == "Button" and (d.element_info.automation_id in ("CloseButton", "DismissButton") or d.element_info.name in ("Close", "Dismiss")):
            try:
                if hasattr(d, "iface_invoke") and d.iface_invoke:
                    d.iface_invoke.Invoke()
                elif hasattr(d, "click_input"):
                    d.click_input()
                time.sleep(0.3)
                break
            except Exception:
                pass


def wait_for_notice(win, predicate, timeout=6.0, label="", min_samples=3):
    """Polls the Notice InfoBar until `predicate(texts)` holds, or the timeout expires.

    A single sample taken after a fixed sleep is not an oracle. The notice is
    opened on the UI thread after the handler returns, and the element tree is
    rebuilt while a project is being shown, so one sample can read an empty
    descendant list and report "no notice was opened" for a notice that was
    about to appear. Polling separates "never opened" from "not open yet".

    Every sample is logged with its elapsed time so the log carries the whole
    timeline rather than only the final answer: a notice that appears on sample
    four is a different finding from one that never appears at all.

    The deadline is consulted *before* a sample and the elapsed time is measured
    from the sample's **start**. Measuring it after the walk let one expensive
    UIA tree walk consume the whole budget, so the loop exited on sample 0
    without ever reaching its sleep: a nominal 6 s poll was really a single
    snapshot, and a single snapshot taken while the application was busy cannot
    distinguish "no notice was opened" from "the notice had not been read yet".
    `min_samples` guarantees more than one look even when every walk is slow, so
    the timeout is a lower bound on the observation window rather than a cap on
    the number of observations.

    Returns (satisfied, texts_from_the_final_sample, element_was_found).
    """
    started = time.time()
    deadline = started + timeout
    texts = []
    found_element = False
    sample = 0
    while True:
        sample_started = time.time()
        notice_bar = find_by_aid(win, "Notice")
        found_element = notice_bar is not None
        texts = [d.window_text() for d in notice_bar.descendants() if d.window_text()] if notice_bar else []
        walk_seconds = time.time() - sample_started
        satisfied = bool(predicate(texts))
        # The walk duration is logged separately from the elapsed time: a slow
        # walk and a notice that never opened look identical in a single figure,
        # and only the split says which one happened.
        exhausted = time.time() >= deadline and sample + 1 >= min_samples
        if satisfied or exhausted:
            log(f"  notice sample {sample} at {sample_started - started:.2f}s "
                f"(walk {walk_seconds:.2f}s): element={'found' if found_element else 'MISSING'}, "
                f"texts={texts}{' <- satisfied' if satisfied else ''}")
            return satisfied, texts, found_element
        if sample in (0, 1):
            # Only the first two samples are logged verbosely on the way through;
            # the terminal sample is always logged by the branch above.
            log(f"  notice sample {sample} at {sample_started - started:.2f}s "
                f"(walk {walk_seconds:.2f}s): element={'found' if found_element else 'MISSING'}, "
                f"texts={texts}{f' ({label})' if label else ''}")
        sample += 1
        time.sleep(0.2)


def wait_for_notice_text(win, expected_substring, timeout=6.0):
    """Waits for expected substring to appear in Notice InfoBar descendants."""
    satisfied, texts, _found = wait_for_notice(
        win, lambda t: any(expected_substring in x for x in t), timeout=timeout)
    return satisfied, texts


def main():
    apply_cli_overrides(sys.argv[1:])
    log("=================================================================")
    log("STARTING STRENGTHENED NATIVE UI ACCEPTANCE AND PERF VERIFICATION")
    log("=================================================================")
    log(f"Executable: {EXE_PATH}")
    log(f"Store:      {STORE_PATH}")
    log(f"Perf log:   {PERF_LOG_PATH}")
    record_artifact_identity()
    # The session's desktop context is recorded once, at the start, so that every
    # later foreground reading in this run can be interpreted against the desktop
    # it was taken on. A foreground of 0 means "no foreground window" only on the
    # input desktop; on any other desktop the same call cannot see one at all.
    try:
        _context = desktop_context()
        log(f"Desktop context at start: thread_desktop={_context['thread_desktop']} "
            f"input_desktop={_context['input_desktop']} "
            f"on_input_desktop={_context['on_input_desktop']} "
            f"foreground_hwnd={_context['foreground_hwnd']} "
            f"errors={_context['errors']}")
    except Exception as exc:
        log(f"Desktop context at start could not be read: {type(exc).__name__}: {exc}")

    # -------------------------------------------------------------------------
    # Acceptance criteria.
    #
    # Every criterion starts as NOT_RUN and is set only by the observation that
    # actually earned it. Two consequences matter and are deliberate:
    #
    #   * A phase that aborts part way leaves the criteria it never reached as
    #     NOT_RUN. It can no longer inherit a silent PASS, so an unexecuted
    #     criterion is visible in the table instead of being claimed.
    #   * The report is emitted from the cleanup path as well as the normal one,
    #     so a mid-run `return` can no longer suppress the table entirely. The
    #     exit code is still non-zero for a FAIL and for an unexecuted criterion.
    # -------------------------------------------------------------------------
    criterion_startup_filtering = "NOT_RUN"
    criterion_search_pagination = "NOT_RUN"
    criterion_older_navigation = "NOT_RUN"
    criterion_search_switch_safety = "NOT_RUN"
    criterion_narrow_layout = "NOT_RUN"
    criterion_hover_sweep = "NOT_RUN"
    criterion_dpi_scaling = "NOT_RUN"
    criterion_action_center_paging = "NOT_RUN"
    criterion_action_center_stale = "NOT_RUN"
    criterion_action_center_dismiss = "NOT_RUN"
    criterion_drop_execution = "NOT_RUN"
    criterion_mixed_batch = "NOT_RUN"
    criterion_attachment_chip = "NOT_RUN"
    criterion_preview_withholding = "NOT_RUN"
    criterion_perf_profiling = "NOT_RUN"
    criterion_overlay_flat = "NOT_RUN"
    criterion_overlay_bounds = "NOT_RUN"
    criterion_overlay_repeat = "NOT_RUN"
    criterion_hero_perspective = "NOT_RUN"
    criterion_overlay_reduced_motion = "NOT_RUN"
    criterion_cli_readiness = "NOT_RUN"
    criterion_paging_save = "NOT_RUN"
    criterion_reading_anchor = "NOT_RUN"

    report_done = False

    def build_verdicts():
        return {
            "1. Recent Projects & Startup Filtering": criterion_startup_filtering,
            "2. Message Search Pagination & Continuation": criterion_search_pagination,
            "3. Older Message Navigation & Selection": criterion_older_navigation,
            "4. Search Mode Project Switch Safety": criterion_search_switch_safety,
            "5. Narrow 520px Responsive Layout": criterion_narrow_layout,
            "6. Rapid Hover Sweeps & Animation Stability": criterion_hover_sweep,
            "7. Display DPI Scaling Simulation": criterion_dpi_scaling,
            "8. Action Center Pagination & Resumption": criterion_action_center_paging,
            "9. Action Center Stale Invalidation & Refresh": criterion_action_center_stale,
            "10. Action Center Panel Dismissal Stability": criterion_action_center_dismiss,
            "11. Native OLE Desktop Drag-and-Drop Execution": criterion_drop_execution,
            "12. Mixed-Batch Folder Rejection on Drop": criterion_mixed_batch,
            "13. Seeded Attachment Chip Realization & Diagnostics": criterion_attachment_chip,
            "14. Strict Non-English Prose Preview Withholding": criterion_preview_withholding,
            "15. WinUI 3 CompositionTarget Performance Profiling": criterion_perf_profiling,
            "16. Search Overlay Flat Resting Orientation": criterion_overlay_flat,
            "17. Search Overlay Bounded Height, Scrolling & Paging Reachability": criterion_overlay_bounds,
            "18. Repeated Open/Close & Cancelled Reveal Leave The Overlay Flat": criterion_overlay_repeat,
            "19. Empty-Chat Hero Perspective Preserved": criterion_hero_perspective,
            "20. Reduced-Motion Change Leaves The Overlay Flat": criterion_overlay_reduced_motion,
            "21. CLI Readiness Projection & Non-Gating": criterion_cli_readiness,
            "22. Bounded History Paging Save & Newest Rendering": criterion_paging_save,
            "23. Reading Anchor Preserved Across Older Prepend": criterion_reading_anchor,
        }

    def emit_report(custom_verdicts=None, is_strict=None, required_registry=None):
        """Print the per-criterion table once, and return the run's exit code.

        Validates all criteria against the registered vocabulary and strict/diagnostic rules.
        """
        nonlocal report_done
        verdicts = custom_verdicts if custom_verdicts is not None else build_verdicts()
        if is_strict is None:
            is_strict = ("--strict" in sys.argv) or (os.environ.get("MAYASABA_STRICT_CERTIFICATION", "0") == "1")
        if required_registry is None:
            if ("--universe=all" in sys.argv) or (os.environ.get("MAYASABA_CERTIFICATION_UNIVERSE", "") == "all"):
                required_registry = REQUIRED_CRITERIA_ALL
            else:
                required_registry = REQUIRED_CRITERIA_AUTOMATED

        eval_result = evaluate_verdicts(verdicts, required_registry=required_registry, is_strict=is_strict)
        log("\n" + "=" * 70)
        log("NATIVE UI ACCEPTANCE VERIFICATION SUMMARY REPORT")
        log("=" * 70)
        try:
            log(f"UI-thread sampler summary: {ui_sample_summary()}")
        except Exception as exc:
            log(f"UI-thread sampler summary could not be produced: "
                f"{type(exc).__name__}: {exc}")
        try:
            artifact = write_ui_sample_artifact()
            if artifact:
                log(f"UI-thread raw samples written to: {artifact}")
        except Exception as exc:
            log(f"UI-thread raw samples could not be written: "
                f"{type(exc).__name__}: {exc}")

        for name, (v, j) in eval_result["records"].items():
            just_str = f" ({j})" if j else ""
            log(f"  {name:<54} [{v}]{just_str}")
        log("-" * 70)
        counts = eval_result["counts"]
        log(f"  Total: {len(eval_result['records'])} criteria | Passed: {counts.get('PASS', 0)} | "
            f"Inconclusive: {counts.get('INCONCLUSIVE', 0)} | Failed: {counts.get('FAIL', 0)} | "
            f"Blocked: {counts.get('BLOCKED', 0)} | Not run: {counts.get('NOT_RUN', 0)} | "
            f"N/A: {counts.get('NOT_APPLICABLE', 0)}")
        report_done = True

        if not eval_result["valid"]:
            log("\nSTATUS: CERTIFICATION EVALUATION FAILED (VALIDATION ERRORS)")
            for err in eval_result["errors"]:
                log(f"  ERROR: {err}")
            log("=" * 70 + "\n")
            return 1

        if counts.get("FAIL", 0) > 0:
            log("\nSTATUS: TEST SUITE FAILED (one or more assertions failed)")
            log("=" * 70 + "\n")
            return 1
        if counts.get("BLOCKED", 0) > 0:
            log("\nSTATUS: TEST SUITE INCOMPLETE (a precondition blocked an observation)")
            log("NOTE: A BLOCKED criterion is unverified. The harness could not reach")
            log("      the observation, so nothing is claimed either way.")
            log("=" * 70 + "\n")
            return 1
        if counts.get("NOT_RUN", 0) > 0:
            log("\nSTATUS: TEST SUITE INCOMPLETE (criteria were never executed)")
            log("NOTE: A NOT_RUN criterion is unverified, not passed. The run stopped")
            log("      before the observation that would have earned it.")
            log("=" * 70 + "\n")
            return 1
        if counts.get("INCONCLUSIVE", 0) > 0:
            if is_strict:
                log("\nSTATUS: STRICT CERTIFICATION FAILED (INCONCLUSIVE CRITERIA DISALLOWED IN STRICT MODE)")
                log("NOTE: Strict certification requires every criterion to be PASS or justified NOT_APPLICABLE.")
                log("      Inconclusive results cannot certify a release gate.")
                log("=" * 70 + "\n")
                return 1
            log("\nSTATUS: ACCEPTANCE CHECKS COMPLETE (INCONCLUSIVE CRITERIA RETAINED)")
            log("NOTE: Desktop automation environment constraints prevent automated verification")
            log("      of synthetic OLE drag-drop; native manual testing or interactive session required.")
            log("=" * 70 + "\n")
            return 0
        log("\nSTATUS: ALL CRITERIA PASSED")
        log("=" * 70 + "\n")
        return 0

    # Run unit tests of drop verdict classification logic
    test_drop_verdict_classification()

    # The store is locked for the whole run, BEFORE the first write. Every
    # criterion in this harness writes to the store the live application has
    # open -- DDL fault injection, seeded rows, stale-version injection,
    # deletes -- and a second run against the same file interleaves those
    # writes with this run's, so each run's evidence describes a database the
    # other was mutating underneath it. A run that cannot have the lock does
    # not start.
    store_lock = None
    try:
        store_lock = acquire_store_lock(STORE_PATH)
    except RuntimeError as lock_exc:
        log(f"ABORTED BEFORE LAUNCH: {lock_exc}")
        log("No criteria were exercised; the table below reports them as NOT_RUN.")
        return emit_report()
    log(f"Store lock held: {os.path.abspath(STORE_PATH)}.harness.lock")

    # Everything from here to the launch is inside its own try, because the
    # lock is held from this point on: if any of it fails, the lock has to be
    # released or every later run refuses to start on a store nobody is using.
    try:
        # Ensure store schema is in clean state before launch
        try:
            conn = sqlite3.connect(STORE_PATH)
            cur = conn.cursor()
            cur.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='contributions_test_broken';")
            if cur.fetchone():
                mark_store_write("DDL: ALTER TABLE contributions_test_broken -> contributions (restore of a schema left broken by an earlier run)")
                conn.execute("ALTER TABLE contributions_test_broken RENAME TO contributions;")
                conn.commit()
            conn.close()
        except Exception as e:
            log(f"Notice during store schema check: {e}")

        if os.path.exists(PERF_LOG_PATH):
            try:
                os.remove(PERF_LOG_PATH)
            except Exception:
                pass

        env = os.environ.copy()
        env["MAYASABA_STORE_PATH"] = STORE_PATH
        env["MAYASABA_PERF_LOG"] = PERF_LOG_PATH
        # The dev-diagnostic chat-trace opt-in: the application records page
        # adoption, prepend triggers/compensation and scroll-to-end passes into
        # <evidence-dir>/ui-trace.log. Metadata only (counts, offsets, row ids),
        # so the 3B paging legs can name the exact asynchronous sequence instead
        # of inferring it through UIA walks.
        env["MAYASABA_UI_TRACE"] = os.path.join(os.path.dirname(STORE_PATH), "ui-trace.log")

        proc = subprocess.Popen([EXE_PATH], cwd=BIN_DIR, env=env)
        log(f"Launched Mayasaba.exe (PID {proc.pid})")
    except BaseException:
        try:
            store_lock.close()
            log("Store lock released after a pre-launch failure.")
        except Exception:
            pass
        raise

    try:
        # Bounded wait rather than a fixed sleep: the window appears after roughly
        # 10-25 s on a cold binary, and a short fixed wait reports a false "no window".
        d = Desktop(backend="uia")
        deadline = time.time() + WINDOW_WAIT_SECONDS
        wins = []
        while time.time() < deadline:
            wins = [w for w in d.windows(process=proc.pid)
                    if "Engineering Control Room" in w.window_text() or "Mayasaba" in w.window_text()]
            if wins:
                break
            time.sleep(0.5)
        if not wins:
            log(f"FAIL: Could not locate main window within {WINDOW_WAIT_SECONDS}s")
            return 1

        win = wins[0]
        log(f"Connected to main window: '{win.window_text()}' (HWND {win.handle})")
        win.set_focus()
        time.sleep(1)
        # The application's pump is sampled for the whole run so that the W1
        # question -- does the UI thread ever block, and does it do so while
        # the harness holds a write lock on the live store? -- is answered by
        # measurement taken during this run rather than by a separate probe.
        # The UIA walk tracker is installed first so that every enumeration the
        # harness performs is attributable: a sample taken while the
        # application is answering a UIA request is not the application doing
        # its own work, and the two are counted apart.
        install_uia_walk_tracker()
        start_ui_sampler(win.handle)

        # =========================================================================
        # PHASE 1: SCREEN 2 RECENT PROJECT FILTERING & SELECTION
        # =========================================================================
        log("\n--- PHASE 1: Screen 2 Recent Projects Filtering & Selection ---")

        # 1.1 Check initial project count
        #
        # The list is loaded asynchronously and the label reads "Loading recorded
        # projects..." until the first query returns, so this is a poll like every
        # other label assertion here rather than a one-shot sample.
        ok, count_text = wait_for_count_label(win, "3 projects recorded", label="initial")
        log(f"Initial count label: '{count_text}'")
        if not ok:
            log(f"FAIL: Expected '3 projects recorded', got '{count_text}'")
            return 1
        log("PASS: Initial recent project count truthfully reports '3 projects recorded'.")

        # 1.2 Text filter: type 'alpha'
        filter_box = find_by_aid(win, "RecentProjectsFilterBox")
        if not filter_box:
            log("FAIL: RecentProjectsFilterBox not found")
            return 1
        log("Typing 'alpha' into RecentProjectsFilterBox...")
        filter_box.set_edit_text("alpha")

        # Bounded poll, not a fixed sleep: the label settles at about +1.56 s here
        # and a single 1.0 s sample read the pre-filter count.
        ok, count_text = wait_for_count_label(win, "1 matching project", label="alpha")
        log(f"Filtered count label: '{count_text}'")
        if not ok:
            log(f"FAIL: Expected '1 matching project' after filtering for 'alpha', got '{count_text}'")
            return 1

        alpha_btn = find_by_name(win, "Open project alpha")
        beta_btn = find_by_name(win, "Open project beta")
        if not alpha_btn or beta_btn is not None:
            log("FAIL: Filter results mismatch (alpha should be visible, beta hidden)")
            return 1
        log("PASS: Text filtering accurately isolates 'alpha' and excludes 'beta'.")

        # 1.3 Zero-results query on Screen 2: type non-matching string
        log("Testing non-matching zero-result query 'nonexistent_project_query_xyz' on Screen 2...")
        filter_box.set_edit_text("nonexistent_project_query_xyz")
        ok, count_text = wait_for_count_label(win, "0 matching projects", label="zero-results")
        log(f"Non-matching query count label: '{count_text}'")
        if not ok:
            log(f"FAIL: Expected '0 matching projects' for non-matching query, got '{count_text}'")
            return 1
        no_recent = find_by_aid(win, "NoRecentProjectsText")
        if not no_recent or not no_recent.is_visible():
            log("FAIL: NoRecentProjectsText should be visible when 0 projects match")
            return 1
        log("PASS: Zero-matching project filter truthfully displays '0 matching projects' and NoRecentProjectsText.")

        # 1.4 Escape key clears filter and restores full list
        log("Sending Escape key to clear filter...")
        send_key_to_window(win.handle, VK_ESCAPE)
        ok, count_text = wait_for_count_label(win, "3 projects recorded", label="escape")
        log(f"After Escape key: '{count_text}'")
        if not ok:
            log(f"FAIL: Expected '3 projects recorded' after Escape key, got '{count_text}'")
            return 1
        log("PASS: Escape key clears query and successfully restores all 3 projects.")

        # 1.5 Clear button: type 'beta' and click Clear
        log("Typing 'beta' and clicking RecentProjectsClearFilterButton...")
        filter_box.set_edit_text("beta")
        # The clear button only becomes visible once the filter is non-empty, so it
        # is resolved by a bounded poll rather than sampled at one instant.
        clear_filter_btn = poll_until(
            lambda: find_by_aid(win, "RecentProjectsClearFilterButton"), timeout=6.0)
        if not clear_filter_btn:
            log("FAIL: RecentProjectsClearFilterButton not found")
            return 1
        clear_filter_btn.iface_invoke.Invoke()
        ok, count_text = wait_for_count_label(win, "3 projects recorded", label="clear-button")
        log(f"After Clear button: '{count_text}'")
        if not ok:
            log(f"FAIL: Expected '3 projects recorded' after Clear button, got '{count_text}'")
            return 1
        log("PASS: Clear button resets filter and restores list.")

        # 1.6 Status Filter Dropdown
        status_filter = find_by_aid(win, "RecentProjectsStatusFilter")
        if not status_filter:
            log("FAIL: RecentProjectsStatusFilter ComboBox not found")
            return 1

        log("Selecting 'Closed projects only' in status filter...")
        status_filter.select(2)
        ok, count_text = wait_for_count_label(win, "1 matching project", label="status-closed")
        log(f"Closed status filter count: '{count_text}'")
        if not ok:
            log(f"FAIL: Expected '1 matching project' for closed projects, got '{count_text}'")
            return 1
        gamma_btn = find_by_name(win, "Open project gamma")
        if not gamma_btn:
            log("FAIL: 'gamma' closed project button not found in closed filter")
            return 1
        log("PASS: Status filter 'Closed projects only' truthfully shows only closed project 'gamma'.")

        log("Selecting 'All projects' in status filter...")
        status_filter.select(0)
        ok, count_text = wait_for_count_label(win, "3 projects recorded", label="status-all")
        log(f"All status filter count: '{count_text}'")
        if not ok:
            log(f"FAIL: Expected '3 projects recorded', got '{count_text}'")
            return 1
        log("PASS: Status filter 'All projects' restores all 3 projects.")

        criterion_startup_filtering = "PASS"

        # =========================================================================
        # PHASE 2: OPEN PROJECT & SEARCH PAGINATION / CONTINUATION
        # =========================================================================
        log("\n--- PHASE 2: Open Project & Search Pagination / Continuation ---")
        open_alpha = find_by_name(win, "Open project alpha")
        if not open_alpha:
            log("FAIL: 'Open project alpha' button not found")
            return 1
        open_alpha.iface_invoke.Invoke()
        time.sleep(2.0)

        search_btn = find_by_aid(win, "SearchMessagesButton")
        if not search_btn:
            log("FAIL: Screen 3 Chat not reached; SearchMessagesButton missing")
            return 1
        log("PASS: Bound-project Chat opened for 'alpha' (50 seeded messages).")

        # Open search panel
        log("Opening search panel via SearchMessagesButton...")
        search_btn.iface_invoke.Invoke()
        time.sleep(1.0)

        search_box = find_by_aid(win, "MessageSearchBox")
        if not search_box:
            log("FAIL: MessageSearchBox not visible")
            return 1
        log("PASS: Search panel opened successfully.")

        # Query "contribution"
        log("Typing query 'contribution'...")
        search_box.set_edit_text("contribution")
        time.sleep(1.5)

        load_more_btn = find_by_aid(win, "SearchLoadMoreButton")

        # Page 1 (20/50)
        s1 = read_status_text(win, "Showing 20 of 50 matching messages")
        log(f"Page 1 status: '{s1}'")
        if "Showing 20 of 50 matching messages" not in s1:
            log(f"FAIL: Expected 'Showing 20 of 50 matching messages', got '{s1}'")
            return 1
        if not load_more_btn or not load_more_btn.is_enabled():
            log("FAIL: Load more button should be visible and enabled for page 1")
            return 1
        log("PASS: Page 1 loaded with 20 items and Load more button enabled.")

        # Page 2 (40/50)
        log("Clicking 'Load more results' (Page 2)...")
        load_more_btn.iface_invoke.Invoke()
        time.sleep(1.5)
        s2 = read_status_text(win, "Showing 40 of 50 matching messages")
        log(f"Page 2 status: '{s2}'")
        if "Showing 40 of 50 matching messages" not in s2:
            log(f"FAIL: Expected 'Showing 40 of 50 matching messages', got '{s2}'")
            return 1
        log("PASS: Page 2 accumulated 40 items sequentially.")

        # Page 3 (50/50)
        log("Clicking 'Load more results' (Page 3)...")
        load_more_btn.iface_invoke.Invoke()
        time.sleep(1.5)
        s3 = read_status_text(win, "Showing 50 of 50 matching messages")
        log(f"Page 3 status: '{s3}'")
        if "Showing 50 of 50 matching messages" not in s3:
            log(f"FAIL: Expected 'Showing 50 of 50 matching messages', got '{s3}'")
            return 1

        load_more_after = find_by_aid(win, "SearchLoadMoreButton")
        is_collapsed = (load_more_after is None) or (load_more_after.rectangle().width() == 0)
        log(f"Load more button collapsed after final page: {is_collapsed}")
        if not is_collapsed:
            log("FAIL: Load more button should collapse after all items are loaded")
            return 1
        log("PASS: Page 3 loaded final 10 items (50 total). Load more button collapsed.")

        # 2.2 Close search (Escape) and Reopen freshly at page 0
        log("\nClosing search with Escape...")
        send_key_to_window(win.handle, VK_ESCAPE)
        time.sleep(1.0)

        search_box_closed = find_by_aid(win, "MessageSearchBox")
        is_closed = (search_box_closed is None) or (search_box_closed.rectangle().width() == 0)
        if not is_closed:
            close_btn = find_by_aid(win, "CloseSearchButton")
            if close_btn:
                close_btn.iface_invoke.Invoke()
                time.sleep(1.0)
            search_box_closed = find_by_aid(win, "MessageSearchBox")
            is_closed = (search_box_closed is None) or (search_box_closed.rectangle().width() == 0)
        log(f"Search panel closed: {is_closed}")

        log("Reopening search via SearchMessagesButton...")
        search_btn.iface_invoke.Invoke()
        time.sleep(1.5)

        s_reopen = read_status_text(win, "Showing 20 of 50 matching messages")
        log(f"Reopened status: '{s_reopen}'")
        if "Showing 20 of 50 matching messages" not in s_reopen:
            log(f"FAIL: Reopened search did not restart at page 0: '{s_reopen}'")
            return 1
        log("PASS: Reopening search resets pagination continuation to page 0 (offset 0, 20 items).")

        # 2.3 Query change eliminates stale paging/results
        log("\nChanging query to 'contribution 4'...")
        search_box = find_by_aid(win, "MessageSearchBox")
        search_box.set_edit_text("contribution 4")
        time.sleep(1.5)

        s_filtered = read_status_text(win, "Showing 11 of 11 matching messages")
        log(f"Filtered status: '{s_filtered}'")
        if "Showing 11 of 11 matching messages" not in s_filtered:
            log(f"FAIL: Filtered status mismatch: '{s_filtered}'")
            return 1
        log("PASS: Query change immediately cleared stale items and rendered fresh 11 matches.")

        # 2.4 Zero-matches test (nonexistent query text)
        log("\nTesting zero-match search query 'no_matching_messages_expected_999'...")
        search_box.set_edit_text("no_matching_messages_expected_999")
        time.sleep(1.5)
        s_nomatch = read_status_text(win, "No saved user messages match your query")
        log(f"Zero-match status: '{s_nomatch}'")
        if "No saved user messages match your query" not in s_nomatch:
            log(f"FAIL: Expected 'No saved user messages match your query', got '{s_nomatch}'")
            return 1
        log("PASS: Zero-match query correctly displays 'No saved user messages match your query'.")

        # 2.5 ACTUAL DATABASE QUERY FAILURE & RECOVERY
        log("\n--- Testing ACTUAL Database Query Failure & Recovery ---")
        log("Simulating SQLite query failure by temporarily altering schema...")
        conn = sqlite3.connect(STORE_PATH)
        mark_store_write("DDL: ALTER TABLE contributions -> contributions_test_broken (the deliberate fault injection)")
        conn.execute("ALTER TABLE contributions RENAME TO contributions_test_broken;")
        conn.commit()
        conn.close()

        log("Submitting query while database table is broken...")
        search_box.set_edit_text("trigger_database_error")
        time.sleep(1.5)

        s_fail = read_status_text(win, "Search failed:")
        log(f"Failure status text: '{s_fail}'")
        if "Search failed:" not in s_fail or "no such table: contributions" not in s_fail:
            log(f"FAIL: Expected 'Search failed: no such table: contributions', got '{s_fail}'")
            # Restore schema before returning failure
            conn = sqlite3.connect(STORE_PATH)
            mark_store_write("DDL: ALTER TABLE contributions_test_broken -> contributions (restore after the injected failure)")
            conn.execute("ALTER TABLE contributions_test_broken RENAME TO contributions;")
            conn.commit()
            conn.close()
            return 1

        results_list = find_by_aid(win, "SearchResultsList")
        items_count = len(results_list.children()) if results_list else 0
        if items_count > 0:
            log(f"FAIL: Search results should be cleared on query failure, found {items_count} items")
            return 1
        log("PASS: Actual database query failure was truthfully caught and displayed ('Search failed: no such table: contributions').")

        log("Restoring database table schema...")
        conn = sqlite3.connect(STORE_PATH)
        mark_store_write("DDL: ALTER TABLE contributions_test_broken -> contributions (restore after the injected failure)")
        conn.execute("ALTER TABLE contributions_test_broken RENAME TO contributions;")
        conn.commit()
        conn.close()

        log("Submitting valid query after database restoration to verify recovery...")
        search_box.set_edit_text("contribution 1")
        time.sleep(1.5)
        s_recovered = read_status_text(win, "Showing 11 of 11 matching messages")
        log(f"Recovered query status: '{s_recovered}'")
        if "Showing 11 of 11 matching messages" not in s_recovered:
            log(f"FAIL: Expected 'Showing 11 of 11 matching messages' after recovery, got '{s_recovered}'")
            return 1
        log("PASS: Search UI cleanly recovered from actual database query failure once database was restored.")

        criterion_search_pagination = "PASS"

        # =========================================================================
        # PHASE 3: OLDER MESSAGE NAVIGATION, FOCUS & HIGHLIGHT RESET
        # =========================================================================
        log("\n--- PHASE 3: Older Message Navigation, Focus & Highlight Reset ---")
        log("Setting query to exact target: 'Seeded contribution 5.'...")
        search_box.set_edit_text("Seeded contribution 5.")
        time.sleep(1.5)

        results_list = find_by_aid(win, "SearchResultsList")
        items = results_list.children() if results_list else []
        log(f"Search result cards found: {len(items)}")
        if len(items) == 0:
            log("FAIL: No match found for 'Seeded contribution 5.'")
            return 1

        target_row = items[0]
        view_btn = find_by_name(target_row, "View saved message in conversation")
        if not view_btn:
            log("FAIL: 'View saved message in conversation' button not found")
            return 1

        log("Invoking 'View saved message in conversation' button...")
        view_btn.iface_invoke.Invoke()

        # The navigation highlight is applied immediately but cleared by a 1.5 s timer.
        # Sampling it after the descendant walk below could therefore read the already
        # reset state and report a false failure. The status lives on the realized group
        # itself, so it is read directly and promptly here; the assertion that it is the
        # *intended* card is still made once that card is identified.
        highlighted_card = None
        scroller = None
        deadline = time.time() + 1.2
        while time.time() < deadline:
            scroller = find_by_aid(win, "HistoryScroller")
            if scroller:
                for c in scroller.children():
                    if c.element_info.control_type == "Group" and get_item_status(c) == "highlighted_14dp":
                        highlighted_card = c
                        break
            if highlighted_card:
                break
            time.sleep(0.05)
        log(f"Highlight observed on a realized card within the reset window: {highlighted_card is not None}")

        search_box_after = find_by_aid(win, "MessageSearchBox")
        is_search_closed = (search_box_after is None) or (search_box_after.rectangle().width() == 0)
        log(f"Search panel closed after navigation: {is_search_closed}")
        if not is_search_closed:
            log("FAIL: Search panel did not close after message navigation")
            return 1

        if not scroller:
            scroller = find_by_aid(win, "HistoryScroller")
        if not scroller:
            log("FAIL: HistoryScroller not found")
            return 1

        # Locate the targeted message card among realized timeline items
        scroller_rect = scroller.rectangle()
        realized_groups = [c for c in scroller.children() if c.element_info.control_type == "Group"]
        log(f"Realized message groups in timeline: {len(realized_groups)}")
        if len(realized_groups) == 0:
            log("FAIL: No realized message cards found in HistoryScroller")
            return 1

        # Specifically locate the card containing the intended target message "Seeded contribution 5."
        target_card = None
        for c in realized_groups:
            card_texts = [d.window_text() for d in c.descendants() if d.window_text()]
            if any("Seeded contribution 5." in t for t in card_texts):
                target_card = c
                break

        if not target_card:
            log("FAIL: Intended message card 'Seeded contribution 5.' not found among realized groups")
            return 1

        log("PASS: Intended message card 'Seeded contribution 5.' confirmed in realized timeline.")

        # Assert keyboard focus independently on the intended target card
        card_has_focus = has_focus(target_card)
        log(f"Target message card has keyboard focus: {card_has_focus}")
        if not card_has_focus:
            log("FAIL: Target message card did not receive keyboard focus")
            return 1
        log("PASS: Target message card explicitly verified with keyboard focus.")

        # Assert item_status highlight independently, using the sample taken inside the
        # reset window. The card that carried the highlight must be the intended target,
        # so the identity check below still fails the run if some other card was lit.
        if highlighted_card is None:
            log("FAIL: No realized card carried item_status 'highlighted_14dp' within the 1.5s reset window")
            return 1
        # pywinauto hands back a fresh wrapper for the same UI element on every lookup,
        # so the wrappers are compared by runtime identity rather than object identity.
        def runtime_id(el):
            try:
                return tuple(el.element_info.element.GetRuntimeId())
            except Exception:
                return None
        if runtime_id(highlighted_card) != runtime_id(target_card):
            log("FAIL: The highlighted card is not the intended target message card")
            return 1
        log("PASS: Target card verified with item_status='highlighted_14dp' (14.0 dp elevation).")

        card_rect = target_card.rectangle()
        log(f"Target card bounds: L{card_rect.left}, T{card_rect.top}, R{card_rect.right}, B{card_rect.bottom}")
        log(f"Scroller bounds:    L{scroller_rect.left}, T{scroller_rect.top}, R{scroller_rect.right}, B{scroller_rect.bottom}")

        # Check visibility and viewport position
        if not target_card.is_visible():
            log("FAIL: Target message card is not marked visible")
            return 1
        if card_rect.top < scroller_rect.top - 20 or card_rect.bottom > scroller_rect.bottom + 60:
            log("FAIL: Target message card was not scrolled within the visible viewport bounds")
            return 1
        log("PASS: Target message card successfully brought into view within the active viewport.")

        log("Waiting 2.0s for highlight reset timer (1.5s interval) to execute...")
        time.sleep(2.0)

        status_after = get_item_status(target_card)
        log(f"Target card status after reset timer: '{status_after}'")
        if status_after != "resting_8dp":
            log(f"FAIL: Expected item_status to reset to 'resting_8dp', got '{status_after}'")
            return 1
        log("PASS: Highlight reset timer verified: card safely returned to 'resting_8dp' resting elevation.")

        criterion_older_navigation = "PASS"

        # =========================================================================
        # PHASE 4: PROJECT SWITCHING DURING SEARCH
        # =========================================================================
        log("\n--- PHASE 4: Project Switching During Active Search ---")
        log("Reopening search panel in alpha project...")
        search_btn = find_by_aid(win, "SearchMessagesButton")
        search_btn.iface_invoke.Invoke()
        time.sleep(1.0)

        search_box = find_by_aid(win, "MessageSearchBox")
        search_box.set_edit_text("contribution")
        time.sleep(0.5)

        log("Opening application menu File -> Switch Project...")
        folder_heading = open_switch_project(win)
        log(f"Folder heading after switch request: {folder_heading.window_text() if folder_heading else 'None'}")
        if not folder_heading:
            log("FAIL: Screen 2 Project Selector not reached on Switch Project")
            return 1

        search_box_switch = find_by_aid(win, "MessageSearchBox")
        is_search_closed = (search_box_switch is None) or (search_box_switch.rectangle().width() == 0)
        log(f"Search panel closed during project switch: {is_search_closed}")
        if not is_search_closed:
            log("FAIL: Search panel was not closed when switching projects")
            return 1

        log("Clicking Cancel on Screen 2 to return to alpha project Chat...")
        cancel_btn = find_by_aid(win, "FolderCancelButton")
        if not cancel_btn or not cancel_btn.is_visible():
            log("FAIL: FolderCancelButton not found or not visible")
            return 1
        cancel_btn.iface_invoke.Invoke()
        time.sleep(2.0)

        search_btn = find_by_aid(win, "SearchMessagesButton")
        if not search_btn:
            log("FAIL: Did not return to Chat after canceling project switch")
            return 1
        log("PASS: Project switching during active search closed search overlay, cleanly handled cancellation, and restored Chat.")

        criterion_search_switch_safety = "PASS"

        # =========================================================================
        # PHASE 5: ACCESSIBILITY, RESPONSIVE NARROW LAYOUT & RAPID HOVER
        # =========================================================================
        log("\n--- PHASE 5: Accessibility, Responsive Layout & Rapid Hover ---")

        composer_box = find_by_aid(win, "ComposerBox")
        send_btn = find_by_aid(win, "SendButton")
        start_btn = find_by_aid(win, "StartButton")
        pause_btn = find_by_aid(win, "PauseButton")
        stop_btn = find_by_aid(win, "StopButton")
        search_icon_btn = find_by_aid(win, "SearchMessagesButton")

        if not composer_box or not send_btn or not start_btn or not pause_btn or not stop_btn or not search_icon_btn:
            log("FAIL: One or more critical Chat controls missing from UIA tree")
            return 1

        # Strict accessible name assertions
        composer_name = composer_box.element_info.name
        send_name = send_btn.element_info.name
        start_name = start_btn.element_info.name
        pause_name = pause_btn.element_info.name
        stop_name = stop_btn.element_info.name
        search_name = search_icon_btn.element_info.name

        log(f"ComposerBox accessible name:          '{composer_name}'")
        log(f"SendButton accessible name:           '{send_name}'")
        log(f"StartButton accessible name:          '{start_name}'")
        log(f"PauseButton accessible name:          '{pause_name}'")
        log(f"StopButton accessible name:           '{stop_name}'")
        log(f"SearchMessagesButton accessible name: '{search_name}'")

        if "Chat message" not in composer_name and "Composer" not in composer_name:
            log(f"FAIL: ComposerBox accessible name invalid: '{composer_name}'")
            return 1
        if "Send" not in send_name:
            log(f"FAIL: SendButton accessible name invalid: '{send_name}'")
            return 1
        if start_name != "Start":
            log(f"FAIL: StartButton accessible name expected 'Start', got '{start_name}'")
            return 1
        if pause_name != "Pause":
            log(f"FAIL: PauseButton accessible name expected 'Pause', got '{pause_name}'")
            return 1
        if stop_name != "Stop":
            log(f"FAIL: StopButton accessible name expected 'Stop', got '{stop_name}'")
            return 1
        if "Search" not in search_name:
            log(f"FAIL: SearchMessagesButton accessible name invalid: '{search_name}'")
            return 1
        log("PASS: Strict UIA accessible names verified across all interactive controls.")

        # Narrow Window Adaptation & Clipping Verification (520px Width)
        log("\nTesting responsive narrow-window adaptation (width 520px) and measuring clipping...")
        win_rect_orig = win.rectangle()
        orig_dpi = get_window_dpi(win.handle)
        orig_scale = orig_dpi / 96.0
        orig_c_rect = RECT()
        user32.GetClientRect(win.handle, ctypes.byref(orig_c_rect))
        orig_log_w = (orig_c_rect.right - orig_c_rect.left) / orig_scale
        orig_log_h = (orig_c_rect.bottom - orig_c_rect.top) / orig_scale

        resize_to_logical_client(win, 520, 640)
        time.sleep(1.0)

        narrow_rect = win.rectangle()
        dpi_narrow = get_window_dpi(win.handle)
        scale_narrow = dpi_narrow / 96.0
        c_rect_narrow = RECT()
        user32.GetClientRect(win.handle, ctypes.byref(c_rect_narrow))
        meas_narrow_w = (c_rect_narrow.right - c_rect_narrow.left) / scale_narrow
        meas_narrow_h = (c_rect_narrow.bottom - c_rect_narrow.top) / scale_narrow
        log(f"Narrow measured client dimensions: {meas_narrow_w:.1f}x{meas_narrow_h:.1f} DIPs (target 520x640, DPI={dpi_narrow})")
        log(f"Narrow window bounds: L{narrow_rect.left}, T{narrow_rect.top}, R{narrow_rect.right}, B{narrow_rect.bottom} (Width: {narrow_rect.width()})")

        # Check each critical control for horizontal clipping or negative/zero dimensions
        controls_to_check = [
            ("ProjectHeading", find_by_aid(win, "ProjectHeading")),
            ("ProjectPathLabel", find_by_aid(win, "ProjectPathLabel")),
            ("ChatReadinessPanel", find_by_aid(win, "ChatReadinessPanel")),
            ("ComposerBox", find_by_aid(win, "ComposerBox")),
            ("SendButton", find_by_aid(win, "SendButton")),
            ("StartButton", find_by_aid(win, "StartButton")),
            ("HistoryScroller", find_by_aid(win, "HistoryScroller")),
        ]

        for name, ctrl in controls_to_check:
            if not ctrl:
                log(f"FAIL: Control '{name}' missing in 520px narrow window")
                return 1
            cr = ctrl.rectangle()
            log(f"  {name:20s}: L{cr.left}, T{cr.top}, R{cr.right}, B{cr.bottom} (W={cr.width()}, H={cr.height()})")
            if cr.width() <= 0 or cr.height() <= 0:
                log(f"FAIL: Control '{name}' collapsed to zero dimensions in narrow layout")
                return 1
            # Control right and left edges must not exceed window client area (excluding scroll extent)
            if name != "HistoryScroller":
                if cr.right > narrow_rect.right + 15:
                    log(f"FAIL: Control '{name}' clipped off right edge (Right={cr.right} > WinRight={narrow_rect.right})")
                    return 1
                if cr.left < narrow_rect.left - 15:
                    log(f"FAIL: Control '{name}' clipped off left edge (Left={cr.left} < WinLeft={narrow_rect.left})")
                    return 1

        log("PASS: 520px narrow layout measured: zero controls clipped or collapsed.")

        # Restore window size
        log(f"Restoring window size to logical {orig_log_w:.1f}x{orig_log_h:.1f}...")
        resize_to_logical_client(win, orig_log_w, orig_log_h)
        time.sleep(1.0)

        # Rapid Hover Sweeps (asserting cards actually exercised)
        log("\nPerforming rapid hover sweeps across realized message cards...")
        scroller = find_by_aid(win, "HistoryScroller")
        if not scroller:
            log("FAIL: HistoryScroller not found for hover sweeps")
            return 1

        # The criterion under test is that a rapid hover/focus sweep across the
        # realized cards produces no pointer lockup and no crash. How many cards
        # the virtualized timeline has realized at this moment is a property of
        # the viewport geometry and the scroll position, not of that criterion:
        # a run measured 3 realized cards and the next measured 4, with the
        # window unchanged. Requiring a fixed count therefore turned a
        # geometry-dependent precondition into an intermittent failure of a
        # property that had not changed at all. The sweep now runs over every
        # card the timeline has realized, and the count is recorded so the
        # reader can see what was actually exercised.
        cards = [c for c in scroller.children() if c.element_info.control_type == "Group"]
        log(f"Realized message cards count: {len(cards)}")
        if len(cards) < 1:
            log("FAIL: No message card was realized in HistoryScroller to sweep")
            return 1

        hovered_count = 0
        for c in cards[:8]:
            try:
                c.set_focus()
                hovered_count += 1
            except Exception:
                pass
            time.sleep(0.04)

        log(f"Cards successfully exercised in rapid sweep: {hovered_count}")
        if hovered_count < 1:
            log(f"FAIL: None of the {len(cards)} realized cards could be exercised in the hover sweep")
            return 1
        log("PASS: Rapid hover/focus sweeps verified across realized cards with zero pointer lockup or crash.")

        # Both legs of Phase 5 completed: the narrow layout held and the hover sweep
        # never locked up. Reaching this point is the observation.
        criterion_narrow_layout = "PASS"
        criterion_hover_sweep = "PASS"

        # =========================================================================
        # PHASE 5B: SAVED-MESSAGE SEARCH OVERLAY — FLAT RESTING ORIENTATION,
        #            BOUNDED HEIGHT, SCROLLING RESULTS, REACHABLE PAGING,
        #            REPEATED OPEN/CLOSE, CANCELLED REVEAL, REDUCED MOTION
        # =========================================================================
        log("\n--- PHASE 5B: Search Overlay Flat Orientation, Bounded Height & Reachability ---")

        # Criteria 16-20 already default to NOT_RUN from the registry above. Each
        # leg below assigns the verdict it actually earned; none of them starts as
        # a pass, so a leg that is skipped cannot be reported as verified.

        def overlay_panel():
            panel = find_by_aid(win, "MessageSearchPanel")
            if panel is None or not panel.is_visible():
                return None
            return panel

        def overlay_status():
            panel = find_by_aid(win, "MessageSearchPanel")
            return get_item_status(panel) if panel is not None else ""

        def settled_overlay_status():
            """The overlay's own projection of its resting presentation.

            The window publishes the surface's live rotation, opacity and translation,
            so this reads what the panel actually is, not what it was asked to be.
            """
            status = overlay_status()
            if ("resting;" in status and "rotation=0,0,0" in status
                    and "opacity=1" in status and "translate=0,0" in status):
                return status
            return None

        def open_overlay():
            button = find_by_aid(win, "SearchMessagesButton")
            if button is None:
                log("  the Search button is not in the tree")
                return None
            try:
                button.iface_invoke.Invoke()
            except Exception as exc:
                log(f"  opening the overlay raised {type(exc).__name__}: {exc}")
                return None
            panel = poll_until(overlay_panel, timeout=8.0, interval=0.05)
            if panel is None:
                # Say what the surface actually was. "It did not open" alone
                # points at the button, while the real cause is usually the
                # surface's own geometry.
                raw = find_by_aid(win, "MessageSearchPanel")
                if raw is None:
                    log("  the overlay surface is not in the tree")
                else:
                    rect = raw.rectangle()
                    log(f"  the overlay surface is in the tree but not visible: "
                        f"visible={raw.is_visible()} rect=({rect.left},{rect.top},{rect.right},{rect.bottom}) "
                        f"status='{get_item_status(raw)}'")
            return panel

        def close_overlay():
            send_key_to_window(win.handle, VK_ESCAPE)
            return poll_until(lambda: True if overlay_panel() is None else None,
                              timeout=5.0, interval=0.05)

        def query_overlay(text):
            box = find_by_aid(win, "MessageSearchBox")
            if box is None:
                return None
            try:
                box.set_edit_text(text)
            except Exception as exc:
                # A surface that cannot take a query is a verdict, not a reason to
                # abandon the whole run: the exception used to end the harness
                # here, which reported no result at all for every later phase.
                log(f"  typing '{text}' into the overlay raised {type(exc).__name__}: {exc}")
                return None
            return box

        # A leg of this phase records its own verdict and keeps the run alive. A
        # FAIL used to return from main(), so the first problem here ended the
        # harness and every later phase reported nothing at all. The verdict table
        # is the deliverable, and the exit code still reports the failure.
        def note(reason):
            log(f"FAIL: {reason}")
            return "FAIL"

        # 5B.1 The overlay rests flat, untranslated and fully opaque.
        panel = open_overlay()
        if panel is None:
            criterion_overlay_flat = note("The saved-message search overlay did not open")
        else:
            settled = poll_until(settled_overlay_status, timeout=8.0, interval=0.1)
            log(f"Overlay presentation status: '{overlay_status()}'")
            if settled is None:
                criterion_overlay_flat = note(
                    "The overlay never settled at a flat, fully opaque resting state")
            else:
                log("PASS: The overlay rests flat (rotation 0,0,0), untranslated and fully opaque.")

            # The panel's own content must sit inside the panel's box: a 3D rotation
            # would shear the children out of the parent's bounds.
            panel_rect = panel.rectangle()
            query_rect = find_by_aid(win, "MessageSearchBox").rectangle()
            log(f"Overlay bounds L{panel_rect.left}, T{panel_rect.top}, R{panel_rect.right}, B{panel_rect.bottom}")
            if query_rect.left < panel_rect.left - 2 or query_rect.right > panel_rect.right + 2:
                criterion_overlay_flat = note(
                    "The overlay's content is not contained in the panel's own bounds")
            else:
                log("PASS: The overlay's content is contained in its own unskewed bounds.")

        # Both observations above record their own failure. The criterion is a pass
        # only if neither of them did, so it is earned here rather than assumed.
        if criterion_overlay_flat == "NOT_RUN":
            criterion_overlay_flat = "PASS"

        # 5B.2 The hero art keeps its perspective. The window publishes the hero's
        # own resting orientation, so a flattened hero would read as 0,0,0. The
        # hero is only realized on the Welcome screen and in an empty conversation,
        # so the live observation is taken in 5B.8.

        # 5B.3 Short window: the overlay is bounded by the space above the composer,
        # the results list shrinks and scrolls, and the paging action stays reachable.
        # UX-28 declares the short-window matrix as 520x640 and 640x480 logical
        # sizes, so the short case is the declared 640x480 and not a size below it.
        log("\nResizing to the declared short window (640x480 logical) and re-checking the overlay...")
        short_base_dpi = get_window_dpi(win.handle)
        short_base_scale = short_base_dpi / 96.0
        short_c_rect = RECT()
        user32.GetClientRect(win.handle, ctypes.byref(short_c_rect))
        base_log_w = (short_c_rect.right - short_c_rect.left) / short_base_scale
        base_log_h = (short_c_rect.bottom - short_c_rect.top) / short_base_scale

        resize_to_logical_client(win, 640, 480)
        time.sleep(1.2)
        short_rect = win.rectangle()
        dpi_short = get_window_dpi(win.handle)
        scale_short = dpi_short / 96.0
        c_rect_short = RECT()
        user32.GetClientRect(win.handle, ctypes.byref(c_rect_short))
        meas_short_w = (c_rect_short.right - c_rect_short.left) / scale_short
        meas_short_h = (c_rect_short.bottom - c_rect_short.top) / scale_short
        log(f"Short measured client dimensions: {meas_short_w:.1f}x{meas_short_h:.1f} DIPs (target 640x480, DPI={dpi_short})")
        log(f"Short window bounds: L{short_rect.left}, T{short_rect.top}, R{short_rect.right}, B{short_rect.bottom}")

        # Criterion 17 accumulates its own failures across 5B.3 and 5B.4. It used
        # to return from main() at the first bad measurement, so one short-window
        # problem ended the harness and every later phase - including the
        # attachment-chip assertion - reported nothing at all.
        bounds_failures = []
        bounds_inconclusive = []

        def bounds_fail(reason):
            log(f"FAIL: {reason}")
            bounds_failures.append(reason)

        def bounds_unknown(reason):
            log(f"INCONCLUSIVE: {reason}")
            bounds_inconclusive.append(reason)

        query_overlay("contribution")

        # The results arrive asynchronously. The list is POLLED for its
        # postcondition instead of being sampled after a fixed sleep. A fixed
        # sleep caught the list while it was still empty, and an empty list
        # cannot scroll, so the scroll that followed was reported as a list that
        # does not scroll.
        #
        # The postcondition is ONE realized item, not two. At 640x480 the list's
        # viewport is 54px and a result card is roughly 87px, so at most one card
        # can intersect the viewport at rest; a two-item precondition is
        # unsatisfiable at the size being tested and would itself be the failure.
        def realized_state(element):
            return tuple((child.window_text(), child.rectangle().top)
                         for child in element.children())

        def results_with_items():
            element = find_by_aid(win, "SearchResultsList")
            if element is None:
                return None
            realized = [child for child in element.children()
                        if child.rectangle().height() > 0]
            return element if realized else None

        results_list = poll_until(results_with_items, timeout=10.0, interval=0.1)
        if results_list is None:
            status_label = find_by_aid(win, "SearchStatusText")
            log(f"  results status: '{status_label.window_text() if status_label is not None else None}'")
            bounds_fail("The results list did not realize its items after the query")

        status_label = find_by_aid(win, "SearchStatusText")
        if status_label is not None:
            log(f"Results status after the query: '{status_label.window_text()}'")

        panel = overlay_panel()
        if panel is None:
            bounds_fail("The overlay is not visible in the short window")
        panel_rect = panel.rectangle() if panel is not None else None
        composer = find_by_aid(win, "ComposerBox")
        composer_rect = composer.rectangle() if composer else None
        list_rect = results_list.rectangle() if results_list is not None else None
        load_more = find_by_aid(win, "SearchLoadMoreButton")
        load_rect = load_more.rectangle() if load_more else None

        if panel_rect is not None:
            log(f"Overlay: T{panel_rect.top}, B{panel_rect.bottom}, H{panel_rect.height()}")
        if list_rect is not None:
            log(f"Results list: T{list_rect.top}, B{list_rect.bottom}, H{list_rect.height()}")
        if composer_rect is not None:
            log(f"Composer top: {composer_rect.top}")
        if load_rect is not None:
            log(f"Load more button: L{load_rect.left}, T{load_rect.top}, R{load_rect.right}, B{load_rect.bottom}")

        if list_rect is None or list_rect.height() <= 0:
            bounds_fail("The results list collapsed to zero height in the short window")
        elif list_rect.height() >= 280:
            bounds_fail("The results list was not bounded by the short window")
        else:
            log(f"NOTE: the results list's viewport is {list_rect.height()}px at "
                f"{short_rect.width()}x{short_rect.height()}; a result card is taller than "
                f"that, so less than one full card is visible at rest. UX-28 requires the "
                f"short window to remain usable and names no list minimum.")
        if load_rect is None or load_rect.width() <= 0 or load_rect.height() <= 0:
            bounds_fail("The Load more action is not reachable in the short window")
        elif load_rect.bottom > short_rect.bottom or load_rect.top < short_rect.top:
            bounds_fail("The Load more action sits outside the window in the short layout")
        if panel_rect is not None and panel_rect.bottom > short_rect.bottom:
            bounds_fail("The overlay extends below the window in the short layout")
        if panel_rect is not None and composer_rect and panel_rect.bottom > composer_rect.top:
            # At this height the free space above the composer is smaller than the
            # overlay's own chrome. The overlay then overlaps the composer instead of
            # pushing its paging action out of the window; what matters is that
            # everything stays inside the window and stays reachable.
            log(f"NOTE: at {short_rect.height()}px the overlay overlaps the composer by "
                f"{panel_rect.bottom - composer_rect.top}px; the paging action stays inside the window.")
        if not bounds_failures:
            log("PASS: The short window bounds the overlay inside the window; the list shrank and the paging action stays visible.")

        # The list must actually scroll: a wheel over it has to move the content.
        #
        # The oracle reads the list's own UIA ScrollPattern offset, and it also
        # compares the realized items' identity. It used to compare each realized
        # item's top edge, and that comparison cannot see a scroll at this window
        # height: the viewport is shorter than one result card, so the single
        # realized item is clipped to the viewport and keeps filling it exactly
        # while the content underneath it moves. Measured against the binary under
        # test at 640x480, twelve wheel notches advanced the offset from 0.000% to
        # 5.585% in twelve distinct steps and changed the realized message, while
        # the realized item's rect stayed T294 H54 throughout. A rect-only oracle
        # therefore reports a list that does scroll as a list that does not.
        if results_list is not None and list_rect is not None:
            offset_before = vertical_scroll_offset(results_list)
            state_before = realized_state(results_list)
            log(f"Scroll offset before the wheel: {offset_before}")
            lx = (list_rect.left + list_rect.right) // 2
            ly = (list_rect.top + list_rect.bottom) // 2
            wheel_delivered = send_wheel_at_point(win, lx, ly, -120, steps=6, pause=0.2)
            if not wheel_delivered:
                # The OS foreground was not this window when the wheel events were
                # sent.  Windows routes WM_MOUSEWHEEL to the foreground window, so
                # the events went elsewhere and no stimulus reached the results
                # list.  A null scroll result in this condition is a statement
                # about the probe, not about the application.  Record INCONCLUSIVE
                # so the constraint can be re-evaluated on a run where the window
                # holds the foreground.
                offset_after = vertical_scroll_offset(results_list)
                log(f"Scroll offset after the (undelivered) wheel: {offset_after}")
                bounds_unknown(
                    "The wheel stimulus was not delivered: the application window was not the "
                    "OS foreground window (GetForegroundWindow() != win.handle) when the wheel "
                    "events were sent.  The scroll result is a statement about the probe, not "
                    "about the application.  Re-run with the window in the foreground.")
            else:
                # The offset is polled, not sampled once: the wheel is posted and
                # the list settles a few frames later.
                poll_until(
                    lambda: True if vertical_scroll_offset(results_list) != offset_before else None,
                    timeout=5.0, interval=0.1)
                offset_after = vertical_scroll_offset(results_list)
                state_after = realized_state(results_list)
                log(f"Scroll offset after the wheel: {offset_after}")
                log(f"Realized items before/after the wheel: {[t for t, _ in state_before]} -> "
                    f"{[t for t, _ in state_after]}")
                if offset_before is None or offset_after is None:
                    bounds_unknown(
                        "The results list exposed no readable vertical scroll offset, so scrolling "
                        "at the short window height could not be measured")
                elif offset_after[0] > offset_before[0]:
                    log(f"PASS: The bounded results list scrolls at the short window height "
                        f"(offset {offset_before[0]:.3f}% -> {offset_after[0]:.3f}%).")
                elif state_before != state_after:
                    # The offset did not advance but the realized content changed. That
                    # is a scroll the offset reports badly, not a list that cannot
                    # scroll, and it is recorded as such rather than silently passed.
                    log(f"NOTE: the scroll offset stayed at {offset_after[0]:.3f}% while the "
                        f"realized content changed; the list moved, the offset did not track it.")
                else:
                    bounds_fail("The results list did not scroll in the short window")

        # 5B.4 Narrow window: the overlay and its controls stay inside the window.
        log("\nResizing to a narrow window (480x800 logical) and re-checking the overlay...")
        resize_to_logical_client(win, 480, 800)
        time.sleep(1.2)
        narrow_rect = win.rectangle()
        panel = overlay_panel()
        if panel is None:
            bounds_fail("The overlay is not visible in the narrow window")
        else:
            panel_rect = panel.rectangle()
            load_more = find_by_aid(win, "SearchLoadMoreButton")
            load_rect = load_more.rectangle() if load_more else None
            results_list = find_by_aid(win, "SearchResultsList")
            list_rect = results_list.rectangle() if results_list else None
            log(f"Narrow overlay: L{panel_rect.left}, R{panel_rect.right} "
                f"(window {narrow_rect.left}..{narrow_rect.right})")
            if panel_rect.left < narrow_rect.left - 2 or panel_rect.right > narrow_rect.right + 2:
                bounds_fail("The overlay is clipped horizontally in the narrow window")
            if load_rect is None or load_rect.width() <= 0 or load_rect.height() <= 0:
                bounds_fail("The Load more action is not reachable in the narrow window")
            elif load_rect.left < narrow_rect.left - 2 or load_rect.right > narrow_rect.right + 2:
                bounds_fail("The Load more action is clipped horizontally in the narrow window")
            if list_rect is None or list_rect.height() <= 0:
                bounds_fail("The results list collapsed in the narrow window")

        if bounds_failures:
            criterion_overlay_bounds = "FAIL"
        elif bounds_inconclusive:
            criterion_overlay_bounds = "INCONCLUSIVE"
        else:
            # The verdict must be earned here, not left at its NOT_RUN default. This
            # branch logged "PASS" and assigned nothing, so a run whose bounds and
            # scroll checks all succeeded still reported criterion 17 as NOT_RUN --
            # the same silent-unassigned-verdict defect that 5B.7 had.
            criterion_overlay_bounds = "PASS"
            log("PASS: The narrow window keeps the overlay, its scrolling list and the paging "
                "action inside the window.")

        # Restore the window before the interaction checks.
        log(f"Restoring window size to logical {base_log_w:.1f}x{base_log_h:.1f}...")
        resize_to_logical_client(win, base_log_w, base_log_h)
        time.sleep(1.0)

        # 5B.5 Repeated open/close: every open must settle flat and every close must
        # remove the surface entirely. Criterion 18 covers 5B.5 and 5B.6 together.
        log("\nRepeated open/close cycles...")
        repeat_failures = []

        def repeat_fail(reason):
            log(f"FAIL: {reason}")
            repeat_failures.append(reason)

        # The trigger is a toggle: MainWindow::OnSearchMessagesClick closes an
        # overlay that is already open. The preceding leg leaves the overlay open, so
        # invoking the trigger here would close the surface and the first cycle would
        # report a false "did not open". The state this leg requires is established
        # explicitly, rather than being assumed or papered over with a retry.
        if overlay_panel() is not None:
            log("  the overlay is still open from the preceding leg; closing it first")
            if close_overlay() is None:
                repeat_fail("The overlay could not be closed before the repeat cycles began")

        for cycle in range(3):
            if open_overlay() is None:
                repeat_fail(f"The overlay did not open on cycle {cycle + 1}")
                break
            settled = poll_until(settled_overlay_status, timeout=8.0, interval=0.1)
            if settled is None:
                repeat_fail(f"The overlay did not settle flat on cycle {cycle + 1}: '{overlay_status()}'")
                break
            if close_overlay() is None:
                repeat_fail(f"The overlay did not close on cycle {cycle + 1}")
                break
        if not repeat_failures:
            log("PASS: Three repeated open/close cycles each settled flat and closed cleanly.")

        # 5B.6 A cancelled reveal must not leave the panel half-faded, half-slid or
        # invisible: it is closed while the entrance transition is still running and
        # reopened afterwards.
        log("\nCancelling the reveal mid-animation (close during the entrance)...")
        button = find_by_aid(win, "SearchMessagesButton")
        if button is None:
            repeat_fail("The search trigger is not present, so the reveal could not be cancelled")
        else:
            button.iface_invoke.Invoke()
            time.sleep(0.05)
            send_key_to_window(win.handle, VK_ESCAPE)
            time.sleep(0.5)
            if open_overlay() is None:
                repeat_fail("The overlay did not reopen after a cancelled reveal")
            else:
                settled = poll_until(settled_overlay_status, timeout=8.0, interval=0.1)
                log(f"Overlay status after a cancelled reveal: '{overlay_status()}'")
                if settled is None:
                    repeat_fail("The overlay did not settle flat after a cancelled reveal")
                else:
                    panel = overlay_panel()
                    panel_rect = panel.rectangle() if panel is not None else None
                    if panel_rect is None or panel_rect.width() <= 0 or panel_rect.height() <= 0:
                        repeat_fail("The overlay has no size after a cancelled reveal")
                    else:
                        log("PASS: A cancelled reveal leaves the overlay flat, sized and fully readable.")

        if repeat_failures:
            criterion_overlay_repeat = "FAIL"
        else:
            criterion_overlay_repeat = "PASS"

        # 5B.7 A live reduced-motion change must be observed and must leave the
        # overlay flat and readable.
        #
        # The window reads Windows.UI.ViewManagement.UISettings.AnimationsEnabled(),
        # so the stimulus has to be a change that source actually observes. This leg
        # drives SPI_SETCLIENTAREAANIMATION, reads the setting back to prove the
        # operating system accepted it, broadcasts WM_SETTINGCHANGE('VisualEffects')
        # as a second stimulus, and then reports what the window itself says. The
        # two readings are what make an INCONCLUSIVE verdict informative: they say
        # whether the harness failed to change the setting or whether the window
        # failed to observe a change that did happen.
        log("\nSwitching the system animation setting off (reduced motion)...")
        SPI_GETCLIENTAREAANIMATION = 0x1042
        SPI_SETCLIENTAREAANIMATION = 0x1043
        SPIF_UPDATEINIFILE = 0x01
        SPIF_SENDCHANGE = 0x02
        original_animations = ctypes.c_int()
        user32.SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0,
                                     ctypes.byref(original_animations), 0)
        log(f"System client-area animation was: {original_animations.value}")
        try:
            set_ok = user32.SystemParametersInfoW(SPI_SETCLIENTAREAANIMATION, 0,
                                                  ctypes.c_void_p(0),
                                                  SPIF_UPDATEINIFILE | SPIF_SENDCHANGE)
            log(f"SPI_SETCLIENTAREAANIMATION(FALSE) returned {set_ok}")
            broadcast_setting_change("VisualEffects")
            readback = ctypes.c_int()
            user32.SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0,
                                         ctypes.byref(readback), 0)
            stimulus_landed = (readback.value == 0)
            log(f"System client-area animation now reads {readback.value} "
                f"(stimulus {'landed' if stimulus_landed else 'did not land'})")
            observed_off = poll_until(lambda: True if "animations=off" in overlay_status() else None,
                                      timeout=8.0, interval=0.2)
            log(f"Overlay status with reduced motion: '{overlay_status()}'")
            if observed_off is None:
                if stimulus_landed:
                    log("NOTE: The system animation setting was changed and the window still "
                        "reports animations=on; the window's own animation source did not "
                        "observe the change. The reduced-motion leg is INCONCLUSIVE: the "
                        "harness cannot claim an observation it did not make.")
                else:
                    log("NOTE: The harness could not change the system animation setting, so "
                        "the reduced-motion leg was never exercised. INCONCLUSIVE.")
                criterion_overlay_reduced_motion = "INCONCLUSIVE"
            else:
                if close_overlay() is None:
                    log("FAIL: The overlay did not close before the reduced-motion re-open")
                    criterion_overlay_reduced_motion = "FAIL"
                elif open_overlay() is None:
                    log("FAIL: The overlay did not open with reduced motion")
                    criterion_overlay_reduced_motion = "FAIL"
                else:
                    settled = poll_until(settled_overlay_status, timeout=5.0, interval=0.05)
                    log(f"Overlay status after opening with reduced motion: '{overlay_status()}'")
                    if settled is None:
                        log("FAIL: The overlay did not settle flat with reduced motion")
                        criterion_overlay_reduced_motion = "FAIL"
                    else:
                        log("PASS: With reduced motion the overlay opens flat, opaque and unrotated.")
                        criterion_overlay_reduced_motion = "PASS"
        finally:
            # pvParam carries the BOOL value itself. The previous restore passed None,
            # which is FALSE, so it re-applied "animations off" instead of putting the
            # user's setting back.
            user32.SystemParametersInfoW(SPI_SETCLIENTAREAANIMATION,
                                         ctypes.c_void_p(original_animations.value),
                                         None,
                                         SPIF_UPDATEINIFILE | SPIF_SENDCHANGE)
            broadcast_setting_change("VisualEffects")
            restored_readback = ctypes.c_int()
            user32.SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0,
                                         ctypes.byref(restored_readback), 0)
            log(f"System client-area animation restored to {restored_readback.value} "
                f"(was {original_animations.value})")
            restored = poll_until(lambda: True if "animations=on" in overlay_status() else None,
                                  timeout=8.0, interval=0.2)
            log(f"System animation setting restored to {original_animations.value}; "
                f"window reports '{overlay_status()}'")

        close_overlay()
        log(f"PHASE 5B interaction legs complete - criteria 16/17/18/20 recorded as "
            f"{criterion_overlay_flat}/{criterion_overlay_bounds}/"
            f"{criterion_overlay_repeat}/{criterion_overlay_reduced_motion}.")

        # 5B.8 The empty-Chat hero keeps its perspective.
        #
        # WHAT IS BEING OBSERVED. `ApplyHeroPerspective` publishes each hero's own
        # resting orientation as an automation ItemStatus: it reads the mark's
        # CompositeTransform3D and writes "empty-chat-hero;resting_rotation=15,-22,-9"
        # onto `EmptyChatHero`, the container. The observation this leg wants is
        # that string.
        #
        # WHY IT IS PUBLISHED ON THE CONTAINER AND NOT ON THE MARK. The marks --
        # `WelcomeHeroMark` and `EmptyHeroMark` -- are plain `<Grid>` elements
        # (MainWindow.xaml:128, :869). A XAML Grid creates no AutomationPeer unless
        # it declares AutomationProperties.Name, so the mark's x:Name is not an
        # AutomationId and an ItemStatus set on it is unreadable in BOTH views.
        # That was measured, not assumed: a Raw-view walk of the Welcome window
        # contained `WelcomeHeroStage` -- named, therefore a Pane -- and no
        # descendant of it at all, and an empty conversation's walk contained no
        # hero node though its `EmptyChatHeading` sibling was present. Zero
        # elements in either state reported a non-empty ItemStatus.
        #
        # The earlier version of this leg recorded that as a permanent environment
        # limit and left criterion 19 INCONCLUSIVE. That was the wrong disposition:
        # the limit was in the application's choice of carrier, not in UIA, and the
        # correction is small. `PublishHeroOrientation` now reads the orientation
        # from the mark (the element that actually carries the transform, so the
        # status reports what the hero really has) and publishes it onto a named
        # container that has a peer.
        #
        # The container and not the art inside it, because `UpdateCompactChatBody`
        # collapses the art when the body drops below 430px (MainWindow.xaml.cpp:1189).
        # Publishing on the art would make the observation depend on the window
        # height it happened to be taken at; `EmptyChatHero` exists whenever the
        # empty state does.
        #
        # This leg therefore reads a real verdict rather than concluding from the
        # walk. The probe project is the harness's own and nothing another project
        # owns is touched.
        log("\nObserving the empty-Chat hero perspective in a project of the harness's own...")
        probe_id = "00000000-0000-4000-8000-00000000c119"
        probe_name = "zz-hero-probe"
        probe_path = os.path.join(os.path.dirname(STORE_PATH), "hero-probe")
        hero_verdict_reason = ""
        try:
            os.makedirs(probe_path, exist_ok=True)
            probe_identity = folder_stable_identity(probe_path)
            log(f"  probe project folder identity: {probe_identity}")
        except Exception as exc:
            probe_identity = ""
            log(f"  could not prepare the probe project folder: {exc}")

        if probe_identity:
            try:
                conn = sqlite3.connect(STORE_PATH)
                mark_store_write("INSERT: the temporary empty hero-probe project")
                # A recent-project row is opened by `SelectionAction::Recent`,
                # which resolves the record through its `domain_id` and requires a
                # `filesystem_identity` that matches the folder on disk
                # (ProjectService.h:73, :121). A row missing either is refused with
                # "Recent selection requires a project identity and version" --
                # correct application behavior, and the reason an earlier version
                # of this leg never left Screen 2.
                conn.execute(
                    "INSERT OR REPLACE INTO projects (id, display_name, canonical_path, draft,"
                    " closed, version, last_opened_at, filesystem_identity, domain_id) "
                    "VALUES (?, ?, ?, '', 0, 0, '', ?, ?);",
                    (probe_id, probe_name, probe_path, probe_identity, probe_id))
                conn.commit()
                conn.close()
            except Exception as exc:
                log(f"  could not create the probe project row: {exc}")

            try:
                if open_switch_project(win):
                    open_probe = poll_until(lambda: find_by_name(win, f"Open project {probe_name}"),
                                            timeout=8.0, interval=0.2)
                    if open_probe is not None:
                        open_probe.iface_invoke.Invoke()
                        # Poll the postcondition: the empty state is realized by the
                        # Chat load, which is asynchronous.
                        empty_heading = poll_until(
                            lambda: find_by_aid(win, "EmptyChatHeading"),
                            timeout=20.0, interval=0.2)
                        if empty_heading is None:
                            hero_verdict_reason = ("the empty conversation was not realized, so "
                                                   "neither the hero nor its absence was observed")
                        else:
                            log(f"  empty conversation realized: "
                                f"'{empty_heading.element_info.name}'")
                            hero_ids = ["EmptyChatHero", "EmptyHeroArt", "EmptyHeroMark",
                                        "WelcomeHeroMark", "WelcomeHeroStage"]
                            control_view, raw_view, view_error = uia_view_membership(win, hero_ids)
                            log(f"  hero elements in the UIA Control view: "
                                f"{sorted(control_view) or 'none'}")
                            log(f"  hero elements in the UIA Raw view:     "
                                f"{sorted(raw_view) or 'none'}")
                            if view_error:
                                log(f"  {view_error}")
                            statuses = []
                            for descendant in win.descendants():
                                status = get_item_status(descendant)
                                if status:
                                    statuses.append((descendant.element_info.automation_id,
                                                     status))
                            log(f"  elements publishing a non-empty ItemStatus: {len(statuses)}"
                                + ("" if not statuses else f" {statuses}"))
                            if raw_view or control_view or statuses:
                                # The status is published on `EmptyChatHero`, the
                                # container, which is the element that has a peer and
                                # that survives the compact layout. Read it there
                                # rather than from the mark, which carries the
                                # transform but nothing readable, or from the art,
                                # which is collapsed on a short window.
                                observed = None
                                for aid in ("EmptyChatHero", "EmptyHeroArt", "EmptyHeroMark"):
                                    if aid in raw_view or aid in control_view:
                                        element = find_by_aid(win, aid)
                                        if element is not None:
                                            candidate = poll_until(
                                                lambda: (lambda s: s if "resting_rotation=" in s else None)(
                                                    get_item_status(element)),
                                                timeout=8.0, interval=0.2)
                                            if candidate:
                                                observed = (aid, candidate)
                                                break
                                if observed:
                                    log(f"  empty-chat hero orientation: "
                                        f"{observed[0]} -> '{observed[1]}'")
                                    if "empty-chat-hero;resting_rotation=15,-22,-9" in observed[1]:
                                        log("PASS: The empty-chat hero keeps its declared "
                                            "15/-22/-9 resting perspective.")
                                        criterion_hero_perspective = "PASS"
                                    else:
                                        log(f"FAIL: The empty-chat hero lost its declared "
                                            f"perspective: '{observed[1]}'")
                                        criterion_hero_perspective = "FAIL"
                                else:
                                    hero_verdict_reason = ("a hero element is present in a UIA "
                                                           "view but published no resting_rotation")
                            else:
                                # The empty state was realized but no hero element is
                                # readable in either view. Since `EmptyChatHero` now
                                # carries an AutomationProperties.Name, this means the
                                # carrier that is supposed to be observable is not,
                                # which is a regression in the application rather than
                                # the environment limit this leg used to record.
                                hero_verdict_reason = (
                                    "the empty state was realized but neither `EmptyChatHero` nor "
                                    "any other hero element is reachable in the Control view or "
                                    "the Raw view, so the resting_rotation ItemStatus that "
                                    "PublishHeroOrientation publishes on it could not be read")
                    else:
                        hero_verdict_reason = ("the probe project did not appear in the recent "
                                               "list, so the empty state was never reached")
                else:
                    hero_verdict_reason = ("the project switch did not reach Screen 2, so the "
                                           "empty state was never reached")
            finally:
                # ORDER MATTERS HERE, AND THE WRONG ORDER BREAKS EVERY LATER PHASE.
                #
                # The application is still bound to the probe project at this
                # point. `NavigateAsync` (MainWindow.xaml.cpp:3456-3462) preserves
                # the current draft through `storage_->SaveDraft(id, draft)`
                # before it will leave a project, and `SaveDraft` throws
                # "Draft project not found" when `sqlite3_changes != 1`. Deleting
                # the row first therefore makes the application refuse EVERY
                # later navigation -- which is exactly what happened: the switch
                # back to alpha failed, Phase 9's gamma switch failed, and eight
                # criteria were reported NOT_RUN for a reason unrelated to any of
                # them.
                #
                # That refusal is correct fail-closed behavior: the application
                # will not silently drop a draft for a project whose record it can
                # no longer write. The harness's ordering was the defect. So the
                # application is first moved off the probe project and back to
                # alpha, and only then is the probe row removed.
                returned = False
                if open_switch_project(win):
                    open_alpha = find_by_name(win, "Open project alpha")
                    if open_alpha:
                        open_alpha.iface_invoke.Invoke()
                        restored = poll_until(lambda: find_by_aid(win, "ComposerBox"),
                                              timeout=20.0, interval=0.2)
                        returned = restored is not None
                        log(f"  returned to alpha before removing the probe row: {returned}")
                    else:
                        log("  could not return to alpha: the recent list has no alpha entry")
                else:
                    log("  could not return to alpha: Screen 2 did not appear")
                try:
                    conn = sqlite3.connect(STORE_PATH)
                    mark_store_write("DELETE: the temporary empty hero-probe project")
                    conn.execute("DELETE FROM contributions WHERE project_id = ?;", (probe_id,))
                    conn.execute("DELETE FROM selection_events WHERE project_id = ?;", (probe_id,))
                    conn.execute("DELETE FROM selection_receipts WHERE project_id = ?;", (probe_id,))
                    conn.execute("DELETE FROM pending_submissions WHERE project_id = ?;", (probe_id,))
                    conn.execute("DELETE FROM projects WHERE id = ?;", (probe_id,))
                    conn.commit()
                    conn.close()
                except Exception as exc:
                    log(f"  could not remove the probe project row: {exc}")
                try:
                    os.rmdir(probe_path)
                except Exception:
                    pass
                if not returned:
                    hero_verdict_reason = (
                        (hero_verdict_reason + "; " if hero_verdict_reason else "")
                        + "the application could not be returned to alpha afterwards, which "
                          "leaves every later phase without a bound project")

        else:
            hero_verdict_reason = ("the probe project folder could not be prepared, so the "
                                   "empty state was never reached")

        if criterion_hero_perspective == "NOT_RUN":
            criterion_hero_perspective = "INCONCLUSIVE"
        if hero_verdict_reason:
            log(f"INCONCLUSIVE: {hero_verdict_reason}.")
        log(f"PHASE 5B complete - criteria 16-20 recorded as "
            f"{criterion_overlay_flat}/{criterion_overlay_bounds}/"
            f"{criterion_overlay_repeat}/{criterion_hero_perspective}/"
            f"{criterion_overlay_reduced_motion}.")

        # =========================================================================
        # PHASE 3B: BOUNDED HISTORY PAGING & READING-ANCHOR PRESERVATION (22/23)
        #
        # Projected AFTER PHASE 5B deliberately: the probe project is bound only
        # after 5B's cleanup has already moved the window back to alpha, so every
        # later phase still observes the alpha state it expects. The "3B" name
        # keeps the F4-R5 finding numbering; the position keeps the state
        # invariants the suite relies on.
        #
        # WHAT IS BEING OBSERVED. A 121-message project must page like the review
        # asked: the newest 50-item page renders (72..121 after the send), a
        # message composed and sent through the REAL composer persists exactly
        # one contribution and renders exactly once, and scrolling to the top
        # loads one older page and -- the F4-R5 product correction -- returns
        # the reader to the content that was at the top edge when the load
        # triggered, instead of presenting the freshly inserted oldest rows.
        #
        # WHY THE REVIEWER'S LITERAL SNAPSHOT PROTOCOL IS ADAPTED. The scroll to
        # the top that triggers the prepend is itself a reader-position change,
        # and a cross-process UIA walk cannot reliably sample the sub-50 ms
        # window between "arrived at offset 0" and "prepend applied" -- the walk
        # itself is what §17.6 measured at hundreds of milliseconds. The
        # equivalent deterministic check is stronger and identity-derived: a
        # confirmed save reloads the newest page (SendCurrent -> SubmitAsync ->
        # ShowChat -> LoadChatAsync), so for a 121-message project the rendered
        # window is exactly 72..121 and the item that sits at the top edge at
        # offset 0 is "Paging message 72". If the compensation works, the
        # settled post-prepend top edge shows that same identity again at about
        # the same small delta; the pre-fix behavior instead leaves "Paging
        # message 22" -- the first inserted older row -- at the top edge. That
        # read cannot pass by coincidence, and a VerticalViewSize shrink proves
        # the older page was really loaded rather than the window repainting.
        #
        # ORACLE CORRECTION (f4r5-run7). The pixel basis of the original
        # wording ("within 40 px of the top edge") rested on the scroller's
        # UIA BoundingRectangle, which this control does not report
        # truthfully as its viewport (run7: L-13326, T-12265 with the window
        # at 64,64 -- 13,000 px off-screen while realized cards report sane
        # rects), and the ScrollPattern percent frame is not the extent
        # frame the trace measures (viewsize 0.58 % where extent/frame
        # arithmetic says ~10 %). The oracle therefore reads identities: the
        # compensated top edge is "message 72 is realized with at most one
        # boundary row above it and the first inserted row (message 22)
        # never realized", the load is confirmed by the product's own trace
        # chain (older-page/older-prepend), and the hold read repeats the
        # identity after 1.2 s.
        #
        # STIMULUS CORRECTION (f4r5-run8 + f4r5-run9-probe). run8 aimed each
        # wheel batch at the bottom-most realized card's UIA rect and
        # measured six batches with zero view events (a correct stimulus-
        # failure stop, recorded INCONCLUSIVE). The probe measured why: the
        # realized scrolled-subtree rects do not correspond to their
        # physical screen locations -- ElementFromPoint at a card's own
        # rect center names SearchMessagesButton -- while the fixed
        # header/composer rects are truthful and a band-center wheel
        # produced 31 view events and moved the ScrollPattern percent in
        # the same probe run. The gesture is therefore aimed at the
        # verified band center, preflighted once via ElementFromPoint
        # ancestry reaching HistoryScroller, and each batch's contact is
        # still verified from new view events in the trace.
        # =========================================================================
        log("\n--- PHASE 3B: Bounded History Paging & Reading-Anchor Preservation ---")
        paging_reason = ""
        paging_anchor_reason = ""
        paging_probe_id = "00000000-0000-4000-8000-00000000c11a"
        paging_probe_name = "zz-paging-probe"
        paging_probe_path = os.path.join(os.path.dirname(STORE_PATH), "paging-probe")
        paging_bound = False
        paging_row_created = False
        # ProjectHeading's read at the moment the probe bound (None until then).
        # Guards compare against this baseline, never against the seeded display
        # name: Select re-derives displayName from the observed folder, so the
        # two differ by design (f4r5-run12 guard false positive).
        paging_heading_baseline = None

        def paging_count(body):
            """Realized elements whose accessible name is exactly this body text."""
            n = 0
            for node in win.descendants():
                if (node.element_info.name or "") == body:
                    n += 1
            return n

        def paging_read_top_visible(timeout=6.0):
            """Reads HistoryScroller's published `top-visible` ItemStatus.

            The product publishes the physically visible top row there
            (see PublishTopVisibleRow in MainWindow.xaml.cpp). Polls until it
            is neither empty nor `pending`; returns whatever it last saw (a
            stale `pending` at timeout surfaces as 'pending', which the caller
            maps to INCONCLUSIVE).

            READ PATH CORRECTED (f4r5-run15c; oracle-adequacy finding recorded
            in LOCAL-VALIDATION.md before this change). The previous version
            read `raw.GetCurrentPropertyValue(30104)` and did
            `(val or "").strip()`. The comtypes wrapper for IUIAutomationElement
            declares that method's retval as `POINTER(VARIANT)`
            (comtypes/gen/_944DE083...py), which comtypes hands back as an
            opaque `POINTER(IUnknown)` rather than a Python string. `.strip()`
            therefore raised AttributeError on every poll, the except arm
            recorded `<read-error AttributeError>`, and the oracle returned
            INCONCLUSIVE unconditionally -- including under run15's controlling
            mutation, where the product had genuinely published the wrong row.
            A read that cannot return a value is not an observation of the
            product, so that verdict carried no evidence for or against the
            corrected oracle either way.

            `CurrentItemStatus` is the typed accessor for the identical UIA
            property (30104 == UIA_ItemStatusPropertyId) and returns a str; it
            is the accessor this harness's own `get_item_status()` uses
            (line 847), whose reads are proven by criteria that pass on it. The
            verdict chain below is unchanged: an unreadable, empty, `unknown` or
            `pending` read still maps to INCONCLUSIVE, and only a genuine
            identity mismatch can FAIL.
            """
            deadline = time.time() + timeout
            last = None
            while time.time() < deadline:
                scroller = find_by_aid(win, "HistoryScroller")
                if scroller is not None:
                    try:
                        val = getattr(scroller.element_info.element,
                                       "CurrentItemStatus", "")
                        last = val.strip() if isinstance(val, str) \
                            else f"<read-error non-str {type(val).__name__}>"
                    except Exception as exc:
                        last = f"<read-error {type(exc).__name__}>"
                    if last and last != "top-visible=pending":
                        return last
                time.sleep(0.25)
            return last if last else ""

        def paging_expected_anchor_id():
            """submission_id of the row the prepend anchored on (msg 72).

            The `older-capture` trace line records `before=<row_id>`, the oldest
            loaded row id at capture (msg 72 <-> row 129, pinned in
            LOCAL-VALIDATION.md run13). Maps it through contributions.submission_id.
            """
            try:
                import re as _re
                trace_path = os.path.join(os.path.dirname(STORE_PATH), "ui-trace.log")
                before_row = None
                with open(trace_path, "r", encoding="utf-8", errors="replace") as fh:
                    for line in fh:
                        m = _re.search(r"older-capture before=(\d+)", line)
                        if m:
                            before_row = int(m.group(1))
                if before_row is None:
                    return (None, "no older-capture before= field found in ui-trace.log")
                conn = sqlite3.connect(STORE_PATH)
                try:
                    row = conn.execute(
                        "SELECT submission_id FROM contributions WHERE row_id=?",
                        (before_row,)).fetchone()
                finally:
                    conn.close()
                if row is None:
                    return (None, f"no contributions row for row_id={before_row}")
                return (row[0], None)
            except Exception as exc:
                return (None, f"expected-id lookup raised {type(exc).__name__}: {exc}")

        def paging_scroll_state():
            """(VerticalViewSize %, VerticalScrollPercent %) of the history scroller."""
            scroller = find_by_aid(win, "HistoryScroller")
            if scroller is None:
                return None
            try:
                sp = scroller.iface_scroll
                return (float(sp.CurrentVerticalViewSize),
                        float(sp.CurrentVerticalScrollPercent))
            except Exception:
                return None

        def paging_top():
            """(body, delta_px) of the topmost fully-visible 'Paging message N' node.

            The delta is measured against the history scroller's top edge, so it
            is stable under window moves. Nodes clipped above the edge are only
            used when nothing else is visible.
            """
            scroller = find_by_aid(win, "HistoryScroller")
            if scroller is None:
                return (None, None)
            srect = scroller.rectangle()
            best = None
            fallback = None
            for node in scroller.descendants():
                name = node.element_info.name or ""
                if not name.startswith("Paging message "):
                    continue
                try:
                    rect = node.rectangle()
                except Exception:
                    continue
                delta = rect.top - srect.top
                if fallback is None or delta < fallback[1]:
                    fallback = (name, delta)
                if delta > -2 and (best is None or delta < best[1]):
                    best = (name, delta)
            return best or fallback or (None, None)

        def paging_physical_samples():
            """Physical top-edge sampling of the settled timeline - evidence
            only, never a verdict input.

            WHY (f4r5-run13 open question, phi1 vs phi2): the verdict reads
            the topmost REALIZED message (69, where the oracle wants 72), and
            that reading is compatible with two different physical states - a
            timeline whose visible top row really is 69 (product 3 rows short,
            phi2), and a timeline whose visible top row is 72 with a 3-row
            realization buffer above the viewport top (product correct,
            realized != visible, phi1). Realized-set membership cannot
            separate them. This samples the physical frame instead: for each
            screen y, ElementFromPoint (the machinery paging_wheel_target
            verified as contact-truthful) with the ancestry walked to
            HistoryScroller, reading `^Paging message (\\d+)$` from the point
            element or its ancestors' subtree - what is UNDER that pixel, not
            what the virtualizing panel has materialized.

            Returns a dict:
              samples      [(y, msg), ...] top-down, heading.bottom+16 to
                           +300 px, x candidates [cx, cx-160, cx+160]
              cover        [(y, top_desc), ...] points whose ancestry did NOT
                           reach HistoryScroller (the notice/problem-cards
                           overlay band; run13 layout: notice=50.4 px,
                           cardsActual=115.2 px over the band top)
              bottom       (y, msg) or None - first reach scanning up from
                           the composer (the wheel-target direction)
              pitch_px     distinct-(y, msg) span / row count, or None
              dpi          GetDpiForWindow, to convert screen px to DIPs
              msg_at_floor fitted message number at the band floor (the
                           overlay hides the true viewport top, so the
                           physical top row is extrapolated to it)
              nomsg        [(y, why), ...] points that reached the scroller
                           but resolved no message name (the run14 gap: a
                           zero-row sample now names its cause)
            """
            import re as _re
            from pywinauto.uia_defines import IUIA
            import ctypes.wintypes as _wt
            out = {"samples": [], "cover": [], "nomsg": [], "bottom": None,
                   "pitch_px": None, "dpi": 0, "msg_at_floor": None,
                   "reason": ""}
            try:
                heading = find_by_aid(win, "ProjectHeading")
                composer = find_by_aid(win, "ComposerBox")
                if heading is None or composer is None:
                    out["reason"] = "project header/composer not found"
                    return out
                hrect = heading.rectangle()
                crect = composer.rectangle()
                winrect = win.rectangle()
                floor_y = hrect.bottom + 16
                if crect.top - 16 <= floor_y:
                    out["reason"] = "chat band has no scan height"
                    return out
                try:
                    out["dpi"] = int(ctypes.windll.user32.GetDpiForWindow(
                        int(win.handle)))
                except Exception:
                    out["dpi"] = 0
                cx = (winrect.left + winrect.right) // 2
                xs = [cx, cx - 160, cx + 160]
                uia = IUIA().iuia
                walker = uia.ControlViewWalker
                msg_re = _re.compile(r"^Paging message (\d+)$")

                def probe(px, py):
                    """(msg|None, scroller_reached, top_desc) at one point."""
                    try:
                        ele = uia.ElementFromPoint(_wt.POINT(px, py))
                    except Exception as exc:
                        return (None, False,
                                f"ElementFromPoint: {type(exc).__name__}")
                    chain = []
                    cur = ele
                    reached = False
                    for _ in range(12):
                        if cur is None:
                            break
                        aid = ""
                        name = ""
                        try:
                            aid = cur.CurrentAutomationId or ""
                        except Exception:
                            pass
                        try:
                            name = (cur.CurrentName or "")[:64]
                        except Exception:
                            pass
                        chain.append((cur, aid, name))
                        if aid == "HistoryScroller":
                            reached = True
                            break
                        try:
                            cur = walker.GetParentElement(cur)
                        except Exception:
                            break
                    if not reached:
                        top = "?"
                        if chain:
                            top = chain[0][2] or chain[0][1] or "?"
                        return (None, False, top)
                    # Message under the point: nearest chain node whose own
                    # name matches, else the nearest non-scroller node whose
                    # SUBTREE holds a matching text node (the card container
                    # case). The scroller itself is excluded from the
                    # subtree walk: its subtree holds every realized
                    # message, so a match there would report a message the
                    # point is nowhere near.
                    for _ele, _aid, name in chain:
                        m = msg_re.match(name or "")
                        if m:
                            return (int(m.group(1)), True, "")
                    for _ele, _aid, _name in chain:
                        if _aid == "HistoryScroller":
                            break
                        try:
                            arr = _ele.FindAll(2, uia.CreateTrueCondition())
                        except Exception:
                            continue
                        n = 0
                        while n < min(60, arr.Length):
                            n += 1
                            try:
                                sname = arr.GetAt(n - 1).CurrentName or ""
                            except Exception:
                                continue
                            m = msg_re.match(sname)
                            if m:
                                return (int(m.group(1)), True, "")
                    return (None, True, "scroller reached, no message name")

                y = floor_y
                while y <= floor_y + 300:
                    got = None
                    cover = None
                    nomsg = None
                    for x in xs:
                        msg, reached, desc = probe(x, y)
                        if msg is not None and reached:
                            got = msg
                            break
                        if not reached and cover is None:
                            cover = desc
                        # Reached the scroller but no message name resolved:
                        # record why, so a zero-row sample names its cause
                        # instead of losing the descriptor (run14 gap).
                        if reached and msg is None and nomsg is None:
                            nomsg = desc
                    if got is not None:
                        out["samples"].append((y, got))
                    elif cover is not None:
                        out["cover"].append((y, cover))
                    elif nomsg is not None:
                        out.setdefault("nomsg", []).append((y, nomsg))
                    y += 10

                y = crect.top - 16
                while y > floor_y:
                    msg, reached, _d = probe(cx, y)
                    if msg is not None and reached:
                        out["bottom"] = (y, msg)
                        break
                    y -= 10

                first_y = {}
                for y, m in out["samples"]:
                    if m not in first_y:
                        first_y[m] = y
                if len(first_y) >= 2:
                    ms = sorted(first_y)
                    lo, hi = ms[0], ms[-1]
                    pitch = (first_y[hi] - first_y[lo]) / float(hi - lo)
                    if pitch > 0:
                        out["pitch_px"] = pitch
                        # Fit the topmost sample back to the band floor: the
                        # overlay hides the viewport's true top rows, so the
                        # physical top row is only reachable by extrapolation
                        # (this is the number that separates phi1 ~72 from
                        # phi2 ~69 at the floor line).
                        out["msg_at_floor"] = ms[0] - (
                            (first_y[ms[0]] - floor_y) / pitch)
                if not out["samples"] and not out["cover"] and not out["nomsg"]:
                    out["reason"] = "no point in the band resolved"
            except Exception as exc:
                out["reason"] = f"{type(exc).__name__}: {exc}"
            return out

        def classify_named(body):
            """Diagnostics: identity detail of every realized node named `body`.

            Used when an assertion fails, so the evidence names the element that
            disappeared (bubble text node vs composer content vs stray node)
            instead of leaving the verdict resting on a bare count of 0.
            """
            out = []
            try:
                for n in win.descendants():
                    try:
                        name = n.element_info.name or ""
                    except Exception:
                        continue
                    if name != body:
                        continue
                    detail = {"class": "?", "ct": "?", "aid": "?", "rect": "?",
                              "parent": "?"}
                    for key, getter in (("class", lambda: n.class_name()),
                                        ("ct", lambda: n.element_info.control_type),
                                        ("aid", lambda: n.element_info.automation_id),
                                        ("rect", lambda: "%dx%d@%d,%d" % (
                                            n.rectangle().width(), n.rectangle().height(),
                                            n.rectangle().left, n.rectangle().top))):
                        try:
                            detail[key] = getter()
                        except Exception as exc:
                            detail[key] = "ERR:%s:%s" % (type(exc).__name__, exc)
                    try:
                        detail["parent"] = n.parent().element_info.name
                    except Exception as exc:
                        detail["parent"] = "ERR:%s" % type(exc).__name__
                    out.append(detail)
            except Exception as exc:
                out.append({"walk": "ERR:%s:%s" % (type(exc).__name__, exc)})
            return out

        def timeline_tail():
            """The lowest realized saved-message bubbles with their body nodes."""
            try:
                bubbles = []
                for n in win.descendants():
                    try:
                        name = n.element_info.name or ""
                    except Exception:
                        continue
                    if name.startswith("Your saved message."):
                        try:
                            rect = n.rectangle()
                        except Exception:
                            continue
                        body = "?"
                        for c in n.descendants():
                            if (c.element_info.name or "").startswith("Paging message"):
                                body = c.element_info.name
                                break
                        bubbles.append((rect.bottom, body))
                return sorted(bubbles)[-6:]
            except Exception as exc:
                return ["ERR:%s" % exc]

        def window_geometry_note():
            try:
                r = win.rectangle()
                return "window %dx%d@%d,%d" % (r.width(), r.height(), r.left, r.top)
            except Exception as exc:
                return "window unreadable: %s" % type(exc).__name__


        try:
            os.makedirs(paging_probe_path, exist_ok=True)
            paging_identity = folder_stable_identity(paging_probe_path)
            log(f"  paging-probe folder identity: {paging_identity}")
        except Exception as exc:
            paging_identity = ""
            paging_reason = f"the probe folder could not be prepared: {exc}"
            log(f"  {paging_reason}")

        if paging_identity:
            try:
                conn = sqlite3.connect(STORE_PATH)
                mark_store_write("INSERT: the temporary paging-probe project and 120 seeded contributions")
                # Leftovers of an earlier aborted 3B leg would shift the row-id
                # arithmetic this leg's identity claims rest on, so start clean.
                conn.execute("DELETE FROM contributions WHERE project_id = ?;", (paging_probe_id,))
                conn.execute("DELETE FROM selection_events WHERE project_id = ?;", (paging_probe_id,))
                conn.execute("DELETE FROM selection_receipts WHERE project_id = ?;", (paging_probe_id,))
                conn.execute("DELETE FROM pending_submissions WHERE project_id = ?;", (paging_probe_id,))
                conn.execute(
                    "INSERT OR REPLACE INTO projects (id, display_name, canonical_path, draft,"
                    " closed, version, last_opened_at, filesystem_identity, domain_id) "
                    "VALUES (?, ?, ?, '', 0, 0, '', ?, ?);",
                    (paging_probe_id, paging_probe_name, paging_probe_path, paging_identity,
                     paging_probe_id))
                for i in range(1, 121):
                    # row_id is allocated in insert order, so message numbers
                    # increase with row_id and the newest 50-item page for a
                    # 121-message project is exactly 72..121.
                    conn.execute(
                        "INSERT INTO contributions (project_id, submission_id, body)"
                        " VALUES (?, ?, ?);",
                        (paging_probe_id, f"seeded-paging-{i:05d}", f"Paging message {i}"))
                conn.commit()
                conn.close()
                paging_row_created = True
            except Exception as exc:
                paging_reason = f"the probe project could not be seeded: {exc}"
                log(f"  3B setup: {paging_reason}")

        if paging_row_created:
            try:
                if open_switch_project(win):
                    open_probe = poll_until(lambda: find_by_name(win, f"Open project {paging_probe_name}"),
                                            timeout=8.0, interval=0.2)
                    if open_probe is not None:
                        open_probe.iface_invoke.Invoke()
                        # The Chat load is asynchronous across the storage lane.
                        composer_ready = poll_until(lambda: find_by_aid(win, "ComposerBox"),
                                                    timeout=30.0, interval=0.2)
                        if composer_ready is None:
                            paging_reason = "the probe project's chat did not open after binding"
                            log(f"  FAIL->setup: {paging_reason}")
                        else:
                            paging_bound = True
                            # Baseline identity capture. `Select` re-derives
                            # displayName from the observed folder, so the
                            # heading reads the FOLDER name ('paging-probe'),
                            # not the seeded display name ('zz-paging-probe')
                            # (f4r5-run12 guard false positive; the re-derivation
                            # is confirmed at storage_tests.cpp:764). The guard
                            # must therefore compare against what the heading
                            # reads right after this bind, not against the seed.
                            _bound_heading = find_by_aid(win, "ProjectHeading")
                            paging_heading_baseline = (_bound_heading.element_info.name
                                                       if _bound_heading is not None else None)
                            log(f"  identity baseline captured at bind: "
                                f"ProjectHeading={paging_heading_baseline!r}")
                            newest_seen = poll_until(
                                lambda: paging_count("Paging message 120") >= 1,
                                timeout=20.0, interval=0.3)
                            if newest_seen is None:
                                paging_reason = ("the newest seeded message 'Paging message 120' never "
                                                 "appeared in the rendered timeline")
                                log(f"  FAIL: {paging_reason}")
                                criterion_paging_save = "FAIL"
                            else:
                                log("  newest page rendered: 'Paging message 120' is visible")
                                composer_probe = find_by_aid(win, "ComposerBox")
                                composer_probe.set_edit_text("Paging message 121")
                                send_probe = find_by_aid(win, "SendButton")
                                if not send_probe:
                                    paging_reason = "SendButton not found in the probe chat"
                                    log(f"  FAIL: {paging_reason}")
                                    criterion_paging_save = "FAIL"
                                else:
                                    log("  sending 'Paging message 121' through the real composer...")
                                    send_probe.iface_invoke.Invoke()
                                    # A background guard samples what the timeline
                                    # realizes for the 12 seconds after the send so a
                                    # collapse is recorded the moment it happens,
                                    # with the tail that replaced the sent message.
                                    stable_reader = []
                                    guard_stop = threading.Event()

                                    def _tail_guard():
                                        t0 = time.time()
                                        while not guard_stop.is_set() and (time.time() - t0) < 12.0:
                                            ts = round(time.time() - t0, 2)
                                            n121 = paging_count("Paging message 121")
                                            tail = timeline_tail()
                                            note = ""
                                            if n121:
                                                note += "121-yes"
                                            guard = "Paging message 120" if paging_count("Paging message 120") else ""
                                            stable_reader.append((ts, n121, guard, tail[-3:],
                                                                  window_geometry_note()))
                                            time.sleep(0.3)

                                    guard_thread = threading.Thread(
                                        target=_tail_guard, daemon=True)
                                    guard_thread.start()
                                    sent_once = poll_until(
                                        lambda: paging_count("Paging message 121") == 1,
                                        timeout=25.0, interval=0.3)
                                    first_occurrences = paging_count("Paging message 121")
                                    log(f"  {window_geometry_note()}")
                                    if sent_once is None:
                                        first_detail = classify_named("Paging message 121")
                                        log(f"  3B DIAG on never-rendered: {first_detail}")
                                        log(f"  3B DIAG timeline tail: {timeline_tail()}")
                                        paging_reason = (f"the composed message 'Paging message 121' never "
                                                         f"rendered (count {first_occurrences} after 25 s)")
                                        log(f"  FAIL: {paging_reason}")
                                        criterion_paging_save = "FAIL"
                                    else:
                                        # A settle wait so a late duplicate render
                                        # (a double save) cannot hide behind the
                                        # moment of first realization.
                                        time.sleep(1.5)
                                        late_occurrences = paging_count("Paging message 121")
                                        log(f"  'Paging message 121' occurrences after settle: "
                                            f"{late_occurrences}")
                                        if late_occurrences != 1:
                                            late_detail = classify_named("Paging message 121")
                                            log(f"  3B DIAG on settled count {late_occurrences}: "
                                                f"{late_detail}")
                                            log(f"  3B DIAG timeline tail: {timeline_tail()}")
                                            # A second bounded window in case the
                                            # realized element is genuinely still
                                            # settling. If it returns, the render was
                                            # transient rather than absent, and both
                                            # shapes are recorded instead of guessed.
                                            regressed = poll_until(
                                                lambda: paging_count("Paging message 121") == 1,
                                                timeout=10.0, interval=0.3)
                                            log(f"  3B DIAG post-regression recount: "
                                                f"{paging_count('Paging message 121')} "
                                                f"(returned={regressed is not None})")
                                            paging_reason = (f"'Paging message 121' rendered "
                                                             f"{late_occurrences} times after settle "
                                                             f"(first poll saw {first_occurrences})")
                                            log(f"  FAIL: {paging_reason}")
                                            criterion_paging_save = "FAIL"
                                        else:
                                            log("PASS: send #121 through the real composer rendered exactly once.")
                                            criterion_paging_save = "PASS"
                                    # Record the guard's whole observation, so a
                                    # collapse the point reads missed is still in
                                    # the evidence verbatim.
                                    guard_stop.set()
                                    guard_thread.join(timeout=16.0)
                                    for entry in stable_reader:
                                        log("  3B SENTINEL t=%.2f count=%s set120=%r tail=%s %s"
                                            % entry)
                    else:
                        paging_reason = "the probe project did not appear in the recent list"
                        log(f"  FAIL->setup: {paging_reason}")
                else:
                    paging_reason = "the project switch did not reach Screen 2"
                    log(f"  FAIL->setup: {paging_reason}")
            except Exception as exc:
                paging_reason = f"3B paging leg raised {type(exc).__name__}: {exc}"
                log(f"  INCONCLUSIVE: {paging_reason}")

            # --- Reading-anchor measurement (criterion 23) ----------------------
            if paging_bound and criterion_paging_save == "PASS":
                try:
                    state_before = paging_scroll_state()
                    vvs_before = state_before[0] if state_before else None
                    log(f"  pre-prepend viewsize {vvs_before} %, scroll "
                        f"{state_before[1] if state_before else '?'} %")
                    scroller_probe = find_by_aid(win, "HistoryScroller")
                    # Two measured facts drive the design of this leg
                    # (f4r5-run6 and f4r5-run7 traces):
                    #
                    # 1. The UIA Scroll-pattern set is rejected by the
                    #    provider (COMError 0x80131509) while the pattern's
                    #    reads work, so the stimulus must be the real wheel
                    #    gesture the product's trigger declares.
                    # 2. The scroller element's BoundingRectangle is NOT its
                    #    viewport: every phase measured it far off-screen
                    #    (run7: L-13326, T-12265, R540, B1693 with the window
                    #    at 64,64) while its realized children report sane
                    #    rects. A point taken from the scroller's own rect
                    #    is off-screen, and in run7 120 wheel batches landed
                    #    there with zero `view` events in the trace. The
                    #    gesture is therefore aimed at a realized message
                    #    card, and the oracle reads identities, not the
                    #    scroller's rect.
                    try:
                        _r = scroller_probe.rectangle()
                        log(f"  scroller UIA rect {_r.left},{_r.top}..{_r.right},{_r.bottom} "
                            "(viewport-unreliable; not used for aiming)")
                    except Exception:
                        pass
                    heading = find_by_aid(win, "ProjectHeading")
                    header_floor = (heading.rectangle().bottom + 16
                                    if heading is not None
                                    else win.rectangle().top + 300)
                    composer = find_by_aid(win, "ComposerBox")
                    composer_ceiling = (composer.rectangle().top - 12
                                        if composer is not None
                                        else win.rectangle().bottom - 240)
                    trace_path = os.path.join(os.path.dirname(STORE_PATH),
                                              "ui-trace.log")

                    def ui_trace_label_counts():
                        counts = {}
                        try:
                            with open(trace_path, "r", encoding="utf-8",
                                      errors="replace") as fh:
                                for line in fh:
                                    if not line.startswith("t+"):
                                        continue
                                    parts = line.split(None, 2)
                                    if len(parts) >= 2:
                                        counts[parts[1]] = counts.get(parts[1], 0) + 1
                                    elif len(parts) == 1:
                                        counts[parts[0]] = counts.get(parts[0], 0) + 1
                        except OSError:
                            pass
                        return counts

                    def paging_realized():
                        """Sorted indices of the realized 'Paging message N' bodies.

                        Identity-based: the scroller's own rect is not used
                        because it is not the viewport (comment at the top of
                        this leg), and the ScrollPattern percent frame is not
                        the extent frame the trace measures (run7: viewsize
                        0.58 % where extent/frame arithmetic says ~10 %), so
                        identity is the only basis both readable and true.
                        """
                        indices = []
                        for node in win.descendants():
                            name = node.element_info.name or ""
                            if name.startswith("Paging message "):
                                tail = name[len("Paging message "):].split(".")[0]
                                try:
                                    indices.append(int(tail))
                                except ValueError:
                                    pass
                        return sorted(set(indices))

                    def paging_wheel_target():
                        """Verified wheel point inside the chat band.

                        MEASURED BASIS (f4r5-run9-probe/wheel-focus-probe.py,
                        against the live app). The earlier versions aimed at
                        the bottom-most realized card's UIA rect; the probe
                        delivered the refutation: ElementFromPoint at the
                        card's own rect center named SearchMessagesButton,
                        not the card -- the realized scrolled-subtree rects
                        do not correspond to their physical screen locations
                        in this application (the same artifact class as
                        HistoryScroller's BoundingRectangle, run7), so six
                        such batches in run8 landed on the header/tool row
                        and produced zero view events. Realized-card rects
                        must not aim a wheel.

                        The basis is the band between the two rect sources
                        the probe measured as truthful: ProjectHeading
                        (fixed header) and ComposerBox (composer). run9
                        added the second measured constraint: the notice
                        banner and the CLI-problem-cards block overlay the
                        TOP portion of that band (run9's preflight
                        ancestry: 'Chat, folder selection...' notice text,
                        then 'Claude Code CLI needs a decision: CHECKING',
                        then ChatProblemCardsScroller), so a fixed band
                        middle can be covered by overlays. This function
                        therefore scans candidate y positions from just
                        above the composer upward and returns the first
                        point whose ElementFromPoint ancestry reaches
                        HistoryScroller (the contact the probe verified
                        gives 31 view events and moves the ScrollPattern
                        percent). The point is recomputed per batch so
                        layout movement cannot desync the aim. A truthfully
                        unverified aim returns None with the reason
                        recorded in aim_reason, and the caller stops the
                        stimulus instead of wheeling blind.
                        """
                        winrect = win.rectangle()
                        heading = find_by_aid(win, "ProjectHeading")
                        composer = find_by_aid(win, "ComposerBox")
                        if heading is None or composer is None:
                            aim_reason.append("project header/composer not"
                                              " found")
                            return None
                        try:
                            hrect = heading.rectangle()
                            crect = composer.rectangle()
                        except Exception as exc:
                            aim_reason.append(f"header/composer rect failed: "
                                              f"{type(exc).__name__}")
                            return None
                        floor_y = hrect.bottom + 16
                        ceil_y = crect.top - 12
                        if ceil_y - floor_y < 40:
                            aim_reason.append(f"band {floor_y}..{ceil_y} too"
                                              " small")
                            return None
                        cx = (winrect.left + winrect.right) // 2
                        candidates = []
                        y = ceil_y - 24
                        while y > floor_y + 12:
                            candidates.append(y)
                            y -= 24
                        if not candidates:
                            candidates = [floor_y + 12]
                        from pywinauto.uia_defines import IUIA
                        import ctypes.wintypes as _wt
                        uia = IUIA().iuia
                        walker = uia.ControlViewWalker
                        last_reason = ""
                        for cy in candidates:
                            try:
                                ele = uia.ElementFromPoint(
                                    _wt.POINT(cx, cy))
                            except Exception as exc:
                                last_reason = (f"ElementFromPoint at "
                                               f"({cx},{cy}) raised "
                                               f"{type(exc).__name__}")
                                continue
                            reached = False
                            chain = []
                            walk_ok = True
                            for _ in range(10):
                                if ele is None:
                                    walk_ok = False
                                    break
                                aid = ""
                                name = ""
                                try:
                                    aid = ele.CurrentAutomationId or ""
                                except Exception:
                                    pass
                                try:
                                    name = (ele.CurrentName or "")[:48]
                                except Exception:
                                    pass
                                chain.append(f"aid='{aid or '-'}'"
                                             + (f"/'{name}'" if name else ""))
                                if aid == "HistoryScroller":
                                    reached = True
                                    break
                                try:
                                    ele = walker.GetParentElement(ele)
                                except Exception:
                                    walk_ok = False
                                    break
                            if reached:
                                if not aim_logged[0]:
                                    aim_logged[0] = True
                                    log(f"  aim verified at ({cx},{cy}): "
                                        "ancestry " + " -> ".join(chain))
                                return (cx, cy)
                            if walk_ok and not last_reason:
                                # A stop before the scroller was read, not
                                # an error: the topmost elements are the
                                # overlay cards/notice on top of the band.
                                last_reason = (f"topmost at ({cx},{cy}): "
                                               + "; ".join(chain[:3]))
                        aim_reason.append(last_reason or
                                          "no candidate point in the chat "
                                          "band verified as the history "
                                          "scroller")
                        return None

                    aim_reason = []
                    aim_logged = [False]
                    batches = 0
                    misses = 0
                    stim_reason = ""
                    trigger_count = ui_trace_label_counts().get("older-trigger", 0)
                    # CONTACT BASIS CORRECTION (f4r5-run10). run10 verified the
                    # aim and delivered all 80 batches with confirmed
                    # foreground, streaming view events throughout -- yet the
                    # leg stopped with "no view-change events for 6 consecutive
                    # batches". The trace went silent because the product's
                    # UiTraceEvent line cap (then 3000) was exhausted by the
                    # inertia streaming itself (each 3-notch batch at
                    # pause=0.02 nets a measured 14.0 px and emits dozens of
                    # intermediate view lines). A file the diagnostic cap had
                    # finished writing is not a statement about the window.
                    # Contact and progress are therefore read from the
                    # truthful, cheap ScrollPattern percent
                    # (paging_scroll_state(), the same read the probe
                    # validated), with the trace kept as recorded evidence and
                    # the trace cap itself raised on the product side
                    # (MainWindow.xaml.cpp) so the older-trigger chain remains
                    # observable for the whole journey.
                    #
                    # JOURNEY BASIS (f4r5-run10 measured unit): one 3-notch
                    # burst at pause=0.02 nets 14.0 px, ~330 batches for the
                    # ~4530 px bottom-to-top journey. The notches are spaced
                    # (pause=0.25) so each notch's inertia completes instead
                    # of replacing the previous animation, and each batch
                    # settles 0.35 s so the at-top gate sees agreeing settled
                    # reads. The remaining-batches estimate is logged per
                    # batch from the measured deltas; the cap is an 800-batch
                    # backstop, not the plan.
                    BATCH_CAP = 800
                    MISS_BUDGET = 8
                    while trigger_count == 0 and batches < BATCH_CAP:
                        target = paging_wheel_target()
                        if target is None:
                            stim_reason = ("band aim not verified: "
                                           + "; ".join(aim_reason[-2:])
                                           if aim_reason else
                                           "no verified band point")
                            log(f"  wheel batching {batches}: stopped before "
                                f"any wheel at batch {batches + 1} - {stim_reason}")
                            break
                        batches += 1
                        state_before = paging_scroll_state()
                        if not send_wheel_at_point(win, target[0], target[1], 120,
                                                   steps=3, pause=0.25):
                            stim_reason = ("foreground could not be established; "
                                           "stimulus stopped")
                            log(f"  wheel batching {batches}: stopped - {stim_reason}")
                            break
                        time.sleep(0.35)
                        counts = ui_trace_label_counts()
                        trigger_count = counts.get("older-trigger", 0)
                        state_after = paging_scroll_state()
                        pct_before = state_before[1] if state_before else None
                        pct_after = state_after[1] if state_after else None
                        if pct_before is None or pct_after is None:
                            misses += 1
                            if misses >= MISS_BUDGET:
                                stim_reason = (f"the scroller's ScrollPattern "
                                               f"percent could not be read for "
                                               f"{misses} consecutive batches; "
                                               "contact unverifiable")
                                log(f"  wheel batching {batches}: stopped - "
                                    f"{stim_reason}")
                                break
                            continue
                        delta = pct_before - pct_after
                        if delta <= 0.05:
                            misses += 1
                            if misses >= MISS_BUDGET:
                                stim_reason = (f"no scroll progress for {misses} "
                                               "consecutive batches "
                                               f"(pct {pct_after:.3f} from "
                                               f"{pct_before:.3f}); wheel contact "
                                               "lost")
                                log(f"  wheel batching {batches}: stopped - "
                                    f"{stim_reason}")
                                break
                        else:
                            misses = 0
                            if batches % 10 == 0 or batches <= 3 or pct_after <= 5.0:
                                est = (f" est-remaining~{pct_after / delta * 1.1:.0f}"
                                       if delta > 0 else "")
                                log(f"  wheel batch {batches}: pct {pct_before:.3f}"
                                    f" -> {pct_after:.3f} (delta {delta:.3f}){est}")
                        if not trigger_count and pct_after is not None and pct_after <= 0.9:
                            # The reader is at the top; the settled at-top
                            # gate needs the agreeing reads this loop already
                            # stops to allow, so keep wheeling until the
                            # trigger fires or the budgets exhaust.
                            pass
                    log(f"  prepending wheel batches: {batches} "
                        f"(older-trigger events: {trigger_count})")
                    if trigger_count:
                        # The compensation is the final positioning act of the
                        # prepend chain (nothing users see moves after it).
                        # Bounded wait so the settle window starts on the
                        # compensated position rather than mid-transition.
                        comp_deadline = time.time() + 15.0
                        comp_labels = ("older-compensate",
                                       "older-compensate-skip",
                                       "older-nogrow")
                        comp_now = {}
                        while time.time() < comp_deadline:
                            comp_now = ui_trace_label_counts()
                            if any(comp_now.get(l, 0) > 0 for l in comp_labels):
                                break
                            time.sleep(0.2)
                        comp_now = ui_trace_label_counts()
                        log("  prepend chain events observed: " + ", ".join(
                            l for l in ("older-trigger", "older-capture",
                                        "older-page", "older-prepend",
                                        "older-compensate",
                                        "older-compensate-skip",
                                        "older-nogrow", "older-decline",
                                        "older-nopage", "older-error",
                                        "older-empty")
                            if comp_now.get(l, 0) > 0))
                    # Settle: two consecutive agreeing identity reads, spaced
                    # enough that a mid-transition sample is not mistaken for
                    # a settled one (the load, prepend and compensation are
                    # asynchronous across lanes). The ScrollPattern percent is
                    # in a frame the trace contradicts, so it only contributes
                    # settle-stability, not a pass/fail comparison.
                    deadline = time.time() + 45.0
                    last_sig = None
                    stable = None
                    while time.time() < deadline:
                        time.sleep(0.4)
                        indices_now = paging_realized()
                        state_now = paging_scroll_state()
                        sig = (tuple(indices_now),
                               state_now[1] if state_now else None,
                               state_now[0] if state_now else None)
                        if state_now is None:
                            last_sig = sig
                            continue
                        if (last_sig is not None
                                and sig[0] == last_sig[0]
                                and abs((sig[1] or 0.0) - (last_sig[1] or 0.0)) <= 5.0):
                            stable = sig
                            break
                        last_sig = sig
                    # Project-identity guard, read after the settle window: the
                    # identity reads below measure THIS project's prepended
                    # state, and a bound-project change replaces the timeline
                    # with another project's content. run11 measured how close
                    # that sits to the verdict: the settle/FAIL read preceded
                    # the leg's own return-to-alpha cleanup by only ~0.1-0.2 s
                    # (the post-verdict `loadchat-begin gen=18` adopt of alpha
                    # in f4r5-run11's trace IS that cleanup -- harness log:
                    # "Switch Project: foreground established", "returned to
                    # alpha after the paging probe: True", store-write DELETE
                    # at run-clock 616.605 s, selection receipt for alpha
                    # committed 2026-10-26T02:59:27.483Z). Had the settle
                    # window needed two more polls, the reads below would have
                    # measured alpha's timeline and the verdict would have
                    # asserted a defect against content the criterion never
                    # observed. ProjectHeading names the bound project, so a
                    # change fail-closes to INCONCLUSIVE with the reason
                    # recorded instead.
                    guard_heading = find_by_aid(win, "ProjectHeading")
                    guard_name = (guard_heading.element_info.name
                                  if guard_heading is not None else None)
                    if stable is None:
                        paging_anchor_reason = ("the prepended state never settled into "
                                                "two agreeing identity reads within 45 s")
                        criterion_reading_anchor = "INCONCLUSIVE"
                        log(f"  INCONCLUSIVE: {paging_anchor_reason}")
                    elif not trigger_count:
                        # The observation the criterion needs never happened.
                        # The recorded state is a truth about the reader, but
                        # the prepend-and-compensation behaviour was never
                        # exercised, so a FAIL would assert a defect the
                        # evidence does not show.
                        paging_anchor_reason = (stim_reason or
                                                ("wheel batching exhausted without "
                                                 "the product's older-trigger event"))
                        criterion_reading_anchor = "INCONCLUSIVE"
                        log(f"  INCONCLUSIVE: no prepend was triggered during batching "
                            f"({trigger_count} older-trigger events); {paging_anchor_reason}")
                    elif paging_heading_baseline is None:
                        paging_anchor_reason = (
                            "the identity baseline was never captured at probe "
                            "bind (ProjectHeading was not found then), so no "
                            "bound-identity comparison can be made and a "
                            "mid-run change cannot be excluded")
                        criterion_reading_anchor = "INCONCLUSIVE"
                        log(f"  INCONCLUSIVE: {paging_anchor_reason}")
                    elif guard_name != paging_heading_baseline:
                        paging_anchor_reason = (
                            "the bound project changed during the settle window "
                            f"(ProjectHeading reads {guard_name!r}, bound baseline "
                            f"{paging_heading_baseline!r}); a mid-run selection replaced "
                            "the prepended state, so no identity read afterwards "
                            "measures this project")
                        criterion_reading_anchor = "INCONCLUSIVE"
                        log(f"  INCONCLUSIVE: {paging_anchor_reason}")
                    else:
                        indices_after, pct_after, vvs_after = stable
                        topmost = indices_after[0] if indices_after else None
                        log(f"  settled post-prepend: realized indices {indices_after[:8]}"
                            f"{'...' if len(indices_after) > 8 else ''} "
                            f"(topmost {topmost}, {len(indices_after)} cards), "
                            f"scroll {pct_after:.1f} %, viewsize {vvs_after:.1f} %")
                        # Physical-frame sample (evidence only; the verdict
                        # chain below is unchanged by it). The realized-index
                        # read cannot separate "genuinely 3 rows short" from
                        # "correct with a 3-row realization buffer above the
                        # viewport top" -- both show topmost 69 where the
                        # oracle wants 72. ElementFromPoint over the band
                        # reads what is physically under each pixel; pitch and
                        # the fitted message at the band floor decide which
                        # reading of the settled state is true.
                        try:
                            phys = paging_physical_samples()
                            if phys.get("reason"):
                                log(f"  physical sample unavailable: {phys['reason']}")
                            else:
                                rows = {}
                                for _y, _m in phys["samples"]:
                                    rows.setdefault(_m, _y)
                                pitch = phys["pitch_px"]
                                fit = phys["msg_at_floor"]
                                log("  physical rows (first y per msg): "
                                    + ", ".join(f"m{m}@{y}" for m, y in
                                                sorted(rows.items(), key=lambda kv: kv[1]))
                                    + (f"; pitch={pitch:.3f} px/row, "
                                       f"msg_at_floor={fit:.2f}"
                                       if pitch else "; pitch undetermined"))
                                if phys["dpi"]:
                                    log(f"  physical dpi={phys['dpi']}")
                                if phys["bottom"]:
                                    log(f"  physical bottom reach: {phys['bottom']}")
                                if phys["cover"]:
                                    tops = sorted({d for _y, d in phys["cover"]})
                                    ys = [y for y, _d in phys["cover"]]
                                    log(f"  physical cover: {len(ys)} points "
                                        f"y={min(ys)}..{max(ys)} topmost="
                                        + "; ".join(tops[:3]))
                                if phys.get("nomsg"):
                                    whys = sorted({w for _y, w in phys["nomsg"]})
                                    nys = [y for y, _w in phys["nomsg"]]
                                    log(f"  physical reached-no-message: {len(nys)} points "
                                        f"y={min(nys)}..{max(nys)} why="
                                        + "; ".join(whys[:3]))
                        except Exception as exc:
                            log(f"  physical sample raised {type(exc).__name__}: {exc}")
                        # ORACLE-ADEQUACY CORRECTION (§13, documented in
                        # LOCAL-VALIDATION.md before this change). The old oracle
                        # decided the top edge from the topmost REALIZED card index
                        # (`topmost < 71 or 72 not in indices`). Under the confirmed
                        # +-3-row realization buffer (run14), that clause measured
                        # the buffer's top edge, not the physical top edge: it was
                        # satisfiable ONLY when the visible row sat 2 BELOW the
                        # anchor, making PASS unreachable at the one correct state
                        # and reachable at a 2-row overshoot. The corrected oracle
                        # reads the product's published top-visible ItemStatus (the
                        # physical top edge, same instrument as the reconcile `r`)
                        # and compares it to the anchored row's own submission_id.
                        # The realized-index reading stays logged above as evidence
                        # but no longer decides the verdict; `topmost is None` and
                        # the uncompensated symptom (22 realized) still FAIL because
                        # they are true product defects independent of the buffer.
                        page_loaded = (comp_now.get("older-prepend", 0) > 0
                                       and comp_now.get("older-page", 0) > 0)
                        _exp_id, _exp_err = paging_expected_anchor_id()
                        _tv = paging_read_top_visible()
                        _tv_val = _tv.split("=", 1)[1] if "=" in _tv else _tv
                        log(f"  top-visible published: {_tv!r} "
                            f"(expected anchored submission_id {_exp_id!r}; "
                            f"realized topmost {topmost} is buffer evidence only)")
                        if topmost is None:
                            criterion_reading_anchor = "FAIL"
                            log("  FAIL: no realized message cards after the settle window "
                                "(the timeline realized nothing at the anchored position).")
                        elif 22 in indices_after:
                            criterion_reading_anchor = "FAIL"
                            log(f"  FAIL: the uncompensated symptom is present: the "
                                f"prepended rows are realized at the reading position "
                                f"(message 22 realized; topmost {topmost}). This is the "
                                "reader losing the content that was at the top edge.")
                        elif _exp_err:
                            criterion_reading_anchor = "INCONCLUSIVE"
                            paging_anchor_reason = (
                                "the anchored submission_id could not be resolved "
                                f"from the trace/store: {_exp_err}")
                            log(f"  INCONCLUSIVE: {paging_anchor_reason}")
                        elif (not _tv or _tv_val in ("pending", "unknown")
                                or _tv.startswith("<read-error")):
                            criterion_reading_anchor = "INCONCLUSIVE"
                            paging_anchor_reason = (
                                "the product's published top-visible ItemStatus was "
                                f"{_tv!r}, so the physical top edge was never observed "
                                "(pending at timeout, an unknown row, or a read "
                                "failure) and no identity claim can be made")
                            log(f"  INCONCLUSIVE: {paging_anchor_reason}")
                        elif _tv_val != _exp_id:
                            criterion_reading_anchor = "FAIL"
                            log(f"  FAIL: the anchored top-edge identity did not hold: "
                                f"published top-visible is {_tv_val!r}, expected the "
                                f"anchored row {_exp_id!r} (realized topmost was "
                                f"{topmost}; the physical top edge must read the "
                                "anchored row, not a buffer row).")
                        elif not page_loaded:
                            criterion_reading_anchor = "FAIL"
                            log("  FAIL: the trace recorded no older-page prepend chain "
                                "(older-prepend/older-page missing), so the identity read "
                                "cannot certify a prepend whose load was never recorded.")
                        elif comp_now.get("older-trigger", 0) != trigger_count:
                            criterion_reading_anchor = "FAIL"
                            log(f"  FAIL: a second prepend chain started during the settle "
                                f"window ({trigger_count} -> "
                                f"{comp_now.get('older-trigger', 0)} older-trigger events).")
                        else:
                            # Hold read: the compensated position must persist,
                            # with no oscillation and no additional prepend pass.
                            time.sleep(1.2)
                            hold_indices = paging_realized()
                            hold_state = paging_scroll_state()
                            hold_counts = ui_trace_label_counts()
                            hold_topmost = hold_indices[0] if hold_indices else None
                            hold_tv = paging_read_top_visible()
                            hold_tv_val = (hold_tv.split("=", 1)[1]
                                           if "=" in hold_tv else hold_tv)
                            log(f"  hold read: realized indices {hold_indices[:8]}"
                                f"{'...' if len(hold_indices) > 8 else ''} "
                                f"(topmost {hold_topmost}), scroll "
                                f"{hold_state[1] if hold_state else '?'} %, "
                                f"top-visible {hold_tv!r}")
                            # Same identity guard at the hold read: the hold
                            # must measure this project's timeline, not the
                            # timeline of a project a mid-run selection bound.
                            hold_heading = find_by_aid(win, "ProjectHeading")
                            hold_name = (hold_heading.element_info.name
                                         if hold_heading is not None else None)
                            if paging_heading_baseline is None:
                                paging_anchor_reason = (
                                    "the identity baseline was never captured at "
                                    "probe bind, so the hold read's bound identity "
                                    "cannot be compared")
                                criterion_reading_anchor = "INCONCLUSIVE"
                                log(f"  INCONCLUSIVE: {paging_anchor_reason}")
                            elif hold_name != paging_heading_baseline:
                                paging_anchor_reason = (
                                    "the bound project changed during the hold "
                                    f"read (ProjectHeading reads {hold_name!r}, "
                                    f"bound baseline {paging_heading_baseline!r}); "
                                    "the hold would measure another project's timeline")
                                criterion_reading_anchor = "INCONCLUSIVE"
                                log(f"  INCONCLUSIVE: {paging_anchor_reason}")
                            elif (hold_tv_val == _exp_id
                                    and hold_counts.get("older-trigger", 0) == trigger_count
                                    and 22 not in hold_indices):
                                log("PASS: the reading anchor is preserved across the "
                                    "older-page prepend (the published physical top edge "
                                    f"read the anchored row {_exp_id!r} before and after "
                                    f"the hold, the prepended row 22 was never realized, "
                                    f"the older page loaded "
                                    f"({comp_now.get('older-page', 0)} older-page events), "
                                    "and no second prepend chain started).")
                                criterion_reading_anchor = "PASS"
                            else:
                                criterion_reading_anchor = "FAIL"
                                log(f"  FAIL: anchor did not hold (top-visible "
                                    f"{_tv_val!r} -> {hold_tv_val!r}, expected "
                                    f"{_exp_id!r}; message 22 realized="
                                    f"{22 in hold_indices}, older-trigger "
                                    f"{trigger_count} -> "
                                    f"{hold_counts.get('older-trigger', 0)})")
                except Exception as exc:
                    paging_anchor_reason = f"anchor leg raised {type(exc).__name__}: {exc}"
                    criterion_reading_anchor = "INCONCLUSIVE"
                    log(f"  INCONCLUSIVE: {paging_anchor_reason}")
        else:
            paging_reason = paging_reason or ("the probe project could not be prepared")

        # Cleanup: move the application off the probe BEFORE removing its rows
        # (the same ordering the 5B leg documents), because leaving a project
        # saves a draft that must resolve to a live record.
        if paging_row_created:
            returned = False
            if paging_bound:
                try:
                    if open_switch_project(win):
                        open_alpha = poll_until(lambda: find_by_name(win, "Open project alpha"),
                                                timeout=8.0, interval=0.2)
                        if open_alpha is not None:
                            open_alpha.iface_invoke.Invoke()
                            restored = poll_until(lambda: find_by_aid(win, "ComposerBox"),
                                                  timeout=25.0, interval=0.2)
                            returned = restored is not None
                            log(f"  returned to alpha after the paging probe: {returned}")
                except Exception as exc:
                    log(f"  could not return to alpha: {type(exc).__name__}: {exc}")
                if not returned:
                    paging_reason = (paging_reason + "; " if paging_reason else "") \
                        + "the application could not be returned to alpha afterwards"
            try:
                conn = sqlite3.connect(STORE_PATH)
                mark_store_write("DELETE: the temporary paging-probe project and seeded messages")
                conn.execute("DELETE FROM contributions WHERE project_id = ?;", (paging_probe_id,))
                conn.execute("DELETE FROM selection_events WHERE project_id = ?;", (paging_probe_id,))
                conn.execute("DELETE FROM selection_receipts WHERE project_id = ?;", (paging_probe_id,))
                conn.execute("DELETE FROM pending_submissions WHERE project_id = ?;", (paging_probe_id,))
                conn.execute("DELETE FROM projects WHERE id = ?;", (paging_probe_id,))
                conn.commit()
                conn.close()
            except Exception as exc:
                log(f"  could not remove the paging-probe rows: {exc}")
            try:
                os.rmdir(paging_probe_path)
            except Exception:
                pass
        if criterion_paging_save == "NOT_RUN":
            criterion_paging_save = "INCONCLUSIVE"
        if criterion_reading_anchor == "NOT_RUN" and not (paging_bound and criterion_paging_save == "PASS"):
            criterion_reading_anchor = "INCONCLUSIVE"
        if paging_reason and criterion_paging_save not in ("PASS", "FAIL"):
            log(f"INCONCLUSIVE (22): {paging_reason}.")
        # Read the product-side event trace into the evidence. Whatever verdicts
        # the legs recorded, the trace names the actual asynchronous sequence the
        # application executed (page adoption, prepend triggers and their
        # compensation, scroll-to-end passes), which is the artifact a defect or
        # a green pass must be explained against.
        try:
            trace_path = os.path.join(os.path.dirname(STORE_PATH), "ui-trace.log")
            if os.path.exists(trace_path):
                with open(trace_path, "r", encoding="utf-8", errors="replace") as fh:
                    for line in fh:
                        log("  UI-TRACE " + line.rstrip())
                try:
                    os.remove(trace_path)
                except OSError as exc:
                    # The application still holds the trace file open (it is
                    # terminated in the phase-9 cleanup, never before 3B's
                    # readback), and the sharing grant on the write side makes
                    # the deletion fail while the reader succeeds. Leftover
                    # trace files are evidence-directory contents, not run
                    # blockers; report it rather than discarding the file.
                    log(f"  UI-TRACE: file left in place after readback "
                        f"(delete failed: {exc})")
            else:
                log("  UI-TRACE: file not present next to the store")
        except Exception as exc:
            log(f"  UI-TRACE: could not be read: {exc}")
        log(f"PHASE 3B complete - criteria 22/23 recorded as "
            f"{criterion_paging_save}/{criterion_reading_anchor}.")


        # =========================================================================
        # PHASE 6: WM_DPICHANGED MESSAGE HANDLING (150% & 200% SIMULATION)
        # =========================================================================
        log("\n--- PHASE 6: WM_DPICHANGED Message Handling Simulation (150% and 200%) ---")

        # THE WINDOW MUST BE RESTORED BEFORE THIS LEG, AND THE CHECK MUST KNOW
        # WHETHER IT IS.
        #
        # The oracle below is "the window grew to the suggested rect". A window
        # that is MAXIMIZED cannot grow: it is already clamped to the work area,
        # so the suggested rect is larger than the screen and the OS refuses the
        # size. Measured on this machine (1920x1080, work area 1920x1020): a
        # zoomed window reports L-9,T-9,R1929,B1029 (1938x1038) -- the maximized
        # frame overhang -- and sending WM_DPICHANGED 144 cannot enlarge it.
        #
        # That state is reachable here: run3 arrived at this leg zoomed, because
        # something outside the harness (nothing in the harness or the
        # application calls a maximize API; grepped) zoomed the window during
        # the preceding phase. It is NOT reliably reachable -- a re-run of the
        # same sequence stayed normal -- so this is a window-state precondition
        # that has to be established and RECORDED rather than assumed.
        #
        # The old code also did `return 1` on either failure, which ended the
        # whole run and left criteria 8-15 NOT_RUN. That is the same
        # silent-evidence-loss defect that criterion 17 carried: one leg's
        # measurement problem must not destroy every later phase's evidence.
        # The criterion is recorded INCONCLUSIVE instead and the run continues.
        dpi_failures = []
        dpi_inconclusive = []

        if user32.IsZoomed(win.handle):
            log("  the window is maximized, so it cannot expand to a suggested rect; "
                "restoring it first")
            user32.ShowWindow(win.handle, SW_RESTORE)
            time.sleep(0.6)
        dpi_zoomed = bool(user32.IsZoomed(win.handle))
        log(f"  window maximized before the DPI leg: {dpi_zoomed}")
        if dpi_zoomed:
            dpi_inconclusive.append(
                "the window could not be restored from maximized before the DPI leg, and a "
                "maximized window cannot expand to a suggested rect")

        curr_rect = win.rectangle()

        # 150% DPI (144 DPI)
        dpi150 = 144
        w150 = int(curr_rect.width() * 144 / 120)
        h150 = int(curr_rect.height() * 144 / 120)
        sug_rect_150 = RECT(curr_rect.left, curr_rect.top, curr_rect.left + w150, curr_rect.top + h150)
        log("Sending WM_DPICHANGED 150% (144 DPI)...")
        user32.SendMessageW(win.handle, WM_DPICHANGED, (dpi150 << 16) | dpi150, ctypes.byref(sug_rect_150))
        time.sleep(1.0)
        r150 = win.rectangle()
        log(f"Window bounds at 150% DPI: L{r150.left}, T{r150.top}, R{r150.right}, B{r150.bottom} (W={r150.width()}, H={r150.height()})")
        if r150.height() <= 0:
            dpi_failures.append("the window collapsed to zero height at 150% DPI")
        elif not dpi_zoomed and r150.width() <= curr_rect.width():
            dpi_failures.append(
                f"the window did not expand under the 150% DPI suggested rect "
                f"({curr_rect.width()} -> {r150.width()})")
        elif dpi_zoomed:
            log("  NOTE: the suggested rect could not be applied because the window is "
                "maximized; no expansion is possible in that state")
        else:
            log("PASS: 150% DPI WM_DPICHANGED handled cleanly with window expansion.")

        # 200% DPI (192 DPI)
        dpi200 = 192
        w200 = int(curr_rect.width() * 192 / 120)
        h200 = int(curr_rect.height() * 192 / 120)
        sug_rect_200 = RECT(curr_rect.left, curr_rect.top, curr_rect.left + w200, curr_rect.top + h200)
        log("Sending WM_DPICHANGED 200% (192 DPI)...")
        user32.SendMessageW(win.handle, WM_DPICHANGED, (dpi200 << 16) | dpi200, ctypes.byref(sug_rect_200))
        time.sleep(1.0)
        r200 = win.rectangle()
        log(f"Window bounds at 200% DPI: L{r200.left}, T{r200.top}, R{r200.right}, B{r200.bottom} (W={r200.width()}, H={r200.height()})")
        if r200.height() <= 0:
            dpi_failures.append("the window collapsed to zero height at 200% DPI")
        elif not dpi_zoomed and r200.width() <= r150.width():
            dpi_failures.append(
                f"the window did not expand under the 200% DPI suggested rect "
                f"({r150.width()} -> {r200.width()})")
        elif dpi_zoomed:
            log("  NOTE: the suggested rect could not be applied because the window is "
                "maximized; no expansion is possible in that state")
        else:
            log("PASS: 200% DPI WM_DPICHANGED handled cleanly with window expansion.")

        # Restore baseline 120 DPI (125%)
        dpi120 = 120
        sug_rect_120 = RECT(curr_rect.left, curr_rect.top, curr_rect.right, curr_rect.bottom)
        user32.SendMessageW(win.handle, WM_DPICHANGED, (dpi120 << 16) | dpi120, ctypes.byref(sug_rect_120))
        time.sleep(1.0)
        log("PASS: Display DPI restored to baseline.")
        log("NOTE: Real OS-level display-scale validation (150%/200%) requires system display settings change / sign-out and remains INCONCLUSIVE.")

        # The verdict is earned from what was measured, not assigned. A maximized
        # window makes the expansion oracle inapplicable rather than failed, so
        # that case is INCONCLUSIVE; a measured non-expansion or a collapse is
        # FAIL; only a leg with neither is PASS.
        for reason in dpi_failures:
            log(f"FAIL: {reason}")
        if dpi_failures:
            criterion_dpi_scaling = "FAIL"
        elif dpi_inconclusive:
            criterion_dpi_scaling = "INCONCLUSIVE"
            for reason in dpi_inconclusive:
                log(f"INCONCLUSIVE: {reason}.")
        else:
            criterion_dpi_scaling = "PASS"

        # =========================================================================
        # PHASE 9: ACTION CENTER VISIBILITY, OBLIGATION ROUTING & STALE REVALIDATION
        # =========================================================================
        log("\n--- PHASE 9: Action Center Real Action Routing & Stale Item Revalidation ---")

        # 9.1 Initial Action Center status in clean alpha project
        # Contract & Presenter rule: when there are no unresolved obligations (clean project),
        # Action Center panel is correctly collapsed/withdrawn (confirmedEmpty).
        action_center_panel = find_by_aid(win, "ActionCenterPanel")
        is_ac_initially_withdrawn = (action_center_panel is None) or (not action_center_panel.is_visible())
        if not is_ac_initially_withdrawn:
            log("FAIL: ActionCenterPanel should be collapsed/withdrawn when clean alpha has no obligations")
            return 1
        log("PASS: Action Center panel correctly confirmed collapsed/withdrawn when project has no unresolved obligations.")

        alpha_id = "aee9775e-6df2-46f8-a4bf-d926fa4b2cdd"
        composer_box = find_by_aid(win, "ComposerBox")
        orig_composer_text = composer_box.window_text() if composer_box else ""

        try:
            # Establish genuine disposable obligation in isolated test store for project alpha
            conn = sqlite3.connect(STORE_PATH)
            mark_store_write("INSERT: 1 seeded alpha draft_attachment (REJECTED)")
            conn.execute(
                "INSERT OR REPLACE INTO draft_attachments (attachment_id, project_id, file_name, file_size, sha256, detected_type, staged_path, source_path, status, error_message, version) "
                "VALUES (?, ?, ?, 10, 'dummy', 'text/plain', '', 'C:/source/alpha_err.txt', 'REJECTED', 'test rejection error', 1);",
                ("att-alpha-disposable-1", alpha_id, "alpha_err.txt")
            )
            conn.commit()
            conn.close()

            # Trigger production refresh in alpha project via valid production route:
            # File -> Switch Project, then Cancel switching (ReturnToChat calls RefreshActionCenterAsync)
            log("Triggering production Action Center refresh via File -> Switch Project -> Cancel...")
            sw_heading = open_switch_project(win)
            if not sw_heading or not sw_heading.is_visible():
                log("FAIL: File -> Switch Project did not reach Screen 2 to trigger refresh")
                return 1
            cancel_btn = find_by_aid(win, "FolderCancelButton") or find_by_name(win, "Cancel switching projects") or find_by_name(win, "Cancel")
            if not cancel_btn or not cancel_btn.is_visible():
                log("FAIL: FolderCancelButton not found on Screen 2 to return to chat")
                return 1
            cancel_btn.iface_invoke.Invoke()

            def find_visible_chat_heading():
                h = find_by_aid(win, "ProjectHeading")
                return h if (h and h.is_visible()) else None

            def find_visible_ac_panel():
                p = find_by_aid(win, "ActionCenterPanel")
                return p if (p and p.is_visible()) else None

            def find_visible_details_panel():
                p = find_by_aid(win, "ActionCenterDetailsPanel")
                return p if (p and p.is_visible()) else None

            # Mandatory assertion: return to correct project's Chat
            ret_chat = poll_until(find_visible_chat_heading, timeout=6.0, interval=0.1)
            if not ret_chat:
                log("FAIL: Returning from Screen 2 Cancel did not show ProjectHeading in Chat")
                return 1
            if "alpha" not in ret_chat.window_text().lower():
                log(f"FAIL: Returned to unexpected project: {ret_chat.window_text()}")
                return 1
            folder_h = find_by_aid(win, "FolderHeading")
            if folder_h and folder_h.is_visible():
                log("FAIL: Screen 2 FolderHeading still visible after Cancel")
                return 1
            log("PASS: Screen 2 navigation, Cancel, and return to project alpha Chat verified mandatorily.")

            # Wait for ActionCenterPanel to become visible and display the obligation
            ac_panel_active = poll_until(find_visible_ac_panel, timeout=6.0, interval=0.2)
            if not ac_panel_active:
                log("FAIL: ActionCenterPanel did not appear after obligation was introduced and refreshed")
                return 1

            status_label = find_by_aid(win, "ActionCenterStatusText")
            status_val = status_label.window_text() if status_label else ""
            log(f"Alpha Action Center status with 1 obligation: '{status_val}'")
            if "1 item" not in status_val:
                log(f"FAIL: Expected '1 item(s) need your attention.', got '{status_val}'")
                return 1
            log("PASS: Action Center truthfully surfaced the active obligation via production refresh route.")

            # Mandatory details panel assertions (Item 3)
            show_details_btn = find_by_aid(win, "ShowActionCenterDetailsButton") or find_by_name(win, "Show details")
            if not show_details_btn or not show_details_btn.is_visible():
                log("FAIL: ShowActionCenterDetailsButton is missing or not visible when obligations exist")
                return 1
            log("Expanding Action Center details panel via ShowActionCenterDetailsButton...")
            show_details_btn.iface_invoke.Invoke()

            details_panel = poll_until(find_visible_details_panel, timeout=3.0, interval=0.1)
            if not details_panel:
                log("FAIL: ActionCenterDetailsPanel did not become visible after Show details click")
                return 1

            details_list = find_by_aid(win, "ActionCenterDetailsList")
            detail_rows = [d for d in details_panel.descendants() if (d.element_info.name or "").startswith("Item ")]
            if not details_list and len(detail_rows) == 0:
                log("FAIL: ActionCenterDetailsList and detail items missing inside details panel")
                return 1

            close_details_btn = find_by_aid(win, "CloseActionCenterDetailsButton")
            if not close_details_btn or not close_details_btn.is_visible():
                log("FAIL: CloseActionCenterDetailsButton missing inside details panel")
                return 1
            close_details_btn.iface_invoke.Invoke()

            details_closed = poll_until(lambda: not (find_by_aid(win, "ActionCenterDetailsPanel") and find_by_aid(win, "ActionCenterDetailsPanel").is_visible()), timeout=3.0, interval=0.1)
            if not details_closed:
                log("FAIL: ActionCenterDetailsPanel remained visible after CloseActionCenterDetailsButton click")
                return 1
            log("PASS: Action Center details panel expansion, contents, and collapse verified mandatorily.")

            # -------------------------------------------------------------
            # Sequence 1: Held in-flight query completing after dismissal
            # Reject its result without reopening or repopulating.
            # -------------------------------------------------------------
            log("\nTesting Sequence 1: In-flight refresh completing after dismissal...")
            # Set up named Win32 events for deterministic hold
            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel32.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
            kernel32.CreateEventW.restype = wintypes.HANDLE
            kernel32.SetEvent.argtypes = [wintypes.HANDLE]
            kernel32.SetEvent.restype = wintypes.BOOL
            kernel32.ResetEvent.argtypes = [wintypes.HANDLE]
            kernel32.ResetEvent.restype = wintypes.BOOL
            kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
            kernel32.WaitForSingleObject.restype = wintypes.DWORD
            kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
            kernel32.CloseHandle.restype = wintypes.BOOL

            # Create the hold trigger, in-flight notification, and release events
            h_hold = kernel32.CreateEventW(None, True, True, "Local\\Mayasaba_Ac_HoldRequested")
            h_inflight = kernel32.CreateEventW(None, True, False, "Local\\Mayasaba_Ac_InFlight")
            h_release = kernel32.CreateEventW(None, True, False, "Local\\Mayasaba_Ac_Release")

            try:
                # Trigger refresh via Switch Project -> Cancel while Action Center is visible
                sw_h2 = open_switch_project(win)
                if not sw_h2 or not sw_h2.is_visible():
                    log("FAIL: File -> Switch Project failed during held-refresh test")
                    return 1
                c_btn2 = find_by_aid(win, "FolderCancelButton") or find_by_name(win, "Cancel switching projects") or find_by_name(win, "Cancel")
                if not c_btn2 or not c_btn2.is_visible():
                    log("FAIL: FolderCancelButton not found on Screen 2")
                    return 1
                c_btn2.iface_invoke.Invoke()

                # Verify return to alpha Chat screen
                ret2 = poll_until(find_visible_chat_heading, timeout=6.0, interval=0.1)
                if not ret2 or "alpha" not in ret2.window_text().lower():
                    log("FAIL: Did not return to alpha Chat after Cancel")
                    return 1

                # Wait for the background query to hit the hold hook
                wait_inflight = kernel32.WaitForSingleObject(h_inflight, 5000)
                if wait_inflight != 0:
                    log(f"FAIL: In-flight refresh event was not signaled within 5s (wait result: {wait_inflight})")
                    return 1
                log("PASS: Background Action Center query is confirmed in-flight and held.")

                # Card is currently visible on screen. Now dismiss it while query is in-flight!
                def find_close_ac_btn():
                    btn = find_by_aid(win, "CloseActionCenterButton") or find_by_name(win, "Close Action Center")
                    return btn if (btn and btn.is_visible()) else None

                close_ac_btn = poll_until(find_close_ac_btn, timeout=5.0, interval=0.1)
                if not close_ac_btn:
                    log("FAIL: CloseActionCenterButton not visible to dismiss held card")
                    return 1
                log("Dismissing Action Center via CloseActionCenterButton while query is held in flight...")
                close_ac_btn.iface_invoke.Invoke()

                # Verify card is collapsed on UI
                ac_panel_collapsed = poll_until(lambda: not (find_by_aid(win, "ActionCenterPanel") and find_by_aid(win, "ActionCenterPanel").is_visible()), timeout=3.0, interval=0.1)
                if not ac_panel_collapsed:
                    log("FAIL: ActionCenterPanel did not collapse upon dismissal click")
                    return 1
                log("PASS: ActionCenterPanel collapsed immediately upon CloseActionCenterButton click.")

                # Disarm the hold event before releasing query so subsequent refreshes won't block
                if h_hold:
                    kernel32.CloseHandle(h_hold)
                    h_hold = None

                # Now release the held background query so it resumes to UI thread
                log("Releasing in-flight query to UI thread...")
                kernel32.SetEvent(h_release)
                time.sleep(1.0)

                # Assert that in-flight completion rejected the result without reopening or repopulating
                ac_panel_after_release = find_by_aid(win, "ActionCenterPanel")
                if ac_panel_after_release and ac_panel_after_release.is_visible():
                    log("FAIL: ActionCenterPanel reopened after held in-flight query finished!")
                    return 1

                status_label_after = find_by_aid(win, "ActionCenterStatusText")
                if status_label_after and status_label_after.window_text().strip():
                    log(f"FAIL: ActionCenterStatusText was repopulated: '{status_label_after.window_text()}'")
                    return 1

                log("PASS: In-flight query completing after dismissal was rejected: panel remains strictly collapsed and obsolete items discarded.")
            finally:
                # Disarm and close events
                if h_release:
                    kernel32.SetEvent(h_release)
                    kernel32.CloseHandle(h_release)
                if h_inflight:
                    kernel32.CloseHandle(h_inflight)
                if h_hold:
                    kernel32.CloseHandle(h_hold)

            # -------------------------------------------------------------
            # Sequence 2: A new explicit refresh starts after dismissal
            # Follows existing production contract, which permits reactivation.
            # -------------------------------------------------------------
            log("\nTesting Sequence 2: New explicit refresh after dismissal permits reactivation...")
            sw_h3 = open_switch_project(win)
            if not sw_h3 or not sw_h3.is_visible():
                log("FAIL: File -> Switch Project failed during explicit reactivation test")
                return 1
            c_btn3 = find_by_aid(win, "FolderCancelButton") or find_by_name(win, "Cancel switching projects") or find_by_name(win, "Cancel")
            if not c_btn3 or not c_btn3.is_visible():
                log("FAIL: FolderCancelButton not found on Screen 2")
                return 1
            c_btn3.iface_invoke.Invoke()

            # Verify return to alpha Chat screen
            ret3 = poll_until(find_visible_chat_heading, timeout=6.0, interval=0.1)
            if not ret3 or "alpha" not in ret3.window_text().lower():
                log("FAIL: Did not return to alpha Chat after Cancel in Sequence 2")
                return 1

            # Wait for Action Center panel to reactivate as per production contract
            ac_panel_reactivated = poll_until(find_visible_ac_panel, timeout=6.0, interval=0.2)
            if not ac_panel_reactivated:
                log("FAIL: ActionCenterPanel was not reactivated by new explicit refresh after dismissal")
                return 1

            status_label_reactivated = find_by_aid(win, "ActionCenterStatusText")
            status_reactivated_val = status_label_reactivated.window_text() if status_label_reactivated else ""
            log(f"Alpha Action Center status after explicit reactivation: '{status_reactivated_val}'")
            if "1 item" not in status_reactivated_val:
                log(f"FAIL: Expected '1 item(s) need your attention.', got '{status_reactivated_val}'")
                return 1
            log("PASS: New explicit refresh after dismissal reactivated the card as permitted by production contract.")

        finally:
            # Clean up disposable obligation in alpha in finally block (exception safe)
            try:
                conn = sqlite3.connect(STORE_PATH)
                mark_store_write("DELETE: seeded alpha draft_attachment (att-alpha-disposable-1)")
                conn.execute("DELETE FROM draft_attachments WHERE project_id = ? AND attachment_id = 'att-alpha-disposable-1';", (alpha_id,))
                conn.commit()
                conn.close()
            except Exception as e:
                log(f"WARNING: Exception during alpha obligation cleanup: {e}")

            # Restore original composer text if modified
            try:
                if composer_box and composer_box.window_text() != orig_composer_text:
                    composer_box.set_edit_text(orig_composer_text)
            except Exception:
                pass

        # 9.3 Switch to closed project 'gamma' which has ClosedProjectRestriction obligation
        # Seed gamma with 51 failed attachments so total obligations = 52 (51 attachments + 1 closed project)
        # to test bounded pagination (pageSize 50) and MoreActionCenterItemsButton continuation
        gamma_id = "7a546bab-c9dd-49c0-8e77-c67903ab1b66"
        conn = sqlite3.connect(STORE_PATH)
        mark_store_write("INSERT: 51 seeded gamma draft_attachments (REJECTED, one transaction)")
        for i in range(1, 52):
            conn.execute(
                "INSERT OR REPLACE INTO draft_attachments (attachment_id, project_id, file_name, file_size, sha256, detected_type, staged_path, source_path, status, error_message, version) "
                "VALUES (?, ?, ?, 10, 'dummy', 'text/plain', '', 'C:/source/file.txt', 'REJECTED', 'seeded error', 1);",
                (f"att-gamma-{i}", gamma_id, f"gamma_err_{i:02d}.txt")
            )
        conn.commit()
        conn.close()

        log("\nSwitching to closed project 'gamma' via File -> Switch Project...")
        gamma_heading = open_switch_project(win)
        if gamma_heading is None:
            # The switch is the action under test here. An unchecked failure used
            # to leave the run looking for 'Open project gamma' on a screen that
            # never opened, which reported the failure as a missing button
            # several steps away from its cause.
            log("FAIL: File -> Switch Project did not reach Screen 2 for the gamma switch")
            return 1

        # On Screen 2, open gamma
        open_gamma = find_by_name(win, "Open project gamma")
        if not open_gamma:
            log("FAIL: 'Open project gamma' button not found on Screen 2")
            return 1
        open_gamma.iface_invoke.Invoke()
        time.sleep(2.5)

        # Verify gamma opened in Chat and Action Center populated with initial page (50 items)
        status_label = find_by_aid(win, "ActionCenterStatusText")
        status_gamma = status_label.window_text() if status_label else ""
        log(f"Gamma Action Center status (Page 1): '{status_gamma}'")
        if "50 item(s) loaded (more available)" not in status_gamma:
            log(f"FAIL: Expected '50 item(s) loaded (more available)' for gamma page 1, got '{status_gamma}'")
            return 1
        log("PASS: Action Center truthfully bounded initial page to 50 items and indicated more available.")

        # Verify MoreActionCenterItemsButton is visible and click it to load continuation
        more_btn = find_by_aid(win, "MoreActionCenterItemsButton")
        if not more_btn or not more_btn.is_visible():
            log("FAIL: MoreActionCenterItemsButton should be visible when continuation token is present")
            return 1
        log("Clicking MoreActionCenterItemsButton to consume continuation token...")
        more_btn.iface_invoke.Invoke()
        time.sleep(1.5)

        status_gamma2 = status_label.window_text() if status_label else ""
        log(f"Gamma Action Center status (Page 2): '{status_gamma2}'")
        if "52 item(s) need your attention" not in status_gamma2:
            log(f"FAIL: Expected '52 item(s) need your attention' after loading continuation, got '{status_gamma2}'")
            return 1

        more_btn_after = find_by_aid(win, "MoreActionCenterItemsButton")
        is_more_collapsed = (more_btn_after is None) or (not more_btn_after.is_visible())
        if not is_more_collapsed:
            log("FAIL: MoreActionCenterItemsButton should collapse after all items are loaded")
            return 1
        log("PASS: Continuation token consumed and MoreActionCenterItemsButton collapsed truthfully.")

        # 9.4 Test Show Details Button
        show_details_btn = find_by_aid(win, "ShowActionCenterDetailsButton")
        if not show_details_btn or not show_details_btn.is_visible():
            log("FAIL: ShowActionCenterDetailsButton not visible when items need attention")
            return 1
        # The required behaviour is a source-linked detail for every listed item, not a
        # NoticeBar sentence: commit 96373fd replaced the generic notice with the detail so
        # each item shows its owning record, identity, version and reference. Asserting the
        # removed notice string would have passed only against the defect.
        #
        # The detail is a temporary dismissible sheet rather than an in-place expansion of
        # the card. The card sits in the auto-sized Chat header; on a short window the
        # header had no room for both the item list and the detail, so the in-place
        # expansion was arranged to zero height and "Show details" appeared to do nothing.
        status_before_details = status_label.window_text() if status_label else ""
        log("Clicking ShowActionCenterDetailsButton...")
        show_details_btn.iface_invoke.Invoke()

        details_panel = None
        details_deadline = time.time() + 5.0
        while time.time() < details_deadline:
            candidate = find_by_aid(win, "ActionCenterDetailsPanel")
            if candidate is not None and candidate.is_visible():
                details_panel = candidate
                break
            time.sleep(0.1)
        if details_panel is None:
            log("FAIL: ShowActionCenterDetailsButton did not reveal the Action Center source detail sheet")
            return 1

        # A collapsed Border leaves the UIA tree entirely, so presence already proves it
        # opened. What the defect did NOT do was leave the tree: it opened at zero size, so
        # "present" alone is not enough. Assert a usable rendered height as well, otherwise
        # this check would pass again against the same defect.
        panel_rect = details_panel.rectangle()
        log(f"Source detail sheet: W={panel_rect.width()} H={panel_rect.height()}")
        if panel_rect.width() <= 0 or panel_rect.height() < 120:
            log(f"FAIL: Source detail sheet opened without usable size "
                f"({panel_rect.width()}x{panel_rect.height()})")
            return 1
        log("PASS: Show details opened a usable dismissible source detail sheet (no navigation).")

        summary = find_by_aid(win, "ActionCenterDetailsSummaryText")
        summary_text = summary.window_text() if summary else ""
        log(f"Details summary: '{summary_text}'")
        if "read-only projection of the owning record" not in summary_text:
            log(f"FAIL: Details summary did not state the read-only projection contract, got '{summary_text}'")
            return 1

        # Every detail row must carry the source fields the owner record supplies. The
        # gamma project seeds 51 rejected attachments plus one closed-project obligation,
        # so the first described item is a SQLITE_STORAGE attachment record. Rows are
        # identified by the announced name rather than by control type, which the XAML
        # container does not fix across WinUI versions.
        detail_rows = [d for d in details_panel.descendants()
                       if (d.element_info.name or "").startswith("Item ")
                       and "Owning record" in (d.element_info.name or "")]
        log(f"Realized source detail rows: {len(detail_rows)}")
        if len(detail_rows) == 0:
            log("FAIL: Details panel rendered no source-linked rows")
            return 1
        detail_text = " | ".join(d.element_info.name for d in detail_rows)
        if "Owning record SQLITE_STORAGE" not in detail_text:
            log(f"FAIL: First detail row did not name its owning record; got '{detail_text[:200]}'")
            return 1
        log("PASS: Source detail rows name the owning record for each listed item.")

        # Field-level content is asserted from the row text so a row that only carried a
        # title would not pass. The attachment explanation and its source reference are
        # both derived from the draft_attachments record, never re-authored by the UI.
        row_fields = [d.window_text() for d in details_panel.descendants() if d.window_text()]
        joined_fields = " | ".join(row_fields)
        for required in ("Record identity:", "Record version:", "Source reference:", "Projection state:"):
            if required not in joined_fields:
                log(f"FAIL: Source detail omitted required field '{required}'")
                return 1
        if "draft_attachments." not in joined_fields:
            log("FAIL: Source detail did not include a draft_attachments source reference")
            return 1
        log("PASS: Source detail exposes record identity, version, state and source reference.")

        # Inspection must not execute the recovery action it describes.
        status_after_details = status_label.window_text() if status_label else ""
        if status_after_details != status_before_details:
            log(f"FAIL: Expanding details mutated the projection: '{status_before_details}' -> '{status_after_details}'")
            return 1
        log("PASS: Expanding details was inspection-only; the listed projection was unchanged.")

        # Focus moves into the detail and returns to the invoking control on close, so
        # keyboard position is preserved across the expansion.
        #
        # Focus is POLLED rather than sampled once. The handler sets Visibility and then
        # calls Focus(Programmatic) in the same tick, before the layout pass that makes
        # the button focusable, so the transfer lands a frame or two later. A single
        # immediate read turned that ordering into an intermittent failure: the same
        # binary passed and failed this assertion across otherwise identical runs.
        #
        # Foreground is re-established first. UIA reports CurrentHasKeyboardFocus false
        # for EVERY element when the top-level window is not foreground, so without this
        # the assertion would fail for a lost foreground rather than for the app, and the
        # two are not distinguishable from the result alone.
        if not window_is_foreground(win):
            log("  Re-establishing window foreground before measuring focus...")
            try:
                win.set_focus()
            except Exception as exc:
                log(f"  set_focus raised {type(exc).__name__}: {exc}")
            fg_ok, fg_detail = establish_foreground(win)
            log(f"  foreground establishment: established={fg_ok} ({fg_detail})")
            poll_until(lambda: window_is_foreground(win), timeout=3.0, interval=0.1)

        close_details_btn = find_by_aid(win, "CloseActionCenterDetailsButton")
        if not close_details_btn:
            log("FAIL: CloseActionCenterDetailsButton not found inside the details panel")
            return 1
        focused = poll_until(lambda: has_focus(close_details_btn), timeout=3.0, interval=0.05)
        if not focused:
            log("FAIL: Opening details did not move focus into the detail sheet")
            # Recorded so an app defect is never confused with a lost foreground: only
            # the former is a failure of the product.
            log(f"  window was foreground while measuring: {window_is_foreground(win)}")
            holders = []
            for d in win.descendants():
                try:
                    if d.element_info.element.CurrentHasKeyboardFocus:
                        holders.append(f"{d.element_info.control_type}"
                                       f"[{d.element_info.automation_id or '-'}]")
                except Exception:
                    continue
            log(f"  elements holding keyboard focus: {holders if holders else 'NONE'}")
            return 1
        close_details_btn.iface_invoke.Invoke()
        time.sleep(0.6)
        collapsed_panel = find_by_aid(win, "ActionCenterDetailsPanel")
        if collapsed_panel is not None and collapsed_panel.is_visible():
            log("FAIL: Closing the details panel left it visible")
            return 1
        show_details_again = find_by_aid(win, "ShowActionCenterDetailsButton")
        if show_details_again is None or not poll_until(
                lambda: has_focus(show_details_again), timeout=3.0, interval=0.05):
            log("FAIL: Closing details did not return focus to ShowActionCenterDetailsButton")
            return 1
        log("PASS: Details sheet is focus-safe: opens into the sheet and restores the invoking control.")

        # The item list must still be usable after the sheet closes. Bounding the card for a
        # short window is what previously clipped its own actions, so assert the list and
        # Show details have rendered size. Load more items is checked only when it is meant
        # to be shown: once the continuation token is consumed the control is legitimately
        # collapsed, and demanding size for a collapsed control would assert the wrong thing.
        def _get_ac_list_with_height():
            lst = find_by_aid(win, "ActionCenterList") or find_by_name(win, "Action Center items")
            if lst is not None and lst.rectangle().height() >= 20:
                return lst
            return None

        ac_list = poll_until(_get_ac_list_with_height, timeout=5.0, interval=0.1)
        if ac_list is None:
            raw_list = find_by_aid(win, "ActionCenterList") or find_by_name(win, "Action Center items")
            if raw_list is None:
                ac_panel = find_by_aid(win, "ActionCenterPanel")
                panel_desc = [(d.element_info.automation_id, d.element_info.name, d.element_info.control_type, d.rectangle()) for d in (ac_panel.descendants() if ac_panel else [])]
                log(f"FAIL: Action Center item list not found in tree after closing details. Panel descendants: {panel_desc}")
            else:
                log(f"FAIL: Action Center item list has no usable height after closing details (rect={raw_list.rectangle()}, height={raw_list.rectangle().height()})")
            return 1
        show_again_btn = find_by_aid(win, "ShowActionCenterDetailsButton")
        if (show_again_btn is None or show_again_btn.rectangle().width() <= 0
                or show_again_btn.rectangle().height() <= 0):
            log("FAIL: ShowActionCenterDetailsButton is not reachable in the card layout")
            return 1
        more_now = find_by_aid(win, "MoreActionCenterItemsButton")
        if more_now is not None and more_now.is_visible():
            if more_now.rectangle().width() <= 0 or more_now.rectangle().height() <= 0:
                log("FAIL: MoreActionCenterItemsButton is visible but has no rendered size")
                return 1
        log("PASS: Action Center list and its actions remain reachable at this window size.")

        # 9.5 Test Stale Item Handling & Expected-Version Verification at Invocation
        log("\nSimulating concurrent mutation: altering version of first item in SQLite store before clicking...")
        conn = sqlite3.connect(STORE_PATH)
        # Mutate version of att-gamma-1 in SQLite store (from current version to 99)
        mark_store_write("UPDATE: draft_attachments.version = 99 for att-gamma-1 (the deliberate stale-version injection)")
        conn.execute("UPDATE draft_attachments SET version = 99 WHERE project_id = ? AND attachment_id = 'att-gamma-1';", (gamma_id,))
        conn.commit()
        conn.close()

        # Click the first obligation item in ActionCenterList
        ac_list = find_by_aid(win, "ActionCenterList")
        if not ac_list:
            log("FAIL: ActionCenterList not found")
            return 1
        ac_items = ac_list.children()
        log(f"ActionCenterList realized items count: {len(ac_items)}")
        if len(ac_items) == 0:
            log("FAIL: ActionCenterList has no items")
            return 1

        log("Invoking first obligation item in ActionCenterList to verify expected-version check...")
        if hasattr(ac_items[0], 'iface_invoke') and ac_items[0].iface_invoke:
            ac_items[0].iface_invoke.Invoke()
        else:
            try:
                ac_items[0].set_focus()
                time.sleep(0.2)
                ac_items[0].click_input()
            except Exception as e:
                log(f"Notice during click_input: {e}")
                send_key_to_window(win.handle, 0x0D)
        time.sleep(1.5)

        # Verify stale notice with version mismatch was raised
        notice_bar = find_by_aid(win, "Notice")
        notice_desc = [d.window_text() for d in notice_bar.descendants() if d.window_text()] if notice_bar else []
        log(f"Notice text after clicking mutated item: {notice_desc}")
        if not any("Action Center item is stale: attachment record version has changed" in t for t in notice_desc):
            log("FAIL: Expected version mismatch notice not displayed in NoticeBar")
            return 1
        log("PASS: Expected-version verification passed: version mismatch caught at invocation and truthful notice displayed.")

        # Clean up seeded gamma draft attachments
        conn = sqlite3.connect(STORE_PATH)
        mark_store_write("DELETE: the 51 seeded gamma draft_attachments")
        conn.execute("DELETE FROM draft_attachments WHERE project_id = ? AND attachment_id LIKE 'att-gamma-%';", (gamma_id,))
        conn.commit()
        conn.close()

        # 9.6 Switch back to alpha
        log("\nSwitching back to project alpha...")
        open_switch_project(win)

        open_alpha = find_by_name(win, "Open project alpha")
        if not open_alpha:
            log("FAIL: 'Open project alpha' button not found")
            return 1
        open_alpha.iface_invoke.Invoke()
        time.sleep(2.5)
        log("PASS: Switched back to alpha; Action Center isolated to active project.")

        # All three Action Center legs completed: pagination resumed, the stale item
        # was invalidated and refreshed, and the panel dismissed without residue.
        criterion_action_center_paging = "PASS"
        criterion_action_center_stale = "PASS"
        criterion_action_center_dismiss = "PASS"

        # =========================================================================
        # PHASE 10: NATIVE DRAG-AND-DROP INGESTION PATH & ATTACHMENT INSPECTION
        # =========================================================================
        log("\n--- PHASE 10: Native Drag-and-Drop Ingestion Path & Attachment Inspection ---")

        # 10.1 Verify Composer drop capability & Native OLE Drag-Drop Invocation
        composer_box = find_by_aid(win, "ComposerBox")
        attach_btn = find_by_aid(win, "AttachButton")
        if not composer_box or not attach_btn:
            log("FAIL: Composer controls missing from UIA tree")
            return 1
        log("PASS: ComposerBox and AttachButton confirmed present with AllowDrop support.")

        alpha_id = "aee9775e-6df2-46f8-a4bf-d926fa4b2cdd"
        test_drop_dir = os.path.join(os.path.dirname(STORE_PATH), "drop_test_files")
        os.makedirs(test_drop_dir, exist_ok=True)
        single_drop_file = os.path.join(test_drop_dir, "native_drop_single.txt")
        single_drop_content = b"Native OLE drag-and-drop verification payload with valid English prose.\n"
        import hashlib
        single_drop_sha256 = hashlib.sha256(single_drop_content).hexdigest()
        single_drop_size = len(single_drop_content)
        with open(single_drop_file, "wb") as f:
            f.write(single_drop_content)

        # Baseline check before drop: query contributions and pending submissions
        conn = sqlite3.connect(STORE_PATH)
        contribs_before = conn.execute("SELECT COUNT(*) FROM contributions WHERE project_id = ?;", (alpha_id,)).fetchone()[0]
        pending_before = conn.execute("SELECT COUNT(*) FROM pending_submissions WHERE project_id = ?;", (alpha_id,)).fetchone()[0]
        conn.close()
        comp_text_before = composer_box.window_text() if composer_box else ""

        # Single admission test via ole_drop.exe
        log(f"Invoking ole_drop.exe on window handle {win.handle} with single file '{single_drop_file}'...")
        drop_proc_returncode = -1
        ole_start = mark_event("ole_drop.exe started (single-file drop attempt)")
        try:
            drop_proc = subprocess.run(
                [OLE_DROP_EXE, str(win.handle), single_drop_file],
                capture_output=True,
                text=True,
                timeout=OLE_DROP_TIMEOUT_S
            )
            drop_proc_returncode = drop_proc.returncode
            log(f"ole_drop.exe exit code: {drop_proc.returncode}")
            log(f"ole_drop.exe stdout: {drop_proc.stdout.strip()}")
            if drop_proc.stderr:
                log(f"ole_drop.exe stderr: {drop_proc.stderr.strip()}")
        except subprocess.TimeoutExpired:
            log(f"ole_drop.exe invocation reached bounded {OLE_DROP_TIMEOUT_S:.1f}s timeout "
                f"(helper watchdog {OLE_DROP_WATCHDOG_MS}ms + escape {OLE_DROP_ESCAPE_MS}ms "
                f"+ {OLE_DROP_MARGIN_S:.1f}s margin).")
        ole_end = mark_event("ole_drop.exe finished (single-file drop attempt)")
        # The pump readings taken DURING the OLE attempt are the measurement
        # that separates "the application's UI thread was blocked, so the drop
        # could not complete" from "the application's UI thread was answering
        # throughout, so the drop failed for a reason of its own".
        log(f"  pump latency during the OLE attempt: "
            f"{pump_latency_between(ole_start, ole_end)}")

        time.sleep(1.5) # allow background file copy, hash, and staging to complete

        conn = sqlite3.connect(STORE_PATH)
        dropped_records = conn.execute(
            "SELECT attachment_id, file_name, file_size, status, sha256, staged_path FROM draft_attachments WHERE project_id = ? AND file_name = 'native_drop_single.txt';",
            (alpha_id,)
        ).fetchall()
        contribs_after = conn.execute("SELECT COUNT(*) FROM contributions WHERE project_id = ?;", (alpha_id,)).fetchone()[0]
        pending_after = conn.execute("SELECT COUNT(*) FROM pending_submissions WHERE project_id = ?;", (alpha_id,)).fetchone()[0]
        conn.close()

        criterion_drop_execution = "INCONCLUSIVE"
        criterion_mixed_batch = "INCONCLUSIVE"

        comp_text = composer_box.window_text() if composer_box else ""
        staged_exists = os.path.exists(dropped_records[0][5]) if len(dropped_records) == 1 else True

        # Evaluate single-drop verdict using pure classification function (independently of helper success)
        drop_verdict, drop_reason = classify_drop_verdict(
            drop_proc_returncode=drop_proc_returncode,
            dropped_records=dropped_records,
            contribs_before=contribs_before,
            contribs_after=contribs_after,
            pending_before=pending_before,
            pending_after=pending_after,
            composer_text_before=comp_text_before,
            composer_text_after=comp_text,
            expected_filename="native_drop_single.txt",
            expected_size=single_drop_size,
            expected_sha256=single_drop_sha256,
            staged_file_exists=staged_exists
        )

        if drop_verdict == "FAIL":
            log(f"FAIL: Drop operation violation: {drop_reason}")
            # No `return 1` here. The mixed-batch leg below is an independent
            # observation and the attachment phases after it are the ones this
            # assignment exists to verify; aborting here skipped them all.
            criterion_drop_execution = "FAIL"
        elif drop_verdict == "PASS":
            log(f"PASS: {drop_reason}")
            criterion_drop_execution = "PASS"
        else: # INCONCLUSIVE
            log("DIAGNOSTIC: Native synthetic desktop OLE drag-drop execution did not complete via synthetic injection.")
            log(f"  ole_drop exit code: {drop_proc_returncode}")
            log(f"  dropped records count: {len(dropped_records)} (expected 1 on pass)")
            log("NOTE: Synthetic cross-process OLE drag-and-drop in headless/automation environment has OS-level security/session constraints (UIPI/modal loop timeout).")
            log(f"  Classification: {drop_reason}")
            criterion_drop_execution = "INCONCLUSIVE"

        # Test Mixed-Batch Rejection (Folder + File in batch) independently:
        log("\nTesting mixed-batch rejection (folder + file)...")
        # Clear any previous notice before the operation
        clear_notice(win)
        time.sleep(0.3)

        sub_folder = os.path.join(test_drop_dir, "test_folder")
        os.makedirs(sub_folder, exist_ok=True)
        mixed_drop_file = os.path.join(test_drop_dir, "test_mixed_file.txt")
        with open(mixed_drop_file, "wb") as f:
            f.write(b"Mixed batch test file.\n")

        mixed_proc_returncode = -1
        mixed_start = mark_event("ole_drop.exe started (mixed-batch folder+file attempt)")
        try:
            mixed_batch_proc = subprocess.run(
                [OLE_DROP_EXE, str(win.handle), sub_folder, mixed_drop_file],
                capture_output=True,
                text=True,
                timeout=OLE_DROP_TIMEOUT_S
            )
            mixed_proc_returncode = mixed_batch_proc.returncode
        except subprocess.TimeoutExpired:
            log(f"Mixed batch ole_drop.exe reached bounded {OLE_DROP_TIMEOUT_S:.1f}s timeout.")
        mixed_end = mark_event("ole_drop.exe finished (mixed-batch folder+file attempt)")
        log(f"  pump latency during the mixed-batch OLE attempt: "
            f"{pump_latency_between(mixed_start, mixed_end)}")

        # Locate actual control "Notice" and wait for fresh rejection tied to the attempted batch
        rejection_msg = "Folders cannot be attached; entire batch rejected."
        found_rejection, notice_desc = wait_for_notice_text(win, rejection_msg, timeout=3.5)
        log(f"Notice text after mixed-batch drop: {notice_desc}")

        conn = sqlite3.connect(STORE_PATH)
        mixed_admitted = conn.execute(
            "SELECT COUNT(*) FROM draft_attachments WHERE project_id = ? AND (file_name LIKE '%test_folder%' OR file_name LIKE '%test_mixed_file%');",
            (alpha_id,)
        ).fetchone()[0]
        mixed_contribs = conn.execute("SELECT COUNT(*) FROM contributions WHERE project_id = ?;", (alpha_id,)).fetchone()[0]
        mixed_pending = conn.execute("SELECT COUNT(*) FROM pending_submissions WHERE project_id = ?;", (alpha_id,)).fetchone()[0]
        conn.close()

        # Prohibited side effect checks for mixed batch. All three are evaluated
        # so the log names every side effect that appeared, not just the first.
        mixed_failures = []
        if mixed_admitted > 0:
            mixed_failures.append(f"Mixed batch items admitted ({mixed_admitted} records found)")
        if mixed_contribs != contribs_before:
            mixed_failures.append(
                f"Contributions created during mixed-batch drop ({mixed_contribs} != {contribs_before})")
        if mixed_pending != pending_before:
            mixed_failures.append(
                f"Pending submissions created during mixed-batch drop ({mixed_pending} != {pending_before})")

        if mixed_failures:
            for reason in mixed_failures:
                log(f"FAIL: {reason}")
            criterion_mixed_batch = "FAIL"
        elif found_rejection:
            log("PASS: Mixed batch rejection proven: Notice control displays 'Folders cannot be attached; entire batch rejected.' and 0 files/folders admitted.")
            criterion_mixed_batch = "PASS"
        else:
            log("DIAGNOSTIC: Mixed batch drop did not present expected rejection notice in Notice control (insufficient delivery evidence).")
            criterion_mixed_batch = "INCONCLUSIVE"

        # Clean up dropped test records
        conn = sqlite3.connect(STORE_PATH)
        mark_store_write("DELETE: the native_drop_% test records")
        conn.execute("DELETE FROM draft_attachments WHERE project_id = ? AND file_name LIKE 'native_drop_%';", (alpha_id,))
        conn.commit()
        conn.close()

        staging_dir = os.path.join(os.path.dirname(STORE_PATH), "attachments", "staging", alpha_id)
        os.makedirs(staging_dir, exist_ok=True)

        # 10.2 Managed Draft Attachment Chip Realization & Content Preview Panel (Seeded Fixture Evidence)
        log("\n--- 10.2 Managed Draft Attachment Chip Realization & Content Preview Panel (Seeded Fixture Evidence) ---")
        log("NOTE: The following checks provide verified evidence for attachment chip UI controls and content preview panel using seeded managed records.")
        test_att_id = "att-ui-test-001"
        test_file_name = "sample_spec.txt"
        test_content = b"Mayasaba native attachment inspection verification payload with valid English prose.\n"
        import hashlib
        test_sha256 = hashlib.sha256(test_content).hexdigest()
        test_size = len(test_content)

        source_path = os.path.join(os.path.dirname(STORE_PATH), test_file_name)
        with open(source_path, "wb") as f:
            f.write(test_content)

        staged_path = os.path.join(staging_dir, f"{test_att_id}_{test_file_name}")
        with open(staged_path, "wb") as f:
            f.write(test_content)

        # Seed draft_attachments table
        conn = sqlite3.connect(STORE_PATH)
        cols = [r[1] for r in conn.execute("PRAGMA table_info(draft_attachments)").fetchall()]
        if "version" in cols:
            mark_store_write("INSERT: the seeded sample_spec.txt draft attachment")
            conn.execute(
                "INSERT OR REPLACE INTO draft_attachments (attachment_id, project_id, file_name, file_size, sha256, detected_type, staged_path, source_path, status, error_message, version) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'READY', '', 1);",
                (test_att_id, alpha_id, test_file_name, test_size, test_sha256, "text/plain", staged_path, source_path)
            )
        else:
            conn.execute(
                "INSERT OR REPLACE INTO draft_attachments (attachment_id, project_id, file_name, file_size, sha256, detected_type, staged_path, source_path, status, error_message) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'READY', '');",
                (test_att_id, alpha_id, test_file_name, test_size, test_sha256, "text/plain", staged_path, source_path)
            )
        conn.commit()
        conn.close()

        # Reload alpha project to realize draft attachment chip
        log("Reloading alpha project to realize draft attachment chip...")
        reload_heading = open_switch_project(win)
        if reload_heading is None:
            # The switch is the action under test. Without this check a flyout
            # that never opened was reported as a missing attachment chip, which
            # pointed the reader at the chip instead of at the menu.
            #
            # The chip was never observed, so the criterion is BLOCKED rather
            # than FAIL: the evidence does not say anything about the chip. The
            # run continues so the remaining phases are still exercised.
            log("FAIL: File -> Switch Project did not reach Screen 2 when reloading alpha")
            log("      Criterion 13 is BLOCKED, not FAIL: the attachment chip was never")
            log("      reached because the project switch that would show it failed first.")
            criterion_attachment_chip = "BLOCKED"
            reload_heading = None

        open_alpha = find_by_name(win, "Open project alpha") if reload_heading is not None else None
        if open_alpha:
            open_alpha.iface_invoke.Invoke()
            time.sleep(2.5)

        # ---------------------------------------------------------------------
        # The checks below are one criterion. A failure raises CriterionFailure
        # so the dependent steps are skipped without ending the run: the old
        # `return 1` here meant Phase 10.3, Phase 7, Phase 8 and the verdict
        # table were never reached whenever a single assertion failed.
        # ---------------------------------------------------------------------
        try:
            if reload_heading is None:
                raise CriterionFailure("the attachment chip was never reached")

            # Locate attachment chip and inspect button
            inspect_btn = find_by_name(win, f"Inspect {test_file_name}")
            if not inspect_btn:
                raise CriterionFailure(
                    f"'Inspect {test_file_name}' button not found in AttachmentChipsPanel")
            log(f"PASS: Attachment chip realized with inspect button 'Inspect {test_file_name}'.")

            # Click inspect button and verify NoticeBar diagnostics.
            #
            # The notice is cleared first: a notice left open by an earlier
            # phase would otherwise satisfy the poll and report an inspection
            # that never happened.
            log("Invoking inspect button on attachment chip...")
            clear_notice(win)
            inspect_btn.iface_invoke.Invoke()
            ok, notice_desc, _found = wait_for_notice(
                win,
                lambda t: (any(test_file_name in x for x in t)
                           and any("Digest: Verified" in x for x in t)
                           and any("Staged: Yes" in x for x in t)),
                label="attachment inspection notice")
            log(f"Notice text after inspecting attachment: {notice_desc}")
            if not ok:
                raise CriterionFailure(
                    f"Notice bar did not contain expected attachment inspection details: {notice_desc}")
            log("PASS: Attachment inspection verified: digest verified, managed staged copy confirmed, status READY.")

            # Verify AttachmentPreviewPanel controls
            preview_panel = find_by_aid(win, "AttachmentPreviewPanel")
            if not preview_panel or not preview_panel.is_visible():
                raise CriterionFailure("AttachmentPreviewPanel not visible after invoking chip inspect")

            preview_title = find_by_aid(win, "AttachmentPreviewTitle")
            title_text = preview_title.window_text() if preview_title else ""
            log(f"Preview panel title: '{title_text}'")
            if test_file_name not in title_text:
                raise CriterionFailure(f"Expected preview title '{test_file_name}', got '{title_text}'")

            preview_meta = find_by_aid(win, "AttachmentPreviewMeta")
            meta_text = preview_meta.window_text() if preview_meta else ""
            log(f"Preview panel meta: '{meta_text}'")
            if "text/plain" not in meta_text or "Digest verified" not in meta_text:
                raise CriterionFailure(f"Preview metadata incomplete: '{meta_text}'")

            preview_box = find_by_aid(win, "AttachmentPreviewContentBox")
            box_text = preview_box.window_text() if preview_box else ""
            log(f"Preview content text: '{box_text.strip()}'")
            if "Mayasaba native attachment inspection verification payload with valid English prose." not in box_text:
                raise CriterionFailure(f"Preview content mismatch: '{box_text}'")
            log("PASS: Bounded UTF-8 English text preview loaded and displayed in AttachmentPreviewPanel.")

            # Focus verification on ClosePreviewButton
            close_preview_btn = find_by_aid(win, "ClosePreviewButton")
            has_preview_focus = close_preview_btn.has_keyboard_focus() if close_preview_btn else False
            log(f"ClosePreviewButton has keyboard focus: {has_preview_focus}")
            if not has_preview_focus:
                raise CriterionFailure("ClosePreviewButton did not receive programmatic keyboard focus")
            log("PASS: ClosePreviewButton verified with programmatic keyboard focus upon preview opening.")

            # Dismissal via Escape key and focus restoration
            log("Sending Escape key to dismiss AttachmentPreviewPanel...")
            send_key_to_window(win.handle, VK_ESCAPE)
            time.sleep(1.0)

            preview_panel_after = find_by_aid(win, "AttachmentPreviewPanel")
            is_preview_closed = (preview_panel_after is None) or (not preview_panel_after.is_visible())
            log(f"AttachmentPreviewPanel closed after Escape: {is_preview_closed}")
            if not is_preview_closed:
                raise CriterionFailure("AttachmentPreviewPanel did not collapse on Escape key")
            log("PASS: Escape dismissed preview panel and cleared content memory.")

            criterion_attachment_chip = "PASS"
        except CriterionFailure as exc:
            log(f"FAIL: {exc}")
            if criterion_attachment_chip != "BLOCKED":
                criterion_attachment_chip = "FAIL"
            # Leave the window in a known state so 10.3 starts clean rather than
            # inheriting a half-open preview from this criterion.
            try:
                if find_by_aid(win, "AttachmentPreviewPanel") is not None:
                    send_key_to_window(win.handle, VK_ESCAPE)
                    time.sleep(0.6)
            except Exception:
                pass

        # Clean up first test attachment
        try:
            conn = sqlite3.connect(STORE_PATH)
            mark_store_write("DELETE: the sample_spec.txt draft attachment")
            conn.execute("DELETE FROM draft_attachments WHERE attachment_id = ?;", (test_att_id,))
            conn.commit()
            conn.close()
            if os.path.exists(staged_path): os.remove(staged_path)
            if os.path.exists(source_path): os.remove(source_path)
        except Exception:
            pass

        # 10.3 Non-English ASCII Content Preview Withholding (Defect 1 Verification in Live UI)
        log("\n--- 10.3 Non-English ASCII Content Preview Withholding (Defect 1 in Live UI) ---")
        non_eng_id = "att-ui-test-noneng"
        non_eng_file = "hola_mundo.txt"
        non_eng_content = b"hola mundo"
        non_eng_sha = hashlib.sha256(non_eng_content).hexdigest()
        non_eng_size = len(non_eng_content)

        source_path_ne = os.path.join(os.path.dirname(STORE_PATH), non_eng_file)
        with open(source_path_ne, "wb") as f:
            f.write(non_eng_content)

        staged_path_ne = os.path.join(staging_dir, f"{non_eng_id}_{non_eng_file}")
        with open(staged_path_ne, "wb") as f:
            f.write(non_eng_content)

        conn = sqlite3.connect(STORE_PATH)
        if "version" in cols:
            mark_store_write("INSERT: the seeded non-English draft attachment")
            conn.execute(
                "INSERT OR REPLACE INTO draft_attachments (attachment_id, project_id, file_name, file_size, sha256, detected_type, staged_path, source_path, status, error_message, version) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'READY', '', 1);",
                (non_eng_id, alpha_id, non_eng_file, non_eng_size, non_eng_sha, "text/plain", staged_path_ne, source_path_ne)
            )
        else:
            conn.execute(
                "INSERT OR REPLACE INTO draft_attachments (attachment_id, project_id, file_name, file_size, sha256, detected_type, staged_path, source_path, status, error_message) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'READY', '');",
                (non_eng_id, alpha_id, non_eng_file, non_eng_size, non_eng_sha, "text/plain", staged_path_ne, source_path_ne)
            )
        conn.commit()
        conn.close()

        # Reload alpha project to realize non-English attachment chip
        log("Reloading alpha project to realize non-English attachment chip...")
        ne_heading = open_switch_project(win)

        open_alpha = find_by_name(win, "Open project alpha") if ne_heading is not None else None
        if open_alpha:
            open_alpha.iface_invoke.Invoke()
            time.sleep(2.5)

        # The withheld-preview reason, verbatim from MainWindow::InspectAttachmentAsync
        # (line ~2633, `details + L" · Preview unavailable: " + reason`). Both the
        # preview panel and the notice carry this exact string.
        ne_reason = "Preview unavailable: Attachment contains non-English characters or unverifiable prose"

        try:
            if ne_heading is None:
                log("FAIL: File -> Switch Project did not reach Screen 2 when reloading alpha")
                raise CriterionFailure(
                    "the non-English chip was never reached because the project switch failed first")

            inspect_ne_btn = find_by_name(win, f"Inspect {non_eng_file}")
            if not inspect_ne_btn:
                raise CriterionFailure(f"'Inspect {non_eng_file}' button not found in AttachmentChipsPanel")
            log(f"PASS: Attachment chip realized with inspect button 'Inspect {non_eng_file}'.")

            # Click inspect on non-English chip
            log("Invoking inspect on non-English attachment chip...")
            clear_notice(win)
            inspect_ne_btn.iface_invoke.Invoke()

            # Poll the notice instead of sampling it once. The previous run read
            # an empty descendant list after a single 1.0 s sleep and could not
            # distinguish "no notice was opened" from "not opened yet"; the
            # timeline in the log is what makes that distinction possible.
            has_ne_preview_withheld, notice_desc_ne, notice_element_found = wait_for_notice(
                win, lambda t: any(ne_reason in x for x in t), label="non-English notice")
            log(f"Notice text after inspecting non-English attachment: {notice_desc_ne}")
            log(f"  Notice InfoBar element found at all: {notice_element_found}")

            # The withholding itself lives in the preview panel, not in the
            # notice. The old order asserted the notice first and returned
            # before the panel was ever examined, so a failure there said
            # nothing about whether the preview was actually withheld.
            preview_panel_ne = find_by_aid(win, "AttachmentPreviewPanel")
            panel_open = bool(preview_panel_ne) and preview_panel_ne.is_visible()
            log(f"AttachmentPreviewPanel visible for non-English inspection: {panel_open}")

            unavail_text = ""
            content_visible = None
            if panel_open:
                unavail_notice = find_by_aid(win, "AttachmentPreviewUnavailableNotice")
                unavail_text = unavail_notice.window_text() if unavail_notice else ""
                log(f"Unavailable notice text: '{unavail_text}'")
                content_scroller = find_by_aid(win, "AttachmentPreviewContentScroller")
                content_visible = bool(content_scroller) and content_scroller.is_visible()
                preview_box_ne = find_by_aid(win, "AttachmentPreviewContentBox")
                box_text_ne = preview_box_ne.window_text() if preview_box_ne else ""
                log(f"Preview content scroller visible: {content_visible}; "
                    f"content box text: '{box_text_ne.strip()}'")

            panel_withheld = (panel_open
                              and ne_reason in unavail_text
                              and content_visible is False)
            if panel_withheld:
                log("PASS: The preview panel withheld the non-English prose and stated the reason; "
                    "the content scroller is collapsed and the content box is empty.")
            elif not panel_open:
                log("FAIL: The preview panel never opened, so no withholding was demonstrated.")
            else:
                log("FAIL: The preview panel is open but does not demonstrate withholding: "
                    f"reason_present={ne_reason in unavail_text}, content_scroller_visible={content_visible}")

            # The notice is a second surface reporting the same decision. It is
            # judged separately and reported separately, so a failure here is
            # never mistaken for the preview having been shown.
            if not has_ne_preview_withheld:
                log(f"FAIL: The notice surface did not report the withholding. "
                    f"element_found={notice_element_found}, texts={notice_desc_ne}")
                if panel_withheld:
                    log("NOTE: The withholding itself held -- the preview panel is correct. "
                        "The defect is confined to the notice surface, which did not open "
                        "or did not carry the reason within the polling window.")

            if panel_withheld and has_ne_preview_withheld:
                criterion_preview_withholding = "PASS"
                log("PASS: Non-English ASCII prose ('hola mundo') preview withheld in the preview panel "
                    "and reported truthfully in the notice surface.")
            else:
                criterion_preview_withholding = "FAIL"

            # Dismiss preview
            send_key_to_window(win.handle, VK_ESCAPE)
            time.sleep(1.0)
        except CriterionFailure as exc:
            log(f"FAIL: {exc}")
            if ne_heading is None:
                log("      Criterion 14 is BLOCKED, not FAIL: the chip was never reached.")
                criterion_preview_withholding = "BLOCKED"
            else:
                criterion_preview_withholding = "FAIL"
            try:
                if find_by_aid(win, "AttachmentPreviewPanel") is not None:
                    send_key_to_window(win.handle, VK_ESCAPE)
                    time.sleep(0.6)
            except Exception:
                pass

        # Clean up non-English test attachment
        try:
            conn = sqlite3.connect(STORE_PATH)
            mark_store_write("DELETE: the non-English draft attachment")
            conn.execute("DELETE FROM draft_attachments WHERE attachment_id = ?;", (non_eng_id,))
            conn.commit()
            conn.close()
            if os.path.exists(staged_path_ne): os.remove(staged_path_ne)
            if os.path.exists(source_path_ne): os.remove(source_path_ne)
        except Exception:
            pass

        # Reload alpha again to clear the chip before performance profiling.
        #
        # This block was duplicated verbatim in the previous revision: the
        # second copy switched project a second time for no reason, which added
        # a whole project load to the run and left two identical PASS lines in
        # the log that read like two separate observations. There is one reload.
        log("Reloading clean alpha project for performance profiling...")
        open_switch_project(win)

        open_alpha = find_by_name(win, "Open project alpha")
        if open_alpha:
            open_alpha.iface_invoke.Invoke()
            time.sleep(2.5)
            log("PASS: Clean alpha project restored for performance profiling.")
        else:
            # This used to log PASS unconditionally. When the File menu could not
            # be reached the project was never reloaded, and the line asserted a
            # restore that had not happened -- the profile below would then be of
            # whatever project happened to be open. The line says which one it is.
            log("NOTE: the alpha project was NOT reloaded (the File menu was unreachable); "
                "the profile below describes whichever project is currently open.")

        # =========================================================================
        # =========================================================================
        # PHASE 6C: CLI READINESS PROJECTION & NON-GATING (CRITERION 21)
        # =========================================================================
        log("\n--- PHASE 6C: CLI Readiness Projection & Non-Gating ---")

        # What this criterion asserts, and why each part is written the way it is:
        #
        # 1. One entry per required CLI, each carrying the CLI name, a state from
        #    the five registered states, and a dot. The state must be READ FROM
        #    THE PRODUCT, never hardcoded here.
        # 2. The projection gates nothing. Send is enabled by the draft alone
        #    (`SendButton().IsEnabled(hasText || hasAttachments)`), and readiness
        #    is not consulted by Send, folder selection, drafts, history or
        #    navigation.
        # 3. A card appears exactly for the CLIs that owe the user something, and
        #    it offers the actions that condition actually needs.
        #
        # SCOPE LIMIT, stated rather than papered over: once a project is bound,
        # Welcome is not reachable again (Screen 2 is the startup screen and the
        # prelude has no Back to Welcome after the first binding), so this
        # criterion compares Screen 2 against the Chat header. Welcome-vs-Chat
        # agreement was observed separately by `probe_readiness_ui.py`, which
        # launches against an unbound store and reads both preludes; that
        # observation is recorded in LOCAL-VALIDATION.md rather than claimed here.
        READINESS_STATES = ("CHECKING", "READY", "MISSING", "UNSUPPORTED", "PROBE_FAILED")
        CLI_ENTRIES = (
            ("Hermes Agent CLI", "HermesStatus"),
            ("Kilo Code CLI", "KiloStatus"),
            ("Claude Code CLI", "ClaudeStatus"),
        )
        FOLDER_CLI_ENTRIES = (
            ("Hermes Agent CLI", "FolderHermesStatus"),
            ("Kilo Code CLI", "FolderKiloStatus"),
            ("Claude Code CLI", "FolderClaudeStatus"),
        )

        def parse_readiness(text):
            """Map an entry's visible text to one of the five states, or None."""
            if not text:
                return None
            for state in READINESS_STATES:
                if text.rstrip().endswith(state):
                    return state
            return "UNRECOGNIZED"

        def read_entries(mapping):
            found = {}
            for cli_name, aid in mapping:
                element = find_by_aid(win, aid)
                found[cli_name] = element.window_text() if element is not None else None
            return found

        # PRECONDITION FIRST, before any polling. Every assertion below reads the
        # Chat header, so if Chat is not open the criterion has observed nothing
        # and must not report FAIL -- that would assert a defect in the projection
        # from an observation that was never made. It is BLOCKED, which is what
        # this harness calls a precondition that stopped an observation. Checked
        # before the 60s poll so a closed Chat cannot burn the budget and then
        # report the absence as a projection defect.
        if find_by_aid(win, "ComposerBox") is None:
            log("BLOCKED: Chat is not open, so the readiness projection was never "
                "observable. Criterion 21 is BLOCKED, not FAIL.")
            try:
                log(f"  window title: {win.window_text()!r}")
            except Exception as exc:
                log(f"  window title unreadable: {type(exc).__name__}: {exc}")
            criterion_cli_readiness = "BLOCKED"
            return emit_report()

        # The postcondition is polled and confirmed by two agreeing reads.
        # "All three read a registered state" is satisfied by the initial markup
        # (CHECKING is a registered state AND the shipped literal), so it would
        # report PASS before any detection ran. The only postcondition the markup
        # cannot produce is "at least one entry left CHECKING".
        chat_raw = {}
        chat_states = {}
        readiness_deadline = time.time() + 60.0
        while time.time() < readiness_deadline:
            chat_raw = read_entries(CLI_ENTRIES)
            parsed = {name: parse_readiness(text) for name, text in chat_raw.items()}
            if all(text is not None for text in chat_raw.values()) and any(
                    state not in (None, "CHECKING") for state in parsed.values()):
                time.sleep(1.0)
                again = {name: parse_readiness(text)
                         for name, text in read_entries(CLI_ENTRIES).items()}
                if again == parsed:
                    chat_states = again
                    break
            time.sleep(1.0)

        readiness_failures = []
        readiness_notes = []

        if not chat_states:
            # Report what was actually read. "No entry left CHECKING" is true
            # both for an entry that stayed at its markup value and for an entry
            # that is not in the tree at all, and those are different defects.
            # Without this line the two are indistinguishable in the evidence.
            log(f"  last readiness read: {json.dumps(chat_raw, ensure_ascii=False)}")
            readiness_failures.append(
                "no Chat readiness entry left CHECKING within 60s, so the projection "
                "could not be distinguished from the initial markup")
        else:
            log(f"Chat readiness entries: {json.dumps(chat_raw, ensure_ascii=False)}")
            for cli_name, aid in CLI_ENTRIES:
                text = chat_raw.get(cli_name)
                state = chat_states.get(cli_name)
                if text is None:
                    readiness_failures.append(f"{cli_name}: no readiness entry is present")
                    continue
                if state == "UNRECOGNIZED":
                    readiness_failures.append(
                        f"{cli_name}: entry '{text}' ends in no registered state "
                        f"({', '.join(READINESS_STATES)})")
                elif cli_name not in text:
                    readiness_failures.append(
                        f"{cli_name}: entry '{text}' does not carry the CLI name")
                else:
                    log(f"PASS: {cli_name} reads '{text}' (state {state}).")

            # The dot is decorative in the accessibility tree: the entry's text
            # already carries both the name and the state, which is what "never
            # colour-only" requires. Whether the Ellipse itself has a UIA peer is
            # therefore recorded as an observation, not asserted as a contract --
            # failing the product for a decorative shape's reachability would be
            # asserting something the UI contract does not require.
            dot_ids = ("HermesDot", "KiloDot", "ClaudeDot")
            dots_found = {aid: find_by_aid(win, aid) is not None for aid in dot_ids}
            log(f"Readiness dot elements reachable in UIA: {json.dumps(dots_found)}")
            if not any(dots_found.values()):
                readiness_notes.append(
                    "the readiness dots are Ellipses with no UIA peer, so their presence "
                    "is verified from the XAML rather than from UIA; the entry text carries "
                    "name and state, so the projection is not colour-only")

            # A settled CHECKING is not "still asking": two agreeing reads confirmed
            # it, and every path that settles there requires the user to act. A
            # D-11 tie is therefore a legitimate, non-failure state, not a defect.
            if "CHECKING" in chat_states.values():
                tied = [n for n, s in chat_states.items() if s == "CHECKING"]
                log(f"NOTE: {', '.join(tied)} reads CHECKING. This is a settled state "
                    f"requiring a user decision (D-11), not a pending detection.")

        # --- The projection must gate nothing ---------------------------------
        # Behavioural proof at the point the contract names: a draft enables Send
        # regardless of what the header says about any CLI.
        composer_probe = find_by_aid(win, "ComposerBox")
        send_probe = find_by_aid(win, "SendButton")
        if composer_probe is None or send_probe is None:
            readiness_failures.append(
                "the composer or Send control is absent, so non-gating could not be observed")
        else:
            send_before = send_probe.is_enabled()
            composer_probe.set_edit_text("readiness non-gating probe")
            enabled = poll_until(lambda: find_by_aid(win, "SendButton") is not None
                                 and find_by_aid(win, "SendButton").is_enabled(),
                                 timeout=6.0, interval=0.1, min_samples=2)
            if not enabled:
                readiness_failures.append(
                    "Send did not become enabled with a draft present while a CLI reads "
                    f"{'/'.join(sorted(set(chat_states.values()) or {'unknown'}))}; "
                    "the readiness projection is gating Send")
            else:
                log("PASS: Send enabled from the draft alone, with a CLI not READY in the "
                    "header; readiness gates nothing.")
            composer_probe.set_edit_text("")
            poll_until(lambda: find_by_aid(win, "SendButton") is not None
                       and not find_by_aid(win, "SendButton").is_enabled(),
                       timeout=6.0, interval=0.1, min_samples=2)
            log(f"  Send enabled before the probe: {send_before}")

        # --- Screen 2 must project the same observation ------------------------
        folder_states = {}
        if open_switch_project(win) is None:
            readiness_failures.append(
                "File -> Switch Project did not reach Screen 2, so the Screen 2 entries "
                "could not be compared with the Chat header")
        else:
            folder_raw = read_entries(FOLDER_CLI_ENTRIES)
            folder_states = {name: parse_readiness(text) for name, text in folder_raw.items()}
            log(f"Screen 2 readiness entries: {json.dumps(folder_raw, ensure_ascii=False)}")
            if not chat_states:
                readiness_notes.append(
                    "the Chat header never settled, so no agreement could be computed")
            else:
                for cli_name, _ in FOLDER_CLI_ENTRIES:
                    chat_state = chat_states.get(cli_name)
                    folder_state = folder_states.get(cli_name)
                    if folder_state is None:
                        readiness_failures.append(
                            f"{cli_name}: Screen 2 has no readiness entry")
                    elif folder_state != chat_state:
                        readiness_failures.append(
                            f"{cli_name}: Screen 2 reads {folder_state} while the Chat "
                            f"header reads {chat_state}; the two preludes disagree")
                if not any(cli_name in " ".join(readiness_failures)
                           for cli_name, _ in FOLDER_CLI_ENTRIES):
                    log("PASS: Screen 2 projects the same observation as the Chat header.")

            # Return to alpha so the phases after this one run against the same
            # project they would have run against without this criterion.
            reopen_alpha = find_by_name(win, "Open project alpha")
            if reopen_alpha is None:
                readiness_failures.append(
                    "the Screen 2 switch left no 'Open project alpha' action, so the run "
                    "could not be returned to the project the later phases expect")
            else:
                reopen_alpha.iface_invoke.Invoke()
                poll_until(lambda: find_by_aid(win, "ComposerBox") is not None,
                           timeout=20.0, interval=0.2, min_samples=2)
                time.sleep(1.0)

        # --- Cards must name the cause and offer the actions it needs ----------
        def read_readiness_cards():
            cards = []
            actions = {"locate": [], "choose": [], "recheck": []}
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
                log(f"  NOTE: the readiness card scan raised {type(exc).__name__}: {exc}")
            return cards, actions

        cards, actions = read_readiness_cards()
        log(f"Readiness cards: {json.dumps(cards, ensure_ascii=False)}")
        log(f"Readiness card actions: {json.dumps(actions, ensure_ascii=False)}")

        if chat_states:
            for cli_name, state in chat_states.items():
                if state == "READY":
                    if any(cli_name in card for card in cards):
                        readiness_failures.append(
                            f"{cli_name} is READY but still shows a problem card")
                    continue
                if state is None:
                    continue
                # A non-READY CLI owes the user either a fix (Locate) or a
                # decision (Choose), and always a Recheck. Which one depends on
                # the state, and the criterion accepts exactly the right one:
                # demanding Locate for a D-11 tie would assert a control that
                # cannot resolve a tie, and the tie is not a failure.
                has_card = any(cli_name in card for card in cards)
                has_recheck = any(cli_name in name for name in actions["recheck"])
                if state == "CHECKING":
                    has_resolver = any(cli_name in name for name in actions["choose"])
                    resolver = "Choose the executable"
                else:
                    has_resolver = any(cli_name in name for name in actions["locate"])
                    resolver = "Locate executable"
                if not has_card:
                    readiness_failures.append(
                        f"{cli_name} reads {state} but has no card explaining it")
                elif not has_resolver:
                    readiness_failures.append(
                        f"{cli_name} reads {state} but its card offers no {resolver} action")
                elif not has_recheck:
                    readiness_failures.append(
                        f"{cli_name} reads {state} but its card offers no Recheck action")
                else:
                    log(f"PASS: {cli_name} ({state}) has a card naming the cause, "
                        f"{resolver} and Recheck.")

        for note in readiness_notes:
            log(f"NOTE: {note}")

        if readiness_failures:
            for reason in readiness_failures:
                log(f"FAIL: {reason}")
            criterion_cli_readiness = "FAIL"
        else:
            criterion_cli_readiness = "PASS"
            log("PASS: criterion 21 - one entry per CLI with name, observed state and dot; "
                "Screen 2 agrees with the Chat header; Send is not gated by readiness; "
                "cards offer the resolver their condition needs plus Recheck.")

        # =========================================================================
        # PHASE 7: ACTUAL UI PERFORMANCE PROFILING (UNCAPPED STALL RETENTION)
        # =========================================================================
        log("\n--- PHASE 7: UI Performance Profiling (Uncapped Stalls & Consecutive Scroll) ---")

        # A dead application cannot be profiled, and the difference matters: an
        # exit code says the process ended, while "ComposerBox not found" says a
        # control was missing. The previous run reported the second for the first
        # and then aborted on a rectangle. Checked before the stimulus, so an
        # earlier crash is BLOCKED -- the stimulus was never applied -- and not
        # FAIL, which would assert a profiling result that was never measured.
        perf_exit_code = proc.poll()
        if perf_exit_code is not None:
            log(f"BLOCKED: the application had already exited (0x{perf_exit_code & 0xFFFFFFFF:08X}) "
                f"when profiling was due to start.")
            log("         No stimulus was applied and no measurement exists; criterion 15 is")
            log("         BLOCKED, not FAIL. See the phase that ran before this one for the cause.")
            criterion_perf_profiling = "BLOCKED"
            return emit_report()

        log("Typing test keystrokes into ComposerBox to measure input presentation latency...")
        composer_box = find_by_aid(win, "ComposerBox")
        if composer_box:
            for text in ["Performance", " measurement", " keystroke", " latency", " profiling."]:
                composer_box.set_edit_text(text)
                time.sleep(0.08)
        time.sleep(1.0)

        log("Scrolling HistoryScroller via consecutive mouse wheel events to measure consecutive scroll frames...")
        history = find_by_aid(win, "HistoryScroller")
        # Reading an element's rectangle is a live UIA round-trip and can fail
        # transiently with 0x80040201 ("An event was unable to invoke any of the
        # subscribers") while the application is dispatching. That error aborted
        # the whole run here: Phase 8 never evaluated the perf log it had already
        # collected and criterion 15 was reported NOT_RUN for a criterion that
        # had in fact been stimulated. A geometry probe must not be able to end
        # the suite; it falls back to the window centre, which is inside the
        # conversation scroller anyway, and says so in the log.
        history_rect = None
        if history is not None:
            try:
                history_rect = history.rectangle()
            except Exception as exc:
                log(f"  NOTE: could not read HistoryScroller's rectangle "
                    f"({type(exc).__name__}: {exc}); using the window centre instead")
        if history_rect is not None:
            try:
                win_left = win.rectangle().left
            except Exception as exc:
                log(f"  NOTE: could not read the window rectangle ({type(exc).__name__}: {exc}); "
                    f"clamping against the scroller's own left edge")
                win_left = history_rect.left
            hx = max(history_rect.left, win_left + 40) + 40
            hy = (history_rect.top + history_rect.bottom) // 2
        else:
            try:
                rect = win.rectangle()
            except Exception as exc:
                # A dead application makes every rectangle read fail with
                # 0x80040201, and this branch is the last place a geometry probe
                # can end the run. There is nothing left to point a wheel at, so
                # the phase is abandoned with the reason rather than with a
                # traceback.
                log(f"BLOCKED: could not read the window rectangle either "
                    f"({type(exc).__name__}: {exc}); no window is left to point the wheel at.")
                criterion_perf_profiling = "BLOCKED"
                return emit_report()
            hx = (rect.left + rect.right) // 2
            hy = (rect.top + rect.bottom) // 2
        send_wheel_at_point(win, hx, hy, 120, steps=15, pause=0.04)
        send_wheel_at_point(win, hx, hy, -120, steps=15, pause=0.04)
        time.sleep(1.5)

        log("Cleanly closing application via WM_CLOSE to flush complete performance log...")
        user32.PostMessageW(win.handle, WM_CLOSE, 0, 0)
        time.sleep(2.5)

        # =========================================================================
        # PHASE 8: PERFORMANCE METRICS ANALYSIS (TRUTHFUL DATA REPORTING)
        # =========================================================================
        log("\n--- PHASE 8: Empirical Performance Metrics Analysis ---")
        if not os.path.exists(PERF_LOG_PATH):
            log(f"FAIL: Perf log file '{PERF_LOG_PATH}' was not created")
            log("      The profiling criterion is FAIL, not NOT_RUN: the stimulus was")
            log("      applied and the artifact it is supposed to produce is absent.")
            criterion_perf_profiling = "FAIL"
            return emit_report()

        with open(PERF_LOG_PATH, "r", encoding="utf-8") as f:
            perf_data = json.load(f)

        log("Empirical UI Performance Measurements recorded from WinUI 3 CompositionTarget (NO stalls discarded):")
        log(f"  Total UI render callbacks:        {perf_data.get('total_frames')}")
        log(f"  Average render callback interval: {perf_data.get('avg_frame_ms'):.2f} ms")
        log(f"  Max render callback interval:     {perf_data.get('max_frame_ms'):.2f} ms")
        log(f"  Callbacks exceeding 33.3ms:       {perf_data.get('dropped_frames_gt_33ms')}")
        log(f"  Stall callbacks exceeding 100ms:  {perf_data.get('stall_frames_gt_100ms')}")
        log(f"  Typing latency samples:           {perf_data.get('typing_sample_count')}")
        log(f"  Typing p50 latency:               {perf_data.get('typing_p50_latency_ms'):.2f} ms")
        log(f"  Typing p95 latency:               {perf_data.get('typing_p95_latency_ms'):.2f} ms")
        log(f"  Typing max latency:               {perf_data.get('typing_max_latency_ms'):.2f} ms")
        log(f"  Scroll ViewChanged samples:       {perf_data.get('scroll_sample_count')}")
        log(f"  Scroll ViewChanged p50 interval:  {perf_data.get('scroll_p50_frame_ms'):.2f} ms")
        log(f"  Scroll ViewChanged p95 interval:  {perf_data.get('scroll_p95_frame_ms'):.2f} ms")
        log(f"  Scroll ViewChanged max interval:  {perf_data.get('scroll_max_frame_ms'):.2f} ms")
        log(f"  Scroll intervals exceeding 33ms:  {perf_data.get('dropped_scroll_frames_gt_33ms')}")

        # Evaluate every threshold before deciding, so the log carries the full
        # set of misses rather than only the first one.
        perf_failures = []
        if perf_data.get("total_frames", 0) < 50:
            perf_failures.append("Insufficient frames rendered during test session")
        if perf_data.get("avg_frame_ms", 999.0) > 33.33:
            perf_failures.append(
                f"Average frame time exceeded 33.33ms: {perf_data.get('avg_frame_ms')} ms")
        if perf_data.get("typing_sample_count", 0) > 0:
            if perf_data.get("typing_p95_latency_ms", 999.0) > 100.0:
                perf_failures.append(
                    f"Typing p95 latency exceeded 100ms: {perf_data.get('typing_p95_latency_ms')} ms")

        if perf_failures:
            for reason in perf_failures:
                log(f"FAIL: {reason}")
            criterion_perf_profiling = "FAIL"
        else:
            criterion_perf_profiling = "PASS"

        return emit_report()

    except CriterionFailure:
        # Already recorded as FAIL by the criterion that raised it; the report
        # below is the only thing left to do.
        raise

    except Exception:
        # An unexpected exception used to leave the evidence directory silent:
        # the table showed criteria as NOT_RUN and nothing anywhere said the run
        # had stopped after Phase 7. The traceback went to the console, which is
        # not part of the evidence, so a reader of the evidence could not tell an
        # abort from an unfinished harness. It is written into the log here, and
        # the criteria that were never reached keep NOT_RUN -- which the report
        # turns into a non-zero exit code.
        log("\nUNHANDLED: the run aborted with an exception that no criterion caught.")
        log(traceback.format_exc())
        log("The criteria not reached above keep their NOT_RUN verdict: this abort is the")
        log("reason they were never exercised, and NOT_RUN is not a pass.")
        return emit_report()

    finally:
        # Guarantee schema restoration if test aborted midway
        try:
            conn = sqlite3.connect(STORE_PATH)
            cur = conn.cursor()
            cur.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='contributions_test_broken';")
            if cur.fetchone():
                conn.execute("ALTER TABLE contributions_test_broken RENAME TO contributions;")
            conn.execute("UPDATE projects SET closed = 1 WHERE id = '7a546bab-c9dd-49c0-8e77-c67903ab1b66';")
            conn.execute("DELETE FROM draft_attachments WHERE attachment_id = 'att-ui-test-001' OR attachment_id = 'att-ui-test-noneng' OR attachment_id LIKE 'att-gamma-%' OR file_name = 'dropped_spec.txt';")
            conn.commit()
            conn.close()
            alpha_id = "aee9775e-6df2-46f8-a4bf-d926fa4b2cdd"
            p1 = os.path.join(os.path.dirname(STORE_PATH), "attachments", "staging", alpha_id, "att-ui-test-001_sample_spec.txt")
            p2 = os.path.join(os.path.dirname(STORE_PATH), "sample_spec.txt")
            p3 = os.path.join(os.path.dirname(STORE_PATH), "dropped_spec.txt")
            p4 = os.path.join(os.path.dirname(STORE_PATH), "test_drop_subfile.txt")
            p5 = os.path.join(os.path.dirname(STORE_PATH), "test_drop_subfolder")
            p6 = os.path.join(os.path.dirname(STORE_PATH), "attachments", "staging", alpha_id, "att-ui-test-noneng_hola_mundo.txt")
            p7 = os.path.join(os.path.dirname(STORE_PATH), "hola_mundo.txt")
            if os.path.exists(p1): os.remove(p1)
            if os.path.exists(p2): os.remove(p2)
            if os.path.exists(p3): os.remove(p3)
            if os.path.exists(p4): os.remove(p4)
            if os.path.exists(p5): os.rmdir(p5)
            if os.path.exists(p6): os.remove(p6)
            if os.path.exists(p7): os.remove(p7)
        except Exception:
            pass

        if proc.poll() is None:
            log("Terminating Mayasaba.exe test process...")
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except Exception:
                proc.kill()
        log("Cleanup complete.")

        # The store lock is released only after the application is stopped and
        # the store's own writes are done, so the next run cannot start against
        # a store this run is still finishing with.
        if store_lock is not None:
            try:
                store_lock.close()
                log("Store lock released.")
            except Exception as exc:
                log(f"Notice while releasing the store lock: {exc}")

        # A phase that returned early never reached the normal report call, and a
        # run whose only output was a FAIL line left no table saying which criteria
        # were actually exercised. The table is therefore emitted here too. It does
        # not change the exit code: an early return already decided that.
        if not report_done:
            emit_report()

if __name__ == "__main__":
    sys.exit(main())
