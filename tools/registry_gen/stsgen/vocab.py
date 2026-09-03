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
    # (Body Slam, BodySlam.java:37). DAMAGE_STR_MULT deals `amount` base with the
    # player's Strength counted x `extra` (Heavy Blade, HeavyBlade.java:47-56).
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
    # B3.10b colorless-uncommon additions. The four opcodes are the whole
    # published 49-52 reservation; USE_CARD remains 53 and the earlier gaps
    # remain gaps.
    "DARK_SHACKLES": 49,
    "DISCOVERY": 50,
    "ENLIGHTENMENT": 51,
    "RANDOM_COLORLESS_TO_HAND": 52,
    # The played card's filing action, allocated at 53 because 49-52 remain
    # the preceding colorless-card opcode block and permanent gaps are never
    # backfilled.
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
    # B3.11 stage A addition (the one new opcode this stage; 55-59 are reserved
    # for later B3.11 stages and stay unissued here). ApotheosisAction.update
    # (:25-34): upgrade every eligible card in hand, drawPile, discardPile,
    # exhaustPile IN THAT ORDER -- canUpgrade() (AbstractCard.java:672-680)
    # gates each one (false for CURSE/STATUS, else !upgraded; Searing Blow
    # overrides canUpgrade to always-true, SearingBlow.java:58-60, so it can be
    # upgraded past 1). The played Apotheosis is in the LIMBO pile throughout
    # and is in none of the four piles scanned.
    "UPGRADE_ALL": 54,
    # B3.11 stage C addition. RANDOM_CARD_TO_DRAW is the Chrysalis /
    # Metamorphosis generator (Chrysalis.use:31-42, Metamorphosis.use:31-42 --
    # the same loop with CardType.SKILL vs ATTACK, which is why the type rides
    # in `extra` rather than in two opcodes). ONE op for the WHOLE loop because
    # the Java's rng ORDER cannot be expressed as N independent steps: use()
    # performs ALL `amount` pool rolls first (returnTrulyRandomCardInCombat(type),
    # AbstractDungeon.java:964-979 -- ONE cardRandomRng random(size-1) each,
    # interleaved only with addToBot, which consumes nothing), and the `amount`
    # MakeTempCardInDrawPileActions then resolve afterwards, each spending ONE
    # cardRandomRng draw in CardGroup.addToRandomSpot (:463-469; an EMPTY draw
    # pile costs zero). Net stream: N rolls, THEN N inserts -- an interleaved
    # roll/insert/roll/insert encoding produces different cards AND different
    # positions. Each generated BASE copy whose cost is > 0 is zeroed
    # PERMANENTLY for the combat (both cost and costForTurn, Chrysalis.java:
    # 35-39) -- the MADNESS model, not the setCostForTurn one; an X-cost (-1)
    # or already-0 card is left alone.
    "RANDOM_CARD_TO_DRAW": 55,
    # B3.11 stage B addition (57-59 stay unissued -- 57 belongs to a later
    # B3.11 stage and 58-59 are that batch's published reserve; stage C did
    # NOT need 58, since RANDOM_COLORLESS_TO_HAND's two new `extra` bits
    # carried Transmutation, see COLORLESS_TO_HAND_FLAGS below).
    # DRAW_PILE_FETCH is Violence / DrawPileToHandAction.update (:31-71): pull
    # `amount` cards of the CardType in `extra` out of the DRAW PILE into the
    # hand. DUAL-STREAM accounting, and the order of the early-outs is what
    # makes it exact: an EMPTY draw pile ends the action at :33-36 BEFORE the
    # temp browse group exists (zero draws of either stream); the group is then
    # built with CardGroup.addToRandomSpot (:463-469), whose empty-group branch
    # is a free plain append and whose every later insert is ONE cardRandomRng
    # draw, so k matches cost k-1; ZERO matches ends it at :42-45 having spent
    # exactly that (nothing). Each of the `amount` iterations then skips with NO
    # rng while the temp list is empty (:47), else spends ONE shuffleRng
    # randomLong seeding a java.util.Random for Collections.shuffle over the
    # temp list (the NO-ARG CardGroup.shuffle, :561-563) and takes its BOTTOM
    # card, which moves draw -> hand or draw -> DISCARD at a full hand (:51-55).
    "DRAW_PILE_FETCH": 56,
    # B3.11 stage D addition (58-59 remain this batch's published reserve and
    # stay UNISSUED). DAMAGE_GREED is Hand of Greed / GreedAction.update
    # (:33-46): `target.damage(info)` through the ORDINARY pipeline -- a plain
    # DamageInfo(p, damage, damageTypeForTurn) built by HandOfGreed.use
    # (HandOfGreed.java:33), so Strength/Vulnerable/Weak apply and block absorbs,
    # unlike the pure-damage matrices Panache and The Bomb queue -- followed by
    # `extra` gold IF AND ONLY IF the hit left the target dead. The gold gate is
    #   !(!isDying && currentHealth > 0 || halfDead || hasPower("Minion"))
    # (:37); BOTH extra terms are LIVE and both are tested at the opcode body --
    # halfDead is kMonsterFlagHalfDead (schema v7) and the Minion term is
    # PowerId::MINION (powers.yaml id 96, S2.23). The gold
    # accrues in CombatState.combat_gold and reaches RunState only at the run
    # layer's combat fold-back, through the single gain_gold door -- so a
    # combat-only replay never touches a run purse. `extra` carries the gold, the
    # same second-operand shape DAMAGE_FEED (35) uses for its max-HP number.
    "DAMAGE_GREED": 57,
    # Wave-C track 1, relic-tail stage: 63-64 out of the 63-66 block that stage
    # owns. 58-59 remain B3.11's published reserve, 60-62 the potions stage's,
    # 65-66 were RELEASED unspent (an opcode gap costs nothing -- the numbering is
    # append-only, never dense). Both were later claimed by name: 65 by the
    # final-integrate fix-forward (RED_SKULL_ENTRY) and 66 by S2.21
    # (VAMPIRE_DAMAGE), so this block is now fully spent.
    #
    # REMOVE_DEBUFFS is RemoveDebuffsAction.update (RemoveDebuffsAction.java:
    # 23-30): `for (p : c.powers) if (p.type == DEBUFF) addToTop(new
    # RemoveSpecificPowerAction(c, c, p.ID));`. It needs its OWN opcode rather
    # than a queued list of REMOVE_POWER items because the enumeration happens
    # when the ACTION RESOLVES, not when it is queued -- REMOVE_POWER names one
    # PowerId chosen at queue time, so a debuff applied between the queue and the
    # resolve would survive the game's version and not this one. The addToTop per
    # power means the removals resolve in REVERSE power-list order. The DEBUFF
    # predicate is the LIVE-INSTANCE type, not the static table alone:
    # StrengthPower/DexterityPower recompute this.type from the sign of amount
    # (StrengthPower.java:81-89, DexterityPower.java:74-82), so a negative
    # Strength stack IS removed and a positive one is not -- the same two-term
    # test op_apply_power already builds at apply time.
    "REMOVE_DEBUFFS": 63,
    # UPGRADE_RANDOM_CARD is UpgradeRandomCardAction.update
    # (UpgradeRandomCardAction.java:28-50): filter the hand to
    # `canUpgrade() && type != STATUS`, and IF that subset is non-empty shuffle it
    # and upgrade element 0. It needs its own opcode because CHOOSE_CARD's
    # RANDOM + UPGRADE path is a DIFFERENT STREAM over a DIFFERENT SET -- one
    # cardRandomRng draw over the whole hand, versus one shuffleRng.randomLong()
    # seeding a JDK Fisher-Yates over the PRE-FILTERED group (the no-arg
    # CardGroup.shuffle, CardGroup.java:561-563). The shuffle sits INSIDE the
    # non-empty test, so an empty hand or a hand with nothing upgradeable costs
    # ZERO shuffleRng -- that is the single most load-bearing stream fact here.
    "UPGRADE_RANDOM_CARD": 64,
    # Wave-C track 1, potions stage: 60 out of the 60-62 block that stage owns.
    # 61-62 are RELEASED unspent.
    #
    # RANDOMIZE_HAND_COST is RandomizeHandCostAction.update
    # (RandomizeHandCostAction.java:26-38): walk p.hand.group in hand-slot order
    # and roll a new PERMANENT cost (`card.costForTurn = card.cost = newCost`)
    # for every card whose BASE cost is non-negative. No operand -- the hand is
    # an execute-time read.
    #
    # It needs its own opcode rather than a queue-time expansion into SET_COST
    # items because SET_COST names one card-pool index and one literal cost, both
    # fixed when the item is QUEUED, whereas here both the membership and every
    # value are decided at RESOLVE. Snecko Oil queues this behind its own
    # DrawCardAction (SneckoOil.java:42-45), so the hand it walks includes the
    # five freshly drawn cards; a queue-time expansion would randomize the
    # PRE-draw hand and spend the wrong number of cardRandomRng draws.
    #
    # STREAM: one cardRandomRng.random(3) (0..3 INCLUSIVE) per hand card with a
    # non-negative base cost, in hand order -- INCLUDING one that rolls its own
    # current cost, because the `||` short-circuits and the assignment sits in
    # its RIGHT operand. X-cost / unplayable cards (card.cost < 0) cost nothing.
    "RANDOMIZE_HAND_COST": 60,
    # final-integrate fix-forward (ledger: "final-integrate allocation"). 65 was
    # released unspent by Wave-C's relic-tail block.
    #
    # RED_SKULL_ENTRY is RedSkull$1 -- the deciding action RedSkull.atBattleStart
    # addToBots (RedSkull.java:38), stripped by the tree's extraction but
    # recovered byte-exact from the shipped desktop-1.0.jar
    # (tools/oracle_bridge/driver/redskull_capture_runbook.md). Its body:
    # `if (!isActive && player.isBloodied) { player.addPower(Strength+3);
    # isActive = true; }` -- BOTH conjuncts read at RESOLVE time, at the bottom
    # of the battle-start drain, after the addToTop heals (Blood Vial,
    # Pantograph) settle. No operand: the responding slot is found by id in the
    # relic mirror at execute time. The grant is the direct
    # AbstractCreature.addPower (:506-527) -- no sort, no ApplyPowerAction
    # interception -- so it is its own opcode, not an APPLY_POWER item.
    "RED_SKULL_ENTRY": 65,
    # S2.21 (Act-2 city normals I). 66 was the last of Wave-C's relic-tail block
    # to be released unspent -- 65 was taken by the final-integrate fix-forward
    # above, and this takes the other one, so that block is now fully spent.
    #
    # VAMPIRE_DAMAGE is VampireDamageAction.update (VampireDamageAction.java:
    # 29-45): the SINGLE-TARGET, MONSTER-SOURCED lifesteal the Shelled Parasite's
    # Life Suck uses (ShelledParasite.java:132). Distinct from VAMPIRE_DAMAGE_ALL
    # (38), which is the player's Reaper: that one hits EVERY live monster and
    # sums their losses into one queued player HEAL. This one hits ONE target and
    # heals the ATTACKER (`this.source`, which setValues takes from info.owner).
    #
    # It cannot be a DAMAGE step plus a HEAL step, which is the whole reason it
    # needs an opcode: the heal amount is `target.lastDamageTaken` -- the HP the
    # hit ACTUALLY removed -- so block, Intangible and a lethal clamp all shrink
    # it, and none of those is known when the move's program is authored.
    #
    # The heal is applied INLINE rather than queued, and that is exact rather
    # than a shortcut: the Java addToTop's the HealAction, so it jumps ahead of
    # everything target.damage() just queued at the front (a Thorns reflect), and
    # an inline write lands in exactly that position. It is also the only form
    # available -- op_heal (39) is player-only, and the monster branch is S2.22's
    # grant -- and it follows RegenerateMonsterPower's precedent of writing a
    # monster's HP directly (AbstractMonster.heal, AbstractMonster.java:384-399:
    # no relic pass, clamp to maxHealth, and nothing at all while isDying).
    # (S2.22 has since ADDED that monster branch to op_heal; the inline write
    # here is still exact, for the addToTop reason above.)
    "VAMPIRE_DAMAGE": 66,
    # S2.22 (Act-2 city normals II). 67 is this task's whole opcode grant
    # (stage-b-tasks.md "S2 Wave-2 allocations") and opens a fresh block, since
    # 66 spent the last of Wave-C's relic tail.
    #
    # BLOCK_RANDOM_MONSTER is GainBlockRandomMonsterAction.update
    # (GainBlockRandomMonsterAction.java:26-42), the Centurion's Protect
    # (Centurion.java:93). It is an OPCODE and not a BLOCK step because BOTH the
    # recipient AND whether any RNG is spent at all are EXECUTE-TIME facts:
    #
    #   validMonsters = every monster record that is NOT the source, does NOT
    #                   telegraph Intent.ESCAPE, and is NOT isDying;
    #   target        = validMonsters.isEmpty()
    #                       ? source                       // ZERO aiRng draws
    #                       : validMonsters.get(aiRng.random(size - 1));
    #   target.addBlock(amount);
    #
    # Three details are load-bearing and none is expressible in a step list.
    # (a) The escape filter reads the TELEGRAPHED INTENT, not isEscaping -- so a
    # thief that has merely ANNOUNCED its exit is skipped while it is still in
    # the fight, and one that has already gone is skipped by the same intent
    # (both thieves re-telegraph ESCAPE, Looter.java:131 / Mugger.java:132).
    # (b) An EMPTY valid list spends NO draw at all, which is exactly the
    # difference between a solo Centurion and one with a live ally, measured on
    # the shared ai_rng stream. (c) The walk visits DEAD records too (the game
    # never removes them) and excludes them only through isDying.
    #
    # `amount` is the block; `src` is the acting monster -- both the exclusion
    # key and the empty-list fallback recipient. No `extra` operand.
    "BLOCK_RANDOM_MONSTER": 67,
    # S2.2F (the shared Act-2/3 monster framework). 68-70 are this task's whole
    # opcode grant (stage-b-tasks.md "S2 Wave-3 allocations"); 71-72 are
    # S2.24's, ISSUED below. All three landed as bodies with no producer --
    # the consumers are the content batches this framework unblocked.
    #
    # AddCardToDeckAction (AddCardToDeckAction.java:83-88): an in-combat write to
    # the MASTER DECK. The Writhing Mass's MEGA_DEBUFF is the only producer in
    # Acts 1-3 (WrithingMass.java:118, a Parasite), and it is the only in-combat
    # master-deck writer in the game's first three acts -- every other obtain is
    # a run-layer event or a relic pickup.
    #
    # It is an OPCODE rather than a step sequence because the combat layer cannot
    # reach RunState: the whole interp/action_queue layer takes CombatState& and
    # advance.hpp's standalone entry point has no RunState at all. So the body
    # accrues into CombatState.pending_obtain and the run layer drains it through
    # the single add_card_to_master_deck door each pump step -- which is also what
    # makes the OMAMORI gate apply for free (ShowCardAndObtainEffect's ctor,
    # :30-45, spends an Omamori counter to block a CURSE, and Parasite is one)
    # rather than being re-implemented at a second site.
    #
    # `extra` carries the CardId. No amount, no target.
    "OBTAIN_CARD": 68,
    # ClearCardQueueAction. Empties the pending card-play queue; the Awakened
    # One's phase transition addToTop's it (AwakenedOne.java:301) so the cards
    # the player queued behind the killing blow never resolve.
    #
    # TRAP, and the reason this is an opcode and not two lines inline: the Java's
    # body also exhausts every queued card that sits in `player.limbo`, and
    # `player.limbo` is NOT this engine's limbo pile. The engine models
    # AbstractPlayer.cardInUse as `limbo` (interp.hpp), i.e. the ONE card
    # currently resolving. A literal port would exhaust the card being played.
    # See the body for the full reading.
    "CLEAR_CARD_QUEUE": 69,
    # GameActionManager.callEndTurnEarlySequence (:379-392) -- a FORCED turn end
    # driven from inside a power, which nothing in the engine could do before:
    # the only producer of the end-turn sentinel was the player's own END_TURN
    # verb. Time Warp's 12th card is the consumer (TimeWarpPower.java:52-70).
    # No operands.
    "END_PLAYER_TURN": 70,
    # S2.24 (City bosses). 71-72 are this task's whole opcode grant
    # (stage-b-tasks.md "S2 Wave-3 allocations": 71 APPLY_STASIS, 72
    # contingency -- SPENT as the return half, reported in the S2.24 Log).
    #
    # APPLY_STASIS is ApplyStasisAction.update (ApplyStasisAction.java:32-80),
    # the Bronze Orb's card theft (BronzeOrb.java:73). Authored in the orb's
    # STASIS move row: like BLOCK_RANDOM_MONSTER its only operand is `src` (the
    # acting monster, which this domain's queue helper stamps), and EVERYTHING
    # else is an execute-time fact -- both piles empty ends the action with
    # ZERO draws and NO power (:34-37); otherwise the DRAW pile is preferred
    # and the DISCARD used only when the draw pile is empty (:39,51); the pick
    # is a RARE -> UNCOMMON -> COMMON -> unfiltered cascade of
    # CardGroup.getRandomCard(cardRandomRng, rarity) (CardGroup.java:526-538),
    # each NON-EMPTY filtered view costing ONE cardRandomRng draw over its
    # Collections.sort()ed (cardID-compare) membership and each EMPTY view
    # costing ZERO; the unfiltered fallback (CardGroup.java:498-500) indexes
    # the pile IN PILE ORDER, unsorted. The stolen instance moves to the limbo
    # pile (:64) and an addToTop'd ApplyPowerAction then applies the Stasis
    # power holding it (:77) -- the pool index rides the APPLY_POWER counter
    # operand, see powers.yaml id 98.
    "APPLY_STASIS": 71,
    # STASIS_RETURN is StasisPower.onDeath (StasisPower.java:38-44): give the
    # stolen card back when the orb dies. Engine-emitted only (the operand is
    # the stolen card's runtime pool index, carried in the power slot's
    # `counter`); the HAND-vs-DISCARD destination is decided at QUEUE time
    # (`player.hand.size() != 10`, :39) and the HAND arm re-checks the cap at
    # RESOLVE time (MakeTempCardInHandAction.update:71-77 spills to discard).
    "STASIS_RETURN": 72,
    # S2.34 (payout relic/card bodies). 73-74 are this task's whole opcode
    # grant (stage-b-tasks.md "S2.34 allocations").
    #
    # RITUAL_DAGGER is RitualDaggerAction.update (RitualDaggerAction.java:
    # 34-58): ordinary `target.damage(info)` -- the DamageInfo is built from
    # this.damage, whose base is the card's PER-INSTANCE `misc` (ctor
    # `baseDamage = this.misc`, misc starts 15, RitualDagger.java:27-29) -- and
    # then, IF the hit left the target dead (the DAMAGE_GREED gate exactly:
    # !(!isDying && currentHealth > 0 || halfDead || hasPower("Minion"))),
    # `misc += increaseAmount` (magicNumber, 3 base / 5 upgraded) on the
    # master-deck card with the same uuid AND every in-battle instance, each
    # followed by `baseDamage = misc`. CARD_CONTEXT: `amount` is the authored
    # increase and queue_effect_step stamps the SOURCE POOL INDEX into `flags`
    # (the DAMAGE_RAMPAGE shape) -- the base damage and the growth target are
    # both that instance's misc, read/written at EXECUTE time. The master-deck
    # half settles at every in-combat command boundary
    # (sync_run_persistent_misc -- the Java's write is mid-action,
    # RitualDaggerAction.java:39-46; the combat layer has no RunState, the
    # combat_gold precedent) keyed on CardDef.initial_misc != 0, with the
    # combat fold-back as the closing sync.
    "RITUAL_DAGGER": 73,
    # CODEX is CodexAction (CodexAction.java:22-64), queued addToBot by
    # Nilry's Codex's onPlayerEndTurn (NilrysCodex.java:29-32). All-monsters-
    # basically-dead is a ZERO-DRAW no-op (:29-32); otherwise ONE 3-distinct-
    # card offer over the no-arg returnTrulyRandomCardInCombat() pool (the RED
    # combat pool -- NOT colorless; :54 calls the no-arg overload) exactly as
    # DISCOVERY generates its offers, opened ALWAYS-SKIPPABLE
    # (customCombatOpen(..., true), :34). Unlike DISCOVERY the generator runs
    # INSIDE the first-tick branch (:33-36), so a close burns NO wasted
    # regenerations; the pick is makeStatEquivalentCopy'd at its REGISTRY cost
    # (no setCostForTurn) and lands in the DRAW PILE at a RANDOM SPOT --
    # ShowCardAndAddToDrawPileEffect(randomSpot=true) -> CardGroup.
    # addToRandomSpot (:463-468), ONE cardRandomRng draw unless the pile is
    # empty. ENGINE_EMITTED: only the relic's native body queues it.
    "CODEX": 74,
    # MONSTER_CHANGE_STATE is ChangeStateAction.update (ChangeStateAction.
    # java:25-31) as a QUEUED action: `monsters[tgt].changeState(amount)`,
    # dispatched by monster_id in the engine (monster_change_state). It exists
    # for the changeState bodies whose own addToBottom children must land
    # behind whatever was queued between the action and its resolution -- the
    # Guardian's Defensive Mode (GainBlock 20 behind MonsterStartTurnAction
    # when Combust's end-of-turn damage drives the flip) is the first consumer.
    # ENGINE_EMITTED: `amount` is a monster module's private state id.
    "MONSTER_CHANGE_STATE": 75,
    # OBTAIN_POTION is ObtainPotionAction's first tick (ObtainPotionAction.java:
    # 29-38): `amount` is the PotionId to put on the belt. The body accrues into
    # CombatState.pending_potion and the run layer drains it onto
    # RunState.potions at the command boundary (drain_pending_potions), where the
    # action's own Sozu gate and obtainPotion's first-empty-slot placement are
    # applied -- the OBTAIN_CARD layer-boundary shape. ENGINE_EMITTED: queued
    # only by Entropic Brew's in-combat branch (EntropicBrew.java:40-42, one
    # addToBot per potionSlots roll), so the obtains wait behind an open
    # discovery screen exactly as the game's queue does.
    "OBTAIN_POTION": 76,
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
                "exhaust_to_hand": 5,
                # Forethought / ForethoughtAction: move each selected hand card
                # to the BOTTOM of the draw pile (moveToBottomOfDeck ->
                # CardGroup.addToBottom == group.add(0, c)), granting the moved
                # card a free next play when its AbstractCard.cost is > 0. Kinds
                # 6-7 are permanent gaps, so this is 8 -- the first kind needing
                # a FOURTH kind bit (CHOICE_KIND_HIGH_BIT2). The BASE card
                # authors it mandatory-1; the UPGRADED card authors it with
                # `optional: true`.
                "put_on_draw_bottom": 8,
                # Secret Technique / Secret Weapon: the source pile is the DRAW
                # PILE, filtered to ONE CardType (SkillFromDeckToHandAction /
                # AttackFromDeckToHandAction differ only in that type, so
                # `card_type:` is REQUIRED on the step and is what separates
                # them). Kinds 6-7 are permanent gaps; kinds are append-only, so
                # 9 steps over them rather than backfilling.
                "draw_to_hand": 9,
                # Liquid Memories / BetterDiscardPileToHandAction's
                # (numberOfCards, newCost) ctor: the source pile is the DISCARD
                # pile and the destination is the HAND, at cost 0 FOR THE TURN.
                # It needs its own kind because the packed encoding has no
                # destination field -- the kind IS the destination -- and nothing
                # else in the enum moves a discard card into the hand. Kind 10 is
                # a permanent gap, so this is 11.
                "discard_to_hand_free": 11,
                # Gambler's Brew AND Gambling Chip -- literally one Java action
                # (GamblingChipAction), one presentation-only boolean apart.
                # Move each selected hand card to the DISCARD pile, then draw
                # exactly as many as were discarded, the count read AT CONFIRM.
                # Authored OPTIONAL with amount 99 (the action's own literal).
                "hand_to_discard_then_draw": 12}
