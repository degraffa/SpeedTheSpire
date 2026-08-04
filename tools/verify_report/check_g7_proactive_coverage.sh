#!/usr/bin/env bash
# Entry point for the G7 historical-risk regression audit.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
repo=$(cd -- "$script_dir/../.." && pwd)

if [ "$(uname -s)" != Linux ]; then
    exec "$repo/tools/wsl_run.sh" --script \
        tools/verify_report/check_g7_proactive_coverage.sh "$@"
fi

preset=debug
case ${1:-} in
    debug|asan|release) preset=$1; shift ;;
esac

exec python3 "$repo/tools/verify_report/check_g7_proactive_coverage.py" \
    --build-dir "$repo/build/$preset" "$@"
