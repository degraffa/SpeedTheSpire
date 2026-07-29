#!/usr/bin/env bash
# B5.2 campaign post-processing. Called from the Windows pipeline only through
# tools/wsl_run.cmd --script, so the Windows/WSL argv boundary stays on the
# sanctioned path (conventions §6).

set -uo pipefail

if [[ $# -ne 1 || ! "$1" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ || "$1" == "." || "$1" == ".." ]]; then
    echo "usage: postprocess_campaign.sh <safe-campaign-id>" >&2
    exit 2
fi

campaign_id="$1"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
campaign_root="/mnt/d/STS_BG_Mod/_oracle_data/campaigns"
campaign_dir="$campaign_root/$campaign_id"

if [[ ! -d "$campaign_dir" ]]; then
    echo "campaign directory does not exist: $campaign_dir" >&2
    exit 2
fi

python3 "$script_dir/validate_artifacts.py" \
    --require-oracle --require-encounter-lists --campaign "$campaign_dir" \
    >"$campaign_dir/validation.log" 2>&1
validation_rc=$?
if [[ $validation_rc -ne 0 ]]; then
    echo "strict campaign validation failed; see $campaign_dir/validation.log" >&2
    exit "$validation_rc"
fi

cd "$repo_root" || exit $?
cmake --preset release || exit $?
cmake --build --preset release --target \
    translate_cli replay_run_diff encounter_list_oracle || exit $?

translate="$repo_root/build/release/tools/oracle_bridge/translator/translate_cli"
replay="$repo_root/build/release/tools/oracle_bridge/replay/replay_run_diff"
list_oracle="$repo_root/build/release/tools/oracle_bridge/replay/encounter_list_oracle"
for tool in "$translate" "$replay" "$list_oracle"; do
    if [[ ! -f "$tool" ]]; then
        echo "post-process binary missing after build: $tool" >&2
        exit 2
    fi
done

mkdir -p "$campaign_dir/traces" "$campaign_dir/translation" \
    "$campaign_dir/diffs" "$campaign_dir/encounter_lists"

shopt -s nullglob
runs=("$campaign_dir"/run_*_a20_ironclad.jsonl)
if [[ ${#runs[@]} -eq 0 ]]; then
    echo "no run artifacts under $campaign_dir" >&2
    exit 2
fi

for run in "${runs[@]}"; do
    name="$(basename "$run")"
    seed="${name#run_}"
    seed="${seed%_a20_ironclad.jsonl}"

    "$translate" "$run" --trace-out "$campaign_dir/traces/$seed.trace" \
        >"$campaign_dir/translation/$seed.log" 2>&1
    printf '%s\n' "$?" >"$campaign_dir/translation/$seed.status"

    "$replay" "$run" --replay \
        >"$campaign_dir/diffs/$seed.log" 2>&1
    printf '%s\n' "$?" >"$campaign_dir/diffs/$seed.status"

    "$list_oracle" "$run" \
        >"$campaign_dir/encounter_lists/$seed.log" 2>&1
    printf '%s\n' "$?" >"$campaign_dir/encounter_lists/$seed.status"
done

echo "post-processed ${#runs[@]} run(s) under $campaign_dir"
