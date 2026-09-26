#!/usr/bin/env python3
"""Run a pair of bots across concurrent lines and report relative performance.

Each line runs one gamerig game at a time for --games-per-line games; lines run
concurrently.  Lines alternate the starting player by line number, so with an
even number of lines each bot starts half the games.  A per-line seed is derived
from the base seed and advances once per game; both bots receive that game seed.
Games run in relaxed mode, so only a move past --relaxed is forfeited.

Before the run both bots are asked for their `--HELLO` identity, which is shown
in the banner, the live display, the result table and the run's config.txt.  By
default the rig and both bot targets are rebuilt; ai_negamax is built optimised,
without local C&C or assertions, with the evaluation timeout enabled
(D=0 L=0 A=0 E=1).

The result table is printed to the console and written to reports/multi-latest.log
(an existing file of that name is rotated to multi-latest.N.log first).

Ctrl-C during the games stops new games only: every gamerig game runs in its own
process session, so a terminal Ctrl-C never reaches the games in flight.  Those
finish and keep their JSONL records, the result table is written as INTERRUPTED
with the completed count and the raw directory, and the run exits 130.
"""
import argparse
import json
import os
import queue
import shlex
import signal
import subprocess
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAMERIG = ROOT / "bin" / "gamerig"
WORK_BASE = ROOT / "work" / "multi"
LOG_PATH = ROOT / "reports" / "multi-latest.log"
SEED_STRIDE = 1_000_000

GLYPH = {"3inARow": "3", "CountVictory": "C", "Forfeit": "F"}
COLOR = {"p0": "\033[32m", "p1": "\033[31m", "draw": "\033[37m", "pending": "\033[37m"}
RESET = "\033[0m"

# Timing field: a 10-cell background bar, 10 ms per cell, with the right-justified
# time overlaid in white.  Under 100 ms the filled cells are green, at 100 ms and
# above the whole bar turns red; unused cells stay yellow.
BAR_CELLS = 10
BAR_FAST = "\033[42m"
BAR_SLOW = "\033[41m"
BAR_UNUSED = "\033[43m"
BAR_TEXT = "\033[97m"


def positive_int(text):
    value = int(text)
    if value < 1:
        raise argparse.ArgumentTypeError("must be at least 1")
    return value


def env_default(name, fallback):
    value = os.environ.get(name)
    return value if value is not None else fallback


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--p0", default=env_default("RIG_P0", "./bin/ai_negamax"),
                        help="player 0 command, without a seed (default: %(default)s)")
    parser.add_argument("--p1", default=env_default("RIG_P1", "./bin/orig"),
                        help="player 1 command, without a seed (default: %(default)s)")
    parser.add_argument("--lines", type=positive_int,
                        default=env_default("RIG_LINES", "4"),
                        help="concurrent lines (default: %(default)s)")
    parser.add_argument("--games-per-line", type=positive_int,
                        default=env_default("RIG_GAMES_PER_LINE", "25"),
                        help="games per line (default: %(default)s)")
    parser.add_argument("--seed", type=int, default=int(env_default("RIG_BASE_SEED", "20261001")),
                        help="base seed; each line and game advances it (default: %(default)s)")
    parser.add_argument("--relaxed", type=positive_int,
                        default=env_default("RIG_RELAXED", "5000"),
                        help="per-move hang cap in ms (default: %(default)s)")
    parser.add_argument("--no-build", action="store_true",
                        help="do not build the bots and rig before running")
    parser.add_argument("--no-interactive", action="store_true",
                        help="do not draw the live progress display")
    args = parser.parse_args(argv)
    # argparse leaves string env defaults unvalidated.
    args.lines = positive_int(args.lines)
    args.games_per_line = positive_int(args.games_per_line)
    args.relaxed = positive_int(args.relaxed)
    return args


def prepare_command(command, seed):
    """Splits a bot command and forces its per-game seed."""
    parts = shlex.split(command)
    if not parts:
        raise SystemExit("Empty bot command")
    binary = parts[0]
    args = []
    index = 1
    while index < len(parts):
        if parts[index] == "--seed":
            index += 2
            continue
        args.append(parts[index])
        index += 1
    args += ["--seed", str(seed)]
    return binary, " ".join(args)


def resolve_binary(binary):
    path = Path(binary)
    return path if path.is_absolute() else ROOT / path


