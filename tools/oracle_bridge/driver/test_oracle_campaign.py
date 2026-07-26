#!/usr/bin/env python3
"""Synthetic regressions for oracle-campaign environment preflighting."""

from __future__ import annotations

import json
import os
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

import campaign_driver
import orchestrator
import validate_artifacts


SEED = "STS00041"


class _StartStepper:
    def __init__(self, *states):
        self.states = list(states)
        self.commands = []

    def step(self, command):
        self.commands.append(command)
        return "ready", self.states.pop(0)


class _HandshakeStepper:
    def send(self, _command):
        pass

    def await_ready(self):
        return "ready", {
            "ready_for_command": True,
            "in_game": False,
            "available_commands": ["start"],
        }


class _Reader:
    def start(self):
        pass


def _header(oracle_enabled):
    return {
        "record_kind": "header",
        "schema_version": 1,
        "driver_version": "test",
        "created_utc": "2026-07-26T00:00:00Z",
        "game": {},
        "mods": ["basemod", "CommunicationMod-oracle"],
        "fork_jar_sha256": "A" * 64,
        "oracle_block_enabled": oracle_enabled,
        "seed": {"string": SEED, "long": 1},
        "ascension": 20,
        "character": "IRONCLAD",
        "policy": "random-legal",
    }


def _oracle():
    triples = {
        name: {"counter": 0, "s0": "1", "s1": "2"}
        for name in validate_artifacts.RUN_STREAMS
    }
    return {
        "seed": 1,
        "floor": 1,
        "act": 1,
        "ascension": 20,
        "streams": triples,
        "cardBlizzRandomizer": 0.0,
        "blizzardPotionMod": 0,
        "eventPity": {},
        "purgeCost": 75,
        "eventList": [],
        "shrineList": [],
        "specialOneTimeEventList": [],
        "relicPools": {},
    }


def _action(oracle=None):
    game_state = {
        "seed": 1,
        "floor": 1,
        "act": 1,
        "screen_type": "COMBAT_REWARD",
        "class": "IRONCLAD",
        "current_hp": 80,
        "max_hp": 80,
    }
    if oracle is not None:
        game_state["oracle"] = oracle
    return {
        "record_kind": "action",
        "action_command": "choose 0",
        "sim_action_bits": None,
        "state_json": {
            "available_commands": ["choose"],
            "ready_for_command": True,
            "in_game": True,
            "game_state": game_state,
        },
    }


def _terminal():
    return {"record_kind": "terminal", "outcome": "test"}


def _write_artifact(path, records):
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        for record in records:
            fh.write(json.dumps(record) + "\n")


class CampaignDriverPreflightTest(unittest.TestCase):
    def test_first_dungeon_dump_without_oracle_creates_no_artifact(self):
        with tempfile.TemporaryDirectory() as root:
            driver = campaign_driver.CampaignDriver.__new__(
                campaign_driver.CampaignDriver)
            driver.args = SimpleNamespace(
                data_root=root,
                campaign_id="preflight",
                policy_seed=1234,
                menu_timeout=1.0,
            )
            driver.stepper = _StartStepper({
                "ready_for_command": True,
                "in_game": True,
                "available_commands": ["choose"],
                "game_state": {"seed": campaign_driver.seed_to_long(SEED)},
            })

            with self.assertRaisesRegex(
                    campaign_driver.FatalEnvironmentDrift,
                    "no game_state.oracle object"):
                driver.run_seed(SEED, 1)

            run_dir = os.path.join(root, "preflight")
            self.assertFalse(os.path.exists(os.path.join(
                run_dir, f"run_{SEED}_a20_ironclad.jsonl")))
            self.assertFalse(os.path.exists(os.path.join(
                run_dir, f"run_{SEED}_a20_ironclad.timing.jsonl")))

    def test_delayed_dungeon_dump_still_fails_before_artifact_or_policy(self):
        with tempfile.TemporaryDirectory() as root:
            driver = campaign_driver.CampaignDriver.__new__(
                campaign_driver.CampaignDriver)
            driver.args = SimpleNamespace(
                data_root=root,
                campaign_id="preflight",
                policy_seed=1234,
                menu_timeout=1.0,
            )
            menu_state = {
                "ready_for_command": True,
                "in_game": False,
                "available_commands": ["start"],
            }
            dungeon_without_oracle = {
                "ready_for_command": True,
                "in_game": True,
                "available_commands": ["choose"],
                "game_state": {"seed": campaign_driver.seed_to_long(SEED)},
            }
            stepper = _StartStepper(menu_state, dungeon_without_oracle)
            driver.stepper = stepper
            driver._policy_command = mock.Mock(
                side_effect=AssertionError("policy must not run"))

            with self.assertRaises(campaign_driver.FatalEnvironmentDrift):
                driver.run_seed(SEED, 1)

            self.assertEqual(
                [f"start ironclad 20 {SEED}", "state"], stepper.commands)
            driver._policy_command.assert_not_called()
            run_dir = os.path.join(root, "preflight")
            self.assertFalse(os.path.exists(os.path.join(
                run_dir, f"run_{SEED}_a20_ironclad.jsonl")))
            self.assertFalse(os.path.exists(os.path.join(
                run_dir, f"run_{SEED}_a20_ironclad.timing.jsonl")))

    def test_fatal_drift_is_persisted_and_ends_driver(self):
        with tempfile.TemporaryDirectory() as root:
            driver = campaign_driver.CampaignDriver.__new__(
                campaign_driver.CampaignDriver)
            driver.args = SimpleNamespace(
                seeds=[SEED],
                campaign_id="preflight",
                policy="random-legal",
            )
            driver.reader = _Reader()
            driver.stepper = _HandshakeStepper()
            driver.fork_hash = "A" * 64
            run_dir = os.path.join(root, "preflight")
            os.makedirs(run_dir)
            progress_path = os.path.join(run_dir, "campaign_progress.json")
            driver.progress = campaign_driver.Progress(
                progress_path,
                os.path.join(run_dir, "campaign_heartbeat.json"))
            driver.wait_menu = lambda: None
            driver.run_seed = lambda _seed, _attempt: (
                (_ for _ in ()).throw(campaign_driver.FatalEnvironmentDrift(
                    "synthetic missing oracle")))

            self.assertEqual(campaign_driver.EXIT_FATAL, driver.run())
            with open(progress_path, "r", encoding="utf-8") as fh:
                progress = json.load(fh)
            self.assertEqual(
                "fatal_environment_drift", progress["status"])
            self.assertEqual("missing_oracle_block",
                             progress["fatal"]["kind"])
            self.assertEqual(SEED, progress["fatal"]["seed"])
            self.assertEqual([], progress["seeds_done"])
            self.assertEqual([], progress["seeds_failed"])


