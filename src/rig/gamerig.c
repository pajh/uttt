/*
 * Local process-based referee for a single Ultimate Tic-Tac-Toe game.  It
 * speaks the CodinGame stdin/stdout protocol to two independent bot processes,
 * validates their moves, and prints one JSON summary of the game to stdout.
 * This is local test infrastructure, not code submitted to CodinGame.
 *
 * Usage:
 *   gamerig <p0-binary> <p0-args> <p1-binary> <p1-args> <start 0|1> [--relaxed <ms>]
 *
 * <pN-args> is the argument string only; the rig launches <pN-binary> with
 * those arguments.  A bot may take up to --relaxed milliseconds (default
 * 10000) on a single move before the rig forfeits it; moves slower than the
 * arena limits are accepted but logged.  Exit status is 0 for any completed
 * game (including a forfeit), 2 for a usage error, and 1 for a rig-internal
 * failure.
 */
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include <wordexp.h>
#include <sys/wait.h>

#include "board.h"

void error(char *s);

/* Result tokens are user-facing JSON values and must stay stable. */
#define RESULT_THREE_IN_A_ROW "3inARow"
#define RESULT_COUNT          "CountVictory"
#define RESULT_DRAW           "draw"
#define RESULT_FORFEIT        "Forfeit"

/* Longest a bot may take on one move before the rig forfeits it, in ms. */
#define HANG_MS_DEFAULT 10000
#define MAX_CANDIDATES 81

const char *bot_binary[2];
const char *bot_args[2];
char bot_cmdline[2][1024];
char bot_hello[2][256];
int bot_hello_present[2];
int bot_instrument_capable[2];
pid_t bot_pids[2];
char bcache[2048];
int timed_out;
int hang_ms = HANG_MS_DEFAULT;
int instrument_player = -1; /* -1 = no instrumentation, else player 0 or 1 */

typedef struct {
    int row;
    int col;
    int score;
} Candidate;

typedef struct {
    int moveno;
    int player;
    int row;
    int col;
    int time_ms;
    int candidate_count;
    Candidate candidates[MAX_CANDIDATES];
} MoveRecord;

typedef struct {
    int winner;              /* 0 or 1 for a player, 2 for a draw */
    const char *result_type; /* one of the RESULT_* tokens */
    int total_moves;
    int total_time_ms;
    MoveRecord moves[81];
} GameResult;

uint64_t get_gtod_clock_time(void);

/* Launches <binary> with the shell-split <args> and connects its stdin/stdout
   to pipes.  stderr is inherited so bot diagnostics reach the rig's stderr. */
pid_t runAndLink(const char *binary, const char *args, int *read_pipe, int *write_pipe) {
    int in[2], out[2];
    if (pipe(in) < 0 || pipe(out) < 0) error("pipe");
    fcntl(in[0], F_SETFD, FD_CLOEXEC);
    fcntl(in[1], F_SETFD, FD_CLOEXEC);
    fcntl(out[0], F_SETFD, FD_CLOEXEC);
    fcntl(out[1], F_SETFD, FD_CLOEXEC);
    pid_t pid = fork();
    if (pid < 0) error("fork");
    if (pid == 0) {
        if (dup2(in[0], STDIN_FILENO) < 0 || dup2(out[1], STDOUT_FILENO) < 0)
            _exit(126);
        close(in[1]); close(out[0]);
        if (in[0] != STDIN_FILENO) close(in[0]);
        if (out[1] != STDOUT_FILENO) close(out[1]);
        wordexp_t words;
        if (wordexp(args ? args : "", &words, WRDE_NOCMD) != 0) _exit(125);
        char *argv[words.we_wordc + 2];
        argv[0] = (char *)binary;
        for (size_t i = 0; i < words.we_wordc; i++) argv[i + 1] = words.we_wordv[i];
        argv[words.we_wordc + 1] = NULL;
        execvp(binary, argv);
        perror(binary);
        _exit(127);
    }
    close(in[0]); close(out[1]);
    *write_pipe = in[1]; *read_pipe = out[0];
    return pid;
}

