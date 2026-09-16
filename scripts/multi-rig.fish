#!/usr/bin/env fish
# Run the same bot matchup on several seed ranges.

# ============================== CONFIGURATION ==============================
set -l bot_p0 './bin/ai_minimax'   # Bot being tested; win/loss is from its view.
set -l bot_p1 './bin/orig'         # Opponent.
set -l workers 4                   # Simultaneous rig processes.
set -l games_per_worker 25         # 4 x 25 = 100 games on the laptop.
set -l base_seed 20261001
set -l output work/latest
# ===========================================================================

# GitHub sets these to request larger batches and a workflow artifact folder.
if set -q RIG_WORKERS; set workers $RIG_WORKERS; end
if set -q RIG_GAMES_PER_WORKER; set games_per_worker $RIG_GAMES_PER_WORKER; end
if set -q RIG_BASE_SEED; set base_seed $RIG_BASE_SEED; end
if set -q RIG_OUTPUT; set output $RIG_OUTPUT; end

set -l interactive 0
for option in $argv
    if test "$option" = --interactive
        set interactive 1
    else
        echo "Usage: fish multi-rig.fish [--interactive]" >&2
        exit 2
    end
end
if test "$interactive" -eq 1 -a ! -t 1
    echo 'No terminal available; using plain output.'
    set interactive 0
end
for value in "$workers" "$games_per_worker" "$base_seed"
    if not string match -qr '^[0-9]+$' -- "$value"
        echo 'Workers, games per worker, and seed must be nonnegative integers.' >&2
        exit 2
    end
end
if test "$workers" -lt 1 -o "$games_per_worker" -lt 1
    echo 'Workers and games per worker must be at least one.' >&2
    exit 2
end

set -l rig_command "./bin/gamerig -G$games_per_worker --p0 '$bot_p0' --p1 '$bot_p1' --p1-game-seed --quiet-bots"
make bin/gamerig bin/orig bin/ai_minimax
or exit $status

# Test the complete data path with one game per worker before the long run.
mkdir -p work
set -l preflight work/preflight
if test -d "$preflight"
    rm -r -- "$preflight"
end
mkdir -p -- "$preflight"
printf 'worker,seed\n' > "$preflight/seeds.csv"
set -l preflight_pids
for worker in (seq $workers)
    set -l worker_seed (math "$base_seed + ($worker - 1) * 1000000")
    printf '%s,%s\n' "$worker" "$worker_seed" >> "$preflight/seeds.csv"
    fish -c "$rig_command -G1 --seed $worker_seed --games-csv $preflight/worker-$worker-games.csv --moves-csv $preflight/worker-$worker-moves.csv --identity-file $preflight/worker-$worker-identity.tsv" \
        > "$preflight/worker-$worker.log" 2>&1 &
    set -a preflight_pids $last_pid
end
set -l preflight_failed 0
for worker in (seq $workers)
    wait $preflight_pids[$worker]
    if test $status -ne 0
        echo "Preflight worker $worker failed; see $preflight/worker-$worker.log" >&2
        set preflight_failed 1
    end
end
if test "$preflight_failed" -ne 0
    exit 1
end
python3 scripts/summarize-rig.py "$preflight" > "$preflight/summary.log" 2>&1
or begin
    echo "Preflight summary failed; see $preflight/summary.log" >&2
    exit 1
end
python3 scripts/interpret_multi_csv.py "$preflight/summary.csv" --check
or begin
    echo "Preflight report rendering check failed; see $preflight/summary.csv" >&2
    exit 1
end
rm -r -- "$preflight"

# Keep any failed prior run until it can be summarized successfully.
if test "$output" != work/latest -a -e "$output"
    echo "Custom output already exists; refusing to replace: $output" >&2
    exit 2
end
if test -d "$output"
    if not test -f "$output/summary.csv"
        python3 scripts/summarize-rig.py "$output"
        or begin
            echo "Previous run is not summarized; preserving $output" >&2
            exit 1
        end
    end
    source scripts/rotate-report.fish
    set -l archive_index (next_report_number reports/latest-analysis.html)
    for candidate in (find work -maxdepth 1 -type d -name 'latest.*' -printf '%f\n')
        set -l number (string replace -r '^latest\.' '' -- "$candidate")
        if string match -qr '^[1-9][0-9]*$' -- "$number"
            if test "$number" -ge "$archive_index"
                set archive_index (math "$number + 1")
            end
        end
    end
    mv -- "$output" "work/latest.$archive_index"
    set -gx RIG_ARCHIVE_NUMBER $archive_index
