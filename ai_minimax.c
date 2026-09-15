#pragma GCC optimize "O3,omit-frame-pointer,inline"
#pragma GCC target("lzcnt,popcnt")

#ifndef OPENING_SEARCH_THRESHOLD
#define OPENING_SEARCH_THRESHOLD 72
#endif

#define MAX_TIME 0.0900
#define TERMINAL_SCORE 60000
#define TIMEOUT_SCORE 65535
#define RELATIVE_SCORE_SCALE 10000
//#define MAX_TIME 0.50

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h> 
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <search.h>

#include "board.h"

enum Task {Shallow, Minimax, None};
enum Task current_task = None;

uint64_t start_time;
double   mm_step_target_time;
double   mm_step_start_time;
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

void error(const char *format, ...)
{    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);    
    va_end(args);
    fprintf(stderr, "TERMINATING\n");

    exit(-1);
}

static void appendLog(const char *format, ...) {
#ifndef CG_GAME
    char line[2048];
    va_list args; va_start(args,format);
    int n=vsnprintf(line,sizeof line,format,args); va_end(args);
    if(n<0) return;
    if(n>=(int)sizeof line) n=sizeof(line)-1;
    int fd=open("ai_minimax.log",O_WRONLY|O_CREAT|O_APPEND,0644);
    if(fd>=0) { (void)write(fd,line,(size_t)n); close(fd); }
#else
    (void)format;
#endif
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
    if (findHMEntry(map, key, &data)) return (int)data-1;
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
        if (score < best) { best = score; tied.count = 0; }
        if (score == best) push(&tied, move);
        /* A proven win needs no further comparison. */
        if (best == -1) return move;
    }
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
static unsigned long evaluation_calls, count_differential_calls;
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
    if (findHMEntry(map,key,&data)) return (Score){data & 65535, data >> 16};
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

int last_completed_plies;

static int sameMove(Pos a, Pos b) {
    return a.x == b.x && a.y == b.y;
}

/* Pick from the best scores produced by the last wholly completed iteration.
   proven_loss may contain extra information discovered in the next, aborted
   iteration, so a newly proved losing move is never restored here. */
static Pos chooseBestRetainedMove(
    Moves *root_moves,
    int retained_score[81],
    unsigned char proven_loss[81],
    Pos previous_choice
) {
    const int worse_than_any_score = -1000000;
    int best_score = worse_than_any_score;
    Moves best_moves = {0};

    for (int i=0;i<root_moves->count;i++) {
        if (proven_loss[i]) continue;
        if (retained_score[i] > best_score) {
            best_score = retained_score[i];
            best_moves.count = 0;
        }
        if (retained_score[i] == best_score)
            push(&best_moves,root_moves->moves[i]);
    }

    /* If every move is a proved loss, there is no better legal alternative. */
    if (!best_moves.count) return previous_choice;

    /* Preserve the choice already made at the completed depth when possible;
       this avoids consuming another random number merely to select the same
       tied set again. */
    for (int i=0;i<best_moves.count;i++)
        if (sameMove(best_moves.moves[i],previous_choice)) return previous_choice;

    return best_moves.moves[rand() % best_moves.count];
}

