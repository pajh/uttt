//#pragma GCC optimize("O3,inline")
#pragma GCC optimize "O3,omit-frame-pointer,inline"
#pragma GCC target("bmi,lzcnt,popcnt")

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

typedef char DispGrid[9][9];

// Globals
int p0_log;
Board9 p0_board;
HashMap* map;
int spaces_left;

void printBoard(Board9 *board, Pos move, Moves *valid_moves);

uint64_t start_time;

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

void error(const char *format, ...)
{    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);    
    va_end(args);
    fprintf(stderr, "TERMINATING\n");

    exit(-1);
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
    Evaluation ev = evaluate(oboard, 0);
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
                Evaluation ev = evaluate(tboard.cell[pos(x, y)], 0);
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

void evaluateMove(int move, Board9 tboard, Moves *valid_moves, MoveEvaluation *move_eval)
{
    int x = valid_moves->moves[move].x;
    int y = valid_moves->moves[move].y;

    Evaluation prev_ev = evaluate(tboard.cell[pos(x/3, y/3)], 0);
    Evaluation ev = set9(&tboard, x, y, 0);
    move_eval->my_move[move] = ev;
    move_eval->blocks_op_2[move] = (ev.p2[1] < prev_ev.p2[1]);
    
    move_eval->my_delta[move].p1[0] = ev.p1[0] - prev_ev.p1[0];
    move_eval->my_delta[move].p2[0] = ev.p2[0] - prev_ev.p2[0];
    move_eval->my_delta[move].p3[0] = ev.p3[0] - prev_ev.p3[0];
    move_eval->my_delta[move].p1[1] = ev.p1[1] - prev_ev.p1[1];
    move_eval->my_delta[move].p2[1] = ev.p2[1] - prev_ev.p2[1];
    move_eval->my_delta[move].p3[1] = ev.p3[1] - prev_ev.p3[1];

    move_eval->my_overall[move] = evaluate(tboard.overall, tboard.overall_free);

    int ox = x % 3;
    int oy = y % 3;

    if ((tboard.overall_free & mask(ox, oy)) == 0)
    {
        move_eval->op_cell[move] = evaluate(tboard.cell[pos(ox, oy)], 0);
        move_eval->op_multi[move] = 0;
        move_eval->op_next_move[move].x = ox;
        move_eval->op_next_move[move].y = oy;
        move_eval->op_win[move] = 0;
        if (move_eval->op_cell[move].p2[1] > 0)
        { // Op can win in this square as they have 2 + blank already
            move_eval->op_win[move] = doesOpWin(tboard.overall, ox, oy);
        }
    }
    else
    {
        Pos p = {};
        int op_win = 0;
        move_eval->op_cell[move] = findOpBest(tboard, &p, &op_win);
        move_eval->op_multi[move] = 1;
        move_eval->op_next_move[move] = p;
        move_eval->op_win[move] = op_win;
    }
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

    Evaluation ev_orig = evaluate(board->overall, board->overall_free);
    for (int i=0;i<valid_moves->count;i++) {
        Board9 tboard = *board;
        set9(&tboard,valid_moves->moves[i].x,valid_moves->moves[i].y, player);
        Evaluation ev = evaluate(tboard.overall, tboard.overall_free);

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
    int other_player = (player == 0) ? 1: 0;

    set9(&board,p.x, p.y, player);
    if (board.winner > -1) {
        if (board.winner == 0) return -1;
        if (board.winner == 1) return 1;
        if (board.winner == 2) return 0;        
    }

    board.cell[9] = player;
    u32 data = 0;

    if ( findHMEntry(map,(unsigned char*) board.cell, &data)) {
        int score = data;
        return score -1;
    }

    Moves valid_moves = {};
    validMoves( &board, &valid_moves, p.x % 3, p.y % 3 );

    if (valid_moves.count == 0) {
        fprintf(stderr,"Something wrong, no valid moves\n");
        exit(0);
    }
    
    sortMoves(&board, &valid_moves, other_player);

    int max = -10;
    int min = 10;
    for (int i=0;i<valid_moves.count;i++) {
        int score = scoreMoveMM(board, valid_moves.moves[i], other_player, depth+1);

        if (score > max) max = score;
        if (score < min) min = score;
        if ( (other_player == 0 && score == -1) || ( other_player == 1 && score == 1) ) break;
    }
    int final_score = (other_player == 0) ? min : max;

    int store_score = final_score + 1;
    addHMEntry(map, (unsigned char*) board.cell, store_score);

    return final_score;
}

Pos evaluateMovesMM( Moves *valid_moves ) {  
      
    int best = 10;
    int best_move = 0;
    sortMoves(&p0_board, valid_moves,0);
    for (int i=0;i<valid_moves->count;i++) {        
        int score = scoreMoveMM(p0_board,valid_moves->moves[i],0,0);
        if (score < best) {
            best = score;
            best_move = i;            
            if (best == -1) break;
        }
    }
    clearHM(map);
    logger("[MMEND %d]\n",best);
    return valid_moves->moves[best_move];
}

typedef struct Score_s {
    u16 p0;
    u16 p1;
} Score;

Score scoreBoard(Board9 board) {

    Score s = {0,0};
    Evaluation ev_o = evaluate(board.overall,board.overall_free);    

    if (board.winner == 2) {
        return s;
    }

    s.p0 += ev_o.p3[0] * 100;
    s.p1 += ev_o.p3[1] * 100;

    s.p0 += ev_o.p2[0] * 30;
    s.p1 += ev_o.p2[1] * 30;

    s.p0 += ev_o.p1[0] * 10;
    s.p1 += ev_o.p1[1] * 10;

    u16 p0_2s = board.overall;
    u16 p1_2s = board.overall;

    u16 p0_free = board.overall_free;
    u16 p1_free = board.overall_free;

   for (int x = 0; x < 3; x++) {
        for (int y = 0; y < 3; y++) {
            if ((board.overall_free & mask(x, y)) == 0) {
                Evaluation ev = evaluate(board.cell[pos(x, y)], 0);

                if (ev.p2[0] > 0) {                    
                    set(&p0_2s,x,y,0);
                    p0_free = (p0_free | mask(x,y));
                }

                if (ev.p2[1] > 0) {
                    set(&p1_2s,x,y,1);
                    p1_free = (p1_free | mask(x,y));
                }

                s.p0 += ev.p2[0] * 3;
                s.p1 += ev.p2[1] * 3;

                s.p0 += ev.p1[0] * 1;
                s.p1 += ev.p1[1] * 1;
            }
        }
    }

    Evaluation p0_ev = evaluate(p0_2s, p0_free);
    Evaluation p1_ev = evaluate(p1_2s, p1_free);
    
    s.p0 += p0_ev.p3[0] * 7;
    if (p0_ev.p2[0] > ev_o.p2[0]) {
        s.p0 += ( p0_ev.p2[0] - ev_o.p2[0]) * 3;
    }

    s.p1 += p1_ev.p3[1] * 7;
    if (p1_ev.p2[1] > ev_o.p2[1]) {
        s.p1 += (p1_ev.p2[1]  - ev_o.p2[1]) * 3;
    }

    return s;
}

Score evaluateShallow(Board9 board, int player, Pos move, int depth, int timed) {

    int next_player = (player == 0) ? 1: 0;

    if (timed && depth == 0) {
        double elapsed = getElaspedTime();
        if (elapsed > 0.0950) {
            Score timeout = {0xFFFF,0xFFFF};
            //logger("internal timeout (%d,%d) depth:%d\n",move.x, move.y,depth);
            return timeout;
        }
    }

    set9(&board, move.x, move.y,player);

    //if (depth == 0 && player != 1) fprintf(stderr,"weird\n");
    
    board.cell[9] = player;
    u32 data = 0;

    if ( findHMEntry(map,(unsigned char*) board.cell, &data)) {
        Score sc = { data & 0xFFFF, data >> 16};   
        return sc;
    }

    if ( depth == 0 || board.winner > -1) {
        Score sc = scoreBoard(board);
        data = (sc.p1 << 16) | sc.p0;
        addHMEntry(map, (unsigned char*) board.cell, data);        

        return sc;
    }

    Moves valid_moves;        
    validMoves( &board, &valid_moves, move.x % 3, move.y % 3 );
    //sortMoves(&board, &valid_moves, next_player);

    if (valid_moves.count == 0) {
        fprintf(stderr,"ERROR BOARD>>> winner:%d\n",board.winner);
        printBoard( &board, move, &valid_moves );
        error("No valid moves\n");
    }
    
    int besti = -1000;
    Score bests = {};

    int new_depth = depth - 1;

    for (int i=0;i<valid_moves.count;i++) {

        Score score = evaluateShallow(board,next_player,valid_moves.moves[i], new_depth, timed );

        if (score.p0 == 0xFFFF && score.p1 == 0xFFFF) return score; // timeout

        int iscore = (next_player == 0) ? (score.p0 - score.p1) : (score.p1 - score.p0);
        if (iscore > besti) {
            besti = iscore;
            bests = score;              
        }
    }

    data = (bests.p1 << 16) | bests.p0;
    addHMEntry(map, (unsigned char*) board.cell, data);

    return bests;
}

Pos evaluateMoves( Moves *valid_moves )
{
    MoveEvaluation move_eval = {};
    Moves buffer = {};

    if (valid_moves->count == 0)
    {
        logger("Error no valid moves\n");
        exit(0);
    }

    // Only 1 move just pick it and we're done
    if (valid_moves->count == 1)
    {
        logger("Only 1 move\n");
        return valid_moves->moves[0];        
    }

    for (int i = 0; i < valid_moves->count; i++)
    {
        evaluateMove(i, p0_board, valid_moves, &move_eval);
        if (move_eval.my_overall[i].p3[0] > 0)
        {
            logger("Winning move\n");
            return valid_moves->moves[i]; // it's a winner
        }
    }

    // So we can't win whole game so find good moves

    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p3[0] > 0 && move_eval.op_cell[i].p2[1] == 0)
        {
            logger("Winning a square op doesn't win 1 next turn\n");
            return valid_moves->moves[i]; // win a sqaure without giving op one next go
        }        
    }

    // get me a '2' 
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p2[0] > 0 && evalCheck(move_eval.op_cell[i] ,">1","=0","=0","=0" ) )
        {
            logger("Give me a 2 with oponent into zero for him where I have 1 but no 2\n");
            return valid_moves->moves[i]; 
        }        
    }

    // get me a '2' putting oponent into a zero
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p2[0] > 0 && evalCheck(move_eval.op_cell[i] ,"=0","=0","=0","=0" ) )
        {
            logger("Give me a 2 with oponent going into an empty grid 2\n");
            return valid_moves->moves[i]; 
        }        
    }

    // get me a '2' 
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p2[0] > 0 && evalCheck(move_eval.op_cell[i] ,">0","=0",">0","=0" ) )
        {
            logger("Give me a 2 with oponent going into a 1/1 that doesn't let him kill a 2\n");
            return valid_moves->moves[i]; 
        }        
    }

    // get me a '2' 
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p2[0] > 0 && evalCheck(move_eval.op_cell[i] ,"*","=0","*","=0" ) )
        {
            logger("Give me a 2 with oponent going into a 1 for him that doesn't let him kill a 2\n");
            return valid_moves->moves[i]; 
        }        
    }    

    for (int i = 0; i < valid_moves->count; i++) {
        if (move_eval.blocks_op_2[i] ) {
            logger("Blocking moves exist\n");
            break;
        }
    }

    // Blocks op's 2
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.blocks_op_2[i] && evalCheck(move_eval.op_cell[i] ,"*","=0","*","=0" ) )
        {
            logger("Block op 2 without giving him a sqaure or letting him kill one of mine\n");
            return valid_moves->moves[i]; 
        }        
    }

    // Blocks op's 2
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.blocks_op_2[i] && evalCheck(move_eval.op_cell[i] ,"*",">1","*","=0" ) )
        {
            logger("Block op 2 without giving him a square but let him block a double [%d,%d] multi:%d\n",move_eval.op_next_move[i].x,move_eval.op_next_move[i].y,move_eval.op_multi[i]);
            return valid_moves->moves[i]; 
        }        
    }

    // 1s
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_delta[i].p1[0] > 0 && evalCheck(move_eval.op_cell[i] ,">0","=0","=0","=0" )) push(&buffer, valid_moves->moves[i]);        
    }

    if (buffer.count > 0) {
            logger("RANDOM Gained a 1 with oponent going into a sqaure I have a 1 in.\n");
            return pickRandomFromBuffer( &buffer );
    }

    // -----------------------------------------------------------------------------------------------------------
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_delta[i].p1[0] > 0 && evalCheck(move_eval.op_cell[i] ,">0","=0",">0","=0" ) ) push(&buffer,valid_moves->moves[i]);
    
    }
    if (buffer.count > 0) {
            logger("RANDOM: Gained a 1 with oponent going into a square both of us has 1\n");
            return pickRandomFromBuffer( &buffer ) ;
    }        

    // -----------------------------------------------------------------------------------------------------------
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_delta[i].p1[0] > 0 && evalCheck(move_eval.op_cell[i] ,"=0","=0","=0","=0" )) push(&buffer, valid_moves->moves[i]);
    }

    if (buffer.count > 0) {
            logger("RANDOM: Gained a 1 with oponent going into an empty square\n");
            return pickRandomFromBuffer( &buffer );
        }        

    // ---------------------------------------------------------------------------------------------------------------
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_delta[i].p1[0] > 0 && move_eval.op_cell[i].p2[1] == 0 && move_eval.op_cell[i].p1[1] == 0)
        {
            //p0_log = 1;
            logger("****Gained a 1 with oponent going into a zero for him in (%d,%d)\n",move_eval.op_next_move[i].x,move_eval.op_next_move[i].y);            
            return valid_moves->moves[i]; 
        }        
    }

    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p3[0] > 0 &&  move_eval.blocks_op_2 && !move_eval.op_win ) 
        {
            logger("Take a 3 that blocks his 2 but give him a square but not let him win[experimental]\n");
            return valid_moves->moves[i]; 
        }        
    }

    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p1[0] > 0 && evalCheck(move_eval.op_cell[i] ,"*","*","*","=0" ))
        {
            logger("Don't play into a 2\n");
            return valid_moves->moves[i]; 
        }        
    }

    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p3[0] > 0 && !move_eval.op_win[i])
        {
            logger("Take a square even if it gives him one but he doesn't win\n");
            return valid_moves->moves[i]; 
        }        
    }

    for (int i = 0; i < valid_moves->count; i++)
    {
        if ( evalCheck(move_eval.op_cell[i] ,"*","*","*","=0" ) )
        {
            logger("Give him my 2 rather than give him a 3\n");
            return valid_moves->moves[i]; 
        }        
    }

    for (int i = 0; i < valid_moves->count; i++)
    {
        if (!move_eval.op_win[i] && move_eval.blocks_op_2[i] )
        {
            logger("Prevent him winning but grab a block\n");
            return valid_moves->moves[i]; 
        }        
    }

    for (int i = 0; i < valid_moves->count; i++)
    {
        if (!move_eval.op_win[i] && move_eval.my_delta[i].p2[0] > 0 )
        {
            logger("Prevent him winning but grab a 2\n");
            return valid_moves->moves[i]; 
        }        
    }

    for (int i = 0; i < valid_moves->count; i++)
    {
        if (!move_eval.op_win[i])
        {
            logger("Prevent him winning\n");
            return valid_moves->moves[i]; 
        }        
    }

    //Random
    int chosen_move = rand() % valid_moves->count;
    logger("Random\n");
    return valid_moves->moves[chosen_move];    
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

