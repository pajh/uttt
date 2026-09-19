#!/usr/bin/env python3
"""Generate paste-ready 3x3 mask lookup tables for board2.h."""

import sys

LINES = (
    0x007, 0x038, 0x1C0,
    0x049, 0x092, 0x124,
    0x111, 0x054,
)
MASK9 = 0x1FF


def has_three_in_a_row(mask: int) -> bool:
    """Return whether mask completely contains one independent rule line."""
    return any(mask & line == line for line in LINES)


def still_win(opponent_mask: int) -> bool:
    """Return whether at least one rule line has no opponent mark."""
    return any(opponent_mask & line == 0 for line in LINES)


def winning_candidates(mine: int) -> int:
    """Return empty cells that complete a line containing exactly two mine bits."""
    candidates = 0
    for line in LINES:
        if (mine & line).bit_count() == 2:
            candidates |= line & ~mine
    return candidates & MASK9


def brute_winning_candidates(mine: int) -> int:
    """Independently test every empty cell by adding it and checking lines.

    The line-specific check prevents an already-complete unrelated line from
    making every empty cell look like a new winning move.
    """
    candidates = 0
    for cell in range(9):
        bit = 1 << cell
        added = mine | bit
        if (not mine & bit and has_three_in_a_row(added) and
                any(bit & line and
                    has_three_in_a_row((mine & line) | bit)
                    for line in LINES)):
            candidates |= bit
    return candidates


def one_mark_lines(mine: int, opponent: int) -> int:
    """Count lines with exactly one mine bit and no opponent bit."""
    return sum((mine & line).bit_count() == 1 and not (opponent & line)
               for line in LINES)


def make_table(predicate) -> list[int]:
    words = [0] * 8
    for mask in range(MASK9 + 1):
        if predicate(mask):
            # Table index is mask >> 6; bit position is mask & 63.
            words[mask >> 6] |= 1 << (mask & 63)
    return words


def self_check(table: list[int], predicate) -> None:
    for mask in range(MASK9 + 1):
        table_value = (table[mask >> 6] >> (mask & 63)) & 1
        assert table_value == int(predicate(mask)), (mask, table_value)


def print_table(name: str, words: list[int], meaning: str) -> None:
    print(f"/* {meaning} */")
    print(f"static const uint64_t {name}[8] = {{")
    for word in words:
        print(f"    UINT64_C(0x{word:016X}),")
    print("};")
    print()


def main() -> None:
    if "--verify-winning-candidates" in sys.argv[1:]:
        for mask in range(MASK9 + 1):
            expected = brute_winning_candidates(mask)
            assert winning_candidates(mask) == expected, mask
        print("winning_candidates self-check: 512 masks")
        return
    has_line_table = make_table(has_three_in_a_row)
    still_win_table = make_table(still_win)
    self_check(has_line_table, has_three_in_a_row)
    self_check(still_win_table, still_win)
    assert len(has_line_table) == len(still_win_table) == 8
    print("/* 512 entries: index = mask >> 6, bit = mask & 63; set bit means true. */")
    print_table(
        "board2_has_three_in_a_row_table",
        has_line_table,
        "bit 1 iff the 9-bit mask contains a complete 3-in-a-row line",
    )
    print_table(
        "board2_still_win_table",
        still_win_table,
        "bit 1 iff at least one line has no opponent mark",
    )


if __name__ == "__main__":
    main()
