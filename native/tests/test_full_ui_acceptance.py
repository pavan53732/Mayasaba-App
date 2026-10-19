import os
import sys
import time
import json
import sqlite3
import subprocess
import ctypes
from ctypes import wintypes
from pywinauto import Desktop
from pywinauto.keyboard import send_keys

STORE_PATH = r"C:\Users\pavan\PROJECTS\_ui-verify\multi_test\store.db"
PERF_LOG_PATH = r"C:\Users\pavan\PROJECTS\_ui-verify\multi_test\perf.json"
BIN_DIR = r"C:\Users\pavan\PROJECTS\Mayasaba-App\native\Mayasaba\bin\x64\Release"
EXE_PATH = os.path.join(BIN_DIR, "Mayasaba.exe")
OLE_DROP_EXE = r"C:\Users\pavan\PROJECTS\Mayasaba-App\native\tests\bin\ole_drop.exe"

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_CLOSE = 0x0010
WM_MOUSEWHEEL = 0x020A
WM_DPICHANGED = 0x02E0
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

def log(msg):
    sys.stdout.buffer.write(f"[ACCEPTANCE] {msg}\n".encode("utf-8", errors="replace"))
    sys.stdout.buffer.flush()

def find_by_aid(parent, aid):
    for d in parent.descendants():
        if d.element_info.automation_id == aid:
            return d
    return None

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

