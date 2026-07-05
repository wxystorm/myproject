#!/usr/bin/env python3
"""Unified test runner for the shell lab (public tests only).

Usage:
    python3 tests/run_tests.py [--bin PATH] [--phase {a,b,c}] [--json]
"""

from __future__ import annotations

import argparse
import importlib
import importlib.util
import json
import os
import sys

# ---------------------------------------------------------------------------
# Make sure the tests/ package is importable regardless of cwd.
# ---------------------------------------------------------------------------
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)

from harness import TestCase, TestResult, run_cases  # noqa: E402

# ---------------------------------------------------------------------------
# Collect public test modules.
# ---------------------------------------------------------------------------

_PUBLIC_DIR = os.path.join(_HERE, "public")


def _import_module(path: str):
    """Import a Python file by path and return the module object."""
    name = os.path.splitext(os.path.basename(path))[0]
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def collect_public_cases() -> list[TestCase]:
    cases: list[TestCase] = []
    for phase in ("a", "b", "c"):
        path = os.path.join(_PUBLIC_DIR, f"test_phase_{phase}.py")
        if os.path.isfile(path):
            mod = _import_module(path)
            cases.extend(mod.get_cases())
    return cases


# ---------------------------------------------------------------------------
# Formatting helpers.
# ---------------------------------------------------------------------------

_PHASE_LABELS = {"a": "Phase A", "b": "Phase B", "c": "Phase C"}
_SEPARATOR = "\u2501" * 50  # ━


def _format_text(results: list[TestResult]) -> str:
    lines: list[str] = []

    for r in results:
        tag = "[PASS]" if r.passed else "[FAIL]"
        label = f"phase_{r.case.phase} :: {r.case.name}"
        pts = f"({r.case.points:>2} pts)"
        lines.append(f"{tag}  {label:<45s} {pts}")
        if not r.passed and r.message:
            for mline in r.message.splitlines():
                lines.append(f"        {mline}")

    lines.append("")
    lines.append(_SEPARATOR)

    # Per-phase summary.
    phase_earned: dict[str, int] = {}
    phase_total: dict[str, int] = {}
    for r in results:
        p = r.case.phase
        phase_total.setdefault(p, 0)
        phase_earned.setdefault(p, 0)
        phase_total[p] += r.case.points
        if r.passed:
            phase_earned[p] += r.case.points

    for phase in ("a", "b", "c"):
        if phase in phase_total:
            earned = phase_earned[phase]
            total = phase_total[phase]
            label = _PHASE_LABELS.get(phase, phase)
            lines.append(f"{label}:  {earned:>3} / {total}")

    lines.append(_SEPARATOR)

    total_earned = sum(phase_earned.values())
    total_possible = sum(phase_total.values())
    lines.append(f"Total:    {total_earned:>3} / {total_possible}  (public)")
    lines.append("")
    return "\n".join(lines)


def _format_json(results: list[TestResult]) -> str:
    entries = []
    for r in results:
        obj = {
            "phase": r.case.phase,
            "test": r.case.name,
            "points": r.case.points if r.passed else 0,
            "max_points": r.case.points,
            "passed": r.passed,
        }
        if not r.passed and r.message:
            obj["message"] = r.message
        entries.append(json.dumps(obj))
    return "\n".join(entries)


# ---------------------------------------------------------------------------
# Main.
# ---------------------------------------------------------------------------


def main() -> None:
    parser = argparse.ArgumentParser(description="Run shell lab public tests.")
    parser.add_argument(
        "--bin", default="./shell", help="Path to the shell binary (default: ./shell)"
    )
    parser.add_argument(
        "--phase", choices=["a", "b", "c"], help="Run only the given phase"
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Output JSON Lines instead of human-readable text",
    )
    args = parser.parse_args()

    binary = args.bin

    cases = collect_public_cases()
    if args.phase:
        cases = [c for c in cases if c.phase == args.phase]

    results = run_cases(cases, binary)

    if args.json:
        print(_format_json(results))
    else:
        print(_format_text(results))

    any_failed = any(not r.passed for r in results)
    sys.exit(1 if any_failed else 0)


if __name__ == "__main__":
    main()
