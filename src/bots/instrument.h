/*
 * Local, compile-time-only instrumentation for ai_minimax.c.
 *
 * The normal bot compiles these hooks away.  INSTRUMENT builds collect one compact
 * CSV row per turn, which instrument-one-game.fish turns into an HTML report.
 * Keep telemetry here so the search source stays focused on game behaviour.
 */
#ifdef INSTRUMENT
static unsigned long instrument_cache_lookups;
static unsigned long instrument_cache_hits;
static int instrument_root_evaluations;
static int instrument_deepest_plies;
static int instrument_selected_score;
static const char *instrument_search = "shallow";
static const char *instrument_file = "instrument-turns.csv";

static int instrumentCacheFind(HashMap *hm, unsigned char *key, u32 *data) {
    instrument_cache_lookups++;
    int found = searchCacheFind(hm, key, data);
    if (found) instrument_cache_hits++;
    return found;
}

static void instrumentReset(void) {
    const char *configured_file = getenv("INSTRUMENT_FILE");
    if (configured_file && *configured_file) instrument_file = configured_file;
    instrument_cache_lookups = 0;
    instrument_cache_hits = 0;
    instrument_root_evaluations = 0;
    instrument_deepest_plies = 0;
    instrument_selected_score = 0;
    instrument_search = "shallow";
}

static void instrumentRootEvaluated(int plies) {
    instrument_root_evaluations++;
    if (plies > instrument_deepest_plies)
        instrument_deepest_plies = plies;
}

static void instrumentWriteTurn(
    int turn, int budget_ms, int legal_moves, Pos selected,
    unsigned long evaluation_calls
) {
    FILE *file = fopen(instrument_file, "a+");
    if (!file) return;
    fseek(file, 0, SEEK_END);
    if (ftell(file) == 0)
        fputs("move,budget_ms,elapsed_ms,legal_moves,possibilities_evaluated,deepest_completed_ply,winning_row,winning_col,winning_score,scored_positions,cache_lookups,cache_hits,search\n", file);
    fprintf(file, "%d,%d,%.3f,%d,%d,%d,%d,%d,%d,%lu,%lu,%lu,%s\n",
        turn, budget_ms, getElaspedTime() * 1000.0, legal_moves,
        instrument_root_evaluations, instrument_deepest_plies, selected.y,
        selected.x, instrument_selected_score, evaluation_calls,
        instrument_cache_lookups, instrument_cache_hits, instrument_search);
    fclose(file);
}

#define SEARCH_CACHE_FIND(hm, key, data) instrumentCacheFind(hm, key, data)
#define INSTRUMENT_ROOT_EVALUATED(plies) instrumentRootEvaluated(plies)
#define INSTRUMENT_SELECTED(score) (instrument_selected_score = (score))
#define INSTRUMENT_MODE(name) (instrument_search = (name))
#define INSTRUMENT_RESET() instrumentReset()
#define INSTRUMENT_WRITE(turn, budget, legal, move, evaluations) \
    instrumentWriteTurn(turn, budget, legal, move, evaluations)
#define INSTRUMENT_ENABLED() 1
#define INSTRUMENT_METADATA "instrument=1; "
#define INSTRUMENT_HELLO_SUFFIX "-I"
#else
#define SEARCH_CACHE_FIND(hm, key, data) searchCacheFind(hm, key, data)
#define INSTRUMENT_ROOT_EVALUATED(plies) ((void)(plies))
#define INSTRUMENT_SELECTED(score) ((void)(score))
#define INSTRUMENT_MODE(name) ((void)(name))
#define INSTRUMENT_RESET() ((void)0)
#define INSTRUMENT_WRITE(turn, budget, legal, move, evaluations) ((void)0)
#define INSTRUMENT_ENABLED() 0
#define INSTRUMENT_METADATA ""
#define INSTRUMENT_HELLO_SUFFIX ""
#endif
