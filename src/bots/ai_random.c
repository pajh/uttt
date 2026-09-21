#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

#define DEFAULT_SEED 1u

/**
 * Auto-generated code below aims at helping you parse
 * the standard input according to the problem statement.
 **/

int main(int argc, char **argv)
{
    unsigned seed = DEFAULT_SEED;
    if (argc == 3 && strcmp(argv[1], "--seed") == 0) {
        const char *p = argv[2];
        if (!*p) { fprintf(stderr, "Invalid seed arguments\n"); return 2; }
        seed = 0;
        for (; *p; p++) {
            if (*p < '0' || *p > '9') { fprintf(stderr, "Invalid seed arguments\n"); return 2; }
            unsigned digit = (unsigned)(*p - '0');
            if (seed > (UINT_MAX - digit) / 10u) { fprintf(stderr, "Invalid seed arguments\n"); return 2; }
            seed = seed * 10u + digit;
        }
    } else if (argc != 1) {
        fprintf(stderr, "Invalid command-line arguments\n");
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
