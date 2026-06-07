#!/usr/bin/env python3
"""
Simple perft regression checks for chess_terminal.

Pragmatic goals:
- fast feedback on move generation correctness
- easy to run locally and in CI
- no external dependencies
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys


EXPECTED_STARTPOS = {
    1: 20,
    2: 400,
    3: 8902,
    4: 197281,
}


def run_perft(exe_path: pathlib.Path, depth: int) -> int:
    proc = subprocess.run(
        [str(exe_path), "--perft", str(depth)],
        capture_output=True,
        text=True,
        check=False,
    )

    output = (proc.stdout or "") + ("\n" + proc.stderr if proc.stderr else "")
    match = re.search(r"perft\(depth=\d+\):\s*(\d+)", output)
    if proc.returncode != 0 or not match:
        raise RuntimeError(
            f"perft failed at depth {depth}\n"
            f"exit={proc.returncode}\n"
            f"output:\n{output}"
        )

    return int(match.group(1))


def main() -> int:
    parser = argparse.ArgumentParser(description="Run perft regression checks.")
    parser.add_argument(
        "--exe",
        default="build/chess_terminal.exe",
        help="Path to chess_terminal executable",
    )
    parser.add_argument(
        "--max-depth",
        type=int,
        default=3,
        help="Maximum test depth (default: 3)",
    )
    args = parser.parse_args()

    exe = pathlib.Path(args.exe)
    if not exe.exists():
        print(f"[error] executable not found: {exe}")
        return 2

    failures = 0
    for depth in sorted(EXPECTED_STARTPOS):
        if depth > args.max_depth:
            continue
        expected = EXPECTED_STARTPOS[depth]
        actual = run_perft(exe, depth)
        if actual != expected:
            failures += 1
            print(f"[FAIL] depth {depth}: expected {expected}, got {actual}")
        else:
            print(f"[ ok ] depth {depth}: {actual}")

    if failures:
        print(f"\n{failures} regression check(s) failed.")
        return 1

    print("\nAll perft regression checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
