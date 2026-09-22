# New board design record

This records the current mask-native design discussion. The prototype at
`src/engine/board2.h` is unintegrated and table-free. It does not replace
`src/engine/board.h`, `ai_minimax.c`, the rig, tests, or submission tooling.

New board code targets gnu17. The aliases `u8`, `i8`, `u16`, and `i16` are
defined once in `src/engine/support.h` as aliases for matching `<stdint.h>`
exact-width types. `uint` is not standard C, and no pragma selects the C
language dialect.

The board prototype uses `mask9` for a 9-bit 3x3 cell set and `onehot9` for a
single nonzero bit in that set. These are C typedefs for `u16`: they document
the domain but cannot enforce the mask or one-hot invariants.
Prefer `bool` for predicate returns, boolean parameters, and boolean
state/local variables. `src/engine/support.h` is the sole home of shared
generic aliases and generic assertion support. `BOARD_ASSERTS` is undefined or
0 by default and can be enabled with `#define BOARD_ASSERTS 1` before the
first include of `support.h`, or with `make BOARD_ASSERTS=1`. Disabled
assertions expand to `((void)0)` and do not evaluate their conditions; all
assertion conditions must be side-effect-free. Domain-specific `mask9` and
`onehot9` checks belong in `board2.h`.
In production code, prefer readable semantic questions and names, or
documented small helpers, for bit tricks. Explain non-obvious operations in
comments. Consider `__builtin_popcount` for bit counts and `__builtin_ctz`
for the index of a known-nonzero bit (`ctz(0)` is undefined); do not apply
blanket intrinsic substitutions, and require measurement for performance
claims.

## Agreed representation

- `Board2.marks[player][0..8]` stores two disjoint 9-bit player masks for the
  local boards. `Board2.umarks[player]` stores the two U status planes (status
  planes, not owner masks; `owner[p] = umarks[p] & ~umarks[p^1]`).
  Bit `x + 3*y` is the cell at internal `x = column`, `y = row`.
- The ultimate board status planes store, for each master cell,
  `status[0], status[1]` are `00 = open`, `10 = player 0 owned`,
  `01 = player 1 owned`, and `11 = drawn and closed`.
- Actual ownership is filtered from status planes:
  `owner[p] = status[p] & ~status[p ^ 1]`. Draw bits therefore never count for
  either player.
- `cannot_claim[p]` records a local board where geometry proves player `p`
  has no unblocked line. It starts at zero and is monotone: legal moves may
  add bits, never remove them. It is not ownership and does not close a board.
- `winner` is `0` while ongoing, `1` for a player 0 win, `2` for a player 1
  win, and `3` for a final tied draw.
- The goal is a human-readable mask implementation with no base-3 indexing and
  no `3^9` local evaluation table in the prototype.

## Move update

For a legal move in local board `cell` with one-hot local bit `bit`:

```text
mark the player's local mask with bit
local_win = the player's local mask has three in a row
if the opponent has no local line without one of our marks:
    cannot_claim[opponent] |= master-cell-bit
if neither local_win nor local-board-full:
    continue
if local_win:
    fill every still-empty bit of the winner's local mask
    set only the mover's U status plane bit
else:
    set both U status plane bits (draw)
closed = status[0] | status[1]
owner[p] = status[p] & ~status[p ^ 1]
if local_win and the mover's filtered owner mask completes a U line:
    winner = mover + 1
else if closed contains all nine bits:
    compare popcount(owner[0]) and popcount(owner[1])
    winner = larger owner count mapped to 1/2, or 3 when tied
```

The count-based ending applies after the last U cell closes even when that
last cell is a local draw. A closed drawn cell remains unowned.

`board2_play` returns `bool` as a certificate-input change signal. It returns
true when the move newly sets a `cannot_claim` bit or closes a local/U cell;
it is not a legality or general move-success result. An illustrative future
caller could use it like this:

```c
bool proof_changed = board2_play(&board, cell, bit, OP);
if (board.winner != BOARD2_IN_PROGRESS) return score_for_actual_result(board.winner);
if (proof_changed) {
    Board2CertificateResult result = certified_result(&board, OP);
    if (result == BOARD2_CERTIFIED_WIN) return score_for_player(OP, 60000);
    if (result == BOARD2_CERTIFIED_DRAW) return score_for_draw();
}
```

`certified_result` now exists as the prototype's U-status certificate helper.
`score_for_actual_result`, `score_for_player`, and `score_for_draw` remain
illustrative future score helpers and are not implemented by this prototype.
The WIN portion of its formula is:

