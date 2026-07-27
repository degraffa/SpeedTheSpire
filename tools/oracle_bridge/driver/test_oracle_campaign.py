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
import campaign_paths
import orchestrator
import validate_artifacts


SEED = "STS00041"
SEED_LONG = campaign_driver.seed_to_long(SEED)


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
        "seed": {
            "string": SEED,
            "long": SEED_LONG,
            "long_getLong": SEED_LONG,
            "crosscheck_ok": True,
        },
        "ascension": 20,
        "character": "IRONCLAD",
        "policy": "random-legal",
        "campaign_id": "direct",
        "attempt": 1,
    }


def _oracle():
    triples = {
        name: {"counter": 0, "s0": "1", "s1": "2"}
        for name in validate_artifacts.RUN_STREAMS
    }
    return {
        "seed": SEED_LONG,
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


def _action(oracle=None, seq=0, command="choose 0"):
    game_state = {
        "seed": SEED_LONG,
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
        "seq": seq,
        "action_command": command,
        "sim_action_bits": None,
        "state_json": {
            "available_commands": ["choose"],
            "ready_for_command": True,
            "in_game": True,
            "game_state": game_state,
        },
    }


def _menu_action(seq=0):
    return {
        "record_kind": "action",
        "seq": seq,
        "action_command": "state",
        "sim_action_bits": None,
        "state_json": {
            "available_commands": ["start"],
            "ready_for_command": True,
            "in_game": False,
        },
    }


def _terminal(outcome="test", floor=1, actions=1, seq=None):
    return {
        "record_kind": "terminal",
        "seq": actions if seq is None else seq,
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
    seed_long = campaign_driver.seed_to_long(seed)
    header["seed"].update({
        "long": seed_long,
        "long_getLong": seed_long,
        "crosscheck_ok": True,
    })
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
        "schema_version": 1,
        "driver_version": "test",
        "fork_jar_sha256": "A" * 64,
    }, {
        "record_kind": "mark",
        "seq": 0,
        "t_mono": 1.0,
        "t_wall": 2.0,
        "cmd": "choose 0",
        "floor": 1,
        "screen": "COMBAT_REWARD",
    }])


# --- runtime-stack observation (B4.5; design 11 v0.1.7) --------------------
#
# The defect these cover: the artifact header used to carry static constants, so
# every artifact claimed the sanctioned stack no matter what launched. A launch
# log is now the only thing that can put versions in a header.

def _launch_log(sts="12-18-2022", mts="3.30.3", mods=None, java="1.8.0_144"):
    """A ModTheSpire launch log, verbatim in the shape MTS actually emits."""
    if mods is None:
        mods = [("basemod", "5.56.0"),
                ("CommunicationMod-oracle", "1.2.1-oracle.0")]
    lines = ["Running with debug mode turned ON...", "",
             "Version Info:", f" - Java version ({java})"]
    if sts is not None:
        lines.append(f" - Slay the Spire ({sts})")
    if mts is not None:
        lines.append(f" - ModTheSpire ({mts})")
    lines.append("Mod list:")
    lines.extend(f" - {name} ({version})" for name, version in mods)
    lines += ["", "Begin patching...", "Patching enums...Done."]
    return "\n".join(lines) + "\n"


def _write_launch_log(campaign_dir, index=1, **kwargs):
    os.makedirs(campaign_dir, exist_ok=True)
    path = os.path.join(campaign_dir, f"mts_launch{index}.log")
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(_launch_log(**kwargs))
    return path


def _stack_driver(root, campaign_id, state=None, max_actions=0):
    """A CampaignDriver wired just far enough to reach the artifact header."""
    driver = campaign_driver.CampaignDriver.__new__(
        campaign_driver.CampaignDriver)
    driver.args = SimpleNamespace(
        data_root=root,
        campaign_id=campaign_id,
        policy_seed=1234,
        menu_timeout=1.0,
        policy="random-legal",
        run_label="",
        max_actions=max_actions,
        fork_jar=r"D:\SteamLibrary\steamapps\common\SlayTheSpire\mods\CommunicationMod-oracle.jar",
    )
    driver.fork_hash = "A" * 64
    driver.progress = mock.Mock()
    driver.stepper = _StartStepper(
        state if state is not None else _action(_oracle())["state_json"])
    return driver


