"""Frozen vocabularies shared by every registry emitter (design doc §4.3/§4.4).

Everything in this module MIRRORS an engine header. Values are pinned and
append-only: the generated headers ``static_assert`` them byte-equal to the
engine's enums, so a renumber here is a compile error there. The provenance
comments are load-bearing -- they record WHY a bit-packing looks the way it does
(the Java source line a value was derived from), and they must travel with the
value they document.
"""

from __future__ import annotations

import re

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
    # Stage B B3.1 additions (append-only from 9, design doc §4.4).
    "MAKE_CARD": 9,
    "SET_COST": 10,
    # Stage B B3.2 addition: card/self HP loss (bypasses block, HP_LOSS type;
    # the firing site for the wasHPLost hook -- Rupture attribution).
    "LOSE_HP": 11,
    # Stage B B3.4 additions (append-only): CHOOSE-in-combat hand-card select
    # (exhaust/put-on-draw-top/upgrade), play-top-of-draw (Havoc), and
    # remove-power (LoseStrength self-removal -- emitted natively, not authored in
    # YAML, but pinned in the enum for the cards.hpp drift check).
    "CHOOSE_CARD": 12,
    "PLAY_TOP_DRAW": 13,
    "REMOVE_POWER": 14,
    # Stage B B3.3 additions (append-only from 15): dynamic-base attack damage.
    # DAMAGE_BLOCK reads the player's current block as the base at EXECUTE time
    # (Body Slam, BodySlam.java:96). DAMAGE_STR_MULT deals `amount` base with the
    # player's Strength counted x `extra` (Heavy Blade, HeavyBlade.java:426-435).
    # DAMAGE_PER_STRIKE deals `amount` + `extra` per STRIKE-tagged card in
    # hand+draw+discard. It is BAKED into a plain DAMAGE at queue time, matching
    # applyPowers-at-use timing: a hand play counts itself because it has not
    # left the hand yet; an autoplay already in limbo does not
    # (PerfectedStrike.java:37-52; AbstractPlayer.java:1361,1373).
    "DAMAGE_BLOCK": 15,
    "DAMAGE_STR_MULT": 16,
    "DAMAGE_PER_STRIKE": 17,
    # Stage B B3.9 addition (append-only from 18): dynamic self HP loss scaled by
    # the current hand size (Regret -- magicNumber = player.hand.size() locked in
    # triggerOnEndOfTurnForPlayingCard, Regret.java:35-38). Bypasses block (HP_LOSS
    # type). The hand count is read at EXECUTE time; the end-of-turn card triggers
    # queue their programs BEFORE the ethereal-exhaust sweep (action_queue.cpp), so
    # the ethereal cards are still in hand when this resolves -- matching the game's
    # trigger-time lock (nothing between trigger and resolve changes the hand).
    "LOSE_HP_PER_HAND": 18,
    "DISCARD_HAND": 19,
    "REDUCE_POWER": 20,
    # Stage B B3.5 additions (append-only): Dropkick's execute-time Vulnerable
    # branch; Searing Blow's upgrade-count damage; Rampage's per-instance misc
    # scaling; Sever Soul's exhaust-all-non-Attacks action.
    "DROPKICK": 21,
    "DAMAGE_UPGRADE_SCALE": 22,
    "DAMAGE_RAMPAGE": 23,
    "EXHAUST_NON_ATTACKS": 24,
    # Stage B B3.17 additions (append-only from 25): the large-slime split
    # framework's queued actions. CANNOT_LOSE / CAN_LOSE set/clear the room's
    # cannotLose latch (CannotLoseAction.java:12-15 / CanLoseAction.java:12-15)
    # so combat cannot terminate between the parent's suicide and the children's
    # spawns. SUICIDE zeroes the target monster's HP, dying WITHOUT relic
    # triggers when flags bit0 is clear (SuicideAction.java:29-36,
    # die(relicTrigger=false)). SPAWN_MONSTER inserts a monster record at slot
    # `tgt` with `amount` HP and MonsterId in flags low16, then runs the spawned
    # monster's init() rollMove (SpawnMonsterAction.java:42-73). SET_MOVE re-sets
    # a monster's decided move (`amount`) + intent (flags low8) at resolve time
    # (SetMoveAction.java:52-56) -- the split interrupt's queued override. All
    # five are emitted natively by monster modules, never authored in YAML move
    # programs, but pinned in the enum for the cards.hpp drift check.
    "CANNOT_LOSE": 25,
    "CAN_LOSE": 26,
    "SUICIDE": 27,
    "SPAWN_MONSTER": 28,
    "SET_MOVE": 29,
    # Stage B B3.6 additions (append-only from 30): red uncommon skills.
    # DOUBLE_BLOCK: Entrench / DoubleYourBlockAction.update (:25-28) -- if the
    # target has block, addBlock(currentBlock). BLOCK_PER_NON_ATTACK: Second Wind
    # / BlockPerNonAttackAction.update (:29-42) -- exhaust every non-Attack in
    # hand (reverse hand order), then one GainBlock per exhausted card.
    # SPOT_WEAKNESS: SpotWeaknessAction.update (:33-39) -- if the target's intent
    # deals attack damage (getIntentBaseDmg() >= 0), apply `amount` Strength to
    # the player. RANDOM_ATTACK_TO_HAND: Infernal Blade / AbstractDungeon.
    # returnTrulyRandomCardInCombat(ATTACK) (:964-979) -- ONE cardRandomRng draw
    # over the combat attack pool, copy costs 0 this turn, into the hand.
    "DOUBLE_BLOCK": 30,
    "BLOCK_PER_NON_ATTACK": 31,
    "SPOT_WEAKNESS": 32,
    "RANDOM_ATTACK_TO_HAND": 33,
    # Red-rare additions (append-only from 34). PLAY_CARD is the GENERAL
    # recursive-play verb -- queue a card instance to be auto-played free of
    # energy cost; its `flags` (interp.hpp kPlayCard*) choose the source (a
    # runtime card-pool index, or the top of the draw pile), whether a
    # stat-equivalent COPY is played (DoubleTapPower.java:50-60), and the played
    # instance's disposition. Only the draw-top form is authorable in YAML: the
    # copy form's operand is a runtime pool index. DAMAGE_FEED: FeedAction.update
    # (:34-47) -- damage, then increaseMaxHp if the hit left the target dead.
    # FIEND_FIRE: FiendFireAction.update (:32-46) -- hand-size-many random
    # exhausts, then hand-size-many hits. DOUBLE_STRENGTH: LimitBreakAction.update
    # (:27-32) -- addToTop Strength == the current Strength stack.
    # VAMPIRE_DAMAGE_ALL: VampireDamageAllEnemiesAction.update (:53-77) -- hit
    # every live monster, then heal the summed HP actually lost. HEAL: the queued
    # HealAction, routed through the onPlayerHeal relic seam.
    "PLAY_CARD": 34,
    "DAMAGE_FEED": 35,
    "FIEND_FIRE": 36,
    "DOUBLE_STRENGTH": 37,
    "VAMPIRE_DAMAGE_ALL": 38,
    "HEAL": 39,
    # Looter-batch addition (append-only from 40; 41-42 are the batch's published
    # reserve and stay unissued). ESCAPE is the queued EscapeAction
    # (EscapeAction.java:21-28 -> AbstractMonster.escape, AbstractMonster.java:
    # 915-919): the tgt monster leaves the fight ALIVE -- kMonsterFlagEscaped,
    # not an HP write -- and the pump's liveness predicate (monster_dead_or_
    # escaped) is what then ends the battle when nobody is left in it. Emitted
    # natively by monster modules (the operand is a monster slot), never
    # authored in YAML move programs, but pinned in the enum for the cards.hpp
    # drift check -- the SET_MOVE precedent exactly.
    "ESCAPE": 40,
    # Colorless-uncommon additions (append-only from 45; 41-42 are the Looter
    # batch's published reserve and 43-44 are B4.5's block -- deliberately not
    # filled here).
    # DAMAGE_DRAW_PILE: Mind Blast -- baseDamage = drawPile.size()
    # (MindBlast.applyPowers, MindBlast.java:47-52), then the ordinary
    # DamageInfo pipeline. CONDITIONAL_DRAW: Impatience --
    # ConditionalDrawAction.update (:29-45) addToTop's a DrawCardAction(amount)
    # only when NO hand card has the restricted CardType (`extra` carries that
    # type). RESHUFFLE_ALL: Deep Breath -- the FUSED pair of shuffles
    # DeepBreath.use guards on the SAME `discardPile.size() > 0` test
    # (DeepBreath.java:34-38): EmptyDeckShuffleAction (shuffle the discard, move
    # it onto the draw pile) AND ShuffleAction(drawPile) (shuffle the whole draw
    # pile) -- TWO shuffleRng.randomLong() draws, or zero. It cannot be two
    # authored steps: after the first runs the discard is empty either way, so a
    # second step can no longer see the guard's input. MADNESS: MadnessAction.
    # update (:26-65) -- rejection-sample the hand with cardRandomRng until an
    # eligible card is hit, then permanently zero its cost.
    "DAMAGE_DRAW_PILE": 45,
    "CONDITIONAL_DRAW": 46,
    "RESHUFFLE_ALL": 47,
    "MADNESS": 48,
    # The played card's filing action, allocated at 53 because 49-52 remain
    # exclusively reserved for the open colorless-card opcode block and
    # permanent gaps are never backfilled.
    # USE_CARD is
    # UseCardAction.update (UseCardAction.java:77-137) as a queued action:
    # AbstractPlayer.useCard queues the card's own actions FIRST and this LAST
    # (:1369-1375), so the played card sits in the CombatState limbo pile -- in
    # NO observable pile -- until this resolves, which then rolls Strange Spoon
    # (:109-113) and files the card away (discard / exhaust / purge-poof /
    # POWER). Emitted only by resolve_card_play (the operand is a runtime
    # card-pool index), never authored in YAML, but pinned in the enum for the
    # cards.hpp drift check -- the SET_MOVE / ESCAPE precedent.
    "USE_CARD": 53,
}
# CHOOSE_CARD manipulation kind -- MIRROR of interp.hpp ChoiceKind (Stage B B3.4).
# A CHOOSE_CARD effect step in cards.yaml carries `choose: <kind>` (+ optional
# `random: true`); the step's `extra` packs kind (bits 0-1) | RANDOM (bit 2),
# byte-identical to make_choose_flags() in interp.hpp. Stage B B3.3 adds
# `discard_to_draw_top` (source pile = discard, not hand): choose a card from the
# DISCARD pile and put it on top of the draw pile (Headbutt /
# DiscardPileToTopOfDeckAction).
CHOICE_KINDS = {"exhaust": 0, "put_on_draw_top": 1, "upgrade": 2,
                "discard_to_draw_top": 3,
                # B3.6 (Dual Wield / DualWieldAction): choose one ATTACK/POWER
                # hand card and add stat-equivalent copies. Kind 4 needs a third
                # kind bit; bit 2 is RANDOM, so the high kind bit lives at bit 3
                # (kind = bits[0..1] | bit3 << 2) -- kinds 0-3 pack unchanged.
                "duplicate": 4,
                # Exhume / ExhumeAction: the source pile is the EXHAUST pile --
                # choose one exhausted card and return it to the hand
                # (ExhumeAction.update:38-113). Kind 5 packs as 1 | the high kind
                # bit, so kinds 0-3 stay byte-identical.
                "exhaust_to_hand": 5}
