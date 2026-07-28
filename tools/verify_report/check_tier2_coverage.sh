#!/usr/bin/env bash
# Entry point for the tier-2 registry coverage check (G6 checklist leg 1).
#
# Usage:
#   tools/verify_report/check_tier2_coverage.sh [preset] [extra args...]
#
#   preset      debug (default) | asan | release -- selects build/<preset>,
#               which must have been built AND tested already:
#                   tools/wsl_run.sh debug
#   extra args  passed through to check_tier2_coverage.py
#               (e.g. --run-ctest to re-run the suite now).
#
# From Windows (Git-Bash / cmd / PowerShell) this re-executes itself inside WSL
# through the sanctioned tools/wsl_run.sh --script bridge (conventions.md §6);
# ctest and the report both live on the WSL side of the boundary.
#
# Exit: 0 = 100% coverage, 1 = uncovered rows / failing attributed tests,
#       2 = usage or environment error.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
repo=$(cd -- "$script_dir/../.." && pwd)

if [ "$(uname -s)" != Linux ]; then
    exec "$repo/tools/wsl_run.sh" --script \
        tools/verify_report/check_tier2_coverage.sh "$@"
fi

preset=debug
case ${1:-} in
    debug|asan|release) preset=$1; shift ;;
esac

exec python3 "$repo/tools/verify_report/check_tier2_coverage.py" \
    --build-dir "$repo/build/$preset" "$@"
