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
#include "sts/engine/rest_sites.hpp"  // rest_apply_heal (the out-of-combat heal door)
#include "sts/engine/rng_stream.hpp"  // from_seed / random (Mummified Hand's draw)
#include "sts/engine/run_deck.hpp"    // add_card_to_master_deck (egg onObtainCard)
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"  // MonsterDef::is_boss (Pantograph)

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

// CentennialPuzzle.wasHPLost (CentennialPuzzle.java:40-49): the first HP loss of
// a combat addToTop(DrawCardAction(player, NUM_CARDS)) with NUM_CARDS == 3
// (:20, :44). The gate is the class's `private static boolean usedThisCombat`
// (:21), NOT `this.counter` -- which the class never writes, so the relic keeps
// AbstractRelic's -1 (pinned in relic_pools_test's
// CentennialPuzzleKeepsAbstractRelicsMinusOneCounter). Here the counter is
// seeded at -1, which is what the registry row now hands the live slot, and it
// must still be -1 after the relic has fired.
void give_centennial_a_draw_pile(CombatState& s) {
    for (int i = 0; i < 3; ++i) {
        s.card_pool[i].card_id = static_cast<uint16_t>(CardId::STRIKE);
        s.draw[i] = static_cast<uint8_t>(i);
    }
    s.draw_count = 3;
}

TEST(RelicHooks, CentennialPuzzleDrawsThreeOnFirstHpLoss) {
    CombatState s = MakeState();
    give_centennial_a_draw_pile(s);
    Relics r; r.add(RelicId::CENTENNIAL_PUZZLE, /*counter=*/-1);
    ASSERT_EQ(s.flags & kCombatFlagCentennialPuzzleUsed, 0u);

    dispatch_relics_was_hp_lost(s, r.slots, r.count, /*amount=*/5);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::DRAW));
    EXPECT_EQ(queued(s, 0).amount, 3);  // NUM_CARDS (CentennialPuzzle.java:20)
    EXPECT_NE(s.flags & kCombatFlagCentennialPuzzleUsed, 0u)
        << "usedThisCombat = true (CentennialPuzzle.java:46)";
    EXPECT_EQ(r.slots[0].counter, -1)
        << "this.counter is never written (CentennialPuzzle.java:21,33-49)";
    drain(s);
    EXPECT_EQ(s.hand_count, 3);
}

TEST(RelicHooks, CentennialPuzzleDoesNotFireTwiceInOneCombat) {
    CombatState s = MakeState();
    give_centennial_a_draw_pile(s);
    Relics r; r.add(RelicId::CENTENNIAL_PUZZLE, /*counter=*/-1);

    dispatch_relics_was_hp_lost(s, r.slots, r.count, /*amount=*/5);
    ASSERT_EQ(s.action_count, 1);
    drain(s);

    // A SECOND HP loss in the SAME combat does not draw again: usedThisCombat is
    // still true and only atPreBattle clears it (CentennialPuzzle.java:34).
    dispatch_relics_was_hp_lost(s, r.slots, r.count, /*amount=*/4);
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(r.slots[0].counter, -1);
}

// The latch is combat-scoped, not run-scoped: a fresh CombatState IS
// atPreBattle. This is the unit-level statement of it; the end-to-end statement,
// walking two real combats of one run through enter_combat, is
// run_advance_test's RunCombatWasHpLost.CentennialPuzzleReArmsInASecondCombat.
TEST(RelicHooks, CentennialPuzzleReArmsWhenTheCombatStateIsFresh) {
    Relics r; r.add(RelicId::CENTENNIAL_PUZZLE, /*counter=*/-1);

    CombatState first = MakeState();
    give_centennial_a_draw_pile(first);
    dispatch_relics_was_hp_lost(first, r.slots, r.count, /*amount=*/5);
    ASSERT_EQ(first.action_count, 1);
    ASSERT_NE(first.flags & kCombatFlagCentennialPuzzleUsed, 0u);

    // Second combat of the same run: the relic list is carried over untouched,
    // the CombatState is not.
    CombatState second = MakeState();
    give_centennial_a_draw_pile(second);
    ASSERT_EQ(second.flags & kCombatFlagCentennialPuzzleUsed, 0u)
        << "a fresh CombatState is atPreBattle's usedThisCombat = false";
    dispatch_relics_was_hp_lost(second, r.slots, r.count, /*amount=*/5);
    ASSERT_EQ(second.action_count, 1);
    EXPECT_EQ(queued(second, 0).opcode, kOp(Opcode::DRAW));
    EXPECT_EQ(queued(second, 0).amount, 3);
    EXPECT_EQ(r.slots[0].counter, -1) << "no residue in the persistent slot";
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

TEST(RelicHooks, RedSkullDoesNotFireTwiceWhenCombatBeganBloodied) {
    // preBattlePrep pre-seeds isBloodied = currentHealth <= maxHealth / 2
    // (AbstractPlayer.java:1575), so a combat ENTERED at or below half HP
    // never fires the damage-side onBloodied cross (:1476-1481 fires only on
    // the false->true flip). The battle-start hook seeds the slot latch from
    // starting HP; an HP loss while already bloodied then grants nothing.
    //
    // b4faf1c's fix, unchanged. What DID change is the first half: entering
    // bloodied now grants the +3 once, at battle start (the owner-specified
    // body of the action RedSkull.java:38 queues), instead of granting
    // nothing at all -- so the property under test is "exactly one grant",
    // which is what the damage-side suppression exists to protect.
    CombatState s = MakeState();
    s.player_hp = 38;               // 76 <= 80 -> entered combat bloodied
    Relics r; r.add(RelicId::RED_SKULL);
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    EXPECT_EQ(r.slots[0].counter, 1) << "entered bloodied -> isActive";
    drain(s);
    ASSERT_NE(player_power(s, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(player_power(s, PowerId::STRENGTH)->amount, 3);

    s.player_hp = 30;
    dispatch_relics_was_hp_lost(s, r.slots, r.count, /*amount=*/8);
    EXPECT_EQ(s.action_count, 0)
        << "no onBloodied cross for a combat that began bloodied";
    drain(s);
    EXPECT_EQ(player_power(s, PowerId::STRENGTH)->amount, 3)
        << "still the single entry grant";
}

// --- Red Skull: entry grant, heal-cross removal, cumulative crossings --------
//
// The spec for the +3 on an already-bloodied ENTRY is OWNER-PROVIDED (project
// owner, 2026-07-28): RedSkull.atBattleStart's queued action
// (RedSkull.java:38) is an unavailable anonymous inner class in this decompiled
// tree, so its body cannot be derived. Everything else below IS Java-derived
// and cites it.

TEST(RelicHooks, RedSkullGrantsStrengthOnAnAlreadyBloodiedEntry) {
    // Owner-specified 2026-07-28: becoming bloodied grants +3, and that
    // includes ENTERING combat already bloodied. addToBot, because the call
    // itself is visible at RedSkull.java:38 even though the action's body is
    // not.
    CombatState s = MakeState();
    s.player_hp = 40;               // 80 <= 80 -> entered combat bloodied
    Relics r; r.add(RelicId::RED_SKULL);
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::APPLY_POWER));
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::STRENGTH);
    EXPECT_EQ(queued(s, 0).amount, 3);
    EXPECT_EQ(r.slots[0].counter, 1) << "isActive set by the entry grant";
    drain(s);
    EXPECT_EQ(player_power(s, PowerId::STRENGTH)->amount, 3);

    // Negative control: entering ABOVE half grants nothing and arms the latch.
    CombatState s2 = MakeState();
    s2.player_hp = 41;              // 82 > 80 -> not bloodied
    Relics r2; r2.add(RelicId::RED_SKULL);
    dispatch_relics_at_battle_start(s2, r2.slots, r2.count);
    EXPECT_EQ(s2.action_count, 0);
    EXPECT_EQ(r2.slots[0].counter, 0);
}

