// B3.2 power-hook framework (tier-2, constructed states). The frozen stage-a
// §5.2/§5.3/§5.4/§5.5 dispatch order, the data-bound + native escape-hatch
// mechanisms, and the FIVE hook-order stress cases the scoping report flags,
// each hand-derived from the cited decompiled Java (read in full before coding):
//
//   ORDERING (acceptance): §5.3 onPlayCard fan-out queues player powers before
//   monster powers (GameActionManager.java:222-245).
//
//   STRESS 1 -- onExhaust list order (Feel No Pain + Dark Embrace): resolving on
//   one exhaust follows the player power-LIST order (§5.5;
//   CardGroup.moveToExhaustPile:851-856).
//   STRESS 2 -- onUseCard fan-out (Corruption redirect): a played SKILL is
//   redirected to exhaust by Corruption's onUseCard, and skills cost 0 on draw
//   (CorruptionPower.java:38-49; UseCardAction.java:41-64 fan-out).
//   STRESS 3 -- atEndOfTurn stack (Metallicize pre-card vs Combust): the §5.4
//   pre-card powers queue before the atEndOfTurn powers, so Metallicize's block
//   lands before Combust's HP loss/damage (GameActionManager:369-377;
//   AbstractCreature:548-553).
//   STRESS 4 -- APPLY_POWER interception (Artifact vs Sadistic): the SOURCE's
//   onApplyPower fires first (Sadistic), then the target-side Artifact nullify;
//   Artifact consumes the debuff so it never lands, and Sadistic's own Artifact
//   guard means it does NOT fire against an Artifact target
//   (ApplyPowerAction.java:106-138; SadisticPower.java:38-44).
//   STRESS 5 -- wasHPLost attribution (Rupture): Rupture grants Strength for
//   self-inflicted (card/LOSE_HP) HP loss, NOT for unblocked enemy damage
//   (RupturePower.java:60-66; AbstractPlayer.damage:1445-1447).

#include <cstdint>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/state_hash.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

// Add power `id` (stack `amt`) to an actor's power list.
void give_player_power(CombatState& s, PowerId id, int16_t amt) {
    s.player_powers[s.player_power_count].power_id = static_cast<uint16_t>(id);
    s.player_powers[s.player_power_count].amount = amt;
    ++s.player_power_count;
}
void give_monster_power(CombatState& s, uint8_t m, PowerId id, int16_t amt) {
    s.monsters[m].powers[s.monsters[m].power_count].power_id =
        static_cast<uint16_t>(id);
    s.monsters[m].powers[s.monsters[m].power_count].amount = amt;
    ++s.monsters[m].power_count;
}

// The i-th queued action_queue item, front-first (the ring is head..head+count).
ActionQueueItem queued(const CombatState& s, uint8_t i) {
    return s.action_queue[(s.action_head + i) % kActionQueueCap];
}

// Drain ONLY the main action queue (pop + execute), so a directed dispatch's
// queued effects resolve without triggering the monster turn / start-of-turn.
void drain_actions(CombatState& s) {
    ActionQueueItem it{};
    while (pop_action_front(s, it)) {
        execute_opcode(s, it);
    }
}

PowerSlot* monster_power(CombatState& s, uint8_t m, PowerId id) {
    for (uint8_t i = 0; i < s.monsters[m].power_count; ++i) {
        if (s.monsters[m].powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.monsters[m].powers[i];
        }
    }
    return nullptr;
}

