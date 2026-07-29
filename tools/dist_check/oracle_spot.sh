#!/usr/bin/env bash
set -euo pipefail

# Called through tools/wsl_run.sh --script so the WSL-built simulator and the
# Windows-hosted campaign artifacts are both reachable without hand-rolling a
# wsl command line.
if [[ $# -ne 3 ]]; then
    echo "usage: oracle_spot.sh PRESET CAMPAIGN_DIR REPORT_PATH" >&2
    exit 2
fi
preset="$1"
campaign="$2"
report="$3"
binary="build/${preset}/tools/dist_check/dist_check_spot_sim"
if [[ ! -f "${binary}" ]]; then
    echo "oracle_spot: ${binary} is not built; run tools/wsl_run.sh ${preset} first" >&2
    exit 2
fi
exec python3 -B tools/dist_check/oracle_spot.py \
    --campaign "${campaign}" \
    --sim-bin "${binary}" \
    --out "${report}"
