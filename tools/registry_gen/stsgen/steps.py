"""The ONE effect-step parser, shared by every domain that authors programs.

Cards, power hooks, relic hooks, potion USE bodies and monster moves all emit the
same ``CardEffectStep`` shape ``{op, amount, extra, target}``. Before this module
existed each domain carried its own near-copy of the parser, and the copies drifted:
``DAMAGE``'s damage_type packing existed for cards and relics but not powers,
``REMOVE_POWER``'s PowerId packing only for powers, and the ``CHOOSE_CARD`` /
``MAKE_CARD`` / dynamic-damage packings only for cards. The drift was SILENT --
an op passed the OPCODES membership check in every domain, so a relic author who
wrote ``CHOOSE_CARD`` got ``extra = 0`` and no error. That is a bit-exactness hole,
so this module makes the packing table global and the DOMAIN membership explicit:

  * ``pack_extra`` owns every ``extra`` packing, keyed by op, for all domains.
  * each domain declares an op ALLOWLIST; an op outside it is a hard ``GenError``
    naming the domain, the owner row and the op -- never a silent ``extra = 0``.

The allowlists are derived from what the domain's QUEUEING helper in the engine
can carry (see the per-group comments below), not from what happens to be
authored today.
"""

from __future__ import annotations

from .vocab import (CARD_MAKE_UPGRADED_BIT, CARD_PILES, CARD_TYPES,
                    CHOICE_COPIES_SHIFT, CHOICE_KIND_HIGH_BIT, CHOICE_KINDS,
                    CHOICE_RANDOM_BIT, DAMAGE_TYPES, OPCODES, PLAY_CARD_FLAGS,
                    STEP_TARGETS, fail)

# --- Op capability groups ----------------------------------------------------
# Which ops a domain MAY author is a property of the queueing helper that turns a
# generated step into an ActionQueueItem, not of the interpreter.
#
# GENERAL_OPS: the queued item is fully described by the step itself plus the
# helper's src/tgt substitution, so card_play.cpp queue_effect_step,
# power_hooks.cpp / relic_hooks.cpp / potions.cpp queue_*_step all carry them
# identically (each sets src, tgt, amount and flags = step.extra).
GENERAL_OPS = frozenset({
    "NOP", "DAMAGE", "BLOCK", "APPLY_POWER", "REMOVE_POWER", "REDUCE_POWER",
    "DRAW", "GAIN_ENERGY", "SHUFFLE_IN", "LOSE_HP", "LOSE_HP_PER_HAND",
    "DISCARD_HAND", "DROPKICK", "DAMAGE_BLOCK", "DAMAGE_STR_MULT",
    "EXHAUST_NON_ATTACKS", "DOUBLE_BLOCK", "BLOCK_PER_NON_ATTACK",
    "SPOT_WEAKNESS", "RANDOM_ATTACK_TO_HAND",
    # Red-rare verbs whose operands are all literals or execute-time reads:
    # DAMAGE_FEED (base damage + the max-HP amount in `extra`), FIEND_FIRE
    # (per-hit base; the hand size is read at execute), DOUBLE_STRENGTH (no
    # operand), VAMPIRE_DAMAGE_ALL (base damage), HEAL (a literal heal).
    # PLAY_CARD is here for the DRAW-TOP source form -- the only one a YAML
    # author can name; its COPY form's operand is a runtime card-pool index, so
    # that form is emitted natively, never authored (see interp.hpp kPlayCard*).
    "DAMAGE_FEED", "FIEND_FIRE", "DOUBLE_STRENGTH", "VAMPIRE_DAMAGE_ALL",
    "HEAL", "PLAY_CARD",
    # Colorless-uncommon verbs. Every operand is a literal or an execute-time
    # read of player-side state (the draw-pile size, the hand's card types, the
    # discard pile, the hand), so none of them needs the played card's instance
    # and all four carry identically from any domain's queue helper.
    "DAMAGE_DRAW_PILE", "CONDITIONAL_DRAW", "RESHUFFLE_ALL", "MADNESS",
    # B3.10b. DISCOVERY persists its generated offer in its own queued item;
    # the other three read only literal operands plus execute-time combat state.
    "DARK_SHACKLES", "DISCOVERY", "ENLIGHTENMENT",
    "RANDOM_COLORLESS_TO_HAND",
})

