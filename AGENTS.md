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

- `src/` contains the C source for the bots, shared game engine, and local
  game runner. Active C work belongs here.
- `scripts/` contains Python and fish scripts for building, running, testing,
  summarizing, and retrieving results. Use Bash only when needed.
- `reports/` contains human-readable reports from runs and investigations.
  Each report type has a stable `latest` name; previous readable reports may
  be numbered for history. CSV data and logs do not belong here.
- `work/` is scratch space for raw CSVs, logs, and intermediate files. It must
  always be safe to clean. Keep data here only while it may help answer more
  detailed questions about a report; promote any evidence that must survive
  cleanup to a deliberate durable location first.
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

Generated reports belong in ignored `reports/`; disposable supporting data
belongs in ignored `work/`. A report must remain understandable without its
scratch data. If `progress.md` cites raw evidence that must persist, put that
evidence under `published-results/` and update the citation before cleaning
`work/`. Never automate deletion of `progress.md`, `published-results/`, source
files, or a run the user explicitly asked to preserve.

The normal bot also reports one cumulative end-of-game local stats
record (timed search microseconds, scored positions, cache hits), independent
of C&C instrumentation; the rig stores it in each game CSV row. `scripts/summarize-rig.py`
totals these and computes positions/s from the totals. Use
`fish scripts/interpret_multi_csv.fish` to render the latest summary as HTML,
or pass a specific summary CSV path.

Opt-in `LOCAL_RIG=1` builds of `ai_minimax` use a separate version-1 rig C&C socket documented
in `docs/ARCHITECTURE.md`. Keep its protocol and telemetry implementation in
`src/bots/local_rig.h`, with only small hooks in the bot. CodinGame submission
builds compile the hooks away; never put rig commands on the game's stdin or
extra records on move stdout. Forced moves, score events and experiment
settings are the first uses of this common local protocol. Ordinary multi-rig
runs keep the plain build and the existing cumulative `@GAME_STATS` collection;
root-score C&C telemetry would add I/O to every search turn. The one-game
instrument path sends `INSTRUMENT` over C&C and has the rig write raw
turn/score CSVs; the bot itself must not open a report file.

Game CSV rows record the rig seed and starting player. The rig passes that
same seed to `ai_minimax` and, with `--p1-game-seed`, to `orig --seed`. Keep
these fields with any interesting game. A fixed seed is not a guarantee of
move-for-move replay while either bot uses wall-clock search deadlines; verify
replay using the recorded move trace or `trace_hash`.

## Tool output convention

- New scripts should be written in fish.  A repeatable tool command should
  always write the same readable report name, such as
  `reports/latest-analysis.html` or `reports/instrument/report.html`.
- Before replacing `name.ext`, source `scripts/rotate-report.fish` and call
  `rotate_report name.ext`.  It finds the highest positive integer `N` in
  `name.N.ext` and moves the current report to `name.(N+1).ext`.  The new
  report then takes the original, stable name.  Use this for readable output
  files, not raw intermediate data.
- Keep intermediate files under `work/`. A new run clears its preflight files
  and may archive the prior raw run as `work/latest.N/`; all of `work/` remains
  cleanable. If a run or summarizer fails, temporarily retain its raw files so
  `fish scripts/resummarize.fish <run-directory>` can retry without replaying games.
- Run a short end-to-end preflight through parsing and an in-memory HTML
  rendering check before starting a long batch; do not write a preflight
  report. Never leave a summarizer's first execution
  until after the expensive work.

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

### Experimental delegation workflow

- For bounded routine coding and repository operations, the primary design/review
  agent should try a `gpt-5.6-luna` sub-agent with zero inherited conversation
  turns and a short, self-contained brief covering scope, relevant files,
  constraints, verification, and stop conditions. Do not provide broad history.
- Because the workspace is shared, avoid simultaneous edits to the same files;
  the primary agent reviews the resulting diff and evidence. Keep open-ended
  design and algorithm discussion with the primary agent.
- Do not run long match batches, commit, push, or make external changes unless
  specifically authorized. Evaluate the trial by correctness, rework, and
  available usage or elapsed-time evidence; do not claim guaranteed quota
  savings.
- After a change to `ai_minimax`, run `fish scripts/check-candidate.fish` as the
  quick technical gate. It is not a strength test. Add one or two targeted
  assertions for the changed behaviour or position whenever possible; the
  existing unit suite alone does not establish that a heuristic improved.
- Use the reusable, zero-history Luna briefs in `scripts/README.md` for three
  bounded jobs: prepare an experiment and its evidence, operate an explicitly
  requested GitHub run, or gather and summarize evidence read-only. Give the
  worker the hypothesis, exact files, expected bot ID, seed/game policy, and
  stop conditions. The primary agent owns experimental interpretation and
  reviews diffs and results before recommending keep/revert.
- A request to *plan* an experiment does not authorize a run. A request to run
  on GitHub authorizes dispatch with the stated parameters, but not a blind
  commit or push of unrelated changes. Check the staged/dirty diff, branch,
  remote, and launcher preconditions; stop and report the exact mismatch if
  the required source is not already committed and pushed. Commit/push only
  when separately requested, and scope those operations to reviewed files.
