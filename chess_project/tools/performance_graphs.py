#!/usr/bin/env python3
"""
Generate static matplotlib graphs from benchmark history CSV.
"""

from __future__ import annotations

import argparse
import csv
import pathlib
from collections import defaultdict

import matplotlib.pyplot as plt


def load_rows(csv_path: pathlib.Path) -> list[dict[str, str]]:
    if not csv_path.exists():
        raise FileNotFoundError(f"history csv not found: {csv_path}")
    with csv_path.open("r", encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def make_series(rows: list[dict[str, str]], value_key: str) -> dict[str, list[float]]:
    grouped: dict[str, list[float]] = defaultdict(list)
    for row in rows:
        key = f"d{row.get('depth','?')}-t{row.get('time_ms','?')}"
        try:
            grouped[key].append(float(row.get(value_key, "0")))
        except ValueError:
            grouped[key].append(0.0)
    return grouped


def plot_series(series: dict[str, list[float]], ylabel: str, title: str, out_path: pathlib.Path) -> None:
    plt.figure(figsize=(10, 5))
    for label, values in sorted(series.items()):
        x = list(range(1, len(values) + 1))
        plt.plot(x, values, marker="o", linewidth=1.8, label=label)
    plt.title(title)
    plt.xlabel("Run index")
    plt.ylabel(ylabel)
    plt.grid(alpha=0.25)
    if series:
        plt.legend(ncol=2, fontsize=8)
    plt.tight_layout()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(out_path, dpi=140)
    plt.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate benchmark performance graphs.")
    parser.add_argument("--csv", default="docs/benchmark-history.csv", help="Input history csv")
    parser.add_argument("--out-dir", default="docs", help="Output directory for images")
    args = parser.parse_args()

    csv_path = pathlib.Path(args.csv)
    out_dir = pathlib.Path(args.out_dir)
    rows = load_rows(csv_path)
    if not rows:
        raise RuntimeError("no rows found in benchmark history csv")

    nps_series = make_series(rows, "avg_nps")
    time_series = make_series(rows, "avg_time_ms")

    plot_series(
        nps_series,
        "Average NPS",
        "Chess Engine Throughput Trend",
        out_dir / "performance_nps.png",
    )
    plot_series(
        time_series,
        "Average Time (ms)",
        "Chess Engine Time Trend",
        out_dir / "performance_time.png",
    )

    print(f"[ok] wrote {(out_dir / 'performance_nps.png')}")
    print(f"[ok] wrote {(out_dir / 'performance_time.png')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
