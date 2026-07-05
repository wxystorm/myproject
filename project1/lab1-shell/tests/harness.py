"""
Test harness for the shell lab.

Provides ShellSession (spawns the shell binary, sends commands, reads output),
TestCase / TestResult data holders, assertion helpers, and runner functions.
"""

from __future__ import annotations

import os
import select
import signal
import subprocess
import sys
import time
from dataclasses import dataclass, field
from typing import Callable, List, Optional

# ── constants ────────────────────────────────────────────────────────────────

MAX_ARGS = 32
MAX_CMDS = 8

DEFAULT_TIMEOUT = 5.0  # seconds to wait for output after a command
IDLE_POLL = 0.05  # select poll interval
CLOSE_TIMEOUT = 3.0  # seconds to wait for shell to exit


# ── data classes ─────────────────────────────────────────────────────────────


@dataclass
class TestCase:
    name: str
    phase: str  # "a", "b", or "c"
    points: int
    func: Callable[["ShellSession"], None]
    visibility: str = "public"  # "public" or "hidden"


@dataclass
class TestResult:
    case: TestCase
    passed: bool
    message: str = ""


# ── ShellSession ─────────────────────────────────────────────────────────────


class ShellSession:
    """Spawn the shell binary and interact with it via stdin/stdout."""

    def __init__(self, binary: str, timeout: float = DEFAULT_TIMEOUT) -> None:
        self.binary = os.path.abspath(binary)
        self.timeout = timeout
        self._proc: Optional[subprocess.Popen] = None
        self._marker_id = 0

    # -- lifecycle ------------------------------------------------------------

    def start(self) -> None:
        self._proc = subprocess.Popen(
            [self.binary],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={**os.environ, "TERM": "dumb"},
        )
        # Consume any startup output without assuming a prompt is printed.
        self._read_until_idle(allow_empty_idle=True)

    def close(self) -> None:
        if self._proc is None:
            return
        try:
            self._send("exit\n")
        except (OSError, ValueError):
            pass
        try:
            self._proc.wait(timeout=CLOSE_TIMEOUT)
        except subprocess.TimeoutExpired:
            self._proc.kill()
            self._proc.wait()
        self._proc = None

    def __enter__(self) -> "ShellSession":
        self.start()
        return self

    def __exit__(self, *_exc) -> None:
        self.close()

    # -- public API -----------------------------------------------------------

    def run(self, command: str) -> str:
        """Send *command* and return its stdout.

        A follow-up marker command is used to detect completion without
        assuming anything about prompt text or whether the command itself
        produces output.
        """
        self._marker_id += 1
        marker = f"__CODEX_MARKER_{self._marker_id}__"
        self._send(command + "\n")
        self._send(f"printf {marker}\n")
        return self._read_until_marker(marker)

    def run_until_exit(self, command: str) -> str:
        """Send *command* and read stdout until the shell exits."""
        assert self._proc is not None and self._proc.stdin is not None
        self._send(command + "\n")
        self._proc.stdin.close()
        return self._read_until_eof()

    @property
    def alive(self) -> bool:
        if self._proc is None:
            return False
        return self._proc.poll() is None

    # -- internals ------------------------------------------------------------

    def _send(self, text: str) -> None:
        assert self._proc is not None and self._proc.stdin is not None
        self._proc.stdin.write(text.encode())
        self._proc.stdin.flush()

    def _read_until_idle(self, allow_empty_idle: bool = False) -> str:
        """Read stdout until no more data arrives for *self.timeout* seconds,
        or until startup goes idle with no output."""
        assert self._proc is not None and self._proc.stdout is not None
        fd = self._proc.stdout.fileno()
        os.set_blocking(fd, False)

        buf = b""
        deadline = time.monotonic() + self.timeout

        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break

            ready, _, _ = select.select([fd], [], [], min(remaining, IDLE_POLL))
            if ready:
                chunk = os.read(fd, 4096)
                if not chunk:
                    break
                buf += chunk
                # Reset deadline every time we receive data.
                deadline = time.monotonic() + IDLE_POLL * 4
            else:
                # Nothing for a full poll interval -- shell is idle.
                if buf:
                    break
                if allow_empty_idle:
                    break
                # Still waiting for first byte; honour full timeout.

        return buf.decode(errors="replace")

    def _read_until_marker(self, marker: str) -> str:
        """Read stdout until *marker* appears, then return preceding text."""
        assert self._proc is not None and self._proc.stdout is not None
        fd = self._proc.stdout.fileno()
        os.set_blocking(fd, False)

        marker_bytes = marker.encode()
        buf = b""
        deadline = time.monotonic() + self.timeout

        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break

            ready, _, _ = select.select([fd], [], [], min(remaining, IDLE_POLL))
            if not ready:
                continue

            chunk = os.read(fd, 4096)
            if not chunk:
                break

            buf += chunk
            if marker_bytes in buf:
                before, _, _ = buf.partition(marker_bytes)
                return before.decode(errors="replace")

            deadline = time.monotonic() + self.timeout

        return buf.decode(errors="replace")

    def _read_until_eof(self) -> str:
        """Read stdout until EOF or timeout."""
        assert self._proc is not None and self._proc.stdout is not None
        fd = self._proc.stdout.fileno()
        os.set_blocking(fd, False)

        buf = b""
        deadline = time.monotonic() + self.timeout

        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break

            ready, _, _ = select.select([fd], [], [], min(remaining, IDLE_POLL))
            if ready:
                chunk = os.read(fd, 4096)
                if not chunk:
                    break
                buf += chunk
                deadline = time.monotonic() + self.timeout
                continue

            if self._proc.poll() is not None:
                break

        return buf.decode(errors="replace")


# ── assertion helpers ────────────────────────────────────────────────────────


def first_nonempty_line(text: str) -> str:
    """Return the first non-blank line (stripped), or '' if none."""
    for line in text.splitlines():
        stripped = line.strip()
        if stripped:
            return stripped
    return ""


def assert_contains(output: str, expected: str, label: str = "output") -> None:
    if expected not in output:
        raise AssertionError(
            f"{label} does not contain {expected!r}.\n" f"  Got: {output!r}"
        )


def assert_not_contains(output: str, unexpected: str, label: str = "output") -> None:
    if unexpected in output:
        raise AssertionError(
            f"{label} unexpectedly contains {unexpected!r}.\n" f"  Got: {output!r}"
        )


def assert_first_line_equal(output: str, expected: str, label: str = "output") -> None:
    actual = first_nonempty_line(output)
    if actual != expected:
        raise AssertionError(
            f"First non-empty line of {label} is {actual!r}, expected {expected!r}.\n"
            f"  Full output: {output!r}"
        )


def assert_alive(session: ShellSession) -> None:
    """Assert that the shell process is still running."""
    if not session.alive:
        raise AssertionError("Shell process exited unexpectedly.")


# ── runner ───────────────────────────────────────────────────────────────────


def run_case(case: TestCase, binary: str) -> TestResult:
    """Run a single TestCase, returning a TestResult."""
    session = ShellSession(binary)
    try:
        session.start()
        case.func(session)
        return TestResult(case=case, passed=True)
    except AssertionError as exc:
        return TestResult(case=case, passed=False, message=str(exc))
    except Exception as exc:
        return TestResult(case=case, passed=False, message=f"Unexpected error: {exc}")
    finally:
        session.close()


def run_cases(cases: List[TestCase], binary: str) -> List[TestResult]:
    """Run a list of TestCases and return all results."""
    return [run_case(c, binary) for c in cases]
