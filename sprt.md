# SPRT summary: `build/sprt-20260531-211051.pgn`

**gbchess-new** (new) vs **gbchess-base** (base) — all numbers from new's perspective.

| Games | W | D | L | Points | Score % | Elo (pt est.) | Draw % |
|---|---|---|---|---|---|---|---|
| 8569 | 3235 | 2248 | 3086 | 4359.0 | 50.87 | +6.0 | 26.2 |

## Pentanomial (new pts per reversed-color pair)

| 0 | 0.5 | 1.0 | 1.5 | 2.0 | pairs |
|---|---|---|---|---|---|
| 182 | 467 | 2895 | 496 | 242 | 4282 |

Loss-leaning pairs: 649  vs  win-leaning pairs: 738  (ratio 1.14)

## New engine by color

| Color | W | D | L | Score % | Net |
|---|---|---|---|---|---|
| white | 1446 | 1273 | 1567 | 48.6 | -121 |
| black | 1789 | 975 | 1519 | 53.2 | +270 |

## WDL cause breakdown

| Cause | new WINS by | new LOSES by | draws by |
|---|---|---|---|
| fifty-move | 0 | 0 | 95 |
| insufficient-material | 0 | 0 | 355 |
| mate | 2487 | 2678 | 0 |
| repetition | 0 | 0 | 1793 |
| stalemate | 0 | 0 | 5 |
| time forfeit | 748 | 408 | 0 |
| **total** | 3235 | 3086 | 2248 |

*("new WINS by" = base lost by that cause; "new LOSES by" = new lost by that cause.)*

## Timeouts / crashes

| Engine | time forfeits |
|---|---|
| gbchess-base | 748 |
| gbchess-new | 408 |

## Eval-based diagnostics (decisive games)

| new threw (won eval, no win) | new robbed (lost eval, no loss) | median loser swing (cp) | p90 loser swing (cp) |
|---|---|---|---|
| 777 | 835 | 417 | 871 |

*threw/robbed use a +/-200cp self-eval threshold; swing = biggest single-move self-eval drop suffered by the losing side.*

## Per-opening (new score %, worst first)

| new % | n | FEN |
|---|---|---|
| 46.7 | 857 | 8/8/1p1k1p1p/3npp2/2B5/PP1K1PP1/7P/8 b - - 0 1 |
| 48.5 | 857 | 5rk1/3PQppp/p7/6P1/8/2pq4/P5PP/4R2K b - - 0 1 |
| 48.8 | 856 | R7/8/P7/5p2/5kp1/8/r5PK/8 w - - 0 1 |
| 49.5 | 856 | 8/ppp2ppp/2k1p3/4P3/1P1B1PP1/b1PK4/4P2P/8 b - - 0 1 |
| 50.0 | 858 | r2q1rk1/4N1bp/p2p2p1/2p3N1/Pp4P1/1Q5P/1P1n1P2/5RK1 b - - 0 1 |
| 50.0 | 858 | 7k/6R1/p6p/1p1p4/1P4Q1/P1r1q3/4p1PP/4Rr1K w - - 0 1 |
| 50.0 | 858 | 6k1/ppr2p2/2r1pBp1/4P1P1/2PpR3/1P6/P4P2/7K b - - 0 1 |
| 51.8 | 856 | 7k/1pp5/pb1p2Bp/4p1rP/PP2P1r1/2PP4/5PP1/R4RK1 w - - 0 1 |
| 53.7 | 858 | 8/5Q2/4p1pk/2p5/P1q1p1P1/4P2K/8/8 b - - 0 1 |
| 59.8 | 855 | 8/8/8/7p/p1b5/P5kP/2p3P1/2B3K1 b - - 0 1 |

## Game length (plies)

| bucket | n | mean | median | min | max |
|---|---|---|---|---|---|
| new_win | 3235 | 55 | 47 | 2 | 319 |
| new_loss | 3086 | 57 | 55.0 | 2 | 343 |
| draw | 2248 | 59 | 55.0 | 8 | 296 |


---

