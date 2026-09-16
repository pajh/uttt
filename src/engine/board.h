/*
 * Ultimate Tic-Tac-Toe board engine.
 *
 * This is intentionally a header-only, single-translation-unit component:
 * every executable that uses it includes its definitions directly.  Do not
 * include it in more than one source file in the same executable.
 *
 * Coordinates use x = column and y = row.  A Board3 is encoded in base 3;
 * cache[code] decodes a small board into two 9-bit player masks.  Board9
 * stores the nine encoded small boards plus the master board and its closed
 * squares.  A closed square can be won by either player or a drawn board.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emmintrin.h>
#include <tmmintrin.h>
#include <immintrin.h>

#define MASK_ROW 7
#define MASK_COL 73
#define MASK_D1 256 + 16 + 1
#define MASK_D2 4 + 16 + 64

#define POSS_BOARDS 19682 + 1

typedef uint16_t u16;
//
typedef struct Pos_s
{
    int x, y;
} Pos;

typedef struct Board3_s
{
    u16 p[2];
} Board3;

Board3 cache[POSS_BOARDS];
u16 rcache[0x1FF + 1];
u16 count_cache[0x1FF + 1];
u_int32_t eval_count1 = {0};
u_int32_t eval_count2 = {0};

typedef struct Board9_s
{
    u16 cell[9+1];
    u16 overall;
    u16 overall_free;
    int winner;
} Board9;

typedef struct Moves_s
{
    int count;
    Pos moves[81];
} Moves;

typedef struct Moves2_s
{
    /* One legal-cell bitmask per small board; fields after count are iterator state. */
    uint16_t mask[9];
    uint16_t count;
    uint16_t it_current;
    uint16_t it_current_pos;
    uint16_t it_count;
} Moves2;

void _assert(char* log) {
    fprintf(stderr,"ASSERTION FAILED\n%s\n", log);
    exit(-1);
}

#define ASSERT(x,y) do { if (!(x)) _assert(y); } while (0)

int moveNext(Moves2* moves, Pos* p) {
    if (moves->it_count == moves->count) return 0;

    while ( (moves->it_current == 0) && (moves->it_current_pos < 9)){
        moves->it_current = moves->mask[moves->it_current_pos++];
    }

    if (moves->it_current) {        
        unsigned int m = (moves->it_current & -moves->it_current);
        moves->it_current ^= m;
        int m2 = __builtin_ctz(m); /* Index of the isolated set bit. */

        //if (m2 > 8) {
        //    fprintf(stderr,"[m2 is wrong %u->%u]",m,m2);
        // }

        /* Convert a small-board index and its least-significant cell bit to 9x9 coordinates. */
        int real_pos = moves->it_current_pos -1;
        p->x = ( (real_pos % 3) * 3) + m2 % 3;
        p->y = ( (real_pos / 3) * 3) + m2 / 3;
        moves->it_count++;
    } else {
        fprintf(stderr,"Moves2 next underflow %u %u\n",moves->count, moves->it_count);
        exit(0);
    }
    return 1;
}

int moveNextCB(Moves2* moves, u16* cell, u16* bit) {
    if (moves->it_count == moves->count) return 0;

    while ( (moves->it_current == 0) && (moves->it_current_pos < 9)){
        moves->it_current = moves->mask[moves->it_current_pos++];
    }

    if (moves->it_current) {        
        unsigned int m = (moves->it_current & -moves->it_current);
        moves->it_current ^= m;
        *bit = m;
        *cell = moves->it_current_pos -1;        
        moves->it_count++;
    } else {
        fprintf(stderr,"Moves2 next underflow %u %u\n",moves->count, moves->it_count);
        exit(0);
    }
    return 1;
}

static inline Pos cell2pos(u16 cell, u16 bit) {
    int m2 = __builtin_ctz( bit );
    Pos p = { ( (cell % 3) * 3) + m2 % 3,( (cell / 3) * 3) + m2 / 3 };
    return p;
}

static void inline pos2cell(Pos p, u16* cell, u16* bit) {
    *cell = ( p.y / 3) * 3 + (p.x / 3);
    *bit = (1 << ( (p.y % 3) * 3 + (p.x % 3) ));
}

