#define main bot_main
#include "../src/bots/ai_minimax.c"
#undef main
#include <assert.h>

static int reference_win(unsigned bits) {
    static const unsigned lines[] = {7,56,448,73,146,292,273,84};
    for (int i=0;i<8;i++) if ((bits & lines[i]) == lines[i]) return 1;
    return 0;
}

static int reference_can_still_win(unsigned own[2], int player) {
    static const unsigned lines[] = {7,56,448,73,146,292,273,84};
    for (int i=0;i<8;i++) if (!(own[player ^ 1] & lines[i])) return 1;
    return 0;
}

static int reference_claim_certificate(const Board9 *b, int player) {
    static const unsigned lines[] = {7,56,448,73,146,292,273,84};
    Board3 owned=B3(b->overall);
    unsigned active=(~b->overall_free)&0x1FF, opponent=owned.p[player^1];
    for (int cell=0; cell<9; cell++) if (active & (1u<<cell)) {
        Board3 local=B3(b->cell[cell]);
        unsigned possible=0;
        for (int i=0;i<8;i++) if (!(local.p[player^1]&lines[i])) { possible=1; break; }
        if (possible) opponent |= 1u<<cell;
    }
    for (int i=0;i<8;i++) if ((owned.p[player]&lines[i])==lines[i]) return 1;
    for (int i=0;i<8;i++) if ((opponent&lines[i])==lines[i]) return 0;
    return __builtin_popcount(owned.p[player]) > __builtin_popcount(opponent);
}

static int reference_after_claim(const Board9 *b, unsigned cell, int player) {
    if (b->overall_free & (1u<<cell)) return -1;
    Board9 copy=*b;
    Board3 owned=B3(copy.overall);
    owned.p[player] |= 1u<<cell;
    copy.overall=B3_2_U16(owned);
    copy.overall_free |= 1u<<cell;
    return reference_claim_certificate(&copy,player);
}

static int reference_master_certificate(unsigned state, int player);

/* Independent oracle for the MM-010 transform: retain actual owners, turn
 * active cells locally impossible for the opponent into draws, and leave the
 * candidate active until its virtual owner mark is added. */
static unsigned reference_claimability_state(const Board9 *b, int player,
                                             unsigned excluded_cell) {
    Board3 owned = B3(b->overall);
    unsigned state = 0, place = 1;
    unsigned owners = owned.p[0] | owned.p[1];
    unsigned draw = (b->overall_free | b->cannot_claim[player ^ 1]) & ~owners;
    if (excluded_cell < 9) draw &= ~(1u << excluded_cell);
    for (unsigned cell = 0; cell < 9; cell++, place <<= 2) {
        unsigned bit = 1u << cell;
        if (owned.p[0] & bit) state += place;
        else if (owned.p[1] & bit) state += 2u * place;
        else if (draw & bit) state += 3u * place;
    }
    return state;
}

static int reference_claimability_certificate(const Board9 *b, int player) {
    return reference_master_certificate(reference_claimability_state(b, player, 9), player);
}

static int reference_claimability_after_claim(const Board9 *b, unsigned cell, int player) {
    if (cell >= 9 || (b->overall_free & (1u << cell))) return -1;
    unsigned state = reference_claimability_state(b, player, cell);
    state += (1u + (unsigned)player) << (2 * cell);
    return reference_master_certificate(state, player);
}

static void assert_claim_masks(const Board9 *b) {
    for (int cell=0; cell<9; cell++) {
        Board3 local=B3(b->cell[cell]);
        unsigned own[2]={local.p[0],local.p[1]};
        for (int p=0;p<2;p++) {
            unsigned bit=1u<<cell;
            int expected=reference_can_still_win(own,p) ? 0 : (int)bit;
            assert((b->cannot_claim[p] & bit) == (unsigned)expected);
        }
    }
}

