# Architecture

This repository contains a current Ultimate Tic-Tac-Toe bot, a local referee
for testing it, and several preserved recovery snapshots.  Only the current
bot, random baseline and referee are supported build targets.

## Directory map

| Path | Purpose | Status |
| --- | --- | --- |
| `src/bots/` | Current CodinGame bots: `ai_minimax.c` and `ai_random.c` | Supported |
| `src/engine/` | Shared board representation and transposition cache | Included into one executable at a time |
| `src/rig/` | Local POSIX referee and match recorder | Supported locally |
| `src/legacy/` | Recovered historical submissions and incompatible bot versions | Preserve; do not casually modernise |
| `tools/c/` | Optional replay, cache and SIMD experiments | Best-effort utilities |
| `tests/` | Current board/search regression suite | Supported |
| `docs/` | Project documentation | Source of process and architecture guidance |

Generated executables live in `bin/`. Generated readable reports belong in
ignored `reports/`; their logs, game tables and supporting data belong in
ignored `work/`. Do not commit either directory unless a result is deliberately
being published.

## Runtime flow

`gamerig` starts two bot processes.  On every turn it sends the opponent move
and the legal actions in CodinGame's row/column protocol.  The current bot
converts that protocol to its internal `x = column`, `y = row` coordinates,
updates `Board9`, and returns one legal move.

The board engine encodes every 3x3 board in base 3.  Lookup tables decode that
number to player bitmasks and cache ordinary-board line evaluations.  `Board9`
holds nine encoded small boards, the master board, and a mask of closed master
squares.  A closed square may mean either player won its small board or that
the small board was drawn, so the closed mask is part of search identity.

`ai_minimax.c` searches with a strict deadline.  Its transposition key includes
the complete board, directed destination, player to move and remaining depth.
The hash map is fixed-capacity so a move search does not perform allocations.

## Important implementation constraints

`board.h` and `hashmap.h` contain definitions and globals, not just
declarations.  Include each only once per executable.  This is also why the
submission merger can inline them into one C file.

`src/legacy/` is evidence, not a code library.  Keep old sources byte-stable
unless a task explicitly concerns recovering or comparing a snapshot.  New bot
work belongs in `src/bots/` and shared game mechanics belong in `src/engine/`.
