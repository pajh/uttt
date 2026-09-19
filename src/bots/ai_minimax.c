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

#include "board.h"

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

#include "hashmap.h"
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

/* Encodes state, destination, player and horizon into a padding-free cache key. */
void searchKey(Board9 *board, u16 bit, int next_player, int depth, unsigned char key[KEY_SIZE]) {
    int offset = 0;
    for (int i=0;i<9;i++) {
        key[offset++] = board->cell[i] & 255;
        key[offset++] = board->cell[i] >> 8;
    }
    key[offset++] = board->overall & 255;
    key[offset++] = board->overall >> 8;
    key[offset++] = board->overall_free & 255;
    key[offset++] = board->overall_free >> 8;
    key[offset++] = (board->overall_free & bit) ? 9 : __builtin_ctz(bit);
    key[offset++] = next_player;
    key[offset++] = depth & 255;
    key[offset++] = (unsigned)depth >> 8;
}

/* Samples the clock every 64 nodes to amortise timing overhead. */
int searchExpired(void) {
    return ((search_nodes++ & 63) == 0) && getElaspedTime() >= search_deadline;
}

/* Returns a bit per player when that player still has a possible master line. */
static __attribute__((unused)) unsigned possibleMasterLines(Board9 board) {
    const u16 lines[8] = {7,56,448,73,146,292,273,84};
    Board3 owned = B3(board.overall);
    unsigned possible = 0;
    for (int player=0;player<2;player++) {
        u16 blocked = board.overall_free & ~owned.p[player];
        for (int i=0;i<8;i++) if (!(lines[i] & blocked)) {
            possible |= 1u << player;
            break;
        }
    }
    return possible;
}

/* Proves a count-based winner when remaining boards cannot change the lead. */
static __attribute__((unused)) int countProof(Board9 board, unsigned possible) {
    Board3 owned = B3(board.overall);
    int lead = POPCNT(owned.p[0]) - POPCNT(owned.p[1]);
    int remaining = 9 - POPCNT(board.overall_free);
    if (lead > remaining && !(possible & 2)) return 0;
    if (-lead > remaining && !(possible & 1)) return 1;
    return -1;
}

/* Current-state master proof.  Local cannot_claim bits tighten the pessimistic
 * opponent line/count bound; terminal winner/draw handling remains explicit
 * in evaluateShallow below.  countProof/possibleMasterLines remain lower-bound
 * reference helpers for regression comparisons. */
