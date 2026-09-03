#!/usr/bin/env bash
# Replay ALL THREE committed oracle CI corpora through the release
# replay_run_diff -- once per comparison mode (`--replay`, `--costs`, `--masks`)
# -- with each mode's own injected-divergence negative control beside it.
#
# This is the standing regression surface under the 2026-09-03 evidence rule
# (conventions.md 1): the committed captures are real runs, and "still
# zero-diff" is what an engine change has to stay true. It is the same walk
# `OracleCorpusReplay.*` performs, run directly so it can be quoted in an
# Acceptance block without going through ctest.
#
#   tools/wsl_run.sh --script tools/build_presets.sh release
#   tools/wsl_run.sh --script tools/corpus_replay.sh
#
# A RED here is a finding: fix the engine, never the corpus.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=build/release/tools/oracle_bridge/replay/replay_run_diff
test -x "$BIN" || { echo "missing $BIN -- build the release preset first"; exit 2; }
SMOKE=tools/verify_report/ci_corpus_smoke.py
S=build/corpus_replay_scratch
rc=0

# <label> <archive> <manifest> <entries> <scratch> [extra replay flags...]
clean() {
    label=$1; archive=$2; manifest=$3; entries=$4; scratch=$5; shift 5
    extra=()
    # `--extra-flag=--costs`, not `--extra-flag --costs`: argparse reads a
    # separate argument beginning with `--` as the next OPTION, and the whole
    # invocation then dies with a usage error the caller would otherwise have
    # to read as a divergence.
    for flag in "$@"; do extra+=("--extra-flag=$flag"); done
    echo "=== $label ==="
    if python3 "$SMOKE" --archive "$archive" --manifest "$manifest" \
           --replay-bin "$BIN" --expect-entries "$entries" \
           --scratch "$scratch" "${extra[@]}"; then
        echo "$label: ZERO-DIFF (exit 0)"
    else
        echo "$label: DIVERGED (exit $?)"
        return 1
    fi
}

# <label> <archive> <manifest> <entries> <scratch> <inject-kind> [extra flags...]
#
# The kind is passed explicitly because each comparison needs a control that
# breaks ITS OWN claim: `state` moves a run-level field (the `--replay` walk),
# `cost` raises one in-HAND card's cost (the `--costs` walk -- the hand
# deliberately, since it is the one pile that tolerates nothing), and `mask`
# upgrades one grid row (the `--masks` walk). The smoke script walks members
# until it finds a site for the kind and FAILS rather than silently injecting
# nothing, so a green control here really is a caught divergence.
control() {
    label=$1; archive=$2; manifest=$3; entries=$4; scratch=$5; kind=$6; shift 6
    extra=()
    for flag in "$@"; do extra+=("--extra-flag=$flag"); done
    set +e
    python3 "$SMOKE" --archive "$archive" --manifest "$manifest" \
           --replay-bin "$BIN" --expect-entries "$entries" \
           --scratch "$scratch" --inject-divergence --inject-kind "$kind" \
           "${extra[@]}" >/dev/null 2>&1
    code=$?
    set -e
    # EXIT 1 EXACTLY, not "non-zero". The smoke script exits 1 when the replay
    # caught a divergence and 2 when the run never happened -- a corpus error,
    # a missing injection site, a mistyped flag. Accepting any non-zero code
    # once let a whole batch of controls report "fails loud, as required" while
    # every one of them had actually died in argparse, which is the exact
    # failure mode a negative control exists to rule out.
    if [ "$code" -eq 1 ]; then
        echo "$label: control fails loud, as required"
        return 0
    fi
    if [ "$code" -eq 0 ]; then
        echo "$label: CONTROL FAILED -- an injected divergence replayed clean"
    else
        echo "$label: CONTROL FAILED -- the injected run never ran (exit $code)"
    fi
    return 1
}

A1=(tests/golden/oracle_corpus/act1_a20_50.tar.gz
    tests/golden/oracle_corpus/act1_a20_50.manifest.json 50)
A3=(tests/golden/oracle_corpus/three_act_a20_5.tar.gz
    tests/golden/oracle_corpus/three_act_a20_5.manifest.json 5)
# S3.23: the key corpus (v3). Its members deliberately DO carry key-race
# records -- an ObtainKeyEffect is the unavoidable shadow of the very claim the
# corpus freezes -- so `CLEAN` here means zero-diff with that one narrow family
# recognised, and nothing else.
KEYS=(tests/golden/oracle_corpus/keys_a20_4.tar.gz
    tests/golden/oracle_corpus/keys_a20_4.manifest.json 4)

clean   "act1_a20_50 --replay"      "${A1[@]}"   "$S/act1"      || rc=1
clean   "three_act_a20_5 --replay"  "${A3[@]}"   "$S/three"     || rc=1
clean   "keys_a20_4 --replay"       "${KEYS[@]}" "$S/keys"      || rc=1
control "act1_a20_50 injected"      "${A1[@]}"   "$S/act1_inj"  state || rc=1
control "three_act_a20_5 injected"  "${A3[@]}"   "$S/three_inj" state || rc=1
control "keys_a20_4 injected"       "${KEYS[@]}" "$S/keys_inj"  state || rc=1

# S3.53: the two comparisons that closed the replay differ's in-combat-cost and
# grid-mask blind spots. Both reach replay_run_diff's exit code, which is what
# lets them stand here beside `--replay` as acceptance rather than inspection.
clean   "act1_a20_50 --costs"       "${A1[@]}"   "$S/act1_c"    --costs || rc=1
clean   "three_act_a20_5 --costs"   "${A3[@]}"   "$S/three_c"   --costs || rc=1
clean   "keys_a20_4 --costs"        "${KEYS[@]}" "$S/keys_c"    --costs || rc=1
clean   "act1_a20_50 --masks"       "${A1[@]}"   "$S/act1_m"    --masks || rc=1
clean   "three_act_a20_5 --masks"   "${A3[@]}"   "$S/three_m"   --masks || rc=1
clean   "keys_a20_4 --masks"        "${KEYS[@]}" "$S/keys_m"    --masks || rc=1
control "act1_a20_50 cost-injected"     "${A1[@]}"   "$S/act1_ci"  cost --costs || rc=1
control "three_act_a20_5 cost-injected" "${A3[@]}"   "$S/three_ci" cost --costs || rc=1
control "keys_a20_4 cost-injected"      "${KEYS[@]}" "$S/keys_ci"  cost --costs || rc=1
control "act1_a20_50 mask-injected"     "${A1[@]}"   "$S/act1_mi"  mask --masks || rc=1
control "three_act_a20_5 mask-injected" "${A3[@]}"   "$S/three_mi" mask --masks || rc=1
control "keys_a20_4 mask-injected"      "${KEYS[@]}" "$S/keys_mi"  mask --masks || rc=1
exit $rc