CHOICE_RANDOM_BIT = 1 << 2
CHOICE_KIND_HIGH_BIT = 1 << 3
# CHOOSE_CARD `copies` (duplicate kind only): bits [4..7] hold copies - 1, so the
# default (1 copy) encodes as 0 and every pre-B3.6 packed extra is byte-identical.
CHOICE_COPIES_SHIFT = 4
# StepTarget: cards.hpp. SELF=0 (player), CARD_TARGET=1 (the played-on monster).
# Stage B B3.1 adds ALL_ENEMY=2 (execute-time fan-out over live monsters) and
# RANDOM_ENEMY=3 (one card_random_rng draw per resolved step).
STEP_TARGETS = {"SELF": 0, "CARD_TARGET": 1, "ALL_ENEMY": 2, "RANDOM_ENEMY": 3}
# CardType: cards.hpp. Stage B B3.3 adds STATUS=2 (Wound, the first status card,
# created by Wild Strike); B3.9 adds CURSE=3 (the 10 poolable curses + Ascender's
# Bane). B3.6 appends POWER=4 (no POWER card row exists yet -- the value exists
# because Dual Wield's eligibility predicate is `type == ATTACK || type == POWER`,
# DualWieldAction.isDualWieldable:95-97; B3.7 lands the power cards themselves).
CARD_TYPES = {"ATTACK": 0, "SKILL": 1, "STATUS": 2, "CURSE": 3, "POWER": 4}
# DamageType (interp.hpp DamageType): the DAMAGE opcode's `flags` low byte. A card
# DAMAGE step defaults to NORMAL (the full applyPowers pipeline); a status/curse's
# self-inflicted DAMAGE is THORNS (Burn/Decay -- new DamageInfo(player, n, THORNS),
# Burn.java:45 / Decay.java:43), which skips the NORMAL-only power hooks so the
# player's own Strength/Vulnerable do NOT scale the self-damage (but block still
# absorbs it, unlike LOSE_HP). MIRROR of interp.hpp make_damage_flags.
DAMAGE_TYPES = {"NORMAL": 0, "THORNS": 1, "HP_LOSS": 2}
# CardTrigger (cards.hpp CardTrigger): WHEN a card's effect program runs. Default
# ON_PLAY (the played-card path, every ATTACK/SKILL + playable Slimed). The
# unplayable statuses/curses instead run their `effects` program at a passive
# trigger: END_OF_TURN (Burn/Decay/Doubt/Regret/Shame --
# triggerOnEndOfTurnForPlayingCard, the stage-a §5.4 sentinel path), ON_DRAW (Void
# -- triggerWhenDrawn, VoidCard.java:24-26), ON_OTHER_CARD_PLAYED (Pain --
# triggerOnOtherCardPlayed, Pain.java:34-36). Because these cards are unplayable,
# reusing their `effects`/`upgraded` program for the trigger is unambiguous (it is
# never run on play), and the two-row upgrade selection carries Burn's 2->4.
CARD_TRIGGERS = {"on_play": 0, "end_of_turn": 1, "on_draw": 2,
                 "on_other_card_played": 3}
