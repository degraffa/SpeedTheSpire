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
//   (RupturePower.java:32-37; AbstractPlayer.damage:1445-1447).

#include <cstdint>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/monster_dispatch.hpp"  // MonsterIntent (the Flight onRemove telegraph)
#include "sts/engine/powers.hpp"
#include "sts/engine/relic_hooks.hpp"  // dispatch_relics_at_battle_start (Regen x Red Skull)
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
    dispatch_on_exhaust(s, /*pool_index=*/0, static_cast<uint16_t>(CardId::STRIKE));
    dispatch_at_end_of_turn_pre_card(s);
    dispatch_at_end_of_turn(s);
    dispatch_at_start_of_turn(s);
    dispatch_at_start_of_turn_post_draw(s);
    dispatch_on_gained_block(s, kActorPlayer, 5);
    dispatch_was_hp_lost(s, kActorPlayer, kActorPlayer, 4);

    EXPECT_EQ(s.action_count, 0) << "no hook-bearing power -> nothing queued";
}

// --- ORDERING (acceptance): player powers before monster powers --------------

TEST(PowerHooks, OnUseCardFanOutPlayerBeforeMonster) {
    // B3.6 rebound RAGE to its Java hook (RagePower.onUseCard, native with the
    // ATTACK-type guard), so the ordering probe moves to the UseCardAction
    // fan-out (UseCardAction.java:41-64): PLAYER powers first, MONSTER powers
    // LAST. Player Rage(2), monster Rage(4), played card = STRIKE (an ATTACK):
    // the player's block queues before the monster's.
    CombatState s{};
    s.monster_count = 1;
    s.monsters[0].hp = 30;
    give_player_power(s, PowerId::RAGE, 2);
    give_monster_power(s, 0, PowerId::RAGE, 4);

    dispatch_on_use_card(s, /*played_pool_index=*/0,
                         static_cast<uint16_t>(CardId::STRIKE));

    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 0).tgt, kActorPlayer);   // player Rage first
    EXPECT_EQ(queued(s, 0).amount, 2);
    EXPECT_EQ(queued(s, 1).opcode, kOp(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 1).tgt, 0);              // monster 0 Rage second
    EXPECT_EQ(queued(s, 1).amount, 4);
}

TEST(PowerHooks, RageAttackGuardSkipsNonAttacks) {
    // RagePower.onUseCard's card.type == ATTACK guard (RagePower.java:42): a
    // played SKILL queues nothing.
    CombatState s{};
    give_player_power(s, PowerId::RAGE, 3);
    dispatch_on_use_card(s, /*played_pool_index=*/0,
                         static_cast<uint16_t>(CardId::DEFEND));
    EXPECT_EQ(s.action_count, 0);
}

// --- Vigor / Pen Nib: the two attack-consumed damage powers -------------------

// Both are spent by an ATTACK and by nothing else, and both queue their removal
// addToBot (VigorPower.java:49-55; PenNibPower.java:39-44) -- so the removal
// resolves BEHIND the played attack's own actions and every hit of a multi-hit
// attack is boosted before the power leaves.
TEST(PowerHooks, VigorAndPenNibAreConsumedByAnAttackAndNothingElse) {
    const struct { PowerId id; int16_t amount; } powers[] = {
        {PowerId::VIGOR, 8},
        {PowerId::PEN_NIB, 1},
    };
    for (const auto& p : powers) {
        {
            CombatState s{};
            give_player_power(s, p.id, p.amount);
            dispatch_on_use_card(s, /*played_pool_index=*/0,
                                 static_cast<uint16_t>(CardId::STRIKE));
            ASSERT_EQ(s.action_count, 1)
                << "power " << static_cast<int>(p.id);
            EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::REMOVE_POWER));
            EXPECT_EQ(queued(s, 0).tgt, kActorPlayer);
            EXPECT_EQ(queued(s, 0).flags, make_apply_power_flags(p.id));
        }
        {
            // A SKILL leaves it standing.
            CombatState s{};
            give_player_power(s, p.id, p.amount);
            dispatch_on_use_card(s, /*played_pool_index=*/0,
                                 static_cast<uint16_t>(CardId::DEFEND));
            EXPECT_EQ(s.action_count, 0)
                << "power " << static_cast<int>(p.id);
        }
    }
}

