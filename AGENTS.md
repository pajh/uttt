# Ultimate Tic-Tac-Toe bot: working agreement

## Mission

This repository is an effort to improve a former Gold-league CodinGame
Ultimate Tic-Tac-Toe bot.  The immediate measurable goal is a **100–0 local
match result against the preserved `src/legacy/orig.c` bot**, with no illegal
moves, timeouts, malformed output, or other technical failures.

Treat that target as an experiment goal, not proof of arena rank.  A change is
not a strength improvement until it has a recorded, reproducible comparison.

## Project character and proportionality

This is a fun, educational software-learning project, not a production or
DevOps system. Nothing operational, commercial, confidential, or safety
critical depends on it. The priorities are clean, compact, well-documented C;
clear algorithmic reasoning; useful experiments; and learning from failures.

- Prefer the smallest direct implementation that makes the current experiment
  understandable. Do not add frameworks, infrastructure, generic machinery,
  compatibility layers, or production hardening for hypothetical future use.
- Do not expand a request into exhaustive edge-case handling. Cover ordinary
  inputs and failures that are useful for diagnosing the experiment. If an
  unlikely condition occurs, a clear failure that can be fixed then is usually
  better than speculative defensive code now.
- There is no repository-specific security threat model. Do not spend time or
  code on adversarial-input handling, security hardening, sandbox design,
  permissions architecture, secret management, or supply-chain analysis unless
  the user explicitly asks about one of them.
- Scripts are disposable experiment tools, not production services. Keep them
  short and legible; preserve useful evidence on failure, but do not build
  deployment systems, generalized orchestration, retry frameworks, or elaborate
  observability around them.
- Correct game rules, legal CodinGame protocol output, readable C invariants,
  and reproducible stated experiments matter. Proving robustness under every
  theoretical condition does not.
- When scope begins growing beyond the obvious implementation, stop and discuss
  it. Do not consume time or usage anticipating requirements the user did not
  ask for.

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
- New board code targets gnu17. The aliases `u8`, `i8`, `u16`, and `i16` are
  defined once in `src/engine/support.h`, mapped to matching `<stdint.h>`
  exact-width types. `uint` is not a standard C type, and no GCC pragma
  selects the C language dialect.
- Prefer `bool` for predicate returns, boolean parameters, and boolean
  state/local variables. `src/engine/support.h` is the sole home of shared
  generic aliases and generic assertion support. `BOARD_ASSERTS` is undefined
  or 0 by default and can be enabled with `#define BOARD_ASSERTS 1` before
  including `support.h` or `make BOARD_ASSERTS=1`. Disabled assertions expand
  to no-evaluation `((void)0)`; assertion conditions must be side-effect-free.
- In production code, prefer readable semantic questions and names, or
  documented small helpers, for bit tricks. Explain non-obvious operations in
  comments. Consider `__builtin_popcount` for bit counts and `__builtin_ctz`
  for the index of a known-nonzero bit (`ctz(0)` is undefined); do not apply
  blanket intrinsic substitutions, and require measurement for performance
  claims.
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

The normal bot can still report one cumulative end-of-game local stats record
(timed search microseconds, scored positions, cache hits), but the current
single-game `gamerig` does not request or store it. `scripts/multi-rig.py`
summarizes wins, losses, draws and technical forfeits from the per-game JSON; it
does not report search totals.

Opt-in `L=1` builds of `ai_negamax` carry local instrumentation documented in
`docs/ARCHITECTURE.md`. Keep its protocol and telemetry implementation in
`src/bots/local_rig.h`, with only small hooks in the bot. Version 2 is in-band:
the bot strips bracketed control tokens such as `[I]` and `[F56]` from its
ordinary stdin data lines and appends one `[I {row,col,score}, ...]` candidate
record to its move line. `--HELLO` appends the build flags and advertises `L1`
when instrumentation is compiled in, and `gamerig --instrument <0|1>` names the
single instrumented player. CodinGame submission
builds compile the hooks away; never put rig commands on the game's stdin or
extra records on move stdout in a non-`LOCAL_RIG` build. Forced moves and root
candidate scores are the first uses of this common local protocol. Ordinary
multi-rig runs keep the plain build and the existing cumulative `@GAME_STATS`
collection. The retired one-game scripts still target the old fd-3 channel.

