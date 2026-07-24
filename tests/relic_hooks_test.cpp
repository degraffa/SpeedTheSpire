// B3.24 relic-hook framework (tier-2, constructed states). Proves, per the ledger
// acceptance: (1) ACQUISITION-ORDER dispatch -- relics fire in relic-list order
// (stage-a trap 8); (2) combat triggers for each combat-relevant relic in the
// starter + common batch, hand-derived from the cited decompiled Java; (3) the
// counter relics (Nunchaku, Pen Nib) persist their counter in the RelicSlot
// (stage-a §4.3's {relic_id, counter}).
//
// The dispatch functions take an explicit relic list, so these tests construct the
// relic list directly. As of B4.3 CombatState carries the relic mirror
// (s.relics / s.relic_count), so player_relics() returns the LIVE list -- asserted
// here against both an empty and a populated mirror (the wired power_hooks.cpp /
// action_queue.cpp sites read it).

#include <cstdint>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"      // legal_actions (Blue Candle playability)
#include "sts/engine/card_play.hpp"    // resolve_card_play (Blue Candle / Strike Dummy)
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/piles.hpp"        // draw_cards (Sundial reshuffle wiring)
#include "sts/engine/power_hooks.hpp"  // dispatch_at_start_of_turn (Next Turn Block)
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

// --- Seam: player_relics() reads CombatState's relic mirror (live as of B4.3) --

// Empty mirror (the 20 combat fixtures' state): the view is empty and the wired
// dispatch sites are no-ops, so fixtures stay behaviourally unchanged.
TEST(RelicHooks, PlayerRelicsViewEmptyWhenMirrorEmpty) {
    CombatState s = MakeState();
    ASSERT_EQ(s.relic_count, 0);  // value-init -> empty mirror
    const RelicView rv = player_relics(s);
    EXPECT_EQ(rv.count, 0);
    EXPECT_EQ(rv.relics, s.relics);  // returns the mirror, not nullptr
    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    EXPECT_EQ(s.action_count, 0);  // empty list dispatches nothing
}

// Populated mirror: player_relics() returns the live list in acquisition order,
// and dispatching through it drives the wired sites (B3.24's dispatch is now
// reachable from a CombatState, not just a hand-built list).
TEST(RelicHooks, PlayerRelicsViewReflectsPopulatedMirror) {
    CombatState s = MakeState();
    // Anchor (BLOCK 10) then Bag of Preparation (DRAW 2), both atBattleStart --
    // the same proven battle-start relics AtBattleStartFollowsAcquisitionOrder uses.
    s.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::ANCHOR), 0};
    s.relics[1] = RelicSlot{static_cast<uint16_t>(RelicId::BAG_OF_PREPARATION), 0};
    s.relic_count = 2;
    const RelicView rv = player_relics(s);
    ASSERT_EQ(rv.count, 2);
    EXPECT_EQ(rv.relics[0].relic_id, static_cast<uint16_t>(RelicId::ANCHOR));
    EXPECT_EQ(rv.relics[1].relic_id, static_cast<uint16_t>(RelicId::BAG_OF_PREPARATION));
    // Dispatching battle-start THROUGH the mirror view queues their actions,
    // proving the wired path is now reachable from a CombatState (not just a
    // hand-built list) -- B3.24's dispatch is live as of B4.3.
    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    EXPECT_GT(s.action_count, 0) << "populated mirror must drive the dispatch sites";
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
    r.add(RelicId::AKABEKO);          // Vigor apply DEFERRED (Vigor power row is later)
    r.add(RelicId::BOOT);             // damage-pipeline DEFERRED
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    dispatch_relics_on_victory(s, r.slots, r.count);
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(s.player_hp, 70) << "no accidental heal/state change";
}

// --- Un-deferred power-granting relics (now DATA at_battle_start APPLY_POWER) -

TEST(RelicHooks, BronzeScalesAppliesThreeThorns) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::BRONZE_SCALES);
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    drain(s);
    const PowerSlot* thorns = player_power(s, PowerId::THORNS);
    ASSERT_NE(thorns, nullptr);
    EXPECT_EQ(thorns->amount, 3);
}

