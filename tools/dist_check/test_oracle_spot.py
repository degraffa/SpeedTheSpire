import json
import pathlib
import tempfile
import unittest
from collections import Counter

import oracle_spot


class OracleSpotTest(unittest.TestCase):
    def test_gamma_q_matches_known_one_degree_survival(self):
        self.assertAlmostEqual(
            oracle_spot.gamma_q(0.5, 2.0),
            0.0455002638963584,
            places=12,
        )

    def test_homogeneity_pools_sparse_categories_before_testing(self):
        statistic, degrees, p_value, cells = oracle_spot.homogeneity(
            Counter({"A": 40, "B": 60, "rare-one": 2}),
            Counter({"A": 80, "B": 120, "rare-two": 4}),
        )
        self.assertAlmostEqual(statistic, 0.0)
        self.assertEqual(degrees, 2)
        self.assertAlmostEqual(p_value, 1.0)
        self.assertEqual(cells["OTHER"], [2, 4])

    def test_holm_is_step_down(self):
        tests = [
            {"name": "a", "p_value": 0.001},
            {"name": "b", "p_value": 0.006},
            {"name": "c", "p_value": 0.007},
        ]
        oracle_spot.apply_holm(tests)
        self.assertTrue(tests[0]["flagged"])
        self.assertFalse(tests[1]["flagged"])
        self.assertFalse(tests[2]["flagged"])

    def test_orchestrator_complete_status_is_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            campaign = pathlib.Path(directory)
            (campaign / "campaign_progress.json").write_text(
                json.dumps(
                    {
                        "status": "complete",
                        "seeds_done": [],
                        "seeds_failed": [],
                    }
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                ValueError, "0 complete runs, need 200"
            ):
                oracle_spot.load_oracle(campaign)

    def test_unknown_reward_rarity_is_an_exact_failure(self):
        aggregate = {
            "encounters": Counter({"JawWorm": 1}),
            "reward_rarity": Counter({"SPECIAL": 1}),
            "event_outcomes": Counter({"EVENT": 1}),
        }
        with self.assertRaisesRegex(ValueError, "outside C/U/R"):
            oracle_spot.validate_support("oracle", aggregate)

    def test_complete_capture_extracts_all_three_aggregates(self):
        records = [
            {
                "record_kind": "header",
                "seed": {"long": 12345},
            },
            {
                "record_kind": "action",
                "state_json": {
                    "game_state": {
                        "floor": 1,
                        "screen_type": "NONE",
                        "room_type": "MonsterRoom",
                        "combat_state": {
                            "monsters": [{"id": "LouseNormal"}]
                        },
                    }
                },
            },
            {
                "record_kind": "action",
                "state_json": {
                    "game_state": {
                        "floor": 1,
                        "screen_type": "COMBAT_REWARD",
                        "room_type": "MonsterRoom",
                        "screen_state": {"rewards": [{"reward_type": "CARD"}]},
                    }
                },
            },
            {
                "record_kind": "action",
                "state_json": {
                    "game_state": {
                        "floor": 1,
                        "screen_type": "CARD_REWARD",
                        "room_type": "MonsterRoom",
                        "screen_state": {
                            "cards": [
                                {"id": "Anger", "rarity": "UNCOMMON"},
                                {"id": "Strike_R", "rarity": "COMMON"},
                            ]
                        },
                    }
                },
            },
            {
                "record_kind": "action",
                "action_command": "choose 0",
                "state_json": {
                    "game_state": {
                        "floor": 1,
                        "screen_type": "MAP",
                        "screen_state": {
                            "next_nodes": [{"symbol": "?", "x": 2, "y": 1}]
                        },
                    }
                },
            },
            {
                "record_kind": "action",
                "state_json": {
                    "game_state": {
                        "floor": 2,
                        "screen_type": "EVENT",
                        "room_type": "EventRoom",
                        "screen_state": {"event_id": "Big Fish"},
                    }
                },
            },
            {"record_kind": "terminal", "act": 1, "outcome": "death"},
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "run.jsonl"
            path.write_text(
                "".join(json.dumps(record) + "\n" for record in records),
                encoding="utf-8",
            )
            aggregate = {
                name: Counter() for name in oracle_spot.AGGREGATES
            }
            seed = oracle_spot.add_oracle_run(path, aggregate)
        self.assertEqual(seed, 12345)
        self.assertEqual(aggregate["encounters"]["LouseNormal"], 1)
        self.assertEqual(aggregate["reward_rarity"]["UNCOMMON"], 1)
        self.assertEqual(aggregate["reward_rarity"]["COMMON"], 1)
        self.assertEqual(aggregate["event_outcomes"]["EVENT"], 1)

    def test_event_card_reward_is_excluded_from_combat_reward_rarity(self):
        records = [
            {"record_kind": "header", "seed": {"long": 12345}},
            {
                "record_kind": "action",
                "action_command": "choose 0",
                "state_json": {
                    "game_state": {
                        "floor": 0,
                        "screen_type": "MAP",
                        "screen_state": {
                            "next_nodes": [{"symbol": "?", "x": 2, "y": 0}]
                        },
                    }
                },
            },
            {
                "record_kind": "action",
                "state_json": {
                    "game_state": {
                        "floor": 1,
                        "screen_type": "EVENT",
                        "room_type": "EventRoom",
                        "screen_state": {"event_id": "The Cleric"},
                    }
                },
            },
            {
                "record_kind": "action",
                "state_json": {
                    "game_state": {
                        "floor": 1,
                        "screen_type": "CARD_REWARD",
                        "room_type": "EventRoom",
                        "screen_state": {
                            "cards": [
                                {"id": "Offering", "rarity": "RARE"}
                            ]
                        },
                    }
                },
            },
            {
                "record_kind": "terminal",
                "act": 1,
                "outcome": "death",
            },
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "run.jsonl"
            path.write_text(
                "".join(json.dumps(record) + "\n" for record in records),
                encoding="utf-8",
            )
            aggregate = {
                name: Counter() for name in oracle_spot.AGGREGATES
            }
            oracle_spot.add_oracle_run(path, aggregate)
        self.assertEqual(aggregate["reward_rarity"], Counter())

    def test_driver_failure_terminal_is_not_a_full_run(self):
        records = [
            {"record_kind": "header", "seed": {"long": 12345}},
            {
                "record_kind": "terminal",
                "act": 1,
                "outcome": "cmd_never_ready",
            },
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "run.jsonl"
            path.write_text(
                "".join(json.dumps(record) + "\n" for record in records),
                encoding="utf-8",
            )
            aggregate = {
                name: Counter() for name in oracle_spot.AGGREGATES
            }
            with self.assertRaisesRegex(ValueError, "non-gameplay terminal"):
                oracle_spot.add_oracle_run(path, aggregate)


if __name__ == "__main__":
    unittest.main()
