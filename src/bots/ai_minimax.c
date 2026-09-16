/*
 * Current competition bot.  The turn loop maintains a Board9 from the arena
 * protocol, then chooses through timed shallow minimax and (late-game) exact
 * search.  board.h and hashmap.h are deliberately included as implementation
 * headers so `subst` can make a standalone submission from this file.
 */
#pragma GCC optimize "O3,omit-frame-pointer,inline"
#pragma GCC target("lzcnt,popcnt")

#ifndef OPENING_SEARCH_THRESHOLD
#define OPENING_SEARCH_THRESHOLD 72
#endif

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
uint64_t get_gtod_clock_time ()
{
    struct timespec tv;
    if (clock_gettime(CLOCK_MONOTONIC, &tv) != 0) { perror("clock_gettime"); exit(1); }
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_nsec / 1000;
}

double getElaspedTime() {
    uint64_t elasped = get_gtod_clock_time() - start_time;
    return ( (double)elasped ) / 1000000.0;
}

#include "hashmap.h"

// Globals
HashMap* map;
int spaces_left;
uint32_t mm_score_count = 0;
static unsigned long evaluation_calls, count_differential_calls;

#ifdef DEBUG
static unsigned long debug_cache_lookups;
static unsigned long debug_cache_hits;
static int debug_root_evaluations;
static int debug_deepest_plies;
static int debug_selected_score;
static const char *debug_search = "shallow";
static const char *debug_file = "debug-turns.csv";

static int debugFindHMEntry(HashMap *hm, unsigned char *key, u32 *data) {
    debug_cache_lookups++;
    int found = findHMEntry(hm,key,data);
    if (found) debug_cache_hits++;
    return found;
}

static void writeDebugTurn(int turn, int budget_ms, int legal_moves, Pos selected) {
    FILE *file = fopen(debug_file,"a+");
    if (!file) return;
    fseek(file,0,SEEK_END);
    if (ftell(file) == 0)
        fputs("move,budget_ms,elapsed_ms,legal_moves,possibilities_evaluated,deepest_completed_ply,winning_row,winning_col,winning_score,scored_positions,cache_lookups,cache_hits,search\n",file);
    fprintf(file,"%d,%d,%.3f,%d,%d,%d,%d,%d,%d,%lu,%lu,%lu,%s\n",
        turn,budget_ms,getElaspedTime()*1000.0,legal_moves,
        debug_root_evaluations,debug_deepest_plies,selected.y,selected.x,
        debug_selected_score,evaluation_calls,debug_cache_lookups,
        debug_cache_hits,debug_search);
    fclose(file);
}
#define SEARCH_CACHE_FIND(hm,key,data) debugFindHMEntry(hm,key,data)
#else
#define SEARCH_CACHE_FIND(hm,key,data) findHMEntry(hm,key,data)
#endif

void error(const char *format, ...)
{    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);    
    va_end(args);
    fprintf(stderr, "TERMINATING\n");

    exit(-1);
}

int calcPlayable(Board9* board) {
    int free = 0;
    for (int x=0;x<3;x++) {
        for (int y=0;y<3;y++) {
            if ( (board->overall_free & mask(x, y)) == 0) {
                Board3 sb = B3( board->cell[x+y*3] ) ;
                free += countFree(sb.p[0] | sb.p[1]);
            }
        }
    }
    return free;
}

void copyMoves(Moves* dest, Moves* src) {
    for(int i=0;i<src->count;i++) push(dest,src->moves[i]);
}

