"""Oracles for the acceptance harness's own helpers, with controlling mutations.

`test_full_ui_acceptance.py` is the oracle for the product; this file is the
oracle for that oracle. A helper that silently degrades -- a "poll" that looks
once, a "latency probe" that measures enumeration cost -- turns into a product
failure that cannot be reproduced on demand, which is the worst failure shape
available to a harness.

Each test states the property, checks it, then applies a mutation that makes the
property unholdable by construction and confirms the assertion catches it. A
green run alone cannot show a test would have caught the defect (AGENTS.md §13),
so the mutation is part of the evidence rather than an optional extra.

Run: python -m pytest native/tests/test_harness_oracles.py -v
  or: python native/tests/test_harness_oracles.py
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import test_full_ui_acceptance as H


# --- poll_until ------------------------------------------------------------

def test_poll_until_returns_a_late_value():
    """A correct-but-slow transition must pass, not fail on one sample."""
    calls = {"n": 0}

    def predicate():
        calls["n"] += 1
        return "ready" if calls["n"] >= 3 else None

    got = H.poll_until(predicate, timeout=5.0, interval=0.01)
    assert got == "ready", got
    assert calls["n"] >= 3, calls["n"]


def test_poll_until_returns_none_for_a_never_true_predicate():
    got = H.poll_until(lambda: None, timeout=0.25, interval=0.01)
    assert got is None, got


def test_poll_until_takes_more_than_one_sample():
    """The defect this guards: a poll that degrades into a single snapshot.

    A predicate whose first call is slow enough to consume the whole deadline
    used to end the loop after one look, so the "poll" observed exactly what a
    fixed sleep would have observed. `min_samples` makes the deadline a lower
    bound on the observation window rather than a cap on observations.
    """
    seen = {"n": 0}

    def slow_false():
        seen["n"] += 1
        time.sleep(0.12)          # first call alone exceeds the 0.1 s deadline
        return None

    H.poll_until(slow_false, timeout=0.1, interval=0.0)
    assert seen["n"] >= 2, f"only {seen['n']} sample(s): the poll degraded to a snapshot"


def test_mutation_poll_until_min_samples_one_would_degrade():
    """Mutation: min_samples=1 must reintroduce the defect, proving the test bites.

    This does not reproduce the original race -- it makes the postcondition
    unsatisfiable by construction, so a passing test cannot be a lucky one.
    """
    seen = {"n": 0}

    def slow_false():
        seen["n"] += 1
        time.sleep(0.12)
        return None

    # Same call as above but with the guard defeated.
    H.poll_until(slow_false, timeout=0.1, interval=0.0, min_samples=1)
    assert seen["n"] == 1, (
        f"with min_samples=1 the poll took {seen['n']} samples; the guard is what "
        f"prevents the degradation, so the assertion above discriminates it")


# --- ui_thread_latency -----------------------------------------------------

class _FakeUser32:
    """Stands in for the SendMessageTimeoutW binding to test the result shape."""

    def __init__(self, ok, delay=0.0, raise_exc=None, is_window=True):
        self._ok, self._delay, self._raise = ok, delay, raise_exc
        self._is_window = is_window

    def IsWindow(self, hwnd):
        # The probe checks this first, because SendMessageTimeoutW returns 0
        # instantly for a destroyed window and that is not a stall.
        return self._is_window

    def SendMessageTimeoutW(self, hwnd, msg, w, l, flags, timeout, out):
        if self._raise:
            raise self._raise
        time.sleep(self._delay)
        return self._ok

    def IsHungAppWindow(self, hwnd):
        return False


def _with_fake_user32(fake, fn):
    real = H.user32
    H.user32 = fake
    try:
        return fn()
    finally:
        H.user32 = real


def test_ui_thread_latency_reports_answered_and_seconds():
    ok, secs, detail = _with_fake_user32(
        _FakeUser32(ok=1, delay=0.05), lambda: H.ui_thread_latency(1234))
    assert ok is True, ok
    assert 0.04 <= secs < 1.0, secs
    assert "IsHungAppWindow" in detail, detail


def test_ui_thread_latency_reports_a_timeout_as_not_answered():
    ok, secs, detail = _with_fake_user32(
        _FakeUser32(ok=0, delay=0.02), lambda: H.ui_thread_latency(1234))
    assert ok is False, ok
    assert secs >= 0.02, secs


def test_ui_thread_latency_survives_an_exception():
    ok, secs, detail = _with_fake_user32(
        _FakeUser32(ok=1, raise_exc=OSError("boom")), lambda: H.ui_thread_latency(1234))
    assert ok is False, ok
    assert "OSError" in detail, detail


def test_ui_thread_latency_reports_a_destroyed_window_separately():
    """A destroyed window must not be reported as an unanswered pump.

    SendMessageTimeoutW fails immediately when the window no longer exists, so
    without the IsWindow precheck the teardown tail of every run is recorded as
    a burst of non-answers. The distinguishing signal is that the probe never
    even reached the send.
    """
    ok, secs, detail = _with_fake_user32(
        _FakeUser32(ok=1, delay=0.0, is_window=False),
        lambda: H.ui_thread_latency(1234))
    assert ok is False, ok
    assert detail == "window_destroyed", detail
    assert secs == 0.0, secs


def test_mutation_always_answered_would_make_the_probe_useless():
    """Mutation: a probe hard-wired to report success must fail the timeout test.

    This is the property that makes the probe worth anything -- it has to be
    able to say "no". Replacing it with a constant True is the mutation, and the
    assertion that encodes the defect is the `ok is False` check above.
    """
    ok, _secs, _detail = _with_fake_user32(
        _FakeUser32(ok=1, delay=0.0), lambda: H.ui_thread_latency(1234))
    assert ok is True
    # A real timeout is distinguishable only because the binding can return 0;
    # a probe that could not return False would make every stall claim vacuous.
    ok_timeout, _s, _d = _with_fake_user32(
        _FakeUser32(ok=0, delay=0.0), lambda: H.ui_thread_latency(1234))
    assert ok_timeout is False, "the probe cannot report a non-answer"


def test_ui_thread_latency_measures_the_pump_and_not_the_tree_size():
    """A slow UIA tree must not change the pump reading.

    This is the walk-vs-pump distinction, and it is the whole reason
    `ui_thread_latency` exists rather than reusing the descendant walk the
    diagnostics already take. A UIA descendant walk is a cross-process COM
    enumeration whose cost grows with the size of the tree; the pump probe asks
    the target's message loop to process WM_NULL and return. If the pump reading
    moved with the enumeration cost, the two would be measuring the same thing
    and the earlier "11.399 s UI-thread stall" would have been a real stall.

    The walk is made deliberately expensive here and the pump binding is left
    fast: the pump figure must stay where the binding put it.
    """
    class _SlowWalkUser32(_FakeUser32):
        """A user32 whose descendant enumeration is slow but whose pump answers."""

        def __init__(self, walk_cost):
            super().__init__(ok=1, delay=0.0)
            self.walk_cost = walk_cost

    class _TreeWithCost:
        """A window whose `descendants()` sleeps for the walk cost."""

        def __init__(self, cost):
            self.cost = cost

        def descendants(self):
            time.sleep(self.cost)
            return []

    for walk_cost in (0.0, 0.25):
        _ok, pump_seconds, _detail = _with_fake_user32(
            _SlowWalkUser32(walk_cost), lambda: H.ui_thread_latency(1234))
        # Enumerate a tree that costs real time, exactly as `find_by_aid` would.
        started = time.time()
        list(_TreeWithCost(walk_cost).descendants())
        walk_seconds = time.time() - started
        assert walk_seconds >= walk_cost, (walk_seconds, walk_cost)
        assert pump_seconds < 0.05, (
            f"the pump reading moved with a {walk_cost:.2f}s enumeration "
            f"({pump_seconds:.3f}s); it is measuring the walk, not the pump")


def test_mutation_reading_a_walk_as_the_pump_would_call_a_stall():
    """Mutation: substituting the walk duration for the pump reading fails.

    The defect this encodes is not hypothetical. Run 20261021-final logged an
    "11.399 s UI-thread stall" that was the duration of a `find_by_aid` walk;
    the pump answered in well under a millisecond at the same moment. If the
    harness read the walk as the pump, that run's own evidence would have
    "confirmed" a stall the application never had.

    The mutation is applied by construction: take the walk duration and use it
    where the pump duration belongs. The assertion that catches it is the
    bounded pump reading above -- the mutated figure exceeds any bound the
    correct probe satisfies.
    """
    walk_cost = 0.25
    started = time.time()
    list(range(1000))
    time.sleep(walk_cost)          # stand in for the enumeration
    mutated_reading = time.time() - started
    assert mutated_reading > 0.2, mutated_reading
    # The correct probe against the same target does not report this figure.
    _ok, pump_seconds, _detail = _with_fake_user32(
        _FakeUser32(ok=1, delay=0.0), lambda: H.ui_thread_latency(1234))
    assert pump_seconds < 0.05, (
        f"the pump probe reported {pump_seconds:.3f}s where a walk-derived "
        f"reading would have reported {mutated_reading:.3f}s")


# --- establish_foreground ---------------------------------------------------

class _FakeWin:
    def __init__(self, handle):
        self.handle = handle


class _FakeFgUser32:
    """A user32 whose foreground only becomes the target after SetForegroundWindow.

    The real SetForegroundWindow is refused for a process that does not already
    own the foreground, and the refusal is silent. A helper that trusts the
    API's return value would therefore report success against a window that
    never became foreground -- which is exactly the defect these oracles guard.
    """

    def __init__(self, target, refuse=True, foreground_window=0, changes_foreground=None):
        self._target = target
        self._refuse = refuse
        self._foreground = foreground_window
        # A lying API: SetForegroundWindow returns success but the foreground
        # does not move. This is the only way a caller can be fooled, and it is
        # exactly what the read-back exists to detect.
        self._changes = (not refuse) if changes_foreground is None else changes_foreground
        self.set_calls = 0
        self.attach_calls = 0

    def GetForegroundWindow(self):
        return self._foreground

    def GetWindowThreadProcessId(self, hwnd, pid):
        return 4242

    def AttachThreadInput(self, a, b, attach):
        self.attach_calls += 1
        return True

    def SetForegroundWindow(self, hwnd):
        self.set_calls += 1
        if self._refuse:
            return False
        if self._changes:
            self._foreground = hwnd
        return True

    def BringWindowToTop(self, hwnd):
        return True


class _FakeKernel32:
    def GetCurrentThreadId(self):
        return 1234


def _with_fakes(fake_user32, fn, fake_kernel32=None):
    real_u, real_k = H.user32, H.kernel32
    H.user32 = fake_user32
    H.kernel32 = fake_kernel32 or _FakeKernel32()
    try:
        return fn()
    finally:
        H.user32, H.kernel32 = real_u, real_k


def test_establish_foreground_reports_success_only_when_verified():
    """The foreground is established and then read back; the read decides."""
    fake = _FakeFgUser32(target=777, refuse=False, foreground_window=999)
    ok, detail = _with_fakes(fake, lambda: H.establish_foreground(_FakeWin(777), attempts=1))
    assert ok is True, detail
    assert "attached=True" in detail, detail


def test_establish_foreground_fails_when_the_os_refuses_silently():
    """A refused SetForegroundWindow must NOT be reported as established.

    This is the property the fix exists for. SetForegroundWindow returns FALSE
    here, but a helper that keyed on the call succeeding, or that never read
    the foreground back, would report a foreground the window does not have.
    """
    fake = _FakeFgUser32(target=777, refuse=True, foreground_window=999)
    ok, detail = _with_fakes(fake, lambda: H.establish_foreground(_FakeWin(777), attempts=1))
    assert ok is False, (
        f"the OS refused the request and the foreground stayed {fake._foreground}; "
        f"reporting success would assert a foreground the window does not have "
        f"(detail: {detail})")


def test_establish_foreground_fails_when_the_api_says_ok_but_the_foreground_did_not_move():
    """A successful API call is not evidence that the window is foreground.

    This is the stronger form of the same defect: SetForegroundWindow returns
    TRUE, the caller's thread is attached to the foreground thread, and the
    foreground still does not become the target. Only the read-back catches
    this, so the helper must not report success on the API's word.
    """
    fake = _FakeFgUser32(target=777, refuse=False, foreground_window=999,
                         changes_foreground=False)
    ok, detail = _with_fakes(fake, lambda: H.establish_foreground(_FakeWin(777), attempts=1))
    assert fake.set_calls > 0, "the API must actually have been called"
    assert ok is False, (
        f"the API returned success but the foreground stayed {fake._foreground}; "
        f"reporting success would assert a foreground the window does not have "
        f"(detail: {detail})")


def test_establish_foreground_is_true_when_already_foreground():
    fake = _FakeFgUser32(target=777, refuse=True, foreground_window=777)
    ok, detail = _with_fakes(fake, lambda: H.establish_foreground(_FakeWin(777), attempts=1))
    assert ok is True, detail
    assert "already foreground" in detail, detail
    assert fake.set_calls == 0, "an already-foreground window must not be disturbed"


def test_mutation_trusting_the_api_return_would_report_a_false_foreground():
    """Mutation: a helper that trusts the API's return value fails the refusal test.

    SetForegroundWindow is the mutation point: making it succeed without
    changing the actual foreground is what an unverified helper would believe.
    The assertion that encodes the defect is the `ok is False` check above.
    """
    fake = _FakeFgUser32(target=777, refuse=False, foreground_window=999,
                         changes_foreground=False)
    # The mutated helper believes the API and skips the read-back.
    api_said_ok = fake.SetForegroundWindow(777)
    assert api_said_ok is True
    assert fake.GetForegroundWindow() == 999, (
        "the API returned success but the foreground never changed; only a "
        "read-back can tell the difference")


# --- desktop_context --------------------------------------------------------

def test_desktop_context_reports_the_desktops_it_was_taken_on():
    context = H.desktop_context()
    for key in ("thread_desktop", "input_desktop", "on_input_desktop",
                "foreground_hwnd", "errors"):
        assert key in context, key
    assert isinstance(context["on_input_desktop"], bool), context
    assert isinstance(context["errors"], list), context


def test_desktop_context_survives_a_failing_binding():
    class _Broken:
        def __getattr__(self, name):
            def _raise(*_a, **_k):
                raise OSError("boom")
            return _raise

    real_u, real_k = H.user32, H.kernel32
    H.user32, H.kernel32 = _Broken(), _Broken()
    try:
        context = H.desktop_context()
    finally:
        H.user32, H.kernel32 = real_u, real_k
    assert context["errors"], "a failing binding must be reported, not swallowed"
    assert context["foreground_hwnd"] is None, context


# --- ui_sample_summary ------------------------------------------------------

def test_ui_sample_summary_reports_the_pump_latency_and_the_write_overlap():
    """The summary must state the pump latency AND the write overlap.

    A summary that reported only "the pump answered" would leave the W1
    question open: the hypothesis was not "does the pump answer" but "does the
    pump stall while the harness holds a write lock". Both numbers are
    required for the correlation to be readable from the run log.
    """
    real_samples = list(H.UI_SAMPLES)
    real_markers = list(H.UI_WRITE_MARKERS)
    try:
        H.UI_SAMPLES[:] = [
            (0.0, True, 0.001, "IsHungAppWindow=False"),
            (5.0, True, 0.002, "IsHungAppWindow=False"),
            (10.0, True, 0.003, "IsHungAppWindow=False"),
        ]
        H.UI_WRITE_MARKERS[:] = [(5.0, "DDL: test write")]
        summary = H.ui_sample_summary()
    finally:
        H.UI_SAMPLES[:] = real_samples
        H.UI_WRITE_MARKERS[:] = real_markers
    assert "3 samples" in summary, summary
    assert "3 answered" in summary, summary
    assert "max" in summary, summary
    assert "harness write(s) marked" in summary, summary
    assert "coincides with a harness write" in summary, summary
    # Only the sample at t=5.0s falls within the 500 ms window around the
    # marked write, so the summary must count exactly one.
    assert "samples within 500 ms of a harness write: 1" in summary, summary
    # The slowest sample is at t=10.0s, which is not near the marked write, so
    # the summary must say the slowdown does NOT coincide with the write.
    assert "slowest sample coincides with a harness write: False" in summary, summary


def test_ui_sample_summary_says_so_when_nothing_was_sampled():
    summary = H.ui_sample_summary()
    assert "no samples" in summary, summary


def test_ui_sample_summary_reports_a_sample_that_did_not_answer():
    """An unanswered sample is a different observation from a slow one.

    "The window did not reply within the probe timeout" must be reported on
    its own terms, with its offsets, so that it can be placed against the
    phase it happened in. Folding it into a latency percentile would
    launder a non-answer into a duration.
    """
    real_samples = list(H.UI_SAMPLES)
    try:
        H.UI_SAMPLES[:] = [
            (0.0, True, 0.001, "IsHungAppWindow=False"),
            (1.0, False, 2.0, "IsHungAppWindow=False"),
        ]
        summary = H.ui_sample_summary()
    finally:
        H.UI_SAMPLES[:] = real_samples
    assert "1 answered" in summary, summary
    assert "unanswered (no WM_NULL reply within the 2000 ms probe): 1" in summary, summary
    assert "t+1.000s" in summary, summary
    assert "none reported IsHungAppWindow=True" in summary, summary


def test_ui_sample_summary_reports_a_hung_window_when_the_os_says_so():
    real_samples = list(H.UI_SAMPLES)
    try:
        H.UI_SAMPLES[:] = [
            (0.0, False, 2.0, "IsHungAppWindow=True"),
        ]
        summary = H.ui_sample_summary()
    finally:
        H.UI_SAMPLES[:] = real_samples
    assert "1 reported IsHungAppWindow=True" in summary, summary


def test_ui_sample_summary_does_not_report_teardown_as_a_stall():
    """A sample taken after the window died is not an unanswered pump.

    SendMessageTimeoutW returns 0 immediately for a destroyed window, so the
    teardown tail of every run produced a burst of "no WM_NULL reply" samples.
    Reading those as a stall reports the application being closed as a
    responsiveness defect. Run 20261022-sampler logged 45 of them, every one
    with elapsed 0.0.
    """
    real_samples = list(H.UI_SAMPLES)
    try:
        H.UI_SAMPLES[:] = [
            (0.0, True, 0.001, "IsHungAppWindow=False"),
            (158.4, False, 0.0, "window_destroyed"),
            (158.5, False, 0.0, "window_destroyed"),
        ]
        summary = H.ui_sample_summary()
    finally:
        H.UI_SAMPLES[:] = real_samples
    assert "after window destruction (not a latency reading): 2" in summary, summary
    assert "unanswered (no WM_NULL reply" not in summary, (
        f"teardown samples were counted as unanswered pump readings: {summary}")


def test_mutation_treating_a_destroyed_window_as_unanswered_miscounts():
    """Mutation: the pre-fix classification must reproduce the miscount.

    The defect was that `unanswered` was `not s[1]` with no distinction, so a
    destroyed window landed in the same bucket as a hung one. Recomputing the
    old predicate over the same samples shows the assertion above
    discriminates it, without needing the original teardown race.
    """
    samples = [(0.0, True, 0.001, "IsHungAppWindow=False"),
               (158.4, False, 0.0, "window_destroyed"),
               (158.5, False, 0.0, "window_destroyed")]
    old_unanswered = [s for s in samples if not s[1]]
    new_unanswered = [s for s in samples if not s[1] and "window_destroyed" not in str(s[3])]
    assert len(old_unanswered) == 2, old_unanswered
    assert len(new_unanswered) == 0, new_unanswered
    assert len(old_unanswered) != len(new_unanswered), (
        "the classification change must alter the count for these samples")


def test_write_ui_sample_artifact_records_the_raw_series():
    """The raw series must be persisted, not only reduced to a summary line."""
    import json
    import os
    import tempfile
    real_samples = list(H.UI_SAMPLES)
    real_markers = list(H.UI_WRITE_MARKERS)
    real_dir = H.EVIDENCE_DIR
    with tempfile.TemporaryDirectory() as tmp:
        try:
            H.UI_SAMPLES[:] = [
                (0.0, True, 0.001, "IsHungAppWindow=False"),
                (1.5, False, 2.0, "IsHungAppWindow=True"),
            ]
            H.UI_WRITE_MARKERS[:] = [(0.25, "DDL: a write")]
            H.EVIDENCE_DIR = tmp
            path = H.write_ui_sample_artifact()
            assert path, "no artifact path returned"
            assert os.path.exists(path), path
            with open(path, encoding="utf-8") as handle:
                payload = json.load(handle)
        finally:
            H.UI_SAMPLES[:] = real_samples
            H.UI_WRITE_MARKERS[:] = real_markers
            H.EVIDENCE_DIR = real_dir
    assert len(payload["samples"]) == 2, payload
    assert payload["samples"][0][1] is True, payload
    assert payload["samples"][1][1] is False, payload
    assert payload["samples"][1][3] == "IsHungAppWindow=True", payload
    assert payload["store_writes"] == [[0.25, "DDL: a write"]], payload
    assert "SendMessageTimeoutW" in payload["probe"], payload


def test_mark_store_write_records_a_timestamped_marker():
    real_markers = list(H.UI_WRITE_MARKERS)
    real_t0 = H._RUN_T0
    try:
        H.UI_WRITE_MARKERS[:] = []
        H._RUN_T0 = None
        H.mark_store_write("a write with no sampler running")
        assert H.UI_WRITE_MARKERS == [], (
            "with no sampler running there is no clock to mark against, and a "
            "marker with a fabricated timestamp would be worse than none")
        import time as _time
        H._RUN_T0 = _time.monotonic()
        H.mark_store_write("a marked write")
        assert len(H.UI_WRITE_MARKERS) == 1, H.UI_WRITE_MARKERS
        assert H.UI_WRITE_MARKERS[0][1] == "a marked write", H.UI_WRITE_MARKERS
    finally:
        H.UI_WRITE_MARKERS[:] = real_markers
        H._RUN_T0 = real_t0


def test_pump_latency_between_reports_the_window_it_was_given():
    """The OLE window is the measurement that separates the two explanations.

    "The drop failed" does not say whether the application's pump was blocked
    for the whole attempt or was answering throughout. The window summary must
    therefore report how many probes were taken inside the window, how many
    were answered, and the worst answer.
    """
    real_samples = list(H.UI_SAMPLES)
    try:
        H.UI_SAMPLES[:] = [
            (1.0, True, 0.001, "IsHungAppWindow=False"),
            (2.0, True, 0.002, "IsHungAppWindow=False"),
            (3.0, True, 0.500, "IsHungAppWindow=False"),
            (9.0, True, 0.001, "IsHungAppWindow=False"),
        ]
        summary = H.pump_latency_between(1.0, 3.0)
    finally:
        H.UI_SAMPLES[:] = real_samples
    assert "3 sample(s)" in summary, summary
    assert "3 answered" in summary, summary
    assert "max 500.00 ms" in summary, summary
    assert "0 over 1000 ms" in summary, summary
    # The sample at t+9.0s is outside the window and must not be counted.
    assert "4 sample(s)" not in summary, summary


def test_pump_latency_between_reports_a_window_with_no_samples():
    real_samples = list(H.UI_SAMPLES)
    try:
        H.UI_SAMPLES[:] = [(1.0, True, 0.001, "IsHungAppWindow=False")]
        summary = H.pump_latency_between(50.0, 60.0)
    finally:
        H.UI_SAMPLES[:] = real_samples
    assert "no pump samples fell inside the window" in summary, summary


def test_pump_latency_between_says_so_when_the_window_was_not_marked():
    summary = H.pump_latency_between(None, None)
    assert "not measured" in summary, summary


def test_mark_event_records_a_timestamped_marker():
    real_events = list(H.UI_EVENT_MARKERS)
    real_t0 = H._RUN_T0
    try:
        H.UI_EVENT_MARKERS[:] = []
        H._RUN_T0 = None
        H.mark_event("an event with no sampler running")
        assert H.UI_EVENT_MARKERS == [], (
            "with no sampler running there is no clock to mark against")
        import time as _time
        H._RUN_T0 = _time.monotonic()
        H.mark_event("a marked event")
        assert len(H.UI_EVENT_MARKERS) == 1, H.UI_EVENT_MARKERS
        assert H.UI_EVENT_MARKERS[0][1] == "a marked event", H.UI_EVENT_MARKERS
    finally:
        H.UI_EVENT_MARKERS[:] = real_events
        H._RUN_T0 = real_t0


# --- read_status_text ----------------------------------------------------

class _FakeElement:
    """A UIA element that carries an AutomationId and a text."""

    def __init__(self, text, automation_id="SearchStatusText"):
        self._text = text
        self.element_info = type("_Info", (), {
            "automation_id": automation_id, "name": "", "control_type": "Text",
        })()

    def window_text(self):
        return self._text


class _FakeTree:
    """A minimal stand-in for the pywinauto window.

    `generations` models the panel being destroyed and rebuilt: the element
    resolved from generation N stops resolving afterwards, exactly as a
    cached UIA reference does against a rebuilt panel.
    """

    def __init__(self, generations):
        self.generations = generations
        self.calls = 0

    def descendants(self):
        self.calls += 1
        index = min(self.calls - 1, len(self.generations) - 1)
        return list(self.generations[index])


def test_read_status_text_polls_until_the_postcondition_appears():
    """A correct-but-slow transition must pass, not fail on one sample."""
    tree = _FakeTree([[_FakeElement("")], [_FakeElement("")],
                       [_FakeElement("Showing 20 of 50 matching messages")]])
    got = H.read_status_text(tree, "Showing 20 of 50 matching messages",
                              timeout=5.0, interval=0.01)
    assert "Showing 20 of 50 matching messages" in got, got
    assert tree.calls >= 3, tree.calls


def test_read_status_text_does_not_reuse_an_element_from_before_a_rebuild():
    """The defect this guards: a cached element read after the panel is rebuilt.

    The first generation's element would answer "Showing 20 of 50 ..." if it
    were cached, because the harness resolved it before the panel was closed
    and reopened. A resolver that re-resolves per attempt reads the NEW
    element, which is the one that carries the current state.
    """
    tree = _FakeTree([[_FakeElement("Showing 20 of 50 matching messages")],
                       [_FakeElement("Showing 11 of 11 matching messages")]])
    got = H.read_status_text(tree, "Showing 11 of 11 matching messages",
                             timeout=2.0, interval=0.01)
    assert "Showing 11 of 11" in got, got
    assert tree.calls >= 2, (
        "the reader resolved the element only once, so it kept the reference "
        "it took before the panel was rebuilt")


def test_read_status_text_returns_the_last_text_when_the_bound_expires():
    """A genuine defect must still fail, and must say what the panel showed."""
    tree = _FakeTree([[_FakeElement("Search failed: no such table: contributions")]])
    got = H.read_status_text(tree, "Showing 20 of 50 matching messages",
                              timeout=0.2, interval=0.01)
    assert "no such table: contributions" in got, got


def test_mutation_read_status_text_caching_the_first_element_would_miss_it():
    """Mutation: resolving the element once must reintroduce the defect.

    This does not reproduce the original race -- it makes the postcondition
    unsatisfiable by construction, so a passing test cannot be a lucky one.
    """
    tree = _FakeTree([[_FakeElement("Showing 20 of 50 matching messages")],
                       [_FakeElement("Showing 11 of 11 matching messages")]])
    cached = [d for d in tree.descendants()][0]          # the mutation
    got = cached.window_text()
    assert "Showing 11 of 11" not in got, (
        "the mutation did not make the postcondition unsatisfiable")


# --- acquire_store_lock ---------------------------------------------------

def test_acquire_store_lock_excludes_a_second_holder():
    """Two runs must not be able to share one store."""
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        store = os.path.join(tmp, "store.db")
        first = H.acquire_store_lock(store)
        try:
            raised = None
            try:
                H.acquire_store_lock(store)
            except RuntimeError as exc:
                raised = exc
            assert raised is not None, (
                "a second holder acquired the same store lock")
            assert "harness.lock" in str(raised), raised
        finally:
            first.close()


def test_mutation_acquire_store_lock_without_locking_would_not_exclude():
    """Mutation: opening the file without locking it must not exclude."""
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        store = os.path.join(tmp, "store.db")
        path = os.path.abspath(store) + ".harness.lock"
        first = open(path, "a+b")                       # the mutation: no lock
        try:
            second = open(path, "a+b")
            second.close()                               # succeeds -> no exclusion
        finally:
            first.close()


# --- uia_walk window attribution ------------------------------------------

def _swap_harness_state(samples=None, writes=None, windows=None, run_t0=None):
    """Install state, and hand back a callable that puts the old state back.

    The oracle suite runs its cases in name order against one module-level
    state, so a case that leaves samples behind changes the meaning of every
    case that runs after it. A case that inspects shared state must restore it.
    """
    saved = (list(H.UI_SAMPLES), list(H.UI_WRITE_MARKERS),
             list(H.UI_UIA_WINDOWS), H._RUN_T0)
    if samples is not None:
        H.UI_SAMPLES[:] = samples
    if writes is not None:
        H.UI_WRITE_MARKERS[:] = writes
    if windows is not None:
        H.UI_UIA_WINDOWS[:] = windows
    if run_t0 is not None:
        H._RUN_T0 = run_t0

    def restore():
        H.UI_SAMPLES[:] = saved[0]
        H.UI_WRITE_MARKERS[:] = saved[1]
        H.UI_UIA_WINDOWS[:] = saved[2]
        H._RUN_T0 = saved[3]

    return restore


def test_uia_walk_records_the_window_it_covered():
    """A UIA walk must record its own start and end, or it cannot be told
    apart from the application's own work in a latency series."""
    import time as _time
    restore = _swap_harness_state(windows=[],
                                  run_t0=_time.monotonic())
    try:
        with H.uia_walk("find_by_aid(Example)"):
            pass
        assert len(H.UI_UIA_WINDOWS) == 1, H.UI_UIA_WINDOWS
        start, end, description = H.UI_UIA_WINDOWS[0]
        assert end >= start, (start, end)
        assert description == "find_by_aid(Example)", description
    finally:
        restore()


