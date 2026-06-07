#!/usr/bin/env python3
"""
Live matplotlib chart for AI performance while playing GUI games.

Reads docs/live-ai-performance.csv and refreshes continuously.
"""

from __future__ import annotations

import argparse
import csv
import pathlib
from typing import Iterable

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation


def read_rows(path: pathlib.Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open("r", encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def tail(values: Iterable[float], limit: int) -> list[float]:
    vals = list(values)
    if limit <= 0:
        return vals
    return vals[-limit:]


def parse_float(row: dict[str, str], key: str) -> float:
    try:
        return float(row.get(key, "0"))
    except ValueError:
        return 0.0


def main() -> int:
    parser = argparse.ArgumentParser(description="Live AI performance plot")
    parser.add_argument("--csv", default="docs/live-ai-performance.csv", help="Live performance csv path")
    parser.add_argument("--last", type=int, default=40, help="Show last N moves")
    parser.add_argument("--interval-ms", type=int, default=1000, help="Refresh interval")
    args = parser.parse_args()

    csv_path = pathlib.Path(args.csv)

    fig, axes = plt.subplots(3, 1, figsize=(10, 8), sharex=True)
    fig.suptitle("Live AI Performance")

    def draw(_: int) -> None:
        rows = read_rows(csv_path)
        if not rows:
            for ax in axes:
                ax.clear()
                ax.text(0.5, 0.5, "No data yet", ha="center", va="center", transform=ax.transAxes)
                ax.grid(alpha=0.2)
            axes[2].set_xlabel("AI move index")
            fig.tight_layout()
            return

        moves = tail((parse_float(r, "move_number") for r in rows), args.last)
        nodes = tail((parse_float(r, "nodes") for r in rows), args.last)
        times = tail((parse_float(r, "time_ms") for r in rows), args.last)
        nps = tail((parse_float(r, "nps") for r in rows), args.last)

        plots = [
            (axes[0], nodes, "Nodes", "#4e79a7"),
            (axes[1], times, "Time (ms)", "#f28e2b"),
            (axes[2], nps, "NPS", "#59a14f"),
        ]

        for ax, y, ylabel, color in plots:
            ax.clear()
            ax.plot(moves, y, marker="o", linewidth=1.7, color=color)
            ax.set_ylabel(ylabel)
            ax.grid(alpha=0.25)

        axes[2].set_xlabel("AI move index")
        fig.tight_layout()

    anim = FuncAnimation(fig, draw, interval=max(args.interval_ms, 200), cache_frame_data=False)
    plt.show(block=True)
    _ = anim
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
