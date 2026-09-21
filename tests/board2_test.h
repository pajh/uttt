#ifndef UTTT_BOARD2_TEST_H
#define UTTT_BOARD2_TEST_H

/*
 * Include-only board2 test vectors.
 *
 * Each vector names one board2.h function, a fixed-size output blob packed in
 * a canonical byte order, and the FNV-1a hash that an independent Python
 * authority expects.  A vector's build function fills a caller-owned buffer
 * of exactly blob_len bytes; it never allocates.
 *
 * Blob packing rule: fields are appended in the documented order as
 * fixed-width little-endian values.  bool / u8 -> 1 byte, u16 -> 2 bytes.
 * Call order follows the order the output is packed, so a Python
 * implementation can reproduce the blob byte for byte.
 */

#include "../src/engine/board2.h"
#include "test_support.h"

typedef struct Board2TestVector_s {
    const char *name;
    uint64_t expected_hash;
    size_t blob_len;
    void (*build)(uint8_t *blob);
} Board2TestVector;

/* 001: _has_three_in_a_row(mask) for every nine-bit mask 0..511.
 * Canonical order: mask ascending; output one u8 boolean per mask. */
#define BOARD2_TEST_001_BLOB_LEN 512u

/* Confirmed by tests/verify_board2_vectors.py against the committed
 * tests/vectors/001_has_three_in_a_row.expected.bin reference blob. */
#define BOARD2_TEST_001_EXPECTED UINT64_C(0x6be279921dc177b1)

static inline void board2_test_001_build(uint8_t *blob)
{
    for (mask9 mask = 0; mask <= M111111111; mask++)
        blob[mask] = _has_three_in_a_row(mask) ? 1u : 0u;
}

/* 001b: _has_three_in_a_row_vcache(mask) is the lookup equivalent of 001, so
 * it shares that test's reference blob and expected hash. */
#define BOARD2_TEST_001B_BLOB_LEN BOARD2_TEST_001_BLOB_LEN
#define BOARD2_TEST_001B_EXPECTED BOARD2_TEST_001_EXPECTED

static inline void board2_test_001b_build(uint8_t *blob)
{
    for (mask9 mask = 0; mask <= M111111111; mask++)
        blob[mask] = _has_three_in_a_row_vcache(mask) ? 1u : 0u;
}

/* 002: _still_win(opponent_marks) for every nine-bit mask 0..511.
 * Canonical order: mask ascending; output one u8 boolean per mask.  The mask
 * holds the opponent's marks, and the answer is true when at least one of the
 * eight lines has no opponent mark. */
#define BOARD2_TEST_002_BLOB_LEN 512u

/* Confirmed by tests/verify_board2_vectors.py against the committed
 * tests/vectors/002_still_win.expected.bin reference blob. */
#define BOARD2_TEST_002_EXPECTED UINT64_C(0x8959ed04198177f1)

static inline void board2_test_002_build(uint8_t *blob)
{
    for (mask9 mask = 0; mask <= M111111111; mask++)
        blob[mask] = _still_win(mask) ? 1u : 0u;
}

/* 002b: _still_win_vcache(mask) is the lookup equivalent of 002, so it shares
 * that test's reference blob and expected hash. */
#define BOARD2_TEST_002B_BLOB_LEN BOARD2_TEST_002_BLOB_LEN
#define BOARD2_TEST_002B_EXPECTED BOARD2_TEST_002_EXPECTED

static inline void board2_test_002b_build(uint8_t *blob)
{
    for (mask9 mask = 0; mask <= M111111111; mask++)
        blob[mask] = _still_win_vcache(mask) ? 1u : 0u;
}

/* Build the U-board planes for a base-4 state: two bits per cell i, where the
 * digit d = (state >> 2i) & 3 is 0 open, 1 player 0, 2 player 1, 3 draw.
 * Player 0 owns d in {1,3}; player 1 owns d in {2,3}; draws set both bits. */
#define BOARD2_TEST_UBOARD_STATES 262144u /* 4^9 */

static inline void board2_test_build_uboard(unsigned state, Board2 *board)
{
    memset(board, 0, sizeof *board);
    for (u8 cell = 0; cell < 9; cell++) {
        unsigned status = (state >> (2 * cell)) & 3u;
        mask9 bit = (mask9)(1u << cell);
        if (status == 1u || status == 3u)
            board->marks[0][UBOARD] |= bit;
        if (status == 2u || status == 3u)
            board->marks[1][UBOARD] |= bit;
    }
    board->winner = BOARD2_IN_PROGRESS;
}

/* 003: take_one_bit on every nonzero nine-bit mask 1..511, ascending.  The
 * input is copied per case; output is u16 isolated low bit then u16 remaining
 * mask, both little-endian. */
#define BOARD2_TEST_003_BLOB_LEN (511u * 4u)
#define BOARD2_TEST_003_EXPECTED UINT64_C(0x5e737d488511b7dd)

static inline void board2_test_003_build(uint8_t *blob)
{
    uint8_t *out = blob;
    for (mask9 mask = 1; mask <= M111111111; mask++) {
        mask9 remaining = mask;
        onehot9 bit = take_one_bit(&remaining);
        out = test_put_u16(out, (u16)bit);
        out = test_put_u16(out, (u16)remaining);
    }
}

