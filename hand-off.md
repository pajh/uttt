# Ultimate Tic-Tac-Toe Negamax — Codex Hand-off

## Goal

Improve and measure the negamax bot’s search efficiency under a production-shaped budget, without relying on noisy wall-clock timings.

The current experimental budget is approximately **520,000 heuristic leaf evaluations per move**, intended to resemble the work an optimized bot performs in roughly 90 ms.

## Current search design

- Iterative-deepening negamax.
- A completed iteration is published; ordinary results from an interrupted deeper iteration are discarded.
- Exact root proofs found during search may still have value.
- `evaluation_calls` is incremented only inside `score_board()`, so it counts actual heuristic leaf evaluations.
- The evaluator’s old one-move “peek ahead” is no longer in the search path.
  - `has_certified_immediate_win()` still exists but currently has no callers and is dead code.
  - `certifiedScore()` remains legitimate exact-proof machinery after searched moves.
- Terminal/proven scores use values such as `+60000`.
- Earlier work found and fixed a positional `ply` / `stop_at_ply` configuration bug.

## Current transposition-table design

Baseline:

```c
#define TT_BITS 16          // 65,536 direct-mapped entries, about 1 MiB
#define TT_MIN_PLY 2
#define TT_MIN_TO_GO 2
```

The TT uses direct overwrite. Lookup and store share one gate:

```c
to_go = config.stop_at_ply - config.ply;

use_tt =
    config.ply >= TT_MIN_PLY &&
    to_go >= TT_MIN_TO_GO;
```

The board hash should only be calculated when `use_tt` is true.

Meaning:

- Do not use the TT very near the root.
- Do not use it when fewer than two searched moves remain before evaluation.
- This avoids large amounts of low-value, near-horizon TT traffic.

Telemetry has eight ply buckets:

```text
bucket 0 = ply 1
...
bucket 6 = ply 7
bucket 7 = ply 8+
```

### TT results so far

Table-size testing showed:

```text
TT_BITS  slots     size       hits     collisions
15       32,768    ~512 KiB   32,037   467,110
16       65,536    ~1 MiB     33,267   284,609
17       131,072   ~2 MiB     33,866   158,824
```

Doubling from 16 to 17 bits greatly reduced collisions but produced only about **1.8% more hits**. Most collision overwrites therefore appear harmless. `TT_BITS=16` is the current sweet spot.

With the `2,2` gate, compared with unrestricted TT use:

```text
probes:      1,120,914 -> 142,676   (-87.3%)
stores:      1,087,542 -> 140,750   (-87.1%)
collisions:    284,609 ->   5,495   (-98.1%)
```

Only 5.6% of hits were retained, but most discarded hits were close to the leaf and may have saved little work. The fixed-evaluation experiment does not establish wall-clock speed; a real optimized 90 ms A/B test is still needed.

## Measurement framework

The important question is not merely “How many evaluations did we perform?” but:

> How much usable search result did 520,000 evaluations purchase?

Value hierarchy:

1. Exact win/loss/draw proof.
2. Deeper fully completed iteration.
3. Progress through the next iteration, useful diagnostically.
4. Otherwise, discarded work—“spaniel-equivalent heat.”

Desired per-move metrics:

```c
u32 ply_step;
u32 last_completed_ply;
u32 evals_at_last_completed;
u32 attempted_ply;
u32 roots_in_attempt;
u32 roots_completed_in_attempt;

u64 root_wins_proved;
u64 root_draws_proved;
u64 root_losses_proved;
```

Useful derived value:

```text
tail_evaluations =
    evaluations - evals_at_last_completed
```

This distinguishes delivered value from budget consumed by an unfinished iteration.

Current telemetry already includes explicit `attempted_ply` and `completed_ply`; the most important missing field is now:

```c
evals_at_last_completed
```

Root progress and proof counters remain desirable.

## `+1 ply` iterative-deepening experiment

Previous policy:

```text
4 -> 6 -> 8 -> ...
```

Experimental policy:

```text
4 -> 5 -> 6 -> 7 -> ...
```

The aim was to determine whether single-ply steps convert otherwise discarded search into a completed intermediate result.

### Result: efficiency hypothesis confirmed

Against `orig.c`, under the same approximately 520k evaluation cap:

```text
moves 2–22: completed 6, attempted 7
moves 24–32: completed 5, attempted 6
move 34:     completed 4, attempted 5
move 36:     completed 5, attempted 6
moves 38–40: completed 6, attempted 7
move 42:     completed 10, attempted 11
```