// --- STRESS 1: onExhaust list order (Feel No Pain + Dark Embrace) ------------

TEST(PowerHooks, OnExhaustFollowsPlayerPowerListOrder) {
    // Feel No Pain (BLOCK self = amount) + Dark Embrace (DRAW self = amount)
    // resolving on ONE exhaust: the sequence == the player power-LIST order
    // (§5.5, application order), not the hook table order.
    CombatState a{};
    a.monster_count = 1;
    a.monsters[0].hp = 30;  // DarkEmbracePower ignores post-combat exhausts.
    give_player_power(a, PowerId::FEEL_NO_PAIN, 3);   // applied first
    give_player_power(a, PowerId::DARK_EMBRACE, 1);   // applied second
    dispatch_on_exhaust(a, /*pool_index=*/0, static_cast<uint16_t>(CardId::STRIKE));
    ASSERT_EQ(a.action_count, 2);
    EXPECT_EQ(queued(a, 0).opcode, kOp(Opcode::BLOCK));  // Feel No Pain first
    EXPECT_EQ(queued(a, 0).amount, 3);
    EXPECT_EQ(queued(a, 1).opcode, kOp(Opcode::DRAW));   // Dark Embrace second
    EXPECT_EQ(queued(a, 1).amount, 1);

    // Reverse the application order -> reversed resolution order.
    CombatState b{};
    b.monster_count = 1;
    b.monsters[0].hp = 30;
    give_player_power(b, PowerId::DARK_EMBRACE, 1);   // applied first
    give_player_power(b, PowerId::FEEL_NO_PAIN, 3);   // applied second
    dispatch_on_exhaust(b, /*pool_index=*/0, static_cast<uint16_t>(CardId::STRIKE));
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
    ASSERT_EQ(s.action_count, 4);  // then B3.9's hand-discard action
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 0).tgt, kActorPlayer);
    EXPECT_EQ(queued(s, 0).amount, 3);
    EXPECT_EQ(queued(s, 1).opcode, kOp(Opcode::LOSE_HP));   // Combust self HP loss
    EXPECT_EQ(queued(s, 1).amount, 1);
    EXPECT_EQ(queued(s, 2).opcode, kOp(Opcode::DAMAGE));    // Combust AoE
    EXPECT_EQ(queued(s, 2).tgt, kActorAllEnemies);
    EXPECT_EQ(queued(s, 2).amount, 5);
    EXPECT_EQ(queued(s, 3).opcode, kOp(Opcode::DISCARD_HAND));

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
    // lands.
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
    EXPECT_EQ(damage_type_from_flags(queued(s, 0).flags),
              DamageType::THORNS);
    drain_actions(s);
    EXPECT_EQ(s.monsters[0].hp, 30 - 5);
}

