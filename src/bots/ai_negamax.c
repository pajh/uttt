/*
 * Current Ultimate Tic-Tac-Toe competition bot.
 *
 * The main loop reconstructs the board from CodinGame's stdin protocol.  Each
 * turn uses iterative shallow minimax until its deadline, with a separate
 * exact solver in compact late-game positions.  board.h, hashmap.h and the
 * optional instrument.h are implementation headers so `subst` can create the
 * single C file required for submission.
 *
 * Internal coordinates are x = column, y = row; CodinGame I/O is row column.
 */
#pragma GCC optimize "O3,omit-frame-pointer,inline"
#pragma GCC target("lzcnt,popcnt")

/* ================================ LEVERS ================================
 * Change and record these values for an experiment.  The bot accepts no
 * behaviour-changing command-line options: a build has one clear identity.
 * HELLO_TEXT is the small, human-readable version reported by `--HELLO`.
 */
#define HELLO_TEXT "MM-010-R1MM" /* M = minimax, 010 = experiment counter, R = ratio score. */

/* Opening policy: two letters, the outer grid class then the inner cell
 * class.  Each letter is one of M, C or D under the cell numbering
 *
 *     0 1 2        D C D
 *     3 4 5   =>   C M C
 *     6 7 8        D C D
 *
 * so M is the middle cell 4, C a cardinal cell (1, 3, 5, 7) and D a diagonal
 * cell (0, 2, 6, 8).  "MM" is therefore the centre cell of the centre board.
 * D and C draw a fresh random cell from their class on every call. */
#define START_RULE "MM"

#define LEVER_USCALE 5
#define LEVER_COUNT_SCALE 1.0
#define LEVER_MASTER_THREAT 80

#define MAX_TIME 0.0900
#define TERMINAL_SCORE 60000
#define TIMEOUT_SCORE 65535
#define RELATIVE_SCORE_SCALE 10000

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h> 
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>
#include <search.h>

#include "../engine/board2.h"
#include "../engine/support.h"

uint64_t start_time;
double move_budget = MAX_TIME;
double search_deadline;
uint64_t search_nodes;
/* Returns monotonic microseconds for search deadlines and timing telemetry. */
uint64_t get_gtod_clock_time ()
{
    struct timespec tv;
    if (clock_gettime(CLOCK_MONOTONIC, &tv) != 0) { perror("clock_gettime"); exit(1); }
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_nsec / 1000;
}

/* Returns seconds elapsed since the current turn began. */
double getElaspedTime() {
    uint64_t elasped = get_gtod_clock_time() - start_time;
    return ( (double)elasped ) / 1000000.0;
}

#include "../engine/hashmap.h"
/* Game totals are reported only to the local rig, after stdin closes. */
static unsigned long long game_search_us, game_positions, game_cache_hits;
static unsigned long long game_cert_leaf_hits, game_cert_internal_hits;
static unsigned long turn_cache_hits;

/* Counts successful cache probes in normal and instrumented builds. */
static int searchCacheFind(HashMap *hm, unsigned char *key, u32 *data)
{
    int found = findHMEntry(hm, key, data);
    if (found) turn_cache_hits++;
    return found;
}
void error(const char *format, ...);

// Globals
HashMap* map;
static unsigned long evaluation_calls;

/* Prints a fatal diagnostic and terminates the bot. */
void error(const char *format, ...)
{    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);    
    va_end(args);
    fprintf(stderr, "TERMINATING\n");

    exit(-1);
}

/* Samples the clock every 64 nodes to amortise timing overhead. */
int searchExpired(void) {
    return ((search_nodes++ & 63) == 0) && getElaspedTime() >= search_deadline;
}

typedef struct Score_s {
    u16 p0;
    u16 p1;
} Score;

/* Mutable only so the in-process regression fixtures can cover alternatives;
   production has no command-line path that changes these lever defaults. */
static double count_scale = LEVER_COUNT_SCALE;
static int uscale = LEVER_USCALE;
#include "local_rig.h"
#include "instrument.h"
#define COUNT_UNIT 20

static const u16 scoring_lines[8] = {7,56,448,73,146,292,273,84};

