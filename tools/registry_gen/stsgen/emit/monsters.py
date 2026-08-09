"""monster_table.hpp -- the MonsterDef stat/move tables (design doc §4.2).

A monster move program is the same op vocabulary as a card/power/relic/potion
program, but its steps carry a per-ascension-tier `amount` column and target the
acting monster or the player instead of a StepTarget -- so it keeps its own step
loop while sharing the op allowlist and the `extra` packing table (stsgen.steps).
"""

from __future__ import annotations

from ..loader import card_id_map, power_id_map
from ..steps import MONSTER_DOMAIN, check_op, pack_extra
from ..vocab import (BANNER, MONSTER_INTENTS, MONSTER_MOVE_TARGETS,
                     MONSTER_ROLL_STREAMS, MONSTER_ROLL_TIMINGS, IDENT_RE,
                     TIER_RE, fail, pascal, tier_threshold)

# AbstractMonster.EnemyType (AbstractMonster.java:99 declares the field with
# `public EnemyType type = EnemyType.NORMAL;` -- NORMAL is the Java's own
# field-initializer default, which is why the yaml key is optional and why
# omitting it means exactly what a Java monster class that never assigns
# `this.type` means). Only the classes that assign it are non-NORMAL:
# GremlinNob.java:63 / Lagavulin.java:75 / Sentry.java:61 set ELITE;
# SlimeBoss.java:86 / TheGuardian.java:94 / Hexaghost.java:98 set BOSS.
#
# Values are pinned and append-only, like every other generated enum
# (design doc §4.4).
MONSTER_ENEMY_TYPES = {
    "NORMAL": 0,
    "ELITE": 1,
    "BOSS": 2,
}


def parse_tiers(owner: str, what: str, raw) -> list[tuple[int, dict]]:
    """Parse a per-ascension-tier column mapping ({base: ..., aN: ...}) into a
    list of (min_ascension, column) sorted ascending. 'base' (ascension 0) is
    mandatory so every lookup has a floor value."""
    if not isinstance(raw, dict) or not raw:
        raise fail(f"{owner}: {what} must be a non-empty mapping of tier "
                   f"columns (base/aN), got {raw!r}")
    tiers: list[tuple[int, dict]] = []
    for key, col in raw.items():
        if not isinstance(key, str) or not TIER_RE.match(key):
            raise fail(f"{owner}: {what} has invalid tier key {key!r} "
                       f"(expected 'base' or 'a<N>')")
        tiers.append((tier_threshold(key), col))
    tiers.sort(key=lambda t: t[0])
    thresholds = [t[0] for t in tiers]
    if len(set(thresholds)) != len(thresholds):
        raise fail(f"{owner}: {what} has duplicate tier thresholds")
    if thresholds[0] != 0:
        raise fail(f"{owner}: {what} must include the 'base' (ascension 0) tier")
    return tiers


def parse_amount_tiers(owner: str, raw) -> list[tuple[int, int]]:
    """An effect amount: either a flat int (all ascensions) or tier columns."""
    if isinstance(raw, int) and not isinstance(raw, bool):
        return [(0, raw)]
    tiers = parse_tiers(owner, "amount", raw)
    out = []
    for threshold, value in tiers:
        if not isinstance(value, int) or isinstance(value, bool):
            raise fail(f"{owner}: amount tier value must be an integer, "
                       f"got {value!r}")
        out.append((threshold, value))
    return out


