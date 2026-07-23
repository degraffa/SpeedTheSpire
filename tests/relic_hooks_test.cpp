// B3.24 relic-hook framework (tier-2, constructed states). Proves, per the ledger
// acceptance: (1) ACQUISITION-ORDER dispatch -- relics fire in relic-list order
// (stage-a trap 8); (2) combat triggers for each combat-relevant relic in the
// starter + common batch, hand-derived from the cited decompiled Java; (3) the
// counter relics (Nunchaku, Pen Nib) persist their counter in the RelicSlot
// (stage-a §4.3's {relic_id, counter}).
//
// The dispatch functions take an explicit relic list (CombatState carries no relic
// mirror yet -- that additive field is B4.3's schema-owned change), so these tests
// construct the relic list directly. player_relics() returning an empty view (the
// wired power_hooks.cpp / action_queue.cpp sites are no-ops until B4.3) is asserted
// too.

#include <cstdint>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/relic_hooks.hpp"
#include "sts/engine/relics.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

constexpr uint16_t kOp(Opcode o) { return static_cast<uint16_t>(o); }

// Build a relic list in acquisition order (index 0 = acquired first).
struct Relics {
    RelicSlot slots[kRelicCap]{};
    uint8_t count = 0;
    void add(RelicId id, int16_t counter = 0) {
        slots[count].relic_id = static_cast<uint16_t>(id);
        slots[count].counter = counter;
        ++count;
    }
};

// The i-th queued action, front-first.
ActionQueueItem queued(const CombatState& s, uint8_t i) {
    return s.action_queue[(s.action_head + i) % kActionQueueCap];
}

void drain(CombatState& s) {
    ActionQueueItem it{};
    while (pop_action_front(s, it)) {
        execute_opcode(s, it);
    }
}

CombatState MakeState(int monster_count = 1, int16_t monster_hp = 50) {
    CombatState s{};
    s.player_hp = 70;
    s.player_max_hp = 80;
    s.player_energy = 3;
    s.monster_count = static_cast<uint8_t>(monster_count);
    for (int m = 0; m < monster_count; ++m) {
        s.monsters[m].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
        s.monsters[m].hp = monster_hp;
        s.monsters[m].max_hp = monster_hp;
    }
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

const PowerSlot* player_power(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.player_powers[i];
        }
    }
    return nullptr;
}
const PowerSlot* monster_power(const CombatState& s, uint8_t m, PowerId id) {
    for (uint8_t i = 0; i < s.monsters[m].power_count; ++i) {
        if (s.monsters[m].powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.monsters[m].powers[i];
        }
    }
    return nullptr;
}

// --- Seam: player_relics() is empty until B4.3's CombatState relic mirror -----

TEST(RelicHooks, PlayerRelicsViewEmptyUntilB43) {
    CombatState s = MakeState();
    const RelicView rv = player_relics(s);
    EXPECT_EQ(rv.count, 0) << "no CombatState relic storage yet (B4.3)";
    // A null/empty relic list dispatches nothing (the wired no-op sites).
    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    EXPECT_EQ(s.action_count, 0);
}

// --- ACQUISITION-ORDER dispatch (acceptance) ---------------------------------

TEST(RelicHooks, AtBattleStartFollowsAcquisitionOrder) {
    // Anchor (BLOCK 10) + Bag of Preparation (DRAW 2), both atBattleStart. The
    // queue order == the relic-LIST order (acquisition order, trap 8).
    CombatState a = MakeState();
    Relics ra;
    ra.add(RelicId::ANCHOR);            // acquired first
    ra.add(RelicId::BAG_OF_PREPARATION);
    dispatch_relics_at_battle_start(a, ra.slots, ra.count);
    ASSERT_EQ(a.action_count, 2);
    EXPECT_EQ(queued(a, 0).opcode, kOp(Opcode::BLOCK));  // Anchor first
    EXPECT_EQ(queued(a, 0).amount, 10);
    EXPECT_EQ(queued(a, 1).opcode, kOp(Opcode::DRAW));   // Bag of Prep second
    EXPECT_EQ(queued(a, 1).amount, 2);

    // Reverse acquisition -> reversed dispatch.
    CombatState b = MakeState();
    Relics rb;
    rb.add(RelicId::BAG_OF_PREPARATION);
    rb.add(RelicId::ANCHOR);
    dispatch_relics_at_battle_start(b, rb.slots, rb.count);
    ASSERT_EQ(b.action_count, 2);
    EXPECT_EQ(queued(b, 0).opcode, kOp(Opcode::DRAW));
    EXPECT_EQ(queued(b, 1).opcode, kOp(Opcode::BLOCK));
}