# CARD_CONTEXT_OPS: the queued item is COMPLETED from the played card's instance
# at queue time -- card_play.cpp queue_effect_step stamps the source pool index
# (DAMAGE_RAMPAGE's flags), bakes the source card's upgrade count / Strike count
# into a plain DAMAGE (DAMAGE_UPGRADE_SCALE, DAMAGE_PER_STRIKE), or splits
# MAKE_CARD's packed CardPile into item.src. (PLAY_TOP_DRAW and the
# discard-source CHOOSE_CARD once carried a stamped just-played-card exclusion
# too; the limbo pile made those stamps obsolete, but both ops stay in this
# group -- their semantics still assume a card program's queue helper.) No other
# domain's helper does any of that, so authoring one of these outside a card
# program produces a MISPACKED item.
CARD_CONTEXT_OPS = frozenset({
    "MAKE_CARD", "CHOOSE_CARD", "PLAY_TOP_DRAW",
    "DAMAGE_PER_STRIKE", "DAMAGE_UPGRADE_SCALE", "DAMAGE_RAMPAGE",
})

# ENGINE_EMITTED_OPS: pinned in the Opcode enum for the cards.hpp drift check but
# never authorable, because their operands are RUNTIME handles no YAML author can
# name -- a card-pool index (EXHAUST's amount, SET_COST's src) or a monster slot
# decided by a native module (the split framework's ROLL_MOVE / CANNOT_LOSE /
# CAN_LOSE / SUICIDE / SPAWN_MONSTER / SET_MOVE, emitted by monster_slime_large.cpp
# and monster_slime_boss.cpp).
ENGINE_EMITTED_OPS = frozenset({
    "EXHAUST", "SET_COST", "ROLL_MOVE",
    "CANNOT_LOSE", "CAN_LOSE", "SUICIDE", "SPAWN_MONSTER", "SET_MOVE",
    # ESCAPE's operand is the escaping monster's slot, decided by its native
    # take-turn body (monster_looter.cpp) -- same category as SUICIDE/SET_MOVE.
    "ESCAPE",
    # USE_CARD's operand is the played card's runtime pool index; emitted only
    # by resolve_card_play (card_play.cpp) as the queued UseCardAction.update.
    "USE_CARD",
})

# Every opcode must land in exactly one group: a new opcode is then a DELIBERATE
# authorability decision instead of a silently-unauthorable (or silently
# mispacked) one. This fires at generation time, with the offending names.
_GROUPED = GENERAL_OPS | CARD_CONTEXT_OPS | ENGINE_EMITTED_OPS
assert _GROUPED == set(OPCODES), (
    "steps.py op capability groups must partition OPCODES; unclassified: "
    f"{sorted(set(OPCODES) - _GROUPED)}, unknown: {sorted(_GROUPED - set(OPCODES))}")
assert (len(GENERAL_OPS) + len(CARD_CONTEXT_OPS) + len(ENGINE_EMITTED_OPS) ==
        len(OPCODES)), "steps.py op capability groups must be disjoint"


class StepDomain:
    """One authoring domain: its label, its op allowlist, and its message hints.

    `label` names the domain in errors ("cards.yaml", "powers.yaml", ...);
    `allowed_ops` is the closed set of ops rows in that domain may author;
    `power_hint` appends domain-specific advice to the unknown-power message.
    """

    __slots__ = ("key", "label", "allowed_ops", "power_hint")

    def __init__(self, key: str, label: str, allowed_ops: frozenset,
                 *, power_hint: str = ""):
        self.key = key
        self.label = label
        self.allowed_ops = allowed_ops
        self.power_hint = power_hint