# CardPile destination for a MAKE_CARD effect step (Stage B B3.3 card-authoring;
# MIRROR of interp.hpp CardPile). The step's `extra` packs the created CardId in
# bits 0-15, the CardPile in bits 16-23, and an "upgraded copy" flag in bit 24
# (makeStatEquivalentCopy of an upgraded card -- Anger); card_play.cpp splits it
# back into the ActionQueueItem {flags=CardId(+upg bit), src=CardPile}.
CARD_PILES = {"HAND": 0, "DRAW": 1, "DISCARD": 2, "DRAW_RANDOM": 3}
CARD_MAKE_UPGRADED_BIT = 1 << 24

# PLAY_CARD `extra` bits -- MIRROR of interp.hpp's kPlayCard* constants. A step
# authors them as a list, e.g. `{op: PLAY_CARD, play: [from_draw_top]}`. `copy`
# names a runtime card-pool source and so is only ever set by engine code; it is
# listed here to keep the mirror complete.
PLAY_CARD_FLAGS = {
    "copy": 1 << 0,
    "purge": 1 << 1,
    "exhaust": 1 << 2,
    "from_draw_top": 1 << 3,
    "queue_front": 1 << 4,
}

# CardFlag bits -- MIRROR of include/sts/engine/types.hpp CardFlag (append-only).
# YAML `flags:` names are lower-case; cards.hpp static_asserts the emitted
# kCardFlag* constants equal the engine's CardFlag values.
CARD_FLAGS = {
    "exhaust": 1 << 0,
    "ethereal": 1 << 1,
    "innate": 1 << 2,
    "unplayable": 1 << 3,
    "retain": 1 << 4,
    "xcost": 1 << 5,
    # B3.6: per-INSTANCE runtime bit (never authored in YAML -- listed only so
    # the mirror stays complete): setCostForTurn marked this instance's cost_now
    # as a this-turn-only modification; AbstractRoom.endTurn:397-405 resets it.
    "cost_modified_for_turn": 1 << 6,
    # Per-INSTANCE runtime bit (never authored in YAML -- listed only so the
    # mirror stays complete): AbstractCard.purgeOnUse, the replay copy that is
    # destroyed when it resolves instead of going to a pile
    # (UseCardAction.java:89-94, DoubleTapPower.java:59).
    "purge_on_use": 1 << 7,
}