/* Converts a two-player score to p0/p1's bounded relative advantage. */
static int scoreForPlayer(Score score, int player) {
    if (score.p0 == TERMINAL_SCORE && score.p1 == TERMINAL_SCORE)
        return 0;
    if (score.p0 == TERMINAL_SCORE && score.p1 == 0)
        return player == 0 ? TERMINAL_SCORE : -TERMINAL_SCORE;
    if (score.p1 == TERMINAL_SCORE && score.p0 == 0)
        return player == 1 ? TERMINAL_SCORE : -TERMINAL_SCORE;
    int mine = player == 0 ? score.p0 : score.p1;
    int theirs = player == 0 ? score.p1 : score.p0;
    return RELATIVE_SCORE_SCALE * (mine - theirs) / (mine + theirs + 2);
}


static int grid_potential(mask9 mine, mask9 blocked)
{
    mask9 winning = winning_cells_simd(mine, blocked);
    int win_count = __builtin_popcount((unsigned)winning);

    int winning_score =
        win_count == 0 ? 0 :
        win_count == 1 ? 4 : 6;

    return winning_score
         + live_one_mark_lines_simd(mine, blocked);
}

// Based on the overall U board - how many sub-boards does "player" own?
static inline u8 player_u_win_count(const Board2 *board, u8 player)
{
    mask9 owned = board->marks[player][UBOARD]
                & (mask9)~board->marks[player ^ 1u][UBOARD];

    return (u8)__builtin_popcount((unsigned)owned);
}

// Assessment as to how relevant a U board cell is
// replaced the f2 function in the original code
// TODO: simd
// TODO: check if the + 1 is needed
// TODO: do we need the "closed" check - how abaout not calling it on closed cells!!!
// returns 1 + MAX(line score going through this cell)
// line score :
// 0 if the line is blocked by opponent or drawn cell
// 1 if the line is empty, 2 if palyer has 1 mark, 4 if the player has 2 marks
static int u_cell_relevance(const Board2 *board, u8 cell, u8 player)
{
    ASSERT(board != NULL);
    ASSERT(cell < 9);
    ASSERT(player < 2);

    onehot9 cell_bit = (onehot9)(1u << cell);
    mask9 closed = board->marks[0][UBOARD]
                 | board->marks[1][UBOARD];

    if (closed & cell_bit)
        return 0;

    mask9 owned = board2_owned(board, player);
    mask9 blocked = board->marks[player ^ 1u][UBOARD];

    int best_line_score = 0;

    for (u8 i = 0; i < board2_cell_lines[cell].count; ++i) {
        mask9 line = board2_cell_lines[cell].lines[i];

        if (line & blocked)
            continue;

        int owned_count =
            __builtin_popcount((unsigned)(line & owned));

        int line_score = 1 << owned_count;

        if (line_score > best_line_score)
            best_line_score = line_score;
    }

    return 1 + best_line_score;
}

static u8 live_u_winning_cell_count(const Board2 *board, u8 player)
{
    ASSERT(board != NULL);
    ASSERT(player < 2);

    mask9 owned = board2_owned(board, player);

    /*
     * The opponent's U-status plane blocks both opponent-owned and drawn
     * cells. Player-owned cells are already present in owned.
     */
    mask9 blocked = board->marks[opponent(player)][UBOARD];

    mask9 winning_cells = winning_cells_simd(owned, blocked);

    /*
     * A U-winning cell is only live if the player still has an unblocked
     * winning line in that cell's local board.
     */
    winning_cells &= (mask9)~board->cannot_claim[player];

    return (u8)__builtin_popcount((unsigned)winning_cells);
}



/* Score a nonterminal position without searching beyond this leaf. */
static Score score_board(const Board2 *board)
{
    ASSERT(board != NULL);
    ASSERT(board->winner == BOARD2_IN_PROGRESS);

    evaluation_calls++;

    mask9 closed = board->marks[0][UBOARD]
                 | board->marks[1][UBOARD];

    int strength[2];

    for (u8 player = 0; player < 2; ++player) {
        u8 other = opponent(player);

        mask9 u_owned = board2_owned(board, player);

        /*
         * The other status plane contains opponent-owned cells and draws,
         * both of which block this player's U lines.
         */
        mask9 u_blocked = board->marks[other][UBOARD];

        int main_strength =
            grid_potential(u_owned, u_blocked);

        strength[player] = main_strength * uscale;

        strength[player] +=
            (int)(
                player_u_win_count(board, player)
                * COUNT_UNIT
                * count_scale
                + 0.5
            );

        strength[player] +=
            LEVER_MASTER_THREAT
            * live_u_winning_cell_count(board, player);

        for (u8 cell = 0; cell < 9; ++cell) {
            onehot9 cell_bit = (onehot9)(1u << cell);

            if (closed & cell_bit)
                continue;

            int local_strength = grid_potential(
                board->marks[player][cell],
                board->marks[other][cell]
            );

            int relevance =
                u_cell_relevance(board, cell, player);

            strength[player] += local_strength * relevance;
        }

        if (strength[player] > 10000)
            strength[player] = 10000;
    }

    return (Score){
        (u16)strength[0],
        (u16)strength[1]
    };
}

