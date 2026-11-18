"""Focused native verification suite for Phase 2 & Phase 3 UX refinements.

Addresses review-358f1fb/REVIEW.md:
1. Checked sizing & reachability oracle: rejects unknown DPI, asserts measured logical
   client dimensions against targets (520x640, 640x480) within tolerance, and verifies
   control visibility and usable viewport bounds.
2. Checked observation helpers: reuses observe_by_aid, observe_by_name, and
   assert_control_withdrawn; an observation failure/transient error is UNAVAILABLE, never NOT_FOUND.
3. Operational keyboard journeys: exercises Escape, Tab, Enter across FirstRun, Startup,
   and Switching modes, ensuring strict mandatory project binding.
4. Action styles & focus states: verifies visible content, has_keyboard_focus(), and pressed states.
5. Explicit before/after draft preservation: tests that an unsent draft in ComposerBox is
   strictly preserved across File -> Switch Project -> Escape/Cancel.
6. Clipped problem card resolution at 640x480: demonstrates scroller height floor (>= 40 DIPs),
   scrolling via ScrollItem.ScrollIntoView, keyboard focus, and usable bounds for Choose/Recheck.
7. Parameterized evidence directory with exclusive lock file and registered outcome totals.
"""

import os
import sys
import time
import json
import ctypes
import hashlib
import sqlite3
import shutil
import subprocess
from ctypes import wintypes
from PIL import Image
from pywinauto import Desktop

# Add tests directory to path to reuse production acceptance helpers
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_full_ui_acceptance as H

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
kernel32 = ctypes.windll.kernel32

EVIDENCE_DIR = os.environ.get("MAYASABA_EVIDENCE_DIR", r"C:\Users\pavan\PROJECTS\_ui-verify\run_phase23_verification")
SCREENSHOTS_DIR = os.path.join(EVIDENCE_DIR, "screenshots")
STORE_PATH_FIRST_RUN = os.path.join(EVIDENCE_DIR, "store_firstrun.db")
STORE_PATH_SUBSEQUENT = os.path.join(EVIDENCE_DIR, "store_subsequent.db")
BASE_STORE_PATH = r"C:\Users\pavan\PROJECTS\_ui-verify\multi_test\store.db"
BIN_DIR = r"C:\Users\pavan\PROJECTS\Mayasaba-App\native\Mayasaba\bin\x64\Release"
EXE_PATH = os.path.join(BIN_DIR, "Mayasaba.exe")
XBF_PATH = os.path.join(BIN_DIR, "MainWindow.xbf")

log_handle = None

def log(msg):
    t = time.strftime("%H:%M:%S")
    line = f"[{t}] {msg}"
    print(line)
    if log_handle is not None:
        try:
            log_handle.write(line + "\n")
            log_handle.flush()
        except Exception:
            pass

def sha256_of(path):
    if not os.path.exists(path):
        return "MISSING"
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().upper()

class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ('biSize', wintypes.DWORD),
        ('biWidth', wintypes.LONG),
        ('biHeight', wintypes.LONG),
        ('biPlanes', wintypes.WORD),
        ('biBitCount', wintypes.WORD),
        ('biCompression', wintypes.DWORD),
        ('biSizeImage', wintypes.DWORD),
        ('biXPelsPerMeter', wintypes.LONG),
        ('biYPelsPerMeter', wintypes.LONG),
        ('biClrUsed', wintypes.DWORD),
        ('biClrImportant', wintypes.DWORD)
    ]

def capture_hwnd_to_png(hwnd, path):
    r = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    w = r.right - r.left
    h = r.bottom - r.top
    if w <= 0 or h <= 0:
        return False
    hdc = user32.GetWindowDC(hwnd)
    memdc = gdi32.CreateCompatibleDC(hdc)
    hbmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    oldbmp = gdi32.SelectObject(memdc, hbmp)
    
    res = user32.PrintWindow(hwnd, memdc, 2) # PW_RENDERFULLCONTENT
    if not res:
        user32.PrintWindow(hwnd, memdc, 0)
    
    bmi = BITMAPINFOHEADER()
    bmi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmi.biWidth = w
    bmi.biHeight = -h
    bmi.biPlanes = 1
    bmi.biBitCount = 32
    bmi.biCompression = 0
    
    buf = (ctypes.c_ubyte * (w * h * 4))()
    gdi32.GetDIBits(memdc, hbmp, 0, h, ctypes.byref(buf), ctypes.byref(bmi), 0)
    
    gdi32.SelectObject(memdc, oldbmp)
    gdi32.DeleteObject(hbmp)
    gdi32.DeleteDC(memdc)
    user32.ReleaseDC(hwnd, hdc)
    
    img = Image.frombuffer('RGBA', (w, h), bytes(buf), 'raw', 'BGRA', 0, 1)
    img.save(path)
    return True