TEST(RelicHooks, OddlySmoothStoneAppliesOneDexterity) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::ODDLY_SMOOTH_STONE);
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    drain(s);
    const PowerSlot* dex = player_power(s, PowerId::DEXTERITY);
    ASSERT_NE(dex, nullptr);
    EXPECT_EQ(dex->amount, 1);
}

// ============================ B3.25 uncommons ================================

// Seed a card instance into pool row `pi` from its registry def (base program).
void seed_card(CombatState& s, uint8_t pi, CardId id) {
    const CardDef* def = card_def(id);
    ASSERT_NE(def, nullptr);
    s.card_pool[pi].card_id = static_cast<uint16_t>(id);
    s.card_pool[pi].upgrade = 0;
    s.card_pool[pi].cost_now = card_cost(*def, 0);
    s.card_pool[pi].flags = card_flags(*def, 0);
    s.card_pool[pi].misc = 0;
}

void mirror_relic(CombatState& s, RelicId id, int16_t counter = -1) {
    s.relics[s.relic_count].relic_id = static_cast<uint16_t>(id);
    s.relics[s.relic_count].counter = counter;
    ++s.relic_count;
}

// --- Per-turn attack/skill counters (Kunai / Shuriken / Fan / Letter Opener) --

TEST(RelicHooksUncommon, KunaiGrantsOneDexterityEveryThirdAttack) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::KUNAI);
    const uint16_t strike = static_cast<uint16_t>(CardId::STRIKE);
    dispatch_relics_at_turn_start(s, r.slots, r.count);  // counter = 0
    EXPECT_EQ(r.slots[0].counter, 0);
    dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);
    dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);
    EXPECT_EQ(s.action_count, 0) << "no Dexterity before the 3rd attack";
    dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);  // 3rd
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::APPLY_POWER));
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::DEXTERITY);
    EXPECT_EQ(queued(s, 0).amount, 1);
    EXPECT_EQ(r.slots[0].counter, 0) << "cadence resets after firing";
    drain(s);
    EXPECT_EQ(player_power(s, PowerId::DEXTERITY)->amount, 1);
    // Skills do not count; victory parks the counter at -1 (Kunai.java:53-55).
    dispatch_relics_on_use_card(s, r.slots, r.count,
                                static_cast<uint16_t>(CardId::SHRUG_IT_OFF), 0);
    EXPECT_EQ(r.slots[0].counter, 0);
    dispatch_relics_on_victory(s, r.slots, r.count);
    EXPECT_EQ(r.slots[0].counter, -1);
}

TEST(RelicHooksUncommon, ShurikenGrantsOneStrengthEveryThirdAttack) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::SHURIKEN);
    const uint16_t strike = static_cast<uint16_t>(CardId::STRIKE);
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    for (int i = 0; i < 3; ++i) {
        dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);
    }
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::STRENGTH);
    EXPECT_EQ(queued(s, 0).amount, 1);
    drain(s);
    EXPECT_EQ(player_power(s, PowerId::STRENGTH)->amount, 1);
    // The per-turn reset (atTurnStart counter = 0) restarts the cadence.
    dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);
    dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);
    dispatch_relics_at_turn_start(s, r.slots, r.count);  // wipes the 2-count
    EXPECT_EQ(r.slots[0].counter, 0);
    dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);
    EXPECT_EQ(s.action_count, 0) << "reset cadence: 1 of 3, no trigger";
}

TEST(RelicHooksUncommon, OrnamentalFanGainsFourBlockEveryThirdAttack) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::ORNAMENTAL_FAN);
    const uint16_t strike = static_cast<uint16_t>(CardId::STRIKE);
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    for (int i = 0; i < 3; ++i) {
        dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);
    }
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 0).amount, 4);
    EXPECT_NE(queued(s, 0).flags & kBlockNoPowers, 0u)
        << "direct GainBlockAction -- no Dexterity (OrnamentalFan.java:46)";
    drain(s);
    EXPECT_EQ(s.player_block, 4);
}