/* Deliberately independent 4-state reference for the master certificate. */
static int reference_master_certificate(unsigned state, int player) {
    static const unsigned lines[] = {7,56,448,73,146,292,273,84};
    unsigned owned[2] = {0, 0}, active = 0, value = state;
    for (int cell = 0; cell < 9; cell++, value >>= 2) {
        unsigned status = value & 3u;
        if (status == 0) active |= 1u << cell;
        else if (status == 1 || status == 2) owned[status - 1] |= 1u << cell;
    }
    for (int i = 0; i < 8; i++)
        if ((owned[player] & lines[i]) == lines[i]) return 1;
    unsigned opponent = owned[player ^ 1] | active;
    for (int i = 0; i < 8; i++)
        if ((opponent & lines[i]) == lines[i]) return 0;
    return __builtin_popcount(owned[player]) > __builtin_popcount(opponent);
}

int main(void) {
    initBoardCaches();
    initScoringCache();
    /* Exhaustively validate both packed certificate planes and virtual claims. */
    for (unsigned state = 0; state < MASTER_CERT_STATES; state++) {
        Board9 master = {.winner = -1};
        Board3 owners = {0};
        unsigned value = state;
        for (int cell = 0; cell < 9; cell++, value >>= 2) {
            unsigned status = value & 3u;
            if (status != 0) {
                master.overall_free |= 1u << cell;
                if (status == 1) owners.p[0] |= 1u << cell;
                else if (status == 2) owners.p[1] |= 1u << cell;
            }
        }
        master.overall = B3_2_U16(owners);
        int expected_current = -1;
        for (int player = 0; player < 2; player++) {
            int expected = reference_master_certificate(state, player);
            assert(masterCertificateLookup(state, player) == expected);
            if (expected && expected_current < 0) expected_current = player;
            for (int cell = 0; cell < 9; cell++) {
                if (!(master.overall_free & (1u << cell))) {
                    unsigned claimed = state | ((1u + (unsigned)player) << (2 * cell));
                    assert(masterCertificateAfterClaim(&master, cell, player) ==
                           reference_master_certificate(claimed, player));
                }
            }
        }
        /* Reachable nonterminal states cannot certify both players at once. */
        if (!reference_master_certificate(state, 0) ||
            !reference_master_certificate(state, 1))
            assert(currentMasterCertificate(&master) == expected_current);
        assert(masterCertificateAfterClaim(&master, 0, -1) == -1);
        assert(masterCertificateAfterClaim(&master, 9, 0) == -1);
        if (master.overall_free & 1u)
            assert(masterCertificateAfterClaim(&master, 0, 0) == -1);
    }
    /* Focused direct-line, pessimistic-opponent, count, tie, and draw cases. */
    assert(reference_master_certificate(1 | (1 << 2) | (1 << 4), 0));
    assert(!reference_master_certificate(2 | (2 << 2) | (2 << 4), 0));
    assert(reference_master_certificate((1u << 0) | (1u << 2) | (1u << 6) |
                                       (1u << 8) | (3u << 4) | (3u << 10) |
                                       (3u << 12) | (3u << 14) | (3u << 16), 0));
    assert(!reference_master_certificate(1 | (2 << 2) | (1 << 4), 0));
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
            assert(((ev.can_still_win >> p) & 1) == reference_can_still_win(players,p));
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
    /* Independent certificate oracle over deterministic reachable prefixes.
       It derives local impossibility from masks, rather than consulting the
       production cannot_claim fields or certificate helper. */
    for (int sample=0; sample<128; sample++) {
        Board9 sampled={.winner=-1};
        for (int ply=0; ply<24; ply++) {
            int chosen=-1, bit=0;
            for (int n=0;n<81 && chosen<0;n++) {
                int cell=n/9, local=n%9;
                if ((sampled.overall_free&(1u<<cell))==0 &&
                    !((B3(sampled.cell[cell]).p[0]|B3(sampled.cell[cell]).p[1])&(1u<<local))) {
                    chosen=cell; bit=1<<local;
                }
            }
            if (chosen<0) break;
            set9CB(&sampled,(u16)chosen,(u16)bit,(sample+ply)&1);
            for (int p=0;p<2;p++) {
                assert(masterCertificateClaimability(&sampled,p) ==
                       reference_claimability_certificate(&sampled,p));
                assert(masterCertificateClaimability(&sampled,p) ==
                       reference_claim_certificate(&sampled,p));
                for (int c=0;c<9;c++) {
                    assert(masterCertificateAfterClaim(&sampled,c,p) ==
                           reference_claimability_after_claim(&sampled,c,p));
                    assert(masterCertificateAfterClaim(&sampled,c,p) ==
                           reference_after_claim(&sampled,c,p));
                }
            }
        }
    }
    /* Tightened count proof: cells geometrically unavailable to the opponent
       are excluded from its pessimistic remaining claim count. */
    Board9 early={.winner=-1};
    handleCellWin(&early,0,0,0); handleCellWin(&early,1,0,0);
    early.cannot_claim[1] = (u16)(~(1u<<2) & 0x1FF);
    assert(currentMasterCertificate(&early) == 0);
    assert(masterCertificateAfterClaim(&early,2,0) == 1);
    /* An active cell impossible for the opponent is drawn only in its
       pessimistic view; a closed owner cell remains an owner status. */
    Board9 owner_case={.winner=-1};
    handleCellWin(&owner_case,0,0,0);
    owner_case.cannot_claim[1] |= 1u;
    assert(masterCertificateClaimability(&owner_case,0) ==
           reference_claimability_certificate(&owner_case,0));
    Board9 b={0};b.winner=-1;
    assert(b.cannot_claim[0] == 0 && b.cannot_claim[1] == 0);
    /* Monotone local impossibility: both players, one player, draw, and a
       local win are covered before closure is relevant to the master proof. */
    set9Simple(&b,0,0,0); set9Simple(&b,1,0,0); set9Simple(&b,2,0,0);
    assert((b.cannot_claim[1] & 1u) == 0); /* opponent still has other lines */
    b=(Board9){.winner=-1};
    set9CB(&b,0,1u,0); set9CB(&b,0,1u<<4,0); set9CB(&b,0,1u<<8,0);
    assert((b.cannot_claim[1] & 1u) != 0 && !(b.cannot_claim[0] & 1u));
    b=(Board9){.winner=-1};
    for (int i=0;i<9;i++) { handleCellWin(&b,i%3,i/3,2); assert_claim_masks(&b); }
    /* Closed drawn board must redirect to other boards, and stay unowned. */
    b=(Board9){.winner=-1};
    handleCellWin(&b,1,1,2);
    Moves2 m;validMoves2(&b,&m,1,1);assert(m.count==72 && !m.mask[4] && b.overall==0);
    for (int i=0;i<9;i++) if (i!=4) handleCellWin(&b,i%3,i/3,2);
    assert(b.winner==2);
    Score draw_score=evaluateShallow(b,0,4,1u<<8,2,0);
    assert(draw_score.p0==0 && draw_score.p1==0);
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
    /* A pre-existing root certificate is checked once and returns a legal
       root move without rechecking every candidate/depth iteration. */
    b=(Board9){.winner=-1};
    handleCellWin(&b,0,0,0); handleCellWin(&b,1,0,0);
    b.cannot_claim[1]=(u16)(~(1u<<2)&0x1FF);
    m=(Moves2){0}; pushMove(&m,(Pos){8,8}); pushMove(&m,(Pos){7,8});
    start_time=get_gtod_clock_time(); search_deadline=1;
    Pos root_proof_move=evaluateMovesShallowTimed(&b,&m);
    assert(isLegalMove(root_proof_move.x,root_proof_move.y,&m));
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
    /* The leaf now proves the next player's winning reply directly. */
    assert(short_score.p0==TERMINAL_SCORE && short_score.p1==0);
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
    /* Secured ownership is counted even when master line potential exists. */
    b=(Board9){.winner=-1};handleCellWin(&b,0,0,0);
    assert(f1(b.overall,b.overall_free,0)>0);
    count_scale=0;
    Score gate_off=scoreBoard(b,0,0);
    count_scale=0.5;
    Score gate_on=scoreBoard(b,0,0);
    assert(gate_on.p0==gate_off.p0+10);
    /* Two distinct live finishes are counted once each, not once per line. */
    b=(Board9){.winner=-1};
    handleCellWin(&b,1,0,0);handleCellWin(&b,1,1,0);handleCellWin(&b,0,2,0);
    assert(b.winner==-1 && liveMasterWinningCells(b,0)==2);
    assert(liveMasterWinningCells(b,1)==0);
    count_scale=1;
    /* Leaf certificate: forced/open routing, both players, and closed cells. */
    b=(Board9){.winner=-1};
    handleCellWin(&b,0,0,0); handleCellWin(&b,1,0,0);
    set9Simple(&b,6,1,0); set9Simple(&b,7,1,0);
    assert(!certifiedImmediateMasterWin(b,0,1u<<5)); /* forced board 5 */
    assert(certifiedImmediateMasterWin(b,0,1u<<0));  /* board 0 is closed */
    clearHM(map);
    Score next_p0=evaluateShallow(b,1,3,1u<<2,0,0);
    assert(next_p0.p0==TERMINAL_SCORE && next_p0.p1==0);
    handleCellWin(&b,2,0,1);                         /* candidate is closed */
    assert(!certifiedImmediateMasterWin(b,0,1u<<0));
    b=(Board9){.winner=-1};
    handleCellWin(&b,0,0,1); handleCellWin(&b,1,0,1);
    set9Simple(&b,6,1,1); set9Simple(&b,7,1,1);
    assert(certifiedImmediateMasterWin(b,1,1u<<0));
    clearHM(map);
    Score next_p1=evaluateShallow(b,0,3,1u<<2,0,0);
    assert(next_p1.p0==0 && next_p1.p1==TERMINAL_SCORE);
    /* MM-007: internal certificate short-circuits before any descendant leaf.
       Cover forced routing for both players and free routing for player 0. */
    b=(Board9){.winner=-1};
    handleCellWin(&b,0,0,0); handleCellWin(&b,1,0,0);
    set9Simple(&b,6,0,0); set9Simple(&b,6,1,0);
    clearHM(map); evaluation_calls=0; game_cert_internal_hits=0;
    Score internal=evaluateShallow(b,1,3,1u<<2,2,0);
    assert(internal.p0==TERMINAL_SCORE && internal.p1==0 && evaluation_calls==0 && game_cert_internal_hits==1);
    b=(Board9){.winner=-1};
    handleCellWin(&b,0,0,1); handleCellWin(&b,1,0,1);
    set9Simple(&b,6,0,1); set9Simple(&b,6,1,1);
    clearHM(map); evaluation_calls=0;
    internal=evaluateShallow(b,0,3,1u<<2,2,0);
    assert(internal.p0==0 && internal.p1==TERMINAL_SCORE && evaluation_calls==0);
    b=(Board9){.winner=-1};
    handleCellWin(&b,0,0,0); handleCellWin(&b,1,0,0);
    set9Simple(&b,6,0,0); set9Simple(&b,7,0,0);
    clearHM(map); evaluation_calls=0;
    internal=evaluateShallow(b,1,3,1u<<0,2,0);
    assert(internal.p0==TERMINAL_SCORE && internal.p1==0 && evaluation_calls==0);
    /* A non-winning internal node still descends to ordinary leaf scoring. */
    b=(Board9){.winner=-1}; clearHM(map); evaluation_calls=0;
    internal=evaluateShallow(b,0,3,1u<<1,1,0);
    assert(internal.p0!=TERMINAL_SCORE || internal.p1!=TERMINAL_SCORE);
    assert(evaluation_calls>0);
    destroyHM(map);
    puts("All board states, move conversion, endings, cache identity, deadline fallback and terminal search passed.");
}
