/*
 * Current Ultimate Tic-Tac-Toe competition bot.
 *
 * The main loop reconstructs the board from CodinGame's stdin protocol.  Each
 * turn uses iterative shallow minimax until its deadline, with a separate
 * exact solver in compact late-game positions.  board.h, hashmap.h and the
 * optional local_rig.h are implementation headers so `subst` can create the
 * single C file required for submission.
 *
 * Internal coordinates are x = column, y = row; CodinGame I/O is row column.
 */
/* Local debug/optimized builds are selected explicitly by the makefile.
 * TODO: restore suitable GCC optimization/target pragmas before producing a
 * real CodinGame submission, whose compiler flags are outside our control. */

/* ================================ LEVERS ================================
 * Change and record these values for an experiment.  The bot accepts no
 * behaviour-changing command-line options: a build has one clear identity.
 * HELLO_TEXT is the small, human-readable version reported by `--HELLO`.
 */
#define HELLO_TEXT "NM-004-R1" /* N = negamax, 004 = experiment counter, R = ratio score. */

#define _POSIX_C_SOURCE 200809L

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

#define MAX_TIME 0.0900
#define TERMINAL_SCORE 60000
#define TIMEOUT_SCORE 65535
#define RELATIVE_SCORE_SCALE 10000
#ifndef EVALUATION_TIMEOUT
#define EVALUATION_TIMEOUT 0
#endif
#ifndef MAX_SCORE
#define MAX_SCORE 0
#endif
#ifndef NEGAMAX_DEBUG
#define NEGAMAX_DEBUG 0
#endif
#ifndef NEGAMAX_LOCAL
#define NEGAMAX_LOCAL 0
#endif
#ifndef NEGAMAX_ASSERTS
#define NEGAMAX_ASSERTS 0
#endif
#ifndef NEGAMAX_EVAL
#define NEGAMAX_EVAL 0
#endif

/* Build flags appended to --HELLO (D debug, L local rig, A asserts, E eval
 * timeout) so a log or report shows how the bot was compiled.  The makefile
 * sets NEGAMAX_DEBUG/LOCAL/ASSERTS/EVAL; they default to 0. */
#define NEGAMAX_STR_1(x) #x
#define NEGAMAX_STR(x) NEGAMAX_STR_1(x)
#define HELLO_BUILD_FLAGS "-D" NEGAMAX_STR(NEGAMAX_DEBUG) \
    "L" NEGAMAX_STR(NEGAMAX_LOCAL) \
    "A" NEGAMAX_STR(NEGAMAX_ASSERTS) \
    "E" NEGAMAX_STR(NEGAMAX_EVAL)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h> 
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <search.h>

#include "../engine/board2.h"
#include "experiment.h"
#include "../engine/support.h"


typedef struct SearchResult_s {
    int value;
    bool forced_draw;
    bool timeout;
} SearchResult;

typedef struct SearchConfig_s {
    u8 ply;
    u8 stop_at_ply;
    bool timed;
    bool force_cert_check;  
} SearchConfig;

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


/* Game totals are reported only to the local rig, after stdin closes. */
static unsigned long long game_search_us, game_positions, game_cache_hits;
static unsigned long long game_cert_leaf_hits, game_cert_internal_hits;
static unsigned long turn_cache_hits;

void error(const char *format, ...);

// Globals

static unsigned long evaluation_calls;
static bool evaluation_timeout = EVALUATION_TIMEOUT;
static unsigned long max_score = MAX_SCORE;

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

/* Seed argument failures exit with status 2, matching ai_random and orig. */
static void seedError(const char *message)
{
    fprintf(stderr, "%s", message);
    exit(2);
}

/* Samples the selected search limit every 64 checks. */
static bool HAVE_WE_TIMED_OUT(void) {
    if ((search_nodes++ & 63) != 0) return false;
    if (evaluation_timeout) return evaluation_calls >= max_score;
    return getElaspedTime() >= search_deadline;
}

/* HashMap support section*/

#define TT_BITS 16
#define TT_SIZE (1u << TT_BITS)
#define TT_MASK (TT_SIZE - 1)

#define TT_FORCED_DRAW 0x01
#define TT_MIN_PLY 2      // don't cache too near the root
#define TT_MIN_TO_GO 2     // don't cache when too close to the leaf#

