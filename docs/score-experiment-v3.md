# Score Experiment v3

## Purpose

Design a dedicated U-board evaluator that sees progress inside open sub-boards. Do not call the existing local `grid_potential()` on the U-board: a U-board made only from already-owned cells cannot distinguish an empty sub-board from one that is a move from being won.

This is an experiment specification, not a settled formula. Keep the two victory mechanisms separate, establish a catalog and invariants first, and change one layer at a time.

## Pre-experiment baseline

`NM-003-R1` versus `ORIG001`, 100 games:

- Match score: **73.0%**
- Record: **71 wins / 25 losses / 4 draws**
- NM-003-R1 wins: **56 by 3IAR / 15 by count**

This is the comparison baseline for the experiment. Record seeds/start policy, build identity and technical failures with every later result; timed runs against `ORIG001` are not perfectly paired merely because they use the same nominal seed schedule.

## Score architecture

Calculate each player's strength independently. The opponent is evaluated by the same functions; only afterwards are the two strengths differenced or ratioed.

```text
player_strength(p) = combine(THREE_IAR(p), COUNT(p))
```

Initially, `combine` may be addition. Preserve separate channel outputs so later experiments can weight, blend or gate them without changing their meanings.

### COUNT channel

The initial COUNT score is deliberately literal:

```text
COUNT(p) = number (or fixed unit value) of U-cells already won by p
```

Partial local progress does not belong in the initial COUNT channel. A won U-cell is banked count value regardless of its location.

### 3IAR channel

The 3IAR score measures remaining opportunities to complete a U-board line. Already-won U-cells do not score directly here; they make open cells on their live lines more valuable.

For player `p` and U-cell `c`, keep two concepts distinct:

```text
L_p(c) = line-cell value used inside U-line products
C_p(c) = the contribution actually summed for U-cell c
```

`L_p(c)` has these semantics:

| U-cell state for `p` | `L_p(c)` |
|---|---:|
| won by `p` | `1` |
| lost, drawn, or provably unwinnable by `p` | `0` |
| open and still winnable | `V_p(c)`, strictly between `0` and `1` |

For each open, winnable cell, evaluate every still-live U-line through it:

```text
line_product_p(line) = product(L_p(x) for x in line)
C_p(c) = max(line_product_p(line) for live lines containing c)
```

All other cells have `C_p(c) = 0`, including cells already won by `p`.

```text
THREE_IAR(p) = sum(C_p(c) for all nine U-cells)
```

This is the proposed **SUM of open-cell MAX-of-line-products** model. `MAX` prevents one target cell from being counted repeatedly merely because it lies on several equally strong lines. Additional promising lines still raise the position score through the contributions of their other open cells.

## Open sub-board value: moves to win

For an open sub-board that player `p` can still win, define claim distance:

```text
d_p = minimum number of future p marks required to complete a live local line
```

The raw classes are:

| Local state | Distance |
|---|---:|
| already won | `0` |
| immediate winning cell exists | `1` |
| best live line needs two more marks | `2` |
| best live line needs three marks | `3` |
| no live winning line | infinity / unwinnable |

Protection is a half-step improvement in this distance space:

```text
ordinary 2-away     2.0
protected 2-away    1.5
ordinary 1-away     1.0
protected 1-away    0.5
```

A position at distance `d` is protected iff, after **every** legal opponent mark in that sub-board:

- the opponent has not won it;
- it has not become drawn or closed;
- `p` can still win it; and
- `p`'s new distance is no worse than `d`.

Equivalently:

```text
protected_p(d) iff for every legal opponent mark m:
    board_after(m) remains open
    and d_p(board_after(m)) <= d
```

Protection is therefore a property derived by testing hostile replies, not a pattern such as “two winning lines” or “two winning cells.” An opponent move that wins the board is exactly the kind of reply that defeats protection.

Map the effective distance to the open-cell value only at the boundary:

