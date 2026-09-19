# Ultimate Tic-Tac-Toe recovery review

Reviewed 14 September 2026. Existing source and recovered snapshots were left unchanged. Build outputs and diagnostic harnesses were placed in /tmp. This directory currently has no Git repository.

## Rules and protocol

The open challenge page confirms classic tic-tac-toe in the introductory league and Ultimate Tic-Tac-Toe from Bronze. The logged-in page currently displays Gold, 256th / 1,388.

The published CodinGame referee implements nine small 3×3 boards. Winning a small board claims its corresponding master-board square. Three claimed squares in a master-board line win the game. The local position of a move directs the opponent to the corresponding small board: global (row, col) directs to (row % 3, col % 3). If that board is won or full, the opponent can play in any remaining open board. Drawn small boards do not belong to either player. When no legal moves remain, the player with more won small boards wins; equal counts draw. The local board engine includes this count-based ending.

Input each turn: opponent row/column (initially -1 -1), legal-action count, then that many row/column pairs. Output a legal row/column pair followed by a newline; flush stdout. Internally the bots use x=column and y=row, and maintain their own perspective as player 0. The rig adds -2 -2 as a private termination message. The published referee gives each player's first turn 1,000 ms and later turns 100 ms; the recovered timed bots use a fixed 95 ms budget. The referee shuffles legal-action order.

