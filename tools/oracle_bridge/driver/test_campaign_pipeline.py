#!/usr/bin/env python3
"""Synthetic tests for the B5.2 outer pipeline (no game process)."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest import mock

import campaign_pipeline
import instance_runtime
import orchestrator


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


def _runtime_source(root):
    game = os.path.join(root, "source-game")
    preferences = os.path.join(game, "preferences")
    os.makedirs(preferences)
    for name in instance_runtime.REQUIRED_PROFILE_FILES:
        value = ({key: "2"
                  for key in instance_runtime.REQUIRED_POOL_UNLOCK_KEYS}
                 if name == "STSUnlocks" else {})
        _write_json(os.path.join(preferences, name), value)
    with open(os.path.join(game, instance_runtime.DESKTOP_JAR), "wb") as fh:
        fh.write(b"synthetic desktop jar")
    with open(os.path.join(game, "info.displayconfig"), "wb") as fh:
        fh.write(b"display config")
    with open(os.path.join(game, "config.json"), "wb") as fh:
        fh.write(b"launcher config")
    with open(os.path.join(game, "steam_appid.txt"), "wb") as fh:
        fh.write(b"646570")
    fork = os.path.join(root, "source-fork.jar")
    with open(fork, "wb") as fh:
        fh.write(b"synthetic oracle fork")
    return game, fork


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

    def test_parallel_plan_preserves_outer_shard_and_is_stable_complete(self):
        with tempfile.TemporaryDirectory() as root:
            seeds = [f"STS{i:05d}" for i in range(30)]
            seed_path = os.path.join(root, "seeds.txt")
            with open(seed_path, "w", encoding="utf-8") as fh:
                fh.write("\n".join(seeds) + "\n")
            campaign_root = os.path.join(root, "campaigns")
            with mock.patch.object(
                    campaign_pipeline, "CAMPAIGN_ROOT", campaign_root):
                group, _paths = campaign_pipeline.prepare_parallel_group(
                    "parallel", seed_path, 3, 1, "greedy", 17, 4)
                resumed, _resumed_paths = \
                    campaign_pipeline.prepare_parallel_group(
                        "parallel", seed_path, 3, 1, "greedy", 17, 4)

            selected = seeds[1::3]
            worker_seeds = [
                row["seed_list"] for row in group["workers"]
            ]
            flattened = [seed for rows in worker_seeds for seed in rows]
            self.assertEqual(selected, group["selected_seed_list"])
            self.assertEqual(sorted(selected), sorted(flattened))
            self.assertEqual(len(flattened), len(set(flattened)))
            self.assertEqual(
                [selected[index::4] for index in range(4)], worker_seeds)
            self.assertEqual(group, resumed)
            self.assertEqual(
                [campaign_pipeline.worker_campaign_id(
                    "parallel.shard-002-of-003", 4, index)
                 for index in range(4)],
                [row["campaign_id"] for row in group["workers"]])

    def test_parallel_resume_refuses_changed_resolved_instance_count(self):
        with tempfile.TemporaryDirectory() as root:
            seed_path = os.path.join(root, "seeds.txt")
            with open(seed_path, "w", encoding="utf-8") as fh:
                fh.write("STS00001\nSTS00002\nSTS00003\n")
            with mock.patch.object(
                    campaign_pipeline, "CAMPAIGN_ROOT",
                    os.path.join(root, "campaigns")):
                campaign_pipeline.prepare_parallel_group(
                    "parallel", seed_path, 1, 0,
                    "random-legal", 7, 3)
                with self.assertRaisesRegex(
                        ValueError, "already 3 instance"):
                    campaign_pipeline.prepare_parallel_group(
                        "parallel", seed_path, 1, 0,
                        "random-legal", 7, 2)

    def test_auto_instance_count_obeys_cpu_memory_and_seed_caps(self):
        with self.subTest("cpu"), \
                mock.patch.object(
                    campaign_pipeline, "_physical_core_budget",
                    return_value=3), \
                mock.patch.object(
                    campaign_pipeline, "_available_memory_mib",
                    return_value=64 * 1024):
            self.assertEqual(
                3, campaign_pipeline.resolve_instance_count("auto", 10))

        per_worker = (
            campaign_pipeline.DEFAULT_JAVA_XMX_MIB
            + campaign_pipeline.AUTO_NATIVE_HEADROOM_MIB)
        with self.subTest("memory"), \
                mock.patch.object(
                    campaign_pipeline, "_physical_core_budget",
                    return_value=16), \
                mock.patch.object(
                    campaign_pipeline, "_available_memory_mib",
                    return_value=(
                        campaign_pipeline.AUTO_MEMORY_RESERVE_MIB
                        + 2 * per_worker)):
            self.assertEqual(
                2, campaign_pipeline.resolve_instance_count("auto", 10))

        with self.subTest("seeds"), \
                mock.patch.object(
                    campaign_pipeline, "_physical_core_budget",
                    return_value=16), \
                mock.patch.object(
                    campaign_pipeline, "_available_memory_mib",
                    return_value=64 * 1024):
            self.assertEqual(
                2, campaign_pipeline.resolve_instance_count("auto", 2))

    def test_auto_instance_count_falls_back_to_cpu_without_memory_sample(self):
        with mock.patch.object(
                campaign_pipeline, "_physical_core_budget",
                return_value=5), mock.patch.object(
                    campaign_pipeline, "_available_memory_mib",
                    return_value=None):
            self.assertEqual(
                5, campaign_pipeline.resolve_instance_count("auto", 10))

    def test_auto_resume_keeps_persisted_count_when_hardware_changes(self):
        with tempfile.TemporaryDirectory() as root:
            seeds = os.path.join(root, "seeds.txt")
            with open(seeds, "w", encoding="utf-8") as fh:
                fh.write("\n".join(
                    f"STS{i:05d}" for i in range(1, 9)) + "\n")
            with mock.patch.object(
                    campaign_pipeline, "CAMPAIGN_ROOT",
                    os.path.join(root, "campaigns")), mock.patch.object(
                        campaign_pipeline, "_physical_core_budget",
                        return_value=2), mock.patch.object(
                            campaign_pipeline, "_available_memory_mib",
                            return_value=64 * 1024):
                first, _ = campaign_pipeline.prepare_parallel_group(
                    "sticky-auto", seeds, 1, 0,
                    "random-legal", 7, "auto")
            with mock.patch.object(
                    campaign_pipeline, "CAMPAIGN_ROOT",
                    os.path.join(root, "campaigns")), mock.patch.object(
                        campaign_pipeline, "_physical_core_budget",
                        return_value=8), mock.patch.object(
                            campaign_pipeline, "_available_memory_mib",
                            return_value=128 * 1024):
                resumed, _ = campaign_pipeline.prepare_parallel_group(
                    "sticky-auto", seeds, 1, 0,
                    "random-legal", 7, "auto")
            self.assertEqual(2, first["resolved_instances"])
            self.assertEqual(2, resumed["resolved_instances"])
            self.assertEqual(first["workers"], resumed["workers"])


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
                fh.write(
                    "CLEAN run: 2 records; 0 library-order-only, "
                    "0 obtain-race, 1 escape-race, 1 preview-race; "
                    "stop: run terminal\n")
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
            self.assertEqual(report["strict_zero_diff_actions"], 0)
            self.assertEqual(report["known_capture_race_records"], 2)
            self.assertEqual(report["known_obtain_race_records"], 0)
            self.assertEqual(report["known_escape_race_records"], 1)
            self.assertEqual(
                report["known_capture_race_records_by_kind"],
                {"obtain-race": 0, "escape-race": 1, "preview-race": 1})
            self.assertEqual(
                report["runs"][0]["known_capture_race_records"], 2)
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

    def test_capture_race_parser_is_all_family_and_old_log_compatible(self):
        self.assertEqual(
            campaign_pipeline._capture_race_counts(
                "RACE diagnostic cites B5.2 obtain-race\n"
                "CLEAN old: 2 records; 3 obtain-race\n"),
            {"obtain-race": 3})
        self.assertEqual(
            campaign_pipeline._capture_race_counts(
                "CLEAN new: 2 records; 1 obtain-race, 2 escape-race, "
                "3 preview-race, 4 future-race; stop: run terminal\n"),
            {
                "obtain-race": 1,
                "escape-race": 2,
                "preview-race": 3,
                "future-race": 4,
            })
        with self.assertRaisesRegex(ValueError, "repeats"):
            campaign_pipeline._capture_race_counts(
                "CLEAN duplicate: 1 obtain-race, 1 obtain-race")


class InstanceRuntimeTest(unittest.TestCase):
    def test_profile_rejects_one_missing_required_pool_unlock(self):
        with tempfile.TemporaryDirectory() as root:
            game, fork = _runtime_source(root)
            unlocks_path = os.path.join(game, "preferences", "STSUnlocks")
            with open(unlocks_path, "r", encoding="utf-8") as fh:
                unlocks = json.load(fh)
            missing = instance_runtime.REQUIRED_POOL_UNLOCK_KEYS[0]
            del unlocks[missing]
            _write_json(unlocks_path, unlocks)

            self.assertEqual(
                60, len(set(instance_runtime.REQUIRED_POOL_UNLOCK_KEYS)))
            with self.assertRaisesRegex(
                    ValueError, "missing required STSUnlocks pool entries"):
                instance_runtime.prepare_instance_runtime(
                    "campaign", game, fork, os.path.join(root, "runtimes"))

    def test_resume_allows_private_stats_but_rejects_unlock_corruption(self):
        with tempfile.TemporaryDirectory() as root:
            game, fork = _runtime_source(root)
            runtime_root = os.path.join(root, "runtimes")
            runtime = instance_runtime.prepare_instance_runtime(
                "campaign", game, fork, runtime_root)
            player_path = os.path.join(
                runtime.game_workdir, "preferences", "STSPlayer")
            player_stats = {"PLAYTIME": "ordinary worker state changed"}
            _write_json(player_path, player_stats)
            resumed = instance_runtime.prepare_instance_runtime(
                "campaign", game, fork, runtime_root)
            with open(os.path.join(
                    resumed.game_workdir, "preferences", "STSPlayer"),
                    "r", encoding="utf-8") as fh:
                self.assertEqual(player_stats, json.load(fh))

            unlocks_path = os.path.join(
                runtime.game_workdir, "preferences", "STSUnlocks")
            with open(unlocks_path, "r", encoding="utf-8") as fh:
                unlocks = json.load(fh)
            del unlocks[instance_runtime.REQUIRED_POOL_UNLOCK_KEYS[-1]]
            _write_json(unlocks_path, unlocks)

            with self.assertRaisesRegex(
                    ValueError, "missing required STSUnlocks pool entries"):
                instance_runtime.prepare_instance_runtime(
                    "campaign", game, fork, runtime_root)

    def test_root_copy_fidelity_and_immutable_resume_drift_fail(self):
        with self.subTest("copy fidelity"), \
                tempfile.TemporaryDirectory() as root:
            game, fork = _runtime_source(root)
            real_copy2 = instance_runtime.shutil.copy2

            def corrupt_config_copy(source, destination, *args, **kwargs):
                result = real_copy2(source, destination, *args, **kwargs)
                if os.path.basename(source) == "config.json":
                    with open(destination, "ab") as fh:
                        fh.write(b" copy corruption")
                return result

            with mock.patch.object(
                    instance_runtime.shutil, "copy2",
                    side_effect=corrupt_config_copy), \
                    self.assertRaisesRegex(
                        ValueError,
                        "immutable-copy root-file drift: config.json"):
                instance_runtime.prepare_instance_runtime(
                    "campaign", game, fork, os.path.join(root, "runtimes"))

        with self.subTest("immutable resume drift"), \
                tempfile.TemporaryDirectory() as root:
            game, fork = _runtime_source(root)
            runtime_root = os.path.join(root, "runtimes")
            runtime = instance_runtime.prepare_instance_runtime(
                "campaign", game, fork, runtime_root)
            with open(os.path.join(
                    runtime.game_workdir, "config.json"), "ab") as fh:
                fh.write(b" runtime drift")
            with self.assertRaisesRegex(
                    ValueError, "immutable root-file drift: config.json"):
                instance_runtime.prepare_instance_runtime(
                    "campaign", game, fork, runtime_root)

    def test_game_written_root_settings_do_not_rebind_resume(self):
        with tempfile.TemporaryDirectory() as root:
            game, fork = _runtime_source(root)
            runtime_root = os.path.join(root, "runtimes")
            runtime = instance_runtime.prepare_instance_runtime(
                "campaign", game, fork, runtime_root)
            source_display = os.path.join(game, "info.displayconfig")
            private_display = os.path.join(
                runtime.game_workdir, "info.displayconfig")
            with open(source_display, "wb") as fh:
                fh.write(b"operator source display changed")
            with open(private_display, "wb") as fh:
                fh.write(b"worker display changed")

            instance_runtime.prepare_instance_runtime(
                "campaign", game, fork, runtime_root)
            with open(private_display, "rb") as fh:
                self.assertEqual(b"worker display changed", fh.read())

    def test_two_instances_share_template_identity_but_not_mutable_paths(self):
        with tempfile.TemporaryDirectory() as root:
            game, fork = _runtime_source(root)
            runtime_root = os.path.join(root, "runtimes")
            first = instance_runtime.prepare_instance_runtime(
                "campaign-one", game, fork, runtime_root)
            second = instance_runtime.prepare_instance_runtime(
                "campaign-two", game, fork, runtime_root)

            self.assertEqual(first.profile_template_sha256,
                             second.profile_template_sha256)
            self.assertEqual(first.profile_file_digests,
                             second.profile_file_digests)
            for attribute in (
                    "runtime_dir", "game_workdir", "local_app_data",
                    "app_data", "temp_dir", "logs_dir", "fork_jar",
                    "communication_log"):
                self.assertNotEqual(
                    os.path.normcase(getattr(first, attribute)),
                    os.path.normcase(getattr(second, attribute)), attribute)
            for runtime in (first, second):
                for name in ("preferences", "saves", "runs", "sendToDevs"):
                    self.assertTrue(os.path.isdir(os.path.join(
                        runtime.game_workdir, name)))
                with open(runtime.fork_jar, "rb") as fh:
                    self.assertEqual(b"synthetic oracle fork", fh.read())
                self.assertFalse(os.path.samefile(fork, runtime.fork_jar),
                                 "the loaded fork must be a private copy")
                manifest = runtime.manifest_metadata
                self.assertEqual(
                    runtime.fork_sha256,
                    manifest["runtime_immutable"]["fork_jar"])
                self.assertEqual(
                    os.path.normcase(os.path.abspath(runtime.fork_jar)),
                    manifest["paths"]["fork_jar"])

    def test_resume_is_idempotent_and_fresh_is_bounded(self):
        with tempfile.TemporaryDirectory() as root:
            game, fork = _runtime_source(root)
            runtime_root = os.path.join(root, "runtimes")
            first = instance_runtime.prepare_instance_runtime(
                "campaign", game, fork, runtime_root)
            resumed = instance_runtime.prepare_instance_runtime(
                "campaign", game, fork, runtime_root)
            self.assertEqual(first, resumed)

            private_save = os.path.join(
                first.game_workdir, "saves", "IRONCLAD.autosave")
            with open(private_save, "wb") as fh:
                fh.write(b"private mutable state")
            sentinel = os.path.join(runtime_root, "operator-notes.txt")
            with open(sentinel, "wb") as fh:
                fh.write(b"preserve")
            template_manifest = os.path.join(
                runtime_root, instance_runtime.PROFILE_TEMPLATE_ID,
                instance_runtime.PROFILE_MANIFEST)
            template_hash = campaign_pipeline._sha256_file(template_manifest)

            fresh = instance_runtime.prepare_instance_runtime(
                "campaign", game, fork, runtime_root, fresh=True)
            self.assertFalse(os.path.exists(private_save))
            with open(sentinel, "rb") as fh:
                self.assertEqual(b"preserve", fh.read())
            self.assertEqual(
                template_hash,
                campaign_pipeline._sha256_file(template_manifest))
            self.assertEqual(first.profile_template_sha256,
                             fresh.profile_template_sha256)

    def test_operator_profile_drift_does_not_mutate_pinned_template(self):
        with tempfile.TemporaryDirectory() as root:
            game, fork = _runtime_source(root)
            runtime_root = os.path.join(root, "runtimes")
            first = instance_runtime.prepare_instance_runtime(
                "campaign", game, fork, runtime_root)
            private_player = os.path.join(
                first.game_workdir, "preferences", "STSPlayer")
            with open(private_player, "rb") as fh:
                pinned_bytes = fh.read()

            _write_json(os.path.join(game, "preferences", "STSPlayer"), {
                "PLAYTIME": "ordinary operator state changed",
            })
            resumed = instance_runtime.prepare_instance_runtime(
                "campaign", game, fork, runtime_root)
            fresh = instance_runtime.prepare_instance_runtime(
                "campaign", game, fork, runtime_root, fresh=True)

            self.assertEqual(first.profile_template_sha256,
                             resumed.profile_template_sha256)
            self.assertEqual(first.profile_template_sha256,
                             fresh.profile_template_sha256)
            for runtime in (resumed, fresh):
                with open(os.path.join(
                        runtime.game_workdir, "preferences", "STSPlayer"),
                        "rb") as fh:
                    self.assertEqual(pinned_bytes, fh.read())

    def test_source_runtime_and_redirect_drift_fail_loud(self):
        with self.subTest("source fork"), tempfile.TemporaryDirectory() as root:
            game, fork = _runtime_source(root)
            runtime_root = os.path.join(root, "runtimes")
            instance_runtime.prepare_instance_runtime(
                "campaign", game, fork, runtime_root)
            with open(fork, "ab") as fh:
                fh.write(b" drift")
            with self.assertRaisesRegex(ValueError, "source byte drift"):
                instance_runtime.prepare_instance_runtime(
                    "campaign", game, fork, runtime_root)

        with self.subTest("runtime fork"), \
                tempfile.TemporaryDirectory() as root:
            game, fork = _runtime_source(root)
            runtime_root = os.path.join(root, "runtimes")
            runtime = instance_runtime.prepare_instance_runtime(
                "campaign", game, fork, runtime_root)
            with open(runtime.fork_jar, "ab") as fh:
                fh.write(b" drift")
            with self.assertRaisesRegex(ValueError, "runtime fork jar"):
                instance_runtime.prepare_instance_runtime(
                    "campaign", game, fork, runtime_root)

        with self.subTest("redirect"), tempfile.TemporaryDirectory() as root:
            game, fork = _runtime_source(root)
            runtime_root = os.path.join(root, "runtimes")
            runtime = instance_runtime.prepare_instance_runtime(
                "campaign", game, fork, runtime_root)
            os.remove(runtime.fork_jar)
            try:
                os.symlink(fork, runtime.fork_jar)
            except (NotImplementedError, OSError) as exc:
                self.skipTest(f"platform cannot create a file symlink: {exc}")
            with self.assertRaisesRegex(ValueError, "redirected"):
                instance_runtime.prepare_instance_runtime(
                    "campaign", game, fork, runtime_root)


class ParallelRunnerTest(unittest.TestCase):
    @staticmethod
    def _fake_runtime(root, campaign_id):
        runtime_dir = os.path.join(root, "runtimes", campaign_id)
        os.makedirs(runtime_dir, exist_ok=True)
        manifest = os.path.join(runtime_dir, "instance_manifest.json")
        _write_json(manifest, {"campaign_id": campaign_id})
        fields = {
            name: os.path.join(runtime_dir, name)
            for name in (
                "game_workdir", "local_app_data", "app_data", "temp_dir")
        }
        for path in fields.values():
            os.makedirs(path, exist_ok=True)
        fork = os.path.join(runtime_dir, "CommunicationMod-oracle.jar")
        with open(fork, "wb") as fh:
            fh.write(campaign_id.encode("ascii"))
        return SimpleNamespace(
            **fields, fork_jar=fork, manifest_path=manifest,
            runtime_format="STS-ORACLE-INSTANCE v1",
            desktop_sha256="d" * 64, fork_sha256="f" * 64,
            profile_template_sha256="p" * 64)

    def _args(self, seed_path, instances=2):
        return campaign_pipeline.parse_args([
            "run", "--campaign-id", "parallel-runner",
            "--seeds", seed_path, "--instances", str(instances),
        ])

    def test_capture_workers_overlap_then_postprocess_is_serial_after_join(self):
        with tempfile.TemporaryDirectory() as root:
            seeds = os.path.join(root, "seeds.txt")
            with open(seeds, "w", encoding="utf-8") as fh:
                fh.write("STS00001\nSTS00002\nSTS00003\nSTS00004\n")
            args = self._args(seeds)
            barrier = threading.Barrier(2)
            state_lock = threading.Lock()
            active_captures = 0
            max_active_captures = 0
            capture_finished = set()
            active_postprocess = 0
            postprocess_order = []
            lifecycle = []

            def prepare(*positional, **keywords):
                campaign_id = keywords.get("campaign_id", positional[0]
                                           if positional else None)
                return self._fake_runtime(root, campaign_id)

            def capture(_args, campaign_id, _seed_path, _runtime,
                        _process_job):
                nonlocal active_captures, max_active_captures
                with state_lock:
                    active_captures += 1
                    max_active_captures = max(
                        max_active_captures, active_captures)
                barrier.wait(timeout=5)
                time.sleep(0.02)
                with state_lock:
                    active_captures -= 1
                    capture_finished.add(campaign_id)
                return 0

            def postprocess(campaign_id):
                nonlocal active_postprocess
                with state_lock:
                    self.assertEqual(2, len(capture_finished))
                    self.assertEqual(0, active_postprocess)
                    active_postprocess += 1
                time.sleep(0.01)
                with state_lock:
                    active_postprocess -= 1
                    postprocess_order.append(campaign_id)
                    lifecycle.append("postprocess")
                return 0

            lock = mock.Mock()
            lock.acquire.side_effect = lambda: lifecycle.append("acquire")
            lock.release.side_effect = lambda: lifecycle.append("release")
            with mock.patch.object(
                    campaign_pipeline, "CAMPAIGN_ROOT",
                    os.path.join(root, "campaigns")), \
                    mock.patch.object(
                        campaign_pipeline, "GameResourceLock",
                        return_value=lock), \
                    mock.patch.object(
                        instance_runtime, "prepare_instance_runtime",
                        side_effect=prepare), \
                    mock.patch.object(
                        campaign_pipeline, "run_orchestrator",
                        side_effect=capture), \
                    mock.patch.object(
                        campaign_pipeline, "postprocess_and_report",
                        side_effect=postprocess), \
                    mock.patch.object(
                        campaign_pipeline, "generate_parallel_report",
                        side_effect=lambda *unused: lifecycle.append(
                            "report")):
                result = campaign_pipeline.run_parallel_pipeline(
                    args, args.campaign_id)

            self.assertEqual(0, result)
            self.assertEqual(2, max_active_captures)
            self.assertEqual(2, len(postprocess_order))
            self.assertEqual(1, lock.acquire.call_count)
            self.assertEqual(1, lock.release.call_count)
            self.assertEqual("acquire", lifecycle[0])
            self.assertEqual("release", lifecycle[-1])
            self.assertLess(lifecycle.index("report"),
                            lifecycle.index("release"))
            self.assertEqual(2, lifecycle.count("postprocess"))

    def test_worker_failure_is_nonzero_without_seed_or_runtime_aliasing(self):
        with tempfile.TemporaryDirectory() as root:
            seeds = os.path.join(root, "seeds.txt")
            source_seeds = [f"STS{i:05d}" for i in range(1, 7)]
            with open(seeds, "w", encoding="utf-8") as fh:
                fh.write("\n".join(source_seeds) + "\n")
            args = self._args(seeds)
            captures = {}
            runtime_paths = {}

            def prepare(*positional, **keywords):
                campaign_id = keywords.get("campaign_id", positional[0]
                                           if positional else None)
                runtime = self._fake_runtime(root, campaign_id)
                runtime_paths[campaign_id] = runtime.game_workdir
                return runtime

            def capture(_args, campaign_id, seed_path, runtime,
                        _process_job):
                with open(seed_path, "r", encoding="utf-8") as fh:
                    captures[campaign_id] = [
                        line.strip() for line in fh if line.strip()]
                self.assertEqual(runtime_paths[campaign_id],
                                 runtime.game_workdir)
                return 7 if campaign_id.endswith("001-of-002") else 0

            lock = mock.Mock()
            with mock.patch.object(
                    campaign_pipeline, "CAMPAIGN_ROOT",
                    os.path.join(root, "campaigns")), \
                    mock.patch.object(
                        campaign_pipeline, "GameResourceLock",
                        return_value=lock), \
                    mock.patch.object(
                        instance_runtime, "prepare_instance_runtime",
                        side_effect=prepare), \
                    mock.patch.object(
                        campaign_pipeline, "run_orchestrator",
                        side_effect=capture), \
                    mock.patch.object(
                        campaign_pipeline, "postprocess_and_report",
                        return_value=0) as postprocess, \
                    mock.patch.object(
                        campaign_pipeline, "generate_parallel_report"):
                result = campaign_pipeline.run_parallel_pipeline(
                    args, args.campaign_id)

            self.assertEqual(7, result)
            self.assertEqual(2, len(captures))
            captured_sets = [set(rows) for rows in captures.values()]
            self.assertFalse(captured_sets[0] & captured_sets[1])
            self.assertEqual(set(source_seeds), set.union(*captured_sets))
            self.assertEqual(2, len(set(runtime_paths.values())))
            self.assertEqual(1, postprocess.call_count)
            successful = next(
                campaign_id for campaign_id in captures
                if campaign_id.endswith("002-of-002"))
            postprocess.assert_called_once_with(successful)

    def test_partial_aggregate_keeps_failed_worker_and_successful_report(self):
        with tempfile.TemporaryDirectory() as root, mock.patch.object(
                campaign_pipeline, "CAMPAIGN_ROOT",
                os.path.join(root, "campaigns")):
            group_id = "partial"
            workers = [
                {"index": 0, "campaign_id": "partial.worker-001-of-002",
                 "seed_list": ["STS00001"]},
                {"index": 1, "campaign_id": "partial.worker-002-of-002",
                 "seed_list": ["STS00002"]},
            ]
            group_dir, group_paths = campaign_pipeline._group_paths(group_id)
            os.makedirs(group_dir)
            stale_dir, stale_paths = campaign_pipeline._campaign_paths(
                workers[0]["campaign_id"])
            os.makedirs(stale_dir)
            _write_json(stale_paths["report_json"], {
                "runs": [{"seed": "STS00001"}],
                "captured_actions": 999,
                "replay_clean_actions": 999,
                "strict_zero_diff_actions": 999,
                "untriaged_count": 99,
                "diff_counts": {"stale": 99},
            })
            _worker_dir, worker_paths = campaign_pipeline._campaign_paths(
                workers[1]["campaign_id"])
            os.makedirs(_worker_dir)
            _write_json(worker_paths["report_json"], {
                "runs": [{"seed": "STS00002"}],
                "captured_actions": 4,
                "replay_clean_actions": 4,
                "strict_zero_diff_actions": 4,
                "untriaged_count": 0,
                "diff_counts": {},
            })
            report = campaign_pipeline.generate_parallel_report(
                {
                    "group_campaign_id": group_id,
                    "resolved_instances": 2,
                    "selected_seed_list": ["STS00001", "STS00002"],
                    "workers": workers,
                }, {**group_paths, "group_dir": group_dir},
                {
                    workers[0]["campaign_id"]: 7,
                    workers[1]["campaign_id"]: 0,
                }, {workers[1]["campaign_id"]: 0}, 1.0)
            self.assertEqual(1, report["seeds_reported"])
            self.assertEqual(4, report["captured_actions"])
            self.assertEqual(7, report["workers"][0]["capture_exit"])
            self.assertIsNone(report["workers"][0]["report"])
            self.assertEqual(0, report["workers"][1]["postprocess_exit"])
            self.assertIsNotNone(report["workers"][1]["report"])

    def test_postprocess_exception_does_not_skip_later_worker(self):
        with tempfile.TemporaryDirectory() as root:
            seeds = os.path.join(root, "seeds.txt")
            with open(seeds, "w", encoding="utf-8") as fh:
                fh.write("STS00001\nSTS00002\n")
            args = self._args(seeds)
            calls = []

            def prepare(*positional, **keywords):
                campaign_id = keywords.get("campaign_id", positional[0]
                                           if positional else None)
                return self._fake_runtime(root, campaign_id)

            def postprocess(campaign_id):
                calls.append(campaign_id)
                if campaign_id.endswith("001-of-002"):
                    raise RuntimeError("synthetic postprocess failure")
                return 0

            lock = mock.Mock()
            with mock.patch.object(
                    campaign_pipeline, "CAMPAIGN_ROOT",
                    os.path.join(root, "campaigns")), mock.patch.object(
                        campaign_pipeline, "GameResourceLock",
                        return_value=lock), mock.patch.object(
                            instance_runtime, "prepare_instance_runtime",
                            side_effect=prepare), mock.patch.object(
                                campaign_pipeline, "run_orchestrator",
                                return_value=0), mock.patch.object(
                                    campaign_pipeline,
                                    "postprocess_and_report",
                                    side_effect=postprocess), mock.patch.object(
                                        campaign_pipeline,
                                        "generate_parallel_report"):
                result = campaign_pipeline.run_parallel_pipeline(
                    args, args.campaign_id)
            self.assertEqual(2, result)
            self.assertEqual(2, len(calls))
            self.assertTrue(calls[1].endswith("002-of-002"))

    def test_capture_failure_return_is_deterministic_worker_order(self):
        with tempfile.TemporaryDirectory() as root:
            seeds = os.path.join(root, "seeds.txt")
            with open(seeds, "w", encoding="utf-8") as fh:
                fh.write("STS00001\nSTS00002\n")
            args = self._args(seeds)

            def prepare(*positional, **keywords):
                campaign_id = keywords.get("campaign_id", positional[0]
                                           if positional else None)
                return self._fake_runtime(root, campaign_id)

            def capture(_args, campaign_id, _seed_path, _runtime, _job):
                if campaign_id.endswith("001-of-002"):
                    time.sleep(0.03)
                    return 7
                return 8

            with mock.patch.object(
                    campaign_pipeline, "CAMPAIGN_ROOT",
                    os.path.join(root, "campaigns")), mock.patch.object(
                        campaign_pipeline, "GameResourceLock",
                        return_value=mock.Mock()), mock.patch.object(
                            instance_runtime, "prepare_instance_runtime",
                            side_effect=prepare), mock.patch.object(
                                campaign_pipeline, "run_orchestrator",
                                side_effect=capture), mock.patch.object(
                                    campaign_pipeline,
                                    "generate_parallel_report"):
                result = campaign_pipeline.run_parallel_pipeline(
                    args, args.campaign_id)
            self.assertEqual(7, result)

    def test_unexpected_capture_setup_exception_closes_scoped_job(self):
        worker = {
            "index": 0,
            "campaign_id": "unexpected.worker-001-of-001",
            "seed_list": ["STS00001"],
            "paths": {"seeds": "unused"},
        }
        group = {"resolved_instances": 1, "workers": [worker]}
        job = mock.Mock()
        args = SimpleNamespace(
            game_dir="game", fork_jar="fork", fresh=False)
        with mock.patch.object(
                campaign_pipeline, "OrchestratorProcessJob",
                return_value=job), mock.patch.object(
                    instance_runtime, "prepare_instance_runtime",
                    side_effect=RuntimeError("synthetic unexpected setup")):
            with self.assertRaisesRegex(RuntimeError, "unexpected setup"):
                campaign_pipeline._run_parallel_pipeline_locked(
                    args, group, {"workers": [worker]})
        job.close.assert_called_once_with()


class ProcessBoundaryTest(unittest.TestCase):
    def test_script_entrypoint_contains_postprocess_but_not_report_only(self):
        with mock.patch.object(
                campaign_pipeline, "_install_pipeline_lifetime_job") as install, \
                mock.patch.object(
                    campaign_pipeline, "main", return_value=0) as main:
            self.assertEqual(
                0, campaign_pipeline.script_entrypoint([
                    "postprocess", "--campaign-id", "campaign"]))
            install.assert_called_once_with()
            main.assert_called_once_with(
                ["postprocess", "--campaign-id", "campaign"])

        with mock.patch.object(
                campaign_pipeline, "_install_pipeline_lifetime_job") as install, \
                mock.patch.object(campaign_pipeline, "main", return_value=0):
            self.assertEqual(
                0, campaign_pipeline.script_entrypoint([
                    "report", "--campaign-id", "campaign"]))
            install.assert_not_called()

    def test_busy_parallel_contender_cannot_rewrite_prepared_seed_file(self):
        with tempfile.TemporaryDirectory() as root:
            source = os.path.join(root, "seeds.txt")
            with open(source, "w", encoding="utf-8") as fh:
                fh.write("STS00001\nSTS00002\n")
            campaign_root = os.path.join(root, "campaigns")
            with mock.patch.object(
                    campaign_pipeline, "CAMPAIGN_ROOT", campaign_root):
                _group, paths = campaign_pipeline.prepare_parallel_group(
                    "locked", source, 1, 0, "random-legal", 7, 2)
            seed_path = paths["workers"][0]["paths"]["seeds"]
            with open(seed_path, "rb") as fh:
                before = fh.read()
            args = campaign_pipeline.parse_args([
                "run", "--campaign-id", "locked", "--seeds", source,
                "--instances", "2"])
            busy_lock = mock.Mock()
            busy_lock.acquire.side_effect = campaign_pipeline.GameResourceBusy(
                "synthetic busy owner")
            with mock.patch.object(
                    campaign_pipeline, "CAMPAIGN_ROOT", campaign_root), \
                    mock.patch.object(
                        campaign_pipeline, "GameResourceLock",
                        return_value=busy_lock):
                result = campaign_pipeline.run_parallel_pipeline(
                    args, args.campaign_id)
            self.assertEqual(campaign_pipeline.EXIT_GAME_BUSY, result)
            with open(seed_path, "rb") as fh:
                self.assertEqual(before, fh.read())
            busy_lock.release.assert_not_called()

    def test_busy_legacy_contender_cannot_rewrite_seed_file(self):
        with tempfile.TemporaryDirectory() as root:
            source = os.path.join(root, "seeds.txt")
            with open(source, "w", encoding="utf-8") as fh:
                fh.write("STS00001\n")
            campaign_root = os.path.join(root, "campaigns")
            with mock.patch.object(
                    campaign_pipeline, "CAMPAIGN_ROOT", campaign_root):
                _campaign_id, _campaign_dir, paths, _selected = \
                    campaign_pipeline.prepare_campaign(
                        "legacy", source, 1, 0, "random-legal", 7)
            with open(paths["seeds"], "rb") as fh:
                before = fh.read()
            args = campaign_pipeline.parse_args([
                "run", "--campaign-id", "legacy", "--seeds", source,
                "--instances", "1"])
            busy_lock = mock.Mock()
            busy_lock.acquire.side_effect = campaign_pipeline.GameResourceBusy(
                "synthetic busy owner")
            with mock.patch.object(
                    campaign_pipeline, "CAMPAIGN_ROOT", campaign_root), \
                    mock.patch.object(
                        campaign_pipeline, "GameResourceLock",
                        return_value=busy_lock):
                result = campaign_pipeline._run_legacy_pipeline(
                    args, args.campaign_id)
            self.assertEqual(campaign_pipeline.EXIT_GAME_BUSY, result)
            with open(paths["seeds"], "rb") as fh:
                self.assertEqual(before, fh.read())
            busy_lock.release.assert_not_called()

    def test_standalone_artifact_write_refuses_busy_coordinator(self):
        busy_lock = mock.Mock()
        busy_lock.acquire.side_effect = campaign_pipeline.GameResourceBusy(
            "synthetic busy owner")
        operation = mock.Mock(return_value=0)
        with mock.patch.object(
                campaign_pipeline, "GameResourceLock",
                return_value=busy_lock):
            result = campaign_pipeline.run_locked_artifact_operation(
                "campaign", operation)
        self.assertEqual(campaign_pipeline.EXIT_GAME_BUSY, result)
        operation.assert_not_called()
        busy_lock.release.assert_not_called()

    def test_orchestrator_is_assigned_before_pipeline_waits(self):
        args = campaign_pipeline.parse_args([
            "run", "--campaign-id", "managed", "--seeds", "STS00001"])
        events = []
        proc = mock.Mock()
        proc.wait.side_effect = lambda *args, **kwargs: events.append("wait") or 7
        job = mock.Mock()
        job.assign.side_effect = lambda child: events.append("assign")
        with mock.patch.object(
                campaign_pipeline.subprocess, "Popen", return_value=proc):
            result = campaign_pipeline.run_orchestrator(
                args, "managed", "seeds.txt", process_job=job)
        self.assertEqual(7, result)
        self.assertEqual(["assign", "wait"], events)
        job.assign.assert_called_once_with(proc)

    def test_assignment_failure_kills_orchestrator_tree(self):
        args = campaign_pipeline.parse_args([
            "run", "--campaign-id", "managed", "--seeds", "STS00001"])
        proc = mock.Mock()
        job = mock.Mock()
        job.assign.side_effect = OSError("synthetic assignment failure")
        with mock.patch.object(
                campaign_pipeline.subprocess, "Popen", return_value=proc), \
                mock.patch.object(
                    campaign_pipeline, "_terminate_process_tree") as kill:
            with self.assertRaisesRegex(OSError, "assignment failure"):
                campaign_pipeline.run_orchestrator(
                    args, "managed", "seeds.txt", process_job=job)
        kill.assert_called_once_with(proc)
        proc.wait.assert_not_called()

    @unittest.skipUnless(os.name == "nt", "Windows Job Object coverage")
    def test_real_job_close_terminates_assigned_subprocess(self):
        proc = subprocess.Popen(
            [sys.executable, "-c", "import time; time.sleep(60)"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        job = campaign_pipeline.OrchestratorProcessJob(required=True)
        try:
            job.assign(proc)
            job.close()
            self.assertIsNotNone(proc.wait(timeout=10))
        finally:
            job.close()
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=10)

    @unittest.skipUnless(os.name == "nt", "Windows Job inheritance coverage")
    def test_pipeline_lifetime_job_kills_inherited_child_on_owner_exit(self):
        driver_dir = os.path.dirname(campaign_pipeline.__file__)
        child_code = (
            "import os,subprocess,sys;"
            "sys.path.insert(0,sys.argv[1]);"
            "import campaign_pipeline as cp;"
            "cp._install_pipeline_lifetime_job();"
            "p=subprocess.Popen([sys.executable,'-c',"
            "'import time;time.sleep(60)']);"
            "print(p.pid,flush=True);"
            "os._exit(0)"
        )
        owner = subprocess.Popen(
            [sys.executable, "-c", child_code, driver_dir],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        child_pid = int(owner.stdout.readline().strip())
        _stdout, stderr = owner.communicate(timeout=10)
        self.assertEqual(0, owner.returncode, stderr)

        import ctypes
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.OpenProcess.argtypes = [
            ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        kernel32.OpenProcess.restype = ctypes.c_void_p
        kernel32.WaitForSingleObject.argtypes = [
            ctypes.c_void_p, ctypes.c_ulong]
        kernel32.WaitForSingleObject.restype = ctypes.c_ulong
        kernel32.TerminateProcess.argtypes = [ctypes.c_void_p, ctypes.c_uint]
        kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel32.OpenProcess(0x00100001, False, child_pid)
        if not handle:
            return
        try:
            wait_result = kernel32.WaitForSingleObject(handle, 10000)
            if wait_result == 0x00000102:
                kernel32.TerminateProcess(handle, 1)
            self.assertEqual(0x00000000, wait_result)
        finally:
            kernel32.CloseHandle(handle)

    @unittest.skipUnless(os.name == "nt", "Windows inner Job Object coverage")
    def test_real_inner_job_close_terminates_subprocess(self):
        proc = subprocess.Popen(
            [sys.executable, "-c", "import time; time.sleep(60)"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        job = orchestrator._WindowsKillJob(proc)
        try:
            job.close()
            self.assertIsNotNone(proc.wait(timeout=10))
        finally:
            job.close()
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=10)

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
                "--seeds", seeds, "--task-name", "oracle-nightly",
                "--instances", "3", "--seeds-per-launch", "7"])
            self.assertEqual(campaign_pipeline.schedule_nightly(args), 0)
            config = campaign_pipeline._read_json(os.path.join(
                root, "oracle-nightly.json"))
            self.assertEqual(config["arguments"][0], "nightly")
            self.assertEqual(
                "3", config["arguments"][
                    config["arguments"].index("--instances") + 1])
            self.assertEqual(
                "7", config["arguments"][
                    config["arguments"].index("--seeds-per-launch") + 1])
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
