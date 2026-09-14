#pragma GCC optimize "O3,omit-frame-pointer,inline"
#pragma GCC target("lzcnt,popcnt")

#define MAX_TIME 0.0950
//#define MAX_TIME 0.50

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

enum Task {Heuristic, Shallow, Minimax, ShallowFromMM, None};
enum Task current_task = None;

uint64_t start_time;
double   mm_step_target_time;
double   mm_step_start_time;
uint64_t get_gtod_clock_time ()
{
    struct timeval tv;

    if (gettimeofday (&tv, NULL) == 0)
        return (uint64_t) (tv.tv_sec * 1000000 + tv.tv_usec);
    else
        return 0;
}

double getElaspedTime() {
    uint64_t elasped = get_gtod_clock_time() - start_time;
    return ( (double)elasped ) / 1000000.0;
}

#include "hashmap.h"

typedef struct MoveEvaluation_s
{
    Evaluation my_move[81];
    Evaluation op_cell[81];
    Evaluation my_overall[81];
    Evaluation my_delta[81];
    int op_win[81];
    int op_multi[81];
    Pos op_next_move[81];
    int blocks_op_2[81];

} MoveEvaluation;

typedef struct
{
    Evaluation my_move;
    Evaluation op_cell;
    Evaluation my_overall;
    Evaluation my_delta;
    int op_win;
    int op_multi;
    Pos op_next_move;
    int blocks_op_2;
    Pos actual_move;

} MoveEvaluation1;

typedef char DispGrid[9][9];

// Globals
int p0_log;
HashMap* map;
int spaces_left;
//int heuristic_weights[6] =  { 30, 10, 3, 1, 7, 3};
int heuristic_weights[6] =  { 29, 10, 4, 1, 7, 3};
Pos forced_first_move = {-1,-1}; //{0,1}; FIXME
uint32_t mm_score_count = 0;

void printBoard(Board9 *board, Pos move, Moves2 *valid_moves);

void error(const char *format, ...)
{    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);    
    va_end(args);
    fprintf(stderr, "TERMINATING\n");

    exit(-1);
}

void loadWeights(const char* file_name)
{
  FILE* file = fopen (file_name, "r");
  int i = fscanf (file, "%d %d %d %d %d %d", &heuristic_weights[0],&heuristic_weights[1],&heuristic_weights[2],&heuristic_weights[3],&heuristic_weights[4],&heuristic_weights[5] ); 
  if (i < 6) {
      error("Only loaded %d weights\n",i);
  }
  //fprintf(stderr,"Loaded:");
  //for (int i=0;i<6;i++) {
  //    fprintf(stderr,"[%d=%d]",i,heuristic_weights[i]);
  //} 
  //fprintf(stderr,"\n");
  fclose (file);
}

void logger(const char* format, ...) {

#ifdef CG_GAME
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);    
        va_end(args);    
#endif

#ifndef CG_GAME
    if (p0_log) {
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);    
        va_end(args);
    }
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

int doesOpWin(u16 oboard, int x, int y)
{
    set(&oboard, x, y, 1);
    Evaluation ev = evalMAC1(oboard);
    return (ev.p3[1] > 0);
}

Evaluation findOpBest(Board9 tboard, Pos *p, int *op_win)
{
    int high = -1;
    Evaluation best = {};
    for (int x = 0; x < 3; x++)
    {
        for (int y = 0; y < 3; y++)
        {
            if ((tboard.overall_free & mask(x, y)) == 0)
            {
                Evaluation ev = evalMAC1(tboard.cell[pos(x, y)]);
                int win_bonus = 0;
                if (ev.p2[1] > 0 && doesOpWin(tboard.overall, x, y))
                {
                    win_bonus += 100;
                    *op_win = 1;
                }
                if (ev.p3[1] > 0) fprintf(stderr, "WOW\n");
                int score = win_bonus + ev.p2[1] * 10 + ev.p1[1] + ev.p2[0] * 2;
                if (score > high)
                {
                    high = score;
                    best = ev;
                    p->x = x;
                    p->y = y;
                }
            }
        }
    }
    return best;
}

