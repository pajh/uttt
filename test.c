#include <stdio.h>
#include "board.h"

int main(void)
{


    printf("pos2bitNo test\n");

    u16 cell, bit;
    int i = 0;
    int errors = 0;
    for (int y=0;y<9;y++) {
        for (int x=0;x<9;x++) {
            Pos p = {x,y};
            //pos2cell(p,&cell,&bit);
            int bno = pos2bitNo(p);
            int cell = (x / 3) + (y / 3) * 3;
            int offset = (x % 3) + (y % 3) * 3;
            if (bno != cell*9 + offset) {
                printf("(%d,%d)=*%2d*| ",p.x,p.y,bno);
                errors++;
            } else {
                printf("(%d,%d)= %2d | ",p.x,p.y,bno);
            }
            //u16 bno = __builtin_ctz( bit );
            //printf("(%d,%d)=%d.%3d| ",p.x,p.y,cell,bno);
        }
        printf("\n");
    }

    if (errors > 0) {
        printf("%d errors in pos2bitNo() ... abort\n", errors);
        exit(0);
    } else {
        printf("pos2bitNo OK\n");
    }

    for (int y=0;y<9;y++) {
        for (int x=0;x<9;x++) {
            Pos p = {x,y};
            pos2cell(p, &cell, &bit);
            Pos p2 = cell2pos(cell, bit);

            if (p.x != p2.x || p.y != p2.y) {
                printf("cell2pos error (%d,%d) > (%d,%d)\n",p.x,p.y,p2.x,p2.y);
                errors++;
            }
        }
    }
    if (errors > 0) {
        printf("%d errors in cell2pos() ... abort\n", errors);
        exit(0);
    } else {
        printf("cell2pos() OK\n");
    }

    initBoardCaches();

    for (u16 u16_board=0;u16_board < POSS_BOARDS; u16_board++) {
        Board3 b3 = B3(u16_board);
        Evaluation ev = evalMAC1(u16_board);
        if (ev.p0_winners) {
            printf("------------------------\n");

            for (int i=0;i<9;i++) {
                u16 mask = 1 << i;
                if ( mask & b3.p[0]) printf("X ");
                else if ( mask & b3.p[1]) printf("O ");
                else if (mask & ev.p0_winners) printf("* ");
                else printf(". ");
                
                if (i%3 == 2) printf("\n");
            }
        }
    }

    return 0;
}