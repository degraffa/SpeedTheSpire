#!/usr/bin/env python3
"""Synthetic regressions for oracle-campaign environment preflighting."""

from __future__ import annotations

import glob
import hashlib
import io
import json
import os
import random
import tempfile
import unittest
from contextlib import redirect_stdout
from types import SimpleNamespace
from unittest import mock

import campaign_driver
import campaign_paths
import gen_cards_sidetable
import greedy_policy
import orchestrator
import survival_policy_cmd
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


class _GoneHandshakeStepper(_HandshakeStepper):
    def await_ready(self):
        raise campaign_driver.GameGone("synthetic handshake pipe loss")


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


def _stack_driver(root, campaign_id, state=None, max_actions=0,
                  launch_index=1):
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
        launch_log=f"mts_launch{launch_index}.log",
        launch_token="unit-test-launch-token",
        fork_jar=r"D:\SteamLibrary\steamapps\common\SlayTheSpire\mods\CommunicationMod-oracle.jar",
    )
    driver.fork_hash = "A" * 64
    driver.progress = mock.Mock()
    driver.stepper = _StartStepper(
        state if state is not None else _action(_oracle())["state_json"])
    return driver


class LivePolicyTransientSettleTest(unittest.TestCase):
    def test_empty_legal_set_yields_between_bounded_state_probes(self):
        """Twelve probes must span frames, not race one animation frame."""
        with tempfile.TemporaryDirectory() as root:
            campaign_id = "transient_settle"
            _write_launch_log(os.path.join(root, campaign_id))
            state = _action(_oracle())["state_json"]
            state["available_commands"] = []
            state["game_state"]["choice_list"] = []
            driver = _stack_driver(
                root, campaign_id, state=state, max_actions=100)
            driver.args.settle_sleep = 0.0
            driver.rng = random.Random(1234)
            # The first state is consumed by `start`; eleven more probes occur
            # before the twelfth empty expansion becomes legal_exhaustion.
            driver.stepper.states.extend([state] * 11)

            with mock.patch.dict(
                    os.environ,
                    {campaign_paths.ORACLE_LAUNCH_TOKEN_ENV:
                     "unit-test-launch-token"}), \
                    mock.patch.object(
                        campaign_driver.time, "sleep") as sleep:
                outcome, _floor, actions, menu_ok = driver.run_seed(SEED, 1)

            self.assertEqual(("legal_exhaustion", 0, False),
                             (outcome, actions, menu_ok))
            self.assertEqual(11, sleep.call_count)
            sleep.assert_has_calls([
                mock.call(campaign_driver.TRANSIENT_NO_ACTION_SLEEP_S)
            ] * 11)
            self.assertEqual(
                [f"start ironclad 20 {SEED}"] + ["state"] * 11,
                driver.stepper.commands)

    def test_neow_calling_bell_relic_rows_cannot_be_proceeded_past(self):
        """Recorded b1.5.1 STS300076 state: all three rows are mandatory."""
        state = {
            "available_commands": ["choose", "proceed"],
            "in_game": True,
            "game_state": {
                "choice_list": ["relic", "relic", "relic"],
                "screen_type": "COMBAT_REWARD",
                "room_type": "NeowRoom",
                "screen_state": {
                    "rewards": [
                        {"reward_type": "RELIC"},
                        {"reward_type": "RELIC"},
                        {"reward_type": "RELIC"},
                    ],
                },
            },
        }
        self.assertEqual(
            ["choose 0", "choose 1", "choose 2"],
            campaign_driver.expand_legal_actions(state, random.Random(0)))

        # After the last mandatory row has been claimed, the same screen's
        # proceed command is the only way to finish the Neow transition.
        state["game_state"]["choice_list"] = []
        state["game_state"]["screen_state"]["rewards"] = []
        self.assertEqual(
            ["proceed"],
            campaign_driver.expand_legal_actions(state, random.Random(0)))

    def test_game_over_pipe_loss_is_not_reported_as_menu_returnable(self):
        driver = campaign_driver.CampaignDriver.__new__(
            campaign_driver.CampaignDriver)
        driver.args = SimpleNamespace(menu_timeout=1.0)
        driver.stepper = mock.Mock()
        driver.stepper.step.side_effect = campaign_driver.GameGone(
            "synthetic game-over pipe loss")

        self.assertFalse(driver._return_to_menu_from_gameover())

    def test_noop_that_reveals_game_over_is_recorded_before_proceed(self):
        """A no-op response may already carry GAME_OVER, not a recovery screen."""
        with tempfile.TemporaryDirectory() as root:
            campaign_id = "noop_game_over"
            _write_launch_log(os.path.join(root, campaign_id))
            initial = _action(_oracle())["state_json"]
            initial["available_commands"] = ["end"]
            initial["game_state"]["screen_type"] = "COMBAT"
            game_over = {
                "available_commands": ["proceed"],
                "ready_for_command": False,
                "in_game": True,
                "game_state": {
                    "screen_type": "GAME_OVER",
                    "screen_state": {"victory": False},
                    "floor": 1,
                    "act": 1,
                },
            }
            menu = {
                "available_commands": ["start"],
                "ready_for_command": True,
                "in_game": False,
            }
            driver = _stack_driver(root, campaign_id, state=initial,
                                   max_actions=100)
            driver.stepper.states.extend([game_over, menu])

            with mock.patch.dict(
                    os.environ,
                    {campaign_paths.ORACLE_LAUNCH_TOKEN_ENV:
                     "unit-test-launch-token"}):
                outcome, floor, actions, menu_ok = driver.run_seed(SEED, 1)

            self.assertEqual(("death", 1, 1, True),
                             (outcome, floor, actions, menu_ok))
            self.assertEqual(
                [f"start ironclad 20 {SEED}", "end", "proceed"],
                driver.stepper.commands)
            with open(os.path.join(
                    root, campaign_id, f"run_{SEED}_a20_ironclad.jsonl"),
                    encoding="utf-8") as fh:
                records = [json.loads(line) for line in fh]
            self.assertEqual("__terminal_observed__",
                             records[-2]["action_command"])
            self.assertEqual("death", records[-1]["outcome"])
            self.assertEqual(1, records[-1]["floor"])


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

    def setUp(self):
        self.launch_env = mock.patch.dict(
            os.environ,
            {campaign_paths.ORACLE_LAUNCH_TOKEN_ENV:
             "unit-test-launch-token"})
        self.launch_env.start()

    def tearDown(self):
        self.launch_env.stop()

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
                    "bound launch log") as raised:
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

    def test_exact_launch_binding_cannot_fall_back_to_a_stale_higher_log(self):
        """A restarted orchestrator used to accept stale launch3 over launch1."""
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = os.path.join(root, "resume")
            _write_launch_log(campaign_dir, index=1,
                              sts="11-30-2020", mts="3.18.1")
            _write_launch_log(campaign_dir, index=3)
            driver = _stack_driver(root, "resume", launch_index=1)

            with self.assertRaisesRegex(
                    campaign_driver.FatalEnvironmentDrift,
                    "mts_launch1.log") as raised:
                driver.run_seed(SEED, 1)

            self.assertEqual("stack_version_mismatch", raised.exception.kind)
            self._assert_no_artifact(root, "resume")

    def test_persisted_gui_command_cannot_reuse_a_stale_valid_log(self):
        with tempfile.TemporaryDirectory() as root:
            _write_launch_log(os.path.join(root, "gui"))
            driver = _stack_driver(root, "gui")
            with mock.patch.dict(os.environ, {}, clear=True):
                with self.assertRaisesRegex(
                        campaign_driver.FatalEnvironmentDrift,
                        "not bound to the orchestrator launch") as raised:
                    driver.run_seed(SEED, 1)

            self.assertEqual("stack_unobservable", raised.exception.kind)
            self._assert_no_artifact(root, "gui")

    def test_redirected_owned_looking_launch_log_is_never_evidence(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_dir = os.path.join(root, "redirected_log")
            os.makedirs(campaign_dir)
            target = os.path.join(campaign_dir, "operator_evidence.log")
            with open(target, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(_launch_log())
            redirected = os.path.join(campaign_dir, "mts_launch1.log")
            try:
                os.symlink(target, redirected)
            except (NotImplementedError, OSError) as exc:
                self.skipTest(
                    f"platform cannot create a file symlink: {exc}")
            driver = _stack_driver(root, "redirected_log")

            with self.assertRaisesRegex(
                    campaign_driver.FatalEnvironmentDrift,
                    "redirected") as raised:
                driver.run_seed(SEED, 1)

            self.assertEqual("stack_unobservable", raised.exception.kind)
            self.assertTrue(os.path.lexists(redirected))
            with open(target, encoding="utf-8") as fh:
                self.assertEqual(_launch_log(), fh.read())
            with self.assertRaisesRegex(ValueError, "redirected campaign path"):
                orchestrator.highest_launch_index(root, "redirected_log")
            self._assert_no_artifact(root, "redirected_log")

    def test_header_reports_the_observed_stack_not_the_constants(self):
        """The regression that matters: header versions come from the log.

        Asserted against a log that is sanctioned but whose BaseMod version
        differs from the constant would be a drift; instead this pins that the
        header's values are literally the parsed ones, and names the file they
        came from, so the claim is auditable from the artifact alone.
        """
        with tempfile.TemporaryDirectory() as root:
            _write_launch_log(os.path.join(root, "observed"), index=3)
            driver = _stack_driver(root, "observed", launch_index=3)

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

    def test_policy_seed_is_recorded_in_the_run_header(self):
        """Design 7.5 gap: the greedy tie-break RNG is seeded from
        (policy_seed, seed), so without policy_seed in the artifact a
        campaign's real choices cannot be reconstructed from its own output.
        `_stack_driver` defaults `policy_seed=1234`; use a distinctive value
        so this cannot pass by coincidence against that default."""
        with tempfile.TemporaryDirectory() as root:
            _write_launch_log(os.path.join(root, "seedprov"))
            driver = _stack_driver(root, "seedprov")
            driver.args.policy_seed = 918273

            driver.run_seed(SEED, 1)

            artifact = os.path.join(
                root, "seedprov", f"run_{SEED}_a20_ironclad.jsonl")
            with open(artifact, encoding="utf-8") as fh:
                header = json.loads(fh.readline())
            self.assertEqual(918273, header["policy_seed"])
            # additive: the required header-key contract is unchanged
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
                                   "SANCTIONED_MTS_VERSION", sentinel_mts), \
                 mock.patch.object(campaign_driver, "check_runtime_stack",
                                   return_value=[]):
                # Bypass the comparison only: the header must still use the
                # parsed log, never either patched expectation.
                driver.run_seed(SEED, 1)
            artifact = os.path.join(
                root, "guard", f"run_{SEED}_a20_ironclad.jsonl")
            with open(artifact, encoding="utf-8") as fh:
                header = json.loads(fh.readline())
            self.assertEqual("12-18-2022",
                             header["game"]["sts_version"])
            self.assertEqual("3.30.3", header["game"]["mts_version"])
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
                policy_seed=1234,
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

    def test_handshake_pipe_loss_writes_token_bound_restart_request(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_id = "handshake_restart"
            run_dir = os.path.join(root, campaign_id)
            os.makedirs(run_dir)
            driver = campaign_driver.CampaignDriver.__new__(
                campaign_driver.CampaignDriver)
            driver.args = SimpleNamespace(
                seeds=[SEED],
                campaign_id=campaign_id,
                policy="random-legal",
                policy_seed=1234,
                launch_token="handshake-launch-token",
            )
            driver.reader = _Reader()
            driver.stepper = _GoneHandshakeStepper()
            driver.fork_hash = "A" * 64
            progress_path = os.path.join(
                run_dir, "campaign_progress.json")
            driver.progress = campaign_driver.Progress(
                progress_path,
                os.path.join(run_dir, "campaign_heartbeat.json"))

            self.assertEqual(campaign_driver.EXIT_GAME_GONE, driver.run())
            with open(progress_path, "r", encoding="utf-8") as fh:
                progress = json.load(fh)
            self.assertTrue(orchestrator.restart_requested_for_launch(
                progress, "handshake-launch-token"))
            self.assertEqual(
                "handshake_game_gone",
                progress["restart_requested"]["reason"])


class HeartbeatAtomicWriteTest(unittest.TestCase):
    """g6_campaign_spotdiff.md §9: Progress.heartbeat() used to be a plain
    truncating `open(path, "w")`, unlike campaign_progress.json's flush(),
    which goes through a tmp file + fsync + os.replace. A reader racing the
    truncating write could observe a torn/unreadable sample, and the
    orchestrator treated that identically to "stale since launch". These
    tests prove the heartbeat write now goes through the same tmp+rename
    door -- the destination file is only ever touched by a completed
    rename, so a reader can never observe a partial write."""

    def test_heartbeat_write_never_truncates_destination_in_place(self):
        with tempfile.TemporaryDirectory() as root:
            hb_path = os.path.join(root, "campaign_heartbeat.json")
            progress_path = os.path.join(root, "campaign_progress.json")
            old_content = ('{"t": 1.0, "utc": "old", "seed": "OLD", '
                          '"floor": 1, "actions": 1}')
            with open(hb_path, "w", encoding="utf-8") as fh:
                fh.write(old_content)

            progress = campaign_driver.Progress(progress_path, hb_path)

            # Simulate a crash after the new content is written to the tmp
            # file but before the rename lands it on the destination -- the
            # exact window a non-atomic `open(path, "w")` has no equivalent
            # of, because it truncates the destination immediately.
            with mock.patch.object(
                    campaign_driver.os, "replace",
                    side_effect=OSError("simulated crash before rename")):
                progress.heartbeat("STS00099", 5, 42)  # best-effort: swallowed

            with open(hb_path, "r", encoding="utf-8") as fh:
                self.assertEqual(old_content, fh.read())
            # The new content really did land somewhere -- the tmp path --
            # proving the write target was never the destination itself.
            with open(progress.hb_tmp_path, "r", encoding="utf-8") as fh:
                tmp_content = json.load(fh)
            self.assertEqual("STS00099", tmp_content["seed"])

    def test_heartbeat_write_succeeds_atomically_via_rename(self):
        with tempfile.TemporaryDirectory() as root:
            hb_path = os.path.join(root, "campaign_heartbeat.json")
            progress_path = os.path.join(root, "campaign_progress.json")
            progress = campaign_driver.Progress(progress_path, hb_path)

            progress.heartbeat("STS00100", 3, 17)

            with open(hb_path, "r", encoding="utf-8") as fh:
                data = json.load(fh)
            self.assertEqual("STS00100", data["seed"])
            self.assertEqual(3, data["floor"])
            self.assertEqual(17, data["actions"])
            # The rename consumes the tmp file -- nothing left behind.
            self.assertFalse(os.path.exists(progress.hb_tmp_path))


class AtomicReplaceRetryTest(unittest.TestCase):
    @staticmethod
    def _sharing_error():
        exc = PermissionError("synthetic Windows sharing violation")
        exc.winerror = 32
        return exc

    def test_transient_sharing_violation_retries_then_publishes(self):
        with mock.patch.object(
                campaign_driver.os, "replace",
                side_effect=[self._sharing_error(),
                             self._sharing_error(), None]) as replace, \
             mock.patch.object(campaign_driver.time, "sleep") as sleep:
            campaign_driver.atomic_replace_with_retry("tmp", "progress")

        self.assertEqual(3, replace.call_count)
        self.assertEqual(2, sleep.call_count)
        sleep.assert_has_calls([
            mock.call(campaign_driver.ATOMIC_REPLACE_RETRY_SLEEP_S),
            mock.call(campaign_driver.ATOMIC_REPLACE_RETRY_SLEEP_S),
        ])

    def test_exhausted_sharing_violation_still_fails_loud(self):
        with mock.patch.object(
                campaign_driver, "ATOMIC_REPLACE_ATTEMPTS", 3), \
             mock.patch.object(
                campaign_driver.os, "replace",
                side_effect=self._sharing_error()) as replace, \
             mock.patch.object(campaign_driver.time, "sleep") as sleep:
            with self.assertRaises(PermissionError):
                campaign_driver.atomic_replace_with_retry("tmp", "progress")

        self.assertEqual(3, replace.call_count)
        self.assertEqual(2, sleep.call_count)

    def test_non_sharing_error_is_not_hidden_or_retried(self):
        with mock.patch.object(
                campaign_driver.os, "replace",
                side_effect=OSError("real filesystem failure")) as replace, \
             mock.patch.object(campaign_driver.time, "sleep") as sleep:
            with self.assertRaisesRegex(OSError, "real filesystem failure"):
                campaign_driver.atomic_replace_with_retry("tmp", "progress")

        replace.assert_called_once_with("tmp", "progress")
        sleep.assert_not_called()


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


class OrchestratorStallWatchdogTest(unittest.TestCase):
    """g6_campaign_spotdiff.md §9: orchestrator.py's watchdog fell back to
    `now - launch_started` whenever a heartbeat sample was unreadable, which
    made the staleness guard compare that expression to itself -- so ONE
    unreadable sample, any time after stall_timeout seconds, was sufficient
    to kill a healthy, mid-combat game. These tests drive `orchestrator.main`
    with `time.time()` and `read_json` fully mocked (deterministic timestamps
    and canned heartbeat/progress reads -- no real clock, no real files for
    the watchdog decision itself) so the exact sample sequence from that
    incident can be reproduced.
    """

    def _run(self, root, campaign_id, heartbeats, progress_by_call,
             times, stall_timeout=10.0):
        campaign_dir = os.path.join(root, campaign_id)
        os.makedirs(campaign_dir)
        heartbeat_iter = iter(heartbeats)
        call_count = {"n": 0}

        def fake_read_json(path):
            if "heartbeat" in os.path.basename(path):
                return next(heartbeat_iter)
            call_count["n"] += 1
            return progress_by_call(call_count["n"])

        game_proc = mock.Mock()
        game_proc.poll.return_value = None
        local_app_data = os.path.join(root, "local")
        with mock.patch.dict(
                os.environ, {"LOCALAPPDATA": local_app_data}), \
                mock.patch.object(
                    orchestrator, "sha256_file", return_value="A" * 64), \
                mock.patch.object(
                    orchestrator, "launch_game",
                    return_value=game_proc) as launch, \
                mock.patch.object(orchestrator, "kill_tree") as kill, \
                mock.patch.object(
                    orchestrator, "read_json",
                    side_effect=fake_read_json), \
                mock.patch.object(orchestrator.time, "sleep"), \
                mock.patch.object(
                    orchestrator.time, "time", side_effect=times):
            result = orchestrator.main([
                "--data-root", root,
                "--campaign-id", campaign_id,
                "--seeds", SEED,
                "--stall-timeout", str(stall_timeout),
            ])
        with open(os.path.join(
                campaign_dir, "orchestrator_timeline.json"),
                "r", encoding="utf-8") as fh:
            timeline = json.load(fh)["timeline"]
        return result, timeline, launch, kill

    def test_one_transiently_unreadable_sample_between_good_ones_no_kill(self):
        """A single unreadable/missing heartbeat sample, sandwiched between
        two fresh, readable ones, must never be treated as a stall -- the
        exact shape that killed a healthy game twice in the G6 campaign."""
        with tempfile.TemporaryDirectory() as root:
            def progress_by_call(n):
                if n <= 4:
                    return _progress(
                        "stall", status="in_progress", done=[], failed=[])
                return _progress(
                    "stall", status="complete", done=[_done()], failed=[])

            result, timeline, launch, kill = self._run(
                root, "stall",
                heartbeats=[
                    {"t": 110.0},  # iter A: fresh (age 1s)
                    None,          # iter B: ONE unreadable/missing sample
                    {"t": 132.0},  # iter C: fresh again (age 1s)
                ],
                progress_by_call=progress_by_call,
                times=[
                    0.0,     # start
                    0.0,     # outer-loop campaign-timeout check
                    100.0,   # launch_started
                    111.0,   # iter A: now
                    122.0,   # iter B: now (the lone bad sample)
                    133.0,   # iter C: now
                    144.0,   # iter D: now (progress reports complete)
                ])

            self.assertEqual(0, result)
            self.assertEqual(1, launch.call_count,
                             "the lone bad sample must not have caused a "
                             "relaunch")
            events = [t["event"] for t in timeline]
            self.assertNotIn("stall_relaunch", events)
            self.assertEqual(["launch", "complete"], events)

    def test_genuinely_stale_heartbeat_still_triggers_kill_and_relaunch(self):
        """A heartbeat that IS readable but genuinely old (game hung, or the
        driver died without updating it) must still be judged a stall at the
        same threshold as before -- this is the true-stall path and must not
        regress while the unreadable-sample path is fixed."""
        with tempfile.TemporaryDirectory() as root:
            def progress_by_call(n):
                if n >= 4:
                    return _progress(
                        "stall", status="complete", done=[_done()],
                        failed=[])
                return _progress(
                    "stall", status="in_progress", done=[], failed=[])

            result, timeline, launch, kill = self._run(
                root, "stall",
                heartbeats=[
                    {"t": 50.0},  # genuinely old: 61s stale, readable fine
                ],
                progress_by_call=progress_by_call,
                times=[
                    0.0,     # start
                    0.0,     # outer-loop check, launch #1
                    100.0,   # launch_started #1
                    111.0,   # iter 1: now (stale heartbeat -> kill)
                    112.0,   # outer-loop check, launch #2
                    200.0,   # launch_started #2
                    211.0,   # iter for launch #2: now (progress complete)
                ])

            self.assertEqual(0, result)
            self.assertEqual(2, launch.call_count,
                             "the genuine stall must still relaunch")
            events = [t["event"] for t in timeline]
            self.assertEqual(["launch", "stall_relaunch", "launch",
                              "complete"], events)
            stall_event = next(
                t for t in timeline if t["event"] == "stall_relaunch")
            self.assertEqual(61.0, stall_event["age"])

    def test_current_launch_restart_request_relaunches_without_stall_wait(self):
        """A driver-requested mid-dungeon restart is acted on at the next poll.

        The heartbeat is deliberately never sampled: this proves the explicit
        signal wins before the old four-minute stale-heartbeat fallback.
        """
        with tempfile.TemporaryDirectory() as root:
            def progress_by_call(n):
                if n >= 4:
                    return _progress(
                        "restart", status="complete", done=[_done()],
                        failed=[])
                return _progress(
                    "restart", status="in_progress", done=[], failed=[])

            with mock.patch.object(
                    orchestrator, "restart_requested_for_launch",
                    return_value=True) as requested:
                result, timeline, launch, kill = self._run(
                    root, "restart",
                    heartbeats=[],
                    progress_by_call=progress_by_call,
                    times=[
                        0.0, 0.0, 100.0, 101.0,
                        102.0, 200.0, 201.0,
                    ])

            self.assertEqual(0, result)
            self.assertEqual(2, launch.call_count)
            self.assertGreaterEqual(kill.call_count, 1)
            self.assertEqual(
                ["launch", "driver_restart", "launch", "complete"],
                [row["event"] for row in timeline])
            requested.assert_called()


class DurableRestartRequestTest(unittest.TestCase):
    def test_request_is_token_bound_and_cleared_by_the_fresh_launch(self):
        with tempfile.TemporaryDirectory() as root:
            progress_path = os.path.join(root, "campaign_progress.json")
            heartbeat_path = os.path.join(root, "campaign_heartbeat.json")
            progress = campaign_driver.Progress(
                progress_path, heartbeat_path)
            progress.load_or_init(
                "restart", [SEED], "random-legal", "A" * 64,
                policy_seed=1234)

            progress.request_restart("launch-one", "unit-test")

            self.assertTrue(orchestrator.restart_requested_for_launch(
                progress.data, "launch-one"))
            self.assertFalse(orchestrator.restart_requested_for_launch(
                progress.data, "launch-two"))

            resumed = campaign_driver.Progress(
                progress_path, heartbeat_path)
            resumed.load_or_init(
                "restart", [SEED], "random-legal", "A" * 64,
                policy_seed=1234)
            self.assertEqual(2, resumed.data["launches"])
            self.assertNotIn("restart_requested", resumed.data)


class UnexpectedDriverExitRestartTest(unittest.TestCase):
    def test_post_init_exceptions_request_a_durable_restart(self):
        cases = [
            (RuntimeError("synthetic unexpected failure"),
             campaign_driver.EXIT_FATAL, "unexpected_driver_exception"),
            (campaign_driver.GameGone("synthetic uncaught pipe loss"),
             campaign_driver.EXIT_GAME_GONE, "uncaught_game_gone"),
        ]
        for raised, expected_exit, expected_reason in cases:
            with self.subTest(reason=expected_reason), \
                 tempfile.TemporaryDirectory() as root:
                args = SimpleNamespace(
                    campaign_id="unexpected_restart",
                    seeds=SEED,
                    data_root=root,
                    policy="random-legal",
                    script=None,
                    script_dir=None,
                    launch_token="unexpected-launch-token",
                )
                progress = mock.Mock()
                progress.data = {"status": "in_progress"}
                driver = mock.Mock()
                driver.progress = progress
                driver.run.side_effect = raised

                with mock.patch.object(
                        campaign_driver, "parse_args",
                        return_value=args), \
                     mock.patch.object(
                        campaign_driver, "CampaignDriver",
                        return_value=driver):
                    result = campaign_driver.main([])

                self.assertEqual(expected_exit, result)
                progress.request_restart.assert_called_once_with(
                    "unexpected-launch-token", expected_reason)


class ArtifactOracleRequirementTest(unittest.TestCase):
    def _validate(self, records, require_oracle=False,
                  require_encounter_lists=False):
        with tempfile.TemporaryDirectory() as root:
            path = os.path.join(root, f"run_{SEED}_a20_ironclad.jsonl")
            _write_artifact(path, records)
            return validate_artifacts.validate_file(
                path, require_oracle=require_oracle,
                require_encounter_lists=require_encounter_lists)[0]

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

    def test_b52_encounter_list_requirement_is_explicit_and_structural(self):
        old_oracle = _oracle()
        errors = self._validate(
            [_header(True), _action(old_oracle), _terminal()],
            require_oracle=True, require_encounter_lists=True)
        self.assertIn("encounterLists is missing",
                      "\n".join(errors))

        new_oracle = _oracle()
        new_oracle["encounterLists"] = {
            "monster": ["Jaw Worm"], "elite": ["Gremlin Nob"],
            "boss": ["The Guardian"],
        }
        errors = self._validate(
            [_header(True), _action(new_oracle), _terminal()],
            require_oracle=True, require_encounter_lists=True)
        self.assertEqual([], errors)

        new_oracle["encounterLists"]["unknown"] = []
        errors = self._validate(
            [_header(True), _action(new_oracle), _terminal()],
            require_oracle=True, require_encounter_lists=True)
        self.assertIn("extra=['unknown']", "\n".join(errors))

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
            # b1.7.0: the routine claims the row (`choose 0`) and then emits the
            # `proceed` that opens the boss chest, handing control back to the
            # main loop rather than terminating. So it costs TWO actions and
            # reports no stop, and the TERMINAL record is the caller's.
            state, actions, stop = driver._claim_boss_reward(
                logger, timing, first, SEED, 0)
            self.assertIsNone(stop)
            self.assertEqual(2, actions)
            logger.terminal("death", state.get("game_state") or {}, actions)
            logger.close()
            timing.close()

            done = _done()
            done.update({
                "outcome": "death",
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
    def test_launch_game_inherits_binding_and_creates_log_exclusively(self):
        with tempfile.TemporaryDirectory() as data_root:
            campaign_id = "bound"
            os.makedirs(os.path.join(data_root, campaign_id))
            args = SimpleNamespace(
                game_dir=os.path.join(data_root, "game"),
                mts_jar=os.path.join(data_root, "ModTheSpire.jar"),
                data_root=data_root,
                campaign_id=campaign_id,
                launch_log="mts_launch1.log",
                launch_token="one-use-test-binding",
            )
            os.makedirs(args.game_dir)
            sentinel = object()
            with mock.patch.object(
                    orchestrator.subprocess, "Popen",
                    return_value=sentinel) as popen:
                self.assertIs(sentinel, orchestrator.launch_game(args, 1))
            kwargs = popen.call_args.kwargs
            self.assertEqual(
                args.launch_token,
                kwargs["env"][campaign_paths.ORACLE_LAUNCH_TOKEN_ENV])
            self.assertTrue(kwargs["stdout"].closed)
            launch_path = os.path.join(
                data_root, campaign_id, args.launch_log)
            self.assertTrue(os.path.isfile(launch_path))

            with self.assertRaises(FileExistsError), \
                    mock.patch.object(
                        orchestrator.subprocess, "Popen") as second_popen:
                orchestrator.launch_game(args, 1)
            second_popen.assert_not_called()

    def test_private_runtime_launch_uses_canonical_java_and_isolated_process(self):
        with tempfile.TemporaryDirectory() as data_root:
            campaign_id = "private"
            campaign_dir = os.path.join(data_root, campaign_id)
            os.makedirs(campaign_dir)
            game_dir = os.path.join(data_root, "canonical-game")
            workdir = os.path.join(data_root, "runtime", "game")
            local_app_data = os.path.join(data_root, "runtime", "local")
            app_data = os.path.join(data_root, "runtime", "roaming")
            temp_dir = os.path.join(data_root, "runtime", "tmp")
            for path in (game_dir, workdir, local_app_data, app_data,
                         temp_dir, os.path.join(workdir, "mods")):
                os.makedirs(path, exist_ok=True)
            private_fork = os.path.join(
                workdir, "mods", "CommunicationMod-oracle.jar")
            with open(private_fork, "wb") as fh:
                fh.write(b"private fork")
            args = SimpleNamespace(
                game_dir=game_dir,
                mts_jar=os.path.join(data_root, "ModTheSpire.jar"),
                data_root=data_root,
                campaign_id=campaign_id,
                launch_log="mts_launch1.log",
                launch_token="private-launch-binding",
                private_runtime=True,
                effective_workdir=workdir,
                runtime_local_app_data=local_app_data,
                runtime_app_data=app_data,
                runtime_temp_dir=temp_dir,
                runtime_fork_jar=private_fork,
                java_xms_mib=384,
                java_xmx_mib=1536,
            )
            proc = mock.Mock()
            job = mock.Mock()
            with mock.patch.object(
                    orchestrator.subprocess, "Popen",
                    return_value=proc) as popen, \
                    mock.patch.object(
                        orchestrator, "_WindowsKillJob",
                        return_value=job):
                self.assertIs(proc, orchestrator.launch_game(args, 1))

            command = popen.call_args.args[0]
            kwargs = popen.call_args.kwargs
            self.assertEqual(
                os.path.join(game_dir, "jre", "bin", "java.exe"),
                command[0])
            self.assertIn("-Xms384m", command)
            self.assertIn("-Xmx1536m", command)
            self.assertIn(f"-Djava.io.tmpdir={temp_dir}", command)
            self.assertEqual(workdir, kwargs["cwd"])
            self.assertEqual(local_app_data, kwargs["env"]["LOCALAPPDATA"])
            self.assertEqual(app_data, kwargs["env"]["APPDATA"])
            self.assertEqual(temp_dir, kwargs["env"]["TEMP"])
            self.assertEqual(temp_dir, kwargs["env"]["TMP"])
            self.assertEqual(
                args.launch_token,
                kwargs["env"][campaign_paths.ORACLE_LAUNCH_TOKEN_ENV])
            self.assertEqual(
                os.path.join(kwargs["cwd"], "mods",
                             "CommunicationMod-oracle.jar"),
                args.runtime_fork_jar)
            self.assertIs(job, proc._oracle_kill_job)

    def test_seed_capacity_recycles_once_and_preserves_completed_ledger(self):
        with tempfile.TemporaryDirectory() as data_root:
            campaign_id = "capacity"
            os.makedirs(os.path.join(data_root, campaign_id))
            seeds = ["STS00001", "STS00002", "STS00003"]

            def progress(status, completed):
                return {
                    **_progress(
                        campaign_id, status=status,
                        done=[_done(seed) for seed in seeds[:completed]],
                        failed=[]),
                    "seed_list": seeds,
                }

            before = progress("in_progress", 0)
            at_capacity = progress("in_progress", 2)
            complete = progress("complete", 3)
            reads = iter([
                before, at_capacity, at_capacity, complete, complete,
            ])

            def read_json(path):
                if os.path.basename(path) == "campaign_heartbeat.json":
                    return {"t": 1.0}
                return next(reads)

            process = mock.Mock()
            process.poll.return_value = None
            local_app_data = os.path.join(data_root, "local")
            with mock.patch.dict(
                    os.environ, {"LOCALAPPDATA": local_app_data}), \
                    mock.patch.object(
                        orchestrator, "sha256_file",
                        return_value="A" * 64), \
                    mock.patch.object(
                        orchestrator, "read_json",
                        side_effect=read_json), \
                    mock.patch.object(
                        orchestrator, "launch_game",
                        return_value=process) as launch, \
                    mock.patch.object(
                        orchestrator, "cleanup_process") as cleanup, \
                    mock.patch.object(orchestrator.time, "sleep"):
                result = orchestrator.main([
                    "--data-root", data_root,
                    "--campaign-id", campaign_id,
                    "--seeds", ",".join(seeds),
                    "--seeds-per-launch", "2",
                ])

            self.assertEqual(0, result)
            self.assertEqual(2, launch.call_count)
            self.assertEqual(2, cleanup.call_count)
            with open(os.path.join(
                    data_root, campaign_id, "orchestrator_timeline.json"),
                    "r", encoding="utf-8") as fh:
                timeline = json.load(fh)["timeline"]
            self.assertEqual(
                ["launch", "capacity_recycle", "launch", "complete"],
                [row["event"] for row in timeline])
            self.assertEqual(2, timeline[1]["completed_this_launch"])
            self.assertEqual(2, timeline[2]["done_at_launch"])
            self.assertEqual(3, timeline[3]["done"])

    def test_launch_indices_resume_numerically_without_overwriting(self):
        with tempfile.TemporaryDirectory() as data_root:
            campaign_id = "resume"
            campaign_dir = os.path.join(data_root, campaign_id)
            os.makedirs(campaign_dir)
            for index in (1, 2, 12):
                with open(os.path.join(
                        campaign_dir, f"mts_launch{index}.log"),
                        "w", encoding="utf-8"):
                    pass
            _write_campaign_json(
                os.path.join(campaign_dir, "campaign_progress.json"),
                _progress(
                    campaign_id, status="in_progress",
                    done=[], failed=[]))

            seen = {}

            def launch_and_report_fatal(args, launch_idx):
                seen["index"] = launch_idx
                seen["log"] = args.launch_log
                seen["token"] = args.launch_token
                _write_campaign_json(
                    os.path.join(campaign_dir, "campaign_progress.json"),
                    {
                        **_progress(
                            campaign_id, status="fatal_environment_drift",
                            done=[], failed=[]),
                        "fatal": {"message": "synthetic stop"},
                    })
                return object()

            local_app_data = os.path.join(data_root, "local")
            diagnostics = io.StringIO()
            with redirect_stdout(diagnostics), \
                    mock.patch.dict(
                        os.environ, {"LOCALAPPDATA": local_app_data}), \
                    mock.patch.object(
                        orchestrator, "sha256_file", return_value="A" * 64), \
                    mock.patch.object(
                        orchestrator, "launch_game",
                        side_effect=launch_and_report_fatal), \
                    mock.patch.object(orchestrator, "kill_tree"), \
                    mock.patch.object(orchestrator.time, "sleep"):
                result = orchestrator.main([
                    "--data-root", data_root,
                    "--campaign-id", campaign_id,
                    "--seeds", SEED,
                ])

            self.assertEqual(orchestrator.EXIT_FATAL_ENVIRONMENT, result)
            self.assertEqual(13, seen["index"])
            self.assertEqual("mts_launch13.log", seen["log"])
            self.assertRegex(seen["token"], r"\A[0-9a-f]{64}\Z")
            self.assertNotIn(seen["token"], diagnostics.getvalue())
            with open(os.path.join(
                    local_app_data, "ModTheSpire", "CommunicationMod",
                    "config.properties"), encoding="utf-8") as fh:
                config = fh.read()
            self.assertIn("--launch-log mts_launch13.log", config)
            self.assertIn(f"--launch-token {seen['token']}", config)
            for index in (1, 2, 12):
                self.assertTrue(os.path.exists(os.path.join(
                    campaign_dir, f"mts_launch{index}.log")))

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

    def test_progress_refuses_resume_across_driver_revision(self):
        with tempfile.TemporaryDirectory() as root:
            path = os.path.join(root, "campaign_progress.json")
            heartbeat = os.path.join(root, "campaign_heartbeat.json")
            stored = _progress("strict")
            stored["driver_version"] = "b1.4.3"
            _write_campaign_json(path, stored)
            progress = campaign_driver.Progress(path, heartbeat)
            with self.assertRaisesRegex(
                    campaign_driver.CampaignIdentityError, "driver_version"):
                progress.load_or_init(
                    "strict", [SEED], "random-legal", "A" * 64)

    def test_policy_seed_is_recorded_in_campaign_progress(self):
        """Design 7.5 gap, ledger half: a fresh campaign_progress.json must
        carry policy_seed so a greedy campaign is reproducible from its own
        artifacts (the tie-break RNG decides real choices)."""
        with tempfile.TemporaryDirectory() as root:
            path = os.path.join(root, "campaign_progress.json")
            heartbeat = os.path.join(root, "campaign_heartbeat.json")
            progress = campaign_driver.Progress(path, heartbeat)
            data = progress.load_or_init(
                "greedyseed", [SEED], "greedy", "A" * 64, policy_seed=918273)
            self.assertEqual(918273, data["policy_seed"])
            with open(path, encoding="utf-8") as fh:
                on_disk = json.load(fh)
            self.assertEqual(918273, on_disk["policy_seed"])

    def test_policy_seed_mismatch_on_resume_is_a_caught_identity_error(self):
        """A resumed campaign whose stored policy_seed disagrees with the
        one it's being resumed with must refuse, same as policy/seed_list."""
        with tempfile.TemporaryDirectory() as root:
            path = os.path.join(root, "campaign_progress.json")
            heartbeat = os.path.join(root, "campaign_heartbeat.json")
            stored = _progress("strict")
            stored["policy_seed"] = 111
            _write_campaign_json(path, stored)
            progress = campaign_driver.Progress(path, heartbeat)
            with self.assertRaisesRegex(
                    campaign_driver.CampaignIdentityError, "policy_seed"):
                progress.load_or_init(
                    "strict", [SEED], "random-legal", "A" * 64,
                    policy_seed=222)

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
                policy_seed=1234,
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
                policy_seed=1234,
                data_root=root,
                max_attempts=1,
                launch_token="retry-launch-token",
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
            with open(os.path.join(
                    campaign_dir, "campaign_progress.json"),
                    "r", encoding="utf-8") as fh:
                after_pipe_loss = json.load(fh)
            self.assertTrue(orchestrator.restart_requested_for_launch(
                after_pipe_loss, "retry-launch-token"))
            self.assertEqual(
                "game_gone_mid_run",
                after_pipe_loss["restart_requested"]["reason"])

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


# --- greedy policy (B4.x live depth policy) --------------------------------
#
# What these cover: the scorer is a pure function of a parsed protocol dump, so
# every case below is a hand-built dump. Legality is NOT asserted here (it is a
# property of expand_legal_actions, proved against real captures by
# GreedyReplayLegalityTest); these pin the PREFERENCES.

_DRIVER_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.abspath(os.path.join(_DRIVER_DIR, "..", "..", ".."))
_REGISTRY_CARDS = os.path.join(_REPO_ROOT, "registry", "cards.yaml")
_SIDE_TABLE = os.path.join(_DRIVER_DIR, "cards_sidetable.json")

# Recorded random-legal captures, used read-only by the replay harness. They are
# campaign artifacts and therefore live outside the repo (conventions 2 / design
# 7.3), so their absence skips rather than fails.
_CAPTURE_DIR = os.environ.get(
    "STS_ORACLE_CAPTURE_DIR",
    r"D:\STS_BG_Mod\_oracle_data\campaigns"
    r"\b47_treasure_oracle_20260727T204809Z_claude01")


def _card(card_id, cost=1, upgrades=0, has_target=True, card_type="ATTACK"):
    return {"id": card_id, "is_playable": True, "has_target": has_target,
            "cost": cost, "upgrades": upgrades, "type": card_type}


def _monster(hp, intent="ATTACK", adjusted=10, block=0, gone=False):
    return {"current_hp": hp, "max_hp": 50, "intent": intent,
            "move_adjusted_damage": adjusted, "block": block,
            "is_gone": gone, "id": "JawWorm"}


def _combat(hand, monsters, player_block=0, available=("play", "end")):
    return {
        "available_commands": list(available),
        "ready_for_command": True,
        "in_game": True,
        "game_state": {
            "screen_type": "NONE",
            "room_phase": "COMBAT",
            "current_hp": 60, "max_hp": 80,
            "combat_state": {
                "hand": hand,
                "monsters": monsters,
                "player": {"block": player_block, "energy": 3,
                           "current_hp": 60, "max_hp": 80},
            },
        },
    }


def _screen(screen_type, choice_list, screen_state=None,
            available=("choose",), **game_state):
    gs = {"screen_type": screen_type, "choice_list": list(choice_list),
          "current_hp": 60, "max_hp": 80}
    if screen_state is not None:
        gs["screen_state"] = screen_state
    gs.update(game_state)
    return {"available_commands": list(available), "ready_for_command": True,
            "in_game": True, "game_state": gs}


def _pick(state, table=None, rng=None):
    """expand -> greedy pick, exactly as CampaignDriver._policy_command does."""
    actions = campaign_driver.expand_legal_actions(state, random.Random(0))
    return greedy_policy.pick(actions, state, table, rng)


def _deck(attacks=6, others=4, attack_id="Strike_R", other_id="Defend_R"):
    """A run deck with a known ATTACK census -- b1.5.0 R1's gate reads this.

    The Ironclad opener is exactly (6, 4): 5 Strikes + Bash against 4 Defends.
    """
    return ([{"id": attack_id, "type": "ATTACK", "upgrades": 0}] * attacks
            + [{"id": other_id, "type": "SKILL", "upgrades": 0}] * others)


def _potion(pid="FirePotion", can_use=True, requires_target=False):
    return {"id": pid, "name": pid, "can_use": can_use,
            "can_discard": True, "requires_target": requires_target}


_EMPTY_SLOT = {"id": "Potion Slot", "name": "Potion Slot",
               "can_use": False, "can_discard": False,
               "requires_target": False}


class GreedyCombatScoringTest(unittest.TestCase):
    def setUp(self):
        self.table = greedy_policy.load_side_table(_SIDE_TABLE)
        self.assertTrue(self.table, "the committed side table must load")

    def test_lethal_pick_targets_the_monster_it_can_finish(self):
        """Strike kills the 5-HP monster; the 30-HP one is just damage."""
        state = _combat([_card("Strike_R")],
                        [_monster(30, intent="ATTACK"),
                         _monster(5, intent="ATTACK")])
        self.assertEqual("play 1 1", _pick(state, self.table))

    def test_block_is_counted_through_a_monsters_block_before_lethal(self):
        """6 damage does not kill 5 HP behind 4 block -- no lethal bonus."""
        state = _combat([_card("Strike_R")],
                        [_monster(30, intent="ATTACK"),
                         _monster(5, intent="ATTACK", block=4)])
        # Still targets slot 1 (focus fire on the weakest), but only by the +2
        # tie-break, not by the +400 lethal bonus.
        self.assertEqual(
            greedy_policy.score_action("play 1 1", state, self.table),
            greedy_policy.score_action("play 1 0", state, self.table) + 2)

    def test_block_wins_while_a_monster_intends_an_attack(self):
        state = _combat([_card("Defend_R", has_target=False,
                               card_type="SKILL"),
                         _card("Strike_R")],
                        [_monster(50, intent="ATTACK", adjusted=12)])
        self.assertEqual("play 1", _pick(state, self.table))

    def test_damage_wins_again_once_nothing_is_attacking(self):
        state = _combat([_card("Defend_R", has_target=False,
                               card_type="SKILL"),
                         _card("Strike_R")],
                        [_monster(50, intent="BUFF", adjusted=-1)])
        self.assertEqual("play 2 0", _pick(state, self.table))

    def test_lethal_outranks_blocking_even_under_attack(self):
        state = _combat([_card("Defend_R", has_target=False,
                               card_type="SKILL"),
                         _card("Strike_R")],
                        [_monster(4, intent="ATTACK", adjusted=12)])
        self.assertEqual("play 2 0", _pick(state, self.table))

    def test_end_turn_is_strictly_below_every_playable_card(self):
        state = _combat([_card("Flex", has_target=False, cost=0,
                               card_type="SKILL")],
                        [_monster(50, intent="ATTACK")])
        self.assertEqual("play 1", _pick(state, self.table))
        self.assertLess(greedy_policy.score_action("end", state, self.table),
                        greedy_policy.score_action("play 1", state,
                                                   self.table))

    def test_end_turn_is_taken_when_it_is_the_only_action(self):
        state = _combat([], [_monster(50)], available=("end",))
        self.assertEqual("end", _pick(state, self.table))

    def test_unknown_card_scores_as_cheap_utility_and_never_raises(self):
        """A Silent/Defect/modded id is simply not in the S1 registry."""
        state = _combat([_card("Neutralize", has_target=True, cost=0),
                         _card("Strike_R")],
                        [_monster(50, intent="BUFF", adjusted=-1)])
        self.assertEqual((0, 0), greedy_policy.score_card(
            {"id": "Neutralize"}, state, self.table))
        self.assertEqual("play 2 0", _pick(state, self.table))

    def test_missing_side_table_degrades_instead_of_failing(self):
        state = _combat([_card("Strike_R")], [_monster(50)])
        self.assertEqual({}, greedy_policy.load_side_table(
            os.path.join(_DRIVER_DIR, "no_such_side_table.json")))
        self.assertEqual("play 1 0", _pick(state, {}))

    def test_body_slam_reads_the_players_live_block(self):
        naked = _combat([_card("Body Slam")], [_monster(50)], player_block=0)
        armoured = _combat([_card("Body Slam")], [_monster(50)],
                           player_block=17)
        self.assertEqual((0, 0), greedy_policy.score_card(
            {"id": "Body Slam"}, naked, self.table))
        self.assertEqual((17, 0), greedy_policy.score_card(
            {"id": "Body Slam"}, armoured, self.table))

    def test_upgraded_cards_use_the_upgraded_column(self):
        state = _combat([_card("Cleave", has_target=False, upgrades=1)],
                        [_monster(50)])
        # Cleave+ is 11 damage, and ALL_ENEMY multiplies by the live count.
        self.assertEqual((11, 0), greedy_policy.score_card(
            {"id": "Cleave", "upgrades": 1}, state, self.table))

    def test_tie_break_is_reproducible_from_the_run_seed(self):
        """Two identical Strikes tie; the same (policy_seed, seed) must repeat."""
        state = _combat([_card("Strike_R"), _card("Strike_R")],
                        [_monster(50, intent="ATTACK")])
        first = [_pick(state, self.table, random.Random("1234:STS00041"))
                 for _ in range(8)]
        second = [_pick(state, self.table, random.Random("1234:STS00041"))
                  for _ in range(8)]
        self.assertEqual(first, second)
        self.assertTrue(all(cmd in ("play 1 0", "play 2 0") for cmd in first))


class GreedyScreenScoringTest(unittest.TestCase):
    def setUp(self):
        self.table = greedy_policy.load_side_table(_SIDE_TABLE)

    def test_map_prefers_non_combat_then_monster_then_elite(self):
        state = _screen(
            "MAP", ["x=1", "x=3", "x=5"],
            {"boss_available": False, "first_node_chosen": True,
             "next_nodes": [{"symbol": "E", "x": 1, "y": 3},
                            {"symbol": "M", "x": 3, "y": 3},
                            {"symbol": "?", "x": 5, "y": 3}]},
            available=("choose", "return"))
        self.assertEqual("choose 2", _pick(state, self.table))
        scores = [greedy_policy.score_action(f"choose {i}", state, self.table)
                  for i in range(3)]
        self.assertEqual(sorted(scores, reverse=True),
                         [scores[2], scores[1], scores[0]])

    def test_map_prefers_monster_over_elite_when_that_is_the_choice(self):
        state = _screen(
            "MAP", ["x=1", "x=3"],
            {"boss_available": False,
             "next_nodes": [{"symbol": "E"}, {"symbol": "M"}]},
            available=("choose", "return"))
        self.assertEqual("choose 1", _pick(state, self.table))

    def test_map_takes_the_boss_node_when_offered(self):
        state = _screen("MAP", ["boss"],
                        {"boss_available": True, "next_nodes": []},
                        available=("choose", "return"))
        self.assertEqual("choose 0", _pick(state, self.table))

    def test_map_never_backs_out_of_the_screen(self):
        state = _screen(
            "MAP", ["x=1"], {"boss_available": False,
                             "next_nodes": [{"symbol": "E"}]},
            available=("choose", "return"))
        self.assertEqual("choose 0", _pick(state, self.table))

    def test_treasure_reward_claims_the_relic_and_never_the_key(self):
        """The claim-order trap: the key row forfeits the linked relic.

        RewardItem.claimReward (RewardItem.java:255-330) case 6 sets
        relicLink.isDone/ignoreReward, retiring the RELIC row ungranted.
        """
        rewards = [
            {"reward_type": "RELIC",
             "relic": {"id": "Bag of Preparation", "counter": -1}},
            {"reward_type": "SAPPHIRE_KEY",
             "link": {"id": "Bag of Preparation", "counter": -1}},
        ]
        state = _screen("COMBAT_REWARD", ["relic", "sapphire_key"],
                        {"rewards": rewards},
                        available=("choose", "proceed"))
        self.assertEqual("choose 0", _pick(state, self.table))
        self.assertLess(
            greedy_policy.score_action("choose 1", state, self.table),
            greedy_policy.score_action("proceed", state, self.table),
            "the sapphire key must rank BELOW proceed so it is never claimed")

    def test_key_is_still_refused_when_it_is_the_only_row_left(self):
        state = _screen("COMBAT_REWARD", ["sapphire_key"],
                        {"rewards": [{"reward_type": "SAPPHIRE_KEY",
                                      "link": {"id": "Bag of Preparation"}}]},
                        available=("choose", "proceed"))
        self.assertEqual("proceed", _pick(state, self.table))

    def test_gold_and_potion_are_claimed_and_the_card_row_is_left(self):
        rewards = [{"reward_type": "CARD"},
                   {"reward_type": "GOLD", "gold": 18},
                   {"reward_type": "POTION",
                    "potion": {"id": "GamblersBrew"}}]
        state = _screen("COMBAT_REWARD", ["card", "gold", "potion"],
                        {"rewards": rewards},
                        available=("choose", "proceed"))
        self.assertEqual("choose 1", _pick(state, self.table))
        self.assertLess(
            greedy_policy.score_action("choose 0", state, self.table),
            greedy_policy.score_action("proceed", state, self.table),
            "skipping is always safe: the card row is never opened")

    def test_card_reward_screen_skips_rather_than_taking_an_unknown_card(self):
        state = _screen("CARD_REWARD", ["reaper", "juggernaut", "impervious"],
                        {"skip_available": True, "bowl_available": False,
                         "cards": [{"id": "Reaper"}, {"id": "Juggernaut"},
                                   {"id": "Impervious"}]},
                        available=("choose", "skip"))
        self.assertEqual("skip", _pick(state, self.table))

    def test_card_reward_still_picks_when_skipping_is_not_offered(self):
        state = _screen("CARD_REWARD", ["reaper", "juggernaut"],
                        {"skip_available": False,
                         "cards": [{"id": "Reaper"}, {"id": "Juggernaut"}]},
                        available=("choose",))
        self.assertEqual("choose 0", _pick(state, self.table))

    def test_treasure_chest_is_opened(self):
        state = _screen("CHEST", ["open"],
                        {"chest_open": False, "chest_type": "SmallChest"},
                        available=("choose", "proceed"))
        self.assertEqual("choose 0", _pick(state, self.table))

    def test_rest_beats_smith_when_hurt_and_loses_when_healthy(self):
        hurt = _screen("REST", ["rest", "smith", "recall"],
                       {"has_rested": False,
                        "rest_options": ["rest", "smith", "recall"]},
                       current_hp=30, max_hp=80)
        healthy = _screen("REST", ["rest", "smith", "recall"],
                          {"has_rested": False,
                           "rest_options": ["rest", "smith", "recall"]},
                          current_hp=78, max_hp=80)
        self.assertEqual("choose 0", _pick(hurt, self.table))
        self.assertEqual("choose 1", _pick(healthy, self.table))

    def test_shop_is_left_without_buying_or_even_entering(self):
        room = _screen("SHOP_ROOM", ["shop"], {},
                       available=("choose", "proceed"))
        self.assertEqual("proceed", _pick(room, self.table))
        screen = _screen("SHOP_SCREEN", ["purge", "pummel", "block potion"],
                         {"purge_cost": 75, "cards": [], "relics": [],
                          "potions": []},
                         available=("choose", "leave"))
        self.assertEqual("leave", _pick(screen, self.table))

    def test_event_avoids_the_costly_option_when_the_text_says_so(self):
        state = _screen(
            "EVENT",
            ["lose 7 max hp choose a rare card to obtain", "leave"],
            {"event_id": "Neow Event", "options": [
                {"choice_index": 0, "disabled": False},
                {"choice_index": 1, "disabled": False}]})
        self.assertEqual("choose 1", _pick(state, self.table))

    def test_event_falls_back_to_index_zero_when_nothing_is_identifiable(self):
        state = _screen(
            "EVENT", ["remove a card from your deck", "obtain 3 random potions"],
            {"event_id": "Neow Event", "options": [
                {"choice_index": 0, "disabled": False},
                {"choice_index": 1, "disabled": False}]})
        self.assertEqual("choose 0", _pick(state, self.table))

    def test_hand_select_picks_one_card_then_confirms(self):
        empty = _screen("HAND_SELECT", ["strike", "defend"],
                        {"selected": [], "max_cards": 1,
                         "can_pick_zero": False,
                         "hand": [{"id": "Strike_R"}, {"id": "Defend_R"}]},
                        available=("choose", "confirm"))
        self.assertEqual("choose 0", _pick(empty, self.table))
        picked = _screen("HAND_SELECT", ["defend"],
                         {"selected": [{"id": "Strike_R"}], "max_cards": 1,
                          "can_pick_zero": False,
                          "hand": [{"id": "Defend_R"}]},
                         available=("choose", "confirm"))
        self.assertEqual("proceed", _pick(picked, self.table))

    def test_grid_prefers_choose_while_confirm_is_not_up_yet(self):
        """State (a) of the GRID 2-cycle: confirm_up false, `choose` legal."""
        state = _screen("GRID", ["strike", "defend"],
                        {"cards": [{"id": "Strike_R"}, {"id": "Defend_R"}],
                         "selected_cards": [], "num_cards": 1,
                         "confirm_up": False},
                        available=("choose",))
        self.assertEqual("choose 0", _pick(state, self.table))

    def test_grid_confirm_up_wins_even_when_selected_cards_reports_empty(self):
        """The live-pilot bug: on GRID the game alternates confirm_up=false
        (with `choose` legal) and confirm_up=true (with `confirm`/`cancel`
        legal, `selected_cards` reported EMPTY even though a selection was
        made -- PROTOCOL.md 3.19 lists it as a distinct field from `choose`
        never re-arms it). Gating on confirm_up rather than the selection
        count is what makes `proceed` (the alias expand_legal_actions emits
        for `confirm`) win outright in state (b); the old selected_cards-only
        gate tied `proceed` with `cancel` at DEFAULT_CANCEL and let the
        tie-break RNG coin-flip re-open the grid. Observed live: pilot
        campaign b4x_greedy_pilot_20260728T041406Z_claude01, 8/6
        proceed/cancel over 14 decisions, 90s stalls, and one run lost to
        noop_wedge at STS00275 seq 54-59.
        """
        state = _screen("GRID", ["strike", "defend"],
                        {"cards": [], "selected_cards": [], "num_cards": 1,
                         "confirm_up": True},
                        available=("confirm", "cancel"))
        self.assertEqual("proceed", _pick(state, self.table))
        self.assertGreater(
            greedy_policy.score_action("proceed", state, self.table),
            greedy_policy.score_action("cancel", state, self.table))

    def test_random_legal_suppresses_the_confirmable_grid_cancel_noop(self):
        """B5.2 live repro STS70021/22: confirmable GRID cancel clears the
        selection but never restores readiness, costing a watchdog cycle and
        reopening the same mandatory grid. It is not a progress action."""
        grid = _screen(
            "GRID", [],
            {"cards": [], "selected_cards": [], "num_cards": 1,
             "confirm_up": True},
            available=("confirm", "cancel"))
        self.assertEqual(
            ["proceed"],
            campaign_driver.expand_legal_actions(grid, random.Random(0)))

        # The exclusion is narrow: other cancel-capable screens retain cancel.
        hand = _screen(
            "HAND_SELECT", [],
            {"selected": [{"id": "Strike_R"}], "max_cards": 1},
            available=("confirm", "cancel"))
        self.assertEqual(
            ["proceed", "cancel"],
            campaign_driver.expand_legal_actions(hand, random.Random(0)))

    def test_raw_confirm_verb_is_scored_like_proceed(self):
        """expand_legal_actions always emits the literal text `proceed` for
        an advertised `confirm`, but score_action must not silently drop a
        raw `confirm` command to its unknown-verb default of 0 -- callers
        outside the expansion (replay/triage tooling, direct scoring) can
        hand it a `confirm` string verbatim."""
        state = _screen("GRID", ["strike"],
                        {"cards": [], "selected_cards": [], "num_cards": 1,
                         "confirm_up": True},
                        available=("confirm", "cancel"))
        self.assertEqual(
            greedy_policy.score_action("proceed", state, self.table),
            greedy_policy.score_action("confirm", state, self.table))
        self.assertGreater(
            greedy_policy.score_action("confirm", state, self.table), 0)


class CardSideTableTest(unittest.TestCase):
    """The committed JSON must still be what registry/cards.yaml derives.

    This is the sync check a registry_gen emit module would have given for free.
    """

    def test_committed_table_matches_the_registry(self):
        try:
            import yaml
        except ImportError:  # pragma: no cover - dev machines all have it
            self.skipTest("PyYAML absent; the side table is dev-time generated")
        if not os.path.exists(_REGISTRY_CARDS):
            self.skipTest(f"{_REGISTRY_CARDS} not present")
        with open(_REGISTRY_CARDS, "r", encoding="utf-8") as fh:
            cards = yaml.safe_load(fh)
        expected = gen_cards_sidetable.build_document(_REGISTRY_CARDS, cards)
        with open(_SIDE_TABLE, "r", encoding="utf-8") as fh:
            committed = json.load(fh)
        self.assertEqual(
            expected, committed,
            "cards_sidetable.json is stale -- regenerate with "
            "gen_cards_sidetable.py (see its _provenance.regenerate)")

    def test_provenance_names_its_source_and_the_mirrored_scorer(self):
        with open(_SIDE_TABLE, "r", encoding="utf-8") as fh:
            document = json.load(fh)
        provenance = document["_provenance"]
        self.assertEqual("registry/cards.yaml", provenance["source"])
        self.assertIn("policy.cpp", provenance["mirrors"])
        self.assertRegex(provenance["source_sha256"], r"\A[0-9A-F]{64}\Z")
        self.assertEqual(len(document["cards"]), provenance["source_rows"])

    def test_starter_card_numbers_are_the_registry_numbers(self):
        # Upgraded columns per the Java: Strike+ 9 (Strike_Red.java:57-62),
        # Defend+ 8 (Defend_Red.java:43-48), Bash+ 10 (Bash.java:54-60). These
        # once pinned base==upgraded, which was the G6 §8.0 registry gap.
        table = greedy_policy.load_side_table(_SIDE_TABLE)
        self.assertEqual([6, 9], table["Strike_R"]["damage"])
        self.assertEqual([5, 8], table["Defend_R"]["block"])
        self.assertEqual([8, 10], table["Bash"]["damage"])
        self.assertTrue(table["Cleave"]["aoe"])
        self.assertTrue(table["Body Slam"]["damage_from_block"])


# --- argument-level validation (script mode) -------------------------------

class CommandArgumentValidationTest(unittest.TestCase):
    def test_in_range_choose_and_play_pass(self):
        state = _screen("MAP", ["x=1", "x=3"], {"next_nodes": []})
        self.assertEqual((True, ""),
                         campaign_driver.cmd_args_ready(state, "choose 1"))
        combat = _combat([_card("Strike_R"), _card("Defend_R")],
                         [_monster(20), _monster(20)])
        self.assertEqual((True, ""),
                         campaign_driver.cmd_args_ready(combat, "play 2 1"))

    def test_out_of_range_choose_is_a_divergence(self):
        state = _screen("MAP", ["x=1", "x=3"], {"next_nodes": []})
        ok, why = campaign_driver.cmd_args_ready(state, "choose 3")
        self.assertFalse(ok)
        self.assertIn("choice_list has 2 option(s)", why)

    def test_named_choose_passes_through_untouched(self):
        """PROTOCOL.md 2: `choose` matches the exact choice string first."""
        state = _screen("REST", ["rest", "smith"], {})
        self.assertEqual((True, ""),
                         campaign_driver.cmd_args_ready(state, "choose smith"))
        self.assertEqual(
            (True, ""),
            campaign_driver.cmd_args_ready(state, "choose sapphire_key"))

    def test_play_slot_and_target_are_both_range_checked(self):
        combat = _combat([_card("Strike_R")], [_monster(20)])
        bad_slot, why_slot = campaign_driver.cmd_args_ready(combat, "play 4")
        self.assertFalse(bad_slot)
        self.assertIn("hand holds 1 card(s)", why_slot)
        bad_target, why_target = campaign_driver.cmd_args_ready(
            combat, "play 1 2")
        self.assertFalse(bad_target)
        self.assertIn("room holds 1 monster(s)", why_target)
        self.assertFalse(
            campaign_driver.cmd_args_ready(combat, "play 11")[0])

    def test_slot_zero_means_the_tenth_card(self):
        ten = _combat([_card("Strike_R")] * 10, [_monster(20)])
        nine = _combat([_card("Strike_R")] * 9, [_monster(20)])
        self.assertEqual((True, ""),
                         campaign_driver.cmd_args_ready(ten, "play 0 0"))
        self.assertFalse(campaign_driver.cmd_args_ready(nine, "play 0 0")[0])

    def test_absent_collections_are_never_reported_as_a_divergence(self):
        bare = {"available_commands": ["choose"], "in_game": True,
                "game_state": {"screen_type": "EVENT"}}
        self.assertEqual((True, ""),
                         campaign_driver.cmd_args_ready(bare, "choose 4"))
        self.assertEqual((True, ""),
                         campaign_driver.cmd_args_ready(bare, "play 3 1"))
        self.assertEqual((True, ""),
                         campaign_driver.cmd_args_ready(bare, "proceed"))

    def test_verb_gate_alone_would_have_let_the_bad_index_through(self):
        """The exact hole this closes: the verb is legal, the index is not."""
        state = _screen("MAP", ["x=1", "x=3"], {"next_nodes": []})
        self.assertTrue(campaign_driver.cmd_verb_ready(state, "choose 3"))
        self.assertFalse(campaign_driver.cmd_args_ready(state, "choose 3")[0])

    def test_script_mode_ends_the_run_as_cmd_arg_invalid(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_id = "argcheck"
            _write_launch_log(os.path.join(root, campaign_id))
            state = _action(_oracle())["state_json"]
            state["available_commands"] = ["choose", "proceed"]
            state["game_state"]["choice_list"] = ["gold"]
            state["game_state"]["screen_type"] = "COMBAT_REWARD"
            driver = _stack_driver(root, campaign_id, state=state,
                                   max_actions=10)
            driver.args.policy = "script"
            driver.args.script = None
            driver.args.script_dir = None
            driver.args.max_settle = 4
            driver.args.settle_sleep = 0.0
            driver.single_script = ["choose 3"]

            with mock.patch.dict(
                    os.environ,
                    {campaign_paths.ORACLE_LAUNCH_TOKEN_ENV:
                     "unit-test-launch-token"}):
                outcome, _floor, actions, menu_ok = driver.run_seed(SEED, 1)

            self.assertEqual(("cmd_arg_invalid", 0, False),
                             (outcome, actions, menu_ok))
            artifact = os.path.join(
                root, campaign_id, f"run_{SEED}_a20_ironclad.jsonl")
            with open(artifact, encoding="utf-8") as fh:
                records = [json.loads(line) for line in fh if line.strip()]
            self.assertEqual("terminal", records[-1]["record_kind"])
            self.assertEqual("cmd_arg_invalid", records[-1]["outcome"])
            # The command must NOT have been sent: only `start` was.
            self.assertEqual([f"start ironclad 20 {SEED}"],
                             driver.stepper.commands)

    def test_script_mode_still_runs_an_in_range_command(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_id = "argok"
            _write_launch_log(os.path.join(root, campaign_id))
            state = _action(_oracle())["state_json"]
            state["available_commands"] = ["choose", "proceed"]
            state["game_state"]["choice_list"] = ["gold"]
            state["game_state"]["screen_type"] = "COMBAT_REWARD"
            driver = _stack_driver(root, campaign_id, state=state,
                                   max_actions=1)
            driver.args.policy = "script"
            driver.args.script = None
            driver.args.script_dir = None
            driver.args.max_settle = 4
            driver.args.settle_sleep = 0.0
            driver.single_script = ["choose 0"]
            driver.stepper.states.append(state)

            with mock.patch.dict(
                    os.environ,
                    {campaign_paths.ORACLE_LAUNCH_TOKEN_ENV:
                     "unit-test-launch-token"}):
                outcome, _floor, actions, _menu = driver.run_seed(SEED, 1)

            self.assertEqual(("action_cap", 1), (outcome, actions))
            self.assertIn("choose 0", driver.stepper.commands)

    def test_terminal_outcome_still_satisfies_the_artifact_validator(self):
        """validate_artifacts has no outcome whitelist -- prove it, don't assume."""
        with tempfile.TemporaryDirectory() as root:
            path = os.path.join(root, f"run_{SEED}_a20_ironclad.jsonl")
            _write_artifact(path, [
                _header(True), _action(_oracle()),
                _terminal(outcome="cmd_arg_invalid")])
            errors, _actions = validate_artifacts.validate_file(
                path, require_oracle=True)
            self.assertEqual([], errors)


# --- replay harness: legality against real recorded states -----------------

class GreedyReplayLegalityTest(unittest.TestCase):
    """Run recorded captures through the greedy policy and check LEGALITY.

    The policy will disagree with what random-legal actually played -- that is
    the point of it existing. What must hold on every real state is that the
    command it would emit is one the game itself advertised, with in-range
    arguments: exactly the invariant that random-legal satisfied for free and
    that a hand-written script does not (see cmd_args_ready).
    """

    MIN_RUNS = 2
    MIN_STATES = 100

    def _runs(self):
        paths = sorted(glob.glob(os.path.join(
            _CAPTURE_DIR, "run_*_a20_ironclad.jsonl")))
        if len(paths) < self.MIN_RUNS:
            self.skipTest(
                f"need >= {self.MIN_RUNS} recorded runs under {_CAPTURE_DIR} "
                "(campaign artifacts live outside the repo, design 7.3)")
        return paths

    def test_every_greedy_command_is_legal_on_every_recorded_state(self):
        table = greedy_policy.load_side_table(_SIDE_TABLE)
        self.assertTrue(table)
        paths = self._runs()
        checked = 0
        emitted = 0
        screens = set()
        for path in paths:
            rng = random.Random(f"1234:{os.path.basename(path)}")
            with open(path, encoding="utf-8") as fh:
                for line in fh:
                    record = json.loads(line)
                    if record.get("record_kind") != "action":
                        continue
                    state = record.get("state_json") or {}
                    if not state.get("in_game"):
                        continue
                    checked += 1
                    gs = state.get("game_state") or {}
                    screens.add(gs.get("screen_type"))
                    actions = campaign_driver.expand_legal_actions(state, rng)
                    command = greedy_policy.pick(actions, state, table, rng)
                    if command is None:
                        self.assertEqual([], actions)
                        continue
                    emitted += 1
                    self.assertIn(command, actions, f"{path}: {command!r}")
                    self.assertTrue(
                        campaign_driver.cmd_verb_ready(state, command),
                        f"{path}: verb of {command!r} is not advertised "
                        f"({state.get('available_commands')})")
                    ok, why = campaign_driver.cmd_args_ready(state, command)
                    self.assertTrue(ok, f"{path}: {command!r} -- {why}")
        self.assertGreaterEqual(checked, self.MIN_STATES)
        self.assertGreaterEqual(emitted, self.MIN_STATES // 2)
        # The states must actually cover the screens the policy has rules for,
        # or this proves nothing about them.
        for screen in ("MAP", "COMBAT_REWARD", "CARD_REWARD", "EVENT"):
            self.assertIn(screen, screens)

    def test_replay_disagrees_with_random_legal_but_stays_inside_it(self):
        """A sanity check that the policy is doing something at all."""
        table = greedy_policy.load_side_table(_SIDE_TABLE)
        agreements = 0
        decisions = 0
        for path in self._runs():
            rng = random.Random(4321)
            with open(path, encoding="utf-8") as fh:
                for line in fh:
                    record = json.loads(line)
                    if record.get("record_kind") != "action":
                        continue
                    state = record.get("state_json") or {}
                    actions = campaign_driver.expand_legal_actions(state, rng)
                    if len(actions) < 2:
                        continue
                    command = greedy_policy.pick(actions, state, table, rng)
                    decisions += 1
                    if command == record.get("action_command"):
                        agreements += 1
        self.assertGreater(decisions, 0)
        self.assertLess(agreements, decisions,
                        "greedy that never diverges from random-legal is not "
                        "a policy")


# --- b1.5.0: the three Act-1 boss rules ------------------------------------
#
# Evidence they were derived from (greedy_policy module docstring has the
# citations): six STS01221 captures under six policy seeds, every one of which
# drove the Slime Boss to its 50 % split threshold (SlimeBoss.java:175) with a
# 12-14 card starter deck and an empty or one-potion belt, and died to the two
# large slimes the split leaves behind (SlimeBoss.java:155-156).

class GreedyCardRewardGateTest(unittest.TestCase):
    """R1 -- the deck-only gate on card rewards."""

    def setUp(self):
        self.table = greedy_policy.load_side_table(_SIDE_TABLE)
        self.assertTrue(self.table, "the committed side table must load")

    @staticmethod
    def _combat_reward(deck):
        return _screen("COMBAT_REWARD", ["gold", "card"],
                       {"rewards": [{"reward_type": "GOLD", "gold": 18},
                                    {"reward_type": "CARD"}]},
                       available=("choose", "proceed"), deck=deck)

    @staticmethod
    def _card_screen(cards, deck, choices=None):
        return _screen("CARD_REWARD",
                       choices or [c.get("id", "?") for c in cards],
                       {"skip_available": True, "bowl_available": False,
                        "cards": cards},
                       available=("choose", "skip"), deck=deck)

    # -- the gate itself ---------------------------------------------------

    def test_starter_deck_wants_a_card_and_a_stocked_deck_does_not(self):
        opener = _screen("COMBAT_REWARD", [], {}, deck=_deck(6, 4))
        self.assertTrue(greedy_policy.wants_card_reward(opener, self.table))
        stocked = _screen("COMBAT_REWARD", [], {}, deck=_deck(10, 4))
        self.assertFalse(greedy_policy.wants_card_reward(stocked, self.table))

    def test_deck_size_cap_closes_the_gate_even_while_attacks_are_short(self):
        big = _screen("COMBAT_REWARD", [], {}, deck=_deck(6, 14))
        self.assertEqual(6, greedy_policy.deck_attack_count(big, self.table))
        self.assertEqual(20, len(big["game_state"]["deck"]))
        self.assertFalse(greedy_policy.wants_card_reward(big, self.table),
                         "a 20-card deck stops taking regardless of census")

    def test_absent_deck_keeps_the_pre_b150_behaviour(self):
        blind = _screen("COMBAT_REWARD", [], {})
        self.assertIsNone(greedy_policy.deck_attack_count(blind, self.table))
        self.assertFalse(greedy_policy.wants_card_reward(blind, self.table))

    def test_census_reads_the_dumps_own_type_for_an_unregistered_card(self):
        deck = _deck(3, 4) + [{"id": "ModdedSlash", "type": "ATTACK"}] * 7
        state = _screen("COMBAT_REWARD", [], {}, deck=deck)
        self.assertEqual(10, greedy_policy.deck_attack_count(state, self.table))
        self.assertFalse(greedy_policy.wants_card_reward(state, self.table))

    # -- COMBAT_REWARD: open the row, and only then -----------------------

    def test_open_gate_lifts_the_card_row_above_proceed(self):
        state = self._combat_reward(_deck(6, 4))
        self.assertGreater(
            greedy_policy.score_action("choose 1", state, self.table),
            greedy_policy.score_action("proceed", state, self.table),
            "a starter deck must open the card row")

    def test_closed_gate_leaves_the_card_row_exactly_as_before(self):
        state = self._combat_reward(_deck(10, 4))
        self.assertEqual(
            greedy_policy.REWARD_CARD,
            greedy_policy.score_action("choose 1", state, self.table))
        self.assertLess(
            greedy_policy.score_action("choose 1", state, self.table),
            greedy_policy.score_action("proceed", state, self.table))

    def test_gold_is_still_claimed_before_the_card_row(self):
        state = self._combat_reward(_deck(6, 4))
        self.assertEqual("choose 0", _pick(state, self.table))

    def test_open_gate_does_not_disturb_the_sapphire_key_order(self):
        """The never-claim-key property is orthogonal and must stay orthogonal."""
        rewards = [{"reward_type": "RELIC",
                    "relic": {"id": "Bag of Preparation"}},
                   {"reward_type": "SAPPHIRE_KEY",
                    "link": {"id": "Bag of Preparation"}},
                   {"reward_type": "CARD"}]
        state = _screen("COMBAT_REWARD", ["relic", "sapphire_key", "card"],
                        {"rewards": rewards},
                        available=("choose", "proceed"), deck=_deck(6, 4))
        self.assertTrue(greedy_policy.wants_card_reward(state, self.table))
        self.assertEqual("choose 0", _pick(state, self.table))
        self.assertLess(
            greedy_policy.score_action("choose 1", state, self.table),
            greedy_policy.score_action("proceed", state, self.table),
            "the key must still rank below proceed with the gate open")
        self.assertGreater(
            greedy_policy.score_action("choose 0", state, self.table),
            greedy_policy.score_action("choose 2", state, self.table),
            "the relic must still be claimed before the card row")

    # -- CARD_REWARD: which card ------------------------------------------

    def test_open_gate_takes_the_biggest_attack_over_a_big_block_skill(self):
        cards = [{"id": "Impervious", "type": "SKILL", "upgrades": 0},
                 {"id": "Heavy Blade", "type": "ATTACK", "upgrades": 0},
                 {"id": "Anger", "type": "ATTACK", "upgrades": 0}]
        state = self._card_screen(cards, _deck(6, 4))
        self.assertEqual("choose 1", _pick(state, self.table))

    def test_aoe_breaks_a_tie_between_two_equal_attacks(self):
        """The split leaves two targets, so an ALL_ENEMY attack is worth more."""
        cards = [{"id": "Pommel Strike", "type": "ATTACK", "upgrades": 0},
                 {"id": "Sword Boomerang", "type": "ATTACK", "upgrades": 0}]
        state = self._card_screen(cards, _deck(6, 4))
        plain = self.table["Pommel Strike"]["damage"][0]
        aoe = self.table["Sword Boomerang"]["damage"][0]
        self.assertEqual(plain, aoe, "this test needs equal base damage")
        self.assertEqual("choose 1", _pick(state, self.table))

    def test_upgraded_offer_is_ranked_on_its_upgraded_column(self):
        cards = [{"id": "Anger", "type": "ATTACK", "upgrades": 1},
                 {"id": "Anger", "type": "ATTACK", "upgrades": 0}]
        state = self._card_screen(cards, _deck(6, 4))
        self.assertEqual("choose 0", _pick(state, self.table))

    def test_a_curse_ranks_below_every_real_offer(self):
        cards = [{"id": "Clumsy", "type": "CURSE", "upgrades": 0},
                 {"id": "Anger", "type": "ATTACK", "upgrades": 0}]
        state = self._card_screen(cards, _deck(6, 4))
        self.assertEqual("choose 1", _pick(state, self.table))

    def test_the_singing_bowl_row_ranks_below_a_real_card(self):
        cards = [{"id": "Anger", "type": "ATTACK", "upgrades": 0}]
        state = _screen("CARD_REWARD", ["anger", "bowl"],
                        {"skip_available": True, "bowl_available": True,
                         "cards": cards},
                        available=("choose", "skip"), deck=_deck(6, 4))
        self.assertEqual("choose 0", _pick(state, self.table))

    def test_closed_gate_still_skips_the_card_screen(self):
        cards = [{"id": "Heavy Blade", "type": "ATTACK", "upgrades": 0}]
        state = self._card_screen(cards, _deck(10, 4))
        self.assertEqual("skip", _pick(state, self.table))

    def test_missing_side_table_still_takes_when_the_gate_is_open(self):
        """Degraded mode ranks everything 0 but must not open-then-skip."""
        cards = [{"id": "Heavy Blade", "type": "ATTACK", "upgrades": 0}]
        state = self._card_screen(cards, _deck(6, 4))
        self.assertEqual("choose 0", _pick(state, {}))

    # -- the anti-cycle invariant -----------------------------------------

    def test_the_two_screens_can_never_disagree(self):
        """THE load-bearing property of R1.

        `skip` does not retire a card row (b13_off20 run_STS00004 seq 30-33:
        skip -> COMBAT_REWARD still offering `card`). If the open-the-row
        decision could ever be `open` while the take-the-card decision was
        `skip`, greedy would alternate between the two screens forever with a
        signature the driver's stuck detector cannot see. Both read the same
        deck, so over every census the two answers must be the same answer.
        """
        cards = [{"id": "Anger", "type": "ATTACK", "upgrades": 0},
                 {"id": "Impervious", "type": "SKILL", "upgrades": 0}]
        seen = set()
        for attacks in range(0, 14):
            for others in range(0, 14):
                deck = _deck(attacks, others) if attacks or others else None
                reward = self._combat_reward(deck)
                screen = self._card_screen(cards, deck)
                opens = (greedy_policy.score_action("choose 1", reward,
                                                    self.table)
                         > greedy_policy.score_action("proceed", reward,
                                                      self.table))
                takes = (greedy_policy.score_action("choose 0", screen,
                                                    self.table)
                         > greedy_policy.score_action("skip", screen,
                                                      self.table))
                self.assertEqual(
                    opens, takes,
                    f"deck ({attacks} attack / {others} other): opened="
                    f"{opens} but took={takes} -- that is the 2-cycle")
                seen.add(opens)
        self.assertEqual({True, False}, seen,
                         "the sweep must cover both sides of the gate")


class GreedyPotionHoldingTest(unittest.TestCase):
    """R2 -- potions are held for the fight that matters."""

    def setUp(self):
        self.table = greedy_policy.load_side_table(_SIDE_TABLE)

    @staticmethod
    def _fight(room_type="MonsterRoom", hp=70, potions=None, hand=None):
        state = _combat(hand if hand is not None else [],
                        [_monster(40)],
                        available=("play", "end", "potion"))
        gs = state["game_state"]
        gs["room_type"] = room_type
        gs["current_hp"] = hp
        gs["potions"] = potions if potions is not None \
            else [_potion(), dict(_EMPTY_SLOT)]
        return state

    def test_an_ordinary_fight_holds_the_potion_and_ends_the_turn(self):
        state = self._fight()
        self.assertIn("potion use 0",
                      campaign_driver.expand_legal_actions(
                          state, random.Random(0)))
        self.assertEqual("end", _pick(state, self.table))

    def test_the_act1_boss_room_spends_it(self):
        state = self._fight(room_type="MonsterRoomBoss")
        self.assertEqual("potion use 0", _pick(state, self.table))

    def test_an_elite_room_spends_it(self):
        state = self._fight(room_type="MonsterRoomElite")
        self.assertEqual("potion use 0", _pick(state, self.table))

    def test_a_low_hp_fight_spends_it_anywhere(self):
        self.assertEqual("end", _pick(self._fight(hp=33), self.table))
        self.assertEqual("potion use 0", _pick(self._fight(hp=32), self.table))

    def test_a_full_belt_spends_it_rather_than_blocking_a_pickup(self):
        state = self._fight(potions=[_potion("FirePotion"),
                                     _potion("BlockPotion")])
        self.assertTrue(greedy_policy.belt_is_full(state))
        self.assertEqual("potion use 0", _pick(state, self.table))

    def test_a_belt_with_a_free_slot_is_not_full(self):
        self.assertFalse(greedy_policy.belt_is_full(self._fight()))

    def test_a_held_potion_still_outranks_nothing_but_ending_the_turn(self):
        state = self._fight()
        self.assertLess(
            greedy_policy.score_action("potion use 0", state, self.table),
            greedy_policy.score_action("end", state, self.table))

    def test_a_spent_potion_still_ranks_below_every_playable_card(self):
        state = self._fight(room_type="MonsterRoomBoss",
                            hand=[_card("Strike_R")])
        self.assertLess(
            greedy_policy.score_action("potion use 0", state, self.table),
            greedy_policy.score_action("play 1 0", state, self.table))
        self.assertEqual("play 1 0", _pick(state, self.table))

    def test_out_of_combat_potion_handling_is_unchanged(self):
        state = _screen("COMBAT_REWARD", ["gold"],
                        {"rewards": [{"reward_type": "GOLD", "gold": 9}]},
                        available=("choose", "proceed", "potion"),
                        potions=[_potion(), dict(_EMPTY_SLOT)])
        self.assertEqual(
            greedy_policy.POTION_USE_OUT_OF_COMBAT,
            greedy_policy.score_action("potion use 0", state, self.table))
        self.assertEqual(
            greedy_policy.POTION_DISCARD,
            greedy_policy.score_action("potion discard 0", state, self.table))


class GreedyMultiAttackerBlockTest(unittest.TestCase):
    """R3 -- the block weight scales with the number of banners swinging."""

    # damage 25 beats block 5 at weight 4 (25 > 20) and loses at weight 6
    # (25 < 30): the smallest state that shows the rule doing anything.
    TABLE = {
        "BigHit": {"damage": [25, 25], "block": [0, 0], "cost": 2,
                   "type": "ATTACK", "aoe": False, "damage_from_block": False},
        "SmallGuard": {"damage": [0, 0], "block": [5, 5], "cost": 1,
                       "type": "SKILL", "aoe": False,
                       "damage_from_block": False},
    }

    @staticmethod
    def _board(attackers, idle=0, hp=200):
        monsters = [_monster(hp, intent="ATTACK", adjusted=18)
                    for _ in range(attackers)]
        monsters += [_monster(hp, intent="DEBUFF", adjusted=0)
                     for _ in range(idle)]
        return _combat([_card("BigHit", cost=2),
                        _card("SmallGuard", cost=1, has_target=False)],
                       monsters)

    def test_weight_is_four_for_one_attacker_and_climbs_by_two(self):
        for attackers, expected in ((1, 4), (2, 6), (3, 8), (4, 8), (9, 8)):
            state = self._board(attackers)
            self.assertEqual(attackers,
                             greedy_policy.attacker_count(state))
            self.assertEqual(
                expected, greedy_policy.block_weight_under_attack(state),
                f"{attackers} attacker(s)")

    def test_one_attacker_still_prefers_the_bigger_hit(self):
        state = self._board(1)
        self.assertEqual("play 1 0", _pick(state, self.TABLE))

    def test_two_attackers_buy_the_turn_instead(self):
        """The Slime Boss split board: two large slimes, both swinging."""
        state = self._board(2)
        self.assertEqual("play 2", _pick(state, self.TABLE))

    def test_an_idle_second_monster_does_not_raise_the_weight(self):
        state = self._board(1, idle=1)
        self.assertEqual(1, greedy_policy.attacker_count(state))
        self.assertEqual(4, greedy_policy.block_weight_under_attack(state))
        self.assertEqual("play 1 0", _pick(state, self.TABLE))

    def test_dead_and_gone_monsters_are_not_counted(self):
        monsters = [_monster(0, intent="ATTACK", adjusted=18),
                    _monster(30, intent="ATTACK", adjusted=18, gone=True),
                    _monster(30, intent="ATTACK", adjusted=18)]
        state = _combat([_card("BigHit", cost=2)], monsters)
        self.assertEqual(1, greedy_policy.attacker_count(state))

    def test_lethal_still_outranks_block_with_two_attackers(self):
        monsters = [_monster(10, intent="ATTACK", adjusted=18),
                    _monster(200, intent="ATTACK", adjusted=18)]
        state = _combat([_card("BigHit", cost=2),
                         _card("SmallGuard", cost=1, has_target=False)],
                        monsters)
        self.assertEqual(2, greedy_policy.attacker_count(state))
        self.assertEqual("play 1 0", _pick(state, self.TABLE))

    def test_facing_attack_is_still_the_boolean_it_always_was(self):
        self.assertFalse(greedy_policy.facing_attack(self._board(0, idle=2)))
        self.assertTrue(greedy_policy.facing_attack(self._board(1)))


# --- replay harness over the recorded Act-1 boss fights ---------------------

_BOSS_CAPTURE_GLOB = os.environ.get(
    "STS_ORACLE_BOSS_CAPTURE_GLOB",
    r"D:\STS_BG_Mod\_oracle_data\campaigns"
    r"\g6_boss_ps*_20260728T153342Z_claude01")


class GreedyBossFightReplayLegalityTest(unittest.TestCase):
    """Every b1.5.0 command, on every recorded Act-1 boss-fight state.

    Same contract as GreedyReplayLegalityTest and for the same reason -- the
    policy ranks, `expand_legal_actions` decides legality -- but scoped to the
    room the new rules exist for. These captures are the twelve boss fights the
    g6 runbook counted; they live outside the repo (design 7.3), so their
    absence skips.
    """

    MIN_RUNS = 3
    MIN_BOSS_STATES = 200

    def _runs(self):
        paths = []
        for directory in sorted(glob.glob(_BOSS_CAPTURE_GLOB)):
            paths.extend(sorted(glob.glob(os.path.join(
                directory, "run_*_a20_ironclad.jsonl"))))
        if len(paths) < self.MIN_RUNS:
            self.skipTest(
                f"need >= {self.MIN_RUNS} recorded runs under "
                f"{_BOSS_CAPTURE_GLOB} (campaign artifacts live outside the "
                "repo, design 7.3)")
        return paths

    @staticmethod
    def _boss_states(path):
        with open(path, encoding="utf-8") as fh:
            for line in fh:
                record = json.loads(line)
                if record.get("record_kind") != "action":
                    continue
                state = record.get("state_json") or {}
                if not state.get("in_game"):
                    continue
                gs = state.get("game_state") or {}
                if gs.get("room_type") != "MonsterRoomBoss":
                    continue
                yield record, state, gs

    def test_every_greedy_command_is_legal_on_every_boss_fight_state(self):
        table = greedy_policy.load_side_table(_SIDE_TABLE)
        self.assertTrue(table)
        checked = emitted = 0
        runs_with_a_boss_fight = 0
        for path in self._runs():
            rng = random.Random(f"1234:{os.path.basename(path)}")
            here = 0
            for _record, state, _gs in self._boss_states(path):
                checked += 1
                here += 1
                actions = campaign_driver.expand_legal_actions(state, rng)
                command = greedy_policy.pick(actions, state, table, rng)
                if command is None:
                    self.assertEqual([], actions)
                    continue
                emitted += 1
                self.assertIn(command, actions, f"{path}: {command!r}")
                self.assertTrue(
                    campaign_driver.cmd_verb_ready(state, command),
                    f"{path}: verb of {command!r} is not advertised "
                    f"({state.get('available_commands')})")
                ok, why = campaign_driver.cmd_args_ready(state, command)
                self.assertTrue(ok, f"{path}: {command!r} -- {why}")
            if here:
                runs_with_a_boss_fight += 1
        self.assertGreaterEqual(checked, self.MIN_BOSS_STATES)
        self.assertGreaterEqual(emitted, self.MIN_BOSS_STATES // 2)
        self.assertGreaterEqual(runs_with_a_boss_fight, 3)

    def test_the_recorded_boss_fights_actually_exercise_the_new_rules(self):
        """A legality sweep over states no new rule touches proves nothing."""
        table = greedy_policy.load_side_table(_SIDE_TABLE)
        spendable = 0
        multi_attacker = 0
        split_boards = 0
        for path in self._runs():
            for _record, state, gs in self._boss_states(path):
                if (gs.get("combat_state") or {}).get("monsters"):
                    if greedy_policy.potion_worth_spending(state):
                        spendable += 1
                    if greedy_policy.block_weight_under_attack(state) > \
                            greedy_policy.BLOCK_WEIGHT_UNDER_ATTACK:
                        multi_attacker += 1
                names = {(m or {}).get("name") for m in
                         (gs.get("combat_state") or {}).get("monsters") or []}
                if {"Spike Slime (L)", "Acid Slime (L)"} <= names:
                    split_boards += 1
        self.assertGreater(spendable, 0,
                           "R2 must read True in a MonsterRoomBoss")
        self.assertGreater(multi_attacker, 0,
                           "R3 must fire on the post-split board")
        self.assertGreater(split_boards, 0,
                           "the captures must contain a real Slime Boss split")

    def test_replay_is_reproducible_from_policy_seed_and_seed(self):
        """Design 7.5: the whole action sequence follows from the two seeds."""
        table = greedy_policy.load_side_table(_SIDE_TABLE)
        paths = self._runs()

        def sequence(policy_seed, path):
            rng = random.Random(f"{policy_seed}:{os.path.basename(path)}")
            out = []
            with open(path, encoding="utf-8") as fh:
                for line in fh:
                    record = json.loads(line)
                    if record.get("record_kind") != "action":
                        continue
                    state = record.get("state_json") or {}
                    if not state.get("in_game"):
                        continue
                    actions = campaign_driver.expand_legal_actions(state, rng)
                    out.append(greedy_policy.pick(actions, state, table, rng))
            return out

        differed = 0
        for path in paths[:6]:
            first = sequence(1234, path)
            self.assertGreater(len(first), 0)
            self.assertEqual(first, sequence(1234, path),
                             f"{path}: not reproducible from (1234, seed)")
            if sequence(4321, path) != first:
                differed += 1
        self.assertGreater(differed, 0,
                           "a policy seed that can never change a decision is "
                           "not a tie-break")


STUB_POLICY_SOURCE = '''\
import json, sys
mode = "last"
if "--config" in sys.argv:
    with open(sys.argv[sys.argv.index("--config") + 1]) as fh:
        mode = json.load(fh).get("mode", "last")
for line in sys.stdin:
    req = json.loads(line)
    if mode == "nonjson":
        sys.stdout.write("not json\\n")
    elif mode == "bogus":
        sys.stdout.write(json.dumps({
            "format": req["format"], "kind": "decision",
            "command": "definitely-not-legal"}) + "\\n")
    elif mode == "die":
        sys.exit(3)
    else:
        sys.stdout.write(json.dumps({
            "format": req["format"], "kind": "decision",
            "command": req["candidates"][-1]}) + "\\n")
    sys.stdout.flush()
'''


def _write_stub_policy(root, mode=None):
    cmd_path = os.path.join(root, "stub_policy.py")
    with open(cmd_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(STUB_POLICY_SOURCE)
    config_path = None
    if mode is not None:
        config_path = os.path.join(root, "stub_config.json")
        with open(config_path, "w", encoding="utf-8", newline="\n") as fh:
            json.dump({"mode": mode}, fh)
    return cmd_path, config_path


class ExternalPolicyHookTest(unittest.TestCase):
    """TE.1: the campaign harness takes actions from a policy binary/config."""

    def test_decide_round_trips_and_binds_seed_and_policy_seed(self):
        with tempfile.TemporaryDirectory() as root:
            cmd_path, _ = _write_stub_policy(root)
            policy = campaign_driver.ExternalPolicy(cmd_path)
            try:
                chosen = policy.decide(
                    SEED, 1234, ["end", "choose 0"], {"in_game": True})
                self.assertEqual("choose 0", chosen)
                # the child stays up across decisions
                self.assertEqual(
                    "play 1", policy.decide(SEED, 1234, ["end", "play 1"],
                                            {"in_game": True}))
            finally:
                policy.close()

    def test_config_file_reaches_the_binary_as_dash_dash_config(self):
        with tempfile.TemporaryDirectory() as root:
            cmd_path, config_path = _write_stub_policy(root, mode="bogus")
            policy = campaign_driver.ExternalPolicy(cmd_path, config_path)
            try:
                with self.assertRaisesRegex(
                        campaign_driver.ExternalPolicyError,
                        "not one of the 2 legal candidates"):
                    policy.decide(SEED, 1234, ["end", "choose 0"], {})
            finally:
                policy.close()

    def test_non_json_and_dead_binary_are_protocol_errors(self):
        with tempfile.TemporaryDirectory() as root:
            cmd_path, config_path = _write_stub_policy(root, mode="nonjson")
            policy = campaign_driver.ExternalPolicy(cmd_path, config_path)
            try:
                with self.assertRaisesRegex(
                        campaign_driver.ExternalPolicyError, "non-JSON"):
                    policy.decide(SEED, 1234, ["end"], {})
            finally:
                policy.close()
        with tempfile.TemporaryDirectory() as root:
            cmd_path, config_path = _write_stub_policy(root, mode="die")
            policy = campaign_driver.ExternalPolicy(cmd_path, config_path)
            try:
                with self.assertRaisesRegex(
                        campaign_driver.ExternalPolicyError,
                        "closed stdout"):
                    policy.decide(SEED, 1234, ["end"], {})
                # a later decide must not silently respawn past the fault
                with self.assertRaisesRegex(
                        campaign_driver.ExternalPolicyError,
                        "refusing to respawn"):
                    policy.decide(SEED, 1234, ["end"], {})
            finally:
                policy.close()

    def test_paths_are_validated_before_any_launch(self):
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaisesRegex(ValueError, "does not exist"):
                campaign_driver.validate_external_policy_path(
                    os.path.join(root, "missing.py"), "--policy-cmd")
            spaced = os.path.join(root, "has space.py")
            with open(spaced, "w", encoding="utf-8") as fh:
                fh.write("pass\n")
            with self.assertRaisesRegex(ValueError, "whitespace"):
                campaign_driver.validate_external_policy_path(
                    spaced, "--policy-cmd")

    def test_external_identity_pins_binary_and_config_hashes(self):
        with tempfile.TemporaryDirectory() as root:
            cmd_path, config_path = _write_stub_policy(root, mode="last")
            identity = campaign_driver.external_policy_identity(
                cmd_path, config_path)
            self.assertEqual(
                {"cmd_path", "cmd_sha256", "config_path", "config_sha256"},
                set(identity))
            self.assertEqual(64, len(identity["cmd_sha256"]))

            progress_path = os.path.join(root, "campaign_progress.json")
            progress = campaign_driver.Progress(
                progress_path, os.path.join(root, "hb.json"))
            progress.load_or_init(
                "external-pin", [SEED], "external", "A" * 64,
                policy_seed=1234, external_policy=identity)
            # same identity resumes
            resumed = campaign_driver.Progress(
                progress_path, os.path.join(root, "hb.json"))
            resumed.load_or_init(
                "external-pin", [SEED], "external", "A" * 64,
                policy_seed=1234, external_policy=identity)
            # a changed binary hash refuses
            changed = dict(identity, cmd_sha256="B" * 64)
            refused = campaign_driver.Progress(
                progress_path, os.path.join(root, "hb.json"))
            with self.assertRaisesRegex(
                    campaign_driver.CampaignIdentityError,
                    "external_policy"):
                refused.load_or_init(
                    "external-pin", [SEED], "external", "A" * 64,
                    policy_seed=1234, external_policy=changed)

    def test_policy_command_routes_through_external_and_fails_fatal(self):
        driver = campaign_driver.CampaignDriver.__new__(
            campaign_driver.CampaignDriver)
        driver.args = SimpleNamespace(policy="external", policy_seed=77)
        driver.rng = random.Random(0)
        driver._current_seed = SEED
        recorded = {}

        class FakeExternal:
            def decide(self, seed, policy_seed, candidates, state):
                recorded.update(seed=seed, policy_seed=policy_seed,
                                candidates=list(candidates))
                return candidates[0]

        driver.external = FakeExternal()
        state = {
            "available_commands": ["end"],
            "in_game": True,
            "game_state": {},
        }
        self.assertEqual("end", driver._policy_command(state))
        self.assertEqual(
            {"seed": SEED, "policy_seed": 77, "candidates": ["end"]},
            recorded)

        class BrokenExternal:
            def decide(self, *_args, **_kwargs):
                raise campaign_driver.ExternalPolicyError("synthetic break")

        driver.external = BrokenExternal()
        with self.assertRaises(campaign_driver.FatalEnvironmentDrift) as ctx:
            driver._policy_command(state)
        self.assertEqual("external_policy_error", ctx.exception.kind)

    def test_boss_room_decision_state_records_boss_fight_reached(self):
        with tempfile.TemporaryDirectory() as root:
            campaign_id = "boss_reach"
            _write_launch_log(os.path.join(root, campaign_id))
            initial = _action(_oracle())["state_json"]
            initial["available_commands"] = ["end"]
            initial["game_state"]["screen_type"] = "COMBAT"
            initial["game_state"]["room_type"] = "MonsterRoomBoss"
            game_over = {
                "available_commands": ["proceed"],
                "ready_for_command": False,
                "in_game": True,
                "game_state": {
                    "screen_type": "GAME_OVER",
                    "screen_state": {"victory": False},
                    "floor": 16,
                    "act": 1,
                },
            }
            menu = {
                "available_commands": ["start"],
                "ready_for_command": True,
                "in_game": False,
            }
            driver = _stack_driver(root, campaign_id, state=initial,
                                   max_actions=100)
            driver.stepper.states.extend([game_over, menu])
            with mock.patch.dict(
                    os.environ,
                    {campaign_paths.ORACLE_LAUNCH_TOKEN_ENV:
                     "unit-test-launch-token"}):
                outcome, _floor, _actions, _menu = driver.run_seed(SEED, 1)
            self.assertEqual("death", outcome)
            self.assertTrue(driver.last_run_boss_fight)

            # the same shape outside the boss room stays False
            campaign_id2 = "no_boss_reach"
            _write_launch_log(os.path.join(root, campaign_id2))
            initial2 = _action(_oracle())["state_json"]
            initial2["available_commands"] = ["end"]
            initial2["game_state"]["screen_type"] = "COMBAT"
            initial2["game_state"]["room_type"] = "MonsterRoom"
            driver2 = _stack_driver(root, campaign_id2, state=initial2,
                                    max_actions=100)
            driver2.stepper.states.extend([dict(game_over), dict(menu)])
            with mock.patch.dict(
                    os.environ,
                    {campaign_paths.ORACLE_LAUNCH_TOKEN_ENV:
                     "unit-test-launch-token"}):
                driver2.run_seed(SEED, 1)
            self.assertFalse(driver2.last_run_boss_fight)


class ActProfileTest(unittest.TestCase):
    """b1.7.0 (S2.42): the per-act constant overlays.

    Three properties, and the first is the one everything else rests on -- if
    Act 1 is not the module constants, every measured b1.6.0 number silently
    stops describing the policy that produced it.
    """

    def setUp(self):
        self.table = greedy_policy.load_side_table(_SIDE_TABLE)
        greedy_policy.CONFIG_PINNED.clear()

    def tearDown(self):
        greedy_policy.CONFIG_PINNED.clear()

    def test_act1_profile_is_exactly_the_module_constants(self):
        self.assertNotIn(1, greedy_policy.ACT_PROFILES,
                         "an act-1 overlay would make the TE.1 evidence "
                         "unreproducible: act 1 must BE the module constants")
        overlaid = {name for overlay in greedy_policy.ACT_PROFILES.values()
                    for name in overlay}
        self.assertTrue(overlaid, "the profiles must overlay something")
        for name in sorted(overlaid):
            for state in ({"game_state": {"act": 1}}, {"game_state": {}}, {}):
                self.assertEqual(
                    getattr(greedy_policy, name),
                    greedy_policy._const(name, state),
                    f"{name} moved in act 1 (or in an act-less dump)")

    def test_every_profiled_name_is_a_real_numeric_constant(self):
        """A typo'd overlay key would be a silently dead tuning."""
        for act, overlay in greedy_policy.ACT_PROFILES.items():
            self.assertIn(act, (2, 3))
            for name, value in overlay.items():
                current = getattr(greedy_policy, name, None)
                self.assertIsInstance(
                    current, (int, float),
                    f"ACT_PROFILES[{act}][{name!r}] names no numeric constant")
                self.assertNotIsInstance(current, bool)
                self.assertIsInstance(value, (int, float))
                self.assertNotIsInstance(value, bool)

    def test_act_of_defaults_to_one_and_ignores_junk(self):
        self.assertEqual(1, greedy_policy.act_of({}))
        self.assertEqual(1, greedy_policy.act_of({"game_state": {}}))
        self.assertEqual(1, greedy_policy.act_of(
            {"game_state": {"act": None}}))
        self.assertEqual(1, greedy_policy.act_of(
            {"game_state": {"act": True}}), "bool is not an act")
        self.assertEqual(3, greedy_policy.act_of({"game_state": {"act": 3}}))

    def test_the_deck_gate_widens_by_act(self):
        cards = [{"id": "Heavy Blade", "type": "ATTACK", "upgrades": 0}]
        # A deck that is CLOSED in act 1 (10 attacks) and OPEN in act 2.
        deck = _deck(10, 4)
        for act, expected in ((1, "skip"), (2, "choose 0"), (3, "choose 0")):
            state = _screen("CARD_REWARD", ["heavy blade"],
                            {"skip_available": True, "cards": cards},
                            available=("choose", "skip"), deck=deck, act=act)
            self.assertEqual(expected, _pick(state, self.table),
                             f"act {act}")

    def test_the_two_screens_can_never_disagree_in_any_act(self):
        """R1's anti-2-cycle invariant, re-swept per act.

        The act is a property of the same dump on both screens, so widening the
        thresholds by act cannot separate the two decisions -- but that is an
        argument, and this is the check.
        """
        cards = [{"id": "Anger", "type": "ATTACK", "upgrades": 0},
                 {"id": "Impervious", "type": "SKILL", "upgrades": 0}]
        for act in (1, 2, 3):
            seen = set()
            for attacks in range(0, 18):
                for others in range(0, 20):
                    deck = _deck(attacks, others) if attacks or others else None
                    reward = _screen(
                        "COMBAT_REWARD", ["gold", "card"],
                        {"rewards": [{"reward_type": "GOLD", "gold": 30},
                                     {"reward_type": "CARD"}]},
                        available=("choose", "proceed"), deck=deck, act=act)
                    screen = _screen(
                        "CARD_REWARD", ["anger", "impervious"],
                        {"skip_available": True, "cards": cards},
                        available=("choose", "skip"), deck=deck, act=act)
                    opens = (greedy_policy.score_action("choose 1", reward,
                                                       self.table)
                             > greedy_policy.score_action("proceed", reward,
                                                          self.table))
                    takes = (greedy_policy.score_action("choose 0", screen,
                                                        self.table)
                             > greedy_policy.score_action("skip", screen,
                                                          self.table))
                    self.assertEqual(
                        opens, takes,
                        f"act {act}, deck ({attacks} attack / {others} "
                        f"other): opened={opens} but took={takes}")
                    seen.add(opens)
            self.assertEqual({True, False}, seen,
                             f"act {act}: the sweep must cover both sides")

    # -- A1: elite appetite ------------------------------------------------

    @staticmethod
    def _map(act, hp, max_hp=80):
        return _screen(
            "MAP", ["node", "node"],
            {"next_nodes": [{"symbol": "M"}, {"symbol": "E"}]},
            available=("choose",), act=act,
            current_hp=hp, max_hp=max_hp)

    def test_healthy_act2_and_act3_prefer_the_elite_over_a_normal_fight(self):
        for act in (2, 3):
            state = self._map(act, hp=80)
            self.assertEqual("choose 1", _pick(state, self.table),
                             f"act {act}: a healthy run should want the elite")

    def test_act1_still_avoids_the_elite(self):
        self.assertEqual("choose 0", _pick(self._map(1, hp=80), self.table))

    def test_a_hurt_act3_run_falls_back_to_act1_elite_avoidance(self):
        # 40/80 = 0.50 <= ELITE_APPETITE_HP_FRACTION (0.60)
        state = self._map(3, hp=40)
        self.assertEqual("choose 0", _pick(state, self.table))
        self.assertEqual(greedy_policy.MAP_ELITE,
                         greedy_policy.elite_map_value(state))

    def test_elite_appetite_is_unchanged_when_hp_is_unknown(self):
        """A dump with no max_hp must not silently lose the raised value."""
        state = _screen("MAP", ["node", "node"],
                        {"next_nodes": [{"symbol": "M"}, {"symbol": "E"}]},
                        act=3)
        state["game_state"].pop("max_hp", None)
        state["game_state"].pop("current_hp", None)
        self.assertEqual(greedy_policy.ACT_PROFILES[3]["MAP_ELITE"],
                         greedy_policy.elite_map_value(state))

    # -- A3: potion discipline by act --------------------------------------

    def test_act3_spends_a_potion_in_a_normal_fight(self):
        for act, expected in ((1, False), (2, False), (3, True)):
            state = {"game_state": {"act": act, "room_type": "MonsterRoom",
                                    "current_hp": 70, "max_hp": 80,
                                    "potions": [_potion(), _EMPTY_SLOT]}}
            self.assertEqual(
                expected, greedy_policy.potion_worth_spending(state),
                f"act {act}")

    def test_the_low_hp_floor_rises_by_act(self):
        # 44/80 = 0.55: below act 3's 0.60 floor, above act 1's 0.40.
        for act, expected in ((1, False), (2, False), (3, True)):
            state = {"game_state": {"act": act, "room_type": "RestRoom",
                                    "current_hp": 44, "max_hp": 80,
                                    "potions": [_potion(), _EMPTY_SLOT]}}
            self.assertEqual(
                expected, greedy_policy.potion_worth_spending(state),
                f"act {act}")

    # -- precedence --------------------------------------------------------

    def test_a_cohort_config_beats_the_act_profile_in_every_act(self):
        """The failure this prevents: a cohort labelled with a policy it did
        not run. Without CONFIG_PINNED the overlay silently wins in acts 2/3
        and the campaign identity is a lie."""
        original = greedy_policy.MAP_ELITE
        try:
            survival_policy_cmd.apply_constants(
                greedy_policy, {"MAP_ELITE": 111})
            self.assertIn("MAP_ELITE", greedy_policy.CONFIG_PINNED)
            for act in (1, 2, 3):
                self.assertEqual(
                    111,
                    greedy_policy._const("MAP_ELITE",
                                         {"game_state": {"act": act}}),
                    f"act {act}: the act profile overrode the cohort config")
        finally:
            greedy_policy.MAP_ELITE = original
            greedy_policy.CONFIG_PINNED.clear()

    def test_an_unpinned_name_still_takes_the_act_profile(self):
        original = greedy_policy.MAP_ELITE
        try:
            survival_policy_cmd.apply_constants(
                greedy_policy, {"DECK_SIZE_CAP": 99})
            self.assertEqual(
                greedy_policy.ACT_PROFILES[2]["MAP_ELITE"],
                greedy_policy._const("MAP_ELITE",
                                     {"game_state": {"act": 2}}))
        finally:
            greedy_policy.MAP_ELITE = original
            greedy_policy.DECK_SIZE_CAP = 20
            greedy_policy.CONFIG_PINNED.clear()


class BossRelicPickTest(unittest.TestCase):
    """R4 (b1.7.0): the boss-chest pick, and its two SHA-pinned cohorts."""

    def setUp(self):
        self.table = greedy_policy.load_side_table(_SIDE_TABLE)
        self._skip_mode = greedy_policy.BOSS_RELIC_SKIP_MODE
        greedy_policy.CONFIG_PINNED.clear()

    def tearDown(self):
        greedy_policy.BOSS_RELIC_SKIP_MODE = self._skip_mode
        greedy_policy.CONFIG_PINNED.clear()

    @staticmethod
    def _chest(relic_ids, act=2):
        return _screen(
            "BOSS_REWARD", list(relic_ids),
            {"relics": [{"id": r, "name": r} for r in relic_ids]},
            available=("choose", "skip"), act=act, floor=17,
            room_type=campaign_driver.BOSS_CHEST_ROOM)

    def test_take_cohort_takes_a_takeable_relic(self):
        state = self._chest(["Sozu", "Philosopher's Stone", "Runic Dome"])
        self.assertEqual("choose 1", _pick(state, self.table))

    def test_skip_cohort_skips_even_a_takeable_relic(self):
        greedy_policy.BOSS_RELIC_SKIP_MODE = 1
        state = self._chest(["Philosopher's Stone", "Black Star", "Astrolabe"])
        self.assertEqual("skip", _pick(state, self.table))

    def test_a_chest_of_nothing_but_never_take_relics_skips(self):
        """The take cohort's `skip` sits BETWEEN takeable and never-take, so
        this is a property of the bands, not a special case."""
        state = self._chest(list(greedy_policy.BOSS_RELIC_NEVER_TAKE[:3]))
        self.assertEqual("skip", _pick(state, self.table))

    def test_every_never_take_name_is_a_registry_boss_relic(self):
        """A typo'd name would be a rule that silently never fires."""
        path = os.path.join(_REPO_ROOT, "registry", "relics.yaml")
        with open(path, "r", encoding="utf-8") as fh:
            text = fh.read()
        for name in greedy_policy.BOSS_RELIC_NEVER_TAKE:
            self.assertIn(f'game_id: "{name}"', text,
                          f"{name!r} is not a registry relic game_id")

    def test_the_choice_list_is_the_fallback_when_relics_are_absent(self):
        state = _screen("BOSS_REWARD", ["Sozu", "Black Star"],
                        available=("choose", "skip"), act=2)
        self.assertEqual("choose 1", _pick(state, self.table))

    def test_the_committed_cohort_configs_select_their_cohorts(self):
        expected = {
            "policy_survival_act.json": 0,
            "policy_bossrelic_take.json": 0,
            "policy_bossrelic_skip.json": 1,
        }
        for filename, mode in expected.items():
            path = os.path.join(_DRIVER_DIR, filename)
            self.assertTrue(os.path.exists(path), filename)
            config = survival_policy_cmd.load_config(path)
            try:
                survival_policy_cmd.SurvivalPolicy(config)
                self.assertEqual(
                    mode, greedy_policy.BOSS_RELIC_SKIP_MODE, filename)
            finally:
                greedy_policy.BOSS_RELIC_SKIP_MODE = self._skip_mode
                greedy_policy.CONFIG_PINNED.clear()

    def test_the_three_cohort_configs_are_distinct_identities(self):
        """A campaign's policy identity is the config's SHA-256; two cohorts
        that hashed the same would be one cohort wearing two labels."""
        digests = set()
        for filename in ("policy_survival_act.json",
                         "policy_bossrelic_take.json",
                         "policy_bossrelic_skip.json"):
            with open(os.path.join(_DRIVER_DIR, filename), "rb") as fh:
                digests.add(hashlib.sha256(fh.read()).hexdigest())
        self.assertEqual(3, len(digests))


class BossChestSequencingTest(unittest.TestCase):
    """The driver half of R4: one open per chest, and the act-aware terminal."""

    @staticmethod
    def _driver(policy="greedy"):
        driver = campaign_driver.CampaignDriver.__new__(
            campaign_driver.CampaignDriver)
        driver.args = SimpleNamespace(policy=policy, policy_seed=0)
        driver.rng = random.Random(0)
        driver.card_table = greedy_policy.load_side_table(_SIDE_TABLE)
        driver._current_seed = SEED
        driver._reset_reach()
        return driver

    @staticmethod
    def _closed_chest(act=2, floor=17):
        return _screen("CHEST", ["open"], available=("choose", "proceed"),
                       act=act, floor=floor,
                       room_type=campaign_driver.BOSS_CHEST_ROOM)

    def test_the_first_open_is_offered_and_the_second_is_not(self):
        driver = self._driver()
        state = self._closed_chest()
        acts = campaign_driver.expand_legal_actions(state, driver.rng)
        self.assertIn("choose 0", acts)

        self.assertEqual("choose 0", driver._policy_command(state))
        # ... the pick was skipped, so the game re-advertises `open`. The
        # candidate set must no longer contain it, and `proceed` must survive.
        filtered = driver._boss_chest_reopen_filter(state, list(acts))
        self.assertNotIn("choose 0", filtered)
        self.assertIn("proceed", filtered)
        self.assertEqual("proceed", driver._policy_command(state))

    def test_a_second_act_chest_is_a_fresh_open(self):
        driver = self._driver()
        self.assertEqual("choose 0",
                         driver._policy_command(self._closed_chest(act=2)))
        self.assertEqual("proceed",
                         driver._policy_command(self._closed_chest(act=2)))
        self.assertEqual("choose 0",
                         driver._policy_command(self._closed_chest(act=3,
                                                                   floor=34)),
                         "act 3's chest must not inherit act 2's guard")

    def test_an_ordinary_treasure_chest_is_never_filtered(self):
        driver = self._driver()
        state = _screen("CHEST", ["open"], available=("choose", "proceed"),
                        act=1, floor=8, room_type="TreasureRoom")
        self.assertEqual("choose 0", driver._policy_command(state))
        self.assertEqual("choose 0", driver._policy_command(state),
                         "the guard is the BOSS chest's, not every chest's")

    def test_the_boss_reward_terminal_is_no_longer_act_gated(self):
        for act in (1, 2, 3):
            gs = {"act": act, "room_type": campaign_driver.BOSS_ROOM,
                  "screen_type": "COMBAT_REWARD"}
            self.assertTrue(campaign_driver.is_boss_combat_reward(gs),
                            f"act {act}")
        self.assertFalse(campaign_driver.is_boss_combat_reward(
            {"act": 2, "room_type": "MonsterRoom",
             "screen_type": "COMBAT_REWARD"}))

    def test_boss_reward_claims_every_row_then_proceeds(self):
        sent = []

        class Stepper:
            def __init__(self):
                self.remaining = ["gold", "card"]

            def step(self, command):
                sent.append(command)
                if command == "choose 0" and self.remaining:
                    self.remaining.pop(0)
                gs = {"act": 2, "floor": 34,
                      "room_type": campaign_driver.BOSS_ROOM,
                      "screen_type": "COMBAT_REWARD",
                      "choice_list": list(self.remaining)}
                return "ready", {"available_commands": ["choose", "proceed"],
                                 "ready_for_command": True, "in_game": True,
                                 "game_state": gs}

        driver = self._driver()
        driver.stepper = Stepper()
        rl = mock.Mock()
        timing = mock.Mock()
        first = {"available_commands": ["choose", "proceed"],
                 "ready_for_command": True, "in_game": True,
                 "game_state": {"act": 2, "floor": 34,
                                "room_type": campaign_driver.BOSS_ROOM,
                                "screen_type": "COMBAT_REWARD",
                                "choice_list": ["gold", "card"]}}
        _state, actions, stop = driver._claim_boss_reward(
            rl, timing, first, SEED, 0)
        self.assertIsNone(stop)
        self.assertEqual(["choose 0", "choose 0", "proceed"], sent)
        self.assertEqual(3, actions)

    def test_a_boss_reward_screen_that_never_clears_is_named_not_spun_on(self):
        class StuckStepper:
            def step(self, _command):
                return "ready", {
                    "available_commands": ["choose"],
                    "ready_for_command": True, "in_game": True,
                    "game_state": {"act": 1, "floor": 17,
                                   "room_type": campaign_driver.BOSS_ROOM,
                                   "screen_type": "COMBAT_REWARD",
                                   "choice_list": ["gold"]}}

        driver = self._driver()
        driver.stepper = StuckStepper()
        _state, actions, stop = driver._claim_boss_reward(
            mock.Mock(), mock.Mock(), {
                "game_state": {"act": 1, "floor": 17,
                               "room_type": campaign_driver.BOSS_ROOM,
                               "screen_type": "COMBAT_REWARD",
                               "choice_list": ["gold"]}}, SEED, 0)
        self.assertEqual("boss_reward_wedge", stop)
        self.assertLess(actions, 100, "the guard must bound the spend")


class PerActReachFieldsTest(unittest.TestCase):
    """The `seeds_done` reach block the S2.42 report aggregates."""

    @staticmethod
    def _driver():
        driver = campaign_driver.CampaignDriver.__new__(
            campaign_driver.CampaignDriver)
        driver._reset_reach()
        return driver

    def test_a_fresh_run_reports_nothing_reached(self):
        self.assertEqual(
            {"boss_fight_reached": False, "boss_fight_acts": [],
             "boss_kill_acts": [], "boss_relic_acts": [], "max_act": 0,
             "victory": False},
            self._driver()._reach_fields())

    def test_the_boss_chest_is_the_act_1_and_2_kill_probe(self):
        driver = self._driver()
        driver._observe_reach({"act": 1, "room_type": campaign_driver.BOSS_ROOM})
        self.assertEqual([], driver._reach_fields()["boss_kill_acts"],
                         "standing in the boss room is a FIGHT, not a kill")
        driver._observe_reach({"act": 1,
                               "room_type": campaign_driver.BOSS_CHEST_ROOM})
        fields = driver._reach_fields()
        self.assertEqual([1], fields["boss_fight_acts"])
        self.assertEqual([1], fields["boss_kill_acts"])
        self.assertTrue(fields["boss_fight_reached"])

    def test_the_act_3_kill_is_the_victory_flag_not_a_chest(self):
        driver = self._driver()
        driver._observe_reach({"act": 3, "room_type": campaign_driver.BOSS_ROOM})
        self.assertEqual([], driver._reach_fields()["boss_kill_acts"])
        driver.last_run_victory = True
        fields = driver._reach_fields()
        self.assertEqual([3], fields["boss_kill_acts"])
        self.assertTrue(fields["victory"])
        self.assertEqual([3], fields["boss_fight_acts"])

    def test_the_relic_screen_is_recorded_per_act(self):
        driver = self._driver()
        driver._observe_reach({"act": 2, "screen_type": "BOSS_REWARD"})
        self.assertEqual([2], driver._reach_fields()["boss_relic_acts"])

    def test_max_act_is_a_max_and_observation_is_idempotent(self):
        driver = self._driver()
        for act in (1, 2, 3, 2, 1):
            driver._observe_reach({"act": act,
                                   "room_type": campaign_driver.BOSS_ROOM})
        fields = driver._reach_fields()
        self.assertEqual(3, fields["max_act"])
        self.assertEqual([1, 2, 3], fields["boss_fight_acts"])

    def test_a_junk_act_is_ignored_rather_than_recorded(self):
        driver = self._driver()
        for act in (None, True, "2"):
            driver._observe_reach({"act": act,
                                   "room_type": campaign_driver.BOSS_ROOM})
        fields = driver._reach_fields()
        self.assertEqual([], fields["boss_fight_acts"])
        self.assertEqual(0, fields["max_act"])
        self.assertTrue(fields["boss_fight_reached"],
                        "the act-less boss room is still a boss fight")

    def test_the_reach_block_stays_outside_the_strict_key_set(self):
        """b1.6.0's compatibility rule, re-checked for b1.7.0's fields: a
        pre-b1.7.0 ledger must still validate."""
        for key in self._driver()._reach_fields():
            self.assertNotIn(key, validate_artifacts.STRICT_DONE_KEYS, key)


class SurvivalPolicyCmdTest(unittest.TestCase):
    """The reference external binary: greedy scoring behind STS-POLICY-IO."""

    def test_constants_overrides_are_strictly_validated(self):
        original = greedy_policy.MAP_ELITE
        try:
            applied = survival_policy_cmd.apply_constants(
                greedy_policy, {"MAP_ELITE": 150})
            self.assertEqual({"MAP_ELITE": (original, 150)}, applied)
            self.assertEqual(150, greedy_policy.MAP_ELITE)
        finally:
            greedy_policy.MAP_ELITE = original
        for bad in ({"NOT_A_CONSTANT": 1},
                    {"map_elite": 1},
                    {"MAP_ELITE": "high"},
                    {"MAP_ELITE": True},
                    {"SYMBOL_MONSTER": 5}):
            with self.assertRaises(survival_policy_cmd.ConfigError):
                survival_policy_cmd.apply_constants(greedy_policy, bad)
        self.assertEqual(original, greedy_policy.MAP_ELITE)

    def test_unknown_config_keys_fail_loud(self):
        with tempfile.TemporaryDirectory() as root:
            path = os.path.join(root, "config.json")
            with open(path, "w", encoding="utf-8") as fh:
                json.dump({"constants": {}, "typo_key": 1}, fh)
            with self.assertRaisesRegex(
                    survival_policy_cmd.ConfigError, "typo_key"):
                survival_policy_cmd.load_config(path)

    def test_decisions_match_in_driver_greedy_from_the_same_seeds(self):
        state = {
            "available_commands": ["choose"],
            "in_game": True,
            "game_state": {
                "screen_type": "UNKNOWN_SCREEN",
                "choice_list": ["a", "b", "c"],
            },
        }
        candidates = campaign_driver.expand_legal_actions(
            state, random.Random(0))
        self.assertEqual(["choose 0", "choose 1", "choose 2"], candidates)
        policy = survival_policy_cmd.SurvivalPolicy({})
        rng = random.Random(f"1234:{SEED}")
        expected = [greedy_policy.pick(candidates, state, policy.table, rng)
                    for _ in range(8)]
        got = [policy.decide({"seed": SEED, "policy_seed": 1234,
                              "candidates": candidates, "state": state})
               for _ in range(8)]
        self.assertEqual(expected, got)
        # a fresh policy for a different seed diverges eventually
        other = survival_policy_cmd.SurvivalPolicy({})
        other_got = [other.decide({"seed": "STS09999", "policy_seed": 1234,
                                   "candidates": candidates, "state": state})
                     for _ in range(8)]
        self.assertNotEqual(got, other_got)

    def test_serve_answers_decide_and_rejects_out_of_contract(self):
        request = {
            "format": survival_policy_cmd.PROTOCOL,
            "kind": "decide",
            "seed": SEED,
            "policy_seed": 1234,
            "candidates": ["end"],
            "state": {"in_game": True, "game_state": {}},
        }
        stdin = io.StringIO(json.dumps(request) + "\n")
        stdout = io.StringIO()
        self.assertEqual(0, survival_policy_cmd.serve(stdin, stdout, {}))
        response = json.loads(stdout.getvalue())
        self.assertEqual(
            {"format": survival_policy_cmd.PROTOCOL, "kind": "decision",
             "command": "end"}, response)

        bad = dict(request, kind="mystery")
        self.assertEqual(2, survival_policy_cmd.serve(
            io.StringIO(json.dumps(bad) + "\n"), io.StringIO(), {}))

    def test_end_to_end_through_the_driver_hook(self):
        cmd_path = os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            "survival_policy_cmd.py")
        policy = campaign_driver.ExternalPolicy(cmd_path)
        try:
            state = {
                "available_commands": ["end"],
                "in_game": True,
                "game_state": {},
            }
            self.assertEqual(
                "end", policy.decide(SEED, 1234, ["end"], state))
        finally:
            policy.close()


if __name__ == "__main__":
    unittest.main()
