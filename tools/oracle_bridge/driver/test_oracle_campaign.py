#!/usr/bin/env python3
"""Synthetic regressions for oracle-campaign environment preflighting."""

from __future__ import annotations

import io
import json
import os
import tempfile
import unittest
from contextlib import redirect_stdout
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


def _menu_action():
    return {
        "record_kind": "action",
        "action_command": "state",
        "sim_action_bits": None,
        "state_json": {
            "available_commands": ["start"],
            "ready_for_command": True,
            "in_game": False,
        },
    }


def _terminal(outcome="test", floor=1, actions=1):
    return {
        "record_kind": "terminal",
        "outcome": outcome,
        "floor": floor,
        "actions": actions,
    }


def _write_artifact(path, records):
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        for record in records:
            fh.write(json.dumps(record) + "\n")


def _campaign_header(campaign_id, seed=SEED, attempt=1):
    header = _header(True)
    header["campaign_id"] = campaign_id
    header["attempt"] = attempt
    header["seed"]["string"] = seed
    return header


def _done(seed=SEED, attempt=1):
    return {
        "seed": seed,
        "outcome": "test",
        "floor": 1,
        "actions": 1,
        "attempts": attempt,
        "artifact": f"run_{seed}_a20_ironclad.jsonl",
    }


def _progress(campaign_id, status="complete", done=None, failed=None):
    return {
        "campaign_id": campaign_id,
        "schema_version": 1,
        "driver_version": "test",
        "fork_jar_sha256": "A" * 64,
        "policy": "random-legal",
        "seed_list": [SEED],
        "status": status,
        "seeds_done": [_done()] if done is None else done,
        "seeds_failed": [] if failed is None else failed,
        "current_seed": None,
        "current_seed_attempt": 0,
    }


def _write_campaign_json(path, value):
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(value, fh)


