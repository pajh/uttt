CC = gcc
CFLAGS ?= -Wall -g
LOCAL_ARCH ?= -march=native

.PHONY: all submission
all: bin/gamerig bin/orig bin/ai_random bin/ai_minimax

bin:
	mkdir -p bin

bin/gamerig: gamerig.c board.h | bin
	$(CC) $(CFLAGS) gamerig.c -o $@

bin/orig: orig.c orig_identified.c instrument-orig.py | bin
	python3 instrument-orig.py
	$(CC) $(CFLAGS) -DBOT_BUILD_ID=\"$$(sha256sum orig.c orig_identified.c instrument-orig.py | sha256sum | cut -c1-16)\" orig_identified.c -o $@

bin/ai_random: ai_random.c | bin
	$(CC) $(CFLAGS) ai_random.c -o $@

bin/ai_minimax: ai_minimax.c board.h hashmap.h | bin
	$(CC) $(CFLAGS) $(LOCAL_ARCH) -DBOT_BUILD_ID=\"$$(sha256sum ai_minimax.c board.h hashmap.h | sha256sum | cut -c1-16)\" $(mini) ai_minimax.c -o $@

# Keep the recovered cg_tictac.c snapshot intact.
submission: bin/submission.c

bin/submission.c: ai_minimax.c board.h hashmap.h subst | bin
	bash subst > $@.tmp
	mv $@.tmp $@

bin/cg_tictac: cg_tictac.c | bin
	$(CC) $(CFLAGS) cg_tictac.c -o $@

# One-lever experimental variants; defaults remain unchanged.
bin/ai_uniform_opening: ai_minimax.c board.h hashmap.h | bin
	$(CC) $(CFLAGS) $(LOCAL_ARCH) -DUNIFORM_OPENING_SELECTION ai_minimax.c -o $@

bin/ai_search_start: ai_minimax.c board.h hashmap.h | bin
	$(CC) $(CFLAGS) $(LOCAL_ARCH) -DBOT_BUILD_ID=\"$$(sha256sum ai_minimax.c board.h hashmap.h | sha256sum | cut -c1-16)\" -DOPENING_SEARCH_THRESHOLD=82 ai_minimax.c -o $@

bin/heatdump: heatdump.c ai_minimax.c board.h hashmap.h | bin
	$(CC) $(CFLAGS) $(LOCAL_ARCH) heatdump.c -o $@
