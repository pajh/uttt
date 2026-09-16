#include <stdio.h>
#include <stdint.h>

#define POSS_BOARDS 19682 + 1

#define pos(x,y)  (x + y * 3)
#define mask(x,y) (1 << (x + y * 3) )

typedef uint16_t u16;
typedef struct Board3_s
{
    u16 p[2];
} Board3;

Board3 cache[POSS_BOARDS];

void printBoard(Board3 board) {
    printf("Board p0:%d  p1:%d\n",board.p[0], board.p[1]);
    for (int y=0;y<3;y++) {
        for (int x=0;x<3;x++) {
            char c = '?';
            u16 mask = mask(x,y);
            if ( board.p[0] & board.p[1] ) printf("Error:\n");
            if ( ( board.p[0] & mask ) && !(board.p[1] & mask) ) c = 'X';
            if ( !( board.p[0] & mask ) && (board.p[1] & mask) ) c = 'O';
            if ( !( board.p[0] & mask ) && !(board.p[1] & mask) ) c = '.';

            printf("%c ",c);
        }
        if (y <2 ) printf("\n-----\n");
    }
    printf("\n\n");
}

void inc3(int a[], int p) {
    a[p]++;
    a[p] = a[p] % 3;
    if (a[p] == 0) inc3(a,p+1);
}

Board3 genB3(int const a[]) {
    Board3 b = {0};
    for (int i=0;i<9;i++) {
        if (a[i] == 1) b.p[0] |= (1 << i);
        if (a[i] == 2) b.p[1] |= (1 << i);
    }
    return b;
}

u16 board3u16(Board3 b3) {
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

void initB3Cache() {

    Board3 b = {0};
    int count = 0;
    int base3[9] = {0};

    while ( count < POSS_BOARDS ) {
        if (count < 10) printf("[%d %d %d %d %d %d %d %d %d ]\n", base3[0], base3[1],base3[2],base3[3],base3[4],base3[5],base3[6],base3[7],base3[8]);
        cache[count++] = genB3(base3);
        inc3(base3,0);
    }
}

void main() {

    initB3Cache();
    printBoard(cache[0]);
    printBoard(cache[1]);
    printBoard(cache[2]);
    printBoard(cache[3]);
    printBoard(cache[4]);
    printBoard(cache[5]);
    printBoard(cache[POSS_BOARDS-2]);
    printBoard(cache[POSS_BOARDS-1]);

    for (u16 i=0;i<1000;i++) {
        Board3 b = cache[i];
        u16 i2 = board3u16(b);
        if (i != i2) printf("error %d %d\n",i,i2);
    }

}


