# User-facing commands

All commands run from the repository root.

## Build variants

```sh
make ai_negamax D=<0|1> L=<0|1> A=<0|1> E=<0|1>
```

Builds `ai_negamax` with the four build flags, each `0` or `1`:

- `D` — debug: `1` uses `-O0` with Address/UB sanitizers, `0` builds optimised.
- `L` — local C&C / instrumentation (`LOCAL_RIG`).
- `A` — assertions (`BOARD_ASSERTS`).
- `E` — evaluation timeout (`EVALUATION_TIMEOUT` with the `MAX_SCORE` budget).

The chosen flags are appended to the bot's `--HELLO` identity, for example
`NM-003-R1-D0L1A0E1`, so every rig report records how the bot was compiled.
The target always rebuilds; `make clean` removes generated binaries.  `orig`
and `ai_random` always build optimised and ignore these flags.

## Board2 proof and vector tests

```sh
make test-board2
```

Checks the independent proof blobs when needed, then runs the assertion-enabled
Board2 vector tests.  `make test` runs this suite.

## Board2 million-game stress test

```sh
make test-board2-stress
```

Runs one million deterministic games through the assertion-enabled Board2
implementation and its independent reference model.

## Instrumented game and board report

```sh
fish scripts/instrument-one-game.fish ai_negamax "--seed 1" orig "--seed 1"
```

Builds the first bot with local instrumentation (`D=1 L=1 A=1 E=1`), runs one
`gamerig --instrument 0` game, and publishes `reports/instrument/report.html`:
one 9×9 board per instrumented move showing evaluated candidates and scores, our
choice and the opponent's last move, closed small boards, and a run summary
(date/time, winner, win reason).

To render a saved game directly, build an instrumented bot first:

```sh
make ai_negamax L=1 E=1
./bin/gamerig ./bin/ai_negamax '--seed 1' ./bin/orig '--seed 1' 0 --instrument 0 > game.json
python3 -B scripts/instrument-report.py game.json report.html
```

## Multi-line bot comparison

```sh
python3 scripts/multi-rig.py --p0 "./bin/ai_negamax" --p1 "./bin/orig" \
    --lines 4 --games-per-line 25 --seed 20261001
```

Runs both bots across concurrent lines in relaxed mode and prints a result
table (wins by 3IAR/Count/Forfeit and match percentage, draws counting half).
Before the run it asks both bots for their `--HELLO` identity and shows it in
the banner, live display and report; unless `--no-build` is given it rebuilds
the rig and both bot targets (`ai_negamax` as `D=0 L=0 A=0 E=1`).  The table is
also written to `reports/multi-latest.log`, rotating any existing file to
`reports/multi-latest.N.log`.

## Negamax evaluation-timeout tuning

```sh
make tune-negamax MAX_SCORE=32000
```

Builds `gamerig`, `ai_random` and a `D=0 L=0 A=0 E=1` `ai_negamax` with the
given `MAX_SCORE` budget, then runs 10 seeded negamax-versus-random games through
the single-game rig, recording the worst first and later response times and the
count of later moves over 100 ms, with raw JSON under `work/tune-negamax/latest/`.
Run the script directly for a different game count:

```sh
fish scripts/tune-negamax.fish 32000 20
```