def target_for(command):
    """Maps a bot command to its repository make target, or None."""
    binary = shlex.split(command)[0]
    try:
        return str(resolve_binary(binary).relative_to(ROOT))
    except ValueError:
        return None


def build_commands(args):
    """Make commands for the rig and both bots; ai_negamax uses D=0 L=0 A=0 E=1."""
    targets = ["bin/gamerig"]
    for command in (args.p0, args.p1):
        target = target_for(command)
        if target and target not in targets:
            targets.append(target)
    commands = []
    for target in targets:
        build = ["make", target]
        if target == "bin/ai_negamax":
            build += ["D=0", "L=0", "A=0", "E=1"]
        commands.append(build)
    return commands


def probe_hello(command):
    """Returns a bot command's first --HELLO line, or None on failure."""
    binary = shlex.split(command)[0]
    try:
        proc = subprocess.run([str(resolve_binary(binary)), "--HELLO"],
                              cwd=ROOT, capture_output=True, text=True)
    except OSError:
        return None
    if proc.returncode != 0:
        return None
    text = proc.stdout.strip()
    return text.splitlines()[0].strip() if text else None


def rotate_log():
    LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    if not LOG_PATH.exists():
        return
    number = 1
    while (LOG_PATH.parent / f"{LOG_PATH.stem}.{number}{LOG_PATH.suffix}").exists():
        number += 1
    LOG_PATH.rename(LOG_PATH.parent / f"{LOG_PATH.stem}.{number}{LOG_PATH.suffix}")


def archive_work():
    WORK_BASE.mkdir(parents=True, exist_ok=True)
    latest = WORK_BASE / "latest"
    if latest.exists():
        number = 1
        while (WORK_BASE / f"latest.{number}").exists():
            number += 1
        latest.rename(WORK_BASE / f"latest.{number}")
    latest.mkdir()
    return latest


def on_interrupt(stop, interrupt):
    """Builds the SIGINT handler for a game batch.

    The first signal sets `interrupt` and `stop`: no line starts another game, and
    the driver keeps collecting events until every line is done, so each game in
    flight keeps its record.  Later signals are absorbed, so the report is still
    written; the handler stays installed through that and the process then exits.
    The notice goes to stderr, which leaves the live display on stdout intact.
    """
    def handler(signum, frame):
        if interrupt.is_set():
            return
        interrupt.set()
        stop.set()
        print("interrupt: no new games; in-flight games finish first", file=sys.stderr)
    return handler


def run_line(line_no, args, run_dir, events, stop):
    line_seed = args.seed + (line_no - 1) * SEED_STRIDE
    start = 0 if line_no % 2 == 1 else 1
    jsonl = run_dir / f"line-{line_no}.jsonl"
    log = run_dir / f"line-{line_no}.log"
    played = 0
    try:
        with jsonl.open("w") as out, log.open("w") as errlog:
            for game in range(args.games_per_line):
                if stop.is_set():
                    break
                game_seed = line_seed + game
                p0_bin, p0_args = prepare_command(args.p0, game_seed)
                p1_bin, p1_args = prepare_command(args.p1, game_seed)
                command = [str(GAMERIG), p0_bin, p0_args, p1_bin, p1_args, str(start),
                           "--relaxed", str(args.relaxed)]
                # Own process session: a terminal Ctrl-C reaches this driver only,
                # never the game in flight, which must run to completion.
                proc = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                      start_new_session=True)
                errlog.write(f"--- game {game} seed {game_seed} start {start} rc {proc.returncode}\n")
                errlog.write(proc.stderr)
                errlog.flush()
                if proc.returncode != 0:
                    events.put(("error", line_no, game, game_seed, proc.returncode))
                    stop.set()
                    return
                try:
                    game_json = json.loads(proc.stdout)
                except json.JSONDecodeError:
                    events.put(("error", line_no, game, game_seed, "invalid JSON"))
                    stop.set()
                    return
                record = {"line": line_no, "game": game, "seed": game_seed,
                          "start": start, **game_json}
                out.write(json.dumps(record) + "\n")
                out.flush()
                played += 1
                events.put(("game", line_no, record))
    finally:
        events.put(("done", line_no, played))


