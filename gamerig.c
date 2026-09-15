#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include <wordexp.h>
#include <sys/wait.h>

#include "board.h"

#define WEIGHTS 6
void error(char *s);
char bcache[2048];
char *bot_paths[2] = {"./bin/orig", "./bin/ai_random"};
pid_t bot_pids[2];
int response_ms = 0; /* Zero allows 20% grace beyond arena limits. */
int quiet_bots = 0;
FILE *identity_file, *usage_file;
char bot_identity[2][2048];
unsigned rig_seed = 20260914;
unsigned game_seed;
Pos forced_opening = {-1, -1};
int fixed_starting_player = -1;
char opening_class[4] = "";
FILE *games_csv, *moves_csv;
int timed_out;
int dfs_failed[2], dfs_failed_primary[2], dfs_failed_narrow[2];
int timeouts[2], overruns[2], started_scores[2][3];
uint64_t response_count[2], response_total[2], max_first[2], max_later[2];

int failures[2];

static int chooseOpeningIndex(char category, unsigned random_value) {
    static const int middle[] = {4};
    static const int diagonal[] = {0, 2, 6, 8};
    static const int cardinal[] = {1, 3, 5, 7};
    const int *choices;
    int count;
    if (category == 'M') { choices = middle; count = 1; }
    else if (category == 'D') { choices = diagonal; count = 4; }
    else { choices = cardinal; count = 4; }
    return choices[random_value % count];
}

static int chooseRelatedOpeningIndex(int grid, char cell_category, char relation, unsigned random_value) {
    if (!relation) return chooseOpeningIndex(cell_category, random_value);
    if (relation == 'S') return grid;
    if (relation == 'O') return 8-grid;
    int gr=grid/3, gc=grid%3, choices[4], count=0;
    for (int cell=0;cell<9;cell++) {
        if (cell==4 || (cell_category=='D') != (cell==0 || cell==2 || cell==6 || cell==8)) continue;
        int distance=abs(gr-cell/3)+abs(gc-cell%3);
        if ((relation=='A' && cell!=grid && cell!=8-grid) ||
            (relation=='N' && distance==1) || (relation=='F' && distance==3))
            choices[count++]=cell;
    }
    if (!count) error("opening relation");
    return choices[random_value % count];
}


void loadWeights(const char* file_name, int weights[], double* win_per)
{
  FILE* file = fopen (file_name, "r");
  fscanf (file, "%d %d %d %d %d %d %lf", &weights[0],&weights[1],&weights[2],&weights[3],&weights[4],&weights[5], win_per );
  fprintf(stderr,"Loaded:");
  for (int i=0;i<WEIGHTS;i++) {
      fprintf(stderr,"[%d=%d]",i,weights[i]);
  }
  fprintf(stderr,"win=%lf\n", *win_per);
  fclose (file);
}

void saveWeights(const char* file_name, int weights[], double* win_per)
{
  FILE* file = fopen (file_name, "w");
  fprintf (file, "%d %d %d %d %d %d ", weights[0],weights[1],weights[2],weights[3],weights[4],weights[5] );

  if (win_per) {
    fprintf (file,"%lf ",*win_per);
  }

  fclose (file);
}