/* 004: winning_cells_simd(mine, opponent) over disjoint pairs.  Order: mine
 * ascending; opponent runs through all submasks of ~mine from ~mine down to 0
 * as (opponent - 1) & remaining.  Output u16 per pair. */
#define BOARD2_TEST_004_BLOB_LEN (19683u * 2u)
#define BOARD2_TEST_004_EXPECTED UINT64_C(0x4174c9ed1722f969)

static inline void board2_test_004_build(uint8_t *blob)
{
    uint8_t *out = blob;
    for (mask9 mine = 0; mine <= M111111111; mine++) {
        mask9 remaining = (mask9)(M111111111 & ~mine);
        mask9 opponent = remaining;
        for (;;) {
            out = test_put_u16(out, winning_cells_simd(mine, opponent));
            if (opponent == 0)
                break;
            opponent = (mask9)((opponent - 1) & remaining);
        }
    }
}

/* 005: live_one_mark_lines_simd(mine, opponent) over the same pair order as
 * 004.  Output one u8 count per pair. */
#define BOARD2_TEST_005_BLOB_LEN 19683u
#define BOARD2_TEST_005_EXPECTED UINT64_C(0x8ab577669b57d4df)

static inline void board2_test_005_build(uint8_t *blob)
{
    size_t offset = 0;
    for (mask9 mine = 0; mine <= M111111111; mine++) {
        mask9 remaining = (mask9)(M111111111 & ~mine);
        mask9 opponent = remaining;
        for (;;) {
            blob[offset++] = live_one_mark_lines_simd(mine, opponent);
            if (opponent == 0)
                break;
            opponent = (mask9)((opponent - 1) & remaining);
        }
    }
}

/* 006: board2_owned over all base-4 U-board states.  Output player 0's owned
 * mask then player 1's, each u16 little-endian. */
#define BOARD2_TEST_006_BLOB_LEN (BOARD2_TEST_UBOARD_STATES * 4u)
#define BOARD2_TEST_006_EXPECTED UINT64_C(0xc4368e85354f1725)

static inline void board2_test_006_build(uint8_t *blob)
{
    uint8_t *out = blob;
    for (unsigned state = 0; state < BOARD2_TEST_UBOARD_STATES; state++) {
        Board2 board;
        board2_test_build_uboard(state, &board);
        out = test_put_u16(out, board2_owned(&board, 0));
        out = test_put_u16(out, board2_owned(&board, 1));
    }
}

/* 007: check_and_close_uboard over all base-4 U-board states.  Output the
 * returned bool as u8 then the resulting winner as i8, one pair per state. */
#define BOARD2_TEST_007_BLOB_LEN (BOARD2_TEST_UBOARD_STATES * 2u)
#define BOARD2_TEST_007_EXPECTED UINT64_C(0xe22b5f73dadcc8d9)

static inline void board2_test_007_build(uint8_t *blob)
{
    size_t offset = 0;
    for (unsigned state = 0; state < BOARD2_TEST_UBOARD_STATES; state++) {
        Board2 board;
        board2_test_build_uboard(state, &board);
        bool closed = check_and_close_uboard(&board);
        blob[offset++] = closed ? 1u : 0u;
        blob[offset++] = (uint8_t)board.winner;
    }
}

static const Board2TestVector board2_test_vectors[] = {
    {
        "001_has_three_in_a_row",
        BOARD2_TEST_001_EXPECTED,
        BOARD2_TEST_001_BLOB_LEN,
        board2_test_001_build,
    },
    {
        "001b_has_three_in_a_row_vcache",
        BOARD2_TEST_001B_EXPECTED,
        BOARD2_TEST_001B_BLOB_LEN,
        board2_test_001b_build,
    },
    {
        "002_still_win",
        BOARD2_TEST_002_EXPECTED,
        BOARD2_TEST_002_BLOB_LEN,
        board2_test_002_build,
    },
    {
        "002b_still_win_vcache",
        BOARD2_TEST_002B_EXPECTED,
        BOARD2_TEST_002B_BLOB_LEN,
        board2_test_002b_build,
    },
    {
        "003_take_one_bit",
        BOARD2_TEST_003_EXPECTED,
        BOARD2_TEST_003_BLOB_LEN,
        board2_test_003_build,
    },
    {
        "004_winning_cells_simd",
        BOARD2_TEST_004_EXPECTED,
        BOARD2_TEST_004_BLOB_LEN,
        board2_test_004_build,
    },
    {
        "005_live_one_mark_lines_simd",
        BOARD2_TEST_005_EXPECTED,
        BOARD2_TEST_005_BLOB_LEN,
        board2_test_005_build,
    },
    {
        "006_board2_owned",
        BOARD2_TEST_006_EXPECTED,
        BOARD2_TEST_006_BLOB_LEN,
        board2_test_006_build,
    },
    {
        "007_check_and_close_uboard",
        BOARD2_TEST_007_EXPECTED,
        BOARD2_TEST_007_BLOB_LEN,
        board2_test_007_build,
    },
};

#define BOARD2_TEST_VECTOR_COUNT \
    (sizeof board2_test_vectors / sizeof board2_test_vectors[0])

#endif /* UTTT_BOARD2_TEST_H */
