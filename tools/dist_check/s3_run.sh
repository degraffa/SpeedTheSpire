#!/usr/bin/env bash
set -euo pipefail

# S3.63 tier-4 S3 family. Called through tools/wsl_run.sh --script; the preset
# must already be built (wsl_run's ordinary preset mode -- or
# tools/build_presets.sh under the post-2026-09-03 no-unit-tests directive --
# is the sanctioned configure/build door), exactly like run.sh / s2_run.sh
# beside it.
preset="${1:-release}"
shift || true
binary="build/${preset}/tools/dist_check/dist_check_s3"
if [[ ! -f "${binary}" ]]; then
    echo "dist_check_s3: ${binary} is not built; run tools/wsl_run.sh ${preset} (or tools/build_presets.sh ${preset}) first" >&2
    exit 2
fi
exec "${binary}" "$@"
