"""card_table.hpp -- the CardDef/CardEffectStep effect-program table.

The shape stage-a §6 froze: two FULL effect programs per card (base + upgraded,
indexed by CardInstance.upgrade), plus the B3.6 on-exhaust program pair and the
B3.3/B3.6/B3.9 card-property columns.
"""

from __future__ import annotations

from ..loader import card_id_map, power_id_map
from ..steps import CARD_DOMAIN, padded_step_literals, parse_steps
from ..vocab import (
    BANNER, CARD_FLAGS, CARD_TARGETING, CARD_TARGET_KINDS, CARD_TRIGGERS,
    CARD_TYPES, OPCODES, STEP_TARGETS, fail, pascal,
)


# --- CardLibrary "library order" ---------------------------------------------
#
# Every card pool the game builds by walking the library
# (AbstractDungeon.initializeCardPools -> CardLibrary.addRedCards /
# addColorlessCards, and returnTrulyRandomCardInCombat's concatenations) fills
# in `CardLibrary.cards` iteration order, and `CardGroup.addToTop` is a plain
# `group.add(c)` append (CardGroup.java:455-457) -- so the emitted array index
# and the game's `group.get(i)` index must agree element for element.
#
# `CardLibrary.cards` is a `HashMap<String, AbstractCard>` keyed by cardID
# (CardLibrary.java:409, written only by add() at :954), so its iteration order
# is COMPUTABLE, not arbitrary: HashMap walks buckets 0..capacity-1 and, inside a
# bucket, a linked list in insertion order (Java 8's resize splits each bucket
# into lo/hi lists that preserve relative order, so insertion order survives
# every rehash; treeification needs 8 entries in one bucket and this map's worst
# bucket holds 4).
#
#   bucket = (h ^ (h >>> 16)) & (capacity - 1),   h = String.hashCode(cardID)
#
# CAPACITY. CardLibrary.initialize() adds 370 cards to a default-constructed
# HashMap, which doubles from 16 while size > 0.75 * capacity and so ends at 512
# (0.75 * 512 == 384 >= 370). That is a property of the frozen 12-18-2022 content
# set, hence a constant here; registry_gen_test re-derives it.
#
# TIEBREAK. Two cards in one bucket keep their insertion order, and every pool
# emitted below filters a SINGLE addXCards() call's cards, each of which inserts
# in alphabetical order of the Java class name. That class name is the cardID
# with its separators removed, bar the handful listed below.
#
# VERIFIED against the oracle capture: 27 offered card identities over 9
# CARD_REWARD screens and 5 seeds are reproduced exactly by indexing the pools
# below with the simulator's own (stream-exact) draw indices; the previous
# registry-id order reproduced 0 of the 27.
_CARD_LIBRARY_CAPACITY = 512

# CardLibrary.curses is a second HashMap holding only the 14 CURSE rows
# (CardLibrary.java:410/949); 14 > 0.75 * 16, so it settles one doubling in.
_CURSE_LIBRARY_CAPACITY = 32

_LIBRARY_CLASS_NAMES = {
    # cardID -> Java class name, for the rows where stripping separators does not
    # produce it (CardLibrary.java's import list is the authority).
    "Ghostly": "Apparition",
    "Void": "VoidCard",
    "Strike_R": "Strike_Red", "Defend_R": "Defend_Red",
    "Strike_G": "Strike_Green", "Defend_G": "Defend_Green",
    "Strike_B": "Strike_Blue", "Defend_B": "Defend_Blue",
    "Strike_P": "Strike_Purple", "Defend_P": "Defend_Watcher",
}


def _java_string_hash(s: str) -> int:
    """java.lang.String.hashCode, as an unsigned 32-bit value."""
    h = 0
    for ch in s:
        h = (h * 31 + ord(ch)) & 0xFFFFFFFF
    return h


def _library_key(game_id: str, capacity: int) -> tuple[int, str]:
    h = _java_string_hash(game_id)
    cls = _LIBRARY_CLASS_NAMES.get(
        game_id, "".join(ch for ch in game_id if ch.isalnum() or ch == "_"))
    return ((h ^ (h >> 16)) & (capacity - 1), cls)


