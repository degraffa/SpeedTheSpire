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

    def test_a_blessing_index_0_does_not_false_match_the_talk_screen(self):
        # Fifth live witness (s2v2_skip_b, STS108173): `neow index 0` must
        # not consume against the one-option talk screen -- glue rule 3
        # answers it and the step waits for the real menu.
        policy = self._policy_with([{"k": "neow", "index": 0},
                                    {"k": "proceed", "ctx": "neow"},
                                    {"k": "map", "x": 1, "sym": "M"}])
        talk = {"available_commands": ["choose", "state"],
                "game_state": {"screen_type": "EVENT",
                               "choice_list": ["talk"],
                               "screen_state": {"event_id": "Neow Event"}}}
        got = policy.decide(decide_request("STS1", talk, ["choose 0"]))
        self.assertEqual(got, "choose 0")
        self.assertEqual(policy._script.cursor, 0)  # NOT consumed
        blessing = {"available_commands": ["choose", "state"],
                    "game_state": {"screen_type": "EVENT",
                                   "choice_list": ["a", "b", "c", "d"],
                                   "screen_state": {
                                       "event_id": "Neow Event"}}}
        got = policy.decide(decide_request(
            "STS1", blessing,
            ["choose 0", "choose 1", "choose 2", "choose 3"]))
        self.assertEqual(got, "choose 0")
        self.assertEqual(policy._script.cursor, 1)  # the real menu consumes

    def test_scripted_single_option_choice_is_consumed_not_glued(self):
        # Match-first: when the sim DID record a one-option EVENT decision
        # (a state-changing single option, which the engine does not
        # collapse), the step matches and is consumed; rule 3 never gets a
        # look-in. (The NEOW kind deliberately refuses this shape -- see
        # the blessing-index-0 test above.)
        policy = self._policy_with([{"k": "event", "event": "X",
                                     "opt": 0, "index": 0}])
        state = {"available_commands": ["choose", "state"],
                 "game_state": {"screen_type": "EVENT",
                                "choice_list": ["leave"],
                                "screen_state": {"event_id": "X"}}}
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

    def test_potion_discards_do_not_make_a_proceed_screen_a_decision(self):
        # divergence_STS100439_ps0: the post-rest campfire aftermath offers
        # `potion discard 0/1` beside its leave-`proceed`; the sim recorded
        # nothing there and the next step is the boss edge.
        policy = self._policy_with([{"k": "map_boss"}])
        rest = {"available_commands": ["potion", "proceed", "state"],
                "game_state": {"screen_type": "REST",
                               "choice_list": [],
                               "screen_state": {}}}
        got = policy.decide(decide_request(
            "STS1", rest, ["potion discard 0", "potion discard 1",
                           "proceed"]))
        self.assertEqual(got, "proceed")
        self.assertEqual(policy._script.cursor, 0)  # nothing consumed
        boss = {"available_commands": ["choose", "state"],
                "game_state": {"screen_type": "MAP",
                               "choice_list": ["boss"],
                               "screen_state": {"boss_available": True}}}
        got = policy.decide(decide_request("STS1", boss, ["choose 0"]))
        self.assertEqual(policy._script.cursor, 1)

    def test_a_scripted_potion_discard_still_matches_before_the_glue(self):
        policy = self._policy_with([{"k": "potion_discard", "slot": 1},
                                    {"k": "map_boss"}])
        rest = {"available_commands": ["potion", "proceed", "state"],
                "game_state": {"screen_type": "REST",
                               "choice_list": [],
                               "screen_state": {},
                               "potions": [{"id": "Block Potion"},
                                           {"id": "Fire Potion"}]}}
        got = policy.decide(decide_request(
            "STS1", rest, ["potion discard 0", "potion discard 1",
                           "proceed"]))
        self.assertEqual(got, "potion discard 1")
        self.assertEqual(policy._script.cursor, 1)  # consumed, not glued

    def test_an_untargeted_potion_accepts_the_unique_targeted_live_form(self):
        # Sixth live witness (s2v2_db153_b, STS108107): the sim records
        # Explosive Potion with t:-1; CommunicationMod expands it as
        # `potion use 0 0`.
        policy = self._policy_with([{"k": "potion", "slot": 0,
                                     "potion": "Explosive Potion", "t": -1}])
        state = {"available_commands": ["potion", "play", "end", "state"],
                 "game_state": {"screen_type": "NONE",
                                "potions": [{"id": "Explosive Potion"}],
                                "screen_state": {}}}
        got = policy.decide(decide_request(
            "STS1", state, ["play 1", "end", "potion use 0 0"]))
        self.assertEqual(got, "potion use 0 0")
        self.assertEqual(policy._script.cursor, 1)

    def test_an_untargeted_potion_with_many_targets_picks_the_lowest(self):
        # Seventh witness (s2v2_skip_108173, floor 20): three monsters ->
        # three target variants for a potion whose effect ignores the
        # target. The lowest index is the deterministic canonical pick; a
        # target that DID matter would diverge downstream and still stop.
        policy = self._policy_with([{"k": "potion", "slot": 1,
                                     "potion": "Explosive Potion",
                                     "t": -1}])
        state = {"available_commands": ["potion", "end", "state"],
                 "game_state": {"screen_type": "NONE",
                                "potions": [{"id": "Block Potion"},
                                            {"id": "Explosive Potion"}],
                                "screen_state": {}}}
        got = policy.decide(decide_request(
            "STS1", state,
            ["end", "potion use 1 2", "potion use 1 0", "potion use 1 1"]))
        self.assertEqual(got, "potion use 1 0")
        self.assertEqual(policy._script.cursor, 1)

    def test_a_usable_belt_potion_does_not_defeat_the_proceed_glue(self):
        # Eighth live witness (s2v3_wave1, STS205404 ps17, floor 6): the
        # vestigial post-SMITH REST `proceed` arrives with the belt's
        # commands beside it, and the belt held an out-of-combat-USABLE
        # potion, so the discard-only filter left two progress candidates
        # and the follower stopped with "script expects the MAP screen,
        # game shows 'REST'". The candidate list below is that divergence
        # record's own, verbatim. The corpus answers this screen with
        # `proceed` (act1_a20_50 STS71037 seq 77-81 walks the whole smith:
        # rest->smith, GRID pick, GRID confirm, THIS screen, map).
        policy = self._policy_with([{"k": "rest", "opt": "smith"},
                                    {"k": "grid", "ctx": "smith",
                                     "card": "Dark Embrace", "up": 0,
                                     "ord": 0},
                                    {"k": "map", "x": 0, "sym": "E"}])
        rest = {"available_commands": ["choose", "potion", "state"],
                "game_state": {"screen_type": "REST",
                               "choice_list": ["rest", "smith", "recall"],
                               "potions": [{"id": "Block Potion"},
                                           {"id": "BloodPotion"}],
                               "screen_state": {}}}
        got = policy.decide(decide_request(
            "STS1", rest, ["choose 0", "choose 1", "choose 2",
                           "potion discard 0", "potion use 1",
                           "potion discard 1"]))
        self.assertEqual(got, "choose 1")  # smith
        self.assertEqual(policy._script.cursor, 1)
        grid = {"available_commands": ["choose", "cancel", "state"],
                "game_state": {"screen_type": "GRID",
                               "choice_list": ["dark embrace"],
                               "screen_state": {"confirm_up": False,
                                                "selected_cards": [],
                                                "cards": [
                                                    {"id": "Dark Embrace",
                                                     "upgrades": 0}]}}}
        got = policy.decide(decide_request("STS1", grid,
                                           ["choose 0", "cancel"]))
        self.assertEqual(got, "choose 0")
        self.assertEqual(policy._script.cursor, 2)
        # The witness state: REST again, `proceed` plus the belt.
        aftermath = {"available_commands": ["potion", "proceed", "state"],
                     "game_state": {"screen_type": "REST",
                                    "choice_list": [],
                                    "potions": [{"id": "Block Potion"},
                                                {"id": "BloodPotion"}],
                                    "screen_state": {}}}
        got = policy.decide(decide_request(
            "STS1", aftermath, ["potion discard 0", "potion use 1",
                                "potion discard 1", "proceed"]))
        self.assertEqual(got, "proceed")
        self.assertEqual(policy._script.cursor, 2)  # nothing consumed
        map_state = {"available_commands": ["choose", "return", "state"],
                     "game_state": {"screen_type": "MAP",
                                    "choice_list": ["x=0"],
                                    "screen_state": {"next_nodes": [
                                        {"x": 0, "symbol": "E"}]}}}
        got = policy.decide(decide_request("STS1", map_state,
                                           ["choose 0", "return"]))
        self.assertEqual(got, "choose 0")
        self.assertEqual(policy._script.cursor, 3)

    def test_the_post_heal_rest_screen_glues_with_one_belt_potion(self):
        # The same seam without a grid in the middle, and with a
        # single-slot belt: STS216298 ps107 (floor 46, Act 3) scripts
        # `rest opt=rest` at step 562 and the map at 563, and the game puts
        # the vestigial `proceed` between them with candidates
        # ['potion use 0', 'potion discard 0', 'proceed'] -- the shape
        # STS227212 ps88 also hit at floor 6 after a smith. So the screen
        # follows EVERY rest-site option, not just the smith, and one
        # usable potion is enough to defeat a discard-only filter.
        policy = self._policy_with([{"k": "rest", "opt": "rest"},
                                    {"k": "map", "x": 2, "sym": "M"}])
        rest = {"available_commands": ["choose", "potion", "state"],
                "game_state": {"screen_type": "REST",
                               "choice_list": ["rest", "smith", "recall"],
                               "potions": [{"id": "EntropicBrew"}],
                               "screen_state": {}}}
        got = policy.decide(decide_request(
            "STS1", rest, ["choose 0", "choose 1", "choose 2",
                           "potion use 0", "potion discard 0"]))
        self.assertEqual(got, "choose 0")  # rest/heal
        self.assertEqual(policy._script.cursor, 1)
        aftermath = {"available_commands": ["potion", "proceed", "state"],
                     "game_state": {"screen_type": "REST",
                                    "choice_list": [],
                                    "potions": [{"id": "EntropicBrew"}],
                                    "screen_state": {}}}
        got = policy.decide(decide_request(
            "STS1", aftermath,
            ["potion use 0", "potion discard 0", "proceed"]))
        self.assertEqual(got, "proceed")
        self.assertEqual(policy._script.cursor, 1)  # nothing consumed
        map_state = {"available_commands": ["choose", "return", "state"],
                     "game_state": {"screen_type": "MAP",
                                    "choice_list": ["x=2"],
                                    "screen_state": {"next_nodes": [
                                        {"x": 2, "symbol": "M"}]}}}
        got = policy.decide(decide_request("STS1", map_state,
                                           ["choose 0", "return"]))
        self.assertEqual(got, "choose 0")
        self.assertEqual(policy._script.cursor, 2)

    def test_a_scripted_potion_use_still_matches_before_the_glue(self):
        # The belt filter must not swallow a decision the sim DID record:
        # match-first runs before any glue rule consults it.
        policy = self._policy_with([{"k": "potion", "slot": 1,
                                     "potion": "BloodPotion", "t": -1},
                                    {"k": "map_boss"}])
        rest = {"available_commands": ["potion", "proceed", "state"],
                "game_state": {"screen_type": "REST",
                               "choice_list": [],
                               "potions": [{"id": "Block Potion"},
                                           {"id": "BloodPotion"}],
                               "screen_state": {}}}
        got = policy.decide(decide_request(
            "STS1", rest, ["potion discard 0", "potion use 1",
                           "potion discard 1", "proceed"]))
        self.assertEqual(got, "potion use 1")
        self.assertEqual(policy._script.cursor, 1)  # consumed, not glued

    def test_belt_commands_do_not_make_an_unrelated_screen_gluable(self):
        # The negative control for both belt rules: a screen with a REAL
        # decision beside the belt still stops. Two progress candidates
        # remain after the filter, so neither the proceed glue nor the
        # one-click glue may fire.
        policy = self._policy_with([{"k": "map", "x": 0, "sym": "E"}])
        event = {"available_commands": ["choose", "potion", "state"],
                 "game_state": {"screen_type": "EVENT",
                                "choice_list": ["a", "b"],
                                "potions": [{"id": "BloodPotion"}],
                                "screen_state": {"event_id": "X"}}}
        with self.assertRaises(spc.Divergence):
            policy.decide(decide_request(
                "STS1", event, ["choose 0", "choose 1", "potion use 0",
                                "potion discard 0"]))
        self.assertEqual(policy._script.cursor, 0)  # stopped ON the step

    def test_a_belt_potion_does_not_defeat_the_one_click_glue(self):
        # The same belt exclusion on glue rule 3, third shape of the seam
        # and NOT a rest site: divergence_STS221674_ps7 (floor 39, Act 3,
        # Sensory Stone). The sim's line is two `event` picks, a
        # COMBAT_REWARD `proceed`, then the map; the live event's closing
        # one-click `leave` page -- the class the engine collapses --
        # arrives as ['choose 0', 'potion use 0', 'potion discard 0'], so
        # the discard-only filter left two candidates and rule 3 did not
        # fire. The record's own choice_list and candidates are below.
        policy = self._policy_with([{"k": "map", "x": 1, "sym": "R"}])
        leave = {"available_commands": ["choose", "potion", "state"],
                 "game_state": {"screen_type": "EVENT",
                                "choice_list": ["leave"],
                                "potions": [{"id": "BloodPotion"}],
                                "screen_state": {"event_id": "SensoryStone"}}}
        got = policy.decide(decide_request(
            "STS1", leave,
            ["choose 0", "potion use 0", "potion discard 0"]))
        self.assertEqual(got, "choose 0")
        self.assertEqual(policy._script.cursor, 0)  # nothing consumed
        map_state = {"available_commands": ["choose", "return", "state"],
                     "game_state": {"screen_type": "MAP",
                                    "choice_list": ["x=1"],
                                    "screen_state": {"next_nodes": [
                                        {"x": 1, "symbol": "R"}]}}}
        got = policy.decide(decide_request("STS1", map_state,
                                           ["choose 0", "return"]))
        self.assertEqual(got, "choose 0")
        self.assertEqual(policy._script.cursor, 1)

    def test_the_one_click_glue_answers_the_choose_not_a_belt_command(self):
        # The glue must read the sole PROGRESS candidate, not
        # `candidates[0]`: the belt's position in the list is not
        # contractual, and answering a potion here would spend it.
        policy = self._policy_with([{"k": "neow", "index": 3}])
        talk = {"available_commands": ["choose", "potion", "state"],
                "game_state": {"screen_type": "EVENT",
                               "choice_list": ["talk"],
                               "potions": [{"id": "BloodPotion"}],
                               "screen_state": {"event_id": "Neow Event"}}}
        got = policy.decide(decide_request(
            "STS1", talk, ["potion use 0", "potion discard 0", "choose 0"]))
        self.assertEqual(got, "choose 0")
        self.assertEqual(policy._script.cursor, 0)  # nothing consumed

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


