#!/usr/bin/env bash
# Configure + BUILD one or more CMake presets, and stop there.
#
# WHY IT IS NOT `wsl_run.sh <preset>`: that entry point always runs ctest after
# the build, which is correct for everything written before 2026-09-03 and wrong
# for everything written after it. The owner directive of that date
# (conventions.md 1) retires unit tests as acceptance: an S3 Acceptance block
# says "all six presets BUILD", and the marker of truth is oracle / real-run
# replay. This is the build half of that bar on the WSL side, run as
#
#   tools/wsl_run.sh --script tools/build_presets.sh            # debug asan release
#   tools/wsl_run.sh --script tools/build_presets.sh release    # just one
#
# The Windows half is `cmake --preset win-X && cmake --build --preset win-X`
# through a vcvars64 + LLVM wrapper (conventions 6); it needs no script because
# it does not cross the WSL boundary.
set -euo pipefail
cd "$(dirname "$0")/.."
presets=("$@")
if [ "${#presets[@]}" -eq 0 ]; then presets=(debug asan release); fi
for preset in "${presets[@]}"; do
    echo "=== configure $preset ==="
    cmake --preset "$preset"
    echo "=== build $preset ==="
    cmake --build --preset "$preset"
done
echo "PRESETS BUILT: ${presets[*]}"