Pos evaluateMovesShallow( Moves *valid_moves ) {

    Pos best_move = {};
    int best_iscore = -1000;
    Score best_score = {};
    sortMoves(&p0_board, valid_moves, 0);
    
    for ( int i=0;i<valid_moves->count;i++ ) {
        int depth = (valid_moves->count > 9) ? 3 : 5;
        Score sc = evaluateShallow(p0_board,0,valid_moves->moves[i], depth, false );
        int s = sc.p0 - sc.p1;
        //logger("[%d/%d]",i+1,valid_moves->count);
        //logger("(%d,%d)=[%d,%d] ",valid_moves->moves[i].x, valid_moves->moves[i].y, sc.p0,sc.p1);
        if ( s > best_iscore) { 
            best_iscore = s;
            best_score = sc;
            best_move = valid_moves->moves[i];    
            //if (best_score.p0 >= 100) break;
        }
    }
    logger("FINISH::(%d,%d) sc[p0:%d,p1:%d]\n",best_move.x,best_move.y,best_score.p0,best_score.p1);
    //printMetrics(map);
    //clearHM(map);
    return best_move;
}

Pos evaluateMovesShallowTimed( Moves *valid_moves ) {

    Pos best_move = {};
    int best_iscore = -1000;
    Score best_score = {};
    Score scores[81] = {};
    int current_depth = 1;
    int finished = false;

    sortMoves(&p0_board, valid_moves, 0);


    while (!finished) {
        clearHM(map);
        double loop_start = getElaspedTime();
        logger("<d:%d>",current_depth);
        for (int i = 0 ; i < valid_moves->count ; i++ ) { 
            logger("%d,%d=",valid_moves->moves[i].x,valid_moves->moves[i].y);
            Score sc = evaluateShallow(p0_board,0,valid_moves->moves[i], current_depth, true );            
            if (sc.p0 == 0xFFFF && sc.p1 == 0xFFFF) {
                finished = true;
                logger("(timed out)");
                break;
            } else {
                logger("%d(%d %d) || ", sc.p0 -sc.p1, sc.p0, sc.p1);
                scores[i] = sc;
            }

            double now = getElaspedTime();
            double loop_so_far = now - loop_start;
            double avg_time = loop_so_far / ((double)(i+1));
            if ( (now + avg_time) > 0.0950) {
                logger("\nTimeout depth=%d, i=%d/%d now:%f loopsofar:%f avg:%f\n", current_depth, i, valid_moves->count, now, loop_so_far, avg_time);
                finished = true;
                break;
            }   
        }
        //printMetrics(map);
        clearHM(map);
        //logger("depth=%d\n", current_depth);
        //finished = true;
        current_depth += 2;
    }

    for ( int i=0;i<valid_moves->count;i++ ) {
        int s = scores[i].p0 - scores[i].p1;
        if ( s > best_iscore) { 
            best_iscore = s;
            best_score = scores[i];
            best_move = valid_moves->moves[i];    
        }
    }
    logger("FINISH::(%d,%d) sc[p0:%d,p1:%d] elapsed=%f\n",best_move.x,best_move.y,best_score.p0,best_score.p1, getElaspedTime());

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

int moveIn(int x, int y, Moves *moves) {
    if (moves == NULL || moves->count == 0) return 0;

    for (int i=0;i<moves->count;i++) {
        if (moves->moves[i].x == x && moves->moves[i].y == y) return 1;
    }
    return 0;
}

void printBoard(Board9 *board, Pos move, Moves *valid_moves) {
    DispGrid grid = {};
    dumpGrid(board, grid);

    for (int y=0;y<9;y++) {
        for (int x=0;x<9;x++) {
            if (moveIn(x,y,valid_moves)) {
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
    Score sc = scoreBoard(*board);
    fprintf(stderr, "----------------------[0:%d,1:%d]\n",sc.p0, sc.p1);
}


Pos getMove(Pos last_move, Moves *valid_moves)
{
    if (last_move.x != -1)
    {
        set9(&p0_board, last_move.x, last_move.y, 1);
    }    
    Pos my_move = {};

    spaces_left = calcPlayable(&p0_board);
    logger("[FREE %d]", spaces_left);
    if ( spaces_left < 20) {
        logger("Minimax:");
        my_move = evaluateMovesMM(valid_moves);
    } else {
        if (spaces_left < 72) {
            logger("ShallowTimed:\n");
            my_move = evaluateMovesShallowTimed(valid_moves);
        } else {
            logger("Heuristic:");
            my_move = evaluateMoves(valid_moves);
        }
    }

    //my_move = evaluateMovesShallow(valid_moves);
    set9(&p0_board, my_move.x, my_move.y, 0);
 
    if (p0_log) printBoard(&p0_board, my_move, valid_moves);
    return my_move;
}

int main(int argc,char* argv[])
{
    // Init

#ifdef CG_GAME
    fprintf(stderr, "Running in CodinGame\n"); 
#endif

    p0_log = 0;
    initB3Cache();

    Evaluation ev = { {127,127},{127,127},{127,127} };
    for (int i=0;i<POSS_BOARDS;i++) {
        ev_cache[i] = ev;
    }

    if (argc > 1) {
        char* arg_1 = argv[1];
        if (arg_1[0] == '-' && arg_1[1] == 'l') {
            p0_log = 1;    
        }
    }

    memset(&p0_board, 0, sizeof(p0_board)); 
    p0_board.winner = -1;
    map = createHM(64000,256000);

    /*
    set9(&p0_board,3,3,1);
    set9(&p0_board,4,4,1);
    set9(&p0_board,5,5,1);
    set9(&p0_board,7,7,0);

    Moves valid_moves = {0};
    Pos p = {1,1};
    push(&valid_moves,p);

    printBoard(&p0_board,p, &valid_moves);
    exit(0);*/
    
    //srand(time(NULL)); // Initialization, should only be called once.   
    srand(12345);
    /*int pid = getpid(); 
    int t = 0;
    for (int i=0;i<pid % 109;i++) {
        t += rand() % 2;
    }*/
    // game loop
    while (1) {
        Pos last_move;
        Moves valid_moves = {};

        scanf("%d%d", &last_move.y, &last_move.x);
        
        if (last_move.x == -2) exit(0);  // Internal not for codinGame
        start_time = get_gtod_clock_time();
        
        int valid_action_count;
        scanf("%d", &valid_action_count);
        for (int i = 0; i < valid_action_count; i++) {
            int row;
            int col;
            scanf("%d%d", &row, &col);
            valid_moves.moves[i].x = col;
            valid_moves.moves[i].y = row;            
        }
        valid_moves.count = valid_action_count;
        
        Pos my_move = getMove(last_move, &valid_moves);
        
        printf("%d %d\n", my_move.y, my_move.x);
        fflush(stdout);
    }
    destroyHM(map);    
    return 0;
}
