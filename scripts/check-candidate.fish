#!/usr/bin/env fish
# Short deterministic candidate check: build, package, and play two games.
set -l work_dir work/check-candidate
if test -d "$work_dir"
    rm -r -- "$work_dir"
end
mkdir -p -- "$work_dir"

echo 'check-candidate: regression tests'
make test >/dev/null
or exit $status
make LOCAL_RIG=0 bin/gamerig bin/orig bin/ai_minimax >/dev/null
or exit $status

set -l hello (./bin/ai_minimax --HELLO)
if test $status -ne 0 -o (string length -- "$hello") -eq 0
    echo 'check-candidate: --HELLO failed or was empty' >&2
    exit 1
end

make submission >/dev/null
or exit $status
set -l submission_bytes (wc -c < bin/submission.c | string trim)
if test "$submission_bytes" -ge 100000
    echo "check-candidate: submission too large ($submission_bytes bytes)" >&2
    exit 1
end
gcc -fsyntax-only -I src/engine bin/submission.c >/dev/null
or begin
    echo 'check-candidate: generated submission does not compile' >&2
    exit 1
end

set -l seed 20261001
for starter in p0 p1
    set -l csv "$work_dir/$starter-games.csv"
    set -l moves "$work_dir/$starter-moves.csv"
    ./bin/gamerig -G1 --p0 ./bin/ai_minimax --p1 ./bin/orig --p1-game-seed \
        --$starter-first --quiet-bots --seed $seed --games-csv "$csv" \
        --moves-csv "$moves" > "$work_dir/$starter.log" 2>&1
    or begin
        echo "check-candidate: rig failed for $starter" >&2
        exit 1
    end
    set -l reason (tail -n 1 "$csv" | awk -F, '{print $10}')
    if test "$reason" != played
        echo "check-candidate: technical failure for $starter (reason=$reason)" >&2
        exit 1
    end
    set seed (math $seed + 1)
end

echo "check-candidate: PASS (2 games, alternating starts; submission $submission_bytes bytes)"
