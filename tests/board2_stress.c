#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine/board2.h"

enum { TEST_MASTER_SEED = 0x13579BDFu };

static const mask9 lines[8] = {
    0x007, 0x038, 0x1C0, 0x049, 0x092, 0x124, 0x111, 0x054
};

typedef struct Reference_s {
    mask9 local[2][9];
    mask9 u[2];
    mask9 playable_subboards;
    i8 winner;
} Reference;

static uint64_t next_random(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static bool scalar_has_line(mask9 marks)
{
    for (unsigned i = 0; i < 8; i++)
        if ((marks & lines[i]) == lines[i]) return true;
    return false;
}

static bool scalar_can_still_win(mask9 opponent)
{
    for (unsigned i = 0; i < 8; i++)
        if ((opponent & lines[i]) == 0) return true;
    return false;
}

static i8 scalar_winner(const Reference *ref)
{
    mask9 owner[2] = {
        (mask9)(ref->u[0] & (mask9)~ref->u[1]),
        (mask9)(ref->u[1] & (mask9)~ref->u[0])
    };
    mask9 closed = (mask9)(ref->u[0] | ref->u[1]);
    for (unsigned p = 0; p < 2; p++)
        if (scalar_has_line(owner[p])) return (i8)(p + 1);
    if (closed == M111111111) {
        unsigned p0 = (unsigned)__builtin_popcount(owner[0]);
        unsigned p1 = (unsigned)__builtin_popcount(owner[1]);
        return (i8)(p0 == p1 ? BOARD2_DRAW :
                    (p0 > p1 ? BOARD2_PLAYER0_WIN : BOARD2_PLAYER1_WIN));
    }
    return BOARD2_IN_PROGRESS;
}

static void fail(unsigned game, unsigned ply, const char *message)
{
    fprintf(stderr, "board2 stress failure game=%u ply=%u: %s\n",
            game, ply, message);
    exit(EXIT_FAILURE);
}

#define CHECK(g,p,c,msg) do { if (!(c)) fail((g),(p),(msg)); } while (0)

static void check_state(const Board2 *board, const Reference *ref,
                        unsigned game, unsigned ply)
{
    mask9 closed = (mask9)(board->marks[0][UBOARD] |
                           board->marks[1][UBOARD]);
    CHECK(game, ply, board->winner == ref->winner, "winner mismatch");
    CHECK(game, ply, board->playable_subboards == ref->playable_subboards,
          "playable subboard mismatch");
    CHECK(game, ply,
          (board->playable_subboards & (mask9)~M111111111) == 0,
          "bad playable subboard mask");
    CHECK(game, ply, (closed & (mask9)~M111111111) == 0, "bad U mask");
    for (unsigned p = 0; p < 2; p++) {
        CHECK(game, ply, board->marks[p][UBOARD] == ref->u[p],
              "U plane mismatch");
        CHECK(game, ply, (board->marks[p][UBOARD] &
                          (mask9)~M111111111) == 0, "bad U plane");
        CHECK(game, ply, (board->cannot_claim[p] &
                          (mask9)~M111111111) == 0, "bad claim mask");
        for (unsigned cell = 0; cell < 9; cell++) {
            mask9 mine = board->marks[p][cell];
            CHECK(game, ply, (mine & (mask9)~M111111111) == 0,
                  "bad local mask");
            CHECK(game, ply, (mine & board->marks[p ^ 1u][cell]) == 0,
                  "local masks overlap");
            if ((closed & (mask9)(1u << cell)) == 0) {
                CHECK(game, ply, mine == ref->local[p][cell],
                      "open local state mismatch");
                if (!scalar_can_still_win(board->marks[p ^ 1u][cell]) &&
                    (board->cannot_claim[p] & (mask9)(1u << cell)) == 0) {
                    fprintf(stderr, "p=%u cell=%u own=%03x opp=%03x claim=%03x\n",
                            p, cell, board->marks[p][cell],
                            board->marks[p ^ 1u][cell], board->cannot_claim[p]);
                    fail(game, ply, "missing claimability bit");
                }
            }
        }
    }
    for (unsigned cell = 0; cell < 9; cell++) {
        mask9 bit = (mask9)(1u << cell);
        bool is_closed = (closed & bit) != 0;
        bool draw = (board->marks[0][UBOARD] & bit) != 0 &&
                    (board->marks[1][UBOARD] & bit) != 0;
        if (is_closed) {
            mask9 occupied = (mask9)(board->marks[0][cell] |
                                     board->marks[1][cell]);
            CHECK(game, ply, occupied == M111111111, "closed local not full");
            if (draw) {
                CHECK(game, ply, !scalar_has_line(board->marks[0][cell]) &&
                      !scalar_has_line(board->marks[1][cell]),
                      "drawn local contains a line");
            } else {
                mask9 owner = (board->marks[0][UBOARD] & bit) ?
                    board->marks[0][cell] : board->marks[1][cell];
                CHECK(game, ply, scalar_has_line(owner),
                      "owned local lacks a line");
            }
        }
    }
}

static void reference_play(Reference *ref, unsigned cell, onehot9 bit,
                           unsigned player, unsigned game, unsigned ply)
{
    mask9 cell_bit = (mask9)(1u << cell);
    CHECK(game, ply, (ref->u[0] | ref->u[1]) & cell_bit ? 0 : 1,
          "reference selected closed board");
    CHECK(game, ply, ((ref->local[0][cell] | ref->local[1][cell]) & bit) == 0,
          "reference selected occupied cell");
    ref->local[player][cell] |= bit;
    if (scalar_has_line(ref->local[player][cell])) {
        ref->u[player] |= cell_bit;
    } else if ((ref->local[0][cell] | ref->local[1][cell]) == M111111111) {
        ref->u[0] |= cell_bit;
        ref->u[1] |= cell_bit;
    }
    ref->winner = scalar_winner(ref);
    if (ref->winner != BOARD2_IN_PROGRESS) {
        ref->playable_subboards = 0;
    } else {
        mask9 open_subboards = (mask9)(M111111111 &
            ~(ref->u[0] | ref->u[1]));
        ref->playable_subboards = (open_subboards & bit) != 0
            ? bit : open_subboards;
    }
}

static uint64_t hash_state(uint64_t hash, const Board2 *board,
                           unsigned player, Move move)
{
    const uint64_t fnv_prime = UINT64_C(1099511628211);
    #define HASH_BYTE(h, b) (((h) ^ (uint8_t)(b)) * fnv_prime)
    #define HASH_U16(h, v) HASH_BYTE(HASH_BYTE((h), (v)), (v) >> 8)
    hash = HASH_BYTE(hash, player);
    hash = HASH_BYTE(hash, move.subboard);
    hash = HASH_U16(hash, move.local_bit);
    for (unsigned p = 0; p < 2; p++) {
        for (unsigned cell = 0; cell <= UBOARD; cell++) {
            hash = HASH_U16(hash, board->marks[p][cell]);
        }
        hash = HASH_U16(hash, board->cannot_claim[p]);
    }
    hash = HASH_BYTE(hash, board->winner);
    hash = HASH_U16(hash, board->playable_subboards);
    #undef HASH_U16
    #undef HASH_BYTE
    return hash;
}

static unsigned enumerate_moves(ValidMoves moves, bool legal[81],
                                unsigned game, unsigned ply)
{
    Move move;
    unsigned count = 0;
    while (next_move(&moves, &move)) {
        unsigned index = (unsigned)move.subboard * 9u +
                         (unsigned)__builtin_ctz((unsigned)move.local_bit);
        CHECK(game, ply, index < 81 && legal[index], "illegal generated move");
        legal[index] = false;
        count++;
    }
    return count;
}

static unsigned play_game(unsigned game, uint64_t master_seed,
                          uint64_t *out_hash, unsigned *out_plies,
                          unsigned *out_cert_positions,
                          bool *out_certified_game)
{
    Board2 board = board2_initial();
    Reference ref = {.playable_subboards = M111111111};
    uint64_t rng = master_seed + UINT64_C(0x9e3779b97f4a7c15) * (game + 1);
    uint64_t hash = UINT64_C(1469598103934665603);
    Move last = {0, 1};
    unsigned player = 0;
    unsigned ply = 0;
    unsigned certified_players = 0;
    unsigned cert_positions = 0;
    bool draw_certificate = false;
    bool certificate_seen = false;
    ref.winner = BOARD2_IN_PROGRESS;

    {
        CHECK(game, ply, player == 0, "opening player mismatch");
        unsigned opening = (unsigned)(next_random(&rng) % 81u);
        Move move = {(u8)(opening / 9u),
                     (onehot9)(1u << (opening % 9u))};
        bool legal[81] = {true};
        (void)legal;
        board2_play(&board, move, player);
        reference_play(&ref, move.subboard, move.local_bit, player, game, ply);
        check_state(&board, &ref, game, ply);
        hash = hash_state(hash, &board, player, move);
        last = move;
        ply++;
        if (board.winner == BOARD2_IN_PROGRESS) {
            Board2CertificateResult p0_result = certified_result(&board, 0);
            Board2CertificateResult p1_result = certified_result(&board, 1);
            if (p0_result == BOARD2_CERTIFIED_WIN) certified_players |= 1u;
            if (p1_result == BOARD2_CERTIFIED_WIN) certified_players |= 2u;
            if (p0_result == BOARD2_CERTIFIED_DRAW ||
                p1_result == BOARD2_CERTIFIED_DRAW) draw_certificate = true;
            if (p0_result != BOARD2_NO_CERTIFICATE ||
                p1_result != BOARD2_NO_CERTIFICATE) {
                cert_positions++;
                certificate_seen = true;
            }
        }
        player ^= 1u;
    }

    while (board.winner == BOARD2_IN_PROGRESS) {
        CHECK(game, ply, player == (ply & 1u), "player alternation mismatch");
        bool legal[81] = {false};
        unsigned target = (unsigned)__builtin_ctz((unsigned)last.local_bit);
        mask9 closed = (mask9)(ref.u[0] | ref.u[1]);
        ValidMoves moves = valid_moves(&board);
        mask9 expected_playable = 0;
        unsigned expected = 0;
        bool remaining_legal[81];

        if ((closed & (mask9)(1u << target)) == 0) {
            mask9 available = (mask9)(M111111111 &
                ~(ref.local[0][target] | ref.local[1][target]));
            expected_playable = (mask9)(1u << target);
            for (unsigned bit = 0; bit < 9; bit++)
                if (available & (mask9)(1u << bit))
                    legal[target * 9u + bit] = true;
        } else {
            for (unsigned cell = 0; cell < 9; cell++) {
                if (closed & (mask9)(1u << cell)) continue;
                mask9 available = (mask9)(M111111111 &
                    ~(ref.local[0][cell] | ref.local[1][cell]));
                if (available != 0)
                    expected_playable |= (mask9)(1u << cell);
                for (unsigned bit = 0; bit < 9; bit++)
                    if (available & (mask9)(1u << bit))
                        legal[cell * 9u + bit] = true;
            }
        }
        CHECK(game, ply, board.playable_subboards == expected_playable,
              "playable subboards differ from independent legal mask");
        for (unsigned i = 0; i < 81; i++) expected += legal[i];
        if (moves.kind == SINGLE_BOARD)
            CHECK(game, ply, __builtin_popcount(moves.single.bits) == expected,
                  "single move count mismatch");
        else
            CHECK(game, ply, moves.full.count == expected,
                  "full move count mismatch");
        memcpy(remaining_legal, legal, sizeof legal);
        CHECK(game, ply,
              enumerate_moves(moves, remaining_legal, game, ply) == expected,
              "enumerated move count mismatch");

        moves = valid_moves(&board);
        {
            CHECK(game, ply, expected != 0, "no legal moves in progress");
            unsigned choice = (unsigned)(next_random(&rng) % expected);
            Move chosen = {0, 0};
            for (unsigned i = 0; i <= choice; i++)
                CHECK(game, ply, next_move(&moves, &chosen),
                      "chosen move exhausted early");
            unsigned chosen_index = (unsigned)chosen.subboard * 9u +
                (unsigned)__builtin_ctz((unsigned)chosen.local_bit);
            CHECK(game, ply, chosen_index < 81 && legal[chosen_index],
                  "chosen move not independently legal");
            board2_play(&board, chosen, player);
            reference_play(&ref, chosen.subboard, chosen.local_bit, player,
                           game, ply);
            check_state(&board, &ref, game, ply);
            hash = hash_state(hash, &board, player, chosen);
            last = chosen;
            if (board.winner == BOARD2_IN_PROGRESS) {
                Board2CertificateResult p0_result = certified_result(&board, 0);
                Board2CertificateResult p1_result = certified_result(&board, 1);
                if (p0_result == BOARD2_CERTIFIED_WIN) certified_players |= 1u;
                if (p1_result == BOARD2_CERTIFIED_WIN) certified_players |= 2u;
                if (p0_result == BOARD2_CERTIFIED_DRAW ||
                    p1_result == BOARD2_CERTIFIED_DRAW) draw_certificate = true;
                if (p0_result != BOARD2_NO_CERTIFICATE ||
                    p1_result != BOARD2_NO_CERTIFICATE) {
                    cert_positions++;
                    certificate_seen = true;
                }
            }
        }
        player ^= 1u;
        ply++;
        CHECK(game, ply, ply <= 81, "game exceeded 81 plies");
    }
    if (certified_players != 0) {
        CHECK(game, ply, certified_players == (1u << (board.winner - 1)),
              "certificate disagrees with final winner");
    }
    CHECK(game, ply, !(draw_certificate && certified_players),
          "win and draw certificates conflict");
    if (draw_certificate)
        CHECK(game, ply, board.winner == BOARD2_DRAW,
              "draw certificate disagrees with final result");
    *out_hash = hash;
    *out_plies = ply;
    *out_cert_positions = cert_positions;
    *out_certified_game = certificate_seen;
    return (unsigned)board.winner;
}

int main(int argc, char **argv)
{
    bool summary_only = argc == 3 && strcmp(argv[2], "--summary-only") == 0;
    unsigned games = (argc >= 2 && argc <= 3) ?
        (unsigned)strtoul(argv[1], NULL, 10) : 32;
    unsigned outcomes[4] = {0};
    unsigned min_plies = 1000, max_plies = 0, total_plies = 0;
    unsigned certificate_games = 0, certificate_positions = 0;
    uint64_t aggregate = UINT64_C(1469598103934665603);

    for (unsigned game = 0; game < games; game++) {
        uint64_t hash;
        unsigned plies;
        unsigned cert_positions;
        bool certified_game;
        unsigned outcome = play_game(game, TEST_MASTER_SEED, &hash, &plies,
                                     &cert_positions, &certified_game);
        if (outcome < 1 || outcome > 3) return 2;
        outcomes[outcome]++;
        if (plies < min_plies) min_plies = plies;
        if (plies > max_plies) max_plies = plies;
        total_plies += plies;
        certificate_positions += cert_positions;
        if (certified_game) certificate_games++;
        aggregate ^= hash;
        aggregate *= UINT64_C(1099511628211);
        if (!summary_only)
            printf("game=%u outcome=%u plies=%u hash=%016" PRIx64 "\n",
                   game, outcome, plies, hash);
    }
    printf("games=%u p0=%u p1=%u draws=%u min_plies=%u mean_plies=%.2f "
           "max_plies=%u aggregate=%016" PRIx64
           " certificate_games=%u certificate_positions=%u\n", games,
           outcomes[1], outcomes[2], outcomes[3], min_plies,
           games ? (double)total_plies / games : 0.0, max_plies, aggregate,
           certificate_games, certificate_positions);
    return 0;
}
