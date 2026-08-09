#pragma once

// Effect interpreter -- opcode dispatch + the DAMAGE pipeline (design doc §5.5,
// §6). The pump (action_queue.hpp) decides *which* queued ActionQueueItem
// resolves next; this layer decides *what an item does*. pump_step() calls
// execute_opcode() on every action_queue / pre_turn item it pops (the wiring
// lives in action_queue.cpp).
//
// Provenance (read from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * DamageInfo.applyPowers (DamageInfo.java:35-100) -- the exact hook order
//     for both ownership branches, float accumulation, single floor at the end
//     via MathUtils.floor, clamp >= 0.
//   * MathUtils.floor (MathUtils.java:217) -- (int)((double)v + 16384.0) - 16384,
//     replicated bit-for-bit as mathutils_floor() below (a true floor for the
//     skeleton's |value| < 16384 range; NOT round).
//   * StrengthPower.atDamageGive (StrengthPower.java:92-98) -- damage + amount
//     (float add), NORMAL only.
//   * VulnerablePower.atDamageReceive (VulnerablePower.java:61-73) -- damage *
//     1.5f, NORMAL only. The Paper Phrog 1.75f relic branch (:67-69, a
//     Vulnerable MONSTER hit while the player owns Paper Frog) is LIVE: Paper
//     Phrog is a registered UNCOMMON relic, so this is no longer the
//     unreachable branch the relic-free skeleton documented. The Odd Mushroom
//     1.25f branch (:64-66, player-side) IS still unreachable -- Odd Mushroom
//     is RARE-tier and registry/relics.yaml has no RARE rows yet.
//   * WeakPower.atDamageGive (WeakPower.java:61-70) -- damage * 0.75f, NORMAL
//     only (the Paper Crane 0.6f branch stays unreachable: Paper Crane is
//     addGreen/Silent-only, never Ironclad-obtainable in S1).
//   * AbstractCreature.addBlock -- in the base game only relic
//     (onPlayerGainedBlock) and power (onGainedBlock) hooks touch block gain;
//     Strength/Vulnerable/Weak do NOT. The skeleton has neither, so BLOCK is a
//     straight `block += amount` (hook site marked for later extension).
//
// -------------------------------------------------------------------------
// SCOPE-BOUNDARY NOTES:
//
// (1) SHUFFLE_IN's body -- the discard-pile-into-draw reshuffle via shuffle_rng
//     + the JDK LCG -- lives in piles.cpp (shuffle_discard_into_draw); the
//     dispatch delegates there.
//
// (2) ROLL_MOVE dispatches to the target monster's queued-roll function
//     (monster_dispatch.hpp roll_monster_move) and is a no-op for monsters
//     without one. Most natives (Jaw Worm, Cultist, louses, small/medium
//     slimes) roll DIRECTLY inside their MonsterTurnFn -- their queues never
//     carry ROLL_MOVE items. The LARGE slimes queue real ROLL_MOVE items
//     because the game's
//     RollMoveAction resolves AFTER any mid-turn damage interrupt queues its
//     SetMoveAction (and even on the already-dead split parent --
//     RollMoveAction.update has no liveness check, RollMoveAction.java:17-21),
//     so the aiRng draw order is only reproducible by rolling at dequeue time.
//
// (3) Opcode NUMBERING: NOP == 0 is reserved as a safe no-op. This is required,
//     not cosmetic: value-initialized ActionQueueItems (opcode == 0) are pushed
//     through pump_step, and any unrecognized/zero opcode must drain harmlessly
//     rather than mis-fire a real effect. The real opcodes are therefore 1..8
//     (design doc §6's set, in its listed order); action_queue.hpp's
//     kOpcodeDrawCard mirrors Opcode::DRAW and is static_assert'd equal in
//     action_queue.cpp. Design doc §6 names the opcode SET but assigns no
//     numeric values, so the numbering is an implementation choice, not a doc
//     conflict. MAKE_CARD == 9 and SET_COST == 10 were the first appends under
//     the append-from-9 rule; gen.py's OPCODES and the cards.hpp drift pin
//     cover them.
//
// (4) DYNAMIC TARGETS. An item's `tgt` may be a sentinel
//     kActorAllEnemies / kActorRandomEnemy (action_queue.hpp). execute_opcode
//     resolves them at EXECUTE time -- fanning the op out over the currently-
//     live monsters (AoE, one DamageInfo per target) or rolling a single
//     card_random_rng draw for the random case (one draw per queued hit). A
//     concrete monster slot or kActorPlayer dispatches straight to the op body.
//
// FIELD-ENCODING CONVENTIONS:
//   * Actor identity: src/tgt are actor indices -- monster slots 0..4, and the
//     player is kActorPlayer (== 0xFF, reused from action_queue.hpp). DAMAGE
//     reads src (attacker) to pick the ownership branch and iterate the
//     attacker's powers, and tgt (defender) for the receive hooks and to land
//     the damage.
//   * APPLY_POWER: ActionQueueItem has no dedicated power-id field, so `flags`
//     carries the PowerId (low 16 bits; make_apply_power_flags/
//     apply_power_id_from_flags below) and `amount` carries the stack amount.
//     `tgt` is the recipient actor.
//   * BLOCK: `tgt` is the recipient actor, `amount` the block gained.
//   * GAIN_ENERGY: always the player; `amount` is added to player_energy (no
//     max-energy field in CombatState -- Ironclad's base 3 is a constant, and
//     the skeleton has no relic/potion that stores a raised max, so no clamp).
//     SPENDING energy is the SAME opcode with a negative `amount` (player_energy
//     += negative) -- no separate opcode/field is needed, so the card-play
//     cost deduction reuses GAIN_ENERGY directly.
//   * DRAW: `amount` is the number of cards to draw (the start-of-turn
//     DrawCardAction(gameHandSize) carries 5). Player only. draw_cards
//     (piles.cpp) applies the up-front hand-size cap and empty-pile reshuffle.
//   * EXHAUST: `amount` carries the card-POOL index to exhaust (chosen over src
//     because src/tgt are actor lanes; a pool index 0..159 fits i32 trivially).
//     The card is located in `hand` and moved to the `exhaust` pile.

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // kActorPlayer (shared player-actor sentinel)
#include "sts/engine/combat_state.hpp"  // CombatState, ActionQueueItem, PowerSlot
#include "sts/engine/types.hpp"         // PowerId

