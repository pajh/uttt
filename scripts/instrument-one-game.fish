#!/usr/bin/env fish
# Request detailed telemetry through the local rig C&C channel for one game.
if test (count $argv) -ne 4
    echo 'Usage: fish scripts/instrument-one-game.fish bot1 "bot1 parameters" bot2 "bot2 parameters"' >&2
    exit 2
end

set -l bot1 "$argv[1]"
set -l bot1_params "$argv[2]"
set -l bot2 "$argv[3]"
set -l bot2_params "$argv[4]"
for bot in "$bot1" "$bot2"
    if not string match -qr '^[A-Za-z0-9_-]+$' -- "$bot"
        echo "Invalid bot target name: $bot" >&2
        exit 2
    end
end

set -l output reports/instrument
set -l work_base work/instrument
set -l work_dir "$work_base/latest"
mkdir -p -- "$work_base"
or begin
    set -l setup_status $status
    echo "Could not create instrument work directory: $work_base" >&2
    exit $setup_status
end

set -l highest_archive 0
for archived_name in (find "$work_base" -mindepth 1 -maxdepth 1 -type d -name 'latest.*' -printf '%f\n')
    set -l archive_number (string replace 'latest.' '' -- "$archived_name")
    if string match -qr '^[1-9][0-9]*$' -- "$archive_number"
        if test "$archive_number" -gt "$highest_archive"
            set highest_archive "$archive_number"
        end
    end
end

if test -e "$work_dir"
    set -l archive_number (math "$highest_archive + 1")
    mv -- "$work_dir" "$work_base/latest.$archive_number"
    or begin
        set -l setup_status $status
        echo "Could not archive prior instrument work directory: $work_dir" >&2
        exit $setup_status
    end
end

mkdir -p -- "$work_dir"
or begin
    set -l setup_status $status
    echo "Could not create instrument work directory: $work_dir" >&2
    exit $setup_status
end

set -l diagnostic_cflags '-Wall -Wextra -g3 -O0 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
set -l bot1_command "./bin/$bot1"
set -l bot2_command "./bin/$bot2"
if test -n "$bot1_params"
    set bot1_command "$bot1_command $bot1_params"
end
if test -n "$bot2_params"
    set bot2_command "$bot2_command $bot2_params"
end

set -l turns_csv "$work_dir/turns.csv"
set -l scores_csv "$work_dir/scores.csv"
set -l game_csv "$work_dir/game.csv"
set -l moves_csv "$work_dir/all-moves.csv"
set -l identity_file "$work_dir/identity.tsv"

printf '%s\n' \
    "bot1 target/name: $bot1" \
    "bot1 literal parameters: $bot1_params" \
    "bot1 command (p0, instrumented): $bot1_command" \
    "bot2 target/name: $bot2" \
    "bot2 literal parameters: $bot2_params" \
    "bot2 command (p1): $bot2_command" \
    "fixed referee seed: 1" \
    "starting player: p0" \
    "diagnostic CFLAGS: $diagnostic_cflags" \
    "gamerig executable: ./bin/gamerig" \
    "logical gamerig options:" \
    "  -G1" \
    "  --p0 $bot1_command" \
    "  --p1 $bot2_command" \
    "  --seed 1" \
    "  --p0-first" \
    "  --timeout-ms 5000" \
    "  --instrument-csv $turns_csv" \
    "  --scores-csv $scores_csv" \
    "  --games-csv $game_csv" \
    "  --moves-csv $moves_csv" \
    "  --identity-file $identity_file" \
    > "$work_dir/command.txt"
or begin
    set -l setup_status $status
    echo "Could not write command record; instrument work: $work_dir" >&2
    exit $setup_status
end

make -B BOARD_ASSERTS=1 LOCAL_RIG=0 "CFLAGS=$diagnostic_cflags" bin/gamerig > "$work_dir/build-rig.log" 2>&1
if test $status -ne 0
    set -l build_status $status
    echo "gamerig build failed" >&2
    echo "Instrument work: $work_dir" >&2
    exit $build_status
