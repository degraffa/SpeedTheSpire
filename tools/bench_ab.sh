#!/usr/bin/env bash
# Interleaved A/B benchmark comparison.
#
# WHY THIS EXISTS (conventions.md §7, §8): this box drifts by more than the
# effects being measured, so "run A, then run B" is not a comparison. Two
# measurements on this project were wrong that way — a spurious -1.4% and a
# spurious 5.1s saving that a direct measurement showed to be noise. This script
# can only measure interleaved (A B A B ...), and refuses to print a headline
# number when the spread swamps the difference.
#
# Usage:
#   tools/bench_ab.sh [-n PAIRS] [-w WARMUP] [-f FILTER] <binary-A> <binary-B>
#
#   -n  measured A/B pairs        (default 5, minimum 2)
#   -w  discarded warm-up pairs   (default 1)
#   -f  --benchmark_filter regex; required if the binary registers more than
#       one benchmark, since exactly one items_per_second reading must match
#
# Both binaries must be Google Benchmark executables that report
# `items_per_second=<N>M/s` (that is the field parsed). To compare two commits,
# build each into its own tree and pass both paths:
#   cmake --preset release -B build/rel-A ... ; cp build/rel-A/... /tmp/A
# From the Windows host, go through the WSL helper:
#   tools/wsl_run.sh --script tools/bench_ab.sh /tmp/A /tmp/B
#
# Exit: 0 a real difference was measured, 3 UNMEASURED (noise >= effect),
# 1/2 error. A 3 is a valid answer, not a failure -- report "no measurable
# difference" and do not quote the mean.
set -euo pipefail

pairs=5 warmup=1 filter=.
while getopts :n:w:f:h opt; do
    case $opt in
        n) pairs=$OPTARG ;;
        w) warmup=$OPTARG ;;
        f) filter=$OPTARG ;;
        *) awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "$0"
           exit 2 ;;
    esac
done
shift $((OPTIND - 1))

[ $# -eq 2 ] || { echo "bench_ab: need exactly two binaries (got $#)" >&2; exit 2; }
[ "$pairs" -ge 2 ] || { echo "bench_ab: -n must be >= 2; one pair has no spread" >&2; exit 2; }
A=$1 B=$2
for bin in "$A" "$B"; do
    [ -x "$bin" ] || { echo "bench_ab: not an executable: $bin" >&2; exit 2; }
done

# One run -> one items/second figure, unit-normalised. More than one match means
# the filter is ambiguous: stop rather than average unrelated benchmarks.
run_one() {
    "$1" --benchmark_filter="$filter" 2>/dev/null \
    | grep -oE 'items_per_second=[0-9.]+[kMGT]?/s' \
    | awk -F'[=/]' '
        { v = $2 + 0
          u = $2; sub(/^[0-9.]+/, "", u)
          if (u == "k") v *= 1e3; else if (u == "M") v *= 1e6
          else if (u == "G") v *= 1e9; else if (u == "T") v *= 1e12
          n++; last = v }
        END { if (n != 1) { printf "bench_ab: %d items_per_second readings, need exactly 1\n", n > "/dev/stderr"; exit 1 }
              printf "%.6f\n", last }'
}
# 0 readings means the run produced no result at all (a crash, an assert, a
# filter that matched nothing); >1 means the filter is ambiguous. Either way,
# stop: a partial run must never become half of a comparison.
die_run() { echo "bench_ab: $1 did not yield one benchmark result -- run it by hand, or pass -f" >&2; exit 1; }

data=$(mktemp)
trap 'rm -f "$data"' EXIT

echo "bench_ab: A=$A"
echo "bench_ab: B=$B"
echo "bench_ab: $warmup warm-up + $pairs measured pairs, interleaved A/B, filter=/$filter/"
echo
printf '%-6s %12s %12s %9s\n' pair 'A (M/s)' 'B (M/s)' 'delta %'

for i in $(seq 1 $((warmup + pairs))); do
    a=$(run_one "$A") || die_run "$A"
    b=$(run_one "$B") || die_run "$B"
    if [ "$i" -le "$warmup" ]; then
        printf '%-6s %12.3f %12.3f %9s\n' "warm" \
            "$(awk -v v="$a" 'BEGIN{print v/1e6}')" \
            "$(awk -v v="$b" 'BEGIN{print v/1e6}')" "-"
        continue
    fi
    echo "$a $b" >> "$data"
    printf '%-6s %12.3f %12.3f %+9.2f\n' "$((i - warmup))" \
        "$(awk -v v="$a" 'BEGIN{print v/1e6}')" \
        "$(awk -v v="$b" 'BEGIN{print v/1e6}')" \
        "$(awk -v a="$a" -v b="$b" 'BEGIN{print (b-a)/a*100}')"
done

echo
awk '
{ a[NR] = $1; b[NR] = $2; d[NR] = ($2 - $1) / $1 * 100
  sa += $1; sb += $2; sd += d[NR]
  if (NR == 1 || d[NR] < lo) lo = d[NR]
  if (NR == 1 || d[NR] > hi) hi = d[NR] }
END {
  n = NR; mean = sd / n
  for (i = 1; i <= n; i++) var += (d[i] - mean) ^ 2
  s = sqrt(var / (n - 1)); sem = s / sqrt(n)
  printf "A mean %.3f M/s   B mean %.3f M/s   (n=%d pairs)\n", sa/n/1e6, sb/n/1e6, n
  printf "delta  mean %+.2f%%   sd %.2f%%   sem %.2f%%   spread [%+.2f%%, %+.2f%%]\n",
         mean, s, sem, lo, hi
  # 2 x standard error of the mean ~ the 95%% band. Inside it, the machine is
  # louder than the effect and any headline number would be fiction.
  if ((mean < 0 ? -mean : mean) <= 2 * sem) {
    printf "RESULT: UNMEASURED -- |mean| %.2f%% <= 2*sem %.2f%%; noise swamps the delta.\n",
           (mean < 0 ? -mean : mean), 2 * sem
    printf "        Report this as no measurable difference, or re-run with a larger -n.\n"
    exit 3
  }
  printf "RESULT: B is %.2f%% %s than A (95%% band %+.2f%% .. %+.2f%%).\n",
         (mean < 0 ? -mean : mean), (mean < 0 ? "SLOWER" : "FASTER"),
         mean - 2 * sem, mean + 2 * sem
}' "$data"
