#pragma GCC optimize("O3,inline")
#pragma GCC target("bmi,lzcnt,popcnt")

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"

typedef struct MoveEvaluation_s
{
    Evaluation my_move[81];
    Evaluation op_cell[81];
    Evaluation my_overall[81];
    int op_win[81];
    int op_multi[81];
    Pos op_next_move[81];
    int blocks_op_2[81];

} MoveEvaluation;

int calcPlayable(Board9 *board) {
    int free = 0;
    for (int x=0;x<3;x++) {
        for (int y=0;y<3;y++) {
            if ( (board->overall_free & mask(x, y)) == 0) {
                Board3 sb = board->cell[x+y*3];
                free += countFree(sb.p[0] | sb.p[1]);
            }
        }
    }
    return free;
}

typedef char DispGrid[9][9];

void dumpGrid(Board9 *board, DispGrid grid) {
    
    for (int c=0;c<9;c++) {
        int cx = c % 3;
        int cy = c / 3;
        Board3 mb = board->cell[c];
    
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

int moveIn(int x, int y, Moves *moves) {
    for (int i=0;i<moves->count;i++) {
        if (moves->moves[i].x == x && moves->moves[i].y == y) return 1;
    }
    return 0;
}

int isCellInDeadGrid(Board9 *board, int x, int y) {
    return (board->overall_free & mask(x/3, y/3));
}

void printBoard(Board9 *board, Pos move, Moves *valid_moves) {
    DispGrid grid = {};
    dumpGrid(board, grid);
    for (int y=0;y<9;y++) {
        for (int x=0;x<9;x++) {
            if (moveIn(x,y,valid_moves)) {
                printf("\e[31m%c \e[0m",grid[x][y]);
            }
            else {
                if (isCellInDeadGrid(board,x,y)) {
                    printf("\e[2m%c \e[0m",grid[x][y]);
                } else {
                    printf("%c ",grid[x][y]);
                }
            }
            if (x%3 == 2) printf("  ");
        }
        printf("\n");
        if (y%3 == 2) printf("\n");
    }
    printf("----------------------\n");
}

Board9 p0_board;
int p0_log = 1;
int p0_moves_made = 0;

char get_symbol(Board3 board, int x, int y)
{
    u16 m = mask(x, y);
    if (board.p[0] & m)
        return 'X';
    if (board.p[1] & m)
        return 'O';
    return ' ';
}

void print(Board3 board)
{
    for (int row = 0; row < 3; row++)
    {
        printf(" %c | %c | %c \n", get_symbol(board, 0, row), get_symbol(board, 1, row), get_symbol(board, 2, row));
        if (row < 2)
            printf(" ---------\n");
    }
}



int scoreMoveMM(Board9 board, Pos p, int player, int depth) {
    //printf("scoreMM (%d,%d) p=%d depth=%d\n", p.x,p.y,player,depth++);
    set9(&board,p.x, p.y, player);
    if (board.winner > -1) {
        if (board.winner == 0) return -1;
        if (board.winner == 1) return 1;
        if (board.winner == 2) return 0;        
    }

    Moves valid_moves = {};
    validMoves( &board, &valid_moves, p.x % 3, p.y % 3 );
    if (valid_moves.count == 0) {
        printf("Something wrong, no valid moves\n");
        exit(0);
    }
    int next_player = (player == 0) ? 1: 0;
    int max = -10;
    int min = 10;
    for (int i=0;i<valid_moves.count;i++) {
        int score = scoreMoveMM(board, valid_moves.moves[i], next_player, depth);
        if (score > max) max = score;
        if (score < min) min = score;
        if ( (player == 0 && score == -1) || ( player == 1 && score == 1) ) break;
    }
    return (player == 0) ? min : max;
}

void play0InitGame()
{
    memset(&p0_board, 0, sizeof(p0_board));
    p0_board.winner = -1;
    p0_moves_made = 0;
}

int doesOpWin(Board3 oboard, int x, int y)
{
    set(&oboard, x, y, 1);
    Evaluation ev = evaluate(oboard, 0);
    return (ev.p3[1] > 0);
}

Evaluation findOpBest(Board9 tboard, Pos *p)
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
                }
                int score = win_bonus + ev.p3[1] * 10 + ev.p2[1] * 7 + ev.p1[1] + ev.p2[0] * 4;
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

    move_eval->my_overall[move] = evaluate(tboard.overall, tboard.overall_free);

    int ox = x % 3;
    int oy = y % 3;

    if ((tboard.overall_free & mask(ox, oy)) == 0)
    {
        move_eval->op_cell[move] = evaluate(tboard.cell[pos(ox, oy)], 0);
        move_eval->op_multi[move] = 0;
        move_eval->op_next_move[move].x = ox;
        move_eval->op_next_move[move].y = oy;
    }
    else
    {
        Pos p = {};
        move_eval->op_cell[move] = findOpBest(tboard, &p);
        move_eval->op_multi[move] = 1;
        move_eval->op_next_move[move] = p;
    }
    move_eval->op_win[move] = 0;
    if (move_eval->op_cell[move].p2[1] > 0)
    { // Op can win in this square as they have 2 + blank already
        move_eval->op_win[move] = doesOpWin(tboard.overall, ox, oy);
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
        printf("WARNING INVALID DIGIT\n");
    return 0;
}
}
int evalCheck(Evaluation ev,char* p0_1, char* p0_2, char* p1_1, char* p1_2) {
    return ( evalDigit(ev.p1[0], p0_1) && evalDigit(ev.p2[0], p0_2) && evalDigit(ev.p1[1], p1_1) && evalDigit(ev.p2[1], p1_2) );
}