static bool has_certified_immediate_win(
    const Board2 *board,
    Move previous_move,
    u8 player)
{
    ASSERT(board != NULL);
    ASSERT(board->winner == BOARD2_IN_PROGRESS);
    ASSERT(player < 2);

    ValidMoves moves = valid_moves(board, previous_move);
    Move move;

    while (next_move(&moves, &move)) {
        mask9 local_wins = winning_cells_simd(
            board->marks[player][move.subboard],
            board->marks[opponent(player)][move.subboard]
        );

        if (!(local_wins & move.local_bit))
            continue;

        Board2 after = *board;
        board2_play(
            &after,
            move,
            player
        );

        if (after.winner == (Board2Winner)(player + 1u))
            return true;

        if (certified_result(&after, player) ==
            BOARD2_CERTIFIED_WIN)
            return true;
    }

    return false;
}
static inline Score winnerScore(u8 winner) {
    ASSERT(winner < 2);
    return (winner == 0) ? (Score) { TERMINAL_SCORE,0 } : (Score) { 0,TERMINAL_SCORE };
}

static bool certifiedScore(const Board2 *board, Score *score) {
    for (u8 player = 0; player < 2; player++) {
        if (certified_result(board, player) == BOARD2_CERTIFIED_WIN) {
            *score = winnerScore(player);
            return true;
        }
    }
    if (certified_result(board, 0) == BOARD2_CERTIFIED_DRAW) {
        *score = (Score){TERMINAL_SCORE,TERMINAL_SCORE};
        return true;
    }
    return false;
}

/* Evaluates a bounded minimax subtree and propagates terminal or timeout values. */
Score evaluateShallow(Board2 board, int player, Move last_move, int depth, int timed,
                      bool force_cert_check) {
    if (timed && searchExpired()) return (Score){TIMEOUT_SCORE,TIMEOUT_SCORE};
    
    bool proof_changed = board2_play(&board, last_move, player);

    if (board.winner > 0) {
        u8 awinner = board.winner - 1u;
        if (awinner == 2)
            return (Score){TERMINAL_SCORE,TERMINAL_SCORE};
        return winnerScore(awinner);
    }

    if (force_cert_check || proof_changed) {
        Score certified;
        if (certifiedScore(&board, &certified)) return certified;
    }

    u8 op = opponent(player);

    if (depth == 0) return score_board(&board);

    ValidMoves moves = valid_moves(&board, last_move);
 
    int best_score = -1000000;
    Score best = (Score){0,0};
    u16 next_cell, next_bit;

    for (Move move; next_move(&moves, &move);) {
        Score score = evaluateShallow(board,op,move,depth-1,timed,false);
        if (score.p0 == TIMEOUT_SCORE) return score;
           
        int value = scoreForPlayer(score,1-player);  
        if (value > best_score) { best_score = value; best = score; }
        if (best_score == TERMINAL_SCORE) break;     
    } 
    return best;
}

enum RootProof {
    RootUnproved,
    RootForcedWin,
    RootForcedDraw,
    RootForcedLoss
};

typedef struct RootMove_s {
    Move move;
    int score;
    int evaluated_plies;
    enum RootProof proof;
} RootMove;

typedef struct RootMoves_s {
    RootMove moves[81];
    int count;
} RootMoves;

