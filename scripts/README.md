# Run and report scripts

`multi-rig.fish` plays `ai_minimax` against `orig` on separate seed ranges.
The configuration block at the top names both bot commands, the number of
simultaneous workers, and games per worker. Its local default is 4 × 25.

From the repository root:

```fish
fish scripts/multi-rig.fish --interactive
```

`--interactive` shows a live line per worker. Green `3` and `C` are player-0
wins by three in a row and small-board count; red means player-0 losses.
White `D` is a draw, and `F` marks a technical forfeit. Each finished worker
shows its win percentage. Omit the flag for plain output.

Before the long batch, the script rebuilds the normal `ai_minimax`, plays one
game per worker, checks summarization, and validates HTML rendering without
writing a preflight report. The full run then
writes game tables and logs into `work/latest/`. It prints the combined result
and creates the final `reports/latest-analysis.html`. Previous HTML reports
become `reports/latest-analysis.N.html`, with supporting CSVs and logs in
`work/latest.N/`. Only human-readable HTML goes in `reports/`; CSVs are data
and stay in `work/`. Both directories are local and ignored by Git.
The run also records start and finish epoch seconds in `work/latest/run-timing.csv`.
The HTML displays these as local time, its generation time, and elapsed
`MMMM:SS` at the top. Older runs without a timing file show “not recorded”.
The HTML shows wins and
losses by type, draws, and per-bot search totals. The bot sends one cumulative
stats record at game end in local rig runs, whether or not `INSTRUMENT` is on.
`orig` does not report these figures, so its metrics display as unavailable.
Search time covers the timed evaluation functions only; positions scored are
leaf scoring calls, and positions/s uses the aggregate totals.

Each game row records its numeric `seed` and `starting_player`. The rig seeds
its legal-move shuffle and gives the same game seed to `ai_minimax` (`CG_SEED`)
and `orig` (`--seed`). `--p1-game-seed` enables the latter for the normal
matchup. The seed changes by one for each game in a worker. To rerun one saved
game with the same binaries, use the row's seed and starting player, for
example:

```fish
./bin/gamerig -G1 --p0 ./bin/ai_minimax --p1 ./bin/orig \
    --p1-game-seed --seed 20261001 --p0-first \
    --games-csv work/replay-games.csv --moves-csv work/replay-moves.csv
```

Use `--p1-first` when `starting_player` is 1. Compare `trace_hash` or the move
CSV to see whether the rerun matched. Seeds alone do **not** guarantee an exact
replay: both bots stop some searches by elapsed wall-clock time. Repeated
one-game checks on two seeds and both starting players matched in two of four
cases; the other two first diverged at an `orig` move from the same board.
Deterministic search budgeting would be a separate behavioural change.

To regenerate the report from saved data without playing games:

```fish
fish scripts/resummarize.fish
fish scripts/resummarize.fish work/latest.2
fish scripts/interpret_multi_csv.fish
fish scripts/interpret_multi_csv.fish work/latest.2/summary.csv
```

`resummarize.fish` first rebuilds `summary.csv` and `games.csv` in the supplied
run directory, then calls `interpret_multi_csv.fish` to publish the HTML.
`interpret_multi_csv.fish` reads `work/latest/summary.csv` by default, or a
supplied path, and rotates the existing HTML before publishing. These commands
never play games. `summarize-rig.py` combines worker game CSVs and identities;
`interpret_multi_csv.py` renders the self-contained chart and tables.
`rotate-report.fish` is the shared numbered-archive helper.

For a single detailed game, use `fish scripts/instrument-one-game.fish`.
It builds the same bot with `INSTRUMENT`, runs one game, and uses
`instrument-report.py` to publish `reports/instrument/report.html`; raw turn
CSV and logs remain in `work/instrument/`.

All commands above are run from the repository root. The old CSV reports from
before this layout change were retained in `work/legacy-summary-reports/`.

## Standard 1,000-game GitHub run

From the repository root:

```fish
fish scripts/start-github-1000.fish
fish scripts/github-status.fish
fish scripts/retrieve-github-latest-1000.fish
xdg-open reports/github-latest.html
```

To run 100 games instead, substitute
`fish scripts/start-github-1000.fish 100` for the first command; do not run
both start commands.

`start-github-1000.fish` does **not** commit or push. It refuses to launch if
any source, build, workflow, or reporting file used by the runner has local
changes, or if the current branch's HEAD differs from `origin/<branch>`.
Commit and push those files yourself, then rerun the start command. It uses
`gh workflow run` with 1,000 games by default, or the positive multiple of four
you pass (for example, `100`). The value is a runtime workflow input, not a
committed file change. Seed 20261001 and a unique request ID are also sent;
the request and game count are saved in `work/github-request.txt`. This requires GitHub CLI (`gh`) to be
installed and authenticated. The workflow checks for fish on `ubuntu-latest`
and installs it if necessary.

`github-status.fish` checks that exact request: queued, running, completed,
or failed. It prints the run URL. If the run has completed successfully,
`retrieve-github-latest-1000.fish` downloads its artifact, verifies the run
ID and requested game count, then publishes `reports/github-latest.html` and
`work/github-latest/`. Previous GitHub results become numbered
`github-latest.N.html` / `work/github-latest.N/`. Local
`reports/latest-analysis.html` and `work/latest/` are untouched. The report
header identifies the GitHub Actions run and links to it. No command here
automatically opens the report; `xdg-open` is explicit.

The [Bot versus orig workflow](../.github/workflows/bot-vs-orig.yml) uploads
the HTML and raw `work/latest/` data as one artifact. Its run start/finish and
generation timestamps use the runner's local timezone (normally UTC), shown
explicitly in the HTML.