/* Stops a bot process, escalating to SIGKILL if it ignores SIGTERM, and reaps
   it.  Bounds the shutdown so a hung or signal-ignoring bot cannot stall the
   rig. */
static void terminateAndReap(pid_t pid) {
    kill(pid, SIGTERM);
    uint64_t deadline = get_gtod_clock_time() + 1000000; /* 1 s grace */
    for (;;) {
        pid_t reaped = waitpid(pid, NULL, WNOHANG);
        if (reaped == pid) return;
        if (reaped < 0 && errno != EINTR) return; /* already reaped */
        if (get_gtod_clock_time() >= deadline) break;
        struct timespec pause = {0, 1000000}; /* 1 ms */
        nanosleep(&pause, NULL);
    }
    kill(pid, SIGKILL);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
}

/* Runs a bot's optional --HELLO probe.  A bot that does not implement it is
   recorded as absent rather than treated as a failure. */
static void collectBotHello(int player) {
    int read_pipe, write_pipe;
    pid_t pid = runAndLink(bot_binary[player], "--HELLO", &read_pipe, &write_pipe);
    close(write_pipe);
    struct pollfd ready = {read_pipe, POLLIN, 0};
    char hello[256];
    ssize_t got = -1;
    if (poll(&ready, 1, 200) > 0) got = read(read_pipe, hello, sizeof(hello) - 1);
    if (got > 0) {
        hello[got] = 0;
        hello[strcspn(hello, "\r\n")] = 0;
        strncpy(bot_hello[player], hello, sizeof(bot_hello[player]) - 1);
        bot_hello[player][sizeof(bot_hello[player]) - 1] = 0;
        bot_hello_present[player] = 1;
    } else {
        bot_hello[player][0] = 0;
        bot_hello_present[player] = 0;
    }
    /* Local builds advertise instrumentation as the L1 flag in the --HELLO
     * build suffix (e.g. NM-003-R1-D0L1A0E1); older builds used INSTRUMENT=1. */
    bot_instrument_capable[player] =
        bot_hello_present[player] &&
        (strstr(bot_hello[player], "L1") != NULL ||
         strstr(bot_hello[player], "INSTRUMENT=1") != NULL);
    close(read_pipe);
    terminateAndReap(pid);
    fprintf(stderr, "Player %d HELLO: %s\n", player,
        bot_hello_present[player] ? bot_hello[player] : "(none)");
}