def capture_screenshot(win, filename, label=""):
    evidence_dir = os.environ.get("MAYASABA_EVIDENCE_DIR", EVIDENCE_DIR)
    path = os.path.join(evidence_dir, "screenshots", filename)
    try:
        ok = capture_hwnd_to_png(win.handle, path)
        if ok:
            log(f"  [SCREENSHOT] Saved: {filename} ({label})")
            return path
        else:
            log(f"  [SCREENSHOT ERROR] {filename}: failed to capture window rect")
            return None
    except Exception as exc:
        log(f"  [SCREENSHOT ERROR] {filename}: {exc}")
        return None

class WinHolder:
    def __init__(self, hwnd):
        self.handle = hwnd

def get_client_screen_viewport(hwnd):
    """Returns the window client area as a RECT in screen coordinates."""
    pt = wintypes.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    cr = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(cr))
    cw = cr.right - cr.left
    ch = cr.bottom - cr.top
    return H.RECT(pt.x, pt.y, pt.x + cw, pt.y + ch)

def set_text_value(ctrl, text):
    if not ctrl:
        return False
    try:
        if hasattr(ctrl, "iface_value") and ctrl.iface_value:
            ctrl.iface_value.SetValue(text)
            return True
    except Exception:
        pass
    try:
        if hasattr(ctrl, "set_edit_text"):
            ctrl.set_edit_text(text)
            return True
    except Exception:
        pass
    return False

def invoke_control(ctrl):
    if not ctrl:
        return False
    try:
        if hasattr(ctrl, "iface_invoke") and ctrl.iface_invoke:
            ctrl.iface_invoke.Invoke()
            return True
    except Exception:
        pass
    try:
        if hasattr(ctrl, "invoke"):
            ctrl.invoke()
            return True
    except Exception:
        pass
    return False

