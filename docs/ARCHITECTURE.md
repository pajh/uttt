# Architecture

This repository contains a current Ultimate Tic-Tac-Toe bot, a local referee
for testing it, and several preserved recovery snapshots.  Only the current
bot, random baseline and referee are supported build targets.

## Directory map

| Path | Purpose | Status |
| --- | --- | --- |
| `src/bots/` | Current CodinGame bots: `ai_negamax.c` and `ai_random.c`; `ai_minimax.c` retained as source | Supported |
| `src/engine/` | Shared board representation and transposition cache | Included into one executable at a time |
| `src/rig/` | Local POSIX referee and match recorder | Supported locally |
| `src/legacy/` | Recovered historical submissions and incompatible bot versions | Preserve; do not casually modernise |
| `tools/` | Optional maintenance scripts (proof-table generation, artifact pruning) | Best-effort utilities |
| `tests/` | Current board/search regression suite | Supported |
| `docs/` | Project documentation | Source of process and architecture guidance |

Generated executables live in `bin/`. Generated readable reports belong in
ignored `reports/`; their logs, game tables and supporting data belong in
ignored `work/`. Do not commit either directory unless a result is deliberately
being published.

## Runtime flow

`gamerig` takes five positional arguments --- the p0 binary and argument string,
the p1 binary and argument string, and the starting player (0 or 1) --- plus an
optional `--relaxed <ms>` hang cap (default 10000 ms) and an optional
`--instrument <0|1>` player selector, and plays exactly one
game between two bot processes.  On every turn it sends the opponent move and
the legal actions in CodinGame's row/column protocol, in board order (the
single-game rig does not shuffle).  The current bot converts that protocol to
its internal `x = column`, `y = row` coordinates, updates `Board9`, and returns
one legal move.  The rig validates the move, updates its own `Board9`, and
prints one JSON summary to stdout when the game ends.  The arena limits
(1000/100 ms) are advisory and only produce a warning; a move is forfeited once
it exceeds the `--relaxed` cap.  After the game the rig stops and reaps both
bots, escalating from `SIGTERM` to `SIGKILL` if a bot ignores termination.

The board engine encodes every 3x3 board in base 3.  Lookup tables decode that
number to player bitmasks and cache ordinary-board line evaluations.  `Board9`
holds nine encoded small boards, the master board, and a mask of closed master
squares.  A closed square may mean either player won its small board or that
the small board was drawn, so the closed mask is part of search identity.

`ai_minimax.c` searches with a strict deadline.  Its transposition key includes
the complete board, directed destination, player to move and remaining depth.
The hash map is fixed-capacity so a move search does not perform allocations.

### gamerig JSON summary

```json
{
  "header": {
    "p0 command line": "./bin/ai_negamax --seed 1",
    "p1 command line": "./bin/ai_random --seed 1",
    "p0 HELLO": "NM-002-R1",
    "p1 HELLO": "RANDOM001",
    "date/time": "2026-09-21T14:05:03+01:00",
    "first player": 0
  },
  "moves": [
    {"moveno": 0, "player": 0, "row": 4, "col": 4, "time": 1}
  ],
  "result": {
    "winner": 1,
    "total moves": 1,
    "result type": "Forfeit",
    "total time": 1300
  }
}
```

- `result type` is one of `3inARow`, `CountVictory`, `draw`, `Forfeit`.
- `winner` is `0` or `1`, or `null` for a draw.  For `Forfeit`, `winner` is the
  opponent of the bot that failed.
- `time` and `total time` are whole milliseconds (`time` per accepted move,
  rounded to nearest; `total time` is the game's wall-clock duration).
- `moves` holds only accepted moves, in board order.
- With `--instrument <player>`, that player's searched moves carry a
  `candidates` array of `{"row", "col", "score"}` objects, one per root move the
  search compared; the played move is always one of them.  A move that ran no
  search (the opening move) has no `candidates` key.  The header then also has
  `"instrumented": <player>`; the key is absent when instrumentation was off, so
  a renderer can require it.
- Both bots must answer `--HELLO` with a non-empty version string.  If either
  does not, the rig names the offending player, exits 2, and plays no game, so
  no JSON is emitted.
- Exit status: 0 for any completed game, including a forfeit; 2 for a usage
  error or a missing `--HELLO` reply; 1 for a rig-internal failure.  A bot
  timeout, illegal move, crash or EOF is a forfeit, not a rig failure.

## Local rig control, version 2

Opt-in local bot builds (`make ai_negamax L=1`) define `LOCAL_RIG` and include
`local_rig.h`; normal bot builds do not.  Every `ai_negamax` `--HELLO` ends with
a compact build-flag suffix `-D<d>L<l>A<a>E<e>` (debug, local rig, asserts,
evaluation timeout), so a harness can detect the `L1` capability and a report
can record how the bot was compiled.  The one-file CodinGame submission does not
define `LOCAL_RIG`, so every hook compiles away, and standard input and output
keep the unmodified CodinGame turn protocol.

There is no separate channel.  A `LOCAL_RIG` bot accepts bracketed control
tokens on its ordinary stdin data lines and strips them before parsing the
numbers: `[I]` enables instrumentation for that turn, and `[F<row><col>]`
(e.g. `[F56]`) forces a move.  When instrumented, the bot appends one bracketed
candidate record to its move line: `row col [I {row,col,score}, ...]`, one
tuple per root move the search compared, in the bot's own row/column
coordinates.  This channel is local testing infrastructure, not a way to obtain
extra CodinGame compute time or send extra stdout lines.

`gamerig` drives this with `--instrument <0|1>`: it names the single player to
instrument (the value is the player index; omit the option to disable), checks
that the bot's `--HELLO` advertises `L1`, sends `[I]` on that player's
turn input, and turns the returned candidate record into a `candidates` array on
each JSON move object.  Instrumenting a bot that does not advertise the
capability is a usage error (exit 2).

Note: the older scripts that drove the fd-3 channel
(`scripts/instrument-one-game.fish` and friends) still target the retired
multi-game rig and are out of date.

## Important implementation constraints

`board.h` and `hashmap.h` contain definitions and globals, not just
declarations.  Include each only once per executable.  This is also why the
submission merger can inline them into one C file.

`src/legacy/` is evidence, not a code library.  Keep old sources byte-stable
unless a task explicitly concerns recovering or comparing a snapshot.  New bot
work belongs in `src/bots/` and shared game mechanics belong in `src/engine/`.