Pos evaluateMoves( Moves *valid_moves )
{
    MoveEvaluation move_eval = {};

    if (valid_moves->count == 0)
    {
        printf("Error no valid moves\n");
        exit(0);
    }

    // Only 1 move just pick it and we're done
    if (valid_moves->count == 1)
    {
        if (p0_log) printf("Only 1 move\n");
        return valid_moves->moves[0];        
    }

    for (int i = 0; i < valid_moves->count; i++)
    {
        evaluateMove(i, p0_board, valid_moves, &move_eval);
        if (move_eval.my_overall[i].p3[0] > 0)
        {
            if (p0_log) printf("Winning move\n");
            return valid_moves->moves[i]; // it's a winner
        }
    }

    // So we can't win whole game so find good moves

    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p3[0] > 0 && move_eval.op_cell[i].p2[1] == 0)
        {
            if (p0_log) printf("Winning a square op doesn't win 1 next turn\n");
            return valid_moves->moves[i]; // win a sqaure without giving op one next go
        }        
    }

    // get me a '2' putting oponent into a zero
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p2[0] > 0 && evalCheck(move_eval.op_cell[i] ,"=0","=0","=0","=0" ) )
        {
            if (p0_log) printf("Give me a 2 with oponent going into an empty grid 2\n");
            return valid_moves->moves[i]; 
        }        
    }

    // get me a '2' 
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p2[0] > 0 && evalCheck(move_eval.op_cell[i] ,"*","=0","=0","=0" ) )
        {
            if (p0_log) printf("Give me a 2 with oponent going into a zero for him that doesn't let him kill a 2\n");
            return valid_moves->moves[i]; 
        }        
    }

    // get me a '2' 
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p2[0] > 0 && evalCheck(move_eval.op_cell[i] ,">0","=0",">0","=0" ) )
        {
            if (p0_log) printf("Give me a 2 with oponent going into a 1/1 that doesn't let him kill a 2\n");
            return valid_moves->moves[i]; 
        }        
    }

    // get me a '2' 
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p2[0] > 0 && evalCheck(move_eval.op_cell[i] ,"*","=0","*","=0" ) )
        {
            if (p0_log) printf("Give me a 2 with oponent going into a 1 for him that doesn't let him kill a 2\n");
            return valid_moves->moves[i]; 
        }        
    }    

    for (int i = 0; i < valid_moves->count; i++) {
        if (p0_log && move_eval.blocks_op_2[i] ) {
            printf("Blocking moves exist\n");
            break;
        }
    }

    // Blocks op's 2
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.blocks_op_2[i] && evalCheck(move_eval.op_cell[i] ,"*","=0","*","=0" ) )
        {
            if (p0_log) printf("Block op 2 without giving him a sqaure or letting him kill one of mine\n");
            return valid_moves->moves[i]; 
        }        
    }

    // Blocks op's 2
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.blocks_op_2[i] && evalCheck(move_eval.op_cell[i] ,"*",">1","*","=0" ) )
        {
            if (p0_log) printf("Block op 2 without giving him a sqaure but let him block a double\n");
            return valid_moves->moves[i]; 
        }        
    }


    // get me a '1' putting oponent into a total zero
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p1[0] > 0 && evalCheck(move_eval.op_cell[i] ,"=0","=0","=0","=0" ))
        {
            if (p0_log) printf("Give me a 1 with oponent going into an empty square (%d,%d)\n",move_eval.op_next_move[i].x,move_eval.op_next_move[i].y);
            return valid_moves->moves[i]; 
        }        
    }

    // get me a '1' putting oponent into a square only I have a 1 in
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p1[0] > 0 && move_eval.op_cell[i].p2[1] == 0 && move_eval.op_cell[i].p1[1] == 0 && move_eval.op_cell[i].p2[0] == 0 && move_eval.op_cell[i].p1[0] > 0)
        {
            if (p0_log) printf("Give me a 1 with oponent going into a sqaure I have a 1 in (%d,%d)\n",move_eval.op_next_move[i].x,move_eval.op_next_move[i].y);
            return valid_moves->moves[i]; 
        }        
    }

    // get me a '1' putting oponent into a square we both have 1;s in
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p1[0] > 0 && move_eval.op_cell[i].p2[1] == 0 && move_eval.op_cell[i].p1[1] > 0 && move_eval.op_cell[i].p2[0] == 0 && move_eval.op_cell[i].p1[0] > 0)
        {
            if (p0_log) printf("Give me a 1 with oponent going into a sqaure both of us has 1 in (%d,%d)\n",move_eval.op_next_move[i].x,move_eval.op_next_move[i].y);
            return valid_moves->moves[i]; 
        }        
    }

        // get me a '1' putting oponent into a zero
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p1[0] > 0 && move_eval.op_cell[i].p2[1] == 0 && move_eval.op_cell[i].p1[1] == 0)
        {
            if (p0_log) printf("Give me a 1 with oponent going into a zero for him in (%d,%d)\n",move_eval.op_next_move[i].x,move_eval.op_next_move[i].y);
            return valid_moves->moves[i]; 
        }        
    }
    
    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p1[0] > 0 && evalCheck(move_eval.op_cell[i] ,"*","*","*","=0" ))
        {
            if (p0_log) printf("Don't play into a 2\n");
            return valid_moves->moves[i]; 
        }        
    }

    for (int i = 0; i < valid_moves->count; i++)
    {
        if (move_eval.my_move[i].p3[0] > 0)
        {
            if (p0_log) printf("Take a square even if it gives him one\n");
            return valid_moves->moves[i]; 
        }        
    }


    // Random
    int chosen_move = rand() % valid_moves->count;
    if (p0_log) printf("Random\n");
    return valid_moves->moves[chosen_move];    

}