TEST(RelicHooksUncommon, LetterOpenerDealsFiveThornsToAllEveryThirdSkill) {
    CombatState s = MakeState(/*monster_count=*/2);
    Relics r; r.add(RelicId::LETTER_OPENER);
    const uint16_t skill = static_cast<uint16_t>(CardId::SHRUG_IT_OFF);
    const uint16_t strike = static_cast<uint16_t>(CardId::STRIKE);
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);
    EXPECT_EQ(r.slots[0].counter, 0) << "attacks are not counted";
    for (int i = 0; i < 3; ++i) {
        dispatch_relics_on_use_card(s, r.slots, r.count, skill, 0);
    }
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::DAMAGE));
    EXPECT_EQ(queued(s, 0).tgt, kActorAllEnemies);
    EXPECT_EQ(queued(s, 0).amount, 5);
    EXPECT_EQ(damage_type_from_flags(queued(s, 0).flags), DamageType::THORNS);
    // THORNS: flat 5 even into a Vulnerable enemy (NORMAL-only pipeline skipped).
    s.monsters[0].powers[0].power_id =
        static_cast<uint16_t>(PowerId::VULNERABLE);
    s.monsters[0].powers[0].amount = 1;
    s.monsters[0].power_count = 1;
    drain(s);
    EXPECT_EQ(s.monsters[0].hp, 45);
    EXPECT_EQ(s.monsters[1].hp, 45);
}

// --- Ink Bottle: any-card counter, persists across combats -------------------

TEST(RelicHooksUncommon, InkBottleDrawsOneOnEveryTenthCardAndPersists) {
    CombatState s = MakeState();
    for (int i = 0; i < 2; ++i) {
        s.card_pool[i].card_id = static_cast<uint16_t>(CardId::STRIKE);
        s.draw[i] = static_cast<uint8_t>(i);
    }
    s.draw_count = 2;
    Relics r; r.add(RelicId::INK_BOTTLE, /*counter=*/0);
    const uint16_t strike = static_cast<uint16_t>(CardId::STRIKE);
    const uint16_t skill = static_cast<uint16_t>(CardId::SHRUG_IT_OFF);
    for (int i = 1; i <= 9; ++i) {  // mixed types both count (no filter)
        dispatch_relics_on_use_card(s, r.slots, r.count,
                                    (i % 2 == 0) ? skill : strike, 0);
        EXPECT_EQ(r.slots[0].counter, i);
        EXPECT_EQ(s.action_count, 0);
    }
    dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);  // 10th
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::DRAW));
    EXPECT_EQ(queued(s, 0).amount, 1);
    EXPECT_EQ(r.slots[0].counter, 0);
    drain(s);
    EXPECT_EQ(s.hand_count, 1);
    // NO victory reset (InkBottle has no onVictory): the count persists in the
    // RelicSlot across combats (unlike Kunai/Shuriken/Fan/Letter Opener).
    dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);
    dispatch_relics_on_victory(s, r.slots, r.count);
    EXPECT_EQ(r.slots[0].counter, 1);
}

// --- Horn Cleat: 14 block on turn 2, once per combat --------------------------

TEST(RelicHooksUncommon, HornCleatGainsFourteenBlockOnTurnTwoOncePerCombat) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::HORN_CLEAT, /*counter=*/-1);  // constructor default
    dispatch_relics_at_battle_start(s, r.slots, r.count);  // arm: counter = 0
    EXPECT_EQ(r.slots[0].counter, 0);
    dispatch_relics_at_turn_start(s, r.slots, r.count);    // turn 1
    EXPECT_EQ(s.action_count, 0);
    dispatch_relics_at_turn_start(s, r.slots, r.count);    // turn 2 -> fire
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 0).amount, 14);
    EXPECT_NE(queued(s, 0).flags & kBlockNoPowers, 0u);
    EXPECT_EQ(r.slots[0].counter, -1) << "fired latch (grayscale)";
    drain(s);
    EXPECT_EQ(s.player_block, 14);
    dispatch_relics_at_turn_start(s, r.slots, r.count);    // turn 3+: latched
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    EXPECT_EQ(s.action_count, 0) << "once per combat";
    dispatch_relics_on_victory(s, r.slots, r.count);
    EXPECT_EQ(r.slots[0].counter, -1);
    // Next combat re-arms via atBattleStart.
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    EXPECT_EQ(r.slots[0].counter, 0);
}

// --- Sundial: every 3rd reshuffle -> 2 energy ---------------------------------

