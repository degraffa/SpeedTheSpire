#!/usr/bin/env python3
"""Synthetic tests for the B5.2 outer pipeline (no game process)."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import campaign_pipeline


def _write_json(path, value):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(value, fh)
        fh.write("\n")


def _run(path, seed):
    records = [
        {"record_kind": "header", "seed": {"string": seed}},
        {"record_kind": "action", "seq": 0,
         "action_command": "choose 0"},
        {"record_kind": "action", "seq": 1,
         "action_command": "play 1 0"},
        {"record_kind": "terminal", "seq": 2, "actions": 2},
    ]
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        for record in records:
            fh.write(json.dumps(record) + "\n")


def _timing(path):
    records = [
        {"record_kind": "timing_header"},
        {"record_kind": "mark", "seq": 0, "t_mono": 10.0},
        {"record_kind": "mark", "seq": 1, "t_mono": 12.0},
    ]
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        for record in records:
            fh.write(json.dumps(record) + "\n")


class ShardingTest(unittest.TestCase):
    def test_two_hundred_seed_shape_is_stable_and_complete(self):
        seeds = [f"STS{i:05d}" for i in range(200)]
        shards = [
            campaign_pipeline.shard_seeds(seeds, 7, index)
            for index in range(7)
        ]
        self.assertEqual(
            sorted(seed for shard in shards for seed in shard),
            sorted(seeds))
        self.assertEqual(len(set(seed for shard in shards for seed in shard)),
                         200)
        self.assertEqual(shards[3], seeds[3::7])
        self.assertEqual(
            campaign_pipeline.shard_campaign_id("nightly", 7, 3),
            "nightly.shard-004-of-007")

    def test_prepare_refuses_resume_with_changed_identity(self):
        with tempfile.TemporaryDirectory() as root:
            seeds = os.path.join(root, "seeds.txt")
            with open(seeds, "w", encoding="utf-8") as fh:
                fh.write("STS00001\nSTS00002\n")
            with mock.patch.object(
                    campaign_pipeline, "CAMPAIGN_ROOT",
                    os.path.join(root, "campaigns")):
                campaign_pipeline.prepare_campaign(
                    "same", seeds, 1, 0, "greedy", 7)
                with self.assertRaisesRegex(ValueError, "identity mismatch"):
                    campaign_pipeline.prepare_campaign(
                        "same", seeds, 1, 0, "random-legal", 7)


class ReportAndTriageTest(unittest.TestCase):
    def test_report_counts_throughput_and_writes_minimal_reproducer(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_id = "report-test"
            campaign_dir = os.path.join(root, campaign_id)
            os.makedirs(campaign_dir)
            rows = [
                {"seed": "STS00001", "outcome": "death", "floor": 3,
                 "actions": 2, "attempts": 1,
                 "artifact": "run_STS00001_a20_ironclad.jsonl"},
                {"seed": "STS00002", "outcome": "death", "floor": 4,
                 "actions": 2, "attempts": 1,
                 "artifact": "run_STS00002_a20_ironclad.jsonl"},
            ]
            _write_json(os.path.join(campaign_dir, "campaign_progress.json"), {
                "status": "complete", "seed_list": [r["seed"] for r in rows],
                "seeds_done": rows, "seeds_failed": [], "policy": "greedy",
                "policy_seed": 11, "started_utc": "2026-07-29T00:00:00Z",
            })
            _write_json(os.path.join(campaign_dir, "campaign_manifest.json"), {
                "finished_utc": "2026-07-29T01:00:00Z",
            })
            _write_json(os.path.join(campaign_dir, "pipeline_config.json"), {
                "shard_count": 2, "shard_index": 1,
                "source_seed_count": 4,
            })
            for seed in ("STS00001", "STS00002"):
                _run(os.path.join(
                    campaign_dir,
                    f"run_{seed}_a20_ironclad.jsonl"), seed)
                _timing(os.path.join(
                    campaign_dir,
                    f"run_{seed}_a20_ironclad.timing.jsonl"))
                for directory in ("translation", "diffs", "encounter_lists"):
                    os.makedirs(os.path.join(campaign_dir, directory),
                                exist_ok=True)
                for directory in ("translation", "encounter_lists"):
                    with open(os.path.join(
                            campaign_dir, directory, f"{seed}.status"),
                            "w", encoding="ascii") as fh:
                        fh.write("0\n")
            with open(os.path.join(
                    campaign_dir, "diffs", "STS00001.status"),
                    "w", encoding="ascii") as fh:
                fh.write("0\n")
            with open(os.path.join(
                    campaign_dir, "diffs", "STS00001.log"),
                    "w", encoding="utf-8") as fh:
                fh.write("CLEAN run: 2 records; 0 obtain-race\n")
            with open(os.path.join(
                    campaign_dir, "diffs", "STS00002.status"),
                    "w", encoding="ascii") as fh:
                fh.write("1\n")
            with open(os.path.join(
                    campaign_dir, "diffs", "STS00002.log"),
                    "w", encoding="utf-8") as fh:
                fh.write(
                    "PART run\n"
                    "first divergence: seq=1 floor=4 screen=NONE "
                    "(2 fields)\n")

            with mock.patch.object(campaign_pipeline, "CAMPAIGN_ROOT", root):
                report = campaign_pipeline.generate_report(campaign_id)

            self.assertEqual(report["diff_counts"],
                             {"clean": 1, "state_divergence": 1})
            self.assertEqual(report["untriaged_count"], 1)
            self.assertAlmostEqual(report["actions_per_second"], 1.0)
            self.assertEqual(report["captured_actions"], 4)
            self.assertEqual(report["replay_clean_actions"], 2)
            self.assertEqual(report["strict_zero_diff_actions"], 2)
            self.assertEqual(report["outcome_counts"], {"death": 2})
            self.assertEqual(report["floor_counts"], {"3": 1, "4": 1})
            self.assertEqual(
                len(report["runs"][0]["source_artifact_sha256"]), 64)
            repro = campaign_pipeline._read_json(os.path.join(
                campaign_dir, "triage", "pending",
                "STS00002.reproducer.json"))
            self.assertEqual(
                [row["command"] for row in repro["action_prefix"]],
                ["choose 0", "play 1 0"])
            self.assertEqual(
                repro["first_divergence"]["first_divergence_seq"], 1)


class ProcessBoundaryTest(unittest.TestCase):
    def test_game_resource_lock_rejects_concurrency_and_recovers_after_crash(
            self):
        with tempfile.TemporaryDirectory() as root:
            lock_path = os.path.join(root, "oracle_game.lock")
            driver_dir = os.path.dirname(campaign_pipeline.__file__)
            child_code = (
                "import sys,time;"
                "sys.path.insert(0,sys.argv[1]);"
                "from campaign_pipeline import GameResourceLock;"
                "lock=GameResourceLock('first-campaign',sys.argv[2]);"
                "lock.acquire();"
                "print('acquired',flush=True);"
                "time.sleep(60)"
            )
            child = subprocess.Popen(
                [sys.executable, "-c", child_code, driver_dir, lock_path],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                self.assertEqual(child.stdout.readline().strip(), "acquired")
                contender = campaign_pipeline.GameResourceLock(
                    "second-campaign", lock_path)
                with self.assertRaisesRegex(
                        campaign_pipeline.GameResourceBusy,
                        "campaign=first-campaign"):
                    contender.acquire()
            finally:
                child.terminate()
                child.communicate(timeout=10)

            # A killed owner cannot strand the singleton: the OS releases it.
            successor = campaign_pipeline.GameResourceLock(
                "second-campaign", lock_path)
            successor.acquire()
            successor.release()

    @mock.patch("campaign_pipeline.subprocess.run")
    def test_nightly_schedule_uses_short_external_config_action(self, run):
        run.return_value.returncode = 0
        with tempfile.TemporaryDirectory() as root, mock.patch.object(
                campaign_pipeline, "SCHEDULE_ROOT", root):
            seeds = os.path.join(root, "seeds.txt")
            with open(seeds, "w", encoding="utf-8") as fh:
                fh.write("STS00001\n")
            args = campaign_pipeline.parse_args([
                "schedule", "--campaign-prefix", "nightly",
                "--seeds", seeds, "--task-name", "oracle-nightly"])
            self.assertEqual(campaign_pipeline.schedule_nightly(args), 0)
            config = campaign_pipeline._read_json(os.path.join(
                root, "oracle-nightly.json"))
            self.assertEqual(config["arguments"][0], "nightly")
            task_action = run.call_args.args[0][-1]
            self.assertLessEqual(len(task_action), 262)
            self.assertIn(" scheduled ", task_action)

    @mock.patch("campaign_pipeline.subprocess.run")
    def test_postprocess_uses_the_sanctioned_wsl_helper(self, run):
        run.return_value.returncode = 0
        self.assertEqual(campaign_pipeline.run_postprocess("safe-id"), 0)
        command = run.call_args.args[0]
        self.assertEqual(command[:4], ["cmd.exe", "/d", "/s", "/c"])
        self.assertIn("wsl_run.cmd", command[4])
        self.assertIn("postprocess_campaign.sh", command[4])
        self.assertIn(
            "tools/oracle_bridge/driver/postprocess_campaign.sh", command[4])
        self.assertNotIn(
            "tools\\oracle_bridge\\driver\\postprocess_campaign.sh", command[4])
        self.assertNotIn("wsl.exe", command[4].lower())


if __name__ == "__main__":
    unittest.main()