TEST(PowerHooks, SadisticThornsIsNotAmplifiedByTheDebuffItTriggeredOn) {
    // STS302329: a 10-HP Jaw Worm behind 5 block receives Bash (8), then
    // Sadistic 5 from the newly applied Vulnerable. The ordinary hit leaves 7
    // HP; THORNS ignores Vulnerable and leaves 2. Treating Sadistic as NORMAL
    // amplified it to 7, killed the monster one command early, and assembled a
    // reward with twelve downstream RunState fields out of sync.
    CombatState s{};
    s.monster_count = 1;
    s.monsters[0].hp = 10;
    s.monsters[0].max_hp = 10;
    s.monsters[0].block = 5;
    give_player_power(s, PowerId::SADISTIC, 5);

    ActionQueueItem bash_hit{};
    bash_hit.opcode = kOp(Opcode::DAMAGE);
    bash_hit.src = kActorPlayer;
    bash_hit.tgt = 0;
    bash_hit.amount = 8;
    execute_opcode(s, bash_hit);
    ASSERT_EQ(s.monsters[0].hp, 7);
    ASSERT_EQ(s.monsters[0].block, 0);

    execute_opcode(
        s, apply_power_item(kActorPlayer, 0, PowerId::VULNERABLE, 2));
    ASSERT_EQ(s.action_count, 1);
    drain_actions(s);
    EXPECT_EQ(s.monsters[0].hp, 2);
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

// Monster `m`'s power stack amount, or -1 if absent (a removed slot reads as
// absent) -- the monster-owned twin of player_power_stack above.
int monster_power_stack(const CombatState& s, uint8_t m, PowerId id) {
    for (uint8_t i = 0; i < s.monsters[m].power_count; ++i) {
        if (s.monsters[m].powers[i].power_id == static_cast<uint16_t>(id)) {
            return s.monsters[m].powers[i].amount;
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
    // B3.6 Java-exactness fix: DexterityPower.stackPower removes the slot when a
    // stack lands on exactly 0 (addToTop RemoveSpecificPowerAction,
    // DexterityPower.java:44-49) -- so the reversed Dexterity slot is GONE, not
    // a 0-amount residue (the pre-B3.6 expectation contradicted the cited Java;
    // recorded in the B3.6 ledger Log).
    EXPECT_EQ(player_power_stack(s, PowerId::DEXTERITY), -1)
        << "Dexterity stacked to exactly 0 removes the slot";
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
//
// THE REDUCTION IS QUEUED, NOT SYNCHRONOUS: PlatedArmorPower.wasHPLost addToBot's
// a ReducePowerAction (PlatedArmorPower.java:58), so the stack does not move
// until the queue drains. This test used to read the stack immediately and
// expect 3, because the native body decremented its own slot in place -- which
// was wrong in TIMING and, worse, BYPASSED the removal choke point
// (remove_slot_at), so a power carrying an onRemove could never fire it. See
// power_plated_armor.cpp.
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
    EXPECT_EQ(player_power_stack(s, PowerId::PLATED_ARMOR), 4)
        << "still 4: the ReducePowerAction is addToBot and has not resolved";
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::REDUCE_POWER));
    drain_actions(s);
    EXPECT_EQ(player_power_stack(s, PowerId::PLATED_ARMOR), 3) << "lost 1 stack";
}

// The last stack REMOVES the power, through op_reduce_power -> remove_slot_at.
// That path is what makes an armour-break telegraph possible at all.
TEST(PowerHooks, PlatedArmorLastStackRemovesThePowerThroughTheChokePoint) {
    CombatState s{};
    s.player_hp = 50;
    s.player_max_hp = 50;
    s.monster_count = 1;
    s.monsters[0].hp = 30;
    give_player_power(s, PowerId::PLATED_ARMOR, 1);
    ActionQueueItem hit{};
    hit.opcode = kOp(Opcode::DAMAGE);
    hit.src = 0;
    hit.tgt = kActorPlayer;
    hit.amount = 5;
    execute_opcode(s, hit);
    drain_actions(s);
    EXPECT_EQ(player_power_stack(s, PowerId::PLATED_ARMOR), -1)
        << "the slot is GONE (the helper's absent sentinel), not left at 0";
    EXPECT_EQ(s.player_power_count, 0);
    EXPECT_EQ(s.action_count, 0)
        << "onRemove is gated on a non-player owner (PlatedArmorPower.java:66), "
           "so a player's armour running out telegraphs nothing";
}

// ON_POWER_REMOVED ROUTING GUARD.
//
// The hook fires exactly ONE body -- the REMOVED power's own -- from
// remove_slot_at, the single point every destruction path reaches. This test
// exists so the hook cannot become silently unrouted: it drives the removal
// through the TWO distinct opcodes that reach that point (a bare REMOVE_POWER,
// and a REDUCE_POWER falling to zero) and requires the observable consequence
// both times. Flight is the only binder today and its consequence is the Byrd's
// GROUNDED change of state, so a refactor that stopped dispatching -- or that
// dispatched to the wrong power -- leaves the Byrd airborne here.
TEST(PowerHooks, OnPowerRemovedFiresTheRemovedPowersOwnBodyFromBothPaths) {
    for (int path = 0; path < 2; ++path) {
        CombatState s{};
        s.player_hp = 50;
        s.player_max_hp = 50;
        s.monster_count = 1;
        s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::BYRD);
        s.monsters[0].hp = 30;
        s.monsters[0].max_hp = 30;
        s.monsters[0].flags = kMonsterFlagByrdFlying;
        give_monster_power(s, 0, PowerId::FLIGHT, 1);

        ActionQueueItem it{};
        it.src = 0;
        it.tgt = 0;
        it.flags = make_apply_power_flags(PowerId::FLIGHT);
        if (path == 0) {
            it.opcode = kOp(Opcode::REMOVE_POWER);
        } else {
            it.opcode = kOp(Opcode::REDUCE_POWER);
            it.amount = 1;  // 1 - 1 == 0 -> remove_slot_at
        }
        execute_opcode(s, it);
        drain_actions(s);

        EXPECT_EQ(s.monsters[0].power_count, 0) << "path " << path;
        EXPECT_EQ(s.monsters[0].flags & kMonsterFlagByrdFlying, 0u)
            << "path " << path << ": onRemove must have run";
        EXPECT_EQ(s.monsters[0].intent,
                  static_cast<uint8_t>(MonsterIntent::STUN))
            << "path " << path << ": onRemove's queued STUN telegraph";
    }
}

