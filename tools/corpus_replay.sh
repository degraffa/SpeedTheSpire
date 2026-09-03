#!/usr/bin/env bash
# Replay BOTH committed oracle CI corpora through the release replay_run_diff
# --replay, with each corpus's injected-divergence negative control beside it.
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

clean() {  # <label> <archive> <manifest> <entries> <scratch>
    echo "=== $1 ==="
    if python3 "$SMOKE" --archive "$2" --manifest "$3" --replay-bin "$BIN" \
           --expect-entries "$4" --scratch "$5"; then
        echo "$1: ZERO-DIFF (exit 0)"
    else
        echo "$1: DIVERGED (exit $?)"
        return 1
    fi
}

control() {  # <label> <archive> <manifest> <entries> <scratch>
    if python3 "$SMOKE" --archive "$2" --manifest "$3" --replay-bin "$BIN" \
           --expect-entries "$4" --scratch "$5" --inject-divergence >/dev/null 2>&1; then
        echo "$1: CONTROL FAILED -- an injected divergence replayed clean"
        return 1
    fi
    echo "$1: control fails loud, as required"
}

A1=(tests/golden/oracle_corpus/act1_a20_50.tar.gz
    tests/golden/oracle_corpus/act1_a20_50.manifest.json 50)
A3=(tests/golden/oracle_corpus/three_act_a20_5.tar.gz
    tests/golden/oracle_corpus/three_act_a20_5.manifest.json 5)

clean   "act1_a20_50 --replay"      "${A1[@]}" "$S/act1"      || rc=1
clean   "three_act_a20_5 --replay"  "${A3[@]}" "$S/three"     || rc=1
control "act1_a20_50 injected"      "${A1[@]}" "$S/act1_inj"  || rc=1
control "three_act_a20_5 injected"  "${A3[@]}" "$S/three_inj" || rc=1
exit $rc
