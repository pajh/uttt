#ifndef UTTT_EXPERIMENT_H
#define UTTT_EXPERIMENT_H

/* Header-only scalar port of the pure scorer in subboard-score.html (v2.1).
 *
 * The page scores one 3x3 board for one player.  This port keeps its integer
 * arithmetic exactly: every value is in twentieths of one move, so all
 * comparisons stay exact and no rounding can leak into a decision.
 *
 * Status is always relative to the scored player, so one code path scores
 * either perspective and the two must agree by symmetry.  Raw status,
 * protection credit and the improving-move pass are separate steps and the
 * pass never recurses, so a child's protectUnits can be read without scoring
 * that child's children.  No table, no SIMD, no allocation.
 *
 * Bits are board2's row-major layout: bit (x + 3*y) of a 3x3 board.  `mine`
 * is the player being scored, `opponent` is the other player, and the two
 * masks must be disjoint nine-bit masks.  The caller must pass an open board
 * on which `mine` still has a live line.  Other inputs violate the contract.
 */

#include "../engine/board2.h"

/* A raw distance of 1, 2 or 3 is 20, 40 or 60 units; a protected position
 * gives back a flat 5 units; each improving move costs 1 unit. */
#define EXP_SUBBOARD_UNITS_PER_DISTANCE 20
#define EXP_SUBBOARD_PROTECTION_UNITS 5
#define EXP_SUBBOARD_IMPROVING_MOVE_UNITS 1

/* True when some line is entirely `marks`. */
static inline bool exp_subboard_has_line(mask9 marks)
{
    ASSERT_MASK9(marks);
    for (u8 i = 0; i < 8; i++)
        if ((marks & board2_line_masks[i]) == board2_line_masks[i])
            return true;
    return false;
}

/* Raw status only: no protection test and no improving-move term, so this is
 * the minimum the hostile-reply test needs about one opponent reply.  A live
 * line is any line with no opponent mark and its distance is the empty cells
 * the player would still have to fill.  Complete lines were rejected above,
 * so a live line always has at least one empty cell. */
static inline bool exp_subboard_raw_open(mask9 mine, mask9 opponent, u8 *distance)
{
    mask9 occupied;
    u8 best = 0; /* 0 means no live line, i.e. the player is unwinnable. */

    ASSERT_MASK9(mine);
    ASSERT_MASK9(opponent);
    ASSERT((mine & opponent) == 0);
    ASSERT(distance != NULL);

    /* Either player's completed line and a full board are both unscorable. */
    if (exp_subboard_has_line(mine) || exp_subboard_has_line(opponent))
        return false;
    occupied = (mask9)(mine | opponent);
    if (occupied == M111111111)
        return false;

    for (u8 i = 0; i < 8; i++) {
        mask9 line = board2_line_masks[i];
        u8 missing;
        if ((line & opponent) != 0)
            continue;
        missing = (u8)__builtin_popcount((unsigned)(line & (mask9)~occupied));
        if (best == 0 || missing < best)
            best = missing;
    }
    if (best == 0)
        return false;
    *distance = best;
    return true;
}

/* The raw distance and its protection credit in units, and nothing else.  This
 * is the only source of protectUnits.  Protection is defined by testing each
 * distinct empty cell once, because several lines through one cell are still
 * a single opponent action, and it is asked only of raw distances 1 and 2: a
 * 3-away position is never protected and earns no credit. */
static inline int exp_subboard_protect_units(mask9 mine, mask9 opponent, u8 distance)
{
    int raw_units = EXP_SUBBOARD_UNITS_PER_DISTANCE * (int)distance;
    mask9 empty;

    ASSERT(distance >= 1 && distance <= 3);
    if (distance > 2)
        return raw_units;

    empty = (mask9)(M111111111 & ~(mine | opponent));
    while (empty != 0) {
        onehot9 reply = take_one_bit(&empty);
        u8 reply_distance = 0;
        /* A reply that closes the board for either player refutes. */
        if (!exp_subboard_raw_open(mine, (mask9)(opponent | reply),
                                   &reply_distance))
            return raw_units;
        if (reply_distance > distance)
            return raw_units;
    }
    return raw_units - EXP_SUBBOARD_PROTECTION_UNITS;
}

/* Raw status plus protectUnits.  Returns false, and leaves *protect_units
 * alone, for every unscorable position. */
static inline bool exp_subboard_open_measure(mask9 mine, mask9 opponent,
                                             int *protect_units)
{
    u8 distance = 0;

    ASSERT(protect_units != NULL);
    if (!exp_subboard_raw_open(mine, opponent, &distance))
        return false;
    *protect_units = exp_subboard_protect_units(mine, opponent, distance);
    return true;
}

/* Score one open, still-winnable 3x3 sub-board for one player, in twentieths
 * of one move.  Calling this on a resolved or unwinnable board violates the
 * contract: debug builds assert; optimized builds have undefined behavior.
 *
 * The result is protectUnits minus one unit per improving move.  Every
 * distinct legal empty cell counts as one candidate: it improves when playing
 * it wins the board outright, or when the child stays open with strictly
 * lower protectUnits than the parent.  Any other terminal child counts for
 * nothing. */