MoveEvaluation1 evaluateMove(Pos move, Board9 tboard)
{
    MoveEvaluation1 move_eval = {0};
    move_eval.actual_move = move;
    int x = move.x;
    int y = move.y;

    Evaluation prev_ev = evalMAC1( tboard.cell[pos(x/3, y/3)] );
    Evaluation ev = set9(&tboard, x, y, 0);
    move_eval.my_move = ev;
    move_eval.blocks_op_2 = (ev.p2[1] < prev_ev.p2[1]);
    
    move_eval.my_delta.p1[0] = ev.p1[0] - prev_ev.p1[0];
    move_eval.my_delta.p2[0] = ev.p2[0] - prev_ev.p2[0];
    move_eval.my_delta.p3[0] = ev.p3[0] - prev_ev.p3[0];
    move_eval.my_delta.p1[1] = ev.p1[1] - prev_ev.p1[1];
    move_eval.my_delta.p2[1] = ev.p2[1] - prev_ev.p2[1];
    move_eval.my_delta.p3[1] = ev.p3[1] - prev_ev.p3[1];

    move_eval.my_overall = evalMAC2(tboard.overall, tboard.overall_free);

    int ox = x % 3;
    int oy = y % 3;

    if ((tboard.overall_free & mask(ox, oy)) == 0)
    {
        move_eval.op_cell = evalMAC1( tboard.cell[pos(ox, oy)] );
        move_eval.op_multi = 0;
        move_eval.op_next_move.x = ox;
        move_eval.op_next_move.y = oy;
        move_eval.op_win = 0;
        if (move_eval.op_cell.p2[1] > 0)
        { // Op can win in this square as they have 2 + blank already
            move_eval.op_win = doesOpWin(tboard.overall, ox, oy);
        }
    }
    else
    {
        Pos p = {};
        int op_win = 0;
        move_eval.op_cell = findOpBest(tboard, &p, &op_win);
        move_eval.op_multi = 1;
        move_eval.op_next_move = p;
        move_eval.op_win = op_win;
    }
    return move_eval;
}

int evalComp(Evaluation ev, int p0_1, int p0_2, int p1_1, int p1_2) {
    return (ev.p1[0] == p0_1 && ev.p2[0] == p0_2 && ev.p1[1] == p1_1 && ev.p2[1] == p1_2);
}

int evalDigit(int digit, char* comp) {
    switch (comp[0])
{
    case '=':{
      int c_digit = comp[1] - '0';
      return (digit == c_digit);  
    }  

    case '>': {
      int c_digit = comp[1] - '0';
      return (digit > c_digit);
    }
    
    case '*':
        return 1;

    default:
        logger("WARNING INVALID DIGIT\n");
    return 0;
}
}
int evalCheck(Evaluation ev,char* p0_1, char* p0_2, char* p1_1, char* p1_2) {
    return ( evalDigit(ev.p1[0], p0_1) && evalDigit(ev.p2[0], p0_2) && evalDigit(ev.p1[1], p1_1) && evalDigit(ev.p2[1], p1_2) );
}

Pos pickRandomFromBuffer(Moves *moves) {
    if (moves->count == 0) error("Buffer is zero, pick not possible\n");
    if (moves->count == 1) return moves->moves[0];

    Moves cross_moves;
    cross_moves.count = 0;

    for (int i=0 ; i < moves->count ; i++) {
        Pos p1 = moves->moves[i];
        Pos p = {p1.x % 3, p1.y % 3};        
        if ( ( p.x==1 && p.y==0 ) || ( p.x==0 && p.y==1 ) || ( p.x==2 && p.y==1 ) || ( p.x==1 && p.y==2 ) ) 
            push(&cross_moves, p1);
    }

    if (cross_moves.count > 0) {
        if (cross_moves.count == 1) {
            return cross_moves.moves[0];
        }   else {
            return cross_moves.moves[rand() % cross_moves.count];
        }
    }
    return moves->moves[rand() % moves->count];
}