end
mkdir -p -- "$output"
or exit $status
printf '%s\n' "$rig_command" > "$output/command.txt"
printf 'worker,seed\n' > "$output/seeds.csv"
if test "$GITHUB_ACTIONS" = true
    printf 'origin=github\nrun_id=%s\nsha=%s\nurl=%s/%s/actions/runs/%s\n' \
        "$GITHUB_RUN_ID" "$GITHUB_SHA" "$GITHUB_SERVER_URL" "$GITHUB_REPOSITORY" "$GITHUB_RUN_ID" \
        > "$output/run-source.txt"
else
    printf 'origin=local\n' > "$output/run-source.txt"
end
set -l run_started (date +%s)
printf 'started_epoch,finished_epoch\n%s,\n' "$run_started" > "$output/run-timing.csv"

set -l pids
for worker in (seq $workers)
    set -l worker_seed (math "$base_seed + ($worker - 1) * 1000000")
    printf '%s,%s\n' "$worker" "$worker_seed" >> "$output/seeds.csv"
    fish -c "$rig_command --seed $worker_seed --games-csv $output/worker-$worker-games.csv --moves-csv $output/worker-$worker-moves.csv --identity-file $output/worker-$worker-identity.tsv" \
        > "$output/worker-$worker.log" 2>&1 &
    set -a pids $last_pid
end

# The parent alone writes to the terminal.  Workers only write recoverable CSV.
if test "$interactive" -eq 1
    set -l started $run_started
    set -l seen
    set -l symbols
    set -l wins
    set -l finished
    for worker in (seq $workers)
        set -a seen 0
        set -a symbols ''
        set -a wins 0
        set -a finished 0
    end
    printf 'Local run 0s elapsed\n'
    for worker in (seq $workers)
        printf '%d:\n' "$worker"
    end

    while true
        set -l all_done 1
        for worker in (seq $workers)
            set -l csv_file "$output/worker-$worker-games.csv"
            if test -f "$csv_file"
                set -l rows (tail -n +2 -- "$csv_file")
                for index in (seq (math "$seen[$worker] + 1") (count $rows))
                    set -l fields (string split , -- "$rows[$index]")
                    if test (count $fields) -lt 13
                        continue
                    end
                    set -l glyph D
                    set -l color 37
                    if test "$fields[10]" != played
                        set glyph F
                    else if test "$fields[13]" = 3iar
                        set glyph 3
                    else if test "$fields[13]" = count
                        set glyph C
                    end
                    if test "$fields[8]" = 0
                        set color 32
                        set wins[$worker] (math "$wins[$worker] + 1")
                    else if test "$fields[8]" = 1
                        set color 31
                    end
                    set symbols[$worker] "$symbols[$worker]"(printf '\033[%sm%s\033[0m' "$color" "$glyph")
                    set seen[$worker] $index
                end
            end
            set -l process_state (ps -o stat= -p $pids[$worker] 2>/dev/null | string trim)
            if test -n "$process_state" -a (string sub -s 1 -l 1 -- "$process_state") != Z
                set all_done 0
            else
                set finished[$worker] 1
            end
        end

        printf '\033[%dA' (math "$workers + 1")
        printf '\r\033[2KLocal run %ss elapsed\n' (math (date +%s) - $started)
        for worker in (seq $workers)
            printf '\r\033[2K%d:%s' "$worker" "$symbols[$worker]"
            if test "$finished[$worker]" -eq 1 -a "$seen[$worker]" -gt 0
                printf '  %.1f%%' (math -s1 "100 * $wins[$worker] / $seen[$worker]")
            end
            printf '\n'
        end
        if test "$all_done" -eq 1
            break
        end
        sleep 1
    end
end

printf 'worker,exit_status\n' > "$output/status.csv"
set -l failed 0
for worker in (seq $workers)
    wait $pids[$worker]
    set -l worker_status $status
    printf '%s,%s\n' "$worker" "$worker_status" >> "$output/status.csv"
    if test "$worker_status" -ne 0
        set failed 1
    end
end
set -l run_finished (date +%s)
printf 'started_epoch,finished_epoch\n%s,%s\n' "$run_started" "$run_finished" > "$output/run-timing.csv"
if test "$failed" -ne 0
    echo "A worker failed. Raw files remain in $output" >&2
    exit 1
end

if test "$output" = work/latest
    fish scripts/resummarize.fish "$output"
else
    python3 scripts/summarize-rig.py "$output"
    or exit $status
    fish scripts/interpret_multi_csv.fish "$output/summary.csv"
end
or begin
    echo "Summarization failed. Rerun: fish scripts/resummarize.fish $output" >&2
    exit 1
end