# Power hook points (generated sts::registry::Hook) -- MIRROR of the engine's
# include/sts/engine/power_hooks.hpp Hook enum. Values are pinned and append-only
# (design doc §4.4); powers.hpp static_asserts the generated Hook byte-equal to
# the engine's. YAML `hooks:` keys are these lower-case names. The frozen dispatch
# order (stage-a §5.2-5.5) lives in the framework (power_hooks.cpp), NOT in this
# numbering -- these are identity tags, not a sequence.
HOOKS = {
    "on_play_card": 0,               # §5.3 card-play fan-out
    "on_use_card": 1,                # UseCardAction fan-out
    "on_exhaust": 2,                 # §5.5 CardGroup.moveToExhaustPile
    "on_card_draw": 3,               # per drawn card, in draw
    "at_end_of_turn_pre_card": 4,    # §5.4 applyEndOfTurnPreCardPowers
    "at_end_of_turn": 5,             # applyEndOfTurnTriggers
    "at_end_of_round": 6,            # decrement-at-round-end
    "at_start_of_turn": 7,           # §5.2 step 6, pre-draw
    "at_start_of_turn_post_draw": 8, # §5.2 step 6, post-draw
    "on_gained_block": 9,            # inside the BLOCK opcode
    "on_attacked": 10,               # DAMAGE receive path
    "on_apply_power": 11,            # inside the APPLY_POWER opcode
    "was_hp_lost": 12,               # after an HP write (LOSE_HP / DAMAGE)
    "on_death": 13,                  # actor death
}
# Power type (AbstractPower.PowerType): the APPLY_POWER interception (Artifact /
# Sadistic) reads this. Pinned/append-only (fixtures never store it, but the
# translator's power table joins on it).
POWER_TYPES = {"BUFF": 0, "DEBUFF": 1}
# Stacking behaviour: INTENSITY == additive amount (stackPower this.amount +=);
# NONE == a non-stacking marker for a bespoke consumer. Default INTENSITY.
POWER_STACK = {"intensity": 0, "none": 1}