TEST(RelicHooks, RedSkullBloodiedBoundaryRoundsHalfDown) {
    // "At or below half, half rounded DOWN": max 7 -> bloodied at hp <= 3.
    // preBattlePrep's seed is the int division `currentHealth <= maxHealth / 2`
    // (AbstractPlayer.java:1575) and the damage-side cross is the float
    // `currentHealth <= maxHealth / 2.0f` (:1476); for max 7 both answer
    // hp <= 3, and hp*2 <= max is the integer form of each.
    for (int hp = 0; hp <= 7; ++hp) {
        CombatState s = MakeState();
        s.player_max_hp = 7;
        s.player_hp = static_cast<int16_t>(hp);
        Relics r; r.add(RelicId::RED_SKULL);
        dispatch_relics_at_battle_start(s, r.slots, r.count);
        const bool bloodied = hp <= 3;
        EXPECT_EQ(s.action_count, bloodied ? 1 : 0) << "hp " << hp << "/7";
        EXPECT_EQ(r.slots[0].counter, bloodied ? 1 : 0) << "hp " << hp << "/7";
    }
}

TEST(RelicHooks, RedSkullRemovesStrengthWhenAHealLiftsAboveHalf) {
    // RedSkull.onNotBloodied (RedSkull.java:54-63) -- DECOMPILABLE, unlike the
    // atBattleStart action: while isActive and in a COMBAT-phase room, addToTop
    // ApplyPowerAction(p, p, StrengthPower(p, -3), -3); then isActive = false.
    // The cross itself is AbstractCreature.heal:404-408 (isBloodied && the heal
    // leaves currentHealth above maxHealth / 2.0f), reached from
    // AbstractPlayer.heal (:1544-1552).
    CombatState s = MakeState();
    s.player_hp = 40;               // bloodied at entry -> the entry grant
    s.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::RED_SKULL), 0};
    s.relic_count = 1;
    dispatch_relics_at_battle_start(s, s.relics, s.relic_count);
    drain(s);
    ASSERT_EQ(player_power(s, PowerId::STRENGTH)->amount, 3);

    heal_player_with_relics(s, 5);  // 45 -> 90 > 80 -> the not-bloodied cross
    EXPECT_EQ(s.player_hp, 45);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::APPLY_POWER));
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::STRENGTH);
    EXPECT_EQ(queued(s, 0).amount, -3);
    EXPECT_EQ(s.relics[0].counter, 0) << "isActive cleared (RedSkull.java:61)";
    drain(s);
    EXPECT_EQ(player_power(s, PowerId::STRENGTH), nullptr)
        << "3 + (-3) == 0 -> StrengthPower.stackPower queues its own removal";

    // Negative control: a heal that leaves the player AT or below half is no
    // cross at all -- the Java's condition is strictly greater than half.
    CombatState s2 = MakeState();
    s2.player_hp = 30;
    s2.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::RED_SKULL), 0};
    s2.relic_count = 1;
    dispatch_relics_at_battle_start(s2, s2.relics, s2.relic_count);
    drain(s2);
    ASSERT_EQ(player_power(s2, PowerId::STRENGTH)->amount, 3);
    heal_player_with_relics(s2, 10);  // 40 -> 80 == 80, still bloodied
    EXPECT_EQ(s2.action_count, 0);
    EXPECT_EQ(s2.relics[0].counter, 1) << "still isActive";
}

TEST(RelicHooks, RedSkullHealCrossDoesNothingWhileArmed) {
    // Negative control for the fan-out gate: a player who was never bloodied
    // has isActive == false, so a heal past half must queue nothing at all.
    // (This is the guard at RedSkull.java:56, not an outer isBloodied test.)
    CombatState s = MakeState();
    s.player_hp = 60;
    s.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::RED_SKULL), 0};
    s.relic_count = 1;
    dispatch_relics_at_battle_start(s, s.relics, s.relic_count);
    ASSERT_EQ(s.relics[0].counter, 0);
    heal_player_with_relics(s, 10);
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(s.relics[0].counter, 0);
}