// ...and it must NOT fire for a power that does not bind it. A power with no
// on_power_removed binding is destroyed with no side effect at all, which is
// what keeps every landed fixture byte-identical across this hook's arrival.
TEST(PowerHooks, OnPowerRemovedIsANoOpForAPowerThatDoesNotBindIt) {
    CombatState s{};
    s.player_hp = 50;
    s.player_max_hp = 50;
    give_player_power(s, PowerId::STRENGTH, 3);
    give_player_power(s, PowerId::THORNS, 2);
    ActionQueueItem rem{};
    rem.opcode = kOp(Opcode::REMOVE_POWER);
    rem.src = kActorPlayer;
    rem.tgt = kActorPlayer;
    rem.flags = make_apply_power_flags(PowerId::THORNS);
    execute_opcode(s, rem);
    EXPECT_EQ(s.player_power_count, 1);
    EXPECT_EQ(player_power_stack(s, PowerId::STRENGTH), 3);
    EXPECT_EQ(s.action_count, 0) << "no hook queued anything";
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

// REGEN heals through the FULL AbstractCreature.heal (RegenAction.java:38 ->
// AbstractCreature.java:386-417), not a bare HP write, and the difference is
// the NOT-BLOODIED cross at :404-408: a heal that lifts the player above
// maxHealth / 2.0f fires RedSkull.onNotBloodied (RedSkull.java:55-63), the
// addToTop ApplyPowerAction(StrengthPower -3). Capture s2v3_wave2_STS205404_
// ps296, floor 41 "3 Darklings", seq 710 -> 711 exactly: 37/75 HP, Strength 4
// (Vajra 1 + Red Skull 3), Regeneration 4 at end of turn 2 -> 41 HP and
// Strength 1 in the game; the sim kept 4 and out-damaged the game by 3 on
// every attack for the rest of the fight.
TEST(PowerHooks, RegenHealCrossesRedSkullAndRemovesTheStrength) {
    CombatState s{};
    s.player_hp = 37;
    s.player_max_hp = 75;
    s.monster_count = 1;
    s.monsters[0].hp = 50;
    s.monsters[0].max_hp = 50;
    give_player_power(s, PowerId::STRENGTH, 1);  // Vajra's battle-start +1
    s.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::RED_SKULL), -1};
    s.relic_count = 1;
    dispatch_relics_at_battle_start(s, s.relics, s.relic_count);
    drain_actions(s);  // the entry decider: 37 <= 37.5 -> bloodied -> +3
    ASSERT_EQ(player_power_stack(s, PowerId::STRENGTH), 4);
    ASSERT_TRUE(combat_red_skull_active(s.flags));
    give_player_power(s, PowerId::REGEN, 4);

    dispatch_at_end_of_turn(s);
    EXPECT_EQ(s.player_hp, 41) << "the heal itself is unchanged";
    EXPECT_EQ(player_power_stack(s, PowerId::REGEN), 3) << "and so is the decay";
    ASSERT_EQ(s.action_count, 1)
        << "41 > 37.5 with isActive set -> exactly one queued -3";
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::APPLY_POWER));
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::STRENGTH);
    EXPECT_EQ(queued(s, 0).amount, -3);
    EXPECT_FALSE(combat_red_skull_active(s.flags))
        << "isActive cleared (RedSkull.java:61)";
    drain_actions(s);
    EXPECT_EQ(player_power_stack(s, PowerId::STRENGTH), 1)
        << "the game's Strength 1 at seq 711, not the sim's former 4";

    // Negative control: the same tick from 33 lands on 37 <= 37.5 -- still
    // bloodied, no cross, the +3 stands (the game's turn-1 -> turn-2 shape,
    // where 38 -> 37 armed the relic and nothing since has disarmed it).
    CombatState s2{};
    s2.player_hp = 33;
    s2.player_max_hp = 75;
    s2.monster_count = 1;
    s2.monsters[0].hp = 50;
    s2.monsters[0].max_hp = 50;
    s2.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::RED_SKULL), -1};
    s2.relic_count = 1;
    dispatch_relics_at_battle_start(s2, s2.relics, s2.relic_count);
    drain_actions(s2);
    ASSERT_EQ(player_power_stack(s2, PowerId::STRENGTH), 3);
    give_player_power(s2, PowerId::REGEN, 4);
    dispatch_at_end_of_turn(s2);
    EXPECT_EQ(s2.player_hp, 37);
    EXPECT_EQ(s2.action_count, 0);
    EXPECT_TRUE(combat_red_skull_active(s2.flags));
    EXPECT_EQ(player_power_stack(s2, PowerId::STRENGTH), 3);
}

