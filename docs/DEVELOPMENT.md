# Development guide

## Everyday commands

```sh
make                 # supported bots and local referee
make test            # board2 engine regression suite
./bin/gamerig ./bin/ai_negamax '--seed 1' ./bin/ai_random '--seed 1' 0   # one game, JSON summary
```

`ai_negamax` builds with four flags, each `0` or `1`:
`make ai_negamax D=<0|1> L=<0|1> A=<0|1> E=<0|1>` selects debug
(sanitisers), local instrumentation, assertions and the evaluation timeout.
The chosen flags are appended to the bot's `--HELLO` identity (for example
`NM-003-R1-D0L1A0E1`) and therefore appear in every rig report.  `orig` and
`ai_random` always build optimised and ignore these flags.

`gamerig` is a single-game runner: five positional arguments
(`<p0-binary> <p0-args> <p1-binary> <p1-args> <start 0|1>`), an optional
`--relaxed <ms>` per-move hang cap (default 10000), and one JSON summary on
stdout. The arena 1000/100 ms limits only produce warnings; only the hang cap
forfeits a move. Both bots must answer `--HELLO`; a missing reply aborts the
run with exit status 2. See `docs/ARCHITECTURE.md`.

Use `make -B CFLAGS='-Wall -g -O0'` for a clean diagnostic rebuild.  Local
benchmark builds use `-march=native`; do not use that flag as evidence that a
submission will run on CodinGame hardware.

## Before changing behaviour

1. Start by reading `docs/ARCHITECTURE.md` and the relevant source-file header.
2. Keep board-rule changes in `src/engine/`; keep policy/search changes in
   `src/bots/ai_minimax.c`.
3. Add or update a focused regression in `tests/board2_test.c` for a rule or
   search correctness change.
4. Run `make test`, then a small rig batch before treating an experiment as a
   strength result.
5. Record meaningful experiment conclusions in `progress.md`, including bot
   configuration, game count, starting policy and technical failures.

## Comments and naming

Write comments for invariants, coordinate conventions, cache-key fields,
deadline behaviour and non-obvious performance choices.  Do not comment a
literal restatement of the next line.  Prefer names that expose ownership and
units: `next_player`, `budget_ms`, `closed_mask`, and `search_deadline` are
clearer than one-letter aliases outside tight loops.

The project currently uses `x = column`, `y = row` internally, while the arena
protocol is `row column`.  State the conversion at I/O boundaries rather than
letting it remain implicit.

## Submission and experiments

Submission generation has been removed from the makefile: there is no
`make submission` target until a bot is ready to submit, at which point the
single-file CodinGame build must be reconstructed.  `scripts/subst` is kept for
reference.  Never overwrite a recovered snapshot in `src/legacy/`.

The runner's timeout is a local safety guard, not an emulation of the arena.
Small batches are useful regression checks, but they are not strong statistical
evidence.  Keep raw outputs ignored by default and commit concise summaries or
deliberately selected published results instead.
