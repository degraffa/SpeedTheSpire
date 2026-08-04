#!/usr/bin/env python3
"""Synthetic tests for the standing-deviation shape checker (TE.1)."""

from __future__ import annotations

import json
import os
import tempfile
import unittest

import standing_triage


def _write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def _write_run(path, monsters_by_seq):
    records = [{"record_kind": "header", "seed": {"string": "STS00001"}}]
    for seq in sorted(monsters_by_seq):
        records.append({
            "record_kind": "action",
            "seq": seq,
            "action_command": "end",
            "state_json": {
                "game_state": {
                    "combat_state": {
                        "monsters": [
                            {"id": mid} for mid in monsters_by_seq[seq]
                        ],
                    },
                },
            },
        })
    records.append({"record_kind": "terminal", "seq": 99})
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        for record in records:
            fh.write(json.dumps(record) + "\n")


def _campaign(root, campaign_id, seed, diff_text, monsters_by_seq,
              classification="state_divergence"):
    campaign_dir = os.path.join(root, campaign_id)
    os.makedirs(campaign_dir, exist_ok=True)
    _write(os.path.join(campaign_dir, "diffs", f"{seed}.log"), diff_text)
    _write_run(os.path.join(
        campaign_dir, f"run_{seed}_a20_ironclad.jsonl"), monsters_by_seq)
    _write(os.path.join(campaign_dir, "report.json"), json.dumps({
        "campaign_id": campaign_id,
        "runs": [
            {"seed": seed, "classification": classification},
            {"seed": "STS99999", "classification": "clean"},
        ],
    }))
    return campaign_dir


GOLD_DIFF = """\
=== run_STS00001_a20_ironclad.jsonl
DIFF seq=5 floor=5 screen=NONE sim_phase=COMBAT cmd='play 1 0' (1 field)
gold: 119 -> 139

DIFF seq=6 floor=5 screen=NONE sim_phase=COMBAT cmd='end' (1 field)
gold: 119 -> 139

PART  run_STS00001_a20_ironclad.jsonl: 10 records compared; stop: run terminal
      first divergence: seq=5 floor=5 screen=NONE (1 field)
--- 1 file(s), 1 not clean ---
"""

FAIRY_DIFF = """\
=== run_STS00001_a20_ironclad.jsonl
DIFF seq=5 floor=8 screen=NONE sim_phase=COMBAT cmd='play 2' (1 field)
potions[0]: NONE(0) -> FairyPotion(31)

PART  run_STS00001_a20_ironclad.jsonl: 10 records compared; stop: run terminal
      first divergence: seq=5 floor=8 screen=NONE (1 field)
--- 1 file(s), 1 not clean ---
"""

MIXED_BAD_DIFF = """\
=== run_STS00001_a20_ironclad.jsonl
DIFF seq=5 floor=5 screen=NONE sim_phase=COMBAT cmd='end' (2 field)
gold: 119 -> 139
current_hp: 40 -> 44

PART  run_STS00001_a20_ironclad.jsonl: 10 records compared; stop: run terminal
"""


class StandingTriageTest(unittest.TestCase):
    def test_gold_only_looter_combat_is_standing(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = _campaign(
                root, "gold-ok", "STS00001", GOLD_DIFF,
                {5: ["Looter"], 6: ["Looter", "Cultist"]})
            result = standing_triage.triage_campaign(campaign_dir)
            self.assertEqual([], result["unmatched"])
            self.assertEqual(1, len(result["items"]))
            item = result["items"][0]
            self.assertEqual("standing-deviation", item["status"])
            self.assertEqual("state_divergence", item["classification"])
            self.assertEqual("STS00001", item["seed"])
            self.assertIn("stolen-gold ordering", item["note"])

    def test_gold_outside_looter_combat_is_unmatched(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = _campaign(
                root, "gold-bad", "STS00001", GOLD_DIFF,
                {5: ["Looter"], 6: ["Cultist"]})
            result = standing_triage.triage_campaign(campaign_dir)
            self.assertEqual([], result["items"])
            self.assertEqual(1, len(result["unmatched"]))
            self.assertIn("not during a Looter combat",
                          result["unmatched"][0]["reason"])

    def test_fairy_belt_slot_timing_is_standing(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = _campaign(
                root, "fairy-ok", "STS00001", FAIRY_DIFF, {5: ["Cultist"]})
            result = standing_triage.triage_campaign(campaign_dir)
            self.assertEqual([], result["unmatched"])
            self.assertIn("Fairy-in-a-Bottle",
                          result["items"][0]["note"])

    def test_any_unrecognized_field_is_unmatched(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = _campaign(
                root, "hp-drift", "STS00001", MIXED_BAD_DIFF,
                {5: ["Looter"]})
            result = standing_triage.triage_campaign(campaign_dir)
            self.assertEqual([], result["items"])
            self.assertIn("matches no standing shape",
                          result["unmatched"][0]["reason"])

    def test_non_state_divergence_classifications_stay_manual(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = _campaign(
                root, "harness", "STS00001", GOLD_DIFF, {5: ["Looter"]},
                classification="replay_harness_error")
            result = standing_triage.triage_campaign(campaign_dir)
            self.assertEqual([], result["items"])
            self.assertEqual("replay_harness_error",
                             result["unmatched"][0]["classification"])

    def test_main_writes_dispositions_and_gates_on_unmatched(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = _campaign(
                root, "cli", "STS00001", GOLD_DIFF,
                {5: ["Looter"], 6: ["Looter"]})
            rc = standing_triage.main(
                ["--campaign-dir", campaign_dir, "--require-all-triaged"])
            self.assertEqual(0, rc)
            out = os.path.join(
                campaign_dir, "triage", "standing_dispositions.json")
            with open(out, encoding="utf-8") as fh:
                value = json.load(fh)
            self.assertEqual(standing_triage.DISPOSITIONS_FORMAT,
                             value["format"])
            self.assertEqual(1, len(value["items"]))

            bad_dir = _campaign(
                root, "cli-bad", "STS00001", MIXED_BAD_DIFF,
                {5: ["Looter"]})
            self.assertEqual(1, standing_triage.main(
                ["--campaign-dir", bad_dir, "--require-all-triaged"]))


if __name__ == "__main__":
    unittest.main()