typedef struct {
    u64 hash;
    i32 value;
    u16 generation;
    u8 flags;
    u8 pad;
} TTEntry;

static TTEntry tt[TT_SIZE];
static u16 tt_generation = 0;

typedef struct {
    u32 size;
    u32 pid;
    u32 tt_min_ply;
    u32 tt_min_to_go;
    u64 tt_probes_by_depth[8]; /* EDATA_DELTA */
    u64 tt_hits_by_depth[8]; /* EDATA_DELTA */
    u64 tt_stores; /* EDATA_DELTA */
    u64 tt_collisions; /* EDATA_DELTA */
    u64 evaluations;
    u64 peek_probes;
    u64 peek_hits;
    u64 evals_at_last_completed;
    u16 attempted_ply;
    u16 completed_ply;
} EData;

static u8 current_depth = 0;
static EData edata;

/* Number of elements in a fixed-size array member of edata.  The declaration is
 * the only place a metric's extent is written. */
#define EDATA_LEN(var) (sizeof edata.var / sizeof edata.var[0])

/* Folds any position at or past the last element into that last element. */
static inline size_t edata_index(size_t pos, size_t last)
{
    return pos < last ? pos : last;
}

/* Record one increment of an array-valued metric at position pos.  `var` is the
 * member name only and must be a fixed-size array, not a pointer; resizing the
 * array never touches a call site. */
#define EDATA_LOG(var, pos) \
    (edata.var[edata_index((size_t)(pos), EDATA_LEN(var) - 1)]++)

#ifndef CG_GAME
/* NEGAMAX_RESULTS names the binary experiment log.  It is captured once at
 * startup; a NULL path is the single flag that disables all experiment output
 * (and skips the getpid call).  A single write() to an O_APPEND fd keeps
 * concurrent bot processes' records intact; the harness must create the file
 * beforehand, as the bot carries no file-lifecycle code.  The summary tooling
 * reads fixed-size EData records and derives per-turn diffs from the
 * cumulative counters.  CG_GAME submission builds compile this away. */
static const char *results_path;

static void resultsLogStartup(void)
{
    results_path = getenv("NEGAMAX_RESULTS");
    if (results_path == NULL || *results_path == '\0') {
        results_path = NULL;
        return;
    }
    edata.size = (u32)sizeof edata;
    edata.pid = (u32)getpid();
    edata.tt_min_ply = TT_MIN_PLY;
    edata.tt_min_to_go = TT_MIN_TO_GO;
}

/* Append this turn's cumulative tt counters as one raw EData record. */
static void logData(void)
{
    if (results_path == NULL) return;

    int fd = open(results_path, O_WRONLY | O_APPEND);
    if (fd < 0) return;

    // Anything that needs GATHERING goes here

    edata.evaluations = evaluation_calls;
    ssize_t written = write(fd, &edata, sizeof edata);
    if (written != (ssize_t)sizeof edata) {
        fprintf(stderr, "logData: short write %zd/%zu bytes\n",
                written, sizeof edata);
    }
    close(fd);
}
#else
#define resultsLogStartup() ((void)0)
#define logData()           ((void)0)
#endif


static inline u64 hash_mix(u64 a, u64 b)
{
    __uint128_t r = (__uint128_t)a * (__uint128_t)b;
    return (u64)r ^ (u64)(r >> 64);
}

static inline u64 board2_hash(const Board2 *board)
{
    static const u64 C0 = UINT64_C(0xa0761d6478bd642f);
    static const u64 C1 = UINT64_C(0xe7037ed1a0b428db);
    static const u64 C2 = UINT64_C(0x8ebc6af09c88c6e3);
    static const u64 C3 = UINT64_C(0x589965cc75374cc3);
    static const u64 C4 = UINT64_C(0x9e3779b97f4a7c15);
    static const u64 C5 = UINT64_C(0xd6e8feb86659fd93);

    u64 w[5];

    memcpy(w, board, 40);

    u64 a = hash_mix(w[0] ^ C0, w[1] ^ C1);
    u64 b = hash_mix(w[2] ^ C2, w[3] ^ C3);
    u64 c = hash_mix(w[4] ^ C4, C5);

    return hash_mix(a ^ b ^ C4, c ^ C0);
}