TEST(RelicHooks, RedSkullRemovalIsBlockedByArtifact) {
    // The -3 is a NEGATIVE Strength application through the ordinary
    // APPLY_POWER door, so Artifact eats it with no special case anywhere:
    // StrengthPower's ctor types an instance built with a non-positive amount
    // as a DEBUFF (StrengthPower.java:37 -> updateDescription :81-89), and
    // ApplyPowerAction.java:131-138 spends one Artifact stack
    // (ArtifactPower.onSpecificTrigger, ArtifactPower.java:33-40) and RETURNS
    // without applying. Charge consumed, Strength stays.
    CombatState s = MakeState();
    s.player_hp = 40;
    s.player_powers[0] = PowerSlot{static_cast<uint16_t>(PowerId::ARTIFACT), 1,
                                   0, 0};
    s.player_power_count = 1;
    s.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::RED_SKULL), 0};
    s.relic_count = 1;
    dispatch_relics_at_battle_start(s, s.relics, s.relic_count);
    drain(s);
    ASSERT_EQ(player_power(s, PowerId::STRENGTH)->amount, 3);
    EXPECT_EQ(player_power(s, PowerId::ARTIFACT)->amount, 1) << "+3 is a BUFF";

    heal_player_with_relics(s, 5);  // 45 -> above half -> the -3 is queued
    drain(s);
    EXPECT_EQ(player_power(s, PowerId::ARTIFACT)->amount, 0)
        << "one Artifact charge consumed";
    ASSERT_NE(player_power(s, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(player_power(s, PowerId::STRENGTH)->amount, 3)
        << "the Strength survives the nullified removal";
    EXPECT_EQ(s.relics[0].counter, 0)
        << "isActive is cleared regardless -- RedSkull.java:61 sits OUTSIDE "
           "the guarded block, and the relic never learns the -3 was eaten";
}

TEST(RelicHooks, RedSkullCrossingsAreCumulativeDeltasNotAnInvariant) {
    // Each cross-down applies +3 and each cross-up attempts -3; nothing
    // reconciles the two. With the -3 eaten by Artifact, a second cross-down
    // therefore leaves +6.
    CombatState s = MakeState();
    s.player_hp = 60;               // above half at entry -> armed
    s.player_powers[0] = PowerSlot{static_cast<uint16_t>(PowerId::ARTIFACT), 1,
                                   0, 0};
    s.player_power_count = 1;
    s.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::RED_SKULL), 0};
    s.relic_count = 1;
    dispatch_relics_at_battle_start(s, s.relics, s.relic_count);
    ASSERT_EQ(s.action_count, 0);

    s.player_hp = 35;               // 70 <= 80 -> first cross DOWN
    dispatch_relics_was_hp_lost(s, s.relics, s.relic_count, /*amount=*/25);
    drain(s);
    ASSERT_EQ(player_power(s, PowerId::STRENGTH)->amount, 3);

    heal_player_with_relics(s, 10);  // 45 -> cross UP, -3 eaten by Artifact
    drain(s);
    ASSERT_EQ(player_power(s, PowerId::ARTIFACT)->amount, 0);
    ASSERT_EQ(player_power(s, PowerId::STRENGTH)->amount, 3);
    ASSERT_EQ(s.relics[0].counter, 0) << "re-armed by the cross-up";

    s.player_hp = 39;               // 78 <= 80 -> second cross DOWN
    dispatch_relics_was_hp_lost(s, s.relics, s.relic_count, /*amount=*/6);
    drain(s);
    EXPECT_EQ(player_power(s, PowerId::STRENGTH)->amount, 6)
        << "+3, blocked -3, +3 -- deltas, not an invariant";
}

TEST(RelicHooks, RedSkullOutOfCombatCrossOnlyMovesTheLatch) {
    // Out of combat there are no powers to move (resetPlayer clears the list,
    // AbstractDungeon.java:1671) and RedSkull.onNotBloodied's -3 is gated on
    // RoomPhase.COMBAT (RedSkull.java:56) -- but `isActive = false` (:61) sits
    // outside that guard, so a run-layer cross-up still clears the latch. The
    // bridge to the next combat is the at_battle_start re-seed either way.
    RunState rs{};
    rs.max_hp = 80;
    rs.hp = 38;                     // 76 <= 80 -> bloodied out of combat
    rs.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::RED_SKULL), 1};
    rs.relic_count = 1;

    EXPECT_TRUE(rest_apply_heal(rs));  // (int)(80 * 0.3f) == 24 -> 62
    EXPECT_EQ(rs.hp, 62);
    EXPECT_EQ(rs.relics[0].counter, 0) << "the run-layer cross clears isActive";

    // ... and the NEXT combat opens armed, so nothing is granted at entry.
    CombatState s = MakeState();
    s.player_hp = rs.hp;
    s.player_max_hp = rs.max_hp;
    s.relics[0] = rs.relics[0];
    s.relic_count = 1;
    dispatch_relics_at_battle_start(s, s.relics, s.relic_count);
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(s.relics[0].counter, 0);

    // Negative control: a run-layer heal that does NOT lift the player above
    // half is no cross -- and the next combat is still entered bloodied, so it
    // opens with the entry grant.
    RunState rs2{};
    rs2.max_hp = 80;
    rs2.hp = 10;
    rs2.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::RED_SKULL), 1};
    rs2.relic_count = 1;
    EXPECT_TRUE(rest_apply_heal(rs2));  // 10 + 24 == 34, still <= 40
    EXPECT_EQ(rs2.hp, 34);
    EXPECT_EQ(rs2.relics[0].counter, 1) << "no cross -> latch untouched";

    CombatState s2 = MakeState();
    s2.player_hp = rs2.hp;
    s2.player_max_hp = rs2.max_hp;
    s2.relics[0] = rs2.relics[0];
    s2.relic_count = 1;
    dispatch_relics_at_battle_start(s2, s2.relics, s2.relic_count);
    ASSERT_EQ(s2.action_count, 1);
    EXPECT_EQ(queued(s2, 0).amount, 3);
    EXPECT_EQ(s2.relics[0].counter, 1);
}

