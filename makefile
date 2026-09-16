CC = gcc
CFLAGS ?= -Wall -g
LOCAL_ARCH ?= -march=native
ENGINE_DIR := src/engine
BOT_DIR := src/bots
RIG_DIR := src/rig
LEGACY_DIR := src/legacy
TOOL_DIR := tools/c
CPPFLAGS := -I$(ENGINE_DIR)

.PHONY: all submission test tools help prune-experiments
all: bin/gamerig bin/orig bin/ai_random bin/ai_minimax

help:
	@echo "make              Build the supported local bots and match runner"
	@echo "make test         Run current board/search regression tests"
	@echo "make submission   Generate bin/submission.c for CodinGame"
	@echo "make tools        Build optional local analysis tools"
	@echo "make prune-experiments  Delete raw artifacts older than 14 days"

bin:
	mkdir -p bin

bin/gamerig: $(RIG_DIR)/gamerig.c $(ENGINE_DIR)/board.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

bin/orig: $(LEGACY_DIR)/orig.c $(LEGACY_DIR)/orig_identified.c instrument-orig.py | bin
	python3 instrument-orig.py
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. -DBOT_BUILD_ID=\"$$(sha256sum $(LEGACY_DIR)/orig.c $(LEGACY_DIR)/orig_identified.c instrument-orig.py | sha256sum | cut -c1-16)\" $(LEGACY_DIR)/orig_identified.c -o $@

bin/ai_random: $(BOT_DIR)/ai_random.c | bin
	$(CC) $(CFLAGS) $< -o $@

bin/ai_minimax: $(BOT_DIR)/ai_minimax.c $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOCAL_ARCH) -DBOT_BUILD_ID=\"$$(sha256sum $(BOT_DIR)/ai_minimax.c $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h | sha256sum | cut -c1-16)\" $(mini) $< -o $@

# Keep the recovered cg_tictac.c snapshot intact.
submission: bin/submission.c

bin/submission.c: $(BOT_DIR)/ai_minimax.c $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h subst | bin
	bash subst > $@.tmp
	mv $@.tmp $@

bin/cg_tictac: $(LEGACY_DIR)/cg_tictac.c | bin
	$(CC) $(CFLAGS) $< -o $@

# One-lever experimental variants; defaults remain unchanged.
bin/ai_uniform_opening: $(BOT_DIR)/ai_minimax.c $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOCAL_ARCH) -DUNIFORM_OPENING_SELECTION $< -o $@

bin/ai_search_start: $(BOT_DIR)/ai_minimax.c $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOCAL_ARCH) -DBOT_BUILD_ID=\"$$(sha256sum $(BOT_DIR)/ai_minimax.c $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h | sha256sum | cut -c1-16)\" -DOPENING_SEARCH_THRESHOLD=82 $< -o $@

bin/ai_search_debug: $(BOT_DIR)/ai_minimax.c $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOCAL_ARCH) -DDEBUG -DBOT_BUILD_ID=\"$$(sha256sum $(BOT_DIR)/ai_minimax.c $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h | sha256sum | cut -c1-16)\" -DOPENING_SEARCH_THRESHOLD=82 $< -o $@

bin/heatdump: $(TOOL_DIR)/heatdump.c $(BOT_DIR)/ai_minimax.c $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOCAL_ARCH) $< -o $@

bin/test_current: tests/current_bot.c $(BOT_DIR)/ai_minimax.c $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOCAL_ARCH) $< -o $@

test: bin/test_current
	./bin/test_current

tools: bin/heatdump

prune-experiments:
	bash tools/prune-experiments.sh
