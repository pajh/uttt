#!/usr/bin/env bash
# Launch, inspect, or retrieve the hosted opening-symmetry experiment.
set -euo pipefail

repo=pajh/uttt
workflow=starting-move-finder.yml

usage() {
    cat <<'EOF'
Usage:
  ./github-experiment.sh launch [games-per-policy] [seed]
  ./github-experiment.sh list
  ./github-experiment.sh watch RUN_ID
  ./github-experiment.sh fetch RUN_ID

Defaults: 200 games per policy (3,200 total), seed 20261001.
Fetched artifacts go in published-results/run-RUN_ID/.
EOF
}

action=${1:-}
case "$action" in
    launch)
        games=${2:-200}
        seed=${3:-20261001}
        [[ $games =~ ^[1-9][0-9]*$ ]] || { echo 'games must be a positive integer' >&2; exit 1; }
        [[ $seed =~ ^[0-9]+$ ]] || { echo 'seed must be a nonnegative integer' >&2; exit 1; }
        gh workflow run "$workflow" --repo "$repo" --ref main \
            -f "games_per_policy=$games" -f "base_seed=$seed"
        echo "Submitted $games games for each of 16 policies ($((games*16)) total)."
        echo "Use './github-experiment.sh list' to obtain its run id."
        ;;
    list)
        gh run list --repo "$repo" --workflow "$workflow" --limit 10
        ;;
    watch)
        [[ ${2:-} =~ ^[0-9]+$ ]] || { usage >&2; exit 1; }
        gh run watch "$2" --repo "$repo" --exit-status
        ;;
    fetch)
        [[ ${2:-} =~ ^[0-9]+$ ]] || { usage >&2; exit 1; }
        destination="published-results/run-$2"
        mkdir -p "$destination"
        gh run download "$2" --repo "$repo" \
            --name "starting-classes-$2" --dir "$destination"
        echo "Downloaded to $destination"
        cat "$destination/summary.csv"
        ;;
    *) usage; [[ -z $action ]] || exit 1 ;;
esac
