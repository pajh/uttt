#!/usr/bin/env bash
# Run one rig command with four seed ranges.
# Usage: ./multi-rig.sh [command] [base-seed] [output-directory]
set -u
command=${1:-'./bin/gamerig -G10 --p0 ./bin/ai_search_start --p1 ./bin/orig --quiet-bots'}
seed=${2:-20261001}
[[ $seed =~ ^[0-9]+$ ]] || { echo 'Seed must be a nonnegative integer' >&2; exit 1; }
output=${3:-"run-$(date +%Y%m%d-%H%M%S)-$$"}
mkdir "$output" || exit 1
printf '%s\n' "$command" > "$output/command.txt"
pids=(); failed=0
trap 'for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done; wait; exit 130' INT TERM
for worker in 1 2 3 4; do
    worker_seed=$((seed + (worker-1)*1000000))
    printf '%s,%s\n' "$worker" "$worker_seed" >> "$output/seeds.csv"
    bash -c "$command"' --seed "$1" --games-csv "$2" --moves-csv "$3" --identity-file "$4"' _ \
        "$worker_seed" "$output/worker-$worker-games.csv" "$output/worker-$worker-moves.csv" "$output/worker-$worker-identity.tsv" \
        > "$output/worker-$worker.log" 2>&1 &
    pids+=("$!")
done
printf 'worker,exit_status\n' > "$output/status.csv"
for i in "${!pids[@]}"; do
    status=0; wait "${pids[i]}" || status=$?
    printf '%s,%s\n' "$((i+1))" "$status" >> "$output/status.csv"
    (( status == 0 )) || failed=1
done
python3 summarize-rig.py "$output" || failed=1
echo "Results: $output"
exit "$failed"
