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
//     1.5f, NORMAL only (the Odd Mushroom 1.25f / Paper Frog 1.75f relic
//     branches are unreachable in the relic-free skeleton).
//   * WeakPower.atDamageGive (WeakPower.java:61-70) -- damage * 0.75f, NORMAL
//     only (the Paper Crane 0.6f branch is unreachable here).
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
//     without one. Most natives (Jaw Worm, B3.13/B3.14 monsters) roll DIRECTLY
//     inside their MonsterTurnFn -- their queues never carry ROLL_MOVE items.
//     The B3.17 large slimes queue real ROLL_MOVE items because the game's
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
//     conflict. Stage B B3.1 appends MAKE_CARD == 9 and SET_COST == 10 (the
//     append-from-9 rule); gen.py's OPCODES and the cards.hpp drift pin are
//     extended to cover them.
//
// (4) DYNAMIC TARGETS (Stage B B3.1). An item's `tgt` may be a sentinel
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
    // --- Stage B B3.1 additions (append-only from 9, design doc §4.4) ---
    MAKE_CARD = 9,    // create `amount` copies of CardId(flags low16) into pile(src)
    SET_COST = 10,    // set card_pool[src].cost_now = `amount` (temporary cost modifier)
    // --- Stage B B3.2 addition (append-only) ---
    LOSE_HP = 11,     // tgt loses `amount` HP directly (bypasses block; HP_LOSS
                       // type). Fires wasHPLost with source == tgt (self). The
                       // firing site for the Rupture attribution (power_hooks).
    // --- Stage B B3.4 additions (append-only from 12) ---
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
                       // ArmamentsAction's "no screen" branches.
    PLAY_TOP_DRAW = 13, // play the top card of the draw pile, then exhaust it
                         // (Havoc / PlayTopCardAction.update). Rolls one
                         // card_random_rng draw for the played card's random-monster
                         // target (getRandomMonster), reshuffles an empty draw pile,
                         // and plays the card free (cost 0) with exhaust-on-use.
    REMOVE_POWER = 14, // remove PowerId(flags low16) from `tgt`'s power list
                        // (RemoveSpecificPowerAction). Used by LoseStrengthPower's
                        // end-of-turn self-removal (Flex).
    // --- Stage B B3.3 additions (append-only from 15): dynamic-base attack damage.
    DAMAGE_BLOCK = 15,  // src (player) attacks tgt for base == player_block, read
                         // at EXECUTE time, then the normal DamageInfo pipeline
                         // (Body Slam; BodySlam.java:96 baseDamage = p.currentBlock).
    DAMAGE_STR_MULT = 16, // src attacks tgt for `amount` base with Strength counted
                           // x `flags` (the multiplier), then the pipeline (Heavy
                           // Blade; HeavyBlade.java:426-435 strength.amount *= magic).
    DAMAGE_PER_STRIKE = 17, // deal `amount` + `extra`-per-"Strike"-card damage
                             // (Perfected Strike; PerfectedStrike.java:592-607). BAKED
                             // into a plain DAMAGE at QUEUE time (card_play.cpp), with
                             // the just-played source card excluded from the count --
                             // it is in limbo at applyPowers-at-use time. This opcode
                             // therefore never reaches execute_opcode (a safe no-op if
                             // it somehow does); it exists only as the table encoding.
    // --- Stage B B3.9 addition (append-only from 18) ---
    LOSE_HP_PER_HAND = 18,  // `tgt` loses HP == the current hand size, bypassing
                             // block (HP_LOSS type). Regret's end-of-turn self-loss
                             // (Regret.java:35-38: magicNumber = player.hand.size()
                             // locked at triggerOnEndOfTurnForPlayingCard). The hand
                             // count is read at EXECUTE; the end-of-turn card triggers
                             // are queued BEFORE the ethereal-exhaust sweep so the
                             // ethereals are still in hand when this resolves --
                             // matching the game's trigger-time value lock.
    DISCARD_HAND = 19,       // end-of-turn DiscardAtEndOfTurnAction collapse:
                             // exhaust ETHEREAL hand cards, then move the remaining
                             // non-RETAIN hand cards to discard. This is queued after
                             // all end-of-turn card/power effects, never invoked by a
                             // registry card program.
    REDUCE_POWER = 20,       // ReducePowerAction: subtract `amount` from
                             // PowerId(flags low16) on `tgt`, removing at zero.
                             // Frail queues this at end of round so power-list
                             // iteration completes before the slot can disappear.
    // --- Stage B B3.5 additions (append-only from 21) ---
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
    // --- Stage B B3.17 additions (append-only from 25): split framework ---
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
    SET_MOVE = 29,            // set monsters[tgt]'s decided move to `amount` with
                              // intent flags low8 -- pushes move history exactly
                              // like a direct setMove (SetMoveAction.java:52-56 ->
                              // AbstractMonster.setMove:431-437). No liveness
                              // check, matching the Java.
};