namespace sts::engine {

// --- Opcode set (design doc §6) ---------------------------------------------
//
// NOP == 0 is reserved (see decision (3) above): unrecognized or value-init'd
// items dispatch here and do nothing. Real opcodes follow §6's listed order.
enum class Opcode : uint16_t {
    NOP = 0,          // reserved safe no-op (value-init / unrecognized opcode)
    DAMAGE = 1,       // src attacks tgt for `amount` base (full DamageInfo pipeline)
    BLOCK = 2,        // tgt gains `amount` block
    APPLY_POWER = 3,  // tgt gains PowerId(flags) x `amount` stacks
    DRAW = 4,         // player draws `amount` cards
    GAIN_ENERGY = 5,  // player_energy += `amount`
    SHUFFLE_IN = 6,   // reshuffle discard -> draw (implemented in piles.cpp)
    EXHAUST = 7,      // move card-pool-index `amount` from hand to exhaust
    ROLL_MOVE = 8,    // no-op: Jaw Worm rolls its own next move in its MonsterTurnFn
    // --- Card creation + cost modification (append-only from 9, design §4.4) ---
    MAKE_CARD = 9,    // create `amount` copies of CardId(flags low16) into pile(src)
    SET_COST = 10,    // set card_pool[src].cost_now = `amount` (temporary cost modifier)
    // --- Direct HP loss (append-only) ---
    LOSE_HP = 11,     // tgt loses `amount` HP directly (bypasses block; HP_LOSS
                       // type). Fires wasHPLost with source == tgt (self). The
                       // firing site for the Rupture attribution (power_hooks).
    // --- Hand selection, play-from-draw, power removal (append-only from 12) ---
    CHOOSE_CARD = 12, // player-selected (or random / forced) hand-card manipulation:
                       // exhaust / put-on-draw-top / upgrade. `amount` = number of
                       // cards to select; `flags` = the ChoiceKind + RANDOM bit
                       // (make_choose_flags below). This is the CHOOSE-in-combat
                       // "hand card select screen" verb -- the pump BLOCKS on it
                       // (phase WAITING_ON_USER) when a real choice is needed
                       // (choice_requires_user), and advance(CHOOSE, hand_slot)
                       // resolves it. When the selection is FORCED (eligible <=
                       // amount) or RANDOM it auto-resolves at execute time (no
                       // block), matching ExhaustAction / PutOnDeckAction /
                       // ArmamentsAction's "no screen" branches. An OPTIONAL
                       // item (kChoiceOptionalBit) instead selects ZERO to
                       // `amount`: advance(CHOOSE, slot) TOGGLES a card in or
                       // out of the pending selection and advance(CONFIRM)
                       // resolves it, empty selection included.
    PLAY_TOP_DRAW = 13, // play the top card of the draw pile, then exhaust it
                         // (Havoc / PlayTopCardAction.update). Rolls one
                         // card_random_rng draw for the played card's random-monster
                         // target (getRandomMonster), reshuffles an empty draw pile,
                         // and plays the card free (cost 0) with exhaust-on-use.
    REMOVE_POWER = 14, // remove PowerId(flags low16) from `tgt`'s power list
                        // (RemoveSpecificPowerAction). Used by LoseStrengthPower's
                        // end-of-turn self-removal (Flex).
    // --- Dynamic-base attack damage (append-only from 15) ------------------
    DAMAGE_BLOCK = 15,  // src (player) attacks tgt for base == player_block, read
                         // at EXECUTE time, then the normal DamageInfo pipeline
                         // (Body Slam; BodySlam.java:37 baseDamage = p.currentBlock).
    DAMAGE_STR_MULT = 16, // src attacks tgt for `amount` base with Strength counted
                           // x `flags` (the multiplier), then the pipeline (Heavy
                           // Blade; HeavyBlade.java:47-56 strength.amount *= magic).
    DAMAGE_PER_STRIKE = 17, // deal `amount` + `extra` per STRIKE-tagged card in
                             // hand+draw+discard (PerfectedStrike.java:37-52).
                             // BAKED into plain DAMAGE at QUEUE time
                             // (card_play.cpp): a hand play counts itself because
                             // calculateCardDamage precedes hand.removeCard
                             // (AbstractPlayer.java:1361,1373); an autoplay
                             // already in limbo does not. This opcode therefore
                             // never reaches execute_opcode (a safe no-op if it
                             // somehow does); it exists only as table encoding.
    // --- End-of-turn hand/power sweeps (append-only from 18) ---
    LOSE_HP_PER_HAND = 18,  // `tgt` loses HP == a hand size LOCKED AT TRIGGER
                             // TIME, bypassing block (HP_LOSS type). Regret's
                             // end-of-turn self-loss (Regret.java:35-39:
                             // magicNumber = baseMagicNumber = player.hand.size()
                             // inside triggerOnEndOfTurnForPlayingCard, read back by
                             // the LoseHPAction the ensuing play queues). The registry
                             // authors no amount; dispatch_card_end_of_turn STAMPS
                             // `amount` with the hand size it saw before any curse
                             // left the hand. An execute-time read would be short:
                             // the end-of-turn autoplay pulls every triggering card
                             // (Regret included) out of the hand first.
    DISCARD_HAND = 19,       // end-of-turn DiscardAtEndOfTurnAction collapse:
                             // exhaust ETHEREAL hand cards, then move the remaining
                             // non-RETAIN hand cards to discard. This is queued after
                             // all end-of-turn card/power effects, never invoked by a
                             // registry card program.
    REDUCE_POWER = 20,       // ReducePowerAction: subtract `amount` from
                             // PowerId(flags low16) on `tgt`, removing at zero.
                             // Frail queues this at end of round so power-list
                             // iteration completes before the slot can disappear.
    // --- Queue-time dynamic damage + hand exhaust (append-only from 21) ---
    DROPKICK = 21,            // DropkickAction: if the target has Vulnerable when
                              // this action executes, damage, then gain 1 energy
                              // and draw 1; otherwise damage only.
    DAMAGE_UPGRADE_SCALE = 22,// queue-time dynamic base: amount + sum(extra+i,
                              // i=0..upgrade-1). Searing Blow uses amount=12,
                              // extra=4 and the CardInstance upgrade count.
    DAMAGE_RAMPAGE = 23,      // queue-time dynamic base: amount + source.misc,
                              // then source.misc += extra (Rampage 8,+5 / +8).
    EXHAUST_NON_ATTACKS = 24, // exhaust every non-Attack remaining in hand, from
                              // top to bottom (Sever Soul).
    // --- Monster split framework (append-only from 25) ---
    CANNOT_LOSE = 25,         // set kCombatFlagCannotLose: combat may not end on
                              // all-monsters-dead while the split sequence is in
                              // flight (CannotLoseAction.java:12-15; the pump's
                              // victory gate mirrors AbstractMonster.
                              // updateDeathAnimation's !cannotLose check, :869).
    CAN_LOSE = 26,            // clear kCombatFlagCannotLose (CanLoseAction.java:12-15).
    SUICIDE = 27,             // tgt monster: hp = 0, dying; flags bit0 selects
                              // relic triggers (SuicideAction.java:29-36 --
                              // splits pass triggerRelics=false, so bit0 clear;
                              // gold=0 has no CombatState field). Block is NOT
                              // cleared (SuicideAction bypasses damage()).
    SPAWN_MONSTER = 28,       // insert a monster record at slot `tgt` (records at
                              // >= tgt shift up one; monster_queue indices are
                              // remapped) with hp = max_hp = `amount` and
                              // MonsterId in flags low16, then run the spawned
                              // monster's init() rollMove on ai_rng
                              // (SpawnMonsterAction.java:42-73; the 4-arg medium
                              // slime ctor takes newHealth directly -- NO
                              // monster_hp_rng draw, AcidSlime_M.java:65-66 /
                              // AbstractMonster.java:139,150).
                              //
                              // `flags` bit 16 (kSpawnRunPreBattle) additionally
                              // runs the spawned monster's usePreBattleAction.
                              // It is a BIT and not the default because the two
                              // Java spawn actions genuinely differ:
                              // SpawnMonsterAction does NOT call it, while
                              // SummonGremlinAction.update DOES, at isDone --
                              // which is how a summoned Gremlin Warrior gets its
                              // Angry power and a Bronze Orb does not.
    SET_MOVE = 29,            // set monsters[tgt]'s decided move to `amount` with
                              // intent flags low8 -- pushes move history exactly
                              // like a direct setMove (SetMoveAction.java:52-56 ->
                              // AbstractMonster.setMove:431-437). No liveness
                              // check, matching the Java.
    // --- Block manipulation + in-combat card generation (append-only from 30) -
    DOUBLE_BLOCK = 30,        // Entrench / DoubleYourBlockAction.update (:24-30):
                              // if tgt has block, addBlock(currentBlock) -- a
                              // direct addBlock (kBlockNoPowers path: no card
                              // applyPowers, so no Dexterity/Frail), firing
                              // onGainedBlock; zero block does nothing.
    BLOCK_PER_NON_ATTACK = 31,// Second Wind / BlockPerNonAttackAction.update
                              // (:29-42): exhaust every non-Attack in hand (the
                              // queued addToTop actions resolve in REVERSE hand
                              // order -- last hand card first), then queue one
                              // card-style BLOCK(`amount`) per exhausted card
                              // (this.block was applyPowers block, so Dexterity/
                              // Frail apply per gain -- flags 0, NOT NoPowers).
    SPOT_WEAKNESS = 32,       // SpotWeaknessAction.update (:32-40): if tgt's
                              // telegraphed move deals attack damage
                              // (getIntentBaseDmg() >= 0 <=> the stored intent
                              // classification is an ATTACK* variant -- setMove
                              // stores baseDamage -1 for non-attacks,
                              // AbstractMonster.java:451-463), addToBot
                              // APPLY_POWER Strength `amount` on the player.
    RANDOM_ATTACK_TO_HAND = 33, // Infernal Blade / returnTrulyRandomCardInCombat
                              // (ATTACK) (AbstractDungeon.java:964-979): ONE
                              // card_random_rng random(poolSize-1) draw over the
                              // generated kIroncladAttackPool, then a base copy
                              // with cost_now 0 for THIS turn only
                              // (COST_MODIFIED_FOR_TURN) into the hand
                              // (MakeTempCardInHandAction hand-cap spill).
    // --- Replay, conditional damage, heal (append-only from 34) ---
    PLAY_CARD = 34,           // the GENERAL recursive-play verb: queue a card
                              // instance to be auto-played, free of energy cost.
                              // `flags` (kPlayCard* below) select the source (a
                              // card-pool index in `amount`, or the top of the
                              // draw pile), whether a stat-equivalent COPY is
                              // played instead of the instance itself, and the
                              // played instance's disposition (purge / forced
                              // exhaust). `tgt` is the monster it is played at,
                              // or kActorRandomEnemy to roll one card_random_rng
                              // target at execute time. PLAY_TOP_DRAW (13) is the
                              // older Havoc-specific form and is left as it is
                              // (opcodes are append-only).
    DAMAGE_FEED = 35,         // FeedAction.update (:34-47): damage tgt through the
                              // full pipeline; if the hit LEFT the target dead,
                              // increaseMaxHp(`amount`, false) on the player --
                              // max HP += amount, then a heal of the same amount
                              // through the relic heal seam
                              // (AbstractCreature.increaseMaxHp:199-208).
    FIEND_FIRE = 36,          // FiendFireAction.update (:32-46): read the hand size
                              // n at EXECUTE, addToTop n DAMAGE(`amount`) items,
                              // then addToTop n random single-card hand exhausts --
                              // so the exhausts resolve first and the damage after.
    DOUBLE_STRENGTH = 37,     // LimitBreakAction.update (:27-32): if the player
                              // HAS a Strength power, addToTop APPLY_POWER
                              // Strength == its current amount (a doubling; a
                              // NEGATIVE Strength doubles too).
    VAMPIRE_DAMAGE_ALL = 38,  // VampireDamageAllEnemiesAction.update (:53-77):
                              // damage every live monster in slot order for
                              // `amount` base, sum the HP each ACTUALLY lost
                              // (lastDamageTaken, AbstractMonster.java:669), and
                              // addToBot a HEAL of that sum when it is positive.
    HEAL = 39,                // HealAction.update (:30-34): tgt (the player) heals
                              // `amount`, routed through heal_player_with_relics
                              // so the onPlayerHeal relic pass (Magic Flower)
                              // cannot be skipped. Queued rather than applied
                              // inline wherever the Java queues a HealAction.
    ESCAPE = 40,              // EscapeAction.update (EscapeAction.java:21-28) ->
                              // AbstractMonster.escape (:915-919): the tgt
                              // monster leaves the fight ALIVE -- set
                              // kMonsterFlagEscaped, touch nothing else. The
                              // Java's isEscaping + the animation's `escaped`
                              // latch (updateEscapeAnimation:894-906) collapse
                              // into the one bit; the pump's liveness predicate
                              // (monster_basically_dead) then ends the battle
                              // when nobody is left in the fight, exactly as
                              // :902-904's areMonstersDead-and-!cannotLose check
                              // does. Emitted natively (monster_looter.cpp);
                              // never authored in YAML. 41-42 are this batch's
                              // published reserve and stay unissued.
    // --- Colorless-uncommon verbs (append-only from 45; 41-44 were allocated
    // --- to other batches that did not spend them and stay permanent gaps) ---
    DAMAGE_DRAW_PILE = 45,    // src (player) attacks tgt for base == the DRAW
                              // PILE SIZE, read at EXECUTE time, then the normal
                              // DamageInfo pipeline (Mind Blast;
                              // MindBlast.applyPowers:47-52 sets baseDamage =
                              // player.drawPile.size() before super.applyPowers).
                              // Same execute-time-read model as DAMAGE_BLOCK: the
                              // card queues exactly one damage action and nothing
                              // between queue and execute touches the draw pile.
    CONDITIONAL_DRAW = 46,    // ConditionalDrawAction.update (:29-45): if NO card
                              // in hand has the CardType carried in `flags`,
                              // addToTop a DrawCardAction(`amount`); otherwise do
                              // nothing. The queued DRAW is what runs, so the No
                              // Draw gate and the per-card onCardDraw fan-out are
                              // reached identically (Impatience, restricted type
                              // ATTACK -- Impatience.java:32).
    RESHUFFLE_ALL = 47,       // Deep Breath's FUSED double shuffle. DeepBreath.use
                              // (:34-38) guards BOTH EmptyDeckShuffleAction AND
                              // ShuffleAction(drawPile, false) on the SAME
                              // `discardPile.size() > 0` test, so it draws TWO
                              // shuffleRng.randomLong()s or none: shuffle the
                              // discard and move it onto the draw pile
                              // (EmptyDeckShuffleAction.update:42-64 -- this also
                              // fires relics' onShuffle from its ctor:37-39), then
                              // shuffle the WHOLE draw pile (CardGroup.shuffle():
                              // 561-563; triggerRelics false, so no second
                              // onShuffle). One opcode because the guard's input
                              // is destroyed by the first half.
    MADNESS = 48,             // MadnessAction.update (:26-65): scan the hand for
                              // an eligible card; if one exists, REJECTION-SAMPLE
                              // the hand with cardRandomRng (one
                              // getRandomCard(cardRandomRng) draw per attempt,
                              // CardGroup.java:498-500) until an eligible card is
                              // hit, then set BOTH its cost and its costForTurn to
                              // 0 -- a permanent, not this-turn, zeroing. With no
                              // eligible card it draws NOTHING.
    DARK_SHACKLES = 49,       // DarkShackles.use (:32-38): queue Strength(-amount)
                              // and, only when Artifact was absent BEFORE that
                              // debuff resolves, queue Shackled(+amount).
    DISCOVERY = 50,           // DiscoveryAction: rejection-sample three distinct
                              // non-healing RED combat-pool cards, persist the
                              // generated offer in this queue item, then block
                              // for a GENERATED-source CHOOSE. The selected base
                              // copy costs 0 this turn and enters hand/discard.
    ENLIGHTENMENT = 51,       // EnlightenmentAction: cap hand costForTurn at 1;
                              // nonzero amount also makes base costs >1 equal 1
                              // for the rest of combat (the upgraded card).
    RANDOM_COLORLESS_TO_HAND = 52,  // Jack of All Trades: `amount` independent
                              // cardRandomRng draws over the generated non-
                              // healing colorless combat pool; base copies enter
                              // hand/discard and duplicates are allowed.
                              // `flags` (kColorlessToHand* below, both default
                              // 0) additionally re-cost the copy for the turn
                              // and/or generate it upgraded -- Transmutation's
                              // per-X-repetition body.
    // --- The played card's filing action (allocated at 53) -------------------
    // 49-52 are the preceding colorless-card block; the lower permanent gaps
    // 41-44 are never backfilled.
    USE_CARD = 53,            // UseCardAction.update (UseCardAction.java:77-137)
                              // as a queued action. AbstractPlayer.useCard
                              // (:1369-1375) queues the card's own actions FIRST
                              // and the UseCardAction LAST, so from
                              // hand.removeCard (:1373) until this item resolves
                              // the played card is cardInUse -- the CombatState
                              // `limbo` pile here -- and in NO observable pile.
                              // Executing it rolls Strange Spoon (:109-113, ONE
                              // cardRandomRng boolean, only for an exhausting
                              // non-POWER play), then files the card out of
                              // limbo: purge poof (:89-94) and POWER empower
                              // (:95-108) leave it in no pile; otherwise exhaust
                              // (:129-131, moveToExhaustPile -> onExhaust +
                              // resetAttributes) or discard (:127). `amount` is
                              // the card-pool index. Never authored in YAML
                              // (ENGINE_EMITTED_OPS); emitted only by
                              // resolve_card_play. onAfterUseCard (:79-88) has
                              // no S1 binder (BeatOfDeath / Rebound / Slow /
                              // TimeMaze / TimeWarp are all out of scope), so
                              // its seam is a named comment in op_use_card.
    // --- Colorless-rare addition (append-only from 54) ------------------------
    UPGRADE_ALL = 54,         // Apotheosis / ApotheosisAction.update (:25-34):
                              // upgrade every eligible card in hand, drawPile,
                              // discardPile, exhaustPile IN THAT ORDER.
                              // canUpgrade() (AbstractCard.java:672-680) gates
                              // each one: false for CURSE/STATUS, else
                              // !upgraded; Searing Blow overrides canUpgrade to
                              // always-true (SearingBlow.java:58-60), so its
                              // upgrade COUNT keeps climbing past 1. The played
                              // Apotheosis sits in the LIMBO pile throughout and
                              // is in none of the four piles scanned.
    RANDOM_CARD_TO_DRAW = 55, // Chrysalis (SKILL) / Metamorphosis (ATTACK) --
                              // Chrysalis.use:31-42 and Metamorphosis.use:31-42
                              // are the SAME loop with one CardType changed, so
                              // the type rides in `flags` (make_random_card_to_
                              // draw_flags) and `amount` is the loop count (3
                              // base / 5 upgraded).
                              // THE WHOLE LOOP IS ONE OPCODE because the rng
                              // ORDER cannot be expressed as N independent
                              // steps. use() performs ALL `amount` pool rolls
                              // FIRST -- returnTrulyRandomCardInCombat(type)
                              // (AbstractDungeon.java:964-979), ONE
                              // cardRandomRng random(size-1) each over the
                              // type-filtered RED combat pool, interleaved only
                              // with addToBot, which consumes nothing -- and the
                              // `amount` queued MakeTempCardInDrawPileActions
                              // resolve AFTERWARDS, each spending ONE
                              // cardRandomRng draw in CardGroup.addToRandomSpot
                              // (:463-469; the EMPTY-pile branch is a free plain
                              // append). Net stream: N rolls, THEN N inserts. An
                              // encoding that interleaved roll/insert would pick
                              // different cards AND different positions.
                              // Nothing can interleave between the two halves in
                              // the Java either: the N MakeTempCardInDrawPile-
                              // Actions are consecutive queue entries that queue
                              // nothing themselves, and the played card's
                              // USE_CARD (with its Strange Spoon roll) sits
                              // BEHIND all of them (AbstractPlayer.useCard:1370).
                              // Each generated copy is a BASE library copy whose
                              // cost, when > 0, is zeroed PERMANENTLY for the
                              // combat -- Chrysalis.java:35-39 writes BOTH
                              // card.cost and card.costForTurn, the MADNESS
                              // (opcode 48) model, NOT setCostForTurn's
                              // this-turn one. X-cost (-1) and already-0 cards
                              // are untouched, exactly as the `cost > 0` guard
                              // says. makeStatEquivalentCopy carries cost and
                              // costForTurn through both copy hops
                              // (AbstractCard.java:838-840), so the zero
                              // survives into the draw pile.
    DRAW_PILE_FETCH = 56,     // Violence / DrawPileToHandAction.update (:31-71):
                              // pull up to `amount` cards of the CardType carried
                              // in `flags` out of the DRAW PILE into the hand.
                              // An EMPTY draw pile early-outs at :33-36 BEFORE
                              // the temp list is built, so it costs ZERO rng.
                              // Otherwise a temp CardGroup is filled with every
                              // matching draw-pile card via
                              // CardGroup.addToRandomSpot (:463-469) -- k matches
                              // cost k-1 card_random_rng draws, the first insert
                              // into an EMPTY group being free -- and a zero-match
                              // list then early-outs at :42-45 having spent
                              // exactly those (zero) draws. Each of the `amount`
                              // iterations then: skips silently while the temp
                              // list is empty (:47, NO rng at all), else spends
                              // ONE shuffle_rng randomLong seeding a fresh
                              // java.util.Random for Collections.shuffle over the
                              // temp list (the NO-ARG CardGroup.shuffle(),
                              // :561-563), pops its BOTTOM card (:49-50) and moves
                              // that card draw -> hand, or draw -> DISCARD when
                              // the hand is already full (:51-55). The REAL draw
                              // pile is never reordered by the browse; only the
                              // taken cards leave it. 58-59 remain this batch's
                              // published reserve and stay UNISSUED (see 57
                              // below); 58 was available to the generated-card
                              // family and was NOT spent, because
                              // RANDOM_COLORLESS_TO_HAND's two new default-0 flag
                              // bits carried Transmutation cleanly.
    DAMAGE_GREED = 57,        // Hand of Greed / GreedAction.update (:33-46): the
                              // ORDINARY damage pipeline (this is a plain
                              // `target.damage(info)` at :36, so Strength /
                              // Vulnerable / Weak all apply and block absorbs --
                              // NOT a pure-damage matrix), and then, ONLY if the
                              // hit left the target dead, `amount` gold. `flags`
                              // carries the gold (HandOfGreed's magicNumber ->
                              // GreedAction.increaseGold, :24-27), the same
                              // second-operand shape DAMAGE_FEED (35) uses for
                              // its max-HP number. The gold lands in
                              // CombatState.combat_gold and reaches RunState only
                              // at the run layer's combat fold-back, through the
                              // single gain_gold door.
                              //
                              // The Java gate is
                              //   !(!isDying && currentHealth > 0 || halfDead ||
                              //     hasPower("Minion"))
                              // (:37). BOTH extra terms are LIVE: halfDead is
                              // kMonsterFlagHalfDead, and the Minion term is
                              // PowerId::MINION (powers.yaml id 96, S2.23) --
                              // killing a Gremlin Leader's minion pays no gold.
                              // See op_damage_greed.
    // Wave-C track 1, relic-tail stage. 63-64 out of the 63-66 block that stage
    // owns; 65-66 are RELEASED unspent, and 60-62 stay the potions stage's.
    REMOVE_DEBUFFS = 63,      // Orange Pellets / RemoveDebuffsAction.update
                              // (RemoveDebuffsAction.java:23-30): `for (p :
                              // c.powers) if (p.type == DEBUFF) addToTop(new
                              // RemoveSpecificPowerAction(c, c, p.ID));`.
                              // `tgt` is the creature; no other operand.
                              //
                              // WHY IT CANNOT BE A LIST OF REMOVE_POWER ITEMS:
                              // the enumeration happens when the ACTION
                              // RESOLVES. REMOVE_POWER names one PowerId chosen
                              // at QUEUE time, so a debuff that lands between
                              // the queue and the resolve survives the game's
                              // version and would survive a queue-time
                              // expansion too -- reachable, since Orange
                              // Pellets queues addToBot behind the played
                              // card's own actions.
                              //
                              // The addToTop per power means the removals
                              // resolve in REVERSE power-list order. Only
                              // observable if a removal's onRemove is read by a
                              // later one -- none is today -- but it is free,
                              // so it is reproduced rather than approximated.
                              //
                              // The DEBUFF predicate is the LIVE-INSTANCE type,
                              // not PowerDef::type alone: StrengthPower and
                              // DexterityPower recompute this.type from the
                              // sign of amount (StrengthPower.java:81-89,
                              // DexterityPower.java:74-82), so a NEGATIVE
                              // Strength stack IS removed and a positive one is
                              // not. That is the same two-term test
                              // op_apply_power builds at apply time
                              // (interp_powers.cpp negative_stat_flip).
    UPGRADE_RANDOM_CARD = 64, // Warped Tongs / UpgradeRandomCardAction.update
                              // (UpgradeRandomCardAction.java:28-50): filter the
                              // hand to `canUpgrade() && type != STATUS`, and IF
                              // that subset is non-empty shuffle it and upgrade
                              // element 0. No operand at all.
                              //
                              // WHY IT IS NOT CHOOSE_CARD{RANDOM, UPGRADE}: a
                              // DIFFERENT STREAM over a DIFFERENT SET. That path
                              // draws card_random_rng once over the WHOLE hand;
                              // this spends one shuffle_rng.random_long()
                              // seeding a JDK Fisher-Yates over the PRE-FILTERED
                              // group (the no-arg CardGroup.shuffle,
                              // CardGroup.java:561-563) and takes index 0.
                              // Reusing the other path would silently move
                              // shuffleRng.
                              //
                              // THE STREAM FACT THAT MATTERS: the shuffle sits
                              // INSIDE `if (upgradeable.size() > 0)` (:40-45),
                              // and an empty hand returns even earlier (:31-34),
                              // so a hand with nothing upgradeable costs ZERO
                              // shuffle_rng draws.
    // Wave-C track 1, potions stage. 60 out of the 60-62 block that stage owns;
    // 61-62 are RELEASED unspent (an opcode gap costs nothing -- the numbering
    // is append-only, never dense).
    RANDOMIZE_HAND_COST = 60, // Snecko Oil / RandomizeHandCostAction.update
                              // (RandomizeHandCostAction.java:26-38): walk
                              // p.hand.group in HAND-SLOT ORDER and roll a new
                              // PERMANENT cost for every card whose BASE cost is
                              // non-negative. No operand at all -- the hand is an
                              // execute-time read -- so it is GENERAL_OPS and any
                              // domain's queue helper carries it identically.
                              //
                              //   for (AbstractCard card : this.p.hand.group) {
                              //       int newCost;
                              //       if (card.cost < 0 ||
                              //           card.cost == (newCost =
                              //             cardRandomRng.random(3))) continue;
                              //       card.costForTurn = card.cost = newCost;
                              //       card.isCostModified = true;
                              //   }
                              //
                              // WHY IT IS NOT A LOOP OF SET_COST ITEMS: SET_COST
                              // names one card-pool index and one literal cost,
                              // both chosen at QUEUE time. Here the membership
                              // (the hand) and every value (a draw each) are
                              // decided at RESOLVE time, and Snecko Oil queues
                              // this BEHIND its own DrawCardAction, so the hand
                              // it sees INCLUDES the five cards that action drew
                              // (SneckoOil.java:42-45, two addToBots). A
                              // queue-time expansion would randomize the PRE-draw
                              // hand and spend the wrong number of draws.
                              //
                              // STREAM. One card_random_rng random(3) (0..3
                              // INCLUSIVE) per hand card with a non-negative base
                              // cost, in hand order -- INCLUDING cards that roll
                              // their own current cost, because the `||` is
                              // short-circuit and the assignment sits in its
                              // RIGHT operand. X-cost / unplayable cards
                              // (card.cost < 0) cost NOTHING. The per-card body
                              // is randomize_card_cost, shared with
                              // ConfusionPower.onCardDraw -- see that helper for
                              // the equality and freeToPlayOnce differences,
                              // which are exactly where two hand-written copies
                              // diverged before.
    // final-integrate fix-forward (claimed in the ledger's namespace table;
    // 65 was released unspent by Wave-C's relic-tail block).
    RED_SKULL_ENTRY = 65,     // Red Skull's battle-start DECIDING action --
                              // RedSkull$1 (recovered from desktop-1.0.jar;
                              // tools/oracle_bridge/driver/
                              // redskull_capture_runbook.md SS3), the addToBot
                              // at RedSkull.java:38:
                              //
                              //   if (!isActive && player.isBloodied) {
                              //       player.addPower(StrengthPower(p, 3));
                              //       isActive = true;
                              //   }
                              //
                              // Both conjuncts are RESOLVE-time reads -- the
                              // action sits at the BOTTOM of the battle-start
                              // drain while Blood Vial's / Pantograph's heals
                              // resolve first (addToTop in the Java,
                              // synchronous-at-dispatch here), so an entry at
                              // exactly half HP that a battle-start heal lifts
                              // above half grants NOTHING. Deciding at queue
                              // time instead produced a spurious -3/+3 pair
                              // (net 0 without Artifact; +3 and a burned
                              // charge with it). The grant is the game's
                              // DIRECT AbstractCreature.addPower(:506-527) --
                              // stack-or-append, NO priority sort, NO
                              // ApplyPowerAction interception -- not an
                              // APPLY_POWER item. No operands; the responding
                              // slot is found by id in s.relics at resolve
                              // (per-slot latch, matching the per-instance
                              // isActive of N queued Java actions).
    // --- S2.21 (Act-2 city normals I) ---
    VAMPIRE_DAMAGE = 66,      // VampireDamageAction.update (VampireDamageAction.
                              // java:29-45): src attacks tgt for `amount` base
                              // through the full DAMAGE pipeline, then heals the
                              // ATTACKER by the HP that hit ACTUALLY removed
                              // (`target.lastDamageTaken`,
                              // AbstractMonster.java:669 -- block, Intangible and
                              // a lethal clamp all shrink it). The Shelled
                              // Parasite's Life Suck (ShelledParasite.java:132).
                              //
                              // DISTINCT from VAMPIRE_DAMAGE_ALL (38), which is
                              // the player's Reaper: that hits EVERY live monster
                              // and addToBot's ONE summed player HEAL. This hits
                              // ONE target and heals `this.source` -- the
                              // attacker setValues took from info.owner.
                              //
                              // It cannot be a DAMAGE step followed by a HEAL
                              // step, which is why it is an opcode: the heal
                              // amount is only known after the hit lands.
                              //
                              // THE HEAL IS APPLIED INLINE, and that is exact.
                              // The Java addToTop's the HealAction, jumping it
                              // ahead of everything target.damage() just queued
                              // at the FRONT (a Thorns reflect), which is the
                              // same position an inline write occupies. It is
                              // also the only form available: op_heal (39) is
                              // player-only and its monster branch is S2.22's
                              // grant. The write follows AbstractMonster.heal
                              // (AbstractMonster.java:384-399) -- no relic pass,
                              // clamp to maxHealth, nothing at all while isDying
                              // -- the same direct-HP-write escape hatch
                              // RegenerateMonsterPower already uses. (S2.22 has
                              // since added op_heal's monster branch; the inline
                              // write here still stands, for the addToTop reason
                              // above.)
    // --- S2.22 (Act-2 city normals II) ---
    BLOCK_RANDOM_MONSTER = 67,  // GainBlockRandomMonsterAction.update
                              // (GainBlockRandomMonsterAction.java:26-42): the
                              // Centurion's Protect (Centurion.java:93). `src` is
                              // the acting monster and `amount` the block; the
                              // RECIPIENT is chosen at EXECUTE time.
                              //
                              //   valid = every monster record that is NOT src,
                              //           does NOT telegraph Intent.ESCAPE, and
                              //           is NOT isDying (hp <= 0);
                              //   tgt   = valid.empty()
                              //             ? src                 // ZERO draws
                              //             : valid[ai_rng.random(size - 1)];
                              //   tgt.addBlock(amount);
                              //
                              // Three things make it an opcode rather than a
                              // BLOCK step. The escape filter reads the
                              // TELEGRAPHED INTENT, not the escaped flag, so an
                              // ally that has merely ANNOUNCED its exit is
                              // already skipped. An empty valid list spends NO
                              // ai_rng draw at all -- the observable difference
                              // between a solo Centurion and one with a live
                              // ally. And the walk visits DEAD records too,
                              // excluding them only by hp.
                              //
                              // `tgt` on the queued item is IGNORED (the op
                              // resolves its own); the authored step sets it to
                              // src, which is also the fallback recipient.