```text
V_p(c) = value(effective_distance_p(local_board_c))
```

The numeric mapping is intentionally not fixed yet. It must satisfy:

```text
0 < v3 < v2 < v1.5 < v1 < v0.5 < 1
```

where `vx = value(distance x)`. Lower distance is better in the reasoning model; higher value is better in the evaluator.

Strict endpoints are reserved:

- exactly `0` only for lost, drawn, or provably unwinnable cells;
- exactly `1` only for cells already won by `p`;
- every unresolved, still-winnable cell is strictly inside `(0, 1)`.

The score is absolute progress, not a probability and not locally zero-sum. Both players may have high values in the same volatile sub-board.

## Representative local-position catalog

Dots are empty. Scores below are for `X`; symbolic values are preferred until the distance-to-value mapping is chosen.

| Position | Class | Expected X value / relation |
|---|---|---:|
| `.../.../...` | 3-away | `v3` |
| `X../.../...` | protected 2-away on the otherwise empty board | `v1.5` |
| `OO./.X./O..` | ordinary 2-away; O can block X's sole live row | `v2` |
| `XX./.../...` | ordinary 1-away | `v1` |
| `XX./X../...` | protected 1-away; cells 2 and 6 are independent immediate wins | `v0.5` |
| `XX./X../.OO` | **not** protected; `O6` wins the board | `v1` |
| `XXO/O.O/OXX` | mutual knife-edge; either player wins at centre | X=`v1`, O=`v1`, both greater than their empty-board values |
| any X-won local board | won | `1` |
| O-won, drawn, or no live X line | unavailable to X | `0` |

Required qualitative order:

```text
unwinnable < empty < ordinary 2-away < protected 2-away
           < ordinary 1-away < protected 1-away < won
```

The empty-board and mutual-immediate examples deliberately demonstrate why `V_X + V_O = 1` is not an invariant.

## Representative U-board catalog

Use `b = v3` for an empty open sub-board and `a > b` for an improved open sub-board. `X` means won by the player, `#` means lost/drawn/unwinnable, and `?` means open and winnable.

1. **All nine local boards empty.** Every line product is `b^3`; each U-cell contributes `b^3`, so `THREE_IAR = 9b^3`.

2. **One open cell improves from `b` to `a`.** With all other cells at `b`, geometry emerges from line products rather than a hard-coded location multiplier:

   ```text
   centre: 9ab^2
   corner: 7ab^2 + 2b^3
   edge:   5ab^2 + 4b^3
   ```

   Therefore centre > corner > edge > the all-empty baseline when `a > b`.

3. **Two owned cells and one open target: `XX?`.** The open target's relevant line product is `1 * 1 * V = V`. The two owned cells contribute zero directly to 3IAR. If `?` becomes unwinnable, the product becomes zero.

4. **Blocked line: `X#?`.** The line product is zero. A lost, drawn or `cannot_claim[p]` cell kills that line identically.

5. **MAX versus double counting.** Compare:

   ```text
   A:        B:
   ??X       ??X
   ...       ?..
   ...       X..
   ```

   If the two best contexts through cell 0 are equal, cell 0's own contribution is the same in A and B because it takes `MAX`, not `SUM`. B is still stronger overall because its additional open cells receive useful contributions from the extra developed line.

6. **Owned-cell location.** With otherwise equivalent open cells, owning a centre U-cell should strengthen more remaining opportunities than owning a corner, which should strengthen more than an edge. The effect must arise through the open cells' products; the owned cell still contributes zero directly to 3IAR and one banked unit to COUNT.

## Invariants and exhaustive checks

These are stronger than example-based tests and should be checked over the complete local catalog where practical (`3^9 = 19,683` raw local grids per player, filtered or labelled for legality as needed).

### Local score invariants

