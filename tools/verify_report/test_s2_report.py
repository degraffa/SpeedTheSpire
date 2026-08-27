#!/usr/bin/env python3
"""Unit tests for the S2.43 dashboard (`generate_s2_report.py`).

Every fixture here is synthetic and built in a temporary directory: the tests
must never depend on the uncommitted oracle data root (stage-B design section
7.3), so they pin *behaviour* -- loud failure, exact disposition naming,
determinism, shortfall rendering -- rather than any day's campaign numbers.
"""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import generate_s2_report as s2
from generate_report import ReportError


def dump(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(value, indent=2, sort_keys=True) + "\n")


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(text)


def sha256(path: Path) -> str:
    return s2.sha256_file(path)


class S2ReportTest(unittest.TestCase):
    """A miniature but structurally faithful S2 evidence tree."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.campaigns = self.root / "campaigns"
        self.registry = self.root / "registry"
        self._write_registry()
        self._write_census()
        self.dispositions = self.root / "dispositions.json"
        self.set_dispositions([], [])
        self.retest = self.root / "retest.log"
        # Every fixture tree below carries bg.worker-001/STS1, so the default
        # sweep is a real, matched verdict rather than an unmatched placeholder
        # (an unmatched one is fatal, which is itself a test below).
        write(self.retest, self.retest_text(
            [("bg.worker-001", "STS1", "CLEAN", "run terminal")]))

    def tearDown(self):
        self.temp.cleanup()

    # -- fixture builders --------------------------------------------------

    def _write_registry(self):
        """A registry the loader accepts, with the rows these bars read."""
        write(self.registry / "cards.yaml", "[]\n")
        write(self.registry / "powers.yaml", "[]\n")
        write(self.registry / "relics.yaml", "[]\n")
        write(self.registry / "potions.yaml", "[]\n")
        write(self.registry / "monsters.yaml", "[]\n")
        dump(self.registry / "a20.yaml", [
            {"id": 1, "level": 20, "provenance": "fixture"},
        ])
        dump(self.registry / "encounters.yaml", [
            {"id": 1, "name": "ACT2_BOSS", "game_id": "Act2 Boss",
             "act": 2, "pool": "BOSS", "provenance": "fixture"},
            {"id": 2, "name": "ACT3_BOSS_A", "game_id": "Act3 Boss A",
             "act": 3, "pool": "BOSS", "provenance": "fixture"},
            {"id": 3, "name": "ACT3_BOSS_B", "game_id": "Act3 Boss B",
             "act": 3, "pool": "BOSS", "provenance": "fixture"},
            {"id": 4, "name": "ACT3_BOSS_C", "game_id": "Act3 Boss C",
             "act": 3, "pool": "BOSS", "provenance": "fixture"},
        ])
        dump(self.registry / "events.yaml", [
            {"id": 1, "name": "ACT1_ONLY", "game_id": "Act1 Only",
             "provenance": "fixture",
             "conditions": {"pool": "EVENT", "acts": [1], "draw": "ALWAYS"}},
            {"id": 2, "name": "DEEP_SEEN", "game_id": "Deep Seen",
             "provenance": "fixture",
             "conditions": {"pool": "EVENT", "acts": [2], "draw": "ALWAYS"}},
            {"id": 3, "name": "DEEP_OWED", "game_id": "Deep Owed",
             "provenance": "fixture",
             "conditions": {"pool": "EVENT", "acts": [3], "draw": "ALWAYS"}},
            {"id": 4, "name": "CROSS_ACT", "game_id": "Cross Act",
             "provenance": "fixture",
             "conditions": {"pool": "SHRINE", "acts": [1, 2, 3],
                            "draw": "ALWAYS"}},
        ])

    def _write_census(self):
        self.census = self.root / "census.txt"
        write(self.census, "\n".join([
            "rows=10 seeds=5 actions=100 max_floor=20 failures=0",
            "depth [all policies] rows=10",
            "  act boss FIGHT: a1=4 (40.00%) a2=0 (0.00%) a3=0 (0.00%)",
            "  act boss KILL:  a1=1 (10.00%) a2=0 (0.00%) a3=0 (0.00%)",
            "depth [random] rows=10",
            "  act boss FIGHT: a1=1 (10.00%) a2=0 (0.00%) a3=0 (0.00%)",
            "events fired (rows in which the event fired at least once):",
            "  Act1 Only: 7 (70.00%)",
            "  Cross Act: 3 (30.00%)",
            "wall_clock=1.00s",
        ]) + "\n")

    def set_dispositions(self, items, event_rows):
        dump(self.dispositions, {
            "format": s2.DISPOSITIONS_FORMAT,
            "items": items,
            "event_rows": event_rows,
        })

    def retest_text(self, entries):
        lines = []
        for campaign_id, seed, result, note in entries:
            path = f"/mnt/d/data/{campaign_id}/run_{seed}_a20_ironclad.jsonl"
            lines.append(f"{result} {path}: {note}")
            lines.append("      first divergence: none")
        return "\n".join(lines) + "\n"

    def group(self, group_id, workers):
        dump(self.campaigns / group_id / "parallel_group.json", {
            "format": "STS-ORACLE-PARALLEL-GROUP v1",
            "group_campaign_id": group_id,
            "workers": [{"campaign_id": worker, "index": index}
                        for index, worker in enumerate(workers)],
        })

    def worker(self, campaign_id, runs, policy="external",
               config="policy_bossrelic_take.json", fork="FORK0",
               corrupt_hash=False):
        """Write one worker report plus every artifact its run rows name."""
        directory = self.campaigns / campaign_id
        rows = []
        for spec in runs:
            seed = spec["seed"]
            artifact = directory / f"run_{seed}_a20_ironclad.jsonl"
            header = {
                "record_kind": "header", "campaign_id": campaign_id,
                "seed": {"string": seed}, "ascension": 20,
                "character": "IRONCLAD", "oracle_block_enabled": True,
                "fork_jar_sha256": fork, "driver_version": "b1.7.0",
                "policy": policy, "policy_seed": 1234,
                "schema_version": 1,
            }
            if config is not None:
                header["external_policy"] = {
                    "cmd_sha256": "CMD0",
                    "config_path": f"D:/tools/{config}",
                    "config_sha256": f"CFG-{config}",
                }
            body = [json.dumps(header)]
            for act, boss, events in spec.get("records", []):
                body.append(json.dumps({
                    "record_kind": "action",
                    "state_json": {
                        "game_state": {
                            "act": act, "act_boss": boss,
                            "oracle": {"act": act},
                            "screen_state": {
                                "event_id": events} if events else {},
                        },
                    },
                }))
            write(artifact, "\n".join(body) + "\n")
            digest = "0" * 64 if corrupt_hash else sha256(artifact)
            rows.append({
                "seed": seed,
                "actions": spec.get("actions", 5),
                "classification": spec.get("classification", "clean"),
                "outcome": spec.get("outcome", "death"),
                "max_act": spec.get("max_act", 1),
                "victory": spec.get("victory", False),
                "boss_fight_acts": spec.get("boss_fight_acts", []),
                "boss_kill_acts": spec.get("boss_kill_acts", []),
                "boss_relic_acts": spec.get("boss_relic_acts", []),
                "known_capture_race_records": spec.get("races", 0),
                "source_artifact": artifact.name,
                "source_artifact_sha256": digest,
            })
        dump(directory / "report.json", {
            "report_format": "STS-ORACLE-CAMPAIGN-REPORT v1",
            "campaign_id": campaign_id,
            "campaign_status": "complete",
            "schema_version": 1, "driver_version": "b1.7.0",
            "pipeline_version": "b5.4.0", "fork_jar_sha256": fork,
            "policy": policy, "policy_seed": 1234,
            "finished_utc": "2026-08-26T00:00:00Z",
            "runs": rows,
        })

    def simple_tree(self):
        """One breadth worker, two clean runs, no findings."""
        self.group("bg", ["bg.worker-001"])
        self.worker("bg.worker-001", [
            {"seed": "STS1", "records": [(1, "Act1 Boss", "Act1 Only")]},
            {"seed": "STS2", "max_act": 2,
             "records": [(1, "Act1 Boss", None), (2, "Act2 Boss", "Deep Seen"),
                         (2, "Act2 Boss", "Cross Act")]},
        ])

    def run_aggregate(self, breadth=("bg",), recapture=()):
        return s2.aggregate(
            self.campaigns, list(breadth), list(recapture), self.retest,
            self.dispositions, self.registry, self.census)

    # -- loud failure ------------------------------------------------------

    def test_malformed_artifact_header_is_fatal(self):
        self.simple_tree()
        artifact = self.campaigns / "bg.worker-001" / "run_STS1_a20_ironclad.jsonl"
        write(artifact, "this is not JSON\n")
        report = self.campaigns / "bg.worker-001" / "report.json"
        value = json.loads(report.read_text(encoding="utf-8"))
        value["runs"][0]["source_artifact_sha256"] = sha256(artifact)
        dump(report, value)
        with self.assertRaises(ReportError) as caught:
            self.run_aggregate()
        self.assertIn("unreadable artifact header", str(caught.exception))

    def test_artifact_hash_drift_is_fatal(self):
        self.group("bg", ["bg.worker-001"])
        self.worker("bg.worker-001",
                    [{"seed": "STS1", "records": [(1, "Act1 Boss", None)]}],
                    corrupt_hash=True)
        with self.assertRaises(ReportError) as caught:
            self.run_aggregate()
        self.assertIn("hash drift", str(caught.exception))

    def test_wrong_ascension_in_header_is_fatal(self):
        self.simple_tree()
        artifact = self.campaigns / "bg.worker-001" / "run_STS1_a20_ironclad.jsonl"
        lines = artifact.read_text(encoding="utf-8").splitlines()
        header = json.loads(lines[0])
        header["ascension"] = 19
        lines[0] = json.dumps(header)
        write(artifact, "\n".join(lines) + "\n")
        report = self.campaigns / "bg.worker-001" / "report.json"
        value = json.loads(report.read_text(encoding="utf-8"))
        value["runs"][0]["source_artifact_sha256"] = sha256(artifact)
        dump(report, value)
        with self.assertRaises(ReportError) as caught:
            self.run_aggregate()
        self.assertIn("A20 Ironclad", str(caught.exception))

    def test_unknown_classification_is_fatal_not_bucketed(self):
        self.simple_tree()
        report = self.campaigns / "bg.worker-001" / "report.json"
        value = json.loads(report.read_text(encoding="utf-8"))
        value["runs"][0]["classification"] = "probably_fine"
        dump(report, value)
        with self.assertRaises(ReportError) as caught:
            self.run_aggregate()
        self.assertIn("unclassifiable run", str(caught.exception))

    def test_unknown_event_id_is_fatal(self):
        self.group("bg", ["bg.worker-001"])
        self.worker("bg.worker-001", [
            {"seed": "STS1", "records": [(1, "Act1 Boss", "Not A Registry Row")]},
        ])
        with self.assertRaises(ReportError) as caught:
            self.run_aggregate()
        self.assertIn("neither an events.yaml row", str(caught.exception))

    def test_allowlisted_non_registry_event_id_is_accepted(self):
        self.group("bg", ["bg.worker-001"])
        self.worker("bg.worker-001", [
            {"seed": "STS1", "records": [(1, "Act1 Boss", "Neow Event")]},
        ])
        report = self.run_aggregate()
        self.assertEqual(report["evidence_totals"]["runs_consumed"], 1)

    def test_ambiguous_act_in_one_record_is_fatal(self):
        self.group("bg", ["bg.worker-001"])
        self.worker("bg.worker-001",
                    [{"seed": "STS1", "records": [(1, "Act1 Boss", None)]}])
        artifact = self.campaigns / "bg.worker-001" / "run_STS1_a20_ironclad.jsonl"
        lines = artifact.read_text(encoding="utf-8").splitlines()
        record = json.loads(lines[1])
        record["state_json"]["game_state"]["oracle"]["act"] = 2
        lines[1] = json.dumps(record)
        write(artifact, "\n".join(lines) + "\n")
        report = self.campaigns / "bg.worker-001" / "report.json"
        value = json.loads(report.read_text(encoding="utf-8"))
        value["runs"][0]["source_artifact_sha256"] = sha256(artifact)
        dump(report, value)
        with self.assertRaises(ReportError) as caught:
            self.run_aggregate()
        self.assertIn("distinct `act` values", str(caught.exception))

    def test_retest_verdict_outside_the_cohorts_is_fatal(self):
        self.simple_tree()
        write(self.retest,
              self.retest_text([("other.worker-001", "STS9", "PART", "x")]))
        with self.assertRaises(ReportError) as caught:
            self.run_aggregate()
        self.assertIn("outside the consumed cohorts", str(caught.exception))

    # -- disposition naming ------------------------------------------------

    def divergent_tree(self):
        """One divergent breadth run plus its clean recapture."""
        self.group("bg", ["bg.worker-001"])
        self.worker("bg.worker-001", [
            {"seed": "STS1", "classification": "state_divergence",
             "records": [(1, "Act1 Boss", None)], "races": 1},
        ])
        self.group("rc", ["rc.worker-001"])
        self.worker("rc.worker-001",
                    [{"seed": "STS1", "records": [(1, "Act1 Boss", None)]}],
                    fork="FORK1")
        write(self.retest,
              self.retest_text([("bg.worker-001", "STS1", "PART", "stop")]))

    def supersession(self, campaign="rc.worker-001", seed="STS1"):
        return [{
            "campaign_id": "bg.worker-001", "seed": "STS1",
            "classification": "part", "status": "superseded-by-recapture",
            "superseded_by": {"campaign_id": campaign, "seed": seed},
            "reference": "fixture", "note": "fixture",
        }]

    def test_untriaged_finding_is_counted_and_named(self):
        self.divergent_tree()
        report = self.run_aggregate(breadth=("bg",), recapture=("rc",))
        item1 = report["item1_breadth"]
        self.assertEqual(item1["untriaged_findings"], 1)
        self.assertFalse(item1["zero_untriaged"])
        self.assertFalse(item1["met"])
        self.assertIn("**UNTRIAGED**", s2.markdown(report))

    def test_supersession_names_the_recapture_exactly(self):
        self.divergent_tree()
        self.set_dispositions(self.supersession(), [])
        report = self.run_aggregate(breadth=("bg",), recapture=("rc",))
        finding = report["divergence_inventory"][0]
        named = finding["superseding_recapture"]
        self.assertEqual(named["campaign_id"], "rc.worker-001")
        self.assertEqual(named["seed"], "STS1")
        self.assertEqual(named["fork_jar_sha256"], "FORK1")
        self.assertEqual(named["capture_race_records"], 0)
        self.assertEqual(report["item1_breadth"]["untriaged_findings"], 0)
        self.assertIn("rc.worker-001 / STS1", s2.markdown(report))

    def test_supersession_of_an_absent_recapture_is_fatal(self):
        self.divergent_tree()
        self.set_dispositions(self.supersession(campaign="ghost.worker"), [])
        with self.assertRaises(ReportError) as caught:
            self.run_aggregate(breadth=("bg",), recapture=("rc",))
        self.assertIn("not in the consumed evidence", str(caught.exception))

    def test_supersession_of_a_different_seed_is_fatal(self):
        self.divergent_tree()
        self.set_dispositions(self.supersession(seed="STS2"), [])
        with self.assertRaises(ReportError) as caught:
            self.run_aggregate(breadth=("bg",), recapture=("rc",))
        self.assertIn("names a different seed", str(caught.exception))

    def test_supersession_by_a_non_clean_recapture_is_fatal(self):
        self.divergent_tree()
        report_path = self.campaigns / "rc.worker-001" / "report.json"
        value = json.loads(report_path.read_text(encoding="utf-8"))
        value["runs"][0]["classification"] = "state_divergence"
        dump(report_path, value)
        self.set_dispositions(self.supersession(), [])
        with self.assertRaises(ReportError) as caught:
            self.run_aggregate(breadth=("bg",), recapture=("rc",))
        self.assertIn("not clean", str(caught.exception))

    def test_open_disposition_is_reviewed_but_never_acceptance(self):
        self.divergent_tree()
        self.set_dispositions([{
            "campaign_id": "bg.worker-001", "seed": "STS1",
            "classification": "part", "status": "open-product-divergence",
            "reference": "fixture", "note": "fixture",
        }], [])
        report = self.run_aggregate(breadth=("bg",), recapture=("rc",))
        self.assertEqual(report["item1_breadth"]["open_findings"], 1)
        self.assertFalse(report["item1_breadth"]["met"])

    def test_a_fixed_finding_leaves_a_stale_disposition_not_a_crash(self):
        """A sibling task makes the run replay clean; the dashboard still runs."""
        self.divergent_tree()
        self.set_dispositions(self.supersession(), [])
        write(self.retest,
              self.retest_text([("bg.worker-001", "STS1", "CLEAN", "terminal")]))
        report = self.run_aggregate(breadth=("bg",), recapture=("rc",))
        self.assertEqual(report["divergence_inventory"], [])
        self.assertEqual(
            report["stale_dispositions"]["runs"],
            [["bg.worker-001", "STS1", "part"]])
        self.assertIn("no longer exercised", s2.markdown(report))

    def test_unsupported_disposition_status_is_fatal(self):
        self.divergent_tree()
        self.set_dispositions([{
            "campaign_id": "bg.worker-001", "seed": "STS1",
            "classification": "part", "status": "probably-fine",
            "reference": "fixture", "note": "fixture",
        }], [])
        with self.assertRaises(ReportError) as caught:
            self.run_aggregate(breadth=("bg",), recapture=("rc",))
        self.assertIn("invalid status", str(caught.exception))

    def test_supersession_without_a_named_target_is_fatal(self):
        self.divergent_tree()
        self.set_dispositions([{
            "campaign_id": "bg.worker-001", "seed": "STS1",
            "classification": "part", "status": "superseded-by-recapture",
            "reference": "fixture", "note": "fixture",
        }], [])
        with self.assertRaises(ReportError) as caught:
            self.run_aggregate(breadth=("bg",), recapture=("rc",))
        self.assertIn("must name the recapture", str(caught.exception))

    # -- classification layering -------------------------------------------

    def test_retest_verdict_overrides_the_capture_classification(self):
        self.divergent_tree()
        write(self.retest,
              self.retest_text([("bg.worker-001", "STS1", "CLEAN", "terminal")]))
        report = self.run_aggregate(breadth=("bg",), recapture=("rc",))
        item1 = report["item1_breadth"]
        self.assertEqual(item1["classification_as_captured"],
                         {"state_divergence": 1})
        self.assertEqual(item1["classification_final"], {"clean": 1})
        self.assertEqual(item1["reclassified_by_retest"], 1)

    def test_non_gameplay_terminal_is_excluded_from_full_run_attempts(self):
        self.group("bg", ["bg.worker-001"])
        self.worker("bg.worker-001", [
            {"seed": "STS1", "records": [(1, "Act1 Boss", None)]},
            {"seed": "STS2", "outcome": "noop_wedge",
             "records": [(1, "Act1 Boss", None)]},
        ])
        report = self.run_aggregate()
        item1 = report["item1_breadth"]
        self.assertEqual(item1["distinct_breadth_seeds"], 2)
        self.assertEqual(item1["full_run_attempts"], 1)
        self.assertEqual(item1["non_gameplay_terminal_runs"], [{
            "campaign_id": "bg.worker-001", "seed": "STS2",
            "outcome": "noop_wedge"}])
        self.assertIn("`noop_wedge`", s2.markdown(report))

    # -- determinism -------------------------------------------------------

    def test_regeneration_is_byte_identical(self):
        self.simple_tree()
        first = self.root / "out-first"
        second = self.root / "out-second"
        for out in (first, second):
            s2.write_report(self.run_aggregate(), out)
        names = ["s243-dashboard.md", "s243-dashboard.json",
                 "s243-event-coverage.csv", "s243-artifact-manifest.csv"]
        for name in names:
            self.assertEqual((first / name).read_bytes(),
                             (second / name).read_bytes(), name)

    def test_every_consumed_artifact_is_hashed_into_the_report(self):
        self.simple_tree()
        report = self.run_aggregate()
        manifest = report["artifact_manifest"]
        self.assertEqual(len(manifest), 2)
        for entry in manifest:
            artifact = (self.campaigns / entry["campaign_id"]
                        / entry["artifact"])
            self.assertEqual(entry["sha256"], sha256(artifact))
        self.assertEqual(report["inputs"]["artifacts_consumed"], 2)
        rendered = s2.artifact_csv(manifest)
        self.assertIn(manifest[0]["sha256"], rendered)

    def test_roll_up_hash_changes_when_an_artifact_changes(self):
        self.simple_tree()
        before = self.run_aggregate()["inputs"]["artifact_roll_up_sha256"]
        artifact = self.campaigns / "bg.worker-001" / "run_STS1_a20_ironclad.jsonl"
        write(artifact, artifact.read_text(encoding="utf-8") + "\n")
        report_path = self.campaigns / "bg.worker-001" / "report.json"
        value = json.loads(report_path.read_text(encoding="utf-8"))
        value["runs"][0]["source_artifact_sha256"] = sha256(artifact)
        dump(report_path, value)
        after = self.run_aggregate()["inputs"]["artifact_roll_up_sha256"]
        self.assertNotEqual(before, after)

    def test_pins_are_surfaced(self):
        self.simple_tree()
        report = self.run_aggregate()
        cohort = report["inputs"]["cohorts"][0]
        self.assertEqual(cohort["provenance"]["fork_jar_sha256"], "FORK0")
        self.assertEqual(cohort["provenance"]["driver_version"], "b1.7.0")
        self.assertEqual(cohort["provenance"]["pipeline_version"], "b5.4.0")
        self.assertEqual(cohort["capture_pins"][0]["policy_cmd_sha256"], "CMD0")
        self.assertEqual(cohort["capture_pins"][0]["policy_config"],
                         "policy_bossrelic_take.json")

    def test_mixed_worker_provenance_inside_one_cohort_is_fatal(self):
        self.group("bg", ["bg.worker-001", "bg.worker-002"])
        self.worker("bg.worker-001",
                    [{"seed": "STS1", "records": [(1, "Act1 Boss", None)]}])
        self.worker("bg.worker-002",
                    [{"seed": "STS2", "records": [(1, "Act1 Boss", None)]}],
                    fork="OTHER")
        with self.assertRaises(ReportError) as caught:
            self.run_aggregate()
        self.assertIn("provenance differs", str(caught.exception))

    # -- shortfall rendering -----------------------------------------------

    def test_empty_depth_cohorts_render_literal_shortfalls(self):
        self.simple_tree()
        report = self.run_aggregate()
        item2 = report["item2_act2_depth"]
        item3 = report["item3_act3_depth"]
        self.assertEqual(item2["registry_boss_rows"], ["Act2 Boss"])
        self.assertEqual(item2["rows_with_zero_diff_boss_reward_claim"], [])
        self.assertFalse(item2["met"])
        self.assertEqual(item3["double_boss_run_count"], 0)
        self.assertEqual(item3["double_boss_shortfall_to_3"], 3)
        self.assertEqual(item3["double_boss_identity_shortfall_to_2"], 2)
        self.assertFalse(item3["detector_exercised_by_live_evidence"])
        text = s2.markdown(report)
        self.assertIn("missing: **Act2 Boss**", text)
        self.assertIn("shortfall to 3: **3**", text)
        self.assertIn("no row", text)

    def test_act2_depth_counts_a_witnessed_row(self):
        self.group("bg", ["bg.worker-001"])
        self.worker("bg.worker-001", [
            {"seed": "STS1", "max_act": 3, "boss_fight_acts": [2],
             "boss_kill_acts": [2], "boss_relic_acts": [2],
             "records": [(2, "Act2 Boss", None), (3, "Act3 Boss A", None)]},
        ], config="policy_bossrelic_take.json")
        self.group("bs", ["bs.worker-001"])
        self.worker("bs.worker-001", [
            {"seed": "STS2", "max_act": 3, "boss_fight_acts": [2],
             "boss_kill_acts": [2], "boss_relic_acts": [2],
             "records": [(2, "Act2 Boss", None), (3, "Act3 Boss A", None)]},
        ], config="policy_bossrelic_skip.json")
        report = self.run_aggregate(breadth=("bg", "bs"))
        item2 = report["item2_act2_depth"]
        cell = item2["per_row"]["Act2 Boss"]
        self.assertEqual(cell["boss_reward_claim_runs"], 2)
        self.assertEqual(cell["take_cohort_runs"], 1)
        self.assertEqual(cell["skip_cohort_runs"], 1)
        self.assertEqual(cell["onward_transition_runs"], 2)
        self.assertEqual(item2["rows_with_take_witness"], ["Act2 Boss"])
        self.assertEqual(item2["rows_with_skip_witness"], ["Act2 Boss"])
        self.assertTrue(item2["met"])

    def test_a_non_clean_run_never_witnesses_depth(self):
        self.group("bg", ["bg.worker-001"])
        self.worker("bg.worker-001", [
            {"seed": "STS1", "max_act": 3, "classification": "state_divergence",
             "boss_fight_acts": [2], "boss_kill_acts": [2],
             "boss_relic_acts": [2],
             "records": [(2, "Act2 Boss", None), (3, "Act3 Boss A", None)]},
        ])
        write(self.retest,
              self.retest_text([("bg.worker-001", "STS1", "PART", "stop")]))
        self.set_dispositions([{
            "campaign_id": "bg.worker-001", "seed": "STS1",
            "classification": "part",
            "status": "open-harness-gap",
            "reference": "fixture", "note": "fixture",
        }], [])
        report = self.run_aggregate()
        cell = report["item2_act2_depth"]["per_row"]["Act2 Boss"]
        self.assertEqual(cell["boss_reward_claim_runs"], 0)
        self.assertEqual(report["item2_act2_depth"]["act2_entering_clean_runs"],
                         0)

    def test_double_boss_detector_reads_two_act3_identities(self):
        self.group("bg", ["bg.worker-001"])
        self.worker("bg.worker-001", [
            {"seed": "STS1", "max_act": 3, "victory": True,
             "boss_fight_acts": [3], "boss_kill_acts": [3],
             "records": [(3, "Act3 Boss A", None), (3, "Act3 Boss B", None)]},
            {"seed": "STS2", "max_act": 3, "victory": True,
             "boss_fight_acts": [3], "boss_kill_acts": [3],
             "records": [(3, "Act3 Boss B", None), (3, "Act3 Boss A", None)]},
            {"seed": "STS3", "max_act": 3, "victory": True,
             "boss_fight_acts": [3], "boss_kill_acts": [3],
             "records": [(3, "Act3 Boss A", None), (3, "Act3 Boss B", None)]},
        ])
        report = self.run_aggregate()
        item3 = report["item3_act3_depth"]
        self.assertEqual(item3["double_boss_run_count"], 3)
        self.assertEqual(item3["double_boss_shortfall_to_3"], 0)
        self.assertEqual(item3["double_boss_first_boss_identities"],
                         ["Act3 Boss A", "Act3 Boss B"])
        self.assertTrue(item3["detector_exercised_by_live_evidence"])
        self.assertEqual(item3["rows_witnessed_killed"],
                         ["Act3 Boss A", "Act3 Boss B"])
        # The third registry row is still missing, so the bar is still UNMET.
        self.assertFalse(item3["met"])
        self.assertIn("missing: **Act3 Boss", s2.markdown(report))

    # -- event coverage join -----------------------------------------------

    def test_event_join_separates_act1_sightings_from_deep_ones(self):
        self.simple_tree()
        report = self.run_aggregate()
        rows = {row["game_id"]: row
                for row in report["item4_event_depth"]["rows"]}
        self.assertNotIn("Act1 Only", rows)  # not an Act-2/3 row at all
        self.assertEqual(rows["Deep Seen"]["status"], "sighted-zero-diff")
        self.assertEqual(rows["Deep Seen"]["act2_3_sightings_clean"], 1)
        self.assertEqual(rows["Deep Seen"]["witness"],
                         {"campaign_id": "bg.worker-001", "seed": "STS2"})
        self.assertEqual(rows["Cross Act"]["status"], "sighted-zero-diff")
        self.assertEqual(rows["Deep Owed"]["status"], "OWED")
        self.assertEqual(rows["Deep Owed"]["sim_prep_census_rows"], 0)
        self.assertEqual(rows["Cross Act"]["sim_prep_census_rows"], 3)
        self.assertEqual(report["item4_event_depth"]["owed"], 1)
        self.assertFalse(report["item4_event_depth"]["met"])

    def test_an_act1_only_sighting_does_not_satisfy_the_deep_bar(self):
        self.group("bg", ["bg.worker-001"])
        self.worker("bg.worker-001", [
            {"seed": "STS1", "records": [(1, "Act1 Boss", "Cross Act")]},
        ])
        report = self.run_aggregate()
        rows = {row["game_id"]: row
                for row in report["item4_event_depth"]["rows"]}
        self.assertEqual(rows["Cross Act"]["any_act_sightings_clean"], 1)
        self.assertEqual(rows["Cross Act"]["act2_3_sightings_clean"], 0)
        self.assertEqual(rows["Cross Act"]["status"], "OWED")

    def test_event_row_disposition_replaces_the_owed_status(self):
        self.simple_tree()
        self.set_dispositions([], [{
            "game_id": "Deep Owed", "status": "reachability-argument",
            "reference": "fixture", "note": "fixture",
        }])
        report = self.run_aggregate()
        rows = {row["game_id"]: row
                for row in report["item4_event_depth"]["rows"]}
        self.assertEqual(rows["Deep Owed"]["status"], "disposition-on-record")
        self.assertEqual(report["item4_event_depth"]["owed"], 0)
        self.assertTrue(report["item4_event_depth"]["met"])

    def test_a_wildcard_event_disposition_status_is_fatal(self):
        self.simple_tree()
        self.set_dispositions([], [{
            "game_id": "Deep Owed", "status": "probably-unreachable",
            "reference": "fixture", "note": "fixture",
        }])
        with self.assertRaises(ReportError) as caught:
            self.run_aggregate()
        self.assertIn("invalid event status", str(caught.exception))

    def test_a_census_naming_an_unknown_event_is_fatal(self):
        self.simple_tree()
        write(self.census, self.census.read_text(encoding="utf-8").replace(
            "  Cross Act: 3 (30.00%)", "  Cross Act: 3 (30.00%)\n"
            "  Ghost Event: 1 (10.00%)"))
        with self.assertRaises(ReportError) as caught:
            self.run_aggregate()
        self.assertIn("not registry rows", str(caught.exception))

    def test_census_head_and_depth_are_parsed(self):
        self.simple_tree()
        census = self.run_aggregate()["inputs"]["prep_census"]
        self.assertEqual(census["rows"], 10)
        self.assertEqual(census["seeds"], 5)
        self.assertEqual(census["max_floor"], 20)
        self.assertEqual(census["depth_all_policies"]["fight"],
                         {"act1": 4, "act2": 0, "act3": 0})
        self.assertEqual(census["depth_all_policies"]["kill"]["act1"], 1)


if __name__ == "__main__":
    unittest.main()