Pos evaluateMovesMM( Moves *valid_moves ) {    
    int best = 10;
    int best_move = 0;
    for (int i=0;i<valid_moves->count;i++) {
        //printf("Scoring %d/%d\n",i,valid_moves->count);
        int score = scoreMoveMM(p0_board,valid_moves->moves[i],0,0);
        if (score < best) {
            best = score;
            best_move = i;            
            if (best == -1) break;
        }
    }
    if (p0_log) printf("Using MinMax [%d]\n",best);
    return valid_moves->moves[best_move];
}

Pos play0GetMove(int player, Pos last_move, Moves *valid_moves)
{
    if (player != 0)
    {
        printf("hey I'm player zero\n");
        exit(1);
    }

    if (last_move.x != -1)
    {
        set9(&p0_board, last_move.x, last_move.y, 1);
    }

    Pos my_move = {};
    if ( calcPlayable(&p0_board) < 16) {
        my_move = evaluateMovesMM(valid_moves);
    }
    else {
        my_move = evaluateMoves(valid_moves);
    }
    set9(&p0_board, my_move.x, my_move.y, 0);
    p0_moves_made++;

    if (p0_log) printBoard(&p0_board, my_move, valid_moves);
    return my_move;
}

Pos getMove(int player, Pos last_move, Moves *valid_moves)
{
    if (valid_moves->count == 0)
    {
        printf("Error: offered player %d no moves\n", player);
        exit(-1);
    }

    if (player == 0) {
        return play0GetMove(player, last_move, valid_moves);
    }
    // Player 1 random plays
    int chosen_move = rand() % valid_moves->count;
    return valid_moves->moves[chosen_move];
}

int playGame()
{
    int next_player = rand() % 2;
    play0InitGame();    
    Board9 board = {0};
    board.winner = -1;
    Moves valid_moves = {0};
    Pos last_move = {-1, -1};

    for (int x = 0; x < 9; x++)
    {
        for (int y = 0; y < 9; y++)
        {
            Pos p = {x, y};
            valid_moves.moves[x + y * 9] = p;
        }
    }

    valid_moves.count = 81;

    while (board.winner < 0)
    {
        Pos played = getMove(next_player, last_move, &valid_moves);
        if (!containsMove(&valid_moves, played))
        {
            printf("Invalid move played (%d,%d)\n", played.x, played.y);
            exit(-1);
        }

        int mx = played.x / 3;
        int my = played.y / 3;

        if ((board.overall_free & mask(mx, my)) != 0)
        {
            printf("Error: player was allowed play in a non free cell (%d,%d)\n", mx, my);
            exit(-1);
        }

        set9(&board, played.x, played.y, next_player);

        if (board.winner < 0)
        {
            next_player = (next_player == 0) ? 1 : 0;
            validMoves(&board, &valid_moves, played.x % 3, played.y % 3);
            last_move = played;
        }
    }
    
    return board.winner;
}

int main(void)
{
   /* Board3 b = {};
    set(&b,1,0,0);
    set(&b,1,2,1);
    Evaluation ev = evaluate(b,0);
    print(b);
    printf("P0[1:%d,2:%d] P1[1:%d,2:%d]\n", ev.p1[0],ev.p2[0],ev.p1[1],ev.p2[1]);*/

    srand(time(NULL)); // Initialization, should only be called once.

    int results[3] = {0};

    for (int g = 0; g < 1000; g++)
    {
        int winner = playGame();
        results[winner]++;
        p0_log = 0;
        if (g % 10 == 0) {
            printf(".");
            fflush(stdout);
        }
    }
    printf("P0=%d, P1=%d, Draws=%d\n", results[0], results[1], results[2]);
    return 0;
}
