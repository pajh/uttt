#!/usr/bin/env fish
# Dispatch a GitHub batch; 1,000 games by default, or a supplied game count.
set -l games 1000
if test (count $argv) -gt 1
    echo 'Usage: fish scripts/start-github-1000.fish [games]' >&2
    exit 2
end
if test (count $argv) -eq 1
    set games $argv[1]
end
if not string match -qr '^[1-9][0-9]*$' -- "$games"
    echo 'Games must be a positive integer divisible by four.' >&2
    exit 2
end
if test (math "$games % 4") -ne 0
    echo 'Games must be divisible by four (one quarter per worker).' >&2
    exit 2
end

set -l required \
    .github/workflows/bot-vs-orig.yml makefile \
    src/bots/ai_minimax.c src/bots/instrument.h \
    src/engine/board.h src/engine/hashmap.h \
    src/rig/gamerig.c src/legacy/orig.c \
    scripts/multi-rig.fish scripts/resummarize.fish \
    scripts/summarize-rig.py scripts/interpret_multi_csv.fish \
    scripts/interpret_multi_csv.py scripts/rotate-report.fish

set -l dirty (git status --porcelain=v1 --untracked-files=all -- $required)
or exit $status
if test (count $dirty) -gt 0
    echo 'Not launching: commit and push these run inputs first:' >&2
    printf '  %s\n' $dirty >&2
    echo 'This command never commits or pushes for you.' >&2
    exit 2
end

set -l branch (git branch --show-current)
if test -z "$branch"
    echo 'Not launching from detached HEAD; check out a branch first.' >&2
    exit 2
end
set -l local_sha (git rev-parse HEAD)
set -l remote_record (git ls-remote --exit-code origin "refs/heads/$branch")
or begin
    echo "Cannot confirm origin/$branch. Push this branch first." >&2
    exit 2
end
set -l remote_fields (string split \t -- "$remote_record")
if test "$local_sha" != "$remote_fields[1]"
    echo "Not launching: HEAD $local_sha is not origin/$branch $remote_fields[1]." >&2
    echo "Commit and push the current branch, then retry." >&2
    exit 2
end
if not command -sq gh
    echo 'GitHub CLI (gh) is required.' >&2
    exit 2
end

set -l request_id (date -u +%Y%m%dT%H%M%SZ)-(random)
echo "Launching $games games from origin/$branch at $local_sha"
gh workflow run bot-vs-orig.yml --ref "$branch" \
    -f "games=$games" -f base_seed=20261001 -f "request_id=$request_id"
or exit $status
mkdir -p work
printf '%s\n%s\n%s\n%s\n' "$request_id" "$branch" "$local_sha" "$games" > work/github-request.txt
echo "Request: $request_id"
echo 'Check: fish scripts/github-status.fish'
