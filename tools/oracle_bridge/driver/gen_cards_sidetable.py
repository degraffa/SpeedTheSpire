#!/usr/bin/env python3
"""Generate `cards_sidetable.json` -- the greedy driver policy's card numbers.

WHY THIS EXISTS AS A COMMITTED JSON + A COMMITTED SCRIPT, and not as a new
`tools/registry_gen` emit module (the alternative the B4.x brief offered):

  * `tools/registry_gen/gen.py` writes into the **build tree**
    (`<out>/sts/registry/*.hpp`) and its own docstring states the generated
    headers "are never committed". The campaign driver runs on the Windows host,
    spawned by the game through CommunicationMod's `config.properties` command;
    there is no CMake build tree in that process's world (the build lives in
    WSL). A registry_gen output would simply not be reachable at capture time.
  * `campaign_driver.py` is deliberately **standard-library only** ("it runs
    under the game's own Windows Python, outside the WSL build/CI world"), while
    the registry loader needs PyYAML (`stsgen/loader.py:13`), a codegen-only
    dependency under conventions.md 5. Reading `registry/cards.yaml` at capture
    time would break that constraint; loading a checked-in JSON does not.
  * registry_gen's contract is "generated, never committed". A JSON that must be
    committed to be usable would be a third category the contract does not have,
    and nothing in the WSL build would keep it in sync with a Windows-host
    consumer anyway.

So: this script is the generator, `cards_sidetable.json` is its committed
output, and `test_oracle_campaign.py::CardSideTableTest` re-derives the table
from `registry/cards.yaml` and fails if the committed JSON has drifted. That
test is the sync check the codegen path would otherwise have provided.

SCORING SHAPE -- deliberately a one-to-one mirror of the simulator's fuzz
policy, `tools/fuzz/src/policy.cpp` `score_card` (the `Opcode` switch in the
anonymous namespace). Same opcode buckets, same "sum the amounts" crudeness,
including the same known under-counts: DAMAGE_GREED / DAMAGE_FEED /
DAMAGE_DRAW_PILE / VAMPIRE_DAMAGE_ALL are NOT summed there and are not summed
here, so Reaper and Feed score as zero-damage utility. Mirroring beats being
cleverer than the sim: when the two policies disagree the difference should be
the transport, not the heuristic.

Usage (Windows host, PyYAML required -- this is a dev-time tool, not a runtime
dependency of the driver):

    python tools/oracle_bridge/driver/gen_cards_sidetable.py \
        --registry registry/cards.yaml \
        --out tools/oracle_bridge/driver/cards_sidetable.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys

# Mirrors tools/fuzz/src/policy.cpp score_card's `case` labels exactly.
DAMAGE_OPCODES = (
    "DAMAGE",
    "DAMAGE_STR_MULT",
    "DAMAGE_PER_STRIKE",
    "DAMAGE_UPGRADE_SCALE",
    "DAMAGE_RAMPAGE",
)
BLOCK_OPCODES = ("BLOCK",)
# Its `case Opcode::DAMAGE_BLOCK: sc.damage += cs.player_block;` -- the amount is
# not in the registry at all, it is the player's live block. Flagged per card so
# the driver can add `combat_state.player.block` at scoring time.
DYNAMIC_BLOCK_DAMAGE_OPCODE = "DAMAGE_BLOCK"

AOE_TARGETS = ("ALL_ENEMY",)


def score_effects(steps):
    """(damage, block, damage_from_block) for one effect program.

    Pure function of the registry rows -- no game state, no I/O.
    """
    damage = 0
    block = 0
    from_block = False
    for step in steps or []:
        op = step.get("op")
        amount = step.get("amount") or 0
        if op in DAMAGE_OPCODES:
            damage += amount
        elif op in BLOCK_OPCODES:
            block += amount
        elif op == DYNAMIC_BLOCK_DAMAGE_OPCODE:
            from_block = True
    return damage, block, from_block


def build_table(cards):
    """registry/cards.yaml rows -> the side table's `cards` object."""
    out = {}
    for row in cards:
        game_id = row.get("game_id")
        if not game_id:
            raise ValueError(f"card row {row.get('id')!r} has no game_id")
        if game_id in out:
            raise ValueError(f"duplicate game_id {game_id!r}")
        base = score_effects(row.get("effects"))
        upg = score_effects(row.get("upgraded")) if row.get("upgraded") \
            else base
        out[game_id] = {
            # [base, upgraded]; upgraded repeats base when the registry has no
            # upgraded program (cards.yaml header: the upgrade dimension is
            # optional and absent rows are base-only).
            "damage": [base[0], upg[0]],
            "block": [base[1], upg[1]],
            "damage_from_block": bool(base[2] or upg[2]),
            "aoe": row.get("target") in AOE_TARGETS,
            "type": row.get("type"),
            "cost": row.get("cost"),
        }
    return out


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def build_document(registry_path, cards):
    """The full JSON document, including its provenance header."""
    return {
        "_provenance": {
            "generated_by":
                "tools/oracle_bridge/driver/gen_cards_sidetable.py",
            "source": "registry/cards.yaml",
            "source_sha256": sha256_file(registry_path),
            "source_rows": len(cards),
            "mirrors": "tools/fuzz/src/policy.cpp score_card (opcode buckets)",
            "damage_opcodes": list(DAMAGE_OPCODES),
            "block_opcodes": list(BLOCK_OPCODES),
            "dynamic": {
                DYNAMIC_BLOCK_DAMAGE_OPCODE:
                    "damage == the player's live block; the driver adds "
                    "combat_state.player.block at scoring time",
            },
            "known_undercounts": [
                "DAMAGE_GREED", "DAMAGE_FEED", "DAMAGE_DRAW_PILE",
                "VAMPIRE_DAMAGE_ALL",
            ],
            "scope": "registry/cards.yaml is S1 only (Ironclad + colorless + "
                     "curses/statuses). A card the live game offers that is "
                     "absent here scores as zero-damage cheap utility; the "
                     "driver never raises on an unknown id.",
            "regenerate":
                "python tools/oracle_bridge/driver/gen_cards_sidetable.py "
                "--registry registry/cards.yaml --out "
                "tools/oracle_bridge/driver/cards_sidetable.json",
            "checked_by":
                "test_oracle_campaign.py::CardSideTableTest",
        },
        "cards": build_table(cards),
    }


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", "..", ".."))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--registry",
                    default=os.path.join(repo, "registry", "cards.yaml"))
    ap.add_argument("--out",
                    default=os.path.join(here, "cards_sidetable.json"))
    args = ap.parse_args(argv)

    try:
        import yaml
    except ImportError:
        print("error: PyYAML is required to regenerate the side table "
              "(dev-time only; the driver itself never imports it)",
              file=sys.stderr)
        return 2

    with open(args.registry, "r", encoding="utf-8") as fh:
        cards = yaml.safe_load(fh)
    document = build_document(args.registry, cards)
    with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(document, fh, indent=2, sort_keys=True)
        fh.write("\n")
    print(f"wrote {args.out} ({len(document['cards'])} cards)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