def _write_timing(path, campaign_id, seed=SEED, attempt=1):
    _write_artifact(path, [{
        "record_kind": "timing_header",
        "campaign_id": campaign_id,
        "policy": "random-legal",
        "seed": seed,
        "attempt": attempt,
    }])


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
                progress = _progress(
                    "preflight", status="fatal_environment_drift",
                    done=[], failed=[])
                progress.update({
                    "status": "fatal_environment_drift",
                    "fatal": {"message": "synthetic missing oracle"},
                })
                json.dump(progress, fh)
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
                json.dump(_progress(
                    "preflight", status="in_progress",
                    done=[], failed=[]), fh)

            def launch_and_report_fatal(_args, _launch_idx):
                with open(progress_path, "w", encoding="utf-8") as fh:
                    json.dump({
                        **_progress(
                            "preflight", status="fatal_environment_drift",
                            done=[], failed=[]),
                        "status": "fatal_environment_drift",
                        "fatal": {"message": "active missing oracle"},
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

    def test_complete_with_failures_is_nonzero_and_never_relaunched(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = os.path.join(root, "preflight")
            os.makedirs(campaign_dir)
            failed = [{
                "seed": SEED, "reason": "crash",
                "attempts": 2, "at_floor": None,
            }]
            _write_campaign_json(
                os.path.join(campaign_dir, "campaign_progress.json"),
                _progress(
                    "preflight", status="complete_with_failures",
                    done=[], failed=failed))
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

            self.assertEqual(orchestrator.EXIT_CAMPAIGN_INVALID, result)
            launch.assert_not_called()


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

    def test_require_oracle_rejects_header_and_terminal_only(self):
        errors = self._validate(
            [_header(True), _terminal(actions=0)],
            require_oracle=True)
        self.assertIn("at least one in-game action with a valid oracle block",
                      "\n".join(errors))

    def test_require_oracle_rejects_all_menu_actions(self):
        errors = self._validate(
            [_header(True), _menu_action(), _terminal()],
            require_oracle=True)
        self.assertIn("at least one in-game action with a valid oracle block",
                      "\n".join(errors))


class StrictCampaignValidationTest(unittest.TestCase):
    def _make_campaign(self, root, campaign_id="strict"):
        campaign_dir = os.path.join(root, campaign_id)
        os.makedirs(campaign_dir)
        progress = _progress(campaign_id)
        manifest = {
            key: progress[key]
            for key in validate_artifacts.STRICT_MANIFEST_KEYS
        }
        _write_campaign_json(
            os.path.join(campaign_dir, "campaign_progress.json"), progress)
        _write_campaign_json(
            os.path.join(campaign_dir, "campaign_manifest.json"), manifest)
        _write_artifact(
            os.path.join(
                campaign_dir, f"run_{SEED}_a20_ironclad.jsonl"),
            [_campaign_header(campaign_id), _action(_oracle()), _terminal()])
        _write_timing(
            os.path.join(
                campaign_dir, f"run_{SEED}_a20_ironclad.timing.jsonl"),
            campaign_id)
        return campaign_dir

    def test_strict_campaign_binds_complete_ledger_and_artifact_families(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = self._make_campaign(root)
            files, errors = validate_artifacts.validate_campaign(
                campaign_dir, require_oracle=True)
            self.assertEqual([], errors)
            self.assertEqual(1, len(files))
            file_errors, _actions = validate_artifacts.validate_file(
                files[0], require_oracle=True)
            self.assertEqual([], file_errors)
            with redirect_stdout(io.StringIO()):
                self.assertEqual(0, validate_artifacts.main([
                    "--require-oracle", "--campaign", campaign_dir]))

    def test_strict_campaign_rejects_failed_seed_with_stale_artifacts(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = self._make_campaign(root)
            failed = [{
                "seed": SEED, "reason": "crash",
                "attempts": 2, "at_floor": None,
            }]
            progress = _progress(
                "strict", status="complete_with_failures",
                done=[], failed=failed)
            manifest = {
                key: progress[key]
                for key in validate_artifacts.STRICT_MANIFEST_KEYS
            }
            _write_campaign_json(
                os.path.join(campaign_dir, "campaign_progress.json"), progress)
            _write_campaign_json(
                os.path.join(campaign_dir, "campaign_manifest.json"), manifest)

            _files, errors = validate_artifacts.validate_campaign(
                campaign_dir, require_oracle=True)
            joined = "\n".join(errors)
            self.assertIn("status must be 'complete'", joined)
            self.assertIn("contains failed seeds", joined)
            self.assertIn("unexpected/stale run_STS00041", joined)

    def test_strict_campaign_rejects_missing_extra_and_identity_drift(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = self._make_campaign(root)
            os.remove(os.path.join(
                campaign_dir, f"run_{SEED}_a20_ironclad.timing.jsonl"))
            extra = "STS00999"
            _write_artifact(
                os.path.join(
                    campaign_dir, f"run_{extra}_a20_ironclad.jsonl"),
                [_campaign_header("strict", extra),
                 _action(_oracle()), _terminal()])
            path = os.path.join(
                campaign_dir, f"run_{SEED}_a20_ironclad.jsonl")
            records = [
                _campaign_header("wrong-campaign"),
                _action(_oracle()), _terminal(),
            ]
            _write_artifact(path, records)

            _files, errors = validate_artifacts.validate_campaign(
                campaign_dir, require_oracle=True)
            joined = "\n".join(errors)
            self.assertIn("timing artifacts: missing", joined)
            self.assertIn("unexpected/stale run_STS00999", joined)
            self.assertIn("header campaign_id", joined)


class CampaignIdentityAndFreshTest(unittest.TestCase):
    def test_fresh_cleanup_is_bounded_to_owned_expected_files(self):
        with tempfile.TemporaryDirectory() as root:
            expected = [
                "campaign_progress.json",
                f"run_{SEED}_a20_ironclad.jsonl",
                f"run_{SEED}_a20_ironclad.timing.jsonl",
                "mts_launch12.log",
            ]
            preserved = [
                "operator_notes.txt",
                "run_STS00999_a20_ironclad.jsonl",
                "unrelated.log",
            ]
            for name in expected + preserved:
                with open(os.path.join(root, name), "w", encoding="utf-8"):
                    pass

            removed = orchestrator.clear_fresh_campaign_files(root, [SEED])

            self.assertEqual(set(expected), set(removed))
            for name in expected:
                self.assertFalse(os.path.exists(os.path.join(root, name)))
            for name in preserved:
                self.assertTrue(os.path.exists(os.path.join(root, name)))

    def test_progress_refuses_resume_under_different_seed_identity(self):
        with tempfile.TemporaryDirectory() as root:
            path = os.path.join(root, "campaign_progress.json")
            heartbeat = os.path.join(root, "campaign_heartbeat.json")
            _write_campaign_json(path, _progress("strict"))
            progress = campaign_driver.Progress(path, heartbeat)
            with self.assertRaisesRegex(
                    campaign_driver.CampaignIdentityError, "seed_list"):
                progress.load_or_init(
                    "strict", ["STS00999"], "random-legal", "A" * 64)

    def test_retry_exhaustion_cannot_accept_stale_artifact(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_id = "retry"
            campaign_dir = os.path.join(root, campaign_id)
            os.makedirs(campaign_dir)
            _write_artifact(
                os.path.join(
                    campaign_dir, f"run_{SEED}_a20_ironclad.jsonl"),
                [_campaign_header(campaign_id),
                 _action(_oracle()), _terminal()])
            _write_timing(
                os.path.join(
                    campaign_dir, f"run_{SEED}_a20_ironclad.timing.jsonl"),
                campaign_id)

            args = SimpleNamespace(
                seeds=[SEED],
                campaign_id=campaign_id,
                policy="random-legal",
                data_root=root,
                max_attempts=1,
            )

            def make_driver():
                driver = campaign_driver.CampaignDriver.__new__(
                    campaign_driver.CampaignDriver)
                driver.args = args
                driver.reader = _Reader()
                driver.stepper = _HandshakeStepper()
                driver.fork_hash = "A" * 64
                driver.progress = campaign_driver.Progress(
                    os.path.join(campaign_dir, "campaign_progress.json"),
                    os.path.join(campaign_dir, "campaign_heartbeat.json"))
                driver.wait_menu = lambda: None
                return driver

            first = make_driver()
            first.run_seed = lambda _seed, _attempt: (
                (_ for _ in ()).throw(campaign_driver.GameGone(
                    "synthetic retry exhaustion")))
            self.assertEqual(campaign_driver.EXIT_GAME_GONE, first.run())

            second = make_driver()
            second.run_seed = mock.Mock(
                side_effect=AssertionError("failed seed must not rerun"))
            self.assertEqual(campaign_driver.EXIT_FATAL, second.run())
            second.run_seed.assert_not_called()

            _files, errors = validate_artifacts.validate_campaign(
                campaign_dir, require_oracle=True)
            joined = "\n".join(errors)
            self.assertIn("complete_with_failures", joined)
            self.assertIn("contains failed seeds", joined)
            self.assertIn("unexpected/stale run_STS00041", joined)
            with redirect_stdout(io.StringIO()):
                self.assertNotEqual(0, validate_artifacts.main([
                    "--require-oracle", "--campaign", campaign_dir]))


if __name__ == "__main__":
    unittest.main()
