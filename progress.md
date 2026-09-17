# Bot experiments

## 15 September 2026 morning summary

The recovered project is now a usable development system rather than a collection of historical fragments. `orig.c` remains the untouched point-in-time opponent. The current modular bot builds normally and through the `subst` single-file submission path. The process rig speaks the CodinGame stdin/stdout protocol, accepts quoted bot commands with arguments, verifies each bot's build identity and effective settings, distinguishes played wins from technical forfeits, records 3IAR/count finishes and exact-DFS failures, and supports four concurrent batches with compact combined CSV output. Local generated binaries, logs and raw runs are excluded from Git.

The current bot now searches from the opening instead of using the large deterministic opening-rule block. Its evaluator was rebuilt around named `f1`, `f2` and `fc` pieces: local and ultimate-board line strength, the importance of each sub-board to the ultimate board, and gated claimed-board count strength. Hard terminal handling now propagates proven 3IAR and count wins/losses through both shallow and exact minimax. Completed forced wins survive an interrupted iterative-deepening pass; completed forced losses exclude that root even if a deeper iteration later times out. The speculative cross-depth shallow-cache retention change was reverted because remaining depth is part of the cache key and the observed result did not demonstrate useful reuse.

Heat-map tooling can replay our turns from a recorded game as successive 9x9 boards. Every legal root move is searched to the same selected minimax depth, coloured by score and annotated with immediate `f1`/`f2`/count components. This exposed and fixed a legal-bitmask display bug, clarified occasions where the timed bot chose a move below the fixed-depth maximum, and gave a practical way to inspect synthetic positions without trying to infer evaluator quality solely from noisy match totals.

Exact-search entry thresholds were measured independently. We selected and locked primary 17 / narrow 19: exact search at 17 or fewer playable cells, plus the narrow case at 18 cells with fewer than 10 legal moves. USCALE 5 and count scale 0 are the pinned settings for the current opening experiment. These are working experimental choices rather than settled optimal values.

The repository is public at https://github.com/pajh/uttt. A GitHub Actions smoke run proved the hosted build, four-way execution, summaries and artifact upload. The active full run compares 100 games each for `BASE MM MD MC DM DD DC CM CD CC`, with our bot always player 0 and always starting. `M` means middle, `D` diagonal/corner and `C` cardinal/edge. Each forced policy independently samples an eligible outer grid and inner cell on every game; `BASE` lets minimax choose normally. Every exact opening and both bot identities/settings are recorded. The local smoke confirmed that normal minimax selected grid 4/cell 4 (`MM`).

### Longer GitHub experiment: force the opponent's first reply

Extend the randomized opening classes to controlled two-ply positions. For a second-letter diagonal, classify the opponent's diagonal reply as `S` (same cell, sending us back to our starting grid), `O` (opposite corner, forming a line through the middle), or `A` (one of the two adjacent corners). Cardinal destinations have the same `S`/`O`/`A` relationships; middle has only `S`. This produces seven reply geometries per initial outer-grid class and 21 forced two-ply policies: `MMS`, `MDS/MDO/MDA`, `MCS/MCO/MCA`, and the equivalent seven under initial `D` and `C`.

Include a normal-response control for every first-move class as well as the completely normal `BASE`. A forced reply tells us whether the resulting position is good; its normal-response control tells us whether `orig` is inclined to enter it. Record our exact opening, the opponent's exact reply, its relationship, the board sent back to us, result and win type. At 100 games per policy this is a suitable longer hosted computation after the current one has been reviewed.

### TODO

- Check GitHub Actions run #3 (`https://github.com/pajh/uttt/actions/runs/34945301762`), download its artifact, review `summary.csv` and `openings.csv`, and record the result here before changing another lever.
- Explain how this repository's GitHub-hosted compute works: workflow triggers and branches, runner allocation and concurrency, time and storage limits, logs/artifacts, rerunning with manual inputs, billing for a public repository, and how to stop or avoid accidental expensive runs.
- Design the opponent-reply forcing so both bots consume the imposed moves through the ordinary protocol and retain valid internal state; use a small hosted smoke run before starting the 21-policy job.
- Revisit the `set9CB` arithmetic if lower-level optimisation becomes valuable: a 1M-call local hot-cache microbenchmark measured 24.35 ns/call for decode/mask/encode, versus 20.72 for `cell_code += (player+1)*rcache[bit]` and 20.89 for `pow3[ctz(bit)]`. Addition equivalence was checked for legal moves; this is microbenchmark-only evidence (full DFS/search impact unmeasured), so treat it as a deferred optimisation todo.

## Starting-move finder

Added a rig-level `--force-opening row,col` option. It fixes player 0 as the starter and presents exactly that one legal move on ply zero, so the tested bot consumes the forced move through its normal CodinGame protocol and retains its normal internal state. `find-starting-moves.py` tests all 81 openings with the same seed schedule and four concurrent games, verifies that bot identity/settings are identical across every opening, and emits both coordinate-level results and the 15 simultaneous outer/inner D4 symmetry classes. The manual GitHub Actions workflow defaults to 25 games per opening (2,025 total) with USCALE 5, count scale 0 and exact-search thresholds 17/19.

Revised the first hosted experiment to compare nine randomized category policies plus a normal baseline. `M` is centre, `D` diagonal/corner and `C` cardinal/edge. Each of `MM MD MC DM DD DC CM CD CC` randomly samples an eligible outer grid and eligible inner cell independently for every game; `BASE` lets the bot choose normally. Our bot is always player 0 and starts all 100 games per policy. Exact sampled/chosen coordinates are recorded. Total hosted run size is 1,000 games, with four policies concurrent.

GitHub run 34945301762 completed successfully in 10m37s: 1,000 games, zero technical failures, verified minimax build 18b9bb9cab810b40 versus historical orig build 21d1b83935be6da9. Scores: BASE 86.0%, MM 81.5%, MD 88.0%, MC 90.0%, DM 80.0%, DD 87.0%, DC 81.5%, CM 81.5%, CD 85.5%, CC 87.5%. BASE chose grid4/cell4 in all 100 games, confirming normal minimax opens MM. The 4.5-point BASE/MM difference despite the same resulting first-move board is a direct warning about run noise and/or the perturbation from presenting a single legal move. Extracted artifact is preserved under `published-results/run-34945301762/`.

The nine coarse policies contain 15 true simultaneous D4 symmetry classes. Same-category DD and CC split into same (`S`), opposite (`O`) and adjacent (`A`) outer-grid/inner-cell relationships. Mixed DC and CD split into near (`N`) and far (`F`). Existing sparse aggregation suggested DDA 91.7% versus DDO 76.2%, while CCO 92.9% versus CCA 83.3%, but subgroup sizes were only 21--48 and intervals overlap widely. Next hosted experiment therefore uses BASE plus `MM MD MC DM DDS DDO DDA DCN DCF CM CDN CDF CCS CCO CCA`, 200 games each (3,200 total), with equal sample size per symmetry and four concurrent policies.

## Control: repaired hybrid bot

Historical opponent: unchanged orig.c. Current hybrid: opening rules while at least 72 playable squares remain, existing timed shallow minimax thereafter, terminal endgame solver. Six evaluator weights: 29,10,4,1,7,3. No alpha-beta or hash-table performance tuning.

Initial pilot: **6 wins / 3 losses / 1 draw**, ten distinct traces; current starting first 5/0/0, starting second 1/3/1. No technical forfeits or nominal timing overruns. Mean game time 2.3 s. Evidence: results/pilot-games.csv, results/pilot-moves.csv, results/pilot.log. Original pilot used strict 1000/100 ms response limits; current rig allows 20% grace and warns above nominal limits.

