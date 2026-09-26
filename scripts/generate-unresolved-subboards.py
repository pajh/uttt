#!/usr/bin/env python3
"""Enumerate every unresolved Ultimate Tic-Tac-Toe 3x3 local grid.

A grid is a nine-character row-major string over {'.', 'X', 'O'}, where '.'
is an empty cell, 'X' is player 0 and 'O' is player 1.  A grid is kept when it
is not full and neither player owns a completed line (a grid with several
winning lines still counts once, as an exclusion).  No move parity, turn
order, UTTT routing or geometric symmetry filter is applied, so every
surviving grid appears exactly once.

For a kept grid, U0 (U1) is true iff player 0 (player 1) has no line free of
opponent marks, i.e. opponent marks appear on all eight lines.

The generator refuses to write anything unless the counts and the record
uniqueness match the expected metadata.
"""
import itertools
import json
import sys
from pathlib import Path

OUTPUT = Path(__file__).resolve().parent.parent / "data" / "unresolved-subboards.json"

LINES = ((0, 1, 2), (3, 4, 5), (6, 7, 8),      # rows
         (0, 3, 6), (1, 4, 7), (2, 5, 8),      # columns
         (0, 4, 8), (2, 4, 6))                 # diagonals

EXPECTED = {"total_raw": 19683, "excluded_full": 512, "excluded_win": 8078,
            "kept": 11093, "u0_true": 736, "u1_true": 736, "both_true": 40}


def wins(grid, mark):
    """True iff mark owns at least one of the eight lines."""
    return any(all(grid[index] == mark for index in line) for line in LINES)


def unresolved(grid, mark):
    """True iff every line holds at least one mark, so no line is mark-free."""
    return all(any(grid[index] == mark for index in line) for line in LINES)


def main():
    records = []
    counts = dict.fromkeys(EXPECTED, 0)
    for cells in itertools.product(".XO", repeat=9):
        grid = "".join(cells)
        counts["total_raw"] += 1
        if "." not in grid:
            counts["excluded_full"] += 1
            continue
        if wins(grid, "X") or wins(grid, "O"):
            counts["excluded_win"] += 1
            continue
        u0 = unresolved(grid, "O")   # player 0 blocked on every line
        u1 = unresolved(grid, "X")   # player 1 blocked on every line
        counts["kept"] += 1
        counts["u0_true"] += u0
        counts["u1_true"] += u1
        counts["both_true"] += u0 and u1
        records.append({"board": grid, "U0": u0, "U1": u1})

    records.sort(key=lambda record: record["board"])
    boards = [record["board"] for record in records]
    problems = [f"{key}: expected {EXPECTED[key]}, got {counts[key]}"
                for key in EXPECTED if counts[key] != EXPECTED[key]]
    if len(set(boards)) != len(boards):
        problems.append(f"records are not unique: {len(boards) - len(set(boards))} duplicates")
    if problems:
        print("counts do not match, nothing written:", *problems, sep="\n  ", file=sys.stderr)
        return 1

    lines = ['{', f'  "metadata": {json.dumps(counts)},', '  "subboards": [']
    lines += [f"    {json.dumps(record)}," for record in records[:-1]]
    lines += [f"    {json.dumps(records[-1])}", "  ]", "}"]
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text("\n".join(lines) + "\n")
    print(f"{OUTPUT}: {len(records)} grids")
    for key in EXPECTED:
        print(f"  {key} {counts[key]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