class LibraryGridTest(unittest.TestCase):
    """The Library's read pick reaches the follower as a GRID, not an EVENT.

    S2.43, STS100009 ps0, step 224: the emitter used to write
    `{"k":"event","event":"The Library","opt":7,"index":7}` and `_match_event`
    stopped on the screen kind. The card list below is the archived
    divergence record's own `choice_list` -- the twenty live rows, in the
    LIVE order, which is the sim board's roll order REVERSED
    (`group.addToBottom` is `group.add(0, c)`, CardGroup.java:459-461). No
    follower arm was added for this: `_match_grid` already joins
    `screen_state.cards` by identity, and these tests pin that it does.
    """

    # Live order, from divergence_STS100009_ps0.
    LIVE = ["Power Through", "Thunderclap", "True Grit", "Anger",
            "Shrug It Off", "Dual Wield", "Whirlwind", "Body Slam",
            "Wild Strike", "Battle Trance", "Ghostly Armor",
            "Perfected Strike", "Rage", "Clash", "Clothesline", "Headbutt",
            "Warcry", "Feel No Pain", "Searing Blow", "Dropkick"]

    STATE = {"available_commands": ["choose", "state"],
             "game_state": {
                 "screen_type": "GRID",
                 "choice_list": [name.lower() for name in LIVE],
                 "screen_state": {
                     "confirm_up": False,
                     "selected_cards": [],
                     "cards": [{"id": name, "upgrades": 0} for name in LIVE]}}}

    def _candidates(self):
        return campaign_driver.expand_legal_actions(self.STATE,
                                                    random.Random(0))

    def test_identity_join_finds_the_reversed_row(self):
        # The sim's board slot 7 (`opt 7`, the pick that stopped the line) is
        # the eighth ROLL, which the live grid shows twelfth from the top.
        # The emitter names the card, so the follower lands there without
        # either side doing index arithmetic.
        cmd = spc.match_step(
            {"k": "grid", "ctx": "library", "event": "The Library",
             "card": "Rage", "up": 0, "ord": 0, "opt": 7},
            self.STATE, self._candidates())
        self.assertEqual(cmd, "choose 12")
        # And the old step's index would have flipped a different card --
        # the silent half of the bug, had the screen kind not caught it.
        self.assertEqual(self.LIVE[7], "Body Slam")

    def test_every_row_is_reachable_by_identity(self):
        candidates = self._candidates()
        for i, name in enumerate(self.LIVE):
            cmd = spc.match_step(
                {"k": "grid", "ctx": "library", "card": name, "up": 0,
                 "ord": 0}, self.STATE, candidates)
            self.assertEqual(cmd, "choose %d" % i)

    def test_a_card_the_grid_does_not_hold_still_stops(self):
        with self.assertRaises(spc.Divergence):
            spc.match_step(
                {"k": "grid", "ctx": "library", "card": "Bash", "up": 0,
                 "ord": 0}, self.STATE, self._candidates())

    def test_a_bare_index_event_step_is_still_refused_here(self):
        # The stop that produced the finding, pinned: nothing about this fix
        # relaxes `_match_event`'s screen-kind check.
        with self.assertRaises(spc.Divergence):
            spc.match_step(
                {"k": "event", "event": "The Library", "opt": 7, "index": 7},
                self.STATE, self._candidates())


