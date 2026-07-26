#!/usr/bin/env bash
# Overnight sim-side fuzz soak (ledger B5.1; design §7.1(2) "the fuzz driver
# runs when the machine would otherwise idle").
#
# Runs the main sweep with an optimised binary and an ASAN SAMPLE with the
# sanitizer binary (acceptance: >= 1 % of runs under asan), then merges every
# shard's kv summary into one report.
#
# THIS SCRIPT NEVER BUILDS. tools/bench_ab.sh learned that the hard way: a
# script that builds can measure the wrong tree. Point it at binaries that
# already exist and it can only report what those binaries did.
#
# ARTIFACTS ARE NEVER COMMITTED (conventions §5). Everything lands under
# --out, which defaults to the non-repo campaign data root of design §7.3.
#
# Usage:
#   tools/fuzz/soak.sh [options]
#     --main-bin PATH     fuzz_soak built release/debug   (required)
#     --asan-bin PATH     fuzz_soak built with the asan preset (optional)
#     --out DIR           artifact root (default $STS_CAMPAIGN_ROOT/fuzz or
#                         /mnt/d/STS_BG_Mod/SpeedTheSpire-campaigns/fuzz)
#     --seeds N           run seeds in the main sweep (default 10000)
#     --seed-start N      first seed (default 1)
#     --reps N            policy seeds per (seed, policy) (default 1)
#     --asan-percent P    disjoint asan seeds as a percent of main (default 1)
#     --jobs N            worker threads for the main sweep (default 4)
#     --asan-jobs N       worker threads for the asan sweep (default 2)
#     --max-actions N     per-run action cap (default 4000)
#     --label NAME        artifact filename prefix (default fuzz)
#     --dry-run           print the commands and exit
#
# Exit: 0 clean, 1 any failure found, 2 usage/setup.
#
# BE A GOOD NEIGHBOUR. The defaults are deliberately modest (4 + 2 threads):
# this box is also the build host and the only machine the oracle game runs on
# (design §7.1). Raise --jobs only when nothing else is running.
set -uo pipefail

main_bin=""
asan_bin=""
out_dir="${STS_CAMPAIGN_ROOT:-/mnt/d/STS_BG_Mod/SpeedTheSpire-campaigns}/fuzz"
seeds=10000
seed_start=1
reps=1
asan_percent=1
jobs=4
asan_jobs=2
max_actions=4000
label="fuzz"
dry_run=0

die() { echo "soak.sh: $*" >&2; exit 2; }
positive_int() { [[ "$1" =~ ^[0-9]+$ ]] && [ "$1" -gt 0 ]; }
signed_int() { [[ "$1" =~ ^-?[0-9]+$ ]]; }

while [ $# -gt 0 ]; do
    case "$1" in
        --main-bin) main_bin="${2:-}"; shift 2 ;;
        --asan-bin) asan_bin="${2:-}"; shift 2 ;;
        --out) out_dir="${2:-}"; shift 2 ;;
        --seeds) seeds="${2:-}"; shift 2 ;;
        --seed-start) seed_start="${2:-}"; shift 2 ;;
        --reps) reps="${2:-}"; shift 2 ;;
        --asan-percent) asan_percent="${2:-}"; shift 2 ;;
        --jobs) jobs="${2:-}"; shift 2 ;;
        --asan-jobs) asan_jobs="${2:-}"; shift 2 ;;
        --max-actions) max_actions="${2:-}"; shift 2 ;;
        --label) label="${2:-}"; shift 2 ;;
        --dry-run) dry_run=1; shift ;;
        -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
        *) die "unknown argument '$1'" ;;
    esac
done

[ -n "$main_bin" ] || die "--main-bin is required (this script never builds)"
[ -f "$main_bin" ] || die "--main-bin '$main_bin' does not exist"
[ -z "$asan_bin" ] || [ -f "$asan_bin" ] || die "--asan-bin '$asan_bin' does not exist"
positive_int "$seeds" || die "--seeds must be a positive integer"
signed_int "$seed_start" || die "--seed-start must be an integer"
positive_int "$reps" || die "--reps must be a positive integer"
positive_int "$jobs" || die "--jobs must be a positive integer"
positive_int "$asan_jobs" || die "--asan-jobs must be a positive integer"
positive_int "$max_actions" || die "--max-actions must be a positive integer"
positive_int "$asan_percent" || die "--asan-percent must be in 1..100"
[ "$asan_percent" -le 100 ] || die "--asan-percent must be in 1..100"

stamp="$(date +%Y%m%d_%H%M%S)"
run_dir="$out_dir/${label}_${stamp}"

# The sanitizer interval begins immediately after the main interval.  Keeping
# the seed ranges disjoint makes the two reports independent evidence and
# prevents accidental double counting even outside this script.
# Split the percentage calculation so neither multiplication nor the +99 can
# overflow Bash's signed arithmetic for an otherwise valid seed count.
asan_seeds=$(( (seeds / 100) * asan_percent +
               ((seeds % 100) * asan_percent + 99) / 100 ))