```text
open = M111111111 & ~(U0 | U1)
p_owned = Up & ~Uother
other_owned = Uother & ~Up
other_claimable = open & ~cannot_claim[other]
other_possible = other_owned | other_claimable
WIN result = !has_three_in_a_row(other_possible)
             && popcount(p_owned) > popcount(other_possible)
```

`other_possible` gives the opponent every open cell not geometrically ruled
out, so it is an optimistic upper bound. The owner lead must be strict because
a tied count can still be won by the opponent. The DRAW result additionally
requires every open bit in both `cannot_claim` masks and equal actual owner
counts.

The current prototype checks the complete nine-bit player mask for each line
test. A last-move-only optimization remains a TODO for later measurement; no
speed benefit is assumed.

## Current minimax support checklist

The prototype currently supports:

- applying a move and detecting local closure, U closure, U line wins, and
  count-based terminal results;
- local disjoint masks and geometric monotone `cannot_claim` updates;
- `valid_moves` and `next_move` for both single-board and multi-board paths;
- `certified_result` for current U win/draw certificates;
- experimental `winning_cells_simd` and `live_one_mark_lines_simd` local
  evaluator primitives, exhaustively checked against independent references.

Tomorrow's scoring/search adapters remain unresolved for:

- the Board2-to-`f1` score assembly and closed-cell context;
- master threat scoring and immediate master-win certification using virtual
  candidate claims;
- minimax2 integration around the Board2 move and terminal APIs;
- complete cache identity for every state component;
- cache initialization and startup behavior;
- opening and coordinate/I/O adapters;
- single-file submission integration and the 100,000-character limit.

### Next scoring review

After reproducing the baseline `f1` behavior, try a cap of 3 for the live
one-mark-line contribution. A cap of 2 flattens isolated center, corner, and
edge positions; test strength separately from the scoring reproduction.

For a future `f1` adapter, derive local-style U masks as follows:

```c
mine = umarks[player] & ~umarks[other];
blocked = umarks[other];
```

Pass `(mine, blocked)` to `winning_cells_simd` and
`live_one_mark_lines_simd`. The owner masks are disjoint and the filtering
removes drawn U cells from `mine`; draws remain in `blocked` so they block
future lines.

A virtual master claim can initially be evaluated by copying `Board2`, adding
the candidate bit to the passed player's U status plane for an open U cell,
then calling `certified_result`. A dedicated engine primitive is only needed
if measurement or readability later justifies it. The current primitives cover
win detection, winning-cell masks, and one-mark counts; assembling the full
score remains future minimax2 work and does not require reproducing the old
`Evaluation` record as one structure.

This checklist records the forced single-board representation and the now
implemented multi-board storage and iteration. Integrated evaluator and search
performance remain unmeasured.

The prototype has a concrete forced single-board path: an open destination
returns one subboard and its empty-cell mask. A closed destination now returns
`MULTI_BOARD` with all nine local availability masks, a board bitmask, and a
total count. `next_move` destructively consumes the lowest local bit from
either variant; full moves clear a board bit when that board is exhausted and
decrement the remaining count. The `take_one_bit` helper has a nonzero-mask
precondition and removes exactly its returned bit.

Experimental `has_three_in_a_row_vcache` and `still_win_vcache` functions now
provide exhaustive 512-entry truth-table variants for comparison. They are not
adopted by default. Defining `BOARD2_USE_VCACHE=1` at compile time switches the
three existing `board2_play` rule checks to those variants for A/B measurement;
the default keeps the original expressions and scan.

## Ideas bank

### Depth-selective transposition storage

The main prospective value of a search transposition table is avoiding descent
through a subtree that has already been evaluated at a sufficient remaining
depth. Memoizing the cheapest depth-zero score calculations may instead cost
more than recomputing them, especially now that Board2 evaluation primitives are
fast relative to hashing, key construction, and memory access.

If a transposition table is added to the Board2 negamax, measure a policy that
does not probe or store at the lowest remaining depth. This also avoids flooding
the table at the widest level of the tree, where the search produces the most
keys with the least reusable work behind each value. Treat the cutoff depth as
an experimental lever: compare total nodes, heuristic leaf evaluations, table
traffic, completed search depth, and wall-clock cost before selecting it.
Correct cache identity and sufficient stored search depth remain mandatory for
every entry that is used.

`winning_cells_simd(mine, opponent)` is an additional unconnected SSE2
experiment. It returns empty cells that complete a local line, and has an
exhaustive test against an independent Python reference; no search path uses
it yet.

`live_one_mark_lines_simd(mine, opponent)` is a matching unconnected SSE2
experiment for counting one-mine, opponent-free lines. It is covered by the
same exhaustive comparison and is not connected to evaluation.
