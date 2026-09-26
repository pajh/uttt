# Run and report scripts

All commands run from the repository root and drive the single-game JSON
`gamerig` (`gamerig <p0-binary> <p0-args> <p1-binary> <p1-args> <start 0|1>`).

## Small-context Luna handoffs

The design/review agent keeps the strategic discussion. For a bounded job,
start a `gpt-5.6-luna` sub-agent with **zero inherited conversation turns** and
give it only the filled-in brief below. It shares this workspace; the design
agent reviews its diff and evidence. These are task briefs, not commands to
launch a run automatically.

**Prepare/check an experiment:** “Hypothesis: [one sentence]. Bot ID: [ID].
Files in scope: [paths]. Implement [exact change] and a targeted assertion for
[position/behaviour]. Run `make test` and a short local rig smoke check; if those pass,
run [explicit local comparison command and count, if authorized]. Record the
command, seeds, identity, W/L/D, technical failures, and decision evidence in
`progress.md`. Stop on an unexpected diff or failed check. Do not commit, push,
or launch GitHub.” The design agent must choose the hypothesis and interpret
whether the result supports it; the worker must not silently tune several
variables until a score looks good.

**Operate an authorized GitHub run:** “Launch [N] games of bot [ID] from
[branch] against `orig` via the `bot-vs-orig` workflow. First inspect the exact
changed files, current HEAD, upstream, and the workflow's preconditions. Do not
commit or push unless separately authorized; if the required run inputs are not
committed and pushed, report the files and stop. Return the run link and the
exact status commands. Do not repeatedly poll or claim results before
completion.” The old local dispatch/retrieve helpers were removed with the
retired multi-game rig; use `gh workflow run bot-vs-orig.yml` and
`gh run download` directly.

**Gather evidence read-only:** “Question: [specific question]. Sources:
[report/CSV/game/position paths]. Read only the narrow fields needed; do not
edit bot code, trigger matches, or regenerate reports. Return the identifiers,
seed/starting player, exact figures or board observations, uncertainty, and
the next discriminating test in at most a short paragraph or small table.”

`scripts/multi-rig.py` plays a pair of bots across concurrent lines to compare
their relative performance.  It runs one single-game `gamerig` at a time per
line, in relaxed mode, and needs only the two bot commands plus the line and
game counts:

```fish
python3 scripts/multi-rig.py --p0 "./bin/ai_negamax" --p1 "./bin/orig" \
    --lines 4 --games-per-line 25 --seed 20261001
```

Each line derives its own seed from `--seed` and advances it per game; both bots
receive the game seed.  Lines alternate the starting player by line number, so
with an even number of lines each bot starts half the games (4 × 25 gives each
bot 50 first moves).  Each worker line is `<p0 time>:<results>:<p1 time>`.  The
results are a progress bar: green `3`/`C` are player-0 wins by three-in-a-row and
small-board count, red means player-0 losses, grey `D` is a draw, `F` marks a
technical forfeit, and white `.` marks a game not yet played.  Each time field is
a 10-cell bar (10 ms per cell) showing that bot's slowest move in the last
completed game, with the `NNNNms` text right-justified over it: green below
100 ms, a full red bar at 100 ms and above, and yellow unused cells.  Before the run
both bots are asked for their `--HELLO` identity, which is shown in a banner, the
live display and the result table.  Unless `--no-build` is given, the rig and
both bot targets (`ai_negamax` as `D=0 L=0 A=0 E=1`) are rebuilt first.
`--no-interactive` suppresses the display, and
`RIG_LINES`/`RIG_GAMES_PER_LINE`/`RIG_BASE_SEED`/`RIG_RELAXED` override the
defaults.

The console result table is also written to `reports/multi-latest.log`; an
existing file of that name is rotated to `reports/multi-latest.N.log` first.
Raw per-line JSON and bot logs stay in `work/multi/latest/` (a prior run is
archived as `work/multi/latest.N/`).

Ctrl-C during the games stops new games only.  Every `gamerig` game runs in its
own process session, so the terminal signal never reaches the games in flight;
they finish, keep their JSONL records, and are counted in the result table.  The
table then gains an `INTERRUPTED:` line with the completed/target game count and
the raw directory, is written and rotated to `reports/multi-latest.log` as
usual, and the run exits 130.  Further Ctrl-C presses during the drain are
absorbed, so the report is always written; wait for the slowest game in flight
to end.

