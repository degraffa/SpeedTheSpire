#!/usr/bin/env python3
"""Unit tests for script_policy_cmd (S2.V2), game never launched.

The matcher's evidence base is the COMMITTED corpus of recorded protocol
dumps (tests/golden/oracle_corpus/act1_a20_50.tar.gz): for every supported
command form in a captured run we re-derive the STS-SCRIPT step the sim
emitter would have written for that decision (identity, not index), then
prove the matcher maps it back to the exact command the live campaign
actually sent. That inverse round-trip over real dumps is what makes the
identity vocabulary trustworthy without launching the game; the screens the
Act-1 corpus cannot contain (BOSS_REWARD) are covered by synthetic dumps
built to PROTOCOL.md 3.8's shape, the same approach test_oracle_campaign.py
takes for driver states.
"""

import io
import json
import os
import random
import tarfile
import tempfile
import unittest

import campaign_driver
import script_policy_cmd as spc

HERE = os.path.dirname(os.path.abspath(__file__))
CORPUS = os.path.normpath(os.path.join(
    HERE, "..", "..", "..", "tests", "golden", "oracle_corpus",
    "act1_a20_50.tar.gz"))


def corpus_runs(limit):
    """Yield (name, [action records]) for the first `limit` corpus runs."""
    with tarfile.open(CORPUS) as tar:
        names = sorted(m.name for m in tar.getmembers()
                       if m.name.endswith(".jsonl"))
        for name in names[:limit]:
            records = []
            for line in tar.extractfile(name):
                record = json.loads(line)
                if record.get("record_kind") == "action":
                    records.append(record)
            yield name, records


def _gs(state):
    return (state or {}).get("game_state") or {}


def _screen(state):
    return _gs(state).get("screen_state") or {}


def derive_step(state, command):
    """The STS-SCRIPT step the sim emitter would write for this decision.

    Test-local inverse of the matcher, driven by the RECORDED command and the
    RECORDED dump alone. Returns None for command forms this policy never
    scripts (shop purchases etc.) -- the caller stops its prefix there.
    """
    parts = command.split()
    verb = parts[0]
    gs = _gs(state)
    screen_type = gs.get("screen_type")

    if verb == "end":
        return {"k": "end"}
    if verb == "play":
        hand = (gs.get("combat_state") or {}).get("hand") or []
        slot = int(parts[1])
        idx = 9 if slot == 0 else slot - 1
        if idx >= len(hand):
            return None
        card = hand[idx] or {}
        ord_ = sum(1 for c in hand[:idx]
                   if (c or {}).get("id") == card.get("id")
                   and ((c or {}).get("upgrades") or 0) ==
                   (card.get("upgrades") or 0))
        step = {"k": "play", "card": card.get("id"),
                "up": card.get("upgrades") or 0, "ord": ord_,
                "t": int(parts[2]) if len(parts) > 2 else -1}
        return step
    if verb == "potion":
        slot = int(parts[2])
        potions = gs.get("potions") or []
        if slot >= len(potions):
            return None
        potion = (potions[slot] or {}).get("id")
        if parts[1] == "discard":
            return {"k": "potion_discard", "slot": slot, "potion": potion}
        return {"k": "potion", "slot": slot, "potion": potion,
                "t": int(parts[3]) if len(parts) > 3 else -1}
    if verb in ("proceed", "confirm"):
        return {"k": "proceed", "ctx": "test"}
    if verb == "skip":
        if screen_type == "CARD_REWARD":
            return {"k": "skip_card"}
        return None
    if verb in ("cancel", "return", "leave"):
        return {"k": "grid_cancel", "ctx": "test"}
    if verb != "choose":
        return None

    index = int(parts[1])
    choices = gs.get("choice_list") or []
    if screen_type == "MAP":
        screen = _screen(state)
        if screen.get("boss_available"):
            return {"k": "map_boss"}
        nodes = screen.get("next_nodes") or []
        if index >= len(nodes):
            return None
        node = nodes[index] or {}
        return {"k": "map", "x": node.get("x"), "sym": node.get("symbol")}
    if screen_type == "EVENT":
        return {"k": "event",
                "event": _screen(state).get("event_id"),
                "opt": index, "index": index}
    if screen_type == "COMBAT_REWARD":
        rewards = _screen(state).get("rewards") or []
        if index >= len(rewards):
            return None
        row = rewards[index] or {}
        rtype = (row.get("reward_type") or "").upper()
        step = {"k": "claim", "rtype": rtype,
                "ord": sum(1 for r in rewards[:index]
                           if ((r or {}).get("reward_type") or "").upper() ==
                           rtype)}
        payload = row.get("relic") or row.get("potion") or {}
        if payload.get("id"):
            step["id"] = payload["id"]
        return step
    if screen_type == "CARD_REWARD":
        cards = _screen(state).get("cards") or []
        if index >= len(cards):
            return None
        card = cards[index] or {}
        return {"k": "take_card", "card": card.get("id"),
                "up": card.get("upgrades") or 0,
                "ord": sum(1 for c in cards[:index]
                           if (c or {}).get("id") == card.get("id")
                           and ((c or {}).get("upgrades") or 0) ==
                           (card.get("upgrades") or 0))}
    if screen_type == "REST":
        if index >= len(choices):
            return None
        return {"k": "rest", "opt": str(choices[index]).lower()}
    if screen_type == "CHEST":
        if index < len(choices) and str(choices[index]).lower() == "open":
            return {"k": "open_chest"}
        return None
    if screen_type in ("GRID", "HAND_SELECT"):
        screen = _screen(state)
        cards = screen.get("cards") or screen.get("hand") or []
        if index >= len(cards):
            return None
        card = cards[index] or {}
        return {"k": "grid", "ctx": "test", "card": card.get("id"),
                "up": card.get("upgrades") or 0,
                "ord": sum(1 for c in cards[:index]
                           if (c or {}).get("id") == card.get("id")
                           and ((c or {}).get("upgrades") or 0) ==
                           (card.get("upgrades") or 0))}
    return None