CHOICE_RANDOM_BIT = 1 << 2
CHOICE_KIND_HIGH_BIT = 1 << 3       # ChoiceKind bit 2
# CHOOSE_CARD `copies` (duplicate kind only): bits [4..7] hold copies - 1, so the
# default (1 copy) encodes as 0 and every pre-B3.6 packed extra is byte-identical.
CHOICE_COPIES_SHIFT = 4
# ChoiceKind bit 3, for kinds 8+. Bits 0-1 hold the low kind bits, 2 is RANDOM,
# 3 is kind bit 2, 4-7 are `copies`, 8 is the queue-time guard -- so the next
# free bit is 9. kind = bits[0..1] | bit3 << 2 | bit9 << 3, which leaves every
# kind 0-7 packing byte-identical.
CHOICE_KIND_HIGH_BIT2 = 1 << 9
# CHOOSE_CARD `card_type` (draw_to_hand only): bits [10..12] hold CardType + 1,
# so 0 means "no filter". The +1 is load-bearing -- CardType ATTACK is 0, so a
# raw type would be indistinguishable from an absent key, which is exactly the
# silent-misauthoring hole CONDITIONAL_DRAW's required `card_type:` closes.
CHOICE_TYPE_FILTER_SHIFT = 10
# CHOOSE_CARD `optional: true` (bit 14): the hand-select screen opened with
# anyNumber && canPickZero -- it selects ZERO to `amount` cards and is ended by
# an explicit confirm, never by reaching a count. Bit 13 is the runtime
# draw-source latch and bits 16-19 the runtime selected-card counter, so 14 is
# the free author-visible bit. Purity (ExhaustAction(magic, false, true, true))
# and upgraded Forethought (open(msg, 99, true, true)) are the two authors.
CHOICE_OPTIONAL_BIT = 1 << 14
# Thinking Ahead: an author-only queue-time guard, never read by the
# interpreter's execute path. ThinkingAhead.use (:32-37) queues
# PutOnDeckAction(1, false) only when AbstractDungeon.player.hand.size() > 0 AT
# THE INSTANT use() reads it (:34). AbstractPlayer.useCard (AbstractPlayer.
# java:1358-1384) runs c.use() at :1369 and only removes the played card from
# hand.group at :1374 -- AFTER use() returns -- so for an ORDINARY HAND PLAY
# the played card is STILL counted at that read (hand.size() >= 1 always,
# since it counts itself): the guard is a structural no-op there. It matters
# only when the card is AUTOPLAYED off the draw pile (PlayTopCardAction /
# Havoc), where it was never a hand.group member. card_play.cpp
# queue_effect_step processes a card's steps in that same synchronous order,
# none of them executed yet, so checking CombatState::hand_count when it
# reaches THIS step reproduces the Java read exactly in both cases. Packed
# into bit 8 -- clear of kind[0-1]/RANDOM[2]/kind-hi[3]/copies[4-7] -- and
# authored as `guard: hand_nonempty` on a CHOOSE_CARD step.
CHOICE_QUEUE_GUARD_HAND_NONEMPTY_BIT = 1 << 8
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
# CardRarity (AbstractCard.CardRarity, declaration order BASIC, SPECIAL,
# COMMON, UNCOMMON, RARE, CURSE). S2.24 promotes the rows' documentation-only
# `rarity:` column into a generated card_rarity(CardId) table, because
# ApplyStasisAction's pick cascade filters on the LIVE CardRarity -- where a
# BASIC Strike is NOT a COMMON, a status IS (Wound.java:24 rarity COMMON), and
# a poolable curse is CURSE while Ascender's Bane is SPECIAL
# (AscendersBane.java:24). Values are pinned/append-only like every enum here.
CARD_RARITIES = {"BASIC": 0, "SPECIAL": 1, "COMMON": 2, "UNCOMMON": 3,
                 "RARE": 4, "CURSE": 5}