// The other half of the same seam: AbstractCreature.heal's onPlayerHeal relic
// fold (:393-395) runs over a Regeneration tick too -- Magic Flower's
// MathUtils.round(amount * 1.5f) (MagicFlower.java:30-37) and Mark of the
// Bloom's unconditional 0 (MarkOfTheBloom.java:25-29).
TEST(PowerHooks, RegenHealRunsTheOnPlayerHealRelicFold) {
    CombatState s{};
    s.player_hp = 40;
    s.player_max_hp = 80;
    s.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::MAGIC_FLOWER), -1};
    s.relic_count = 1;
    give_player_power(s, PowerId::REGEN, 4);
    dispatch_at_end_of_turn(s);
    EXPECT_EQ(s.player_hp, 46) << "round(4 * 1.5f) == 6, not 4";
    EXPECT_EQ(player_power_stack(s, PowerId::REGEN), 3);

    CombatState s2{};
    s2.player_hp = 40;
    s2.player_max_hp = 80;
    s2.relics[0] =
        RelicSlot{static_cast<uint16_t>(RelicId::MARK_OF_THE_BLOOM), -1};
    s2.relic_count = 1;
    give_player_power(s2, PowerId::REGEN, 4);
    dispatch_at_end_of_turn(s2);
    EXPECT_EQ(s2.player_hp, 40) << "Mark of the Bloom zeroes every heal";
    EXPECT_EQ(player_power_stack(s2, PowerId::REGEN), 3)
        << "the decay is RegenAction's own and does not read the heal";
}