static inline int score_subboard_exper(mask9 mine, mask9 opponent)
{
    int protect_units = 0;
    int improving = 0;
    mask9 empty;

    ASSERT_MASK9(mine);
    ASSERT_MASK9(opponent);
    ASSERT((mine & opponent) == 0);

    bool valid = exp_subboard_open_measure(mine, opponent, &protect_units);
    ASSERT(valid);
    if (!valid)
        __builtin_unreachable();

    empty = (mask9)(M111111111 & ~(mine | opponent));
    while (empty != 0) {
        onehot9 move = take_one_bit(&empty);
        int child_units = 0;
        if (exp_subboard_has_line((mask9)(mine | move)) ||
            (exp_subboard_open_measure((mask9)(mine | move), opponent,
                                       &child_units) &&
             child_units < protect_units))
            improving++;
    }
    return protect_units - improving * EXP_SUBBOARD_IMPROVING_MOVE_UNITS;
}

static inline bool board2_play_exper(Board2 *board, Move move)
{
    bool proof_changed = board2_play(board, move);
    onehot9 subboard_bit = onehot9_from_index(move.subboard);

    if ((board->umarks[0] | board->umarks[1]) & subboard_bit)
        return proof_changed;

    for (u8 p = 0; p < 2; p++)
        if ((board->cannot_claim[p] & subboard_bit) == 0)
            board->experiment_units[p][move.subboard] =
                (u8)score_subboard_exper(board->marks[p][move.subboard],
                                         board->marks[opponent(p)][move.subboard]);
    return proof_changed;
}

/* Two full-board channels per player, reported side by side and never mixed
 * here: `three_iar` is a soft immediate-threat measure, `count` is plain
 * U-cell ownership.  A diff, ratio or weighting is left to score_board. */

typedef struct ExperBoardScore_s {
    double three_iar[2];
    double count[2];
} ExperBoardScore;

/* Score one nonterminal full board for both players.
 *
 * inner[c] is 1.0 for a cell the player owns, 0.0 for a cell it can never
 * claim, and the normalized sub-board score for an open cell it can still
 * win.  three_iar[p] adds one term per open claimable cell: the strongest
 * line product through that cell.  count[p] is the number of owned cells, so
 * partial progress inside an unclaimed cell earns nothing. */
static inline ExperBoardScore score_board_exper(const Board2 *board)
{
    ExperBoardScore result = {0};
    mask9 closed;

    ASSERT(board != NULL);
    ASSERT(board->winner == BOARD2_IN_PROGRESS);

    closed = (mask9)(board->umarks[0] | board->umarks[1]);

    for (u8 p = 0; p < 2; p++) {
        double inner[9];
        double best[9] = {0.0};
        mask9 owned = board2_owned(board, p);
        /* A cell closed by a win or a draw rules out both players, so this
         * mask alone cannot separate owned, opponent-owned, drawn and locally
         * unwinnable cells. */
        mask9 unclaimable = board->cannot_claim[p];
        /* Owned cells lie inside `closed`, so this is exactly the set of open
         * cells p can still claim. */
        mask9 claimable = (mask9)(M111111111 & ~(closed | unclaimable));

        for (u8 c = 0; c < 9; c++) {
            onehot9 cell = onehot9_from_index(c);
            /* Owned is tested before cannot_claim because winning a sub-board
             * sets both cannot_claim bits: testing cannot_claim first would
             * score a cell this player owns as 0.0. */
            if ((owned & cell) != 0)
                inner[c] = 1.0;
            else if ((unclaimable & cell) != 0)
                inner[c] = 0.0;
            else {
                /* An unowned closed cell always carries the cannot_claim bit,
                 * so reaching here means the cell is open and still winnable:
                 * the only state score_subboard_exper accepts. */
                ASSERT((closed & cell) == 0);
                int units = board->experiment_units[p][c];
                ASSERT(units == score_subboard_exper(board->marks[p][c],
                                                     board->marks[opponent(p)][c]));
                inner[c] = (61.0 - (double)units) / 60.0;
                ASSERT(inner[c] > 0.0 && inner[c] < 1.0);
            }
        }

        /* Strongest line product through each cell. */
        for (u8 l = 0; l < 8; l++) {
            mask9 line = board2_line_masks[l];
            double product = 1.0;
            for (u8 k = 0; k < 9; k++)
                if ((line & onehot9_from_index(k)) != 0)
                    product *= inner[k];
            for (u8 k = 0; k < 9; k++) {
                onehot9 cell = onehot9_from_index(k);
                if ((line & cell) != 0 && product > best[k])
                    best[k] = product;
            }
        }

        /* A won cell has inner 1.0 but is never added here; it only raises
         * the products it appears in, so contributions and won cells stay
         * distinct. */
        for (u8 c = 0; c < 9; c++)
            if ((claimable & onehot9_from_index(c)) != 0)
                result.three_iar[p] += best[c];

        result.count[p] = (double)__builtin_popcount((unsigned)owned);
    }

    return result;
}

#endif /* UTTT_EXPERIMENT_H */
