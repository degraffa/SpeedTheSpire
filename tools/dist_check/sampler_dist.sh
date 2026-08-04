#!/usr/bin/env bash
set -euo pipefail

# T0.6 nightly entry point for the belief sampler's distributional suite.
#
# Same shape as run.sh next door: the preset must already be built (wsl_run's
# ordinary preset mode, or a native win-* build, is the sanctioned
# configure/build/test door). The only thing this script adds over running the
# binary by hand is the mode switch -- STS_SAMPLER_DIST_MODE=nightly is what
# lifts the pre-registered sample sizes from the per-commit smoke values to the
# full ones and turns on the seed-filtered hypothesis (H9). The hypothesis
# family, its alpha and the family-wise accounting live in the header of
# tests/sampler_dist_test.cpp and are not settable from here on purpose: a
# nightly job that could dial its own alpha is not a pre-registered test.
#
#   tools/wsl_run.sh release
#   tools/wsl_run.sh --script tools/dist_check/sampler_dist.sh release
#
# Any further arguments are forwarded to the gtest binary (e.g. --gtest_filter).
preset="${1:-release}"
shift || true

# The WSL/GCC presets keep the per-directory layout; the win-* presets put every
# runtime artifact in build/<preset>/bin (root CMakeLists.txt, the gtest DLL
# rule). Both are checked so the same entry point works from either host.
candidates=(
    "build/${preset}/tests/sampler_dist_test"
    "build/${preset}/bin/sampler_dist_test.exe"
    "build/${preset}/tests/sampler_dist_test.exe"
)
binary=""
for candidate in "${candidates[@]}"; do
    if [[ -f "${candidate}" ]]; then
        binary="${candidate}"
        break
    fi
done
if [[ -z "${binary}" ]]; then
    binary="build/${preset}/tests/sampler_dist_test"
    echo "sampler_dist: ${binary} is not built; build the ${preset} preset first" >&2
    exit 2
fi

export STS_SAMPLER_DIST_MODE=nightly
exec "${binary}" "$@"