// REGENERATE_MONSTER: heals `amount` at end of turn, clamped to max HP, and
// NEVER decrements (the whole distinction from REGEN above -- see
// power_regenerate_monster.cpp / RegenerateMonsterPower.java:37-43). A
// monster's own AT_END_OF_TURN is dispatched from dispatch_at_end_of_round's
// per-monster walk (power_hooks.cpp:258-259), NOT from dispatch_at_end_of_turn
// (player-only, :234-236) -- that walk is exactly MonsterGroup
// .applyEndOfTurnPowers's step (1) (MonsterGroup.java:290-304).
TEST(PowerHooks, RegenerateMonsterHealsAndNeverDecrements) {
    CombatState s{};
    s.monster_count = 1;
    s.monsters[0].hp = 20;
    s.monsters[0].max_hp = 30;
    give_monster_power(s, 0, PowerId::REGENERATE_MONSTER, 3);
    dispatch_at_end_of_round(s);
    EXPECT_EQ(s.monsters[0].hp, 23) << "healed 3";
    EXPECT_EQ(monster_power_stack(s, 0, PowerId::REGENERATE_MONSTER), 3)
        << "amount is untouched -- no decrement, unlike REGEN";
}

TEST(PowerHooks, RegenerateMonsterClampsToMaxHp) {
    CombatState s{};
    s.monster_count = 1;
    s.monsters[0].hp = 29;
    s.monsters[0].max_hp = 30;
    give_monster_power(s, 0, PowerId::REGENERATE_MONSTER, 3);
    dispatch_at_end_of_round(s);
    EXPECT_EQ(s.monsters[0].hp, 30) << "heal clamped to max HP";
    EXPECT_EQ(monster_power_stack(s, 0, PowerId::REGENERATE_MONSTER), 3);
}

// A dead monster's AT_END_OF_TURN never fires at all -- the dispatch walk
// (power_hooks.cpp:254-256) skips monster_dead_or_escaped before reaching any
// power's hook, which is what stands in for RegenerateMonsterPower's own
// isDying/isDead guard (RegenerateMonsterPower.java:39).
TEST(PowerHooks, RegenerateMonsterSkipsADeadMonster) {
    CombatState s{};
    s.monster_count = 1;
    s.monsters[0].hp = 0;
    s.monsters[0].max_hp = 30;
    give_monster_power(s, 0, PowerId::REGENERATE_MONSTER, 3);
    dispatch_at_end_of_round(s);
    EXPECT_EQ(s.monsters[0].hp, 0) << "dead monster is skipped, not healed back up";
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

// --- DURATION debuffs: Vulnerable / Weak / Frail atEndOfRound ----------------
//
// VulnerablePower.atEndOfRound (VulnerablePower.java:44-53),
// WeakPower.atEndOfRound (WeakPower.java:44-53) and FrailPower.atEndOfRound
// (FrailPower.java:40-49) are the same body: consume a justApplied latch if one
// is set, otherwise queue a one-stack ReducePowerAction (which REMOVES rather
// than reduces once the request meets the stack, ReducePowerAction.java:45-51).
// The three differ ONLY in when their ctor sets justApplied -- Vulnerable needs
// `turnHasEnded && isSourceMonster` (:36-38); Weak and Frail need
// `isSourceMonster` alone (:35-37 / :32-34).
//
// The latch is the slot's own PowerSlot.counter, so it is per INSTANCE: the
// player and every monster can carry one at the same time.

PowerSlot* player_power_slot(CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.player_powers[i];
        }
    }
    return nullptr;
}

void end_round(CombatState& s) {
    dispatch_at_end_of_round(s);
    drain_actions(s);
}

