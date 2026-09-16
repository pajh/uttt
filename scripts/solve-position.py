#!/usr/bin/env python3
"""Exact Ultimate Tic-Tac-Toe position workbench (X=p0, O=p1).

Load a saved move trace with --moves-csv/--ply, or a JSON position with
`board` (nine X/O/. strings), `player` (0/1), `last_move` ([row,col] or null),
and optional `legal_moves` ([[row,col], ...]) for independent validation.
The verdict is terminal W/D/L under best play, never the bot's heuristic.
"""
import argparse
import csv
import json
import time
from pathlib import Path

LINES = (7, 56, 448, 73, 146, 292, 273, 84)
FULL = 511
WIN = tuple(any(bits & line == line for line in LINES) for bits in range(512))
POSITION = tuple(((row // 3) * 3 + col // 3,
                  1 << ((row % 3) * 3 + col % 3))
                 for row in range(9) for col in range(9))
COORD = tuple((row, col) for row in range(9) for col in range(9))


def initial():
    # (nine encoded small boards, X-owned, O-owned, closed, target, player, winner)
    return ((0,) * 9, 0, 0, 0, -1, 0, -1)


def legal(state):
    boards, _, _, closed, target, _, winner = state
    if winner != -1:
        return ()
    allowed = (1 << target) if target >= 0 and not (closed & (1 << target)) else FULL & ~closed
    result = []
    for move, (small, bit) in enumerate(POSITION):
        if allowed & (1 << small) and not (boards[small] & bit) and not (boards[small] & (bit << 9)):
            result.append(move)
    return tuple(result)


def play(state, move):
    boards, x_owned, o_owned, closed, _, player, winner = state
    if winner != -1:
        raise ValueError("Move after game end")
    small, bit = POSITION[move]
    if closed & (1 << small) or boards[small] & (bit | (bit << 9)):
        raise ValueError(f"Occupied or closed move {COORD[move]}")
    updated = boards[small] | (bit if player == 0 else bit << 9)
    new_boards = boards[:small] + (updated,) + boards[small + 1:]
    x_bits, o_bits = updated & FULL, updated >> 9
    if WIN[x_bits]:
        x_owned |= 1 << small
        closed |= 1 << small
    elif WIN[o_bits]:
        o_owned |= 1 << small
        closed |= 1 << small
    elif x_bits | o_bits == FULL:
        closed |= 1 << small
    winner = (0 if WIN[x_owned] else 1 if WIN[o_owned] else
              (0 if x_owned.bit_count() > o_owned.bit_count() else
               1 if o_owned.bit_count() > x_owned.bit_count() else 2)
              if closed == FULL else -1)
    return (new_boards, x_owned, o_owned, closed, (move // 9 % 3) * 3 + move % 3,
            1 - player, winner)


def from_trace(path, ply):
    records = sorted(csv.DictReader(Path(path).open()), key=lambda row: int(row["ply"]))
    if not 0 <= ply <= len(records):
        raise ValueError(f"--ply must be between 0 and {len(records)}")
    state = initial()
    for expected, record in enumerate(records):
        if expected >= ply:
            break
        if int(record["ply"]) != expected or int(record["player"]) != state[5]:
            raise ValueError(f"Trace ply/player mismatch at {expected}")
        move = int(record["row"]) * 9 + int(record["col"])
        if move not in legal(state):
            raise ValueError(f"Illegal trace move at ply {expected}: {COORD[move]}")
        state = play(state, move)
    return state


def from_json(path):
    position = json.loads(Path(path).read_text())
    rows = position["board"]
    if len(rows) != 9 or any(len(row) != 9 or set(row) - set("XO.") for row in rows):
        raise ValueError("board must contain nine 9-character X/O/. rows")
    boards = [0] * 9
    for move, (row, col) in enumerate(COORD):
        mark = rows[row][col]
        if mark != ".":
            small, bit = POSITION[move]
            boards[small] |= bit if mark == "X" else bit << 9
    x_owned = o_owned = closed = 0
    for small, encoded in enumerate(boards):
        x_bits, o_bits = encoded & FULL, encoded >> 9
        if WIN[x_bits] and WIN[o_bits]:
            raise ValueError(f"Both players win small board {small}")
        if WIN[x_bits]:
            x_owned |= 1 << small
        if WIN[o_bits]:
            o_owned |= 1 << small
        if WIN[x_bits] or WIN[o_bits] or x_bits | o_bits == FULL:
            closed |= 1 << small
    previous = position.get("last_move")
    target = -1 if previous is None else (previous[0] % 3) * 3 + previous[1] % 3
    player = position["player"]
    if player not in (0, 1):
        raise ValueError("player must be 0 or 1")
    winner = (0 if WIN[x_owned] else 1 if WIN[o_owned] else
              (0 if x_owned.bit_count() > o_owned.bit_count() else
               1 if o_owned.bit_count() > x_owned.bit_count() else 2)
              if closed == FULL else -1)
    state = (tuple(boards), x_owned, o_owned, closed, target, player, winner)
    if "legal_moves" in position:
        supplied = {row * 9 + col for row, col in position["legal_moves"]}
        if supplied != set(legal(state)):
            raise ValueError("Supplied legal_moves disagree with rules")
    return state


def f1(x_bits, o_bits, closed, player):
    if WIN[x_bits] or WIN[o_bits]:
        return 0
    own = x_bits if player == 0 else o_bits
    occupied = x_bits | o_bits | closed
    winning_cells = 0
    one_mark_lines = 0
    for line in LINES:
        own_count = (own & line).bit_count()
        filled_count = (occupied & line).bit_count()
        if own_count == 2 and filled_count == 2:
            winning_cells |= line & ~occupied
        if own_count == 1 and filled_count == 1:
            one_mark_lines += 1
    wins = winning_cells.bit_count()
    return (0 if wins == 0 else 4 if wins == 1 else 6) + one_mark_lines


def f2(x_owned, o_owned, closed, small, player):
    bit = 1 << small
    if closed & bit:
        return 0
    own = x_owned if player == 0 else o_owned
    opponent = o_owned if player == 0 else x_owned
    blocked = (closed | opponent) & ~own
    relevance = 1
    for line in LINES:
        if line & bit and not line & blocked:
            relevance += 1 << (line & own).bit_count()
    return relevance


def live_master_winning_cells(boards, x_owned, o_owned, closed, player):
    """Distinct unfinished, still-capturable U cells completing a master line."""
    own = x_owned if player == 0 else o_owned
    unavailable = x_owned | o_owned | closed
    cells = 0
    for line in LINES:
        if (line & own).bit_count() == 2 and (line & unavailable).bit_count() == 2:
            cells |= line & ~unavailable
    return sum(bool(cells & (1 << small)) and
               any(not line & ((boards[small] >> 9) if player == 0 else boards[small] & FULL)
                   for line in LINES)
               for small in range(9))


def evaluate_current(state, uscale=5, count_scale=1.0):
    """Independent mirror of ai_minimax.c's nonterminal leaf scoreBoard()."""
    boards, x_owned, o_owned, closed, _, _, winner = state
    if winner != -1:
        raise ValueError("Current evaluator is not called on terminal positions")
    details = []
    for player in (0, 1):
        master = f1(x_owned, o_owned, closed, player)
        ownership = (int((x_owned if player == 0 else o_owned).bit_count()
                         * 20 * count_scale + 0.5) if master == 0 else 0)
        local = []
        for small, encoded in enumerate(boards):
            if not closed & (1 << small):
                local.append(f1(encoded & FULL, encoded >> 9, 0, player)
                             * f2(x_owned, o_owned, closed, small, player))
            else:
                local.append(0)
        total = min(10000, master * uscale + ownership + sum(local))
        details.append((total, master, ownership, local))
    mine, theirs = details[0][0], details[1][0]
    relative = int(10000 * (mine - theirs) / (mine + theirs + 2))
    return relative, details


def evaluate_experiment(state, ownership=20, threat=80, uscale=5):
    """MM-004 proposal: additive ownership and distinct live master threats."""
    boards, x_owned, o_owned, closed, _, _, winner = state
    if winner != -1:
        raise ValueError("Leaf evaluator is not called on terminal positions")
    strengths = []
    for player in (0, 1):
        own = x_owned if player == 0 else o_owned
        master = f1(x_owned, o_owned, closed, player) * uscale
        secured = own.bit_count() * ownership
        threats = live_master_winning_cells(boards, x_owned, o_owned, closed, player) * threat
        local = sum(f1(board & FULL, board >> 9, 0, player)
                    * f2(x_owned, o_owned, closed, small, player)
                    for small, board in enumerate(boards) if not closed & (1 << small))
        strengths.append((master + secured + threats + local,
                          master, secured, threats, local))
    mine, theirs = strengths[0][0], strengths[1][0]
    return int(10000 * (mine - theirs) / (mine + theirs + 2)), strengths


class Deadline(Exception):
    pass


def describe(move):
    small, bit = POSITION[move]
    return f"{COORD[move]} [U{small}/cell{bit.bit_length() - 1}]"


class Solver:
    def __init__(self, seconds):
        self.deadline = time.monotonic() + seconds
        self.nodes = 0
        self.cache = {}
        self.win_by_cache = {}

    def solve(self, state):
        self.nodes += 1
        if self.nodes & 4095 == 0 and time.monotonic() >= self.deadline:
            raise Deadline
        winner = state[6]
        if winner != -1:
            return (0 if winner == 2 else 1 if winner == state[5] else -1), ()
        cached = self.cache.get(state)
        if cached is not None:
            return cached
        best, best_line = -2, ()
        for move in legal(state):
            child = play(state, move)
            value, line = self.solve(child)
            value = -value
            if value > best:
                best, best_line = value, (move,) + line
                if best == 1:  # A win is the maximum possible outcome.
                    break
        result = best, best_line
        self.cache[state] = result
        return result

    def win_within(self, state, plies, winner):
        """Whether winner can force a win by this horizon against all replies."""
        self.nodes += 1
        if self.nodes & 4095 == 0 and time.monotonic() >= self.deadline:
            raise Deadline
        if state[6] != -1:
            return state[6] == winner
        if plies == 0:
            return False
        key = state, plies, winner
        cached = self.win_by_cache.get(key)
        if cached is not None:
            return cached
        if state[5] == winner:
            result = any(self.win_within(play(state, move), plies - 1, winner)
                         for move in legal(state))
        else:
            result = all(self.win_within(play(state, move), plies - 1, winner)
                         for move in legal(state))
        self.win_by_cache[key] = result
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--moves-csv")
    source.add_argument("--position")
    parser.add_argument("--ply", type=int, help="Position before this zero-based game ply")
    parser.add_argument("--compare", nargs="+", required=True, metavar="ROW,COL")
    parser.add_argument("--seconds", type=float, default=60, help="Total proof budget (default 60)")
    parser.add_argument("--fastest-win", action="store_true",
                        help="Find the earliest ply by which a proven win can be forced")
    parser.add_argument("--explain-eval", action="store_true",
                        help="Mirror and decompose the current one-ply heuristic")
    parser.add_argument("--experiment-eval", action="store_true",
                        help="Compare the proposed MM-004 one-ply heuristic")
    parser.add_argument("--ownership-bonus", type=int, default=20)
    parser.add_argument("--threat-bonus", type=int, default=80)
    parser.add_argument("--uscale", type=int, default=5)
    parser.add_argument("--count-scale", type=float, default=1.0)
    args = parser.parse_args()
    if args.moves_csv and args.ply is None:
        parser.error("--moves-csv requires --ply")
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    state = from_trace(args.moves_csv, args.ply) if args.moves_csv else from_json(args.position)
    root_legal = set(legal(state))
    choices = []
    for choice in args.compare:
        row, col = map(int, choice.split(","))
        if not (0 <= row < 9 and 0 <= col < 9):
            parser.error(f"Invalid coordinate: {choice}")
        move = row * 9 + col
        if move not in root_legal:
            parser.error(f"Move {choice} is not legal in this position")
        choices.append(move)
    playable_count = sum((FULL & ~(board & FULL | board >> 9)).bit_count()
                         for i, board in enumerate(state[0]) if not state[3] & (1 << i))
    print(f"Player p{state[5]}; {len(root_legal)} legal moves; {playable_count} playable cells")
    solver = Solver(args.seconds)
    started = time.monotonic()
    for move in choices:
        if args.explain_eval:
            child = play(state, move)
            if child[6] == -1:
                relative, details = evaluate_current(child, args.uscale, args.count_scale)
                for player, (total, master, ownership, local) in enumerate(details):
                    print(f"  {describe(move)} leaf p{player}: total={total}, master={master}×{args.uscale}, "
                          f"ownership={ownership}, local={sum(local)} "
                          f"(U1={local[1]})", flush=True)
                print(f"  {describe(move)} leaf relative={relative:+d}", flush=True)
            else:
                print(f"  {describe(move)} is immediately terminal", flush=True)
        if args.experiment_eval and play(state, move)[6] == -1:
            relative, details = evaluate_experiment(
                play(state, move), args.ownership_bonus, args.threat_bonus, args.uscale)
            for player, (total, master, owned, threats, local) in enumerate(details):
                print(f"  {describe(move)} MM-004 p{player}: total={total}, master={master}, "
                      f"owned={owned}, threats={threats}, local={local}", flush=True)
            print(f"  {describe(move)} MM-004 relative={relative:+d}", flush=True)
        try:
            value, line = solver.solve(play(state, move))
            value = -value
            verdict = {1: "FORCED WIN", 0: "FORCED DRAW", -1: "FORCED LOSS"}[value]
            continuation = (move,) + line
            text = " ".join(f"{row},{col}" for row, col in (COORD[item] for item in continuation))
            print(f"{describe(move)}: {verdict}; nodes={solver.nodes}; "
                  f"elapsed={time.monotonic()-started:.2f}s; one best-play line: {text}", flush=True)
        except Deadline:
            print(f"{describe(move)}: UNRESOLVED at {solver.nodes} nodes, "
                  f"{time.monotonic()-started:.2f}s", flush=True)
            break
        if args.fastest_win and value == 1:
            try:
                for horizon in range(1, playable_count + 1):
                    if solver.win_within(play(state, move), horizon - 1, state[5]):
                        print(f"  Earliest forced win: within {horizon} plies "
                              f"({solver.nodes} nodes, {time.monotonic()-started:.2f}s)", flush=True)
                        break
            except Deadline:
                print(f"  Earliest forced win: UNRESOLVED within {args.seconds:g}s total budget", flush=True)
                break


if __name__ == "__main__":
    main()