The critical middle-game comparison was:

```text
old +2 policy: completed ply 4, failed during ply 6
new +1 policy: completed ply 5, failed during ply 6
```

Thus `+1` recovered a genuine completed ply-5 result that the old policy skipped. Early in the game, adding the intermediate pass did not reduce the final completed depth: both policies still reached ply 6.

### Clean behavioural divergence

The two games followed the same moves through move 29. The first move divergence was move 30:

```text
old +2: chose (4,7), score -112
new +1: chose (5,7), score +898
```

At this position, `+1` published ply 5 while the older policy had only published ply 4. This is an unusually clean A/B event: same position, different search policy, different move.

The `+1` game was still a win, ending after 54 moves by `CountVictory`. The earlier `+2` game won in 43 moves by a U-board three-in-a-row. One game is not enough to compare strength.

### Important odd/even effect

Before the first move divergence, the selected moves often remained unchanged while their scores swung dramatically:

```text
move 24: -108 ->  +800
move 26: +337 -> +1157
move 28: -169 -> +1145
```

This suggests a substantial odd/even horizon effect. It does not mean odd-depth negamax is incorrect, but it means completed ply 5 is not automatically proven stronger than completed ply 4. Search efficiency and playing strength must be evaluated separately.

### Late-game proof behaviour

At move 42, the bot completed ply 10 and entered ply 11 using only 487,229 evaluations. That is also where the game first displayed a forced-win score of `60000`.

Afterwards, proofs became extremely cheap:

```text
move 44: 1,423 evaluations
move 46:    57
move 48:    51
moves 50–52: 0
```

This demonstrates why raw evaluation count is a poor success measure: a small search that proves a win can be more valuable than a full 520k search that merely refines a heuristic score.

## `replay_bot` idea

`orig.c` is stronger than the random opponent, but its timing behaviour can make games diverge even when the experimental bot has not changed.

Create a deliberately simple `replay_bot`:

- Store a known game transcript.
- When the opponent plays the expected move, return the recorded `orig.c` response.
- Continue while the transcript matches.
- In strict A/B mode, abort as soon as the experimental bot deviates.
- Optionally support a second mode that falls back to live `orig.c` after divergence.

Strict abort is preferred for measurement because it guarantees identical positions until the first experimental difference. The divergence itself is useful output.

An even stronger later test harness would capture 50–100 real positions and run every configuration independently from those exact positions with the same evaluation cap.

## Immediate TODOs

1. **Add `evals_at_last_completed`.**
   - Record `evaluation_calls` whenever an entire target depth completes.
   - Report `tail_evaluations`.

2. **Complete the per-move search instrumentation.**
   - `roots_in_attempt`
   - `roots_completed_in_attempt`
   - root win/draw/loss proof counters
   - retain `attempted_ply` and `completed_ply`

3. **Implement `replay_bot`.**
   - Start with strict transcript replay and abort-on-divergence.
   - Use it to reproduce the move-30 position and other interesting paths.

4. **Repeat `+1` versus `+2` from fixed positions.**
   Measure separately:
   - completed ply;
   - evaluations required to complete it;
   - tail evaluations;
   - progress through the failed next iteration;
   - exact root proofs;
   - selected move and score.

5. **Investigate odd/even score oscillation.**
   - Prioritize moves 24, 26, 28 and 30.
   - Compare ply-4, ply-5 and ply-6 root choices/scores from identical positions.
   - Do not assume odd ply is stronger solely because it is deeper.

6. **Run the outstanding production-speed A/B test.**
   - Optimized `-O3` build.
   - Real 90 ms deadline.
   - `TT_BITS=16`.
   - Compare unrestricted `1,1` against gated `2,2`.
   - Record evaluations and deepest completed ply per move.

7. **Later: build a fixed-position corpus.**
   - Capture 50–100 positions from real games.
   - Compare all policies at exactly 520k evaluations.
   - Eventually use a very deep offline search as a reference for decision quality.

## Current conclusion

The `+1 ply` policy is already justified as a **search-budget policy**: it converts work that the `+2` scheme discarded into completed intermediate-depth results, without visibly sacrificing early-game completed depth in this run.

What remains unknown is whether the odd-depth result is consistently a stronger decision. The large parity-related score swings make fixed-position testing—ideally through `replay_bot`—the immediate priority.