Sources: [challenge](https://www.codingame.com/multiplayer/bot-programming/tic-tac-toe), [published referee](https://github.com/CodinGame/game-ultimate-tictactoe/blob/master/src/main/java/com/codingame/game/Referee.java), [staff explanation of count-based ending](https://forum.codingame.com/t/ultimate-tic-tac-toe-puzzle-discussion/22616). The published source is evidence for the rules, not proof that the deployed server runs exactly that revision.

## Inventory

| File | Role and present condition |
| --- | --- |
| orig.c | Self-contained editor snapshot, including its own old board engine and hash table. Compiles; opening smoke test returns row 1, column 0, matching its forced internal opening (0,1). Preserve as the historical baseline. |
| cg_tictac.c | Older amalgamated submission snapshot, related to orig.c but different. Compiles, but crashed on the opening smoke test with local GCC 16.2.1. Not the output of today's modular source. |
| ai_minimax.c | Most evolved modular bot for the current board.h. Compiles with -march=native but fails its opening smoke test. Hybrid opening heuristics, timed search, and endgame solving. |
| ai_base.c | Historical fixed opponent for the rig. Older board API; fails to compile against current board.h. Also includes hashmap.h before declaring its timing function. |
| ai_heuristic.c | Earlier bot with rule heuristics and fixed shallow search. Despite its name, includes recursive search. Fails against current board API. |
| ai_random.c | Standalone random legal-action baseline; compiles. |
| board.h | Shared current engine: base-3 encoding of each small board, bitboards, precomputed evaluation tables, and bitmask move iteration. Contains function definitions and globals, so it is designed for inclusion in one translation unit per executable. |
| hashmap.h | Fixed-capacity chained hash table used as a search cache; 20-byte keys, preallocated storage. Depends on the including program's getElaspedTime(). |
| gamerig.c | POSIX fork/pipe match runner with local referee logic. Compiles; hardcodes ai_minimax versus ai_base. Has match, opening-sweep, and coordinate weight-tuning routines; only ordinary matches are enabled in main. |
| tictac.c | Earlier bot using a still older Board3-based API; fails against current board.h. |
| bases.c | Standalone base-3 board-table experiment. Its nine-element counter can overrun on the final increment. |
| test.c | Coordinate conversion and winning-square table diagnostics. Does not exercise moveNext(), so misses the key iterator defect. Needs header/include cleanup for modern builds. |
| hashtest.c | Hash-map experiment; missing the timing function required by current hashmap.h. |
| simd_test.c | SIMD memory-fill benchmark, rather than a game bot. |

## Submission merger survives

subst reads ai_minimax.c and replaces lines mentioning board.h or hashmap.h with those files' contents, prefixing #define CG_GAME. It is a simple textual amalgamator, not a generic recursive C dependency merger. The current merged output is 58,842 bytes; orig.c is 45,106 and recovered cg_tictac.c is 47,062 bytes.

A safe way to inspect a new submission without replacing the historical snapshot is:

```sh
bash subst > /tmp/submission.c
gcc -Wall /tmp/submission.c -o /tmp/submission
```

The makefile has a cg_tictac.c generation rule, but omits subst from its dependencies. bin/ is missing and no target creates it. all builds only gamerig and ai_minimax, although the rig also requires ai_base. The bin/cg_tictac rule copies to an obsolete ~/codingame path. The -t board test also references /home/paul/tictac/initboard.txt. Do not regenerate cg_tictac.c in place until the recovered version is archived.

## How the main bot works

Each small board is encoded as a base-3 integer in 0..19,682. Tables map that to two nine-bit occupancy masks and cached line/threat evaluations. The master board has the same encoding, with a separate mask for closed boards. Won small boards become all-one-player sentinel boards; drawn boards are blanked but marked closed.

ai_minimax starts with hand-written tactical priorities while at least 72 playable squares remain. It then searches with heuristic evaluation at depth parameters 1, 3, and 5, representing 2, 4, and 6 plies including the candidate root move. With roughly 18–19 playable squares left it attempts terminal minimax, falling back to timed shallow search. These are minimax variants with early exits on winning results; there is no general alpha-beta bound propagation or MCTS implementation here.

The six weights reward master-board two/one-in-line patterns, small-board two/one-in-line patterns, and potential master-board lines inferred from small-board threats. Weight tuning is coordinate hill climbing against one fixed bot. It is useful experiment scaffolding, not a robust measure of arena strength.

## Findings to address before performance work

1. **Current move iterator is broken (board.h:86).** moveNext() uses __builtin_clz(m) as a bit offset. For bit 0 it yields 31, producing (1,10) instead of (0,0). __builtin_ctz() is already used correctly in cell2pos(). A temporary harness reproduced the failure for all nine bit positions. This affects opening heuristics, single-move selection and endgame search. The opening smoke test exits with `Moves2 next underflow` and no stdout move.
2. **Search-cache identity is incomplete (ai_minimax.c:348,561; orig.c:816,988).** cell[9] is uint16_t, so player * 0x10000 is discarded. The key also omits the directed next board and the closed-board mask (especially important because drawn boards are blanked), and shallow-search entries omit remaining depth. Identical stored board bytes can therefore refer to different legal moves, turns or search horizons. Clearing the cache between iterations does not make each iteration's entries safe.
3. **Interrupted depth iterations overwrite completed scores (ai_minimax.c:903; orig.c:1277).** Partial deeper results are compared with retained shallower results. Current modular code also leaves unvisited move slots initialized to (0,0) if the very first iteration is interrupted, so a default move could be selected. Commit a depth's result only after all root candidates finish; initialize a legal fallback immediately.
4. **Endgame timeout checks depend on terminal leaves (ai_minimax.c:334; orig.c:802).** Time is sampled only every 256 terminal positions, rather than reliably across search nodes. Fallback shallow search shares an already-spent budget. Sparse shallow checks, logging and post-search work also make the nominal 5 ms margin unreliable. Use a common deadline with controlled node sampling and always retain a valid completed result.
5. **Current heuristic overclaims a win (ai_minimax.c:448).** It assigns 149 when the directed small board occupies a master-board winning square, without checking that player 0 can actually win that small board on the next move. Scores over 100 are treated as real-win indications by iterative stopping, so this can end search prematurely. Heuristic sums also are not explicitly bounded below terminal scores, and cache packing assumes eight bits per player's score.
6. **Historical tactical branch is unreachable (orig.c:1190; cg_tictac.c:1224).** blocks_op_2 and op_win arrays are tested as pointers instead of indexing element i. !op_win is always false. GCC warns about this.
7. **Historical evaluation cache conflates contexts (orig.c:127).** Evaluations depend on both board encoding and the closed-square mask, but evaluate() writes all results into a table indexed only by board encoding. Later ordinary-board lookups can reuse a mask-dependent result. The current board.h instead precomputes ordinary states and recomputes closed-mask contexts.
8. **Rig cannot enforce arena behaviour reliably (gamerig.c:110,145,190).** Reads assume a complete output line arrives in one read; read errors can index bcache[-1], and parsing is unchecked. There is no bot-response timeout. Invalid moves are marked drawn but still applied to the board. Player arguments are cleared after game zero, so weight tuning and forced-opening sweeps apply the experiment only to the first game in a batch. This makes their reported results misleading. Also add reproducible seeds and balanced starts when recovering the rig.

## GCC optimization status

Your recollection is supported by the [2015–2016 optimization discussion](https://forum.codingame.com/t/optimisation-options/506), including the exact O3,omit-frame-pointer,inline string used in these files. More recent [April 2025 discussion](https://forum.codingame.com/t/spring-challenge-2025/206389?page=3) still explicitly describes C/C++ as unoptimized and recommends GCC pragmas. Other participants speculate about release builds, so this is community evidence rather than a verified current compiler command. No verified announcement of a default optimization change was found in this review.

[GCC documents](https://gcc.gnu.org/onlinedocs/gcc/Function-Specific-Option-Pragmas.html) that optimize pragmas apply to subsequent function definitions, including the inlined local headers here. Debug information (-g) and optimization are separate options: -g itself does not prevent -O3. Target pragmas enable CPU instructions; they do not verify the runtime CPU supports them. -march=native is appropriate for local benchmarking but cannot establish arena portability. The current modular file enables lzcnt,popcnt while board.h contains an SSSE3 intrinsic experiment; the recovered local recipe uses -march=native. A no-extra-flags merged build succeeded locally, but older compilers may behave differently.

Keep C/GCC and the existing O3 pragma while recovering correctness. A separate arena diagnostic can report __VERSION__, __OPTIMIZE__ and CPU features and benchmark with/without pragmas, without replacing the historical bot. This review did not edit or submit anything in the web IDE and did not verify its current compiler version.

## Validation and recovery order

Local GCC: 16.2.1, dated 2026-08-19. orig.c, cg_tictac.c, ai_minimax.c, the fresh amalgamation, gamerig.c and ai_random.c compiled. Historical ai_base.c, ai_heuristic.c and tictac.c failed due to incompatible recovered APIs. Opening tests: orig returned `1 0`; cg_tictac terminated with SIGSEGV; modular ai_minimax exited without a move. These are smoke tests, not strength measurements. No working full-match run was possible with the recovered default setup.

Recommended order: archive both submission snapshots and put the recovered tree under version control; repair build/merger paths; repair move iteration and add meaningful board-rule checks; correct cache identity and completed-depth/deadline handling; recover a trustworthy rig and compare orig, the repaired modular bot and random baseline. Then profile and consider alpha-beta ordering or an MCTS challenger. Preserve the historical bots as distinct versions rather than silently replacing their old board engines with current board.h.

## Local rig recovery update

The subsequent rig repair preserves orig.c byte-for-byte and leaves all bot optimization pragmas intact. make now creates bin/, builds orig and random alongside the modular bot, and the rig defaults to orig versus random with configurable executable paths and local timeout. Pipe I/O handles partial lines, malformed responses, EOF and deadlines; invalid moves forfeit without modifying the board; child processes are terminated and reaped. Batch arguments are no longer cleared after the first game. New amalgamations go to bin/submission.c, preserving recovered cg_tictac.c. The main bot correctness findings above remain outstanding. See README.md for usage.

Validation: two complete orig-versus-random games with alternating starts completed without failures, as did two random-versus-random games. Failure-path checks cover split output lines, invalid coordinates, malformed output, no response and early process exit. These checks validate the runner, not bot strength or arena performance.

## Three-version comparison update

VERSION_COMPARISON.md records the detailed comparison. The recovered cg_tictac.c startup crash is explained by its unconditional testRig call, which opens a missing historical absolute-path fixture without checking fopen. It then exits before the game loop, so this recovered amalgamation is an experimental build rather than a ready arena submission. Its recursive endgame timeout checks are commented out, unlike orig.c and the current modular source.
