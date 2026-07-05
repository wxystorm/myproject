"""Phase A public tests -- single command execution and error recovery (36 pts)."""

from __future__ import annotations

import sys, os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), os.pardir))

from harness import TestCase, assert_contains, assert_alive

# ── test functions ───────────────────────────────────────────────────────────


def test_single_command(s):
    """Run `echo hello`; expect 'hello' in stdout."""
    out = s.run("echo hello")
    assert_contains(out, "hello")


def test_error_recovery(s):
    """Run an invalid command, then a valid one -- shell must survive."""
    s.run("__nonexistent_cmd_xyz__")
    assert_alive(s)
    out = s.run("echo recovered")
    assert_contains(out, "recovered")


# ── public API ───────────────────────────────────────────────────────────────


def get_cases():
    return [
        TestCase(
            name="single_command",
            phase="a",
            points=18,
            func=test_single_command,
            visibility="public",
        ),
        TestCase(
            name="error_recovery",
            phase="a",
            points=18,
            func=test_error_recovery,
            visibility="public",
        ),
    ]
