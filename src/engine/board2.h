#ifndef UTTT_BOARD2_H
#define UTTT_BOARD2_H

/* This standalone prototype targets C17. */
#include "support.h"
#include <emmintrin.h>
#include <string.h>

/* The nine low bits represent a 3x3 board in row-major order.  Internal
 * coordinates are x = column and y = row, so bit (x + 3*y) is that cell. */
#define M111111111 0x1FFu
#define UBOARD 9

/* Domain aliases document 3x3 mask shapes; C typedefs do not enforce them.
 * mask9 has only bits 0..8, while onehot9 has exactly one of those bits and
 * is never zero. */
typedef u16 mask9;
typedef u16 onehot9;

typedef enum Board2Winner_e {
    BOARD2_IN_PROGRESS = 0,
    BOARD2_PLAYER0_WIN = 1,
    BOARD2_PLAYER1_WIN = 2,
    BOARD2_DRAW = 3
} Board2Winner;

#if defined(BOARD_ASSERTS) && BOARD_ASSERTS
/* The complement selects any forbidden bit above bit 8. */
#define ASSERT_MASK9(v) do { \
    mask9 assert_mask9_value = (v); \
    if ((assert_mask9_value & (mask9)~M111111111) != 0) \
        support_assert_fail(#v, __FILE__, __LINE__); \
} while (0)
/* A one-hot value must pass both the nine-bit mask and exactly-one-bit tests. */
#define ASSERT_1SHOT9(v) do { \
    onehot9 assert_1shot9_value = (v); \
    ASSERT_MASK9(assert_1shot9_value); \
    ASSERT(__builtin_popcount((unsigned)assert_1shot9_value) == 1); \
} while (0)
#else
#define ASSERT_MASK9(v) ((void)0)
#define ASSERT_1SHOT9(v) ((void)0)
#endif

typedef struct Board2_s {
    /* Local masks use indices 0..8; UBOARD is a special status cell with
     * 00=open, 10/01=player-owned, and 11=draw. */
    mask9 marks[2][10];

    /* A bit means local geometry proves that player p has no unblocked line
     * in that local board.  These masks start at zero and only gain bits. */
    mask9 cannot_claim[2];

    /* Subboards where the next move may legally be played. */
    mask9 playable_subboards;

    /* 0 ongoing, 1 player 0 winner, 2 player 1 winner, 3 final draw. */
    i8 winner;
} Board2;

static inline Board2 board2_initial(void)
{
    return (Board2){.playable_subboards = M111111111,
                    .winner = BOARD2_IN_PROGRESS};
}

typedef struct Move_s {
    u8 subboard;    /* 0..8 master-cell index. */
    onehot9 local_bit; /* One nonzero bit for a local cell, bits 0..8. */
} Move;

typedef struct SingleBoardMoves_s {
    u8 subboard; /* 0..8 master-cell index. */
    mask9 bits;  /* All currently empty local cells in that board. */
} SingleBoardMoves;

typedef struct FullBoardMoves_s {
    u16 count;
    mask9 moves[9];
    mask9 boards;
} FullBoardMoves;

typedef enum ValidMovesKind_e {
    SINGLE_BOARD,
    MULTI_BOARD
} ValidMovesKind;

typedef struct ValidMoves_s {
    ValidMovesKind kind;
    union {
        /* Meaningful only for SINGLE_BOARD. */
        SingleBoardMoves single;
        /* Meaningful only for MULTI_BOARD. */
        FullBoardMoves full;
    };
} ValidMoves;

typedef struct Board2CellLines_s {
    mask9 lines[4];
    u8 count;
} Board2CellLines;

/* Convert a 0..8 index to a onehot9 bit. */
static inline onehot9 onehot9_from_index(u8 index)
{
    ASSERT(index < 9);
    return (onehot9)(1u << index);
}