class RuntimeStackParsingTest(unittest.TestCase):
    def test_parses_version_info_and_mod_list(self):
        with tempfile.TemporaryDirectory() as root:
            path = _write_launch_log(root)
            observed = campaign_driver.parse_launch_log(path)
            self.assertEqual("12-18-2022", observed["sts_version"])
            self.assertEqual("3.30.3", observed["mts_version"])
            self.assertEqual({"basemod": "5.56.0",
                              "CommunicationMod-oracle": "1.2.1-oracle.0"},
                             observed["mods"])
            self.assertEqual([], campaign_driver.check_runtime_stack(observed))

    def test_java_version_is_not_mistaken_for_the_game(self):
        """' - Java version (…)' sits in the same block and must not leak."""
        with tempfile.TemporaryDirectory() as root:
            path = _write_launch_log(root, sts=None, mts=None)
            observed = campaign_driver.parse_launch_log(path)
            self.assertIsNone(observed["sts_version"])
            self.assertIsNone(observed["mts_version"])

    def test_latest_launch_log_orders_numerically_not_lexically(self):
        with tempfile.TemporaryDirectory() as root:
            for index in (1, 2, 12):
                _write_launch_log(root, index=index)
            self.assertEqual(
                "mts_launch12.log",
                os.path.basename(campaign_driver.latest_launch_log(root)))

    def test_latest_launch_log_is_none_without_one(self):
        with tempfile.TemporaryDirectory() as root:
            self.assertIsNone(campaign_driver.latest_launch_log(root))

    def test_check_reports_every_drifted_component_at_once(self):
        observed = {
            "sts_version": "11-30-2020",
            "mts_version": "3.18.1",
            "mods": {"basemod": "5.27.0",
                     "CommunicationMod-oracle": "1.2.1-oracle.0"},
        }
        problems = campaign_driver.check_runtime_stack(observed)
        joined = " | ".join(problems)
        self.assertIn("Slay the Spire", joined)
        self.assertIn("ModTheSpire", joined)
        self.assertIn("basemod", joined)
        self.assertEqual(3, len(problems))

    def test_check_rejects_stock_communicationmod_beside_the_fork(self):
        observed = {
            "sts_version": "12-18-2022",
            "mts_version": "3.30.3",
            "mods": {"basemod": "5.56.0",
                     "CommunicationMod": "1.2.1",
                     "CommunicationMod-oracle": "1.2.1-oracle.0"},
        }
        problems = campaign_driver.check_runtime_stack(observed)
        self.assertTrue(any("stock CommunicationMod" in p for p in problems),
                        problems)

    def test_check_rejects_a_missing_fork(self):
        observed = {"sts_version": "12-18-2022", "mts_version": "3.30.3",
                    "mods": {"basemod": "5.56.0"}}
        problems = campaign_driver.check_runtime_stack(observed)
        self.assertTrue(
            any("CommunicationMod-oracle is not in" in p for p in problems),
            problems)


