#!/usr/bin/env python3
"""Compare three pre-registered full-run aggregates from oracle and simulator."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import subprocess
import tempfile
from collections import Counter

MIN_RUNS = 200
FAMILY_ALPHA = 0.01
MIN_POOLED_TOTAL = 10
AGGREGATES = ("encounters", "reward_rarity", "event_outcomes")
REWARD_RARITIES = {"COMMON", "UNCOMMON", "RARE"}
EVENT_OUTCOMES = {"EVENT", "MONSTER", "SHOP", "TREASURE"}


def gamma_q(a: float, x: float) -> float:
    if x < 0.0 or a <= 0.0:
        raise ValueError("invalid incomplete-gamma arguments")
    if x == 0.0:
        return 1.0
    eps = 1.0e-14
    tiny = 1.0e-300
    if x < a + 1.0:
        term = 1.0 / a
        total = term
        ap = a
        for _ in range(10000):
            ap += 1.0
            term *= x / ap
            total += term
            if abs(term) <= abs(total) * eps:
                break
        p = total * math.exp(-x + a * math.log(x) - math.lgamma(a))
        return max(0.0, min(1.0, 1.0 - p))

    b = x + 1.0 - a
    c = 1.0 / tiny
    d = 1.0 / max(b, tiny)
    h = d
    for i in range(1, 10001):
        an = -float(i) * (float(i) - a)
        b += 2.0
        d = an * d + b
        if abs(d) < tiny:
            d = tiny
        c = b + an / c
        if abs(c) < tiny:
            c = tiny
        d = 1.0 / d
        delta = d * c
        h *= delta
        if abs(delta - 1.0) <= eps:
            break
    return max(
        0.0,
        min(1.0, math.exp(-x + a * math.log(x) - math.lgamma(a)) * h),
    )


def add_oracle_run(path: pathlib.Path, aggregate: dict[str, Counter]) -> int:
    header_seed = None
    terminal = None
    previous_screen = None
    combat_floors: set[int] = set()
    event_floors: set[int] = set()
    event_target_floors: set[int] = set()

    with path.open("r", encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            record = json.loads(line)
            kind = record.get("record_kind")
            if kind == "header":
                if header_seed is not None:
                    raise ValueError(f"{path}: duplicate header")
                header_seed = int(record["seed"]["long"])
                continue
            if kind == "terminal":
                terminal = record
                continue
            if kind != "action":
                continue

            game = record.get("state_json", {}).get("game_state", {})
            floor = int(game.get("floor", 0))
            screen = game.get("screen_type")
            room = game.get("room_type")
            combat = game.get("combat_state") or {}
            monsters = combat.get("monsters") or []

            command = str(record.get("action_command", ""))
            if screen == "MAP" and command.startswith("choose "):
                choice = int(command.split()[1])
                nodes = game.get("screen_state", {}).get("next_nodes") or []
                if choice < 0 or choice >= len(nodes):
                    raise ValueError(
                        f"{path}:{line_number}: map choice outside next_nodes"
                    )
                node = nodes[choice]
                if node.get("symbol") == "?":
                    event_target_floors.add(int(node["y"]) + 1)

            if floor > 0 and monsters and floor not in combat_floors:
                ids = sorted(str(monster["id"]) for monster in monsters)
                aggregate["encounters"]["+".join(ids)] += 1
                combat_floors.add(floor)

            if (
                floor > 0
                and screen == "CARD_REWARD"
                and previous_screen == "COMBAT_REWARD"
            ):
                cards = game.get("screen_state", {}).get("cards") or []
                if not cards:
                    raise ValueError(
                        f"{path}:{line_number}: empty CARD_REWARD screen"
                    )
                for card in cards:
                    rarity = str(card.get("rarity", "UNKNOWN"))
                    aggregate["reward_rarity"][rarity] += 1

            if floor in event_target_floors and floor not in event_floors:
                outcome = None
                if monsters:
                    outcome = "MONSTER"
                elif screen == "SHOP_ROOM":
                    outcome = "SHOP"
                elif screen in {"CHEST", "TREASURE_ROOM"}:
                    outcome = "TREASURE"
                elif screen == "EVENT":
                    event_id = game.get("screen_state", {}).get("event_id")
                    if event_id and event_id != "Neow Event":
                        outcome = "EVENT"
                if outcome is not None:
                    aggregate["event_outcomes"][outcome] += 1
                    event_floors.add(floor)

            previous_screen = screen

    if header_seed is None or terminal is None:
        raise ValueError(f"{path}: capture is not a complete full run")
    if int(terminal.get("act", 0)) != 1:
        raise ValueError(f"{path}: terminal record is not Act 1")
    if terminal.get("outcome") not in {"death", "act1_boss_reward"}:
        raise ValueError(
            f"{path}: non-gameplay terminal {terminal.get('outcome')!r}"
        )
    return header_seed


def load_oracle(campaign: pathlib.Path) -> tuple[list[int], dict[str, Counter]]:
    progress_path = campaign / "campaign_progress.json"
    progress = json.loads(progress_path.read_text(encoding="utf-8"))
    done = progress.get("seeds_done") or []
    failed = progress.get("seeds_failed") or []
    if progress.get("status") != "complete":
        raise ValueError(f"{progress_path}: campaign is not complete")
    if failed:
        raise ValueError(f"{progress_path}: campaign has failed seeds")
    if len(done) < MIN_RUNS:
        raise ValueError(
            f"{progress_path}: {len(done)} complete runs, need {MIN_RUNS}"
        )

    aggregate = {name: Counter() for name in AGGREGATES}
    seeds = []
    artifacts = set()
    for item in done:
        artifact = str(item["artifact"])
        if artifact in artifacts:
            raise ValueError(f"{progress_path}: duplicate artifact {artifact}")
        artifacts.add(artifact)
        seeds.append(add_oracle_run(campaign / artifact, aggregate))
    if len(set(seeds)) != len(seeds):
        raise ValueError(f"{progress_path}: duplicate seed long")
    return seeds, aggregate


def run_simulator(binary: pathlib.Path, seeds: list[int]) -> dict:
    with tempfile.TemporaryDirectory(prefix="sts-dist-spot-") as temp:
        seed_file = pathlib.Path(temp) / "seeds.txt"
        seed_file.write_text(
            "".join(f"{seed}\n" for seed in seeds), encoding="utf-8"
        )
        completed = subprocess.run(
            [str(binary), "--seed-file", str(seed_file)],
            check=False,
            text=True,
            capture_output=True,
        )
    if completed.returncode != 0:
        raise RuntimeError(
            f"simulator failed ({completed.returncode}):\n{completed.stderr}"
        )
    result = json.loads(completed.stdout)
    if result.get("seeds") != len(seeds) or result.get("clean") != len(seeds):
        raise ValueError("simulator did not finish every requested seed cleanly")
    return result


def validate_support(label: str, aggregate: dict) -> None:
    reward_unknown = set(aggregate["reward_rarity"]) - REWARD_RARITIES
    event_unknown = set(aggregate["event_outcomes"]) - EVENT_OUTCOMES
    if reward_unknown:
        raise ValueError(
            f"{label}: reward rarity outside C/U/R: {sorted(reward_unknown)}"
        )
    if event_unknown:
        raise ValueError(
            f"{label}: question-room outcome outside registered support: "
            f"{sorted(event_unknown)}"
        )
    if aggregate["encounters"].get("EMPTY", 0):
        raise ValueError(f"{label}: empty encounter group observed")


def homogeneity(
    oracle: Counter, sim: Counter
) -> tuple[float, int, float, dict[str, list[int]]]:
    names = sorted(set(oracle) | set(sim))
    kept = [name for name in names if oracle[name] + sim[name] >= MIN_POOLED_TOTAL]
    pooled = [name for name in names if name not in kept]
    cells: dict[str, list[int]] = {
        name: [oracle[name], sim[name]] for name in kept
    }
    if pooled:
        cells["OTHER"] = [
            sum(oracle[name] for name in pooled),
            sum(sim[name] for name in pooled),
        ]
    cells = {name: counts for name, counts in cells.items() if sum(counts) > 0}
    if len(cells) < 2:
        raise ValueError("aggregate has fewer than two populated pooled categories")

    row_oracle = sum(counts[0] for counts in cells.values())
    row_sim = sum(counts[1] for counts in cells.values())
    grand = row_oracle + row_sim
    if row_oracle == 0 or row_sim == 0:
        raise ValueError("aggregate has an empty sample")
    statistic = 0.0
    for counts in cells.values():
        column = counts[0] + counts[1]
        for observed, row in ((counts[0], row_oracle), (counts[1], row_sim)):
            expected = row * column / grand
            statistic += (observed - expected) ** 2 / expected
    degrees = len(cells) - 1
    return statistic, degrees, gamma_q(degrees / 2.0, statistic / 2.0), cells


def apply_holm(tests: list[dict]) -> None:
    ordered = sorted(tests, key=lambda test: test["p_value"])
    retain_rest = False
    for index, test in enumerate(ordered):
        threshold = FAMILY_ALPHA / (len(ordered) - index)
        test["holm_threshold"] = threshold
        test["flagged"] = (
            not retain_rest and test["p_value"] <= threshold
        )
        if not test["flagged"]:
            retain_rest = True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--campaign", required=True, type=pathlib.Path)
    parser.add_argument("--sim-bin", required=True, type=pathlib.Path)
    parser.add_argument("--out", type=pathlib.Path)
    args = parser.parse_args()

    seeds, oracle = load_oracle(args.campaign)
    sim = run_simulator(args.sim_bin, seeds)
    validate_support("oracle", oracle)
    validate_support("sim", sim)
    tests = []
    for name in AGGREGATES:
        statistic, degrees, p_value, cells = homogeneity(
            oracle[name], Counter(sim[name])
        )
        tests.append(
            {
                "name": name,
                "chi_square": statistic,
                "degrees_of_freedom": degrees,
                "p_value": p_value,
                "cells": cells,
            }
        )

    apply_holm(tests)
    result = {
        "schema_version": 1,
        "campaign_id": args.campaign.name,
        "cell_order": ["oracle", "simulator"],
        "full_act1_runs": len(seeds),
        "policy": "random-legal",
        "family_alpha": FAMILY_ALPHA,
        "correction": "Holm-Bonferroni",
        "rare_category_pooling_min_combined": MIN_POOLED_TOTAL,
        "tests": tests,
        "flagged": any(test["flagged"] for test in tests),
    }
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 1 if result["flagged"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