def symbol_for(record):
    winner = record["result"]["winner"]
    kind = record["result"]["result type"]
    if winner is None:
        return "D", COLOR["draw"]
    glyph = GLYPH.get(kind, "?")
    return glyph, COLOR["p0"] if winner == 0 else COLOR["p1"]


def timing_field(ms):
    """A 10-cell bar (10 ms per cell) with the right-justified time over it."""
    text = f"{ms}ms"
    if len(text) > BAR_CELLS:
        text = text[-BAR_CELLS:]
    padded = text.rjust(BAR_CELLS)
    if ms >= BAR_CELLS * 10:
        filled, fill = BAR_CELLS, BAR_SLOW
    else:
        filled, fill = ms // 10, BAR_FAST
    parts = []
    index = 0
    while index < BAR_CELLS:
        background = fill if index < filled else BAR_UNUSED
        end = index
        while end < BAR_CELLS and (end < filled) == (index < filled):
            end += 1
        parts.append(BAR_TEXT + background + padded[index:end] + RESET)
        index = end
    return "".join(parts)


def draw_display(state, args, started, first, p0_hello, p1_hello):
    played = sum(line["played"] for line in state.values())
    total = args.lines * args.games_per_line
    lines = [
        f"multi-rig {int(time.time() - started)}s elapsed · {played}/{total} games",
        f"p0 {p0_hello}",
        f"p1 {p1_hello}",
    ]
    for line_no in range(1, args.lines + 1):
        line = state[line_no]
        pending = args.games_per_line - line["played"]
        results = "".join(line["symbols"]) + COLOR["pending"] + "." * pending + RESET
        lines.append(f"line {line_no}: {timing_field(line['p0_ms'])}:"
                     f"{results}:{timing_field(line['p1_ms'])}")
    if first:
        sys.stdout.write("\n".join(lines) + "\n")
    else:
        sys.stdout.write(f"\033[{len(lines)}A")
        sys.stdout.write("\n".join("\r\033[2K" + text for text in lines) + "\n")
    sys.stdout.flush()


def result_table(records, args, p0_hello, p1_hello):
    wins = {"p0": {"3inARow": 0, "CountVictory": 0, "Forfeit": 0},
            "p1": {"3inARow": 0, "CountVictory": 0, "Forfeit": 0}}
    draws = 0
    for record in records:
        winner = record["result"]["winner"]
        kind = record["result"]["result type"]
        if winner is None:
            draws += 1
            continue
        side = "p0" if winner == 0 else "p1"
        if kind not in wins[side]:
            kind = "Forfeit"
        wins[side][kind] += 1
    games = len(records)

    lines = [
        "multi-rig report",
        f"run: {datetime.now().astimezone().strftime('%Y-%m-%d %H:%M:%S %Z')}",
        f"p0: {args.p0}",
        f"p0 HELLO: {p0_hello}",
        f"p1: {args.p1}",
        f"p1 HELLO: {p1_hello}",
        f"lines: {args.lines}  games per line: {args.games_per_line}  "
        f"base seed: {args.seed}  relaxed: {args.relaxed} ms",
        "",
    ]
    for side, label in (("p0", "p0"), ("p1", "p1")):
        total_wins = sum(wins[side].values())
        score = total_wins + 0.5 * draws
        percent = score / games * 100 if games else 0.0
        detail = ", ".join(f"{name} {count}" for name, count in wins[side].items())
        lines.append(f"{label} {args.p0 if side == 'p0' else args.p1}: "
                     f"{total_wins} wins ({detail})  {percent:.1f}%")
    lines.append(f"draws: {draws}")
    lines.append(f"games: {games}")
    return "\n".join(lines) + "\n"


