#!/usr/bin/env python3

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

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
                 artifact_hash=None):
        directory = self.root / "campaigns" / campaign_id
        directory.mkdir(parents=True, exist_ok=True)
        artifact = directory / f"run_{seed}_a20_ironclad.jsonl"
        artifact.write_text(json.dumps({
            "record_kind": "action",
            "state_json": {"nested": ["gid-cards", {"x": "gid-relics"}]},
        }) + "\n", encoding="utf-8")
        digest = artifact_hash or vr.sha256_file(artifact)
        run = {
            "seed": seed, "actions": 7, "classification": classification,
            "known_obtain_race_records": 0, "outcome": "death",
            "source_artifact": artifact.name,
            "source_artifact_sha256": digest,
        }
        report = {
            "report_format": "STS-ORACLE-CAMPAIGN-REPORT v1",
            "campaign_id": campaign_id, "campaign_status": "complete",
            "schema_version": 1, "driver_version": "d",
            "pipeline_version": "p", "fork_jar_sha256": "f",
            "policy": "random-legal", "runs": [run],
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

    def test_exact_duplicate_is_deduplicated_conflict_is_rejected(self):
        run = self.campaign("c1", "S1")
        self.campaign("c2", "S1")
        first = vr.aggregate(self.root / "campaigns", ["c1", "c2"],
                             self.coverage, self.dispositions, self.registry)
        self.assertEqual(first["g7_evidence"]["distinct_seeds"], 1)
        self.assertEqual(first["g7_evidence"]["captured_actions"], 7)
        report = vr.read_json(self.root / "campaigns" / "c2" / "report.json")
        report["runs"][0]["actions"] = run["actions"] + 1
        dump(self.root / "campaigns" / "c2" / "report.json", report)
        with self.assertRaisesRegex(vr.ReportError, "conflicting duplicate"):
            vr.aggregate(self.root / "campaigns", ["c1", "c2"],
                         self.coverage, self.dispositions, self.registry)

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
        self.assertFalse(report["g7_evidence"]["g7_oracle_volume_met"])

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