static const Board2CellLines board2_cell_lines[9] = {
    /* 0: top-left */
    {{0x007u, 0x049u, 0x111u, 0x000u}, 3},

    /* 1: top-middle */
    {{0x007u, 0x092u, 0x000u, 0x000u}, 2},

    /* 2: top-right */
    {{0x007u, 0x124u, 0x054u, 0x000u}, 3},

    /* 3: middle-left */
    {{0x038u, 0x049u, 0x000u, 0x000u}, 2},

    /* 4: centre */
    {{0x038u, 0x092u, 0x111u, 0x054u}, 4},

    /* 5: middle-right */
    {{0x038u, 0x124u, 0x000u, 0x000u}, 2},

    /* 6: bottom-left */
    {{0x1C0u, 0x049u, 0x054u, 0x000u}, 3},

    /* 7: bottom-middle */
    {{0x1C0u, 0x092u, 0x000u, 0x000u}, 2},

    /* 8: bottom-right */
    {{0x1C0u, 0x124u, 0x111u, 0x000u}, 3},
};

// Opponent player 0->1, 1->0
static inline u8 opponent(u8 player)
{
    ASSERT(player < 2);
    return (u8)(player ^ 1u);
}

static inline mask9 board2_owned(const Board2 *board, u8 player)
{
    ASSERT(board != NULL);
    ASSERT(player < 2);

    return (mask9)(
        board->marks[player][UBOARD]
        & (mask9)~board->marks[opponent(player)][UBOARD]
    );
}

/* *remaining is a nonzero mask9; return its lowest set bit and remove exactly
 * that bit.  The AND with the two's-complement negation isolates the lowest
 * set bit without scanning the mask. */
static inline onehot9 take_one_bit(mask9 *remaining)
{
    onehot9 bit;
    ASSERT(remaining != NULL);
    ASSERT(*remaining != 0);
    ASSERT_MASK9(*remaining);
    bit = (onehot9)(*remaining & (mask9)(-*remaining));
    *remaining &= (mask9)~bit;
    return bit;
}

static const mask9 board2_line_masks[8] = {
    0x007u, 0x038u, 0x1C0u,
    0x049u, 0x092u, 0x124u,
    0x111u, 0x054u
};
_Static_assert(sizeof board2_line_masks == 16,
               "SSE2 line-mask load must cover exactly eight u16 lanes");

/* Experimental SSE2 candidate generation.  Each lane computes the cells
 * missing from one line, then missing & (missing - 1) is zero exactly when
 * that line has zero or one missing cell.  The compare keeps one-missing
 * lanes; zero-missing lanes contribute zero naturally.  Byte shifts fold the
 * eight u16 candidates into lane zero, and the final mask removes opponent
 * cells. */
static inline mask9 winning_cells_simd(mask9 mine, mask9 opponent)
{
    ASSERT_MASK9(mine);
    ASSERT_MASK9(opponent);
    const __m128i lines = _mm_loadu_si128(
        (const __m128i *)(const void *)board2_line_masks);
    const __m128i mine_lanes = _mm_set1_epi16((short)mine);
    const __m128i missing = _mm_andnot_si128(mine_lanes, lines);
    const __m128i one = _mm_set1_epi16(1);
    const __m128i lower = _mm_sub_epi16(missing, one);
    const __m128i multiple_missing = _mm_and_si128(missing, lower);
    const __m128i one_or_zero_missing = _mm_cmpeq_epi16(
        multiple_missing, _mm_setzero_si128());
    __m128i candidates = _mm_and_si128(missing, one_or_zero_missing);
    candidates = _mm_or_si128(candidates, _mm_srli_si128(candidates, 8));
    candidates = _mm_or_si128(candidates, _mm_srli_si128(candidates, 4));
    candidates = _mm_or_si128(candidates, _mm_srli_si128(candidates, 2));
    mask9 result = (mask9)((unsigned)_mm_cvtsi128_si32(candidates) &
                           M111111111);

    return (mask9)(result & (mask9)~opponent);
}

/* Experimental SSE2 count of lines with exactly one mine mark and no
 * opponent mark.  A nonzero mine lane ANDed with mine & (mine - 1) == 0 is
 * exactly one set bit; the opponent zero comparison excludes blocked lines.
 * A qualifying u16 lane becomes 0xffff, so _mm_movemask_epi8 contributes two
 * set bits per qualifying lane.  Dividing the popcount by two recovers the
 * number of qualifying u16 lines. */