// --- Data relics -------------------------------------------------------------

TEST(RelicHooks, AnchorGainsTenBlock) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::ANCHOR);
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    drain(s);
    EXPECT_EQ(s.player_block, 10);
}

TEST(RelicHooks, BagOfMarblesVulnerablesAllEnemies) {
    CombatState s = MakeState(/*monster_count=*/2);
    Relics r; r.add(RelicId::BAG_OF_MARBLES);
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    drain(s);
    const PowerSlot* v0 = monster_power(s, 0, PowerId::VULNERABLE);
    const PowerSlot* v1 = monster_power(s, 1, PowerId::VULNERABLE);
    ASSERT_NE(v0, nullptr);
    ASSERT_NE(v1, nullptr);
    EXPECT_EQ(v0->amount, 1);
    EXPECT_EQ(v1->amount, 1);
}

TEST(RelicHooks, VajraGivesOneStrength) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::VAJRA);
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    drain(s);
    const PowerSlot* str = player_power(s, PowerId::STRENGTH);
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->amount, 1);
}

TEST(RelicHooks, BagOfPreparationDrawsTwo) {
    CombatState s = MakeState();
    // Two cards in the draw pile so DRAW 2 has something to move.
    s.card_pool[0].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.card_pool[1].card_id = static_cast<uint16_t>(CardId::DEFEND);
    s.draw[0] = 0; s.draw[1] = 1; s.draw_count = 2;
    Relics r; r.add(RelicId::BAG_OF_PREPARATION);
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    drain(s);
    EXPECT_EQ(s.hand_count, 2);
    EXPECT_EQ(s.draw_count, 0);
}

// --- Native heals ------------------------------------------------------------

TEST(RelicHooks, BurningBloodHealsSixOnVictory) {
    CombatState s = MakeState();       // hp 70 / max 80
    Relics r; r.add(RelicId::BURNING_BLOOD);
    dispatch_relics_on_victory(s, r.slots, r.count);
    EXPECT_EQ(s.player_hp, 76);
    EXPECT_EQ(s.action_count, 0) << "heal is applied directly, no queued op";

    // Clamps to max HP.
    CombatState s2 = MakeState();
    s2.player_hp = 78;
    dispatch_relics_on_victory(s2, r.slots, r.count);
    EXPECT_EQ(s2.player_hp, 80);
}

TEST(RelicHooks, BloodVialHealsTwoAtBattleStart) {
    CombatState s = MakeState();       // hp 70 / max 80
    Relics r; r.add(RelicId::BLOOD_VIAL);
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    EXPECT_EQ(s.player_hp, 72);
}

// --- Native conditionals + once-per-combat flags -----------------------------

TEST(RelicHooks, CentennialPuzzleDrawsThreeOnceOnFirstHpLoss) {
    CombatState s = MakeState();
    for (int i = 0; i < 3; ++i) {
        s.card_pool[i].card_id = static_cast<uint16_t>(CardId::STRIKE);
        s.draw[i] = static_cast<uint8_t>(i);
    }
    s.draw_count = 3;
    Relics r; r.add(RelicId::CENTENNIAL_PUZZLE);

    dispatch_relics_was_hp_lost(s, r.slots, r.count, /*amount=*/5);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::DRAW));
    EXPECT_EQ(queued(s, 0).amount, 3);
    EXPECT_EQ(r.slots[0].counter, 1) << "fired flag set";
    drain(s);
    EXPECT_EQ(s.hand_count, 3);

    // A SECOND HP loss does not draw again.
    dispatch_relics_was_hp_lost(s, r.slots, r.count, /*amount=*/4);
    EXPECT_EQ(s.action_count, 0);
}

TEST(RelicHooks, OrichalcumGainsBlockOnlyWhenUnblocked) {
    // 0 block at end of turn -> gain 6.
    CombatState s = MakeState();
    s.player_block = 0;
    Relics r; r.add(RelicId::ORICHALCUM);
    dispatch_relics_on_player_end_turn(s, r.slots, r.count);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 0).amount, 6);

    // With block already present, no gain.
    CombatState s2 = MakeState();
    s2.player_block = 4;
    dispatch_relics_on_player_end_turn(s2, r.slots, r.count);
    EXPECT_EQ(s2.action_count, 0);
}

