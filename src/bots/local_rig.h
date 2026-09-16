/* Local-only rig control. The CodinGame build compiles every hook away.
 * FD 3 carries newline-framed, version-1 commands/events; stdout remains moves.
 */
#ifdef LOCAL_RIG
#include <unistd.h>

static int local_rig_fd = -1;
static int local_rig_turn;
static Pos local_rig_forced = {-1, -1};
static int local_rig_instrument;

static void localRigInit(void) {
    const char *value = getenv("CG_RIG_FD");
    if (value && strcmp(value, "3") == 0) local_rig_fd = 3;
}

static void localRigReadTurn(int turn) {
    if (local_rig_fd < 0) return;
    local_rig_turn = turn;
    local_rig_forced = (Pos){-1, -1};
    char line[128];
    int saw_turn = 0;
    for (;;) {
        size_t n = 0;
        char ch;
        while (n + 1 < sizeof(line)) {
            if (read(local_rig_fd, &ch, 1) != 1) error("Local rig channel closed\n");
            if (ch == '\n') break;
            line[n++] = ch;
        }
        if (n + 1 == sizeof(line) && ch != '\n') error("Local rig command too long\n");
        line[n] = 0;
        if (!saw_turn) {
            int received;
            if (sscanf(line, "TURN %d", &received) != 1 || received != turn)
                error("Local rig turn mismatch\n");
            saw_turn = 1;
        } else if (strcmp(line, "END") == 0) {
            return;
        } else if (strcmp(line, "INSTRUMENT") == 0) {
            local_rig_instrument = 1;
        } else if (sscanf(line, "FORCE %d %d", &local_rig_forced.y, &local_rig_forced.x) == 2) {
            if (local_rig_forced.x < 0 || local_rig_forced.x > 8 ||
                local_rig_forced.y < 0 || local_rig_forced.y > 8)
                error("Invalid local rig forced move\n");
        } else if (sscanf(line, "USCALE %d", &uscale) == 1) {
            if (uscale < 0 || uscale > 1000) error("Invalid local rig uscale\n");
        } else if (sscanf(line, "COUNT_SCALE %lf", &count_scale) == 1) {
            if (count_scale < 0 || count_scale > 1000) error("Invalid local rig count scale\n");
        } else if (sscanf(line, "TIME_MS %lf", &move_budget) == 1) {
            move_budget /= 1000.0;
            if (move_budget <= 0 || move_budget > 0.9) error("Invalid local rig time budget\n");
        } else error("Unknown local rig command: %s\n", line);
    }
}

static void localRigScore(int plies, Pos move, int score) {
    if (local_rig_fd >= 0 && local_rig_instrument)
        dprintf(local_rig_fd, "SCORE %d %d %d %d %d\n",
            local_rig_turn, plies, move.y, move.x, score);
}

static void localRigEndTurn(void) {
    if (local_rig_fd >= 0) dprintf(local_rig_fd, "END %d\n", local_rig_turn);
}
#define LOCAL_RIG_HELLO " LOCAL_RIG=1"
#else
#define localRigInit() ((void)0)
#define localRigReadTurn(turn) ((void)0)
#define localRigScore(plies, move, score) ((void)0)
#define localRigEndTurn() ((void)0)
#define LOCAL_RIG_HELLO ""
#endif