// A minimal combat: one hand card (pool 0), one live monster, player-turn set.
CombatState MakeState(CardId id, uint8_t cost, int16_t monster_hp = 50) {
    CombatState s{};
    s.card_pool[0].card_id = static_cast<uint16_t>(id);
    s.card_pool[0].cost_now = cost;
    s.hand[0] = 0;
    s.hand_count = 1;
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = 3;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = monster_hp;
    s.monsters[0].max_hp = monster_hp;
    s.monster_attacks_queued = 1;
    s.turn_has_ended = 0;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

constexpr uint16_t kOp(Opcode o) { return static_cast<uint16_t>(o); }

// --- Regression invariant: dispatch is a no-op without a hook-bearing power ---

TEST(PowerHooks, NoBoundPowerQueuesNothing) {
    CombatState s{};
    s.monster_count = 1;
    s.monsters[0].hp = 30;
    // The skeleton's only powers bind zero hooks; a state carrying them must
    // dispatch nothing (so the 20 combat fixtures stay byte-identical).
    give_player_power(s, PowerId::STRENGTH, 2);
    give_monster_power(s, 0, PowerId::VULNERABLE, 1);

    dispatch_on_play_card(s, static_cast<uint16_t>(CardId::STRIKE), 0);
    dispatch_on_exhaust(s, static_cast<uint16_t>(CardId::STRIKE));
    dispatch_at_end_of_turn_pre_card(s);
    dispatch_at_end_of_turn(s);
    dispatch_at_start_of_turn(s);
    dispatch_at_start_of_turn_post_draw(s);
    dispatch_on_gained_block(s, kActorPlayer, 5);
    dispatch_was_hp_lost(s, kActorPlayer, kActorPlayer, 4);

    EXPECT_EQ(s.action_count, 0) << "no hook-bearing power -> nothing queued";
}

// --- ORDERING (acceptance): §5.3 player powers before monster powers ---------

TEST(PowerHooks, OnPlayCardFanOutPlayerBeforeMonster) {
    // Rage binds on_play_card (BLOCK self = power amount). Player Rage(2),
    // monster Rage(4): §5.3 fires PLAYER powers, then MONSTER powers -> the
    // player's block queues before the monster's.
    CombatState s{};
    s.monster_count = 1;
    s.monsters[0].hp = 30;
    give_player_power(s, PowerId::RAGE, 2);
    give_monster_power(s, 0, PowerId::RAGE, 4);

    dispatch_on_play_card(s, static_cast<uint16_t>(CardId::STRIKE), 0);

    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 0).tgt, kActorPlayer);   // player Rage first
    EXPECT_EQ(queued(s, 0).amount, 2);
    EXPECT_EQ(queued(s, 1).opcode, kOp(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 1).tgt, 0);              // monster 0 Rage second
    EXPECT_EQ(queued(s, 1).amount, 4);
}

// --- STRESS 1: onExhaust list order (Feel No Pain + Dark Embrace) ------------

TEST(PowerHooks, OnExhaustFollowsPlayerPowerListOrder) {
    // Feel No Pain (BLOCK self = amount) + Dark Embrace (DRAW self = amount)
    // resolving on ONE exhaust: the sequence == the player power-LIST order
    // (§5.5, application order), not the hook table order.
    CombatState a{};
    give_player_power(a, PowerId::FEEL_NO_PAIN, 3);   // applied first
    give_player_power(a, PowerId::DARK_EMBRACE, 1);   // applied second
    dispatch_on_exhaust(a, static_cast<uint16_t>(CardId::STRIKE));
    ASSERT_EQ(a.action_count, 2);
    EXPECT_EQ(queued(a, 0).opcode, kOp(Opcode::BLOCK));  // Feel No Pain first
    EXPECT_EQ(queued(a, 0).amount, 3);
    EXPECT_EQ(queued(a, 1).opcode, kOp(Opcode::DRAW));   // Dark Embrace second
    EXPECT_EQ(queued(a, 1).amount, 1);

    // Reverse the application order -> reversed resolution order.
    CombatState b{};
    give_player_power(b, PowerId::DARK_EMBRACE, 1);   // applied first
    give_player_power(b, PowerId::FEEL_NO_PAIN, 3);   // applied second
    dispatch_on_exhaust(b, static_cast<uint16_t>(CardId::STRIKE));
    ASSERT_EQ(b.action_count, 2);
    EXPECT_EQ(queued(b, 0).opcode, kOp(Opcode::DRAW));   // Dark Embrace first now
    EXPECT_EQ(queued(b, 1).opcode, kOp(Opcode::BLOCK));  // Feel No Pain second
}

// --- STRESS 2: onUseCard fan-out (Corruption redirect) ----------------------

