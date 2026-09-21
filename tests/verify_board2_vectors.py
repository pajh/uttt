#!/usr/bin/env python3
"""Independent verifier for board2 test-vector blobs.

The verifier is deliberately simple and slow: it expands each nine-bit mask
into an explicit 3x3 grid and tests the eight winning lines with plain Python
loops.  It intentionally does not reuse the bit-parallel logic from
src/engine/board2.h so that agreement is genuine independent evidence.

Bit layout of a mask is row-major::

    0 1 2
    3 4 5
    6 7 8

Usage: python3 tests/verify_board2_vectors.py [test_name ...]
With no arguments every registered test is verified.
"""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
VECTOR_DIR = ROOT / "tests" / "vectors"

MASK9 = 0x1FF

# Cell indices for the eight lines of a 3x3 board.
LINES = (
    (0, 1, 2), (3, 4, 5), (6, 7, 8),  # rows
    (0, 3, 6), (1, 4, 7), (2, 5, 8),  # columns
    (0, 4, 8), (2, 4, 6),             # diagonals
)


def to_grid(mask: int) -> list[list[bool]]:
    """Expand a nine-bit mask into a 3x3 grid of booleans."""
    return [
        [bool((mask >> (3 * row + col)) & 1) for col in range(3)]
        for row in range(3)
    ]