TEST(RelicHooks, RedSkullReArmsAtEveryBattleStart) {
    // fold_back_combat persists the mirrored counter into the run's RelicSlot,
    // so a combat where Red Skull fired leaves counter == 1 behind. The game
    // resets isActive at every atBattleStart (RedSkull.java:37) and re-derives
    // isBloodied from starting HP (AbstractPlayer.java:1575): entering the
    // next combat above half HP must re-arm the grant.
    CombatState s = MakeState();
    s.player_hp = 60;               // 120 > 80 -> not bloodied at entry
    Relics r; r.add(RelicId::RED_SKULL);
    r.slots[0].counter = 1;         // stale isActive from the previous combat
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    EXPECT_EQ(r.slots[0].counter, 0) << "atBattleStart resets isActive";

    s.player_hp = 35;               // 70 <= 80 -> the cross happens in-combat
    dispatch_relics_was_hp_lost(s, r.slots, r.count, /*amount=*/25);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::APPLY_POWER));
    EXPECT_EQ(apply_power_id_from_flags(queued(s, 0).flags), PowerId::STRENGTH);
    EXPECT_EQ(queued(s, 0).amount, 3);
    EXPECT_EQ(r.slots[0].counter, 1);
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

// The grant is on the NINTH attack, not the tenth: PenNib.java:44-47 resets at
// ten WITHOUT granting, and :48-51 grants at nine -- so the TENTH attack is the
// empowered one.
TEST(RelicHooks, PenNibGrantsTheDoublingAfterTheNinthAttackNotTheTenth) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::PEN_NIB);
    const uint16_t strike = static_cast<uint16_t>(CardId::STRIKE);
    for (int i = 1; i <= 8; ++i) {
        dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);
        EXPECT_EQ(s.action_count, 0) << "granted early at attack " << i;
    }
    dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);  // 9th
    ASSERT_EQ(s.action_count, 1);
    const ActionQueueItem it = queued(s, 0);
    EXPECT_EQ(it.opcode, kOp(Opcode::APPLY_POWER));
    EXPECT_EQ(it.tgt, kActorPlayer);
    EXPECT_EQ(it.amount, 1);
    EXPECT_EQ(it.flags, make_apply_power_flags(PowerId::PEN_NIB));
    drain(s);
    ASSERT_NE(player_power(s, PowerId::PEN_NIB), nullptr);

    // The tenth attack resets the counter and grants nothing more (the power it
    // already holds is what makes that attack the empowered one).
    dispatch_relics_on_use_card(s, r.slots, r.count, strike, 0);  // 10th
    EXPECT_EQ(r.slots[0].counter, 0);
    EXPECT_EQ(s.action_count, 0);
}

// A SKILL never moves the counter and never grants (the ATTACK gate,
// PenNib.java:38).
TEST(RelicHooks, PenNibIgnoresNonAttacksEntirely) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::PEN_NIB, 8);
    dispatch_relics_on_use_card(s, r.slots, r.count,
                                static_cast<uint16_t>(CardId::SHRUG_IT_OFF), 0);
    EXPECT_EQ(r.slots[0].counter, 8);
    EXPECT_EQ(s.action_count, 0);
}

// atBattleStart re-grants when the RUN-persistent counter is already 9, and
// leaves the counter alone (PenNib.java:54-62). This is the case that makes the
// power visible on turn 1 of a fresh combat.
TEST(RelicHooks, PenNibReGrantsAtBattleStartWhenTheCounterIsNine) {
    {
        CombatState s = MakeState();
        Relics r; r.add(RelicId::PEN_NIB, 9);
        dispatch_relics_at_battle_start(s, r.slots, r.count);
        ASSERT_EQ(s.action_count, 1);
        EXPECT_EQ(queued(s, 0).flags, make_apply_power_flags(PowerId::PEN_NIB));
        EXPECT_EQ(r.slots[0].counter, 9) << "atBattleStart never writes counter";
        drain(s);
        EXPECT_NE(player_power(s, PowerId::PEN_NIB), nullptr);
    }
    for (const int16_t counter : {int16_t{0}, int16_t{8}, int16_t{10}}) {
        CombatState s = MakeState();
        Relics r; r.add(RelicId::PEN_NIB, counter);
        dispatch_relics_at_battle_start(s, r.slots, r.count);
        EXPECT_EQ(s.action_count, 0) << "counter " << counter;
    }
}

// --- Akabeko / Vigor ---------------------------------------------------------

// Akabeko.atBattleStart (Akabeko.java:30-35): unconditional Vigor 8.
TEST(RelicHooks, AkabekoGrantsEightVigorAtBattleStart) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::AKABEKO, -1);
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    ASSERT_EQ(s.action_count, 1);
    const ActionQueueItem it = queued(s, 0);
    EXPECT_EQ(it.opcode, kOp(Opcode::APPLY_POWER));
    EXPECT_EQ(it.src, kActorPlayer);
    EXPECT_EQ(it.tgt, kActorPlayer);
    EXPECT_EQ(it.amount, 8);
    EXPECT_EQ(it.flags, make_apply_power_flags(PowerId::VIGOR));
    drain(s);
    const PowerSlot* vigor = player_power(s, PowerId::VIGOR);
    ASSERT_NE(vigor, nullptr);
    EXPECT_EQ(vigor->amount, 8);
    EXPECT_EQ(r.slots[0].counter, -1) << "Akabeko never writes its counter";
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

// --- Preserved Insect -------------------------------------------------------

// PreservedInsect.atBattleStart (PreservedInsect.java:30-41): in an ELITE room
// only, clamp each monster's CURRENT health down to (int)((float)maxHealth *
// 0.75f). maxHealth is untouched, and the clamp never raises health.
TEST(RelicHooks, PreservedInsectClampsEliteMonsterHealthToThreeQuarters) {
    CombatState s = MakeState(/*monster_count=*/2, /*monster_hp=*/50);
    s.flags |= kCombatFlagEliteRoom;
    // A monster already BELOW the threshold is left exactly where it is: the
    // Java `continue` makes the clamp one-directional.
    s.monsters[1].hp = 10;
    Relics r; r.add(RelicId::PRESERVED_INSECT, -1);
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    EXPECT_EQ(s.action_count, 0) << "the clamp is synchronous, not queued";
    EXPECT_EQ(s.monsters[0].hp, 37) << "(int)(50.0f * 0.75f) == 37";
    EXPECT_EQ(s.monsters[0].max_hp, 50) << "maxHealth is never scaled";
    EXPECT_EQ(s.monsters[1].hp, 10);
    EXPECT_EQ(s.monsters[1].max_hp, 50);
}