# Relic hook points (generated sts::registry::RelicHook) -- MIRROR of the engine's
# include/sts/engine/relic_hooks.hpp RelicHook enum (B3.24). Values are pinned and
# append-only (design doc §4.4); relics.hpp static_asserts the generated RelicHook
# byte-equal to the engine's. YAML `hooks:` keys are these lower-case names. The
# frozen ACQUISITION-order dispatch (stage-a trap 8) lives in relic_hooks.cpp, NOT
# in this numbering -- these are identity tags. DISTINCT from the power Hook enum:
# relics carry battle-start/turn-start/end-turn/victory hooks powers do not
# (AbstractRelic's hook inventory, AbstractRelic.java:492-620).
RELIC_HOOKS = {
    "at_pre_battle": 0,              # AbstractRelic.atPreBattle (pre-combat reset)
    "at_battle_start": 1,           # atBattleStart
    "at_battle_start_pre_draw": 2,  # atBattleStartPreDraw
    "at_turn_start": 3,             # atTurnStart (pre-draw)
    "at_turn_start_post_draw": 4,   # atTurnStartPostDraw
    "on_player_end_turn": 5,        # onPlayerEndTurn
    "on_use_card": 6,               # onUseCard
    "on_play_card": 7,              # onPlayCard
    "on_exhaust": 8,                # onExhaust
    "on_card_draw": 9,              # onCardDraw
    "on_gained_block": 10,          # onPlayerGainedBlock
    "was_hp_lost": 11,              # wasHPLost / onLoseHp
    "on_attack": 12,                # onAttack / onAttackToChangeDamage
    "on_victory": 13,               # onVictory (combat end)
    # --- B3.25 additions (append-only, design doc §4.4) ---
    "on_monster_death": 14,         # onMonsterDeath (AbstractMonster.die:933-937)
    "on_shuffle": 15,               # onShuffle (EmptyDeckShuffleAction ctor :37-39)
    # onBlockBroken (AbstractCreature.brokeBlock:159-167) -- fired ONLY when the
    # creature whose block was reduced to zero is an AbstractMonster, reached from
    # decrementBlock (:169-183). Hand Drill.
    "on_block_broken": 16,
}
# RelicTier (AbstractRelic.RelicTier). Pinned/append-only; the translator's relic
# table joins on it and reward/shop pools gate by it (design doc §5.3).
RELIC_TIERS = {
    "STARTER": 0, "COMMON": 1, "UNCOMMON": 2, "RARE": 3,
    "BOSS": 4, "SHOP": 5, "SPECIAL": 6, "EVENT": 7,
}