#define POPCNT(x) __builtin_popcount(x)

// Not really a bit, returns a number between 0 and 80 indicating which cell of 81 this is.
static u16 inline pos2bitNo(Pos p) {
    u16 cell, bit;
    pos2cell(p, &cell, &bit);
    return cell * 9 + __builtin_ctz(bit);
}


void pushMove(Moves2* moves, Pos p) {
    u16 cell, bit;
    pos2cell(p,&cell,&bit);
    if (!(moves->mask[cell] & bit)) {
        moves->mask[cell] |= bit;
        moves->count++;
    }
}

void resetMoveIt(Moves2* moves) {
    moves->it_count = 0;
    moves->it_current = 0;
    moves->it_current_pos = 0;
}


typedef struct Evaluation_s
{
    unsigned char p3[2], p2[2], p1[2];
    u16 free,p0_winners,p1_winners;
} Evaluation;

Evaluation ev_cache[POSS_BOARDS];

#define pos(x,y)  (x + y * 3)
#define mask(x,y) (1 << (x + y * 3) )
#define B3(u) cache[u]
#define B3_2_U16(X) rcache[X.p[0]] + 2 * rcache[X.p[1]]

// static inline u16 pc(u16 mask) {
//     if (mask > 0x1FF) {
//         fprintf(stderr,"Error mask is %hx\n",mask);
//     }
//     return count_cache[mask];
// }

//#define POPCNT(x) count_cache[x]


static inline u16 board3u16(Board3 b3) {
    u16 tot = 0;
    u16 pow = 1;
    for (int i=0;i<9;i++) {
        u16 mask = (1 << i);
        if ( b3.p[0] & mask ) tot+= pow;
        if ( b3.p[1] & mask ) tot+= 2 * pow;
        pow *= 3;
    }
    return tot;
}

#define CMASK(x) 1 << x

static inline u16 board3u16i(Board3 b3) {
    const __m128i masks =  _mm_set_epi16 (CMASK(7), CMASK(6), CMASK(5), CMASK(4), CMASK(3), CMASK(2), CMASK(1), CMASK(0) );
    const __m128i mults =  _mm_set_epi16 ( (u16)3*3*3*3*3*3*3,3*3*3*3*3*3,3*3*3*3*3,3*3*3*3,3*3*3,3*3,3, 1 );
    const __m128i zero = _mm_setzero_si128 ();

    __m128i p0 = _mm_set1_epi16 ( b3.p[0]);
    __m128i p1 = _mm_set1_epi16 ( b3.p[1]);

    p0 = _mm_and_si128 (p0, masks);
    p1 = _mm_and_si128 (p1, masks);

    p0 = _mm_cmpgt_epi16(p0, zero);
    p1 = _mm_cmpgt_epi16(p1, zero);

    //p0 = _mm_mullo_epi16(p0, mults);
    //p1 = _mm_mullo_epi16(p1, mults);

    p0 = _mm_and_si128(p0, mults);
    p1 = _mm_and_si128(p1, mults);

    p0 = _mm_hadd_epi16(p0, p0);
    p0 = _mm_hadd_epi16(p0, p0);
    p0 = _mm_hadd_epi16(p0, p0);

    p1 = _mm_hadd_epi16(p1, p1);
    p1 = _mm_hadd_epi16(p1, p1);
    p1 = _mm_hadd_epi16(p1, p1);

    uint64_t total1,total2;
    
    _mm_storeu_si64((void*) &total1, p0);
    _mm_storeu_si64((void*) &total2, p1);

    total1 &= 0xFFFF;
    total2 &= 0xFFFF;

    if (b3.p[0] & CMASK(8) ) total1+= (3 * 3 * 3 * 3 * 3 * 3 * 3 * 3);
    if (b3.p[1] & CMASK(8) ) total2+= (3 * 3 * 3 * 3 * 3 * 3 * 3 * 3);

    return total2 * 2 + total1;

}