asan_seed_start=$(( seed_start + seeds ))
main_seed_last=$(( seed_start + seeds - 1 ))
asan_seed_last=$(( asan_seed_start + asan_seeds - 1 ))
[ "$main_seed_last" -ge "$seed_start" ] ||
    die "main seed interval overflows signed 64-bit arithmetic"
[ "$asan_seed_start" -gt "$main_seed_last" ] &&
    [ "$asan_seed_last" -ge "$asan_seed_start" ] ||
    die "sanitizer seed interval overflows or overlaps the main interval"

main_cmd=("$main_bin"
    --seed-start "$seed_start" --seeds "$seeds" --reps "$reps"
    --max-actions "$max_actions" --threads "$jobs"
    --label "${label}_main" --out "$run_dir")
asan_cmd=()
if [ -n "$asan_bin" ]; then
    asan_cmd=("$asan_bin"
        --seed-start "$asan_seed_start" --seeds "$asan_seeds" --reps "$reps"
        --max-actions "$max_actions" --threads "$asan_jobs"
        --label "${label}_asan" --out "$run_dir")
fi

if [ "$dry_run" = "1" ]; then
    echo "run dir: $run_dir"
    printf 'main: '; printf '%q ' "${main_cmd[@]}"; echo
    if [ ${#asan_cmd[@]} -gt 0 ]; then printf 'asan: '; printf '%q ' "${asan_cmd[@]}"; echo; fi
    exit 0
fi

mkdir -p "$run_dir" || die "cannot create $run_dir"
echo "soak.sh: artifacts -> $run_dir"

rc=0
{
    echo "# fuzz soak $stamp"
    echo "# main: ${main_cmd[*]}"
    [ ${#asan_cmd[@]} -gt 0 ] && echo "# asan: ${asan_cmd[*]}"
} > "$run_dir/COMMANDS.txt" || die "cannot write COMMANDS.txt"

echo "soak.sh: main sweep ($seeds seeds, $jobs threads)..."
"${main_cmd[@]}" 2>&1 | tee "$run_dir/${label}_main.log"
main_status=("${PIPESTATUS[@]}")
[ "${main_status[0]}" = "0" ] && [ "${main_status[1]}" = "0" ] || rc=1

if [ ${#asan_cmd[@]} -gt 0 ]; then
    echo "soak.sh: asan sample ($asan_seeds seeds = ${asan_percent}%, $asan_jobs threads)..."
    # Leak detection is on in the project's asan preset; keep it.
    ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=0}" \
        "${asan_cmd[@]}" 2>&1 | tee "$run_dir/${label}_asan.log"
    asan_status=("${PIPESTATUS[@]}")
    [ "${asan_status[0]}" = "0" ] && [ "${asan_status[1]}" = "0" ] || rc=1
fi

# An in-flight journal that survived means a worker died mid-case. That file is
# the reproducer for a crash, so surface it loudly rather than leaving it to be
# noticed.
shopt -s nullglob
inflight=("$run_dir"/*_inflight_*.txt)
if [ ${#inflight[@]} -gt 0 ]; then
    echo "!! soak.sh: a worker died with cases in flight -- these are the reproducers:"
    for f in "${inflight[@]}"; do echo "--- $f"; cat "$f"; done
    rc=1
fi

repros=("$run_dir"/*.repro)
if [ ${#repros[@]} -gt 0 ]; then
    echo "!! soak.sh: ${#repros[@]} reproducer(s) written:"
    for f in "${repros[@]}"; do echo "   $f"; done
fi

main_kvs=("$run_dir"/"${label}_main"_summary_s*.kv)
if [ ${#main_kvs[@]} -gt 0 ]; then
    echo "soak.sh: main report ->" "$run_dir/MAIN_REPORT.txt"
    if ! "$main_bin" --merge "${main_kvs[@]}" |
        tee "$run_dir/MAIN_REPORT.txt"; then
        rc=1
    fi
else
    echo "!! soak.sh: main sweep produced no summary"
    rc=1
fi

asan_kvs=("$run_dir"/"${label}_asan"_summary_s*.kv)
if [ ${#asan_cmd[@]} -gt 0 ]; then
    if [ ${#asan_kvs[@]} -gt 0 ]; then
        echo "soak.sh: sanitizer report ->" "$run_dir/ASAN_REPORT.txt"
        if ! "$asan_bin" --merge "${asan_kvs[@]}" |
            tee "$run_dir/ASAN_REPORT.txt"; then
            rc=1
        fi
    else
        echo "!! soak.sh: sanitizer sweep produced no summary"
        rc=1
    fi
fi

echo "soak.sh: exit $rc"
exit $rc