# DamageType (interp.hpp DamageType): the DAMAGE opcode's `flags` low byte. A card
# DAMAGE step defaults to NORMAL (the full applyPowers pipeline); a status/curse's
# self-inflicted DAMAGE is THORNS (Burn/Decay -- new DamageInfo(player, n, THORNS),
# Burn.java:45 / Decay.java:43), which skips the NORMAL-only power hooks so the
# player's own Strength/Vulnerable do NOT scale the self-damage (but block still
# absorbs it, unlike LOSE_HP). MIRROR of interp.hpp make_damage_flags.
DAMAGE_TYPES = {"NORMAL": 0, "THORNS": 1, "HP_LOSS": 2}
# DAMAGE `flags` bits 8..9 (interp.hpp kDamagePure / kDamageNullSource). Two
# independent step keys, MIRRORS of the C++ constants:
#   pure: true        -- DamageInfo.createDamageMatrix(amount, true) (DamageInfo.
#                        java:126-136): applyPowers never runs, the landed number
#                        is the raw base whatever the DamageType.
#   null_source: true -- the DamageInfo was built with a NULL owner
#                        (DamageAllEnemiesAction(null, ...), ExplosivePotion.
#                        java:52): every body-level `info.owner != null` gate
#                        (onAttacked powers, Boot's call site, Torii, Plated
#                        Armor's wasHPLost) fails.
# Explosive Potion authors both on one NORMAL step; Panache/The Bomb's matrices
# are pure with a REAL owner but are native bodies, not YAML steps.
DAMAGE_PURE_BIT = 1 << 8
DAMAGE_NULL_SOURCE_BIT = 1 << 9
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
    # Engine-only, like `copy`: Mayhem's recovered MayhemPower$1 two-level
    # deferral (roll the target at item execute, re-queue the play at the
    # bottom -- behind the turn's draw). Listed to keep the mirror complete;
    # never authored from YAML (the native body sets it).
    "defer_roll": 1 << 5,
}

