#!/usr/bin/env fish
# Bring back the exact successful request without touching local latest.
if not test -f work/github-request.txt
    echo 'No recorded GitHub request.' >&2
    exit 2
end
set -l request (command cat work/github-request.txt)
if test (count $request) -ne 4
    echo 'Invalid work/github-request.txt' >&2
    exit 2
end
set -l expected_games $request[4]
set -l details (fish scripts/github-status.fish --machine)
or exit $status
set -l fields (string split \t -- "$details")
set -l run_id $fields[1]
if test "$fields[2]" != completed -o "$fields[3]" != success
    echo "GitHub run $run_id is $fields[2] ($fields[3]); no result downloaded." >&2
    exit 2
end
if test -f work/github-latest/run-source.txt
    if rg -F -x -q "run_id=$run_id" work/github-latest/run-source.txt
        echo "GitHub run $run_id is already at reports/github-latest.html and work/github-latest/"
        exit 0
    end
end
set -l download_dir (mktemp -d work/github-download.XXXXXX)
or exit $status
gh run download "$run_id" --name "bot-vs-orig-$run_id" --dir "$download_dir"
or begin
    rm -r -- "$download_dir"
    exit 1
end
set -l incoming_report "$download_dir/reports/latest-analysis.html"
set -l incoming_data "$download_dir/work/latest"
if not test -f "$incoming_report" -a -f "$incoming_data/summary.csv" -a -f "$incoming_data/games.csv"
    echo 'Downloaded artifact is incomplete; existing latest files were not changed.' >&2
    rm -r -- "$download_dir"
    exit 1
end
if not rg -F -x -q "run_id=$run_id" "$incoming_data/run-source.txt"
    echo 'Downloaded artifact does not identify the expected GitHub run.' >&2
    rm -r -- "$download_dir"
    exit 1
end
set -l total_line (rg '^TOTAL,' "$incoming_data/summary.csv")
set -l total_fields (string split , -- "$total_line")
if test (count $total_fields) -lt 2 -o "$total_fields[2]" != "$expected_games"
    echo "Downloaded summary does not have the requested $expected_games games." >&2
    rm -r -- "$download_dir"
    exit 1
end

source scripts/rotate-report.fish
set -l archive_index (next_report_number reports/github-latest.html)
for candidate in (find work -maxdepth 1 -type d -name 'github-latest.*' -printf '%f\n')
    set -l number (string replace -r '^github-latest\.' '' -- "$candidate")
    if string match -qr '^[1-9][0-9]*$' -- "$number"
        if test "$number" -ge "$archive_index"
            set archive_index (math "$number + 1")
        end
    end
end
if test -d work/github-latest
    mv -- work/github-latest "work/github-latest.$archive_index"
    or exit $status
end
rotate_report reports/github-latest.html "$archive_index"
or exit $status
mv -- "$incoming_data" work/github-latest
or exit $status
mv -- "$incoming_report" reports/github-latest.html
or exit $status
rm -r -- "$download_dir"
echo "Retrieved GitHub run $run_id: reports/github-latest.html and work/github-latest/"
