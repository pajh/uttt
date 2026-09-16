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

## Local rig control, version 1

Opt-in local builds (`make LOCAL_RIG=1 bin/ai_minimax`) define `LOCAL_RIG`
and include `local_rig.h`; normal `make` and `multi-rig.fish` do not.
`--HELLO` advertises `LOCAL_RIG=1`; the rig sends controls only to bots that
advertise this capability. The one-file CodinGame submission does not define
`LOCAL_RIG`, so every hook compiles away. Standard input and output retain the
unmodified CodinGame turn protocol in both builds.

The rig passes a bidirectional Unix socket as file descriptor 3. Before each
ordinary turn input, it sends `TURN n`, zero or more commands, and `END`, one
line each. After reading the ordinary input, the bot consumes this complete
control frame. The turn number is the bot's one-based turn count. Supported
commands are `FORCE row col`, `USCALE integer`, `COUNT_SCALE number`, and
`TIME_MS number`. Forced moves must be in the ordinary legal-action list and
are applied to the bot's normal board state. Settings persist for the game;
the current rig repeats configured settings each turn. There is no algorithm
selector yet because the bot currently has one search algorithm.

When the rig sends `INSTRUMENT`, the bot emits each completed root evaluation
as `SCORE turn depth row col score` and one compact `TURN_STATS` record per
turn with time, selected move/score, cache and search counters. It sends
`END turn` before its ordinary move. The rig drains socket and stdout
concurrently under one response deadline, writes optional `--scores-csv` and
`--instrument-csv` files, and treats malformed or missing instrument frames as
a failed response. Scores describe candidate moves, not every leaf evaluation.
The bot writes no instrument file. The channel is
local testing infrastructure, not a way to obtain extra CodinGame compute
time or send extra stdout lines.

Example: `./bin/gamerig -G1 --p0 ./bin/ai_minimax --p1 ./bin/orig
--force-opening 0,0 --rig-uscale 7 --rig-count-scale 1.2 --rig-time-ms 80
--scores-csv work/local-rig-scores.csv` after building with `LOCAL_RIG=1`.
The forced opening is sent as a
control command to capable bots; legacy bots still receive a restricted legal
list for that opening. `--force-prefix '4,4;3,3;0,0'` instead forces three
consecutive plies (for either starting player) and then returns control to the
bots. The rig rejects a prefix move that is illegal in the reached position.

## Important implementation constraints

`board.h` and `hashmap.h` contain definitions and globals, not just
declarations.  Include each only once per executable.  This is also why the
submission merger can inline them into one C file.

`src/legacy/` is evidence, not a code library.  Keep old sources byte-stable
unless a task explicitly concerns recovering or comparing a snapshot.  New bot
work belongs in `src/bots/` and shared game mechanics belong in `src/engine/`.