// The float product is a C-style truncation of `(float)maxHealth * 0.75f`, not
// MathUtils.floor and not the integer maxHealth * 3 / 4. 90 -> 67.5f -> 67 is
// the worked case (an A20 Gremlin Nob); the two formulations happen to agree
// there, so the odd-multiple cases are pinned alongside it.
TEST(RelicHooks, PreservedInsectUsesTheGamesFloatTruncation) {
    const struct { int16_t max_hp; int16_t want; } cases[] = {
        {90, 67}, {85, 63}, {1, 0}, {3, 2}, {7, 5},
    };
    for (const auto& c : cases) {
        CombatState s = MakeState(/*monster_count=*/1, c.max_hp);
        s.flags |= kCombatFlagEliteRoom;
        Relics r; r.add(RelicId::PRESERVED_INSECT, -1);
        dispatch_relics_at_battle_start(s, r.slots, r.count);
        EXPECT_EQ(s.monsters[0].hp, c.want) << "max_hp " << c.max_hp;
    }
}

// A BOSS room does not set eliteTrigger (MonsterRoomBoss.java:22-24), so the
// flag is clear and this relic does nothing -- the same answer an ordinary
// monster room gives.
TEST(RelicHooks, PreservedInsectLeavesNonEliteMonstersAlone) {
    CombatState s = MakeState(/*monster_count=*/2, /*monster_hp=*/50);
    Relics r; r.add(RelicId::PRESERVED_INSECT, -1);
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    EXPECT_EQ(s.monsters[0].hp, 50);
    EXPECT_EQ(s.monsters[1].hp, 50);
    EXPECT_EQ(s.action_count, 0);
}

// --- Art of War --------------------------------------------------------------

// ArtOfWar (ArtOfWar.java:52-82): +1 energy at the start of turn N (N >= 2) iff
// no ATTACK was played during turn N-1. `firstTurn` suppresses turn 1 only.
TEST(RelicHooks, ArtOfWarGrantsEnergyOnlyAfterAnAttacklessTurn) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::ART_OF_WAR, -1);

    // Turn 1's atTurnStart: firstTurn suppresses the grant even though
    // gainEnergyNext is true.
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    EXPECT_EQ(s.action_count, 0) << "turn 1 never grants";
    ++s.turn;

    // Turn 1 passed with no attack -> turn 2 grants.
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::GAIN_ENERGY));
    EXPECT_EQ(queued(s, 0).amount, 1);
    EXPECT_EQ(queued(s, 0).tgt, kActorPlayer);
    drain(s);
    ++s.turn;

    // An ATTACK during turn 2 cancels turn 3's grant.
    dispatch_relics_on_use_card(s, r.slots, r.count,
                                static_cast<uint16_t>(CardId::STRIKE), 0);
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    EXPECT_EQ(s.action_count, 0) << "an attack last turn cancels the grant";
    ++s.turn;

    // ...and the latch is re-armed by that same atTurnStart, so turn 4 grants
    // again. Getting the Java's line order wrong (clearing before the test)
    // would have granted on turn 3 too.
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    EXPECT_EQ(s.action_count, 1);
    EXPECT_EQ(r.slots[0].counter, -1) << "the counter is never touched";
}

// A SKILL or POWER play does not cancel the grant -- only ATTACK does
// (ArtOfWar.java:78).
TEST(RelicHooks, ArtOfWarIgnoresNonAttackPlays) {
    CombatState s = MakeState();
    s.turn = 1;
    Relics r; r.add(RelicId::ART_OF_WAR, -1);
    dispatch_relics_on_use_card(s, r.slots, r.count,
                                static_cast<uint16_t>(CardId::SHRUG_IT_OFF), 0);
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    EXPECT_EQ(s.action_count, 1);
}

// --- Ancient Tea Set ---------------------------------------------------------

// AncientTeaSet.atTurnStart (AncientTeaSet.java:49-61): if the RUN-persistent
// counter is -2 (armed by onEnterRestRoom), the FIRST turn of the combat gains 2
// energy and the counter is spent to -1.
TEST(RelicHooks, AncientTeaSetSpendsTheArmedCounterOnTurnOne) {
    CombatState s = MakeState();
    Relics r; r.add(RelicId::ANCIENT_TEA_SET, -2);
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::GAIN_ENERGY));
    EXPECT_EQ(queued(s, 0).amount, 2);
    EXPECT_EQ(r.slots[0].counter, -1) << "-2 armed becomes -1 spent";
    drain(s);
    EXPECT_EQ(s.player_energy, 5);

    // Later turns of the SAME combat do nothing -- firstTurn is one-shot.
    ++s.turn;
    dispatch_relics_at_turn_start(s, r.slots, r.count);
    EXPECT_EQ(s.action_count, 0);
}

// An UNARMED tea set (counter -1, the spent value) grants nothing, and a turn
// that is not the first grants nothing even while armed.
TEST(RelicHooks, AncientTeaSetGrantsNothingUnarmedOrOffTurnOne) {
    {
        CombatState s = MakeState();
        Relics r; r.add(RelicId::ANCIENT_TEA_SET, -1);
        dispatch_relics_at_turn_start(s, r.slots, r.count);
        EXPECT_EQ(s.action_count, 0);
        EXPECT_EQ(r.slots[0].counter, -1);
    }
    {
        CombatState s = MakeState();
        s.turn = 3;
        Relics r; r.add(RelicId::ANCIENT_TEA_SET, -2);
        dispatch_relics_at_turn_start(s, r.slots, r.count);
        EXPECT_EQ(s.action_count, 0);
        EXPECT_EQ(r.slots[0].counter, -2) << "still armed for the NEXT combat";
    }
}