TEST(RelicHooks, RedSkullGainsStrengthWhenBloodied) {
    // Dropping to <= 50% max HP grants +3 Strength, once while active.
    CombatState s = MakeState();
    s.player_hp = 40;               // 40*2 == 80 == max -> bloodied
    Relics r; r.add(RelicId::RED_SKULL);
    dispatch_relics_was_hp_lost(s, r.slots, r.count, /*amount=*/6);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::APPLY_POWER));
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::STRENGTH);
    EXPECT_EQ(queued(s, 0).amount, 3);
    EXPECT_EQ(r.slots[0].counter, 1) << "isActive set";
    drain(s);
    EXPECT_EQ(player_power(s, PowerId::STRENGTH)->amount, 3);

    // Above 50% -> not bloodied -> no Strength.
    CombatState s2 = MakeState();
    s2.player_hp = 50;              // 100 > 80 -> not bloodied
    Relics r2; r2.add(RelicId::RED_SKULL);
    dispatch_relics_was_hp_lost(s2, r2.slots, r2.count, /*amount=*/6);
    EXPECT_EQ(s2.action_count, 0);
}

// --- Counter relics (persist counter in the RelicSlot) -----------------------

TEST(RelicHooks, NunchakuGrantsEnergyEveryTenthAttack) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::NUNCHAKU);
    const uint16_t strike = static_cast<uint16_t>(CardId::STRIKE);  // an ATTACK

    for (int i = 1; i <= 9; ++i) {
        dispatch_relics_on_use_card(s, r.slots, r.count, strike, /*pool=*/0);
        EXPECT_EQ(r.slots[0].counter, i) << "counter persists in the RelicSlot";
        EXPECT_EQ(s.action_count, 0) << "no energy before the 10th";
    }
    dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);  // 10th
    EXPECT_EQ(r.slots[0].counter, 0) << "counter resets at 10";
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::GAIN_ENERGY));
    EXPECT_EQ(queued(s, 0).amount, 1);
    drain(s);
    EXPECT_EQ(s.player_energy, 4);
}

TEST(RelicHooks, NunchakuIgnoresNonAttacks) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::NUNCHAKU);
    // Shrug It Off is a SKILL -> Nunchaku does not count it.
    dispatch_relics_on_use_card(s, r.slots, r.count,
                                static_cast<uint16_t>(CardId::SHRUG_IT_OFF), 0);
    EXPECT_EQ(r.slots[0].counter, 0);
}

TEST(RelicHooks, PenNibCountsAttacksAndCyclesAtTen) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::PEN_NIB);
    const uint16_t strike = static_cast<uint16_t>(CardId::STRIKE);
    for (int i = 1; i <= 9; ++i) {
        dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);
        EXPECT_EQ(r.slots[0].counter, i);
    }
    dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);  // 10th
    EXPECT_EQ(r.slots[0].counter, 0) << "cycles back to 0 at the 10th attack";
}

// --- Turn-start counters -----------------------------------------------------

TEST(RelicHooks, HappyFlowerGrantsEnergyEveryThirdTurn) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::HAPPY_FLOWER);
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    EXPECT_EQ(s.action_count, 0);
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    EXPECT_EQ(s.action_count, 0);
    dispatch_relics_at_turn_start(s, r.slots, r.count);  // 3rd
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::GAIN_ENERGY));
    EXPECT_EQ(r.slots[0].counter, 0) << "cadence counter reset";
}

TEST(RelicHooks, LanternGrantsEnergyOnFirstTurnOnly) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::LANTERN);
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::GAIN_ENERGY));
    drain(s);
    EXPECT_EQ(s.player_energy, 4);
    // Second turn: nothing.
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    EXPECT_EQ(s.action_count, 0);
}

// --- Non-combat / deferred relics dispatch nothing ---------------------------

TEST(RelicHooks, NonCombatAndDeferredRelicsAreNoOps) {
    CombatState s = MakeState();
    Relics r;
    r.add(RelicId::WHETSTONE);        // equip-time, no combat hook
    r.add(RelicId::BRONZE_SCALES);    // Thorns apply DEFERRED (power row is B3.4)
    r.add(RelicId::BOOT);             // damage-pipeline DEFERRED
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    dispatch_relics_on_victory(s, r.slots, r.count);
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(s.player_hp, 70) << "no accidental heal/state change";
}

}  // namespace
}  // namespace sts::engine
