#!/usr/bin/env fish
# Tune the negamax evaluation budget: play MAX_SCORE-limited negamax against the
# random bot and report its worst response times.
#
# Usage: fish scripts/tune-negamax.fish <positive MAX_SCORE> [games]
if test (count $argv) -lt 1; or test (count $argv) -gt 2
    echo 'Usage: fish scripts/tune-negamax.fish <positive MAX_SCORE> [games]' >&2
    exit 2
end
if not string match -qr '^[1-9][0-9]*$' -- "$argv[1]"
    echo 'MAX_SCORE must be a positive integer' >&2
    exit 2
end
set -l max_score "$argv[1]"
set -l games 10
if test (count $argv) -eq 2
    if not string match -qr '^[1-9][0-9]*$' -- "$argv[2]"
        echo 'Games must be a positive integer' >&2
        exit 2
    end
    set games "$argv[2]"
end

set -l work_base work/tune-negamax
set -l work_dir "$work_base/latest"
mkdir -p -- "$work_base"
or exit 1

set -l highest_archive 0
for archived_name in (find "$work_base" -mindepth 1 -maxdepth 1 -type d -name 'latest.*' -printf '%f\n')
    set -l archive_number (string replace 'latest.' '' -- "$archived_name")
    if string match -qr '^[1-9][0-9]*$' -- "$archive_number"; and test "$archive_number" -gt "$highest_archive"
        set highest_archive "$archive_number"
    end
end
if test -e "$work_dir"
    set -l archive_dir "$work_base/latest."(math $highest_archive + 1)
    mv -- "$work_dir" "$archive_dir"
    or exit 1
end
mkdir -p -- "$work_dir"
or exit 1

set -l cflags '-Wall -O3 -march=native'
make -B BOARD_ASSERTS=0 LOCAL_RIG=0 "CFLAGS=$cflags" bin/gamerig > "$work_dir/build-rig.log" 2>&1; or begin
    echo "gamerig build failed; see $work_dir/build-rig.log" >&2
    exit 1
end
make -B bin/ai_random > "$work_dir/build-random.log" 2>&1; or begin
    echo "ai_random build failed; see $work_dir/build-random.log" >&2
    exit 1
end
make -B D=0 L=0 A=0 E=1 "MAX_SCORE=$max_score" bin/ai_negamax > "$work_dir/build-negamax.log" 2>&1; or begin
    echo "ai_negamax build failed; see $work_dir/build-negamax.log" >&2
    exit 1
end

set -l base_seed 1
set -l max_first 0
set -l max_later 0
set -l over 0
for game in (seq 0 (math $games - 1))
    set -l game_seed (math $base_seed + $game)
    set -l start (math $game % 2)
    set -l game_json "$work_dir/game-$game.json"
    ./bin/gamerig ./bin/ai_negamax "--seed $game_seed" ./bin/ai_random "--seed $game_seed" \
        $start --relaxed 5000 > "$game_json" 2> "$work_dir/game-$game.log"
    set -l run_status $status
    if test $run_status -ne 0
        echo "gamerig failed for game $game with status $run_status; see $work_dir/game-$game.log" >&2
        exit $run_status
    end

    set -l times (jq -r '.moves[] | select(.player == 0) | .time' "$game_json")
    if test (count $times) -eq 0
        echo "no negamax moves in game $game; see $game_json" >&2
        exit 1
    end
    if test "$times[1]" -gt "$max_first"
        set max_first "$times[1]"
    end
    if test (count $times) -gt 1
        for later in $times[2..-1]
            if test "$later" -gt "$max_later"
                set max_later "$later"
            end
            if test "$later" -gt 100
                set over (math $over + 1)
            end
        end
    end
end

echo "MAX_SCORE=$max_score; negamax max later response=$max_later ms; max first response=$max_first ms; later moves over 100 ms=$over"
echo "Raw data: $work_dir"