Pos evaluateMovesShallowTimed(Board9 *board, Moves2 *valid_moves) {
    /* Materialise the root moves because Moves2 is an iterator over bitmasks,
       while the arrays below need a stable index for each candidate. */
    Moves root_moves = {0};
    resetMoveIt(valid_moves);
    Pos move;
    while (moveNext(valid_moves,&move)) push(&root_moves,move);

    last_completed_plies = 0;

    /* Always have a legal answer, even if the clock expires before the first
       complete iteration. */
    Pos chosen = root_moves.moves[rand() % root_moves.count];

    /* Scores are committed only when every surviving root move completed at
       the same depth.  Proofs are different: a completely searched root that
       is a forced loss remains a forced loss even if the iteration later runs
       out of time on another root. */
    int retained_score[81] = {0};
    unsigned char proven_loss[81] = {0};

    /* depth is the number of recursive plies below our root move.  Odd values
       1,3,5 therefore complete searches of 2,4,6 total plies, always ending
       after the opponent has replied. */
    for (int depth_below_root=1;depth_below_root<=5;depth_below_root+=2) {
        clearHM(map);
        int iteration_complete = 1;
        int iteration_best_score = -1000000;
        int iteration_score[81] = {0};
        Moves iteration_best_moves = {0};

        for (int i=0;i<root_moves.count;i++) {
            if (proven_loss[i]) continue;

            if (getElaspedTime() >= search_deadline) {
                iteration_complete = 0;
                break;
            }

            u16 cell, bit;
            pos2cell(root_moves.moves[i],&cell,&bit);
            Score score = evaluateShallow(
                *board,0,cell,bit,depth_below_root,1
            );
            if (score.p0 == TIMEOUT_SCORE) {
                iteration_complete = 0;
                break;
            }

            /* This root was fully minimaxed to a forced win.  No result from
               another root can be better, so it is immediately actionable. */
            if (score.p0 == TERMINAL_SCORE && score.p1 == 0) {
                last_completed_plies = depth_below_root + 1;
                return root_moves.moves[i];
            }

            /* Retain a completed forced-loss proof across later iterations.
               We may still return such a move if every legal move loses. */
            if (score.p1 == TERMINAL_SCORE && score.p0 == 0) {
                proven_loss[i] = 1;
                continue;
            }

            int value = scoreForPlayer(score,0);
            iteration_score[i] = value;
            if (value > iteration_best_score) {
                iteration_best_score = value;
                iteration_best_moves.count = 0;
            }
            if (value == iteration_best_score)
                push(&iteration_best_moves,root_moves.moves[i]);
        }

        /* Positional results from an incomplete iteration are deliberately
           discarded.  Only the forced-loss flags above survive it. */
        if (!iteration_complete) break;

        last_completed_plies = depth_below_root + 1;
        memcpy(retained_score,iteration_score,sizeof retained_score);

        /* No unproved move remains: every legal root is a forced loss. */
        if (!iteration_best_moves.count) break;

        chosen = iteration_best_moves.moves[
            rand() % iteration_best_moves.count
        ];
    }

    return chooseBestRetainedMove(
        &root_moves,retained_score,proven_loss,chosen
    );
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
    last_completed_plies = -1;

    spaces_left = calcPlayable(board);
    search_deadline = move_budget;

    int exact_attempted = 0;
    int exact_failed = 0;
    Pos my_move;

    int primary_exact_trigger = spaces_left <= exact_primary_spaces;
    int narrow_exact_trigger =
        !primary_exact_trigger &&
        spaces_left < exact_narrow_spaces &&
        valid_moves->count < 10;
    int try_exact_search = primary_exact_trigger || narrow_exact_trigger;

    if (try_exact_search) {
        current_task = Minimax;
        exact_attempted = 1;

        double elapsed = getElaspedTime();
        search_deadline = elapsed + (move_budget - elapsed) * 0.5;
        my_move = evaluateMovesMM(board, valid_moves);

        search_deadline = move_budget;
        if (my_move.x == 0xFF) {
            exact_failed = 1;
            if (getenv("CG_LOCAL_HELLO")) printf("@DFS_FAILED\t%d\t%u\t%s\n",spaces_left,valid_moves->count,primary_exact_trigger?"primary":"narrow");
            my_move = evaluateMovesShallowTimed(board, valid_moves);
        }
    } else {
        current_task = Shallow;
        my_move = evaluateMovesShallowTimed(board, valid_moves);
    }

    if (!isLegalMove(my_move.x, my_move.y, valid_moves)) {
        error("Selected illegal move\n");
    }

    appendLog(
        "turn pid=%ld seed=%s spaces=%d legal=%u task=%s "
        "exact_attempted=%d exact_failed=%d trigger=%s completed_plies=%d "
        "nodes=%llu elapsed_us=%llu move=%d,%d evals=%lu "
        "count_diff_evals=%lu uscale=%d count_scale=%.6g score_mode=%s\n",
        (long)getpid(),
        getenv("CG_SEED") ? getenv("CG_SEED") : "",
        spaces_left,
        valid_moves->count,
        current_task == Minimax ? "exact" : "shallow",
        exact_attempted,
        exact_failed,
        exact_attempted ? (primary_exact_trigger ? "primary" : "narrow") : "none",
        last_completed_plies,
        (unsigned long long)search_nodes,
        (unsigned long long)(getElaspedTime() * 1000000),
        my_move.y,
        my_move.x,
        evaluation_calls,
        count_differential_calls,
        uscale,
        count_scale,
        score_mode == ScoreRatio ? "ratio" : "difference"
    );

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
    appendLog("start pid=%ld seed=%u build=%s opening=minimax-from-start exact_primary=%d exact_narrow=%d uscale=%d count_scale=%.6g score_mode=%s\n",(long)getpid(),seed,BOT_BUILD_ID,exact_primary_spaces,exact_narrow_spaces,uscale,count_scale,score_mode==ScoreRatio?"ratio":"difference");
    if (getenv("CG_LOCAL_HELLO")) {
        printf("@BOT\tminimax-%s\topening=minimax-from-start; exact-primary=%d; exact-narrow=%d; evaluator=f1-f2-fc; score-mode=%s; relative-scale=%d; shallow-cache=clear-each-depth; count-gate=f1-U-zero-per-player; uscale=%d; f1=winning-cells:0/4/6+one-lines; f2=base1+lines:1/2/4; count-component=20*owned; scale=%.6g; one-board-count-term=%d\n",BOT_BUILD_ID,exact_primary_spaces,exact_narrow_spaces,score_mode==ScoreRatio?"ratio":"difference",RELATIVE_SCORE_SCALE,uscale,count_scale,(int)(20*count_scale+0.5));
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
        move_budget = turn++ == 0 ? 0.900 : MAX_TIME;
        
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
        
        if(getenv("CG_LOCAL_HELLO")) printf("@USE\t%lu\t%lu\n",evaluation_calls,count_differential_calls);
        printf("%d %d\n", my_move.y, my_move.x);
        fflush(stdout);
    }
    destroyHM(map);    
    exit(0);
    return 0;
}
