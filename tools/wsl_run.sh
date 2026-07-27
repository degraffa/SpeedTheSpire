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
# Env: STS_WSL_DISTRO (default Ubuntu-2404), STS_JOBS (default: see below),
#      STS_TEST_JOBS (default: same as STS_JOBS).
#
# PARALLELISM, AND WHY IT IS GATED ACROSS INVOCATIONS
#
# This repo is worked by SEVERAL AGENTS AT ONCE, each in its own worktree, each
# running its own copy of this script. Nothing any single invocation knows tells
# it how many siblings exist, so a per-invocation job count is guaranteed to
# oversubscribe: the old fixed `STS_JOBS:-6` became 24 build jobs on a 16-core
# box as soon as four agents were active, and each of them thought it was being
# modest.
#
# So the job count is not a constant here. It is drawn from a machine-wide token
# pool under /dev/shm (flock, one lock file per token), sized to the core count
# and shared by every concurrent wsl_run.sh on the box. An invocation takes what
# is free, down to a floor of 2 so it can never deadlock behind its siblings, and
# releases on exit -- including on Ctrl-C, via the trap. A caller that passes
# STS_JOBS explicitly still gets exactly that number: the override is preserved
# deliberately, because bisecting a build problem sometimes needs -j1.
#
# The gate prints what it took and what it waited for, because an agent staring
# at a silent terminal cannot tell "waiting for cores" from "hung" -- and a
# hang is what it would report.
#
# ctest ALSO runs in parallel now. It never did: the build got -j and the tests
# did not, so ~640 test binaries (gtest_discover_tests registers one ctest entry
# per gtest CASE) were launched strictly serially. That is process-launch bound,
# not CPU bound, which is why the test job count is allowed to exceed the build
# token count.
set -euo pipefail

distro=${STS_WSL_DISTRO:-Ubuntu-2404}
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

# ----------------------------------------------------- per-build-tree ownership
# The machine-wide token pool below prevents host oversubscription. It does NOT
# make two Ninja invocations safe when they point at this same worktree's build
# directory: they can race while replacing one object and leave the linker a
# truncated file. Hold one kernel lock for the whole configure/build/test
# sequence and fail loudly before CMake starts if another wsl_run owns it.
#
# The lock file itself may persist forever; only the kernel lock matters. An
# interrupted process releases it automatically when this fd closes.
mkdir -p "$root/build"
build_lock_path=$root/build/.wsl_run.lock
exec {build_lock_fd}>"$build_lock_path"
if ! flock -n "$build_lock_fd"; then
    echo "wsl_run: refusing concurrent use of this build tree: $root/build" >&2
    echo "wsl_run: another wsl_run process already owns it; wait for that" \
         "invocation or use a separate worktree/build directory" >&2
    exit 2
fi

# ------------------------------------------------------- machine-wide job gate
# One lock file per build token, in /dev/shm so the pool is per-boot and cannot
# survive as a stale on-disk artifact. flock -n either takes a token now or
# moves on; fds stay open for the life of this process and the kernel releases
# every one of them on exit, so a killed agent cannot leak a token. That is the
# property a counter file or a PID list would not have.
cores=$(nproc)
pool_dir=/dev/shm/sts_build_tokens
held_fds=()
mkdir -p "$pool_dir" 2>/dev/null || pool_dir=

# Sets the global `acquired`. It must NOT be called in a command substitution:
# $(...) runs in a subshell, that subshell exits as soon as the substitution
# completes, and the kernel then drops every lock it held -- so the gate would
# hand out full parallelism to every caller while appearing to work. That is
# not hypothetical: it was the first implementation here, and three concurrent
# invocations each reported -j15 on a 16-core box (45 jobs total) before the
# demo caught it. The locks have to be held by the shell that owns the build.
acquire_tokens() {
    local want=$1 i fd
    acquired=0
    [ -n "$pool_dir" ] || { acquired=$want; return; }
    for ((i = 0; i < want; i++)); do
        exec {fd}>"$pool_dir/$i" 2>/dev/null || break
        if flock -n "$fd"; then
            acquired=$((acquired + 1))
            held_fds+=("$fd")   # kept open for the life of this process
        else
            exec {fd}>&-        # someone else holds this token; stop asking
        fi
    done
}

if [ -n "${STS_JOBS:-}" ]; then
    jobs=$STS_JOBS
    echo "wsl_run: STS_JOBS=$jobs (explicit override; the machine-wide gate is bypassed)"
else
    # Leave one core for the rest of the box; floor of 2 so an invocation that
    # arrives when every token is taken still makes progress rather than
    # deadlocking behind its siblings.
    acquire_tokens "$((cores > 1 ? cores - 1 : 1))"
    jobs=$acquired
    if [ "$jobs" -lt 2 ]; then
        jobs=2
        echo "wsl_run: build -j$jobs (every token held by a concurrent wsl_run;" \
             "running at the floor rather than waiting)"
    else
        echo "wsl_run: build -j$jobs of $cores cores (machine-wide gate;" \
             "concurrent wsl_run invocations share this pool)"
    fi
fi

# Tests are process-launch bound rather than CPU bound -- ~640 short-lived
# binaries -- so they are allowed past the build gate.
test_jobs=${STS_TEST_JOBS:-$((cores > 1 ? cores : 1))}
echo "wsl_run: ctest -j$test_jobs"

# configure -> repair truncated dependency records -> build -> test, for one
# preset. `set -e` is suppressed for a function called from an `if`, so every
# step states its own failure explicitly.
run_preset() {
    local preset=$1 log=$2 deps_rc=0
    cmake --preset "$preset" ${cmake_args[@]+"${cmake_args[@]}"} || return 1

    # An object whose recorded header dependency list is EMPTY is one Ninja can
    # never rebuild -- no header edit will mark it dirty -- so it sits in the
    # archive at whatever struct layout it was compiled against, for the life of
    # the build tree, while `cmake --build` keeps reporting everything up to
    # date. The per-build-tree lock above prevents NEW corruption; it cannot see
    # damage already recorded. check_ninja_deps.sh exits 3 when it found and
    # repaired something, which is a normal outcome here, not a failed run.
    bash "$root/tools/check_ninja_deps.sh" --repair "build/$preset" || deps_rc=$?
    [ "$deps_rc" = 0 ] || [ "$deps_rc" = 3 ] || return 1

    # `cmake --build` before every ctest is mandatory (conventions §6): the
    # build is what stops discovery reporting <name>_NOT_BUILT.
    cmake --build --preset "$preset" -- -j"$jobs" || return 1
    ctest --preset "$preset" --parallel "$test_jobs" 2>&1 | tee "$log"
}

summary=() failed=0
for preset in "${presets[@]}"; do
    echo "=== wsl_run: $preset ==="
    log=$(mktemp)
    if run_preset "$preset" "$log"; then
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
