#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

/* Identity reported by `--HELLO`.  Increment the trailing number whenever this
 * bot's behaviour or capabilities change.  The reply names the bot and any
 * local command-and-control options it supports (this baseline supports none). */
#define HELLO_TEXT "RANDOM001"

/**
 * Auto-generated code below aims at helping you parse
 * the standard input according to the problem statement.
 **/

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--HELLO") == 0) {
        if (argc != 2) { fprintf(stderr, "--HELLO must be the only argument\n"); return 2; }
        puts(HELLO_TEXT);
        return 0;
    }
    unsigned seed;
    if (argc == 3 && strcmp(argv[1], "--seed") == 0) {
        const char *p = argv[2];
        if (!*p) { fprintf(stderr, "bad seed\n"); return 2; }
        seed = 0;
        for (; *p; p++) {
            if (*p < '0' || *p > '9') { fprintf(stderr, "bad seed\n"); return 2; }
            unsigned digit = (unsigned)(*p - '0');
            if (seed > (UINT_MAX - digit) / 10u) { fprintf(stderr, "bad seed\n"); return 2; }
            seed = seed * 10u + digit;
        }
    } else {
        fprintf(stderr, "missing seed\n");
        return 2;
    }
    int move_x[81], move_y[81];
    srand(seed);
    // game loop
    while (1) {
        int opponent_row;
        int opponent_col;
        scanf("%d%d", &opponent_row, &opponent_col);
        if (opponent_row == -2) exit(0);
        int valid_action_count;
        scanf("%d", &valid_action_count);
        for (int i = 0; i < valid_action_count; i++) {
            int row;
            int col;
            scanf("%d%d", &row, &col);
            move_x[i] = col;
            move_y[i] = row;
        }
        
        // Write an action using printf(). DON'T FORGET THE TRAILING \n
        // To debug: fprintf(stderr, "Debug messages...\n");
        int pick = rand() % valid_action_count;
        printf("%d %d\n", move_y[pick], move_x[pick]);
        fflush(stdout);
        //fprintf(stderr, "AI random:sent my move\n");
    }
    return 0;
}