def main(argv):
    args = parse_args(argv)

    if not args.no_build:
        for command in build_commands(args):
            target = command[1]
            probe = subprocess.run(["make", "-n", target], cwd=ROOT,
                                   capture_output=True, text=True)
            if probe.returncode != 0:
                continue  # not a repository make target; use the binary as-is
            build = subprocess.run(command, cwd=ROOT)
            if build.returncode != 0:
                print(f"build failed: {' '.join(command)}", file=sys.stderr)
                return 1
    if not GAMERIG.exists():
        print(f"gamerig not found: {GAMERIG}", file=sys.stderr)
        return 2
    for name, command in (("p0", args.p0), ("p1", args.p1)):
        binary = shlex.split(command)[0]
        if not resolve_binary(binary).exists():
            print(f"{name} binary not found: {binary}", file=sys.stderr)
            return 2

    p0_hello = probe_hello(args.p0)
    p1_hello = probe_hello(args.p1)
    for name, command, hello in (("p0", args.p0, p0_hello), ("p1", args.p1, p1_hello)):
        if hello is None:
            print(f"{name} did not answer --HELLO: {command}", file=sys.stderr)
            return 2
    print("multi-rig identities")
    print(f"  p0: {args.p0}")
    print(f"      HELLO: {p0_hello}")
    print(f"  p1: {args.p1}")
    print(f"      HELLO: {p1_hello}")

    run_dir = archive_work()
    (run_dir / "config.txt").write_text(
        f"p0={args.p0}\np0_hello={p0_hello}\np1={args.p1}\np1_hello={p1_hello}\n"
        f"lines={args.lines}\ngames_per_line={args.games_per_line}\n"
        f"seed={args.seed}\nrelaxed={args.relaxed}\n")

    events = queue.Queue()
    stop = threading.Event()
    interrupt = threading.Event()
    state = {line_no: {"symbols": [], "wins": 0, "draws": 0, "played": 0,
                       "p0_ms": 0, "p1_ms": 0, "done": False}
             for line_no in range(1, args.lines + 1)}
    interactive = sys.stdout.isatty() and not args.no_interactive
    started = time.time()

    # Installed before the lines start, so a Ctrl-C anywhere in the batch, display
    # updates included, is handled here instead of raising KeyboardInterrupt.  An
    # earlier Ctrl-C, during the build or the --HELLO probes, keeps the default
    # behaviour and exits 130.
    signal.signal(signal.SIGINT, on_interrupt(stop, interrupt))

    threads = []
    for line_no in range(1, args.lines + 1):
        thread = threading.Thread(target=run_line,
                                  args=(line_no, args, run_dir, events, stop), daemon=True)
        thread.start()
        threads.append(thread)

    records = []
    finished = 0
    error = None
    first_draw = True
    if interactive:
        draw_display(state, args, started, first_draw, p0_hello, p1_hello)
        first_draw = False
    # One loop serves the whole run, whether it ends normally, on a line error or
    # on Ctrl-C.  A shutdown only sets `stop`, so no line starts another game, and
    # the loop still runs until every line has reported `done`: that keeps the
    # record and the events of each game in flight.
    while finished < args.lines:
        try:
            event = events.get(timeout=0.5)
        except queue.Empty:
            event = None
        if event:
            if event[0] == "game":
                record = event[2]
                glyph, color = symbol_for(record)
                line = state[event[1]]
                line["symbols"].append(color + glyph + RESET)
                line["played"] += 1
                # Timing shows the slowest move of each bot in this last game.
                line["p0_ms"] = max((m["time"] for m in record["moves"]
                                     if m["player"] == 0), default=0)
                line["p1_ms"] = max((m["time"] for m in record["moves"]
                                     if m["player"] == 1), default=0)
                if record["result"]["winner"] is None:
                    line["draws"] += 1
                elif record["result"]["winner"] == 0:
                    line["wins"] += 1
                records.append(record)
            elif event[0] == "done":
                state[event[1]]["done"] = True
                finished += 1
            elif event[0] == "error":
                error = event
                stop.set()
        if interactive:
            draw_display(state, args, started, first_draw, p0_hello, p1_hello)
            first_draw = False

    if interactive:
        draw_display(state, args, started, first_draw, p0_hello, p1_hello)
        sys.stdout.write("\n")
        sys.stdout.flush()

    total = args.lines * args.games_per_line
    table = result_table(records, args, p0_hello, p1_hello)
    if error:
        table += (f"\nABORTED: line {error[1]} game {error[2]} seed {error[3]} "
                  f"failed ({error[4]}); raw files: {run_dir}\n")
    if interrupt.is_set():
        table += (f"\nINTERRUPTED: {len(records)}/{total} games completed; "
                  f"raw files: {run_dir}\n")
    print(table, end="")
    rotate_log()
    LOG_PATH.write_text(table)
    print(f"log: {LOG_PATH}")
    if error:
        return 1
    if interrupt.is_set():
        return 130
    if len(records) < total:
        print("Some games are missing; inspect the run directory.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        sys.exit(130)
