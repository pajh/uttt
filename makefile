CC = gcc
CFLAGS ?= -Wall -g
LOCAL_ARCH ?= -march=native
INSTRUMENT ?= 0
ENGINE_DIR := src/engine
BOT_DIR := src/bots
RIG_DIR := src/rig
LEGACY_DIR := src/legacy
TOOL_DIR := tools/c
CPPFLAGS := -I$(ENGINE_DIR)

.PHONY: all submission test tools help instrument prune-experiments bin/ai_minimax
all: bin/gamerig bin/orig bin/ai_random bin/ai_minimax

help:
	@echo "make              Build the supported local bots and match runner"
	@echo "make test         Run current board/search regression tests"
	@echo "make submission   Generate bin/submission.c for CodinGame"
	@echo "make tools        Build optional local analysis tools"
	@echo "make instrument  Rebuild ai_minimax with HTML instrumentation enabled"
	@echo "make prune-experiments  Delete raw artifacts older than 14 days"

bin:
	mkdir -p bin

bin/gamerig: $(RIG_DIR)/gamerig.c $(ENGINE_DIR)/board.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

bin/orig: $(LEGACY_DIR)/orig.c | bin
	$(CC) $(CFLAGS) $< -o $@

bin/ai_random: $(BOT_DIR)/ai_random.c | bin
	$(CC) $(CFLAGS) $< -o $@

bin/ai_minimax: $(BOT_DIR)/ai_minimax.c $(BOT_DIR)/instrument.h $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h | bin
	rm -f $@
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOCAL_ARCH) $(if $(filter 1,$(INSTRUMENT)),-DINSTRUMENT,) $< -o $@

# Keep the recovered cg_tictac.c snapshot intact.
submission: bin/submission.c

bin/submission.c: $(BOT_DIR)/ai_minimax.c $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h subst | bin
	bash subst > $@.tmp
	mv $@.tmp $@

bin/cg_tictac: $(LEGACY_DIR)/cg_tictac.c | bin
	$(CC) $(CFLAGS) $< -o $@

instrument:
	$(MAKE) -B INSTRUMENT=1 bin/ai_minimax

bin/heatdump: $(TOOL_DIR)/heatdump.c $(BOT_DIR)/ai_minimax.c $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOCAL_ARCH) $< -o $@

bin/test_current: tests/current_bot.c $(BOT_DIR)/ai_minimax.c $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOCAL_ARCH) $< -o $@

test: bin/test_current
	./bin/test_current

tools: bin/heatdump

prune-experiments:
	bash tools/prune-experiments.sh
