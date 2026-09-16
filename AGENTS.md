# Ultimate Tic-Tac-Toe bot: working agreement

## Mission

This repository is an effort to improve a former Gold-league CodinGame
Ultimate Tic-Tac-Toe bot.  The immediate measurable goal is a **100–0 local
match result against the preserved `src/legacy/orig.c` bot**, with no illegal
moves, timeouts, malformed output, or other technical failures.

Treat that target as an experiment goal, not proof of arena rank.  A change is
not a strength improvement until it has a recorded, reproducible comparison.

## CodinGame submission constraints

- A bot reads the opponent's move and legal moves from standard input and
  writes exactly one legal `row column` move to standard output, followed by a
  newline and flush.  Diagnostic output goes only to standard error.
- Internally this project uses `x = column`, `y = row`; convert explicitly at
  the input/output boundary.
- The first response has a 1,000 ms limit and subsequent responses have a
  100 ms limit.  Keep an internal safety margin; the local rig's timeout is a
  guard, not a faithful arena simulation.
- CodinGame controls the GCC command line.  Source-level GCC optimisation
  pragmas are allowed and used here; do not assume local `-march=native` is
  portable to the arena.
- The submitted bot must be one C source file and remain under the 100,000
  character submission limit.
- Develop normally in `src/bots/ai_minimax.c` with the shared engine headers.
  Run `make submission` to use `subst`, which inlines `board.h` and `hashmap.h`
  into `bin/submission.c`.  Compile-check the generated file before treating it
  as submit-ready.

## Source boundaries

- New bot policy and search work belongs in `src/bots/ai_minimax.c`.
- Shared game rules, coordinate conversions and board representation belong in
  `src/engine/`.
- The local referee belongs in `src/rig/`.
- `src/legacy/` is historical evidence.  Do not refactor or "clean up" it
  while experimenting with the current bot; preserve it as the benchmark.
- Read `docs/ARCHITECTURE.md` before changing a subsystem, and add comments
  for invariants, units, coordinate conversion, cache identity and deadline
  behaviour—not narration of obvious code.

## Experiment protocol

1. State one hypothesis and change the smallest amount of code that tests it.
2. Run `make test`, then a small local technical smoke test.
3. Run a reproducible comparison against `orig` using a recorded command,
   configuration, game count, seed/start policy and bot build identity.
4. Add one concise entry to `progress.md`: hypothesis, implementation,
   result (W/L/D and technical failures), and decision (keep, revert, or
   investigate).  Do not paste raw logs or per-game tables into that file.
5. Keep raw data only long enough to support the conclusion.  Curated results
   may go under `published-results/`; the progress log is the durable record.

Raw experiment output should be written under `artifacts/runs/`.  It is ignored
by Git and can be safely pruned after the retention window.  Never automate
deletion of `progress.md`, `published-results/`, source files, or a run the user
has explicitly asked to preserve.

## Tool and context budget

Tool usage is expensive.  Work deliberately:

- Before calling a tool, identify the exact question it answers.  Prefer one
  targeted command over several exploratory calls, and request only the output
  needed to make the next decision.
- Do not re-read broad histories, huge logs, raw result trees, or whole source
  files when a symbol/line-range search is sufficient.  Start with `rg` and
  inspect a narrow range.
- Keep tool output compact.  Combine independent read-only checks in one call
  where that does not obscure the result; avoid repetitive status polling.
- For a simple, isolated batch/script edit, either ask the user to make it or
  use an available lower-cost coding agent with only the task-specific context.
  Do not delegate broad design history or full solver context just to edit a
  small file.  If no appropriate lower-cost agent is available, make the small
  edit directly and verify it proportionately.
- Reserve heavyweight investigation and long benchmark runs for a named
  hypothesis with a clear success criterion.
