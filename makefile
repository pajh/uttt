CC = gcc
CFLAGS ?= -Wall -g
CFLAGS += -std=gnu17
ifeq ($(BOARD_ASSERTS),1)
CFLAGS += -DBOARD_ASSERTS=1
endif
LOCAL_ARCH ?= -march=native
# Use 550000 evaluations unless the caller supplies MAX_SCORE explicitly.
MAX_SCORE ?= 450000
# ai_negamax build options, each 0 or 1.  They affect only ai_negamax; orig and
# ai_random always build optimised and ignore them.
#   D=1 debug (sanitisers, -O0); D=0 optimised (-O3)
#   L=1 local C&C / instrumentation (LOCAL_RIG)
#   A=1 assertions (BOARD_ASSERTS)
#   E=1 evaluation timeout (EVALUATION_TIMEOUT + MAX_SCORE)
D ?= 0
L ?= 0
A ?= 0
E ?= 0
NEGAMAX_CFLAGS = $(if $(filter 1,$(D)),$(DEBUG_CFLAGS),$(OPTIMIZED_CFLAGS))
NEGAMAX_DEFS = -DNEGAMAX_DEBUG=$(D) -DNEGAMAX_LOCAL=$(L) -DNEGAMAX_ASSERTS=$(A) -DNEGAMAX_EVAL=$(E)
NEGAMAX_DEFS += $(if $(filter 1,$(L)),-DLOCAL_RIG,)
NEGAMAX_DEFS += $(if $(filter 1,$(A)),-DBOARD_ASSERTS=1,)
NEGAMAX_DEFS += $(if $(filter 1,$(E)),-DEVALUATION_TIMEOUT=1 -DMAX_SCORE=$(MAX_SCORE),)
ENGINE_DIR := src/engine
BOT_DIR := src/bots
RIG_DIR := src/rig
LEGACY_DIR := src/legacy
CPPFLAGS := -I$(ENGINE_DIR)
DEBUG_CFLAGS := -Wall -Wextra -g3 -O0 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
OPTIMIZED_CFLAGS := -Wall -O3

.PHONY: all test test-board2 test-board2-stress help prune-experiments tune-negamax \
	ai_negamax clean bin/ai_negamax bin/ai_failbot
all: bin/gamerig bin/orig bin/ai_random bin/ai_failbot

help:
	@echo "make              Build the supported local bots and match runner"
	@echo "make test         Run the board2 engine regression tests"
	@echo "make ai_negamax D=<0|1> L=<0|1> A=<0|1> E=<0|1>"
	@echo "                  Build negamax (D debug, L local rig, A asserts, E eval timeout)"
	@echo "make clean        Remove generated binaries"
	@echo "make prune-experiments  Delete raw artifacts older than 14 days"

bin:
	mkdir -p bin

bin/gamerig: $(RIG_DIR)/gamerig.c $(ENGINE_DIR)/board.h | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

# orig and ai_random are fixed benchmarks and are always built optimized; a
# global CFLAGS override must not turn them into slow debug binaries.
bin/orig: $(LEGACY_DIR)/orig.c | bin
	$(CC) $(OPTIMIZED_CFLAGS) $(LOCAL_ARCH) -std=gnu17 $< -o $@

bin/ai_random: $(BOT_DIR)/ai_random.c | bin
	$(CC) $(OPTIMIZED_CFLAGS) $(LOCAL_ARCH) -std=gnu17 $< -o $@

bin/ai_failbot: $(BOT_DIR)/ai_failbot.c | bin
	$(CC) $(CFLAGS) $< -o $@

bin/ai_negamax: $(BOT_DIR)/ai_negamax.c $(BOT_DIR)/local_rig.h $(ENGINE_DIR)/board2.h $(ENGINE_DIR)/support.h $(ENGINE_DIR)/hashmap.h | bin
	@case '$(D)$(L)$(A)$(E)' in [01][01][01][01]) ;; *) echo 'D, L, A and E must each be 0 or 1' >&2; exit 2;; esac
	rm -f $@
	$(CC) $(CPPFLAGS) -std=gnu17 $(NEGAMAX_CFLAGS) $(LOCAL_ARCH) $(NEGAMAX_DEFS) $< -o $@

# Always rebuilds (bin/ai_negamax is phony), so a script can pick a build and
# run it without a separate clean step.
ai_negamax: bin/ai_negamax

clean:
	rm -rf bin

tune-negamax:
	@test -n "$(MAX_SCORE)" || { echo 'Usage: make tune-negamax MAX_SCORE=<positive integer>' >&2; exit 2; }
	@case '$(MAX_SCORE)' in *[!0-9]*|''|0*) echo 'MAX_SCORE must be a positive integer without leading zeroes' >&2; exit 2;; esac
	fish scripts/tune-negamax.fish '$(MAX_SCORE)'

BOARD2_TEST_SOURCES := tests/board2_test.c tests/board2_test.h tests/test_support.h $(ENGINE_DIR)/board2.h $(ENGINE_DIR)/support.h
BOARD2_TEST_FLAGS := -std=gnu17 -Wall -Wextra -Werror -g3 -O0 -I$(ENGINE_DIR)
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

test: test-board2

prune-experiments:
	bash tools/prune-experiments.sh