static inline u8 live_one_mark_lines_simd(mask9 mine, mask9 opponent)
{
    ASSERT_MASK9(mine);
    ASSERT_MASK9(opponent);
    const __m128i lines = _mm_loadu_si128(
        (const __m128i *)(const void *)board2_line_masks);
    const __m128i mine_lanes = _mm_set1_epi16((short)mine);
    const __m128i opponent_lanes = _mm_set1_epi16((short)opponent);
    const __m128i zero = _mm_setzero_si128();
    const __m128i one = _mm_set1_epi16(1);
    const __m128i mine_in_line = _mm_and_si128(lines, mine_lanes);
    const __m128i opponent_in_line = _mm_and_si128(lines, opponent_lanes);
    const __m128i mine_nonzero = _mm_andnot_si128(
        _mm_cmpeq_epi16(mine_in_line, zero), _mm_set1_epi16((short)-1));
    const __m128i one_mine = _mm_cmpeq_epi16(
        _mm_and_si128(mine_in_line,
                      _mm_sub_epi16(mine_in_line, one)), zero);
    const __m128i opponent_zero = _mm_cmpeq_epi16(opponent_in_line, zero);
    const __m128i qualifying = _mm_and_si128(
        _mm_and_si128(mine_nonzero, one_mine), opponent_zero);
    const int lane_bits = _mm_movemask_epi8(qualifying);
    return (u8)(__builtin_popcount((unsigned)lane_bits) / 2);
}

/* Experimental 512-entry caches.  Each mask selects one bit: word m >> 6,
 * bit m & 63.  Values were generated independently by
 * tools/generate_board2_truth_tables.py. */
static const uint64_t board2_has_three_in_a_row_vcache_table[8] = {
    UINT64_C(0xFF80808080808080),
    UINT64_C(0xFFF0AA80FAF0AA80),
    UINT64_C(0xFFCC8080CCCC8080),
    UINT64_C(0xFFFCAA80FEFCAA80),
    UINT64_C(0xFFFAF0F0AAAA8080),
    UINT64_C(0xFFFAFAF0FAFAAA80),
    UINT64_C(0xFFFEF0F0EEEE8080),
    UINT64_C(0xFFFFFFFFFFFFFFFF),
};

static const uint64_t board2_still_win_vcache_table[8] = {
    UINT64_C(0xFFFFFFFFFFFFFFFF),
    UINT64_C(0x010177770F0F7FFF),
    UINT64_C(0x01555F5F0F5F5FFF),
    UINT64_C(0x010155550F0F5FFF),
    UINT64_C(0x01553F7F01553FFF),
    UINT64_C(0x01013333010133FF),
    UINT64_C(0x01550F5F01550FFF),
    UINT64_C(0x01010101010101FF),
};

/* Bits are row-major (x + 3*y).  The shifts detect complete rows, columns,
 * and diagonals: 0x049 marks row starts 0/3/6, 0x007 marks column starts
 * 0/1/2, and 0x004 selects the anti-diagonal start at 2.  TODO: evaluate
 * later whether using the last move's bit improves speed; make no assumption
 * about that until measured. */
static inline bool _has_three_in_a_row(mask9 m)
{
    ASSERT_MASK9(m);
    const mask9 rows = m & (m >> 1) & (m >> 2) & 0x049;
    const mask9 columns = m & (m >> 3) & (m >> 6) & 0x007;
    const mask9 diagonal = m & (m >> 4) & (m >> 8);
    const mask9 anti_diagonal = m & (m >> 2) & (m >> 4) & 0x004;
    return (rows | columns | diagonal | anti_diagonal) != 0;
}

/* Experimental lookup equivalent of has_three_in_a_row; the two-level index
 * selects the precomputed truth-table bit for this nine-bit mask. */
static inline bool _has_three_in_a_row_vcache(mask9 m)
{
    ASSERT_MASK9(m);
    return (board2_has_three_in_a_row_vcache_table[m >> 6] >> (m & 63)) & 1;
}

// Wrapper to use the vcache if enabled
static inline bool has_three_in_a_row(mask9 m)
{
#if defined(BOARD2_USE_VCACHE) && BOARD2_USE_VCACHE
    return _has_three_in_a_row_vcache(m);
#else
    return _has_three_in_a_row(m);
#endif
}


/* A player can still claim this local board iff no line is blocked by the
 * opponent.  The board may already be closed at the master level; this is a
 * geometric fact used by the monotone cannot_claim masks. */