static inline void tt_store(u64 hash, SearchResult result)
{
    TTEntry *entry = &tt[hash & TT_MASK];

    if (entry->generation == tt_generation &&
        entry->hash != hash)
        edata.tt_collisions++;

    entry->hash = hash;
    entry->value = result.value;
    entry->flags = result.forced_draw ? TT_FORCED_DRAW : 0;
    entry->generation = tt_generation;

    edata.tt_stores++;
}

static inline bool tt_find(u64 hash, SearchResult *result)
{
    TTEntry *entry = &tt[hash & TT_MASK];

    EDATA_LOG(tt_probes_by_depth, current_depth);

    if (entry->generation != tt_generation)
        return false;

    if (entry->hash != hash)
        return false;

    EDATA_LOG(tt_hits_by_depth, current_depth);

    *result = (SearchResult){
        .value = entry->value,
        .forced_draw = (entry->flags & TT_FORCED_DRAW) != 0,
        .timeout = false
    };

    return true;
}


// End of HashMap support section

typedef struct Score_s {
    u16 p0;
    u16 p1;
} Score;

#include "local_rig.h"

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
    return RELATIVE_SCORE_SCALE * (mine - theirs) / (mine + theirs + 2000);
}

/* Score a nonterminal position without searching beyond this leaf. */
static Score score_board(const Board2 *board)
{
    ASSERT(board != NULL);
    ASSERT(board->winner == BOARD2_IN_PROGRESS);

    evaluation_calls++;

    ExperBoardScore channels = score_board_exper(board);

    return (Score){
        (u16)((channels.three_iar[0] + channels.count[0]) * 1000.0 + 0.5),
        (u16)((channels.three_iar[1] + channels.count[1]) * 1000.0 + 0.5)
    };
}