TEST(PowerHooks, CorruptionRedirectsPlayedSkillToExhaust) {
    // A played SKILL (Shrug It Off) with Corruption in play is exhausted instead
    // of discarded (CorruptionPower.onUseCard). The redirect is read AFTER the
    // onUseCard fan-out, so it takes effect on the card move.
    CombatState s = MakeState(CardId::SHRUG_IT_OFF, /*cost=*/1);
    give_player_power(s, PowerId::CORRUPTION, 1);

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    EXPECT_EQ(s.exhaust_count, 1) << "corrupted skill exhausts";
    EXPECT_EQ(s.exhaust[0], 0);
    EXPECT_EQ(s.discard_count, 0) << "and does NOT go to discard";
}

TEST(PowerHooks, CorruptionDoesNotRedirectAttacks) {
    // Corruption only touches SKILLs; an ATTACK (Strike) still discards.
    CombatState s = MakeState(CardId::STRIKE, /*cost=*/1);
    give_player_power(s, PowerId::CORRUPTION, 1);

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    EXPECT_EQ(s.exhaust_count, 0);
    EXPECT_EQ(s.discard_count, 1) << "attacks are unaffected by Corruption";
}

TEST(PowerHooks, CorruptionZeroesDrawnSkillCost) {
    // onCardDraw: a drawn SKILL costs 0 this turn; a drawn ATTACK is unchanged.
    CombatState s{};
    give_player_power(s, PowerId::CORRUPTION, 1);
    // Draw pile (top == end): [Strike, ShrugItOff] -> both drawn.
    s.card_pool[0].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.card_pool[0].cost_now = 1;
    s.card_pool[1].card_id = static_cast<uint16_t>(CardId::SHRUG_IT_OFF);
    s.card_pool[1].cost_now = 1;
    s.draw[0] = 0;
    s.draw[1] = 1;
    s.draw_count = 2;

    execute_opcode(s, [] {
        ActionQueueItem it{};
        it.opcode = kOp(Opcode::DRAW);
        it.src = kActorPlayer;
        it.tgt = kActorPlayer;
        it.amount = 2;
        return it;
    }());

    EXPECT_EQ(s.card_pool[1].cost_now, 0) << "drawn skill costs 0 (Corruption)";
    EXPECT_EQ(s.card_pool[0].cost_now, 1) << "drawn attack cost unchanged";
}

// --- STRESS 3: atEndOfTurn stack (Metallicize pre-card vs Combust) -----------

TEST(PowerHooks, EndOfTurnPreCardPowersBeforeAtEndOfTurnPowers) {
    // §5.4: applyEndOfTurnPreCardPowers (Metallicize) queues BEFORE the
    // atEndOfTurn powers (Combust). Drive the end-turn sentinel through pump_step
    // and inspect the resulting queue order, then resolve just those actions.
    CombatState s{};
    s.player_hp = 50;
    s.player_max_hp = 50;
    s.monster_count = 1;
    s.monsters[0].hp = 40;
    s.monsters[0].max_hp = 40;
    give_player_power(s, PowerId::METALLICIZE, 3);  // at_end_of_turn_pre_card
    give_player_power(s, PowerId::COMBUST, 5);      // at_end_of_turn
    s.card_queue[0] = make_end_turn_sentinel();
    s.card_queue_count = 1;
    s.monster_attacks_queued = 1;  // keep step 4 from firing before the sentinel

    const PumpStepResult r = pump_step(s, default_monster_turn);
    ASSERT_EQ(r.outcome, PumpOutcome::END_TURN_SENTINEL);

    // Queue order: Metallicize BLOCK(3) -> Combust LOSE_HP(1) -> Combust DAMAGE(5).
    ASSERT_EQ(s.action_count, 3);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 0).tgt, kActorPlayer);
    EXPECT_EQ(queued(s, 0).amount, 3);
    EXPECT_EQ(queued(s, 1).opcode, kOp(Opcode::LOSE_HP));   // Combust self HP loss
    EXPECT_EQ(queued(s, 1).amount, 1);
    EXPECT_EQ(queued(s, 2).opcode, kOp(Opcode::DAMAGE));    // Combust AoE
    EXPECT_EQ(queued(s, 2).tgt, kActorAllEnemies);
    EXPECT_EQ(queued(s, 2).amount, 5);

    drain_actions(s);
    EXPECT_EQ(s.player_block, 3);      // Metallicize
    EXPECT_EQ(s.player_hp, 50 - 1);    // Combust HP loss
    EXPECT_EQ(s.monsters[0].hp, 40 - 5);  // Combust AoE
}