# Card effect programs (effects / upgraded / on_exhaust / upgraded_on_exhaust):
# the only domain whose queue helper carries the played-card instance, so it is
# the only one that may author the CARD_CONTEXT ops.
CARD_DOMAIN = StepDomain("cards", "cards.yaml", GENERAL_OPS | CARD_CONTEXT_OPS)

# Power hook programs: power_hooks.cpp queue_hook_step sets src/tgt from the
# power's owner and flags = step.extra -- the GENERAL set, no card instance.
POWER_DOMAIN = StepDomain("powers", "powers.yaml", GENERAL_OPS)

# Relic hook programs: relic_hooks.cpp queue_relic_step, same shape as powers
# (src is always the player).
RELIC_DOMAIN = StepDomain("relics", "relics.yaml", GENERAL_OPS)

# Potion USE programs: potions.cpp queue_use_step, same shape again.
POTION_DOMAIN = StepDomain(
    "potions", "potions.yaml", GENERAL_OPS,
    power_hint=" (potion-granted powers must be registered in powers.yaml first)")

# Monster move programs. monster_dispatch.cpp queue_monster_move_effects sets
# src = the acting monster, tgt = the monster or the player, and does the
# MAKE_CARD CardPile split (Slimed/Wound authoring) -- so MAKE_CARD is in, but
# the rest of the CARD_CONTEXT set is not. The player-pile / player-economy ops
# (DRAW, GAIN_ENERGY, SHUFFLE_IN, DISCARD_HAND and the card-manipulation verbs)
# are excluded: no monster move queues them, and the ones a monster move could
# plausibly need land on an actor (DAMAGE/BLOCK/LOSE_HP) or a power slot
# (APPLY_POWER/REMOVE_POWER/REDUCE_POWER).
MONSTER_MOVE_OPS = frozenset({
    "NOP", "DAMAGE", "BLOCK", "LOSE_HP",
    "APPLY_POWER", "REMOVE_POWER", "REDUCE_POWER", "MAKE_CARD",
})
MONSTER_DOMAIN = StepDomain("monsters", "monsters.yaml", MONSTER_MOVE_OPS)


def check_op(domain: StepDomain, owner: str, op) -> None:
    """Reject an unknown op, and an op this domain may not author.

    The second check is the one that closes the silent-mispack hole: before it
    existed, an op that passed the OPCODES membership check but had no packing
    branch in the calling domain's copy of the parser emitted `extra = 0`.
    """
    if op not in OPCODES:
        raise fail(f"{owner} uses unknown op {op!r}")
    if op in domain.allowed_ops:
        return
    if op in ENGINE_EMITTED_OPS:
        why = ("it is emitted natively by engine code (its operands are runtime "
               "handles -- a card-pool index or a monster slot) and is never "
               "authored in YAML")
    elif op in CARD_CONTEXT_OPS:
        why = ("it is completed from the played card's instance at queue time "
               "(card_play.cpp queue_effect_step), which only card effect "
               "programs have")
    else:
        why = "it is not queueable from this domain's dispatch path"
    raise fail(f"{owner} uses op {op!r}, which the {domain.label} domain does "
               f"not support: {why}. Supported ops: "
               f"{sorted(domain.allowed_ops)}")