static bool has_certified_immediate_win(const Board2 *board)
{
    ASSERT(board != NULL);
    ASSERT(board->winner == BOARD2_IN_PROGRESS);
    u8 player = (u8)board->next_player;
    ASSERT(player < 2);

    ValidMoves moves = valid_moves(board);
    Move move;

    while (next_move(&moves, &move)) {
        mask9 local_wins = winning_cells_simd(
            board->marks[player][move.subboard],
            board->marks[opponent(player)][move.subboard]
        );

        if (!(local_wins & move.local_bit))
            continue;

        Board2 after = *board;
        /* This probe only checks the winner/certificate, so no score cache is needed. */
        board2_play(&after, move);

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



/* * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * * * * * * * 
                                 N E G A M A X 
Score the already-constructed position from the side-to-move perspective. 

* * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * * * * * * **/
static SearchResult negamax(const Board2 *board, SearchConfig config) {
    current_depth = config.ply -1;
    if (config.timed && HAVE_WE_TIMED_OUT())
        return (SearchResult){.timeout=true};

    if (board->winner != BOARD2_IN_PROGRESS) {
        if (board->winner == BOARD2_DRAW)
            return (SearchResult){.value=0,.forced_draw=true};
        return (SearchResult){
            .value=board->winner == board->next_player + 1u
                ? TERMINAL_SCORE : -TERMINAL_SCORE
        };
    }
    u8 player_to_move = (u8)board->next_player;
    if (config.force_cert_check) {
        Score certified;
        if (certifiedScore(board, &certified)) {
            if (certified.p0 == TERMINAL_SCORE && certified.p1 == TERMINAL_SCORE)
                return (SearchResult){.value=0,.forced_draw=true};
            return (SearchResult){
                .value=scoreForPlayer(certified, player_to_move)
            };
        }
    }

    if (config.ply == config.stop_at_ply) {
        /* Selectively extend the horizon by one move only for an exact immediate win. */
        edata.peek_probes++;
        if (has_certified_immediate_win(board)) {
            edata.peek_hits++;
            return (SearchResult){.value=TERMINAL_SCORE};
        }
        return (SearchResult){
            .value=scoreForPlayer(score_board(board), player_to_move)
        };
    }
    int to_go = config.stop_at_ply - config.ply;
    bool use_tt = config.ply >= TT_MIN_PLY && to_go >= TT_MIN_TO_GO;
    u64 hash = 0;
    if (use_tt) {
        // Is this position already in the hash table?
        hash = board2_hash(board);

        SearchResult cached;
        if (tt_find(hash, &cached))
            return cached;
    }
    ValidMoves moves = valid_moves(board);
 
    int best_score = -1000000;
    bool forced_draw = true;

    for (Move move; next_move(&moves, &move);) {
        Board2 child = *board;
        bool proof_changed = board2_play_exper(&child, move);
        SearchResult child_result = negamax(
            &child,
            (SearchConfig){.ply = config.ply + 1,
                           .stop_at_ply = config.stop_at_ply,
                           .timed = config.timed,
                           .force_cert_check = proof_changed});
        if (child_result.timeout) return child_result;

        int value = -child_result.value;
        if (value > best_score ) {
            best_score = value;
        }

        if (best_score == TERMINAL_SCORE) break;     
        if (!child_result.forced_draw && value != -TERMINAL_SCORE) forced_draw = false;
    } 
    SearchResult result = (SearchResult){.value=best_score,.forced_draw=forced_draw && best_score == 0};
    if (use_tt) tt_store(hash, result);
    return result;
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

typedef struct XYMove_s {
    u8 x;
    u8 y;
} XYMove;

/* Convert an internal Board2 move to global 9x9 coordinates. */
static inline XYMove move2xy(Move move);

/* Records every root move and its final score for the instrumented move line. */
#ifdef LOCAL_RIG
static void captureCandidates(const RootMoves *roots) {
    if (!localRigInstrumenting()) return;
    localRigBeginCandidates();
    for (int i = 0; i < roots->count; i++) {
        XYMove xy = move2xy(roots->moves[i].move);
        localRigAddCandidate(xy.y, xy.x, roots->moves[i].score);
    }
}
#else
#define captureCandidates(roots) ((void)(roots))
#endif

/* Iteratively deepens all viable root moves until the per-turn deadline. */
Move evaluateMovesShallowTimed(Board2 *board, ValidMoves valid_moves) {
    RootMoves current = {0};
    RootMoves previous = {0};
    
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
    edata.evals_at_last_completed = 0;
    for (int target_plies=4; !time_expired; target_plies+=1) {

        int searchable_roots = 0;
        tt_generation++;
        edata.attempted_ply = target_plies;
        
        for (int i=0;i<current.count;i++)
            searchable_roots += current.moves[i].proof == RootUnproved;
        if (searchable_roots == 0) break;

        for (int i=0;i<current.count;i++) {
            RootMove *root = &current.moves[i];

            // On deeper plies - if the move is a proven draw or loss - don't wasted time on it
            if (root->proof != RootUnproved) continue;
            if (HAVE_WE_TIMED_OUT()) {
                time_expired = 1;
                break;
            }

            Board2 child = *board;
            board2_play_exper(&child, root->move);
            SearchResult result = negamax(
                &child,
                (SearchConfig){.ply=1, .stop_at_ply=target_plies, .timed=true,
                               .force_cert_check=true});
            if (result.timeout) {
                time_expired = 1;
                break;
            }
            int score = -result.value;

            if (score == TERMINAL_SCORE) {
                root->proof = RootForcedWin;
                root->score = TERMINAL_SCORE;
                root->evaluated_plies = target_plies;
                captureCandidates(&current);
                return root->move;
            }

            if (score == -TERMINAL_SCORE) {
                root->proof = RootForcedLoss;
                root->score = -TERMINAL_SCORE;
                root->evaluated_plies = target_plies;
                continue;
            }
            
            if (result.forced_draw) {
                root->proof = RootForcedDraw;
                root->score = 0;
                root->evaluated_plies = target_plies;
                continue;
            }

            root->score = score;
            root->evaluated_plies = target_plies;
        }

        if (time_expired) break;

        /* Publish only whole-depth results. Both buffers keep the same root
           index so a timed-out iteration can be reconciled without remapping. */
        previous = current;
        completed_depth = true;
        edata.completed_ply = target_plies;
        edata.evals_at_last_completed = evaluation_calls;
        
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
        captureCandidates(&current);
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
    captureCandidates(&current);
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
        return ((moves->full.moves[move.subboard] & move.local_bit) != 0);
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
    if (last_move.subboard != 0xFF)
        board2_play_exper(board, last_move);

    evaluation_calls = 0;
    edata.peek_probes = 0;
    edata.peek_hits = 0;
    turn_cache_hits = 0;
    search_nodes = 0;

    if (localRigForcedX() >= 0) {
        Move forced = xy2move((u8)localRigForcedX(), (u8)localRigForcedY());
        if (!isLegalMove(forced, valid_moves))
            error("Local rig forced an illegal move\n");
        board2_play_exper(board, forced);
        return forced;
    }

    /* First turn: there is no opponent move to answer, so play the configured
       START_RULE opening directly instead of searching. */
    if (last_move.subboard == 0xFF) {
        Move move = getStartMove();
        board2_play_exper(board, move);
        return move;
    }

    search_deadline = move_budget;

    uint64_t timed_start = get_gtod_clock_time();
    Move my_move = evaluateMovesShallowTimed(board, *valid_moves);  // copy - iterate is destructive
    game_search_us += get_gtod_clock_time() - timed_start;

    game_positions += evaluation_calls;
    game_cache_hits += turn_cache_hits;
    logData();

    if (!isLegalMove(my_move, valid_moves)) {
        error("Selected illegal move\n");
    }

    board2_play_exper(board, my_move);
    return my_move;
}

/* Reads exactly n integers from stdin, one line at a time.  Local-rig control
   tokens such as [I] are stripped and applied before the line is parsed. */
static int readInts(int *out, int n) {
    char line[512];
    int got = 0;
    while (got < n) {
        if (!fgets(line, sizeof(line), stdin)) return got;
        localRigStripCommands(line);
        char *p = line;
        while (got < n) {
            int value, used;
            if (sscanf(p, " %d%n", &value, &used) != 1) break;
            out[got++] = value;
            p += used;
        }
    }
    return got;
}

/* Either reports the fixed build identity or runs the CodinGame game loop. */
int main(int argc,char* argv[])
{
    if (argc >= 2 && strcmp(argv[1], "--HELLO") == 0) {
        if (argc != 2) { fprintf(stderr, "--HELLO must be the only argument\n"); return 2; }
        puts(HELLO_TEXT HELLO_BUILD_FLAGS);
        return 0;
    }
    unsigned seed = 0;
    if (argc == 3 && strcmp(argv[1], "--seed") == 0) {
        const char *p = argv[2];
        if (!*p) seedError("bad seed\n");
        seed = 0;
        for (; *p; p++) {
            if (*p < '0' || *p > '9') seedError("bad seed\n");
            unsigned digit = (unsigned)(*p - '0');
            if (seed > (UINT_MAX - digit) / 10u) seedError("bad seed\n");
            seed = seed * 10u + digit;
        }
    } else {
        seedError("missing seed\n");
    }

    resultsLogStartup();

#ifdef CG_GAME
    fprintf(stderr, "Running in CodinGame\n"); 
#endif

    Board2 p0_board = board2_initial();

    srand(seed);
    int turn = 0;
    // game loop
    while (1) {
        int last_move[2];
        ValidMoves valid_moves = { .kind = MULTI_BOARD, .full = { 0 } };

        localRigBeginTurn();
        int got = readInts(last_move, 2);
        if (got == 0) break;
        if (got < 2) error("Failed to read last move\n");
        int last_x = last_move[1];
        int last_y = last_move[0];

        if (!((last_x == -1 && last_y == -1) ||
        (last_x>=0 && last_x<=8 && last_y>=0 && last_y<=8))) error("Invalid opponent move\n");
        start_time = get_gtod_clock_time();
        int current_turn = ++turn;
        move_budget = current_turn == 1 ? 0.900 : MAX_TIME;

        int valid_action_count;
        if (readInts(&valid_action_count, 1) < 1) error("Fail to read action_count\n");
        if (valid_action_count < 1 || valid_action_count > 81) error("Invalid action count\n");
        for (int i = 0; i < valid_action_count; i++) {
            int rc[2];
            if (readInts(rc, 2) < 2) error("failed to read a move\n");
            int row = rc[0];
            int col = rc[1];
            if (row < 0 || row > 8 || col < 0 || col > 8) error("Invalid action coordinates\n");

            add_xy_move(&valid_moves, col, row);
        }
        if (valid_moves.full.count != valid_action_count) error("Duplicate legal actions\n");

        Move opponent_move = last_x == -1
            ? (Move){.subboard=0xFF,.local_bit=1}
            : xy2move((u8)last_x, (u8)last_y);
        Move my_move = getMove(&p0_board, opponent_move, &valid_moves);
        XYMove xym = move2xy(my_move);
        printf("%d %d", xym.y, xym.x);
        localRigPrintCandidates();
        putchar('\n');
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
