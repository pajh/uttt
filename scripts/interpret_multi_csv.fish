#!/usr/bin/env fish
# Render latest summary, or a supplied summary CSV, without running any games.
set -l source work/latest/summary.csv
if test (count $argv) -gt 0
    set source $argv[1]
end
if not test -f "$source"
    echo "Summary not found: $source" >&2
    exit 2
end
mkdir -p reports work
set -l temporary work/interpret-multi.tmp.html
python3 scripts/interpret_multi_csv.py "$source" "$temporary"
or exit $status
source scripts/rotate-report.fish
set -l archive_number
if set -q RIG_ARCHIVE_NUMBER
    set archive_number $RIG_ARCHIVE_NUMBER
end
rotate_report reports/latest-analysis.html "$archive_number"
or exit $status
mv -- "$temporary" reports/latest-analysis.html
or exit $status
echo 'Open reports/latest-analysis.html'