// A card-applied Vulnerable lands during the player's own turn, so turnHasEnded
// is 0, no latch is taken, and the very first round end costs it a stack.
TEST(DurationDebuffs, PlayerAppliedVulnerableStartsDecayingImmediately) {
    CombatState s = MakeState(CardId::BASH, 2);
    s.turn_has_ended = 0;
    execute_opcode(s, apply_power_item(kActorPlayer, 0, PowerId::VULNERABLE, 2));
    ASSERT_NE(monster_power(s, 0, PowerId::VULNERABLE), nullptr);
    EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE)->counter, 0)
        << "no justApplied: Bash is played while turnHasEnded is 0";

    end_round(s);
    ASSERT_NE(monster_power(s, 0, PowerId::VULNERABLE), nullptr);
    EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE)->amount, 1);

    end_round(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE), nullptr)
        << "1 - 1 is a removal, not a slot resting at 0";
}

// The monster-applied direction. A Vulnerable a monster puts on the player during
// the enemy phase takes the latch, so its FIRST round end only clears the latch.
TEST(DurationDebuffs, MonsterAppliedVulnerableSkipsItsFirstTick) {
    CombatState s = MakeState(CardId::BASH, 2);
    s.turn_has_ended = 1;  // the enemy phase: EndTurnAction has run
    execute_opcode(s,
                   apply_power_item(/*src=*/0, kActorPlayer, PowerId::VULNERABLE, 2));
    ASSERT_NE(player_power_slot(s, PowerId::VULNERABLE), nullptr);
    EXPECT_EQ(player_power_slot(s, PowerId::VULNERABLE)->counter, 1);

    end_round(s);
    ASSERT_NE(player_power_slot(s, PowerId::VULNERABLE), nullptr);
    EXPECT_EQ(player_power_slot(s, PowerId::VULNERABLE)->amount, 2)
        << "justApplied consumed instead of a stack";
    EXPECT_EQ(player_power_slot(s, PowerId::VULNERABLE)->counter, 0);

    end_round(s);
    EXPECT_EQ(player_power_slot(s, PowerId::VULNERABLE)->amount, 1);
    end_round(s);
    EXPECT_EQ(player_power_slot(s, PowerId::VULNERABLE), nullptr);
}

// Weak's ctor has no turnHasEnded clause, which is the one place the three
// bodies' preconditions differ. In S1 scope `isSourceMonster` is exactly "the
// owner is the player": the Doubt curse applies Weak to the player from the
// player's OWN turn and still passes true (Doubt.java:35).
TEST(DurationDebuffs, WeakLatchesByOwnerWithNoTurnHasEndedClause) {
    CombatState player_side = MakeState(CardId::BASH, 2);
    player_side.turn_has_ended = 0;  // Doubt fires at the end of the player's turn
    execute_opcode(player_side,
                   apply_power_item(kActorPlayer, kActorPlayer, PowerId::WEAK, 1));
    ASSERT_NE(player_power_slot(player_side, PowerId::WEAK), nullptr);
    EXPECT_EQ(player_power_slot(player_side, PowerId::WEAK)->counter, 1)
        << "Weak latches on a player owner even with turnHasEnded == 0";
    end_round(player_side);
    EXPECT_EQ(player_power_slot(player_side, PowerId::WEAK)->amount, 1);
    end_round(player_side);
    EXPECT_EQ(player_power_slot(player_side, PowerId::WEAK), nullptr);

    // A card weakening a MONSTER passes false, so it never latches.
    CombatState monster_side = MakeState(CardId::BASH, 2);
    execute_opcode(monster_side, apply_power_item(kActorPlayer, 0, PowerId::WEAK, 2));
    ASSERT_NE(monster_power(monster_side, 0, PowerId::WEAK), nullptr);
    EXPECT_EQ(monster_power(monster_side, 0, PowerId::WEAK)->counter, 0);
    end_round(monster_side);
    EXPECT_EQ(monster_power(monster_side, 0, PowerId::WEAK)->amount, 1);
}

