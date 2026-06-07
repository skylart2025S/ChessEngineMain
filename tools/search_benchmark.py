#!/usr/bin/env python3
"""
Search benchmark runner for chess_terminal --bench.

Runs multiple depth/time combinations and summarizes throughput.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import statistics
import subprocess
import sys


BENCH_LINE = re.compile(
    r"BENCH ply=(?P<ply>\d+) side=(?P<side>\w+) move=(?P<move>\w+) "
    r"score=(?P<score>-?\d+) depth=(?P<depth>\d+) nodes=(?P<nodes>\d+) time_ms=(?P<time>\d+)"
)


def parse_csv_ints(raw: str) -> list[int]:
    values: list[int] = []
    for item in raw.split(","):
        item = item.strip()
        if not item:
            continue
        values.append(int(item))
    if not values:
        raise ValueError("no numeric values provided")
    return values


def run_once(exe: pathlib.Path, depth: int, time_ms: int, plies: int) -> dict[str, float]:
    proc = subprocess.run(
        [str(exe), "--bench", str(depth), str(time_ms), str(plies)],
        capture_output=True,
        text=True,
        check=False,
    )
    output = (proc.stdout or "") + ("\n" + proc.stderr if proc.stderr else "")
    if proc.returncode != 0:
        raise RuntimeError(f"benchmark failed (exit={proc.returncode})\n{output}")

    nodes = 0
    elapsed = 0
    reached_depth = 0
    parsed = 0

    for line in output.splitlines():
        m = BENCH_LINE.search(line)
        if not m:
            continue
        parsed += 1
        nodes += int(m.group("nodes"))
        elapsed += int(m.group("time"))
        reached_depth = max(reached_depth, int(m.group("depth")))

    if parsed == 0:
        raise RuntimeError(f"no BENCH lines found\n{output}")

    nps = (nodes * 1000.0 / elapsed) if elapsed > 0 else 0.0
    return {
        "plies": float(parsed),
        "depth": float(reached_depth),
        "nodes": float(nodes),
        "time_ms": float(elapsed),
        "nps": nps,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Run search benchmark matrix.")
    parser.add_argument("--exe", default="build/chess_terminal.exe", help="Path to chess_terminal")
    parser.add_argument("--depths", default="3,4", help="Comma-separated depths")
    parser.add_argument("--times", default="150,300,600", help="Comma-separated time budgets (ms)")
    parser.add_argument("--plies", type=int, default=8, help="Plies per benchmark run")
    parser.add_argument("--runs", type=int, default=2, help="Runs per (depth,time) pair")
    args = parser.parse_args()

    exe = pathlib.Path(args.exe)
    if not exe.exists():
        print(f"[error] executable not found: {exe}")
        return 2

    depths = parse_csv_ints(args.depths)
    times = parse_csv_ints(args.times)

    print("depth,time_ms,runs,avg_nodes,avg_time_ms,avg_nps,avg_reached_depth")
    for depth in depths:
        for time_ms in times:
            samples = [run_once(exe, depth, time_ms, args.plies) for _ in range(args.runs)]
            avg_nodes = statistics.mean(s["nodes"] for s in samples)
            avg_time = statistics.mean(s["time_ms"] for s in samples)
            avg_nps = statistics.mean(s["nps"] for s in samples)
            avg_depth = statistics.mean(s["depth"] for s in samples)
            print(
                f"{depth},{time_ms},{args.runs},"
                f"{avg_nodes:.0f},{avg_time:.0f},{avg_nps:.0f},{avg_depth:.2f}"
            )

    return 0


if __name__ == "__main__":
    sys.exit(main())
