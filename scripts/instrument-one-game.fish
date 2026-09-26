#!/usr/bin/env fish
# Run one instrumented local game and publish a board-per-move HTML report.
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
# The instrumented bot is only intended to finish inside the rig's relaxed
# 5000ms local guard, which is not the 100ms arena target.  The debug build
# (D=1 sanitisers at -O0) exceeded even the local guard and forfeited in the
# observed game, so only L=1 is forced on top of the optimized E=1
# fixed-evaluation build used by the tuning batches; the fixed evaluation cap
# is preserved.
set -l bot1_build D=0 L=1 A=0 E=1 MAX_SCORE=450000
set -l bot1_command "./bin/$bot1"
set -l bot2_command "./bin/$bot2"
if test -n "$bot1_params"
    set bot1_command "$bot1_command $bot1_params"
end
if test -n "$bot2_params"
    set bot2_command "$bot2_command $bot2_params"
end

set -l game_json "$work_dir/game.json"
set -l bot1_build_text (string join ' ' $bot1_build)

printf '%s\n' \
    "bot1 target/name: $bot1" \
    "bot1 literal parameters: $bot1_params" \
    "bot1 command (p0, instrumented): $bot1_command" \
    "bot2 target/name: $bot2" \
    "bot2 literal parameters: $bot2_params" \
    "bot2 command (p1): $bot2_command" \
    "starting player: p0" \
    "instrumented bot build: $bot1_build_text (optimized)" \
    "gamerig diagnostic CFLAGS: $diagnostic_cflags" \
    "gamerig executable: ./bin/gamerig" \
    "logical gamerig options:" \
    "  ./bin/$bot1 \"$bot1_params\"" \
    "  ./bin/$bot2 \"$bot2_params\"" \
    "  0" \
    "  --instrument 0" \
    "  --relaxed 5000" \
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
    tail -n 20 "$work_dir/build-rig.log" >&2
    echo "Instrument work: $work_dir" >&2
    exit $build_status
end

make -B $bot1_build "bin/$bot1" > "$work_dir/build-bot1.log" 2>&1
if test $status -ne 0
    set -l build_status $status
    echo "bot1 build failed" >&2
    tail -n 20 "$work_dir/build-bot1.log" >&2
    echo "Instrument work: $work_dir" >&2
    exit $build_status
end

make -B D=1 L=0 A=1 E=0 "bin/$bot2" > "$work_dir/build-bot2.log" 2>&1
if test $status -ne 0
    set -l build_status $status
    echo "bot2 build failed" >&2
    tail -n 20 "$work_dir/build-bot2.log" >&2
    echo "Instrument work: $work_dir" >&2
    exit $build_status
end

set -l hello (./bin/$bot1 --HELLO)
if test $status -ne 0
    echo "bot1 --HELLO failed" >&2
    echo "Instrument work: $work_dir" >&2
    exit $status
end
if not string match -q '*L1*' -- "$hello"
    echo "bot1 --HELLO does not advertise instrumentation (L1): $hello" >&2
    echo "Instrument work: $work_dir" >&2
    exit 2
end

# Start each game from an empty experiment log (creates it if absent) so the
# bot only ever appends; it carries no file-lifecycle code.
if set -q NEGAMAX_RESULTS; and test -n "$NEGAMAX_RESULTS"
    truncate -s 0 -- "$NEGAMAX_RESULTS"
end

./bin/gamerig "./bin/$bot1" "$bot1_params" "./bin/$bot2" "$bot2_params" 0 \
    --instrument 0 --relaxed 5000 \
    > "$game_json" 2> "$work_dir/game.log"
set -l game_status $status

if test $game_status -ne 0
    echo '=== INSTRUMENT GAME FAILURE OUTPUT ==='
    tail -n 80 "$work_dir/game.log"
    echo '=== END FAILURE OUTPUT ==='
    if test -s "$game_json"
        python3 scripts/instrument-report.py "$game_json" "$work_dir/report.html"
    end
    echo "Instrument game failed with status $game_status; raw files: $work_dir" >&2
    echo "Instrument work: $work_dir" >&2
    exit $game_status
end

python3 scripts/instrument-report.py "$game_json" "$work_dir/report.html"
if test $status -ne 0
    set -l render_status $status
    echo "Report rendering failed; instrument work: $work_dir" >&2
    exit $render_status
end

mkdir -p -- "$output"
or begin
    set -l setup_status $status
    echo "Could not create instrument report directory: $output" >&2
    exit $setup_status
end
source scripts/rotate-report.fish
rotate_report "$output/report.html"
or begin
    set -l setup_status $status
    echo "Could not rotate instrument report; $output/report.html" >&2
    exit $setup_status
end
cp -- "$work_dir/report.html" "$output/report.html"
or begin
    set -l setup_status $status
    echo "Could not publish instrument report; $output/report.html" >&2
    exit $setup_status
end

# Publish a CSV view of the binary experiment log, if one was configured.
if set -q NEGAMAX_RESULTS; and test -n "$NEGAMAX_RESULTS"
    python3 scripts/edata-csv.py "$NEGAMAX_RESULTS" "$NEGAMAX_RESULTS.csv"
end

# gamerig exits 0 for a completed forfeit, so process status alone does not
# prove a completed game: read the result type before reporting PASS.
set -l result_line (python3 -c 'import json,sys; r=json.load(open(sys.argv[1]))["result"]; print(r["result type"], r["winner"])' "$game_json")
if test $status -ne 0
    set -l parse_status $status
    echo "Could not read the game result from $game_json" >&2
    echo "Instrument work: $work_dir" >&2
    exit $parse_status
end
set -l result_parts (string split ' ' -- "$result_line")
set -l result_type $result_parts[1]
set -l winner $result_parts[2]

if test "$result_type" = 'Forfeit'
    # gamerig records the forfeiter's opponent as the winner, so the forfeiting
    # player is 1 - winner; the log line names it directly as well.
    set -l forfeiter unknown
    if string match -qr '^[01]$' -- "$winner"
        set forfeiter (math 1 - $winner)
    end
    echo '=== INSTRUMENT GAME TECHNICAL FAILURE: FORFEIT ===' >&2
    echo "gamerig reported a Forfeit: p$forfeiter forfeited (winner p$winner)." >&2
    grep -m 1 -i 'forfeit' "$work_dir/game.log" >&2
    echo '=== END FORFEIT OUTPUT ===' >&2
    echo 'Instrument game: FAIL (technical forfeit)' >&2
    echo "Instrument work: $work_dir" >&2
    echo "Instrument report: $output/report.html" >&2
    exit 1
end

echo 'Instrument game: PASS'
echo "Instrument work: $work_dir"
echo "Instrument report: $output/report.html"