void copyMoves(Moves* dest, Moves* src) {
    if (src->count == 0) return;

    for (int i=0;i < src->count; i++) {
        push(dest, src->moves[i]);
    }
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

int scoreMoveMM(Board9 board, Pos p, int player, int depth) {   

    static u_int64_t score_count = 0;
    
    int other_player = (player == 0) ? 1: 0;
    mm_score_count++;

    set9(&board,p.x, p.y, player);
    if (board.winner > -1) {        
       if ( ( score_count++ & 0xFF) == 0xFF ) {
           double now = getElaspedTime();
           double elapsed = now - mm_step_start_time;
           if (elapsed > mm_step_target_time) {
               return -2;
           }
       }

        if (board.winner == 0) return -1;
        if (board.winner == 1) return 1;
        if (board.winner == 2) return 0;        
    }

    board.cell[9] = board.overall | (player * 0x10000);
    u32 data = 0;

    if ( findHMEntry(map,(unsigned char*) board.cell, &data)) {
        int score = data;
        return score -1;
    }

    Moves2 valid_moves = {};
    validMoves2( &board, &valid_moves, p.x % 3, p.y % 3 );

    if (valid_moves.count == 0) {
        fprintf(stderr,"Something wrong, no valid moves\n");
        exit(0);
    }
    
    //sortMoves(&board, &valid_moves, other_player);

    int max = -10;
    int min = 10;
    Pos move;
    
    while ( moveNext(&valid_moves, &move) ) {
        int score = scoreMoveMM(board, move, other_player, depth+1);
        if (score == -2) return score; // timeout

        if (score > max) max = score;
        if (score < min) min = score;
        if ( (other_player == 0 && score == -1) || ( other_player == 1 && score == 1) ) break;
    }
    int final_score = (other_player == 0) ? min : max;

    int store_score = final_score + 1;
    addHMEntry(map, (unsigned char*) board.cell, store_score);

    return final_score;
}

Pos evaluateMovesMM( Board9* board, Moves2 *valid_moves ) {  

    clearHM(map);  
    int best = 10;
    Pos best_move = {0};
    //sortMoves(board, valid_moves,0);

    double loop_start = getElaspedTime();
    
    Pos move = {0};
    int i = 0;
    while (  moveNext(valid_moves,&move) ) { 
        int moves_to_go = valid_moves->count-i;
        mm_step_start_time = getElaspedTime();
        mm_step_target_time = ( MAX_TIME - mm_step_start_time ) / (double)moves_to_go;
        if (i >0) mm_step_target_time *= 1.2;
        logger("(%d,%d)=",move.x, move.y);
        int score = scoreMoveMM( *board, move,0,0 );
        double now = getElaspedTime();

        if (score == -2) {            
            fprintf(stderr,"........MM[TIMEOUT@%d/%d] Total MM=%1.3f this step=%1.3f target=%1.3f\n",i+1, valid_moves->count,(now-loop_start), (now-mm_step_start_time), mm_step_target_time );
            Pos timeout = {0xFF,0xFF};
            return timeout;
        }
        logger("%d |",score);
        if (score < best) {
            best = score;
            best_move = move;            
            if (best == -1) break;
        }

        double avg_time_per_score = (now - loop_start) / ((double)i+1);
        double to_do = (double)(valid_moves->count-(i+1));
        double estimated_end = now + (to_do * avg_time_per_score);
        if (estimated_end > MAX_TIME) {
            //fprintf(stderr,"........MM[PFAIL@%d/%d] step=%1.3f/%1.3f total=%1.3f avg=%1.3f est end=%1.3f\n",i+1,valid_moves->count, (now-mm_step_start_time), mm_step_target_time, (now-loop_start), avg_time_per_score, estimated_end );
            Pos timeout = {0xFF,0xFF};
            return timeout;
        }
        i++;
    }
    
    logger("[MMEND %d]\n",best);
    return best_move;
}

typedef struct Score_s {
    u16 p0;
    u16 p1;
} Score;

Score scoreBoard(Board9 board, u16 last_cell, u16 last_bit) {

    ASSERT((board.winner == -1),"Scoring a won board");

    Score s = {0,0};

    Evaluation ev_o = evalMAC2(board.overall,board.overall_free);   

    u16 next_moves_p0 = (board.overall_free & last_bit) ? ~board.overall_free : last_bit;

    if (next_moves_p0 & ev_o.p0_winners) {
        s.p0 = 149;
        s.p1 = 0;
        return s;
    }
    s.p0 += ev_o.p2[0] * heuristic_weights[0]; // 30
    s.p1 += ev_o.p2[1] * heuristic_weights[0];

    s.p0 += ev_o.p1[0] * heuristic_weights[1]; // 10
    s.p1 += ev_o.p1[1] * heuristic_weights[1];

    u16 p0_2s = board.overall;
    u16 p1_2s = board.overall;

    u16 p0_free = board.overall_free;
    u16 p1_free = board.overall_free;

   for (int x = 0; x < 3; x++) {
        for (int y = 0; y < 3; y++) {
            if ((board.overall_free & mask(x, y)) == 0) {
                Evaluation ev = evalMAC1( board.cell[pos(x, y)] );

                if (ev.p2[0] > 0) {                    
                    set(&p0_2s,x,y,0);
                    p0_free = (p0_free | mask(x,y));
                }

                if (ev.p2[1] > 0) {
                    set(&p1_2s,x,y,1);
                    p1_free = (p1_free | mask(x,y));
                }

                s.p0 += ev.p2[0] * heuristic_weights[2]; // 3
                s.p1 += ev.p2[1] * heuristic_weights[2];

                s.p0 += ev.p1[0] * heuristic_weights[3]; // 1
                s.p1 += ev.p1[1] * heuristic_weights[3];
            }
        }
    }

    Evaluation p0_ev = evalMAC2(p0_2s, p0_free);
    Evaluation p1_ev = evalMAC2(p1_2s, p1_free);
    
    s.p0 += p0_ev.p3[0] * heuristic_weights[4]; // 7
    // if (p0_ev.p2[0] > ev_o.p2[0]) {
    //     s.p0 += ( p0_ev.p2[0] - ev_o.p2[0]) * heuristic_weights[5]; // 3
    // }

    s.p1 += p1_ev.p3[1] * heuristic_weights[4];
    // if (p1_ev.p2[1] > ev_o.p2[1]) {
    //     s.p1 += (p1_ev.p2[1]  - ev_o.p2[1]) * heuristic_weights[5]; 
    // }

    s.p0 += p0_ev.p2[0]  * heuristic_weights[5]; // 3
    s.p1 += p1_ev.p2[1]  * heuristic_weights[5]; 

    //s.p0 &= 0xFF;
    //s.p1 &= 0xFF;
    //if (s.p0 > 255) s.p0 = 255;
    //if (s.p1 > 255) s.p1 = 255; 
    //if (s.p0 == 103 && s.p1 == 48) fprintf(stderr,"found it overall.p1=%d, overall.p2=%d, f9=%d, f10=%d\n",ev_o.p1[0],ev_o.p2[0], fix_9, fix_10);

    return s;
}

Score evaluateShallow(Board9 board, int player, u16 cell, u16 bit, int depth, int timed) {

    //fprintf(stderr,"evaluateShallow(player=%d, move=%d %d)\n", player, move.x, move.y);
    static uint32_t count = 0;
    int next_player = (player == 0) ? 1: 0;

    if ( depth == 0 && timed)
    {
        if ((count++ & 0x1FF) == 0x1FF)
        {
            double elapsed = getElaspedTime();
            if (elapsed > MAX_TIME)
            {
                Score timeout = {0xFF, 0xFF};
                // logger("internal timeout (%d,%d) depth:%d\n",move.x, move.y,depth);
                return timeout;
            }
        }
    }

    //set9(&board, move.x, move.y,player);
    set9CB(&board, cell, bit, player);
    if (board.winner > -1) {
        Score sc = {0};

        switch (board.winner)
        {
        case 2:
            return sc;
            
        case 0:
            sc.p0 = 150;
            return sc;
        
        case 1:
            sc.p1 = 150;
            return sc;

        default:
            ASSERT( (board.winner >-1 && board.winner < 3),"invalid winner");
        }

        return sc;
    }

    //if (depth == 0 && player != 1) fprintf(stderr,"weird\n");
    
    board.cell[9] = board.overall | (player * 0x10000);
    u32 data = 0;

     if ( findHMEntry(map,(unsigned char*) board.cell, &data)) {
         Score sc = { data & 0xFF, data >> 8};   
         return sc;
     }

    if ( depth == 0 ) {
        Score sc = scoreBoard(board, cell, bit);
        data = (sc.p1 << 8) | sc.p0;
        addHMEntry(map, (unsigned char*) board.cell, data);        
        return sc;
    }

    Moves2 valid_moves;        
    Pos move = cell2pos(cell, bit); // FIXME peformance
    validMoves2( &board, &valid_moves, move.x % 3, move.y % 3 );
    //sortMoves(&board, &valid_moves, next_player);

    if (valid_moves.count == 0) {
        fprintf(stderr,"ERROR BOARD>>> winner:%d\n",board.winner);        
        error("No valid moves\n");
    }
    
    int besti = -1000;
    Score bests = {};

    int new_depth = depth - 1;
    //Pos mv;//,bestmv = {0};
    u16 ncell, nbit;
    while ( moveNextCB(&valid_moves, &ncell, &nbit) ) {
        //mv = cell2pos(cell,bit);
        Score score = evaluateShallow(board,next_player, ncell, nbit, new_depth, timed );

        if (score.p0 == 0xFF && score.p1 == 0xFF) return score; // timeout

        int iscore = (next_player == 0) ? (score.p0 - score.p1) : (score.p1 - score.p0);
        if (iscore > besti) {
            besti = iscore;
            bests = score;    
            //bestmv = mv;
            if ( (next_player == 0 && score.p0 >= 150) || (next_player == 1 && score.p1 >= 150) ) break;
        }
    }

    data = (bests.p1 << 8) | bests.p0;
    addHMEntry(map, (unsigned char*) board.cell, data);
    // if (player == 0 && move.x == 8 && move.y == 8 && depth == 3) {
    //     fprintf(stderr,"Replying to player %d move (%d,%d), player %d played (%d,%d)\n", player, move.x, move.y, next_player, bestmv.x, bestmv.y);
    // }
    return bests;
}

Pos evaluateMoves( Board9* board, Moves2 *valid_moves )
{
    MoveEvaluation1 move_eval[81] = {0};
    Moves buffer = {};
    Pos p = {};

    if (valid_moves->count == 0)
    {
        logger("Error no valid moves\n");
        exit(0);
    }

    // Only 1 move just pick it and we're done
    if (valid_moves->count == 1)
    {
        logger("Only 1 move\n");
        ASSERT( moveNext(valid_moves,&p), "No move found." );
        return p;
    }

    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        MoveEvaluation1 temp_eval = evaluateMove(p, *board );
        if (temp_eval.my_overall.p3[0] > 0)
        {
            logger("Winning move\n");
            return p; // it's a winner
        }
        move_eval[pos2bitNo(p)] = temp_eval;
    }

    // So we can't win whole game so find good moves
    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (move_eval[i].my_move.p3[0] > 0 && move_eval[i].op_cell.p2[1] == 0)
        {
            logger("Winning a square op doesn't win 1 next turn\n");
            return p; // win a sqaure without giving op one next go
        }        
    }

    // get me a '2' 
    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (move_eval[i].my_move.p2[0] > 0 && evalCheck(move_eval[i].op_cell ,">1","=0","=0","=0" ) )
        {
            logger("Give me a 2 with oponent into zero for him where I have 1 but no 2\n");
            return p; 
        }        
    }

    // get me a '2' putting oponent into a zero
    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (move_eval[i].my_move.p2[0] > 0 && evalCheck(move_eval[i].op_cell,"=0","=0","=0","=0" ) )
        {
            logger("Give me a 2 with oponent going into an empty grid 2\n");
            return p; 
        }        
    }

    // get me a '2' 
    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (move_eval[i].my_move.p2[0] > 0 && evalCheck(move_eval[i].op_cell ,">0","=0",">0","=0" ) )
        {
            logger("Give me a 2 with oponent going into a 1/1 that doesn't let him kill a 2\n");
            return p; 
        }        
    }

    // get me a '2' 
    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (move_eval[i].my_move.p2[0] > 0 && evalCheck(move_eval[i].op_cell ,"*","=0","*","=0" ) )
        {
            logger("Give me a 2 with oponent going into a 1 for him that doesn't let him kill a 2\n");
            return p; 
        }        
    }    

    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) ) {
        int i = pos2bitNo(p);
        if (move_eval[i].blocks_op_2 ) {
            logger("Blocking moves exist\n");
            break;
        }        
    }

    // Blocks op's 2
    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (move_eval[i].blocks_op_2 && evalCheck(move_eval[i].op_cell,"*","=0","*","=0" ) )
        {
            logger("Block op 2 without giving him a sqaure or letting him kill one of mine\n");
            return p; 
        }        
    }

    // Blocks op's 2
    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (move_eval[i].blocks_op_2 && evalCheck(move_eval[i].op_cell ,"*",">1","*","=0" ) )
        {
            logger("Block op 2 without giving him a square but let him block a double [%d,%d] multi:%d\n",p.x,p.y,move_eval[i].op_multi );
            return p; 
        }        
    }

    // 1s
    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    { 
        int i = pos2bitNo(p);
        if (move_eval[i].actual_move.x != p.x || move_eval[i].actual_move.y != p.y) {
            fprintf(stderr,"move eval has a different move (%d,%d) to pos (%d,%d)\n",move_eval[i].actual_move.x,move_eval[i].actual_move.y,p.x,p.y);
            fprintf(stderr,"(%d,%d)->%d",p.x,p.y,i);
        }
        if (move_eval[i].my_delta.p1[0] > 0 && evalCheck(move_eval[i].op_cell ,">0","=0","=0","=0" )) push(&buffer, p);
    }

    if (buffer.count > 0) {
            Pos mv = pickRandomFromBuffer( &buffer );
            logger("RANDOM (%d,%d)Gained a 1 with oponent going into a sqaure I have a 1 in.\n",mv.x, mv.y);
            return mv;
    }

    // -----------------------------------------------------------------------------------------------------------
    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (move_eval[i].my_delta.p1[0] > 0 && evalCheck(move_eval[i].op_cell, ">0","=0",">0","=0" ) ) push(&buffer,p);
    
    }
    if (buffer.count > 0) {
            logger("RANDOM: Gained a 1 with oponent going into a square both of us has 1\n");
            return pickRandomFromBuffer( &buffer ) ;
    }        

    // -----------------------------------------------------------------------------------------------------------
    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (move_eval[i].my_delta.p1[0] > 0 && evalCheck(move_eval[i].op_cell ,"=0","=0","=0","=0" )) push(&buffer, p);
    }

    if (buffer.count > 0) {
            logger("RANDOM: Gained a 1 with oponent going into an empty square\n");
            return pickRandomFromBuffer( &buffer );
        }        

    // ---------------------------------------------------------------------------------------------------------------
    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (move_eval[i].my_delta.p1[0] > 0 && move_eval[i].op_cell.p2[1] == 0 && move_eval[i].op_cell.p1[1] == 0)
        {
            //p0_log = 1;
            logger("****Gained a 1 with oponent going into a zero for him in (%d,%d)\n",p.x,p.y);
            return p; 
        }        
    }

    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (move_eval[i].my_move.p3[0] > 0 &&  move_eval[i].blocks_op_2 && !move_eval[i].op_win ) 
        {
            logger("Take a 3 that blocks his 2 but give him a square but not let him win[experimental]\n");
            return p; 
        }        
    }

    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (move_eval[i].my_move.p1[0] > 0 && evalCheck(move_eval[i].op_cell ,"*","*","*","=0" ))
        {
            logger("Don't play into a 2\n");
            return p;
        }        
    }

    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (move_eval[i].my_move.p3[0] > 0 && !move_eval[i].op_win)
        {
            logger("Take a square even if it gives him one but he doesn't win\n");
            return p; 
        }        
    }

    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if ( evalCheck(move_eval[i].op_cell ,"*","*","*","=0" ) )
        {
            logger("Give him my 2 rather than give him a 3\n");
            return p; 
        }        
    }

    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (!move_eval[i].op_win && move_eval[i].blocks_op_2 )
        {
            logger("Prevent him winning but grab a block\n");
            return p; 
        }        
    }

    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (!move_eval[i].op_win && move_eval[i].my_delta.p2[0] > 0 )
        {
            logger("Prevent him winning but grab a 2\n");
            return p; 
        }        
    }

    resetMoveIt(valid_moves);
    while ( moveNext(valid_moves,&p) )
    {
        int i = pos2bitNo(p);
        if (!move_eval[i].op_win)
        {
            logger("Prevent him winning\n");
            return p; 
        }        
    }

    //Random
    resetMoveIt(valid_moves);
    int chosen_move = rand() % valid_moves->count;
    for (int i=0;i<=chosen_move;i++) {
        moveNext(valid_moves,&p);
    }
    logger("Random\n");
    return p;    
}