/* Iteratively deepens all viable root moves until the per-turn deadline. */
Move evaluateMovesShallowTimed(Board2 *board, ValidMoves valid_moves) {
    RootMoves current = {0};
    RootMoves previous = {0};
    //resetMoveIt(valid_moves); ?? still needed?
    
    for (Move move; next_move(&valid_moves,&move);) {
        current.moves[current.count].move = move;
        current.moves[current.count].proof = RootUnproved;
        current.count++;
    }
    previous = current;

    /* Four plies is the safety floor: current timing evidence says it always
       completes, while the old two-ply pass only consumed time.  There is no
       arbitrary depth ceiling.  Keep adding two plies until the clock stops
       us. Only a fully completed depth becomes eligible for final scoring. */
    int time_expired = 0;
    bool completed_depth = false;
    for (int target_plies=4; !time_expired; target_plies+=2) {

        int searchable_roots = 0;
        for (int i=0;i<current.count;i++)
            searchable_roots += current.moves[i].proof == RootUnproved;
        if (searchable_roots == 0) break;

        for (int i=0;i<current.count;i++) {
            RootMove *root = &current.moves[i];

            if (root->proof != RootUnproved) continue;
            if (getElaspedTime() >= search_deadline) {
                time_expired = 1;
                break;
            }

            u16 cell, bit;

            Score score = evaluateShallow(*board,0,root->move,target_plies-1,1,true
            );
            if (score.p0 == TIMEOUT_SCORE) {
                time_expired = 1;
                break;
            }
            INSTRUMENT_ROOT_EVALUATED(target_plies);
            localRigScore(target_plies, root->move, scoreForPlayer(score, 0));

            if (score.p0 == TERMINAL_SCORE && score.p1 == 0) {
                root->proof = RootForcedWin;
                root->score = TERMINAL_SCORE;
                root->evaluated_plies = target_plies;
                INSTRUMENT_SELECTED(TERMINAL_SCORE);
                return root->move;
            }

            if (score.p0 == TERMINAL_SCORE && score.p1 == TERMINAL_SCORE) {
                root->proof = RootForcedDraw;
                root->score = 0;
                root->evaluated_plies = target_plies;
                continue;
            }

            if (score.p1 == TERMINAL_SCORE && score.p0 == 0) {
                root->proof = RootForcedLoss;
                root->score = -TERMINAL_SCORE;
                root->evaluated_plies = target_plies;
                continue;
            }

            root->score = scoreForPlayer(score,0);
            root->evaluated_plies = target_plies;
        }

        if (time_expired) break;

        /* Publish only whole-depth results. Both buffers keep the same root
           index so a timed-out iteration can be reconciled without remapping. */
        previous = current;
        completed_depth = true;

        /* The next attempt starts with fresh ordinary scores, while proofs
           remain valid across depths. */
        for (int i=0;i<current.count;i++) {
            RootMove *root = &current.moves[i];
            if (root->proof == RootForcedLoss)
                root->score = -TERMINAL_SCORE;
            else
                root->score = 0;
            root->evaluated_plies = 0;
        }
    }

    if (completed_depth) {
        /* Discard ordinary scores from an interrupted deeper pass, but keep
           terminal proofs discovered during that pass. */
        for (int i=0;i<current.count;i++) {
            RootMove *root = &current.moves[i];
            const RootMove *saved = &previous.moves[i];
            if (root->proof == RootForcedLoss) {
                root->score = -TERMINAL_SCORE;
                continue;
            }
            if (root->proof == RootForcedDraw) {
                root->score = 0;
                if (root->evaluated_plies == 0 &&
                    saved->proof == RootForcedDraw)
                    root->evaluated_plies = saved->evaluated_plies;
                continue;
            }
            root->score = saved->score;
            root->evaluated_plies = saved->evaluated_plies;
            if (saved->proof == RootForcedDraw) {
                root->proof = RootForcedDraw;
                root->score = 0;
                continue;
            }
            if (saved->proof == RootForcedLoss) {
                root->proof = RootForcedLoss;
                root->score = -TERMINAL_SCORE;
            }
        }
    } else {
        /* With no complete depth there are no trustworthy ordinary scores.
           Prefer an unproved root, then a proved draw, over a forced loss. */
        for (int i=0;i<current.count;i++)
            if (current.moves[i].proof == RootUnproved)
                return current.moves[i].move;
        for (int i=0;i<current.count;i++)
            if (current.moves[i].proof == RootForcedDraw)
                return current.moves[i].move;
        return current.moves[0].move;
    }

    int best_index = 0;
    for (int i=1;i<current.count;i++)
        if (current.moves[i].score > current.moves[best_index].score)
            best_index = i;
    INSTRUMENT_SELECTED(current.moves[best_index].score);
    return current.moves[best_index].move;
}

  static inline Move xy2move(u8 x, u8 y) {
    u8 subboard =
        (u8)((y / 3u) * 3u + x / 3u);

    u8 local_index =
        (u8)((y % 3u) * 3u + x % 3u);

    onehot9 local_bit =
        onehot9_from_index(local_index);
    return (Move){
        .subboard = subboard,
        .local_bit = local_bit
    };
}

