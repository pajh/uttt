/*
 * Deliberately misbehaving bot used by test_gamerig.py to exercise the rig's
 * failure paths.  It speaks the normal CodinGame turn protocol but can be told
 * to fail in specific ways:
 *
 *   (no flag)        plays a valid random legal move every turn
 *   --bad_move       answers its first move request with the malformed "45 X"
 *   --late_bad_move  plays a valid random move when it starts, then returns the
 *                    opponent's last move (an occupied cell) on later turns
 *   --crash          exits with status 77 as soon as it is asked for a move
 *   --goslow         always plays the first listed move, sleeping 50 ms plus
 *                    10 ms per move already played before each reply
 *   --hung           sleeps 20000 ms before every reply, then plays the first
 *                    listed move; only completes if the rig's cap allows it
 *
 * `--HELLO` is always accepted as the sole argument.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

/* Identity reported by `--HELLO`.  Increment the trailing number whenever this
 * bot's behaviour or capabilities change.  The reply names the bot and any
 * local command-and-control options it supports (this test bot supports none). */
#define HELLO_TEXT "FAILBOT002"

#define GOSLOW_BASE_MS 50
#define GOSLOW_STEP_MS 10
#define HUNG_DELAY_MS 20000

enum FailMode {
    MODE_VALID,
    MODE_BAD_MOVE,
    MODE_LATE_BAD_MOVE,
    MODE_CRASH,
    MODE_GOSLOW,
    MODE_HUNG
};

static void sleep_ms(int ms)
{
    struct timespec pause = { ms / 1000, (long)(ms % 1000) * 1000000 };
    nanosleep(&pause, NULL);
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--HELLO") == 0) {
        if (argc != 2) { fprintf(stderr, "--HELLO must be the only argument\n"); return 2; }
        puts(HELLO_TEXT);
        return 0;
    }

    enum FailMode mode = MODE_VALID;
    if (argc == 2 && strcmp(argv[1], "--bad_move") == 0) mode = MODE_BAD_MOVE;
    else if (argc == 2 && strcmp(argv[1], "--late_bad_move") == 0) mode = MODE_LATE_BAD_MOVE;
    else if (argc == 2 && strcmp(argv[1], "--crash") == 0) mode = MODE_CRASH;
    else if (argc == 2 && strcmp(argv[1], "--goslow") == 0) mode = MODE_GOSLOW;
    else if (argc == 2 && strcmp(argv[1], "--hung") == 0) mode = MODE_HUNG;
    else if (argc != 1) { fprintf(stderr, "Invalid command-line arguments\n"); return 2; }

    int move_x[81], move_y[81];
    int moves_played = 0;

    while (1) {
        int opponent_row, opponent_col;
        if (scanf("%d%d", &opponent_row, &opponent_col) != 2) return 0;
        if (opponent_row == -2) return 0;

        int valid_action_count;
        if (scanf("%d", &valid_action_count) != 1) return 0;
        for (int i = 0; i < valid_action_count; i++) {
            int row, col;
            if (scanf("%d%d", &row, &col) != 2) return 0;
            move_x[i] = col;
            move_y[i] = row;
        }

        if (mode == MODE_CRASH) exit(77);

        if (mode == MODE_BAD_MOVE) {
            printf("45 X\n");
            fflush(stdout);
            return 0;
        }

        if (mode == MODE_GOSLOW) {
            sleep_ms(GOSLOW_BASE_MS + GOSLOW_STEP_MS * moves_played);
            printf("%d %d\n", move_y[0], move_x[0]);
            fflush(stdout);
            moves_played++;
            continue;
        }

        if (mode == MODE_HUNG) {
            sleep_ms(HUNG_DELAY_MS);
            printf("%d %d\n", move_y[0], move_x[0]);
            fflush(stdout);
            moves_played++;
            continue;
        }

        bool first_turn = (opponent_row == -1 && opponent_col == -1);
        if (mode == MODE_LATE_BAD_MOVE && !first_turn) {
            printf("%d %d\n", opponent_row, opponent_col);
            fflush(stdout);
            return 0;
        }

        int pick = rand() % valid_action_count;
        printf("%d %d\n", move_y[pick], move_x[pick]);
        fflush(stdout);
    }
}
