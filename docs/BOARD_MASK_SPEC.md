# Mask-native board specification

This is the implementation brief for `src/engine/board_mask.h`. It defines a
separate mask-native API. The existing `src/engine/board.h` API and all legacy
clients remain unchanged until an explicitly approved migration.

## Scope and naming

The header is a single-translation-unit implementation header, like
`board.h`. Public names use the `Mask` prefix so both representations can be
included by compatibility tests without type or macro collisions.

Phase 1 implements the types, invariant-preserving mutation, legal-move
generation, coordinate conversion, local evaluation, and exhaustive tests.
Certificate table generation/compression, startup decompression, and bot/rig
migration are deferred. No public function, type, field, or parameter may be
added, renamed, or changed without approval from the primary design agent.

## Public types

```c
typedef uint16_t MaskBits; /* only low MASK9 bits are valid */

typedef struct MaskPos_s {
    int x; /* column, 0..8 */
    int y; /* row, 0..8 */
} MaskPos;

typedef struct MaskBoard3_s {
    MaskBits p[2]; /* disjoint local marks, bit x + 3*y */
} MaskBoard3;

typedef struct MaskBoard9_s {
    MaskBoard3 cell[9];
    MaskBits p[2];             /* actual master owners */
    MaskBits closed;           /* won or drawn local boards */
    MaskBits cannot_claim[2];  /* local geometry says player cannot win */
    int winner;                /* -1 ongoing, 0/1 winner, 2 draw */
} MaskBoard9;

typedef struct MaskMoves2_s {
    MaskBits mask[9];
    uint16_t count;
    uint16_t it_current;
    uint16_t it_current_cell;
    uint16_t it_count;
} MaskMoves2;

typedef struct MaskEvaluation_s {
    unsigned char p3[2], p2[2], p1[2];
    unsigned char can_still_win; /* bit p set when player p remains possible */
    MaskBits free, p0_winners, p1_winners;
} MaskEvaluation;
```

The implementation may add private tables and helpers, but they are not part
of this interface. Define `MASK9` as `0x1ff` and use it to mask every public
mask result.

## Bit and coordinate contract

- Local and master indices are row-major: `index = x + 3*y`.
- A local bit is `1u << (x + 3*y)`, with x as column and y as row.
- `MaskPos` always uses internal x/y coordinates. CodinGame row/column I/O
  conversion remains at the caller boundary.
- Player masks are disjoint: `(p[0] & p[1]) == 0` for every local board and
  for the master.
- All masks have no bits above bit 8. One-hot arguments must be nonzero and
  contained by `MASK9`.

## Master-state invariants

- `closed` contains every local board that has ended by a player line or a
  full draw.
- A won board has its bit in `closed` and the corresponding bit in exactly one
  master owner mask. A drawn board has its bit in `closed` and neither owner
  mask. Therefore `closed` is not derivable from owner masks alone.
- `open = (MaskBits)(~closed) & MASK9`.
- `cannot_claim[p]` is monotone for a position's history and may only gain a
  bit. It means local geometry proves player p can no longer complete any
  local line in that board. It does not mean the board is closed or owned.
- Mutation must preserve these invariants even when a board closes. Existing
  owner bits remain actual owners; cannot-claim bits must never be interpreted
  as virtual ownership.
- `winner == -1` means play continues. `winner == 0` or `1` means that player
  completed a master line. `winner == 2` means all nine master cells closed
  without a master line and the secured owner counts tie. A master score win
  on final closure sets the corresponding player winner.

## Required public functions

The following exact signatures are the phase-1 interface:

```c
void mask_init_caches(void);

MaskPos mask_cell_to_pos(unsigned cell, MaskBits bit);
void mask_pos_to_cell(MaskPos pos, unsigned *cell, MaskBits *bit);

int mask_moves_next(MaskMoves2 *moves, MaskPos *pos);
int mask_moves_next_cb(MaskMoves2 *moves, unsigned *cell, MaskBits *bit);
void mask_moves_reset(MaskMoves2 *moves);
void mask_moves_push(MaskMoves2 *moves, MaskPos pos);

MaskEvaluation mask_evaluate(MaskBoard3 board, MaskBits closed);
MaskEvaluation mask_set_cell(MaskBoard9 *board, unsigned cell,
                             MaskBits bit, int player);
MaskEvaluation mask_set_pos(MaskBoard9 *board, MaskPos pos, int player);
void mask_close_cell(MaskBoard9 *board, unsigned cell, int owner);

void mask_valid_moves(const MaskBoard9 *board, MaskMoves2 *moves,
                      unsigned target_cell);
```

`target_cell` is 0..8. If it is open, only that local board is playable; if
it is closed, every open local board is playable. A zero target is valid and
must not be confused with “unrestricted”. The caller must not request moves
from a terminal board.

`mask_set_cell` accepts a one-hot local bit and updates the local marks,
claimability, local closure, master ownership, and winner state atomically.
`mask_set_pos` is the coordinate convenience form and must have identical
semantics. `mask_close_cell` is intended for reconstruction/tests and must
accept only an open cell; owner `0`/`1` records ownership, owner `2` records a
draw. All mutation functions must reject or assert invalid players, cells,
bits, occupied bits, or closed boards according to the project’s existing
assertion convention.

## Evaluation and certificate boundary

Phase 1 must preserve the existing `MaskEvaluation` meanings: `p3` detects a
line, `p2`/`p1` count currently open two/one-mark lines, `free` counts empty
local cells, winner masks contain playable one-hot winning cells, and
`can_still_win` is geometric. No generated master certificate is part of this
phase.

If a certificate API is later added, it must accept actual owner masks,
`closed`, and `cannot_claim` separately. Treating an opponent’s cannot-claim
active cell as a virtual owner is forbidden. Generated tables and any
decompression helper require a separate approved brief.

## Cache and build contract

- Any cache key must include every state component that affects the result:
  all nine local masks, both master owner masks, `closed`, both cannot-claim
  masks unless they are proven mechanically derivable, destination, player to
  move, and depth where applicable.
- `mask_init_caches()` must run before evaluation or search. It must be safe to
  call once during process startup and must not allocate during search.
- The header must remain suitable for `scripts/subst` single-file inlining.
  It must not add stdout output, read game stdin, open report files, or depend
  on local-rig instrumentation.
- The eventual generated submission must remain below 100,000 characters;
  phase-1 code must avoid committing to table formats that make that limit
  impossible.

## Focused acceptance checks

Before migration, add tests that assert:

1. Every local mask state round-trips through evaluation and preserves
   disjointness, `MASK9`, line detection, winning-cell masks, and
   `can_still_win`.
2. `mask_pos_to_cell` and `mask_cell_to_pos` round-trip all 81 cells and all
   nine one-hot local bits.
3. Move generation obeys forced routing, closed-board unrestricted routing,
   occupancy, and exact move counts.
4. A local win closes and assigns exactly one master owner; a local draw closes
   without assigning an owner; closure redirects routing.
5. `cannot_claim[p]` only gains bits and never changes actual ownership or
   closed/drawn status.
6. Master-line wins, final tied draws, and final owner-count wins set the
   specified `winner` values.
7. A copied position produces identical evaluation, legal moves, mutation
   result, and cache identity.

The old `board.h` tests remain the compatibility baseline. Do not delete or
rewrite them as part of introducing this header. Migration of `ai_minimax`,
the rig, submission generation, or legacy sources requires a separate approved
implementation and experiment brief.