static inline bool _still_win(mask9 opponent_marks)
{
    ASSERT_MASK9(opponent_marks);
    for (u8 i = 0; i < 8; i++) {
        if ((opponent_marks & board2_line_masks[i]) == 0)
            return true;
    }
    return false;
}

/* Experimental lookup equivalent of still_win; the table bit means some
 * complete line has no mark from the opponent. */
static inline bool _still_win_vcache(mask9 opponent_marks)
{
    ASSERT_MASK9(opponent_marks);
    return (board2_still_win_vcache_table[opponent_marks >> 6] >>
            (opponent_marks & 63)) & 1;
}

static inline bool still_win(mask9 opponent_marks) {
#if defined(BOARD2_USE_VCACHE) && BOARD2_USE_VCACHE
    return _still_win_vcache(opponent_marks);
#else
    return _still_win(opponent_marks);
#endif
}


typedef enum Board2CertificateResult_e {
    BOARD2_NO_CERTIFICATE = 0,
    BOARD2_CERTIFIED_WIN = 1,
    BOARD2_CERTIFIED_DRAW = 2
} Board2CertificateResult;

/* Prove a master result for player from the current U
 * status.  `other_possible` is an optimistic upper bound for the opponent:
 * every open cell not already ruled out by cannot_claim[other] is treated as
 * theirs.  Therefore a missing opponent line plus a strict owner-count lead
 * is sufficient; equality is not sufficient for a win because the opponent
 * may still take the tied cells.  A certified draw additionally requires that
 * every open cell is ruled out for both players and the actual owner counts
 * are equal. */
static inline Board2CertificateResult certified_result(const Board2 *board,
                                                       u8 player)
{
    mask9 open;
    mask9 p_owned;
    mask9 other_owned;
    mask9 other_claimable;
    mask9 other_possible;
    u8 other;

    ASSERT(board != NULL);
    ASSERT(player < 2);
    ASSERT_MASK9(board->marks[0][UBOARD]);
    ASSERT_MASK9(board->marks[1][UBOARD]);
    ASSERT_MASK9(board->cannot_claim[0]);
    ASSERT_MASK9(board->cannot_claim[1]);
    other = player ^ 1u;
    open = (mask9)(M111111111 &
                   ~(board->marks[0][UBOARD] | board->marks[1][UBOARD]));
    p_owned = (mask9)(board->marks[player][UBOARD] &
                      (mask9)~board->marks[other][UBOARD]);
    other_owned = (mask9)(board->marks[other][UBOARD] &
                          (mask9)~board->marks[player][UBOARD]);
    other_claimable = (mask9)(open &
                              (mask9)~board->cannot_claim[other]);
    other_possible = (mask9)(other_owned | other_claimable);
    ASSERT_MASK9(open);
    ASSERT_MASK9(p_owned);
    ASSERT_MASK9(other_owned);
    ASSERT_MASK9(other_claimable);
    ASSERT_MASK9(other_possible);
    if (!has_three_in_a_row(other_possible) &&
        __builtin_popcount(p_owned) > __builtin_popcount(other_possible))
        return BOARD2_CERTIFIED_WIN;
    if ((open & (mask9)~board->cannot_claim[0]) == 0 &&
        (open & (mask9)~board->cannot_claim[1]) == 0 &&
        __builtin_popcount(p_owned) == __builtin_popcount(other_owned))
        return BOARD2_CERTIFIED_DRAW;
    return BOARD2_NO_CERTIFICATE;
}

// Call after a uboard cell is won or drawn.
// checks is this closes all uboard cells and if so resolves the game
// to a count win or a draw.
// returns true if the game is closed
static inline bool check_and_close_uboard(Board2 *board) {
    mask9 closed = board->marks[0][UBOARD] | board->marks[1][UBOARD];
    if (closed == M111111111) {
        u8 p0 = (u8)__builtin_popcount(board->marks[0][UBOARD]);
        u8 p1 = (u8)__builtin_popcount(board->marks[1][UBOARD]);
        if (p0 == p1) {
            board->winner = 3;
        } else {
            board->winner = (i8)(p0 > p1) ? (0+1) : (1+1); // p0 win is 1, p1 win is 2
        }
        return true;
    }
    return false;
}

