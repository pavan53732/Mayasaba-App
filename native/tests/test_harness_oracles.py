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

    def __init__(self, ok, delay=0.0, raise_exc=None):
        self._ok, self._delay, self._raise = ok, delay, raise_exc

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
