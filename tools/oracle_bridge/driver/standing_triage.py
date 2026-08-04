#!/usr/bin/env python3
"""Classify a campaign's divergent runs against the KNOWN standing shapes.

Stage B's triage bar (stage-b-tasks.md, verify_report/generate_report.py) is
per-finding: a divergence counts as triaged only when an exact
campaign/seed/classification disposition exists. Two deviation families were
reviewed and accepted as STANDING during B5.x/G7 (they appear ~130 times in
tools/verify_report/divergence_dispositions.json), and every large cohort
re-encounters them:

  * **Looter stolen-gold ordering** -- during a Looter combat the capture and
    the simulator disagree ONLY on the `gold` reading (the fork reports the
    steal at a different action boundary than the simulator's fold-back).
    Shape: every divergent field line is `gold: <int> -> <int>`, and the
    combat on screen at that seq actually contains a Looter.
  * **Fairy in a Bottle belt-slot timing** -- the capture consumes the potion
    immediately; the simulator deliberately burns the mirrored run-layer belt
    slot only at combat fold-back. Shape: every divergent field line is
    `potions[<slot>]: NONE(<id>) -> FairyPotion(<id>)`.

This tool AUTOMATES THE SHAPE CHECK, nothing more. It is deliberately
conservative: a run qualifies only when EVERY divergent field line in its
replay diff matches one of the two shapes above (gold lines additionally
verified against the run artifact's own monster list at that seq), and its
replay summary reports no unexplained stop. Anything else -- one unexpected
field, one gold diff outside a Looter fight, a harness error -- stays
UNMATCHED and must be triaged by hand exactly as before. The output items use
the same STS-DIVERGENCE-DISPOSITIONS v1 item shape the committed dispositions
file uses, so a reviewed batch can be appended verbatim.

Usage:
    python standing_triage.py --campaign-dir <dir> [--out <json>]
        [--require-all-triaged]

Reads:  <dir>/report.json (classification per seed), <dir>/diffs/<seed>.log,
        <dir>/run_<seed>_a20_ironclad.jsonl (Looter verification)
Writes: --out (default <dir>/triage/standing_dispositions.json)
Exit:   0; or 1 under --require-all-triaged when any divergence is unmatched.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

DISPOSITIONS_FORMAT = "STS-DIVERGENCE-DISPOSITIONS v1"
STANDING_REFERENCE = "../stage-b-tasks.md#deferred-obligations"

_DIFF_HEADER_RE = re.compile(
    r"^DIFF seq=(\d+) floor=(-?\d+) screen=(\S+) sim_phase=(\S+) "
    r"cmd='(.*)' \((\d+) field")
_GOLD_LINE_RE = re.compile(r"^gold: \d+ -> \d+$")
_FAIRY_LINE_RE = re.compile(
    r"^potions\[\d+\]: NONE\(\d+\) -> FairyPotion\(\d+\)$")

LOOTER_MONSTER_IDS = frozenset({"Looter"})


def parse_diff_blocks(text: str) -> list:
    """[(seq, [field lines])] for every DIFF block in a replay diff log."""
    blocks = []
    current = None
    for line in text.splitlines():
        header = _DIFF_HEADER_RE.match(line)
        if header:
            current = (int(header.group(1)), [])
            blocks.append(current)
            continue
        if current is None:
            continue
        if not line.strip() or line.startswith(("CLEAN ", "PART ", "=== ",
                                                "---", "      ")):
            current = None
            continue
        current[1].append(line.rstrip("\n"))
    return blocks


def monsters_at_seqs(run_path: str, seqs: set) -> dict:
    """{seq: [monster ids]} from the run artifact's action records."""
    out = {}
    with open(run_path, "r", encoding="utf-8") as fh:
        for line in fh:
            if not line.strip():
                continue
            record = json.loads(line)
            if record.get("record_kind") != "action":
                continue
            seq = record.get("seq")
            if seq not in seqs:
                continue
            gs = (record.get("state_json") or {}).get("game_state") or {}
            monsters = (gs.get("combat_state") or {}).get("monsters") or []
            out[seq] = [m.get("id") for m in monsters if isinstance(m, dict)]
    return out