void sortMoves(Board9 *board, Moves *valid_moves, int player) {

    if (valid_moves->count <= 1) return;

    Moves game_winners = {0};
    Moves square_winners = {0};
    Moves the_rest = {0};

    Evaluation ev_orig = evalMAC2(board->overall, board->overall_free);
    for (int i=0;i<valid_moves->count;i++) {
        Board9 tboard = *board;
        set9(&tboard,valid_moves->moves[i].x,valid_moves->moves[i].y, player);
        Evaluation ev = evalMAC2(tboard.overall, tboard.overall_free);

        if ( ev.p3[player] > 0) {
            push(&game_winners,valid_moves->moves[i]);
            continue;
        }

        if ( ( ev.p2[player] > ev_orig.p2[player]) || ( ev.p1[player] > ev_orig.p1[player]) ) {
            push(&square_winners,valid_moves->moves[i]);
            continue;
        }
        push(&the_rest, valid_moves->moves[i] ); 
    }

    int original_count = valid_moves->count;
    valid_moves->count = 0;
    copyMoves(valid_moves, &game_winners);
    copyMoves(valid_moves, &square_winners);
    copyMoves(valid_moves, &the_rest);

    if (valid_moves->count != original_count) {
        fprintf(stderr, "Valid moves size has changed now:%d orig:%d winners:%d sq:%d rest:%d\n", valid_moves->count, original_count, game_winners.count, square_winners.count, the_rest.count);
        exit(0);
    }
}

/* Keys encode the entire playable state, directed destination, turn and horizon.
   Explicit bytes avoid struct padding; completed entries always store exact values. */
/* Encode every search-relevant state byte explicitly, avoiding padding and
 * distinguishing directed-board, side-to-move and horizon variants. */
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

int searchExpired(void) {
    return ((search_nodes++ & 63) == 0) && getElaspedTime() >= search_deadline;
}