int containsMove(Moves *moves, Pos move)
{
    for (int i = 0; i < moves->count; i++)
    {
        if (moves->moves[i].x == move.x && moves->moves[i].y == move.y)
        {
            return 1;
        }
    }
    return 0;
}

static inline void evaluate_line_old(u16 mask, Board3 board, Evaluation *evaluation, u16 free_bits)
{    
    int p0_count = POPCNT(board.p[0] & mask);
    int p1_count = POPCNT(board.p[1] & mask);
    int free_count = POPCNT(free_bits & mask);

    if (p0_count == 3)
        evaluation->p3[0]++;
    if (p1_count == 3)
        evaluation->p3[1]++;

    if (p0_count == 2 && p1_count == 0 && free_count <= 2)
        evaluation->p2[0]++;
    if (p1_count == 2 && p0_count == 0 && free_count <=2)
        evaluation->p2[1]++;

    if (p0_count == 1 && p1_count == 0 && free_count <= 1)
        evaluation->p1[0]++;
    if (p1_count == 1 && p0_count == 0 && free_count <= 1)
        evaluation->p1[1]++;
    return;
}

static inline void evaluate_line(u16 mask, Board3 board, Evaluation *evaluation, u16 free_bits)
{    
    int p0_count = POPCNT(board.p[0] & mask);
    int p1_count = POPCNT(board.p[1] & mask);
    int tot_count = POPCNT(free_bits & mask);

    if (p0_count == 3)
        evaluation->p3[0]++;
    if (p1_count == 3)
        evaluation->p3[1]++;

    if (p0_count == 2 && tot_count == 2)
        evaluation->p2[0]++;
    if (p1_count == 2 && tot_count == 2)
        evaluation->p2[1]++;

    if (p0_count == 1 && tot_count == 1)
        evaluation->p1[0]++;
    if (p1_count == 1 && tot_count == 1)
        evaluation->p1[1]++;
    return;
}

static inline int countFree(u16 board)
{
    return (9 - POPCNT(board));
}

#define evalMAC2(board_u16, free_bits) _evaluate(board_u16, free_bits)
#define evalMAC1(board_u16) ( ev_cache[board_u16] )

static inline Evaluation _evaluate(u16 board_u16, u16 full_bits)
{
    eval_count1++;
    Evaluation ev = {0};
    const u16 masks[8] = {MASK_ROW << (0*3),MASK_ROW << (1*3),MASK_ROW << (2*3), MASK_COL << 0, MASK_COL << 1, MASK_COL << 2, MASK_D1, MASK_D2 };
    ASSERT( (board_u16 < POSS_BOARDS),"Invalid board sent to evaluate");

    Board3 board = B3(board_u16);

    u16 comb = board.p[0] | board.p[1];

    if ( comb == full_bits ) {
        return ev_cache[board_u16];
    }
    eval_count2++;
    full_bits |= comb;
    ev.free = countFree(full_bits);
    evaluate_line(MASK_ROW << (0*3), board, &ev, full_bits);
    evaluate_line(MASK_ROW << (1*3), board, &ev, full_bits);
    evaluate_line(MASK_ROW << (2*3), board, &ev, full_bits);

    evaluate_line(MASK_COL << 0, board, &ev, full_bits);
    evaluate_line(MASK_COL << 1, board, &ev, full_bits);
    evaluate_line(MASK_COL << 2, board, &ev, full_bits);

    evaluate_line(MASK_D1, board, &ev, full_bits);
    evaluate_line(MASK_D2, board, &ev, full_bits);

    u16 empty_bits = ~full_bits;

    for (u16 mask = 1; mask < 1024; mask <<= 1) {
        for (int i=0;i<8;i++) {
            if (mask & empty_bits & masks[i]) {
                if ( ( (masks[i] & board.p[0]) | mask) == masks[i] ) ev.p0_winners |= mask;
                if ( ( (masks[i] & board.p[1]) | mask) == masks[i] ) ev.p1_winners |= mask;
            }
        }
    }

    return ev;
}

static inline void setB3(Board3 *board, int x, int y, int player)
{    
    board->p[player] = board->p[player] | (mask(x, y));
}

