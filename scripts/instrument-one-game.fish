#!/usr/bin/env fish
# Request detailed telemetry through the local rig C&C channel for one game.
set -l output reports/instrument
set -l work_dir work/instrument
mkdir -p -- "$output"
or exit $status
mkdir -p work
if test -d "$work_dir"
    rm -r -- "$work_dir"
end
mkdir -p -- "$work_dir"

make bin/gamerig bin/orig
or exit $status
make LOCAL_RIG=1 bin/ai_minimax
or exit $status
set -l run_at (date '+%Y-%m-%d %H:%M:%S %Z')
set -l hello (./bin/ai_minimax --HELLO)
or exit $status

# --HELLO is a separate preflight call.  The game process must read moves.
fish -c "./bin/gamerig -G1 --p0 ./bin/ai_minimax --p1 ./bin/orig --p1-game-seed --quiet-bots --seed 20261001 --rig-time-ms 75 --instrument-csv $work_dir/turns.csv --scores-csv $work_dir/scores.csv --games-csv $work_dir/game.csv --moves-csv $work_dir/all-moves.csv --identity-file $work_dir/identity.tsv" \
    > "$work_dir/game.log" 2>&1
or begin
    echo "Instrument game failed; raw files: $work_dir" >&2
    exit 1
end

python3 scripts/instrument-report.py "$work_dir/turns.csv" "$work_dir/report.html" "$run_at" "$hello" "$work_dir/scores.csv" "$work_dir/all-moves.csv"
or begin
    echo "Report rendering failed; raw files: $work_dir" >&2
    exit 1
end

python3 scripts/instrument-context.py "$work_dir/turns.csv" "$work_dir/scores.csv" "$work_dir/all-moves.csv" "$work_dir/game.csv" "$work_dir/assistant-view.md" "$hello"
or begin
    echo "Context generation failed; raw files: $work_dir" >&2
    exit 1
end

source scripts/rotate-report.fish
rotate_report "$output/report.html"
or exit $status
mv -- "$work_dir/report.html" "$output/report.html"
or exit $status
echo "Instrument report: $output/report.html"