# Potion rarity (AbstractPotion.PotionRarity): the reward tier gate reads it
# (65/25/10, PotionHelper.java:70-71). Pinned/append-only; the potion table and
# the identity roll (AbstractDungeon.returnRandomPotion) join on it. YAML
# `rarity:` names are these UPPER-case keys.
POTION_RARITIES = {"COMMON": 0, "UNCOMMON": 1, "RARE": 2}

# Card-level targeting -> (needs_target, random_target, stable target-kind value).
# The target kind preserves the Java CardTarget distinction at dequeue time:
# ENEMY alone takes GameActionManager's post-hook dead/escaping suppression,
# while SELF_AND_ENEMY (Spot Weakness) deliberately does not.
CARD_TARGET_KINDS = {
    "ENEMY": 0,
    "ALL_ENEMY": 1,
    "SELF": 2,
    "NONE": 3,
    "RANDOM_ENEMY": 4,
    "SELF_AND_ENEMY": 5,
}
CARD_TARGETING = {
    "ENEMY": (True, False, CARD_TARGET_KINDS["ENEMY"]),
    "ALL_ENEMY": (False, False, CARD_TARGET_KINDS["ALL_ENEMY"]),
    "SELF": (False, False, CARD_TARGET_KINDS["SELF"]),
    "NONE": (False, False, CARD_TARGET_KINDS["NONE"]),
    "RANDOM_ENEMY": (False, True, CARD_TARGET_KINDS["RANDOM_ENEMY"]),
    "SELF_AND_ENEMY": (True, False, CARD_TARGET_KINDS["SELF_AND_ENEMY"]),
}