## Experiment 01: existing minimax from the start

Hypothesis: existing position scoring plus shallow minimax can outperform the hand-written opening rules even without an opening-specific evaluator.

One lever: OPENING_SEARCH_THRESHOLD=82 instead of 72. With at most 81 playable squares this bypasses opening rules and invokes existing timed shallow minimax from the first turn. Endgame solver, evaluator, weights, depth parameters, source pragmas and budgets remain unchanged. Default source behaviour remains 72; the experiment is a separate executable.

Ten games against orig.c, alternating starts, seed schedule 20260914 onwards; 1200/120 ms local cutoffs with nominal-limit warnings. orig.c retains time/PID randomness, so the seed schedule cannot reproduce its choices exactly. Compare with pilot as exploratory feedback, not a paired statistical test.

Result: **6 wins / 3 losses / 1 draw**, matching the pilot's aggregate score. Starting first: 2/2/1; starting second: 4/1/0 (wins/losses/draws for current). All ten traces were distinct. Zero forfeits, timeouts or nominal-limit warnings. Mean game time 3.5 s, versus pilot 2.3 s. Maximum experimental first response 907.444 ms, subsequent response 90.331 ms. Evidence: results/experiment-01-source.c, results/experiment-01-games.csv, results/experiment-01-moves.csv, results/experiment-01.log. Build: gcc -Wall -g -DOPENING_SEARCH_THRESHOLD=82 results/experiment-01-source.c -o bin/ai_search_start.

Interpretation: no aggregate improvement in this small sample, and increased time cost. The starting-side breakdown changed considerably; that is a reason not to infer equivalence or superiority from ten unpaired games. Existing minimax can play the opening legally within budget, but this run does not justify replacing the rules.

Decision: retain threshold 72 as the default; keep this experiment available as a reference. Next hypothesis remains open; do not change weights or other opening rules as part of this experiment.

## Experiment 02: uniform opening selection versus cardinal preference

Hypothesis: restricting eligible opening development moves to local edges is less effective than uniform choice among the same candidates. One lever: define UNIFORM_OPENING_SELECTION to bypass the edge-only candidate pool in pickRandomFromBuffer. Rule priorities, threshold 72, weights, evaluator, search and budgets unchanged. Optional local telemetry counts mixed edge/non-edge pools and selected squares. Default retains edge preference. Ten experimental games versus orig, alternating starts, same rig seed schedule, 20% timing grace. Historical pilot remains exploratory control; orig's time/PID randomness prevents exact pairing.

Result: **5 wins / 5 losses / 0 draws**. Starting first: 4/1/0; starting second: 1/4/0 (current wins/losses/draws). Ten distinct game traces; zero technical failures, timeouts or nominal overruns. Mean game time 2.6 s. Current maximum first response 7.825 ms, later response 90.312 ms.