void dumpGrid(Board9 *board, DispGrid grid) {
    
    for (int c=0;c<9;c++) {
        int cx = c % 3;
        int cy = c / 3;
        
        Board3 mb = B3(board->cell[c]);
    
        for (int x=0;x<3;x++) {
            for (int y=0;y<3;y++) {
                char ch = '.';
                if ( mb.p[0] & (mask(x, y))) {  ch = 'X';  }
                if ( mb.p[1] & (mask(x, y))) {  ch = 'O';  }
                grid[cx*3 + x][cy*3+y] = ch;
    
            }
        }
    }   
}

Pos evaluateMovesShallowTimed(Board9* board, Moves2 *valid_moves ) {

    Pos best_move = {};
    int best_iscore = -1000;    
    Score scores[81] = {};
    Pos moves[81] = {};
    int current_depth = 1;
    int finished = false;

    //sortMoves(board, valid_moves, 0);

    while (!finished) {
        clearHM(map);
        double loop_start = getElaspedTime();
        logger("\n<d:%d>",current_depth);
        Pos move;
        u16 cell, bit;
        int i = 0;
        resetMoveIt(valid_moves);
        while ( moveNextCB(valid_moves, &cell, &bit) ) { 
            move = cell2pos(cell,bit);
            moves[i] = move;
            logger("(%d,%d)=",move.x,move.y);
            Score sc = evaluateShallow(*board, 0, cell, bit, current_depth, true );            
            if (sc.p0 == 0xFF && sc.p1 == 0xFF) {
                finished = true;
                logger("[T/O]i=%d\n",i);
                break;
            } else {
                logger("[%d,%d]", sc.p0 , sc.p1);
                scores[i] = sc;
            }

            double now = getElaspedTime();
            double loop_so_far = now - loop_start;
            double avg_time = loop_so_far / ((double)(i+1));
            if ( (now + avg_time) > MAX_TIME) {
                logger("Timeout@%d, i=%d/%d now:%f loopsofar:%f avg:%f\n", current_depth, i, valid_moves->count, now, loop_so_far, avg_time);
                finished = true;
                break;
            }   
            i++;
        }

        int all_moves_above_100 = true;
        i = 0;
        while (i < valid_moves->count && all_moves_above_100 ) {
            all_moves_above_100 = ( all_moves_above_100 &  (( scores[i].p0 > 100) || ( scores[i].p1 > 100) ) );
            i++;
        }

        if (all_moves_above_100) {
            logger("All moves lead to a 'real' win\n");
            finished = true;
        }
        //printMetrics(map);       
        //logger("depth=%d\n", current_depth);
        //finished = true;
        current_depth += 2;
        if (current_depth == 7) finished = true;
       // printMetrics(map); 
    }

    for ( int i=0;i<valid_moves->count;i++ ) {
        int s = scores[i].p0 - scores[i].p1;
        if ( s > best_iscore) { 
            best_iscore = s;            
        }
    }

    Moves best_moves = {0};
    for ( int i=0;i<valid_moves->count;i++ ) {
        int s = scores[i].p0 - scores[i].p1;
        if (s == best_iscore)
            push(&best_moves, moves[i]);
    }

    if (best_moves.count == 0) {
        error("Something went wrong, no best moves\n");
    }

    if (best_moves.count == 1) {
        best_move = best_moves.moves[0];
    } else {
        best_move = best_moves.moves[rand() % best_moves.count];
    }

    logger("FINISH::(%d,%d) sc[%d] elapsed=%f\n",best_move.x,best_move.y,best_iscore, getElaspedTime());

    return best_move;
}

