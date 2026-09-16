#!/usr/bin/env fish
# Show the exact GitHub run launched by start-github-1000.fish.
set -l machine 0
if test (count $argv) -gt 0
    if test "$argv[1]" != --machine
        echo 'Usage: fish scripts/github-status.fish [--machine]' >&2
        exit 2
    end
    set machine 1
end
if not test -f work/github-request.txt
    echo 'No recorded GitHub request. Run fish scripts/start-github-1000.fish first.' >&2
    exit 2
end
set -l request (command cat work/github-request.txt)
if test (count $request) -ne 4
    echo 'Invalid work/github-request.txt' >&2
    exit 2
end
set -l request_id $request[1]
set -l branch $request[2]
set -l sha $request[3]
set -l games $request[4]
set -l query ".[] | select(.displayTitle | contains(\"$request_id\")) | select(.headSha == \"$sha\") | [.databaseId,.status,.conclusion,.headSha,.url] | @tsv"
set -l matches (gh run list --workflow bot-vs-orig.yml --branch "$branch" \
    --event workflow_dispatch --limit 100 \
    --json databaseId,displayTitle,status,conclusion,headSha,url --jq "$query")
or exit $status
if test (count $matches) -eq 0
    echo "Request $request_id is not listed yet; try again shortly."
    exit 3
end
set -l fields (string split \t -- "$matches[1]")
if test (count $fields) -ne 5
    echo 'Unexpected GitHub run response.' >&2
    exit 2
end
if test "$machine" -eq 1
    printf '%s\t%s\t%s\t%s\t%s\n' $fields
else
    echo "GitHub run $fields[1] ($games games): $fields[2] ($fields[3])"
    echo "Commit: $fields[4]"
    echo "$fields[5]"
    if test "$fields[2]" = completed -a "$fields[3]" = success
        echo 'Ready: fish scripts/retrieve-github-latest-1000.fish'
    end
end