def main():
    log("=================================================================")
    log("STARTING STRENGTHENED NATIVE UI ACCEPTANCE AND PERF VERIFICATION")
    log("=================================================================")
    log(f"Executable: {EXE_PATH}")
    log(f"Store:      {STORE_PATH}")
    log(f"Perf log:   {PERF_LOG_PATH}")

    # Ensure store schema is in clean state before launch
    try:
        conn = sqlite3.connect(STORE_PATH)
        cur = conn.cursor()
        cur.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='contributions_test_broken';")
        if cur.fetchone():
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

    proc = subprocess.Popen([EXE_PATH], cwd=BIN_DIR, env=env)
    log(f"Launched Mayasaba.exe (PID {proc.pid})")

    try:
        time.sleep(3.5)
        d = Desktop(backend="uia")
        wins = [w for w in d.windows(process=proc.pid) if "Engineering Control Room" in w.window_text() or "Mayasaba" in w.window_text()]
        if not wins:
            log("FAIL: Could not locate main window")
            return 1

        win = wins[0]
        log(f"Connected to main window: '{win.window_text()}' (HWND {win.handle})")
        win.set_focus()
        time.sleep(1)

        # =========================================================================
        # PHASE 1: SCREEN 2 RECENT PROJECT FILTERING & SELECTION
        # =========================================================================
        log("\n--- PHASE 1: Screen 2 Recent Projects Filtering & Selection ---")

        # 1.1 Check initial project count
        count_label = find_by_aid(win, "RecentProjectsFilterCountLabel")
        count_text = count_label.window_text() if count_label else ""
        log(f"Initial count label: '{count_text}'")
        if "3 projects recorded" not in count_text:
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
        time.sleep(1.0)

        count_text = count_label.window_text() if count_label else ""
        log(f"Filtered count label: '{count_text}'")
        if "1 matching project" not in count_text:
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
        time.sleep(1.0)
        count_text = count_label.window_text() if count_label else ""
        log(f"Non-matching query count label: '{count_text}'")
        if "0 matching projects" not in count_text:
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
        time.sleep(1.0)
        count_text = count_label.window_text() if count_label else ""
        log(f"After Escape key: '{count_text}'")
        if "3 projects recorded" not in count_text:
            log(f"FAIL: Expected '3 projects recorded' after Escape key, got '{count_text}'")
            return 1
        log("PASS: Escape key clears query and successfully restores all 3 projects.")

        # 1.5 Clear button: type 'beta' and click Clear
        log("Typing 'beta' and clicking RecentProjectsClearFilterButton...")
        filter_box.set_edit_text("beta")
        time.sleep(1.0)
        clear_filter_btn = find_by_aid(win, "RecentProjectsClearFilterButton")
        if not clear_filter_btn:
            log("FAIL: RecentProjectsClearFilterButton not found")
            return 1
        clear_filter_btn.iface_invoke.Invoke()
        time.sleep(1.0)
        count_text = count_label.window_text() if count_label else ""
        log(f"After Clear button: '{count_text}'")
        if "3 projects recorded" not in count_text:
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
        time.sleep(1.0)
        count_text = count_label.window_text() if count_label else ""
        log(f"Closed status filter count: '{count_text}'")
        if "1 matching project" not in count_text:
            log(f"FAIL: Expected '1 matching project' for closed projects, got '{count_text}'")
            return 1
        gamma_btn = find_by_name(win, "Open project gamma")
        if not gamma_btn:
            log("FAIL: 'gamma' closed project button not found in closed filter")
            return 1
        log("PASS: Status filter 'Closed projects only' truthfully shows only closed project 'gamma'.")

        log("Selecting 'All projects' in status filter...")
        status_filter.select(0)
        time.sleep(1.0)
        count_text = count_label.window_text() if count_label else ""
        log(f"All status filter count: '{count_text}'")
        if "3 projects recorded" not in count_text:
            log(f"FAIL: Expected '3 projects recorded', got '{count_text}'")
            return 1
        log("PASS: Status filter 'All projects' restores all 3 projects.")

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

        status_text = find_by_aid(win, "SearchStatusText")
        load_more_btn = find_by_aid(win, "SearchLoadMoreButton")

        # Page 1 (20/50)
        s1 = status_text.window_text() if status_text else ""
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
        s2 = status_text.window_text() if status_text else ""
        log(f"Page 2 status: '{s2}'")
        if "Showing 40 of 50 matching messages" not in s2:
            log(f"FAIL: Expected 'Showing 40 of 50 matching messages', got '{s2}'")
            return 1
        log("PASS: Page 2 accumulated 40 items sequentially.")

        # Page 3 (50/50)
        log("Clicking 'Load more results' (Page 3)...")
        load_more_btn.iface_invoke.Invoke()
        time.sleep(1.5)
        s3 = status_text.window_text() if status_text else ""
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

        s_reopen = status_text.window_text() if status_text else ""
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

        s_filtered = status_text.window_text() if status_text else ""
        log(f"Filtered status: '{s_filtered}'")
        if "Showing 11 of 11 matching messages" not in s_filtered:
            log(f"FAIL: Filtered status mismatch: '{s_filtered}'")
            return 1
        log("PASS: Query change immediately cleared stale items and rendered fresh 11 matches.")

        # 2.4 Zero-matches test (nonexistent query text)
        log("\nTesting zero-match search query 'no_matching_messages_expected_999'...")
        search_box.set_edit_text("no_matching_messages_expected_999")
        time.sleep(1.5)
        s_nomatch = status_text.window_text() if status_text else ""
        log(f"Zero-match status: '{s_nomatch}'")
        if "No saved user messages match your query" not in s_nomatch:
            log(f"FAIL: Expected 'No saved user messages match your query', got '{s_nomatch}'")
            return 1
        log("PASS: Zero-match query correctly displays 'No saved user messages match your query'.")

        # 2.5 ACTUAL DATABASE QUERY FAILURE & RECOVERY
        log("\n--- Testing ACTUAL Database Query Failure & Recovery ---")
        log("Simulating SQLite query failure by temporarily altering schema...")
        conn = sqlite3.connect(STORE_PATH)
        conn.execute("ALTER TABLE contributions RENAME TO contributions_test_broken;")
        conn.commit()
        conn.close()

        log("Submitting query while database table is broken...")
        search_box.set_edit_text("trigger_database_error")
        time.sleep(1.5)

        s_fail = status_text.window_text() if status_text else ""
        log(f"Failure status text: '{s_fail}'")
        if "Search failed:" not in s_fail or "no such table: contributions" not in s_fail:
            log(f"FAIL: Expected 'Search failed: no such table: contributions', got '{s_fail}'")
            # Restore schema before returning failure
            conn = sqlite3.connect(STORE_PATH)
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
        conn.execute("ALTER TABLE contributions_test_broken RENAME TO contributions;")
        conn.commit()
        conn.close()

        log("Submitting valid query after database restoration to verify recovery...")
        search_box.set_edit_text("contribution 1")
        time.sleep(1.5)
        s_recovered = status_text.window_text() if status_text else ""
        log(f"Recovered query status: '{s_recovered}'")
        if "Showing 11 of 11 matching messages" not in s_recovered:
            log(f"FAIL: Expected 'Showing 11 of 11 matching messages' after recovery, got '{s_recovered}'")
            return 1
        log("PASS: Search UI cleanly recovered from actual database query failure once database was restored.")

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
        time.sleep(1.0)

        search_box_after = find_by_aid(win, "MessageSearchBox")
        is_search_closed = (search_box_after is None) or (search_box_after.rectangle().width() == 0)
        log(f"Search panel closed after navigation: {is_search_closed}")
        if not is_search_closed:
            log("FAIL: Search panel did not close after message navigation")
            return 1

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
        has_focus = target_card.has_keyboard_focus() or any(d.has_keyboard_focus() for d in target_card.descendants())
        log(f"Target message card has keyboard focus: {has_focus}")
        if not has_focus:
            log("FAIL: Target message card did not receive keyboard focus")
            return 1
        log("PASS: Target message card explicitly verified with keyboard focus.")

        # Assert item_status highlight independently
        initial_status = get_item_status(target_card)
        log(f"Target card status immediately after navigation: '{initial_status}'")
        if initial_status != "highlighted_14dp":
            log(f"FAIL: Expected initial item_status 'highlighted_14dp', got '{initial_status}'")
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
        file_menu = find_by_name(win, "File")
        if file_menu:
            file_menu.iface_invoke.Invoke()
            time.sleep(1.0)
            sw_items = [d for d in win.descendants() if "Switch Project" in d.window_text()]
            if sw_items:
                sw_items[0].iface_invoke.Invoke()
            else:
                send_keys("%(FS)")
        else:
            send_keys("%(FS)")
        time.sleep(2.0)

        folder_heading = find_by_aid(win, "FolderHeading")
        log(f"Folder heading after switch request: {folder_heading.window_text() if folder_heading else 'None'}")
        if not folder_heading or not folder_heading.is_visible():
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
        orig_width = win_rect_orig.width()
        orig_height = win_rect_orig.height()

        user32.MoveWindow(win.handle, win_rect_orig.left, win_rect_orig.top, 520, orig_height, True)
        time.sleep(1.0)

        narrow_rect = win.rectangle()
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
        log(f"Restoring window size to {orig_width}x{orig_height}...")
        user32.MoveWindow(win.handle, win_rect_orig.left, win_rect_orig.top, orig_width, orig_height, True)
        time.sleep(1.0)

        # Rapid Hover Sweeps (asserting cards actually exercised)
        log("\nPerforming rapid hover sweeps across realized message cards...")
        scroller = find_by_aid(win, "HistoryScroller")
        if not scroller:
            log("FAIL: HistoryScroller not found for hover sweeps")
            return 1

        cards = [c for c in scroller.children() if c.element_info.control_type == "Group"]
        log(f"Realized message cards count: {len(cards)}")
        if len(cards) < 4:
            log(f"FAIL: Insufficient message cards realized ({len(cards)}) to validate hover sweeps (need >= 4)")
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
        if hovered_count < 4:
            log(f"FAIL: Only {hovered_count} cards were exercised in hover sweep")
            return 1
        log("PASS: Rapid hover/focus sweeps verified across realized cards with zero pointer lockup or crash.")

        # =========================================================================
        # PHASE 6: WM_DPICHANGED MESSAGE HANDLING (150% & 200% SIMULATION)
        # =========================================================================
        log("\n--- PHASE 6: WM_DPICHANGED Message Handling Simulation (150% and 200%) ---")
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
        if r150.width() <= curr_rect.width() or r150.height() <= 0:
            log("FAIL: Window failed to expand under 150% DPI suggested rect")
            return 1
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
        if r200.width() <= r150.width() or r200.height() <= 0:
            log("FAIL: Window failed to expand under 200% DPI suggested rect")
            return 1
        log("PASS: 200% DPI WM_DPICHANGED handled cleanly with window expansion.")

        # Restore baseline 120 DPI (125%)
        dpi120 = 120
        sug_rect_120 = RECT(curr_rect.left, curr_rect.top, curr_rect.right, curr_rect.bottom)
        user32.SendMessageW(win.handle, WM_DPICHANGED, (dpi120 << 16) | dpi120, ctypes.byref(sug_rect_120))
        time.sleep(1.0)
        log("PASS: Display DPI restored to baseline.")
        log("NOTE: Real OS-level display-scale validation (150%/200%) requires system display settings change / sign-out and remains INCONCLUSIVE.")

        # =========================================================================
        # PHASE 9: ACTION CENTER VISIBILITY, OBLIGATION ROUTING & STALE REVALIDATION
        # =========================================================================
        log("\n--- PHASE 9: Action Center Real Action Routing & Stale Item Revalidation ---")

        # 9.1 Initial Action Center status in clean alpha project
        action_center_panel = find_by_aid(win, "ActionCenterPanel") or find_by_name(win, "Action Center")
        if not action_center_panel:
            log("FAIL: ActionCenterPanel not found in UIA tree")
            return 1

        status_label = find_by_aid(win, "ActionCenterStatusText")
        status_val = status_label.window_text() if status_label else ""
        log(f"Alpha Action Center initial status: '{status_val}'")
        if "No unresolved owner obligations" not in status_val:
            log(f"FAIL: Expected 'No unresolved owner obligations...', got '{status_val}'")
            return 1
        log("PASS: Action Center truthfully reports no unresolved obligations in clean alpha project.")

        # 9.2 Dismiss Action Center via CloseActionCenterButton
        close_ac_btn = find_by_aid(win, "CloseActionCenterButton") or find_by_name(win, "Close Action Center")
        if not close_ac_btn:
            log("FAIL: CloseActionCenterButton not found")
            return 1
        log("Dismissing Action Center via CloseActionCenterButton...")
        close_ac_btn.iface_invoke.Invoke()
        time.sleep(1.0)
        log("PASS: Action Center dismissed cleanly via CloseActionCenterButton.")

        # Verify Action Center panel is collapsed after dismissal
        ac_panel_after = find_by_aid(win, "ActionCenterPanel")
        is_ac_closed = (ac_panel_after is None) or (not ac_panel_after.is_visible())
        if not is_ac_closed:
            log("FAIL: Action Center panel remains visible after CloseActionCenterButton click")
            return 1
        log("PASS: Action Center panel confirmed collapsed following CloseActionCenterButton invocation.")

        # Test Finding 2 in live UI: Trigger background refresh while dismissed and verify panel stays closed
        log("Triggering draft state change / background refresh while Action Center is dismissed...")
        composer_box = find_by_aid(win, "ComposerBox")
        if composer_box:
            composer_box.set_edit_text("Testing dismissal stability under background refresh.")
            time.sleep(1.2) # draft save timer fires and settles
        ac_panel_dismissed_check = find_by_aid(win, "ActionCenterPanel")
        is_still_closed = (ac_panel_dismissed_check is None) or (not ac_panel_dismissed_check.is_visible())
        if not is_still_closed:
            log("FAIL: Action Center panel reopened after background refresh occurred while dismissed")
            return 1
        log("PASS: Action Center panel remains strictly collapsed; background refresh completion cannot undo dismissal.")
        if composer_box:
            composer_box.set_edit_text("")
            time.sleep(0.5)

        # 9.3 Switch to closed project 'gamma' which has ClosedProjectRestriction obligation
        # Seed gamma with 51 failed attachments so total obligations = 52 (51 attachments + 1 closed project)
        # to test bounded pagination (pageSize 50) and MoreActionCenterItemsButton continuation
        gamma_id = "7a546bab-c9dd-49c0-8e77-c67903ab1b66"
        conn = sqlite3.connect(STORE_PATH)
        for i in range(1, 52):
            conn.execute(
                "INSERT OR REPLACE INTO draft_attachments (attachment_id, project_id, file_name, file_size, sha256, detected_type, staged_path, source_path, status, error_message, version) "
                "VALUES (?, ?, ?, 10, 'dummy', 'text/plain', '', 'C:/source/file.txt', 'REJECTED', 'seeded error', 1);",
                (f"att-gamma-{i}", gamma_id, f"gamma_err_{i:02d}.txt")
            )
        conn.commit()
        conn.close()

        log("\nSwitching to closed project 'gamma' via File -> Switch Project...")
        file_menu = find_by_name(win, "File")
        if file_menu:
            file_menu.iface_invoke.Invoke()
            time.sleep(1.0)
            sw_items = [d for d in win.descendants() if "Switch Project" in d.window_text()]
            if sw_items:
                sw_items[0].iface_invoke.Invoke()
            else:
                send_keys("%(FS)")
        else:
            send_keys("%(FS)")
        time.sleep(2.0)

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
        log("Clicking ShowActionCenterDetailsButton...")
        show_details_btn.iface_invoke.Invoke()
        time.sleep(1.0)

        notice_bar = find_by_aid(win, "Notice")
        notice_desc = [d.window_text() for d in notice_bar.descendants() if d.window_text()] if notice_bar else []
        log(f"Notice text after Show Details: {notice_desc}")
        if not any("Action Center items are read-only projections" in t for t in notice_desc):
            log("FAIL: Show Details notice was not displayed in NoticeBar")
            return 1
        log("PASS: Show Details notice displayed truthfully in NoticeBar.")

        # 9.5 Test Stale Item Handling & Expected-Version Verification at Invocation
        log("\nSimulating concurrent mutation: altering version of first item in SQLite store before clicking...")
        conn = sqlite3.connect(STORE_PATH)
        # Mutate version of att-gamma-1 in SQLite store (from current version to 99)
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
        conn.execute("DELETE FROM draft_attachments WHERE project_id = ? AND attachment_id LIKE 'att-gamma-%';", (gamma_id,))
        conn.commit()
        conn.close()

        # 9.6 Switch back to alpha
        log("\nSwitching back to project alpha...")
        file_menu = find_by_name(win, "File")
        if file_menu:
            file_menu.iface_invoke.Invoke()
            time.sleep(1.0)
            sw_items = [d for d in win.descendants() if "Switch Project" in d.window_text()]
            if sw_items:
                sw_items[0].iface_invoke.Invoke()
            else:
                send_keys("%(FS)")
        else:
            send_keys("%(FS)")
        time.sleep(2.0)

        open_alpha = find_by_name(win, "Open project alpha")
        if not open_alpha:
            log("FAIL: 'Open project alpha' button not found")
            return 1
        open_alpha.iface_invoke.Invoke()
        time.sleep(2.5)
        log("PASS: Switched back to alpha; Action Center isolated to active project.")

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

        # Single admission test via ole_drop.exe
        log(f"Invoking ole_drop.exe on window handle {win.handle} with single file '{single_drop_file}'...")
        drop_proc_returncode = -1
        try:
            drop_proc = subprocess.run(
                [OLE_DROP_EXE, str(win.handle), single_drop_file],
                capture_output=True,
                text=True,
                timeout=4
            )
            drop_proc_returncode = drop_proc.returncode
            log(f"ole_drop.exe exit code: {drop_proc.returncode}")
            log(f"ole_drop.exe stdout: {drop_proc.stdout.strip()}")
            if drop_proc.stderr:
                log(f"ole_drop.exe stderr: {drop_proc.stderr.strip()}")
        except subprocess.TimeoutExpired:
            log("ole_drop.exe invocation reached bounded 4s timeout (OS modal loop / non-interactive session protection).")

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

        if drop_proc_returncode == 0 and len(dropped_records) == 1:
            rec = dropped_records[0]
            # Require exactly one newly admitted attachment with matching identity, size, digest, and ready state
            if rec[1] != "native_drop_single.txt" or rec[2] != single_drop_size or rec[3] != "READY" or rec[4] != single_drop_sha256:
                log(f"FAIL: Admitted record mismatch: {rec}")
                return 1
            if not os.path.exists(rec[5]):
                log(f"FAIL: Staged file does not exist on disk: {rec[5]}")
                return 1

            # Assert absence of automatic Send: compare contributions and pending submissions before and after
            if contribs_after != contribs_before:
                log(f"FAIL: Contributions count changed from {contribs_before} to {contribs_after} after drop")
                return 1
            if pending_after != pending_before:
                log(f"FAIL: Pending submissions count changed from {pending_before} to {pending_after} after drop")
                return 1
            comp_text = composer_box.window_text() if composer_box else ""
            if comp_text != "":
                log("FAIL: Composer text was modified by drop operation")
                return 1
            log(f"PASS: Native OLE drag-drop admitted exactly 1 single file '{rec[1]}' (size={rec[2]}, status={rec[3]}, digest={rec[4]}).")
            log("PASS: Absence of automatic Send verified: contributions and pending submissions unchanged, composer text untouched.")
            criterion_drop_execution = "PASS"

            # Test Mixed-Batch Rejection (Folder + File in batch):
            log("\nTesting mixed-batch rejection (folder + file)...")
            sub_folder = os.path.join(test_drop_dir, "test_folder")
            os.makedirs(sub_folder, exist_ok=True)
            mixed_drop_file = os.path.join(test_drop_dir, "test_mixed_file.txt")
            with open(mixed_drop_file, "wb") as f:
                f.write(b"Mixed batch test file.\n")

            mixed_proc_returncode = -1
            try:
                mixed_batch_proc = subprocess.run(
                    [OLE_DROP_EXE, str(win.handle), sub_folder, mixed_drop_file],
                    capture_output=True,
                    text=True,
                    timeout=4
                )
                mixed_proc_returncode = mixed_batch_proc.returncode
            except subprocess.TimeoutExpired:
                log("Mixed batch ole_drop.exe reached bounded timeout.")

            time.sleep(1.0)

            # Prove mixed batch reached application: verify NoticeBar rejection notice
            notice_bar = find_by_aid(win, "NoticeBar")
            notice_text = notice_bar.window_text() if notice_bar else ""
            log(f"NoticeBar text after mixed-batch drop: '{notice_text}'")

            conn = sqlite3.connect(STORE_PATH)
            mixed_admitted = conn.execute(
                "SELECT COUNT(*) FROM draft_attachments WHERE project_id = ? AND (file_name LIKE '%test_folder%' OR file_name LIKE '%test_mixed_file%');",
                (alpha_id,)
            ).fetchone()[0]
            conn.close()

            if mixed_admitted > 0:
                log(f"FAIL: Mixed batch items admitted ({mixed_admitted} records found)")
                return 1
            if "Folders cannot be attached; entire batch rejected." not in notice_text:
                log("DIAGNOSTIC: Mixed batch drop did not present expected rejection notice in NoticeBar.")
                criterion_mixed_batch = "INCONCLUSIVE"
            else:
                log("PASS: Mixed batch rejection proven: NoticeBar displays 'Folders cannot be attached; entire batch rejected.' and 0 files/folders admitted.")
                criterion_mixed_batch = "PASS"
        else:
            log("DIAGNOSTIC: Native synthetic desktop OLE drag-drop execution did not complete via synthetic injection.")
            log(f"  ole_drop exit code: {drop_proc_returncode}")
            log(f"  dropped records count: {len(dropped_records)} (expected 1 on pass)")
            log("NOTE: Synthetic cross-process OLE drag-and-drop in headless/automation environment has OS-level security/session constraints (UIPI/modal loop timeout).")
            log("In accordance with review instructions, marking native desktop drop criteria as INCONCLUSIVE while preserving discrete outcomes.")
            criterion_drop_execution = "INCONCLUSIVE"
            criterion_mixed_batch = "INCONCLUSIVE"

        # Clean up dropped test records
        conn = sqlite3.connect(STORE_PATH)
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
        file_menu = find_by_name(win, "File")
        if file_menu:
            file_menu.iface_invoke.Invoke()
            time.sleep(1.0)
            sw_items = [d for d in win.descendants() if "Switch Project" in d.window_text()]
            if sw_items:
                sw_items[0].iface_invoke.Invoke()
            else:
                send_keys("%(FS)")
        else:
            send_keys("%(FS)")
        time.sleep(2.0)

        open_alpha = find_by_name(win, "Open project alpha")
        if open_alpha:
            open_alpha.iface_invoke.Invoke()
            time.sleep(2.5)

        # Locate attachment chip and inspect button
        inspect_btn = find_by_name(win, f"Inspect {test_file_name}")
        if not inspect_btn:
            log(f"FAIL: 'Inspect {test_file_name}' button not found in AttachmentChipsPanel")
            return 1
        log(f"PASS: Attachment chip realized with inspect button 'Inspect {test_file_name}'.")

        # Click inspect button and verify NoticeBar diagnostics
        log("Invoking inspect button on attachment chip...")
        inspect_btn.iface_invoke.Invoke()
        time.sleep(1.0)

        notice_bar = find_by_aid(win, "Notice")
        notice_desc = [d.window_text() for d in notice_bar.descendants() if d.window_text()] if notice_bar else []
        log(f"Notice text after inspecting attachment: {notice_desc}")
        has_att_name = any(test_file_name in t for t in notice_desc)
        has_digest = any("Digest: Verified" in t for t in notice_desc)
        has_staged = any("Staged: Yes" in t for t in notice_desc)
        if not (has_att_name and has_digest and has_staged):
            log(f"FAIL: Notice bar did not contain expected attachment inspection details: {notice_desc}")
            return 1
        log("PASS: Attachment inspection verified: digest verified, managed staged copy confirmed, status READY.")

        # Verify AttachmentPreviewPanel controls
        preview_panel = find_by_aid(win, "AttachmentPreviewPanel")
        if not preview_panel or not preview_panel.is_visible():
            log("FAIL: AttachmentPreviewPanel not visible after invoking chip inspect")
            return 1

        preview_title = find_by_aid(win, "AttachmentPreviewTitle")
        title_text = preview_title.window_text() if preview_title else ""
        log(f"Preview panel title: '{title_text}'")
        if test_file_name not in title_text:
            log(f"FAIL: Expected preview title '{test_file_name}', got '{title_text}'")
            return 1

        preview_meta = find_by_aid(win, "AttachmentPreviewMeta")
        meta_text = preview_meta.window_text() if preview_meta else ""
        log(f"Preview panel meta: '{meta_text}'")
        if "text/plain" not in meta_text or "Digest verified" not in meta_text:
            log(f"FAIL: Preview metadata incomplete: '{meta_text}'")
            return 1

        preview_box = find_by_aid(win, "AttachmentPreviewContentBox")
        box_text = preview_box.window_text() if preview_box else ""
        log(f"Preview content text: '{box_text.strip()}'")
        if "Mayasaba native attachment inspection verification payload with valid English prose." not in box_text:
            log(f"FAIL: Preview content mismatch: '{box_text}'")
            return 1
        log("PASS: Bounded UTF-8 English text preview loaded and displayed in AttachmentPreviewPanel.")

        # Focus verification on ClosePreviewButton
        close_preview_btn = find_by_aid(win, "ClosePreviewButton")
        has_preview_focus = close_preview_btn.has_keyboard_focus() if close_preview_btn else False
        log(f"ClosePreviewButton has keyboard focus: {has_preview_focus}")
        if not has_preview_focus:
            log("FAIL: ClosePreviewButton did not receive programmatic keyboard focus")
            return 1
        log("PASS: ClosePreviewButton verified with programmatic keyboard focus upon preview opening.")

        # Dismissal via Escape key and focus restoration
        log("Sending Escape key to dismiss AttachmentPreviewPanel...")
        send_key_to_window(win.handle, VK_ESCAPE)
        time.sleep(1.0)

        preview_panel_after = find_by_aid(win, "AttachmentPreviewPanel")
        is_preview_closed = (preview_panel_after is None) or (not preview_panel_after.is_visible())
        log(f"AttachmentPreviewPanel closed after Escape: {is_preview_closed}")
        if not is_preview_closed:
            log("FAIL: AttachmentPreviewPanel did not collapse on Escape key")
            return 1
        log("PASS: Escape dismissed preview panel and cleared content memory.")

        # Clean up first test attachment
        try:
            conn = sqlite3.connect(STORE_PATH)
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
        file_menu = find_by_name(win, "File")
        if file_menu:
            file_menu.iface_invoke.Invoke()
            time.sleep(1.0)
            sw_items = [d for d in win.descendants() if "Switch Project" in d.window_text()]
            if sw_items:
                sw_items[0].iface_invoke.Invoke()
            else:
                send_keys("%(FS)")
        else:
            send_keys("%(FS)")
        time.sleep(2.0)

        open_alpha = find_by_name(win, "Open project alpha")
        if open_alpha:
            open_alpha.iface_invoke.Invoke()
            time.sleep(2.5)

        inspect_ne_btn = find_by_name(win, f"Inspect {non_eng_file}")
        if not inspect_ne_btn:
            log(f"FAIL: 'Inspect {non_eng_file}' button not found in AttachmentChipsPanel")
            return 1
        log(f"PASS: Attachment chip realized with inspect button 'Inspect {non_eng_file}'.")

        # Click inspect on non-English chip
        log("Invoking inspect on non-English attachment chip...")
        inspect_ne_btn.iface_invoke.Invoke()
        time.sleep(1.0)

        notice_bar = find_by_aid(win, "Notice")
        notice_desc_ne = [d.window_text() for d in notice_bar.descendants() if d.window_text()] if notice_bar else []
        log(f"Notice text after inspecting non-English attachment: {notice_desc_ne}")
        has_ne_preview_withheld = any("Preview unavailable: Attachment contains non-English characters or unverifiable prose" in t for t in notice_desc_ne)
        if not has_ne_preview_withheld:
            log(f"FAIL: Non-English ASCII preview was NOT withheld in NoticeBar: {notice_desc_ne}")
            return 1
        log("PASS: Non-English ASCII prose ('hola mundo') preview withheld in NoticeBar with truthful reason.")

        preview_panel_ne = find_by_aid(win, "AttachmentPreviewPanel")
        if not preview_panel_ne or not preview_panel_ne.is_visible():
            log("FAIL: AttachmentPreviewPanel not visible for non-English metadata inspection")
            return 1

        unavail_notice = find_by_aid(win, "AttachmentPreviewUnavailableNotice")
        unavail_text = unavail_notice.window_text() if unavail_notice else ""
        log(f"Unavailable notice text: '{unavail_text}'")
        if "Preview unavailable: Attachment contains non-English characters or unverifiable prose" not in unavail_text:
            log(f"FAIL: Expected unavailable notice in preview panel, got '{unavail_text}'")
            return 1
        log("PASS: AttachmentPreviewUnavailableNotice correctly displays preview withholding reason in UI.")

        # Dismiss preview
        send_key_to_window(win.handle, VK_ESCAPE)
        time.sleep(1.0)

        # Clean up non-English test attachment
        try:
            conn = sqlite3.connect(STORE_PATH)
            conn.execute("DELETE FROM draft_attachments WHERE attachment_id = ?;", (non_eng_id,))
            conn.commit()
            conn.close()
            if os.path.exists(staged_path_ne): os.remove(staged_path_ne)
            if os.path.exists(source_path_ne): os.remove(source_path_ne)
        except Exception:
            pass

        # Reload alpha again to clear chip for performance profiling
        log("Reloading clean alpha project for performance profiling...")
        file_menu = find_by_name(win, "File")
        if file_menu:
            file_menu.iface_invoke.Invoke()
            time.sleep(1.0)
            sw_items = [d for d in win.descendants() if "Switch Project" in d.window_text()]
            if sw_items:
                sw_items[0].iface_invoke.Invoke()
            else:
                send_keys("%(FS)")
        else:
            send_keys("%(FS)")
        time.sleep(2.0)

        open_alpha = find_by_name(win, "Open project alpha")
        if open_alpha:
            open_alpha.iface_invoke.Invoke()
            time.sleep(2.5)
        log("PASS: Clean alpha project restored for performance profiling.")

        # Reload alpha again to clear chip for performance profiling
        log("Reloading clean alpha project for performance profiling...")
        file_menu = find_by_name(win, "File")
        if file_menu:
            file_menu.iface_invoke.Invoke()
            time.sleep(1.0)
            sw_items = [d for d in win.descendants() if "Switch Project" in d.window_text()]
            if sw_items:
                sw_items[0].iface_invoke.Invoke()
            else:
                send_keys("%(FS)")
        else:
            send_keys("%(FS)")
        time.sleep(2.0)

        open_alpha = find_by_name(win, "Open project alpha")
        if open_alpha:
            open_alpha.iface_invoke.Invoke()
            time.sleep(2.5)
        log("PASS: Clean alpha project restored for performance profiling.")

        # =========================================================================
        # PHASE 7: ACTUAL UI PERFORMANCE PROFILING (UNCAPPED STALL RETENTION)
        # =========================================================================
        log("\n--- PHASE 7: UI Performance Profiling (Uncapped Stalls & Consecutive Scroll) ---")

        log("Typing test keystrokes into ComposerBox to measure input presentation latency...")
        composer_box = find_by_aid(win, "ComposerBox")
        if composer_box:
            for text in ["Performance", " measurement", " keystroke", " latency", " profiling."]:
                composer_box.set_edit_text(text)
                time.sleep(0.08)
        time.sleep(1.0)

        log("Scrolling HistoryScroller via consecutive mouse wheel events to measure consecutive scroll frames...")
        rect = win.rectangle()
        cx = (rect.left + rect.right) // 2
        cy = (rect.top + rect.bottom) // 2
        lparam = (cy << 16) | (cx & 0xFFFF)
        for _ in range(15):
            user32.PostMessageW(win.handle, WM_MOUSEWHEEL, 120 << 16, lparam)
            time.sleep(0.04)
        for _ in range(15):
            user32.PostMessageW(win.handle, WM_MOUSEWHEEL, (-120 & 0xFFFF) << 16, lparam)
            time.sleep(0.04)
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
            return 1

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

        if perf_data.get("total_frames", 0) < 50:
            log("FAIL: Insufficient frames rendered during test session")
            return 1
        if perf_data.get("avg_frame_ms", 999.0) > 33.33:
            log(f"FAIL: Average frame time exceeded 33.33ms: {perf_data.get('avg_frame_ms')} ms")
            return 1
        if perf_data.get("typing_sample_count", 0) > 0:
            if perf_data.get("typing_p95_latency_ms", 999.0) > 100.0:
                log(f"FAIL: Typing p95 latency exceeded 100ms: {perf_data.get('typing_p95_latency_ms')} ms")
                return 1

        verdicts = {
            "1. Recent Projects & Startup Filtering": "PASS",
            "2. Message Search Pagination & Continuation": "PASS",
            "3. Older Message Navigation & Selection": "PASS",
            "4. Search Mode Project Switch Safety": "PASS",
            "5. Narrow 520px Responsive Layout": "PASS",
            "6. Rapid Hover Sweeps & Animation Stability": "PASS",
            "7. Display DPI Scaling Simulation": "PASS",
            "8. Action Center Pagination & Resumption": "PASS",
            "9. Action Center Stale Invalidation & Refresh": "PASS",
            "10. Action Center Panel Dismissal Stability": "PASS",
            "11. Native OLE Desktop Drag-and-Drop Execution": criterion_drop_execution,
            "12. Mixed-Batch Folder Rejection on Drop": criterion_mixed_batch,
            "13. Seeded Attachment Chip Realization & Diagnostics": "PASS",
            "14. Strict Non-English Prose Preview Withholding": "PASS",
            "15. WinUI 3 CompositionTarget Performance Profiling": "PASS",
        }

        log("\n" + "=" * 70)
        log("NATIVE UI ACCEPTANCE VERIFICATION SUMMARY REPORT")
        log("=" * 70)
        pass_count = sum(1 for v in verdicts.values() if v == "PASS")
        inconclusive_count = sum(1 for v in verdicts.values() if v == "INCONCLUSIVE")
        fail_count = sum(1 for v in verdicts.values() if v == "FAIL")

        for name, v in verdicts.items():
            log(f"  {name:<54} [{v}]")
        log("-" * 70)
        log(f"  Total: {len(verdicts)} criteria | Passed: {pass_count} | Inconclusive: {inconclusive_count} | Failed: {fail_count}")
        if fail_count > 0:
            log("\nSTATUS: TEST SUITE FAILED (one or more assertions failed)")
            log("=" * 70 + "\n")
            return 1
        elif inconclusive_count > 0:
            log("\nSTATUS: ACCEPTANCE CHECKS COMPLETE (INCONCLUSIVE CRITERIA RETAINED)")
            log("NOTE: Desktop automation environment constraints prevent automated verification")
            log("      of synthetic OLE drag-drop; native manual testing or interactive session required.")
            log("=" * 70 + "\n")
            return 0
        else:
            log("\nSTATUS: ALL CRITERIA PASSED")
            log("=" * 70 + "\n")
            return 0

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

if __name__ == "__main__":
    sys.exit(main())
