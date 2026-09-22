/* Local-only instrumentation, version 2.  A LOCAL_RIG build accepts bracketed
 * control tokens on its ordinary stdin data lines and appends one bracketed
 * candidate record to its move line; there is no separate channel or file
 * descriptor.  Non-LOCAL_RIG (CodinGame) builds compile every hook away.
 *
 * Commands are typed as "[<letter><digits>]", e.g. [I] enables instrumentation
 * for the turn and [F56] forces row 5, column 6.  The candidate record is
 * "[I {row,col,score}, ...]", one tuple per root move the search compared.
 */
#ifdef LOCAL_RIG
static int local_rig_instrument;
static int local_rig_forced_x = -1;
static int local_rig_forced_y = -1;
static int local_rig_have_candidates;
static int local_rig_candidate_count;
static struct { int row, col, score; } local_rig_candidates[81];

/* Applies every bracketed control token in line and removes it in place. */
static void localRigStripCommands(char *line) {
    char *read = line;
    char *write = line;
    while (*read) {
        if (*read != '[') {
            *write++ = *read++;
            continue;
        }
        char *close = strchr(read, ']');
        if (!close) break; /* Unterminated token: drop the rest of the line. */
        *close = '\0';
        const char *body = read + 1;
        if (body[0] == 'I') {
            local_rig_instrument = 1;
        } else if (body[0] == 'F') {
            char row = body[1];
            char col = body[2];
            if (row < '0' || row > '8' || col < '0' || col > '8' || body[3] != '\0')
                error("Invalid local rig force token [%s]\n", body);
            local_rig_forced_y = row - '0';
            local_rig_forced_x = col - '0';
        } else {
            error("Unknown local rig command [%s]\n", body);
        }
        read = close + 1;
    }
    *write = '\0';
}

/* Clears per-turn control state before the turn's input is read. */
static void localRigBeginTurn(void) {
    local_rig_instrument = 0;
    local_rig_forced_x = -1;
    local_rig_forced_y = -1;
    local_rig_have_candidates = 0;
    local_rig_candidate_count = 0;
}

static int localRigInstrumenting(void) { return local_rig_instrument; }
static int localRigForcedX(void) { return local_rig_forced_x; }
static int localRigForcedY(void) { return local_rig_forced_y; }

static void localRigBeginCandidates(void) {
    local_rig_candidate_count = 0;
    local_rig_have_candidates = 1;
}

/* Records one root move's final score.  row/col are CodinGame coordinates. */
static void localRigAddCandidate(int row, int col, int score) {
    if (local_rig_candidate_count >= 81) return;
    local_rig_candidates[local_rig_candidate_count].row = row;
    local_rig_candidates[local_rig_candidate_count].col = col;
    local_rig_candidates[local_rig_candidate_count].score = score;
    local_rig_candidate_count++;
}

/* Appends the candidate record to the move line when this turn was searched. */
static void localRigPrintCandidates(void) {
    if (!local_rig_instrument || !local_rig_have_candidates) return;
    fputs(" [I", stdout);
    for (int i = 0; i < local_rig_candidate_count; i++) {
        if (i) fputc(',', stdout);
        printf(" {%d,%d,%d}", local_rig_candidates[i].row,
            local_rig_candidates[i].col, local_rig_candidates[i].score);
    }
    fputc(']', stdout);
}

#else
#define localRigStripCommands(line) ((void)(line))
#define localRigBeginTurn() ((void)0)
#define localRigInstrumenting() (0)
#define localRigForcedX() (-1)
#define localRigForcedY() (-1)
#define localRigPrintCandidates() ((void)0)
#endif
