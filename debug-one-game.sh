#!/usr/bin/env bash
set -euo pipefail

output=${1:-debug-game}
mkdir -p "$output"
: > "$output/turns.csv"

make bin/gamerig bin/orig bin/ai_search_debug
./bin/gamerig -G1 \
    --p0 "./bin/ai_search_debug --uscale=5 --count-scale=0 --exact-primary=17 --exact-narrow=19 --score-mode=ratio --debug-file=$output/turns.csv" \
    --p1 ./bin/orig \
    --quiet-bots \
    --seed 20261001 \
    --games-csv "$output/game.csv" \
    --moves-csv "$output/all-moves.csv" \
    --identity-file "$output/identity.tsv" \
    > "$output/game.log" 2>&1

python3 debug-report.py "$output/turns.csv" "$output/report.html"
echo "Debug report: $output/report.html"