Game CSV rows record the rig seed and starting player. The rig passes that
same seed to `ai_minimax` and, with `--p1-game-seed`, to `orig --seed`. Keep
these fields with any interesting game. A fixed seed is not a guarantee of
move-for-move replay while either bot uses wall-clock search deadlines; verify
replay using the recorded move trace or `trace_hash`.

## Tool output convention

- New scripts should be written in fish, except where a tool consumes the rig's
  JSON or otherwise needs structured parsing, in which case Python (standard
  library only) is preferred.  A repeatable tool command should always write the
  same readable report name, such as `reports/multi-latest.log` or
  `reports/instrument/report.html`.
- Before replacing `name.ext`, source `scripts/rotate-report.fish` and call
  `rotate_report name.ext` (or reproduce its numbering in Python).  It finds the
  highest positive integer `N` in `name.N.ext` and moves the current report to
  `name.(N+1).ext`.  The new report then takes the original, stable name.  Use
  this for readable output files, not raw intermediate data.
- Keep intermediate files under `work/`. A new run clears its preflight files
  and may archive the prior raw run as `work/multi/latest.N/`; all of `work/`
  remains cleanable. If a run fails, temporarily retain its raw JSON and logs so
  the games can be re-read without replaying them.
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

### Design, delegation and execution workflow

- The primary agent is the C designer and reviewer. Begin a behavioural or
  structural C task with discussion, not implementation: define the term or
  algorithm, analyse how it maps onto the current program, identify invariants
  and trade-offs, and present concrete design options. Continue that discussion
  until the user agrees which option to implement. A request to discuss,
  analyse, review, or plan does not authorize edits, compilation, tests, or
  runs.
- Treat C source as a constrained resource: justify interfaces, state, branches,
  and abstractions, and fight unnecessary lines rather than casually adding
  scaffolding. The primary agent owns the design and the final code review; it
  does not silently turn a design conversation into an implementation session.
- Once the user approves a C design, delegate the bounded edit to
  `gpt-5.6-luna` with zero inherited conversation turns. The brief must specify
  the exact files and functions in scope, the complete allowed function
  prototypes, required invariants and behaviour, forbidden changes, and stop
  conditions. Luna has no authority to add, remove, rename, or alter a function
  prototype unless that exact prototype change appears in the primary agent's
  user-approved brief. Ambiguity is a stop condition, not permission to invent.
- Dispatch Luna once with the complete brief and let it finish without repeated
  polling or live direction. Tell the user when it is dispatched. Because the
  workspace is shared, no other agent edits the same files concurrently. When
  Luna returns, the primary agent reviews the diff against the agreed design
  before proposing any compilation or execution.
- OpenCode owns scripting changes and all compilation, test, benchmark,
  monitoring, summarisation, and reporting work. Give it a separately approved,
  bounded brief containing exact commands or parameters, expected identities,
  seed/start policy, outputs, and stop conditions. If OpenCode is unavailable,
  stop and tell the user; do not silently substitute the primary agent or Luna.
- Every transition is explicit: discussion -> approved design -> Luna C edit ->
  primary review -> separately approved OpenCode verification/run. Do not begin
  the next stage merely because it is a conventional follow-up. Keep the user
  informed at stage boundaries; never disappear into an unrequested compile,
  test, or match batch.
- Long match batches, commits, pushes, external changes, and report publication
  always require specific authorization. Evaluate a trial by correctness,
  rework, and available usage or elapsed-time evidence; do not claim guaranteed
  quota savings.
- `make test` plus a short local rig smoke check is the normal quick technical
  gate after an approved `ai_negamax` change, but running it still belongs to the
  separately approved OpenCode verification stage. It is not a strength test.
  The C design should call for one or two targeted assertions for changed
  behaviour or a position whenever possible; the existing unit suite alone does
  not establish that a heuristic improved.
- Use the reusable zero-history briefs in `scripts/README.md` only after adapting
  them to this staged workflow. The primary agent owns experimental
  interpretation and reviews diffs and evidence before recommending keep,
  revert, or further investigation.
- A request to *plan* an experiment does not authorize a run. A request to run
  on GitHub authorizes dispatch with the stated parameters, but not a blind
  commit or push of unrelated changes. Check the staged/dirty diff, branch,
  remote, and launcher preconditions; stop and report the exact mismatch if
  the required source is not already committed and pushed. Commit/push only
  when separately requested, and scope those operations to reviewed files.
