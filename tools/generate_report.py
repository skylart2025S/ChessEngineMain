#!/usr/bin/env python3
"""
Phase 13: one-command regression + benchmark report generator.

Outputs a markdown report in docs/ so progress is easy to track.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import pathlib
import re
import subprocess
import sys
from typing import Tuple


def run_cmd(cmd: list[str]) -> Tuple[int, str]:
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    combined = (proc.stdout or "") + ("\n" + proc.stderr if proc.stderr else "")
    return proc.returncode, combined.strip()


def run_perft_script(py_exe: str, script_path: pathlib.Path, exe: pathlib.Path, max_depth: int) -> Tuple[bool, str]:
    code, out = run_cmd(
        [
            py_exe,
            str(script_path),
            "--exe",
            str(exe),
            "--max-depth",
            str(max_depth),
        ]
    )
    ok = code == 0 and "All perft regression checks passed." in out
    return ok, out


def run_benchmark_script(
    py_exe: str,
    script_path: pathlib.Path,
    exe: pathlib.Path,
    depths: str,
    times: str,
    plies: int,
    runs: int,
) -> Tuple[bool, str]:
    code, out = run_cmd(
        [
            py_exe,
            str(script_path),
            "--exe",
            str(exe),
            "--depths",
            depths,
            "--times",
            times,
            "--plies",
            str(plies),
            "--runs",
            str(runs),
        ]
    )
    header_ok = "depth,time_ms,runs,avg_nodes,avg_time_ms,avg_nps,avg_reached_depth" in out
    return code == 0 and header_ok, out


def make_summary_table(benchmark_output: str) -> str:
    lines = [ln.strip() for ln in benchmark_output.splitlines() if ln.strip()]
    csv_lines = [ln for ln in lines if re.match(r"^\d+,\d+,\d+,", ln)]
    if not csv_lines:
        return "_No benchmark rows parsed._"

    rows = ["| Depth | Time (ms) | Runs | Avg Nodes | Avg Time (ms) | Avg NPS | Avg Reached Depth |",
            "|---:|---:|---:|---:|---:|---:|---:|"]
    for line in csv_lines:
        depth, time_ms, runs, avg_nodes, avg_time, avg_nps, avg_depth = line.split(",")
        rows.append(
            f"| {depth} | {time_ms} | {runs} | {avg_nodes} | {avg_time} | {avg_nps} | {avg_depth} |"
        )
    return "\n".join(rows)

def parse_benchmark_rows(benchmark_output: str) -> list[dict[str, str]]:
    lines = [ln.strip() for ln in benchmark_output.splitlines() if ln.strip()]
    csv_lines = [ln for ln in lines if re.match(r"^\d+,\d+,\d+,", ln)]
    rows: list[dict[str, str]] = []
    for line in csv_lines:
        depth, time_ms, runs, avg_nodes, avg_time, avg_nps, avg_depth = line.split(",")
        rows.append(
            {
                "depth": depth,
                "time_ms": time_ms,
                "runs": runs,
                "avg_nodes": avg_nodes,
                "avg_time_ms": avg_time,
                "avg_nps": avg_nps,
                "avg_reached_depth": avg_depth,
            }
        )
    return rows

def append_history_csv(
    history_path: pathlib.Path,
    generated_at_iso: str,
    perft_ok: bool,
    bench_ok: bool,
    rows: list[dict[str, str]],
) -> None:
    history_path.parent.mkdir(parents=True, exist_ok=True)
    write_header = not history_path.exists()
    fieldnames = [
        "timestamp",
        "perft_ok",
        "bench_ok",
        "depth",
        "time_ms",
        "runs",
        "avg_nodes",
        "avg_time_ms",
        "avg_nps",
        "avg_reached_depth",
    ]
    with history_path.open("a", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        if write_header:
            writer.writeheader()
        for row in rows:
            writer.writerow(
                {
                    "timestamp": generated_at_iso,
                    "perft_ok": "1" if perft_ok else "0",
                    "bench_ok": "1" if bench_ok else "0",
                    **row,
                }
            )


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate combined perft + benchmark markdown report.")
    parser.add_argument("--python", default=sys.executable, help="Python executable to run helper scripts")
    parser.add_argument("--exe", default="build/chess_terminal.exe", help="Path to chess_terminal executable")
    parser.add_argument("--perft-max-depth", type=int, default=3, help="Perft max depth for regression checks")
    parser.add_argument("--depths", default="3,4", help="Benchmark depth list")
    parser.add_argument("--times", default="150,300,600", help="Benchmark time list (ms)")
    parser.add_argument("--plies", type=int, default=8, help="Benchmark plies")
    parser.add_argument("--runs", type=int, default=2, help="Benchmark runs per point")
    parser.add_argument("--output", default="docs/benchmark-report.md", help="Output markdown path")
    parser.add_argument("--history-csv", default="docs/benchmark-history.csv", help="Benchmark history csv path")
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parent.parent
    exe = (root / args.exe).resolve() if not pathlib.Path(args.exe).is_absolute() else pathlib.Path(args.exe)
    perft_script = root / "tools" / "perft_regression.py"
    bench_script = root / "tools" / "search_benchmark.py"
    output = (root / args.output).resolve() if not pathlib.Path(args.output).is_absolute() else pathlib.Path(args.output)
    history_csv = (root / args.history_csv).resolve() if not pathlib.Path(args.history_csv).is_absolute() else pathlib.Path(args.history_csv)

    if not exe.exists():
        print(f"[error] executable not found: {exe}")
        return 2
    if not perft_script.exists() or not bench_script.exists():
        print("[error] required helper scripts are missing in tools/")
        return 2

    perft_ok, perft_out = run_perft_script(args.python, perft_script, exe, args.perft_max_depth)
    bench_ok, bench_out = run_benchmark_script(
        args.python, bench_script, exe, args.depths, args.times, args.plies, args.runs
    )

    now_dt = dt.datetime.now()
    now = now_dt.strftime("%Y-%m-%d %H:%M:%S")
    now_iso = now_dt.isoformat(timespec="seconds")
    summary_table = make_summary_table(bench_out)
    benchmark_rows = parse_benchmark_rows(bench_out)
    if benchmark_rows:
        append_history_csv(history_csv, now_iso, perft_ok, bench_ok, benchmark_rows)

    report = f"""# Chess Engine Benchmark Report

Generated: {now}

## Configuration

- Executable: `{exe}`
- Perft max depth: `{args.perft_max_depth}`
- Benchmark depths: `{args.depths}`
- Benchmark times (ms): `{args.times}`
- Benchmark plies: `{args.plies}`
- Benchmark runs: `{args.runs}`

## Results Snapshot

- Perft regression: **{"PASS" if perft_ok else "FAIL"}**
- Search benchmark: **{"PASS" if bench_ok else "FAIL"}**

## Benchmark Summary

{summary_table}

## Perft Output

```text
{perft_out}
```

## Benchmark Output

```text
{bench_out}
```
"""

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(report, encoding="utf-8")
    print(f"[ok] report written: {output}")
    if benchmark_rows:
        print(f"[ok] history updated: {history_csv}")
    return 0 if (perft_ok and bench_ok) else 1


if __name__ == "__main__":
    sys.exit(main())