TEST(RelicHooksUncommon, SundialGrantsTwoEnergyEveryThirdShuffle) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::SUNDIAL, /*counter=*/0);  // onEquip counter = 0
    dispatch_relics_on_shuffle(s, r.slots, r.count);
    dispatch_relics_on_shuffle(s, r.slots, r.count);
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(r.slots[0].counter, 2);
    dispatch_relics_on_shuffle(s, r.slots, r.count);   // 3rd
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::GAIN_ENERGY));
    EXPECT_EQ(queued(s, 0).amount, 2);
    EXPECT_EQ(r.slots[0].counter, 0);
    drain(s);
    EXPECT_EQ(s.player_energy, 5);
}

TEST(RelicHooksUncommon, SundialCountsTheLiveDrawPileReshuffle) {
    // The wired site: an empty-draw-pile draw reshuffles the discard
    // (EmptyDeckShuffleAction) and fires onShuffle through the relic mirror.
    CombatState s = MakeState();
    mirror_relic(s, RelicId::SUNDIAL, /*counter=*/2);  // one away from firing
    for (int i = 0; i < 2; ++i) {
        s.card_pool[i].card_id = static_cast<uint16_t>(CardId::STRIKE);
        s.discard[i] = static_cast<uint8_t>(i);
    }
    s.discard_count = 2;
    (void)draw_cards(s, 1);
    EXPECT_EQ(s.relics[0].counter, 0) << "reshuffle counted through the mirror";
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::GAIN_ENERGY));
    EXPECT_EQ(queued(s, 0).amount, 2);
}

// --- Gremlin Horn: monster death (not the last) -------------------------------

TEST(RelicHooksUncommon, GremlinHornFiresOnlyWhenAnotherMonsterLives) {
    CombatState s = MakeState(/*monster_count=*/2, /*monster_hp=*/10);
    mirror_relic(s, RelicId::GREMLIN_HORN);
    ActionQueueItem hit{};
    hit.opcode = kOp(Opcode::DAMAGE);
    hit.src = kActorPlayer;
    hit.tgt = 0;
    hit.amount = 10;
    execute_opcode(s, hit);                    // kills monster 0; monster 1 lives
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::GAIN_ENERGY));
    EXPECT_EQ(queued(s, 0).amount, 1);
    EXPECT_EQ(queued(s, 1).opcode, kOp(Opcode::DRAW));
    EXPECT_EQ(queued(s, 1).amount, 1);
    drain(s);
    EXPECT_EQ(s.player_energy, 4);
    // The LAST monster's death does not fire (areMonstersBasicallyDead guard).
    hit.tgt = 1;
    execute_opcode(s, hit);
    EXPECT_EQ(s.action_count, 0);
    // A non-lethal hit never fires.
    CombatState s2 = MakeState(2, 10);
    mirror_relic(s2, RelicId::GREMLIN_HORN);
    hit.tgt = 0;
    hit.amount = 4;
    execute_opcode(s2, hit);
    EXPECT_EQ(s2.action_count, 0);
}

// --- Mercury Hourglass (DATA): 3 THORNS to all at turn start ------------------

TEST(RelicHooksUncommon, MercuryHourglassDealsThreeThornsToAllAtTurnStart) {
    CombatState s = MakeState(/*monster_count=*/2);
    // THORNS damage ignores Strength/Vulnerable (NORMAL-only hooks).
    s.player_powers[0].power_id = static_cast<uint16_t>(PowerId::STRENGTH);
    s.player_powers[0].amount = 5;
    s.player_power_count = 1;
    s.monsters[1].powers[0].power_id =
        static_cast<uint16_t>(PowerId::VULNERABLE);
    s.monsters[1].powers[0].amount = 1;
    s.monsters[1].power_count = 1;
    Relics r; r.add(RelicId::MERCURY_HOURGLASS);
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    drain(s);
    EXPECT_EQ(s.monsters[0].hp, 47);
    EXPECT_EQ(s.monsters[1].hp, 47) << "flat 3 -- THORNS skips the pipeline";
}

// --- Self-Forming Clay + Next Turn Block --------------------------------------

