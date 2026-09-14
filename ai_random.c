#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

/**
 * Auto-generated code below aims at helping you parse
 * the standard input according to the problem statement.
 **/

int main()
{
    int move_x[81], move_y[81];
    srand(time(NULL)); // Initialization, should only be called once.   
    int pid = getpid(); 
    int t = 0;
    for (int i=0;i<pid % 109;i++) {
        t += rand() % 2;
    }
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
    if (t > 1000) printf("xxxxx");
    return 0;
}