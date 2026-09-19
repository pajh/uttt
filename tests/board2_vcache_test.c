#include <stdio.h>
#include "board2.h"

_Static_assert(sizeof board2_has_three_in_a_row_vcache_table == 64,
               "has-three lookup must contain eight uint64_t words");
_Static_assert(sizeof board2_still_win_vcache_table == 64,
               "still-win lookup must contain eight uint64_t words");

int main(void)
{
    for (mask9 mask = 0; mask <= M111111111; mask++) {
        bool original_line = has_three_in_a_row(mask);
        bool cached_line = has_three_in_a_row_vcache(mask);
        bool original_still = still_win(mask);
        bool cached_still = still_win_vcache(mask);
        if (original_line != cached_line || original_still != cached_still) {
            fprintf(stderr, "mask=%03x line=%d/%d still=%d/%d\n", mask,
                    original_line, cached_line, original_still, cached_still);
            return 1;
        }
    }
    {
        Board2 ordinary = {0};
        Board2 blocked = {0};
        Board2 local_win = {0};
        if (board2_play(&ordinary, 0, 1, 0)) {
            fprintf(stderr, "ordinary move incorrectly changed proof input\n");
            return 1;
        }
        blocked.marks[0][0] = 0x063;
        if (!board2_play(&blocked, 0, 0x010, 0) ||
            !(blocked.cannot_claim[1] & 1u)) {
            fprintf(stderr, "new cannot_claim bit was not reported\n");
            return 1;
        }
        local_win.marks[0][0] = 0x003;
        if (!board2_play(&local_win, 0, 4, 0) ||
            !(local_win.marks[0][UBOARD] & 1u)) {
            fprintf(stderr, "local U closure was not reported\n");
            return 1;
        }
    }
    {
        const mask9 draws = 0x1F8;
        Board2 positive = {0};
        Board2 line_possible = {0};
        Board2 count_tie = {0};
        Board2 draw_proof = {0};
        Board2 actual_draw = {0};
        const u8 p0_draw[5] = {0, 1, 4, 5, 6};
        const u8 p1_draw[4] = {2, 3, 7, 8};

        positive.marks[0][UBOARD] = draws | 0x003;
        positive.marks[1][UBOARD] = draws;
        if (certified_result(&positive, 0) != BOARD2_CERTIFIED_WIN) {
            fprintf(stderr, "positive certificate was rejected\n");
            return 1;
        }

        line_possible.marks[0][UBOARD] = 0x02B;
        line_possible.marks[1][UBOARD] = 0x180;
        if (certified_result(&line_possible, 0) != BOARD2_NO_CERTIFICATE) {
            fprintf(stderr, "opponent line possibility was ignored\n");
            return 1;
        }

        count_tie.marks[0][UBOARD] = 0x003 | 0x1E8;
        count_tie.marks[1][UBOARD] = 0x1E8;
        if (certified_result(&count_tie, 0) != BOARD2_NO_CERTIFICATE) {
            fprintf(stderr, "count tie was certified as a win\n");
            return 1;
        }

        draw_proof.marks[0][UBOARD] = 0x001 | 0x004 | 0x0F8;
        draw_proof.marks[1][UBOARD] = 0x002 | 0x004 | 0x0F8;
        draw_proof.cannot_claim[0] = 0x100;
        draw_proof.cannot_claim[1] = 0x100;
        if (certified_result(&draw_proof, 0) != BOARD2_CERTIFIED_DRAW ||
            certified_result(&draw_proof, 1) != BOARD2_CERTIFIED_DRAW) {
            fprintf(stderr, "positive draw certificate was rejected\n");
            return 1;
        }

        for (u8 cell = 0; cell < 9; cell++) {
            for (u8 turn = 0; turn < 9; turn++) {
                u8 player = (u8)(turn & 1u);
                u8 local = player ? p1_draw[turn / 2] : p0_draw[turn / 2];
                (void)board2_play(&actual_draw, cell,
                                  (onehot9)(1u << local), player);
            }
        }
        if (actual_draw.winner != BOARD2_DRAW ||
            certified_result(&actual_draw, 0) != BOARD2_CERTIFIED_DRAW ||
            certified_result(&actual_draw, 1) != BOARD2_CERTIFIED_DRAW) {
            fprintf(stderr, "constructed full draw certificate mismatch\n");
            return 1;
        }
    }
    puts("board2 vcache exhaustive comparison: 512 masks match");
    return 0;
}