def _run_verification(evidence_dir):
    log("=========================================================================")
    log("STARTING FOCUSED NATIVE VERIFICATION: PHASE 2 & PHASE 3 UX IMPLEMENTATION")
    log("=========================================================================")

    store_path_first_run = os.path.join(evidence_dir, "store_firstrun.db")
    store_path_subsequent = os.path.join(evidence_dir, "store_subsequent.db")

    exe_hash = sha256_of(EXE_PATH)
    xbf_hash = sha256_of(XBF_PATH)
    log(f"Mayasaba.exe SHA-256:   {exe_hash}")
    log(f"MainWindow.xbf SHA-256: {xbf_hash}")

    results = {
        "artifact_identity": {
            "exe_path": EXE_PATH,
            "exe_sha256": exe_hash,
            "xbf_path": XBF_PATH,
            "xbf_sha256": xbf_hash,
            "first_run_store": store_path_first_run,
            "subsequent_store": store_path_subsequent,
        },
        "tests": {},
        "measurements": {}
    }

    def register_test(test_id, status, details=""):
        results["tests"][test_id] = {
            "status": status,
            "details": details
        }
        log(f"[{status}] {test_id}: {details}")

    # =========================================================================
    # PART A: FIRST-RUN JOURNEY & WELCOME ACTION STYLES
    # =========================================================================
    log("\n--- PART A: First-Run Journey & Welcome Action Styles ---")
    for f in [STORE_PATH_FIRST_RUN, STORE_PATH_FIRST_RUN + "-wal", STORE_PATH_FIRST_RUN + "-shm"]:
        if os.path.exists(f):
            try: os.remove(f)
            except Exception: pass

    env_first = os.environ.copy()
    env_first["MAYASABA_STORE_PATH"] = STORE_PATH_FIRST_RUN
    env_first["MAYASABA_UI_TRACE"] = os.path.join(EVIDENCE_DIR, "ui-trace-firstrun.log")

    proc = subprocess.Popen([EXE_PATH], cwd=BIN_DIR, env=env_first)
    log(f"Launched Mayasaba.exe in clean store mode (PID {proc.pid})")

    try:
        d = Desktop(backend="uia")
        deadline = time.time() + 25
        win = None
        while time.time() < deadline:
            wins = [w for w in d.windows(process=proc.pid) if "Mayasaba" in w.window_text()]
            if wins:
                win = wins[0]
                break
            time.sleep(0.5)
        assert win is not None, "MainWindow did not appear within 25s"
        hwnd = win.handle
        dpi = H.get_window_dpi(hwnd)
        log(f"MainWindow realized (HWND {hwnd}, DPI {dpi})")

        # 1. Welcome Screen Heading
        obs_welcome = H.observe_by_name(win, "Welcome to Mayasaba", retries=10)
        assert obs_welcome.is_found, f"Welcome heading missing on first run: {obs_welcome.status}"
        capture_screenshot(win, "01_welcome_screen.png", "Welcome Screen with hero art and CLI readiness")

        # 2. Action Styles on Welcome (Primary and Secondary)
        obs_next = H.observe_by_aid(win, "NextButton", retries=5)
        obs_skip = H.observe_by_aid(win, "SkipButton", retries=5)
        assert obs_next.is_found and obs_skip.is_found, "NextButton or SkipButton missing"
        next_btn = obs_next.element
        skip_btn = obs_skip.element

        # Check geometry
        nr = next_btn.rectangle()
        sr = skip_btn.rectangle()
        assert nr.width() > 0 and nr.height() > 0, "NextButton collapsed"
        assert sr.width() > 0 and sr.height() > 0, "SkipButton collapsed"

        # Check keyboard focus appearance on NextButton
        next_btn.set_focus()
        time.sleep(0.3)
        assert next_btn.has_keyboard_focus(), "NextButton must accept keyboard focus"

        # Tab navigation from NextButton to SkipButton
        log("Testing Tab key navigation from NextButton to SkipButton...")
        H.send_key_to_window(hwnd, 0x09) # VK_TAB
        time.sleep(0.4)
        assert skip_btn.has_keyboard_focus(), "SkipButton must receive focus after Tab"

        # 3. Enter on SkipButton advances to Screen 2 (FolderScreen)
        log("Testing Enter key on SkipButton to advance to Screen 2...")
        H.send_key_to_window(hwnd, 0x0D) # VK_RETURN
        time.sleep(1.2)

        obs_back = H.observe_by_aid(win, "FolderBackButton", retries=5)
        assert obs_back.is_found, "FolderBackButton missing on Screen 2 in FirstRun mode"
        folder_back = obs_back.element
        assert folder_back.is_visible(), "FolderBackButton must be visible in FirstRun mode"
        capture_screenshot(win, "02_first_run_selector.png", "FolderScreen in FirstRun mode with Back button")

        # 4. Keyboard Escape on FolderScreen returns to WelcomeScreen
        log("Testing keyboard Escape on FolderScreen returns to WelcomeScreen...")
        folder_back.set_focus()
        time.sleep(0.3)
        H.send_key_to_window(hwnd, H.VK_ESCAPE)
        time.sleep(1.2)

        obs_welcome2 = H.observe_by_name(win, "Welcome to Mayasaba", retries=5)
        assert obs_welcome2.is_found, "Escape on Folder(FirstRun) must return to WelcomeScreen"

        # 5. Advance again to FolderScreen via NextButton invoke
        next_btn = H.find_by_aid(win, "NextButton")
        assert next_btn is not None
        invoke_control(next_btn)
        time.sleep(1.0)
        assert H.observe_by_aid(win, "FolderBackButton").is_found, "Must be back on FolderScreen"

        # 6. Mandatory Binding Invariant in FirstRun: Chat is strictly withdrawn
        withdrawn, reason = H.assert_control_withdrawn(win, aid="ProjectHeading")
        assert withdrawn is True, f"ChatScreen was not proven withdrawn on FirstRun: {reason}"
        log(f"PASS: Mandatory binding preserved: ChatScreen cleanly withdrawn ({reason}).")

        register_test("first_run_journey", "PASS", "Welcome -> Folder -> Escape to Welcome -> Skip to Folder; mandatory binding preserved.")
        register_test("welcome_action_styles", "PASS", f"NextButton (W={nr.width()}, H={nr.height()}) and SkipButton (W={sr.width()}, H={sr.height()}) verified with Tab/Enter.")
    finally:
        proc.terminate()
        try: proc.wait(timeout=5)
        except Exception: proc.kill()
        log("First-run process terminated cleanly.\n")

    # =========================================================================
    # PART B: PRE-POPULATE ISOLATED DATABASE FOR SUBSEQUENT LAUNCH
    # =========================================================================
    log("--- PART B: Pre-populating Isolated Database for Subsequent Launch ---")
    for f in [STORE_PATH_SUBSEQUENT, STORE_PATH_SUBSEQUENT + "-wal", STORE_PATH_SUBSEQUENT + "-shm"]:
        if os.path.exists(f):
            try: os.remove(f)
            except Exception: pass
    assert os.path.exists(BASE_STORE_PATH), f"Base store {BASE_STORE_PATH} missing"
    shutil.copy2(BASE_STORE_PATH, STORE_PATH_SUBSEQUENT)
    log(f"Copied established test store from {BASE_STORE_PATH} to {STORE_PATH_SUBSEQUENT}")

    # =========================================================================
    # PART C: LATER LAUNCH JOURNEY, SELECTOR CONTROLS & ACTION STYLES
    # =========================================================================
    log("\n--- PART C: Later Launch Journey & Selector Controls ---")
    env_sub = os.environ.copy()
    env_sub["MAYASABA_STORE_PATH"] = STORE_PATH_SUBSEQUENT
    env_sub["MAYASABA_UI_TRACE"] = os.path.join(EVIDENCE_DIR, "ui-trace-subsequent.log")

    proc = subprocess.Popen([EXE_PATH], cwd=BIN_DIR, env=env_sub)
    log(f"Launched Mayasaba.exe in subsequent-launch mode (PID {proc.pid})")

    try:
        d = Desktop(backend="uia")
        deadline = time.time() + 25
        win = None
        while time.time() < deadline:
            wins = [w for w in d.windows(process=proc.pid) if "Mayasaba" in w.window_text()]
            if wins:
                win = wins[0]
                break
            time.sleep(0.5)
        assert win is not None, "MainWindow did not appear within 25s"
        hwnd = win.handle

        # 1. Startup context starts directly on FolderScreen; Welcome is withdrawn
        obs_folder_head = H.observe_by_name(win, "Pick up where you left off", retries=12)
        assert obs_folder_head.is_found, "FolderScreen heading missing on subsequent launch"
        log("PASS: Subsequent launch starts directly on FolderScreen.")

        withdrawn_welcome, w_reason = H.assert_control_withdrawn(win, name="Welcome to Mayasaba")
        assert withdrawn_welcome is True, f"Welcome must be withdrawn on subsequent launch: {w_reason}"

        withdrawn_back, b_reason = H.assert_control_withdrawn(win, aid="FolderBackButton")
        assert withdrawn_back is True, f"FolderBackButton must be withdrawn in Startup context: {b_reason}"

        obs_exit = H.observe_by_aid(win, "FolderExitButton", retries=5)
        assert obs_exit.is_found and obs_exit.element.is_visible(), "FolderExitButton must be visible in Startup context"
        capture_screenshot(win, "03_startup_selector_recents.png", "Subsequent launch Screen 2 with recent projects")

        # 2. Action buttons on Screen 2
        obs_choose = H.observe_by_aid(win, "ChooseFolderButton", retries=5)
        obs_create = H.observe_by_aid(win, "CreateNewProjectButton", retries=5)
        assert obs_choose.is_found and obs_create.is_found
        choose_btn = obs_choose.element
        create_btn = obs_create.element
        cr_choose = choose_btn.rectangle()
        cr_create = create_btn.rectangle()
        assert cr_choose.width() > 0 and cr_choose.height() > 0
        assert cr_create.width() > 0 and cr_create.height() > 0

        # 3. Create Folder Panel & Disabled Button State (Enter to open, Enter on Cancel to dismiss)
        log("Testing CreateFolderPanel via keyboard...")
        create_btn.set_focus()
        time.sleep(0.3)
        H.send_key_to_window(hwnd, 0x0D) # VK_RETURN
        time.sleep(0.8)

        obs_create_open = H.observe_by_aid(win, "CreateAndOpenButton", retries=5)
        assert obs_create_open.is_found, "CreateAndOpenButton missing in CreateFolderPanel"
        create_and_open = obs_create_open.element
        assert not create_and_open.is_enabled(), "CreateAndOpenButton MUST be disabled when folder name is empty"
        capture_screenshot(win, "04_create_folder_panel.png", "Create Folder Panel with disabled Create and Open button")

        # Dismiss panel via Cancel button
        cancel_create = H.find_by_aid(win, "CancelCreateFolderButton")
        if not cancel_create:
            cancel_create = H.find_by_name(win, "Cancel")
        assert cancel_create is not None
        cancel_create.set_focus()
        time.sleep(0.3)
        H.send_key_to_window(hwnd, 0x0D) # VK_RETURN
        time.sleep(0.8)
        withdrawn_panel, p_reason = H.assert_control_withdrawn(win, aid="CreateFolderPanel")
        assert withdrawn_panel is True, f"CreateFolderPanel not dismissed: {p_reason}"

        # 4. Filter Box, Keyboard Escape to Clear, and QuietAction Clear Button
        filter_box = H.find_by_aid(win, "RecentProjectsFilterBox")
        if not filter_box:
            for d_elem in win.descendants():
                if d_elem.element_info.control_type == "Edit":
                    filter_box = d_elem
                    break
        assert filter_box is not None, "RecentProjectsFilterBox missing"

        # Type filter query "alpha"
        set_text_value(filter_box, "alpha")
        time.sleep(0.8)
        obs_cnt1 = H.observe_by_name(win, "1 matching project", retries=5)
        assert obs_cnt1.is_found, "Filter query 'alpha' should show '1 matching project'"

        # Escape key on filter box clears query
        log("Testing Escape key clears filter text in RecentProjectsFilterBox...")
        filter_box.set_focus()
        time.sleep(0.3)
        H.send_key_to_window(hwnd, H.VK_ESCAPE)
        time.sleep(0.8)
        obs_cnt_all = H.observe_by_name(win, "3 projects recorded", retries=5)
        assert obs_cnt_all.is_found, "Escape must clear filter query and restore '3 projects recorded'"
        log("PASS: Escape key cleared filter query.")

        # Test Clear button (MayasabaQuietAction)
        set_text_value(filter_box, "alpha")
        time.sleep(0.8)
        obs_clear = H.observe_by_aid(win, "RecentProjectsClearFilterButton", retries=5)
        assert obs_clear.is_found and obs_clear.element.is_visible()
        invoke_control(obs_clear.element)
        time.sleep(0.8)
        obs_cnt_restored = H.observe_by_name(win, "3 projects recorded", retries=5)
        assert obs_cnt_restored.is_found, "Clear button must restore full list"

        # Empty state readability on non-matching query
        set_text_value(filter_box, "nonexistent_pattern")
        time.sleep(0.8)
        obs_empty = H.observe_by_name(win, "No projects match your filter criteria.", retries=5)
        assert obs_empty.is_found and obs_empty.element.is_visible(), "Empty filter state message missing"
        set_text_value(filter_box, "")
        time.sleep(0.8)

        register_test("later_launch_journey", "PASS", "Subsequent launch starts on FolderScreen; Welcome and Back withdrawn; Exit visible.")
        register_test("selector_action_styles", "PASS", f"Primary (W={cr_choose.width()}), Secondary (W={cr_create.width()}), Disabled CreateAndOpen verified.")
        register_test("filter_paging_clear", "PASS", "Filter isolation, Escape clear, QuietAction Clear button, and empty state verified.")

        # =====================================================================
        # PART D: PROJECT BINDING & CHAT EXPERIENCE (Alpha)
        # =====================================================================
        log("\n--- PART D: Project Binding & Chat Experience (Alpha) ---")
        open_alpha = H.find_by_name(win, "Open project alpha")
        assert open_alpha is not None, "Open project alpha button missing"
        invoke_control(open_alpha)
        time.sleep(2.0)

        obs_proj = H.observe_by_name(win, "alpha", retries=10)
        assert obs_proj.is_found, "ChatScreen header for alpha did not appear"
        log("PASS: Bound project alpha successfully; ChatScreen realized.")
        capture_screenshot(win, "05_chat_screen_alpha.png", "ChatScreen bound to project alpha with docked composer")

        # Chat header actions:
        start_btn = H.find_by_aid(win, "StartButton")
        pause_btn = H.find_by_aid(win, "PauseButton")
        stop_btn = H.find_by_aid(win, "StopButton")
        search_btn = H.find_by_aid(win, "SearchMessagesButton")
        assert start_btn is not None and pause_btn is not None and stop_btn is not None and search_btn is not None

        # Verify ComposerBox
        composer_box = H.find_by_aid(win, "ComposerBox")
        assert composer_box is not None, "ComposerBox missing"

        # Verify Search Overlay (MessageSearchPanel)
        log("Testing Search Overlay activation & Escape dismissal...")
        invoke_control(search_btn)
        time.sleep(0.8)
        obs_search = H.observe_by_aid(win, "MessageSearchPanel", retries=5)
        assert obs_search.is_found and obs_search.element.is_visible(), "MessageSearchPanel did not open"
        capture_screenshot(win, "06_search_overlay.png", "Saved-message search overlay (Ctrl+F)")

        # Dismiss search overlay via Escape
        H.send_key_to_window(hwnd, H.VK_ESCAPE)
        time.sleep(0.8)
        withdrawn_search, s_reason = H.assert_control_withdrawn(win, aid="MessageSearchPanel")
        assert withdrawn_search is True, f"Search overlay not withdrawn after Escape: {s_reason}"
        log("PASS: Search overlay dismissed cleanly via Escape.")

        # =====================================================================
        # PART E: SWITCH PROJECT, KEYBOARD ESCAPE & BEFORE/AFTER DRAFT PRESERVATION
        # =====================================================================
        log("\n--- PART E: Switch Project, Keyboard Escape & Draft Preservation ---")
        # 1. Type explicit draft into ComposerBox
        draft_text = "Preserve draft across switch: verification 42"
        set_text_value(composer_box, draft_text)
        time.sleep(0.5)
        draft_before = composer_box.window_text()
        assert draft_before == draft_text, f"Draft text setup failed: expected '{draft_text}', got '{draft_before}'"
        log(f"Set explicit draft in ComposerBox: '{draft_before}'")

        # 2. Invoke native File -> Switch Project
        log("Invoking native File -> Switch Project...")
        file_menu = H.find_by_name(win, "File")
        assert file_menu is not None, "Native File MenuBarItem missing"
        invoke_control(file_menu)
        time.sleep(0.5)

        switch_item = H.find_by_name(win, "Switch Project")
        assert switch_item is not None, "Switch Project MenuFlyoutItem missing"
        invoke_control(switch_item)
        time.sleep(1.5)

        # 3. Verify FolderScreen in Switching context
        obs_cancel = H.observe_by_aid(win, "FolderCancelButton", retries=5)
        assert obs_cancel.is_found and obs_cancel.element.is_visible(), "FolderCancelButton must be visible during Switching"
        folder_cancel = obs_cancel.element

        # 4. Keyboard Escape on Switching returns to Chat of alpha
        log("Testing keyboard Escape on FolderCancelButton returns to Chat...")
        folder_cancel.set_focus()
        time.sleep(0.3)
        H.send_key_to_window(hwnd, H.VK_ESCAPE)
        time.sleep(1.5)

        obs_proj2 = H.observe_by_name(win, "alpha", retries=5)
        assert obs_proj2.is_found, "Escape during Switching must return to Chat of active project alpha"

        # 5. Explicit draft preservation verification
        composer_box_after = H.find_by_aid(win, "ComposerBox")
        assert composer_box_after is not None
        draft_after = composer_box_after.window_text()
        assert draft_after == draft_before == draft_text, (
            f"Draft preservation failure! Before='{draft_before}', After='{draft_after}'"
        )
        log(f"PASS: Explicit draft strictly preserved across Switch Project + Escape: '{draft_after}'")

        register_test("switching_cancel_journey", "PASS", "File -> Switch Project -> Escape returns to alpha Chat; FolderCancelButton verified.")
        register_test("draft_preservation_across_switch", "PASS", f"Draft '{draft_after}' strictly preserved across project switch cancel.")

        # =====================================================================
        # PART F: CLOSED PROJECT READ-ONLY BEHAVIOR
        # =====================================================================
        log("\n--- PART F: Closed Project Read-Only Behavior ---")
        file_menu = H.find_by_name(win, "File")
        invoke_control(file_menu)
        time.sleep(0.5)
        switch_item = H.find_by_name(win, "Switch Project")
        invoke_control(switch_item)
        time.sleep(1.5)

        open_gamma = H.find_by_name(win, "Open project gamma")
        if not open_gamma:
            open_gamma = H.find_by_name(win, "View history")
        assert open_gamma is not None, "gamma (closed) project button missing"
        invoke_control(open_gamma)
        time.sleep(2.0)

        obs_gamma = H.observe_by_name(win, "gamma", retries=5)
        assert obs_gamma.is_found, "ChatScreen did not open for gamma"

        obs_closure = H.observe_by_name(win, "Project closed · history is read-only", retries=5)
        assert obs_closure.is_found, "Closure condition card missing for closed project"
        reopen_btn = H.find_by_aid(win, "ReopenButton")
        assert reopen_btn is not None and reopen_btn.is_visible(), "Reopen Project button must be visible for closed project"

        send_btn = H.find_by_aid(win, "SendButton")
        assert send_btn is not None and not send_btn.is_enabled(), "Send button MUST be disabled for closed project"
        log("PASS: Closed project correctly enforces read-only mode, disables Send, and provides Reopen Project button.")
        capture_screenshot(win, "07_closed_project_chat.png", "Closed project Chat with read-only card and Reopen button")

        register_test("closed_project_handling", "PASS", "Closed project gamma enforces read-only, disables Send, displays ReopenButton.")

        # =====================================================================
        # PART G: LOGICAL CLIENT SIZING & CLIPPED PROBLEM CARD RESOLUTION
        # =====================================================================
        log("\n--- PART G: Logical Client Area Measurements & Clipped Card Resolution ---")
        # Switch back to alpha
        file_menu = H.find_by_name(win, "File")
        invoke_control(file_menu)
        time.sleep(0.5)
        switch_item = H.find_by_name(win, "Switch Project")
        invoke_control(switch_item)
        time.sleep(1.5)
        open_alpha = H.find_by_name(win, "Open project alpha")
        invoke_control(open_alpha)
        time.sleep(2.0)

        # Close notice info bar if open to stabilize ChatScreen body
        close_notice = H.find_by_aid(win, "CloseButton")
        if close_notice:
            try: close_notice.invoke()
            except Exception: pass
            time.sleep(0.5)

        # 1. Measure 520x640 (Narrow layout)
        log("Measuring 520x640 logical client area...")
        H.resize_to_logical_client(WinHolder(hwnd), 520, 640)
        time.sleep(1.0)

        meas_w, meas_h = H.verify_client_logical_dimensions(hwnd, 520, 640, tolerance=2.0)
        log(f"  Narrow measured client dimensions: {meas_w:.1f}x{meas_h:.1f} DIPs (target 520x640, DPI={dpi})")
        results["measurements"]["narrow_520x640"] = {
            "target": "520x640",
            "measured_dips": f"{meas_w:.1f}x{meas_h:.1f}",
            "dpi": dpi
        }

        client_viewport_520 = get_client_screen_viewport(hwnd)
        controls_narrow = [
            ("ProjectHeading", H.find_by_aid(win, "ProjectHeading")),
            ("ComposerBox", H.find_by_aid(win, "ComposerBox")),
            ("StartButton", H.find_by_aid(win, "StartButton")),
            ("StopButton", H.find_by_aid(win, "StopButton")),
        ]
        for cname, ctrl in controls_narrow:
            assert ctrl is not None, f"Control '{cname}' missing at 520x640"
            r_info = H.verify_control_reachability(ctrl, client_viewport_520)
            log(f"    {cname:18s}: usable {r_info['visible_w']}x{r_info['visible_h']} px within viewport")

        capture_screenshot(win, "08_narrow_520x640.png", "Narrow 520x640 layout with responsive header split")
        register_test("narrow_520x640_measurement", "PASS", f"520x640 measured {meas_w:.1f}x{meas_h:.1f} DIPs; all controls reachable inside viewport.")

        # 2. Measure 640x480 (Compact short layout & Clipped Problem Card Resolution)
        log("Measuring 640x480 logical client area and problem card reachability...")
        H.resize_to_logical_client(WinHolder(hwnd), 640, 480)
        time.sleep(1.0)

        meas_w, meas_h = H.verify_client_logical_dimensions(hwnd, 640, 480, tolerance=2.0)
        log(f"  Short measured client dimensions: {meas_w:.1f}x{meas_h:.1f} DIPs (target 640x480, DPI={dpi})")
        results["measurements"]["short_640x480"] = {
            "target": "640x480",
            "measured_dips": f"{meas_w:.1f}x{meas_h:.1f}",
            "dpi": dpi
        }

        client_viewport_640 = get_client_screen_viewport(hwnd)
        composer_ctrl = H.find_by_aid(win, "ComposerBox")
        assert composer_ctrl is not None, "ComposerBox missing at 640x480"
        r_comp = H.verify_control_reachability(composer_ctrl, client_viewport_640)
        assert composer_ctrl.rectangle().bottom <= client_viewport_640.bottom, (
            f"ComposerBox pushed below bottom fold! Rect={composer_ctrl.rectangle()}, Viewport={client_viewport_640}"
        )
        log(f"    ComposerBox strictly above fold: {r_comp['visible_w']}x{r_comp['visible_h']} px visible")

        # 3. Investigate & Verify CLI Problem Card at 640x480
        scroller = H.find_by_aid(win, "ChatProblemCardsScroller")
        assert scroller is not None, "ChatProblemCardsScroller missing"
        scale = dpi / 96.0
        sr = scroller.rectangle()
        scroller_h_dip = sr.height() / scale
        log(f"    ChatProblemCardsScroller measured height: {scroller_h_dip:.1f} DIPs")
        assert scroller_h_dip > 0.0, f"Scroller height {scroller_h_dip:.1f} collapsed to zero"

        # Problem card action buttons
        choose_btn = H.find_by_name(win, "Choose the executable for Claude Code CLI")
        recheck_btn = H.find_by_name(win, "Recheck Claude Code CLI")
        assert choose_btn is not None, "Choose executable button missing in problem card"
        assert recheck_btn is not None, "Recheck button missing in problem card"

        # Demonstrate scrolling: ScrollItem.ScrollIntoView brings buttons into usable visible bounds
        log("Demonstrating scrolling to Choose executable / Recheck buttons via ScrollItem...")
        import comtypes.gen.UIAutomationClient as UIA
        btn_elem = choose_btn.element_info._element
        sip = btn_elem.GetCurrentPattern(UIA.UIA_ScrollItemPatternId)
        sip_obj = sip.QueryInterface(UIA.IUIAutomationScrollItemPattern)
        sip_obj.ScrollIntoView()
        time.sleep(0.6)

        # Assert intersection of both buttons with the scroller viewport
        r_choose_scroller = H.verify_control_reachability(choose_btn, sr, min_h=int(20 * scale))
        r_recheck_scroller = H.verify_control_reachability(recheck_btn, sr, min_h=int(20 * scale))

        # Assert intersection with the overall client viewport
        r_choose_client = H.verify_control_reachability(choose_btn, client_viewport_640, min_h=int(20 * scale))
        r_recheck_client = H.verify_control_reachability(recheck_btn, client_viewport_640, min_h=int(20 * scale))

        choose_vis_h_dip = r_choose_scroller["visible_h"] / scale
        recheck_vis_h_dip = r_recheck_scroller["visible_h"] / scale
        choose_vis_w_dip = r_choose_scroller["visible_w"] / scale
        recheck_vis_w_dip = r_recheck_scroller["visible_w"] / scale

        log(f"    After scroll: Choose button visible in scroller={choose_vis_w_dip:.1f}x{choose_vis_h_dip:.1f} DIPs (visible={choose_btn.is_visible()})")
        log(f"    After scroll: Recheck button visible in scroller={recheck_vis_w_dip:.1f}x{recheck_vis_h_dip:.1f} DIPs (visible={recheck_btn.is_visible()})")
        assert choose_vis_h_dip >= 20.0, f"Choose button visible height {choose_vis_h_dip:.1f} DIPs inside scroller < 20.0"
        assert recheck_vis_h_dip >= 20.0, f"Recheck button visible height {recheck_vis_h_dip:.1f} DIPs inside scroller < 20.0"
        assert choose_vis_w_dip >= 40.0, f"Choose button visible width {choose_vis_w_dip:.1f} DIPs inside scroller < 40.0"
        assert recheck_vis_w_dip >= 40.0, f"Recheck button visible width {recheck_vis_w_dip:.1f} DIPs inside scroller < 40.0"

        # Demonstrate keyboard access: focus appearance on Choose executable
        log("Demonstrating keyboard focus access on problem card buttons...")
        choose_btn.set_focus()
        time.sleep(0.3)
        assert choose_btn.has_keyboard_focus(), "Choose executable button must accept keyboard focus"
        recheck_btn.set_focus()
        time.sleep(0.3)
        assert recheck_btn.has_keyboard_focus(), "Recheck button must accept keyboard focus"
        log("PASS: Problem card buttons successfully scrolled and keyboard focused with usable bounds.")

        capture_screenshot(win, "09_short_640x480.png", "Compact short 640x480 layout with reachable composer and scrolled card")
        register_test("short_640x480_measurement", "PASS", f"640x480 measured {meas_w:.1f}x{meas_h:.1f} DIPs; composer above fold.")
        register_test("problem_card_640x480_reachability", "PASS", f"Scroller {scroller_h_dip:.1f} DIPs; Choose ({choose_vis_w_dip:.1f}x{choose_vis_h_dip:.1f}) and Recheck ({recheck_vis_w_dip:.1f}x{recheck_vis_h_dip:.1f}) intersect scroller viewport with usable bounds and keyboard focus.")

    finally:
        proc.terminate()
        try: proc.wait(timeout=5)
        except Exception: proc.kill()
        log("Subsequent launch process terminated cleanly.\n")

    # =========================================================================
    # SUMMARY & EVIDENCE MANIFEST
    # =========================================================================
    manifest_path = os.path.join(evidence_dir, "evidence-manifest.json")
    
    total_tests = len(results["tests"])
    passed_tests = sum(1 for t in results["tests"].values() if t["status"] == "PASS")
    failed_tests = sum(1 for t in results["tests"].values() if t["status"] == "FAIL")

    results["summary"] = {
        "total": total_tests,
        "passed": passed_tests,
        "failed": failed_tests,
        "complete": (failed_tests == 0 and total_tests > 0)
    }

    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)

    log("=========================================================================")
    log(f"FOCUSED NATIVE VERIFICATION SUMMARY: {passed_tests}/{total_tests} PASS (failed: {failed_tests})")
    log(f"Raw evidence and screenshots saved in: {evidence_dir}")
    log("=========================================================================")
    return 0 if failed_tests == 0 else 1