// --- STRESS 4: APPLY_POWER interception (Artifact vs Sadistic) ---------------

ActionQueueItem apply_power_item(uint8_t src, uint8_t tgt, PowerId id,
                                 int32_t stacks) {
    ActionQueueItem it{};
    it.opcode = kOp(Opcode::APPLY_POWER);
    it.src = src;
    it.tgt = tgt;
    it.amount = stacks;
    it.flags = make_apply_power_flags(id);
    return it;
}

TEST(PowerHooks, SadisticFiresWhenPlayerDebuffsUnprotectedTarget) {
    // Source-side onApplyPower fires first: applying a DEBUFF to a monster with no
    // Artifact makes Sadistic queue damage on that monster, AND the debuff still
    // lands. Uses WEAK (a debuff that alters the target's OUTGOING damage, not
    // incoming) rather than Vulnerable: Sadistic's damage is THORNS-typed in the
    // game (Vulnerable, a NORMAL-only receive hook, does not boost it), but the
    // DAMAGE opcode does not yet carry a damage-type (deferred -- see the Log), so
    // a Vulnerable target would over-count the queued damage here. WEAK keeps the
    // check on the DISPATCH/ordering the framework owns, not damage typing.
    CombatState s{};
    s.monster_count = 1;
    s.monsters[0].hp = 30;
    give_player_power(s, PowerId::SADISTIC, 5);

    execute_opcode(s, apply_power_item(kActorPlayer, 0, PowerId::WEAK, 2));

    // Weak landed (the debuff is applied normally).
    const PowerSlot* weak = monster_power(s, 0, PowerId::WEAK);
    ASSERT_NE(weak, nullptr);
    EXPECT_EQ(weak->amount, 2);
    // Sadistic queued 5 damage on the target (source-side onApplyPower).
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::DAMAGE));
    EXPECT_EQ(queued(s, 0).tgt, 0);
    EXPECT_EQ(queued(s, 0).amount, 5);
    drain_actions(s);
    EXPECT_EQ(s.monsters[0].hp, 30 - 5);
}

TEST(PowerHooks, ArtifactNullifiesDebuffAndBeatsSadistic) {
    // Same setup but the target has Artifact: the debuff is NULLIFIED (never
    // lands, one Artifact stack consumed), and Sadistic does NOT fire (its own
    // guard skips an Artifact target) -- Artifact wins on both sides of the op.
    CombatState s{};
    s.monster_count = 1;
    s.monsters[0].hp = 30;
    give_monster_power(s, 0, PowerId::ARTIFACT, 1);
    give_player_power(s, PowerId::SADISTIC, 5);

    execute_opcode(s, apply_power_item(kActorPlayer, 0, PowerId::VULNERABLE, 2));

    EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE), nullptr)
        << "debuff nullified by Artifact -- never lands";
    const PowerSlot* art = monster_power(s, 0, PowerId::ARTIFACT);
    ASSERT_NE(art, nullptr);
    EXPECT_EQ(art->amount, 0) << "one Artifact stack consumed";
    EXPECT_EQ(s.action_count, 0) << "Sadistic does NOT fire against an Artifact target";
}

