#!/usr/bin/env python3
"""Phase B public tests (50 pts): Robust response handling."""

from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from harness import TestCase, run_client

# ---------------------------------------------------------------------------
# Test functions
# ---------------------------------------------------------------------------


def test_fragmented_recv(binary: str) -> None:
    """Mock sends response in tiny chunks; client must reassemble correctly."""
    result = run_client(binary, scenario="fragmented", user_input="test fragmented\n")
    if result.returncode != 0:
        raise AssertionError(
            f"expected exit 0, got {result.returncode}\n" f"stderr: {result.stderr}"
        )
    if "fragmented delivery works" not in result.stdout:
        raise AssertionError(
            f"expected 'fragmented delivery works' in stdout, got: {result.stdout!r}"
        )


def test_non_200_status(binary: str) -> None:
    """HTTP 500 must fail cleanly and report the status."""
    result = run_client(binary, scenario="non_200", user_input="trigger error\n")
    if result.returncode == 0:
        raise AssertionError("expected non-zero exit on HTTP 500, got 0")
    if "500" not in result.stderr:
        raise AssertionError(f"expected '500' in stderr, got: {result.stderr!r}")


def test_no_content_length(binary: str) -> None:
    """Client must read until close when Content-Length is absent."""
    result = run_client(
        binary,
        scenario="no_content_length",
        user_input="test no length\n",
    )
    if result.returncode != 0:
        raise AssertionError(
            f"expected exit 0 when Content-Length is missing, got {result.returncode}\n"
            f"stderr: {result.stderr}"
        )
    if "no content length response" not in result.stdout:
        raise AssertionError(
            f"expected missing-length response text, got: {result.stdout!r}"
        )


# ---------------------------------------------------------------------------
# Exported test cases
# ---------------------------------------------------------------------------


def get_cases():
    return [
        TestCase(
            name="fragmented_recv", phase="b", points=17, func=test_fragmented_recv
        ),
        TestCase(name="non_200_status", phase="b", points=17, func=test_non_200_status),
        TestCase(
            name="no_content_length", phase="b", points=16, func=test_no_content_length
        ),
    ]