static int writeAll(int fd, const char *text, size_t length) {
    while (length) {
        ssize_t n = write(fd, text, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        text += n; length -= n;
    }
    return 1;
}

uint64_t get_gtod_clock_time(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) error("clock_gettime");
    return (uint64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

/* Sends one turn and waits up to wait_ms for one "row col" reply.  On timeout
   or malformed input it returns {-1,-1}; timed_out distinguishes a timeout
   from EOF. */
static void parseCandidates(const char *line, Candidate *candidates, int *count) {
    const char *p = strstr(line, "[I");
    if (!p) return;
    p += 2;
    while (*count < MAX_CANDIDATES) {
        const char *brace = strchr(p, '{');
        if (!brace) break;
        int row, col, score, used;
        if (sscanf(brace, "{%d,%d,%d}%n", &row, &col, &score, &used) != 3) break;
        candidates[*count].row = row;
        candidates[*count].col = col;
        candidates[*count].score = score;
        (*count)++;
        p = brace + used;
    }
}

/* Sends one turn and waits up to wait_ms for one "row col" reply, plus the
   optional "[I {row,col,score}, ...]" candidate record.  On timeout or
   malformed input it returns {-1,-1}; timed_out distinguishes a timeout from
   EOF. */
static Pos getMove(Pos last_move, Moves *valid_moves, int read_pipe, int write_pipe, int wait_ms,
    int player, Candidate *candidates, int *candidate_count) {
    Pos failed = {-1, -1};
    timed_out = 0;
    *candidate_count = 0;
    uint64_t deadline = get_gtod_clock_time() + (uint64_t)wait_ms * 1000;
    char input[2048];
    int used = 0;
    if (instrument_player == player)
        used += snprintf(input, sizeof(input), "[I] ");
    used += snprintf(input + used, sizeof(input) - used, "%d %d\n%d\n",
        last_move.y, last_move.x, valid_moves->count);
    for (int i = 0; i < valid_moves->count; i++)
        used += snprintf(input + used, sizeof(input) - used, "%d %d\n", valid_moves->moves[i].y, valid_moves->moves[i].x);
    bcache[0] = 0;
    if (!writeAll(write_pipe, input, used)) return failed;
    size_t length = 0;
    while (length < sizeof(bcache) - 1) {
        uint64_t now = get_gtod_clock_time();
        if (now >= deadline) { timed_out = 1; return failed; }
        struct pollfd ready = {read_pipe, POLLIN, 0};
        int result = poll(&ready, 1, (int)((deadline - now + 999) / 1000));
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) { timed_out = result == 0; return failed; }
        if (ready.revents & (POLLIN | POLLHUP)) {
            char ch;
            ssize_t n = read(read_pipe, &ch, 1);
            if (n < 0 && errno == EINTR) continue;
            if (n != 1) return failed;
            bcache[length++] = ch;
            bcache[length] = 0;
            if (ch == '\n') {
                Pos selected = {-1, -1};
                if (sscanf(bcache, "%d%d", &selected.y, &selected.x) != 2) return failed;
                parseCandidates(bcache, candidates, candidate_count);
                return selected;
            }
        }
    }
    return failed;
}

/* Plays one game.  The arena 1000/100 ms limits are advisory: a slower reply
   is accepted and logged, and only a move exceeding the rig's hang cap is
   forfeited.  Any bot timeout, illegal move, crash or EOF is a forfeit, not a
   rig error. */
static void playGame(int first_player, GameResult *g) {
    int read_pipe[2], write_pipe[2];
    bot_pids[0] = runAndLink(bot_binary[0], bot_args[0], &read_pipe[0], &write_pipe[0]);
    bot_pids[1] = runAndLink(bot_binary[1], bot_args[1], &read_pipe[1], &write_pipe[1]);

    uint64_t game_begin = get_gtod_clock_time();

    Board9 board = {0};
    board.winner = -1;
    Moves valid_moves = {0};
    Pos last_move = {-1, -1};
    for (int x = 0; x < 9; x++)
        for (int y = 0; y < 9; y++)
            valid_moves.moves[x + y * 9] = (Pos){x, y};
    valid_moves.count = 81;

    int player = first_player;
    int play_counts[2] = {0, 0};
    int count = 0;
    int forfeited = 0;
    g->winner = 2;
    g->result_type = RESULT_DRAW;

    while (board.winner < 0) {
        int first_response = play_counts[player] == 0;
        int arena_ms = first_response ? 1000 : 100;
        uint64_t begin = get_gtod_clock_time();
        Pos played = getMove(last_move, &valid_moves, read_pipe[player], write_pipe[player], hang_ms,
            player, g->moves[count].candidates, &g->moves[count].candidate_count);
        uint64_t duration = get_gtod_clock_time() - begin;

        if (timed_out || duration > (uint64_t)hang_ms * 1000) {
            fprintf(stderr, "Forfeit: player %d timed out at moveno %d\n", player, count);
            g->winner = 1 - player;
            g->result_type = RESULT_FORFEIT;
            forfeited = 1;
            break;
        }
        if (!containsMove(&valid_moves, played)) {
            fprintf(stderr, "Forfeit: player %d produced an invalid/failed response at moveno %d: %s\n",
                player, count, bcache);
            g->winner = 1 - player;
            g->result_type = RESULT_FORFEIT;
            forfeited = 1;
            break;
        }
        if (duration > (uint64_t)arena_ms * 1000)
            fprintf(stderr, "WARNING: player %d moveno %d took %.3f ms (nominal %d ms)\n",
                player, count, (double)duration / 1000.0, arena_ms);

        MoveRecord *m = &g->moves[count];
        m->moveno = count;
        m->player = player;
        m->row = played.y;
        m->col = played.x;
        m->time_ms = (int)((duration + 500) / 1000); /* round to nearest ms */
        count++;
        play_counts[player]++;

        int mx = played.x / 3;
        int my = played.y / 3;
        if ((board.overall_free & mask(mx, my)) != 0)
            error("Illegal cell reached: board says the target square is not free");

        set9(&board, played.x, played.y, player);

        if (board.winner < 0) {
            player = 1 - player;
            validMoves(&board, &valid_moves, played.x % 3, played.y % 3);
            last_move = played;
        }
    }

    for (int i = 0; i < 2; i++) {
        close(write_pipe[i]);
        close(read_pipe[i]);
        /* Bots may not handle EOF; terminate and reap every process. */
        terminateAndReap(bot_pids[i]);
    }

    g->total_moves = count;
    g->total_time_ms = (int)((get_gtod_clock_time() - game_begin + 500) / 1000);

    if (!forfeited) {
        g->winner = board.winner; /* 0, 1 or 2 (draw) */
        g->result_type = board.winner == 2 ? RESULT_DRAW :
            ev_cache[board.overall].p3[board.winner] ? RESULT_THREE_IN_A_ROW : RESULT_COUNT;
    }
}

static void jsonString(const char *s) {
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", stdout); break;
            case '\\': fputs("\\\\", stdout); break;
            case '\b': fputs("\\b", stdout); break;
            case '\f': fputs("\\f", stdout); break;
            case '\n': fputs("\\n", stdout); break;
            case '\r': fputs("\\r", stdout); break;
            case '\t': fputs("\\t", stdout); break;
            default:
                if (*p < 0x20) printf("\\u%04x", *p);
                else putchar(*p);
        }
    }
    putchar('"');
}

