#!/usr/bin/env bash
# Release-preset B5.5 acceptance. Run from the Windows host through:
#   tools/wsl_run.sh --script benchmarks/run_throughput.sh
#
# Absolute floor checks are intentionally separate from tools/bench_ab.sh:
# this script measures one release build against frozen thresholds; bench_ab is
# the sanctioned helper when comparing two different builds.
set -euo pipefail

root=$(cd -- "$(dirname -- "$0")/.." && pwd)
binary_dir=${1:-"$root/build/release/benchmarks"}
minimum_time=${2:-2s}

normalize_rate() {
    awk -F'[=/]' '
        {
            value = $2 + 0
            unit = $2
            sub(/^[0-9.]+/, "", unit)
            if (unit == "k") value *= 1e3
            else if (unit == "M") value *= 1e6
            else if (unit == "G") value *= 1e9
            else if (unit == "T") value *= 1e12
            count++
            last = value
        }
        END {
            if (count != 1) {
                printf "run_throughput: expected one items_per_second result, got %d\n",
                       count > "/dev/stderr"
                exit 1
            }
            printf "%.6f\n", last
        }'
}

run_and_rate() {
    local binary=$1 filter=$2 output rate
    [ -f "$binary" ] ||
        { echo "run_throughput: missing benchmark: $binary" >&2; exit 2; }
    output=$("$binary" \
        --benchmark_filter="$filter" \
        --benchmark_min_time="$minimum_time" 2>&1)
    printf '%s\n' "$output" >&2
    rate=$(printf '%s\n' "$output" |
        grep -oE 'items_per_second=[0-9.]+[kMGT]?/s' |
        normalize_rate)
    printf '%s\n' "$rate"
}

require_floor() {
    local name=$1 measured=$2 floor=$3 unit=$4
    if ! awk -v measured="$measured" -v floor="$floor" \
        'BEGIN { exit !(measured >= floor) }'; then
        printf 'FAIL: %s %.3f %s < floor %.3f %s\n' \
            "$name" "$measured" "$unit" "$floor" "$unit" >&2
        return 1
    fi
    printf 'PASS: %s %.3f %s >= floor %.3f %s\n' \
        "$name" "$measured" "$unit" "$floor" "$unit"
}

steps=$(run_and_rate \
    "$binary_dir/bench_advance_mask" \
    '^BM_AdvanceBatch')
combats=$(run_and_rate \
    "$binary_dir/bench_throughput" \
    '^BM_RandomPolicyFullCombatPerCore')
runs=$(run_and_rate \
    "$binary_dir/bench_throughput" \
    '^BM_RandomPolicyFullA20RunWholeMachine')

echo
require_floor combat_steps "$steps" 50000 steps/sec/core
require_floor full_combats "$combats" 300 combats/sec/core
require_floor full_a20_runs "$runs" 0.4 runs/sec/whole-machine