def make_script_lines(seed, steps):
    header = {"format": spc.SCRIPT_FORMAT, "seed": seed, "seed_int": 0,
              "ascension": 20, "policy": "sim_search", "policy_seed": 0,
              "engine_schema": 8, "steps": len(steps),
              "final_hash": "0" * 16, "end_reason": "run_over",
              "victory": False, "max_act": 1, "max_floor": 0}
    return [json.dumps(header)] + [json.dumps(s) for s in steps]


def decide_request(seed, state, candidates):
    return {"format": spc.PROTOCOL, "kind": "decide", "seed": seed,
            "policy_seed": 0, "candidates": candidates, "state": state}


class CorpusRoundTripTest(unittest.TestCase):
    """Derived identity steps map back to the exact recorded commands."""

    def test_matcher_reproduces_recorded_commands_across_corpus_runs(self):
        total = 0
        runs_used = 0
        for name, records in corpus_runs(limit=12):
            with tempfile.TemporaryDirectory() as tmp:
                seed = os.path.basename(name).split(".")[0]
                steps = []
                prefix = []
                for record in records:
                    state = record.get("state_json") or {}
                    command = record.get("action_command") or ""
                    step = derive_step(state, command)
                    if step is None:
                        break
                    candidates = campaign_driver.expand_legal_actions(
                        state, random.Random(0))
                    if command not in candidates:
                        break  # a form the expansion no longer produces
                    steps.append(step)
                    prefix.append((state, candidates, command))
                if not steps:
                    continue
                path = os.path.join(
                    tmp, f"{seed}__sim_search__ps0.script.jsonl")
                with open(path, "w", encoding="utf-8") as fh:
                    fh.write("\n".join(make_script_lines(seed, steps)) + "\n")
                policy = spc.ScriptPolicy({"script_dir": tmp})
                for state, candidates, command in prefix:
                    got = policy.decide(decide_request(seed, state,
                                                       candidates))
                    self.assertEqual(
                        got, command,
                        f"{name}: matcher answered {got!r} where the "
                        f"capture sent {command!r}")
                total += len(prefix)
                runs_used += 1
        # The corpus must actually exercise the matcher at scale -- a
        # regression that silently empties the prefixes must fail loudly.
        self.assertGreaterEqual(runs_used, 8)
        self.assertGreaterEqual(total, 400)

    def test_corpus_covers_the_main_step_kinds(self):
        kinds = set()
        for _name, records in corpus_runs(limit=12):
            for record in records:
                step = derive_step(record.get("state_json") or {},
                                   record.get("action_command") or "")
                if step:
                    kinds.add(step["k"])
        for wanted in ("play", "end", "map", "event", "claim", "take_card",
                       "proceed"):
            self.assertIn(wanted, kinds)


