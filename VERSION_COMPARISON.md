# Three recovered bot versions

Compared 14 September 2026. No bot sources or recovered snapshots were changed. `bash subst` was run into /tmp and confirmed byte-for-byte identical to `bin/submission.c`.

| Property | orig.c | Recovered cg_tictac.c | Current bin/submission.c |
| --- | --- | --- | --- |
| Provenance | Editor snapshot supplied by Paul | Recovered amalgamated file, consistent with old subst layout | Exact output of present subst using ai_minimax.c, board.h and hashmap.h |
| Size | 1,599 lines / 45,106 bytes | 1,668 lines / 47,062 bytes | 2,100 lines / 58,842 bytes |
| Startup | Enters game loop | Unconditionally calls testRig then exits | Enters game loop; test is optional with -t |
| Board encoding | Base-3 small boards, bitboard lookup | Same basic engine | Same encoding, expanded precomputed tables and faster conversions |
| Move representation used by bot | Array of 81 coordinate pairs | Same | Nine bitmasks with stateful iteration |
| Evaluation cache | Lazy, mutable, sentinel initialized | Same, optional metrics | Fully precomputed ordinary-board evaluations; special closed-mask contexts computed separately |
| Evaluation data | Plain char line counts | Unsigned char line counts | Unsigned counts plus free-square count and winning-move masks |
| Default weights | 29,10,4,1,7,3 | Same | Same |
| Forced opening, internal x/y | (0,1) | (0,1) | Disabled (-1,-1) |
| Timed shallow depth parameters | 1,3,5 | Same | Same; 2,4,6 plies including root move |
| Nominal move budget | 95 ms | 95 ms | 95 ms |
| Hash buckets / entry capacity | 1,280,000 / 512,000 | 128,000 / 512,000 | 128,000 / 512,000 |
| Source optimization pragma | O3,omit-frame-pointer,inline | Same plus redundant O3,inline pragma | O3,omit-frame-pointer,inline |
| Source CPU target pragma | bmi,lzcnt,popcnt | Same | lzcnt,popcnt |
| Observed condition | Full matches against random completed | Local-test startup crashes with missing fixture | Broken coordinate iterator prevents opening move |

## orig versus recovered merger output

These are closely related versions of the same hybrid bot, rather than different algorithms. Both use tactical opening rules, timed shallow minimax once fewer than 72 playable squares remain, and terminal minimax around 18–19 remaining squares. Both have the same weights and opening.

The most consequential differences in cg_tictac.c are:

- Its main unconditionally runs `testRig(); exit(0);` before argument parsing or the game loop (lines 1588–1589). testRig opens `/home/paul/tictac/initboard.txt` without checking fopen, so a missing fixture explains the observed startup crash. Even with that fixture restored, this file would run the local experiment and exit rather than play arena turns.
- Recursive endgame timeout checks are commented out. orig checks every 256 terminal leaves; the recovered output has no active recursive deadline check. Its root prediction can only act after a candidate's whole search finishes.
- Root endgame move ordering is disabled, though ordering inside recursive endgame search remains enabled. Shallow root ordering remains enabled in both.
- Shallow timing checks are sampled every 512 depth-zero calls, instead of checking every depth-zero call as orig does. This reduces timing overhead but increases potential deadline overshoot.
- Hash bucket count drops by a factor of ten while capacity stays the same: lower bucket memory/clearing cost, potentially longer lookup chains. This is a tradeoff, not established as an improvement.
- Line-count types become unsigned, evaluation adds an index check, metrics become optional, and input/weight reads gain error checks. Diagnostic routines and logging also change.

The on-disk cg_tictac.c timestamp is February 2022, but orig.c's timestamp reflects its recent paste. Neither timestamps nor similarity establish the exact historical sequence. We can identify these contents; we cannot prove the recovered amalgamation was the last-ever merger output or an actually submitted version.

## Recovered merger output versus current merger output

The current modular tree looks like a subsequent performance-oriented refactor of the same strategy. It is not a new MCTS or general alpha-beta bot.

The largest change is the board engine. Ordinary small-board evaluations are eagerly computed for all 19,683 ternary states. Reverse lookup tables map occupancy masks to ternary contributions, replacing repeated conversion loops. Evaluation records include free-square counts and immediate winning-square masks. Moves2 stores legal moves as nine masks; shallow recursion applies `(cell, bit)` directly instead of repeatedly turning coordinate pairs into board positions. There is also an SIMD conversion experiment in the shared header; its presence does not mean the production search uses that SIMD path.

Changes with playing implications:

- The forced (0,1) opening is disabled, so the opening heuristic selects a move instead.
- All active search ordering calls are disabled. Bitmask traversal also imposes its own order, discarding the supplied legal-action order. The broken moveNext currently prevents meaningful comparison of that behaviour.
- Shallow search checks terminal wins before consulting the transposition cache, returning explicit 150-point terminal results. The old versions consult the cache before handling terminal boards. This addresses one failure mode, but the cache identity is still incomplete.
- A new heuristic assigns 149 for a master-board winning destination without verifying a corresponding small-board win is available. This is not equivalent to a proven next-move win and can interact badly with the stopping rule that treats scores over 100 as decisive.
- The old array-as-pointer tactical condition is corrected by using each move's evaluation record.
- Recursive endgame timeout checks are active again, still only sampled at terminal leaves. Shallow checks remain sampled every 512 leaf calls.
- The current startup disables the unconditional test. It retains optional tests, including a comparison of both move iterators, but the test fixture path is obsolete.

## Shared problems and new regressions

All three retain incomplete transposition identity: the player bit is written outside a uint16_t's width, the forced destination and closed-board mask are omitted, and shallow entries omit remaining depth. All three overwrite prior-iteration scores with partial deeper results. These affect correctness, regardless of compile flags or pragma performance.

The two historical snapshots also have an evaluation cache that stores closed-mask-dependent results under a board-only index, and an unreachable tactical condition caused by testing arrays as pointers. The current refactor improves those areas but introduces the confirmed clz/ctz iterator defect and an unverified near-win heuristic. Its root move buffer can also retain default coordinates if an initial iteration is interrupted before visiting every candidate.

## Practical interpretation

Preserve all three as separate reference points: orig is the runnable historical baseline; cg_tictac is the historical experimental amalgamation; bin/submission.c records what the modular project produces now. Repairing the current modular version is sensible because its data representation is promising, but there is no evidence yet that it plays better or faster within the arena budget. A fair comparison requires explicit repaired variants, full legal matches, balanced starts and timing measurements. Local compiler flags do not establish server performance; the source pragmas above are the relevant submission differences.