static inline void set(u16 *board_16, int x, int y, int player) {
    const u16 pow3[9] = { 1,3,9,27,81,243,729,2187,6561 };
    u16 plus = pow3[x+y*3] * ( player + 1);
    *board_16 += plus;
}

static inline void set_old(u16 *board_16, int x, int y, int player)
{
    Board3 board = B3(*board_16);
    setB3(&board,x,y,player);
    //*board_16 = board3u16(board);
    *board_16 = B3_2_U16(board);
}

void handleCellWin(Board9 *board, int x, int y, int player)
{

    board->overall_free = board->overall_free | (mask(x, y));

    if (player < 2)
    {
        set(&board->overall, x, y, player);
        board->cell[pos(x,y)] = (player == 0) ? 0x2671 : 0x4ce2;
        Evaluation ev2 = evalMAC2(board->overall, board->overall_free);
        if (ev2.p3[player] > 0)
        {
            board->winner = player;
            return;
        }
    } else { // player == 2 ie a draw, blank all cells in this square
        board->cell[pos(x,y)] = 0;
    }

    // Game not won by a line of 3 but all squares may be played 
    //if (countFree(board->overall_free) == 0)
    if ( board->overall_free == 0x1FF )
    {
        Board3 board_overall = B3(board->overall);
        int p0_score = POPCNT(board_overall.p[0]);
        int p1_score = POPCNT(board_overall.p[1]);

        if (p0_score == p1_score)
        {
            board->winner = 2;            
        }
        else
        {
            board->winner = (p0_score > p1_score) ? 0 : 1;            
        }
    }
}

static inline void set9Simple(Board9 *board, int x, int y, int player) {
    int mx = x / 3;
    int my = y / 3;
    u16 *mb = &board->cell[pos(mx, my)];
    set(mb, x % 3, y % 3, player);
}

static inline Evaluation set9CB(Board9 *board, u16 cell, u16 bit, int player)
{

    //set(mb, x % 3, y % 3, player);

    Board3 b3 = B3(board->cell[cell]);
    b3.p[player] |= bit;
    board->cell[cell] = B3_2_U16( b3 );

    Evaluation ev = evalMAC1( board->cell[cell] );

    // check if this cell is full and if so mark it not free
    int mx = cell % 3;
    int my = cell / 3;

    if (ev.p3[player] > 0)
    {
        handleCellWin(board, mx, my, player);
    } else {
        if (ev.free == 0) {
            handleCellWin(board, mx, my, 2);
        }
    }
    return ev;
}

static inline Evaluation set9(Board9 *board, int x, int y, int player)
{
    int mx = x / 3;
    int my = y / 3;
    u16 *mb = &board->cell[pos(mx, my)];
    set(mb, x % 3, y % 3, player);
    Evaluation ev = evalMAC1( *mb );

    // check if this cell is full and if so mark it not free

    if (ev.p3[player] > 0)
    {
        handleCellWin(board, mx, my, player);
    } else {
        Board3 b3 = B3(*mb);
        u16 comb = b3.p[0] | b3.p[1];
        if (POPCNT(comb) == 9) {
            handleCellWin(board, mx, my, 2);
        }
    }
    return ev;
}

static inline void addMoves(Board3 b, Moves *moves, int x_offset, int y_offset)
{
    int moves_added = 0;
    u16 comb = b.p[0] | b.p[1];
    for (int x = 0; x < 3; x++)
    {
        for (int y = 0; y < 3; y++)
        {
            if ((comb & mask(x, y)) == 0)
            {
                moves->moves[moves->count].x = x + x_offset;
                moves->moves[moves->count].y = y + y_offset;
                moves->count++;
                moves_added++;
            }
        }
    }
    if (moves_added == 0)
    {
        fprintf(stderr,"Serious error, tried to add moves but found no empty squares\n");
    }
}