static inline void board2_update_playable_subboards(Board2 *board, Move move)
{
    if (board->winner != BOARD2_IN_PROGRESS) {
        board->playable_subboards = 0;
        return;
    }

    mask9 open_subboards = (mask9)(M111111111 &
        ~(board->marks[0][UBOARD] | board->marks[1][UBOARD]));
    mask9 destination_bit = move.local_bit;
    board->playable_subboards = (open_subboards & destination_bit) != 0
        ? destination_bit : open_subboards;
}
/*
 * Apply one legal local move and close the local/master cell when required.
 * Preconditions: board is in progress; cell is 0..8; player is 0 or 1; bit is one
 * hot and contained in M111111111; the selected local bit is empty; and the
 * selected master cell is open.  Invalid inputs are caller errors.
 *
 * The function intentionally has no table, allocation, I/O, or hidden
 * coordinate conversion.  The preconditions guarantee nine-bit status and
 * claim masks.  A local draw sets both status planes (11), while a local win
 * sets only the winning player's plane (10 or 01).  Master line ownership is
 * computed from actual owner masks, so drawn cells never count for either
 * player.  Once all master cells close, the owner counts decide a final
 * non-line result; equal counts produce winner 3.  The boolean return reports
 * only whether this move changed certificate input (`cannot_claim` or U
 * status); it is not a legality or move-success result.
 */
static inline bool board2_play(Board2 *board, Move move ,u8 player)
{
    ASSERT(board != NULL);
    ASSERT(move.subboard < 9);
    ASSERT(player < 2);
    ASSERT_1SHOT9(move.local_bit);
    ASSERT(board->winner == BOARD2_IN_PROGRESS);
    
    mask9 occupied = (mask9)(board->marks[0][move.subboard] | board->marks[1][move.subboard]);
    ASSERT_MASK9(occupied);
    ASSERT( (move.local_bit & occupied) == 0 );
    bool proof_changed = false;
    u8 op = opponent(player);
    onehot9 subboard_bit = onehot9_from_index(move.subboard);

    board->marks[player][move.subboard] |= move.local_bit;
    occupied |= move.local_bit;
 
    bool local_win = has_three_in_a_row(board->marks[player][move.subboard]);

    if (local_win) { // player just won the local board with a three-in-a-row
        board->marks[player][UBOARD] |= subboard_bit;      // Win this sqaure on the U-board
        proof_changed = true;
        board->cannot_claim[0] |= subboard_bit;
        board->cannot_claim[1] |= subboard_bit;
        mask9 remaining = (mask9)(M111111111 & ~occupied);

        board->marks[player][move.subboard] |= remaining; // FILL all blanks in sub-board
        mask9 owned = board2_owned(board, player);
        
        if ( has_three_in_a_row(owned) ) {
            board->winner = (i8)(player + 1);  
            board2_update_playable_subboards(board, move);
            return true;      
        }
        // Player has not got 3IAR but might have closed the last sub-board
        check_and_close_uboard(board);
        board2_update_playable_subboards(board, move);
        return true;
    }

    bool local_draw = ( occupied == M111111111 );

    if (local_draw) {
        board->marks[0][UBOARD] |= subboard_bit; // Both players bit set for a draw
        board->marks[1][UBOARD] |= subboard_bit;
        board->cannot_claim[0] |= subboard_bit;  // neither player can claim this board
        board->cannot_claim[1] |= subboard_bit;
        check_and_close_uboard(board);
        board2_update_playable_subboards(board, move);
        return true;
    }

    // final check - op just played - can p still win this as yet unclaimed sub-board?

    bool claimability_already_lost = (board->cannot_claim[op] & subboard_bit) != 0;

    if (!claimability_already_lost && !still_win(board->marks[player][move.subboard])) {
        board->cannot_claim[op] |= subboard_bit;
        proof_changed = true;
    }    

    board2_update_playable_subboards(board, move);
    return proof_changed;
}