int isCellInDeadGrid(Board9 *board, int x, int y) {
    return (board->overall_free & mask(x/3, y/3));
}

void printCell(Board9* board, int x, int y, char c) {
    if (isCellInDeadGrid(board,x,y)) {
        if ( (c == 'X' && isCellInPlayerWon(board,x,y,0) ) || (c == 'O' && isCellInPlayerWon(board,x,y,1)) ){
            fprintf(stderr,"\e[32m%c \e[0m", c);
        } else {
            fprintf(stderr,"\e[2m%c \e[0m", c);
        }
    } else {
        fprintf(stderr, "%c ", c);
    }
}

int moveIn(int x, int y, Moves2 *moves) {
    if (moves == NULL || moves->count == 0) return 0;
    Pos p = {x,y};
    u16 cell,mask;
    pos2cell(p,&cell,&mask);
    return ( moves->mask[cell] & mask);
}

void printBoard(Board9 *board, Pos move, Moves2 *valid_moves) {
    DispGrid grid = {};
    dumpGrid(board, grid);

    for (int y=0;y<9;y++) {
        for (int x=0;x<9;x++) {
            if (moveIn(x,y,valid_moves)  || ( move.x == x && move.y == y )) {
                fprintf(stderr, "\e[31m%c \e[0m",grid[x][y]);
            }
            else {
                printCell(board,x,y,grid[x][y]);
                }            
            if (x%3 == 2) fprintf(stderr, "  ");
        }
        fprintf(stderr,"\n");
        if (y%3 == 2) fprintf(stderr,"\n");
    }
    //Score sc = scoreBoard(*board);
    fprintf(stderr, "------------------------------\n");
}