// --- CHOOSE_CARD field encoding (Stage B B3.4) ------------------------------
// The blocking hand-card select verb. `amount` carries how many cards to select;
// `flags` (the step's `extra`) packs the manipulation kind and the RANDOM bit:
//   * bits [0..1] -> ChoiceKind (what to do with each selected card).
//   * bit  [2]    -> RANDOM: pick with card_random_rng instead of prompting the
//                    player (ExhaustAction.isRandom -- True Grit base).
// MIRRORED in tools/registry_gen/gen.py (CHOICE_KINDS + the bit layout) so a
// CHOOSE_CARD effect step authored in cards.yaml packs an identical `extra`.
enum class ChoiceKind : uint8_t {
    EXHAUST = 0,         // move each selected hand card to the exhaust pile
    PUT_ON_DRAW_TOP = 1, // move each selected hand card to the top of the draw pile
    UPGRADE = 2,         // upgrade each selected hand card in place (upgrade++)
    // Stage B B3.3: the source pile is the DISCARD pile, not the hand. Choose a
    // card from the discard pile and put it on top of the draw pile (Headbutt /
    // DiscardPileToTopOfDeckAction: forced when discard has <= 1 card, a real
    // gridSelect prompt when >= 2). All other kinds source from the hand.
    DISCARD_TO_DRAW_TOP = 3,
};

// Does a CHOOSE_CARD of this kind select from the discard pile (vs. the hand)?
[[nodiscard]] constexpr bool choice_source_is_discard(ChoiceKind k) noexcept {
    return k == ChoiceKind::DISCARD_TO_DRAW_TOP;
}

inline constexpr uint32_t kChoiceRandomBit = 1u << 2;

[[nodiscard]] constexpr uint32_t make_choose_flags(ChoiceKind kind,
                                                   bool random) noexcept {
    return static_cast<uint32_t>(static_cast<uint8_t>(kind)) |
           (random ? kChoiceRandomBit : 0u);
}
[[nodiscard]] constexpr ChoiceKind choose_kind_from_flags(uint32_t flags) noexcept {
    return static_cast<ChoiceKind>(static_cast<uint8_t>(flags & 0x3u));
}
[[nodiscard]] constexpr bool choose_is_random(uint32_t flags) noexcept {
    return (flags & kChoiceRandomBit) != 0u;
}

// --- MAKE_CARD field encoding (Stage B B3.1) --------------------------------
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
// `flags` low 16 bits hold the PowerId; `amount` holds the stack count. Use
// these helpers so no caller hand-rolls the packing.
[[nodiscard]] constexpr uint32_t make_apply_power_flags(PowerId id) noexcept {
    return static_cast<uint32_t>(static_cast<uint16_t>(id));
}
[[nodiscard]] constexpr PowerId apply_power_id_from_flags(uint32_t flags) noexcept {
    return static_cast<PowerId>(static_cast<uint16_t>(flags & 0xFFFFu));
}