def library_order_key(row) -> tuple[int, str]:
    """The `CardLibrary.cards` iteration-order sort key for a cards.yaml row."""
    return _library_key(row["game_id"], _CARD_LIBRARY_CAPACITY)


def curse_library_order_key(row) -> tuple[int, str]:
    """The `CardLibrary.curses` iteration-order sort key (14 entries -> cap 32)."""
    return _library_key(row["game_id"], _CURSE_LIBRARY_CAPACITY)


def src_combat_order(pool_rows: list) -> list:
    """Order rows the way `returnTrulyRandomCardInCombat` sees them.

    Two things separate this from the plain reward-pool order.

    RARITY-MAJOR. That method does not walk one list; it concatenates
    srcCommonCardPool, then srcUncommonCardPool, then srcRareCardPool
    (AbstractDungeon.java:944-978), so every COMMON precedes every UNCOMMON
    precedes every RARE regardless of hash bucket.

    REVERSED WITHIN A RARITY. `initializeCardPools` copies each pool into its
    `src*` twin with `addToBottom`, which is `group.add(0, c)`
    (CardGroup.java:459-461) -- a PREPEND. So a `src*` pool holds its rarity's
    library order backwards, and everything indexing a `src*` pool inherits
    that, while `getCard(rarity)` (which reads the un-copied pools) does not.
    """
    out: list = []
    for tier in ("COMMON", "UNCOMMON", "RARE"):
        tier_rows = [r for r in pool_rows if r["rarity"] == tier]
        tier_rows.sort(key=library_order_key)
        tier_rows.reverse()
        out.extend(tier_rows)
    if len(out) != len(pool_rows):
        raise fail("src_combat_order: a pool row is outside COMMON/UNCOMMON/RARE")
    return out


def parse_card_flags(card_name: str, raw_flags, cost: int) -> tuple[int, int]:
    """Return (flag_bits, base_cost). YAML `flags:` names OR the game's negative
    cost sentinels (-1 == X-cost, -2 == unplayable status/curse) set the bits;
    base_cost is the non-negative cost (0 for the sentinel costs, since base_cost
    is unsigned)."""
    bits = 0
    for name in (raw_flags or []):
        key = str(name).lower()
        if key not in CARD_FLAGS:
            raise fail(f"cards.yaml: card {card_name} has unknown flag {name!r} "
                       f"(known: {sorted(CARD_FLAGS)})")
        bits |= CARD_FLAGS[key]
    base_cost = cost
    if cost == -1:            # X-cost (AbstractCard cost -1; consumes all energy)
        bits |= CARD_FLAGS["xcost"]
        base_cost = 0
    elif cost == -2:          # unplayable status/curse (cost < -1)
        bits |= CARD_FLAGS["unplayable"]
        base_cost = 0
    elif cost < 0:
        raise fail(f"cards.yaml: card {card_name} has unsupported negative cost "
                   f"{cost} (only -1 X-cost / -2 unplayable are sentinels)")
    if base_cost > 255:
        raise fail(f"cards.yaml: card {card_name} cost {cost} exceeds u8 base_cost")
    return bits, base_cost