The older `multi-rig.fish`, `summarize-rig.py`, `interpret_multi_csv.py`,
`interpret_multi_csv.fish`, `resummarize.fish` and the GitHub dispatch/retrieve
helpers have been removed; they drove the retired multi-game rig.
`rotate-report.fish` remains the shared numbered-archive helper.

`fish scripts/tune-negamax.fish <MAX_SCORE> [games]` builds optimized bots with
that evaluation budget and plays negamax against `ai_random`, reporting the
worst first and later response times and the number of later moves over 100 ms.
Raw per-game JSON and logs go to `work/tune-negamax/latest/`.

`fish scripts/instrument-one-game.fish bot1 "bot1 params" bot2 "bot2 params"`
runs one instrumented game through the single-game `gamerig` and publishes
`reports/instrument/report.html`.  It builds `bot1` with
`D=0 L=1 A=0 E=1 MAX_SCORE=450000` — optimized, because a debug build
(sanitisers, `-O0`) exceeds even the rig's relaxed 5000ms local guard and
forfeits.  That guard is a rig convenience, not the arena limit: a move may
take most of a second and still complete here, while the real arena allows
1000ms for the first response and 100ms for later ones, which this build does
not meet.  It runs
`gamerig --instrument 0`, and keeps the raw JSON in `work/instrument/latest/`.
A `Forfeit` is a technical failure, not a result: `gamerig` still exits 0, so
the script renders and publishes the report, keeps the raw files, prints the
forfeiting player and the rig Forfeit line, and exits 1 without printing
`PASS`; the report then shows a red banner under the heading.  A completed
non-forfeit game prints `PASS`.
The report shows one 9×9 board per instrumented move: the position before the
move, the opponent's last move (purple `LAST`), every evaluated candidate's root
score, our choice (yellow), and closed small boards badged X WON/O WON/DRAW.
A short run summary (date/time, bot identity, winner, win reason, totals)
follows.  `scripts/instrument-report.py <game.json> <report.html>` renders the
same report from a saved JSON; it refuses a JSON whose header has no
`instrumented` field.

The older CSV-driven renderer (`instrument-context.py`) and the
`turns/scores/games/moves.csv` files are gone, so
`scripts/solve-position.py --moves-csv` no longer has an `all-moves.csv` to
read.

To investigate a saved position without replaying the game, use the Python
workbench (zero-based row/column coordinates and zero-based `--ply`).  It needs
a move-trace CSV (`ply,player,row,col`); the instrument flow no longer writes
one, so supply a trace from another source:

```fish
python3 -B scripts/solve-position.py --moves-csv work/instrument/all-moves.csv \
    --ply 42 --compare 1,3 0,3 --fastest-win --explain-eval
```

`solve-position.py` independently applies the game rules and proves terminal
win/draw/loss against all replies; `--fastest-win` finds the earliest guaranteed
winning horizon, including an opponent trying to delay. `--explain-eval` mirrors
the current bot's one-ply score to expose why its preference differs. The
proposed MM-004 evaluator can be compared on the same moves with
`--experiment-eval` (optional `--ownership-bonus` and `--threat-bonus`);
this changes only the Python workbench, not the bot. The
default proof budget is 60 seconds in total (`--seconds` changes it), so an
unfinished result is explicitly marked unresolved. A custom position can be
loaded with `--position FILE` instead of the move CSV; see the script's opening
comment for the JSON format. The displayed continuation is one best-play line,
not necessarily the opponent's most delaying line.

All commands above are run from the repository root. The old CSV reports from
before this layout change were retained in `work/legacy-summary-reports/`.

## GitHub batch run

The [Bot versus orig workflow](../.github/workflows/bot-vs-orig.yml) runs
`scripts/multi-rig.py` on a runner and uploads `reports/` and
`work/multi/latest/` as one artifact. Dispatch and collect it with the GitHub
CLI:

```fish
gh workflow run bot-vs-orig.yml -f games=1000 -f base_seed=20261001 -f request_id=manual
gh run list --workflow bot-vs-orig.yml --limit 5
gh run download <run-id> --name bot-vs-orig-<run-id> --dir work/github-download
```

The guarded local dispatch/status/retrieve helpers and the `github-latest.html`
report were removed with the retired multi-game rig.
