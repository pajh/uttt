# Development guide

## Everyday commands

```sh
make                 # supported bots and local referee
make test            # board and search regression suite
make submission      # generate bin/submission.c
make tools           # optional local analysis tool(s)
./bin/gamerig -G10   # alternating-start baseline match batch
```

Use `make -B CFLAGS='-Wall -g -O0'` for a clean diagnostic rebuild.  Local
benchmark builds use `-march=native`; do not use that flag as evidence that a
submission will run on CodinGame hardware.

## Before changing behaviour

1. Start by reading `docs/ARCHITECTURE.md` and the relevant source-file header.
2. Keep board-rule changes in `src/engine/`; keep policy/search changes in
   `src/bots/ai_minimax.c`.
3. Add or update a focused regression in `tests/current_bot.c` for a rule or
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

`make submission` uses `subst` to inline the current bot and both implementation
headers into `bin/submission.c`.  Inspect or compile that generated file; never
overwrite a recovered snapshot in `src/legacy/`.

The runner's timeout is a local safety guard, not an emulation of the arena.
Small batches are useful regression checks, but they are not strong statistical
evidence.  Keep raw outputs ignored by default and commit concise summaries or
deliberately selected published results instead.