def pack_extra(domain: StepDomain, owner: str, op: str, step: dict,
               powers: dict[str, int], cards: dict[str, int]) -> int:
    """The step's `extra` payload for `op` -- the ONE packing table.

    Ops with no branch here carry `extra = 0` legitimately (their operands ride
    in `amount`/`target`); `check_op` has already guaranteed the op is one this
    domain may author, so a 0 here is a decision, not a gap.
    """
    if op in ("APPLY_POWER", "REMOVE_POWER", "REDUCE_POWER"):
        # All three pack a PowerId into `extra` (make_apply_power_flags; op_remove_
        # power / op_reduce_power read the same low-16 packing). REMOVE_POWER
        # authoring is the B3.6 self-removal pattern (NoDrawPower.atEndOfTurn ->
        # RemoveSpecificPowerAction on the owner, NoDrawPower.java:33-37).
        pname = step.get("power")
        if pname not in powers:
            raise fail(f"{owner} {op} references unknown power {pname!r}"
                       f"{domain.power_hint}")
        # make_apply_power_flags: low 16 bits carry the PowerId.
        return powers[pname]

    if op == "DAMAGE":
        # `extra` = the DamageType (interp.hpp make_damage_flags). Default
        # NORMAL (0) keeps every existing DAMAGE step byte-identical; a
        # status/curse self-hit sets THORNS (Burn/Decay) so it skips the
        # NORMAL-only power pipeline (no self Strength/Vulnerable scaling). A
        # relic's DamageAllEnemies hit is THORNS the same way (Mercury Hourglass
        # -- createDamageMatrix(n, true) + DamageType.THORNS,
        # MercuryHourglass.java:37).
        dt = str(step.get("damage_type", "NORMAL")).upper()
        if dt not in DAMAGE_TYPES:
            raise fail(f"{owner} DAMAGE has unknown damage_type "
                       f"{step.get('damage_type')!r} "
                       f"(known: {sorted(DAMAGE_TYPES)})")
        return DAMAGE_TYPES[dt]

    if op == "CHOOSE_CARD":
        # `extra` packs the ChoiceKind + RANDOM bit (interp.hpp make_choose_flags).
        # B3.6: kind bit 2 lives at extra bit 3 (bit 2 is RANDOM); `copies`
        # (duplicate kind, Dual Wield magicNumber) packs copies-1 in bits 4-7
        # so the default 1 leaves every pre-B3.6 extra byte-identical.
        kind = str(step.get("choose", "")).lower()
        if kind not in CHOICE_KINDS:
            raise fail(f"{owner} CHOOSE_CARD has unknown choose "
                       f"{step.get('choose')!r} (known: {sorted(CHOICE_KINDS)})")
        kv = CHOICE_KINDS[kind]
        extra = (kv & 0x3) | ((kv >> 2) * CHOICE_KIND_HIGH_BIT)
        if bool(step.get("random", False)):
            extra |= CHOICE_RANDOM_BIT
        copies = int(step.get("copies", 1))
        if copies < 1 or copies > 16:
            raise fail(f"{owner} CHOOSE_CARD copies {copies} out of range 1..16")
        if copies != 1 and kind != "duplicate":
            raise fail(f"{owner} CHOOSE_CARD 'copies' is only meaningful for "
                       f"choose: duplicate")
        return extra | ((copies - 1) << CHOICE_COPIES_SHIFT)

    if op == "MAKE_CARD":
        # `extra` = CardId | (CardPile << 16) | (upgraded-copy << 24).
        # card_play.cpp (and monster_dispatch.cpp for monster moves) splits the
        # CardPile back out into the ActionQueueItem's `src`.
        csym = step.get("card")
        if csym not in cards:
            raise fail(f"{owner} MAKE_CARD references unknown card {csym!r}")
        pile = str(step.get("pile", "")).upper()
        if pile not in CARD_PILES:
            raise fail(f"{owner} MAKE_CARD has unknown pile "
                       f"{step.get('pile')!r} (known: {sorted(CARD_PILES)})")
        extra = cards[csym] | (CARD_PILES[pile] << 16)
        if bool(step.get("upgraded_copy", False)):
            extra |= CARD_MAKE_UPGRADED_BIT
        return extra

    if op == "DAMAGE_STR_MULT":
        # Strength counts x `mult` (Heavy Blade magicNumber).
        return int(step.get("mult", 1))

    if op == "DAMAGE_PER_STRIKE":
        # +`per` damage per "Strike"-named card (Perfected Strike magicNumber).
        return int(step.get("per", 0))

    if op == "DAMAGE_FEED":
        # `extra` = the max-HP gained when the hit leaves the target dead
        # (Feed's magicNumber -> FeedAction.increaseHpAmount, FeedAction.java:28).
        return int(step.get("max_hp", 0))

    if op == "PLAY_CARD":
        # `extra` = the kPlayCard* bit set (interp.hpp), authored as a list of
        # names. `copy` is rejected: its source is a runtime card-pool index that
        # no YAML author can name, so that form is emitted natively.
        bits = 0
        for name in (step.get("play") or []):
            key = str(name).lower()
            if key not in PLAY_CARD_FLAGS:
                raise fail(f"{owner} PLAY_CARD has unknown play flag {name!r} "
                           f"(known: {sorted(PLAY_CARD_FLAGS)})")
            if key == "copy":
                raise fail(f"{owner} PLAY_CARD cannot author 'copy': the copied "
                           f"instance is a runtime card-pool index, so that form "
                           f"is emitted by engine code, not by a YAML program")
            bits |= PLAY_CARD_FLAGS[key]
        if (bits & PLAY_CARD_FLAGS["from_draw_top"]) == 0:
            raise fail(f"{owner} PLAY_CARD must author 'from_draw_top': it is the "
                       f"only source a YAML program can name")
        return bits

    if op == "CONDITIONAL_DRAW":
        # `extra` = the RESTRICTED CardType (ConditionalDrawAction's
        # restrictedType ctor argument, :20-27): the draw happens only when NO
        # hand card has that type. Required, never defaulted -- ATTACK packs as
        # 0, so an omitted key would be indistinguishable from `ATTACK` and
        # would silently author Impatience's exact semantics onto a row that
        # meant something else.
        ct = step.get("card_type")
        if ct is None:
            raise fail(f"{owner} CONDITIONAL_DRAW needs an explicit "
                       f"card_type: (known: {sorted(CARD_TYPES)})")
        ct = str(ct).upper()
        if ct not in CARD_TYPES:
            raise fail(f"{owner} CONDITIONAL_DRAW has unknown card_type "
                       f"{step.get('card_type')!r} (known: {sorted(CARD_TYPES)})")
        return CARD_TYPES[ct]

    if op in ("DAMAGE_UPGRADE_SCALE", "DAMAGE_RAMPAGE"):
        # Dynamic per-instance damage: `increment` is the first-upgrade
        # increment (Searing Blow) or post-play misc increment (Rampage).
        return int(step.get("increment", 0))

    return 0


