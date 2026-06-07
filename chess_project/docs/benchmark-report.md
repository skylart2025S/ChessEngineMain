# Chess Engine Benchmark Report

Generated: 2026-06-07 18:15:26

## Configuration

- Executable: `C:\msys64\home\skyla\projects\chess_project\build\chess_terminal.exe`
- Perft max depth: `3`
- Benchmark depths: `3,4`
- Benchmark times (ms): `150,300,600`
- Benchmark plies: `8`
- Benchmark runs: `2`

## Results Snapshot

- Perft regression: **PASS**
- Search benchmark: **PASS**

## Benchmark Summary

| Depth | Time (ms) | Runs | Avg Nodes | Avg Time (ms) | Avg NPS | Avg Reached Depth |
|---:|---:|---:|---:|---:|---:|---:|
| 3 | 150 | 2 | 13613 | 408 | 33366 | 3.00 |
| 3 | 300 | 2 | 13613 | 386 | 35282 | 3.00 |
| 3 | 600 | 2 | 13613 | 382 | 35797 | 3.00 |
| 4 | 150 | 2 | 35676 | 1212 | 29436 | 4.00 |
| 4 | 300 | 2 | 57482 | 2053 | 28000 | 4.00 |
| 4 | 600 | 2 | 65311 | 2342 | 27893 | 4.00 |

## Perft Output

```text
[ ok ] depth 1: 20
[ ok ] depth 2: 400
[ ok ] depth 3: 8902

All perft regression checks passed.
```

## Benchmark Output

```text
depth,time_ms,runs,avg_nodes,avg_time_ms,avg_nps,avg_reached_depth
3,150,2,13613,408,33366,3.00
3,300,2,13613,386,35282,3.00
3,600,2,13613,382,35797,3.00
4,150,2,35676,1212,29436,4.00
4,300,2,57482,2053,28000,4.00
4,600,2,65311,2342,27893,4.00
```