def has_complete_line(mask: int) -> bool:
    """True iff any of the eight lines is fully occupied."""
    grid = to_grid(mask)
    return any(all(grid[i // 3][i % 3] for i in line) for line in LINES)


def fnv1a64(data: bytes) -> int:
    """The same FNV-1a 64-bit hash the C side uses (independent code)."""
    value = 0xCBF29CE484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def verify_001_has_three_in_a_row() -> tuple[int, int]:
    """Every 3x3 board: the stored flag is 1 iff a line is complete."""
    path = VECTOR_DIR / "001_has_three_in_a_row.expected.bin"
    blob = path.read_bytes()
    if len(blob) != 512:
        raise AssertionError(f"{path.name}: length {len(blob)}, want 512")

    for mask, stored in enumerate(blob):
        if stored not in (0, 1):
            raise AssertionError(f"mask={mask}: byte {stored} is not 0 or 1")
        expected = 1 if has_complete_line(mask) else 0
        if stored != expected:
            grid = "".join("1" if c else "0"
                           for row in to_grid(mask) for c in row)
            raise AssertionError(
                f"mask={mask} grid={grid} stored={stored} expected={expected}")

    return len(blob), fnv1a64(blob)


def verify_002_still_win() -> tuple[int, int]:
    """Every opponent-mark mask: flag is 1 iff some line is free of marks."""
    path = VECTOR_DIR / "002_still_win.expected.bin"
    blob = path.read_bytes()
    if len(blob) != 512:
        raise AssertionError(f"{path.name}: length {len(blob)}, want 512")

    for mask, stored in enumerate(blob):
        if stored not in (0, 1):
            raise AssertionError(f"mask={mask}: byte {stored} is not 0 or 1")
        grid = to_grid(mask)
        expected = 1 if any(
            not any(grid[i // 3][i % 3] for i in line) for line in LINES) else 0
        if stored != expected:
            cells = "".join("1" if c else "0"
                            for row in grid for c in row)
            raise AssertionError(
                f"mask={mask} grid={cells} stored={stored} expected={expected}")

    return len(blob), fnv1a64(blob)


def pairs():
    """Disjoint (mine, opponent) pairs in the canonical vector order.

    For each mine, opponent runs through every submask of ~mine from the full
    remaining mask down to zero, matching the C enumeration.
    """
    for mine in range(512):
        remaining = (~mine) & MASK9
        opponent = remaining
        while True:
            yield mine, opponent
            if opponent == 0:
                break
            opponent = (opponent - 1) & remaining


def verify_003_take_one_bit() -> tuple[int, int]:
    """Lowest set bit is isolated and removed, leaving the other bits."""
    path = VECTOR_DIR / "003_take_one_bit.expected.bin"
    blob = path.read_bytes()
    if len(blob) != 511 * 4:
        raise AssertionError(f"{path.name}: length {len(blob)}, want {511 * 4}")

    for index, mask in enumerate(range(1, 512)):
        # Find the lowest occupied cell by scanning single-cell values.
        bit = 1
        while not (mask & bit):
            bit <<= 1
        remaining = mask & ~bit
        base = index * 4
        got_bit = blob[base] | (blob[base + 1] << 8)
        got_remaining = blob[base + 2] | (blob[base + 3] << 8)
        if got_bit != bit or got_remaining != remaining:
            raise AssertionError(
                f"mask={mask} bit={got_bit:#x}/{bit:#x} "
                f"remaining={got_remaining:#x}/{remaining:#x}")

    return len(blob), fnv1a64(blob)


def winning_cells(mine: int, opponent: int) -> int:
    """Empty cells that complete a line already holding two mine marks.

    The check is per line and per cell: a line contributes its single non-mine
    cell when that cell is not an opponent mark.
    """
    mine_grid = to_grid(mine)
    opponent_grid = to_grid(opponent)
    candidates = 0
    for line in LINES:
        empty = [(i // 3, i % 3) for i in line
                 if not mine_grid[i // 3][i % 3]]
        if len(empty) == 1:
            row, col = empty[0]
            if not opponent_grid[row][col]:
                candidates |= 1 << (3 * row + col)
    return candidates


def verify_004_winning_cells_simd() -> tuple[int, int]:
    path = VECTOR_DIR / "004_winning_cells_simd.expected.bin"
    blob = path.read_bytes()
    if len(blob) != 19683 * 2:
        raise AssertionError(f"{path.name}: length {len(blob)}, want {19683 * 2}")

    for index, (mine, opponent) in enumerate(pairs()):
        base = index * 2
        stored = blob[base] | (blob[base + 1] << 8)
        expected = winning_cells(mine, opponent)
        if stored != expected:
            raise AssertionError(
                f"mine={mine:#05x} opponent={opponent:#05x} "
                f"stored={stored:#05x} expected={expected:#05x}")

    return len(blob), fnv1a64(blob)


def live_one_mark_lines(mine: int, opponent: int) -> int:
    """Count lines with exactly one mine mark and no opponent mark."""
    mine_grid = to_grid(mine)
    opponent_grid = to_grid(opponent)
    count = 0
    for line in LINES:
        mine_count = sum(mine_grid[i // 3][i % 3] for i in line)
        opponent_count = sum(opponent_grid[i // 3][i % 3] for i in line)
        if mine_count == 1 and opponent_count == 0:
            count += 1
    return count


def verify_005_live_one_mark_lines_simd() -> tuple[int, int]:
    path = VECTOR_DIR / "005_live_one_mark_lines_simd.expected.bin"
    blob = path.read_bytes()
    if len(blob) != 19683:
        raise AssertionError(f"{path.name}: length {len(blob)}, want {19683}")

    for index, (mine, opponent) in enumerate(pairs()):
        expected = live_one_mark_lines(mine, opponent)
        if blob[index] != expected:
            raise AssertionError(
                f"mine={mine:#05x} opponent={opponent:#05x} "
                f"stored={blob[index]} expected={expected}")

    return len(blob), fnv1a64(blob)


def verify_006_board2_owned() -> tuple[int, int]:
    """Owned mask removes the other player's bits; draws count for neither."""
    path = VECTOR_DIR / "006_board2_owned.expected.bin"
    blob = path.read_bytes()
    if len(blob) != 262144 * 4:
        raise AssertionError(f"{path.name}: length {len(blob)}, "
                             f"want {262144 * 4}")

    for state in range(262144):
        owned0 = 0
        owned1 = 0
        for cell in range(9):
            status = (state >> (2 * cell)) & 3
            if status == 1:
                owned0 |= 1 << cell
            elif status == 2:
                owned1 |= 1 << cell
        base = state * 4
        got0 = blob[base] | (blob[base + 1] << 8)
        got1 = blob[base + 2] | (blob[base + 3] << 8)
        if got0 != owned0 or got1 != owned1:
            raise AssertionError(
                f"state={state} owned0={got0:#05x}/{owned0:#05x} "
                f"owned1={got1:#05x}/{owned1:#05x}")

    return len(blob), fnv1a64(blob)


def verify_007_check_and_close_uboard() -> tuple[int, int]:
    """A full U-board resolves by owner counts; partial boards do nothing."""
    path = VECTOR_DIR / "007_check_and_close_uboard.expected.bin"
    blob = path.read_bytes()
    if len(blob) != 262144 * 2:
        raise AssertionError(f"{path.name}: length {len(blob)}, "
                             f"want {262144 * 2}")

    for state in range(262144):
        closed = True
        p0 = 0
        p1 = 0
        for cell in range(9):
            status = (state >> (2 * cell)) & 3
            if status == 0:
                closed = False
            if status in (1, 3):
                p0 += 1
            if status in (2, 3):
                p1 += 1

        if closed:
            expected_closed = 1
            if p0 == p1:
                expected_winner = 3  # BOARD2_DRAW
            elif p0 > p1:
                expected_winner = 1  # BOARD2_PLAYER0_WIN
            else:
                expected_winner = 2  # BOARD2_PLAYER1_WIN
        else:
            expected_closed = 0
            expected_winner = 0  # BOARD2_IN_PROGRESS, unchanged

        got_closed = blob[state * 2]
        got_winner = blob[state * 2 + 1]
        if got_closed != expected_closed or got_winner != expected_winner:
            raise AssertionError(
                f"state={state} closed={got_closed}/{expected_closed} "
                f"winner={got_winner}/{expected_winner}")

    return len(blob), fnv1a64(blob)


VERIFIERS = {
    "001_has_three_in_a_row": verify_001_has_three_in_a_row,
    # 001b is the vcache equivalent and reuses 001's reference blob.
    "001b_has_three_in_a_row_vcache": verify_001_has_three_in_a_row,
    "002_still_win": verify_002_still_win,
    # 002b is the vcache equivalent and reuses 002's reference blob.
    "002b_still_win_vcache": verify_002_still_win,
    "003_take_one_bit": verify_003_take_one_bit,
    "004_winning_cells_simd": verify_004_winning_cells_simd,
    "005_live_one_mark_lines_simd": verify_005_live_one_mark_lines_simd,
    "006_board2_owned": verify_006_board2_owned,
    "007_check_and_close_uboard": verify_007_check_and_close_uboard,
}


def main(argv: list[str]) -> int:
    names = argv[1:] if len(argv) > 1 else list(VERIFIERS)
    for name in names:
        verifier = VERIFIERS.get(name)
        if verifier is None:
            print(f"unknown test '{name}'; known: {', '.join(VERIFIERS)}",
                  file=sys.stderr)
            return 2
        try:
            count, digest = verifier()
        except AssertionError as error:
            print(f"verify {name}: FAIL: {error}", file=sys.stderr)
            return 1
        print(f"verify {name}: OK {count} values, "
              f"fnv1a64={digest:016x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