# RANDOM_COLORLESS_TO_HAND `extra` bits -- MIRROR of interp.hpp's
# kColorlessToHand* constants (B3.11 stage C). BOTH default to 0, so Jack of All
# Trades' rows (which author neither) keep the `extra = 0` they packed before
# these bits existed -- verified byte-identical rather than assumed.
#   cost_zero_for_turn -- TransmutationAction.java:49 `c.setCostForTurn(0)`:
#       the generated copy costs 0 for THIS TURN ONLY (COST_MODIFIED_FOR_TURN,
#       the RANDOM_ATTACK_TO_HAND / Infernal Blade model). Jack of All Trades
#       does NOT set it (JackOfAllTrades never re-costs its copies).
#   upgraded_copy -- TransmutationAction.java:46-48 `if (this.upgraded)
#       c.upgrade()`: upgraded Transmutation generates UPGRADED copies. The
#       upgrade runs BEFORE setCostForTurn, so the cost the this-turn zero
#       replaces is the UPGRADED row's cost.
COLORLESS_TO_HAND_FLAGS = {
    "cost_zero_for_turn": 1 << 0,
    "upgraded_copy": 1 << 1,
}

# APPLY_POWER `extra` bits 16..31 -- MIRROR of interp.hpp's
# kApplyPowerCounterShift (B3.11 stage D). The applied instance's COUNTER
# OPERAND: the second number a counter-carrying PowerSlot (types.hpp) is
# constructed with, authored as `counter:` on an APPLY_POWER step. Absent -> 0,
# which is exactly what every APPLY_POWER step packed before these bits existed,
# so all of them stay byte-identical (pinned by the kBash assert in cards.hpp).
APPLY_POWER_COUNTER_SHIFT = 16

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
    # S2.21. AbstractPower.onRemove -- fired on the power BEING DESTROYED, from
    # the engine's single removal choke point (remove_slot_at,
    # interp/interp_powers.cpp), which every REMOVE_POWER / REDUCE_POWER-to-zero /
    # REMOVE_DEBUFFS path reaches. NOT a fan-out like every other entry here: the
    # one body that runs is the removed power's own, so power_hooks.cpp routes it
    # straight to that PowerId's native handler instead of walking a power list.
    # Only a `native: true` row may bind it -- a data program would have to queue
    # against a list that is mid-compaction. Flight is the first binder
    # (FlightPower.onRemove, FlightPower.java:75-78 -> the Byrd's GROUNDED).
    "on_power_removed": 14,
    # S2.2F (the shared Act-2/3 monster framework). Three DISPATCH SITES land
    # with the framework; the first BINDERS are content-batch rows, which is why
    # every one of these is unbound today.
    #
    # AbstractCreature.applyTurnPowers -> AbstractPower.duringTurn, fired from
    # GameActionManager.java:322-323 as `m.takeTurn(); m.applyTurnPowers();` --
    # i.e. SYNCHRONOUSLY after the monster's turn body, so whatever it queues
    # lands AFTER everything takeTurn queued, the trailing RollMoveAction
    # included. Only three classes override duringTurn in the whole tree:
    # AbstractPower (empty base), ExplosivePower (the Exploder's countdown and
    # self-destruct) and FadingPower (the Transient's).
    "during_turn": 15,
    # AbstractPower.onAfterUseCard, fired from UseCardAction.UPDATE (:79-88).
    # This is NOT on_use_card (1), which is the CONSTRUCTOR fan-out (:20-45):
    # different trigger point -- after the played card's own actions have been
    # queued rather than before -- and a different participant list, PLAYER
    # POWERS then MONSTER POWERS only, with no relics and no card-level stages.
    # Binding a counter to the wrong one of the two counts cards at the wrong
    # moment and in the wrong order. Binders: Slow (Giant Head) and Time Warp
    # (Time Eater).
    "on_after_use_card": 16,
    # AbstractPower.onInflictDamage, fired from AbstractPlayer.damage
    # (:1449-1453) over the ATTACKER's power list -- `info.owner.powers`, not the
    # victim's, which is what makes it a different hook from was_hp_lost (12).
    # Its position in that method is load-bearing: inside the `damageAmount > 0`
    # block, AFTER the wasHPLost power+relic fan-outs. Binder: Painful Stabs
    # (Book of Stabbing).
    "on_inflict_damage": 17,
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