// --- DAMAGE damage-type encoding (discharges B3.2's deferred DAMAGE type item) -
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
// before applyPowers, /= after; HeavyBlade.java:426-435). strength_mult == 1
// reproduces compute_damage bit-for-bit (float * 1.0f is exact). Exposed pure so
// the Heavy Blade tier-2 test can check the hand-computed number directly.
[[nodiscard]] int compute_damage(const CombatState& state, uint8_t src_actor,
                                 uint8_t tgt_actor, int base_damage,
                                 int strength_mult) noexcept;

// --- CHOOSE_CARD queries (Stage B B3.4) -------------------------------------
// Shared by the pump (block-or-auto decision), legal_actions (which hand slots
// the player may CHOOSE), and advance (validating a CHOOSE action). Pure reads.

// "No card excluded" sentinel for a discard-source choice's `excluded` argument
// (a real pool index is 0..kCardPoolCap-1 == 0..159, so 255 can never collide).
inline constexpr uint8_t kNoChoiceExclusion = 255;

// The pool index a CHOOSE_CARD excludes from its SOURCE pile, or kNoChoiceExclusion.
// Discard-source choices (Headbutt) stamp the just-played card's pool index into
// the item's `tgt` (card_play.cpp): our resolve_card_play moves the played card to
// the discard pile BEFORE the queued choice resolves, but the game keeps it in
// limbo (cardInUse) -- the UseCardAction that discards it is queued AFTER the
// card's own DiscardPileToTopOfDeckAction (AbstractPlayer.useCard:1369-1375). So
// the just-played card must not be a discard-choice candidate. Hand-source choices
// carry no exclusion.
[[nodiscard]] constexpr uint8_t choice_excluded_index(
    const ActionQueueItem& item) noexcept {
    if (static_cast<Opcode>(item.opcode) == Opcode::CHOOSE_CARD &&
        choice_source_is_discard(choose_kind_from_flags(item.flags))) {
        return item.tgt;
    }
    return kNoChoiceExclusion;
}

// Is slot `slot` of the kind's SOURCE pile (hand, or discard for
// discard-to-draw-top) a legal selection for a CHOOSE_CARD of the given kind?
// UPGRADE requires an un-upgraded card (AbstractCard.canUpgrade: !upgraded, and
// non-CURSE/STATUS -- every card in scope is ATTACK/SKILL); EXHAUST /
// PUT_ON_DRAW_TOP accept any hand card; DISCARD_TO_DRAW_TOP accepts any discard
// card except `excluded` (the just-played source, in limbo in the game).
[[nodiscard]] bool choice_slot_eligible(
    const CombatState& state, uint8_t slot, ChoiceKind kind,
    uint8_t excluded = kNoChoiceExclusion) noexcept;

// Does the CHOOSE_CARD `item` (assumed at the front of the action queue) require
// a real player prompt? True iff it selects fewer cards than are eligible AND it
// is not RANDOM (a RANDOM or forced-all selection auto-resolves at execute time).
// The pump BLOCKS (phase WAITING_ON_USER) exactly when this is true.
[[nodiscard]] bool choice_requires_user(const CombatState& state,
                                        const ActionQueueItem& item) noexcept;

// Apply one player-selected CHOOSE_CARD manipulation to hand slot `slot`
// (advance's CHOOSE dispatch). exhaust / put-on-draw-top / upgrade the card.
// Precondition (checked by the caller): slot < hand_count and the slot is a
// legal selection for `kind`.
void apply_choice_selection(CombatState& state, uint8_t slot,
                            ChoiceKind kind) noexcept;

// --- Dispatch ----------------------------------------------------------------
// Execute one popped ActionQueueItem against `state`. One case per Opcode;
// NOP and any unrecognized opcode are safe no-ops (see decision (3)). This is
// what pump_step() invokes on every action_queue / pre_turn item it pops.
void execute_opcode(CombatState& state, const ActionQueueItem& item) noexcept;

}  // namespace sts::engine