pid_t runAndLink(char* file, char*cmd, char* arg_1, int* read_pipe, int* write_pipe) {
    int in[2], out[2];
    if (pipe(in) < 0 || pipe(out) < 0) error("pipe");
    for (int i=0;i<2;i++) {
        fcntl(in[i], F_SETFD, FD_CLOEXEC);
        fcntl(out[i], F_SETFD, FD_CLOEXEC);
    }
    pid_t pid = fork();
    if (pid < 0) error("fork");
    if (pid == 0) {
        if (dup2(in[0], STDIN_FILENO) < 0 || dup2(out[1], STDOUT_FILENO) < 0)
            _exit(126);
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        char seed[32];
        snprintf(seed,sizeof(seed),"%u",game_seed ^ (cmd == bot_paths[0] ? 0x12345678u : 0x87654321u));
        setenv("CG_SEED",seed,1);
        if (quiet_bots) {
            int null_fd = open("/dev/null",O_WRONLY);
            if (null_fd >= 0) { dup2(null_fd,STDERR_FILENO); close(null_fd); }
        }
        setenv("CG_LOCAL_HELLO","1",1);
        wordexp_t words;
        if (wordexp(file,&words,WRDE_NOCMD) != 0 || !words.we_wordc) _exit(125);
        char *args[words.we_wordc+2];
        for(size_t i=0;i<words.we_wordc;i++) args[i]=words.we_wordv[i];
        size_t n=words.we_wordc;
        if(arg_1 && *arg_1) args[n++]=arg_1;
        args[n]=NULL;
        execvp(args[0],args);
        perror(file);
        _exit(127);
    }
    close(in[0]); close(out[1]);
    *write_pipe = in[1]; *read_pipe = out[0];
    return pid;
}

int writeAll(int fd, const char *text, size_t length) {
    while (length) {
        ssize_t n = write(fd, text, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        text += n; length -= n;
    }
    return 1;
}

uint64_t get_gtod_clock_time(void);
Pos getMove(int player, Pos last_move, Moves *valid_moves, int read_pipe, int write_pipe, int move_no, int limit_ms) {
    Pos failed = {-1,-1};
    timed_out = 0;
    char input[2048];
    int used = snprintf(input, sizeof(input), "%d %d\n%d\n", last_move.y, last_move.x, valid_moves->count);
    for (int i=0;i<valid_moves->count;i++)
        used += snprintf(input+used, sizeof(input)-used, "%d %d\n", valid_moves->moves[i].y, valid_moves->moves[i].x);
    bcache[0] = 0;
    if (!writeAll(write_pipe, input, used)) return failed;
    uint64_t deadline = get_gtod_clock_time() + (uint64_t)limit_ms * 1000;
    size_t length = 0;
    while (length < sizeof(bcache)-1) {
        uint64_t now = get_gtod_clock_time();
        if (now >= deadline) {
            timed_out = 1;
            return failed;
        }
        struct pollfd ready = {read_pipe, POLLIN, 0};
        int result = poll(&ready, 1, (int)((deadline-now+999)/1000));
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) { timed_out = result == 0; return failed; }
        char ch;
        ssize_t n = read(read_pipe, &ch, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n != 1) return failed;
        bcache[length++] = ch; bcache[length] = 0;
        if (ch == '\n') {
            if (!strncmp(bcache,"@DFS_FAILED\t",12)) {
                int spaces,legal;
                char trigger[16];
                if(sscanf(bcache+12,"%d%d%15s",&spaces,&legal,trigger)!=3) return failed;
                dfs_failed[player]++;
                if(!strcmp(trigger,"primary")) dfs_failed_primary[player]++; else if(!strcmp(trigger,"narrow")) dfs_failed_narrow[player]++; else return failed;
                length=0; bcache[0]=0; continue;
            }
            if (strncmp(bcache,"@USE\t",5)==0) {
                unsigned long calls,active;
                if(sscanf(bcache+5,"%lu%lu",&calls,&active)!=2) return failed;
                if(usage_file) { fprintf(usage_file,"%d,%lu,%lu\n",player,calls,active); fflush(usage_file); }
                length=0; bcache[0]=0; continue;
            }
            if (strncmp(bcache,"@BOT\t",5)==0) {
                if (!strchr(bcache+5,'\t')) return failed;
                if (*bot_identity[player] && strcmp(bot_identity[player],bcache)) {
                    fprintf(stderr,"BOT IDENTITY CHANGED: player %d\n",player); return failed;
                }
                if (!*bot_identity[player]) {
                    strcpy(bot_identity[player],bcache);
                    if(identity_file) { fprintf(identity_file,"%d\t%s",player,bcache+5); fflush(identity_file); }
                    fprintf(stderr,"Player %d hello: %s",player,bcache+5);
                }
                length=0; bcache[0]=0; continue;
            }
            if(identity_file && !*bot_identity[player]) {
                fprintf(stderr,"MISSING BOT HELLO: player %d\n",player); return failed;
            }
            Pos p;
            if (sscanf(bcache, "%d%d", &p.y, &p.x) != 2) return failed;
            return p;
        }
    }
    return failed;
}

