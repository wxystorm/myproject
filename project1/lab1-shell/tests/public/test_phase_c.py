"""Phase C public tests -- builtins and multi-stage pipelines (28 pts)."""

from __future__ import annotations

import sys, os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), os.pardir))

from harness import TestCase, assert_contains, assert_alive, first_nonempty_line

# ── test functions ───────────────────────────────────────────────────────────


def test_cd_builtin(s):
    """Run `cd /` then `pwd`; expect '/' as output."""
    s.run("cd /")
    out = s.run("pwd")
    line = first_nonempty_line(out)
    if line != "/":
        raise AssertionError(f"After 'cd /', pwd should output '/'. Got: {line!r}")


def test_multi_stage_pipeline(s):
    """Three-stage pipeline: `echo hello | cat | wc -l` -> '1'."""
    out = s.run("echo hello | cat | wc -l")
    line = first_nonempty_line(out)
    if line != "1":
        raise AssertionError(f"Expected '1' from three-stage pipeline, got: {line!r}")


def test_exit_builtin(s):
    """`exit` should terminate the shell process."""
    s.run_until_exit("exit")
    if s.alive:
        raise AssertionError("Shell should terminate after `exit`.")


def test_builtin_in_pipeline(s):
    """Builtin in pipeline `cd / | wc` should be rejected; shell survives."""
    s.run("cd / | wc")
    assert_alive(s)
    out = s.run("echo ok")
    assert_contains(out, "ok")


# ── public API ───────────────────────────────────────────────────────────────


def get_cases():
    return [
        TestCase(
            name="cd_builtin",
            phase="c",
            points=8,
            func=test_cd_builtin,
            visibility="public",
        ),
        TestCase(
            name="multi_stage_pipeline",
            phase="c",
            points=7,
            func=test_multi_stage_pipeline,
            visibility="public",
        ),
        TestCase(
            name="exit_builtin",
            phase="c",
            points=6,
            func=test_exit_builtin,
            visibility="public",
        ),
        TestCase(
            name="builtin_in_pipeline",
            phase="c",
            points=7,
            func=test_builtin_in_pipeline,
            visibility="public",
        ),
    ]
