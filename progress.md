# Bot experiments

## Starting-move finder

Added a rig-level `--force-opening row,col` option. It fixes player 0 as the starter and presents exactly that one legal move on ply zero, so the tested bot consumes the forced move through its normal CodinGame protocol and retains its normal internal state. `find-starting-moves.py` tests all 81 openings with the same seed schedule and four concurrent games, verifies that bot identity/settings are identical across every opening, and emits both coordinate-level results and the 15 simultaneous outer/inner D4 symmetry classes. The manual GitHub Actions workflow defaults to 25 games per opening (2,025 total) with USCALE 5, count scale 0 and exact-search thresholds 17/19.

Revised the first hosted experiment to compare nine randomized category policies plus a normal baseline. `M` is centre, `D` diagonal/corner and `C` cardinal/edge. Each of `MM MD MC DM DD DC CM CD CC` randomly samples an eligible outer grid and eligible inner cell independently for every game; `BASE` lets the bot choose normally. Our bot is always player 0 and starts all 100 games per policy. Exact sampled/chosen coordinates are recorded. Total hosted run size is 1,000 games, with four policies concurrent.

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