def parse_monster(entry: dict, powers: dict[str, int],
                  cards: dict[str, int]) -> dict:
    name = entry["name"]
    owner = f"monsters.yaml: monster {name}"

    hp_tiers = []
    for threshold, col in parse_tiers(owner, "hp", entry.get("hp")):
        if (not isinstance(col, dict) or
                not isinstance(col.get("min"), int) or
                not isinstance(col.get("max"), int)):
            raise fail(f"{owner}: hp tier must be {{min: <int>, max: <int>}}, "
                       f"got {col!r}")
        hp_tiers.append((threshold, col["min"], col["max"]))

    rolls = []
    seen_roll_names: set[str] = set()
    raw_rolls = entry.get('rolls', [])
    if not isinstance(raw_rolls, list):
        raise fail(f'{owner}: optional rolls must be a list, got {raw_rolls!r}')
    for roll in raw_rolls:
        if not isinstance(roll, dict):
            raise fail(f'{owner}: each roll must be a mapping, got {roll!r}')
        rname = roll.get('name')
        if not isinstance(rname, str) or not IDENT_RE.match(rname):
            raise fail(f'{owner}: roll name must be an UPPER_SNAKE symbol, '
                       f'got {rname!r}')
        if rname in seen_roll_names:
            raise fail(f'{owner}: duplicate roll name {rname!r}')
        seen_roll_names.add(rname)
        stream = roll.get('stream')
        if stream not in MONSTER_ROLL_STREAMS:
            raise fail(f'{owner}: roll {rname} has unknown stream {stream!r} '
                       f'(known: {sorted(MONSTER_ROLL_STREAMS)})')
        timing = roll.get('timing')
        if timing not in MONSTER_ROLL_TIMINGS:
            raise fail(f'{owner}: roll {rname} has unknown timing {timing!r} '
                       f'(known: {sorted(MONSTER_ROLL_TIMINGS)})')
        ranges = []
        for threshold, col in parse_tiers(
                f'{owner}: roll {rname}', 'range', roll.get('range')):
            if (not isinstance(col, dict) or
                    not isinstance(col.get('min'), int) or
                    not isinstance(col.get('max'), int)):
                raise fail(f'{owner}: roll {rname} range tier must be '
                           f'{{min: <int>, max: <int>}}, got {col!r}')
            if col['min'] > col['max']:
                raise fail(f'{owner}: roll {rname} range min exceeds max '
                           f'({col["min"]} > {col["max"]})')
            ranges.append((threshold, col['min'], col['max']))
        rolls.append({'name': rname, 'stream': stream, 'timing': timing,
                      'ranges': ranges})

    raw_moves = entry.get("moves")
    if not isinstance(raw_moves, list) or not raw_moves:
        raise fail(f"{owner}: 'moves' must be a non-empty list")
    moves = []
    seen_move_ids: dict[int, str] = {}
    seen_move_names: set[str] = set()
    for mv in raw_moves:
        mname = mv.get("name")
        if not isinstance(mname, str) or not IDENT_RE.match(mname):
            raise fail(f"{owner}: move 'name' must be an UPPER_SNAKE symbol, "
                       f"got {mname!r}")
        if mname in seen_move_names:
            raise fail(f"{owner}: duplicate move name '{mname}'")
        seen_move_names.add(mname)
        mid = mv.get("move_id")
        # move_id is the GAME'S BYTE ID and >= 0 is the whole constraint.
        #
        # This check demanded >= 1 until S2.26, on the grounds that 0 is
        # MonsterState.move_history's empty-slot sentinel (monster_dispatch.hpp:
        # 29-31, where last_move_is is a bare `move_history[0] == move`). Then a
        # monster with a 0 id arrived: WrithingMass.BIG_HIT (WrithingMass.java:48).
        # The id is not negotiable -- monsters.yaml says move_id IS the game's
        # byte id, and renumbering it to dodge a loader rule would make the row
        # lie about the source -- so the rule was re-derived instead of worked
        # around (conventions section 8: the prerequisite arrived, so amend rather
        # than route around).
        #
        # WHAT THE SENTINEL ACTUALLY PROTECTS, and why 0 is admissible: an id-0
        # move is indistinguishable from "no move decided yet" ONLY to a monster
        # that reads its history while that history can still be empty. Whether
        # that is possible is a per-monster property, not a per-id one, and it is
        # checkable by reading the class's getMove -- which is what the Writhing
        # Mass's monsters.yaml row does, arm by arm, to conclude the collision is
        # unreachable there (its lastMove(0) sits behind a firstMove branch that
        # returns unconditionally, and it never calls lastTwoMoves or
        # lastMoveBefore at all).
        #
        # So this gate cannot decide the question and no longer pretends to. It
        # rejects what is unconditionally wrong -- a non-integer, a bool, a
        # NEGATIVE id (move ids are Java bytes used as array/switch keys and the
        # engine stores them in a uint8_t, so a negative would silently wrap) --
        # and leaves the 0-vs-empty-history reasoning to the row, where the
        # evidence is. A negative id is still a hard error, and a generator test
        # pins that.
        if not isinstance(mid, int) or isinstance(mid, bool) or mid < 0:
            raise fail(f"{owner}: move {mname} 'move_id' must be a non-negative "
                       f"integer (the game's byte move id; 0 is admissible -- "
                       f"WrithingMass.BIG_HIT is 0 -- but the row must argue "
                       f"that its monster never reads move_history while it can "
                       f"still be empty, since 0 is also the empty-slot "
                       f"sentinel), got {mid!r}")
        if mid > 255:
            raise fail(f"{owner}: move {mname} 'move_id' {mid} does not fit the "
                       f"uint8_t the engine stores move ids in")
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
        move_owner = f"{owner}: move {mname}"
        for step in raw_effects:
            op = step.get("op")
            # Same allowlist gate as every other domain: an op a monster move
            # cannot queue is a hard error, never a silent extra = 0.
            check_op(MONSTER_DOMAIN, move_owner, op)
            tgt = step.get("target")
            if tgt not in MONSTER_MOVE_TARGETS:
                raise fail(f"{owner}: move {mname} step has unknown target "
                           f"{tgt!r} (known: {sorted(MONSTER_MOVE_TARGETS)})")
            amount = parse_amount_tiers(move_owner, step.get("amount"))
            # Shared packing table: APPLY_POWER's PowerId and MAKE_CARD's
            # CardId | (CardPile << 16) are the SAME representation a card
            # program emits (queue_monster_move_effects splits the pile back
            # into item.src, monster_dispatch.cpp).
            extra = pack_extra(MONSTER_DOMAIN, move_owner, op, step, powers,
                               cards)
            effects.append({"op": op, "target": tgt, "extra": extra,
                            "amount": amount})
        moves.append({"name": mname, "move_id": mid, "intent": intent,
                      "effects": effects})

    ai = entry.get("ai")
    if ai != "native":
        raise fail(f"{owner}: 'ai' must be 'native' (ai tables are a later "
                   f"task), got {ai!r}")

    # AbstractMonster.type. Optional, defaulting to NORMAL because that is the
    # Java field initializer (AbstractMonster.java:99); an unknown spelling is a
    # hard error rather than a silent demotion to NORMAL, so a typo cannot
    # quietly turn a boss into a normal monster.
    enemy_type = entry.get("enemy_type", "NORMAL")
    if enemy_type not in MONSTER_ENEMY_TYPES:
        raise fail(f"{owner}: unknown enemy_type {enemy_type!r} "
                   f"(known: {sorted(MONSTER_ENEMY_TYPES)})")

    return {'name': name, 'hp': hp_tiers, 'rolls': rolls, 'moves': moves,
            'ai_native': True, 'enemy_type': enemy_type}