TEST(PowerHooks, ArtifactDoesNotBlockBuffs) {
    // Artifact only nullifies DEBUFFs; a BUFF (Strength) applies normally and
    // does not consume a charge.
    CombatState s{};
    s.monster_count = 1;
    s.monsters[0].hp = 30;
    give_monster_power(s, 0, PowerId::ARTIFACT, 1);

    execute_opcode(s, apply_power_item(kActorPlayer, 0, PowerId::STRENGTH, 3));

    const PowerSlot* str = monster_power(s, 0, PowerId::STRENGTH);
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->amount, 3) << "buff lands";
    EXPECT_EQ(monster_power(s, 0, PowerId::ARTIFACT)->amount, 1)
        << "Artifact not consumed by a buff";
}

// --- STRESS 5: wasHPLost attribution (Rupture) ------------------------------

TEST(PowerHooks, RuptureFiresOnSelfInflictedHpLoss) {
    // LOSE_HP on the player (source == self) is card/self HP loss -> Rupture
    // grants Strength (RupturePower.wasHPLost, info.owner == owner).
    CombatState s{};
    s.player_hp = 40;
    s.player_max_hp = 40;
    give_player_power(s, PowerId::RUPTURE, 1);

    ActionQueueItem lose{};
    lose.opcode = kOp(Opcode::LOSE_HP);
    lose.tgt = kActorPlayer;
    lose.amount = 3;
    execute_opcode(s, lose);

    EXPECT_EQ(s.player_hp, 40 - 3);
    ASSERT_EQ(s.action_count, 1) << "Rupture queued its Strength gain";
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::APPLY_POWER));
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::STRENGTH);
    EXPECT_EQ(queued(s, 0).tgt, kActorPlayer);
    EXPECT_EQ(queued(s, 0).amount, 1);
    drain_actions(s);
    const PowerSlot* str = &s.player_powers[0];
    (void)str;
    // Strength now present on the player (1).
    bool has_str = false;
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(PowerId::STRENGTH)) {
            has_str = s.player_powers[i].amount == 1;
        }
    }
    EXPECT_TRUE(has_str);
}

TEST(PowerHooks, RuptureDoesNotFireOnUnblockedEnemyDamage) {
    // Unblocked ENEMY attack damage (source == the monster, not the player) does
    // NOT grant Strength -- the info.owner == owner attribution guard.
    CombatState s{};
    s.player_hp = 40;
    s.player_max_hp = 40;
    s.monster_count = 1;
    s.monsters[0].hp = 30;
    give_player_power(s, PowerId::RUPTURE, 1);

    ActionQueueItem hit{};
    hit.opcode = kOp(Opcode::DAMAGE);
    hit.src = 0;               // monster 0 attacks
    hit.tgt = kActorPlayer;
    hit.amount = 6;
    execute_opcode(s, hit);

    EXPECT_EQ(s.player_hp, 40 - 6) << "damage landed";
    EXPECT_EQ(s.action_count, 0) << "Rupture does NOT fire for enemy damage";
}

// --- potion-support-powers follow-up: the six new powers --------------------

// Player power stack amount, or -1 if absent (a removed slot reads as absent).
int player_power_stack(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return s.player_powers[i].amount;
        }
    }
    return -1;
}

// DEXTERITY: modifyBlock adds to CARD block only; power/relic/potion block (the
// kBlockNoPowers items) is a direct GainBlockAction and does NOT get Dexterity.
TEST(PowerHooks, DexterityBoostsCardBlockNotDirectBlock) {
    CombatState s{};
    s.player_hp = 50;
    s.player_max_hp = 50;
    give_player_power(s, PowerId::DEXTERITY, 3);
    ActionQueueItem card_blk{};
    card_blk.opcode = kOp(Opcode::BLOCK);
    card_blk.tgt = kActorPlayer;
    card_blk.amount = 5;
    execute_opcode(s, card_blk);
    EXPECT_EQ(s.player_block, 8) << "card block gets Dexterity (5 + 3)";
    ActionQueueItem power_blk{};
    power_blk.opcode = kOp(Opcode::BLOCK);
    power_blk.tgt = kActorPlayer;
    power_blk.amount = 5;
    power_blk.flags = kBlockNoPowers;
    execute_opcode(s, power_blk);
    EXPECT_EQ(s.player_block, 8 + 5) << "direct GainBlockAction block skips Dexterity";
}

