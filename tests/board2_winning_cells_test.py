#!/usr/bin/env python3
"""Compare board2's experimental SSE2 candidate function to Python rules."""

import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from generate_board2_truth_tables import one_mark_lines, winning_candidates  # noqa: E402

RUNNER = r'''
#include <stdio.h>
#include "board2.h"

int main(void)
{
    unsigned mine, opponent;
    while (scanf("%u %u", &mine, &opponent) == 2)
        printf("%u %u\n",
               (unsigned)winning_cells_simd((mask9)mine, (mask9)opponent),
               (unsigned)live_one_mark_lines_simd((mask9)mine,
                                                   (mask9)opponent));
    return 0;
}
'''


def pairs():
    for mine in range(512):
        remaining = (~mine) & 0x1FF
        opponent = remaining
        while True:
            yield mine, opponent
            if opponent == 0:
                break
            opponent = (opponent - 1) & remaining


def check(define: str | None) -> int:
    with tempfile.TemporaryDirectory() as directory:
        directory = pathlib.Path(directory)
        source = directory / "runner.c"
        binary = directory / "runner"
        source.write_text(RUNNER)
        command = [
            "gcc", "-std=gnu17", "-Wall", "-Wextra", "-Werror", "-Isrc/engine",
        ]
        if define:
            command.append(f"-D{define}")
        command += [str(source), "-o", str(binary)]
        subprocess.run(command, cwd=ROOT, check=True)

        all_pairs = list(pairs())
        payload = "".join(f"{mine} {opponent}\n"
                           for mine, opponent in all_pairs)
        result = subprocess.run([str(binary)], input=payload, text=True,
                                capture_output=True, cwd=ROOT, check=True)
        actual = [tuple(map(int, line.split()))
                  for line in result.stdout.splitlines()]
        expected = [(winning_candidates(mine) & ~opponent,
                     one_mark_lines(mine, opponent))
                    for mine, opponent in all_pairs]
        if actual != expected:
            for index, (got, want) in enumerate(zip(actual, expected)):
                if got != want:
                    mine, opponent = all_pairs[index]
                    raise AssertionError(
                        f"mine={mine:03x} opponent={opponent:03x} "
                        f"got={got} want={want}")
            raise AssertionError(f"output length {len(actual)} != {len(expected)}")
        return len(all_pairs)


def main() -> None:
    count = check(None)
    check("BOARD_ASSERTS=1")
    print(f"winning_cells_simd/live_one_mark_lines_simd exhaustive check: "
          f"{count} disjoint pairs, assertions on/off")


if __name__ == "__main__":
    main()