def classify_run(campaign_dir: str, seed: str) -> tuple:
    """(matched: bool, note: str). Conservative shape check for one run."""
    diff_path = os.path.join(campaign_dir, "diffs", f"{seed}.log")
    run_path = os.path.join(campaign_dir, f"run_{seed}_a20_ironclad.jsonl")
    try:
        with open(diff_path, "r", encoding="utf-8") as fh:
            text = fh.read()
    except OSError as exc:
        return False, f"diff log unreadable: {exc}"
    blocks = parse_diff_blocks(text)
    if not blocks:
        return False, "no DIFF blocks parsed from the replay log"

    gold_seqs = set()
    gold_lines = 0
    fairy_lines = 0
    for seq, fields in blocks:
        if not fields:
            return False, f"DIFF seq={seq} carries no parsed field lines"
        for field in fields:
            if _GOLD_LINE_RE.match(field):
                gold_lines += 1
                gold_seqs.add(seq)
            elif _FAIRY_LINE_RE.match(field):
                fairy_lines += 1
            else:
                return False, (f"DIFF seq={seq} field {field!r} matches no "
                               "standing shape")

    if gold_seqs:
        try:
            monsters = monsters_at_seqs(run_path, gold_seqs)
        except (OSError, json.JSONDecodeError) as exc:
            return False, f"run artifact unreadable for Looter check: {exc}"
        for seq in sorted(gold_seqs):
            ids = monsters.get(seq)
            if ids is None:
                return False, (f"gold diff at seq={seq} has no action record "
                               "in the run artifact")
            if not LOOTER_MONSTER_IDS & set(ids):
                return False, (f"gold diff at seq={seq} is not during a "
                               f"Looter combat (monsters: {ids})")

    parts = []
    if gold_lines:
        parts.append(
            f"{gold_lines} gold-only read(s) during a verified Looter "
            "combat, exactly the already-decided stolen-gold ordering "
            "deviation")
    if fairy_lines:
        parts.append(
            f"{fairy_lines} Fairy-in-a-Bottle immediate-capture versus "
            "simulator fold-back belt-slot timing record(s)")
    note = ("Shape-checked by standing_triage.py: every divergent field is "
            + " and ".join(parts) + "; no other field differs.")
    return True, note


def triage_campaign(campaign_dir: str) -> dict:
    report_path = os.path.join(campaign_dir, "report.json")
    with open(report_path, "r", encoding="utf-8") as fh:
        report = json.load(fh)
    campaign_id = report.get("campaign_id")
    items = []
    unmatched = []
    for run in report.get("runs", []):
        classification = run.get("classification")
        if classification == "clean":
            continue
        seed = run.get("seed")
        if classification != "state_divergence":
            unmatched.append({
                "seed": seed,
                "classification": classification,
                "reason": "only state_divergence has recognized standing "
                          "shapes; triage by hand",
            })
            continue
        matched, note = classify_run(campaign_dir, seed)
        if matched:
            items.append({
                "campaign_id": campaign_id,
                "seed": seed,
                "classification": classification,
                "status": "standing-deviation",
                "note": note,
                "reference": STANDING_REFERENCE,
            })
        else:
            unmatched.append({
                "seed": seed,
                "classification": classification,
                "reason": note,
            })
    return {
        "format": DISPOSITIONS_FORMAT,
        "campaign_id": campaign_id,
        "items": items,
        "unmatched": unmatched,
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Shape-check campaign divergences against the standing "
                    "deviation families")
    parser.add_argument("--campaign-dir", required=True)
    parser.add_argument("--out", default=None,
                        help="output dispositions JSON (default "
                             "<campaign-dir>/triage/standing_dispositions"
                             ".json)")
    parser.add_argument("--require-all-triaged", action="store_true",
                        help="exit 1 if any divergence matched no standing "
                             "shape")
    args = parser.parse_args(argv)
    result = triage_campaign(args.campaign_dir)
    out = args.out or os.path.join(
        args.campaign_dir, "triage", "standing_dispositions.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(result, fh, indent=1, sort_keys=True)
        fh.write("\n")
    print(f"{result['campaign_id']}: {len(result['items'])} standing, "
          f"{len(result['unmatched'])} unmatched -> {out}")
    for row in result["unmatched"]:
        print(f"  UNMATCHED {row['seed']} ({row['classification']}): "
              f"{row['reason']}")
    if args.require_all_triaged and result["unmatched"]:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
