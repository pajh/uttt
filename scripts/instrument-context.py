#!/usr/bin/env python3
"""Turn-by-turn text companion to the one-game instrument HTML report."""
import csv
import sys
from collections import defaultdict
from pathlib import Path

turns_file, scores_file, moves_file, game_file, target_file, hello = sys.argv[1:7]
turns = list(csv.DictReader(Path(turns_file).open()))
scores = list(csv.DictReader(Path(scores_file).open()))
moves = sorted(csv.DictReader(Path(moves_file).open()), key=lambda row: int(row["ply"]))
games = list(csv.DictReader(Path(game_file).open()))
ours = [move for move in moves if move["player"] == "0"]
if len(ours) != len(turns) or len(games) != 1:
    raise SystemExit("Instrument game/turn/move records do not agree")

LINES = ((0, 1, 2), (3, 4, 5), (6, 7, 8), (0, 3, 6),
         (1, 4, 7), (2, 5, 8), (0, 4, 8), (2, 4, 6))


def board_before(ply):
    board = [["." for _ in range(9)] for _ in range(9)]
    for move in moves:
        if int(move["ply"]) >= ply:
            break
        row, col = int(move["row"]), int(move["col"])
        if board[row][col] != ".":
            raise SystemExit(f"Duplicate played cell before ply {ply}: {row},{col}")
        board[row][col] = "X" if move["player"] == "0" else "O"
    return board


def master(board):
    result = []
    for outer_row in range(3):
        cells = []
        for outer_col in range(3):
            mini = [board[outer_row * 3 + row][outer_col * 3 + col]
                    for row in range(3) for col in range(3)]
            owner = next((player for player in "XO"
                          if any(all(mini[index] == player for index in line)
                                 for line in LINES)), None)
            cells.append(owner or ("=" if "." not in mini else "."))
        result.append(" ".join(cells))
    return result


def candidate_effect(board, row, col):
    after = [line.copy() for line in board]
    after[row][col] = "X"
    owned_before = master(board)[row // 3].split()[col // 3]
    owned_after = master(after)[row // 3].split()[col // 3]
    capture = "wins its small board; " if owned_before == "." and owned_after == "X" else ""
    target = (row % 3, col % 3)
    target_status = master(after)[target[0]].split()[target[1]]
    route = ("opponent may choose any open board" if target_status != "."
             else f"opponent directed to small board {target}")
    return capture + route


events = defaultdict(lambda: defaultdict(list))
for event in scores:
    key = (int(event["row"]), int(event["col"]))
    events[int(event["ply"])][key].append((int(event["depth"]), int(event["score"])))

game = games[0]
lines = [
    "# One-game instrument context",
    "",
    f"Bot: {hello}",
    f"Seed: {game['seed']}; starting player: p{game['starting_player']}; "
    f"result: p{game['winner']} ({game['win_type']}); plies: {game['plies']}.",
    "Coordinates are row,col, 0–8. X is p0 (our bot); O is p1. "
    "Master cells: X/O won, = drawn, . open.",
    "Scores are backed-up p0-perspective root scores, one per completed "
    "candidate/depth. +60000 is a proved win; -60000 a proved loss. "
    "A blank candidate was not completed before the deadline.",
]

for index, (turn, chosen) in enumerate(zip(turns, ours), start=1):
    ply = int(chosen["ply"])
    board = board_before(ply)
    selected = (int(chosen["row"]), int(chosen["col"]))
    if board[selected[0]][selected[1]] != ".":
        raise SystemExit(f"Selected occupied cell at ply {ply}")
    prior = moves[ply - 1] if ply else None
    next_move = moves[ply + 1] if ply + 1 < len(moves) else None
    lines += ["", f"## Our turn {index} — game ply {ply}", ""]
    if prior:
        destination = (int(prior["row"]) % 3, int(prior["col"]) % 3)
        lines.append(f"Previous: p{prior['player']} ({prior['row']},{prior['col']}); "
                     f"directed to small board ({destination[0]},{destination[1]}).")
    else:
        lines.append("Opening: no previous move; all cells available.")
    lines += [
        "Master before: " + " / ".join(master(board)),
        "Board before (3×3 groups):",
        "```text",
    ]
    for row in board:
        lines.append(" ".join("".join(row[col:col + 3]) for col in (0, 3, 6)))
    lines += ["```", "Candidate scores (depth:score):"]
    candidates = events[ply]
    if not candidates:
        lines.append("- No search: opening or forced move.")
    for candidate, values in sorted(candidates.items()):
        mark = " ← PLAYED" if candidate == selected else ""
        depth_scores = ", ".join(f"d{depth}:{score:+d}" for depth, score in values)
        effect = candidate_effect(board, *candidate)
        lines.append(f"- ({candidate[0]},{candidate[1]}): {depth_scores}{mark}; {effect}")
    lines.append(
        f"Played ({selected[0]},{selected[1]}); selected score "
        f"{turn['winning_score']}; {turn['elapsed_ms']} ms; "
        f"{turn['scored_positions']} leaf scores; "
        f"{turn['cache_hits']}/{turn['cache_lookups']} cache hits/lookups."
    )
    if next_move:
        lines.append(f"Opponent replied ({next_move['row']},{next_move['col']}).")

Path(target_file).write_text("\n".join(lines) + "\n")
print(target_file)