static void isoLocalNow(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char base[32];
    strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &tmv);
    long off = tmv.tm_gmtoff;
    char sign = off < 0 ? '-' : '+';
    if (off < 0) off = -off;
    snprintf(buf, n, "%s%c%02ld:%02ld", base, sign, off / 3600, (off % 3600) / 60);
}

static void printJson(const GameResult *g, int first_player) {
    char timestamp[48];
    isoLocalNow(timestamp, sizeof(timestamp));

    printf("{\n");
    printf("  \"header\": {\n");
    printf("    \"p0 command line\": "); jsonString(bot_cmdline[0]); printf(",\n");
    printf("    \"p1 command line\": "); jsonString(bot_cmdline[1]); printf(",\n");
    printf("    \"p0 HELLO\": "); jsonString(bot_hello[0]); printf(",\n");
    printf("    \"p1 HELLO\": "); jsonString(bot_hello[1]); printf(",\n");
    printf("    \"date/time\": "); jsonString(timestamp); printf(",\n");
    printf("    \"first player\": %d", first_player);
    /* Present only when --instrument ran, so a renderer can require it. */
    if (instrument_player >= 0)
        printf(",\n    \"instrumented\": %d", instrument_player);
    printf("\n");
    printf("  },\n");

    printf("  \"moves\": [\n");
    for (int i = 0; i < g->total_moves; i++) {
        const MoveRecord *m = &g->moves[i];
        printf("    {\"moveno\": %d, \"player\": %d, \"row\": %d, \"col\": %d, \"time\": %d",
            m->moveno, m->player, m->row, m->col, m->time_ms);
        if (m->candidate_count > 0) {
            printf(", \"candidates\": [");
            for (int j = 0; j < m->candidate_count; j++)
                printf("%s{\"row\": %d, \"col\": %d, \"score\": %d}",
                    j ? ", " : "", m->candidates[j].row, m->candidates[j].col, m->candidates[j].score);
            printf("]");
        }
        printf("}%s\n", i + 1 < g->total_moves ? "," : "");
    }
    printf("  ],\n");

    printf("  \"result\": {\n");
    printf("    \"winner\": ");
    if (g->winner == 2) printf("null"); else printf("%d", g->winner);
    printf(",\n");
    printf("    \"total moves\": %d,\n", g->total_moves);
    printf("    \"result type\": "); jsonString(g->result_type); printf(",\n");
    printf("    \"total time\": %d\n", g->total_time_ms);
    printf("  }\n");
    printf("}\n");
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <p0-binary> <p0-args> <p1-binary> <p1-args> <start 0|1> [--relaxed <ms>] [--instrument <0|1>]\n", argv[0]);
        return 2;
    }
    if ((strcmp(argv[5], "0") != 0 && strcmp(argv[5], "1") != 0) || !*argv[1] || !*argv[3]) {
        fprintf(stderr, "start must be 0 or 1, and both binary paths must be non-empty\n");
        return 2;
    }
    for (int i = 6; i < argc; ) {
        if (strcmp(argv[i], "--relaxed") == 0 && i + 1 < argc) {
            const char *p = argv[i + 1];
            unsigned value = 0;
            if (!*p) { fprintf(stderr, "--relaxed requires a positive integer\n"); return 2; }
            for (; *p; p++) {
                if (*p < '0' || *p > '9') { fprintf(stderr, "--relaxed requires a positive integer\n"); return 2; }
                unsigned digit = (unsigned)(*p - '0');
                if (value > (UINT_MAX - digit) / 10u) { fprintf(stderr, "--relaxed requires a positive integer\n"); return 2; }
                value = value * 10u + digit;
            }
            if (value < 1 || value > (unsigned)INT_MAX) { fprintf(stderr, "--relaxed requires a positive integer\n"); return 2; }
            hang_ms = (int)value;
            i += 2;
        } else if (strcmp(argv[i], "--instrument") == 0 && i + 1 < argc) {
            if (strcmp(argv[i + 1], "0") == 0) instrument_player = 0;
            else if (strcmp(argv[i + 1], "1") == 0) instrument_player = 1;
            else { fprintf(stderr, "--instrument requires 0 or 1\n"); return 2; }
            i += 2;
        } else {
            fprintf(stderr, "Usage: %s <p0-binary> <p0-args> <p1-binary> <p1-args> <start 0|1> [--relaxed <ms>] [--instrument <0|1>]\n", argv[0]);
            return 2;
        }
    }
    bot_binary[0] = argv[1]; bot_args[0] = argv[2];
    bot_binary[1] = argv[3]; bot_args[1] = argv[4];
    int first_player = argv[5][0] - '0';

    for (int i = 0; i < 2; i++) {
        if (*bot_args[i])
            snprintf(bot_cmdline[i], sizeof(bot_cmdline[i]), "%s %s", bot_binary[i], bot_args[i]);
        else
            snprintf(bot_cmdline[i], sizeof(bot_cmdline[i]), "%s", bot_binary[i]);
    }

    signal(SIGPIPE, SIG_IGN);
    initBoardCaches();
    for (int i = 0; i < 2; i++) collectBotHello(i);
    /* Every bot must identify itself; a silent probe makes the run invalid. */
    if (!bot_hello_present[0] || !bot_hello_present[1]) {
        for (int i = 0; i < 2; i++)
            if (!bot_hello_present[i])
                fprintf(stderr, "Error: player %d did not provide a valid --HELLO reply\n", i);
        return 2;
    }
    /* Instrumentation is opt-in and needs the bot to advertise the capability. */
    if (instrument_player >= 0 && !bot_instrument_capable[instrument_player]) {
        fprintf(stderr, "Error: player %d does not advertise instrumentation (L1)\n", instrument_player);
        return 2;
    }

    GameResult result;
    playGame(first_player, &result);
    printJson(&result, first_player);
    return 0;
}

void error(char *s)
{
  perror(s);
  exit(1);
}