/*
 * Add one CodinGame-format x/y move to a full-board move collection.
 * Internal coordinates remain x = column, y = row.
 */
static void add_xy_move(
    ValidMoves *valid_moves,
    u8 x,
    u8 y)
{
    if (valid_moves == NULL)
        error("Cannot add a move to NULL ValidMoves\n");

    if (valid_moves->kind != MULTI_BOARD)
        error("Cannot add an x/y move to single-board ValidMoves\n");

    if (x > 8 || y > 8)
        error("Move coordinates outside 0..8: x=%u y=%u\n", x, y);

    u8 subboard =
        (u8)((y / 3u) * 3u + x / 3u);

    u8 local_index =
        (u8)((y % 3u) * 3u + x % 3u);

    onehot9 local_bit =
        onehot9_from_index(local_index);

    if (valid_moves->full.moves[subboard] & local_bit)
        return;

    valid_moves->full.moves[subboard] |= local_bit;
    valid_moves->full.boards |= onehot9_from_index(subboard);
    valid_moves->full.count++;
}

typedef struct XYMove_s {
    u8 x;
    u8 y;
} XYMove;

/* Convert an internal Board2 move to global 9×9 coordinates. */
static inline XYMove move2xy(Move move)
{
    ASSERT(move.subboard < 9);
    ASSERT_1SHOT9(move.local_bit);

    u8 local_index =
        (u8)__builtin_ctz((unsigned)move.local_bit);

    return (XYMove){
        .x = (u8)(
            (move.subboard % 3u) * 3u
            + local_index % 3u
        ),
        .y = (u8)(
            (move.subboard / 3u) * 3u
            + local_index / 3u
        )
    };
}

/* Tests whether an internal coordinate is present in the arena-provided moves. */
static bool isLegalMove(Move move, const ValidMoves *moves)
{
    ASSERT(moves != NULL);
    ASSERT(moves->kind == MULTI_BOARD || moves->kind == SINGLE_BOARD);
    if (moves->kind == SINGLE_BOARD) 
        return ((moves->single.subboard == move.subboard) && (moves->single.bits & move.local_bit));
    else
        return ((moves->full.moves[move.subboard] & onehot9_from_index(move.local_bit)) != 0);
}

/* Resolves one START_RULE letter to a cell index, drawing a random member of
 * the D or C class.  Caller guarantees the letter is M, C or D. */
static int startRuleIndex(char cell_class) {
    static const int diagonal[] = {0, 2, 6, 8};
    static const int cardinal[] = {1, 3, 5, 7};
    switch (cell_class) {
        case 'M': return 4;
        case 'D': return diagonal[rand() % 4];
        case 'C': return cardinal[rand() % 4];
    }
    error("Invalid START_RULE cell class '%c'\n", cell_class);
    return -1; /* Unreachable: error() terminates the process. */
}

/* Chooses the opening move from START_RULE (see the diagram beside its
 * definition): the first letter names the outer grid, the second the cell
 * inside it.  Intended only for the empty opening board, where every cell is
 * legal.  Returns internal x = column, y = row coordinates like the rest of
 * the bot. */
Move getStartMove(void) {
    if (strlen(START_RULE) != 2)
        error("START_RULE must be exactly two M/C/D letters, got \"%s\"\n", START_RULE);

    int subboard = startRuleIndex(START_RULE[0]);
    onehot9 bit = onehot9_from_index( startRuleIndex(START_RULE[1]) );

    return (Move) { .subboard = subboard, .local_bit = bit };
}