// --- Non-combat / deferred relics dispatch nothing ---------------------------

TEST(RelicHooks, NonCombatAndDeferredRelicsAreNoOps) {
    CombatState s = MakeState();
    Relics r;
    r.add(RelicId::WHETSTONE);        // equip-time, no combat hook
    r.add(RelicId::BOOT);             // live, but at a damage-pipeline site, not a hook
    r.add(RelicId::PRESERVED_INSECT); // live, but only in an elite room (flag clear here)
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

// --- Mummified Hand: a POWER play zeroes one random hand card's turn cost ------
//
// MummifiedHand.onUseCard (MummifiedHand.java:38-72). The acceptance points are
// (a) only a POWER play triggers it (:39), (b) the eligibility filter
// cost > 0 && costForTurn > 0 (:44), (c) the just-played card is NOT a candidate
// because it remains in actionManager.cardQueue until getNextAction removes it
// after AbstractPlayer.useCard returns (:50-54), (d) EXACTLY ONE
// cardRandomRng draw when a candidate
// exists (:61) and ZERO when none does (:56-64) -- the stream-desync guard --
// and (e) the discount is this-turn-only (setCostForTurn -> isCostModifiedForTurn,
// AbstractCard.java:2001-2011).

// Play `pi` as a card through the full §5.3 path (queue -> resolve), which is
// what fires the relic onUseCard fan-out (power_hooks.cpp).
void play_card(CombatState& s, CardPoolIndex pi) {
    CardQueueItem q{};
    q.card_index = pi;
    q.target = 0;
    resolve_card_play(s, q);
}

TEST(RelicHooksUncommonMummifiedHand, PowerPlayZeroesOneRandomEligibleHandCard) {
    CombatState s = MakeState();
    seed_card(s, 0, CardId::INFLAME);  // the played POWER
    seed_card(s, 1, CardId::BASH);     // cost 2 -- eligible
    seed_card(s, 2, CardId::CLEAVE);   // cost 1 -- eligible
    s.hand[0] = 0; s.hand[1] = 1; s.hand[2] = 2;
    s.hand_count = 3;
    mirror_relic(s, RelicId::MUMMIFIED_HAND);

    // Hand-derive the pick over an identical stream: two candidates, in hand
    // order (pool 1, pool 2), so random(0, 1) selects between them.
    RngStream ref = from_seed(2024);
    const int32_t expected = random(ref, 0, 1);
    s.card_random_rng = from_seed(2024);

    play_card(s, 0);

    EXPECT_EQ(s.card_random_rng.counter, 1)
        << "exactly one cardRandomRng draw (MummifiedHand.java:61)";
    const CardPoolIndex picked = static_cast<CardPoolIndex>(1 + expected);
    const CardPoolIndex other = static_cast<CardPoolIndex>(2 - expected);
    EXPECT_EQ(s.card_pool[picked].cost_now, 0) << "setCostForTurn(0)";
    EXPECT_TRUE(has_card_flag(s.card_pool[picked].flags,
                              CardFlag::COST_MODIFIED_FOR_TURN))
        << "the discount is this-turn-only (AbstractCard.java:2007-2009)";
    const CardDef* od = card_def(static_cast<CardId>(s.card_pool[other].card_id));
    ASSERT_NE(od, nullptr);
    EXPECT_EQ(s.card_pool[other].cost_now, card_cost(*od, 0))
        << "only ONE card is discounted";
}

TEST(RelicHooksUncommonMummifiedHand, PlayedPowerIsNotItsOwnCandidate) {
    // The engine dispatches onUseCard while the source card is still in hand
    // (card_play.cpp step 5 precedes the move to limbo), matching Java where
    // UseCardAction's constructor fires the hook before hand.removeCard. Java
    // excludes the source via cardQueue; this direct-hook test supplies the
    // equivalent source identity through the hook context. With nothing else
    // in hand there is NO candidate, so no draw happens and the power keeps
    // its cost.
    CombatState s = MakeState();
    seed_card(s, 0, CardId::INFLAME);
    s.hand[0] = 0;
    s.hand_count = 1;
    mirror_relic(s, RelicId::MUMMIFIED_HAND);
    s.card_random_rng = from_seed(7);
    const int32_t before = s.card_random_rng.counter;

    dispatch_relics_on_use_card(s, s.relics, s.relic_count,
                                static_cast<uint16_t>(CardId::INFLAME), 0);

    EXPECT_EQ(s.card_random_rng.counter, before)
        << "empty candidate list draws nothing (MummifiedHand.java:56-64)";
    const CardDef* d = card_def(CardId::INFLAME);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(s.card_pool[0].cost_now, card_cost(*d, 0));
}

TEST(RelicHooksUncommonMummifiedHand, IneligibleHandConsumesNoCardRandomRngDraw) {
    // cost == 0 (Injury: the game's -2 sentinel -> UNPLAYABLE, base_cost 0) and
    // costForTurn == 0 (an already-discounted instance) are both filtered out
    // (:44), leaving an empty list -- and therefore no draw at all.
    CombatState s = MakeState();
    seed_card(s, 0, CardId::INFLAME);
    seed_card(s, 1, CardId::INJURY);
    seed_card(s, 2, CardId::DEFEND);
    s.card_pool[2].cost_now = 0;  // costForTurn already 0
    s.hand[0] = 0; s.hand[1] = 1; s.hand[2] = 2;
    s.hand_count = 3;
    mirror_relic(s, RelicId::MUMMIFIED_HAND);
    s.card_random_rng = from_seed(11);
    const int32_t before = s.card_random_rng.counter;

    dispatch_relics_on_use_card(s, s.relics, s.relic_count,
                                static_cast<uint16_t>(CardId::INFLAME), 0);

    EXPECT_EQ(s.card_random_rng.counter, before);
    EXPECT_EQ(s.card_pool[1].cost_now, 0);
    EXPECT_EQ(s.card_pool[2].cost_now, 0);
    EXPECT_FALSE(has_card_flag(s.card_pool[2].flags,
                               CardFlag::COST_MODIFIED_FOR_TURN));
}

TEST(RelicHooksUncommonMummifiedHand, CardQueueMembersAreExcluded) {
    // MummifiedHand.java:50-54 removes every cardQueue member from the
    // candidate list. Parking Cleave in the cardQueue leaves Bash as the only
    // candidate -- still ONE draw (random(0, 0) is called for a non-empty list).
    CombatState s = MakeState();
    seed_card(s, 0, CardId::INFLAME);
    seed_card(s, 1, CardId::BASH);
    seed_card(s, 2, CardId::CLEAVE);
    s.hand[0] = 0; s.hand[1] = 1; s.hand[2] = 2;
    s.hand_count = 3;
    CardQueueItem pending{};
    pending.card_index = 2;
    add_card_to_queue_bottom(s, pending);
    mirror_relic(s, RelicId::MUMMIFIED_HAND);
    s.card_random_rng = from_seed(5);

    dispatch_relics_on_use_card(s, s.relics, s.relic_count,
                                static_cast<uint16_t>(CardId::INFLAME), 0);

    EXPECT_EQ(s.card_random_rng.counter, 1);
    EXPECT_EQ(s.card_pool[1].cost_now, 0) << "Bash was the only candidate";
    EXPECT_EQ(s.card_pool[2].cost_now, card_cost(*card_def(CardId::CLEAVE), 0))
        << "the queued card is untouched";
}

TEST(RelicHooksUncommonMummifiedHand, DiscountRevertsAtEndOfTurn) {
    // isCostModifiedForTurn -> AbstractRoom.endTurn:397-405 restores costForTurn.
    CombatState s = MakeState();
    seed_card(s, 0, CardId::INFLAME);
    seed_card(s, 1, CardId::BASH);
    s.hand[0] = 0; s.hand[1] = 1;
    s.hand_count = 2;
    mirror_relic(s, RelicId::MUMMIFIED_HAND);
    s.card_random_rng = from_seed(3);

    dispatch_relics_on_use_card(s, s.relics, s.relic_count,
                                static_cast<uint16_t>(CardId::INFLAME), 0);
    ASSERT_EQ(s.card_pool[1].cost_now, 0);

    reset_cost_for_turn(s, 1);
    EXPECT_EQ(s.card_pool[1].cost_now, card_cost(*card_def(CardId::BASH), 0));
    EXPECT_FALSE(has_card_flag(s.card_pool[1].flags,
                               CardFlag::COST_MODIFIED_FOR_TURN));
}

// --- Deferred / run-layer uncommons are combat no-ops --------------------------

TEST(RelicHooksUncommon, DeferredAndRunLayerUncommonsAreCombatNoOps) {
    CombatState s = MakeState();
    Relics r;
    // Mummified Hand's body is implemented; it stays in this list because its
    // POWER gate (MummifiedHand.java:39) must leave the Strike play below inert.
    r.add(RelicId::MUMMIFIED_HAND);
    // Pantograph is NOT deferred any more -- relics_uncommon.cpp carries its
    // body and RelicHooksPantograph.HealsTwentyFiveAtBossBattleStart pins it. It
    // stays in this list for the same reason as Mummified Hand: its GATE must
    // leave this fixture inert. MakeState's group is Jaw Worms, whose enemy_type
    // is NORMAL (AbstractMonster.java:99), so the BOSS scan at
    // Pantograph.java:33-34 finds nothing and queues no heal.
    r.add(RelicId::PANTOGRAPH);
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

// --- Frozen Egg upgrades an obtained POWER card -------------------------------
//
// FrozenEgg2.onObtainCard (FrozenEgg2.java:46-50): `c.type == POWER &&
// c.canUpgrade() && !c.upgraded -> c.upgrade()`. Identical in shape to
// MoltenEgg2/ToxicEgg2 (:46-50), which gate on ATTACK / SKILL respectively.

TEST(RunDeckFrozenEgg, UpgradesAnObtainedPowerAndLeavesOtherTypesAlone) {
    RunState rs{};
    rs.hp = 60;
    rs.max_hp = 80;
    rs.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::FROZEN_EGG), 0};
    rs.relic_count = 1;

    // A POWER obtain arrives upgraded.
    ASSERT_TRUE(add_card_to_master_deck(rs, CardId::INFLAME));
    EXPECT_EQ(rs.master_deck[0].upgrade, 1);
    // ATTACK / SKILL obtains are Molten/Toxic Egg's business, not Frozen Egg's.
    ASSERT_TRUE(add_card_to_master_deck(rs, CardId::CLEAVE));
    EXPECT_EQ(rs.master_deck[1].upgrade, 0);
    ASSERT_TRUE(add_card_to_master_deck(rs, CardId::SHRUG_IT_OFF));
    EXPECT_EQ(rs.master_deck[2].upgrade, 0);
    // An already-upgraded POWER is left alone (the !c.upgraded guard).
    ASSERT_TRUE(add_card_to_master_deck(rs, CardId::METALLICIZE, /*upgrade=*/1));
    EXPECT_EQ(rs.master_deck[3].upgrade, 1);
    // No HP side effect -- that is Darkstone Periapt's CURSE branch.
    EXPECT_EQ(rs.max_hp, 80);
    EXPECT_EQ(rs.hp, 60);
}

