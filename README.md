# Chess Project

C chess engine with SDL3 GUI.

## Features

- Bitboard board representation
- Legal move generation and validation
- Check, checkmate, stalemate, and draw detection
- Castling, en passant, and promotion (auto-queen)
- Draw rules:
  - threefold repetition
  - fifty-move rule
- Search:
  - negamax with alpha-beta pruning
  - iterative deepening
  - quiescence search
  - transposition table
  - killer/history move ordering
  - time-budgeted search
- GUI:
  - human vs human / human vs AI
  - live AI depth and think-time controls
  - hover and last-move highlights
  - AI search stats in sidebar

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run

- Terminal: `build/chess_terminal.exe`
- GUI: `build/chess_gui.exe`

## GUI Controls

- `A` toggle AI mode
- `C` switch AI color
- `-` / `+` decrease/increase AI depth
- `,` / `.` decrease/increase AI think time (ms)
- `ESC` quit


## Tooling

- Perft regression:
  - `python tools/perft_regression.py --exe build/chess_terminal.exe --max-depth 3`
- Search benchmark matrix:
  - `python tools/search_benchmark.py --exe build/chess_terminal.exe --depths 3,4 --times 150,300,600 --plies 8 --runs 2`
- Combined report + history:
  - `python tools/generate_report.py --exe build/chess_terminal.exe --perft-max-depth 3 --depths 3,4 --times 150,300,600 --plies 8 --runs 2`
  - Report: `docs/benchmark-report.md`
  - History: `docs/benchmark-history.csv`
- Static graphs (matplotlib):
  - `python tools/performance_graphs.py --csv docs/benchmark-history.csv --out-dir docs`
  - Output: `docs/performance_nps.png`, `docs/performance_time.png`
- Live graph while playing (matplotlib):
  - Run GUI and, in another terminal:
  - `python tools/live_performance_plot.py --csv docs/live-ai-performance.csv --last 40`
  - GUI logs AI telemetry to `docs/live-ai-performance.csv`

## Python Dependencies

```bash
pip install matplotlib
```

If you have multiple Python installations, install with:

```bash
python -m pip install matplotlib
```
