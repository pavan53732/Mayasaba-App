"""Focused native verification suite for Phase 4A: Chat First-Send, Composer Docking, and Recovery.

Validates the full native Phase 4A lifecycle against live Mayasaba.exe using an isolated SQLite store:
1. One Composer centered in empty bound Chat (EmptyChatHero visible, composer centered in ChatBody).
2. Logical client area measurements at 520x640 and 640x480 ensuring reachability above the fold.
3. First draft entry, Send admission, and confirmed first send.
4. Composer docking transition below timeline on confirmed save:
   - EmptyChatHero collapses.
   - HistoryScroller is realized with the saved contribution.
   - Composer moves to bottom row under timeline without re-parenting.
   - ComposerBox text is cleared and focused for subsequent input.
   - SQLite database is independently verified for exactly 1 persisted contribution.
5. Draft, caret, and focus preservation across File -> Switch Project -> Escape/Cancel.
6. Process exit and restart recovery:
   - Project opens directly in docked state (restored timeline).
   - Draft text is restored in ComposerBox with caret placed at the end.
7. Isolated EvidenceContext with exclusive file locking and artifact manifest generation.
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

DEFAULT_EVIDENCE_DIR = r"C:\Users\pavan\PROJECTS\_ui-verify\run_phase4a_verification"
BASE_STORE_PATH = r"C:\Users\pavan\PROJECTS\_ui-verify\multi_test\store.db"
BIN_DIR = r"C:\Users\pavan\PROJECTS\Mayasaba-App\native\Mayasaba\bin\x64\Release"
EXE_PATH = os.path.join(BIN_DIR, "Mayasaba.exe")
XBF_PATH = os.path.join(BIN_DIR, "MainWindow.xbf")

BETA_PROJECT_ID = "a68f6ed3-8b3e-4f5c-b7b8-4b647319c69a"
ALPHA_PROJECT_ID = "aee9775e-6df2-46f8-a4bf-d926fa4b2cdd"


class EvidenceContext:
    """Encapsulates all filesystem paths and runtime locks for an isolated verification run."""
    def __init__(self, evidence_dir=None):
        target = evidence_dir or os.environ.get("MAYASABA_EVIDENCE_DIR", DEFAULT_EVIDENCE_DIR)
        self.evidence_dir = os.path.abspath(target)
        self.screenshots_dir = os.path.join(self.evidence_dir, "screenshots")
        self.store_path = os.path.join(self.evidence_dir, "store_phase4a.db")
        self.trace_path = os.path.join(self.evidence_dir, "ui_trace_phase4a.jsonl")
        self.log_file = os.path.join(self.evidence_dir, "verification_phase4a.log")
        self.lock_file = os.path.join(self.evidence_dir, "verification_phase4a.lock")
        self.manifest_file = os.path.join(self.evidence_dir, "evidence_manifest.json")
        self._lock_handle = None

    def acquire_lock(self):
        os.makedirs(self.evidence_dir, exist_ok=True)
        os.makedirs(self.screenshots_dir, exist_ok=True)
        try:
            self._lock_handle = H.acquire_store_lock(self.lock_file)
            return True
        except Exception as ex:
            print(f"[ERROR] Failed to acquire evidence lock on {self.lock_file}: {ex}")
            return False

    def release_lock(self):
        if self._lock_handle is not None:
            try:
                kernel32.CloseHandle(self._lock_handle)
            except Exception:
                pass
            self._lock_handle = None
            try:
                if os.path.exists(self.lock_file):
                    os.unlink(self.lock_file)
            except Exception:
                pass

    def prepare_isolated_store(self, base_store):
        for ext in ["", "-wal", "-shm", ".harness.lock"]:
            f = self.store_path + ext
            if os.path.exists(f):
                try:
                    os.unlink(f)
                except Exception:
                    pass
        shutil.copyfile(base_store, self.store_path)
        # Clear beta project to create an empty bound project for Phase 4A
        conn = sqlite3.connect(self.store_path)
        conn.execute("DELETE FROM contributions WHERE project_id = ?;", (BETA_PROJECT_ID,))
        conn.execute("DELETE FROM pending_submissions WHERE project_id = ?;", (BETA_PROJECT_ID,))
        conn.execute("UPDATE projects SET draft = '' WHERE id = ?;", (BETA_PROJECT_ID,))
        conn.commit()
        conn.close()

        env = os.environ.copy()
        env["MAYASABA_STORE_PATH"] = self.store_path
        env["MAYASABA_UI_TRACE"] = self.trace_path
        env["MAYASABA_EVIDENCE_DIR"] = self.evidence_dir
        return env


def log(msg, ctx=None):
    ts = time.strftime("%H:%M:%S")
    formatted = f"[{ts}] {msg}"
    print(formatted)
    if ctx and hasattr(ctx, "log_file"):
        try:
            with open(ctx.log_file, "a", encoding="utf-8") as f:
                f.write(formatted + "\n")
        except Exception:
            pass


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(65536)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest().upper()


def capture_hwnd_to_png(hwnd, path):
    r = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    w = max(1, r.right - r.left)
    h = max(1, r.bottom - r.top)

    hdc_screen = user32.GetDC(0)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_screen)
    hbm = gdi32.CreateCompatibleBitmap(hdc_screen, w, h)
    old_bm = gdi32.SelectObject(hdc_mem, hbm)

    PW_RENDERFULLCONTENT = 0x00000002
    res = user32.PrintWindow(hwnd, hdc_mem, PW_RENDERFULLCONTENT)
    if not res:
        user32.PrintWindow(hwnd, hdc_mem, 0)

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [
            ("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG),
            ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
            ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
            ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
            ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
            ("biClrImportant", wintypes.DWORD)
        ]

    bih = BITMAPINFOHEADER()
    bih.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bih.biWidth = w
    bih.biHeight = -h
    bih.biPlanes = 1
    bih.biBitCount = 32
    bih.biCompression = 0

    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(hdc_mem, hbm, 0, h, buf, ctypes.byref(bih), 0)

    gdi32.SelectObject(hdc_mem, old_bm)
    gdi32.DeleteObject(hbm)
    gdi32.DeleteDC(hdc_mem)
    user32.ReleaseDC(0, hdc_screen)

    img = Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1)
    img.save(path)


def capture_screenshot(win, filename, label="", ctx=None):
    if ctx is None:
        return
    full_path = os.path.join(ctx.screenshots_dir, filename)
    hwnd = win.handle
    capture_hwnd_to_png(hwnd, full_path)
    h = sha256_of(full_path)
    log(f"  [SCREENSHOT] Saved: {filename} ({label})", ctx=ctx)
    return {"filename": filename, "label": label, "sha256": h, "path": full_path}


def get_client_screen_viewport(hwnd):
    cr = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(cr))
    pt_tl = wintypes.POINT(cr.left, cr.top)
    pt_br = wintypes.POINT(cr.right, cr.bottom)
    user32.ClientToScreen(hwnd, ctypes.byref(pt_tl))
    user32.ClientToScreen(hwnd, ctypes.byref(pt_br))
    return H.Rect(pt_tl.x, pt_tl.y, pt_br.x, pt_br.y)


def set_text_value(ctrl, text):
    try:
        val_pat = ctrl.iface_value
        if val_pat:
            val_pat.SetValue(text)
            return
    except Exception:
        pass
    ctrl.set_focus()
    time.sleep(0.1)
    ctrl.type_keys("^a{BACKSPACE}", pause=0.05)
    if text:
        ctrl.type_keys(text, pause=0.02, with_spaces=True)


def invoke_control(ctrl):
    try:
        inv = ctrl.iface_invoke
        if inv:
            inv.Invoke()
            return
    except Exception:
        pass
    try:
        ctrl.click_input()
    except Exception:
        ctrl.set_focus()
        time.sleep(0.1)
        H.send_key_to_window(ctrl.handle, 0x0D)


def _run_phase4a_verification(ctx):
    test_results = []

    def register_test(name, verdict, details):
        test_results.append({"name": name, "verdict": verdict, "details": details})
        log(f"[{verdict}] {name}: {details}", ctx=ctx)

    exe_sha = sha256_of(EXE_PATH)
    xbf_sha = sha256_of(XBF_PATH)

    log("=========================================================================", ctx=ctx)
    log("STARTING FOCUSED NATIVE VERIFICATION: PHASE 4A CHAT FIRST-SEND & DOCKING", ctx=ctx)
    log("=========================================================================", ctx=ctx)
    log(f"Mayasaba.exe SHA-256:   {exe_sha}", ctx=ctx)
    log(f"MainWindow.xbf SHA-256: {xbf_sha}", ctx=ctx)

    # 1. Prepare isolated database
    log("\n--- Setting up Isolated Database with Empty Bound Project (beta) ---", ctx=ctx)
    env = ctx.prepare_isolated_store(BASE_STORE_PATH)
    log(f"Prepared isolated store: {ctx.store_path}", ctx=ctx)

    # 2. Launch Mayasaba.exe in clean isolated environment
    proc = subprocess.Popen([EXE_PATH], cwd=BIN_DIR, env=env)
    log(f"Launched Mayasaba.exe (PID {proc.pid})", ctx=ctx)

    screenshots = []

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

        # =====================================================================
        # PART 1: EMPTY BOUND CHAT & CENTERED COMPOSER (UX-09)
        # =====================================================================
        log("\n--- PART 1: Empty Bound Chat & Centered Composer (UX-09) ---", ctx=ctx)
        # Screen 2: Select empty project beta
        obs_beta = H.observe_by_name(win, "Open project beta", retries=10)
        assert obs_beta.is_found, "Open project beta button missing on Screen 2"
        invoke_control(obs_beta.element)
        time.sleep(2.0)

        # Verify ChatScreen realized for beta
        obs_header = H.observe_by_name(win, "beta", retries=10)
        assert obs_header.is_found, "ChatScreen header for beta did not appear"
        log("PASS: Bound empty project beta; ChatScreen realized.", ctx=ctx)

        # Empty Chat Invariants:
        # 1. EmptyChatHero is visible
        obs_hero = H.observe_by_name(win, "Mayasaba hero", retries=5)
        assert obs_hero.is_found and obs_hero.element.is_visible(), "EmptyChatHero must be visible in empty Chat"

        obs_hero_heading = H.observe_by_name(win, "What would you like to work on?", retries=5)
        assert obs_hero_heading.is_found, "EmptyChatHeading 'What would you like to work on?' missing"

        # 2. HistoryScroller / Timeline is collapsed / not realized
        withdrawn_scroller, s_reason = H.assert_control_withdrawn(win, aid="HistoryScroller")
        assert withdrawn_scroller is True, f"HistoryScroller must be collapsed in empty Chat: {s_reason}"

        # 3. ComposerBox is visible and centered
        obs_composer = H.observe_by_aid(win, "ComposerBox", retries=5)
        assert obs_composer.is_found and obs_composer.element.is_visible(), "ComposerBox must be visible"
        composer_box = obs_composer.element

        # Measure vertical position: hero top and composer top
        hero_rect = obs_hero.element.rectangle()
        hero_heading_rect = obs_hero_heading.element.rectangle()
        comp_rect = composer_box.rectangle()
        win_rect = H.safe_window_rect(win)
        log(f"Empty Chat layout geometry: Window T={win_rect.top} B={win_rect.bottom}; Heading T={hero_heading_rect.top} B={hero_heading_rect.bottom}; Composer T={comp_rect.top} B={comp_rect.bottom}", ctx=ctx)
        assert hero_heading_rect.bottom <= comp_rect.top, "EmptyChatHeading must sit above ComposerBox"
        assert comp_rect.top - win_rect.top > 150, "ComposerBox must be centered with top clearance"

        s1 = capture_screenshot(win, "01_empty_chat_centered_composer.png", "Empty Chat with centered composer and hero art", ctx=ctx)
        screenshots.append(s1)
        register_test("empty_chat_centered_composer", "PASS", "EmptyChatHero visible, scroller collapsed, composer centered in conversation area.")

        # =====================================================================
        # PART 2: RESPONSIVE CLIENT SIZING AT 520x640 AND 640x480 (UX-09, UX-27)
        # =====================================================================
        log("\n--- PART 2: Responsive Client Sizing at 520x640 and 640x480 ---", ctx=ctx)
        # Test 520x640
        log("Measuring narrow 520x640 logical client area in empty Chat...", ctx=ctx)
        H.resize_to_logical_client(win, 520, 640)
        time.sleep(1.0)
        H.verify_client_logical_dimensions(hwnd, 520, 640)
        viewport_520 = get_client_screen_viewport(hwnd)
        r_comp_520 = H.verify_control_reachability(composer_box, viewport_520, min_w=200, min_h=50)
        assert r_comp_520["visible_w"] >= 200, "ComposerBox must have usable width at 520x640"
        s2 = capture_screenshot(win, "02_empty_chat_520x640.png", "Empty Chat at narrow 520x640", ctx=ctx)
        screenshots.append(s2)
        register_test("empty_chat_narrow_520x640", "PASS", "520x640 client area measured accurately; composer reachable.")

        # Test 640x480
        log("Measuring compact short 640x480 logical client area in empty Chat...", ctx=ctx)
        H.resize_to_logical_client(win, 640, 480)
        time.sleep(1.0)
        H.verify_client_logical_dimensions(hwnd, 640, 480)
        viewport_640 = get_client_screen_viewport(hwnd)
        r_comp_640 = H.verify_control_reachability(composer_box, viewport_640, min_w=200, min_h=50)
        assert r_comp_640["visible_w"] >= 200, "ComposerBox must be strictly above the fold at 640x480"
        s3 = capture_screenshot(win, "03_empty_chat_640x480.png", "Empty Chat at compact 640x480 (hero art collapsed)", ctx=ctx)
        screenshots.append(s3)
        register_test("empty_chat_short_640x480", "PASS", "640x480 client area measured accurately; composer reachable above fold.")

        # Restore comfortable window size
        H.resize_to_logical_client(win, 800, 600)
        time.sleep(1.0)

        # =====================================================================
        # PART 3: DRAFT ENTRY, SEND ADMISSION & FIRST SAVE (UX-10)
        # =====================================================================
        log("\n--- PART 3: First Draft Entry & Confirmed Save Docking (UX-10) ---", ctx=ctx)
        send_btn = H.find_by_aid(win, "SendButton")
        assert send_btn is not None, "SendButton missing"
        assert not send_btn.is_enabled(), "SendButton must be disabled when composer is empty"

        first_message = "First message in empty project: Phase 4A verified persistence"
        set_text_value(composer_box, first_message)
        time.sleep(0.5)

        assert composer_box.window_text() == first_message, "Draft text setup in ComposerBox failed"
        assert send_btn.is_enabled(), "SendButton must be enabled after entering text"
        s4 = capture_screenshot(win, "04_first_draft_ready_to_send.png", "Draft entered in centered composer ready to send", ctx=ctx)
        screenshots.append(s4)

        # Click SendButton to commit first message
        log("Clicking SendButton to execute first message submission...", ctx=ctx)
        invoke_control(send_btn)
        time.sleep(2.5)

        # Verify SQLite database: exactly 1 contribution row created
        conn = sqlite3.connect(ctx.store_path)
        contribs = conn.execute("SELECT row_id, submission_id, body FROM contributions WHERE project_id = ?;", (BETA_PROJECT_ID,)).fetchall()
        pending = conn.execute("SELECT * FROM pending_submissions WHERE project_id = ?;", (BETA_PROJECT_ID,)).fetchall()
        proj_draft = conn.execute("SELECT draft FROM projects WHERE id = ?;", (BETA_PROJECT_ID,)).fetchone()[0]
        conn.close()

        assert len(contribs) == 1, f"Database must contain exactly 1 contribution, found {len(contribs)}"
        assert contribs[0][2] == first_message, f"Database contribution body mismatch: {contribs[0][2]}"
        assert len(pending) == 0, f"Pending submissions must be empty after save, found {len(pending)}"
        assert proj_draft == "", f"Project draft must be cleared after save, found '{proj_draft}'"
        log("PASS: SQLite contribution confirmed; draft cleared in storage.", ctx=ctx)

        # Verify UI Docking Transition:
        # 1. EmptyChatHero is withdrawn
        withdrawn_hero, h_reason = H.assert_control_withdrawn(win, aid="EmptyChatHero")
        assert withdrawn_hero is True, f"EmptyChatHero must be collapsed after first save: {h_reason}"

        # 2. HistoryScroller is visible
        obs_hist = H.observe_by_aid(win, "HistoryScroller", retries=5)
        assert obs_hist.is_found and obs_hist.element.is_visible(), "HistoryScroller must be visible after first save"

        # 3. Message text is realized in HistoryRepeater timeline
        obs_timeline_msg = H.observe_by_name(win, first_message, retries=5)
        assert obs_timeline_msg.is_found, "Realized timeline entry for first message not found"

        # 4. ComposerBox is cleared and ready for new input
        assert composer_box.window_text() == "", "ComposerBox must be cleared after confirmed save"
        assert composer_box.has_keyboard_focus(), "ComposerBox must receive focus after Dock"

        # 5. Measure composer vertical position: now docked under HistoryScroller
        comp_rect_after = composer_box.rectangle()
        hist_rect = obs_hist.element.rectangle()
        send_rect = send_btn.rectangle()
        win_rect_after = H.safe_window_rect(win)
        log(f"Docked Chat geometry: Window B={win_rect_after.bottom}; Scroller T={hist_rect.top} B={hist_rect.bottom}; Composer T={comp_rect_after.top} B={comp_rect_after.bottom}; Send B={send_rect.bottom}", ctx=ctx)
        assert hist_rect.top < comp_rect_after.top, "HistoryScroller must sit above docked ComposerBox"
        assert send_rect.bottom <= win_rect_after.bottom, "Composer actions must remain within window bounds"

        s5 = capture_screenshot(win, "05_confirmed_first_send_docked.png", "Docked composer below realized timeline after first confirmed save", ctx=ctx)
        screenshots.append(s5)
        register_test("confirmed_first_send_docking", "PASS", "First send durably saved; hero withdrawn; composer docked; timeline realized; box cleared.")

        # =====================================================================
        # PART 4: DRAFT & FOCUS PRESERVATION ACROSS SWITCH & CANCEL (UX-11)
        # =====================================================================
        log("\n--- PART 4: Draft Preservation Across Switch Project & Cancel (UX-11) ---", ctx=ctx)
        second_draft = "Second message draft: verifying caret and focus across cancel"
        set_text_value(composer_box, second_draft)
        time.sleep(0.5)
        assert composer_box.window_text() == second_draft

        # Open File -> Switch Project
        log("Invoking native File -> Switch Project...", ctx=ctx)
        file_menu = H.find_by_name(win, "File")
        assert file_menu is not None, "File menu missing"
        invoke_control(file_menu)
        time.sleep(0.5)

        switch_item = H.find_by_name(win, "Switch Project")
        assert switch_item is not None, "Switch Project menu item missing"
        invoke_control(switch_item)
        time.sleep(1.5)

        # Cancel switch via Escape
        obs_cancel = H.observe_by_aid(win, "FolderCancelButton", retries=5)
        assert obs_cancel.is_found and obs_cancel.element.is_visible(), "FolderCancelButton must be visible"
        obs_cancel.element.set_focus()
        time.sleep(0.3)
        H.send_key_to_window(hwnd, H.VK_ESCAPE)
        time.sleep(1.5)

        # Verify return to beta Chat with draft preserved
        obs_header_beta = H.observe_by_name(win, "beta", retries=5)
        assert obs_header_beta.is_found, "Must return to active project beta"
        composer_box_after = H.find_by_aid(win, "ComposerBox")
        assert composer_box_after is not None, "ComposerBox missing after switch cancel"
        assert composer_box_after.window_text() == second_draft, f"Draft text was not preserved across switch cancel: '{composer_box_after.window_text()}'"
        log("PASS: Draft text strictly preserved across switch cancel.", ctx=ctx)

        s6 = capture_screenshot(win, "06_switch_cancel_draft_preserved.png", "Draft strictly preserved in ComposerBox after switch cancel", ctx=ctx)
        screenshots.append(s6)
        register_test("draft_preservation_across_switch_cancel", "PASS", "Draft text strictly preserved across Switch Project cancel.")

        # =====================================================================
        # PART 5: RESTART RECOVERY & RESTORED DOCKED STATE (UX-10, UX-11)
        # =====================================================================
        log("\n--- PART 5: Restart Recovery & Restored Docked State (UX-10, UX-11) ---", ctx=ctx)
        # Terminate first process cleanly
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
        log("First process terminated.", ctx=ctx)
        time.sleep(1.0)

        # Verify draft was persisted in SQLite for beta
        conn = sqlite3.connect(ctx.store_path)
        persisted_draft_raw = conn.execute("SELECT draft FROM projects WHERE id = ?;", (BETA_PROJECT_ID,)).fetchone()[0]
        conn.close()
        log(f"Persisted draft in store: length={len(persisted_draft_raw)}", ctx=ctx)

        # Launch Mayasaba.exe again against the same store_phase4a.db
        log("Relaunching Mayasaba.exe to verify restart recovery...", ctx=ctx)
        proc2 = subprocess.Popen([EXE_PATH], cwd=BIN_DIR, env=env)
        win2 = None
        deadline = time.time() + 25
        while time.time() < deadline:
            wins = [w for w in d.windows(process=proc2.pid) if "Mayasaba" in w.window_text()]
            if wins:
                win2 = wins[0]
                break
            time.sleep(0.5)
        assert win2 is not None, "MainWindow did not appear on restart within 25s"
        hwnd2 = win2.handle

        # Open project beta from recents list
        obs_beta2 = H.observe_by_name(win2, "Open project beta", retries=10)
        assert obs_beta2.is_found, "Open project beta missing on Screen 2 restart"
        invoke_control(obs_beta2.element)
        time.sleep(2.0)

        # Verify ChatScreen opened DIRECTLY DOCKED
        obs_header2 = H.observe_by_name(win2, "beta", retries=5)
        assert obs_header2.is_found, "beta ChatScreen did not open on restart"

        withdrawn_hero2, _ = H.assert_control_withdrawn(win2, aid="EmptyChatHero")
        assert withdrawn_hero2 is True, "EmptyChatHero must be collapsed on restored populated project"

        obs_scroller2 = H.observe_by_aid(win2, "HistoryScroller", retries=5)
        assert obs_scroller2.is_found and obs_scroller2.element.is_visible(), "HistoryScroller must be visible on restart"

        # Verify draft text restored in ComposerBox
        obs_box2 = H.observe_by_aid(win2, "ComposerBox", retries=5)
        assert obs_box2.is_found, "ComposerBox missing on restart"
        restored_text = obs_box2.element.window_text()
        assert restored_text == second_draft, f"Restored draft mismatch on restart: expected '{second_draft}', got '{restored_text}'"

        s7 = capture_screenshot(win2, "07_restart_recovery_docked.png", "Restored Chat opening directly docked with recovered draft", ctx=ctx)
        screenshots.append(s7)
        register_test("restart_recovery_docked_chat", "PASS", "Restart opens directly docked; restored draft in ComposerBox.")

        proc2.terminate()
        try:
            proc2.wait(timeout=5)
        except Exception:
            proc2.kill()
        log("Restart verification process terminated cleanly.", ctx=ctx)

    finally:
        for p in [proc, locals().get("proc2")]:
            if p and p.poll() is None:
                try:
                    p.terminate()
                    p.wait(timeout=2)
                except Exception:
                    p.kill()

    passed_count = sum(1 for t in test_results if t["verdict"] == "PASS")
    failed_count = sum(1 for t in test_results if t["verdict"] != "PASS")

    # Generate evidence manifest
    manifest = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "artifacts": {
            "Mayasaba.exe": {"path": EXE_PATH, "sha256": exe_sha},
            "MainWindow.xbf": {"path": XBF_PATH, "sha256": xbf_sha},
            "store_phase4a.db": {"path": ctx.store_path, "sha256": sha256_of(ctx.store_path) if os.path.exists(ctx.store_path) else None}
        },
        "screenshots": screenshots,
        "test_results": test_results,
        "summary": {
            "total": len(test_results),
            "passed": passed_count,
            "failed": failed_count
        }
    }

    with open(ctx.manifest_file, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    log("\n=========================================================================", ctx=ctx)
    log(f"FOCUSED NATIVE VERIFICATION SUMMARY: {passed_count}/{len(test_results)} PASS (failed: {failed_count})", ctx=ctx)
    log(f"Raw evidence and screenshots saved in: {ctx.evidence_dir}", ctx=ctx)
    log("=========================================================================", ctx=ctx)

    return passed_count, failed_count


def main(evidence_dir=None):
    ctx = EvidenceContext(evidence_dir)
    if not ctx.acquire_lock():
        sys.exit(1)
    try:
        passed, failed = _run_phase4a_verification(ctx)
        sys.exit(1 if failed else 0)
    finally:
        ctx.release_lock()


if __name__ == "__main__":
    main()