Pos getMove(Board9 *board, Pos last_move, Moves2 *valid_moves)
{
 
    if (last_move.x != -1)
    {
        set9(board, last_move.x, last_move.y, 1);
    }
    Pos my_move = {};

    spaces_left = calcPlayable(board);
    
    if ((spaces_left <= 18) || (spaces_left < 20 && valid_moves->count < 10))
    {
        logger("Minimax:");
        current_task = Minimax;
        my_move = evaluateMovesMM(board, valid_moves);
        if (my_move.x == 0xFF)
        {
            my_move = evaluateMovesShallowTimed(board, valid_moves);
        }
    }
    else if (spaces_left < 72)
    {
        current_task = Shallow;
        logger("ShallowTimed:");
        my_move = evaluateMovesShallowTimed(board, valid_moves);
    }
    else if ((spaces_left == 81) && (forced_first_move.x != -1) )
    {
        my_move = forced_first_move;        
    }
    else
    {
        current_task = Heuristic;
        logger("Heuristic: moves:%d free:%d:",valid_moves->count, spaces_left);
        my_move = evaluateMoves(board, valid_moves);
        //logger("<%d,%d>",my_move.x,my_move.y);
    }

    if (p0_log)
    {
        Board9 copy_board = *board;
        set9Simple(&copy_board, my_move.x, my_move.y, 0);
        printBoard(&copy_board, my_move, valid_moves);        
    }

    set9(board, my_move.x, my_move.y, 0); // real move

    return my_move;
}