def emit_card_table(domains: dict[str, list[dict]]) -> str:
    cards = domains["cards"]
    powers = power_id_map(domains)
    card_ids = card_id_map(domains)

    def program(card_name: str, effects):
        return parse_steps(CARD_DOMAIN, f"cards.yaml: card {card_name}",
                           effects, powers, card_ids)

    # Build the resolved rows first so we can compute kMaxCardSteps and surface
    # validation errors before emitting anything.
    rows = []
    max_steps = 1  # cards.hpp reserves at least 1; skeleton max is 2.
    for c in cards:
        if "native" in c:
            raise fail(
                f"cards.yaml: card {c['name']} uses unsupported key 'native'; "
                "cards are effect-program driven and have no native-card "
                "dispatch surface")
        ctype = c.get("type")
        if ctype not in CARD_TYPES:
            raise fail(f"cards.yaml: card {c['name']} has unsupported type "
                       f"{ctype!r} (CardDef supports {sorted(CARD_TYPES)})")
        target = c.get("target")
        if target not in CARD_TARGETING:
            raise fail(f"cards.yaml: card {c['name']} has unknown target "
                       f"{target!r}")
        needs_target, random_target, target_kind = CARD_TARGETING[target]

        cost = int(c.get("cost", 0))
        flags, base_cost = parse_card_flags(c["name"], c.get("flags"), cost)
        steps = program(c["name"], c.get("effects"))

        # Upgraded program (design doc §4.2: a FULL program, not a delta). Absent
        # -> upgraded == base (safe default; real content lands per-card in B3.3+).
        if "upgraded" in c and c["upgraded"] is not None:
            up_steps = program(c["name"], c["upgraded"])
            up_cost = int(c.get("upgraded_cost", cost))
            up_flags_bits, up_base_cost = parse_card_flags(
                c["name"], c.get("upgraded_flags", c.get("flags")), up_cost)
        else:
            up_steps = list(steps)
            up_base_cost = base_cost
            up_flags_bits = flags

        # B3.6 on-exhaust trigger program (card.triggerOnExhaust, fired at §5.5
        # CardGroup.moveToExhaustPile:857 AFTER relics+powers onExhaust). A FULL
        # base/upgraded pair like `effects`/`upgraded`; absent -> empty (almost
        # every card). Sentinel is the S1 consumer (addToTop GainEnergyAction 2/3,
        # Sentinel.java:37-43); steps are queued add_to_top per the Java.
        ox_steps = program(c["name"], c.get("on_exhaust"))
        if "upgraded_on_exhaust" in c and c["upgraded_on_exhaust"] is not None:
            up_ox_steps = program(c["name"], c["upgraded_on_exhaust"])
        else:
            up_ox_steps = list(ox_steps)

        # B3.3 card-property columns (NOT per-instance flags -- CardDef-only, so
        # they never touch CardInstance.flags / the combat fixtures). `strike`
        # mirrors CardTags.STRIKE (Perfected Strike's per-Strike count reads it);
        # `requires_all_attacks` is Clash's canUse predicate (playable only when
        # every hand card is an Attack).
        is_strike = bool(c.get("strike", False))
        requires_all_attacks = bool(c.get("requires_all_attacks", False))

        # `requires_draw_pile_type` is Secret Technique's / Secret Weapon's
        # canUse predicate (SecretTechnique.java:35-50 / SecretWeapon.java:35-50):
        # unplayable unless the DRAW PILE holds at least one card of this
        # CardType. Same centralized-canUse shape as requires_all_attacks -- the
        # column feeds card_can_use_without_target, so the public ActionMask and
        # the dequeue-time autoplay revalidation read one authority. Absent ->
        # kNoDrawPileType (255), which is NOT a CardType value: ATTACK is 0, so
        # an "absent == 0" spelling would gate every card on having an Attack in
        # the draw pile.
        rdpt_raw = c.get("requires_draw_pile_type")
        if rdpt_raw is None:
            requires_draw_pile_type = 255
        else:
            rdpt = str(rdpt_raw).upper()
            if rdpt not in CARD_TYPES:
                raise fail(f"cards.yaml: card {c['name']} has unknown "
                           f"requires_draw_pile_type {rdpt_raw!r} "
                           f"(known: {sorted(CARD_TYPES)})")
            requires_draw_pile_type = CARD_TYPES[rdpt]

        # B3.9 trigger column: WHEN the effect program runs (default ON_PLAY). The
        # passive statuses/curses (Burn/Void/Pain/...) run their program at an
        # end-of-turn / on-draw / on-other-card-played hook instead of on play.
        trig = str(c.get("trigger", "on_play")).lower()
        if trig not in CARD_TRIGGERS:
            raise fail(f"cards.yaml: card {c['name']} has unknown trigger "
                       f"{c.get('trigger')!r} (known: {sorted(CARD_TRIGGERS)})")

        # Run-layer B3.9 metadata. CardLibrary.getCurse selects only the ten
        # ordinary curses; Parasite alone has an on-remove master-deck penalty.
        curse_pool = bool(c.get("curse_pool", False))
        if curse_pool and ctype != "CURSE":
            raise fail(f"cards.yaml: card {c['name']} marks curse_pool but is not CURSE")
        on_remove_max_hp_loss = int(c.get("on_remove_max_hp_loss", 0))
        if on_remove_max_hp_loss < 0 or on_remove_max_hp_loss > 127:
            raise fail(f"cards.yaml: card {c['name']} has invalid on_remove_max_hp_loss")

        # B3.6 combat-pool membership inputs (Infernal Blade / returnTrulyRandom-
        # CardInCombat, AbstractDungeon.java:964-979): the pool is every RED
        # COMMON/UNCOMMON/RARE card (BASIC and STATUS/CURSE never enter the srcX
        # pools -- CardLibrary.addRedCards pool filter, CardLibrary.java:1153-1161)
        # minus HEALING-tagged cards (Feed, Reaper -- B3.8 must set `healing: true`
        # on those rows when they land). color/rarity were documentation-only
        # before B3.6; the pool builder now reads them.
        color = str(c.get("color", "")).upper()
        rarity = str(c.get("rarity", "")).upper()
        healing = bool(c.get("healing", False))

        max_steps = max(max_steps, len(steps), len(up_steps),
                        len(ox_steps), len(up_ox_steps))
        rows.append({
            "name": c["name"], "cost": base_cost, "flags": flags,
            "ctype": CARD_TYPES[ctype], "needs_target": needs_target,
            "random_target": random_target, "target_kind": target_kind,
            "steps": steps,
            "up_cost": up_base_cost, "up_flags": up_flags_bits,
            "up_steps": up_steps, "is_strike": is_strike,
            "requires_all_attacks": requires_all_attacks,
            "requires_draw_pile_type": requires_draw_pile_type,
            "trigger": CARD_TRIGGERS[trig],
            "curse_pool": curse_pool,
            "on_remove_max_hp_loss": on_remove_max_hp_loss,
            "ox_steps": ox_steps, "up_ox_steps": up_ox_steps,
            "id": c["id"], "color": color, "rarity": rarity,
            "healing": healing,
            # The game's cardID string. Needed as a POOL SORT KEY, not only as a
            # translator join key: CardGroup.getRandomCard(useRng, rarity)
            # Collections.sort()s its rarity-filtered view, and AbstractCard.
            # compareTo is cardID.compareTo (CardGroup.java:509-524;
            # AbstractCard.java:2583-2584).
            "game_id": c["game_id"],
            "is_status": ctype == "STATUS",
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
    out.append("enum class CardTargetKind : uint8_t {")
    for name, val in sorted(CARD_TARGET_KINDS.items(), key=lambda kv: kv[1]):
        out.append(f"    {name} = {val},")
    out.append("};\n")
    # CardTrigger (B3.9): mirror of cards.hpp CardTrigger; pinned/append-only.
    out.append("enum class CardTrigger : uint8_t {")
    for name, val in sorted(CARD_TRIGGERS.items(), key=lambda kv: kv[1]):
        out.append(f"    {name.upper()} = {val},")
    out.append("};\n")
    # Card-flag bit constants (Stage B B3.1): mirror of the engine's CardFlag
    # (types.hpp); cards.hpp static_asserts these equal. Emitted sorted by bit.
    for fname, fval in sorted(CARD_FLAGS.items(), key=lambda kv: kv[1]):
        out.append(f"inline constexpr uint16_t kCardFlag{pascal(fname.upper())} "
                   f"= {fval};")
    out.append("")
    out.append("struct CardEffectStep {")
    out.append("    Opcode op;")
    out.append("    int32_t amount;")
    out.append("    uint32_t extra;")
    out.append("    StepTarget target;")
    out.append("};\n")
    out.append(f"inline constexpr int kMaxCardSteps = {max_steps};\n")
    out.append("// CardDef::requires_draw_pile_type sentinel: this card has NO")
    out.append("// draw-pile-type canUse predicate. Deliberately outside the")
    out.append("// CardType range -- ATTACK is 0, so \"absent == 0\" would gate")
    out.append("// every card on holding an Attack in the draw pile.")
    out.append("inline constexpr uint8_t kNoDrawPileType = 255;\n")
    out.append("// Two effect-program rows per card (design doc §4.2: base +")
    out.append("// upgraded, indexed by CardInstance.upgrade). A card with no")
    out.append("// `upgraded:` block emits upgraded_* byte-identical to base.")
    out.append("struct CardDef {")
    out.append("    CardId id;")
    out.append("    uint8_t base_cost;")
    out.append("    CardType type;")
    out.append("    bool needs_target;")
    out.append("    bool random_target;")
    out.append("    CardTargetKind target_kind;")
    out.append("    uint16_t flags;                // CardFlag bits (base)")
    out.append("    uint8_t step_count;")
    out.append("    std::array<CardEffectStep, kMaxCardSteps> steps;")
    out.append("    uint8_t upgraded_cost;")
    out.append("    uint16_t upgraded_flags;       // CardFlag bits (upgraded)")
    out.append("    uint8_t upgraded_step_count;")
    out.append("    std::array<CardEffectStep, kMaxCardSteps> upgraded_steps;")
    out.append("    bool is_strike;                // CardTags.STRIKE (B3.3)")
    out.append("    bool requires_all_attacks;     // Clash canUse predicate (B3.3)")
    out.append("    uint8_t requires_draw_pile_type;  // CardType this card's canUse")
    out.append("                                     // needs in the draw pile, or")
    out.append("                                     // kNoDrawPileType (B3.11)")
    out.append("    CardTrigger trigger;           // WHEN effects run (B3.9)")
    out.append("    bool curse_pool;               // CardLibrary.getCurse member (B3.9)")
    out.append("    uint8_t on_remove_max_hp_loss; // master-deck removal hook (B3.9)")
    out.append("    // B3.6: card.triggerOnExhaust program (CardGroup.")
    out.append("    // moveToExhaustPile:857 -- Sentinel), base + upgraded rows.")
    out.append("    uint8_t on_exhaust_step_count;")
    out.append("    std::array<CardEffectStep, kMaxCardSteps> on_exhaust_steps;")
    out.append("    uint8_t upgraded_on_exhaust_step_count;")
    out.append("    std::array<CardEffectStep, kMaxCardSteps> upgraded_on_exhaust_steps;")
    out.append("};\n")

    def pad(steps) -> list[str]:
        return padded_step_literals(steps, max_steps)

    for r in rows:
        ctype_name = next(k for k, v in CARD_TYPES.items() if v == r["ctype"])
        steps_txt = ",\n        ".join(pad(r["steps"]))
        up_steps_txt = ",\n        ".join(pad(r["up_steps"]))
        out.append(f"inline constexpr CardDef k{pascal(r['name'])}{{")
        out.append(f"    CardId::{r['name']}, {r['cost']}, "
                   f"CardType::{ctype_name}, "
                   f"{'true' if r['needs_target'] else 'false'}, "
                   f"{'true' if r['random_target'] else 'false'}, "
                   f"CardTargetKind::{next(k for k, v in CARD_TARGET_KINDS.items() if v == r['target_kind'])}, "
                   f"{r['flags']}, {len(r['steps'])},")
        out.append("    {{")
        out.append(f"        {steps_txt},")
        out.append("    }},")
        out.append(f"    {r['up_cost']}, {r['up_flags']}, {len(r['up_steps'])},")
        out.append("    {{")
        out.append(f"        {up_steps_txt},")
        out.append("    }},")
        trig_name = next(k for k, v in CARD_TRIGGERS.items()
                         if v == r["trigger"])
        ox_txt = ",\n        ".join(pad(r["ox_steps"]))
        up_ox_txt = ",\n        ".join(pad(r["up_ox_steps"]))
        out.append(f"    {'true' if r['is_strike'] else 'false'}, "
                   f"{'true' if r['requires_all_attacks'] else 'false'}, "
                   f"{r['requires_draw_pile_type']}, "
                   f"CardTrigger::{trig_name.upper()}, "
                   f"{'true' if r['curse_pool'] else 'false'}, "
                   f"{r['on_remove_max_hp_loss']},")
        out.append(f"    {len(r['ox_steps'])},")
        out.append("    {{")
        out.append(f"        {ox_txt},")
        out.append("    }},")
        out.append(f"    {len(r['up_ox_steps'])},")
        out.append("    {{")
        out.append(f"        {up_ox_txt},")
        out.append("    }}};\n")

    # CardLibrary.getCurse (CardLibrary.java:1043-1050) walks the SEPARATE
    # `curses` HashMap (CardLibrary.java:410, written at :949) -- 14 entries, so
    # its capacity settles at 32, not the 512 of the main card map -- drops the
    # four non-poolable curses and indexes what is left with one cardRng draw.
    # Same computable iteration order, different capacity.
    poolable_curses = [r for r in rows if r["curse_pool"]]
    poolable_curses.sort(key=curse_library_order_key)
    out.append(f"inline constexpr int kPoolableCurseCount = {len(poolable_curses)};")
    out.append("inline constexpr std::array<CardId, kPoolableCurseCount> "
               "kPoolableCurses{{")
    for r in poolable_curses:
        out.append(f"    CardId::{r['name']},")
    out.append("}};\n")

    # B3.6: the Ironclad in-combat ATTACK transform pool (Infernal Blade /
    # AbstractDungeon.returnTrulyRandomCardInCombat(ATTACK), :964-979): every RED
    # COMMON/UNCOMMON/RARE ATTACK without the HEALING tag, drawn with ONE
    # cardRandomRng random(size - 1). MEMBERSHIP is derived from the rows'
    # color/rarity/type/healing columns, so it self-completes as B3.8's rares
    # land (B3.8 MUST set `healing: true` on Feed and Reaper -- CardTags.HEALING).
    # ORDER: `src_combat_order` below -- the rarity-major concatenation of the
    # REVERSED per-rarity library order the three `src*CardPool`s hold. The
    # one-draw accounting and membership were always Java-exact; the order is
    # now Java-exact too.
    attack_pool = src_combat_order(
        [r for r in rows
         if r["color"] == "RED"
         and r["rarity"] in ("COMMON", "UNCOMMON", "RARE")
         and r["ctype"] == CARD_TYPES["ATTACK"]
         and not r["healing"]])
    out.append(f"inline constexpr int kIroncladAttackPoolCount = "
               f"{len(attack_pool)};")
    out.append("inline constexpr std::array<CardId, kIroncladAttackPoolCount> "
               "kIroncladAttackPool{{")
    for r in attack_pool:
        out.append(f"    CardId::{r['name']},")
    out.append("}};\n")

    # B3.11 stage C: the SKILL sibling of the pool above -- Chrysalis
    # (Chrysalis.java:34) passes CardType.SKILL to the SAME
    # returnTrulyRandomCardInCombat(type) (:964-979) that Infernal Blade and
    # Metamorphosis pass ATTACK to, so the two are one emission shape with one
    # filter changed. EMITTING the type-filtered pool (rather than filtering
    # kIroncladCombatPool at runtime) is what makes the opcode's pick a single
    # indexed read with no per-call scan, and it keeps the ONE draw over
    # [0, size-1] structurally identical to the ATTACK pool's.
    # ORDER: Java's three-pool concatenation preserves each source pool's
    # relative order and the type filter is order-preserving, so a type-filtered
    # subsequence of the full RED combat pool is order-equivalent to the game's
    # filtered build. Both go through src_combat_order, so that subsequence
    # relation still holds and stays pinned by its named test.
    skill_pool = src_combat_order(
        [r for r in rows
         if r["color"] == "RED"
         and r["rarity"] in ("COMMON", "UNCOMMON", "RARE")
         and r["ctype"] == CARD_TYPES["SKILL"]
         and not r["healing"]])
    out.append(f"inline constexpr int kIroncladSkillPoolCount = "
               f"{len(skill_pool)};")
    out.append("inline constexpr std::array<CardId, kIroncladSkillPoolCount> "
               "kIroncladSkillPool{{")
    for r in skill_pool:
        out.append(f"    CardId::{r['name']},")
    out.append("}};\n")

    # B3.10b Discovery: AbstractDungeon.returnTrulyRandomCardInCombat()
    # (:944-962) concatenates the player's src COMMON, UNCOMMON and RARE pools,
    # excluding HEALING-tagged cards, and spends one cardRandomRng draw per
    # attempt. Membership is generated so later card rows self-complete it, and
    # the order is src_combat_order's rarity-major reversed library order.
    combat_pool = src_combat_order(
        [r for r in rows
         if r["color"] == "RED"
         and r["rarity"] in ("COMMON", "UNCOMMON", "RARE")
         and not r["healing"]])
    out.append(f"inline constexpr int kIroncladCombatPoolCount = "
               f"{len(combat_pool)};")
    out.append("inline constexpr std::array<CardId, kIroncladCombatPoolCount> "
               "kIroncladCombatPool{{")
    for r in combat_pool:
        out.append(f"    CardId::{r['name']},")
    out.append("}};\n")

    # B3.10b Jack of All Trades: returnTrulyRandomColorlessCardInCombat
    # (:981-995) draws from every poolable COLORLESS UNCOMMON/RARE except a
    # HEALING-tagged card (Bandage Up). This reaches the final 34 members
    # automatically as B3.10c and B3.11 add the remaining colorless rows.
    # ORDER: srcColorlessCardPool, i.e. colorlessCardPool's library order
    # REVERSED by initializeCardPools' addToBottom copy (:1180-1183).
    colorless_pool = [r for r in rows
                      if r["color"] == "COLORLESS"
                      and r["rarity"] in ("UNCOMMON", "RARE")
                      and not r["healing"]]
    colorless_pool.sort(key=library_order_key)
    colorless_pool.reverse()
    out.append(f"inline constexpr int kColorlessCombatPoolCount = "
               f"{len(colorless_pool)};")
    out.append("inline constexpr std::array<CardId, kColorlessCombatPoolCount> "
               "kColorlessCombatPool{{")
    for r in colorless_pool:
        out.append(f"    CardId::{r['name']},")
    out.append("}};\n")

    # B4.5: the three per-rarity combat CARD-REWARD pools (AbstractDungeon.
    # initializeCardPools, AbstractDungeon.java:1135-1180: player.getCardPool ->
    # CardLibrary.addRedCards, CardLibrary.java:1152-1161 -- every RED card
    # whose rarity != BASIC, split by rarity into commonCardPool /
    # uncommonCardPool / rareCardPool). Unlike the ATTACK transform pool above,
    # these have NO type filter and NO healing exclusion (Feed and Reaper ARE
    # reward-poolable; only returnTrulyRandomCardInCombat excludes them).
    # getCard(rarity) (AbstractDungeon.java:1481-1498) draws
    # pool.getRandomCard(true) == group.get(cardRng.random(size - 1))
    # (CardGroup.java:502-506): one cardRng draw indexing THIS list.
    # ORDER: PINNED by the B4.5 oracle capture. These three are the pools the
    # capture directly exercises, and they are in plain CardLibrary iteration
    # order -- `addToTop` appends (CardGroup.java:455-457), so no reversal --
    # which reproduces all 27 captured offer identities.
    for tier in ("COMMON", "UNCOMMON", "RARE"):
        pool = [r for r in rows
                if r["color"] == "RED" and r["rarity"] == tier]
        pool.sort(key=library_order_key)
        tname = pascal(tier)
        out.append(f"inline constexpr int kIronclad{tname}PoolCount = "
                   f"{len(pool)};")
        out.append(f"inline constexpr std::array<CardId, "
                   f"kIronclad{tname}PoolCount> kIronclad{tname}Pool{{{{")
        for r in pool:
            out.append(f"    CardId::{r['name']},")
        out.append("}};\n")

    # The dungeon COLORLESS card pool (AbstractDungeon.addColorlessCards,
    # AbstractDungeon.java:1203-1210): every COLORLESS card whose rarity is
    # neither BASIC nor SPECIAL and whose type is not STATUS. Two views are
    # emitted because the game reads it two different ways.
    #
    # (1) THE RARITY VIEWS, kColorlessUncommonPool / kColorlessRarePool.
    #     getColorlessCardFromPool(rarity) (AbstractDungeon.java:1579-1595) calls
    #     colorlessCardPool.getRandomCard(true, rarity), which filters by rarity,
    #     **Collections.sort()s the filtered list**, and indexes it with
    #     `cardRng.random(size - 1)` (CardGroup.java:509-524). AbstractCard.
    #     compareTo is `cardID.compareTo(other.cardID)` (AbstractCard.java:
    #     2583-2584), so the order is a plain lexicographic sort of the game_id
    #     strings -- these two arrays are ORDER-EXACT, with none of the
    #     CardLibrary-HashMap-order deviation the RED reward pools carry. Sorted
    #     here with Python's default str ordering, which matches Java's
    #     String.compareTo for the ASCII-only cardID set.
    #     NOTE the stream: those draws are `cardRng`, NOT the caller's rng --
    #     Neow's colorless options therefore consume cardRng even though their
    #     rarity rolls consume NeowEvent.rng (NeowReward.java:309-330).
    # (2) THE WHOLE-POOL VIEW, kColorlessPool, is what the colorless branch of
    #     returnTrulyRandomColorlessCardFromAvailable indexes
    #     (AbstractDungeon.java:998-1014). That reads srcColorlessCardPool, so
    #     it is the library order REVERSED, exactly like kColorlessCombatPool.
    colorless_all = [r for r in rows
                     if r["color"] == "COLORLESS"
                     and r["rarity"] in ("UNCOMMON", "RARE")
                     and not r["is_status"]]
    for tier in ("UNCOMMON", "RARE"):
        pool = [r for r in colorless_all if r["rarity"] == tier]
        pool.sort(key=lambda r: r["game_id"])
        tname = pascal(tier)
        out.append(f"inline constexpr int kColorless{tname}PoolCount = "
                   f"{len(pool)};")
        out.append(f"inline constexpr std::array<CardId, "
                   f"kColorless{tname}PoolCount> kColorless{tname}Pool{{{{")
        for r in pool:
            out.append(f"    CardId::{r['name']},  // {r['game_id']}")
        out.append("}};\n")
    colorless_all.sort(key=library_order_key)
    colorless_all.reverse()
    out.append(f"inline constexpr int kColorlessPoolCount = "
               f"{len(colorless_all)};")
    out.append("inline constexpr std::array<CardId, kColorlessPoolCount> "
               "kColorlessPool{{")
    for r in colorless_all:
        out.append(f"    CardId::{r['name']},")
    out.append("}};\n")

    # Lookup table + accessor (mirrors cards.hpp card_def()).
    out.append("inline constexpr std::array<const CardDef*, "
               f"{len(rows)}> kCardDefs{{{{")
    for r in rows:
        out.append(f"    &k{pascal(r['name'])},")
    out.append("}};\n")
    out.append("[[nodiscard]] inline const CardDef* card_def(CardId id) noexcept {")
    out.append("    switch (id) {")
    for r in rows:
        out.append(f"        case CardId::{r['name']}: "
                   f"return &k{pascal(r['name'])};")
    out.append("        case CardId::NONE:")
    out.append("        default: return nullptr;")
    out.append("    }")
    out.append("}\n")
    out.append("}  // namespace sts::registry")
    return "\n".join(out) + "\n"