def parse_steps(domain: StepDomain, owner: str, effects,
                powers: dict[str, int],
                cards: dict[str, int] | None = None) -> list:
    """Parse one effect program into (op, amount, extra, target) tuples.

    `owner` is the fully-qualified row description used in error messages
    ("cards.yaml: card BASH", "powers.yaml: power VIGOR hook at_end_of_turn").
    `target: SELF` means the program's owner (the player for cards/relics/
    potions, the power's owner for power hooks); ALL_ENEMY / RANDOM_ENEMY fan out
    at execute time. A relic/potion step's `amount` is always a literal; a power
    step's 0 amount means "the power's stack amount" (power_hooks.cpp).
    """
    steps = []
    for step in effects or []:
        op = step.get("op")
        check_op(domain, owner, op)
        amount = int(step.get("amount", 0))
        st = step.get("target", "SELF")
        if st not in STEP_TARGETS:
            raise fail(f"{owner} step has unknown target {st!r} "
                       f"(known: {sorted(STEP_TARGETS)})")
        extra = pack_extra(domain, owner, op, step, powers, cards or {})
        steps.append((OPCODES[op], amount, extra, STEP_TARGETS[st]))
    return steps


# --- Emission helpers (shared by every table that stores CardEffectStep) ------

def step_literal(step) -> str:
    """One C++ CardEffectStep aggregate initializer."""
    op, amount, extra, tgt = step
    op_name = next(k for k, v in OPCODES.items() if v == op)
    tgt_name = next(k for k, v in STEP_TARGETS.items() if v == tgt)
    return (f"{{Opcode::{op_name}, {amount}, {extra}, "
            f"StepTarget::{tgt_name}}}")


def padded_step_literals(steps, width: int) -> list[str]:
    """`steps` as literals, NOP-padded to the table's fixed array width."""
    padded = list(steps)
    while len(padded) < width:
        padded.append((OPCODES["NOP"], 0, 0, STEP_TARGETS["SELF"]))
    return [step_literal(s) for s in padded]