TEST(RunDeckFrozenEgg, WithoutTheRelicPowersAreObtainedUnupgraded) {
    RunState rs{};
    rs.hp = 60;
    rs.max_hp = 80;
    ASSERT_TRUE(add_card_to_master_deck(rs, CardId::INFLAME));
    EXPECT_EQ(rs.master_deck[0].upgrade, 0);
}

// --- Pantograph: heal 25 at the start of a BOSS fight -------------------------
//
// Pantograph.atBattleStart (Pantograph.java:32-40) walks the monster group and,
// on the FIRST member with m.type == EnemyType.BOSS, addToTop a
// HealAction(player, player, 25, 0.0f), then returns (:36-38). HEAL_AMT is 25
// (Pantograph.java:20). The test is on the MONSTER's EnemyType, never on the
// room: Pantograph does not reference AbstractRoom / MonsterRoomBoss at all. The
// engine answers that question from registry data -- registry/monsters.yaml's
// `enemy_type` column (AbstractMonster.java:99's `type` field) reaching
// MonsterDef::is_boss().

TEST(RelicHooksPantograph, HealsTwentyFiveAtBossBattleStart) {
    CombatState s = MakeState();          // hp 70 / max 80, one monster
    s.player_max_hp = 100;                // headroom so the full 25 lands
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SLIME_BOSS);
    Relics r; r.add(RelicId::PANTOGRAPH);
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    EXPECT_EQ(s.player_hp, 95);           // 70 + 25
    EXPECT_EQ(s.action_count, 0) << "the heal is applied directly, not queued";
}