class RuntimeStackEnforcementTest(unittest.TestCase):
    """A drifted or unobservable stack must never reach an artifact header."""

    def _assert_no_artifact(self, root, campaign_id):
        run_dir = os.path.join(root, campaign_id)
        for name in (f"run_{SEED}_a20_ironclad.jsonl",
                     f"run_{SEED}_a20_ironclad.timing.jsonl"):
            self.assertFalse(os.path.exists(os.path.join(run_dir, name)),
                             f"{name} must not exist")

    def test_missing_launch_log_refuses_before_artifact_or_policy(self):
        """The GUI-launch case that produced the invalid b45_rewards capture."""
        with tempfile.TemporaryDirectory() as root:
            driver = _stack_driver(root, "no_log")
            driver._policy_command = mock.Mock(
                side_effect=AssertionError("policy must not run"))

            with self.assertRaisesRegex(
                    campaign_driver.FatalEnvironmentDrift,
                    "no mts_launch") as raised:
                driver.run_seed(SEED, 1)

            self.assertEqual("stack_unobservable", raised.exception.kind)
            driver._policy_command.assert_not_called()
            self._assert_no_artifact(root, "no_log")

    def test_unparseable_launch_log_refuses(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = os.path.join(root, "garbled")
            os.makedirs(campaign_dir)
            with open(os.path.join(campaign_dir, "mts_launch1.log"),
                      "w", encoding="utf-8") as fh:
                fh.write("Begin patching...\nPatching enums...Done.\n")
            driver = _stack_driver(root, "garbled")

            with self.assertRaisesRegex(
                    campaign_driver.FatalEnvironmentDrift, "Version Info") as r:
                driver.run_seed(SEED, 1)

            self.assertEqual("stack_unparseable", r.exception.kind)
            self._assert_no_artifact(root, "garbled")

    def test_drifted_versions_refuse_before_artifact_or_policy(self):
        """The exact drift that went unnoticed, now fatal instead of silent."""
        with tempfile.TemporaryDirectory() as root:
            _write_launch_log(os.path.join(root, "drift"),
                              sts="11-30-2020", mts="3.18.1")
            driver = _stack_driver(root, "drift")
            driver._policy_command = mock.Mock(
                side_effect=AssertionError("policy must not run"))

            with self.assertRaisesRegex(
                    campaign_driver.FatalEnvironmentDrift,
                    "runtime stack drift") as raised:
                driver.run_seed(SEED, 1)

            self.assertEqual("stack_version_mismatch", raised.exception.kind)
            driver._policy_command.assert_not_called()
            self._assert_no_artifact(root, "drift")

    def test_stock_communicationmod_beside_the_fork_refuses(self):
        with tempfile.TemporaryDirectory() as root:
            _write_launch_log(
                os.path.join(root, "stock"),
                mods=[("basemod", "5.56.0"),
                      ("CommunicationMod", "1.2.1"),
                      ("CommunicationMod-oracle", "1.2.1-oracle.0")])
            driver = _stack_driver(root, "stock")

            with self.assertRaisesRegex(
                    campaign_driver.FatalEnvironmentDrift,
                    "stock CommunicationMod") as raised:
                driver.run_seed(SEED, 1)

            self.assertEqual("stack_version_mismatch", raised.exception.kind)
            self._assert_no_artifact(root, "stock")

    def test_header_reports_the_observed_stack_not_the_constants(self):
        """The regression that matters: header versions come from the log.

        Asserted against a log that is sanctioned but whose BaseMod version
        differs from the constant would be a drift; instead this pins that the
        header's values are literally the parsed ones, and names the file they
        came from, so the claim is auditable from the artifact alone.
        """
        with tempfile.TemporaryDirectory() as root:
            _write_launch_log(os.path.join(root, "observed"), index=3)
            driver = _stack_driver(root, "observed")

            outcome, _floor, actions, _menu = driver.run_seed(SEED, 1)
            self.assertEqual(("action_cap", 0), (outcome, actions))

            artifact = os.path.join(
                root, "observed", f"run_{SEED}_a20_ironclad.jsonl")
            with open(artifact, encoding="utf-8") as fh:
                header = json.loads(fh.readline())

            self.assertEqual(
                {"sts_version": "12-18-2022",
                 "mts_version": "3.30.3",
                 "basemod": campaign_driver.BASEMOD_WORKSHOP,
                 "basemod_version": "5.56.0",
                 "version_source": "mts_launch3.log"},
                header["game"])
            self.assertEqual(
                {"basemod": "5.56.0",
                 "CommunicationMod-oracle": "1.2.1-oracle.0"},
                header["mods_loaded"])
            # and it still satisfies the validator's header contract
            self.assertEqual(set(), validate_artifacts.HEADER_KEYS
                             - header.keys())

    def test_no_constant_can_reach_a_header(self):
        """Guard the shape of the defect, not just one instance of it.

        A future edit that reintroduces `"sts_version": SANCTIONED_STS_VERSION`
        would pass every test above (the sanctioned log agrees with the
        constants). So patch the constants to values no log will ever contain
        and require that the header still reports the log.
        """
        with tempfile.TemporaryDirectory() as root:
            _write_launch_log(os.path.join(root, "guard"))
            driver = _stack_driver(root, "guard")
            sentinel_sts, sentinel_mts = "00-00-0000", "0.0.0"
            with mock.patch.object(campaign_driver,
                                   "SANCTIONED_STS_VERSION", sentinel_sts), \
                 mock.patch.object(campaign_driver,
                                   "SANCTIONED_MTS_VERSION", sentinel_mts):
                # the sanctioned values now disagree with the log, so it refuses
                with self.assertRaises(campaign_driver.FatalEnvironmentDrift):
                    driver.run_seed(SEED, 1)
            self._assert_no_artifact(root, "guard")

            # and with the constants restored the same log is accepted
            driver = _stack_driver(root, "guard")
            driver.run_seed(SEED, 1)
            artifact = os.path.join(
                root, "guard", f"run_{SEED}_a20_ironclad.jsonl")
            with open(artifact, encoding="utf-8") as fh:
                header = json.loads(fh.readline())
            self.assertNotIn(sentinel_sts, json.dumps(header))
            self.assertNotIn(sentinel_mts, json.dumps(header))


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

    def test_requested_dump_seed_mismatch_fails_before_artifact_or_policy(self):
        with tempfile.TemporaryDirectory() as root:
            driver = campaign_driver.CampaignDriver.__new__(
                campaign_driver.CampaignDriver)
            driver.args = SimpleNamespace(
                data_root=root,
                campaign_id="seed_mismatch",
                policy_seed=1234,
                menu_timeout=1.0,
            )
            state = _action(_oracle())["state_json"]
            state["game_state"]["seed"] = SEED_LONG + 1
            driver.stepper = _StartStepper(state)
            driver._policy_command = mock.Mock(
                side_effect=AssertionError("policy must not run"))

            with self.assertRaisesRegex(
                    campaign_driver.FatalEnvironmentDrift,
                    "seed crosscheck mismatch") as raised:
                driver.run_seed(SEED, 1)

            self.assertEqual(
                "seed_identity_mismatch", raised.exception.kind)
            driver._policy_command.assert_not_called()
            run_dir = os.path.join(root, "seed_mismatch")
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
                        orchestrator, "sha256_file", return_value="A" * 64), \
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
                        orchestrator, "sha256_file", return_value="A" * 64), \
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
                        orchestrator, "sha256_file", return_value="A" * 64), \
                    mock.patch.object(
                        orchestrator, "launch_game") as launch:
                result = orchestrator.main([
                    "--data-root", root,
                    "--campaign-id", "preflight",
                    "--seeds", SEED,
                ])

            self.assertEqual(orchestrator.EXIT_CAMPAIGN_INVALID, result)
            launch.assert_not_called()

    def test_completed_progress_is_joined_to_current_fork_before_acceptance(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = os.path.join(root, "preflight")
            os.makedirs(campaign_dir)
            _write_campaign_json(
                os.path.join(campaign_dir, "campaign_progress.json"),
                _progress("preflight"))
            local_app_data = os.path.join(root, "local")
            with mock.patch.dict(
                    os.environ, {"LOCALAPPDATA": local_app_data}), \
                    mock.patch.object(
                        orchestrator, "sha256_file",
                        return_value="B" * 64), \
                    mock.patch.object(
                        orchestrator, "launch_game") as launch:
                result = orchestrator.main([
                    "--data-root", root,
                    "--campaign-id", "preflight",
                    "--seeds", SEED,
                ])

            self.assertEqual(orchestrator.EXIT_CAMPAIGN_INVALID, result)
            launch.assert_not_called()

    def test_complete_with_failures_empty_list_is_still_nonzero(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = os.path.join(root, "preflight")
            os.makedirs(campaign_dir)
            _write_campaign_json(
                os.path.join(campaign_dir, "campaign_progress.json"),
                _progress(
                    "preflight", status="complete_with_failures",
                    failed=[]))
            local_app_data = os.path.join(root, "local")
            with mock.patch.dict(
                    os.environ, {"LOCALAPPDATA": local_app_data}), \
                    mock.patch.object(
                        orchestrator, "sha256_file",
                        return_value="A" * 64), \
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
            path = os.path.join(root, f"run_{SEED}_a20_ironclad.jsonl")
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

    def test_require_oracle_joins_all_seed_identities(self):
        header = _header(True)
        header["seed"].update({
            "long": SEED_LONG + 1,
            "long_getLong": SEED_LONG + 2,
            "crosscheck_ok": False,
        })
        oracle = _oracle()
        oracle["seed"] = SEED_LONG + 3
        action = _action(oracle)
        action["state_json"]["game_state"]["seed"] = SEED_LONG + 4

        errors = self._validate(
            [header, action, _terminal()], require_oracle=True)
        joined = "\n".join(errors)
        self.assertIn("seed.long_getLong", joined)
        self.assertIn("seed.long", joined)
        self.assertIn("crosscheck_ok", joined)
        self.assertIn("game_state.seed", joined)
        self.assertIn("oracle.seed", joined)

    def test_require_oracle_rejects_boolean_seed_longs(self):
        header = _header(True)
        header["seed"]["long"] = True
        header["seed"]["long_getLong"] = True
        action = _action(_oracle())
        action["state_json"]["game_state"]["seed"] = True
        action["state_json"]["game_state"]["oracle"]["seed"] = True

        errors = self._validate(
            [header, action, _terminal()], require_oracle=True)
        joined = "\n".join(errors)
        self.assertIn("seed.long_getLong", joined)
        self.assertIn("seed.long", joined)
        self.assertIn("game_state.seed", joined)
        self.assertIn("oracle.seed", joined)


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

    def test_strict_artifact_rejects_duplicate_terminal_and_action_after_it(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = self._make_campaign(root)
            path = os.path.join(
                campaign_dir, f"run_{SEED}_a20_ironclad.jsonl")
            with open(path, "r", encoding="utf-8") as fh:
                records = [json.loads(line) for line in fh if line.strip()]
            records.append(dict(records[-1]))
            _write_artifact(path, records)

            errors, _actions = validate_artifacts.validate_file(
                path, require_oracle=True)
            self.assertIn(
                "more than one terminal", "\n".join(errors))

            records = records[:-1]
            records.append(_action(_oracle(), seq=1))
            _write_artifact(path, records)
            errors, _actions = validate_artifacts.validate_file(
                path, require_oracle=True)
            joined = "\n".join(errors)
            self.assertIn("terminal must be the final record", joined)
            self.assertIn("action appears after terminal", joined)

    def test_strict_campaign_rejects_missing_done_and_terminal_summaries(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = self._make_campaign(root)
            progress_path = os.path.join(
                campaign_dir, "campaign_progress.json")
            with open(progress_path, "r", encoding="utf-8") as fh:
                progress = json.load(fh)
            del progress["seeds_done"][0]["floor"]
            del progress["seeds_done"][0]["actions"]
            _write_campaign_json(progress_path, progress)
            _write_campaign_json(
                os.path.join(campaign_dir, "campaign_manifest.json"),
                {key: progress[key]
                 for key in validate_artifacts.STRICT_MANIFEST_KEYS})
            path = os.path.join(
                campaign_dir, f"run_{SEED}_a20_ironclad.jsonl")
            with open(path, "r", encoding="utf-8") as fh:
                records = [json.loads(line) for line in fh if line.strip()]
            del records[-1]["floor"]
            del records[-1]["actions"]
            _write_artifact(path, records)

            _files, errors = validate_artifacts.validate_campaign(
                campaign_dir, require_oracle=True)
            joined = "\n".join(errors)
            self.assertIn("seeds_done row missing", joined)
            self.assertIn("strict terminal missing", joined)

    def test_strict_artifact_rejects_seq_and_action_count_drift(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = self._make_campaign(root)
            path = os.path.join(
                campaign_dir, f"run_{SEED}_a20_ironclad.jsonl")
            with open(path, "r", encoding="utf-8") as fh:
                records = [json.loads(line) for line in fh if line.strip()]
            records[1]["seq"] = 4
            records[-1]["actions"] = 7
            _write_artifact(path, records)

            errors, _actions = validate_artifacts.validate_file(
                path, require_oracle=True)
            joined = "\n".join(errors)
            self.assertIn("action seq must be contiguous", joined)
            self.assertIn("does not match 1 injected action", joined)

    def test_strict_timing_rejects_tail_duplicates_and_action_drift(self):
        cases = {
            "malformed_tail": lambda records: records.append("not-json"),
            "duplicate_header": lambda records: records.append(
                dict(records[0])),
            "wrong_command": lambda records: records[1].update(
                {"cmd": "skip"}),
            "missing_mark": lambda records: records.pop(),
        }
        for label, mutate in cases.items():
            with self.subTest(label=label), \
                    tempfile.TemporaryDirectory() as root:
                campaign_dir = self._make_campaign(root)
                timing_path = os.path.join(
                    campaign_dir,
                    f"run_{SEED}_a20_ironclad.timing.jsonl")
                with open(timing_path, "r", encoding="utf-8") as fh:
                    records = [json.loads(line)
                               for line in fh if line.strip()]
                mutate(records)
                with open(timing_path, "w", encoding="utf-8",
                          newline="\n") as fh:
                    for record in records:
                        if isinstance(record, str):
                            fh.write(record + "\n")
                        else:
                            fh.write(json.dumps(record) + "\n")
                _files, errors = validate_artifacts.validate_campaign(
                    campaign_dir, require_oracle=True)
                self.assertTrue(errors, label)

    def test_happy_boss_claim_counter_and_timing_pass_strict_campaign(self):
        class BossStepper:
            def step(self, _command):
                final_state = _action(
                    _oracle(), command=validate_artifacts.TERMINAL_MARKER)
                state = final_state["state_json"]
                state["game_state"].update({
                    "room_type": "MonsterRoomBoss",
                    "choice_list": [],
                })
                return "ready", state

        with tempfile.TemporaryDirectory() as root:
            campaign_id = "boss_strict"
            campaign_dir = os.path.join(root, campaign_id)
            os.makedirs(campaign_dir)
            run_path = os.path.join(
                campaign_dir, f"run_{SEED}_a20_ironclad.jsonl")
            timing_path = os.path.join(
                campaign_dir,
                f"run_{SEED}_a20_ironclad.timing.jsonl")
            logger = campaign_driver.RunLogger(
                run_path, _campaign_header(campaign_id))
            timing = campaign_driver.TimingLog(timing_path, {
                "campaign_id": campaign_id,
                "policy": "random-legal",
                "seed": SEED,
                "attempt": 1,
                "schema_version": 1,
                "driver_version": "test",
                "fork_jar_sha256": "A" * 64,
            })
            driver = campaign_driver.CampaignDriver.__new__(
                campaign_driver.CampaignDriver)
            driver.stepper = BossStepper()
            first = _action(_oracle())["state_json"]
            first["game_state"].update({
                "room_type": "MonsterRoomBoss",
                "choice_list": ["gold"],
            })
            outcome, actions = driver._claim_boss_reward(
                logger, timing, first, SEED, 0)
            logger.close()
            timing.close()
            self.assertEqual(("act1_boss_reward", 1),
                             (outcome, actions))

            done = _done()
            done.update({
                "outcome": outcome,
                "actions": actions,
            })
            progress = _progress(campaign_id, done=[done])
            _write_campaign_json(
                os.path.join(campaign_dir, "campaign_progress.json"),
                progress)
            _write_campaign_json(
                os.path.join(campaign_dir, "campaign_manifest.json"),
                {key: progress[key]
                 for key in validate_artifacts.STRICT_MANIFEST_KEYS})

            _files, errors = validate_artifacts.validate_campaign(
                campaign_dir, require_oracle=True)
            self.assertEqual([], errors)


class CampaignIdentityAndFreshTest(unittest.TestCase):
    def test_fresh_cleanup_is_bounded_to_owned_expected_files(self):
        with tempfile.TemporaryDirectory() as data_root:
            root = os.path.join(data_root, "strict")
            os.makedirs(root)
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

            removed = orchestrator.clear_fresh_campaign_files(
                data_root, "strict", [SEED])

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

    def test_progress_refuses_boolean_schema_identity(self):
        with tempfile.TemporaryDirectory() as root:
            path = os.path.join(root, "campaign_progress.json")
            heartbeat = os.path.join(root, "campaign_heartbeat.json")
            stored = _progress("strict")
            stored["schema_version"] = True
            _write_campaign_json(path, stored)
            progress = campaign_driver.Progress(path, heartbeat)
            with self.assertRaisesRegex(
                    campaign_driver.CampaignIdentityError, "schema_version"):
                progress.load_or_init(
                    "strict", [SEED], "random-legal", "A" * 64)

    def test_campaign_id_escape_is_rejected_before_cleanup_or_write(self):
        invalid_ids = [
            ".", "..", "../outside", r"..\outside",
            os.path.abspath(os.sep),
        ]
        for campaign_id in invalid_ids:
            with self.subTest(campaign_id=campaign_id):
                with self.assertRaises(ValueError):
                    campaign_paths.validate_campaign_id(campaign_id)

        with tempfile.TemporaryDirectory() as root:
            sentinel = os.path.join(root, "campaign_progress.json")
            with open(sentinel, "w", encoding="utf-8") as fh:
                fh.write("preserve")
            data_root = os.path.join(root, "campaigns")
            os.makedirs(data_root)
            with self.assertRaises(ValueError):
                orchestrator.clear_fresh_campaign_files(
                    data_root, "..", [SEED])
            self.assertTrue(os.path.exists(sentinel))

            with mock.patch.object(orchestrator, "launch_game") as launch:
                result = orchestrator.main([
                    "--data-root", data_root,
                    "--campaign-id", "..",
                    "--seeds", SEED,
                    "--fresh",
                ])
            self.assertEqual(orchestrator.EXIT_CAMPAIGN_INVALID, result)
            launch.assert_not_called()
            self.assertTrue(os.path.exists(sentinel))

            driver_result = campaign_driver.main([
                "--data-root", data_root,
                "--campaign-id", "..",
                "--seeds", SEED,
                "--fork-jar", sentinel,
            ])
            self.assertEqual(campaign_driver.EXIT_FATAL, driver_result)
            self.assertTrue(os.path.exists(sentinel))

    def test_redirected_owned_child_fails_without_touching_target(self):
        with tempfile.TemporaryDirectory() as data_root:
            campaign_id = "redirected"
            campaign_dir = os.path.join(data_root, campaign_id)
            os.makedirs(campaign_dir)
            unexpected = os.path.join(campaign_dir, "operator_notes.txt")
            with open(unexpected, "w", encoding="utf-8") as fh:
                fh.write("preserve this evidence")
            redirected = os.path.join(
                campaign_dir, "campaign_progress.json")
            try:
                os.symlink(unexpected, redirected)
            except (NotImplementedError, OSError) as exc:
                self.skipTest(
                    f"platform cannot create a file symlink: {exc}")

            with self.assertRaisesRegex(ValueError, "redirected campaign path"):
                orchestrator.clear_fresh_campaign_files(
                    data_root, campaign_id, [SEED])
            self.assertTrue(os.path.lexists(redirected))
            with open(unexpected, "r", encoding="utf-8") as fh:
                self.assertEqual("preserve this evidence", fh.read())

            _files, errors = validate_artifacts.validate_campaign(
                campaign_dir, require_oracle=True)
            self.assertIn("redirected campaign path", "\n".join(errors))

            with mock.patch.object(orchestrator, "launch_game") as launch:
                result = orchestrator.main([
                    "--data-root", data_root,
                    "--campaign-id", campaign_id,
                    "--seeds", SEED,
                    "--fork-jar", unexpected,
                    "--fresh",
                ])
            self.assertEqual(orchestrator.EXIT_CAMPAIGN_INVALID, result)
            launch.assert_not_called()
            self.assertTrue(os.path.lexists(redirected))
            with open(unexpected, "r", encoding="utf-8") as fh:
                self.assertEqual("preserve this evidence", fh.read())

            progress = campaign_driver.Progress(
                redirected,
                os.path.join(campaign_dir, "campaign_heartbeat.json"))
            with self.assertRaisesRegex(ValueError, "redirected campaign path"):
                progress.load_or_init(
                    campaign_id, [SEED], "random-legal", "A" * 64)
            with open(unexpected, "r", encoding="utf-8") as fh:
                self.assertEqual("preserve this evidence", fh.read())

    def test_resume_identity_mismatch_becomes_durable_fatal_status(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_id = "identity"
            campaign_dir = os.path.join(root, campaign_id)
            os.makedirs(campaign_dir)
            progress_path = os.path.join(
                campaign_dir, "campaign_progress.json")
            _write_campaign_json(
                progress_path,
                _progress(campaign_id, status="in_progress",
                          done=[], failed=[]))
            driver = campaign_driver.CampaignDriver.__new__(
                campaign_driver.CampaignDriver)
            driver.args = SimpleNamespace(
                seeds=[SEED],
                campaign_id=campaign_id,
                policy="random-legal",
            )
            driver.reader = _Reader()
            driver.stepper = _HandshakeStepper()
            driver.fork_hash = "B" * 64
            driver.progress = campaign_driver.Progress(
                progress_path,
                os.path.join(campaign_dir, "campaign_heartbeat.json"))

            self.assertEqual(campaign_driver.EXIT_FATAL, driver.run())
            with open(progress_path, "r", encoding="utf-8") as fh:
                progress = json.load(fh)
            self.assertEqual("fatal_environment_drift", progress["status"])
            self.assertEqual(
                "campaign_identity_mismatch", progress["fatal"]["kind"])

    def test_orchestrator_joins_current_fork_and_rejects_any_failed_status(self):
        args = SimpleNamespace(
            campaign_id="strict",
            seed_list=[SEED],
            policy="random-legal",
            fork_hash="B" * 64,
        )
        progress = _progress("strict")
        self.assertIn(
            "fork_jar_sha256",
            orchestrator.progress_identity_error(progress, args))
        progress["fork_jar_sha256"] = "B" * 64
        progress["schema_version"] = True
        self.assertIn(
            "schema_version",
            orchestrator.progress_identity_error(progress, args))
        progress["status"] = "complete_with_failures"
        progress["seeds_failed"] = []
        self.assertIn(
            "status must be 'complete'",
            orchestrator.completion_error(progress, [SEED]))

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