class DiscoverySkipTest(unittest.TestCase):
    """A `choose_card` step whose decision is the Skip button.

    Ninth live witness, s2v3_wave1 STS209702 ps255 at floor 50: the sim's
    line took a Skill Potion (step 435) and SKIPPED its three-card
    discovery (step 436, `{"k":"choose_card","src":"generated","card":"",
    "skip":1}`). The follower read `card` first and stopped with "grid has
    no #0 copy of ''+0" against the live CARD_REWARD below, which is that
    divergence record's own screen. The emitter's shape is unambiguous
    (planner/src/script.cpp, the COMBAT arm): `card:""` + `skip:1` is
    written for CHOOSE(kChooseSkipCard) and no card identity is emitted
    with it, so `skip` is the only readable field.
    """

    # divergence_STS209702_ps255's own choice_list / candidates.
    STATE = {"available_commands": ["choose", "potion", "skip", "state"],
             "game_state": {
                 "screen_type": "CARD_REWARD",
                 "choice_list": ["flex", "bloodletting", "entrench"],
                 "screen_state": {"cards": [
                     {"id": "Flex", "upgrades": 0},
                     {"id": "Bloodletting", "upgrades": 0},
                     {"id": "Entrench", "upgrades": 0}]}}}

    CANDIDATES = ["choose 0", "choose 1", "choose 2", "skip"]

    def test_a_generated_skip_step_answers_the_live_skip(self):
        cmd = spc.match_step(
            {"k": "choose_card", "src": "generated", "card": "", "skip": 1},
            self.STATE, self.CANDIDATES)
        self.assertEqual(cmd, "skip")

    def test_the_skip_step_is_consumed_like_any_other_match(self):
        # Not glue: the discovery skip is a decision the sim recorded, so
        # the cursor advances and the next step faces the next screen.
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        steps = [{"k": "potion", "slot": 0, "potion": "SkillPotion",
                  "t": -1},
                 {"k": "choose_card", "src": "generated", "card": "",
                  "skip": 1},
                 {"k": "end"}]
        path = os.path.join(tmp.name, "STS1__sim_search__ps0.script.jsonl")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("\n".join(make_script_lines("STS1", steps)) + "\n")
        policy = spc.ScriptPolicy({"script_dir": tmp.name})
        combat = {"available_commands": ["potion", "end", "state"],
                  "game_state": {"screen_type": "NONE",
                                 "potions": [{"id": "SkillPotion"}],
                                 "screen_state": {}}}
        got = policy.decide(decide_request("STS1", combat,
                                           ["potion use 0", "end"]))
        self.assertEqual(got, "potion use 0")
        got = policy.decide(decide_request("STS1", self.STATE,
                                           self.CANDIDATES))
        self.assertEqual(got, "skip")
        self.assertEqual(policy._script.cursor, 2)

    def test_a_pile_sourced_skip_is_honoured_too(self):
        # `skip` is read before `src` is consulted. Today only
        # `src: "generated"` can carry it (ActionMask::can_skip_choice is
        # false unless choice_from_generated), so this pins the RULE, not a
        # shape the emitter writes: the flag is the decision wherever it
        # appears, and a hand/grid screen exposes the alias as `cancel`.
        state = {"available_commands": ["choose", "cancel", "state"],
                 "game_state": {"screen_type": "HAND_SELECT",
                                "choice_list": ["strike"],
                                "screen_state": {"hand": [
                                    {"id": "Strike_R", "upgrades": 0}]}}}
        cmd = spc.match_step(
            {"k": "choose_card", "src": "hand", "card": "", "skip": 1},
            state, ["choose 0", "cancel"])
        self.assertEqual(cmd, "cancel")

    def test_a_skip_step_on_a_screen_with_no_skip_alias_still_stops(self):
        # The stop contract is untouched: honouring `skip` is a matcher
        # arm, not a licence to improvise when the game offers no way out.
        with self.assertRaises(spc.Divergence):
            spc.match_step(
                {"k": "choose_card", "src": "generated", "card": "",
                 "skip": 1},
                self.STATE, ["choose 0", "choose 1", "choose 2"])

    def test_a_choose_card_without_skip_still_joins_by_identity(self):
        # The un-skipped discovery: `skip` absent means the identity join
        # is still the whole rule.
        cmd = spc.match_step(
            {"k": "choose_card", "src": "generated", "card": "Entrench",
             "up": 0, "ord": 0, "index": 2},
            self.STATE, self.CANDIDATES)
        self.assertEqual(cmd, "choose 2")
        with self.assertRaises(spc.Divergence):
            spc.match_step(
                {"k": "choose_card", "src": "generated", "card": "Bash",
                 "up": 0, "ord": 0, "index": 0},
                self.STATE, self.CANDIDATES)


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