/* Applies the opponent move, chooses our legal reply, then updates the board. */
Move getMove(Board2 *board, Move last_move, ValidMoves *valid_moves)
{
    if (last_move.subboard == 0xFF) {
        board2_play(board, last_move, 1);
    }

    evaluation_calls = 0;
    turn_cache_hits = 0;
    search_nodes = 0;
    INSTRUMENT_RESET();

#ifdef LOCAL_RIG
    if (local_rig_forced.x >= 0) {
        if (!isLegalMove(local_rig_forced.x, local_rig_forced.y, valid_moves))
            error("Local rig forced an illegal move\n");
        set9(board, local_rig_forced.x, local_rig_forced.y, 0);
        return local_rig_forced;
    }
#endif

    /* First turn: there is no opponent move to answer, so play the configured
       START_RULE opening directly instead of searching. */
    if (last_move.subboard == 0xFF) {
        INSTRUMENT_MODE("opening");
        Move move = getStartMove();
        board2_play(board, move, 0);
        return move;
    }

    search_deadline = move_budget;

    uint64_t timed_start = get_gtod_clock_time();
    Move my_move = evaluateMovesShallowTimed(board, *valid_moves);  // copy - iterate is destructive
    game_search_us += get_gtod_clock_time() - timed_start;

    game_positions += evaluation_calls;
    game_cache_hits += turn_cache_hits;

    if (!isLegalMove(my_move, valid_moves)) {
        error("Selected illegal move\n");
    }

    board2_play(board, my_move, 0);
    return my_move;
}

/* Either reports the fixed build identity or runs the CodinGame game loop. */
int main(int argc,char* argv[])
{
    if (argc == 2 && strcmp(argv[1], "--HELLO") == 0) {
        puts(HELLO_TEXT LOCAL_RIG_HELLO);
        return 0;
    }
    if (argc != 1) error("Only --HELLO is supported\n");

#ifdef CG_GAME
    fprintf(stderr, "Running in CodinGame\n"); 
#endif

    Board2 p0_board = {0};

    localRigInit();

    const char *seed_text = getenv("CG_SEED");
    unsigned seed = seed_text ? (unsigned)strtoul(seed_text,NULL,10) :
        (unsigned)(get_gtod_clock_time() ^ (uint64_t)getpid());
    srand(seed);
    int turn = 0;
    // game loop
    while (1) {
        
        struct {
            int x;
            int y;
        } last_move;
        ValidMoves valid_moves = { .kind = MULTI_BOARD, .full = { 0 } };

        int i = scanf("%d%d", &last_move.y, &last_move.x);
        if (i == EOF) break;
        if (i < 2) error("Failed to read last move\n");
        
        if (!((last_move.x == -1 && last_move.y == -1) || 
        (last_move.x>=0 && last_move.x<=8 && last_move.y>=0 && last_move.y<=8))) error("Invalid opponent move\n");
        start_time = get_gtod_clock_time();
        int current_turn = ++turn;
        move_budget = current_turn == 1 ? 0.900 : MAX_TIME;
        
        int valid_action_count;
        if (scanf("%d", &valid_action_count) < 1) error("Fail to read action_count\n");
        if (valid_action_count < 1 || valid_action_count > 81) error("Invalid action count\n");
        for (int i = 0; i < valid_action_count; i++) {
            int row;
            int col;
            if ( scanf("%d%d", &row, &col) < 2) error("failed to read a move\n");
            if (row < 0 || row > 8 || col < 0 || col > 8) error("Invalid action coordinates\n");
            
            add_xy_move(&valid_moves, col, row);
        }
        if (valid_moves.full.count != valid_action_count) error("Duplicate legal actions\n");
        localRigReadTurn(current_turn);
        
        Move my_move = getMove(&p0_board, xy2move(last_move.x, last_move.y), &valid_moves);
        INSTRUMENT_WRITE(current_turn, (int)(move_budget * 1000 + 0.5),
            valid_action_count, my_move, evaluation_calls);
        localRigEndTurn();
        XYMove xym = move2xy(my_move);
        printf("%d %d\n", xym.y, xym.x);
        fflush(stdout);
    }
    if (getenv("CG_LOCAL_STATS")) {
        printf("@GAME_STATS %llu %llu %llu %llu %llu\n",
            game_search_us, game_positions, game_cache_hits,
            game_cert_leaf_hits, game_cert_internal_hits);
        fflush(stdout);
    }
       
    exit(0);
    return 0;
}