/* Flags are derived from the position, so cached scores need no extra mode key. */
static unsigned possibleMasterLines(Board9 board) {
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

static int countProof(Board9 board, unsigned possible) {
    Board3 owned = B3(board.overall);
    int lead = POPCNT(owned.p[0]) - POPCNT(owned.p[1]);
    int remaining = 9 - POPCNT(board.overall_free);
    if (lead > remaining && !(possible & 2)) return 0;
    if (-lead > remaining && !(possible & 1)) return 1;
    return -1;
}

int scoreMoveMM(Board9 board, Pos p, int player, int depth) {
    (void)depth;
    if (searchExpired()) return -2;
    mm_score_count++;
    set9(&board, p.x, p.y, player);
    if (board.winner >= 0) return board.winner == 2 ? 0 : (board.winner == 0 ? -1 : 1);
    int proved = countProof(board, possibleMasterLines(board));
    if (proved >= 0) return proved == 0 ? -1 : 1;
    u16 bit = 1 << ((p.y % 3)*3 + p.x % 3);
    unsigned char key[KEY_SIZE];
    searchKey(&board, bit, 1-player, 65535, key);
    u32 data;
    if (SEARCH_CACHE_FIND(map, key, &data)) return (int)data-1;
    Moves2 moves;
    validMoves2(&board, &moves, p.x%3, p.y%3);
    int best = player == 0 ? -10 : 10;
    Pos next;
    while (moveNext(&moves, &next)) {
        int score = scoreMoveMM(board, next, 1-player, 0);
        if (score == -2) return -2;
        if ((player == 0 && score > best) || (player == 1 && score < best)) best = score;
        if (best == (player == 0 ? 1 : -1)) break;
    }
    addHMEntry(map, key, best+1);
    return best;
}

Pos evaluateMovesMM(Board9 *board, Moves2 *valid_moves) {
    clearHM(map);
    resetMoveIt(valid_moves);
    int best = 10;
    Moves tied = {0};
    Pos move;
    while (moveNext(valid_moves, &move)) {
        if (getElaspedTime() >= search_deadline) return (Pos){255,255};
        int score = scoreMoveMM(*board, move, 0, 0);
        if (score == -2) return (Pos){255,255};
#ifdef DEBUG
        debug_root_evaluations++;
        debug_deepest_plies = spaces_left;
#endif
        if (score < best) { best = score; tied.count = 0; }
        if (score == best) push(&tied, move);
        /* A proven win needs no further comparison. */
        if (best == -1) {
#ifdef DEBUG
            debug_selected_score = TERMINAL_SCORE;
#endif
            return move;
        }
    }
#ifdef DEBUG
    debug_selected_score = best == 1 ? -TERMINAL_SCORE : 0;
#endif
    return tied.moves[rand() % tied.count];
}

typedef struct Score_s {
    u16 p0;
    u16 p1;
} Score;

#ifndef DEFAULT_COUNT_SCALE
#define DEFAULT_COUNT_SCALE 1.0
#endif
static double count_scale = DEFAULT_COUNT_SCALE;
enum ScoreMode { ScoreDifference, ScoreRatio };
static enum ScoreMode score_mode = ScoreDifference;
#ifndef USCALE
#define USCALE 10
#endif
static int uscale = USCALE;
static int exact_primary_spaces = 17;
static int exact_narrow_spaces = 19;
#define COUNT_UNIT 20
static u16 f1_cache[POSS_BOARDS][2];
static const u16 scoring_lines[8] = {7,56,448,73,146,292,273,84};

/* Terminal outcomes keep their absolute ordering.  Positional strengths may
   instead be compared as a bounded relative advantage; adding one to each
   side prevents an empty 0/0 evaluation from creating a zero denominator. */
static int scoreForPlayer(Score score, int player) {
    if (score.p0 == TERMINAL_SCORE && score.p1 == 0)
        return player == 0 ? TERMINAL_SCORE : -TERMINAL_SCORE;
    if (score.p1 == TERMINAL_SCORE && score.p0 == 0)
        return player == 1 ? TERMINAL_SCORE : -TERMINAL_SCORE;
    int mine = player == 0 ? score.p0 : score.p1;
    int theirs = player == 0 ? score.p1 : score.p0;
    if (score_mode == ScoreRatio)
        return RELATIVE_SCORE_SCALE * (mine - theirs) / (mine + theirs + 2);
    return mine - theirs;
}

/* Grid potential: distinct winning cells plus unblocked one-mark lines.
   closed includes unavailable squares (especially drawn squares on U). */
static int f1_raw(u16 grid, u16 closed, int player) {
    Evaluation ev = evalMAC2(grid,closed);
    if (ev.p3[0] || ev.p3[1]) return 0;
    int wins = POPCNT(player ? ev.p1_winners : ev.p0_winners);
    return (wins == 0 ? 0 : wins == 1 ? 4 : 6) + ev.p1[player];
}
static void initScoringCache(void) {
    for(int grid=0;grid<POSS_BOARDS;grid++)
        for(int player=0;player<2;player++) f1_cache[grid][player]=f1_raw(grid,0,player);
}
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

Score scoreBoard(Board9 board, u16 last_cell, u16 last_bit) {
    (void)last_cell; (void)last_bit;
    ASSERT(board.winner == -1,"Scoring a won board");
    evaluation_calls++;
    if(fc(board.overall,0)!=fc(board.overall,1)) count_differential_calls++;
    int strength[2];
    for(int player=0;player<2;player++) {
        int main_strength = f1(board.overall,board.overall_free,player);
        /* Wild-card experiment: ownership matters only when the top-grid score is zero. */
        strength[player] = main_strength*uscale;
        if (main_strength == 0)
            strength[player] += (int)(fc(board.overall,player)*COUNT_UNIT*count_scale+0.5);
        for(int i=0;i<9;i++) if(!(board.overall_free & (1u<<i)))
            strength[player] += f1(board.cell[i],0,player)
                * f2(board.overall,board.overall_free,i,player);
        if(strength[player]>10000) strength[player]=10000;
    }
    return (Score){strength[0],strength[1]};
}

Score evaluateShallow(Board9 board, int player, u16 cell, u16 bit, int depth, int timed) {
    if (timed && searchExpired()) return (Score){TIMEOUT_SCORE,TIMEOUT_SCORE};
    set9CB(&board, cell, bit, player);
    if (board.winner >= 0)
        return board.winner == 2 ? (Score){0,0} :
            (board.winner == 0 ? (Score){TERMINAL_SCORE,0} : (Score){0,TERMINAL_SCORE});
    int proved = countProof(board, possibleMasterLines(board));
    if (proved >= 0)
        return proved == 0 ? (Score){TERMINAL_SCORE,0} : (Score){0,TERMINAL_SCORE};
    unsigned char key[KEY_SIZE];
    searchKey(&board, bit, 1-player, depth, key);
    u32 data;
    if (SEARCH_CACHE_FIND(map,key,&data)) return (Score){data & 65535, data >> 16};
    Score best;
    if (depth == 0) best = scoreBoard(board,cell,bit);
    else {
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
#ifdef DEBUG
            debug_root_evaluations++;
            if (target_plies > debug_deepest_plies)
                debug_deepest_plies = target_plies;
#endif

            if (score.p0 == TERMINAL_SCORE && score.p1 == 0) {
                root->proof = RootForcedWin;
                root->score = TERMINAL_SCORE;
                root->evaluated_plies = target_plies;
#ifdef DEBUG
                debug_selected_score = TERMINAL_SCORE;
#endif
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
#ifdef DEBUG
    debug_selected_score = best_score;
#endif
    return best_moves.moves[rand() % best_moves.count];
}

static int isLegalMove(int x, int y, Moves2 *moves)
{
    if (x < 0 || x > 8 || y < 0 || y > 8) return 0;

    int cell = (y / 3) * 3 + x / 3;
    int bit = (y % 3) * 3 + x % 3;
    return !!(moves->mask[cell] & (1u << bit));
}

Pos getMove(Board9 *board, Pos last_move, Moves2 *valid_moves)
{
    if (last_move.x != -1) {
        set9(board, last_move.x, last_move.y, 1);
    }

    evaluation_calls = 0;
    count_differential_calls = 0;
    search_nodes = 0;
#ifdef DEBUG
    debug_cache_lookups = 0;
    debug_cache_hits = 0;
    debug_root_evaluations = 0;
    debug_deepest_plies = 0;
    debug_selected_score = 0;
    debug_search = "shallow";
#endif

    spaces_left = calcPlayable(board);
    search_deadline = move_budget;

    Pos my_move;

    int primary_exact_trigger = spaces_left <= exact_primary_spaces;
    int narrow_exact_trigger =
        !primary_exact_trigger &&
        spaces_left < exact_narrow_spaces &&
        valid_moves->count < 10;
    int try_exact_search = primary_exact_trigger || narrow_exact_trigger;

    if (try_exact_search) {
#ifdef DEBUG
        debug_search = "exact";
#endif
        double elapsed = getElaspedTime();
        search_deadline = elapsed + (move_budget - elapsed) * 0.5;
        my_move = evaluateMovesMM(board, valid_moves);

        search_deadline = move_budget;
        if (my_move.x == 0xFF) {
            if (getenv("CG_LOCAL_HELLO")) printf("@DFS_FAILED\t%d\t%u\t%s\n",spaces_left,valid_moves->count,primary_exact_trigger?"primary":"narrow");
#ifdef DEBUG
            debug_search = "exact-fallback";
#endif
            my_move = evaluateMovesShallowTimed(board, valid_moves);
        }
    } else {
        my_move = evaluateMovesShallowTimed(board, valid_moves);
    }

    if (!isLegalMove(my_move.x, my_move.y, valid_moves)) {
        error("Selected illegal move\n");
    }

    set9(board, my_move.x, my_move.y, 0);
    return my_move;
}

int main(int argc,char* argv[])
{
    // Init

#ifdef CG_GAME
    fprintf(stderr, "Running in CodinGame\n"); 
#endif

    initBoardCaches();
    initScoringCache();

    Board9 p0_board = {0};
    //memset(&p0_board, 0, sizeof(p0_board)); 
    p0_board.winner = -1;
    map = createHM(128000,512000);

    if (argc > 1) {

        int arg = 1;

        while (arg < argc) {
            char* arg_str = argv[arg];
            arg++;
            if (strncmp(arg_str,"--exact-primary=",16)==0 || strncmp(arg_str,"--exact-narrow=",15)==0) {
                int primary = !strncmp(arg_str,"--exact-primary=",16);
                const char *value = arg_str + (primary ? 16 : 15);
                char *end; long parsed=strtol(value,&end,10);
                if(end==value || *end || parsed<0 || parsed>81)
                    error("Invalid exact threshold: %s\n",arg_str);
                if(primary) exact_primary_spaces=(int)parsed; else exact_narrow_spaces=(int)parsed;
                continue;
            }
            if (strncmp(arg_str,"--uscale=",9)==0) {
                char *end;
                long parsed=strtol(arg_str+9,&end,10);
                if(end==arg_str+9 || *end || parsed<0 || parsed>100)
                    error("Invalid uscale (expected integer 0..100): %s\n",arg_str);
                uscale=(int)parsed;
                continue;
            }
            if (strncmp(arg_str,"--count-scale=",14)==0) {
                char *end;
                double parsed=strtod(arg_str+14,&end);
                if(end==arg_str+14 || *end || !(parsed>=0 && parsed<=10))
                    error("Invalid count scale (expected 0..10): %s\n",arg_str);
                count_scale=parsed;
                continue;
            }
            if (strncmp(arg_str,"--score-mode=",13)==0) {
                const char *value=arg_str+13;
                if(strcmp(value,"difference")==0) score_mode=ScoreDifference;
                else if(strcmp(value,"ratio")==0) score_mode=ScoreRatio;
                else error("Invalid score mode (expected difference or ratio): %s\n",arg_str);
                continue;
            }
#ifdef DEBUG
            if (strncmp(arg_str,"--debug-file=",13)==0) {
                debug_file = arg_str+13;
                if (!*debug_file) error("Empty debug file path\n");
                continue;
            }
#endif
            error("Unknown bot argument: %s\n",arg_str);
        }

    }
    
    const char *seed_text = getenv("CG_SEED");
    unsigned seed = seed_text ? (unsigned)strtoul(seed_text,NULL,10) :
        (unsigned)(get_gtod_clock_time() ^ (uint64_t)getpid());
    srand(seed);
#ifndef BOT_BUILD_ID
#define BOT_BUILD_ID "unversioned"
#endif
    if (getenv("CG_LOCAL_HELLO")) {
#ifdef DEBUG
        const char *version = score_mode == ScoreRatio ? "MM-002-RMD" : "MM-002-DMD";
        const char *debug_metadata = "debug=1; ";
#else
        const char *version = score_mode == ScoreRatio ? "MM-002-RM" : "MM-002-DM";
        const char *debug_metadata = "";
#endif
        printf("@BOT\t%s\tbuild=%s; %sopening=minimax-from-start; exact-primary=%d; exact-narrow=%d; evaluator=f1-f2-fc; score-mode=%s; shallow-plies=4-until-timeout; partial-depth=merge; relative-scale=%d; shallow-cache=clear-each-depth; count-gate=f1-U-zero-per-player; uscale=%d; f1=winning-cells:0/4/6+one-lines; f2=base1+lines:1/2/4; count-component=20*owned; scale=%.6g; one-board-count-term=%d\n",version,BOT_BUILD_ID,debug_metadata,exact_primary_spaces,exact_narrow_spaces,score_mode==ScoreRatio?"ratio":"difference",RELATIVE_SCORE_SCALE,uscale,count_scale,(int)(20*count_scale+0.5));
        fflush(stdout);
    }
    int turn = 0;
    // game loop
    while (1) {
        Pos last_move;
        Moves2 valid_moves = {0};

        int i = scanf("%d%d", &last_move.y, &last_move.x);
        if (i == EOF) break;
        if (i < 2) error("Failed to read last move\n");
        
        if (last_move.x == -2) exit(0);  // Internal not for codinGame 
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
        
        Pos my_move = getMove(&p0_board, last_move, &valid_moves);
#ifdef DEBUG
        writeDebugTurn(current_turn,(int)(move_budget*1000+0.5),valid_action_count,my_move);
#endif
        if(getenv("CG_LOCAL_HELLO")) printf("@USE\t%lu\t%lu\n",evaluation_calls,count_differential_calls);
        printf("%d %d\n", my_move.y, my_move.x);
        fflush(stdout);
    }
    destroyHM(map);    
    exit(0);
    return 0;
}