void validMoves2(Board9 *board, Moves2 *valid_moves, int mx, int my) {

    if (mx < 0 || my < 0 || mx > 2 || my > 2) {
        fprintf(stderr,"Invalid cell positions\n");
        exit(0);
    }
    memset(valid_moves,0,sizeof(Moves2));
    int all_moves = (board->overall_free & mask(mx, my) );

    if (all_moves) {
        //fprintf(stderr,"doing all moves\n");
        for (int i=0;i<9;i++) {
            if (( board->overall_free & (1 << i)) == 0) {
                Board3 b3 = B3(board->cell[i]);
                valid_moves->mask[i] = ~(0xFE00 | b3.p[0] | b3.p[1]);
                valid_moves->count+= POPCNT( valid_moves->mask[i]);
            }
        }
    } else {
        //fprintf(stderr,"doing 1 sqaure\n");
        int i = pos(mx,my);
        Board3 b3 = B3(board->cell[ i ]);
        valid_moves->mask[i] = ~(0xFE00 | b3.p[0] | b3.p[1]);
        valid_moves->count+= POPCNT( valid_moves->mask[i]);
    }
}

void validMoves(Board9 *board, Moves *valid_moves, int mx, int my)
{
    if (mx < 0 || my < 0 || mx > 2 || my > 2) {
        fprintf(stderr,"Invalid cell positions\n");
        exit(0);
    }

    valid_moves->count = 0;

    if ((board->overall_free & mask(mx, my)) == 0)
    {
        Board3 b3 = B3(board->cell[pos(mx, my)]);
        addMoves(b3, valid_moves, mx * 3, my * 3);
    }
    else
    {
        for (int x = 0; x < 3; x++)
        {
            for (int y = 0; y < 3; y++)
            {
                if ((board->overall_free & mask(x, y)) == 0)
                {
                    Board3 b3 = B3(board->cell[pos(x, y)]);
                    addMoves(b3, valid_moves, x * 3, y * 3);
                }
            }
        }
    }

    if (valid_moves->count == 0)
    {
        fprintf(stderr,"Failed to find valid moves for cell (%d,%d) winner status is %d\n", mx, my, board->winner);

        for (int y = 0; y < 3; y++)
        {
            for (int x = 0; x < 3; x++)
            {
                int free = board->overall_free & mask(x, y);
                fprintf(stderr,"Cell status (%d,%d)=%d\n", x, y, free);
            }
        }        
    }
}



static inline void push(Moves *moves, Pos p) {
    moves->moves[moves->count++] = p;
}
// Internal only for initB£Cache
static inline void _inc3(int a[], int p) {
    a[p]++;
    a[p] = a[p] % 3;
    if (a[p] == 0) _inc3(a,p+1);
}

// Internal only for initB3Cache
static inline Board3 _genB3(int const a[]) {
    Board3 b = {0};
    for (int i=0;i<9;i++) {
        if (a[i] == 1) b.p[0] |= (1 << i);
        if (a[i] == 2) b.p[1] |= (1 << i);
    }
    return b;
}


// Initialises cache, rcache & ev_cache
void initBoardCaches() {
    int base3[10] = {0};

    for (int i=0;i< 0x200; i++) {
        count_cache[i] = __builtin_popcount(i);
        if (i > 0) {
            ASSERT( (count_cache[i] > 0 && count_cache[i] < 10) , "bad count cache"); 
        }
    }

    ASSERT((count_cache[256] == 1), "Count cache fail");
    ASSERT((count_cache[255] == 8), "Count cache fail");
    ASSERT( (count_cache[17] == 2), "Count cache fail");

    memset(&ev_cache[0],0,sizeof(Evaluation));
    ev_cache[0].free = 9;

    for( u16 count=0; count< POSS_BOARDS;count++ ) {        
        Board3 b3 =  _genB3(base3);
        cache[count] = b3;
        if (b3.p[1] == 0) {
            rcache[b3.p[0]] = count;
        }
        _inc3(base3,0);
        if (count > 0) {
            ev_cache[count] = _evaluate(count,0);
        }
    }
}

int isCellInPlayerWon(Board9 *board, int x, int y, int player) {
    Board3 b3 = B3(board->overall);
    return (b3.p[player] & mask(x/3, y/3));
}

void printEvalCounts() {
    fprintf(stderr,"Eval1:%u Eval2%u\n",eval_count1, eval_count2);
}