Telemetry: five multi-candidate selections, all at 81 playable squares (current's first move when starting first). Each pool had nine candidates, including four edges. Uniform selection chose three non-edge squares and two edges. No later opening turns or second-player games reached this multi-candidate selection function. At least three first moves therefore selected squares forbidden by the edge-preferring pool; this does not constitute a paired trajectory comparison.

Interpretation: score is lower than historical 6/3/1 pilot, but ten unpaired games and only five eligible decisions cannot establish a preference. This lever primarily tests first-move selection in these matches, not a pervasive early-game policy. Keep the cardinal preference as default; retain uniform variant for future comparisons.

Evidence: results/experiment-02-games.csv, results/experiment-02-moves.csv, results/experiment-02-opening.csv (seed,playable_squares,candidates,edge_candidates,x,y,selected_edge), results/experiment-02.log. Build: make bin/ai_uniform_opening. Normal source and generated submission retain edge preference; only this executable defines UNIFORM_OPENING_SELECTION.

## Correctness fix: retain completed forced-win proofs

The timed shallow root previously accepted results only after completing every candidate at a depth. This could discard an already completed forced-win proof if a later candidate timed out. It now immediately returns a candidate scored (TERMINAL_SCORE, 0), before examining other roots. The exact endgame root already returned immediately on a proven win. Recursive minimax in both paths already stops at the relevant terminal extremum; opponent nodes must establish that all replies lose before returning our forced win. Heuristic scores remain below terminal scores, and timeout sentinels are checked before accepting a proof.

Validation: board/search regression suite passes, including immediate root victory with exactly one visited search node, exact-solver immediate victory, and a three-ply fork not proven at the shallower depth but proven against every reply at the deeper depth. Rebuilt local bot and merged submission; merged submission compiles; orig.c checksum unchanged. No match score measured for this correctness fix. Forced-loss selection policy remains unchanged pending discussion.

## Experiment 03: persistent forced-loss proofs, minimax from the start

Root selection now remembers completed forced-loss proofs across iterative depths. Proven-losing roots are skipped at subsequent depths. Last completed iteration heuristic scores are retained per candidate; deeper loss proofs exclude candidates even when that iteration is interrupted. On return, choose the best retained score among roots without a loss proof. Preserve the existing selected move if still eligible, avoiding an extra random draw. If all supplied moves lose, return a legal fallback and stop further iterations. Completed forced wins still return immediately. Exact endgame solver and historical orig.c unchanged.

Validation: regression suite passes, including immediate and multi-ply forced wins, rejecting a losing candidate in favour of the only blocking move, and legal selection when all supplied moves lose. The interruption policy is implemented by persistent flags independent of iteration completion; these fixtures do not inject a deadline mid-iteration. Default modular source still has opening threshold 72; experimental executable uses 82 to search from the start as requested.

Final ten-game batch versus orig.c: **8 wins / 2 losses / 0 draws**. Starting first: 4/1/0; starting second: 4/1/0. Zero failed moves/responses, timeouts or nominal overruns. Mean 3.5 seconds/game, approximately 35 seconds for ten games. Current maximum first response 907.310 ms, later 90.431 ms. Evidence: results/experiment-03-source.c (threshold 82), results/experiment-03-games.csv, results/experiment-03-moves.csv, results/experiment-03.log. An initial development batch before preserving the existing eligible choice scored 7/3/0; final batch supersedes it. Historical time/PID randomness prevents paired comparisons with experiment 01 (6/3/1). This is encouraging exploratory data, not proof of a strength gain or achievement of 10-0.

Time-budget hypothesis: shorter budgets may accelerate screening but can change completed depths and relative bot strength. Rig timeout settings only enforce deadlines; they do not reduce bots' internal search budgets. No reduced-budget experiment run here. Measure depth completion and confirm promising variants at full arena budgets before adopting them.

## Experiment 04: count-aware search throughout DFS

Both shallow and exact minimax now derive per-player master-line possibility flags from every resulting position. Opponent-owned or closed drawn squares block a line; unfinished squares remain possible. Flags are recomputed from board state (not separately propagated), preserving existing cache-key correctness. Before cache/leaf evaluation, an ownership lead greater than all unfinished boards proves victory if the trailing player has no possible master line; the leading player's own line possibilities need not be exhausted. Symmetric losses return hard scores. These proofs propagate through existing minimax and root win/loss handling.

When neither player has any possible master line, heuristic evaluation switches within that branch to 20 points per claimed board plus 4 per local two-mark line and 1 per local one-mark line in unfinished boards. Otherwise existing scoring remains. No alpha-beta, hash optimisation or other weight changes. Experimental executable retains minimax from start (threshold 82); default threshold remains 72.

Validation: full regression suite passes. Exhaustive checks cover all 262,144 combinations of open/player-0/player-1/drawn master squares against independent line and count-bound calculations. Nonterminal count victory propagates through both solvers, and count-mode ownership/local scoring is checked. Existing fork fixtures were adjusted because their former board leads are now correctly proven wins before the fork. Merged submission passes compiler syntax checking; orig.c checksum unchanged.

Ten games against orig.c, normal budgets and alternating starts: **9 wins / 1 loss / 0 draws**. First: 4/1/0; second: 5/0/0. Zero technical failures, timeouts or nominal overruns. Mean 3.4 seconds/game. Current maximum first response 910.901 ms; later 90.376 ms. Evidence: results/experiment-04-source.c (threshold 82), results/experiment-04-games.csv, results/experiment-04-moves.csv, results/experiment-04.log. Previous experiment was 8/2/0; historical baseline randomness means these small batches are not paired proof of improvement. Goal of 10-0 not yet reached. Keep count-aware implementation for further assessment; no depth telemetry collected in this experiment.

## Experiment 05: root-only count evaluator selection

Corrected experiment 04's branch-dependent heuristic switching to the initial intended policy. After applying the actual opponent move, getMove selects count evaluation only if neither player has a surviving master winning line. This selection remains fixed throughout every depth and branch for that turn. Hard count win/loss proofs continue at every node in both search paths. Evaluator weights, budgets and early-minimax experimental threshold 82 unchanged. No new independent mode enters cache keys: mode is fixed per turn and search tables are cleared before each search iteration.

Validation: regression suite passes, including a no-master-line descendant explicitly retaining the old root-selected evaluator and existing count-scoring/proof tests. Generated submission passes compiler syntax check; orig.c checksum unchanged.

Ten games versus orig.c: **6 wins / 3 losses / 1 draw**. Starting first: 3/2/0; second: 3/1/1. Zero failed moves/responses, timeouts or nominal overruns. Mean 3.5 seconds/game. Current maximum first response 907.598 ms, later 90.354 ms. Evidence: results/experiment-05-source.c (threshold 82), results/experiment-05-games.csv, results/experiment-05-moves.csv, results/experiment-05.log.

Interpretation: weaker observed score than experiment 04's 9/1/0, but these ten-game batches are unpaired because orig retains time/PID randomness. This does not establish branch switching was better, nor isolate the effect of count evaluation. Retain root-only evaluation as the agreed policy. Count-mode activation/depth telemetry was not collected; that is useful next evidence before tuning weights or attributing the result.

## Parallel measurement workflow

multi-rig.sh now launches one identical rig command four times, with seed ranges separated by 1,000,000. Default is ten games per worker, early minimax versus untouched orig.c. Each invocation writes directly into a single run-TIMESTAMP-PID directory under the current directory. summarize-rig.py writes summary.csv with four worker rows and a TOTAL row (games, wins, losses, draws, technical failures, match score), and games.csv with worker identity plus original game fields. Worker logs, moves CSVs, command, seeds and exit statuses are retained alongside them. No games launched during this change; shell syntax checked and aggregation verified using existing tables (24/12/4 over 40 games). Retired experiments/runs.conf from the active workflow; existing historical results retained.

TODO: tune exact final DFS trigger/allocation on target hardware, measuring failed attempts and time lost to fallback.
TODO: examine efficiency of iterative minimax resumption at +2 plies, including reusable work and move ordering; defer optimisation until measured.

## Quoted bot commands and startup identification

Rig --p0/--p1 now accept an executable plus arguments as one quoted value, parsed with wordexp and command substitution disabled, then execvp (no shell execution). Local bots emit @BOT TAB id TAB hello before their first move only when CG_LOCAL_HELLO is set by the rig. Current ID hashes modular sources; orig is compiled through orig_identified.c, preserving orig.c and adding identification only. Hello includes effective opening threshold/evaluator/weights and explicitly states scale is not implemented. Unknown current-bot arguments are rejected.

multi-rig supplies --identity-file per worker. Rig retains and verifies identity consistency across games, even with quiet diagnostics. Summary includes both IDs/hello records, rejecting missing or different records across workers. Build hashes identify compiled source, but automatic comparison against current uncompiled sources is not yet implemented. Scale usage counters await the actual scale feature.

Validation: compiled rig and identified bots; startup records checked directly; quoted multiword arguments checked with a tiny non-playing test process; aggregation verified on historical game tables with synthetic consistent identity records. No real matches launched. Historical orig segfaults on EOF during direct startup probe after emitting hello; no historical gameplay logic altered. Search regression suite was not rerun for these protocol-only changes.

## Count scale parameter and usage evidence

Current bot accepts strict --count-scale=VALUE in range 0..10, default DEFAULT_COUNT_SCALE=1.0. Fractional weighted ownership is rounded to nearest integer per player. COUNT strength is 20 per owned board; its scaled value is added in both root-selected evaluator modes. Local development is counted once. This introduces ownership scoring to the old evaluator too, so previous 71/100 is not an unchanged-formula control. Hard outcome proofs remain independent of scale. Hello reports effective scale and the one-board contribution (1.4 -> 28).

Local @USE records report heuristic evaluation calls and calls where player board counts differ, per turn. Rig stores these alongside identity files; combined summary totals current-bot evaluations and count-differential evaluations. Nonzero differential count means the component had an opportunity to change relative scores; it does not prove it changed the chosen move (nor a nonzero contribution when scale is zero). These are heuristic evaluation calls only, not all DFS nodes or cache hits. No match batches launched.

Suggested initial data points: scales 0, 0.5, 1, 2, with 100 games per scale using the same rig seed schedule. Record pooled match score, starting-side W/L/D, technical failures, bot IDs/settings and count-differential evaluation frequency. orig retains time/PID randomness, so seed-matched batches are not fully paired. Refine around any promising region on fresh data rather than interpreting a few extra wins as a confirmed gain. Preserve normal time budgets for this sweep.

## Cleanup: one shared evaluator

Removed count_evaluation state, its root selection check, and the separate count-only scoring branch. All heuristic leaves now use the existing line/local features plus scaled ownership, with local weights sourced from the existing weight array. Startup hello identifies evaluator=shared-line-plus-count. Hard count proofs remain unchanged in both DFS paths.

Validation: regression suite passes. Exhaustive master-state checks now additionally confirm that, whenever neither player has a possible master line and local boards are empty, all actual/projected master terms vanish and the shared score equals ownership alone. Existing unfinished-local-board fixture checks ownership plus local development at scales 0, 1 and 1.4. Rebuilt search-from-start executable and merged submission; merged syntax check passed; orig.c checksum unchanged. No games launched.

## User-run scale comparison: shared evaluator build e6537a4121c5123c

Both batches: early minimax threshold 82, shared-line-plus-count evaluator, unchanged identified orig baseline bf14aaa7a7a64fd0, 100 games, zero technical failures. Reported by user from combined summaries; seed schedules/run directories not supplied in this message.

Scale 1.4 (one-board term 28): 60 wins / 32 losses / 8 draws; match score 0.640. Heuristic evaluations 422,294,064; count-differential evaluations 101,829,383 (~24.1%).

Scale 0 (one-board term 0): 64 wins / 29 losses / 7 draws; match score 0.675. Heuristic evaluations 409,823,408; count-differential evaluations 97,368,128 (~23.8%). Adopt as current experimental control, not a promoted locked gameplay baseline.

Observed scale-1.4 difference versus control: -3.5 percentage points in match score, -4 wins. Not convincing evidence of a strength difference from two unpaired 100-game samples; orig remains time/PID seeded and timed searches can vary. No demonstrated benefit from ownership scale 1.4. Keep scale 0 as explicit control for further experiments; do not conflate with prior 71/100 from a different build/formula.

## New evaluator: named f1, f2, fc

Replaced prior heuristic and projected-board terms with the agreed structure, calculated separately for both players: f1(U,closed,p)*USCALE + fc(U,p)*20*count_scale + sum(f1(Si,0,p)*f2(U,closed,i,p)) over unfinished boards. All three tweakable functions are in ai_minimax.c. f1 scores distinct immediate winning cells as 0/4/6 plus unblocked one-mark lines, returning zero for grids already won by either player. Grid configurations with no winning routes for p naturally yield zero. Local f1 scores are precomputed for all 19,683 base-3 grids and both players. U additionally uses the closed-board mask to block drawn squares.

Initial f2 weights: base 1 for count relevance plus 1/2/4 per surviving master line through the cell with 0/1/2 owned squares. Closed cells return zero. fc returns raw ownership count. USCALE defaults to 10. These new weights are hypotheses, not previously tested values; this is a new evaluator/control, not directly comparable as a one-weight tweak to the earlier baseline. count scale remains strict runtime option 0..10, default 1. Hard count/terminal proofs and search unchanged. Hello exposes new evaluator identity and effective USCALE/formulas.

Validation: board/search regression suite passes, with all 19,683 player-swap scoring checks and existing exhaustive master/count-proof tests. Built search-from-start and regenerated submission; no matches launched. Local f1 does not account for access/routing or condition its fork premium on opponent immediate threats; search and subtraction still handle those within their horizon. Configured count base in f2 is independent of count_scale (which weights secured ownership only).

## User-run f1/f2/fc control: build b6329d9e9bddd3a6

100 games versus identified historical orig bf14aaa7a7a64fd0: **79 wins / 18 losses / 3 draws**, match score **80.5%**, zero technical failures. Effective settings confirmed in summary: opening threshold 82, f1-f2-fc evaluator, USCALE=10, f1 winning-cell values 0/4/6 plus one-mark lines, f2 base1 plus surviving-line values 1/2/4, count scale 0. Heuristic evaluations 396,011,072; count-differential evaluations 97,325,897 (~24.6%). Scale zero means secured-ownership contribution was zero despite those count differences.

Previous shared-evaluator scale-zero control scored 64/29/7 (67.5%). Observed difference: +15 wins and +13 percentage points in match score. Promising evidence for the new evaluator bundle, not attribution to any one component; batches remain unpaired with historical opponent randomness. Goal 10-0/promotion not achieved. Suggested next step: fresh confirmation at unchanged settings before moving another lever. Run directory/seed schedule not supplied with user summary.

## User-run f1/f2/fc count scale 0.25

Same evaluator build b6329d9e9bddd3a6 versus orig bf14aaa7a7a64fd0, 100 games: 72 wins / 24 losses / 4 draws, match score 74.0%, zero technical failures. Effective scale 0.25, one-board ownership term 5; all other reported settings unchanged. Heuristic evaluations 397,532,306; count-differential evaluations 97,537,724 (~24.5%). Compared with same-build scale-zero 79/18/3 (80.5%), observed change is -6.5 match-score points. Earlier scale 1.4 decline concerned a different evaluator build, so these are two negative observations across designs rather than one within-build dose-response series. Neither isolated unpaired 100-game comparison conclusively proves harm. Decision: park secured-ownership bonus; use explicit scale 0 while investigating other levers. Default remains 1 unless explicitly changed.

## Run diagnostics: compact metadata and outcome breakdown

Combined summary now puts verified bot IDs/hello records in a single metadata block above the numeric CSV table. Worker rows and TOTAL include each player's actual 3IAR/count wins and failed exact-DFS attempts, alongside existing score/usage fields. Games CSV records win_type (3iar/count/draw/forfeit) and both DFS-failure counts. Final win type comes from actual ownership at game completion; early count proofs do not prematurely classify the eventual finish.

Current bot emits local DFS_FAILED event when exact solver returns timeout/fallback. Historical baseline's generated instrumentation emits the same event at both exact root failure returns (timeout and predicted-overrun abort). orig.c remains unchanged, but identified wrapper/build ID changes because instrumentation changes. Count means failed attempts, not games containing a failure, and is not itself a technical disqualification. Events are retained even with quiet bots.

Validation: synthetic four-worker aggregation checks sums and single metadata block; rebuilt identified bots/rig and regenerated submission. No matches launched. Next requested user-run scale: 0.5 with normal budgets and 100 games.

## Wild-card experiment: count gated by zero top-grid score

Ownership term now applies per player only if f1(U,closed,p)==0. USCALE, f1, f2, local terms and hard proofs unchanged. Hello explicitly reports count-gate=f1-U-zero-per-player. This is literal zero score, not absence of every possible line: f1 can be zero with surviving empty lines and no developed line. The gate is per player, not a requirement for both scores to vanish. Gate can therefore introduce a discontinuity; this is a deliberate experimental policy, not a calibrated scoring claim.

Requested next user run: scale 0.5, 100 games. Prior ungated same-scale result reported by user: 75 wins / 23 losses / 2 draws (76.0%); 65/10 current 3IAR/count wins, 22/1 orig 3IAR/count wins, 13/4 failed exact attempts, zero technical failures. Evaluations 394,015,951; count-differential evaluations 96,708,035. Existing differential telemetry measures opportunities based on ownership differences, not actual gate activation; it should not be read as gated-term usage.

## User-run gated count scale 0.5

100 games: 77 wins / 21 losses / 2 draws, match score 78.0%, zero technical failures. Current 67 line wins / 10 count wins; orig 17 line wins / 4 count wins. Failed exact DFS attempts current 19, orig 4. Heuristic evaluations 393,124,957; count-differential evaluations 89,773,884. Worker scores 76%, 62%, 90%, 84% demonstrate substantial ten/twenty-five-game batch variability. User supplied table without metadata; attributed to requested gated-scale-0.5 experiment, build identity not independently confirmed from this pasted table.

Comparison: ungated scale 0.5 scored 76%, gated 78%, scale-zero f1/f2/fc control 80.5%. Small differences do not establish ranking. Finish type and failed-attempt counts alone cannot explain causal changes; successful exact attempts and activation/timing data are absent. Maintain explicit separation of measured outcomes and hypotheses; do not tune from individual worker extremes.

## Runtime USCALE sweep preparation

Added strict --uscale=INTEGER (0..100), defaulting to compile-time USCALE=10. scoreBoard uses effective runtime value; hello reports it. For this sweep pin --count-scale=0 explicitly, disabling secured-ownership term regardless of the experimental gate; f2 retains its existing base count relevance 1. Suggested values 5, 10, 20, all other evaluator/search settings unchanged. Rebuilt search-from-start and regenerated submission; no matches launched.

## User-run USCALE 5, count scale 0

Verified pasted metadata: current build 7dafe7e68a548985, USCALE 5, count scale 0, threshold 82, f1/f2/fc formulas unchanged; identified orig c5d40db457fe6b96 (new diagnostic instrumentation, historical gameplay unchanged).

100 games: 78 wins / 12 losses / 10 draws, match score 83.0%, zero technical failures. Current: 71 3IAR wins / 7 count wins, 25 failed exact attempts. Orig: 10 3IAR wins / 2 count wins, 10 failed exact attempts. Evaluations 424,510,718; count-differential evaluations 134,956,890 (~31.8%). Worker match scores 72%,90%,84%,86%.

Earlier USCALE 10 count-zero result: 79/18/3, score 80.5%. Observed USCALE 5 difference: +2.5 score points, six fewer losses, seven more draws, one fewer win. Not established superiority from these unpaired batches. Complete same-build USCALE 10 and 20 sweep before selecting a setting. Failed exact counts lack attempts/success denominator and reflect changed positions, so cannot alone diagnose regression.

## Evaluation heatmap replay

Added heatdump.c and heatmap.py. heatdump replays one game from rig move CSV and emits all 81 cells for each pre-move position, evaluating legal candidates immediately after one move, with current f1/f2/fc gated-count formula and hard proofs. Moving-player perspective, top/local/count contributions, actual chosen-cell flag, marks and closed status are recorded. HTML displays successive 9x9 boards, shared colour range, hover component breakdown and thick chosen borders. This is direct heuristic inspection, not the deeper minimax score that selected the actual move; terminal proofs override displayed heuristic breakdown. Local cache initialised once. USCALE/count scale supplied explicitly to replay.

Replayed an existing game to check legality and generated a fresh single-game example at USCALE10/count0 in current-directory heat-game*, heat-scores.csv and heatmap.html. No bulk matches. Initial all-81-move handling corrected during verification. Output does not reconstruct hidden bot RNG/cache history, since direct heuristic scoring does not need them. Standalone HTML has no external dependencies.

Heatmap correction: only player 0 evaluation frames are emitted/displayed; player 1 moves are still replayed to reconstruct subsequent positions. Verified all 81 opening cells carry legal candidate evaluations. Regenerated heatmap.html from the existing single-game recording, no new match.

Opening verification initially exposed legal-flag bug: moveIn returns a bitmask, not Boolean; HTML required exactly 1 and therefore hid most candidate scores. Normalised CSV legal field with !!moveIn, rebuilt and regenerated. This was display/export wiring, not bot evaluation logic.

## Heatmap equal-depth minimax scoring

heatdump now defaults to six plies (current configured shallow-search maximum), evaluating every legal root candidate fully with timers disabled. Optional final argument selects 1..6 plies. Uses normal depth-aware transposition cache, cleared per replay position. HTML numbers/colours now represent searched scores, with search depth displayed. Hover explicitly labels immediate heuristic score and its top/local/count breakdown; those components do not sum to the deeper searched value. Actual move remains marked, but timed gameplay can finish a shallower depth or use exact endgame solver, so its choice can differ from fixed-six-ply replay. Regenerated example from same game, no new match.

## Current-bot cleanup and targeted append log

Removed the obsolete deterministic opening evaluator and all supporting MoveEvaluation/evalCheck/pickRandom code, plus its opening telemetry. Current gameplay is now timed iterative shallow minimax from move one, with exact endgame attempt/fallback at the existing trigger. Removed current-bot file-based weight loading, board-fixture loading, embedded test rig, forced-first-move and legacy -l/-w/-f/-t options. Runtime --uscale and --count-scale remain strict. Historical orig.c is unchanged. Source-merging submission workflow remains.

Added local append-only ai_minimax.log. Each process writes one complete bounded line with a single O_APPEND write, making four concurrent bot processes readable without separate log coordination. Startup records include PID, CG seed, build ID and effective evaluator settings. Turn records include PID/seed, playable spaces, legal count, search task, exact attempted/failed, last completed shallow plies (-1 when exact completed without fallback), nodes, elapsed microseconds, chosen move, evaluation counters and scales. A forced win discovered during an iteration records that searched ply count. CG_GAME builds do not perform file I/O.

Validation: rebuilt normal/search-from-start bots and merged submission; board/search regression suite passes; merged syntax check passes. One local game at USCALE5/count0 verified concurrent-safe append records and existing protocol diagnostics, with zero game failures/timeouts. This single validation match is not an experiment result. ai_minimax.c shrank substantially; historical orig.c remains byte-for-byte unmodified according to git diff.

## Exact-trigger baseline and retained shallow-cache experiment

Instrumentation now divides failed exact attempts into the `spaces <= 18` trigger and the special `spaces == 19 && legal < 10` trigger. Both runs used USCALE5/count0, 100 games, base rig seed 20261001, four workers and zero technical failures. Historical orig remains time/PID random, so results are not paired trajectories.

Baseline build 4b92ce6b4d6db5c9 (shallow table cleared before each 2/4/6-ply iteration): 72 wins / 27 losses / 1 draw, score 72.5%. Current exact failures 25: 16 at <=18, 9 at 19. Orig failures 8: 7 at <=18, 1 at 19. Evaluations 243,854,876. Evidence: run-20260915-081908-2/summary.csv.

Experimental build 5b88c4febc1b4b56 (one clear before shallow iterative deepening; table retained across depths): 73 wins / 22 losses / 5 draws, score 75.5%. Current exact failures 28: 23 at <=18, 5 at 19. Orig failures 13: 9 at <=18, 4 at 19. Evaluations 236,212,407. Evidence: run-20260915-082236-2/summary.csv.

Observed retained-cache change: +3 score points and ~3.1% fewer heuristic evaluations, but more exact failures. This does not demonstrate useful cross-depth reuse: cache keys contain remaining depth, so most positions from different iterations cannot hit each other directly. Add per-depth lookup/hit/capacity telemetry before attributing the evaluation reduction to reuse. Retained-cache experiment currently remains in source pending decision; baseline behaviour is the immediately previous build/result.

Two invalid attempted runs produced no usable games and are excluded: run-20260915-082133-2 exceeded the rig's 256-byte hello response buffer; run-20260915-082200-2 then exposed a 256-byte stored-identity overflow. Rig response and identity buffers are now 2048 bytes. Both failures were caught as technical/identity failures before valid comparisons; no scores retained.

## Exact-search entry threshold sweep (cache clearing restored)

All runs: build with shallow table cleared at every completed-depth attempt, USCALE5/count0, 100 games, rig base seed 20261001, zero technical failures. Runtime thresholds and hello records prevent stale/misconfigured variants. `primary=N` means spaces<=N; `narrow=M` means spaces<M and legal<10 if primary did not already trigger. orig remains time/PID random, so results are unpaired.

Baseline primary18/narrow20: 72/27/1, 72.5%; current exact failures 25 (primary16, narrow9); evaluations243,854,876. run-20260915-081908-2.

Primary lowered only, primary17/narrow20: 78/17/5, 80.5%; current exact failures24 (primary15, narrow9); evaluations240,297,241. run-20260915-084050-2.

Narrow lowered only, primary18/narrow19: 77/19/4, 79.0%; current exact failures27 (primary27, narrow0); evaluations241,668,946. Because spaces<19 is covered by primary<=18, this removes the narrow exception entirely. run-20260915-084228-2.

Both lowered, primary17/narrow19: 75/17/8, 79.0%; current exact failures27 (primary19, narrow8); evaluations241,403,924. This uses primary<=17 plus narrow at 18 with legal<10. run-20260915-084417-2.

All tightened variants observed fewer losses and higher match score than this noisy baseline. Primary17/narrow20 has the highest observed score, while both-lowered ties narrow-only on score with more draws. Differences between tightened variants are small relative to known batch noise. A fresh confirmation is needed before fixing defaults. Retained-cache experiment was reverted before sweep; prior retained result remains historical only.

Protocol robustness: two pre-experiment invalid batches caught long hello truncation and identity overflow; expanded response and stored identity buffers to 2048. Those runs contained only technical failures and were excluded.

## Locked exact thresholds

Selected default exact thresholds primary17/narrow19: exact at <=17 playable cells, plus at 18 cells when fewer than 10 legal moves. Runtime overrides remain for experiments. Rebuilt normal/search bot and merged submission; merged syntax check passed. Selected for current working version despite primary17/narrow20 having the highest single observed threshold-sweep score, following user's preference for the combined tighter setting.

## Relative-score experiment preparation

Added runtime `--score-mode=difference|ratio`; the default remains the historical strength difference. Ratio mode compares non-terminal leaf scores as `10000*(mine-theirs)/(mine+theirs+2)`, equivalent to adding one to both nonnegative strengths before normalising. Proven wins and losses remain explicit `+/-60000` values and therefore cannot be confused with a positional ratio; draws remain zero. Both recursive choice and root choice use the selected comparison. Bot hello and append log identify the active mode.

Added a GitHub Actions comparison with 200 alternating-start games per mode by default. Difference and ratio use identical four-worker seed schedules and separate runners/artifacts. USCALE5, count scale zero and exact thresholds 17/19 are pinned in the command and verified through bot hello metadata.

Bot timeouts, invalid replies and process exits during an individual game are now recorded as forfeits without making a completed rig batch return failure. Opening experiment wrappers likewise fail only for an incomplete subprocess or missing games; technical failures remain counted in their CSV summaries. Identity mismatch remains a hard experiment failure. Verified with a deliberately dead bot: one recorded `invalid_or_eof` forfeit, complete CSV, process exit zero.

Validation: rebuilt gamerig, orig and current bot; exercised difference and ratio modes over two alternating-start games each with distinct verified hello metadata and no technical failures. Existing board test executable completed successfully. Full comparison pending GitHub dispatch.

## Difference versus relative-score result

GitHub Actions run 34998705791, commit 5803aa1, used 200 games per mode with identical rig seed schedules and alternating starts. Bot identities and hello strings verified difference versus ratio mode; USCALE5, count0 and exact thresholds 17/19 were pinned. Historical orig still seeds from the rig-provided value but its gameplay and timing can diverge between separate processes, so this is a controlled aggregate comparison rather than identical paired trajectories.

Difference: 154 wins / 40 losses / 6 draws, four technical failures, raw match score 78.50%. All four failures were current-bot timeouts when orig started. Excluding forfeits: 154/36/6 over 196 played games, score 80.10%. Starting split: current first 89/9/2, 90.0%; orig first clean 65/27/4, 69.79%.

Ratio: 151 wins / 38 losses / 11 draws, two technical failures, raw match score 78.25%. Both failures were current-bot timeouts, one under each starting side. Excluding forfeits: 151/36/11 over 198 played games, score 79.04%. Starting split clean: current first 83/12/4 over 99, 85.86%; orig first 68/24/7 over 99, 72.22%.

Observed ratio-minus-difference effect is -0.25 raw score points, or about -1.06 points after excluding forfeits. This is effectively no aggregate improvement at this sample size. Ratio shifted the observed starting-side split: -4.14 points when current started and +2.43 points when orig started on clean rates, while increasing draws and reducing current count wins from 16 to 12. None establishes a causal advantage from one run. Ratio performed about 10.4% fewer heuristic evaluations (972,791,050 versus 1,086,132,749), mainly because changed games/positions and forfeits alter workloads; do not interpret this directly as evaluator speed.

## Timed shallow-search readability refactor

Refactored `evaluateMovesShallowTimed` without changing its search policy. The function now names and documents its phases: stable root collection, legal fallback, 2/4/6-ply completed iterations, immediate terminal proof handling, positional-score commit only after a whole iteration, retention of deeper forced-loss proofs from an aborted iteration, and final selection among the last completed scores. Extracted `chooseBestRetainedMove` makes the post-timeout behaviour explicit. Removed the unreachable completed-iteration `resolved/best==TERMINAL` stop: a proved root win already returns immediately and an all-loss iteration already has no surviving best moves.

Validation: warning-clean bot rebuild, `git diff --check`, existing board test exit zero, and two alternating-start ratio smoke games against orig with verified identity and zero technical failures. No strength experiment was run for this source-only refactor.

Follow-up root-state refactor replaces parallel move/score/loss arrays with an explicit `RootMove`: move, latest score, maximum evaluated plies, score from the last globally completed depth, and unproved/forced-win/forced-loss state. A forced loss is skipped at every subsequent deeper iteration. A forced win records its state and returns immediately. The iterative loop is now expressed in total target plies `2,4,6`, passing `target_plies-1` below the already applied root move. This exposes the existing hard six-ply cap; it was deliberately preserved rather than combined with the structural change. Latest scores from an aborted iteration are recorded per move but selection still uses stable equal-depth scores, except for permanent proof exclusions. Rebuilt warning-clean; board test and two alternating-start ratio smoke games completed with zero technical failures.

## MM-001-RM mixed-depth search

Simplified shallow root search to a `RootMoves` container holding count and `RootMove[81]`. Removed the stable-score copy and selector helper. Every completely evaluated root overwrites its prior fixed-point ratio score and evaluated-ply depth immediately; timeout therefore leaves the intended mixture of 2/4/6-ply results. Forced losses store -60000 and are skipped at every deeper iteration but remain selectable if all roots lose; a forced win stores +60000 and returns immediately. Final selection is a short maximum-score scan with random tie selection. The six-ply cap remains unchanged.

Removed append-only `ai_minimax.log` code, startup/turn file logging, and logging-only task/depth state. Rig protocol identity and compact experiment counters remain. Added human-readable bot IDs: ratio merge is `MM-001-RM`, difference merge is `MM-001-DM`; hello retains the exact source build hash.

Local four-worker run, 100 alternating-start games, seed 20261001, MM-001-RM build d6a6edcd86b1c048 versus identified orig 21d1b83935be6da9: 78 wins / 15 losses / 7 draws, score 81.5%, zero technical failures. Current started 52 games and scored 87.5% (43/4/5); orig started 48 and current scored 75.0% (35/11/2). Worker scores 96%,72%,72%,86% show the expected noise. Previous 200-game ratio/no-merge observation was 78.25%; +3.25 points is promising but not established. Evidence: run-merge-100/summary.csv. Prepared a dedicated 1,000-game GitHub alternating-start workflow for confirmation.

## One-game DEBUG search report

Added compile-time-only `DEBUG` telemetry and `bin/ai_search_debug`, identified as `MM-001-RMD` with an exact build hash. Production builds contain no CSV file output. Per bot turn the debug CSV records budget and elapsed milliseconds, legal roots, completed root evaluations across depths, deepest completed ply, selected move and fixed-point score, static leaf score count, total cache lookups/hits across cache clears, and shallow/exact/fallback search mode. `debug-one-game.sh` runs one identified game; `debug-report.py` creates a standalone HTML report with the 900 ms opening shown separately, regular turns plotted as 10 ms vertical blocks, and the requested move table. Generated `debug-game/report.html` from a clean 39-ply current-bot 3IAR win; our bot made 20 moves with no technical failure. Production and DEBUG builds compile warning-clean and the board test exits zero.

## MM-001-RM 1,000-game confirmation

GitHub Actions run 35034282004, commit 15b0fe369e3740c5ea905ab35e8101bd885b2e6a, completed 1,000 alternating-start games. Verified bot identity was MM-001-RM build d6a6edcd86b1c048 against orig 21d1b83935be6da9, with ratio scoring, mixed partial-depth merge, USCALE5/count0 and exact thresholds 17/19. The run completed with zero technical failures.

Overall: 798 wins / 183 losses / 19 draws, match score 80.75%. The approximate 95% interval for match score is 78.34%-83.16%. Wins comprised 718 by 3IAR and 80 by count; orig won 174 by 3IAR and 9 by count. Current-bot exact search failed and fell back 55 times (36 primary, 19 narrow); orig did so 49 times. Current-bot work totalled 4,786,538,398 evaluations, including 1,632,871,791 count-differential evaluations.

Starting split: with MM-001-RM first, 430/65/5 over 500 games, score 86.50%; with orig first, MM-001-RM scored 368/118/14, score 75.00%. The 11.5-point first-player effect is clear. The overall result confirms the local 100-game observation near 81%, but does not establish a meaningful improvement over the earlier 200-game ratio/no-merge score of 78.25% because that prior batch is small and had two technical losses. Treat MM-001-RM as an approximately 80%-81% opponent score against orig, not a 10-0 replacement yet.

## MM-002-RM open-ended iterative depth

Removed the artificial six-ply shallow-search ceiling. Shallow iterative search now starts at four plies, then attempts 6, 8, 10 and so on until the deadline. Every completed root immediately overwrites that move's previous score, retaining the mixed-depth merge behaviour. Four ply is the safety baseline because the DEBUG evidence showed the bot reaching at least six ply, although the report's depth field means at least one root completed at that depth rather than the entire iteration.

Added two defensive cases required by the new loop: stop if every root is already a proved forced loss, avoiding a zero-work infinite loop; and exclude never-evaluated roots from score selection if an exceptional timeout interrupts the initial four-ply pass, falling back to a random legal root only if no root was evaluated. Updated bot identity to MM-002-RM/MM-002-RMD and hello metadata now states `shallow-plies=4-until-timeout`.

Validation: warning-clean production and DEBUG builds, existing board test passed, and a two-game alternating-start smoke test completed with zero technical failures. A separate identified DEBUG game won cleanly and reported mostly six-ply shallow results; no eight-ply root completed in that single game, so the immediate benefit is additional six-ply root coverage rather than demonstrated eight-ply information. Strength remains unmeasured pending a proper comparison run.

Fresh MM-002-RMD report: identified build 27db7f6bf57708b1 beat orig by 3IAR in 55 plies with zero failures. Regular turns remained within the 90 ms target. Shallow search generally completed at least one six-ply root and spent the remaining allowance attempting deeper work; no shallow eight-ply root completed. The report now labels the value as the selected backed-up score and documents the normalized-difference scale.

Local MM-002-RM gate: 100 alternating-start games over four workers, seed 20261001, produced 73 wins / 25 losses / 2 draws, score 74.0%, with zero technical failures. Worker scores were 88%, 78%, 80% and 50%, demonstrating substantial batch variation. This is below both MM-001-RM's local 81.5% observation and its 1,000-game 80.75% result. The protocol/build gate passed, but the strength result is not positive; a requested 1,000-game GitHub run will determine whether this is noise or a regression.

## MM-004-R1MM evaluator experiment: secured boards and live master threats

Problem: at saved instrument game ply 42, capturing U1 with row/col (1,3) creates two still-capturable master-winning cells, including U7, yet all four alternatives leaving U1 unfinished have higher immediate heuristic scores. The current gated ownership term gives p0 no secured-board credit while its master f1 is nonzero, drops the captured board's local potential, and perversely activates p1's ownership credit when p1 master f1 becomes zero. Exact terminal search proves all five choices win; capturing U1 guarantees a win within 9 plies versus 11 for the played (0,3). This is a scoring defect, not evidence of a loss in that particular game.

Hypothesis/implemented MM-004-R1MM formula for each player at a nonterminal leaf: `uscale*f1(master) + 20*count_scale*owned_U + 80*distinct_live_master_winning_cells + sum(f1(open_local)*f2(master,local))`; default scales are 5 and 1. A live master-winning cell must itself be an unfinished local board with at least one line free of opponent marks. Compare p0 and p1 with the unchanged ratio formula; terminal scores and search are untouched. The master-threat bonus deliberately adds urgency beyond f1's existing 0/4/6 winning-cell feature, while ownership is unconditional. Implemented in `ai_minimax.c`, HELLO `MM-004-R1MM`; the Python workbench retains the reference calculation.

Python checks: at ply 42 the proposed immediate scores are +6590 for the U1 capture and +5141/+5048 for the four alternatives, versus prior +2254 and +5286/+5099. Tested immediate nonterminal p0 leaf rankings at 2,309 reachable positions from 100 games in `work/latest/worker-*-moves.csv`: 172 top choices changed. At three sampled late-game changed positions, both the old and proposed top choices were forced wins by independent terminal search. These are static-leaf checks, not a timed-bot strength result; local capturability also ignores future routing/opponent defence, which shallow search must assess.

Validation: `make test` passed, including new unconditional-ownership and two-threat assertions; normal bot compiled warning-clean and answered `MM-004-R1MM` to `--HELLO`; merged single-file submission passed GCC syntax check and is 47,012 characters. Two alternating-start local smoke games against orig at seed 20261001: 2 wins / 0 losses / 0 draws, zero technical failures or overruns. This tiny smoke is not strength evidence. Decision: run the requested 100-game local gate before considering a 1,000-game GitHub confirmation.

Local 100-game gate completed with the standard four workers, 25 games each, seeds 20261001/21261001/22261001/23261001 and alternating starts: MM-004-R1MM beat orig **80/19/1** (80.5% match score), zero technical failures. Evidence: `work/latest/summary.csv` and `reports/latest-analysis.html`. The archived MM-003-R1MM control used the same command and seed schedule and scored **81/16/3** (82.5%), zero technical failures; evidence: `work/latest.10/summary.csv` and `reports/latest-analysis.10.html`. MM-004 scored 459,365,384 positions in 221,990,469 search microseconds (~2.07M/s), versus 471,172,919 in 220,558,516 microseconds (~2.14M/s) for MM-003. Seeds match, but timed searches do not guarantee paired move traces. Decision: no observed strength gain in this small batch; do not treat MM-004 as promoted or launch a 1,000-game confirmation on this evidence alone. Keep the source as an explicitly reversible experiment pending investigation or user decision.

## MM-005-R1MM leaf master certificate experiment

Hypothesis/implemented: at synchronized depth-0 leaves, if the next player has an `ev_cache`-identified immediate local-board win in the forced destination (or any open destination when routing is closed), and virtual master-cell claiming satisfies the conservative master certificate, return the established terminal score before ordinary evaluation. Added both-player forced/free/closed negative assertions and incremented the official ID to MM-005-R1MM. Validation: `make test` and `fish scripts/check-candidate.fish` passed; submission was 51,170 characters. Against the matched MM-004 policy (standard 4×25, seeds 20261001/21261001/22261001/23261001, alternating starts, `--p1-game-seed`), MM-005 scored **57/34/9**, zero technical failures. Evidence: `published-results/mm005-leaf-certificate-20260917/summary.csv` and `published-results/mm005-leaf-certificate-20260917/latest-analysis.html`; MM-004 baseline is preserved under `published-results/mm004-baseline-20260917/`. Decision: provisional negative result; do not promote without diagnosing the strength loss.

Correction: the depth-zero integration passed the just-applied mover instead of the next player to `certifiedImmediateMasterWin`; therefore the 57/34/9 run used a buggy binary and is invalid evidence against the certificate idea.

## MM-006-R1MM corrected leaf master certificate

Fixed the depth-zero turn-ownership bug and updated the official bot ID to MM-006-R1MM. `make test` and `fish scripts/check-candidate.fish` passed; submission was 51,170 characters. The standard four-worker local 100-game gate (25 each, seeds 20261001/21261001/22261001/23261001, alternating starts, `--p1-game-seed`) scored **84/13/3**, 85.5% match score, with zero technical failures. MM-006 reported 340,860,673 scored positions in 222,520,320 search microseconds (1,531,818 positions/s). Evidence: `published-results/mm006-corrected-leaf-certificate-20260917/summary.csv`, `games.csv`, and `latest-analysis.html`. Decision: keep the corrected experiment and proceed to the authorized 1,000-game GitHub confirmation; retain the MM-005 57/34/9 run as invalid buggy-binary evidence.

## MM-007-R1MM internal master certificate

Hypothesis/implemented: the corrected immediate-master certificate remains valid at every shallow-search node, so checking it after terminal/count/cache handling should safely return a cached absolute terminal score before deeper recursion, not only at depth zero. Added forced-routing tests for both players, free-routing coverage, and a negative internal node that still reaches leaf scoring; updated HELLO to MM-007-R1MM. Added low-overhead end-of-game counters for certificate hits at depth zero versus depth greater than zero, carried through game CSV and summary aggregation. Validation: `make test` and `fish scripts/check-candidate.fish` passed; submission is 51,773 characters and both smoke games were played without technical failure. The 100-game strength comparison is pending; use the standard MM-006 four-worker policy (25 each, seeds 20261001/21261001/22261001/23261001, alternating starts, `--p1-game-seed`). Report will appear at `reports/latest-analysis.html` with raw data under `work/latest/`.
## MM-007-R1MM local 100-game result (archived)

Archived result: **74/21/5**, zero technical failures, using the standard four-worker policy (25 each, seeds 20261001/21261001/22261001/23261001, alternating starts, `--p1-game-seed`). Command: `./bin/gamerig -G25 --p0 './bin/ai_minimax' --p1 './bin/orig' --p1-game-seed --quiet-bots`; identities: MM-007-R1MM versus orig with no `--HELLO` response. Evidence is preserved under `published-results/mm007-internal-certificate-20260917/`. Totals: 6,185,717 leaf certificate hits, 2,473,105 internal hits, 451,169,948 scored positions, 218,693,147 search microseconds, 2,063,027 positions/s. No evidence of strength gain versus MM-006 84/13/3; 100 games are noisy, so investigate/revert and do not overclaim.

## MM-008-R1MM current-state master certificate lookup

Hypothesis/implemented: replacing the post-set9CB possible-line/count proof with the packed four-state master certificate remains equivalent while reducing each evaluateShallow node to an O(1) current-state lookup. Precomputed owner-state and drawn-state expansions are shared by current-state and virtual after-claim checks; explicit winner/draw tuples, timeout, cache identity, and the existing next-player certificate remain unchanged. HELLO is MM-008-R1MM. Exhaustive 4^9 reference/current/after-claim checks and both-player plus terminal-draw search assertions were added.

Validation: `make test` passed; `fish scripts/check-candidate.fish` passed with zero technical failures and submission size 53,025 bytes. No strength batch was run; 100-game strength/performance comparison remains pending.

## MM-008-R1MM local 100-game result (archived)

The standard four-worker local gate (25 games each, seeds 20261001/21261001/22261001/23261001, alternating starts, `--p1-game-seed`) scored **73/21/6**, 76.0% match score, with zero technical failures. Starts were 52 current-first and 48 orig-first; all worker exits were zero and all p0 identities reported MM-008-R1MM against orig with no `--HELLO` response. Durable evidence is under `published-results/mm008-current-certificate-20260917/`; the live `work/latest/` was preserved.

MM-008 totals were 448,899,865 scored positions in 219,589,585 search microseconds, 2,044,268 positions/s, and 39,480,685 cache hits. Certificate counts were 5,711,238 leaf hits and 2,663,217 internal hits. Against MM-007's 74/21/5, 451,169,948 positions, 218,693,147 microseconds, 2,063,027 positions/s, 6,185,717 leaf hits and 2,473,105 internal hits, this is effectively equivalent strength in a noisy 100-game sample, with roughly 0.9% lower measured throughput. MM-006 was 84/13/3 and MM-004 baseline 74/23/3; these older gates are context only, not paired comparisons, and MM-006/MM-004 did not record the newer certificate counters. Decision: keep MM-008 as a performance/equivalence experiment; do not claim a strength improvement or regression from this batch alone.

## MM-009-R1MM local claimability proof experiment

Hypothesis/implemented: local geometric claimability can tighten the existing conservative master certificates without changing heuristic scoring. Evaluation now precomputes one two-bit `can_still_win` mask for all 19,683 local encodings; Board9 carries two monotone u16 `cannot_claim` masks, refreshed by set9CB/set9/set9Simple and direct closure paths. Current and virtual-claim proofs exclude active cells unavailable to the certificate's opponent; old possible-line/count helpers remain lower-bound references. Exhaustive local-reference and incremental-mask assertions cover both-player/one-player impossibility, draws, wins, and early count proofs.

Gates: `make test` and `fish scripts/check-candidate.fish` passed; submission is 55,011 bytes and two alternating-start smoke games had no technical failures. Baseline captured before this change was Board9=28 bytes and Evaluation=12 bytes; MM-009 is Board9=32 (the required four mask bytes) and Evaluation=14. A representative 1M-call set9CB microbenchmark measured 7.96 ns/call and a fixed shallow-search loop 19.70 us/call; these are local microbenchmarks, not strength evidence. Strength comparison remains pending; ready command: `fish scripts/multi-rig.fish`.

## MM-010-R1MM packed claimability correction

Hypothesis/implemented: restore the MM-008 packed 4^9 certificate as the production proof path while retaining MM-009 local `can_still_win`/`cannot_claim` tightening. Precomputed owner and drawn base-4 contributions transform each current or virtual-claim state in O(1); active cells impossible for the opponent become pessimistic draws, while actual owner cells remain owners. Current-state checks snapshot `overall`, `overall_free`, and both `cannot_claim` masks so unchanged local moves skip the proof lookup; root entry checks a pre-existing proof once. Virtual claim checks remain unconditional because local winner masks can change independently.

Validation: independent reachable-prefix checks (128 deterministic prefixes, both players, every candidate including locally impossible active cells) compare transformed current/after-claim results against plain reference reconstruction; exhaustive 4^9 packed-table tests remain. `make test` and `fish scripts/check-candidate.fish` passed with zero technical failures; submission is 56,954 bytes. MM-009 tested a scan-based implementation and is not evidence for table-driven claimability cost. No strength batch run; 100-game comparison remains pending.

## MM-009-R1MM local 100-game result (archived)

The standard four-worker local gate (25 games each, seeds 20261001/21261001/22261001/23261001, alternating starts, `--p1-game-seed`) scored **77/16/7**, with zero technical failures. Evidence is preserved under `published-results/mm009-claimability-proof-20260917/`; `work/latest/` remains intact. MM-009 totals were 433,501,197 scored positions in 220,316,857 search microseconds (1,967,626 scored positions/s), with 5,695,619 leaf certificate hits and 2,548,247 internal hits. Relative to MM-008, the 100-game strength result is inconclusive; measured scored-position throughput was 3.75% lower for all four workers, but this is not total-node speed because proof returns are not counted. Decision: investigate before promotion.
