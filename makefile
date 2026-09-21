CC = gcc
CFLAGS ?= -Wall -g
CFLAGS += -std=c17
ifeq ($(BOARD_ASSERTS),1)
CFLAGS += -DBOARD_ASSERTS=1
endif
LOCAL_ARCH ?= -march=native
LOCAL_RIG ?= 0
ENGINE_DIR := src/engine
BOT_DIR := src/bots
RIG_DIR := src/rig
LEGACY_DIR := src/legacy
TOOL_DIR := tools/c
CPPFLAGS := -I$(ENGINE_DIR)
DEBUG_CFLAGS := -Wall -Wextra -g3 -O0
OPTIMIZED_CFLAGS := -Wall -O3

.PHONY: all submission test test-board2 test-board2-stress tools help prune-experiments \
	bin/ai_minimax bin/ai_negamax \
	debug-ai_random optimized-ai_random debug-ai_negamax optimized-ai_negamax
all: bin/gamerig bin/orig bin/ai_random bin/ai_minimax

help:
	@echo "make              Build the supported local bots and match runner"
	@echo "make test         Run current board/search regression tests"
	@echo "make submission   Generate bin/submission.c for CodinGame"
	@echo "make tools        Build optional local analysis tools"
	@echo "make LOCAL_RIG=1 bin/ai_minimax  Enable local rig C&C (including instrumentation commands)"
	@echo "make debug-ai_random / optimized-ai_random    Force a debug / optimized random-bot build"
	@echo "make debug-ai_negamax / optimized-ai_negamax  Force a debug+assert+instrument / optimized negamax build"
	@echo "make prune-experiments  Delete raw artifacts older than 14 days"

bin:
	mkdir -p bin

bin/gamerig: $(RIG_DIR)/gamerig.c $(ENGINE_DIR)/board.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

bin/orig: $(LEGACY_DIR)/orig.c | bin
	$(CC) $(CFLAGS) $< -o $@

bin/ai_random: $(BOT_DIR)/ai_random.c | bin
	$(CC) $(CFLAGS) $< -o $@

bin/ai_minimax: $(BOT_DIR)/ai_minimax.c $(BOT_DIR)/instrument.h $(BOT_DIR)/local_rig.h $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h | bin
	rm -f $@
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOCAL_ARCH) $(if $(filter 1,$(LOCAL_RIG)),-DLOCAL_RIG,) $< -o $@

bin/ai_negamax: $(BOT_DIR)/ai_negamax.c $(BOT_DIR)/instrument.h $(BOT_DIR)/local_rig.h $(ENGINE_DIR)/board2.h $(ENGINE_DIR)/support.h $(ENGINE_DIR)/hashmap.h | bin
	rm -f $@
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOCAL_ARCH) $(if $(filter 1,$(LOCAL_RIG)),-DLOCAL_RIG,) $< -o $@

debug-ai_random:
	$(MAKE) -B CFLAGS='$(DEBUG_CFLAGS)' bin/ai_random

optimized-ai_random:
	$(MAKE) -B CFLAGS='$(OPTIMIZED_CFLAGS)' bin/ai_random

debug-ai_negamax:
	$(MAKE) -B CFLAGS='$(DEBUG_CFLAGS)' BOARD_ASSERTS=1 LOCAL_RIG=1 bin/ai_negamax

optimized-ai_negamax:
	$(MAKE) -B CFLAGS='$(OPTIMIZED_CFLAGS)' BOARD_ASSERTS=0 LOCAL_RIG=0 bin/ai_negamax

# Keep the recovered cg_tictac.c snapshot intact.
submission: bin/submission.c

bin/submission.c: $(BOT_DIR)/ai_minimax.c $(BOT_DIR)/local_rig.h $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h scripts/subst | bin
	bash scripts/subst > $@.tmp
	mv $@.tmp $@

bin/cg_tictac: $(LEGACY_DIR)/cg_tictac.c | bin
	$(CC) $(CFLAGS) $< -o $@

bin/heatdump: $(TOOL_DIR)/heatdump.c $(BOT_DIR)/ai_minimax.c $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOCAL_ARCH) $< -o $@

bin/test_current: tests/current_bot.c $(BOT_DIR)/ai_minimax.c $(ENGINE_DIR)/board.h $(ENGINE_DIR)/hashmap.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOCAL_ARCH) $< -o $@

BOARD2_TEST_SOURCES := tests/board2_test.c tests/board2_test.h tests/test_support.h $(ENGINE_DIR)/board2.h $(ENGINE_DIR)/support.h
BOARD2_TEST_FLAGS := -std=c17 -Wall -Wextra -Werror -g3 -O0 -I$(ENGINE_DIR)
BOARD2_PROOF_BLOBS := $(wildcard tests/vectors/*.expected.bin)
BOARD2_PROOF_CHECKED := tests/vectors/proof_checked

bin/board2_test: $(BOARD2_TEST_SOURCES) makefile | bin
	$(CC) $(BOARD2_TEST_FLAGS) -DBOARD_ASSERTS=1 tests/board2_test.c -o $@

bin/board2_stress: tests/board2_stress.c $(ENGINE_DIR)/board2.h $(ENGINE_DIR)/support.h makefile | bin
	$(CC) $(BOARD2_TEST_FLAGS) -DBOARD_ASSERTS=1 tests/board2_stress.c -o $@


$(BOARD2_PROOF_CHECKED): tests/verify_board2_vectors.py $(BOARD2_PROOF_BLOBS)
	mkdir -p $(dir $@)
	python3 tests/verify_board2_vectors.py > $@.tmp
	date --iso-8601=seconds >> $@.tmp
	mv $@.tmp $@

# Verify the independent proof blobs before building or running their C tests.
test-board2: $(BOARD2_PROOF_CHECKED)
	@echo "=== PROOFS UP TO DATE: $$(tail -n 1 $(BOARD2_PROOF_CHECKED)) ==="
	$(MAKE) bin/board2_test
	./bin/board2_test

test-board2-stress: bin/board2_stress
	./bin/board2_stress 1000000 --summary-only

test: bin/test_current test-board2
	./bin/test_current

tools: bin/heatdump

prune-experiments:
	bash tools/prune-experiments.sh