# Observability transforms (training-plan §2.3): what a relic row does to the
# PLAYER'S INFORMATION rather than to game state. HIDE_INTENT is Runic Dome's
# intent suppression (a rendering guard in the game, AbstractMonster.java:258,
# :749 -- observation.hpp's encode site); REVEAL_DRAW_ORDER is Frozen Eye's
# true-order draw-pile view (FrozenEye.java:12-27 -- knowledge.hpp's reveal
# hooks). Pinned/append-only; the generated ObservabilityTransform enum
# static_asserts these values, and knowledge.hpp reads membership through the
# generated relic_observability() table. NONE (0) is reserved for "row declares
# no transform" and is not a legal YAML spelling -- omit the key instead.
OBSERVABILITY_TRANSFORMS = {
    "HIDE_INTENT": 1,
    "REVEAL_DRAW_ORDER": 2,
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
    "DEFEND_DEBUFF": 15,  # The Time Eater's Ripple (AbstractMonster.Intent.
                          # DEFEND_DEBUFF; TimeEater.java:194,203 -- BOTH arms that
                          # pick move 3 telegraph it). Block 20 on ITSELF plus
                          # Vulnerable/Weak on the PLAYER (and Frail from A19), which
                          # is none of DEFEND (11, pure block), DEBUFF (5, no block)
                          # or DEFEND_BUFF (2, where the buff is the monster's own).
                          # S2.28's whole MonsterIntent grant; 14 remains B3.15's
                          # published reserve and stays unissued.
                          #
                          # IT IS NOT DONU'S. The dispatching brief assigned
                          # DEFEND_DEBUFF to Donu; the tree says otherwise and was
                          # re-read before the value was spent. Donu's Circle of
                          # Protection telegraphs Intent.BUFF (Donu.java:339) and
                          # Deca's Square of Protection telegraphs DEFEND below A19,
                          # DEFEND_BUFF from A19 (Deca.java:507-511) -- three
                          # constants that all already existed.
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
    # S2.2F. A ctor draw that happens BEFORE the setHp draw, because it sits in
    # the `super(...)` ARGUMENT LIST and Java evaluates arguments before the
    # constructor body runs:
    #     super(NAME, ID, AbstractDungeon.monsterHpRng.random(90, 96), ...);
    #     if (asc >= 7) setHp(92, 102); else setHp(90, 96);
    #                                             (OrbWalker.java:53-58)
    # The first draw's VALUE is then thrown away by setHp -- but it MOVED THE
    # STREAM, so every later roll on that floor shifts. Nothing in Act 1 does
    # this, which is why the timing did not exist; the Act-2/3 roster has seven
    # (Orb Walker, Reptomancer, SnakeDagger, BronzeOrb, TorchHead, Taskmaster,
    # ApologySlime).
    #
    # The ORDER is the whole point of the enumerator: a BEFORE roll must burn
    # ahead of the setHp draw, not after it, which is why the engine's
    # burn_unspawned_ctor_rolls is a two-pass walk rather than one filter.
    'CONSTRUCTOR_BEFORE_HP': 2,
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
