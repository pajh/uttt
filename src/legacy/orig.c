#define CG_GAME
//#pragma GCC optimize("O3,inline")
#pragma GCC optimize "O3,omit-frame-pointer,inline"
#pragma GCC target("bmi,lzcnt,popcnt")
#define MAX_TIME 0.0950

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>
#include <search.h>

#include <stdint.h>
#include <errno.h>
#include <limits.h>

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

typedef struct Evaluation_s
{
    char p3[2], p2[2], p1[2];
} Evaluation;

Evaluation ev_cache[POSS_BOARDS];

#define pos(x,y)  (x + y * 3)
#define mask(x,y) (1 << (x + y * 3) )
#define B3(u) cache[u]

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

static inline void evaluate_line(u16 mask, Board3 board, Evaluation *evaluation, u16 free_bits)
{    
    int p0_count = __builtin_popcount(board.p[0] & mask);
    int p1_count = __builtin_popcount(board.p[1] & mask);
    int free_count = __builtin_popcount(free_bits & mask);

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

static inline int countFree(u16 board)
{
    return (9 - __builtin_popcount(board));
}

static int cache_tries = 0;
static int cache_hits = 0;

void printEvCache() {
    if (cache_tries > 0)
        fprintf(stderr,"ev cache %d/%d(%f)\n",cache_hits,cache_tries, (float)cache_hits / (float)cache_tries);
}

#define evalMAC(board_u16, free_bits) ( free_bits == 0 && ev_cache[board_u16].p3[0] != 127 ) ? ev_cache[board_u16] : evaluate(board_u16, free_bits)

static inline Evaluation evaluate(u16 board_u16, u16 free_bits)
{
    Evaluation ev = {0};
    cache_tries++;
    if ( free_bits == 0 && ev_cache[board_u16].p3[0] != 127 ) {
        cache_hits++;
        return ev_cache[board_u16];
    }

    Board3 board = B3(board_u16);

    if ( (board.p[0] | board.p[1] ) == free_bits && ev_cache[board_u16].p3[0] != 127 ) {
        cache_hits++;
        return ev_cache[board_u16];
    }

    evaluate_line(MASK_ROW << (0*3), board, &ev, free_bits);
    evaluate_line(MASK_ROW << (1*3), board, &ev, free_bits);
    evaluate_line(MASK_ROW << (2*3), board, &ev, free_bits);

    evaluate_line(MASK_COL << 0, board, &ev, free_bits);
    evaluate_line(MASK_COL << 1, board, &ev, free_bits);
    evaluate_line(MASK_COL << 2, board, &ev, free_bits);

    evaluate_line(MASK_D1, board, &ev, free_bits);
    evaluate_line(MASK_D2, board, &ev, free_bits);

    ev_cache[board_u16] = ev;

    return ev;
}

static inline void setB3(Board3 *board, int x, int y, int player)
{    
    board->p[player] = board->p[player] | (mask(x, y));
}

static inline void set(u16 *board_16, int x, int y, int player)
{
    Board3 board = B3(*board_16);
    setB3(&board,x,y,player);
    *board_16 = board3u16(board);
}

void handleCellWin(Board9 *board, int x, int y, int player)
{

    board->overall_free = board->overall_free | (mask(x, y));

    if (player < 2)
    {
        set(&board->overall, x, y, player);
        board->cell[pos(x,y)] = (player == 0) ? 0x2671 : 0x4ce2;
        Evaluation ev2 = evalMAC(board->overall, board->overall_free);
        if (ev2.p3[player] > 0)
        {
            board->winner = player;
            return;
        }
    } else {
        board->cell[pos(x,y)] = 0;
    }

    // Game not won by a line of 3 but all squares may be played
    if (countFree(board->overall_free) == 0)
    {
        Board3 board_overall = B3(board->overall);
        int p0_score = __builtin_popcount(board_overall.p[0]);
        int p1_score = __builtin_popcount(board_overall.p[1]);

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

static inline Evaluation set9(Board9 *board, int x, int y, int player)
{
    int mx = x / 3;
    int my = y / 3;
    u16 *mb = &board->cell[pos(mx, my)];
    set(mb, x % 3, y % 3, player);
    Evaluation ev = evalMAC(*mb, 0);

    // check if this cell is full and if so mark it not free

    if (ev.p3[player] > 0)
    {
        handleCellWin(board, mx, my, player);
    } else {
        Board3 b3 = B3(*mb);
        u16 comb = b3.p[0] | b3.p[1];
        if (__builtin_popcount(comb) == 9) {
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

void validMoves(Board9 *board, Moves *valid_moves, int mx, int my)
{
    if (mx > 2 || my > 2) {
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

/*typedef struct MoveBuffer_s {
    int length;
    int moves[81];
} MoveBuffer;*/

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

void initB3Cache() {
    int base3[10] = {0};

    for( int count=0; count< POSS_BOARDS;count++ ) {        
        cache[count] = _genB3(base3);
        _inc3(base3,0);
    }
}

int isCellInPlayerWon(Board9 *board, int x, int y, int player) {
    Board3 b3 = B3(board->overall);
    return (b3.p[player] & mask(x/3, y/3));
}

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

/* ---------------------
   hashmap.h
   --------------------- */
   
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define KEY_SIZE 20
typedef uint32_t u32;

typedef struct HMEntry_s HMEntry;

typedef struct HMEntry_s {
    u32 hash;
    unsigned char key[KEY_SIZE];
    u32 data;
    HMEntry* next_entry;
} HMEntry;

typedef struct HashMap_s {

    HMEntry** bucket_buffer;
    u32 buckets_size;
    u32 buckets_used;

    HMEntry* entry_buffer;
    u32 entry_buffer_size;
    u32 entries_used;

    // Metrics
    u32 lookups;
    u32 lookup_found;
    
} HashMap;

HashMap* createHM(u32 buckets, u32 entries) {
    
    HashMap *hm = (HashMap *) malloc( sizeof(HashMap) );

    hm-> bucket_buffer = (HMEntry**) calloc( buckets, sizeof( HMEntry *) );
    hm->buckets_size = buckets;
    hm->buckets_used = 0;    

    hm-> entry_buffer = (HMEntry*) calloc( entries, sizeof( HMEntry ) );
    hm->entry_buffer_size = entries;
    hm->entries_used = 0;    
    return hm;
}

void destroyHM(HashMap* hm) {
    free(hm->bucket_buffer);
    free(hm->entry_buffer);
    free(hm);
    hm = NULL;
    return;
}

void clearHM( HashMap* hm ) {
    
    memset( (void *)hm->bucket_buffer, 0, hm->buckets_size * sizeof( HMEntry *) );
    hm->buckets_used = 0;    

    // Does entries need to be zerod?
    hm->entries_used = 0;
    hm->lookups = 0;
    hm->lookup_found = 0;
}

void printMetrics(HashMap* hm) {
    fprintf(stderr,"Buckets %d/%d | Entries %d/%d | Cache %d/%d\n", hm->buckets_used, hm->buckets_size, hm->entries_used, hm->entry_buffer_size, hm->lookup_found, hm->lookups);
}

#define HASHMAP_HASH_INIT 2166136261u
static inline uint32_t hash_data(const unsigned char* data )
{
	size_t nblocks = KEY_SIZE / 8;
	uint64_t hash = HASHMAP_HASH_INIT;
	for (size_t i = 0; i < nblocks; ++i)
	{
		hash ^= (uint64_t)data[0] << 0 | (uint64_t)data[1] << 8 |
			 (uint64_t)data[2] << 16 | (uint64_t)data[3] << 24 |
			 (uint64_t)data[4] << 32 | (uint64_t)data[5] << 40 |
			 (uint64_t)data[6] << 48 | (uint64_t)data[7] << 56;
		hash *= 0xbf58476d1ce4e5b9;
		data += 8;
	}

	uint64_t last = KEY_SIZE & 0xff;
	switch (KEY_SIZE % 8)
	{
	case 7:
		last |= (uint64_t)data[6] << 56; /* fallthrough */
	case 6:
		last |= (uint64_t)data[5] << 48; /* fallthrough */
	case 5:
		last |= (uint64_t)data[4] << 40; /* fallthrough */
	case 4:
		last |= (uint64_t)data[3] << 32; /* fallthrough */
	case 3:
		last |= (uint64_t)data[2] << 24; /* fallthrough */
	case 2:
		last |= (uint64_t)data[1] << 16; /* fallthrough */
	case 1:
		last |= (uint64_t)data[0] << 8;
		hash ^= last;
		hash *= 0xd6e8feb86659fd93;
	}
	// compress to a 32-bit result. also serves as a finalizer.
	return hash ^ hash >> 32;
}

void addHMEntry(HashMap *hm, unsigned char* key, u32 data ) {
    // Initial can add duplicates so be carefule FIXME

    if (hm->entries_used == hm->entry_buffer_size) return;

    u32 hash = hash_data(key);

    // Fill out next entry;
    HMEntry* my_entry = &hm->entry_buffer[hm->entries_used++];
    if (hm->entries_used == hm->entry_buffer_size) {
        double elapsed = getElaspedTime();
        fprintf(stderr,"All %d hashmap entries are used (%1.3f)",hm->entry_buffer_size, elapsed);
        if (current_task == Shallow) fprintf(stderr, "[Shallow]\n");
        if (current_task == Heuristic) fprintf(stderr, "[Heuristic]\n");
        if (current_task == Minimax) fprintf(stderr, "[Minimax]\n");
        printMetrics(hm);        
    }

    my_entry->data = data;
    my_entry->hash = hash;
    memcpy(my_entry->key, key, KEY_SIZE);

    // Find bucket offset
    int bucket_offset = hash % hm->buckets_size;
    //printf("Inserting at bucket offset:%d\n", bucket_offset);
    
    if (hm->bucket_buffer[bucket_offset] == NULL) hm->buckets_used++;
    my_entry->next_entry = hm->bucket_buffer[bucket_offset];
    hm->bucket_buffer[bucket_offset] = my_entry;    
}

int findHMEntry(HashMap* hm, unsigned char* key, u32* data) {

    u32 hash = hash_data(key);
    hm->lookups++;
    int bucket_offset = hash % hm->buckets_size;

    //printf("Find:starting at bucket %d\n", bucket_offset);

    HMEntry* current = hm->bucket_buffer[bucket_offset];
    while (current != NULL) {
        //printf("Current is not null, data=%d\n hash(%d=%d)\n", current->data, hash, current->hash);
        if (current->hash == hash) {
            //printf("Trying memcmp\n");
            if (  ( memcmp(key, current->key, KEY_SIZE) == 0 ) ) {
                *data = current->data;
                hm->lookup_found++;
                return 1;
            }
        }
        current = current->next_entry;
    }
    return 0; // Not found
}

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
HashMap* map;
int spaces_left;
//int heuristic_weights[6] =  { 30, 10, 3, 1, 7, 3};
int heuristic_weights[6] =  { 29, 10, 4, 1, 7, 3};
Pos forced_first_move = {0,1};

void printBoard(Board9 *board, Pos move, Moves *valid_moves);


void loadWeights(const char* file_name)
{
  FILE* file = fopen (file_name, "r");
  fscanf (file, "%d %d %d %d %d %d", &heuristic_weights[0],&heuristic_weights[1],&heuristic_weights[2],&heuristic_weights[3],&heuristic_weights[4],&heuristic_weights[5] );   
  //fprintf(stderr,"Loaded:");
  //for (int i=0;i<6;i++) {
  //    fprintf(stderr,"[%d=%d]",i,heuristic_weights[i]);
  //} 
  //fprintf(stderr,"\n");
  fclose (file);
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

    static u_int64_t score_count = 0;
    
    int other_player = (player == 0) ? 1: 0;

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

Pos evaluateMovesMM( Board9* board, Moves *valid_moves ) {  

    clearHM(map);  
    int best = 10;
    int best_move = 0;
    sortMoves(board, valid_moves,0);

    double loop_start = getElaspedTime();
    
    for (int i=0;i<valid_moves->count;i++) { 
        int moves_to_go = valid_moves->count-i;
        mm_step_start_time = getElaspedTime();
        mm_step_target_time = ( MAX_TIME - mm_step_start_time ) / (double)moves_to_go;
        if (i >0) mm_step_target_time *= 1.2;
        int score = scoreMoveMM( *board, valid_moves->moves[i],0,0 );
        double now = getElaspedTime();

        if (score == -2) {            
            //fprintf(stderr,"........MM[TIMEOUT@%d/%d] Total MM=%1.3f this step=%1.3f target=%1.3f\n",i+1, valid_moves->count,(now-loop_start), (now-mm_step_start_time), mm_step_target_time );
            Pos timeout = {0xFF,0xFF};
            return timeout;
        }

        logger("(%d,%d)=%d |",valid_moves->moves[i].x,valid_moves->moves[i].y,score);
        if (score < best) {
            best = score;
            best_move = i;            
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
    }
    
    logger("[MMEND %d]\n",best);
    return valid_moves->moves[best_move];
}

typedef struct Score_s {
    u16 p0;
    u16 p1;
} Score;

Score scoreBoard(Board9 board) {

    Score s = {0,0};
    Evaluation ev_o = evalMAC(board.overall,board.overall_free);    

    if (board.winner == 2) {
        return s;
    }

    // { 30, 10, 3, 1, 7, 3}
    s.p0 += ev_o.p3[0] * 100;
    s.p1 += ev_o.p3[1] * 100;

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
                Evaluation ev = evalMAC(board.cell[pos(x, y)], 0);

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

    Evaluation p0_ev = evalMAC(p0_2s, p0_free);
    Evaluation p1_ev = evalMAC(p1_2s, p1_free);
    
    s.p0 += p0_ev.p3[0] * heuristic_weights[4]; // 7
    if (p0_ev.p2[0] > ev_o.p2[0]) {
        s.p0 += ( p0_ev.p2[0] - ev_o.p2[0]) * heuristic_weights[5]; // 3
    }

    s.p1 += p1_ev.p3[1] * heuristic_weights[4];
    if (p1_ev.p2[1] > ev_o.p2[1]) {
        s.p1 += (p1_ev.p2[1]  - ev_o.p2[1]) * heuristic_weights[5]; 
    }

    s.p0 &= 0xFF;
    s.p1 &= 0xFF;
    //if (s.p0 > 255) s.p0 = 255;
    //if (s.p1 > 255) s.p1 = 255; 

    return s;
}

Score evaluateShallow(Board9 board, int player, Pos move, int depth, int timed) {

    int next_player = (player == 0) ? 1: 0;

    if (timed && depth == 0) {
        double elapsed = getElaspedTime();
        if (elapsed > MAX_TIME) {
            Score timeout = {0xFF,0xFF};
            //logger("internal timeout (%d,%d) depth:%d\n",move.x, move.y,depth);
            return timeout;
        }
    }

    set9(&board, move.x, move.y,player);

    //if (depth == 0 && player != 1) fprintf(stderr,"weird\n");
    
    board.cell[9] = board.overall | (player * 0x10000);
    u32 data = 0;

    if ( findHMEntry(map,(unsigned char*) board.cell, &data)) {
        Score sc = { data & 0xFF, data >> 8};   
        return sc;
    }

    if ( depth == 0 || board.winner > -1) {
        Score sc = scoreBoard(board);
        data = (sc.p1 << 8) | sc.p0;
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

        if (score.p0 == 0xFF && score.p1 == 0xFF) return score; // timeout

        int iscore = (next_player == 0) ? (score.p0 - score.p1) : (score.p1 - score.p0);
        if (iscore > besti) {
            besti = iscore;
            bests = score;    
            if ( (next_player == 0 && score.p0 > 100) || (next_player == 1 && score.p1 > 100) ) break;
        }
    }

    data = (bests.p1 << 8) | bests.p0;
    addHMEntry(map, (unsigned char*) board.cell, data);

    return bests;
}

Pos evaluateMoves( Board9* board, Moves *valid_moves )
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
        evaluateMove(i, *board, valid_moves, &move_eval);
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

Pos evaluateMovesShallowTimed(Board9* board, Moves *valid_moves ) {

    Pos best_move = {};
    int best_iscore = -1000;    
    Score scores[81] = {};
    int current_depth = 1;
    int finished = false;

    sortMoves(board, valid_moves, 0);

    while (!finished) {
        clearHM(map);
        double loop_start = getElaspedTime();
        logger("\n<d:%d>",current_depth);
        for (int i = 0 ; i < valid_moves->count ; i++ ) { 
            logger("%d,%d=",valid_moves->moves[i].x,valid_moves->moves[i].y);
            Score sc = evaluateShallow(*board, 0, valid_moves->moves[i], current_depth, true );            
            if (sc.p0 == 0xFF && sc.p1 == 0xFF) {
                finished = true;
                logger("(timed out)");
                break;
            } else {
                logger("%d(%d %d),", sc.p0 -sc.p1, sc.p0, sc.p1);
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
        }

        int all_moves_above_100 = true;
        int i = 0;

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
            push(&best_moves, valid_moves->moves[i]);
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
    //Score sc = scoreBoard(*board);
    fprintf(stderr, "------------------------------\n");
}

Pos getMove(Board9 *board, Pos last_move, Moves *valid_moves)
{
    if (last_move.x != -1)
    {
        set9(board, last_move.x, last_move.y, 1);
    }
    Pos my_move = {};

    spaces_left = calcPlayable(board);
    logger("[FREE %d]", spaces_left);
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
        logger("Heuristic:");
        my_move = evaluateMoves(board, valid_moves);
    }

    if (p0_log)
    {
        Board9 copy_board = *board;
        set9Simple(&copy_board, my_move.x, my_move.y, 0);
        printBoard(&copy_board, my_move, valid_moves);
        printEvCache();
    }

    set9(board, my_move.x, my_move.y, 0); // real move

    return my_move;
}

void testRig() {
    Board9 board = {};
    board.winner = -1;
    char *line =NULL;
    size_t len = 0;
    //ssize_t lineSize = 0;
    for (int y=0;y < 9; y++) {
        getline(&line, &len, stdin);
        int x = 0;
        int p = 0;
        while (line[p++] !=0 && x < 9) {
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
        }
    }
    
    for (int i=0;i<9;i++) {
        Evaluation ev = evaluate(board.cell[i],0);
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

    //printBoard(&board,)


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

    unsigned explicit_seed = 0;
    int has_explicit_seed = 0;
    if (argc > 1) {

        int arg = 1;

        while (arg < argc) {
            char* arg_str = argv[arg];
            arg++;

            if (strcmp(arg_str, "--seed") == 0) {
                if (arg >= argc) {
                    fprintf(stderr, "--seed requires an unsigned integer\n");
                    return 2;
                }
                char *end;
                errno = 0;
                unsigned long value = strtoul(argv[arg++], &end, 10);
                if (errno || *end || value > UINT_MAX) {
                    fprintf(stderr, "Invalid --seed value\n");
                    return 2;
                }
                explicit_seed = (unsigned)value;
                has_explicit_seed = 1;
                continue;
            }

            if (arg_str[0] == '-' && arg_str[1] == 'l') {
                p0_log = 1;    
            }

            if (arg_str[0] == '-' && arg_str[1] == 'w') {
                loadWeights("weights.txt");
            }

            if (arg_str[0] == '-' && arg_str[1] == 'f') {
                sscanf(&arg_str[2],"%d %d", &forced_first_move.x, &forced_first_move.y);                         
            }
        }

    }
    Board9 p0_board = {0};
    //memset(&p0_board, 0, sizeof(p0_board)); 
    p0_board.winner = -1;
    map = createHM(1280000,512000);

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
    
    /* Preserve the historical default; explicit seeds replace the PID-dependent
       warm-up with a stable one, without changing the move policy. */
    srand(has_explicit_seed ? explicit_seed : (unsigned)time(NULL));
    unsigned warmup = has_explicit_seed ? explicit_seed % 17 : (unsigned)getpid() % 17;
    int t = 0;
    for (unsigned i=0;i<warmup;i++) {
        t += rand() % 2;
    }
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
        
        Pos my_move = getMove(&p0_board, last_move, &valid_moves);
        
        printf("%d %d\n", my_move.y, my_move.x);
        fflush(stdout);
    }
    destroyHM(map);    
    exit(0);
    return 0;
}
