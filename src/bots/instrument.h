/* Optional per-turn search telemetry. Only LOCAL_RIG builds contain this code.
 * INSTRUMENT is a rig command, not a separate bot build or local output file.
 */
#ifdef LOCAL_RIG
static unsigned long instrument_cache_lookups;
static unsigned long instrument_cache_hits;
static int instrument_root_evaluations;
static int instrument_deepest_plies;
static int instrument_selected_score;
static const char *instrument_search = "shallow";

static int instrumentCacheFind(HashMap *hm, unsigned char *key, u32 *data) {
    if (local_rig_instrument) instrument_cache_lookups++;
    int found = searchCacheFind(hm, key, data);
    if (found && local_rig_instrument) instrument_cache_hits++;
    return found;
}

static void instrumentReset(void) {
    instrument_cache_lookups = 0;
    instrument_cache_hits = 0;
    instrument_root_evaluations = 0;
    instrument_deepest_plies = 0;
    instrument_selected_score = 0;
    instrument_search = "shallow";
}

static void instrumentRootEvaluated(int plies) {
    if (!local_rig_instrument) return;
    instrument_root_evaluations++;
    if (plies > instrument_deepest_plies) instrument_deepest_plies = plies;
}

static void instrumentWriteTurn(int turn, int budget_ms, int legal_moves,
    Pos selected, unsigned long scored_positions) {
    if (!local_rig_instrument || local_rig_fd < 0) return;
    dprintf(local_rig_fd,
        "TURN_STATS %d %d %.3f %d %d %d %d %d %d %lu %lu %lu %s\n",
        turn, budget_ms, getElaspedTime() * 1000.0, legal_moves,
        instrument_root_evaluations, instrument_deepest_plies,
        selected.y, selected.x, instrument_selected_score, scored_positions,
        instrument_cache_lookups, instrument_cache_hits, instrument_search);
}

#define SEARCH_CACHE_FIND(hm, key, data) instrumentCacheFind(hm, key, data)
#define INSTRUMENT_ROOT_EVALUATED(plies) instrumentRootEvaluated(plies)
#define INSTRUMENT_SELECTED(score) (instrument_selected_score = (score))
#define INSTRUMENT_MODE(name) (instrument_search = (name))
#define INSTRUMENT_RESET() instrumentReset()
#define INSTRUMENT_WRITE(turn, budget, legal, move, evaluations) \
    instrumentWriteTurn(turn, budget, legal, move, evaluations)
#else
#define SEARCH_CACHE_FIND(hm, key, data) searchCacheFind(hm, key, data)
#define INSTRUMENT_ROOT_EVALUATED(plies) ((void)(plies))
#define INSTRUMENT_SELECTED(score) ((void)(score))
#define INSTRUMENT_MODE(name) ((void)(name))
#define INSTRUMENT_RESET() ((void)0)
#define INSTRUMENT_WRITE(turn, budget, legal, move, evaluations) ((void)0)
#endif
