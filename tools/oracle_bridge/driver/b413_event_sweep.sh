#!/usr/bin/env bash
# The `--event` regression corpus, named ONCE so a re-run is byte-comparable.
#
# WHY A SCRIPT AND NOT A COMMAND LINE IN THE RUNBOOK. The 88-sighting sweep in
# b47_treasure_spotdiff.md §8b is the regression baseline for every later change
# to the `--event` mode, and a baseline is only a baseline if the NEXT person
# selects exactly the same files. Typed by hand the campaign list drifts (one
# `b45_rewards_oracle2_*` forgotten is three sightings gone and a "strictly
# improved" verdict that means nothing), and the glob order that decides the row
# order of the per-sighting table is the shell's, not the runbook's.
#
# `b45_rewards` is deliberately absent: it predates the oracle gate and carries
# no oracle block, so the translator cannot seed from it (runbook §8b, trap 6).
#
#   tools/wsl_run.sh --script tools/oracle_bridge/driver/b413_event_sweep.sh
#   tools/wsl_run.sh --script tools/oracle_bridge/driver/b413_event_sweep.sh --deal
#
# `--deal` restricts the sweep to the campaign holding the Match and Keep
# sightings (§8c); with no argument it runs the full corpus.
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
bin="${root}/build/debug/tools/oracle_bridge/replay/replay_run_diff"
data="${STS_ORACLE_DATA:-/mnt/d/STS_BG_Mod/_oracle_data}/campaigns"

# `-f`, NOT `-x`: the tree is on `D:`, which is DrvFs and reports mode 777 for
# every file, so an executable-bit test here would always pass and this guard
# would silently never fire (conventions §6).
if [[ ! -f "${bin}" ]]; then
    echo "no replay_run_diff at ${bin} -- run tools/wsl_run.sh debug first" >&2
    exit 2
fi

if [[ "${1:-}" == "--deal" ]]; then
    campaigns=(b4x_greedy_pilot_20260728T041406Z_claude01)
else
    campaigns=(
        b13_on20 b13_off20 b13_on20b b13_offscript b13_offscript2
        b14_accept b14_accept2
        b45_rewards_oracle_20260727T204809Z_claude01
        b45_rewards_oracle2_20260727T204809Z_claude01
        b47_treasure_oracle_20260727T204809Z_claude01
    )
fi

files=()
for c in "${campaigns[@]}"; do
    for f in "${data}/${c}"/run_*_a20_ironclad.jsonl; do
        [[ -e "${f}" ]] || continue
        files+=("${f}")
    done
done

if [[ ${#files[@]} -eq 0 ]]; then
    echo "no run artifacts found under ${data}" >&2
    exit 2
fi

echo "# ${#files[@]} artifact(s) across ${#campaigns[@]} campaign(s)"
"${bin}" --event "${files[@]}"