    // --- S2.2F (the shared Act-2/3 monster framework) ---
    // 68-70 are this task's whole grant; 71-72 are S2.24's (spent below).
    // All three landed with NO producer -- they exist so the four monster
    // batches behind this framework do not each invent their own.
    OBTAIN_CARD = 68,         // AddCardToDeckAction (:83-88): put a card into the
                              // MASTER DECK, mid-combat. `extra` = CardId; no
                              // amount, no target.
                              //
                              // The body does NOT write the deck -- it cannot:
                              // the combat layer has no RunState. It appends to
                              // CombatState.pending_obtain and the run layer
                              // drains that each pump step through the single
                              // add_card_to_master_deck door, which is what makes
                              // the Omamori curse gate apply without being
                              // re-implemented. Consumer: the Writhing Mass's
                              // MEGA_DEBUFF Parasite (S2.26).
    CLEAR_CARD_QUEUE = 69,    // ClearCardQueueAction: drop every PENDING card
                              // play. The Awakened One addToTop's it at its
                              // phase transition (AwakenedOne.java:301) so the
                              // cards queued behind the killing blow never
                              // resolve. No operands.
                              //
                              // The Java body ALSO exhausts queued cards sitting
                              // in `player.limbo` -- a field this engine does NOT
                              // model as its limbo pile (that is cardInUse). See
                              // the body; a literal port destroys the card being
                              // played.
    END_PLAYER_TURN = 70,     // callEndTurnEarlySequence (:379-392): end the
                              // player's turn from inside a power. Before this
                              // the ONLY producer of the end-turn sentinel was
                              // the player's own END_TURN verb. No operands.
                              // Consumer: Time Warp's 12th card (S2.28).
    // --- S2.24 (City bosses) --- 71-72 are that task's whole opcode grant
    // (71 APPLY_STASIS as allocated; 72 was the contingency, spent as the
    // return half -- reported in the S2.24 Log).
    APPLY_STASIS = 71,        // ApplyStasisAction.update (ApplyStasisAction.
                              // java:32-80), the Bronze Orb's card theft
                              // (BronzeOrb.java:73). `src` is the acting orb
                              // -- the future power owner -- and EVERYTHING
                              // else is an execute-time fact:
                              //   * draw AND discard both empty -> done, ZERO
                              //     draws, NO power applied (:34-37);
                              //   * source pile: the DRAW pile unless it is
                              //     empty, then the DISCARD (:39,:51);
                              //   * the pick: getRandomCard(cardRandomRng,
                              //     RARE), then UNCOMMON, then COMMON, then
                              //     the unfiltered overload (:40-49/:52-61).
                              //     Each NON-EMPTY rarity-filtered view costs
                              //     ONE card_random_rng random(size-1) over
                              //     its Collections.sort()ed membership
                              //     (cardID compare -- game_id strings, a
                              //     STABLE sort, so equal ids keep pile
                              //     order; CardGroup.java:526-538,
                              //     AbstractCard.java:2583-2584); an EMPTY
                              //     view returns null WITHOUT drawing. The
                              //     unfiltered fallback (CardGroup.java:
                              //     498-500) indexes the pile IN PILE ORDER,
                              //     unsorted. Rarity is the LIVE CardRarity
                              //     (card_rarity table): a BASIC Strike never
                              //     matches a filter, a status is COMMON.
                              //   * the stolen instance leaves its pile for
                              //     LIMBO (:64 player.limbo.addToBottom) --
                              //     the ORIGINAL pool row moves, preserving
                              //     upgrade/misc/cost -- with the knowledge
                              //     chain told (knowledge_on_remove_known;
                              //     the theft is player-visible via
                              //     ShowCardAction);
                              //   * then addToTop(ApplyPowerAction(owner,
                              //     owner, StasisPower(owner, card))) (:77)
                              //     -- queued add_to_top here, amount -1,
                              //     with the pool index + 1 riding the
                              //     APPLY_POWER counter operand into the
                              //     slot's `counter` (powers.yaml id 98).
                              //     The addToTop'd ShowCardAction (:78) is
                              //     presentation.
    STASIS_RETURN = 72,       // StasisPower.onDeath (StasisPower.java:38-44):
                              // give the stolen card back when the orb dies.
                              // `amount` = the stolen card's pool index;
                              // `flags` bit 0 = the QUEUE-TIME destination
                              // choice (`player.hand.size() != 10` at the
                              // onDeath moment -- set means HAND). The HAND
                              // arm re-checks the cap at RESOLVE and spills
                              // to the discard (MakeTempCardInHandAction.
                              // update:71-77) -- two reads at two times, and
                              // both are the Java's. The card moves OUT of
                              // limbo into the destination pile; a card no
                              // longer in limbo (defensive) is a no-op.
                              // ENGINE_EMITTED_OPS: the pool index is a
                              // runtime handle (the power slot's counter),
                              // queued only by power_stasis.cpp.
};

// --- SPAWN_MONSTER field encoding --------------------------------------------
// `flags` low 16 bits carry the MonsterId. Bit 16 asks the spawn to run the
// spawned monster's usePreBattleAction after its init (SummonGremlinAction's
// behaviour; SpawnMonsterAction's is to leave it alone).
inline constexpr uint32_t kSpawnRunPreBattle = 1u << 16;
// Bit 17 asks the spawn to queue an ApplyPowerAction(m, m, MinionPower) on the
// newly inserted record, at the queue BOTTOM, BEFORE the pre-battle action above
// runs. That is SummonGremlinAction.update's exact pair and its exact order
// (SummonGremlinAction.java:114 addToBot(Minion), then :120
// m.usePreBattleAction() at isDone), which is why a summoned Gremlin Warrior's
// power list ends up [Minion, Angry] and not the other way round.
//
// A BIT rather than a policy, for the same reason bit 16 is: the two Java spawn
// actions differ. SpawnMonsterAction with isMinion=false (the slime split)
// applies NO Minion; SummonGremlinAction addToBot's one BEFORE the pre-battle
// action. Setting both bits together IS that summon pattern.
//
// AMENDED BY S2.24 -- this paragraph previously said "every summoner" uses this
// bit, naming the Collector's torch heads and the Automaton's orbs. They do
// not, and the Java is why: SpawnMonsterAction with isMinion=TRUE (their
// spawner, SpawnMonsterAction.java:67-69) applies its Minion **addToTop at the
// spawn's own resolve** -- ahead of everything else in the queue -- where this
// bit queues **addToBottom, behind it**. S2.24's two summoners (the Automaton's
// orbs, the Collector's torch heads) therefore leave the bit CLEAR and queue an
// explicit APPLY_POWER item immediately AFTER each spawn item at take-turn
// time, which resolves in exactly the addToTop position (the spawn's
// resolution queues nothing ahead of it, so "next item" and "top of queue at
// resolve" are the same slot). S2.27's Reptomancer translates the SAME Java
// fact the other admissible way: it sets this bit together with
// kSpawnMinionAtTop (bit 31 below), which applies the Minion at the spawn's
// own resolve. The two spellings produce the same resolution order --
// spawn, its Minion, the next spawn, its Minion -- and both are pinned by
// their batches' spawn-order tests; neither is "the" canonical one yet, and
// unifying them is a behaviour-preserving refactor that wants its own test
// run, not a change made in passing.
inline constexpr uint32_t kSpawnApplyMinion = 1u << 17;
// Bit 31 puts that Minion application at the queue TOP instead of the bottom.
//
// WHY IT IS A SECOND BIT AND NOT A POLICY (S2.27). The two Java spawn actions
// disagree about the PLACEMENT as well as the presence:
//     SummonGremlinAction.java:114   addToBot(new ApplyPowerAction(m, m, Minion))
//     SpawnMonsterAction.java:68     addToTop(new ApplyPowerAction(m, m, Minion))
// -- literally addToBot versus addToTop, in two actions that are otherwise the
// same shape. The Gremlin Leader is the bottom form; every SpawnMonsterAction
// summoner (Reptomancer's daggers, the Bronze Automaton's orbs, the
// Collector's torch heads) is the top form IN THE JAVA. In the engine only the
// Reptomancer spells it with this bit -- S2.24's two summoners reached the same
// resolution order with an explicitly interleaved APPLY_POWER item instead; see
// the bit-17 paragraph above for why both spellings are exact. Deriving the
// placement from bit 16 instead -- "run_pre_battle means SummonGremlinAction
// means bottom" -- would be correct today by coincidence and would encode a
// class identity in a bit that names a behaviour.
//
// It is bit 31 because the word was full: bits 0-15 are the MonsterId, 16 and 17
// are the two behaviour bits, and `draw_x` held 18..31. Rather than widen the
// item, `draw_x` NARROWS from 14 bits to 13 (see below) and gives up its top
// bit. Zero stays the identity, so no landed caller moves.
inline constexpr uint32_t kSpawnMinionAtTop = 1u << 31;
// Bits 18..30 carry the spawned record's `draw_x` position key
// (MonsterState::draw_x) as a RAW SIGNED 13-BIT two's-complement field --
// range -4096..4095, still comfortably wider than every `offsetX` in Acts 1-3
// (the extremes are the Gremlin Leader's -532 and the Slime Boss layout's +254,
// with the Reptomancer's dagger slots spanning only -250..210).
//
// NARROWED FROM 14 BITS BY S2.27 to free bit 31 for kSpawnMinionAtTop above. The
// change is invisible to every existing caller: the widest value any of them
// encodes is -532, which round-trips identically through a 13-bit field, and the
// sign-extension helper below is written against kSpawnDrawXBits so it followed
// the constant. A future position outside -4096..4095 would have to widen the
// item rather than the field, and nothing in Acts 1-4 comes close.
//
// WHY IT RIDES IN THE ITEM. `draw_x` is a per-SPAWNER, per-SLOT constant out of
// the spawning class's POSX table (GremlinLeader.POSX {-366,-170,-532};
// Reptomancer's {210,-220,180,-250}), NOT a property of the spawned type -- the
// same Gremlin Warrior sits at three different x's depending on which summon
// slot it filled, and at a fourth in the Gremlin Gang encounter. So the spawned
// monster's own spawn-at-hp init cannot know it, and the SPAWNER has to say.
// (monster_dispatch.hpp's "WHO SETS draw_x" paragraph is amended to match.)
//
// ZERO IS THE IDENTITY. A caller that writes only the MonsterId -- which is
// every pre-S2.23 caller, i.e. both large-slime split sites -- decodes to
// draw_x == 0, exactly the value those records already carried, so no landed
// spawn moves.
inline constexpr uint32_t kSpawnDrawXShift = 18u;
inline constexpr uint32_t kSpawnDrawXBits = 13u;
[[nodiscard]] constexpr uint32_t make_spawn_monster_flags(
    uint16_t monster_id, int16_t draw_x = 0, bool run_pre_battle = false,
    bool apply_minion = false, bool minion_at_top = false) noexcept {
    const uint32_t x = static_cast<uint32_t>(static_cast<uint16_t>(draw_x)) &
                       ((1u << kSpawnDrawXBits) - 1u);
    return static_cast<uint32_t>(monster_id) |
           (run_pre_battle ? kSpawnRunPreBattle : 0u) |
           (apply_minion ? kSpawnApplyMinion : 0u) |
           (minion_at_top ? kSpawnMinionAtTop : 0u) | (x << kSpawnDrawXShift);
}
[[nodiscard]] constexpr int16_t spawn_draw_x_from_flags(uint32_t flags) noexcept {
    // Sign-extend the 14-bit field: shift it to the top of a 32-bit word and
    // arithmetic-shift back. Written as an explicit subtract rather than a
    // signed right shift so it does not lean on implementation-defined
    // behaviour.
    const uint32_t raw = (flags >> kSpawnDrawXShift) & ((1u << kSpawnDrawXBits) - 1u);
    const uint32_t sign = 1u << (kSpawnDrawXBits - 1u);
    return static_cast<int16_t>(
        static_cast<int32_t>(raw ^ sign) - static_cast<int32_t>(sign));
}

// --- CONDITIONAL_DRAW field encoding -----------------------------------------
// `amount` is the number of cards to draw; `flags` low byte carries the
// RESTRICTED CardType (ConditionalDrawAction's `restrictedType`, :18-27) whose
// PRESENCE in hand cancels the draw. Stored as the raw CardType value, which is
// the same numbering the generated table uses (cards.hpp CardType). Declared as
// a uint8_t rather than the CardType enum because interp.hpp is below cards.hpp
// in the include order; the DAMAGE damage-type encoding above has the same shape.
[[nodiscard]] constexpr uint32_t make_conditional_draw_flags(
    uint8_t card_type) noexcept {
    return static_cast<uint32_t>(card_type);
}
[[nodiscard]] constexpr uint8_t conditional_draw_type_from_flags(
    uint32_t flags) noexcept {
    return static_cast<uint8_t>(flags & 0xFFu);
}

// --- DRAW_PILE_FETCH field encoding ------------------------------------------
// `amount` is how many cards to pull (Violence's magicNumber, 3 base / 4
// upgraded); `flags` low byte carries the CardType to match in the draw pile
// (DrawPileToHandAction's `typeToCheck`, :21-28), in the same raw-CardType
// spelling CONDITIONAL_DRAW uses above and for the same include-order reason.
[[nodiscard]] constexpr uint32_t make_draw_pile_fetch_flags(
    uint8_t card_type) noexcept {
    return static_cast<uint32_t>(card_type);
}
[[nodiscard]] constexpr uint8_t draw_pile_fetch_type_from_flags(
    uint32_t flags) noexcept {
    return static_cast<uint8_t>(flags & 0xFFu);
}

// --- RANDOM_CARD_TO_DRAW field encoding --------------------------------------
// `amount` is the number of cards to generate (Chrysalis / Metamorphosis
// magicNumber, 3 base / 5 upgraded); `flags` low byte carries the CardType the
// RED combat pool is filtered to (returnTrulyRandomCardInCombat's argument --
// SKILL for Chrysalis, ATTACK for Metamorphosis), in the same raw-CardType
// spelling CONDITIONAL_DRAW and DRAW_PILE_FETCH use above and for the same
// include-order reason.
[[nodiscard]] constexpr uint32_t make_random_card_to_draw_flags(
    uint8_t card_type) noexcept {
    return static_cast<uint32_t>(card_type);
}
[[nodiscard]] constexpr uint8_t random_card_to_draw_type_from_flags(
    uint32_t flags) noexcept {
    return static_cast<uint8_t>(flags & 0xFFu);
}

// --- RANDOM_COLORLESS_TO_HAND field encoding ---------------------------------
// `amount` is the number of independent pool draws; `flags` carries the two bits
// below. BOTH default to 0, which is exactly the `extra` Jack of All Trades'
// rows packed before they existed, so those rows and their tests are
// byte-identical (checked, not assumed -- no test encoded a non-zero extra on
// this opcode). MIRRORED in tools/registry_gen/stsgen/vocab.py
// (COLORLESS_TO_HAND_FLAGS), authored as `generated: [...]` on the step.
//
// TransmutationAction.update (:44-51) is the only setter of either bit; it runs
// upgrade() BEFORE setCostForTurn(0), so the this-turn zero replaces the
// UPGRADED row's cost when both bits are set.
inline constexpr uint32_t kColorlessToHandCostZeroForTurn = 1u << 0;
inline constexpr uint32_t kColorlessToHandUpgradedCopy = 1u << 1;

// --- PLAY_CARD field encoding ------------------------------------------------
// The general "play this card again / play the next card off the deck" verb.
//   * `amount` -> the SOURCE card-pool index, unless kPlayCardFromDrawTop.
//   * `tgt`    -> the monster the play targets, or kActorRandomEnemy (one
//                 card_random_rng draw at execute, getRandomMonster).
//   * `flags`  -> the bits below.
// The played instance is always free: the game reaches this path with
// `isInAutoplay` / `ignoreEnergyTotal` set, and AbstractPlayer.useCard's energy
// deduction is skipped for an autoplayed card (AbstractPlayer.java:1378), so the
// instance's cost_now is zeroed before it is queued.
//
// Play a stat-equivalent COPY of the source instead of the source instance
// (AbstractCard.makeSameInstanceOf, DoubleTapPower.java:50). Without it the
// source instance itself is played.
inline constexpr uint32_t kPlayCardCopy = 1u << 0;
// The played instance carries CardFlag::PURGE_ON_USE -- destroyed when it
// resolves, landing in no pile (UseCardAction.java:89-94). The replay copies use
// this; a card played off the deck does not.
inline constexpr uint32_t kPlayCardPurge = 1u << 1;
// Force the played instance to exhaust (card.exhaustOnUseOnce, the `exhausts`
// argument of PlayTopCardAction, PlayTopCardAction.java:48).
inline constexpr uint32_t kPlayCardExhaust = 1u << 2;
// Source is the TOP OF THE DRAW PILE rather than `amount`: reshuffle the discard
// in when the draw pile is empty, give up when both are empty
// (PlayTopCardAction.update:33-43). This is the bit a start-of-turn "play the top
// card of your draw pile" power authors; it needs no card instance, so it is the
// only form a YAML effect program can express.
inline constexpr uint32_t kPlayCardFromDrawTop = 1u << 3;
// Insert in FRONT of the card queue (addCardQueueItem(item, true) --
// GameActionManager.java:102-108, which lands at index 1 behind the
// currently-resolving head) instead of at the back.
inline constexpr uint32_t kPlayCardQueueFront = 1u << 4;
// TWO-LEVEL DEFERRAL -- the recovered MayhemPower$1 (MayhemPower$1.java, from
// the shipped desktop-1.0.jar; fills MayhemPower.java:37's CFR hole). The
// game's Mayhem hook does NOT queue the play itself: it queues an anonymous
// action whose update() is
//     addToBot(new PlayTopCardAction(getRandomMonster(null, true,
//                                    cardRandomRng), false)); isDone = true;
// i.e. the queued item, when it executes, ROLLS THE TARGET (the ctor argument
// -- one card_random_rng draw, before anything else) and addToBots the real
// play to the very END of the queue. Because the hook's items sit AHEAD of
// the turn's DrawCardAction (GameActionManager.java:361) while their re-queued
// plays land BEHIND it, Mayhem plays the POST-draw top card, and at >= 2
// stacks every target roll is spent before any play resolves. An item with
// this bit reproduces exactly that: op_play_card rolls one live-monster
// target NOW and re-queues the same item -- bit cleared, target baked -- at
// the bottom, touching no pile. Engine-only (Mayhem's native body); never
// authored from YAML.
inline constexpr uint32_t kPlayCardDeferRoll = 1u << 5;

// --- CHOOSE_CARD field encoding ---------------------------------------------
// The blocking hand-card select verb. `amount` carries how many cards to select;
// `flags` (the step's `extra`) packs the manipulation kind and the RANDOM bit:
//   * bits [0..1] -> ChoiceKind low bits (what to do with each selected card).
//   * bit  [2]    -> RANDOM: pick with card_random_rng instead of prompting the
//                    player (ExhaustAction.isRandom -- True Grit base).
//   * bit  [3]    -> ChoiceKind bit 2 -- the high kind bit lives ABOVE the
//                    RANDOM bit so kinds 0..3 keep their original packed bytes
//                    (append-only). kind = bits[0..1] | bit3 << 2.
//   * bits [4..7] -> `copies` - 1 for the DUPLICATE kind (Dual Wield
//                    magicNumber: 1 base / 2 upgraded). Storing copies-1 keeps
//                    the default (1 copy) at 0, so an extra authored before
//                    DUPLICATE existed stays byte-identical.
//   * bit  [8]    -> the author-only queue-time hand-nonempty guard (below).
//   * bit  [9]    -> ChoiceKind bit 3 -- kinds 8+ need a fourth kind bit, and
//                    every lower position is taken, so it lives here.
//                    kind = bits[0..1] | bit3 << 2 | bit9 << 3, which leaves
//                    kinds 0..7 packing exactly as before (append-only).
//   * bits[10..12]-> the CARD-TYPE FILTER, stored as CardType + 1 so that 0
//                    means "no filter" and every previously-authored extra
//                    stays byte-identical (CardType::ATTACK is 0, so a raw
//                    type could not be distinguished from "absent").
//   * bit  [13]   -> a RUNTIME latch, never authored: the draw-source temp
//                    group has already been built and its card_random_rng
//                    draws billed (see prepare_choice_draw_source).
//   * bit  [14]   -> OPTIONAL: the screen selects ZERO to `amount` cards and
//                    ends on an explicit confirm (anyNumber && canPickZero).
//                    Authored as `optional: true`.
//   * bits[16..19]-> a RUNTIME counter, never authored: how many cards of the
//                    hand are currently SELECTED on an open optional screen
//                    (0..kHandCap, which is why four bits suffice). Bit 15 is
//                    left free rather than starting the field there, so the
//                    counter is a clean nibble.
// MIRRORED in tools/registry_gen/stsgen/vocab.py (CHOICE_KINDS + the bit
// layout) so a CHOOSE_CARD effect step authored in cards.yaml packs an
// identical `extra`.
enum class ChoiceKind : uint8_t {
    EXHAUST = 0,         // move each selected hand card to the exhaust pile
    PUT_ON_DRAW_TOP = 1, // move each selected hand card to the top of the draw pile
    UPGRADE = 2,         // upgrade each selected hand card in place (upgrade++)
    // The source pile is the DISCARD pile, not the hand. Choose a
    // card from the discard pile and put it on top of the draw pile (Headbutt /
    // DiscardPileToTopOfDeckAction: forced when discard has <= 1 card, a real
    // gridSelect prompt when >= 2). All other kinds source from the hand.
    DISCARD_TO_DRAW_TOP = 3,
    // Dual Wield / DualWieldAction: choose one ATTACK-or-POWER
    // hand card and add `copies` stat-equivalent clones of it to the hand (hand
    // cap spills to discard). Eligibility is the isDualWieldable predicate
    // (DualWieldAction.java:95-97); the PROMPTED resolution also reproduces the
    // hand-order shuffle of the select screen (see apply_choice_selection).
    DUPLICATE = 4,
    // The source pile is the EXHAUST pile: choose one exhausted card and return
    // it to the hand (ExhumeAction.update:38-113). Eligibility drops every copy
    // of the source card itself (the action lifts them out of the grid, :74-80)
    // and the whole choice is dead while the hand is full (:40-43). A returned
    // SKILL costs 0 for the turn while Corruption is up (:57-59, :94-96).
    EXHAUST_TO_HAND = 5,
    // 6-7 are permanent gaps (their owner needed no kind). Kinds are
    // append-only, so they are stepped over rather than backfilled.
    //
    // Forethought / ForethoughtAction: move each selected hand card to the
    // BOTTOM of the draw pile (CardGroup.moveToBottomOfDeck -> addToBottom ==
    // group.add(0, c), CardGroup.java:459-461,898-902 -- so the FIRST card moved
    // ends up nearest the top and is drawn first). A moved card whose
    // AbstractCard.cost is > 0 additionally gains CardFlag::FREE_TO_PLAY_ONCE
    // (ForethoughtAction.java:39-41 forced path, :57-59 selected path); a
    // 0-cost card does not, and neither does an X-cost or unplayable one (their
    // cost is negative in the Java). The BASE card authors this kind MANDATORY
    // and exactly one -- auto-resolving with no screen and no RNG when the hand
    // holds a single card (:37-45, hand.getTopCard() -- the LAST hand entry, no
    // cardRandomRng draw anywhere, unlike PutOnDeckAction's forced path). The
    // UPGRADED card authors it OPTIONAL (:50, open(msg, 99, true, true)).
    PUT_ON_DRAW_BOTTOM = 8,
    //
    // The source pile is the DRAW PILE, filtered to ONE CardType (the
    // bits[10..12] filter above): choose one matching draw-pile card and put it
    // in the hand -- SkillFromDeckToHandAction / AttackFromDeckToHandAction,
    // which differ only in that type. Both build a temp CardGroup of the
    // matching cards first (billed by prepare_choice_draw_source), then: zero
    // matches is a silent no-op (:40-43), exactly one is auto-taken with no
    // screen (:44-64), and two or more open a MANDATORY exactly-one grid select
    // with no cancel button in combat (:65 ->
    // GridCardSelectScreen.open(group, numCards, tipMsg, forUpgrade=false),
    // :463-465 -> :437-457, where the cancel button is shown only for
    // upgrade/transform/purge/shop screens, :446-448). The chosen card leaves
    // the REAL draw pile for the hand, or for the DISCARD pile when the hand is
    // already full (:46-48 / :72-74); the unchosen cards keep their draw-pile
    // order, because only the temp browse list was ever randomized.
    DRAW_TO_HAND = 9,
    // 10 is a permanent gap (kinds are append-only).
    //
    // Liquid Memories / BetterDiscardPileToHandAction's (count, newCost) ctor
    // (BetterDiscardPileToHandAction.java:40-48, setCost = true, newCost = 0,
    // optional = false). The source pile is the DISCARD pile and the
    // destination is the HAND, at cost 0 FOR THE TURN.
    //
    // WHY IT NEEDS ITS OWN KIND rather than flags on DISCARD_TO_DRAW_TOP: the
    // packed CHOOSE_CARD encoding has no destination field at all -- the kind IS
    // the destination -- and the two differ in destination, in the cost write,
    // and in the hand-full behaviour. Nothing else in the enum sources from the
    // discard.
    //
    // update (:50-117), in branch order:
    //   :53-56   empty discard, or numberOfCards <= 0 -> isDone, nothing.
    //   :57-75   `discardPile.size() <= numberOfCards && !optional` -> FORCED,
    //            NO SCREEN: snapshot the discard and move each card, IN DISCARD
    //            ORDER. That is the eligible <= amount branch op_choose_card
    //            already takes.
    //   :76-86   otherwise a MANDATORY exactly-`numberOfCards` grid select over
    //            the discard pile (`optional` is false on this ctor, so the
    //            zero-pickable overload at :78/:83 is unreachable).
    //   :90-110  resolution applies the same per-card body as the forced branch.
    //
    // THE PER-CARD BODY, and its one trap: `if (hand.size() < 10) { addToHand;
    // setCostForTurn(0); discardPile.removeCard; }` -- the guard wraps the
    // REMOVAL too, so a card that does not fit STAYS IN THE DISCARD PILE. There
    // is no spill-to-discard here, unlike MakeTempCardInHandAction, and no
    // partial move: it is all three writes or none.
    //
    // NO RNG anywhere in this action, on any path.
    DISCARD_TO_HAND_FREE = 11,
    //
    // Gambler's Brew AND Gambling Chip -- literally one Java action, one boolean
    // apart. GamblersBrew.use (GamblersBrew.java:36-41) queues
    // `GamblingChipAction(player, true)`; GamblingChip.atTurnStartPostDraw
    // (GamblingChip.java:34-42) queues `GamblingChipAction(player)`, i.e.
    // notChip = false. The boolean picks a PROMPT STRING (:42-46) and nothing
    // else, so the two consumers share this kind outright.
    //
    // GamblingChipAction.update (GamblingChipAction.java:39-63):
    //   :41-49  first tick -- handCardSelectScreen.open(TEXT[..], 99, true,
    //           true), the SAME optional zero-to-all screen Elixir and
    //           Forethought+ open, then a WaitAction and tickDuration.
    //   :51-61  on retrieval, and ONLY if the selection is non-empty:
    //               addToTop(new DrawCardAction(p, selectedCards.size()));
    //               for (c : selectedCards.group) {
    //                   hand.moveToDiscardPile(c);
    //                   GameActionManager.incrementDiscard(false);
    //                   c.triggerOnManualDiscard();
    //               }
    //
    // ORDERING, which is easy to get backwards: the addToTop only QUEUES, while
    // the discard loop at :54-58 is synchronous inside the same update(). So the
    // observable order is DISCARD EVERY PICK (in pick order), THEN draw exactly
    // that many -- and the draw sits at the TOP of the queue, ahead of anything
    // queued behind this action. An EMPTY confirm does nothing at all: no draw,
    // no discard. The count is `selectedCards.size()`, read AT CONFIRM, for both
    // consumers alike -- there is no relic-side count.
    //
    // The draw-back is folded into resolve_optional_choice rather than given a
    // fused opcode: the discard half is an ordinary per-slot manipulation and
    // the DRAW is just an add_to_top of the existing opcode 4, so an opcode
    // would only duplicate the CHOOSE lifecycle.
    //
    // RNG: this action spends none itself. The queued DRAW may reshuffle
    // (shuffle_rng) and fires onCardDraw per drawn card, so with Snecko Eye's
    // Confusion up each drawn card costs one card_random_rng draw.
    //
    // TWO JAVA CALLS WITH NO S1 COUNTERPART, named rather than invented:
    //   * `c.triggerOnManualDiscard()` -- the binders are Silent's
    //     Reflex/Tactician and Watcher's Sands of Time. No S1 Ironclad card
    //     overrides it, so there is nothing to fire.
    //   * `GameActionManager.incrementDiscard(false)` -- nothing in the engine
    //     reads a discard-this-turn counter (no such field exists), so the
    //     increment has no observable consumer. Whoever registers the first
    //     card that reads one owns adding it here and at the end-of-turn
    //     discard.
    HAND_TO_DISCARD_THEN_DRAW = 12,
};

// Which pile a CHOOSE_CARD of this kind selects from. `arg0` of a CHOOSE action
// indexes THIS pile, not always the hand.
enum class ChoiceSource : uint8_t {
    HAND = 0,
    DISCARD = 1,
    EXHAUST = 2,
    GENERATED = 3,  // Discovery's three-card offer packed in its queue item
    DRAW = 4,       // the draw pile, filtered to one CardType (Secret Technique /
                    // Secret Weapon). arg0 of a CHOOSE indexes the DRAW pile.
};

[[nodiscard]] constexpr ChoiceSource choice_source(ChoiceKind k) noexcept {
    switch (k) {
        case ChoiceKind::DISCARD_TO_DRAW_TOP:
        case ChoiceKind::DISCARD_TO_HAND_FREE:
            return ChoiceSource::DISCARD;
        case ChoiceKind::EXHAUST_TO_HAND:
            return ChoiceSource::EXHAUST;
        case ChoiceKind::DRAW_TO_HAND:
            return ChoiceSource::DRAW;
        default:
            return ChoiceSource::HAND;
    }
}

// Does a CHOOSE_CARD of this kind select from the discard pile (vs. the hand)?
[[nodiscard]] constexpr bool choice_source_is_discard(ChoiceKind k) noexcept {
    return choice_source(k) == ChoiceSource::DISCARD;
}

inline constexpr uint8_t kDiscoveryChoiceCount = 3;

// Discovery's generated offer is stored entirely in the queued DISCOVERY item:
// choices 0/1 occupy flags' low/high u16 and choice 2 occupies amount's low
// u16. amount==0 is the one unprepared sentinel (CardId::NONE is zero and no
// real offer can carry it), so opening the screen needs no CombatState field or
// schema bump.
[[nodiscard]] constexpr bool discovery_choice_prepared(
    const ActionQueueItem& item) noexcept {
    return item.amount != 0;
}
[[nodiscard]] constexpr CardId discovery_choice_card(
    const ActionQueueItem& item, uint8_t slot) noexcept {
    if (slot == 0) {
        return static_cast<CardId>(static_cast<uint16_t>(item.flags & 0xFFFFu));
    }
    if (slot == 1) {
        return static_cast<CardId>(
            static_cast<uint16_t>((item.flags >> 16u) & 0xFFFFu));
    }
    if (slot == 2) {
        return static_cast<CardId>(
            static_cast<uint16_t>(static_cast<uint32_t>(item.amount) & 0xFFFFu));
    }
    return CardId::NONE;
}

// --- DISCOVERY's pool selector and copy count --------------------------------
//
// DiscoveryAction has three ctors (DiscoveryAction.java:25-43) and its consumers
// differ in exactly two ways: WHICH POOL the three offers are drawn from, and
// HOW MANY copies of the chosen card are created.
//
//   Discovery (the card)  -- DiscoveryAction(), the full RED combat pool,
//                            amount 1 (:25-29).
//   Attack / Skill / Power Potion -- DiscoveryAction(CardType, potency), the
//                            type-filtered RED pool, amount = potency
//                            (AttackPotion.java:40-42 and siblings).
//   Colorless Potion      -- DiscoveryAction(true, potency), the colorless pool
//                            (ColorlessPotion.java:38-40).
//
// `flags` and `amount` are already fully consumed by the persisted offer, but a
// DISCOVERY item uses NEITHER of ActionQueueItem's two actor bytes -- there is
// no source or target creature anywhere in DiscoveryAction. So the selector
// rides in `src` and the copy count in `tgt`. ActionQueueItem stays 12 bytes, so
// there is no SCHEMA_VERSION bump and no fixture regeneration; this is the same
// trick the offer itself already plays with flags/amount.
//
// THE DEFAULTS ARE THE ACTOR SENTINELS, deliberately. Every queue helper writes
// src = kActorPlayer and, for a SELF-targeted step, tgt = kActorPlayer, so an
// AUTHORED DISCOVERY step (registry/cards.yaml's Discovery) arrives carrying
// 0xFF in both. Reading 0xFF as "full combat pool" and "one copy" therefore
// leaves every previously-authored item byte-identical AND correct, with no
// generator packer needed. A native writer that wants anything else stamps a
// real value.
enum class DiscoveryPool : uint8_t {
    COMBAT = 0,     // kIroncladCombatPool  -- returnTrulyRandomCardInCombat()
    ATTACK = 1,     // kIroncladAttackPool  -- ...InCombat(CardType.ATTACK)
    SKILL = 2,      // kIroncladSkillPool   -- ...InCombat(CardType.SKILL)
    POWER = 3,      // kIroncladPowerPool   -- ...InCombat(CardType.POWER)
    COLORLESS = 4,  // kColorlessCombatPool -- returnTrulyRandomColorlessCardInCombat()
};

[[nodiscard]] constexpr DiscoveryPool discovery_pool(
    const ActionQueueItem& item) noexcept {
    return item.src == kActorPlayer ? DiscoveryPool::COMBAT
                                    : static_cast<DiscoveryPool>(item.src);
}

// How many stat-equivalent copies of the chosen card are created. This is
// DiscoveryAction's `amount` field, which is the potion's POTENCY -- 1 base, 2
// with Sacred Bark (AbstractPotion.getPotency doubles it). The Discovery CARD
// passes no amount and gets the ctor default of 1 (:28).
[[nodiscard]] constexpr int discovery_copies(
    const ActionQueueItem& item) noexcept {
    return item.tgt == kActorPlayer ? 1 : static_cast<int>(item.tgt);
}

// Is this DISCOVERY screen skippable? DiscoveryAction.update opens
// cardRewardScreen.customCombatOpen(cards, TEXT[1], this.cardType != null)
// (DiscoveryAction.java:49), and customCombatOpen's third parameter IS the
// screen's `skippable` (CardRewardScreen.java:485-500: stored at :498, drives
// SkipCardButton show/hide at :499-502). cardType is non-null for exactly the
// TYPED ctor (DiscoveryAction.java:31-36 -- Attack/Skill/Power Potion); the
// default ctor (the Discovery card, :25-29) and the colorless ctor (Colorless
// Potion, :38-43) both leave it null. So skippability is a pure function of
// the pool selector.
[[nodiscard]] constexpr bool discovery_skippable(
    const ActionQueueItem& item) noexcept {
    const DiscoveryPool p = discovery_pool(item);
    return p == DiscoveryPool::ATTACK || p == DiscoveryPool::SKILL ||
           p == DiscoveryPool::POWER;
}

// How many WASTED offer regenerations one DISCOVERY resolution burns, pick and
// skip alike -- the RNG model behind the capture-observed cardRandomRng spend.
//
// DiscoveryAction.update recomputes `generatedCards` at the top of EVERY update
// tick (DiscoveryAction.java:47, outside the duration branch); only the first
// tick's list reaches the screen (:49). The action ticks once to open the
// screen (duration == ACTION_DUR_FAST -> customCombatOpen + tickDuration,
// :48-51), is then FROZEN while the screen is up (AbstractRoom.update runs
// actionManager.update() only under !AbstractDungeon.isScreenUp), and on close
// ticks until tickDuration drives duration below zero
// (AbstractGameAction.java:74-79), regenerating -- and discarding -- a full
// three-distinct-card offer each tick.
//
// THE TICK COUNT IS TIMING-DRIVEN AND IS PINNED BY THE ORACLE CONTRACT, not by
// game source: tickDuration subtracts Gdx.graphics.getDeltaTime(), which the
// oracle fork's stripAnimationCollapse patch fixes at STEP = 0.043
// (tools/oracle_bridge/communicationmod-oracle/README-oracle.md, the
// rendering-strip patch table). From
// ACTION_DUR_FAST = 0.25 (Settings.java:685): the open tick leaves 0.207, and
// the post-close ticks run 0.164 / 0.121 / 0.078 / 0.035 / -0.008 -- FIVE
// regenerations after the pick or skip, then done. (In an unpatched client the
// count would be frame-rate-dependent; under the frozen §1.2 environment it is
// a constant of the fork, which is the runtime oracle this engine replays.)
//
// Capture table (both G6 campaigns agree record-for-record; counters are the
// cardRandomRng totals at screen-open / after the close resolved):
//   STS00220 seq 61 pick  0 -> 3 -> 19   (open 3, close 16 = 5 regens + 1 dupe)
//   STS00221 seq 53 skip  0 -> 4 -> 19   (open 3+1 dupe, close 15 = 5 regens)
//   STS00425 seq 38 pick  0 -> 3 -> 19
//   STS00610 seq 104 skip 0 -> 3 -> 18   (close 15: no dupe in any regen)
//   STS01372 seq 39 pick  0 -> 3 -> 19
//   STS01861 seq 31 skip  0 -> 3 -> 18
//   STS01861 seq 90 skip  0 -> 3 -> 19   (a dupe INSIDE a wasted regen)
// Pick and skip agree because line :47 runs regardless of what the screen
// returned; the retrieve half (:53-85) spends no cardRandomRng.
//
// The one Java-order nuance: the FIRST wasted regeneration precedes the
// retrieve (tick 2 regenerates, THEN reads discoveryCard), the other four
// follow it. advance.cpp's CHOOSE dispatch reproduces that order exactly;
// nothing observable separates the interleavings today (the retrieve draws
// nothing), but the order is the Java's, so a future rng-spending hand-entry
// hook cannot silently reorder the stream.
inline constexpr int kDiscoveryWastedRegens = 5;

inline constexpr uint32_t kChoiceRandomBit = 1u << 2;
inline constexpr uint32_t kChoiceKindHighBit = 1u << 3;   // ChoiceKind bit 2
inline constexpr uint32_t kChoiceCopiesShift = 4;
inline constexpr uint32_t kChoiceKindHighBit2 = 1u << 9;  // ChoiceKind bit 3
// The card-type filter (bits [10..12]), stored as CardType + 1 so that the
// absent value is 0 and every previously-packed `extra` is byte-identical --
// CardType::ATTACK is 0, so a raw type would be indistinguishable from "none".
inline constexpr uint32_t kChoiceTypeFilterShift = 10;
inline constexpr uint32_t kChoiceTypeFilterMask = 0x7u << kChoiceTypeFilterShift;
// The "no card-type filter" sentinel. Not a CardType value; CardType is a
// generated enum below this header in the include order, so filters travel as
// raw bytes here exactly like the DAMAGE damage-type and CONDITIONAL_DRAW
// encodings above.
inline constexpr uint8_t kChoiceNoTypeFilter = 0xFFu;

// OPTIONAL selection (bit 14): the screen opened with anyNumber && canPickZero,
// so it selects ZERO to `amount` cards and only an explicit confirm ends it.
// Authored as `optional: true`; MIRRORED as CHOICE_OPTIONAL_BIT in vocab.py.
inline constexpr uint32_t kChoiceOptionalBit = 1u << 14;

// RANDOM_ANY_NUMBER (bit 15): a random ExhaustAction whose `anyNumber` argument
// is true.  It never opens a selection screen -- RANDOM already takes that
// path -- but it must NOT take ExhaustAction's `!anyNumber && hand <= amount`
// forced-all shortcut.  FiendFireAction creates one such action per card in the
// hand, so its final one-card action still calls hand.getRandomCard(cardRandomRng)
// and spends `random(0)`.  This is a native runtime bit, not an authored
// registry field.
inline constexpr uint32_t kChoiceRandomAnyNumberBit = 1u << 15;

[[nodiscard]] constexpr uint32_t make_choose_flags(
    ChoiceKind kind, bool random, int copies = 1,
    uint8_t type_filter = kChoiceNoTypeFilter, bool optional = false) noexcept {
    const uint32_t kv = static_cast<uint32_t>(static_cast<uint8_t>(kind));
    return (kv & 0x3u) | ((kv & 0x4u) != 0u ? kChoiceKindHighBit : 0u) |
           ((kv & 0x8u) != 0u ? kChoiceKindHighBit2 : 0u) |
           (random ? kChoiceRandomBit : 0u) |
           (static_cast<uint32_t>(copies - 1) << kChoiceCopiesShift) |
           (optional ? kChoiceOptionalBit : 0u) |
           (type_filter == kChoiceNoTypeFilter
                ? 0u
                : ((static_cast<uint32_t>(type_filter) + 1u)
                   << kChoiceTypeFilterShift));
}
[[nodiscard]] constexpr ChoiceKind choose_kind_from_flags(uint32_t flags) noexcept {
    return static_cast<ChoiceKind>(static_cast<uint8_t>(
        (flags & 0x3u) | ((flags & kChoiceKindHighBit) != 0u ? 0x4u : 0u) |
        ((flags & kChoiceKindHighBit2) != 0u ? 0x8u : 0u)));
}
// The CardType a choice's source pile is filtered to, or kChoiceNoTypeFilter.
[[nodiscard]] constexpr uint8_t choose_type_filter_from_flags(
    uint32_t flags) noexcept {
    const uint32_t v = (flags & kChoiceTypeFilterMask) >> kChoiceTypeFilterShift;
    return v == 0u ? kChoiceNoTypeFilter : static_cast<uint8_t>(v - 1u);
}
[[nodiscard]] constexpr bool choose_is_random(uint32_t flags) noexcept {
    return (flags & kChoiceRandomBit) != 0u;
}
// The DUPLICATE kind's copy count (1-based; bits [4..7] store copies - 1).
[[nodiscard]] constexpr int choose_copies_from_flags(uint32_t flags) noexcept {
    return static_cast<int>((flags >> kChoiceCopiesShift) & 0xFu) + 1;
}
[[nodiscard]] constexpr bool choose_is_optional(uint32_t flags) noexcept {
    return (flags & kChoiceOptionalBit) != 0u;
}
[[nodiscard]] constexpr bool choose_random_any_number(uint32_t flags) noexcept {
    return (flags & kChoiceRandomAnyNumberBit) != 0u;
}

// --- The OPTIONAL selection's accumulated picks ------------------------------
//
// HandCardSelectScreen does not mark a chosen card in place: selectHoveredCard
// REMOVES it from p.hand and appends it to selectedCards (:375-383,
// `hand.removeCard` + `selectedCards.addToTop`), and deselecting it appends it
// back onto the END of p.hand (:441-443 / :171-177, `hand.addToTop`) -- it does
// NOT return to where it came from. The two groups are therefore, at every
// instant, one ordered sequence: [what is left of the hand] ++ [the picks, in
// pick order].
//
// That is exactly how this engine stores it: the selected cards stay in `hand`
// as its trailing SUFFIX, and the only extra state is how long that suffix is --
// this nibble in the open item's flags. hand[0 .. hand_count-k) is the real
// hand, hand[hand_count-k .. hand_count) is selectedCards in pick order, and the
// oracle's own split (getHandSelectState emits `hand` and `selected`
// separately) concatenates onto it one-for-one. Nothing new is stored per card,
// and the pick ORDER -- which is observable, because the confirm applies the
// picks in it -- is carried by the array itself.
//
// Slot indices are stable while the screen is open: the pump is blocked on the
// item, so the only actions that can run are this screen's own toggles.
inline constexpr uint32_t kChoiceSelectedShift = 16;
inline constexpr uint32_t kChoiceSelectedMask = 0xFu << kChoiceSelectedShift;

[[nodiscard]] constexpr uint8_t choose_selected_count(uint32_t flags) noexcept {
    return static_cast<uint8_t>((flags & kChoiceSelectedMask) >>
                                kChoiceSelectedShift);
}
[[nodiscard]] constexpr uint32_t with_choose_selected_count(
    uint32_t flags, uint8_t n) noexcept {
    return (flags & ~kChoiceSelectedMask) |
           ((static_cast<uint32_t>(n) << kChoiceSelectedShift) &
            kChoiceSelectedMask);
}

// Thinking Ahead: an AUTHOR-ONLY queue-time guard bit, never inspected by the
// interpreter's execute path (op_choose_card / choice_slot_eligible etc. never
// read it). ThinkingAhead.use (:32-37) queues PutOnDeckAction(1, false) only
// when AbstractDungeon.player.hand.size() > 0 at the instant use() reads it
// (:34). AbstractPlayer.useCard (AbstractPlayer.java:1358-1384) runs c.use()
// at :1369 and only removes the played card from hand.group at :1374 --
// AFTER use() returns -- so for an ORDINARY HAND PLAY the played card is
// STILL counted in that read (hand.size() >= 1 always, since it counts
// itself): the guard is a structural no-op there. It matters only when the
// card is AUTOPLAYED off the draw pile (PlayTopCardAction / Havoc), where it
// was never a hand.group member and hand.size() reflects the real hand.
// card_play.cpp queue_effect_step processes one card's steps in that same
// synchronous, nothing-executed-yet order, so checking CombatState::hand_count
// when it reaches a step carrying this bit reproduces the Java read exactly
// in both cases -- self-inclusive for a hand play (op_play_card /
// op_play_top_draw limbo_add an autoplayed card BEFORE its own effects queue,
// so it is correctly absent from hand there). A step so gated is simply never
// queued when the check fails. Bit 8 -- clear of kind[0..1]/RANDOM[2]/
// kind-hi[3]/copies[4..7]. MIRRORED in tools/registry_gen/stsgen/vocab.py
// (CHOICE_QUEUE_GUARD_HAND_NONEMPTY_BIT), authored as `guard: hand_nonempty`
// on a CHOOSE_CARD step.
inline constexpr uint32_t kChoiceQueueGuardHandNonemptyBit = 1u << 8;
[[nodiscard]] constexpr bool choose_queue_guard_hand_nonempty(
    uint32_t flags) noexcept {
    return (flags & kChoiceQueueGuardHandNonemptyBit) != 0u;
}

// A RUNTIME latch on a queued draw-source CHOOSE_CARD, never authored in YAML:
// its temp browse group has been built and the card_random_rng draws that build
// costs have been billed. SkillFromDeckToHandAction / AttackFromDeckToHandAction
// build that group on the action's FIRST update tick (:34-39), before they know
// whether a screen will open at all, so the billing must happen once when the
// item starts resolving -- on the blocking path AND on both auto-resolve paths
// (zero matches, one match). The latch is what makes it once: a blocked item
// stays at the queue head across many pump steps.
inline constexpr uint32_t kChoiceDrawTempBuiltBit = 1u << 13;

// --- MAKE_CARD field encoding -----------------------------------------------
// Card creation into a pile (MakeTempCardInHand/Discard, ShuffleIntoDrawPile).
// The status/created card is allocated into a free card_pool row and its pool
// index inserted into the destination pile.
//   * `flags` low 16 bits  -> the CardId to create (make_make_card_flags below).
//   * `src`                -> the destination CardPile (HAND/DRAW/DISCARD/DRAW_RANDOM).
//   * `amount`             -> how many copies to create.
//   * `tgt`                -> unused; MUST be kActorPlayer so it is not mistaken
//                             for an enemy-fan-out sentinel.
// Hand-cap interaction (MakeTempCardInHandAction.update, :71-77): when creating
// into HAND would overflow kHandCap, the overflow copies go to the DISCARD pile
// instead (the game's "hand is full" spill). DRAW_RANDOM inserts at a random
// draw-pile position via ONE card_random_rng draw (CardGroup.addToRandomSpot,
// CardGroup.java:463-468) -- the Wild Strike / Reckless Charge status-shuffle path.
enum class CardPile : uint8_t {
    HAND = 0,
    DRAW = 1,         // onto the top of the draw pile (drawn next)
    DISCARD = 2,
    DRAW_RANDOM = 3,  // random spot in the draw pile (one card_random_rng draw)
};

[[nodiscard]] constexpr uint32_t make_make_card_flags(uint16_t card_id) noexcept {
    return static_cast<uint32_t>(card_id);
}
[[nodiscard]] constexpr uint16_t make_card_id_from_flags(uint32_t flags) noexcept {
    return static_cast<uint16_t>(flags & 0xFFFFu);
}
// Bit 24 of a MAKE_CARD item's `flags`: create the copy already upgraded
// (makeStatEquivalentCopy of an upgraded card -- an upgraded Anger clones an
// upgraded Anger; AbstractCard.makeStatEquivalentCopy:825-848). The destination
// CardPile lives in the item's `src`, not here (card_play.cpp splits the step's
// packed `extra` into {flags = CardId(+upg bit), src = CardPile}).
inline constexpr uint32_t kMakeCardUpgradedBit = 1u << 24;
[[nodiscard]] constexpr bool make_card_upgraded_from_flags(uint32_t flags) noexcept {
    return (flags & kMakeCardUpgradedBit) != 0u;
}

// --- APPLY_POWER field encoding ---------------------------------------------
// `flags` bits 0..14 hold the PowerId; bit 15 is an explicit
// `isSourceMonster == false` constructor override for duration debuffs;
// `amount` holds the stack count. Use these helpers so no caller hand-rolls
// the packing. PowerId currently occupies less than eight bits, so reserving
// bit 15 changes no existing id.
//
// `flags` bits 16..31 additionally carry the applied power's COUNTER OPERAND --
// the second number a `counter`-carrying PowerSlot (types.hpp) is constructed
// with. It defaults to 0, which is exactly what every APPLY_POWER item and every
// generated APPLY_POWER step packed before this existed, so all of them stay
// byte-identical (checked by the kBash pin in cards.hpp, not assumed).
//
// Only The Bomb authors it: TheBomb.use passes THREE numbers to one
// ApplyPowerAction -- the stack amount 3 and the ctor's (turns=3, damage=magic)
// (TheBomb.java:32) -- so `amount` alone cannot carry both. Panache needs no
// operand precisely because its two numbers coincide at the call site
// (ApplyPowerAction(p, p, PanachePower(p, magic), magic), Panache.java:32): its
// apply path reads the item's own `amount` as the damage. Authored as
// `counter:` on an APPLY_POWER step (MIRRORED in
// tools/registry_gen/stsgen/steps.py).
inline constexpr uint32_t kApplyPowerCounterShift = 16;
inline constexpr uint32_t kApplyPowerNotMonsterSourceBit = 1u << 15;

[[nodiscard]] constexpr uint32_t make_apply_power_flags(
    PowerId id, int counter = 0, bool is_source_monster = true) noexcept {
    return static_cast<uint32_t>(static_cast<uint16_t>(id)) |
           (is_source_monster ? 0u : kApplyPowerNotMonsterSourceBit) |
           (static_cast<uint32_t>(static_cast<uint16_t>(counter))
            << kApplyPowerCounterShift);
}
[[nodiscard]] constexpr PowerId apply_power_id_from_flags(uint32_t flags) noexcept {
    return static_cast<PowerId>(static_cast<uint16_t>(flags & 0x7FFFu));
}
[[nodiscard]] constexpr bool apply_power_is_source_monster(
    uint32_t flags) noexcept {
    return (flags & kApplyPowerNotMonsterSourceBit) == 0u;
}
[[nodiscard]] constexpr int apply_power_counter_from_flags(uint32_t flags) noexcept {
    return static_cast<int>(static_cast<int16_t>(
        static_cast<uint16_t>(flags >> kApplyPowerCounterShift)));
}

// PanachePower.CARD_AMT (PanachePower.java:27): the card countdown the power is
// constructed at (:34) and RESET to both on the 5th card (:56-57) and at the
// start of every turn (:64-67). A hard-coded 5 in the Java, so it is a constant
// here too rather than a registry column -- no card supplies it.
inline constexpr int16_t kPanacheCardAmount = 5;

// --- REDUCE_POWER / REMOVE_POWER instance key --------------------------------
//
// Both ops normally name their victim by PowerId and take the FIRST slot that
// matches -- correct while a power_id identifies at most one slot. An INSTANCED
// power (powers.yaml `instanced: true`) breaks that: The Bomb appends a fresh
// slot per play, so a creature can hold several at once with different fuses,
// and Java's ReducePowerAction has an exact-INSTANCE overload for precisely this
// case (ReducePowerAction.java:28-33 stores the AbstractPower itself; :41-51
// reduces or RemoveSpecificPowerAction's THAT object).
//
// A slot INDEX cannot be the handle. The end-of-turn sweep queues one reduce per
// bomb, in list order, and a reduce that empties its slot COMPACTS the array --
// so every later queued index is already stale by the time it resolves (bombs at
// [fuse 1, fuse 3]: the first reduce removes slot 0 and the second would then
// address an empty slot 1 and silently tick nothing).
//
// The handle is therefore the slot's own STATE: {power_id, amount, counter},
// captured when the reduce is queued. That is exact, not approximate. The only
// slots it cannot tell apart are ones with an identical id, amount AND counter,
// which are byte-identical rows -- interchangeable by construction, so resolving
// to either yields the same multiset and leaves the other for the sibling reduce.
// And nothing can change the captured amount in between: only a slot's own
// queued reduce writes it.
//
// Packing (flags): bits 0..15 PowerId (unchanged) | bits 16..23 the expected
// `amount` | bits 24..31 the expected `counter`. Bits 16..23 == 0 means NO
// instance key, which is exactly what every pre-existing REDUCE_POWER /
// REMOVE_POWER item packs -- a real key always has amount >= 1, because a slot
// standing at 0 has already been removed. Both fields are bytes: The Bomb's fuse
// is 1..3 and its damage 40/50, and a future instanced power whose numbers do
// not fit must widen this deliberately rather than silently truncate (the
// queueing helper asserts the range).
inline constexpr uint32_t kPowerInstanceAmountShift = 16;
inline constexpr uint32_t kPowerInstanceAmountMask = 0xFFu << kPowerInstanceAmountShift;
inline constexpr uint32_t kPowerInstanceCounterShift = 24;
inline constexpr uint32_t kPowerInstanceCounterMask = 0xFFu << kPowerInstanceCounterShift;

[[nodiscard]] constexpr bool power_instance_key_present(uint32_t flags) noexcept {
    return (flags & kPowerInstanceAmountMask) != 0u;
}
[[nodiscard]] constexpr int power_instance_amount(uint32_t flags) noexcept {
    return static_cast<int>((flags & kPowerInstanceAmountMask) >>
                            kPowerInstanceAmountShift);
}
[[nodiscard]] constexpr int power_instance_counter(uint32_t flags) noexcept {
    return static_cast<int>((flags & kPowerInstanceCounterMask) >>
                            kPowerInstanceCounterShift);
}
// Build the flags for an instance-targeted REDUCE_POWER / REMOVE_POWER. Values
// outside 1..255 / 0..255 cannot be expressed, and the caller gets a key-free
// (first-match) item rather than a wrong one -- callers are asserted, not
// silently truncated, at the one queueing site (power_the_bomb.cpp).
[[nodiscard]] constexpr uint32_t make_power_instance_flags(
    PowerId id, int slot_amount, int slot_counter) noexcept {
    if (slot_amount < 1 || slot_amount > 255 || slot_counter < 0 ||
        slot_counter > 255) {
        return static_cast<uint32_t>(static_cast<uint16_t>(id));
    }
    return static_cast<uint32_t>(static_cast<uint16_t>(id)) |
           (static_cast<uint32_t>(slot_amount) << kPowerInstanceAmountShift) |
           (static_cast<uint32_t>(slot_counter) << kPowerInstanceCounterShift);
}

// --- DAMAGE_GREED field encoding ---------------------------------------------
// `amount` is the base damage; `flags` is the gold banked when the hit kills
// (HandOfGreed magicNumber -> GreedAction.increaseGold, GreedAction.java:24-27).
// The same whole-word second-operand shape DAMAGE_FEED uses. Authored as `gold:`
// on the step (MIRRORED in tools/registry_gen/stsgen/steps.py).
[[nodiscard]] constexpr uint32_t make_damage_greed_flags(int gold) noexcept {
    return static_cast<uint32_t>(gold);
}
[[nodiscard]] constexpr int damage_greed_gold_from_flags(uint32_t flags) noexcept {
    return static_cast<int>(flags);
}

// --- STASIS_RETURN field encoding ---------------------------------------------
// `amount` is the stolen card's pool index (from the dying orb's Stasis slot
// counter, minus the +1 bias); `flags` bit 0 is the QUEUE-TIME destination
// decision StasisPower.onDeath makes -- set == HAND (`player.hand.size() != 10`,
// StasisPower.java:39), clear == DISCARD. The HAND arm still spills to the
// discard at RESOLVE if the hand has filled in between (MakeTempCardInHand-
// Action.update:71-77) -- the two reads happen at two different times and both
// are modelled.
inline constexpr uint32_t kStasisReturnToHandBit = 1u << 0;
[[nodiscard]] constexpr uint32_t make_stasis_return_flags(bool to_hand) noexcept {
    return to_hand ? kStasisReturnToHandBit : 0u;
}
[[nodiscard]] constexpr bool stasis_return_to_hand(uint32_t flags) noexcept {
    return (flags & kStasisReturnToHandBit) != 0u;
}

// --- DAMAGE damage-type encoding ---------------------------------------------
// DamageInfo.DamageType, carried in the DAMAGE opcode's `flags` low byte. The
// skeleton damage pipeline's power hooks are ALL NORMAL-gated in the Java
// (StrengthPower/WeakPower.atDamageGive, VulnerablePower.atDamageReceive each
// `if (type == NORMAL)`), so a THORNS / HP_LOSS DAMAGE takes NO power modifiers:
// a Vulnerable attacker does NOT amplify reflected Thorns damage, and player
// Strength/Weak do not scale it. `flags == 0 == NORMAL` keeps every existing
// DAMAGE item (card attacks, Explosive/Fire potions, Combust, Sadistic) byte-
// identical -- only a power/relic that queues a typed DAMAGE (Thorns) sets it.
// The value matches HookContext.damage_type (power_hooks.hpp) so a wasHPLost body
// (Plated Armor) can read the same type; interp.cpp static_asserts the parity.
enum class DamageType : uint8_t {
    NORMAL = 0,   // an ordinary attack -- the full applyPowers pipeline runs
    THORNS = 1,   // reflected/side damage -- skips the NORMAL-only power hooks
    HP_LOSS = 2,  // direct HP loss (the LOSE_HP opcode's implied type)
};
[[nodiscard]] constexpr uint32_t make_damage_flags(DamageType t) noexcept {
    return static_cast<uint32_t>(static_cast<uint8_t>(t));
}
[[nodiscard]] constexpr DamageType damage_type_from_flags(uint32_t flags) noexcept {
    return static_cast<DamageType>(static_cast<uint8_t>(flags & 0xFFu));
}

// --- DAMAGE pure-matrix / null-source bits (flags bits 8..9) -----------------
// Two INDEPENDENT properties the DamageType byte cannot carry, split exactly
// as the Java splits them:
//   * kDamagePure -- DamageInfo.createDamageMatrix(amount, /*isPureDamage=*/
//     true) (DamageInfo.java:126-136) never calls info.applyPowers, so the
//     landed number is the raw base: no attacker atDamageGive (Strength/Weak),
//     no target atDamageReceive (Vulnerable), no stance, no final passes.
//     op_damage skips compute_damage entirely for a pure item -- no float op
//     runs at all, so the FP contract for the surviving (non-pure) paths is
//     untouched.
//   * kDamageNullSource -- the DamageInfo was built with a NULL owner
//     (DamageAllEnemiesAction(null, ...), ExplosivePotion.java:52). The
//     victim's onAttacked loop still RUNS in the game (AbstractPlayer.damage:
//     1425-1426 / AbstractMonster.damage:667 iterate unconditionally); it is
//     each power BODY's `info.owner != null` gate that fails (CurlUpPower.
//     java:38, ThornsPower.java:52, FlameBarrierPower.java:54, AngryPower.
//     java:36), so op_damage still dispatches ON_ATTACKED and the bodies read
//     HookContext::source_null -- a future owner-INsensitive body fires,
//     exactly as it would in the game. Relic sites whose Java gate includes an
//     owner test (Boot's `info.owner == player` call site, Torii.java:32's
//     `info.owner != null`) test the bit directly in op_damage's helpers.
// They are orthogonal because the game combines them freely: Explosive Potion
// is pure + null-source NORMAL; Panache/The Bomb are pure matrices with a
// REAL owner (typed THORNS at their call sites); Fire Potion's THORNS item
// keeps a real owner and needs neither bit.
// Bits 8..9 were claimed from the free bits 8+ by the wave2-engine track; no
// other DAMAGE-item flag bits are allocated above the DamageType byte.
inline constexpr uint32_t kDamagePure = 1u << 8;
inline constexpr uint32_t kDamageNullSource = 1u << 9;
[[nodiscard]] constexpr bool damage_is_pure(uint32_t flags) noexcept {
    return (flags & kDamagePure) != 0u;
}
[[nodiscard]] constexpr bool damage_source_is_null(uint32_t flags) noexcept {
    return (flags & kDamageNullSource) != 0u;
}
[[nodiscard]] constexpr uint32_t make_damage_flags(DamageType t, bool pure,
                                                   bool null_source) noexcept {
    return make_damage_flags(t) | (pure ? kDamagePure : 0u) |
           (null_source ? kDamageNullSource : 0u);
}

// --- BLOCK opcode: skip the owner's block-modifier powers (Dexterity) --------
// DexterityPower.modifyBlock (+amount, floor 0) is applied by AbstractCard.
// applyPowers, so ONLY card block gets Dexterity; a power/relic/potion block calls
// GainBlockAction directly (no modifyBlock). Those direct-block emitters set
// kBlockNoPowers in the BLOCK item's `flags` and op_block skips the Dexterity add;
// a card BLOCK step carries flags == 0 (no bit) and Dexterity applies. A BLOCK
// item never otherwise uses `flags`, so bit 0 is free and flags == 0 (the
// pre-existing card path) stays unchanged.
inline constexpr uint32_t kBlockNoPowers = 1u << 0;

// --- libGDX MathUtils.floor (MathUtils.java:217) ----------------------------
// Replicated exactly so the DAMAGE floor matches the game bit-for-bit. A true
// floor for |value| < 16384 (always true for skeleton damage); NOT round.
[[nodiscard]] inline int mathutils_floor(float value) noexcept {
    return static_cast<int>(static_cast<double>(value) + 16384.0) - 16384;
}

// --- libGDX MathUtils.round (MathUtils.java:233-235) ------------------------
// The sibling of the floor above, replicated with the same bias trick and the
// same float->double promotion order: `(int)((double)value + 16384.5) - 16384`.
// It is a half-up round for |value| < 16384, NOT the C `std::round`'s
// half-away-from-zero. Two callers, from different layers: Magic Flower's
// in-combat heal multiplier (MagicFlower.java:34, relic_hooks.cpp) and the
// run-setup 90 %-of-max HP rewrite (run_advance.hpp run_setup_hp, where 90 % of
// 75 lands on exactly 67.5f and the half-up tie is what makes an ascension-20
// Ironclad 68/75). `constexpr` for that second caller, which is itself
// constexpr; the promotion order and bias are unchanged.
[[nodiscard]] constexpr int mathutils_round(float value) noexcept {
    return static_cast<int>(static_cast<double>(value) + 16384.5) - 16384;
}

// --- DAMAGE pipeline (pure) -------------------------------------------------
// Runs DamageInfo.applyPowers (design doc §5.5) for an attack from `src_actor`
// onto `tgt_actor` with `base_damage`, returning the floored, clamped(>=0)
// output WITHOUT applying it. Exposed pure so tier-2 tests can check the
// hand-computed damage number directly, free of block/hp interference; the
// DAMAGE opcode calls this and then lands the result on tgt. All accumulation
// is in float (trap 1: no integer shortcuts).
[[nodiscard]] int compute_damage(const CombatState& state, uint8_t src_actor,
                                 uint8_t tgt_actor, int base_damage) noexcept;

// As compute_damage, but the attacker's Strength contributes `strength_mult` x
// its stacks in the atDamageGive pass (Heavy Blade: strength.amount *= magicNumber
// before applyPowers, /= after; HeavyBlade.java:47-56). strength_mult == 1
// reproduces compute_damage bit-for-bit (float * 1.0f is exact). Exposed pure so
// the Heavy Blade tier-2 test can check the hand-computed number directly.
[[nodiscard]] int compute_damage(const CombatState& state, uint8_t src_actor,
                                 uint8_t tgt_actor, int base_damage,
                                 int strength_mult) noexcept;

// --- CHOOSE_CARD queries -----------------------------------------------------
// Shared by the pump (block-or-auto decision), legal_actions (which hand slots
// the player may CHOOSE), and advance (validating a CHOOSE action). Pure reads.
//
// NOTE: there is deliberately NO "excluded just-played card" parameter here any
// more. The played card is in the `limbo` pile while its own CHOOSE_CARD
// resolves (AbstractPlayer.useCard:1369-1375: cardInUse until the queued
// UseCardAction files it), so a discard-source choice (Headbutt) simply cannot
// see it in the discard -- the per-item `choice_excluded_index` compensation
// this replaced is folded into the general limbo model (USE_CARD above).

// Is slot `slot` of the kind's SOURCE pile (hand, or discard for
// discard-to-draw-top) a legal selection for a CHOOSE_CARD of the given kind?
// UPGRADE mirrors AbstractCard.canUpgrade (AbstractCard.java:672-680): a CURSE
// or a STATUS is never eligible (Writhe / Wound / Burn / Slimed all reach the
// hand in combat), otherwise the card must be un-upgraded; Searing Blow
// overrides canUpgrade to always-true (SearingBlow.java:58-60) and so stays
// eligible when already upgraded. EXHAUST /
// PUT_ON_DRAW_TOP accept any hand card; DISCARD_TO_DRAW_TOP accepts any discard
// card; DUPLICATE (Dual Wield) accepts ATTACK or POWER hand cards only
// (DualWieldAction.isDualWieldable:95-97).
// DRAW_TO_HAND additionally filters by CardType: `type_filter` is the raw
// CardType byte a draw-source choice was authored with
// (choose_type_filter_from_flags of the open item), and every caller that holds
// the item must pass it -- SkillFromDeckToHandAction and
// AttackFromDeckToHandAction share this kind and differ ONLY in that type, so
// defaulting it would silently make every draw-pile card eligible for both.
[[nodiscard]] bool choice_slot_eligible(
    const CombatState& state, uint8_t slot, ChoiceKind kind,
    uint8_t type_filter = kChoiceNoTypeFilter) noexcept;

// Does the CHOOSE_CARD `item` (assumed at the front of the action queue) require
// a real player prompt? True iff it selects fewer cards than are eligible AND it
// is not RANDOM (a RANDOM or forced-all selection auto-resolves at execute time).
// The pump BLOCKS (phase WAITING_ON_USER) exactly when this is true.
//
// An OPTIONAL item is different in kind, and deliberately so: it blocks whenever
// the source pile was non-empty when it opened, INCLUDING when every eligible
// card has already been picked, because the screen is ended by the confirm
// button and by nothing else. The Java's only short-circuit is the empty hand
// (ExhaustAction.java:76-79 `p.hand.size() == 0`; ForethoughtAction.java:33-36
// `p.hand.isEmpty()`), which is exactly "no cards to show".
[[nodiscard]] bool choice_requires_user(const CombatState& state,
                                        const ActionQueueItem& item) noexcept;

// --- OPTIONAL (zero-to-N) selection -------------------------------------------
//
// The three operations an open optional screen supports. `item` is the open
// CHOOSE_CARD at the action-queue head; all three read and write its selected
// nibble (kChoiceSelectedShift) and the hand suffix it describes.

// Is `slot` a legal CHOOSE on an open optional item -- i.e. does toggling it do
// something? An UNSELECTED hand slot is legal while the pick count is below the
// item's `amount` (HandCardSelectScreen.selectHoveredCard's `numCardsToSelect >
// selectedCards.size()` gate, :375); a SELECTED slot (one in the suffix) is
// always legal, because deselecting is never capped.
[[nodiscard]] bool optional_choice_slot_legal(const CombatState& state,
                                              const ActionQueueItem& item,
                                              uint8_t slot) noexcept;

// Toggle hand slot `slot` in or out of the open item's selection, moving the
// card within `hand` exactly as the screen moves it between the two groups and
// updating the item's selected nibble. Precondition: optional_choice_slot_legal.
void toggle_optional_choice_slot(CombatState& state, ActionQueueItem& item,
                                 uint8_t slot) noexcept;

// The confirm button: apply the item's manipulation to the accumulated picks IN
// PICK ORDER, then leave the hand as the remaining prefix. An EMPTY selection is
// legal and does nothing at all -- for Purity that is "exhaust no cards", for
// upgraded Forethought "move no cards", and in both the Java simply walks an
// empty selectedCards.group (ExhaustAction.java:102-108,
// ForethoughtAction.java:55-64). The caller pops the item afterwards.
void resolve_optional_choice(CombatState& state,
                             const ActionQueueItem& item) noexcept;

// Apply one player-selected CHOOSE_CARD manipulation to hand slot `slot`
// (advance's CHOOSE dispatch). exhaust / put-on-draw-top / upgrade / duplicate
// the card. Precondition (checked by the caller): slot < hand_count and the
// slot is a legal selection for `kind`.
//
// DUPLICATE only: `copies` is the Dual Wield magicNumber
// (choose_copies_from_flags of the open item), and `prompted` distinguishes a
// real hand-select resolution from the forced/auto path -- the PROMPTED branch
// reproduces DualWieldAction's screen bookkeeping (:59-84): the hand is
// reordered to [other eligibles] + [ineligibles] + [selected], THEN `copies`
// stat-equivalent clones are appended (the game consumes the original and adds
// 1+copies fresh makeStatEquivalentCopies at the end -- identical observable
// state). The FORCED branch (:49-57) appends the clones with NO reorder. Both
// spill past a full hand to the discard (MakeTempCardInHandAction:71-77).
void apply_choice_selection(CombatState& state, uint8_t slot, ChoiceKind kind,
                            int copies = 1, bool prompted = false) noexcept;

// Queue GamblingChipAction (GamblingChipAction.java:39-63) -- the optional
// zero-to-all hand select whose confirm discards every pick and then draws
// exactly that many. add_to_bottom, matching both call sites' addToBot.
//
// EXPOSED AS A SHARED BUILDER because the action has TWO consumers that are one
// boolean apart, and that boolean is presentation-only: Gambler's Brew passes
// notChip = true (GamblersBrew.java:39) and Gambling Chip passes false
// (GamblingChip.java:40), which selects only the prompt string (:42-46). Writing
// the item twice is how the same discard-then-draw-back logic would end up
// spelled two ways.
//
// The differences that ARE real live at the CALL sites, not here: Gambler's Brew
// carries its own `!hand.isEmpty()` guard (GamblersBrew.java:38) and so queues
// nothing at all on an empty hand, and Gambling Chip fires at most ONCE PER
// COMBAT off a private `activated` latch (GamblingChip.java:31/36-37).
void queue_gambling_chip_choice(CombatState& state) noexcept;

// DISCOVERY choice lifecycle. prepare_discovery_choice rejection-samples and
// persists the three-card offer exactly once. resolve_discovery_choice creates
// the selected base copy at cost 0 for this turn; the caller pops the completed
// queue item and resumes the pump.
// Draw-source CHOOSE_CARD lifecycle. Called by the pump on every CHOOSE_CARD it
// meets at the queue head, BEFORE the block-or-auto decision. For a DRAW-source
// kind whose kChoiceDrawTempBuiltBit is still clear it replays
// SkillFromDeckToHandAction / AttackFromDeckToHandAction's temp-group build
// (:34-39) purely for its card_random_rng cost -- k matching draw-pile cards
// spend k-1 draws (CardGroup.addToRandomSpot, :463-469) -- and sets the latch.
// The resulting browse ORDER is deliberately discarded: nothing observable
// depends on it, because a selection names a real draw-pile slot and the real
// draw pile is never reordered. Every other kind is untouched.
void prepare_choice_draw_source(CombatState& state,
                                ActionQueueItem& item) noexcept;

void prepare_discovery_choice(CombatState& state,
                              ActionQueueItem& item) noexcept;
void resolve_discovery_choice(CombatState& state,
                              const ActionQueueItem& item,
                              uint8_t slot) noexcept;

// Burn `regens` full offer regenerations WITHOUT storing any of them --
// DiscoveryAction.update's wasted work. generateCardChoices runs at the TOP of
// every update tick (DiscoveryAction.java:47, OUTSIDE the duration branch), so
// every tick after the first re-rolls a complete three-distinct-card offer that
// nothing reads. Each burned regeneration is the FULL rejection sampler --
// duplicates re-roll and cost their draw -- against the item's own pool. See
// kDiscoveryWastedRegens (below the DISCOVERY item helpers, this header) for
// why the tick count is a fixed 1 + 5 under the oracle contract, and the
// capture table pinning it.
void discard_discovery_regens(CombatState& state, const ActionQueueItem& item,
                              int regens) noexcept;

// --- Dispatch ----------------------------------------------------------------
// Execute one popped ActionQueueItem against `state`. One case per Opcode;
// NOP and any unrecognized opcode are safe no-ops (see decision (3)). This is
// what pump_step() invokes on every action_queue / pre_turn item it pops.
void execute_opcode(CombatState& state, const ActionQueueItem& item) noexcept;

}  // namespace sts::engine