class DivergenceTest(unittest.TestCase):
    def _combat_state(self):
        return {"available_commands": ["play", "end", "state"],
                "game_state": {"screen_type": "NONE",
                               "combat_state": {
                                   "hand": [{"id": "Strike_R", "upgrades": 0,
                                             "is_playable": True,
                                             "has_target": True}],
                                   "monsters": [{"current_hp": 10,
                                                 "is_gone": False}]}}}

    def test_missing_card_is_a_divergence_not_an_improvisation(self):
        with tempfile.TemporaryDirectory() as tmp:
            steps = [{"k": "play", "card": "Carnage", "up": 0, "ord": 0,
                      "t": 0}]
            path = os.path.join(tmp, "STS1__sim_search__ps0.script.jsonl")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write("\n".join(make_script_lines("STS1", steps)) + "\n")
            policy = spc.ScriptPolicy({"script_dir": tmp})
            state = self._combat_state()
            candidates = campaign_driver.expand_legal_actions(
                state, random.Random(0))
            with self.assertRaises(spc.Divergence):
                policy.decide(decide_request("STS1", state, candidates))

    def test_serve_stops_with_exit_3_and_writes_the_record(self):
        with tempfile.TemporaryDirectory() as tmp:
            steps = [{"k": "play", "card": "Carnage", "up": 0, "ord": 0,
                      "t": 0}]
            path = os.path.join(tmp, "STS1__sim_search__ps0.script.jsonl")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write("\n".join(make_script_lines("STS1", steps)) + "\n")
            state = self._combat_state()
            candidates = campaign_driver.expand_legal_actions(
                state, random.Random(0))
            stdin = io.StringIO(
                json.dumps(decide_request("STS1", state, candidates)) + "\n")
            stdout = io.StringIO()
            rc = spc.serve(stdin, stdout, {"script_dir": tmp})
            self.assertEqual(rc, spc.EXIT_DIVERGED)
            self.assertEqual(stdout.getvalue(), "")  # no decision escaped
            records = [f for f in os.listdir(tmp)
                       if f.startswith("divergence_")]
            self.assertEqual(len(records), 1)
            with open(os.path.join(tmp, records[0]), encoding="utf-8") as fh:
                record = json.load(fh)
            self.assertEqual(record["kind"], "script_divergence")
            self.assertEqual(record["seed"], "STS1")
            self.assertIn("Carnage", record["reason"])

    def test_exhausted_script_is_a_divergence(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "STS1__sim_search__ps0.script.jsonl")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write("\n".join(make_script_lines("STS1", [])) + "\n")
            policy = spc.ScriptPolicy({"script_dir": tmp})
            state = self._combat_state()
            candidates = campaign_driver.expand_legal_actions(
                state, random.Random(0))
            with self.assertRaises(spc.Divergence):
                policy.decide(decide_request("STS1", state, candidates))


