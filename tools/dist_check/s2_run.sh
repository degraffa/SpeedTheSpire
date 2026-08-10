#!/usr/bin/env bash
set -euo pipefail

# S2.44 tier-4 S2 family. Called through tools/wsl_run.sh --script; the preset
# must already be built (wsl_run's ordinary preset mode is the sanctioned
# configure/build/test door), exactly like run.sh beside it.
preset="${1:-release}"
shift || true
binary="build/${preset}/tools/dist_check/dist_check_s2"
if [[ ! -f "${binary}" ]]; then
    echo "dist_check_s2: ${binary} is not built; run tools/wsl_run.sh ${preset} first" >&2
    exit 2
fi
exec "${binary}" "$@"
