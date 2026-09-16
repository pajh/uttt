#!/usr/bin/env bash
# Remove only ignored, disposable raw experiment runs older than a retention
# period.  Curated published results and progress.md are intentionally outside
# this directory and are never touched.
set -euo pipefail

retention_days="${1:-14}"
run_root="artifacts/runs"

case "$retention_days" in
  *[!0-9]*|'')
    echo "Usage: $0 [retention-days]" >&2
    exit 2
    ;;
esac

if [[ ! -d "$run_root" ]]; then
  exit 0
fi

find "$run_root" -mindepth 1 -maxdepth 1 -type d -mtime "+$retention_days" \
  -print -exec rm -rf -- {} +