def emit_monster_table(domains: dict[str, list[dict]]) -> str:
    monsters = [parse_monster(e, power_id_map(domains), card_id_map(domains))
                for e in domains["monsters"]]

    # Array budgets, computed from the data (floor 1 so the header stays valid
    # while a domain is empty).
    max_stat_tiers = 1
    max_hp_tiers = 1
    max_roll_tiers = 1
    max_rolls = 1
    max_moves = 1
    max_effects = 1
    for m in monsters:
        max_hp_tiers = max(max_hp_tiers, len(m["hp"]))
        max_rolls = max(max_rolls, len(m["rolls"]))
        for roll in m["rolls"]:
            max_roll_tiers = max(max_roll_tiers, len(roll["ranges"]))
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
                       "// are the data it enqueues. Native per-instance rolls "
                       "also carry their range,",
                       "// RNG stream, and lifecycle timing here so those values "
                       "cannot drift into code.\n",
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
    out.append("// AbstractMonster.EnemyType -- what KIND of fight this monster "
               "makes it.")
    out.append("// The Java default is NORMAL (AbstractMonster.java:99); only "
               "classes that")
    out.append("// assign this.type are otherwise (SlimeBoss.java:86 -> BOSS). "
               "Relics that")
    out.append("// key on the fight being a boss fight read the MONSTER's type, "
               "not the room's:")
    out.append("// Pantograph.atBattleStart (Pantograph.java:32-40) scans the "
               "monster group for")
    out.append("// any m.type == EnemyType.BOSS. Values are pinned and "
               "append-only.")
    out.append("enum class MonsterEnemyType : uint8_t {")
    for ename, val in sorted(MONSTER_ENEMY_TYPES.items(), key=lambda kv: kv[1]):
        out.append(f"    {ename} = {val},")
    out.append("};")
    for ename, val in sorted(MONSTER_ENEMY_TYPES.items(), key=lambda kv: kv[1]):
        out.append(f"static_assert(static_cast<uint8_t>("
                   f"MonsterEnemyType::{ename}) == {val}, "
                   f"\"MonsterEnemyType::{ename} is pinned to {val} "
                   f"(append-only, never renumber)\");")
    out.append("")
    out.append("// Where a move-effect step lands: the acting monster itself, "
               "or the player.")
    out.append("enum class MonsterMoveTarget : uint8_t {")
    for tname, val in sorted(MONSTER_MOVE_TARGETS.items(), key=lambda kv: kv[1]):
        out.append(f"    {tname} = {val},")
    out.append("};\n")

    out.append("// RNG stream and lifecycle phase for native per-instance rolls.")
    out.append("enum class MonsterRollStream : uint8_t {")
    for rname, val in sorted(MONSTER_ROLL_STREAMS.items(), key=lambda kv: kv[1]):
        out.append(f"    {rname} = {val},")
    out.append("};")
    out.append("enum class MonsterRollTiming : uint8_t {")
    for rname, val in sorted(MONSTER_ROLL_TIMINGS.items(), key=lambda kv: kv[1]):
        out.append(f"    {rname} = {val},")
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
    out.append(f"inline constexpr int kMaxMonsterRollTiers = {max_roll_tiers};")
    out.append(f"inline constexpr int kMaxMonsterRolls = {max_rolls};")
    out.append(f"inline constexpr int kMaxMoveEffects = {max_effects};")
    out.append(f"inline constexpr int kMaxMonsterMoves = {max_moves};\n")
    out.append("struct MonsterMoveEffect {")
    out.append("    Opcode op;")
    out.append("    MonsterMoveTarget target;")
    out.append("    uint32_t extra;      // APPLY_POWER: PowerId; MAKE_CARD: "
               "CardId | (CardPile << 16)")
    out.append("    TieredStat amount;")
    out.append("};\n")
    out.append("struct MonsterMove {")
    out.append("    uint8_t move_id;     // the game's byte move id. MAY BE 0 "
               "(WrithingMass.BIG_HIT is),")
    out.append("                         // which is ALSO move_history's "
               "empty-slot sentinel -- see")
    out.append("                         // the loader note in "
               "emit/monsters.py.")
    out.append("    MonsterIntent intent;")
    out.append("    uint8_t effect_count;")
    out.append("    std::array<MonsterMoveEffect, kMaxMoveEffects> effects;  "
               "// takeTurn addToBottom order")
    out.append("};\n")
    out.append("struct MonsterRollDef {")
    out.append("    MonsterRollStream stream;")
    out.append("    MonsterRollTiming timing;")
    out.append("    uint8_t tier_count;")
    out.append("    std::array<MonsterHpTier, kMaxMonsterRollTiers> range;")
    out.append("    [[nodiscard]] constexpr int32_t min(int32_t ascension) "
               "const noexcept {")
    out.append("        int32_t value = range[0].hp_min;")
    out.append("        for (uint8_t i = 1; i < tier_count; ++i) {")
    out.append("            if (ascension >= range[i].min_ascension) {")
    out.append("                value = range[i].hp_min;")
    out.append("            }")
    out.append("        }")
    out.append("        return value;")
    out.append("    }")
    out.append("    [[nodiscard]] constexpr int32_t max(int32_t ascension) "
               "const noexcept {")
    out.append("        int32_t value = range[0].hp_max;")
    out.append("        for (uint8_t i = 1; i < tier_count; ++i) {")
    out.append("            if (ascension >= range[i].min_ascension) {")
    out.append("                value = range[i].hp_max;")
    out.append("            }")
    out.append("        }")
    out.append("        return value;")
    out.append("    }")
    out.append("};\n")
    out.append("struct MonsterDef {")
    out.append("    MonsterId id;")
    out.append("    uint8_t hp_tier_count;")
    out.append("    std::array<MonsterHpTier, kMaxHpTiers> hp;  "
               "// ascending; [0] is base (0)")
    out.append("    uint8_t roll_count;")
    out.append("    std::array<MonsterRollDef, kMaxMonsterRolls> rolls;")
    out.append("    uint8_t move_count;")
    out.append("    std::array<MonsterMove, kMaxMonsterMoves> moves;")
    out.append("    bool ai_native;      // move selection lives in engine code")
    out.append("    MonsterEnemyType enemy_type;  // AbstractMonster.type")
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
    out.append("    [[nodiscard]] constexpr const MonsterRollDef* "
               "roll(uint8_t index) const noexcept {")
    out.append("        return index < roll_count ? &rolls[index] : nullptr;")
    out.append("    }")
    out.append("    [[nodiscard]] constexpr bool is_boss() const noexcept {")
    out.append("        return enemy_type == MonsterEnemyType::BOSS;")
    out.append("    }")
    out.append("};\n")

    def tiered_literal(amount: list[tuple[int, int]]) -> str:
        pairs = list(amount)
        while len(pairs) < max_stat_tiers:
            pairs.append((0, 0))
        tiers_txt = ", ".join(f"{{{t}, {v}}}" for t, v in pairs)
        return f"{{{len(amount)}, {{{{{tiers_txt}}}}}}}"

    for m in monsters:
        pname = pascal(m["name"])
        # Named per-move constants (the game's byte move ids), so engine code
        # and tests reference moves symbolically.
        for mv in m["moves"]:
            out.append(f"inline constexpr uint8_t k{pname}Move"
                       f"{pascal(mv['name'])} = {mv['move_id']};")
        for index, roll in enumerate(m["rolls"]):
            out.append(f"inline constexpr uint8_t k{pname}Roll"
                       f"{pascal(roll['name'])} = {index};")
        out.append("")
        out.append(f"inline constexpr MonsterDef k{pname}{{")
        out.append(f"    MonsterId::{m['name']},")
        hp_rows = list(m["hp"])
        while len(hp_rows) < max_hp_tiers:
            hp_rows.append((0, 0, 0))
        hp_txt = ", ".join(f"{{{t}, {lo}, {hi}}}" for t, lo, hi in hp_rows)
        out.append(f"    {len(m['hp'])}, {{{{{hp_txt}}}}},")
        out.append(f"    {len(m['rolls'])},")
        out.append("    {{")
        for roll in m["rolls"]:
            range_rows = list(roll["ranges"])
            while len(range_rows) < max_roll_tiers:
                range_rows.append((0, 0, 0))
            range_txt = ", ".join(
                f"{{{t}, {lo}, {hi}}}" for t, lo, hi in range_rows)
            out.append(
                f"        {{MonsterRollStream::{roll['stream']}, "
                f"MonsterRollTiming::{roll['timing']}, "
                f"{len(roll['ranges'])}, {{{{{range_txt}}}}}}},")
        for _ in range(len(m["rolls"]), max_rolls):
            range_txt = ", ".join("{0, 0, 0}" for _ in range(max_roll_tiers))
            out.append(
                "        {MonsterRollStream::MONSTER_HP, "
                "MonsterRollTiming::CONSTRUCTOR_AFTER_HP, 1, "
                f"{{{{{range_txt}}}}}}},")
        out.append("    }},")
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
            out.append(f"        {{k{pname}Move{pascal(mv['name'])}, "
                       f"MonsterIntent::{mv['intent']}, "
                       f"{len(mv['effects'])},")
            out.append("         {{")
            out.extend(eff_rows)
            out.append("         }}},")
        out.append("    }},")
        out.append(f"    {'true' if m['ai_native'] else 'false'}, "
                   f"MonsterEnemyType::{m['enemy_type']}}};\n")

    out.append("[[nodiscard]] inline const MonsterDef* "
               "monster_def(MonsterId id) noexcept {")
    out.append("    switch (id) {")
    for m in monsters:
        out.append(f"        case MonsterId::{m['name']}: "
                   f"return &k{pascal(m['name'])};")
    out.append("        case MonsterId::NONE:")
    out.append("        default: return nullptr;")
    out.append("    }")
    out.append("}\n")
    out.append("}  // namespace sts::registry")
    return "\n".join(out) + "\n"