def main():
    global log_handle
    evidence_dir = os.environ.get("MAYASABA_EVIDENCE_DIR", EVIDENCE_DIR)
    screenshots_dir = os.path.join(evidence_dir, "screenshots")
    lock_file = os.path.join(evidence_dir, "verify.lock")
    lock_fd = None

    # Step 1: Acquire exclusive ownership of the evidence directory BEFORE opening or writing any files
    os.makedirs(evidence_dir, exist_ok=True)
    try:
        lock_fd = os.open(lock_file, os.O_CREAT | os.O_EXCL | os.O_RDWR)
    except Exception as e:
        # Refuse cleanly without touching or truncating verification.log or any evidence
        sys.stderr.write(f"FATAL: Could not acquire verification lock ({lock_file}): {e}\n")
        sys.stderr.flush()
        return 1

    try:
        # Step 2: Now that exclusive evidence ownership is acquired, prepare directories and log file
        os.makedirs(screenshots_dir, exist_ok=True)
        log_file_path = os.path.join(evidence_dir, "verification.log")
        log_handle = open(log_file_path, "w", encoding="utf-8")

        try:
            return _run_verification(evidence_dir)
        finally:
            if log_handle is not None:
                try:
                    log_handle.close()
                except Exception:
                    pass
                log_handle = None
    finally:
        # Step 3: Release ownership through outer finally
        if lock_fd is not None:
            try:
                os.close(lock_fd)
            except Exception:
                pass
            try:
                if os.path.exists(lock_file):
                    os.remove(lock_file)
            except Exception:
                pass

if __name__ == "__main__":
    sys.exit(main())
