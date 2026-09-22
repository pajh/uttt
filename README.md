# Local Ultimate Tic-Tac-Toe rig

This is a development repository for an Ultimate Tic-Tac-Toe CodinGame bot.
The supported code is deliberately separated from recovered snapshots and
one-off analysis tools:

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) explains the directory layout,
  board model and search flow.
- [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) gives the everyday build, test
  and experiment process.
- `src/bots/`, `src/engine/` and `src/rig/` contain supported code.
- `src/legacy/` preserves historical sources; it is not the place for new work.

Run the regression suite before a behaviour change:

```sh
make test
```

Build the bots and referee, then play one game and print its JSON summary:

```sh
make
./bin/gamerig ./bin/ai_negamax '--seed 1' ./bin/ai_random '--seed 1' 0
```

`gamerig` takes five positional arguments: the p0 binary, its argument string,
the p1 binary, its argument string, and the starting player (0 or 1). It plays
exactly one game. It also accepts an optional trailing `--relaxed <ms>` that
sets the per-move hang cap (default 10000 ms), and an optional
`--instrument <0|1>` that names the single player whose root candidate scores
are added to each of its moves as a `candidates` array (the bot must be a local
instrumentation build that advertises `L1` in its `--HELLO`). Every bot must
answer `--HELLO` with a non-empty version string; if either bot does not, the
rig reports which player failed and exits 2 without playing. The baseline bots
reply `ORIG001` (`src/legacy/orig.c`) and `RANDOM001` (`src/bots/ai_random.c`);
`ai_negamax` replies its identity with a build-flag suffix (for example
`NM-003-R1-D0L1A0E1`) that advertises the instrumentation capability. This is a
process-based rig: each bot receives
CodinGame-style turn input on stdin and writes its move to stdout. stderr
remains available for bot diagnostics. No recovered direct-call bot API is used.

The rig prints one JSON game summary to stdout; its schema is documented in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md). The arena limits (1000 ms first
response, 100 ms thereafter) are advisory: a slower reply is accepted and
logged, and only a move exceeding the `--relaxed` cap forfeits the game.
Exit status is 0 for any completed game, including a forfeit; 2 for a usage
error or a missing `--HELLO` reply; and 1 for a rig-internal failure. A bot
that times out, moves illegally, crashes or hits EOF is recorded as a forfeit,
not a rig error. Local compile options can be set through `CFLAGS` and
`LOCAL_ARCH`; the default dialect is `-std=gnu17`. To rebuild with different
local options, use `make -B CFLAGS='-Wall -g -O0'`, for example. Source pragmas
can override command-line optimization choices.

The rig closes pipes, terminates and reaps both bot processes after the game.
The multi-game batch scripts that expected the old `-G`/CSV interface are
currently out of date and will be rewritten separately.

`src/bots/ai_minimax.c` is retained as source but is no longer a build target;
current work targets `src/bots/ai_negamax.c` on the `board2` engine.
`src/legacy/ai_base.c` and `src/legacy/ai_heuristic.c` need their historical
board API recovered or migrated before they build.

## Submission

Single-file submission generation has been removed from the makefile while the
bot is rebuilt on the `board2` engine, so there is no `make submission` target
at present. `scripts/subst` is kept for reference, but a CodinGame submission
will have to be reconstructed and regenerated once a bot is ready to submit.
`src/legacy/orig.c` and recovered `src/legacy/cg_tictac.c` are preserved; the
obsolete copy-to-home-directory action has been removed.

See REVIEW.md for the original inventory and code review. Its initial build and rig observations describe the recovered tree before these repairs.

## Parallel benchmarks

Run two bots across concurrent lines and compare their relative performance:

```sh
python3 scripts/multi-rig.py --p0 "./bin/ai_negamax" --p1 "./bin/orig" \
    --lines 4 --games-per-line 25 --seed 20261001
```

Each line derives its own seed and advances it per game; lines alternate the
starting player, so an even number of lines gives each bot half the first moves.
Games run in relaxed mode. On a terminal a live coloured line per worker shows
progress; `--no-interactive` suppresses it. The result table is printed and
written to `reports/multi-latest.log` (an existing file is rotated to
`reports/multi-latest.N.log`), with raw per-line JSON and logs in
`work/multi/latest/`. See [`scripts/README.md`](scripts/README.md).
