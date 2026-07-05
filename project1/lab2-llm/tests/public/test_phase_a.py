#!/usr/bin/env python3
"""Phase A public tests (50 pts): Connection and request building."""

from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from harness import TestCase, run_client, start_mock_server

# ---------------------------------------------------------------------------
# Test functions  (each receives the binary path)
# ---------------------------------------------------------------------------


def run_phase_a_case(binary: str, scenario: str, user_input: str):
    """Run the client against a real mock server and return both result and server state.

    Phase A only requires request construction + connect + send. The response
    path is covered in Phase B, so these tests inspect what the server received
    instead of requiring successful stdout.
    """
    server, thread = start_mock_server(
        host="127.0.0.1",
        port=0,
        scenario=scenario,
        expected_path="/api/v1/chat/completions",
    )
    port = server.server_address[1]
    try:
        result = run_client(binary, user_input=user_input, port=port)
        return result, server
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=1.0)


def assert_not_killed_by_signal(result) -> None:
    if result.returncode < 0:
        raise AssertionError(
            f"client crashed with signal {-result.returncode}\n"
            f"stdout: {result.stdout}\n"
            f"stderr: {result.stderr}"
        )


def test_connect_basic(binary: str) -> None:
    """Phase A: the request should reach the mock server without crashing."""
    result, server = run_phase_a_case(
        binary, scenario="simple", user_input="say hello\n"
    )
    assert_not_killed_by_signal(result)
    if server.request_count != 1:
        raise AssertionError(
            "expected exactly one valid request to reach the mock server\n"
            f"returncode: {result.returncode}\n"
            f"stdout: {result.stdout!r}\n"
            f"stderr: {result.stderr!r}"
        )


def test_echo_prompt(binary: str) -> None:
    """Phase A: verify the user prompt is serialized into the JSON body."""
    result, server = run_phase_a_case(
        binary,
        scenario="echo_user",
        user_input="say hello world\n",
    )
    assert_not_killed_by_signal(result)
    if server.last_payload is None:
        raise AssertionError(
            "mock server did not record a valid request payload\n"
            f"returncode: {result.returncode}\n"
            f"stdout: {result.stdout!r}\n"
            f"stderr: {result.stderr!r}"
        )
    user_message = server.last_payload["messages"][1]["content"]
    if user_message != "say hello world":
        raise AssertionError(
            f"expected user prompt 'say hello world', got: {user_message!r}"
        )


def test_request_format(binary: str) -> None:
    """Phase A: the mock should accept the full HTTP request format."""
    result, server = run_phase_a_case(
        binary,
        scenario="request_format_check",
        user_input="check my format\n",
    )
    assert_not_killed_by_signal(result)
    if server.request_count != 1:
        raise AssertionError(
            "mock server rejected the request format\n"
            f"returncode: {result.returncode}\n"
            f"stdout: {result.stdout!r}\n"
            f"stderr: {result.stderr!r}"
        )


# ---------------------------------------------------------------------------
# Exported test cases
# ---------------------------------------------------------------------------


def get_cases():
    return [
        TestCase(name="connect_basic", phase="a", points=17, func=test_connect_basic),
        TestCase(name="echo_prompt", phase="a", points=17, func=test_echo_prompt),
        TestCase(name="request_format", phase="a", points=16, func=test_request_format),
    ]
