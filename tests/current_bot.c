#define main bot_main
#include "../src/bots/ai_minimax.c"
#undef main
#include <assert.h>

static int reference_win(unsigned bits) {
    static const unsigned lines[] = {7,56,448,73,146,292,273,84};
    for (int i=0;i<8;i++) if ((bits & lines[i]) == lines[i]) return 1;
    return 0;
}

int main(void) {
    initBoardCaches();
    initScoringCache();
    for (int code=0;code<POSS_BOARDS;code++) {
        unsigned players[2]={0,0}; int value=code;
        for (int i=0;i<9;i++,value/=3) if (value%3) players[value%3-1] |= 1u<<i;
        assert(cache[code].p[0] == players[0] && cache[code].p[1] == players[1]);
        assert(B3_2_U16(cache[code]) == code);
        Evaluation ev=ev_cache[code];
        assert(ev.free == 9-__builtin_popcount(players[0]|players[1]));
        for (int p=0;p<2;p++) {
            assert((ev.p3[p]>0) == reference_win(players[p]));
            unsigned winning=0;
            const unsigned lines[]={7,56,448,73,146,292,273,84};
            for (int bit=1;bit<=256;bit*=2) if (!((players[0]|players[1]) & bit))
                for (int j=0;j<8;j++) if ((lines[j]&bit) && ((players[p]&lines[j])|bit)==lines[j]) winning |= bit;
            int ones=0,twos=0;
            for (int j=0;j<8;j++) {
                int own=__builtin_popcount(players[p]&lines[j]);
                int occupied=__builtin_popcount((players[0]|players[1])&lines[j]);
                ones += own==1 && occupied==1;
                twos += own==2 && occupied==2;
            }
            assert(ev.p1[p]==ones && ev.p2[p]==twos);
            assert(winning == (p ? ev.p1_winners : ev.p0_winners));
        }
    }
    for(int grid=0;grid<POSS_BOARDS;grid++) {
        Board3 swapped=B3(grid);
        u16 tmp=swapped.p[0];swapped.p[0]=swapped.p[1];swapped.p[1]=tmp;
        int flipped=B3_2_U16(swapped);
        assert(f1(grid,0,0)==f1(flipped,0,1));
    }
    for (int y=0;y<9;y++) for (int x=0;x<9;x++) {
        Moves2 m={0}; Pos p={x,y}, got;
        pushMove(&m,p);pushMove(&m,p);assert(m.count==1);
        assert(moveNext(&m,&got));assert(got.x==x && got.y==y);assert(!moveNext(&m,&got));
    }
    Board9 b={0};b.winner=-1;
    /* Closed drawn board must redirect to other boards, and stay unowned. */
    handleCellWin(&b,1,1,2);
    Moves2 m;validMoves2(&b,&m,1,1);assert(m.count==72 && !m.mask[4] && b.overall==0);
    for (int i=0;i<9;i++) if (i!=4) handleCellWin(&b,i%3,i/3,2);
    assert(b.winner==2);
    b=(Board9){.winner=-1};
    handleCellWin(&b,0,0,0);handleCellWin(&b,1,0,0);handleCellWin(&b,2,0,0);assert(b.winner==0);
    b=(Board9){.winner=-1};
    int owners[]={0,0,1,1,2,0,0,1,0};
    for(int i=0;i<9;i++) handleCellWin(&b,i%3,i/3,owners[i]);
    assert(b.winner==0);
    unsigned char k[KEY_SIZE],other[KEY_SIZE];
    b=(Board9){.winner=-1};searchKey(&b,1,0,1,k);
    searchKey(&b,2,0,1,other);assert(memcmp(k,other,KEY_SIZE));
    searchKey(&b,1,1,1,other);assert(memcmp(k,other,KEY_SIZE));
    searchKey(&b,1,0,3,other);assert(memcmp(k,other,KEY_SIZE));
    b.overall_free=16;searchKey(&b,1,0,1,other);assert(memcmp(k,other,KEY_SIZE));
    map=createHM(128,1000);addHMEntry(map,k,60000u<<16|300);u32 data;
    assert(findHMEntry(map,k,&data) && data==(60000u<<16|300));assert(!findHMEntry(map,other,&data));
    /* Deadline already spent: return a supplied legal move, not default (0,0). */
    b=(Board9){.winner=-1};m=(Moves2){0};pushMove(&m,(Pos){8,8});pushMove(&m,(Pos){7,8});
    start_time=get_gtod_clock_time();search_deadline=-1;
    Pos fallback=evaluateMovesShallowTimed(&b,&m);assert(isLegalMove(fallback.x,fallback.y,&m));
    /* Immediate game-winning move survives shallow recursion and has terminal score. */
    b=(Board9){.winner=-1};handleCellWin(&b,0,0,0);handleCellWin(&b,1,0,0);
    set9Simple(&b,6,0,0);set9Simple(&b,7,0,0);clearHM(map);
    Score won=evaluateShallow(b,0,2,4,3,0);assert(won.p0==TERMINAL_SCORE && won.p1==0);
    /* Root must stop immediately, without visiting any remaining candidates. */
    m=(Moves2){0};pushMove(&m,(Pos){8,0});pushMove(&m,(Pos){6,1});
    start_time=get_gtod_clock_time();search_deadline=1;search_nodes=0;
    Pos winning=evaluateMovesShallowTimed(&b,&m);
    assert(winning.x==8 && winning.y==0 && search_nodes==1);
    /* A genuine three-ply fork: every opponent reply still permits a master win. */
    b=(Board9){.winner=-1};
    handleCellWin(&b,0,0,0);handleCellWin(&b,1,0,0);
    for(int i=3;i<9;i++) handleCellWin(&b,i%3,i/3,i==3 || i==4 ? 1 : 2);
    set9Simple(&b,6,0,0);set9Simple(&b,7,1,0);
    clearHM(map);
    Score short_score=evaluateShallow(b,0,2,4,1,0);
    assert(short_score.p0!=TERMINAL_SCORE);
    clearHM(map);
    won=evaluateShallow(b,0,2,4,3,0);
    assert(won.p0==TERMINAL_SCORE && won.p1==0);
    validMoves2(&b,&m,2,0);
    start_time=get_gtod_clock_time();search_deadline=1;
    winning=evaluateMovesShallowTimed(&b,&m);
    u16 winning_cell,winning_bit;pos2cell(winning,&winning_cell,&winning_bit);
    clearHM(map);
    won=evaluateShallow(b,0,winning_cell,winning_bit,3,0);
    assert(won.p0==TERMINAL_SCORE && won.p1==0);
    /* Reject a proven loss while retaining the only move that blocks the opponent. */
    b=(Board9){.winner=-1};
    handleCellWin(&b,0,0,1);handleCellWin(&b,1,0,1);
    for(int i=3;i<9;i++) handleCellWin(&b,i%3,i/3,i==3 || i==4 ? 0 : 2);
    set9Simple(&b,6,0,1);set9Simple(&b,7,0,1);
    clearHM(map);
    Score lost=evaluateShallow(b,0,2,8,1,0);
    assert(lost.p0==0 && lost.p1==TERMINAL_SCORE);
    m=(Moves2){0};pushMove(&m,(Pos){6,1});pushMove(&m,(Pos){8,0});
    start_time=get_gtod_clock_time();search_deadline=1;
    Pos blocking=evaluateMovesShallowTimed(&b,&m);
    assert(blocking.x==8 && blocking.y==0);
    /* With only losing moves supplied, still return a legal move. */
    m=(Moves2){0};pushMove(&m,(Pos){6,1});pushMove(&m,(Pos){7,1});
    start_time=get_gtod_clock_time();search_deadline=1;
    blocking=evaluateMovesShallowTimed(&b,&m);
    assert(isLegalMove(blocking.x,blocking.y,&m));
    /* Exhaust all master cells: open, player 0, player 1, or drawn. */
    for (unsigned code=0;code<(1u<<18);code++) {
        Board9 master={.winner=-1}; Board3 owners={0};
        unsigned value=code, available=0;
        for(int i=0;i<9;i++,value>>=2) {
            int state=value & 3;
            if (!state) available |= 1u<<i;
            else {
                master.overall_free |= 1u<<i;
                if(state<3) owners.p[state-1] |= 1u<<i;
            }
        }
        master.overall=B3_2_U16(owners);
        unsigned reference=0;
        for(int player=0;player<2;player++)
            if(reference_win(owners.p[player] | available)) reference |= 1u<<player;
        assert(possibleMasterLines(master)==reference);
        if (!reference) {
            Score shared=scoreBoard(master,0,0);
            assert(shared.p0==20*__builtin_popcount(owners.p[0]));
            assert(shared.p1==20*__builtin_popcount(owners.p[1]));
        }
        int lead=__builtin_popcount(owners.p[0])-__builtin_popcount(owners.p[1]);
        int remaining=__builtin_popcount(available);
        int expected=lead>remaining && !(reference&2) ? 0 :
                     -lead>remaining && !(reference&1) ? 1 : -1;
        assert(countProof(master,reference)==expected);
    }
    /* Nonterminal count proof must propagate through search. */
    b=(Board9){.winner=-1};
    int count_owners[]={0,0,1,1,0,0,0};
    for(int i=0;i<7;i++) handleCellWin(&b,i%3,i/3,count_owners[i]);
    assert(b.winner==-1 && countProof(b,possibleMasterLines(b))==0);
    clearHM(map);
    won=evaluateShallow(b,0,7,1,3,0);
    assert(won.p0==TERMINAL_SCORE && won.p1==0);
    b=(Board9){.winner=-1};
    int tied_owners[]={0,1,0,0,1,1,1,0};
    for(int i=0;i<8;i++) handleCellWin(&b,i%3,i/3,tied_owners[i]);
    assert(b.winner==-1 && possibleMasterLines(b)==0);
    Score counting=scoreBoard(b,0,0);
    assert(counting.p0==80 && counting.p1==80);
    set9Simple(&b,6,6,0);set9Simple(&b,7,6,0);
    Evaluation local=evalMAC1(b.cell[8]);
    counting=scoreBoard(b,0,0);
    assert(counting.p0==80+4*local.p2[0]+local.p1[0] && counting.p1==80);
    /* Shared scoring retains ownership and naturally drops impossible master lines. */
    Score original=scoreBoard(b,0,0);
    assert(original.p0==80+4*local.p2[0]+local.p1[0] && original.p1==80);
    count_scale=1.4;
    original=scoreBoard(b,0,0);
    assert(original.p0==112+4*local.p2[0]+local.p1[0] && original.p1==112);
    count_scale=0;
    original=scoreBoard(b,0,0);
    assert(original.p0==4*local.p2[0]+local.p1[0] && original.p1==0);
    count_scale=1;
    /* Ownership gate follows f1(U,p), independently for each player. */
    b=(Board9){.winner=-1};handleCellWin(&b,0,0,0);
    assert(f1(b.overall,b.overall_free,0)>0);
    count_scale=0;
    Score gate_off=scoreBoard(b,0,0);
    count_scale=0.5;
    Score gate_on=scoreBoard(b,0,0);
    assert(gate_on.p0==gate_off.p0);
    count_scale=1;
    destroyHM(map);
    puts("All board states, move conversion, endings, cache identity, deadline fallback and terminal search passed.");
}