class GlueRuleTest(unittest.TestCase):
    def _policy_with(self, steps):
        self.tmp = tempfile.TemporaryDirectory()
        path = os.path.join(self.tmp.name,
                            "STS1__sim_search__ps0.script.jsonl")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("\n".join(make_script_lines("STS1", steps)) + "\n")
        return spc.ScriptPolicy({"script_dir": self.tmp.name})

    def tearDown(self):
        if hasattr(self, "tmp"):
            self.tmp.cleanup()

    def test_sole_proceed_screen_glues_without_consuming(self):
        policy = self._policy_with([{"k": "rest", "opt": "rest"}])
        state = {"available_commands": ["proceed", "state"],
                 "game_state": {"screen_type": "COMBAT_REWARD",
                                "screen_state": {"rewards": []}}}
        got = policy.decide(decide_request("STS1", state, ["proceed"]))
        self.assertEqual(got, "proceed")
        self.assertEqual(policy._script.cursor, 0)  # nothing consumed

    def test_scripted_proceed_is_consumed_not_glued(self):
        policy = self._policy_with([{"k": "proceed", "ctx": "combat_reward"}])
        state = {"available_commands": ["proceed", "state"],
                 "game_state": {"screen_type": "COMBAT_REWARD",
                                "screen_state": {"rewards": []}}}
        got = policy.decide(decide_request("STS1", state, ["proceed"]))
        self.assertEqual(got, "proceed")
        self.assertEqual(policy._script.cursor, 1)  # the step was consumed

    def test_grid_confirm_seam_glues_proceed(self):
        policy = self._policy_with([{"k": "rest", "opt": "rest"}])
        state = {"available_commands": ["choose", "confirm", "state"],
                 "game_state": {"screen_type": "GRID",
                                "choice_list": ["strike"],
                                "screen_state": {"confirm_up": True,
                                                 "cards": [
                                                     {"id": "Strike_R",
                                                      "upgrades": 0}]}}}
        candidates = campaign_driver.expand_legal_actions(
            state, random.Random(0))
        self.assertIn("proceed", candidates)
        got = policy.decide(decide_request("STS1", state, candidates))
        self.assertEqual(got, "proceed")
        self.assertEqual(policy._script.cursor, 0)

    def test_sole_choose_dialog_glues_without_consuming(self):
        # Glue rule 3's first live witness (divergence_STS100009_ps0): the
        # Neow `talk` pre-screen shows one candidate while the script's
        # first step is the blessing pick the sim recorded.
        policy = self._policy_with([{"k": "neow", "index": 3}])
        talk = {"available_commands": ["choose", "state"],
                "game_state": {"screen_type": "EVENT",
                               "choice_list": ["talk"],
                               "screen_state": {"event_id": "Neow Event"}}}
        got = policy.decide(decide_request("STS1", talk, ["choose 0"]))
        self.assertEqual(got, "choose 0")
        self.assertEqual(policy._script.cursor, 0)  # nothing consumed
        blessing = {"available_commands": ["choose", "state"],
                    "game_state": {"screen_type": "EVENT",
                                   "choice_list": ["a", "b", "c", "d"],
                                   "screen_state": {
                                       "event_id": "Neow Event"}}}
        got = policy.decide(decide_request(
            "STS1", blessing,
            ["choose 0", "choose 1", "choose 2", "choose 3"]))
        self.assertEqual(got, "choose 3")
        self.assertEqual(policy._script.cursor, 1)

    def test_scripted_single_option_choice_is_consumed_not_glued(self):
        # Match-first: when the sim DID record the one-option decision, the
        # step matches and is consumed; rule 3 never gets a look-in.
        policy = self._policy_with([{"k": "neow", "index": 0}])
        state = {"available_commands": ["choose", "state"],
                 "game_state": {"screen_type": "EVENT",
                                "choice_list": ["talk"],
                                "screen_state": {"event_id": "Neow Event"}}}
        got = policy.decide(decide_request("STS1", state, ["choose 0"]))
        self.assertEqual(got, "choose 0")
        self.assertEqual(policy._script.cursor, 1)  # consumed, not glued

    def test_sole_choose_glue_does_not_mask_a_multi_candidate_desync(self):
        policy = self._policy_with([{"k": "neow", "index": 3}])
        state = {"available_commands": ["choose", "state"],
                 "game_state": {"screen_type": "EVENT",
                                "choice_list": ["a", "b"],
                                "screen_state": {"event_id": "Neow Event"}}}
        with self.assertRaises(spc.Divergence):
            policy.decide(decide_request("STS1", state,
                                         ["choose 0", "choose 1"]))

    def test_sim_only_proceed_is_skipped_when_the_game_auto_advances(self):
        # Skip-rule witness (divergence at step 2 of the first campaign):
        # the sim leaves Neow with an explicit proceed; the live blessing
        # click opens the MAP directly.
        policy = self._policy_with([
            {"k": "proceed", "ctx": "neow"},
            {"k": "map", "x": 1, "sym": "M"}])
        state = {"available_commands": ["choose", "return", "state"],
                 "game_state": {"screen_type": "MAP",
                                "choice_list": ["x=1", "x=4"],
                                "screen_state": {"next_nodes": [
                                    {"x": 1, "symbol": "M"},
                                    {"x": 4, "symbol": "?"}]}}}
        got = policy.decide(decide_request(
            "STS1", state, ["choose 0", "choose 1", "return"]))
        self.assertEqual(got, "choose 0")
        self.assertEqual(policy._script.cursor, 2)  # proceed skipped + map

    def test_proceed_skip_does_not_mask_a_real_desync(self):
        policy = self._policy_with([
            {"k": "proceed", "ctx": "neow"},
            {"k": "rest", "opt": "rest"}])
        state = {"available_commands": ["choose", "state"],
                 "game_state": {"screen_type": "EVENT",
                                "choice_list": ["a", "b"],
                                "screen_state": {"event_id": "X"}}}
        with self.assertRaises(spc.Divergence):
            policy.decide(decide_request("STS1", state,
                                         ["choose 0", "choose 1"]))
        self.assertEqual(policy._script.cursor, 1)  # stopped ON the rest step

    def test_scripted_proceed_with_a_live_confirm_alias_still_matches(self):
        # The skip rule must not fire when either alias is legal.
        policy = self._policy_with([{"k": "confirm", "ctx": "hand"}])
        state = {"available_commands": ["confirm", "state"],
                 "game_state": {"screen_type": "HAND_SELECT",
                                "screen_state": {}}}
        got = policy.decide(decide_request("STS1", state, ["confirm"]))
        self.assertEqual(got, "confirm")
        self.assertEqual(policy._script.cursor, 1)

    def test_exhausted_script_still_glues_a_trailing_one_click_dialog(self):
        policy = self._policy_with([{"k": "proceed", "ctx": "t"}])
        first = {"available_commands": ["proceed", "state"],
                 "game_state": {"screen_type": "COMBAT_REWARD",
                                "screen_state": {"rewards": []}}}
        policy.decide(decide_request("STS1", first, ["proceed"]))
        trailing = {"available_commands": ["choose", "state"],
                    "game_state": {"screen_type": "EVENT",
                                   "choice_list": ["leave"],
                                   "screen_state": {"event_id": "X"}}}
        got = policy.decide(decide_request("STS1", trailing, ["choose 0"]))
        self.assertEqual(got, "choose 0")


