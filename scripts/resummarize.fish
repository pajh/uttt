#!/usr/bin/env fish
# Rebuild a readable report from saved game files; never starts bots.
set -l run_directory work/latest
if test (count $argv) -gt 0
    set run_directory $argv[1]
end
if not test -d "$run_directory"
    echo "Run directory not found: $run_directory" >&2
    exit 2
end

python3 scripts/summarize-rig.py "$run_directory"
or exit $status
fish scripts/interpret_multi_csv.fish "$run_directory/summary.csv"
or exit $status
