#!/usr/bin/env bash
# The sanctioned way to run this repo's build/test work in WSL.
#
# WHY THIS EXISTS (conventions.md §6, §7): the harness's Git-Bash layer mangles
# `$VAR` and bare `/mnt/...` arguments forwarded to `wsl.exe`. A hand-rolled
# `wsl -d ... -- bash -lc '...'` line therefore loses shell variables silently —
# an orchestrator loop over `debug asan release` once produced three empty
# results that way. This script never interpolates anything across the boundary:
# it forwards a fixed argv and re-executes itself inside WSL, where all the
# shell code lives.
#
# Usage (identical from Git-Bash on Windows, from cmd/PowerShell via
# tools\wsl_run.cmd, and from inside WSL):
#
#   tools/wsl_run.sh debug                     configure + build + test
#   tools/wsl_run.sh debug asan release        all three, one summary at the end
#   tools/wsl_run.sh release -DSTS_BUILD_BENCHMARKS=ON
#   tools/wsl_run.sh --script tools/bench_ab.sh [args...]   run a script in WSL
#
# `--script` takes a path relative to the repo root (or an absolute `/mnt/...`
# path); everything after it is passed to that script untouched.
#
# Env: STS_WSL_DISTRO (default Ubuntu-2404), STS_JOBS (default 6).
set -euo pipefail

distro=${STS_WSL_DISTRO:-Ubuntu-2404}
jobs=${STS_JOBS:-6}
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)

usage() {  # print the header comment block, minus its leading '# '
    awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "$0"
    exit "${1:-2}"
}
[ $# -gt 0 ] || usage

# ---------------------------------------------------------------- Windows side
# Not Linux => we are in Git-Bash/MSYS on the host. Translate this script's own
# path to /mnt/<drive>/... and hand the SAME argv to the WSL copy of ourselves.
# MSYS_NO_PATHCONV=1 is what stops MSYS rewriting the /mnt/... argument.
if [ "$(uname -s)" != Linux ]; then
    # The boundary eats `$VAR`: `wsl -- bash -c 'printf %s "$1"' _ x` prints
    # nothing, because the interop layer substitutes `$1` before WSL's bash
    # sees it. Nothing here can un-mangle that, so refuse instead of running a
    # command that has quietly lost an argument.
    for arg in "$@"; do
        case $arg in *'$'*)
            echo "wsl_run: refusing '$arg' -- a '\$' in an argument is eaten by" \
                 "the Windows/WSL boundary. Put the shell code in a file and" \
                 "use: wsl_run.sh --script <path>" >&2
            exit 2 ;;
        esac
    done
    win=$(cd -- "$script_dir" && pwd -W)          # e.g. D:/STS_BG_Mod/.../tools
    case $win in
        [A-Za-z]:/*) ;;
        *) echo "wsl_run: cannot map '$win' to a /mnt path" >&2; exit 2 ;;
    esac
    drive=$(printf '%s' "${win%%:*}" | tr 'A-Z' 'a-z')
    exec env MSYS_NO_PATHCONV=1 wsl.exe -d "$distro" -- \
        bash "/mnt/$drive${win#*:}/wsl_run.sh" "$@"
fi

# ------------------------------------------------------------------- WSL side
root=$(cd -- "$script_dir/.." && pwd)
cd "$root"

if [ "$1" = --script ]; then
    shift
    [ $# -gt 0 ] || usage
    case $1 in /*) target=$1 ;; *) target=$root/$1 ;; esac
    shift
    exec bash "$target" "$@"
fi

presets=() cmake_args=()
for arg in "$@"; do
    case $arg in
        -D*) cmake_args+=("$arg") ;;
        -h|--help) usage 0 ;;
        debug|asan|release) presets+=("$arg") ;;
        *) echo "wsl_run: unknown argument '$arg'" >&2; usage ;;
    esac
done
[ ${#presets[@]} -gt 0 ] || usage

summary=() failed=0
for preset in "${presets[@]}"; do
    echo "=== wsl_run: $preset ==="
    log=$(mktemp)
    if cmake --preset "$preset" ${cmake_args[@]+"${cmake_args[@]}"} \
        && cmake --build --preset "$preset" -- -j"$jobs" \
        && ctest --preset "$preset" 2>&1 | tee "$log"
    then
        # `cmake --build` before every ctest is mandatory (conventions §6): the
        # build above is what stops discovery reporting <name>_NOT_BUILT.
        line=$(grep -E 'tests passed' "$log" | tail -1)
        summary+=("$preset: PASS  ${line:-<no ctest summary line>}")
    else
        summary+=("$preset: FAIL")
        failed=1
    fi
    rm -f "$log"
done

echo
echo "=== wsl_run summary ==="
printf '%s\n' "${summary[@]}"
exit $failed
