#define main historical_main
#include "bin/orig_instrumented.h"
#undef main
#ifndef BOT_BUILD_ID
#define BOT_BUILD_ID "unversioned"
#endif
int main(int argc, char **argv) {
    if (getenv("CG_LOCAL_HELLO")) {
        printf("@BOT\torig-%s\thistorical evaluator; scale=not-supported; rng=time/pid\n",BOT_BUILD_ID);
        fflush(stdout);
    }
    return historical_main(argc,argv);
}