end

make -B BOARD_ASSERTS=1 LOCAL_RIG=1 "CFLAGS=$diagnostic_cflags" "bin/$bot1" > "$work_dir/build-bot1.log" 2>&1
if test $status -ne 0
    set -l build_status $status
    echo "bot1 build failed" >&2
    echo "Instrument work: $work_dir" >&2
    exit $build_status
end

make -B BOARD_ASSERTS=1 LOCAL_RIG=0 "CFLAGS=$diagnostic_cflags" "bin/$bot2" > "$work_dir/build-bot2.log" 2>&1
if test $status -ne 0
    set -l build_status $status
    echo "bot2 build failed" >&2
    echo "Instrument work: $work_dir" >&2
    exit $build_status
end

set -l run_at (date '+%Y-%m-%d %H:%M:%S %Z')
set -l hello (./bin/$bot1 --HELLO)
if test $status -ne 0
    set -l hello_status $status
    echo "bot1 --HELLO failed" >&2
    echo "Instrument work: $work_dir" >&2
    exit $hello_status
end
if test -z "$hello"
    echo "bot1 --HELLO returned an empty result" >&2
    echo "Instrument work: $work_dir" >&2
    exit 1
end

# --HELLO is a separate preflight call. The game process must read moves.
./bin/gamerig -G1 \
    --p0 "$bot1_command" \
    --p1 "$bot2_command" \
    --seed 1 \
    --p0-first \
    --timeout-ms 5000 \
    --instrument-csv "$turns_csv" \
    --scores-csv "$scores_csv" \
    --games-csv "$game_csv" \
    --moves-csv "$moves_csv" \
    --identity-file "$identity_file" \
    > "$work_dir/game.log" 2>&1
set -l game_status $status

if test $game_status -ne 0
    if test -s "$turns_csv"; and test -s "$scores_csv"; and test -s "$moves_csv"
        python3 scripts/instrument-report.py "$turns_csv" "$work_dir/report.html" "$run_at" "$hello" "$scores_csv" "$moves_csv"
    end
    if test -s "$turns_csv"; and test -s "$scores_csv"; and test -s "$moves_csv"; and test -s "$game_csv"
        python3 scripts/instrument-context.py "$turns_csv" "$scores_csv" "$moves_csv" "$game_csv" "$work_dir/assistant-view.md" "$hello"
    end
    echo "Instrument game failed with status $game_status; raw files: $work_dir" >&2
    echo "Instrument work: $work_dir" >&2
    exit $game_status
end

python3 scripts/instrument-report.py "$turns_csv" "$work_dir/report.html" "$run_at" "$hello" "$scores_csv" "$moves_csv"
if test $status -ne 0
    set -l render_status $status
    echo "Report rendering failed; instrument work: $work_dir" >&2
    exit $render_status
end

python3 scripts/instrument-context.py "$turns_csv" "$scores_csv" "$moves_csv" "$game_csv" "$work_dir/assistant-view.md" "$hello"
if test $status -ne 0
    set -l context_status $status
    echo "Context generation failed; instrument work: $work_dir" >&2
    exit $context_status
end

mkdir -p -- "$output"
or begin
    set -l setup_status $status
    echo "Could not create instrument report directory; instrument work: $work_dir" >&2
    exit $setup_status
end
source scripts/rotate-report.fish
rotate_report "$output/report.html"
or begin
    set -l setup_status $status
    echo "Could not rotate instrument report; instrument work: $work_dir" >&2
    exit $setup_status
end
cp -- "$work_dir/report.html" "$output/report.html"
or begin
    set -l setup_status $status
    echo "Could not publish instrument report; instrument work: $work_dir" >&2
    exit $setup_status
end

echo "Instrument work: $work_dir"
echo "Instrument report: $output/report.html"