// Integration: Metallicize's end-of-turn block is queued with kBlockNoPowers, so a
// Dexterity-holding player gains exactly the Metallicize amount (no leak).
TEST(PowerHooks, DexterityDoesNotBoostMetallicizeBlock) {
    CombatState s{};
    s.player_hp = 50;
    s.player_max_hp = 50;
    give_player_power(s, PowerId::DEXTERITY, 3);
    give_player_power(s, PowerId::METALLICIZE, 4);
    dispatch_at_end_of_turn_pre_card(s);
    drain_actions(s);
    EXPECT_EQ(s.player_block, 4) << "Metallicize block is direct -> no Dexterity";
}

// Negative Dexterity reduces card block, floored at 0 (modifyBlock's clamp).
TEST(PowerHooks, NegativeDexterityFloorsCardBlockAtZero) {
    CombatState s{};
    s.player_hp = 50;
    s.player_max_hp = 50;
    give_player_power(s, PowerId::DEXTERITY, -3);
    ActionQueueItem blk{};
    blk.opcode = kOp(Opcode::BLOCK);
    blk.tgt = kActorPlayer;
    blk.amount = 2;  // 2 + (-3) = -1 -> floored to 0
    execute_opcode(s, blk);
    EXPECT_EQ(s.player_block, 0) << "negative Dexterity can't drive block below 0";
}

// LOSE_DEXTERITY (Speed): at end of turn applies Dexterity -amount and removes
// itself -- the exact mirror of Flex/LoseStrength. Net Dexterity 0 this turn.
TEST(PowerHooks, LoseDexterityReversesDexterityAtEndOfTurn) {
    CombatState s{};
    s.player_hp = 50;
    s.player_max_hp = 50;
    give_player_power(s, PowerId::DEXTERITY, 5);
    give_player_power(s, PowerId::LOSE_DEXTERITY, 5);
    dispatch_at_end_of_turn(s);
    ASSERT_EQ(s.action_count, 2) << "APPLY_POWER Dexterity -5 then REMOVE_POWER";
    drain_actions(s);
    EXPECT_EQ(player_power_stack(s, PowerId::DEXTERITY), 0) << "Dexterity reversed to 0";
    EXPECT_EQ(player_power_stack(s, PowerId::LOSE_DEXTERITY), -1) << "LoseDexterity removed";
}

// THORNS: reflects `amount` THORNS damage to the attacker on a NORMAL attack.
TEST(PowerHooks, ThornsReflectsDamageToAttacker) {
    CombatState s{};
    s.player_hp = 50;
    s.player_max_hp = 50;
    s.monster_count = 1;
    s.monsters[0].hp = 30;
    s.monsters[0].max_hp = 30;
    give_player_power(s, PowerId::THORNS, 3);
    ActionQueueItem hit{};
    hit.opcode = kOp(Opcode::DAMAGE);
    hit.src = 0;
    hit.tgt = kActorPlayer;
    hit.amount = 6;
    execute_opcode(s, hit);
    EXPECT_EQ(s.player_hp, 50 - 6) << "attack landed";
    ASSERT_EQ(s.action_count, 1) << "Thorns queued reflected damage";
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::DAMAGE));
    EXPECT_EQ(queued(s, 0).tgt, 0) << "reflected at the attacker";
    EXPECT_EQ(queued(s, 0).amount, 3);
    EXPECT_EQ(damage_type_from_flags(queued(s, 0).flags), DamageType::THORNS);
    drain_actions(s);
    EXPECT_EQ(s.monsters[0].hp, 30 - 3) << "attacker took thorns damage";
}

