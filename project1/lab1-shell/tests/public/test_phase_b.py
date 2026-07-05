"""Phase B public tests -- two-command pipes (36 pts)."""

from __future__ import annotations

import sys, os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), os.pardir))

from harness import TestCase, assert_contains, assert_alive, first_nonempty_line

# ── test functions ───────────────────────────────────────────────────────────


def test_single_pipe(s):
    """Run `echo hello | cat`; expect 'hello'."""
    out = s.run("echo hello | cat")
    assert_contains(out, "hello")


def test_invalid_pipe_syntax(s):
    """Leading pipe `| wc` should produce an error; shell survives."""
    s.run("| wc")
    assert_alive(s)
    out = s.run("echo ok")
    assert_contains(out, "ok")


def test_pipe_recovery(s):
    """After a successful pipe, shell still accepts the next command."""
    s.run("echo hello | cat")
    assert_alive(s)
    out = s.run("echo still_alive")
    assert_contains(out, "still_alive")


# ── public API ───────────────────────────────────────────────────────────────


def get_cases():
    return [
        TestCase(
            name="single_pipe",
            phase="b",
            points=18,
            func=test_single_pipe,
            visibility="public",
        ),
        TestCase(
            name="invalid_pipe_syntax",
            phase="b",
            points=9,
            func=test_invalid_pipe_syntax,
            visibility="public",
        ),
        TestCase(
            name="pipe_recovery",
            phase="b",
            points=9,
            func=test_pipe_recovery,
            visibility="public",
        ),
    ]