uint64_t get_gtod_clock_time(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) error("clock_gettime");
    return (uint64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

int playGame(int game, int player, char* p0_arg)
{
    dfs_failed[0]=dfs_failed[1]=0;
    dfs_failed_primary[0]=dfs_failed_primary[1]=0;
    dfs_failed_narrow[0]=dfs_failed_narrow[1]=0;
    int starting_player = player;
    int failure_player = -1;
    const char *reason = "played";
    uint64_t fingerprint = 14695981039346656037ULL;
    game_seed = rig_seed + (unsigned)game;
    srand(game_seed);
    Pos game_opening = forced_opening;
    int opening_grid = -1, opening_cell = -1;
    if (*opening_class) {
        unsigned grid_random=(unsigned)rand(), cell_random=(unsigned)rand();
        opening_grid = chooseOpeningIndex(opening_class[0],grid_random);
        opening_cell = chooseRelatedOpeningIndex(opening_grid,opening_class[1],opening_class[2],cell_random);
        game_opening.x = (opening_grid % 3) * 3 + opening_cell % 3;
        game_opening.y = (opening_grid / 3) * 3 + opening_cell / 3;
    } else if (fixed_starting_player == 0 && game_opening.x < 0) {
        /* Keep the rig's later shuffle stream aligned with class-policy games. */
        (void)rand(); (void)rand();
    } else if (game_opening.x >= 0) {
        opening_grid = (game_opening.y / 3) * 3 + game_opening.x / 3;
        opening_cell = (game_opening.y % 3) * 3 + game_opening.x % 3;
    }
    int read_pipe[2], write_pipe[2];
    int play_counts[2];
    int move_no = 0;
    uint64_t time_spent[2];
    uint64_t slowest_time[2];

    for (int i=0;i<2;i++) {
        play_counts[i] = 0;
        time_spent[i] = 0.0;
        slowest_time[i] = 0.0;
    }

    // Player 0
    bot_pids[0] = runAndLink(bot_paths[0], bot_paths[0], p0_arg, &read_pipe[0], &write_pipe[0] );

    // Player 1
    bot_pids[1] = runAndLink(bot_paths[1], bot_paths[1], "", &read_pipe[1], &write_pipe[1] );

    Board9 board = {0};
    board.winner = -1;
    Moves valid_moves = {0};
    Pos last_move = {-1, -1};

    for (int x = 0; x < 9; x++)
    {
        for (int y = 0; y < 9; y++)
        {
            Pos p = {x, y};
            valid_moves.moves[x + y * 9] = p;
        }
    }

    valid_moves.count = 81;

    while (board.winner < 0)
    {
        if (move_no == 0 && game_opening.x >= 0) {
            valid_moves.count = 1;
            valid_moves.moves[0] = game_opening;
        }

        /* CodinGame shuffles its legal action list; this changes only ordering. */
        for (int i=valid_moves.count-1;i>0;i--) {
            int j = rand() % (i+1);
            Pos tmp = valid_moves.moves[i]; valid_moves.moves[i] = valid_moves.moves[j]; valid_moves.moves[j] = tmp;
        }
        int first_response = play_counts[player] == 0;
        int arena_ms = first_response ? 1000 : 100;
        int limit_ms = response_ms ? response_ms : arena_ms * 120 / 100;
        uint64_t begin = get_gtod_clock_time();

        Pos played = getMove(player, last_move, &valid_moves, read_pipe[player], write_pipe[player], move_no++, limit_ms);

        uint64_t end = get_gtod_clock_time();;
        uint64_t duration = end - begin;
        //double duration_d = (double)duration / 1000000.0;

        response_count[player]++;
        response_total[player] += duration;
        uint64_t *maximum = first_response ? &max_first[player] : &max_later[player];
        if (duration > *maximum) *maximum = duration;
        int overrun = duration > (uint64_t)arena_ms * 1000;
        if (overrun) {
            overruns[player]++;
            fprintf(stderr,"WARNING game %d ply %d p%d: %.3f ms exceeds %d ms nominal limit (local cutoff %d ms)\n",game,move_no-1,player,duration/1000.0,arena_ms,limit_ms);
        }
        if (moves_csv) fprintf(moves_csv,"%d,%d,%d,%d,%d,%d,%llu,%d,%d\n",game,move_no-1,player,played.y,played.x,arena_ms,(unsigned long long)duration,timed_out,overrun);
        if (timed_out || (!response_ms && duration > (uint64_t)limit_ms * 1000)) {
            timeouts[player]++; failures[player]++;
            failure_player = player; reason = "timeout";
            board.winner = 1-player;
            break;
        }
        play_counts[player]++;
        time_spent[player] += duration;

        //if ( duration_d > 0.1 ) {
        //    printf("Slow move for player %d time=%f\n",player,duration_d);
        //}

        if (duration > slowest_time[player]) {
            slowest_time[player] = duration;
        }

        if (!containsMove(&valid_moves, played))
        {
            fprintf(stderr,"Game %d: invalid/failed response from player %d: %s\n",game,player,bcache);
            failure_player = player; reason = "invalid_or_eof";
            failures[player]++;
            board.winner = 1-player;
            break;
        }

        if (move_no == 1 && opening_grid < 0) {
            game_opening = played;
            opening_grid = (played.y / 3) * 3 + played.x / 3;
            opening_cell = (played.y % 3) * 3 + played.x % 3;
        }

        int mx = played.x / 3;
        int my = played.y / 3;

        if ((board.overall_free & mask(mx, my)) != 0)
        {
            printf("Error: player was allowed play in a non free cell (%d,%d)\n", mx, my);
            exit(-1);
        }

        fingerprint ^= (unsigned)(player*81 + played.y*9 + played.x);
        fingerprint *= 1099511628211ULL;
        set9(&board, played.x, played.y, player);

        if (board.winner < 0)
        {
            player = (player == 0) ? 1 : 0;
            validMoves(&board, &valid_moves, played.x % 3, played.y % 3);
            last_move = played;
        }
    }
    #if 0
    if (game == 0) {
        for (int p=0;p<2;p++) {
            double avg = ( (double)time_spent[p] / ((double)play_counts[p] * 1000000.0) );
            double slow = (double)slowest_time[p] / 1000000.0;
            printf("Player %d avg:%f  slowest:%f\n", p, avg, slow);
        }
    }
    #endif

    for (int i=0;i<2;i++) {
        close(write_pipe[i]);
        close(read_pipe[i]);
        /* Bots may not handle EOF; terminate and reap every match process. */
        kill(bot_pids[i], SIGTERM);
        while (waitpid(bot_pids[i], NULL, 0) < 0 && errno == EINTR) {}
    }

    started_scores[starting_player][board.winner]++;
    if (games_csv) {
        const char *win_type = strcmp(reason,"played") ? "forfeit" : board.winner==2 ? "draw" :
            ev_cache[board.overall].p3[board.winner] ? "3iar" : "count";
        fprintf(games_csv,"%d,%u,%d,%d,%d,%d,%d,%d,%d,%s,%d,%016llx,%s,%d,%d,%d,%d,%d,%d\n",game,game_seed,game_opening.y,game_opening.x,opening_grid,opening_cell,starting_player,board.winner,failure_player,reason,move_no,(unsigned long long)fingerprint,win_type,dfs_failed[0],dfs_failed[1],dfs_failed_primary[0],dfs_failed_narrow[0],dfs_failed_primary[1],dfs_failed_narrow[1]);
        fflush(games_csv);
    }
    if (moves_csv) fflush(moves_csv);
    return board.winner;
}


uint64_t playnGames( int games, int scores[], char* p0_arg, bool swap_first ) {

    uint64_t t_begin = get_gtod_clock_time();

    printf(" 0/%d\n", games);
    scores[0] = scores[1] = scores[2] = 0;
    int first_player = fixed_starting_player >= 0 ? fixed_starting_player : 0;

    for (int g=0;g<games;g++) {
        int winner = playGame(g, first_player, p0_arg);
        scores[winner]++;
        if ((g+1)%10 == 0 || g+1 == games) printf("%d/%d: p0=%d p1=%d draws=%d\n",g+1,games,scores[0],scores[1],scores[2]);
        if (swap_first && fixed_starting_player < 0) first_player = (first_player == 0) ? 1 : 0;
    }

    return get_gtod_clock_time() - t_begin;
}

double calcper(int player, int scores[]) {
    double tot = (double) scores[0]+scores[1]+scores[2] ;
    return (double)scores[player] / tot;
}

void copyWeights(int dest[], int src[]) {
    for (int i=0;i<WEIGHTS;i++)
            dest[i] = src[i];
}

void printWeights(int best_weights[], int current_weights[]) {
    fprintf(stderr,"          Best:");
    for (int i=0;i<WEIGHTS;i++) {
        fprintf(stderr," %2d",best_weights[i]);
    }
    fprintf(stderr," Current:");
    for (int i=0;i<WEIGHTS;i++) {
        fprintf(stderr," %2d",current_weights[i]);
    }
}

void calc_best_1st_move(int games_per_session) {

#define MSIZE 27
    Pos corners[3] = { {0,0}, {3,0}, {3,3} };
    Pos pot_moves[MSIZE] = {0};
    int c = 0;

    for (int i=0;i<3;i++) {
        for (int y=0; y<3; y++) {
            for (int x=0;x<3;x++) {
                Pos tmp = {x+corners[i].x,y + corners[i].y};
                pot_moves[c++] = tmp;
            }
        }

    }

    double move_scores[MSIZE] = {};
    double best_score = 0.0;
    int best_move = -1;
    int pscores[3] = {0};

    for (int i=0;i<MSIZE;i++) {
        char buff[256];
        sprintf(buff,"-f%d %d",pot_moves[i].x,pot_moves[i].y);
        playnGames(games_per_session, pscores, buff, false );
        double per0 = calcper(0, pscores);
        move_scores[i] = per0;

        if (per0 > best_score) {
            best_score = per0;
            best_move = i;
            fprintf(stderr,"+");
        }
        fprintf(stderr,"(%d,%d)=%1.4f\n",pot_moves[i].x,pot_moves[i].y,per0);
    }

    fprintf(stderr, "Finished\n");
    for (int i=0;i<MSIZE;i++) {
        if (i == best_move) {fprintf(stderr,">>>");} else {fprintf(stderr,"  ");}
        fprintf(stderr,"(%d,%d)=%1.4f\n",pot_moves[i].x,pot_moves[i].y,move_scores[i]);
    }
    fprintf(stderr,"BEST (%d,%d)=%1.4f\n",pot_moves[best_move].x,pot_moves[best_move].y,best_score);

}

void train(int games_per_session) {

    int scores[3] = {0};
    int best_weights[WEIGHTS], weights[WEIGHTS];
    double best_win = 0.0;
    int dir = 1; // going up
    const char* BW = "best_weights.txt";
    loadWeights(BW, best_weights, &best_win);

    int failed_to_inc = 0;

    while( failed_to_inc < 20 ) {

        for (int w=0;w<WEIGHTS;w++) {

            if (best_weights[w] == 1 && dir == -1) continue;
            copyWeights(weights, best_weights);
            weights[w] += dir;
            saveWeights("weights.txt", weights, NULL);

            playnGames(games_per_session, scores, "-w", true );
            double per0 = calcper(0,scores);
            printWeights(best_weights, weights);

            fprintf(stderr, " p0 win:%d %1.3f(%1.3f)\n", scores[0], per0, best_win);

            if (per0 > (best_win + 0.01)) {
                copyWeights(best_weights, weights);
                best_win = per0;
                saveWeights(BW,best_weights,&best_win);
                fprintf(stderr,"..........Improved %d %d %d %d %d %d %1.3lf\n", best_weights[0],best_weights[1],best_weights[2],best_weights[3],best_weights[4],best_weights[5], best_win);
                failed_to_inc = 0;
            } else {
                failed_to_inc++;
            }
        }
        dir *=-1;
    }
}

void old(int games) {

    int scores[3] = {0};

    uint64_t t_duration = playnGames(games, scores, "", true );

    double tpg = (double)t_duration / (double)games;

    double per0 = calcper(0,scores);
    double per1 = calcper(1,scores);

    printf("Player 1:%d(%1.2f)  Player 2:%d(%1.2f)  Draws:%d tpg:%1.1fs \n", scores[0],per0, scores[1],per1, scores[2],tpg/1000000.0 );
}

int main(int argc,char* argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("GAMERIG 0.2 running ");
    signal(SIGPIPE, SIG_IGN);


    int games = 1;

    for (int i=1;i<argc;i++) {
        if (strncmp(argv[i], "-G", 2) == 0) games = atoi(argv[i]+2);
        else if (strcmp(argv[i], "--p0") == 0 && i+1<argc) bot_paths[0] = argv[++i];
        else if (strcmp(argv[i], "--p1") == 0 && i+1<argc) bot_paths[1] = argv[++i];
        else if (strcmp(argv[i], "--identity-file") == 0 && i+1<argc) {
            identity_file=fopen(argv[++i],"w"); if(!identity_file) error("identity file");
            char *usage_path=malloc(strlen(argv[i])+12);
            sprintf(usage_path,"%s.usage.csv",argv[i]);
            usage_file=fopen(usage_path,"w"); free(usage_path);
            if(!usage_file) error("usage file");
            fputs("player,evaluations,count_differential_evaluations\n",usage_file);
        }
        else if (strcmp(argv[i], "--quiet-bots") == 0) quiet_bots = 1;
        else if (strcmp(argv[i], "--seed") == 0 && i+1<argc) rig_seed = (unsigned)strtoul(argv[++i],NULL,10);
        else if (strcmp(argv[i], "--force-opening") == 0 && i+1<argc) {
            int row, col;
            if (sscanf(argv[++i], "%d,%d", &row, &col) != 2 || row < 0 || row > 8 || col < 0 || col > 8) {
                fputs("--force-opening requires row,col with each coordinate from 0 to 8\n", stderr);
                return 1;
            }
            forced_opening.x = col;
            forced_opening.y = row;
            fixed_starting_player = 0;
        }
        else if (strcmp(argv[i], "--opening-class") == 0 && i+1<argc) {
            const char *value = argv[++i];
            size_t length=strlen(value);
            int valid = (length==2 && strchr("MDC",value[0]) && strchr("MDC",value[1])) ||
                (length==3 && strchr("DC",value[0]) && strchr("DC",value[1]) &&
                 ((value[0]==value[1] && strchr("SOA",value[2])) ||
                  (value[0]!=value[1] && strchr("NF",value[2]))));
            if (!valid) {
                fputs("--opening-class requires MM-style classes, DDS/DDO/DDA or DCN/DCF-style classes\n", stderr);
                return 1;
            }
            strcpy(opening_class,value);
            fixed_starting_player = 0;
        }
        else if (strcmp(argv[i], "--p0-first") == 0) fixed_starting_player = 0;
        else if (strcmp(argv[i], "--games-csv") == 0 && i+1<argc) {
            games_csv = fopen(argv[++i],"w"); if (!games_csv) error("games csv");
            fputs("game,seed,opening_row,opening_col,opening_grid,opening_cell,starting_player,winner,failure_player,reason,plies,trace_hash,win_type,p0_dfs_failed,p1_dfs_failed,p0_dfs_failed_primary,p0_dfs_failed_narrow,p1_dfs_failed_primary,p1_dfs_failed_narrow\n",games_csv);
        }
        else if (strcmp(argv[i], "--moves-csv") == 0 && i+1<argc) {
            moves_csv = fopen(argv[++i],"w"); if (!moves_csv) error("moves csv");
            fputs("game,ply,player,row,col,arena_limit_ms,response_us,timeout,overrun\n",moves_csv);
        }
        else if (strcmp(argv[i], "--timeout-ms") == 0 && i+1<argc) response_ms = atoi(argv[++i]);
        else {
            fprintf(stderr,"Usage: %s [-G<count>] [--p0 executable] [--p1 executable] [--p0-first] [--force-opening row,col | --opening-class MD] [--timeout-ms milliseconds] [--quiet-bots] [--seed integer] [--games-csv path] [--moves-csv path]\n",argv[0]);
            return 1;
        }
    }
    if (games < 1 || response_ms < 0 || (*opening_class && forced_opening.x >= 0)) return 1;
    for (int i=0;i<2;i++) {
        wordexp_t parsed;
        if(wordexp(bot_paths[i],&parsed,WRDE_NOCMD) != 0) error("Invalid quoted bot command");
        if(!parsed.we_wordc) error("Empty bot command");
        wordfree(&parsed);
    }
    if (*opening_class)
        printf("%s versus %s; p0 always starts using class %s; timeout override %d ms (0 = 1200/120, warnings above 1000/100); seed %u\n",bot_paths[0],bot_paths[1],opening_class,response_ms,rig_seed);
    else if (forced_opening.x >= 0)
        printf("%s versus %s; p0 always starts at row %d col %d; timeout override %d ms (0 = 1200/120, warnings above 1000/100); seed %u\n",bot_paths[0],bot_paths[1],forced_opening.y,forced_opening.x,response_ms,rig_seed);
    else if (fixed_starting_player == 0)
        printf("%s versus %s; p0 always starts normally; timeout override %d ms (0 = 1200/120, warnings above 1000/100); seed %u\n",bot_paths[0],bot_paths[1],response_ms,rig_seed);
    else
        printf("%s versus %s; alternating starts; timeout override %d ms (0 = 1200/120, warnings above 1000/100); seed %u\n",bot_paths[0],bot_paths[1],response_ms,rig_seed);

    // Init
    printf("%d games.\n", games);
    initBoardCaches();

    // Init finisged

    //calc_best_1st_move(games);
    //train(games);
    old(games);
    printf("Failed moves/responses: p0=%d p1=%d\n", failures[0], failures[1]);
    for (int p=0;p<2;p++) printf("p%d: responses=%llu mean=%.3f ms max-first=%.3f ms max-later=%.3f ms timeouts=%d arena-overruns=%d\n",p,(unsigned long long)response_count[p],response_count[p] ? response_total[p]/(1000.0*response_count[p]) : 0,max_first[p]/1000.0,max_later[p]/1000.0,timeouts[p],overruns[p]);
    printf("p0 starting: %d/%d/%d; p1 starting: %d/%d/%d (p0 wins / p1 wins / draws)\n",started_scores[0][0],started_scores[0][1],started_scores[0][2],started_scores[1][0],started_scores[1][1],started_scores[1][2]);
    if (games_csv) fclose(games_csv);
    if (moves_csv) fclose(moves_csv);
    return (failures[0] || failures[1]) ? 2 : 0;
}

void error(char *s)
{
  perror(s);
  exit(1);
}