// Thorns reflects even a FULLY BLOCKED hit (onAttacked has no damage>0 gate).
TEST(PowerHooks, ThornsFiresEvenWhenAttackFullyBlocked) {
    CombatState s{};
    s.player_hp = 50;
    s.player_max_hp = 50;
    s.player_block = 10;
    s.monster_count = 1;
    s.monsters[0].hp = 30;
    give_player_power(s, PowerId::THORNS, 3);
    ActionQueueItem hit{};
    hit.opcode = kOp(Opcode::DAMAGE);
    hit.src = 0;
    hit.tgt = kActorPlayer;
    hit.amount = 6;
    execute_opcode(s, hit);
    EXPECT_EQ(s.player_hp, 50) << "fully blocked";
    EXPECT_EQ(s.player_block, 4);
    ASSERT_EQ(s.action_count, 1) << "Thorns still reflects on a blocked hit";
    drain_actions(s);
    EXPECT_EQ(s.monsters[0].hp, 30 - 3);
}

// A Vulnerable attacker does NOT amplify reflected Thorns (THORNS is not NORMAL,
// Vulnerable.atDamageReceive is NORMAL-only) -- the damage-type bit-correctness.
TEST(PowerHooks, ThornsNotAmplifiedByAttackerVulnerable) {
    CombatState s{};
    s.player_hp = 50;
    s.player_max_hp = 50;
    s.monster_count = 1;
    s.monsters[0].hp = 30;
    give_player_power(s, PowerId::THORNS, 4);
    give_monster_power(s, 0, PowerId::VULNERABLE, 1);
    ActionQueueItem hit{};
    hit.opcode = kOp(Opcode::DAMAGE);
    hit.src = 0;
    hit.tgt = kActorPlayer;
    hit.amount = 6;
    execute_opcode(s, hit);
    drain_actions(s);
    EXPECT_EQ(s.monsters[0].hp, 30 - 4) << "thorns is THORNS-typed -> Vulnerable does not boost it";
}

// Thorns does NOT reflect a THORNS or HP_LOSS incoming (no thorns-vs-thorns loop).
TEST(PowerHooks, ThornsDoesNotReflectThornsOrHpLoss) {
    CombatState s{};
    s.player_hp = 50;
    s.player_max_hp = 50;
    s.monster_count = 1;
    s.monsters[0].hp = 30;
    give_player_power(s, PowerId::THORNS, 3);
    ActionQueueItem thorns_hit{};
    thorns_hit.opcode = kOp(Opcode::DAMAGE);
    thorns_hit.src = 0;
    thorns_hit.tgt = kActorPlayer;
    thorns_hit.amount = 4;
    thorns_hit.flags = make_damage_flags(DamageType::THORNS);
    execute_opcode(s, thorns_hit);
    EXPECT_EQ(s.action_count, 0) << "no thorns-vs-thorns reflection";
    ActionQueueItem lose{};
    lose.opcode = kOp(Opcode::LOSE_HP);
    lose.tgt = kActorPlayer;
    lose.amount = 3;
    execute_opcode(s, lose);
    EXPECT_EQ(s.action_count, 0) << "HP loss does not trigger Thorns";
}

// PLATED_ARMOR: gains its amount as block at the §5.4 pre-card phase, directly
// (no Dexterity even with Dexterity present).
TEST(PowerHooks, PlatedArmorGainsBlockPreCardWithoutDexterity) {
    CombatState s{};
    s.player_hp = 50;
    s.player_max_hp = 50;
    give_player_power(s, PowerId::DEXTERITY, 2);
    give_player_power(s, PowerId::PLATED_ARMOR, 4);
    dispatch_at_end_of_turn_pre_card(s);
    drain_actions(s);
    EXPECT_EQ(s.player_block, 4) << "plated armor block is direct -- 4, no Dexterity";
}

// PLATED_ARMOR loses 1 stack on a NORMAL attack from a distinct attacker.
TEST(PowerHooks, PlatedArmorLosesStackOnNormalAttack) {
    CombatState s{};
    s.player_hp = 50;
    s.player_max_hp = 50;
    s.monster_count = 1;
    s.monsters[0].hp = 30;
    give_player_power(s, PowerId::PLATED_ARMOR, 4);
    ActionQueueItem hit{};
    hit.opcode = kOp(Opcode::DAMAGE);
    hit.src = 0;
    hit.tgt = kActorPlayer;
    hit.amount = 5;
    execute_opcode(s, hit);
    EXPECT_EQ(player_power_stack(s, PowerId::PLATED_ARMOR), 3) << "lost 1 stack";
}

