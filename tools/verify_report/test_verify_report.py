#!/usr/bin/env python3

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import build_ci_corpus as corpus_builder
import check_g7_proactive_coverage as proactive
import generate_report as vr
from ci_corpus_smoke import CorpusError, validate_archive


def dump(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")


class VerifyReportTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.registry = self.root / "registry"
        for key, filename, _enum, _underlying in vr.DOMAINS:
            row = {
                "id": 1, "provenance": "fixture",
                **({"level": 1} if key == "a20" else
                   {"name": key.upper(), "game_id": f"gid-{key}"}),
            }
            dump(self.registry / filename, [row])
        self.coverage = {
            "coverage": {
                key: {
                    ("A1" if key == "a20" else key.upper()): {
                        "id": 1, "tier": "direct", "tests": ["Fixture.Pass"],
                    }
                } for key, *_ in vr.DOMAINS
            }
        }
        self.dispositions = self.root / "dispositions.json"
        dump(self.dispositions, {
            "format": "STS-DIVERGENCE-DISPOSITIONS v1", "items": [],
        })

    def tearDown(self):
        self.temp.cleanup()

    def campaign(self, campaign_id="c1", seed="S1", classification="clean",
                 artifact_hash=None, policy="random-legal", outcome="death",
                 act_boss=None):
        directory = self.root / "campaigns" / campaign_id
        directory.mkdir(parents=True, exist_ok=True)
        artifact = directory / f"run_{seed}_a20_ironclad.jsonl"
        artifact.write_text(
            json.dumps({
                "record_kind": "header", "campaign_id": campaign_id,
                "seed": {"string": seed}, "ascension": 20,
                "character": "IRONCLAD",
            }) + "\n" +
            json.dumps({
                "record_kind": "action",
                "state_json": {
                    "nested": ["gid-cards", {"x": "gid-relics"}],
                    **({"game_state": {"act_boss": act_boss}}
                       if act_boss is not None else {}),
                },
            }) + "\n",
            encoding="utf-8")
        digest = artifact_hash or vr.sha256_file(artifact)
        run = {
            "seed": seed, "actions": 7, "classification": classification,
            "known_obtain_race_records": 0, "outcome": outcome,
            "source_artifact": artifact.name,
            "source_artifact_sha256": digest,
        }
        report = {
            "report_format": "STS-ORACLE-CAMPAIGN-REPORT v1",
            "campaign_id": campaign_id, "campaign_status": "complete",
            "schema_version": 1, "driver_version": "d",
            "pipeline_version": "p", "fork_jar_sha256": "f",
            "policy": policy, "runs": [run],
        }
        dump(directory / "report.json", report)
        return run

    def test_deterministic_generation_and_game_id_join(self):
        self.campaign()
        first = vr.aggregate(self.root / "campaigns", ["c1"], self.coverage,
                             self.dispositions, self.registry)
        second = vr.aggregate(self.root / "campaigns", ["c1"], self.coverage,
                              self.dispositions, self.registry)
        self.assertEqual(json.dumps(first, sort_keys=True),
                         json.dumps(second, sort_keys=True))
        rows = {r["game_id"]: r for r in first["registry_coverage"]["rows"]
                if r["game_id"]}
        self.assertEqual(rows["gid-cards"]["oracle_sightings"], 1)
        self.assertEqual(rows["gid-relics"]["oracle_sightings"], 1)
        self.assertEqual(rows["gid-potions"]["oracle_sightings"], 0)
        self.assertTrue(first["registry_coverage"]["tier2_complete"])
        self.assertTrue(first["g7_evidence"]["a20_modifiers_verified"])

    def test_strict_actions_exclude_all_current_and_legacy_capture_races(self):
        self.campaign()
        report_path = self.root / "campaigns" / "c1" / "report.json"
        report = vr.read_json(report_path)
        run = report["runs"][0]
        run.update({
            "known_capture_race_records": 1,
            "known_capture_race_records_by_kind": {
                "escape-race": 1,
                "obtain-race": 0,
            },
            "known_escape_race_records": 1,
        })
        dump(report_path, report)
        current = vr.aggregate(
            self.root / "campaigns", ["c1"], self.coverage,
            self.dispositions, self.registry)
        self.assertEqual(current["g7_evidence"]["replay_clean_actions"], 7)
        self.assertEqual(current["g7_evidence"]["strict_zero_diff_actions"], 0)
        self.assertEqual(
            current["g7_evidence"]["known_capture_race_records"], 1)
        self.assertEqual(current["g7_evidence"]["known_capture_race_runs"], 1)

        # An old STS-ORACLE-CAMPAIGN-REPORT v1 has only the obtain field.
        run.pop("known_capture_race_records")
        run.pop("known_capture_race_records_by_kind")
        run.pop("known_escape_race_records")
        run["known_obtain_race_records"] = 1
        dump(report_path, report)
        legacy = vr.aggregate(
            self.root / "campaigns", ["c1"], self.coverage,
            self.dispositions, self.registry)
        self.assertEqual(legacy["g7_evidence"]["replay_clean_actions"], 7)
        self.assertEqual(legacy["g7_evidence"]["strict_zero_diff_actions"], 0)
        self.assertEqual(
            legacy["g7_evidence"]["known_capture_race_records"], 1)

        # A transitional per-family field is conservative even without the
        # new authoritative total.
        run["known_obtain_race_records"] = 0
        run["known_escape_race_records"] = 1
        dump(report_path, report)
        transitional = vr.aggregate(
            self.root / "campaigns", ["c1"], self.coverage,
            self.dispositions, self.registry)
        self.assertEqual(
            transitional["g7_evidence"]["strict_zero_diff_actions"], 0)

    def test_capture_race_total_and_by_kind_must_agree(self):
        self.campaign()
        report_path = self.root / "campaigns" / "c1" / "report.json"
        report = vr.read_json(report_path)
        report["runs"][0].update({
            "known_capture_race_records": 0,
            "known_capture_race_records_by_kind": {"escape-race": 1},
        })
        dump(report_path, report)
        with self.assertRaisesRegex(vr.ReportError, "disagrees"):
            vr.aggregate(
                self.root / "campaigns", ["c1"], self.coverage,
                self.dispositions, self.registry)

        report["runs"][0].update({
            "known_capture_race_records": 0,
            "known_capture_race_records_by_kind": {
                "escape-race": 0,
                "obtain-race": 0,
            },
            "known_escape_race_records": 1,
        })
        dump(report_path, report)
        with self.assertRaisesRegex(
                vr.ReportError, "legacy escape-race count 1 disagrees"):
            vr.aggregate(
                self.root / "campaigns", ["c1"], self.coverage,
                self.dispositions, self.registry)

    def test_ci_corpus_excludes_escape_race_clean_run(self):
        self.campaign()
        report_path = self.root / "campaigns" / "c1" / "report.json"
        report = vr.read_json(report_path)
        report["runs"][0].update({
            "known_capture_race_records": 1,
            "known_capture_race_records_by_kind": {"escape-race": 1},
        })
        dump(report_path, report)
        with self.assertRaisesRegex(vr.ReportError, "0 eligible clean seeds"):
            corpus_builder.select(
                self.root / "campaigns", ["c1"], 1)

    def test_non_a20_artifact_is_rejected(self):
        self.campaign()
        path = (
            self.root / "campaigns" / "c1" /
            "run_S1_a20_ironclad.jsonl"
        )
        lines = path.read_text(encoding="utf-8").splitlines()
        header = json.loads(lines[0])
        header["ascension"] = 19
        path.write_text(
            json.dumps(header) + "\n" + "\n".join(lines[1:]) + "\n",
            encoding="utf-8")
        report = vr.read_json(
            self.root / "campaigns" / "c1" / "report.json")
        report["runs"][0]["source_artifact_sha256"] = vr.sha256_file(path)
        dump(self.root / "campaigns" / "c1" / "report.json", report)
        with self.assertRaisesRegex(vr.ReportError, "not a full-scope A20"):
            vr.aggregate(
                self.root / "campaigns", ["c1"], self.coverage,
                self.dispositions, self.registry)

    def test_non_gameplay_terminal_is_rejected(self):
        self.campaign()
        report_path = (
            self.root / "campaigns" / "c1" / "report.json"
        )
        report = vr.read_json(report_path)
        report["runs"][0]["outcome"] = "legal_exhaustion"
        dump(report_path, report)
        with self.assertRaisesRegex(vr.ReportError, "non-gameplay terminal"):
            vr.aggregate(
                self.root / "campaigns", ["c1"], self.coverage,
                self.dispositions, self.registry)

    def test_missing_a20_tier2_row_cannot_satisfy_the_gate(self):
        self.campaign()
        self.coverage["coverage"]["a20"]["A1"]["tier"] = None
        report = vr.aggregate(
            self.root / "campaigns", ["c1"], self.coverage,
            self.dispositions, self.registry)
        self.assertFalse(report["g7_evidence"]["a20_modifiers_verified"])
        self.assertFalse(report["g7_evidence"]["g7_oracle_evidence_met"])

    def test_exact_duplicate_is_deduplicated_conflict_is_rejected(self):
        run = self.campaign("c1", "S1")
        report = vr.read_json(
            self.root / "campaigns" / "c1" / "report.json")
        report["runs"].append(dict(run))
        dump(self.root / "campaigns" / "c1" / "report.json", report)
        first = vr.aggregate(self.root / "campaigns", ["c1"],
                             self.coverage, self.dispositions, self.registry)
        self.assertEqual(first["g7_evidence"]["distinct_seeds"], 1)
        self.assertEqual(first["g7_evidence"]["captured_actions"], 7)
        self.assertEqual(
            first["inputs"]["campaigns"][0]["deduplicated_runs"], 1)

        self.campaign("c2", "S1")
        with self.assertRaisesRegex(vr.ReportError, "conflicting duplicate"):
            vr.aggregate(self.root / "campaigns", ["c1", "c2"],
                         self.coverage, self.dispositions, self.registry)

    def test_mixed_generator_status_is_literal(self):
        self.campaign("c1", "S1", policy="random-legal")
        self.campaign("c2", "S2", policy="greedy")
        report = vr.aggregate(
            self.root / "campaigns", ["c1", "c2"], self.coverage,
            self.dispositions, self.registry)
        evidence = report["g7_evidence"]
        self.assertEqual(evidence["generator_policies"],
                         ["greedy", "random-legal"])
        self.assertTrue(evidence["mixed_generators"])
        rendered = vr.markdown(report)
        self.assertIn("mixed-generator requirement met: **YES**", rendered)
        self.assertIn("| c1 | random-legal |", rendered)
        self.assertIn("| c2 | greedy |", rendered)

    def test_disposition_is_exact_and_does_not_close_open_finding(self):
        self.campaign("c1", "S1", "state_divergence")
        report = vr.aggregate(self.root / "campaigns", ["c1"], self.coverage,
                              self.dispositions, self.registry)
        self.assertEqual(report["g7_evidence"]["untriaged_findings"], 1)
        dump(self.dispositions, {
            "format": "STS-DIVERGENCE-DISPOSITIONS v1",
            "items": [{
                "campaign_id": "c1", "seed": "S1",
                "classification": "state_divergence",
                "status": "open-product-divergence",
                "reference": "ledger", "note": "still open",
            }],
        })
        report = vr.aggregate(self.root / "campaigns", ["c1"], self.coverage,
                              self.dispositions, self.registry)
        self.assertTrue(report["g7_evidence"]["zero_untriaged"])
        self.assertEqual(
            report["divergence_inventory"][0]["disposition"]["status"],
            "open-product-divergence")
        self.assertEqual(report["g7_evidence"]["open_findings"], 1)
        self.assertFalse(report["g7_evidence"]["zero_open_findings"])
        self.assertFalse(report["g7_evidence"]["g7_oracle_evidence_met"])

    def test_action_total_is_diagnostic_not_a_gate_quota(self):
        self.campaign()
        report = vr.aggregate(
            self.root / "campaigns", ["c1"], self.coverage,
            self.dispositions, self.registry)
        evidence = report["g7_evidence"]
        self.assertEqual(evidence["strict_zero_diff_actions"], 7)
        self.assertNotIn(
            "strict_zero_diff_action_shortfall_to_1000000", evidence)
        self.assertIn("no action-count quota", vr.markdown(report))

    def test_depth_coverage_is_derived_from_registry_boss_rows(self):
        encounter_path = self.registry / "encounters.yaml"
        encounters = json.loads(encounter_path.read_text(encoding="utf-8"))
        encounters[0].update({"act": 1, "pool": "BOSS"})
        dump(encounter_path, encounters)
        self.campaign(
            outcome="act1_boss_reward", act_boss="gid-encounters")
        report = vr.aggregate(
            self.root / "campaigns", ["c1"], self.coverage,
            self.dispositions, self.registry)
        evidence = report["g7_evidence"]
        self.assertEqual(evidence["expected_act1_bosses"], ["gid-encounters"])
        self.assertEqual(
            evidence["act1_boss_reward_claims_by_boss"],
            {"gid-encounters": 1})
        self.assertTrue(evidence["all_act1_bosses_reward_claimed"])

    def test_dispositions_for_unselected_campaigns_are_not_stale(self):
        self.campaign("selected", "S1")
        dump(self.dispositions, {
            "format": "STS-DIVERGENCE-DISPOSITIONS v1",
            "items": [{
                "campaign_id": "historical", "seed": "S9",
                "classification": "state_divergence",
                "status": "resolved", "reference": "history",
                "note": "Not part of this aggregate.",
            }],
        })
        report = vr.aggregate(
            self.root / "campaigns", ["selected"], self.coverage,
            self.dispositions, self.registry)
        self.assertEqual(report["g7_evidence"]["untriaged_findings"], 0)

    def test_proactive_manifest_requires_registered_passing_regressions(self):
        manifest = {
            "sources": ["docs/stage-b-design.md"],
            "families": [{
                "id": "family", "risk": "historical risk",
                "regressions": [
                    {"test": "Suite.Pass", "witness": "one"},
                    {"test": "Suite.Missing", "witness": "two"},
                ],
            }],
        }
        report = proactive.evaluate(
            manifest, {"Suite.Pass"}, {"Suite.Pass": True})
        self.assertFalse(report["passed"])
        self.assertTrue(report["families"][0]["regressions"][0]["passed"])
        self.assertFalse(
            report["families"][0]["regressions"][1]["registered"])

    def test_proactive_manifest_requires_existing_historical_sources(self):
        path = self.root / "proactive.json"
        manifest = {
            "format": proactive.FORMAT,
            "sources": ["docs/stage-b-design.md"],
            "families": [{
                "id": "family", "risk": "historical risk",
                "regressions": [
                    {"test": "Suite.One", "witness": "one"},
                    {"test": "Suite.Two", "witness": "two"},
                ],
            }],
        }
        dump(path, manifest)
        self.assertEqual(proactive.load_manifest(path), manifest)
        manifest["sources"] = ["docs/does-not-exist.md"]
        dump(path, manifest)
        with self.assertRaises(proactive.AuditError):
            proactive.load_manifest(path)

    def test_committed_corpus_integrity_and_manifest_hash(self):
        corpus = vr.REPO / "tests" / "golden" / "oracle_corpus"
        manifest, members = validate_archive(
            corpus / "act1_a20_50.tar.gz",
            corpus / "act1_a20_50.manifest.json")
        self.assertEqual(manifest["entry_count"], 50)
        self.assertEqual(len(members), 100)
        broken = self.root / "broken.tar.gz"
        data = bytearray((corpus / "act1_a20_50.tar.gz").read_bytes())
        data[-1] ^= 1
        broken.write_bytes(data)
        with self.assertRaisesRegex(CorpusError, "SHA-256"):
            validate_archive(broken, corpus / "act1_a20_50.manifest.json")


if __name__ == "__main__":
    unittest.main()