class BossRewardTest(unittest.TestCase):
    """PROTOCOL.md 3.8-shaped BOSS_REWARD dumps (the Act-1 corpus cannot
    contain the screen; synthetic, like test_oracle_campaign's states)."""

    STATE = {"available_commands": ["choose", "skip", "state"],
             "game_state": {
                 "screen_type": "BOSS_REWARD",
                 "choice_list": ["black star", "runic dome", "coffee dripper"],
                 "screen_state": {"relics": [
                     {"id": "Black Star"},
                     {"id": "Runic Dome"},
                     {"id": "Coffee Dripper"}]}}}

    def _candidates(self):
        return campaign_driver.expand_legal_actions(self.STATE,
                                                    random.Random(0))

    def test_boss_pick_matches_by_relic_identity(self):
        cmd = spc.match_step({"k": "boss_pick", "relic": "Coffee Dripper"},
                             self.STATE, self._candidates())
        self.assertEqual(cmd, "choose 2")

    def test_boss_skip_uses_the_cancel_alias(self):
        cmd = spc.match_step({"k": "boss_skip"}, self.STATE,
                             self._candidates())
        self.assertEqual(cmd, "skip")

    def test_absent_relic_is_a_divergence(self):
        with self.assertRaises(spc.Divergence):
            spc.match_step({"k": "boss_pick", "relic": "Snecko Eye"},
                           self.STATE, self._candidates())


class ConfigTest(unittest.TestCase):
    def test_unknown_keys_fail_loud(self):
        with tempfile.TemporaryDirectory() as tmp:
            cfg = os.path.join(tmp, "cfg.json")
            with open(cfg, "w", encoding="utf-8") as fh:
                json.dump({"script_dir": tmp, "scripts": "typo"}, fh)
            with self.assertRaises(spc.ConfigError):
                spc.load_config(cfg)

    def test_missing_script_dir_fails_loud(self):
        with self.assertRaises(spc.ConfigError):
            spc.ScriptPolicy({"script_dir": "Z:/does/not/exist"})

    def test_header_step_count_mismatch_fails_loud(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "STS1__sim_search__ps0.script.jsonl")
            lines = make_script_lines("STS1", [{"k": "end"}])
            header = json.loads(lines[0])
            header["steps"] = 7
            lines[0] = json.dumps(header)
            with open(path, "w", encoding="utf-8") as fh:
                fh.write("\n".join(lines) + "\n")
            with self.assertRaises(spc.ConfigError):
                spc.Script(path)


if __name__ == "__main__":
    unittest.main()