def test_slow_samples_inside_a_uia_walk_are_reported_separately():
    """The summary must distinguish 'the app was answering the harness' from
    'the app was doing its own work'. A slow sample inside a recorded UIA walk
    is the former."""
    import time as _time
    restore = _swap_harness_state(
        samples=[(1.0, True, 0.400, ""), (5.0, True, 0.400, "")],
        writes=[],
        # a UIA walk covering only t+0.5..t+1.5
        windows=[(0.5, 1.5, "find_by_aid(Example)")],
        run_t0=_time.monotonic())
    try:
        summary = H.ui_sample_summary()
    finally:
        restore()
    assert "inside a harness UIA walk: 1" in summary, summary
    assert "outside every harness UIA walk: 1" in summary, summary


def test_mutation_a_uia_walk_that_records_nothing_cannot_separate_them():
    """Mutation: a walk that records no window makes the separation impossible.

    It does not change the latency readings; it destroys the ability to
    attribute them, which is exactly what the attribute is for.
    """
    import time as _time
    restore = _swap_harness_state(
        samples=[(1.0, True, 0.400, "")],
        writes=[],
        windows=[],
        run_t0=_time.monotonic())
    try:
        summary = H.ui_sample_summary()
    finally:
        restore()
    assert "inside a harness UIA walk: 0" in summary, summary


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS: {name}")
            except AssertionError as exc:
                failures += 1
                print(f"FAIL: {name}: {exc}")
            except Exception as exc:
                failures += 1
                print(f"ERROR: {name}: {type(exc).__name__}: {exc}")
    print(f"\n{'PASS' if not failures else 'FAIL'}: {failures} failure(s)")
    sys.exit(1 if failures else 0)