// AbstractCreature.addPower (:506-513) hands the amount to the LIVE instance and
// discards the freshly built one, latch and all -- so re-application never
// re-arms, and never disarms, an existing latch.
TEST(DurationDebuffs, StackingPreservesTheExistingLatch) {
    CombatState s = MakeState(CardId::BASH, 2);
    s.turn_has_ended = 1;
    const ActionQueueItem apply =
        apply_power_item(/*src=*/0, kActorPlayer, PowerId::VULNERABLE, 2);
    execute_opcode(s, apply);
    ASSERT_EQ(player_power_slot(s, PowerId::VULNERABLE)->counter, 1);

    execute_opcode(s, apply);  // a second monster application, same round
    EXPECT_EQ(player_power_slot(s, PowerId::VULNERABLE)->amount, 4);
    EXPECT_EQ(player_power_slot(s, PowerId::VULNERABLE)->counter, 1)
        << "the surviving instance keeps its own latch";

    // And once the latch is spent, re-stacking does not re-arm it.
    end_round(s);
    ASSERT_EQ(player_power_slot(s, PowerId::VULNERABLE)->counter, 0);
    execute_opcode(s, apply);
    EXPECT_EQ(player_power_slot(s, PowerId::VULNERABLE)->amount, 6);
    EXPECT_EQ(player_power_slot(s, PowerId::VULNERABLE)->counter, 0);
    end_round(s);
    EXPECT_EQ(player_power_slot(s, PowerId::VULNERABLE)->amount, 5);
}

// Per INSTANCE, not per combat: the player and two monsters each hold their own
// Vulnerable at their own amount and their own latch, and one round end moves all
// three correctly. This is what the retired single CombatState.flags bit could
// not express, and why Frail's latch moved into the slot with the other two.
TEST(DurationDebuffs, EveryOwnerCarriesItsOwnLatchAndAmount) {
    CombatState s = MakeState(CardId::BASH, 2);
    s.monster_count = 2;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[1].hp = 50;
    s.monsters[1].max_hp = 50;

    s.turn_has_ended = 1;
    execute_opcode(s, apply_power_item(0, kActorPlayer, PowerId::VULNERABLE, 3));
    s.turn_has_ended = 0;
    execute_opcode(s, apply_power_item(kActorPlayer, 0, PowerId::VULNERABLE, 2));
    execute_opcode(s, apply_power_item(kActorPlayer, 1, PowerId::VULNERABLE, 1));

    EXPECT_EQ(player_power_slot(s, PowerId::VULNERABLE)->counter, 1);
    EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE)->counter, 0);
    EXPECT_EQ(monster_power(s, 1, PowerId::VULNERABLE)->counter, 0);

    end_round(s);
    EXPECT_EQ(player_power_slot(s, PowerId::VULNERABLE)->amount, 3) << "latched";
    EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE)->amount, 1);
    EXPECT_EQ(monster_power(s, 1, PowerId::VULNERABLE), nullptr) << "1 -> removed";
}

// Frail shares the body, so a MONSTER-owned Frail now ticks. It could not before:
// the old latch was a single player-only flag bit and the hook body returned early
// for any other owner.
TEST(DurationDebuffs, FrailOnAMonsterTicksLikeThePlayers) {
    CombatState s = MakeState(CardId::BASH, 2);
    execute_opcode(s, apply_power_item(kActorPlayer, 0, PowerId::FRAIL, 2));
    ASSERT_NE(monster_power(s, 0, PowerId::FRAIL), nullptr);
    EXPECT_EQ(monster_power(s, 0, PowerId::FRAIL)->counter, 0)
        << "a monster-owned Frail is card-sourced -> isSourceMonster false";

    end_round(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::FRAIL)->amount, 1);
    end_round(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::FRAIL), nullptr);
}

// A dying or escaped monster is skipped by both applyEndOfTurnPowers walks
// (MonsterGroup.java:292,299), so its debuffs stop moving the moment it leaves.
TEST(DurationDebuffs, ADeadMonstersDebuffsStopTicking) {
    CombatState s = MakeState(CardId::BASH, 2);
    execute_opcode(s, apply_power_item(kActorPlayer, 0, PowerId::VULNERABLE, 2));
    s.monsters[0].hp = 0;

    end_round(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE)->amount, 2);
}

}  // namespace
}  // namespace sts::engine
