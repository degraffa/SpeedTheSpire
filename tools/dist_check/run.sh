#!/usr/bin/env bash
set -euo pipefail

# Called through tools/wsl_run.sh --script. The preset must already be built;
# wsl_run's ordinary preset mode is the sanctioned configure/build/test door.
preset="${1:-release}"
shift || true
binary="build/${preset}/tools/dist_check/dist_check"
if [[ ! -f "${binary}" ]]; then
    echo "dist_check: ${binary} is not built; run tools/wsl_run.sh ${preset} first" >&2
    exit 2
fi
exec "${binary}" "$@"
