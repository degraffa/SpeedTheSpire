#!/usr/bin/env python3
"""Registry code generator (Stage B design doc §4.3).

Turns the rules-as-data YAML under ``registry/`` into ``constexpr`` C++ headers
under the build tree (``<build>/generated/sts/registry/*.hpp``). The YAML is the
single source of truth; nothing here is hand-maintained C++, and the generated
headers are never committed.

Emitted headers (namespace ``sts::registry``):
  * ``ids.hpp``           -- CardId/PowerId/MonsterId/RelicId/PotionId/EventId
                             enums, each re-pinning every id with a
                             ``static_assert``.
  * ``card_table.hpp``    -- the ``CardDef``/``CardEffectStep`` effect-program
                             table (the shape stage-a §6 froze).
  * ``monster_table.hpp`` -- the ``MonsterDef`` stat/move tables: per-ascension-
                             tier HP and amount columns (design doc §4.2) plus
                             each move's intent and effect program.
  * ``game_ids.hpp``      -- game_id<->enum string tables for the translator.
  * ``manifest.hpp``      -- per-domain row counts.

Determinism (design doc §4.3): entries are emitted sorted by id, domains in a
fixed order, no timestamps -- the same YAML yields byte-identical output on every
run. Any duplicate/reused id, or a missing mandatory field, fails generation with
a clear ``error:`` message on stderr and a non-zero exit.

The generated tables ARE the engine's tables: ``types.hpp``/``cards.hpp``/
``monster_jaw_worm.hpp`` re-export them into ``sts::engine`` via using-aliases
(the skeleton migration, design doc §4.4 -- no hand copies remain). They live in
``sts::registry`` so the headers compile standalone (registry_gen_standalone.cpp)
and tools that never link the engine (the translator) can consume them directly.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import yaml

# --- Frozen vocabularies (mirror the engine headers) ------------------------
# Opcode numbering: interp.hpp (NOP=0 reserved, real opcodes 1..8 in §6 order).
OPCODES = {
    "NOP": 0,
    "DAMAGE": 1,
    "BLOCK": 2,
    "APPLY_POWER": 3,
    "DRAW": 4,
    "GAIN_ENERGY": 5,
    "SHUFFLE_IN": 6,
    "EXHAUST": 7,
    "ROLL_MOVE": 8,
}
# StepTarget: cards.hpp (SELF=0, CARD_TARGET=1).
STEP_TARGETS = {"SELF": 0, "CARD_TARGET": 1}
# CardType: cards.hpp (ATTACK=0, SKILL=1).
CARD_TYPES = {"ATTACK": 0, "SKILL": 1}

# Card-level targeting -> (needs_target, random_target) (cards.hpp semantics).
CARD_TARGETING = {
    "ENEMY": (True, False),
    "ALL_ENEMY": (False, False),
    "SELF": (False, False),
    "NONE": (False, False),
    "RANDOM_ENEMY": (False, True),
    "SELF_AND_ENEMY": (True, False),
}

# Monster-move telegraphed intents (generated MonsterIntent). Values are pinned
# here and append-only, exactly like the id enums (design doc §4.4): fixtures
# store MonsterState.intent as a raw byte. NONE=0 is the value-init default.
MONSTER_INTENTS = {
    "NONE": 0,
    "ATTACK": 1,
    "DEFEND_BUFF": 2,
    "ATTACK_DEFEND": 3,
}
# Monster-move effect target (generated MonsterMoveTarget): SELF = the acting
# monster itself; PLAYER = the player (the game's AbstractDungeon.player).
MONSTER_MOVE_TARGETS = {"SELF": 0, "PLAYER": 1}

_TIER_RE = re.compile(r"^(base|a[1-9][0-9]?)$")


def _tier_threshold(key: str) -> int:
    """'base' -> ascension 0; 'aN' -> ascension N."""
    return 0 if key == "base" else int(key[1:])


# Domains in fixed emission order. `enum` is the generated enum symbol (None ->
# no enum, manifest count only); `underlying` its storage type (types.hpp uses
# u16 for the id enums).
DOMAINS = [
    ("cards", "cards.yaml", "CardId", "uint16_t"),
    ("powers", "powers.yaml", "PowerId", "uint16_t"),
    ("monsters", "monsters.yaml", "MonsterId", "uint16_t"),
    ("relics", "relics.yaml", "RelicId", "uint16_t"),
    ("potions", "potions.yaml", "PotionId", "uint16_t"),
    ("events", "events.yaml", "EventId", "uint16_t"),
    ("encounters", "encounters.yaml", None, None),
    ("a20", "a20.yaml", None, None),
]

_IDENT_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")

BANNER = (
    "// GENERATED FILE -- do not edit by hand.\n"
    "// Produced by tools/registry_gen/gen.py from registry/*.yaml (design doc §4.3).\n"
    "// Edit the YAML and re-run the generator (the CMake custom command does this\n"
    "// automatically at build time). Never committed -- lives under the build tree.\n"
)


class GenError(Exception):
    """A validation failure that should abort generation with a clear message."""


def fail(msg: str) -> "GenError":
    return GenError(msg)


# --- Loading + validation ---------------------------------------------------

def load_domain(registry_dir: Path, filename: str) -> list[dict]:
    path = registry_dir / filename
    if not path.exists():
        raise fail(f"missing registry file: {path}")
    with path.open("r", encoding="utf-8") as fh:
        data = yaml.safe_load(fh)
    if data is None:
        data = []
    if not isinstance(data, list):
        raise fail(f"{filename}: top-level must be a YAML list, got {type(data).__name__}")
    for i, entry in enumerate(data):
        if not isinstance(entry, dict):
            raise fail(f"{filename}[{i}]: each entry must be a mapping")
    return data


def validate_common(filename: str, entries: list[dict], *, require_name: bool,
                    require_game_id: bool) -> None:
    """Validate mandatory fields + id/name uniqueness for one domain."""
    seen_ids: dict[int, str] = {}
    seen_names: dict[str, int] = {}
    for entry in entries:
        if "id" not in entry:
            raise fail(f"{filename}: entry missing mandatory 'id': {entry!r}")
        eid = entry["id"]
        if not isinstance(eid, int) or isinstance(eid, bool):
            raise fail(f"{filename}: id must be an integer, got {eid!r}")
        if eid < 1:
            raise fail(f"{filename}: id must be >= 1 (0 is reserved for NONE), got {eid}")
        label = entry.get("name", entry.get("game_id", "?"))
        if eid in seen_ids:
            raise fail(
                f"{filename}: duplicate/reused id {eid} "
                f"('{label}' collides with '{seen_ids[eid]}') -- "
                f"ids are append-only and must be unique")
        seen_ids[eid] = str(label)

        if "provenance" not in entry or not str(entry.get("provenance", "")).strip():
            raise fail(f"{filename}: entry id {eid} missing mandatory 'provenance'")
        if require_game_id and not str(entry.get("game_id", "")).strip():
            raise fail(f"{filename}: entry id {eid} missing mandatory 'game_id'")

        if require_name:
            name = entry.get("name")
            if not isinstance(name, str) or not _IDENT_RE.match(name):
                raise fail(
                    f"{filename}: entry id {eid} 'name' must be an UPPER_SNAKE "
                    f"enum symbol, got {name!r}")
            if name in seen_names:
                raise fail(f"{filename}: duplicate name '{name}' (ids "
                           f"{seen_names[name]} and {eid})")
            seen_names[name] = eid


def load_registry(registry_dir: Path) -> dict[str, list[dict]]:
    domains: dict[str, list[dict]] = {}
    for key, filename, enum, _underlying in DOMAINS:
        entries = load_domain(registry_dir, filename)
        # a20 has no game_id concept; every other domain requires it. Names are
        # required only for domains that generate an enum.
        validate_common(
            filename, entries,
            require_name=enum is not None,
            require_game_id=(key != "a20"))
        domains[key] = sorted(entries, key=lambda e: e["id"])
    return domains


# --- Emitters ---------------------------------------------------------------

def emit_ids(domains: dict[str, list[dict]]) -> str:
    out: list[str] = [BANNER, "#pragma once\n", "#include <cstdint>\n",
                      "namespace sts::registry {\n"]
    for key, _filename, enum, underlying in DOMAINS:
        if enum is None:
            continue
        entries = domains[key]
        out.append(f"// {enum} -- generated from {key}.yaml (design doc §4.2).")
        out.append(f"enum class {enum} : {underlying} {{")
        out.append("    NONE = 0,")
        for e in entries:
            out.append(f"    {e['name']} = {e['id']},")
        out.append("};\n")
        # Re-pin every id (design doc §4.4): a renumber becomes a compile error.
        for e in entries:
            out.append(
                f"static_assert(static_cast<{underlying}>({enum}::{e['name']}) "
                f"== {e['id']}, \"{enum}::{e['name']} id is pinned to {e['id']} "
                f"(append-only, never renumber)\");")
        out.append("")
    out.append("}  // namespace sts::registry")
    return "\n".join(out) + "\n"


def _power_id_map(domains: dict[str, list[dict]]) -> dict[str, int]:
    return {e["name"]: e["id"] for e in domains["powers"]}


def emit_card_table(domains: dict[str, list[dict]]) -> str:
    cards = domains["cards"]
    powers = _power_id_map(domains)

    # Build the resolved rows first so we can compute kMaxCardSteps and surface
    # validation errors before emitting anything.
    rows = []
    max_steps = 1  # cards.hpp reserves at least 1; skeleton max is 2.
    for c in cards:
        ctype = c.get("type")
        if ctype not in CARD_TYPES:
            raise fail(f"cards.yaml: card {c['name']} has unsupported type "
                       f"{ctype!r} (CardDef supports {sorted(CARD_TYPES)})")
        target = c.get("target")
        if target not in CARD_TARGETING:
            raise fail(f"cards.yaml: card {c['name']} has unknown target "
                       f"{target!r}")
        needs_target, random_target = CARD_TARGETING[target]

        effects = c.get("effects") or []
        steps = []
        for step in effects:
            op = step.get("op")
            if op not in OPCODES:
                raise fail(f"cards.yaml: card {c['name']} uses unknown op {op!r}")
            st = step.get("target")
            if st not in STEP_TARGETS:
                raise fail(f"cards.yaml: card {c['name']} step has unknown "
                           f"target {st!r}")
            amount = int(step.get("amount", 0))
            extra = 0
            if op == "APPLY_POWER":
                pname = step.get("power")
                if pname not in powers:
                    raise fail(f"cards.yaml: card {c['name']} APPLY_POWER "
                               f"references unknown power {pname!r}")
                # make_apply_power_flags: low 16 bits carry the PowerId.
                extra = powers[pname]
            steps.append((OPCODES[op], amount, extra, STEP_TARGETS[st]))
        max_steps = max(max_steps, len(steps))
        rows.append({
            "name": c["name"], "cost": int(c.get("cost", 0)),
            "ctype": CARD_TYPES[ctype], "needs_target": needs_target,
            "random_target": random_target, "steps": steps,
        })

    out: list[str] = [BANNER, "#pragma once\n",
                      "#include <array>", "#include <cstdint>\n",
                      '#include "sts/registry/ids.hpp"\n',
                      "// Effect-program tables in the exact shape of "
                      "include/sts/engine/cards.hpp",
                      "// (design doc §4.3). Types are duplicated in sts::registry "
                      "so this header",
                      "// compiles standalone; registry_gen_test pins them "
                      "byte-equal to the engine's.\n",
                      "namespace sts::registry {\n"]

    # The engine-mirroring type set.
    out.append("enum class Opcode : uint16_t {")
    for name, val in sorted(OPCODES.items(), key=lambda kv: kv[1]):
        out.append(f"    {name} = {val},")
    out.append("};\n")
    out.append("enum class StepTarget : uint8_t {")
    for name, val in sorted(STEP_TARGETS.items(), key=lambda kv: kv[1]):
        out.append(f"    {name} = {val},")
    out.append("};\n")
    out.append("enum class CardType : uint8_t {")
    for name, val in sorted(CARD_TYPES.items(), key=lambda kv: kv[1]):
        out.append(f"    {name} = {val},")
    out.append("};\n")
    out.append("struct CardEffectStep {")
    out.append("    Opcode op;")
    out.append("    int32_t amount;")
    out.append("    uint32_t extra;")
    out.append("    StepTarget target;")
    out.append("};\n")
    out.append(f"inline constexpr int kMaxCardSteps = {max_steps};\n")
    out.append("struct CardDef {")
    out.append("    CardId id;")
    out.append("    uint8_t base_cost;")
    out.append("    CardType type;")
    out.append("    bool needs_target;")
    out.append("    bool random_target;")
    out.append("    uint8_t step_count;")
    out.append("    std::array<CardEffectStep, kMaxCardSteps> steps;")
    out.append("};\n")

    def step_literal(step) -> str:
        op, amount, extra, tgt = step
        op_name = next(k for k, v in OPCODES.items() if v == op)
        tgt_name = next(k for k, v in STEP_TARGETS.items() if v == tgt)
        return (f"{{Opcode::{op_name}, {amount}, {extra}, "
                f"StepTarget::{tgt_name}}}")

    def pad(steps) -> list[str]:
        padded = list(steps)
        while len(padded) < max_steps:
            padded.append((OPCODES["NOP"], 0, 0, STEP_TARGETS["SELF"]))
        return [step_literal(s) for s in padded]

    for r in rows:
        ctype_name = next(k for k, v in CARD_TYPES.items() if v == r["ctype"])
        steps_txt = ",\n        ".join(pad(r["steps"]))
        out.append(f"inline constexpr CardDef k{_pascal(r['name'])}{{")
        out.append(f"    CardId::{r['name']}, {r['cost']}, "
                   f"CardType::{ctype_name}, "
                   f"{'true' if r['needs_target'] else 'false'}, "
                   f"{'true' if r['random_target'] else 'false'}, "
                   f"{len(r['steps'])},")
        out.append("    {{")
        out.append(f"        {steps_txt},")
        out.append("    }}};\n")

    # Lookup table + accessor (mirrors cards.hpp card_def()).
    out.append("inline constexpr std::array<const CardDef*, "
               f"{len(rows)}> kCardDefs{{{{")
    for r in rows:
        out.append(f"    &k{_pascal(r['name'])},")
    out.append("}};\n")
    out.append("[[nodiscard]] inline const CardDef* card_def(CardId id) noexcept {")
    out.append("    switch (id) {")
    for r in rows:
        out.append(f"        case CardId::{r['name']}: "
                   f"return &k{_pascal(r['name'])};")
    out.append("        case CardId::NONE:")
    out.append("        default: return nullptr;")
    out.append("    }")
    out.append("}\n")
    out.append("}  // namespace sts::registry")
    return "\n".join(out) + "\n"


def _pascal(upper_snake: str) -> str:
    return "".join(part.capitalize() for part in upper_snake.split("_"))


# --- Monster table -----------------------------------------------------------

def _parse_tiers(owner: str, what: str, raw) -> list[tuple[int, dict]]:
    """Parse a per-ascension-tier column mapping ({base: ..., aN: ...}) into a
    list of (min_ascension, column) sorted ascending. 'base' (ascension 0) is
    mandatory so every lookup has a floor value."""
    if not isinstance(raw, dict) or not raw:
        raise fail(f"{owner}: {what} must be a non-empty mapping of tier "
                   f"columns (base/aN), got {raw!r}")
    tiers: list[tuple[int, dict]] = []
    for key, col in raw.items():
        if not isinstance(key, str) or not _TIER_RE.match(key):
            raise fail(f"{owner}: {what} has invalid tier key {key!r} "
                       f"(expected 'base' or 'a<N>')")
        tiers.append((_tier_threshold(key), col))
    tiers.sort(key=lambda t: t[0])
    thresholds = [t[0] for t in tiers]
    if len(set(thresholds)) != len(thresholds):
        raise fail(f"{owner}: {what} has duplicate tier thresholds")
    if thresholds[0] != 0:
        raise fail(f"{owner}: {what} must include the 'base' (ascension 0) tier")
    return tiers


def _parse_amount_tiers(owner: str, raw) -> list[tuple[int, int]]:
    """An effect amount: either a flat int (all ascensions) or tier columns."""
    if isinstance(raw, int) and not isinstance(raw, bool):
        return [(0, raw)]
    tiers = _parse_tiers(owner, "amount", raw)
    out = []
    for threshold, value in tiers:
        if not isinstance(value, int) or isinstance(value, bool):
            raise fail(f"{owner}: amount tier value must be an integer, "
                       f"got {value!r}")
        out.append((threshold, value))
    return out


def _parse_monster(entry: dict, powers: dict[str, int]) -> dict:
    name = entry["name"]
    owner = f"monsters.yaml: monster {name}"

    hp_tiers = []
    for threshold, col in _parse_tiers(owner, "hp", entry.get("hp")):
        if (not isinstance(col, dict) or
                not isinstance(col.get("min"), int) or
                not isinstance(col.get("max"), int)):
            raise fail(f"{owner}: hp tier must be {{min: <int>, max: <int>}}, "
                       f"got {col!r}")
        hp_tiers.append((threshold, col["min"], col["max"]))

    raw_moves = entry.get("moves")
    if not isinstance(raw_moves, list) or not raw_moves:
        raise fail(f"{owner}: 'moves' must be a non-empty list")
    moves = []
    seen_move_ids: dict[int, str] = {}
    seen_move_names: set[str] = set()
    for mv in raw_moves:
        mname = mv.get("name")
        if not isinstance(mname, str) or not _IDENT_RE.match(mname):
            raise fail(f"{owner}: move 'name' must be an UPPER_SNAKE symbol, "
                       f"got {mname!r}")
        if mname in seen_move_names:
            raise fail(f"{owner}: duplicate move name '{mname}'")
        seen_move_names.add(mname)
        mid = mv.get("move_id")
        if not isinstance(mid, int) or isinstance(mid, bool) or mid < 1:
            raise fail(f"{owner}: move {mname} 'move_id' must be an integer "
                       f">= 1 (0 is the move_history empty-slot sentinel), "
                       f"got {mid!r}")
        if mid in seen_move_ids:
            raise fail(f"{owner}: duplicate move_id {mid} ('{mname}' collides "
                       f"with '{seen_move_ids[mid]}') -- move ids are the "
                       f"game's byte ids and must be unique per monster")
        seen_move_ids[mid] = mname
        intent = mv.get("intent")
        if intent not in MONSTER_INTENTS:
            raise fail(f"{owner}: move {mname} has unknown intent {intent!r} "
                       f"(known: {sorted(MONSTER_INTENTS)})")

        raw_effects = mv.get("effects")
        if not isinstance(raw_effects, list) or not raw_effects:
            raise fail(f"{owner}: move {mname} 'effects' must be a non-empty list")
        effects = []
        for step in raw_effects:
            op = step.get("op")
            if op not in OPCODES:
                raise fail(f"{owner}: move {mname} uses unknown op {op!r}")
            tgt = step.get("target")
            if tgt not in MONSTER_MOVE_TARGETS:
                raise fail(f"{owner}: move {mname} step has unknown target "
                           f"{tgt!r} (known: {sorted(MONSTER_MOVE_TARGETS)})")
            amount = _parse_amount_tiers(f"{owner}: move {mname}",
                                         step.get("amount"))
            extra = 0
            if op == "APPLY_POWER":
                pname = step.get("power")
                if pname not in powers:
                    raise fail(f"{owner}: move {mname} APPLY_POWER references "
                               f"unknown power {pname!r}")
                extra = powers[pname]
            effects.append({"op": op, "target": tgt, "extra": extra,
                            "amount": amount})
        moves.append({"name": mname, "move_id": mid, "intent": intent,
                      "effects": effects})

    ai = entry.get("ai")
    if ai != "native":
        raise fail(f"{owner}: 'ai' must be 'native' (ai tables are a later "
                   f"task), got {ai!r}")

    return {"name": name, "hp": hp_tiers, "moves": moves, "ai_native": True}


def emit_monster_table(domains: dict[str, list[dict]]) -> str:
    monsters = [_parse_monster(e, _power_id_map(domains))
                for e in domains["monsters"]]

    # Array budgets, computed from the data (floor 1 so the header stays valid
    # while a domain is empty).
    max_stat_tiers = 1
    max_hp_tiers = 1
    max_moves = 1
    max_effects = 1
    for m in monsters:
        max_hp_tiers = max(max_hp_tiers, len(m["hp"]))
        max_moves = max(max_moves, len(m["moves"]))
        for mv in m["moves"]:
            max_effects = max(max_effects, len(mv["effects"]))
            for e in mv["effects"]:
                max_stat_tiers = max(max_stat_tiers, len(e["amount"]))

    out: list[str] = [BANNER, "#pragma once\n",
                      "#include <array>", "#include <cstdint>\n",
                      '#include "sts/registry/card_table.hpp"',
                      '#include "sts/registry/ids.hpp"\n',
                      "// Monster stat/move tables (design doc §4.2): HP ranges "
                      "and move-effect",
                      "// amounts as per-ascension-tier columns, resolved by "
                      "last-matching-threshold",
                      "// lookup (tiers ascend; the highest tier <= the queried "
                      "ascension wins --",
                      "// mirroring the Java's descending ascensionLevel "
                      "if/else-if branches). Move",
                      "// *selection* for ai_native monsters stays in engine "
                      "code; the effects here",
                      "// are the data it enqueues.\n",
                      "namespace sts::registry {\n"]

    out.append("// Telegraphed intent (AbstractMonster.Intent, player-facing "
               "telegraphing).")
    out.append("// Values are pinned and append-only (fixtures store the raw "
               "byte).")
    out.append("enum class MonsterIntent : uint8_t {")
    for iname, val in sorted(MONSTER_INTENTS.items(), key=lambda kv: kv[1]):
        out.append(f"    {iname} = {val},")
    out.append("};")
    for iname, val in sorted(MONSTER_INTENTS.items(), key=lambda kv: kv[1]):
        out.append(f"static_assert(static_cast<uint8_t>(MonsterIntent::{iname}) "
                   f"== {val}, \"MonsterIntent::{iname} is pinned to {val} "
                   f"(append-only, never renumber)\");")
    out.append("")
    out.append("// Where a move-effect step lands: the acting monster itself, "
               "or the player.")
    out.append("enum class MonsterMoveTarget : uint8_t {")
    for tname, val in sorted(MONSTER_MOVE_TARGETS.items(), key=lambda kv: kv[1]):
        out.append(f"    {tname} = {val},")
    out.append("};\n")

    out.append("// One per-ascension-tier column: value applies from "
               "min_ascension upward")
    out.append("// until a higher tier's threshold matches.")
    out.append("struct AscensionTier {")
    out.append("    int32_t min_ascension;")
    out.append("    int32_t value;")
    out.append("};\n")
    out.append(f"inline constexpr int kMaxStatTiers = {max_stat_tiers};\n")
    out.append("struct TieredStat {")
    out.append("    uint8_t tier_count;")
    out.append("    std::array<AscensionTier, kMaxStatTiers> tiers;  "
               "// ascending; [0] is base (0)")
    out.append("    [[nodiscard]] constexpr int32_t at(int32_t ascension) "
               "const noexcept {")
    out.append("        int32_t value = tiers[0].value;")
    out.append("        for (uint8_t i = 1; i < tier_count; ++i) {")
    out.append("            if (ascension >= tiers[i].min_ascension) {")
    out.append("                value = tiers[i].value;")
    out.append("            }")
    out.append("        }")
    out.append("        return value;")
    out.append("    }")
    out.append("};\n")
    out.append("struct MonsterHpTier {")
    out.append("    int32_t min_ascension;")
    out.append("    int32_t hp_min;")
    out.append("    int32_t hp_max;")
    out.append("};\n")
    out.append(f"inline constexpr int kMaxHpTiers = {max_hp_tiers};")
    out.append(f"inline constexpr int kMaxMoveEffects = {max_effects};")
    out.append(f"inline constexpr int kMaxMonsterMoves = {max_moves};\n")
    out.append("struct MonsterMoveEffect {")
    out.append("    Opcode op;")
    out.append("    MonsterMoveTarget target;")
    out.append("    uint32_t extra;      // APPLY_POWER: the PowerId "
               "(make_apply_power_flags packing)")
    out.append("    TieredStat amount;")
    out.append("};\n")
    out.append("struct MonsterMove {")
    out.append("    uint8_t move_id;     // the game's byte move id; never 0")
    out.append("    MonsterIntent intent;")
    out.append("    uint8_t effect_count;")
    out.append("    std::array<MonsterMoveEffect, kMaxMoveEffects> effects;  "
               "// takeTurn addToBottom order")
    out.append("};\n")
    out.append("struct MonsterDef {")
    out.append("    MonsterId id;")
    out.append("    uint8_t hp_tier_count;")
    out.append("    std::array<MonsterHpTier, kMaxHpTiers> hp;  "
               "// ascending; [0] is base (0)")
    out.append("    uint8_t move_count;")
    out.append("    std::array<MonsterMove, kMaxMonsterMoves> moves;")
    out.append("    bool ai_native;      // move selection lives in engine code")
    out.append("    [[nodiscard]] constexpr int32_t hp_min(int32_t ascension) "
               "const noexcept {")
    out.append("        int32_t value = hp[0].hp_min;")
    out.append("        for (uint8_t i = 1; i < hp_tier_count; ++i) {")
    out.append("            if (ascension >= hp[i].min_ascension) {")
    out.append("                value = hp[i].hp_min;")
    out.append("            }")
    out.append("        }")
    out.append("        return value;")
    out.append("    }")
    out.append("    [[nodiscard]] constexpr int32_t hp_max(int32_t ascension) "
               "const noexcept {")
    out.append("        int32_t value = hp[0].hp_max;")
    out.append("        for (uint8_t i = 1; i < hp_tier_count; ++i) {")
    out.append("            if (ascension >= hp[i].min_ascension) {")
    out.append("                value = hp[i].hp_max;")
    out.append("            }")
    out.append("        }")
    out.append("        return value;")
    out.append("    }")
    out.append("    [[nodiscard]] constexpr const MonsterMove* "
               "move(uint8_t move_id) const noexcept {")
    out.append("        for (uint8_t i = 0; i < move_count; ++i) {")
    out.append("            if (moves[i].move_id == move_id) {")
    out.append("                return &moves[i];")
    out.append("            }")
    out.append("        }")
    out.append("        return nullptr;")
    out.append("    }")
    out.append("};\n")

    def tiered_literal(amount: list[tuple[int, int]]) -> str:
        pairs = list(amount)
        while len(pairs) < max_stat_tiers:
            pairs.append((0, 0))
        tiers_txt = ", ".join(f"{{{t}, {v}}}" for t, v in pairs)
        return f"{{{len(amount)}, {{{{{tiers_txt}}}}}}}"

    for m in monsters:
        pname = _pascal(m["name"])
        # Named per-move constants (the game's byte move ids), so engine code
        # and tests reference moves symbolically.
        for mv in m["moves"]:
            out.append(f"inline constexpr uint8_t k{pname}Move"
                       f"{_pascal(mv['name'])} = {mv['move_id']};")
        out.append("")
        out.append(f"inline constexpr MonsterDef k{pname}{{")
        out.append(f"    MonsterId::{m['name']},")
        hp_rows = list(m["hp"])
        while len(hp_rows) < max_hp_tiers:
            hp_rows.append((0, 0, 0))
        hp_txt = ", ".join(f"{{{t}, {lo}, {hi}}}" for t, lo, hi in hp_rows)
        out.append(f"    {len(m['hp'])}, {{{{{hp_txt}}}}},")
        out.append(f"    {len(m['moves'])},")
        out.append("    {{")
        for mv in m["moves"]:
            eff_rows = []
            for e in mv["effects"]:
                eff_rows.append(
                    f"            {{Opcode::{e['op']}, "
                    f"MonsterMoveTarget::{e['target']}, {e['extra']}, "
                    f"{tiered_literal(e['amount'])}}},")
            while len(eff_rows) < max_effects:
                eff_rows.append(
                    "            {Opcode::NOP, MonsterMoveTarget::SELF, 0, "
                    + tiered_literal([(0, 0)]) + "},")
            out.append(f"        {{k{pname}Move{_pascal(mv['name'])}, "
                       f"MonsterIntent::{mv['intent']}, "
                       f"{len(mv['effects'])},")
            out.append("         {{")
            out.extend(eff_rows)
            out.append("         }}},")
        out.append("    }},")
        out.append(f"    {'true' if m['ai_native'] else 'false'}}};\n")

    out.append("[[nodiscard]] inline const MonsterDef* "
               "monster_def(MonsterId id) noexcept {")
    out.append("    switch (id) {")
    for m in monsters:
        out.append(f"        case MonsterId::{m['name']}: "
                   f"return &k{_pascal(m['name'])};")
    out.append("        case MonsterId::NONE:")
    out.append("        default: return nullptr;")
    out.append("    }")
    out.append("}\n")
    out.append("}  // namespace sts::registry")
    return "\n".join(out) + "\n"


def _cpp_string(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit_game_ids(domains: dict[str, list[dict]]) -> str:
    out: list[str] = [BANNER, "#pragma once\n",
                      "#include <cstdint>", "#include <string_view>\n",
                      '#include "sts/registry/ids.hpp"\n',
                      "// game_id <-> enum string tables (design doc §2.6, §4.3):",
                      "// the translator's join between the game's string ids and",
                      "// the sim's u16 enums.\n",
                      "namespace sts::registry {\n"]
    for key, _filename, enum, _underlying in DOMAINS:
        if enum is None:
            continue
        entries = domains[key]
        lname = key[:-1] if key.endswith("s") else key  # cards -> card, etc.
        out.append(f"// --- {enum} <-> game_id ---")
        out.append(f"[[nodiscard]] inline std::string_view "
                   f"{lname}_game_id({enum} id) noexcept {{")
        out.append("    switch (id) {")
        for e in entries:
            out.append(f"        case {enum}::{e['name']}: "
                       f"return {_cpp_string(e['game_id'])};")
        out.append("        case {}::NONE:".format(enum))
        out.append('        default: return "";')
        out.append("    }")
        out.append("}")
        param = "std::string_view s" if entries else "[[maybe_unused]] std::string_view s"
        out.append(f"[[nodiscard]] inline {enum} "
                   f"{lname}_from_game_id({param}) noexcept {{")
        for e in entries:
            out.append(f"    if (s == {_cpp_string(e['game_id'])}) "
                       f"return {enum}::{e['name']};")
        out.append(f"    return {enum}::NONE;")
        out.append("}\n")
    out.append("}  // namespace sts::registry")
    return "\n".join(out) + "\n"


def emit_manifest(domains: dict[str, list[dict]]) -> str:
    out: list[str] = [BANNER, "#pragma once\n", "#include <cstddef>\n",
                      "// Per-domain row counts (design doc §4.3): consumed by the",
                      "// tier-2 coverage check and the §7.4 dashboard.\n",
                      "namespace sts::registry::manifest {\n"]
    total = 0
    for key, _filename, _enum, _underlying in DOMAINS:
        n = len(domains[key])
        total += n
        out.append(f"inline constexpr std::size_t k{_pascal(key)}Count = {n};")
    out.append(f"inline constexpr std::size_t kTotalCount = {total};")
    out.append("\n}  // namespace sts::registry::manifest")
    return "\n".join(out) + "\n"


# --- Driver -----------------------------------------------------------------

def write_if_changed(path: Path, content: str) -> None:
    """Write `content` (LF newlines, no BOM) only if it differs, so a rebuild
    with unchanged YAML does not touch mtimes needlessly."""
    data = content.encode("utf-8")
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as fh:
        fh.write(data)


def generate(registry_dir: Path, out_dir: Path) -> list[Path]:
    domains = load_registry(registry_dir)
    reg_out = out_dir / "sts" / "registry"
    outputs = {
        "ids.hpp": emit_ids(domains),
        "card_table.hpp": emit_card_table(domains),
        "monster_table.hpp": emit_monster_table(domains),
        "game_ids.hpp": emit_game_ids(domains),
        "manifest.hpp": emit_manifest(domains),
    }
    written = []
    for name, content in outputs.items():
        path = reg_out / name
        write_if_changed(path, content)
        written.append(path)
    return written


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="SpeedTheSpire registry codegen")
    parser.add_argument("--registry", required=True, type=Path,
                        help="directory holding the registry/*.yaml files")
    parser.add_argument("--out", required=True, type=Path,
                        help="output root (headers land under <out>/sts/registry/)")
    args = parser.parse_args(argv)
    try:
        written = generate(args.registry, args.out)
    except GenError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    for path in written:
        print(f"generated {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
