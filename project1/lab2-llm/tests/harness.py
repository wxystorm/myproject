#!/usr/bin/env python3
"""Test harness for the LLM HTTP client lab."""

from __future__ import annotations

import json
import os
import pathlib
import subprocess
import sys
import threading
import time
from contextlib import contextmanager
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOLS_DIR = ROOT / "tools"
MOCK_SERVER = TOOLS_DIR / "mock_server.py"

sys.path.insert(0, str(TOOLS_DIR))
from mock_server import start_mock_server

# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class TestCase:
    name: str
    phase: str
    points: int
    func: Callable[[str], None]
    visibility: str = "public"


@dataclass(frozen=True)
class TestResult:
    case: TestCase
    passed: bool
    message: str


# ---------------------------------------------------------------------------
# Build helpers
# ---------------------------------------------------------------------------


def build_binary(target: str = "all") -> pathlib.Path:
    """Run make to build the binary.  Returns path to the built executable."""
    cmd = ["make", target] if target != "all" else ["make"]
    result = subprocess.run(
        cmd,
        cwd=str(ROOT),
        capture_output=True,
        text=True,
        timeout=30,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"make {target} failed (rc={result.returncode}):\n"
            f"{result.stdout}\n{result.stderr}"
        )
    if target == "ref":
        return ROOT / "llm-ref"
    return ROOT / "llm"


# ---------------------------------------------------------------------------
# Mock server context manager
# ---------------------------------------------------------------------------


@contextmanager
def running_mock_server(
    scenario: str = "simple", expected_path: str = "/api/v1/chat/completions"
):
    """Start a mock server on a random port, yield the port, then shut down."""
    server, thread = start_mock_server(
        host="127.0.0.1",
        port=0,
        scenario=scenario,
        expected_path=expected_path,
    )
    port = server.server_address[1]
    try:
        yield port
    finally:
        server.shutdown()
        server.server_close()


# ---------------------------------------------------------------------------
# Client runner
# ---------------------------------------------------------------------------


def run_client(
    bin_path: str,
    scenario: str = "simple",
    user_input: str = "hello",
    env_overrides: Optional[Dict[str, str]] = None,
    timeout: float = 10.0,
    expected_path: str = "/api/v1/chat/completions",
    port: Optional[int] = None,
) -> subprocess.CompletedProcess:
    """Run the LLM client binary against a mock server.

    If *port* is given the caller has already started a mock server on that
    port and we just connect to it.  Otherwise we start one automatically
    using *scenario*.
    """
    if port is not None:
        return _run_client_on_port(bin_path, port, user_input, env_overrides, timeout)

    with running_mock_server(scenario, expected_path) as auto_port:
        return _run_client_on_port(
            bin_path, auto_port, user_input, env_overrides, timeout
        )


def _run_client_on_port(
    bin_path: str,
    port: int,
    user_input: str,
    env_overrides: Optional[Dict[str, str]],
    timeout: float,
) -> subprocess.CompletedProcess:
    env = {
        "LLM_HOST": "127.0.0.1",
        "LLM_PORT": str(port),
        "MODEL": "mock-model",
        "API_KEY": "test-api-key",
    }
    if env_overrides:
        env.update(env_overrides)

    # Provide a minimal PATH so the binary can find libc etc.
    env.setdefault("PATH", os.environ.get("PATH", "/usr/bin:/bin"))
    # Propagate HOME and TMPDIR if present
    for key in ("HOME", "TMPDIR", "DYLD_LIBRARY_PATH", "LD_LIBRARY_PATH"):
        if key in os.environ:
            env.setdefault(key, os.environ[key])

    try:
        result = subprocess.run(
            [bin_path],
            input=user_input,
            capture_output=True,
            text=True,
            timeout=timeout,
            env=env,
        )
    except subprocess.TimeoutExpired:
        raise TimeoutError(f"client timed out after {timeout}s")

    return result


# ---------------------------------------------------------------------------
# Run helpers  (same pattern as lab1-shell)
# ---------------------------------------------------------------------------


def run_case(binary: str, case: TestCase) -> TestResult:
    """Execute a single test case, catching exceptions."""
    try:
        case.func(binary)
        return TestResult(case=case, passed=True, message="")
    except Exception as exc:
        return TestResult(case=case, passed=False, message=str(exc))


def run_cases(binary: str, cases: List[TestCase]) -> List[TestResult]:
    """Execute a list of test cases sequentially."""
    return [run_case(binary, c) for c in cases]