Board9 loadBoard(char* file_name, Pos* last_move) {

    Board9 board = {};
    board.winner = -1;
    char *line = malloc(512);
    size_t len = 512;
    
    FILE* file = fopen (file_name, "r");
    for (int y=0;y < 9; y++) {
        ssize_t s = getline(&line, &len, file);
        if (s == 0) fprintf(stderr, "bad line\n");
        int x = 0;
        int p = 0;
        while (line[p] !=0 && x < 9) {
            switch (line[p])
            {
            case 'X':
                set9Simple(&board,x++,y,0);
                break;

            case 'O':
                set9Simple(&board,x++,y,1);
                break;

            case '.':
                x++;
                break;
            }
            p++;
        }
    }
    
    for (int i=0;i<9;i++) {
        Evaluation ev = evalMAC1(board.cell[i]);
        if (ev.p3[0] > 0) {
            set(&board.overall,i%3, i/3,0);
        }

        if (ev.p3[1] > 0) {
            set(&board.overall,i%3, i/3,1);
        }

        Board3 b3 = B3( board.cell[i] );
        u16 comb = b3.p[0] | b3.p[1];
        if (countFree(comb) == 0) {
            board.overall_free = board.overall_free | (1 << i);
        }
    }

    //Pos last_move = {0};
    if ( fscanf(file, "%d%d", &last_move->x, &last_move->y) < 2) {
        error("Failed to read last move\n");
    }
    fclose(file);
    free(line);

    return board;

}

