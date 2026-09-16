#!/usr/bin/env fish
# Build the normal ai_minimax binary with instrumentation and play one game.
set -l output reports/instrument
set -l work_dir work/instrument
mkdir -p -- "$output"
or exit $status
mkdir -p work
if test -d "$work_dir"
    rm -r -- "$work_dir"
end
mkdir -p -- "$work_dir"

make bin/gamerig bin/orig instrument
or exit $status
set -l run_at (date '+%Y-%m-%d %H:%M:%S %Z')
set -l hello (./bin/ai_minimax --HELLO)
or exit $status

# --HELLO is a separate preflight call.  The game process must read moves.
fish -c "./bin/gamerig -G1 --p0 'env INSTRUMENT_FILE=$work_dir/turns.csv ./bin/ai_minimax' --p1 ./bin/orig --quiet-bots --seed 20261001 --games-csv $work_dir/game.csv --moves-csv $work_dir/all-moves.csv --identity-file $work_dir/identity.tsv" \
    > "$work_dir/game.log" 2>&1
or begin
    echo "Instrument game failed; raw files: $work_dir" >&2
    exit 1
end

python3 scripts/instrument-report.py "$work_dir/turns.csv" "$work_dir/report.html" "$run_at" "$hello"
or begin
    echo "Report rendering failed; raw files: $work_dir" >&2
    exit 1
end

source scripts/rotate-report.fish
rotate_report "$output/report.html"
or exit $status
mv -- "$work_dir/report.html" "$output/report.html"
or exit $status
echo "Instrument report: $output/report.html"