// The clamp is AbstractCreature.heal's `if (currentHealth > maxHealth)
// currentHealth = maxHealth;` (AbstractCreature.java:401-403), reached through
// HealAction.update (HealAction.java:31-33). Nothing else touches the amount in
// S1 scope: the FullBelly halving is Endless-only (:387-389) and no S1 relic or
// power binds onPlayerHeal / onHeal (:393-399).
TEST(RelicHooksPantograph, HealIsClampedToMaxHp) {
    Relics r; r.add(RelicId::PANTOGRAPH);
    CombatState s = MakeState();          // max 80
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SLIME_BOSS);
    s.player_hp = 75;                     // 75 + 25 = 100 > max 80
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    EXPECT_EQ(s.player_hp, 80);
    // A full-HP player gains nothing at all.
    CombatState full = MakeState();
    full.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SLIME_BOSS);
    full.player_hp = full.player_max_hp;
    dispatch_relics_at_battle_start(full, r.slots, r.count);
    EXPECT_EQ(full.player_hp, full.player_max_hp);
}

// `if (m.type != EnemyType.BOSS) continue;` (Pantograph.java:34): an ordinary
// fight falls out of the loop having done nothing.
TEST(RelicHooksPantograph, DoesNotHealInANormalFight) {
    CombatState s = MakeState(/*monster_count=*/2);  // Jaw Worm + Cultist
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::CULTIST);
    Relics r; r.add(RelicId::PANTOGRAPH);
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    EXPECT_EQ(s.player_hp, 70) << "no BOSS-typed monster -> no heal";
    EXPECT_EQ(s.action_count, 0);
}

// An ELITE fight is, to Pantograph, an ordinary fight: the Java compares against
// BOSS specifically, so ELITE takes the same `continue` branch NORMAL does.
// Lagavulin (Lagavulin.java:75) is the first REAL ELITE-typed row -- the other
// two Exordium elites (GremlinNob.java:63 / Sentry.java:61) are still
// unimplemented -- so it is checked here next to the synthetic one, pinning that
// is_boss() means BOSS and not "anything above NORMAL".
TEST(RelicHooksPantograph, EliteEnemyTypeIsNotABossFight) {
    const sts::registry::MonsterDef* lagavulin =
        sts::registry::monster_def(MonsterId::LAGAVULIN);
    ASSERT_NE(lagavulin, nullptr);
    EXPECT_EQ(lagavulin->enemy_type, sts::registry::MonsterEnemyType::ELITE);
    EXPECT_FALSE(lagavulin->is_boss());

    sts::registry::MonsterDef elite = sts::registry::kSlimeBoss;
    elite.enemy_type = sts::registry::MonsterEnemyType::ELITE;
    EXPECT_FALSE(elite.is_boss());
    sts::registry::MonsterDef normal = sts::registry::kSlimeBoss;
    normal.enemy_type = sts::registry::MonsterEnemyType::NORMAL;
    EXPECT_FALSE(normal.is_boss());
    EXPECT_TRUE(sts::registry::kSlimeBoss.is_boss());
}

// Mixed group: the boss need not be first, and Pantograph fires exactly once
// (the Java `return` at :38, not a heal per boss-typed member).
TEST(RelicHooksPantograph, HealsOnceForABossFoundLaterInTheGroup) {
    Relics r; r.add(RelicId::PANTOGRAPH);
    CombatState s = MakeState(/*monster_count=*/3);
    s.player_max_hp = 100;
    s.monsters[2].monster_id = static_cast<uint16_t>(MonsterId::SLIME_BOSS);
    dispatch_relics_at_battle_start(s, r.slots, r.count);
    EXPECT_EQ(s.player_hp, 95);  // 70 + 25 -- one heal, not three

    // Two BOSS-typed members still heal 25 total, not 50.
    CombatState two = MakeState(/*monster_count=*/2);
    two.player_max_hp = 100;
    two.player_hp = 40;
    two.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SLIME_BOSS);
    two.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::SLIME_BOSS);
    dispatch_relics_at_battle_start(two, r.slots, r.count);
    EXPECT_EQ(two.player_hp, 65);
}

// Pantograph binds atBattleStart and nothing else -- it overrides neither
// onEquip (the ctor at Pantograph.java:22-24 is its whole equip behaviour) nor
// atBattleStartPreDraw (the separate AbstractRelic hook). Dispatching the other
// hooks through the public entry points must leave HP untouched, which pins both
// the registry hook inventory and the body's own hook guard.
TEST(RelicHooksPantograph, OnlyRespondsToAtBattleStart) {
    Relics r; r.add(RelicId::PANTOGRAPH);
    CombatState turn = MakeState();
    turn.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SLIME_BOSS);
    dispatch_relics_at_turn_start(turn, r.slots, r.count);
    EXPECT_EQ(turn.player_hp, 70);

    CombatState end = MakeState();
    end.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SLIME_BOSS);
    dispatch_relics_on_player_end_turn(end, r.slots, r.count);
    EXPECT_EQ(end.player_hp, 70);

    CombatState win = MakeState();
    win.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SLIME_BOSS);
    dispatch_relics_on_victory(win, r.slots, r.count);
    EXPECT_EQ(win.player_hp, 70);
}

}  // namespace
}  // namespace sts::engine