# Monster-move telegraphed intents (generated MonsterIntent). Values are pinned
# here and append-only, exactly like the id enums (design doc §4.4): fixtures
# store MonsterState.intent as a raw byte. NONE=0 is the value-init default.
MONSTER_INTENTS = {
    "NONE": 0,
    "ATTACK": 1,
    "DEFEND_BUFF": 2,
    "ATTACK_DEFEND": 3,
    "BUFF": 4,       # Cultist Incantation, Louse Strengthen (AbstractMonster.Intent.BUFF)
    "DEBUFF": 5,     # Louse Defensive Weaken (AbstractMonster.Intent.DEBUFF)
    "ATTACK_DEBUFF": 6,  # slime tackle + Slimed (AbstractMonster.Intent.ATTACK_DEBUFF)
    "UNKNOWN": 7,        # large-slime Split telegraph (AbstractMonster.Intent.UNKNOWN;
                         # AcidSlime_L.java:137,146 / SpikeSlime_L.java:124,134)
    "STRONG_DEBUFF": 8,  # Slime Boss Goop Spray (AbstractMonster.Intent.STRONG_DEBUFF;
                          # SlimeBoss.java:128,145,188)
    "SLEEP": 9,          # Lagavulin's dormant turns (AbstractMonster.Intent.SLEEP;
                          # Lagavulin.java:144,225)
    "STUN": 10,          # Lagavulin's woken-by-damage turn (AbstractMonster.Intent.STUN;
                          # Lagavulin.java:202)
    "DEFEND": 11,        # Gremlin Tsundere Protect (AbstractMonster.Intent.DEFEND;
                          # GremlinTsundere.java:84,124) and The Guardian's Charge Up
                          # (TheGuardian.java:215,228) -- one Intent constant, two
                          # users. A pure block gain, with no attack and no buff, so
                          # neither DEFEND_BUFF (2) nor ATTACK_DEFEND (3) is the same
                          # telegraph; Charge Up is a bare GainBlockAction
                          # (TheGuardian.java:219).
    "ATTACK_BUFF": 12,   # The Guardian's Twin Slam (AbstractMonster.Intent.ATTACK_BUFF;
                          # TheGuardian.java:204). A DISTINCT Intent constant from
                          # ATTACK_DEFEND=3; the "buff" is the queued return to
                          # Offensive Mode (TheGuardian.java:194).
    "ESCAPE": 13,        # The Looter's telegraphed escape (AbstractMonster.Intent.
                          # ESCAPE; Looter.java:123,131 -- Smoke Bomb telegraphs it
                          # and the Escape turn re-telegraphs it). Value 14 is a
                          # published reserve for the same batch and stays unissued.
}
# Monster-move effect target (generated MonsterMoveTarget): SELF = the acting
# monster itself; PLAYER = the player (the game's AbstractDungeon.player).
MONSTER_MOVE_TARGETS = {"SELF": 0, "PLAYER": 1}

# Native per-instance monster rolls. These are registry data even when a native
# monster module consumes them: the stream and lifecycle phase are as
# bit-exactness-relevant as the range columns (B3.13 louse bite/Curl Up).
MONSTER_ROLL_STREAMS = {'MONSTER_HP': 0}
MONSTER_ROLL_TIMINGS = {
    'CONSTRUCTOR_AFTER_HP': 0,
    'PRE_BATTLE': 1,
}

# Encounter pool (generated EncounterPool) -- which Exordium.generateXxx list an
# encounter belongs to (B3.12). Pinned/append-only. WEAK+STRONG feed the shared
# monsterList; ELITE feeds eliteMonsterList; BOSS is the shuffled bossList.
ENCOUNTER_POOLS = {
    "WEAK": 0,
    "STRONG": 1,
    "ELITE": 2,
    "BOSS": 3,
    # Fixed groups constructed by event bodies. They participate in the
    # game-id composition lookup but never in Exordium's generated lists.
    "EVENT": 4,
}
# Composition-program op (generated CompOp) -- the miscRng spawn-script instruction
# set (B3.12). Pinned/append-only. EMIT: one fixed monster (0 draws). BOOL: one
# randomBoolean() coin (getLouse/getSlaver/Large Slime). PICK: eager-construct every
# choice (a BOOL choice draws its coin during list build) then one random(0,n-1)
# select (bottomGet* helpers). SEQ_BOOL: one randomBoolean() selecting a whole
# sequence (spawnSmallSlimes). POOL: draw-without-replacement (spawnGremlins /
# spawnManySmallSlimes).
COMP_OPS = {"EMIT": 0, "BOOL": 1, "PICK": 2, "SEQ_BOOL": 3, "POOL": 4}

TIER_RE = re.compile(r"^(base|a[1-9][0-9]?)$")


def tier_threshold(key: str) -> int:
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

IDENT_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")

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


def pascal(upper_snake: str) -> str:
    return "".join(part.capitalize() for part in upper_snake.split("_"))


def cpp_string(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