// PLATED_ARMOR is NOT reduced by THORNS damage or self HP loss.
TEST(PowerHooks, PlatedArmorUnaffectedByThornsAndHpLoss) {
    CombatState s{};
    s.player_hp = 50;
    s.player_max_hp = 50;
    s.monster_count = 1;
    s.monsters[0].hp = 30;
    give_player_power(s, PowerId::PLATED_ARMOR, 4);
    ActionQueueItem thit{};
    thit.opcode = kOp(Opcode::DAMAGE);
    thit.src = 0;
    thit.tgt = kActorPlayer;
    thit.amount = 3;
    thit.flags = make_damage_flags(DamageType::THORNS);
    execute_opcode(s, thit);
    EXPECT_EQ(player_power_stack(s, PowerId::PLATED_ARMOR), 4) << "thorns doesn't reduce plated armor";
    ActionQueueItem lose{};
    lose.opcode = kOp(Opcode::LOSE_HP);
    lose.tgt = kActorPlayer;
    lose.amount = 3;
    execute_opcode(s, lose);
    EXPECT_EQ(player_power_stack(s, PowerId::PLATED_ARMOR), 4) << "self HP loss doesn't reduce plated armor";
}

// REGEN: heals `amount` at end of turn and decrements the stack.
TEST(PowerHooks, RegenHealsAndDecrementsEachTurn) {
    CombatState s{};
    s.player_hp = 40;
    s.player_max_hp = 50;
    give_player_power(s, PowerId::REGEN, 3);
    dispatch_at_end_of_turn(s);  // heal applied directly (no HEAL opcode)
    EXPECT_EQ(s.player_hp, 43) << "healed 3";
    EXPECT_EQ(player_power_stack(s, PowerId::REGEN), 2) << "stack decremented";
}

// REGEN clamps the heal to max HP and removes itself when it decrements to 0.
TEST(PowerHooks, RegenClampsToMaxHpAndRemovesAtZero) {
    CombatState s{};
    s.player_hp = 49;
    s.player_max_hp = 50;
    give_player_power(s, PowerId::REGEN, 1);
    dispatch_at_end_of_turn(s);
    EXPECT_EQ(s.player_hp, 50) << "heal clamped to max HP";
    EXPECT_EQ(player_power_stack(s, PowerId::REGEN), -1) << "Regen removed at 0";
}

// RITUAL (data): applies its amount of Strength to the player at end of turn.
TEST(PowerHooks, RitualGrantsStrengthAtEndOfTurn) {
    CombatState s{};
    s.player_hp = 50;
    s.player_max_hp = 50;
    give_player_power(s, PowerId::RITUAL, 2);
    dispatch_at_end_of_turn(s);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::APPLY_POWER));
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::STRENGTH);
    EXPECT_EQ(queued(s, 0).amount, 2);
    drain_actions(s);
    EXPECT_EQ(player_power_stack(s, PowerId::STRENGTH), 2);
}

// Determinism: the same THORNS + REGEN + Dexterity sequence hashes identically.
TEST(PowerHooks, NewPowerSequenceIsDeterministic) {
    auto run = []() {
        CombatState s{};
        s.player_hp = 40;
        s.player_max_hp = 50;
        s.monster_count = 1;
        s.monsters[0].hp = 30;
        s.monsters[0].max_hp = 30;
        give_player_power(s, PowerId::THORNS, 3);
        give_player_power(s, PowerId::REGEN, 2);
        give_player_power(s, PowerId::DEXTERITY, 2);
        ActionQueueItem hit{};
        hit.opcode = kOp(Opcode::DAMAGE);
        hit.src = 0;
        hit.tgt = kActorPlayer;
        hit.amount = 5;
        execute_opcode(s, hit);
        drain_actions(s);
        dispatch_at_end_of_turn(s);
        drain_actions(s);
        return hash_state(s);
    };
    EXPECT_EQ(run(), run()) << "power sequence must be deterministic";
}

}  // namespace
}  // namespace sts::engine