TEST(RelicHooksUncommon, SelfFormingClayStacksNextTurnBlockPaidNextTurn) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::SELF_FORMING_CLAY);
    dispatch_relics_was_hp_lost(s, r.slots, r.count, /*amount=*/5);
    dispatch_relics_was_hp_lost(s, r.slots, r.count, /*amount=*/2);
    drain(s);
    const PowerSlot* ntb = player_power(s, PowerId::NEXT_TURN_BLOCK);
    ASSERT_NE(ntb, nullptr);
    EXPECT_EQ(ntb->amount, 6) << "two losses stack 3 + 3";
    // NextTurnBlockPower.atStartOfTurn: gain the stacked block, then self-remove.
    dispatch_at_start_of_turn(s);
    drain(s);
    EXPECT_EQ(s.player_block, 6);
    EXPECT_EQ(player_power(s, PowerId::NEXT_TURN_BLOCK), nullptr);
}

TEST(RelicHooksUncommon, SelfFormingClayFiresThroughTheLiveDamagePath) {
    // The B3.25 wasHPLost relic wiring (AbstractPlayer.damage:1445-1449): an
    // unblocked enemy hit on the player reaches the relic through op_damage ->
    // dispatch_was_hp_lost -> relics (powers first, then relics).
    CombatState s = MakeState();
    mirror_relic(s, RelicId::SELF_FORMING_CLAY);
    ActionQueueItem hit{};
    hit.opcode = kOp(Opcode::DAMAGE);
    hit.src = 0;
    hit.tgt = kActorPlayer;
    hit.amount = 5;
    execute_opcode(s, hit);
    drain(s);
    const PowerSlot* ntb = player_power(s, PowerId::NEXT_TURN_BLOCK);
    ASSERT_NE(ntb, nullptr);
    EXPECT_EQ(ntb->amount, 3);
    // A fully-blocked hit loses no HP -> no trigger (damageAmount > 0 gate).
    CombatState s2 = MakeState();
    mirror_relic(s2, RelicId::SELF_FORMING_CLAY);
    s2.player_block = 10;
    execute_opcode(s2, hit);
    drain(s2);
    EXPECT_EQ(player_power(s2, PowerId::NEXT_TURN_BLOCK), nullptr);
}

// --- Blue Candle: curse playability + play cost --------------------------------

TEST(RelicHooksUncommon, BlueCandleMakesCursesPlayableForOneHpAndExhausts) {
    CombatState s = MakeState();
    seed_card(s, 0, CardId::INJURY);   // cost -2 curse -> UNPLAYABLE, cost_now 0
    s.hand[0] = 0;
    s.hand_count = 1;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);

    ActionMask mask{};
    legal_actions(s, mask);
    EXPECT_FALSE(mask.can_play[0]) << "curse unplayable without Blue Candle";

    mirror_relic(s, RelicId::BLUE_CANDLE);
    legal_actions(s, mask);
    EXPECT_TRUE(mask.can_play[0]) << "Blue Candle unlocks curse plays";

    CardQueueItem play{};
    play.card_index = 0;
    play.target = 0;
    resolve_card_play(s, play);
    drain(s);
    EXPECT_EQ(s.hand_count, 0);
    EXPECT_EQ(s.exhaust_count, 1) << "the played curse exhausts";
    EXPECT_EQ(s.discard_count, 0);
    EXPECT_EQ(s.player_hp, 69) << "LoseHPAction(player, 1)";
    EXPECT_EQ(s.player_energy, 3) << "cost -2 spends no energy";
}

TEST(RelicHooksUncommon, BlueCandlePlayedCurseRunsNoTriggerProgram) {
    // Regret's `effects` is its END_OF_TURN trigger program (lose HP == hand
    // size); playing it via Blue Candle must NOT run that program (use() is
    // empty in the Java) -- only the 1-HP candle cost applies.
    CombatState s = MakeState();
    seed_card(s, 0, CardId::REGRET);
    seed_card(s, 1, CardId::STRIKE);
    seed_card(s, 2, CardId::STRIKE);
    s.hand[0] = 0; s.hand[1] = 1; s.hand[2] = 2;
    s.hand_count = 3;
    mirror_relic(s, RelicId::BLUE_CANDLE);
    CardQueueItem play{};
    play.card_index = 0;
    play.target = 0;
    resolve_card_play(s, play);
    drain(s);
    EXPECT_EQ(s.player_hp, 69) << "exactly the candle's 1 HP -- Regret's "
                                  "hand-size loss did not run on play";
    EXPECT_EQ(s.exhaust_count, 1);
}