1. **Own-mark monotonicity.** For a still-winnable position `s` and any legal empty cell `m`:

   ```text
   V_p(s) <= V_p(s + p@m)
   ```

   Adding a legal mark for `p` must never decrease `p`'s score. Equality is permitted when the mark does not improve the encoded distance/protection class; require a strict increase whenever the class improves or the move wins the board.

   Any violation is to be printed with the before/after boards and classifications and treated as either an interesting edge case requiring an explicit design decision or a bug.

2. **Endpoint exclusivity.** Only won positions score `1`. Only lost, drawn or provably unwinnable positions score `0`. Open, winnable positions never reach either endpoint.

3. **Distance ordering.** Every mapped class respects `v3 < v2 < v1.5 < v1 < v0.5`.

4. **Protection definition.** A protected label is equivalent to the exhaustive hostile-reply test above. In particular, an opponent winning or drawing reply always makes the position unprotected.

5. **Player symmetry.** Swapping X and O and the requested player leaves the numeric result unchanged:

   ```text
   V_X(s) = V_O(s with X/O swapped)
   ```

6. **Geometric symmetry.** All eight rotations/reflections of a local board have the same score and classification.

7. **No zero-sum requirement.** Do not assert `V_X(s) + V_O(s) = 1`.

### U-board invariants

1. `L_p(c)` is exactly `1` for won, `0` for lost/drawn/unwinnable, and strictly interior for open/winnable.
2. `C_p(c) = 0` for every closed or unwinnable U-cell, including a cell won by `p`.
3. A won cell may affect 3IAR only through the products of open cells on shared live lines.
4. No line containing a zero-valued cell contributes a positive product.
5. An open cell is counted once via its best line; adding an equally valued second line through that cell does not increase that cell's own `C_p(c)`.
6. Increasing any open line-cell value while holding states and all other values fixed cannot decrease `THREE_IAR(p)`.
7. COUNT depends only on already-won U-cells in the initial model and is invariant under U-board rotation/reflection.
8. Player-swap symmetry applies to both channels independently.

## Testing workflow

1. **Catalog first, no gameplay change.** Enumerate local positions and emit, for both players: status, raw distance, hostile-reply distances, protected flag, effective distance and symbolic/numeric value. Include the named examples above.
2. **Run exhaustive properties.** Check endpoint, own-mark monotonicity, protection equivalence, player-swap symmetry and all eight geometric symmetries. Save minimal counterexamples, not just counts.
3. **Choose the distance mapping.** Select candidate values for `v3`, `v2`, `v1.5`, `v1`, `v0.5`; rerun the same catalog and invariants. Treat the mapping as the only variable in this stage.
4. **Validate the U-board model separately.** Build a small catalog around the six U-board cases above. Confirm line-cell values, per-cell MAX contributions, 3IAR sum and COUNT as separate columns.
5. **Integrate behind one named experiment.** Replace only the U-board evaluation path; do not mix in search, time-budget or unrelated scoring changes. Keep `grid_potential()` local-only.
6. **Technical gate.** Run the existing regression suite plus targeted catalog/property tests, then a small legal-move/timeout smoke test.
7. **Strength gate.** Compare with the recorded `NM-003-R1` baseline under the same normal budgets, 100-game schedule, alternating starts and `ORIG001`. Record W/L/D, 3IAR/count win split, technical failures, build IDs, seeds/start policy and exact formula parameters.
8. **Iterate one lever at a time.** Recommended order: local distance mapping; protection bonus; MAX-of-products 3IAR; then COUNT combination/weighting. Keep, revert or investigate each stage based on reproducible evidence.

## Open decisions

- The numeric mapping from effective distance to `(0,1)`.
- Whether equality under an own legal mark is acceptable everywhere it preserves the same class, or whether a later tie-breaker should make every own mark strictly better.
- The initial units/normalisation used to combine raw 3IAR and COUNT outputs.
- Whether prospective COUNT value is ever useful. It is excluded from v3 initially to keep secured ownership and 3IAR opportunity conceptually clean.