/* Generate the currently forced single-board or unrestricted move set. */
static inline ValidMoves valid_moves(const Board2 *board)
{
    ValidMoves result = {0};
    mask9 playable;

    ASSERT(board != NULL);
    ASSERT(board->winner == BOARD2_IN_PROGRESS);
    ASSERT_MASK9(board->marks[0][UBOARD]);
    ASSERT_MASK9(board->marks[1][UBOARD]);

    playable = board->playable_subboards;
    ASSERT(playable != 0);
    ASSERT((playable & (mask9)~M111111111) == 0);
    ASSERT((playable & (board->marks[0][UBOARD] |
                        board->marks[1][UBOARD])) == 0);

    if (__builtin_popcount((unsigned)playable) > 1) {
        __m128i p0 = _mm_loadu_si128(
            (const __m128i *)(const void *)&board->marks[0][0]);
        __m128i p1 = _mm_loadu_si128(
            (const __m128i *)(const void *)&board->marks[1][0]);
        const __m128i all_cells = _mm_set1_epi16((short)M111111111);
        __m128i occupied = _mm_or_si128(p0, p1);
        __m128i free_cells = _mm_andnot_si128(occupied, all_cells);
        __m128i zero = _mm_setzero_si128();
        __m128i zero_lanes = _mm_cmpeq_epi16(free_cells, zero);
        __m128i packed_zeroes = _mm_packs_epi16(zero_lanes, zero_lanes);
        u8 zero_boards = (u8)_mm_movemask_epi8(packed_zeroes);
        u8 boards8 = (u8)(~zero_boards);
        uint64_t chunks[2];

        /* The two unaligned 16-byte loads cover local boards 0..7.  Each
         * 16-bit lane is inverted and clipped to nine legal cell bits. */
        _mm_storeu_si128((__m128i *)(void *)&result.full.moves[0], free_cells);
        result.full.moves[8] = (mask9)(M111111111 &
            ~(board->marks[0][8] | board->marks[1][8]));
        for (u8 subboard = 0; subboard < 9; ++subboard)
            if ((playable & (mask9)(1u << subboard)) == 0)
                result.full.moves[subboard] = 0;
        result.full.boards = (mask9)boards8 & playable;
        if (result.full.moves[8] != 0)
            result.full.boards |= (mask9)(1u << 8) & playable;
        /* _mm_cmpeq_epi16 produces 0xffff for empty lanes.  Packing reduces
         * each 16-bit result to a signed byte while retaining its top bit;
         * _mm_movemask_epi8 extracts those bits, whose low 8 bits represent
         * boards 0..7.  Complementing selects boards that have moves.
         * memcpy counts the eight masks as two 64-bit chunks without creating
         * alignment or strict-aliasing undefined behavior. */
        memcpy(chunks, result.full.moves, sizeof chunks);
        result.full.count = (u16)(__builtin_popcountll(chunks[0]) +
                                  __builtin_popcountll(chunks[1]) +
                                  __builtin_popcount(result.full.moves[8]));
        result.kind = MULTI_BOARD;
        return result;
    }

    {
        u8 subboard = (u8)__builtin_ctz((unsigned)playable);
        mask9 occupied = (mask9)(board->marks[0][subboard] |
                                 board->marks[1][subboard]);
        mask9 bits = (mask9)(M111111111 & ~occupied);
        ASSERT_MASK9(board->marks[0][subboard]);
        ASSERT_MASK9(board->marks[1][subboard]);
        ASSERT_MASK9(occupied);
        ASSERT_MASK9(bits);
        result.kind = SINGLE_BOARD;
        result.single = (SingleBoardMoves){subboard, bits};
    }
    return result;
}

/* Destructively return the lowest local bit from either move representation.
 * Full moves keep boards and count synchronized with their remaining masks. */
static inline bool next_move(ValidMoves *moves, Move *out)
{
    ASSERT(moves != NULL);
    ASSERT(out != NULL);

    if (moves->kind == SINGLE_BOARD) {
        onehot9 bit;
        if (moves->single.bits == 0)
            return false;
        bit = take_one_bit(&moves->single.bits);
        out->subboard = moves->single.subboard;
        out->local_bit = bit;
        return true;
    }

    ASSERT(moves->kind == MULTI_BOARD);
    if (moves->full.boards == 0)
        return false;

    {
        u8 subboard = (u8)__builtin_ctz((unsigned)moves->full.boards);
        onehot9 bit = take_one_bit(&moves->full.moves[subboard]);
        if (moves->full.moves[subboard] == 0)
            moves->full.boards &= (mask9)~(mask9)(1u << subboard);
        moves->full.count--;
        out->subboard = subboard;
        out->local_bit = bit;
        return true;
    }
}

#endif /* UTTT_BOARD2_H */