// --- Strike Dummy: +3 on STRIKE-tagged card damage ----------------------------

TEST(RelicHooksUncommon, StrikeDummyAddsThreeToStrikeCardsOnly) {
    CombatState s = MakeState();
    seed_card(s, 0, CardId::STRIKE);   // base 6, is_strike
    seed_card(s, 1, CardId::BASH);     // base 8, not a strike
    s.hand[0] = 0; s.hand[1] = 1;
    s.hand_count = 2;
    mirror_relic(s, RelicId::STRIKE_DUMMY);

    CardQueueItem play{};
    play.card_index = 0;
    play.target = 0;
    resolve_card_play(s, play);
    ASSERT_GE(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::DAMAGE));
    EXPECT_EQ(queued(s, 0).amount, 9) << "Strike 6 + 3 (StrikeDummy.java:30)";
    drain(s);
    EXPECT_EQ(s.monsters[0].hp, 41);

    play.card_index = 1;               // Bash: no STRIKE tag -> unmodified 8
    resolve_card_play(s, play);
    ASSERT_GE(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::DAMAGE));
    EXPECT_EQ(queued(s, 0).amount, 8);
}

// --- Meat on the Bone: pre-victory heal, before every onVictory ---------------

TEST(RelicHooksUncommon, MeatOnTheBoneHealsTwelveAtHalfBeforeOnVictory) {
    CombatState s = MakeState();       // max 80
    mirror_relic(s, RelicId::BURNING_BLOOD);
    mirror_relic(s, RelicId::MEAT_ON_THE_BONE);
    s.player_hp = 40;                  // 40*2 <= 80 -> eligible
    // The run-layer victory sequence: Meat FIRST (AbstractRoom.endBattle:418-420),
    // then every relic's onVictory (Burning Blood +6) -- regardless of the
    // acquisition order (Meat was acquired AFTER Burning Blood here).
    apply_meat_on_the_bone_pre_victory(s);
    EXPECT_EQ(s.player_hp, 52);
    dispatch_relics_on_victory(s, s.relics, s.relic_count);
    EXPECT_EQ(s.player_hp, 58);

    // Above half: no heal (MeatOnTheBone.java:33).
    CombatState s2 = MakeState();
    mirror_relic(s2, RelicId::MEAT_ON_THE_BONE);
    s2.player_hp = 41;
    apply_meat_on_the_bone_pre_victory(s2);
    EXPECT_EQ(s2.player_hp, 41);

    // Clamps to max HP (heal() semantics): 35/72 -> 47; 70/80 not eligible.
    CombatState s3 = MakeState();
    mirror_relic(s3, RelicId::MEAT_ON_THE_BONE);
    s3.player_max_hp = 20;
    s3.player_hp = 10;
    apply_meat_on_the_bone_pre_victory(s3);
    EXPECT_EQ(s3.player_hp, 20) << "10 + 12 clamps to max 20";
}

// --- Deferred / run-layer uncommons are combat no-ops --------------------------

TEST(RelicHooksUncommon, DeferredAndRunLayerUncommonsAreCombatNoOps) {
    CombatState s = MakeState();
    Relics r;
    r.add(RelicId::MUMMIFIED_HAND);    // DEFERRED: no POWER CardType until B3.7
    r.add(RelicId::PANTOGRAPH);        // DEFERRED: no EnemyType/BOSS metadata
    r.add(RelicId::BOTTLED_FLAME);     // acquisition-choice machinery (run layer)
    r.add(RelicId::QUESTION_CARD);     // reward-layer modifier (B4.5)
    r.add(RelicId::THE_COURIER);       // shop-layer modifier (B4.8)
    r.add(RelicId::WHITE_BEAST_STATUE);// potion-drop modifier (B4.5)
    r.add(RelicId::MATRYOSHKA, 2);     // chest-layer hook (B4.7)
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    dispatch_relics_on_use_card(s, r.slots, r.count,
                                static_cast<uint16_t>(CardId::STRIKE), 0);
    dispatch_relics_on_victory(s, r.slots, r.count);
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(s.player_hp, 70) << "no accidental heal/state change";
    EXPECT_EQ(r.slots[6].counter, 2) << "Matryoshka's chest counter untouched";
}

}  // namespace
}  // namespace sts::engine