class OrchestratorPreflightTest(unittest.TestCase):
    def test_existing_fatal_status_stops_before_relaunch(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = os.path.join(root, "preflight")
            os.makedirs(campaign_dir)
            with open(os.path.join(
                    campaign_dir, "campaign_progress.json"),
                    "w", encoding="utf-8") as fh:
                json.dump({
                    "status": "fatal_environment_drift",
                    "fatal": {"message": "synthetic missing oracle"},
                    "seeds_done": [],
                    "seeds_failed": [],
                }, fh)
            local_app_data = os.path.join(root, "local")
            with mock.patch.dict(
                    os.environ, {"LOCALAPPDATA": local_app_data}), \
                    mock.patch.object(
                        orchestrator, "launch_game") as launch:
                result = orchestrator.main([
                    "--data-root", root,
                    "--campaign-id", "preflight",
                    "--seeds", SEED,
                ])

            self.assertEqual(orchestrator.EXIT_FATAL_ENVIRONMENT, result)
            launch.assert_not_called()
            with open(os.path.join(
                    campaign_dir, "orchestrator_timeline.json"),
                    "r", encoding="utf-8") as fh:
                timeline = json.load(fh)
            self.assertEqual("fatal_environment_drift",
                             timeline["final_status"])

    def test_fatal_status_during_launch_kills_once_and_does_not_relaunch(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = os.path.join(root, "preflight")
            os.makedirs(campaign_dir)
            progress_path = os.path.join(
                campaign_dir, "campaign_progress.json")
            with open(progress_path, "w", encoding="utf-8") as fh:
                json.dump({
                    "status": "in_progress",
                    "seeds_done": [],
                    "seeds_failed": [],
                }, fh)

            def launch_and_report_fatal(_args, _launch_idx):
                with open(progress_path, "w", encoding="utf-8") as fh:
                    json.dump({
                        "status": "fatal_environment_drift",
                        "fatal": {"message": "active missing oracle"},
                        "seeds_done": [],
                        "seeds_failed": [],
                    }, fh)
                return object()

            local_app_data = os.path.join(root, "local")
            with mock.patch.dict(
                    os.environ, {"LOCALAPPDATA": local_app_data}), \
                    mock.patch.object(
                        orchestrator, "launch_game",
                        side_effect=launch_and_report_fatal) as launch, \
                    mock.patch.object(orchestrator, "kill_tree") as kill, \
                    mock.patch.object(orchestrator.time, "sleep"):
                result = orchestrator.main([
                    "--data-root", root,
                    "--campaign-id", "preflight",
                    "--seeds", SEED,
                ])

            self.assertEqual(orchestrator.EXIT_FATAL_ENVIRONMENT, result)
            self.assertEqual(1, launch.call_count)
            self.assertEqual(1, kill.call_count)


class ArtifactOracleRequirementTest(unittest.TestCase):
    def _validate(self, records, require_oracle=False):
        with tempfile.TemporaryDirectory() as root:
            path = os.path.join(root, "run_test_a20_ironclad.jsonl")
            _write_artifact(path, records)
            return validate_artifacts.validate_file(
                path, require_oracle=require_oracle)[0]

    def test_default_keeps_non_oracle_artifacts_backward_compatible(self):
        errors = self._validate([
            _header(False), _action(), _terminal()])
        self.assertEqual([], errors)

    def test_require_oracle_rejects_false_header_and_missing_block(self):
        errors = self._validate(
            [_header(False), _action(), _terminal()],
            require_oracle=True)
        joined = "\n".join(errors)
        self.assertIn("oracle_block_enabled: true", joined)
        self.assertIn("no oracle block", joined)

    def test_oracle_enabled_header_without_block_still_fails_by_default(self):
        errors = self._validate([
            _header(True), _action(), _terminal()])
        self.assertIn("oracle_block_enabled but no oracle block",
                      "\n".join(errors))

    def test_require_oracle_accepts_reward_fields_and_five_stream_triples(self):
        errors = self._validate(
            [_header(True), _action(_oracle()), _terminal()],
            require_oracle=True)
        self.assertEqual([], errors)

    def test_require_oracle_rejects_missing_pity_and_malformed_reward_stream(self):
        oracle = _oracle()
        del oracle["cardBlizzRandomizer"]
        del oracle["streams"]["miscRng"]["counter"]
        errors = self._validate(
            [_header(True), _action(oracle), _terminal()],
            require_oracle=True)
        joined = "\n".join(errors)
        self.assertIn("cardBlizzRandomizer", joined)
        self.assertIn("stream miscRng lacks counter/s0/s1", joined)


if __name__ == "__main__":
    unittest.main()