void testRig() {
    Pos last_move;
    Board9 board = loadBoard("/home/paul/tictac/initboard.txt",&last_move);

    if (board.winner > -1) {
        logger("Board is already won\n");
        exit(0);
    }

    //__m128i st =  _mm_set_epi16 (1, 2, 4, 8, 16, 32, 64, 128 );

    //st = _mm_hadd_epi16(st, st);
    //st = _mm_hadd_epi16(st, st);
    //st = _mm_hadd_epi16(st, st);
    //st = _mm_hadd_epi16(st, st);

    //u16 res[8];
    //_mm_store_si128((void*) res, st); 
    //_mm_storeu_si64((void*) &total1, st);
    //fprintf(stderr,"Intrinsic add test %x %x\n",res[0], res[1]);

    start_time = get_gtod_clock_time();
    for (u16 i=0;i<POSS_BOARDS;i++) {
        Board3 b3 = B3(i);
        //u16 i2 =board3u16i(b3);
        //if (i != i2) {
        //    fprintf(stderr, "i2 Mismatch %u %u\n",i,i2);
        //}

        u16 i3 = B3_2_U16(b3);
        if (i != i3) {
            fprintf(stderr, "i3 Mismatch %u %u\n",i,i3);
        }

    }
    fprintf(stderr,"Time taken for boards %f\n",getElaspedTime());

    Moves valid_moves = {0};
    validMoves(&board, &valid_moves, last_move.x%3, last_move.y%3);

    Moves2 valid_moves2 = {0};
    validMoves2(&board, &valid_moves2, last_move.x%3, last_move.y%3);

    Moves2 valid_moves3 = {0};

    for (int i=0;i<valid_moves.count;i++) {
        pushMove(&valid_moves3, valid_moves.moves[i]);
    }

    if (valid_moves2.count != valid_moves3.count) {
        fprintf(stderr,"Counts differ\n");
    }

    for (int i=0;i<9;i++) {
        if (valid_moves2.mask[i] != valid_moves3.mask[i]) {
            fprintf(stderr,"Masks differ pos %d\n",i);
        }
    }

    resetMoveIt(&valid_moves2);
    resetMoveIt(&valid_moves3);

    Pos mv1 = {0};
    Pos mv2 = {0};
    u16 cell = {0};
    u16 bit = {0};
    while ( moveNext(&valid_moves2,&mv1) ) {
        moveNextCB(&valid_moves3,&cell,&bit);
        mv2 = cell2pos(cell, bit);
        ASSERT ((mv1.x == mv2.x && mv1.y == mv2.y),"moveNext versions differ");
    }
    
    fprintf(stderr,"moveNext compare OK\n");

    if (valid_moves2.count != valid_moves.count) {
        fprintf(stderr, "count mismatch %d %d\n",valid_moves.count,valid_moves2.count);    
        Pos p;
        while ( moveNext(&valid_moves2,&p) ) {
            fprintf(stderr, "(%d,%d)",p.x, p.y);        
        }
    }

    printBoard(&board,last_move, &valid_moves2);
    start_time = get_gtod_clock_time();
    p0_log = true;

    //Pos m = getMove(&board, last_move, &valid_moves);
    resetMoveIt(&valid_moves2);
    
    //Pos m = evaluateMovesMM2(&board, &valid_moves2);
    Pos m = evaluateMovesShallowTimed(&board, &valid_moves2);
    double tt = getElaspedTime();
    double sps = (double)mm_score_count / tt;
    fprintf(stderr,"Move:%d %d(time %1.5f sps:%7.1f\n",m.x,m.y,tt,sps); 
    printEvalCounts();

}

int main(int argc,char* argv[])
{
    // Init

#ifdef CG_GAME
    fprintf(stderr, "Running in CodinGame\n"); 
#endif

    p0_log = 0;
    initBoardCaches();

    Board9 p0_board = {0};
    //memset(&p0_board, 0, sizeof(p0_board)); 
    p0_board.winner = -1;
    map = createHM(128000,512000);
    //testRig();
    //exit(0);

    if (argc > 1) {

        int arg = 1;

        while (arg < argc) {
            char* arg_str = argv[arg];
            arg++;

            if (arg_str[0] == '-' && arg_str[1] == 'l') {
                p0_log = 1;    
            }

            if (arg_str[0] == '-' && arg_str[1] == 'w') {
                loadWeights("weights.txt");
            }

            if (arg_str[0] == '-' && arg_str[1] == 'f') {
                sscanf(&arg_str[2],"%d %d", &forced_first_move.x, &forced_first_move.y);                         
            }

            if (arg_str[0] == '-' && arg_str[1] == 't') {
                testRig();
                return 0;
            }
        }

    }
    
    srand(time(NULL)); // Initialization, should only be called once.   
    //srand(12345);
    int pid = getpid(); 
    int t = 0;
    for (int i=0;i<pid % 17;i++) {
        t += rand() % 2;
    }
    // game loop
    while (1) {
        Pos last_move;
        Moves2 valid_moves = {0};

        int i = scanf("%d%d", &last_move.y, &last_move.x);
        if (i < 2) error("Failed to read last move\n");
        
        if (last_move.x == -2) exit(0);  // Internal not for codinGame 
        start_time = get_gtod_clock_time();
        
        int valid_action_count;
        if (scanf("%d", &valid_action_count) < 1) error("Fail to read action_count\n");
        for (int i = 0; i < valid_action_count; i++) {
            int row;
            int col;
            if ( scanf("%d%d", &row, &col) < 2) error("failed to read a move\n");
            Pos vm = {col,row};
            pushMove(&valid_moves, vm);
        }
        if ( valid_moves.count != valid_action_count)
            fprintf(stderr, "Move counts don't match\n");
        
        Pos my_move = getMove(&p0_board, last_move, &valid_moves);
        
        printf("%d %d\n", my_move.y, my_move.x);
        fflush(stdout);
    }
    destroyHM(map);    
    exit(0);
    return 0;
}