static inline int currentMasterCertificate(const Board9 *board) {
    if (masterCertificateClaimability(board, 0)) return 0;
    if (masterCertificateClaimability(board, 1)) return 1;
    return -1;
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
static u16 f1_cache[POSS_BOARDS][2];
static const u16 scoring_lines[8] = {7,56,448,73,146,292,273,84};

/* Converts a two-player score to p0/p1's bounded relative advantage. */
static int scoreForPlayer(Score score, int player) {
    if (score.p0 == TERMINAL_SCORE && score.p1 == 0)
        return player == 0 ? TERMINAL_SCORE : -TERMINAL_SCORE;
    if (score.p1 == TERMINAL_SCORE && score.p0 == 0)
        return player == 1 ? TERMINAL_SCORE : -TERMINAL_SCORE;
    int mine = player == 0 ? score.p0 : score.p1;
    int theirs = player == 0 ? score.p1 : score.p0;
    return RELATIVE_SCORE_SCALE * (mine - theirs) / (mine + theirs + 2);
}

/* Grid potential: distinct winning cells plus unblocked one-mark lines.
   closed includes unavailable squares (especially drawn squares on U). */
static int f1_raw(u16 grid, u16 closed, int player) {
    Evaluation ev = evalMAC2(grid,closed);
    if (ev.p3[0] || ev.p3[1]) return 0;
    int wins = POPCNT(player ? ev.p1_winners : ev.p0_winners);
    return (wins == 0 ? 0 : wins == 1 ? 4 : 6) + ev.p1[player];
}
/* Precomputes ordinary small-board f1 values for both players. */
static void initScoringCache(void) {
    for(int grid=0;grid<POSS_BOARDS;grid++)
        for(int player=0;player<2;player++) f1_cache[grid][player]=f1_raw(grid,0,player);
}
/* Returns cached ordinary-grid strength or evaluates a closed-grid context. */
static int f1(u16 grid, u16 closed, int player) {
    return closed ? f1_raw(grid,closed,player) : f1_cache[grid][player];
}

/* Ownership already secured; raw count, with its weight in scoreBoard. */
static int fc(u16 grid, int player) {
    return POPCNT(B3(grid).p[player]);
}

/* Small-board relevance: base count value plus surviving master lines.
   A line with 0/1/2 owned squares contributes 1/2/4 respectively. */
static int f2(u16 grid, u16 closed, int cellid, int player) {
    u16 bit=1u<<cellid;
    if(closed & bit) return 0;
    Board3 owned=B3(grid);
    u16 blocked=(closed | owned.p[1-player]) & ~owned.p[player];
    int relevance=1;
    for(int i=0;i<8;i++) if((scoring_lines[i]&bit) && !(scoring_lines[i]&blocked)) {
        int count=POPCNT(scoring_lines[i]&owned.p[player]);
        relevance += 1 << count;
    }
    return relevance;
}

/* Counts distinct open master cells that complete a line and whose local
   board still has a line free of opponent marks. Routing and future defence
   are left to search; this is only a leaf-level possibility, not a proof. */
static int liveMasterWinningCells(Board9 board, int player) {
    Board3 owners=B3(board.overall);
    u16 own=owners.p[player];
    u16 unavailable=board.overall_free;
    u16 threats=0;
    for(int i=0;i<8;i++) {
        u16 line=scoring_lines[i];
        if(POPCNT(line & own)==2 && POPCNT(line & unavailable)==2)
            threats |= line & ~unavailable;
    }
    int count=0;
    for(int i=0;i<9;i++) if(threats & (1u<<i)) {
        u16 opponent=B3(board.cell[i]).p[1-player];
        for(int j=0;j<8;j++) if(!(scoring_lines[j] & opponent)) {
            count++;
            break;
        }
    }
    return count;
}

/* Scores a non-terminal position without searching beyond the current leaf. */
Score scoreBoard(Board9 board, u16 last_cell, u16 last_bit) {
    (void)last_cell; (void)last_bit;
    ASSERT(board.winner == -1,"Scoring a won board");
    evaluation_calls++;
    int strength[2];
    for(int player=0;player<2;player++) {
        int main_strength = f1(board.overall,board.overall_free,player);
        /* MM-004: secured boards always count; a live master-winning cell
           must outweigh the local potential discarded when a board closes. */
        strength[player] = main_strength*uscale;
        strength[player] += (int)(fc(board.overall,player)*COUNT_UNIT*count_scale+0.5);
        strength[player] += LEVER_MASTER_THREAT*liveMasterWinningCells(board,player);
        for(int i=0;i<9;i++) if(!(board.overall_free & (1u<<i)))
            strength[player] += f1(board.cell[i],0,player)
                * f2(board.overall,board.overall_free,i,player);
        if(strength[player]>10000) strength[player]=10000;
    }
    return (Score){strength[0],strength[1]};
}

/* At a depth-zero leaf, prove an immediate master win available to the side
 * to move.  last_bit selects the next local board unless that board is
 * already closed, in which case the rules allow any open local board. */
static int certifiedImmediateMasterWin(Board9 board, int player, u16 last_bit) {
    int target = __builtin_ctz(last_bit);
    u16 candidate_boards = 0;
    if (board.overall_free & (1u << target)) {
        candidate_boards = (u16)(~board.overall_free) & 0x1FF;
    } else {
        candidate_boards = (u16)(1u << target);
    }
    for (int cell = 0; cell < 9; cell++) {
        u16 cell_bit = (u16)(1u << cell);
        if (!(candidate_boards & cell_bit) || (board.overall_free & cell_bit))
            continue;
        Evaluation ev = ev_cache[board.cell[cell]];
        u16 winning = player ? ev.p1_winners : ev.p0_winners;
        if (winning && masterCertificateAfterClaim(&board, (unsigned)cell, player))
            return 1;
    }
    return 0;
}

/* Evaluates a bounded minimax subtree and propagates terminal or timeout values. */
Score evaluateShallow(Board9 board, int player, u16 cell, u16 bit, int depth, int timed) {
    if (timed && searchExpired()) return (Score){TIMEOUT_SCORE,TIMEOUT_SCORE};
    u16 proof_overall_free = board.overall_free;
    u16 proof_cannot_claim0 = board.cannot_claim[0];
    u16 proof_cannot_claim1 = board.cannot_claim[1];
    set9CB(&board, cell, bit, player);
    if (board.winner == 2) return (Score){0,0};
    if (board.overall_free != proof_overall_free ||
        board.cannot_claim[0] != proof_cannot_claim0 ||
        board.cannot_claim[1] != proof_cannot_claim1) {
        int proved = currentMasterCertificate(&board);
        if (proved >= 0)
            return proved == 0 ? (Score){TERMINAL_SCORE,0} : (Score){0,TERMINAL_SCORE};
    }
    unsigned char key[KEY_SIZE];
    searchKey(&board, bit, 1-player, depth, key);
    u32 data;
    if (SEARCH_CACHE_FIND(map,key,&data)) return (Score){data & 65535, data >> 16};
    Score best;
    int next_player = 1 - player;
    /* A certified immediate master win is terminal for the side to move,
       regardless of remaining search depth.  Keep this after the cache probe
       so cached values retain their existing identity and leaf nodes do not
       duplicate the certificate check. */
    if (certifiedImmediateMasterWin(board, next_player, bit)) {
        if (depth == 0) game_cert_leaf_hits++;
        else game_cert_internal_hits++;
        best = next_player == 0 ? (Score){TERMINAL_SCORE,0} : (Score){0,TERMINAL_SCORE};
        addHMEntry(map,key,((u32)best.p1 << 16) | best.p0);
        return best;
    }
    if (depth == 0) {
        best = scoreBoard(board,cell,bit);
    } else {
        Moves2 moves;
        int target = __builtin_ctz(bit);
        validMoves2(&board, &moves, target%3, target/3);
        int best_score = -1000000;
        best = (Score){0,0};
        u16 next_cell, next_bit;
        while (moveNextCB(&moves,&next_cell,&next_bit)) {
            Score score = evaluateShallow(board,1-player,next_cell,next_bit,depth-1,timed);
            if (score.p0 == TIMEOUT_SCORE) return score;
            int value = scoreForPlayer(score,1-player);
            if (value > best_score) { best_score = value; best = score; }
            if (best_score == TERMINAL_SCORE) break;
        }
    }
    addHMEntry(map,key,((u32)best.p1 << 16) | best.p0);
    return best;
}

enum RootProof {
    RootUnproved,
    RootForcedWin,
    RootForcedLoss
};

typedef struct RootMove_s {
    Pos move;
    int score;
    int evaluated_plies;
    enum RootProof proof;
} RootMove;

typedef struct RootMoves_s {
    RootMove moves[81];
    int count;
} RootMoves;

/* Iteratively deepens all viable root moves until the per-turn deadline. */
Pos evaluateMovesShallowTimed(Board9 *board, Moves2 *valid_moves) {
    RootMoves roots = {0};
    resetMoveIt(valid_moves);
    Pos move;
    while (moveNext(valid_moves,&move)) {
        roots.moves[roots.count].move = move;
        roots.moves[roots.count].proof = RootUnproved;
        roots.count++;
    }

    /* Four plies is the safety floor: current timing evidence says it always
       completes, while the old two-ply pass only consumed time.  There is no
       arbitrary depth ceiling.  Keep adding two plies until the clock stops
       us; each completed root immediately replaces its shallower score, so
       the final choice deliberately merges scores from different depths. */
    int time_expired = 0;
    for (int target_plies=4; !time_expired; target_plies+=2) {
        clearHM(map);

        int searchable_roots = 0;
        for (int i=0;i<roots.count;i++)
            searchable_roots += roots.moves[i].proof != RootForcedLoss;
        if (searchable_roots == 0) break;

        for (int i=0;i<roots.count;i++) {
            RootMove *root = &roots.moves[i];

            if (root->proof == RootForcedLoss) continue;
            if (getElaspedTime() >= search_deadline) {
                time_expired = 1;
                break;
            }

            u16 cell, bit;
            pos2cell(root->move,&cell,&bit);
            Score score = evaluateShallow(
                *board,0,cell,bit,target_plies-1,1
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

            if (score.p1 == TERMINAL_SCORE && score.p0 == 0) {
                root->proof = RootForcedLoss;
                root->score = -TERMINAL_SCORE;
                root->evaluated_plies = target_plies;
                continue;
            }

            root->score = scoreForPlayer(score,0);
            root->evaluated_plies = target_plies;
        }
    }

    int best_score = -TERMINAL_SCORE-1;
    Moves best_moves = {0};
    for (int i=0;i<roots.count;i++) {
        /* Normally every root has a four-ply score.  If exceptionally little
           time was available, do not let an unsearched zero beat a searched
           negative score merely because RootMoves was zero-initialised. */
        if (roots.moves[i].evaluated_plies == 0) continue;
        if (roots.moves[i].score > best_score) {
            best_score = roots.moves[i].score;
            best_moves.count = 0;
        }
        if (roots.moves[i].score == best_score)
            push(&best_moves,roots.moves[i].move);
    }
    if (best_moves.count == 0)
        return roots.moves[rand() % roots.count].move;
    INSTRUMENT_SELECTED(best_score);
    return best_moves.moves[rand() % best_moves.count];
}

/* Tests whether an internal coordinate is present in the arena-provided moves. */
static int isLegalMove(int x, int y, Moves2 *moves)
{
    if (x < 0 || x > 8 || y < 0 || y > 8) return 0;

    int cell = (y / 3) * 3 + x / 3;
    int bit = (y % 3) * 3 + x % 3;
    return !!(moves->mask[cell] & (1u << bit));
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
Pos getStartMove(void) {
    if (strlen(START_RULE) != 2)
        error("START_RULE must be exactly two M/C/D letters, got \"%s\"\n", START_RULE);

    int outer = startRuleIndex(START_RULE[0]);
    int inner = startRuleIndex(START_RULE[1]);

    /* outer is the small board index; inner is the cell within that board. */
    Pos move = { (outer % 3) * 3 + inner % 3, (outer / 3) * 3 + inner / 3 };
    return move;
}

/* Applies the opponent move, chooses our legal reply, then updates the board. */
Pos getMove(Board9 *board, Pos last_move, Moves2 *valid_moves)
{
    if (last_move.x != -1) {
        set9(board, last_move.x, last_move.y, 1);
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
    if (last_move.x == -1) {
        INSTRUMENT_MODE("opening");
        Pos move = getStartMove();
        if (!isLegalMove(move.x, move.y, valid_moves))
            error("START_RULE produced an illegal opening move\n");
        set9(board, move.x, move.y, 0);
        return move;
    }

    search_deadline = move_budget;

    uint64_t timed_start = get_gtod_clock_time();
    Pos my_move = evaluateMovesShallowTimed(board, valid_moves);
    game_search_us += get_gtod_clock_time() - timed_start;

    game_positions += evaluation_calls;
    game_cache_hits += turn_cache_hits;

    if (!isLegalMove(my_move.x, my_move.y, valid_moves)) {
        error("Selected illegal move\n");
    }

    set9(board, my_move.x, my_move.y, 0);
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

    initBoardCaches();
    initScoringCache();

    Board9 p0_board = {0};
    //memset(&p0_board, 0, sizeof(p0_board)); 
    p0_board.winner = -1;
    map = createHM(128000,512000);
    localRigInit();

    const char *seed_text = getenv("CG_SEED");
    unsigned seed = seed_text ? (unsigned)strtoul(seed_text,NULL,10) :
        (unsigned)(get_gtod_clock_time() ^ (uint64_t)getpid());
    srand(seed);
    int turn = 0;
    // game loop
    while (1) {
        Pos last_move;
        Moves2 valid_moves = {0};

        int i = scanf("%d%d", &last_move.y, &last_move.x);
        if (i == EOF) break;
        if (i < 2) error("Failed to read last move\n");
        
        if (last_move.x == -2) break;  // Internal local-rig shutdown.
        if (!((last_move.x == -1 && last_move.y == -1) || (last_move.x>=0 && last_move.x<=8 && last_move.y>=0 && last_move.y<=8))) error("Invalid opponent move\n");
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
            Pos vm = {col,row};
            pushMove(&valid_moves, vm);
        }
        if (valid_moves.count != valid_action_count) error("Duplicate legal actions\n");
        localRigReadTurn(current_turn);
        
        Pos my_move = getMove(&p0_board, last_move, &valid_moves);
        INSTRUMENT_WRITE(current_turn, (int)(move_budget * 1000 + 0.5),
            valid_action_count, my_move, evaluation_calls);
        localRigEndTurn();
        printf("%d %d\n", my_move.y, my_move.x);
        fflush(stdout);
    }
    if (getenv("CG_LOCAL_STATS")) {
        printf("@GAME_STATS %llu %llu %llu %llu %llu\n",
            game_search_us, game_positions, game_cache_hits,
            game_cert_leaf_hits, game_cert_internal_hits);
        fflush(stdout);
    }
    destroyHM(map);    
    exit(0);
    return 0;
}
