// Tier-2 suite for the RARE + SHOP relic tiers (registry/relics.yaml ids 66-110)
// plus the SPECIAL-tier Odd Mushroom (111). One or more tests per relic whose
// behaviour is live, hand-derived from the cited decompiled Java; the relics
// whose bodies are deliberately deferred are asserted to be exactly INERT, so a
// later task that implements one will see this suite fail rather than silently
// change behaviour nobody was watching.
//
// The registry table itself (tiers, pool_order contiguity, hook bindings) is
// covered by registry_gen_test; the shuffled pool ORDER against live oracle
// captures is covered by relic_pools_test. What is here is the mechanics.

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"       // legal_actions (Medical Kit playability)
#include "sts/engine/card_play.hpp"     // resolve_card_play (Chemical X / Spoon)
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/piles.hpp"         // shuffle_discard_into_draw (Abacus)
#include "sts/engine/potions.hpp"       // use_potion (the Gambling Chip / Gambler's Brew twin)
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/relic_hooks.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/relics.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_deck.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

constexpr uint16_t kOp(Opcode o) { return static_cast<uint16_t>(o); }

// A relic list in acquisition order (index 0 == acquired first).
struct Relics {
    RelicSlot slots[kRelicCap]{};
    uint8_t count = 0;
    void add(RelicId id, int16_t counter = -1) {
        slots[count].relic_id = static_cast<uint16_t>(id);
        slots[count].counter = counter;
        ++count;
    }
};

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

// Put the relic list into the combat mirror so the sites that read
// player_has_relic / player_relics (the damage pipeline, card play, the queue)
// see it. Returns a view over the mirror, which is what the dispatch helpers
// take.
RelicView install(CombatState& s, const Relics& r) {
    for (uint8_t i = 0; i < r.count; ++i) {
        s.relics[i] = r.slots[i];
    }
    s.relic_count = r.count;
    return player_relics(s);
}

RelicView give(CombatState& s, RelicId id, int16_t counter = -1) {
    Relics r;
    r.add(id, counter);
    return install(s, r);
}

ActionQueueItem queued(const CombatState& s, uint8_t i) {
    return s.action_queue[(s.action_head + i) % kActionQueueCap];
}

void drain(CombatState& s) {
    ActionQueueItem it{};
    while (pop_action_front(s, it)) {
        execute_opcode(s, it);
    }
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

void add_power(CombatState& s, PowerId id, int16_t amount) {
    s.player_powers[s.player_power_count].power_id = static_cast<uint16_t>(id);
    s.player_powers[s.player_power_count].amount = amount;
    ++s.player_power_count;
}

// Put `id` into hand at the first free card-pool slot and return that slot.
CardPoolIndex put_in_hand(CombatState& s, CardId id) {
    CardPoolIndex pi = 0;
    while (pi < kCardPoolCap &&
           s.card_pool[pi].card_id != static_cast<uint16_t>(CardId::NONE)) {
        ++pi;
    }
    const CardDef* def = card_def(id);
    s.card_pool[pi].card_id = static_cast<uint16_t>(id);
    s.card_pool[pi].cost_now = card_cost(*def, 0);
    s.card_pool[pi].flags = card_flags(*def, 0);
    s.hand[s.hand_count++] = pi;
    return pi;
}

// The opcode bodies are internal to sts_engine, so a test drives them the way
// the pump does: build the queue item and execute it.
void op_damage(CombatState& s, uint8_t src, uint8_t tgt, int amount,
               int /*strength_mult*/ = 1,
               DamageType type = DamageType::NORMAL) {
    ActionQueueItem it{};
    it.opcode = kOp(Opcode::DAMAGE);
    it.src = src;
    it.tgt = tgt;
    it.amount = amount;
    it.flags = make_damage_flags(type);
    execute_opcode(s, it);
}

void op_lose_hp(CombatState& s, uint8_t tgt, int amount) {
    ActionQueueItem it{};
    it.opcode = kOp(Opcode::LOSE_HP);
    it.src = tgt;
    it.tgt = tgt;
    it.amount = amount;
    execute_opcode(s, it);
}

void op_apply_power(CombatState& s, uint8_t src, uint8_t tgt, PowerId id,
                    int amount) {
    ActionQueueItem it{};
    it.opcode = kOp(Opcode::APPLY_POWER);
    it.src = src;
    it.tgt = tgt;
    it.amount = amount;
    it.flags = make_apply_power_flags(id);
    execute_opcode(s, it);
}

// ============================================================================
// Registry shape -- the tiers are complete and their rows carry what the
// mechanics below assume.
// ============================================================================

TEST(RelicRaresShop, TierRostersAreExactlyTheLivePools) {
    int rare = 0;
    int shop = 0;
    for (const sts::registry::RelicDef* d : sts::registry::kRelicDefs) {
        if (d->tier == sts::registry::RelicTier::RARE) {
            ++rare;
            EXPECT_GE(d->pool_order, 0) << "every RARE row is poolable";
        } else if (d->tier == sts::registry::RelicTier::SHOP) {
            ++shop;
            EXPECT_GE(d->pool_order, 0) << "every SHOP row is poolable";
        }
    }
    // The live oracle's relicPools.rare / .shop for an Ironclad run.
    EXPECT_EQ(rare, 28);
    EXPECT_EQ(shop, 17);

    // Odd Mushroom is SPECIAL and in NO dungeon pool -- it is granted by an
    // event, so a pool_order would give it a relicRng-visible slot it must not
    // have (OddMushroom.java:12-28, RelicTier.SPECIAL).
    const sts::registry::RelicDef* om = relic_def(RelicId::ODD_MUSHROOM);
    ASSERT_NE(om, nullptr);
    EXPECT_EQ(om->tier, sts::registry::RelicTier::SPECIAL);
    EXPECT_EQ(om->pool_order, -1);

    // Counter seeds that a mechanic below depends on.
    EXPECT_EQ(relic_def(RelicId::WING_BOOTS)->initial_counter, 3);
    EXPECT_EQ(relic_def(RelicId::GIRYA)->initial_counter, 0);
    EXPECT_EQ(relic_def(RelicId::DU_VU_DOLL)->initial_counter, 0);
    EXPECT_EQ(relic_def(RelicId::INCENSE_BURNER)->initial_counter, 0);
    // Lizard Tail's armed state IS AbstractRelic's default counter of -1
    // (LizardTail has no constructor assignment), so the row must NOT override it.
    EXPECT_EQ(relic_def(RelicId::LIZARD_TAIL)->initial_counter, -1);
}

// ============================================================================
// Rare -- battle-start and turn-start relics
// ============================================================================

// FossilizedHelix.atBattleStart (FossilizedHelix.java:31-36): Buffer 1.
// BufferPower.onAttackedToChangeDamage (BufferPower.java:44-47): the next hit
// that would land is cancelled to 0 and one stack is spent via a top-queued
// ReducePowerAction.
TEST(RelicRaresShop, FossilizedHelixGrantsBufferWhichEatsOneHit) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::FOSSILIZED_HELIX);
    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    drain(s);
    const PowerSlot* buf = player_power(s, PowerId::BUFFER);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(buf->amount, 1);

    op_damage(s, 0, kActorPlayer, 12);
    EXPECT_EQ(s.player_hp, 70) << "Buffer cancels the whole hit";
    drain(s);  // the queued ReducePowerAction
    EXPECT_EQ(player_power(s, PowerId::BUFFER), nullptr) << "stack spent";

    op_damage(s, 0, kActorPlayer, 12);
    EXPECT_EQ(s.player_hp, 58) << "no Buffer left";
}

// Buffer sits AFTER decrementBlock (AbstractPlayer.java:1401 then :1412-1415):
// block is spent first, and a hit fully absorbed by block leaves the stack alone
// because BufferPower only queues its ReducePower when damageAmount > 0.
TEST(RelicRaresShop, BufferRunsAfterBlockAndAFullyBlockedHitSpendsNoStack) {
    CombatState s = MakeState();
    give(s, RelicId::FOSSILIZED_HELIX);
    add_power(s, PowerId::BUFFER, 1);
    s.player_block = 20;
    op_damage(s, 0, kActorPlayer, 12);
    EXPECT_EQ(s.player_block, 8) << "block absorbed the hit first";
    EXPECT_EQ(s.player_hp, 70);
    EXPECT_EQ(s.action_count, 0) << "no ReducePower queued for a blocked hit";
    ASSERT_NE(player_power(s, PowerId::BUFFER), nullptr);
    EXPECT_EQ(player_power(s, PowerId::BUFFER)->amount, 1);
}

// ThreadAndNeedle.atBattleStart (ThreadAndNeedle.java:31-35): Plated Armor 4.
TEST(RelicRaresShop, ThreadAndNeedleGrantsPlatedArmorFour) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::THREAD_AND_NEEDLE);
    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    drain(s);
    ASSERT_NE(player_power(s, PowerId::PLATED_ARMOR), nullptr);
    EXPECT_EQ(player_power(s, PowerId::PLATED_ARMOR)->amount, 4);
}

// ClockworkSouvenir.atBattleStart (ClockworkSouvenir.java:30-34): Artifact 1,
// which then nullifies exactly one incoming debuff.
TEST(RelicRaresShop, ClockworkSouvenirGrantsArtifactOne) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::CLOCKWORK_SOUVENIR);
    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    drain(s);
    ASSERT_NE(player_power(s, PowerId::ARTIFACT), nullptr);
    EXPECT_EQ(player_power(s, PowerId::ARTIFACT)->amount, 1);
    op_apply_power(s, 0, kActorPlayer, PowerId::VULNERABLE, 2);
    EXPECT_EQ(player_power(s, PowerId::VULNERABLE), nullptr) << "nullified";
    // The spent Artifact slot is left in place at 0 charges -- power_hooks.cpp
    // documents why (a 0-amount slot already reads as "no charges", and the pump
    // has no power-GC pass), so the assertion is on the AMOUNT, not the slot.
    ASSERT_NE(player_power(s, PowerId::ARTIFACT), nullptr);
    EXPECT_EQ(player_power(s, PowerId::ARTIFACT)->amount, 0) << "stack consumed";
}

// DuVuDoll.atBattleStart (DuVuDoll.java:69-75): Strength == the master deck's
// CURSE count, and NOTHING at all when that count is 0 (`if (counter > 0)`).
TEST(RelicRaresShop, DuVuDollGrantsStrengthEqualToTheCurseCount) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::DU_VU_DOLL, /*counter=*/3);
    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    drain(s);
    ASSERT_NE(player_power(s, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(player_power(s, PowerId::STRENGTH)->amount, 3);

    CombatState none = MakeState();
    const RelicView rv0 = give(none, RelicId::DU_VU_DOLL, /*counter=*/0);
    dispatch_relics_at_battle_start(none, rv0.relics, rv0.count);
    EXPECT_EQ(none.action_count, 0) << "counter 0 queues nothing";
}

// DuVuDoll.onEquip / onMasterDeckChange (DuVuDoll.java:43-66): the counter is
// recomputed from the master deck on pickup AND on every later deck edit.
TEST(RelicRaresShop, DuVuDollCounterTracksTheMasterDeck) {
    RunState rs{};
    RngStream misc = from_seed(1);
    ASSERT_TRUE(add_card_to_master_deck(rs, CardId::STRIKE));
    ASSERT_TRUE(add_card_to_master_deck(rs, CardId::REGRET));
    ASSERT_EQ(acquire_relic(rs, misc, RelicId::DU_VU_DOLL),
              RelicAcquireResult::ACQUIRED);
    EXPECT_EQ(rs.relics[0].counter, 1) << "onEquip counts the curse already held";

    ASSERT_TRUE(add_card_to_master_deck(rs, CardId::SHAME));
    EXPECT_EQ(rs.relics[0].counter, 2) << "onMasterDeckChange on add";

    ASSERT_TRUE(remove_master_deck_card(rs, 1));  // drop Regret
    EXPECT_EQ(rs.relics[0].counter, 1) << "onMasterDeckChange on remove";
}

// Girya.atBattleStart (Girya.java:38-44) tests `counter != 0`, not `> 0`, and
// the campfire Lift that raises the counter is not implemented -- so with a
// fresh Girya the branch is a LIVE no-op rather than an unreachable one.
TEST(RelicRaresShop, GiryaGrantsStrengthOnlyOnceLifted) {
    CombatState fresh = MakeState();
    const RelicView rv0 = give(fresh, RelicId::GIRYA, /*counter=*/0);
    dispatch_relics_at_battle_start(fresh, rv0.relics, rv0.count);
    EXPECT_EQ(fresh.action_count, 0);

    CombatState lifted = MakeState();
    const RelicView rv = give(lifted, RelicId::GIRYA, /*counter=*/2);
    dispatch_relics_at_battle_start(lifted, rv.relics, rv.count);
    drain(lifted);
    ASSERT_NE(player_power(lifted, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(player_power(lifted, PowerId::STRENGTH)->amount, 2);
}

// CaptainsWheel (CaptainsWheel.java:31-53): armed at battle start, 18 block on
// the THIRD turn start, then latched off for the rest of the combat.
TEST(RelicRaresShop, CaptainsWheelGivesEighteenBlockOnTurnThreeOnlyOnce) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::CAPTAINS_WHEEL);
    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    EXPECT_EQ(rv.relics[0].counter, 0);
    for (int turn = 1; turn <= 2; ++turn) {
        dispatch_relics_at_turn_start(s, rv.relics, rv.count);
        EXPECT_EQ(s.action_count, 0) << "nothing before turn 3, turn=" << turn;
    }
    dispatch_relics_at_turn_start(s, rv.relics, rv.count);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::BLOCK));
    EXPECT_EQ(queued(s, 0).amount, 18);
    EXPECT_NE(queued(s, 0).flags & kBlockNoPowers, 0u)
        << "a direct GainBlockAction takes no Dexterity";
    drain(s);
    EXPECT_EQ(rv.relics[0].counter, -1) << "grayscale latch";

    for (int turn = 4; turn <= 8; ++turn) {
        dispatch_relics_at_turn_start(s, rv.relics, rv.count);
        EXPECT_EQ(s.action_count, 0) << "latched off, turn=" << turn;
    }
    dispatch_relics_on_victory(s, rv.relics, rv.count);
    EXPECT_EQ(rv.relics[0].counter, -1);
}

// StoneCalendar (StoneCalendar.java:36-68): 52 THORNS to every enemy at the
// END of turn 7, exactly once (the `counter == 7` equality plus the
// unconditional increment).
TEST(RelicRaresShop, StoneCalendarFiresFiftyTwoThornsAtEndOfTurnSeven) {
    CombatState s = MakeState(2, /*monster_hp=*/200);
    const RelicView rv = give(s, RelicId::STONE_CALENDAR);
    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    for (int turn = 1; turn <= 6; ++turn) {
        dispatch_relics_at_turn_start(s, rv.relics, rv.count);
        dispatch_relics_on_player_end_turn(s, rv.relics, rv.count);
        EXPECT_EQ(s.action_count, 0) << "silent through turn " << turn;
    }
    dispatch_relics_at_turn_start(s, rv.relics, rv.count);   // turn 7
    dispatch_relics_on_player_end_turn(s, rv.relics, rv.count);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::DAMAGE));
    EXPECT_EQ(queued(s, 0).amount, 52);
    drain(s);
    EXPECT_EQ(s.monsters[0].hp, 148);
    EXPECT_EQ(s.monsters[1].hp, 148);

    dispatch_relics_at_turn_start(s, rv.relics, rv.count);   // turn 8
    dispatch_relics_on_player_end_turn(s, rv.relics, rv.count);
    EXPECT_EQ(s.action_count, 0) << "counter moved past 7; never fires again";
}

// StoneCalendar's THORNS damage skips the NORMAL-only applyPowers pipeline: the
// player's Strength does not scale it and the monster's Vulnerable does not
// amplify it (DamageInfo.applyPowers is guarded on type == NORMAL).
TEST(RelicRaresShop, StoneCalendarDamageIsUnscaledThorns) {
    CombatState s = MakeState(1, /*monster_hp=*/200);
    const RelicView rv = give(s, RelicId::STONE_CALENDAR);
    add_power(s, PowerId::STRENGTH, 5);
    s.monsters[0].powers[0].power_id = static_cast<uint16_t>(PowerId::VULNERABLE);
    s.monsters[0].powers[0].amount = 3;
    s.monsters[0].power_count = 1;
    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    for (int turn = 1; turn <= 7; ++turn) {
        dispatch_relics_at_turn_start(s, rv.relics, rv.count);
    }
    dispatch_relics_on_player_end_turn(s, rv.relics, rv.count);
    drain(s);
    EXPECT_EQ(s.monsters[0].hp, 148) << "flat 52, no Strength, no Vulnerable";
}

// IncenseBurner (IncenseBurner.java:31-44): Intangible 1 on every SIXTH turn
// start, counter resetting each time.
TEST(RelicRaresShop, IncenseBurnerGrantsIntangibleEverySixthTurn) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::INCENSE_BURNER, /*counter=*/0);
    for (int turn = 1; turn <= 5; ++turn) {
        dispatch_relics_at_turn_start(s, rv.relics, rv.count);
        EXPECT_EQ(s.action_count, 0) << "turn " << turn;
        EXPECT_EQ(rv.relics[0].counter, turn);
    }
    dispatch_relics_at_turn_start(s, rv.relics, rv.count);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::APPLY_POWER));
    drain(s);
    EXPECT_EQ(rv.relics[0].counter, 0) << "reset after firing";
    ASSERT_NE(player_power(s, PowerId::INTANGIBLE), nullptr);
    EXPECT_EQ(player_power(s, PowerId::INTANGIBLE)->amount, 1);

    // Twelve turns in total -> exactly two procs.
    for (int turn = 7; turn <= 12; ++turn) {
        dispatch_relics_at_turn_start(s, rv.relics, rv.count);
    }
    drain(s);
    EXPECT_EQ(player_power(s, PowerId::INTANGIBLE)->amount, 2);
}

// The -1 counter branch (`counter == -1 ? counter + 2 : counter + 1`,
// IncenseBurner.java:37) is the save-loaded/unset path: it lands on 1, not 0,
// so a restored relic does not lose a turn.
TEST(RelicRaresShop, IncenseBurnerMinusOneCounterAdvancesByTwo) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::INCENSE_BURNER, /*counter=*/-1);
    dispatch_relics_at_turn_start(s, rv.relics, rv.count);
    EXPECT_EQ(rv.relics[0].counter, 1);
}

// IntangiblePlayerPower.atDamageFinalReceive (:43-49) caps NORMAL damage at 1
// BEFORE block; atEndOfRound (:51-55) ticks the duration down and removes it.
TEST(RelicRaresShop, IntangibleCapsDamageAtOneAndExpiresAtRoundEnd) {
    CombatState s = MakeState();
    add_power(s, PowerId::INTANGIBLE, 1);
    op_damage(s, 0, kActorPlayer, 30);
    EXPECT_EQ(s.player_hp, 69) << "30 -> 1";
    // HP_LOSS skips applyPowers, so it is the separate AbstractPlayer.damage
    // guard (:1397-1399) that catches this one -- and it must.
    op_lose_hp(s, kActorPlayer, 30);
    EXPECT_EQ(s.player_hp, 68);
    dispatch_at_end_of_round(s);
    drain(s);
    EXPECT_EQ(player_power(s, PowerId::INTANGIBLE), nullptr) << "expired";
    op_damage(s, 0, kActorPlayer, 30);
    EXPECT_EQ(s.player_hp, 38);
}

// Brimstone.atTurnStart (Brimstone.java:44-51): +2 Strength to the player and +1
// to every monster. The three addToTop calls REVERSE, so the resolution order is
// the last monster, the earlier monsters, then the player.
TEST(RelicRaresShop, BrimstoneBuffsPlayerTwoAndEveryMonsterOne) {
    CombatState s = MakeState(3);
    const RelicView rv = give(s, RelicId::BRIMSTONE);
    dispatch_relics_at_turn_start(s, rv.relics, rv.count);
    ASSERT_EQ(s.action_count, 4);
    EXPECT_EQ(queued(s, 0).tgt, 2) << "last addToTop resolves first";
    EXPECT_EQ(queued(s, 1).tgt, 1);
    EXPECT_EQ(queued(s, 2).tgt, 0);
    EXPECT_EQ(queued(s, 3).tgt, kActorPlayer) << "the player's +2 resolves last";
    drain(s);
    ASSERT_NE(player_power(s, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(player_power(s, PowerId::STRENGTH)->amount, 2);
    for (uint8_t m = 0; m < 3; ++m) {
        ASSERT_NE(monster_power(s, m, PowerId::STRENGTH), nullptr) << "m=" << m;
        EXPECT_EQ(monster_power(s, m, PowerId::STRENGTH)->amount, 1);
    }
}

// ============================================================================
// Rare -- the damage-pipeline relics
// ============================================================================

// VulnerablePower.atDamageReceive (VulnerablePower.java:61-73): x1.5 normally,
// x1.25 on a Vulnerable PLAYER with Odd Mushroom, x1.75 on a Vulnerable MONSTER
// with Paper Phrog. The Odd Mushroom test comes first and the two are mutually
// exclusive by the owner test.
TEST(RelicRaresShop, OddMushroomSoftensVulnerableOnThePlayerOnly) {
    // No relic: 16 * 1.5 = 24.
    {
        CombatState s = MakeState();
        add_power(s, PowerId::VULNERABLE, 2);
        op_damage(s, 0, kActorPlayer, 16);
        EXPECT_EQ(s.player_hp, 46);
    }
    // Odd Mushroom: 16 * 1.25 = 20.
    {
        CombatState s = MakeState();
        give(s, RelicId::ODD_MUSHROOM);
        add_power(s, PowerId::VULNERABLE, 2);
        op_damage(s, 0, kActorPlayer, 16);
        EXPECT_EQ(s.player_hp, 50);
    }
    // Odd Mushroom does NOT touch the monster-side multiplier: a Vulnerable
    // MONSTER still takes x1.5 (its owner is not the player).
    {
        CombatState s = MakeState();
        give(s, RelicId::ODD_MUSHROOM);
        s.monsters[0].powers[0].power_id =
            static_cast<uint16_t>(PowerId::VULNERABLE);
        s.monsters[0].powers[0].amount = 2;
        s.monsters[0].power_count = 1;
        op_damage(s, kActorPlayer, 0, 16);
        EXPECT_EQ(s.monsters[0].hp, 26) << "50 - 24";
    }
}

// Torii.onAttacked (Torii.java:31-38): a NORMAL hit whose post-block damage
// is 2..5 becomes 1. 1 stays 1, 6 is untouched, and HP_LOSS / THORNS are exempt.
TEST(RelicRaresShop, ToriiFloorsSmallNormalHitsToOne) {
    struct Case { int base; int expect_loss; };
    const Case cases[] = {{1, 1}, {2, 1}, {5, 1}, {6, 6}, {30, 30}};
    for (const Case& c : cases) {
        CombatState s = MakeState();
        give(s, RelicId::TORII);
        op_damage(s, 0, kActorPlayer, c.base);
        EXPECT_EQ(s.player_hp, 70 - c.expect_loss) << "base=" << c.base;
    }
    // THORNS and HP_LOSS are excluded by Torii's own type guard.
    CombatState thorns = MakeState();
    give(thorns, RelicId::TORII);
    op_damage(thorns, kActorPlayer, kActorPlayer, 4, 1, DamageType::THORNS);
    EXPECT_EQ(thorns.player_hp, 66);
    CombatState loss = MakeState();
    give(loss, RelicId::TORII);
    op_lose_hp(loss, kActorPlayer, 4);
    EXPECT_EQ(loss.player_hp, 66);
}

// Torii runs AFTER block (it reads the POST-decrementBlock amount), so block
// eats first and only the remainder is floored.
TEST(RelicRaresShop, ToriiReadsThePostBlockRemainder) {
    CombatState s = MakeState();
    give(s, RelicId::TORII);
    s.player_block = 8;
    op_damage(s, 0, kActorPlayer, 12);  // 12 - 8 = 4, in Torii's 2..5 window
    EXPECT_EQ(s.player_block, 0);
    EXPECT_EQ(s.player_hp, 69);
}

// TungstenRod.onLoseHpLast (TungstenRod.java:26-32): -1 on every positive
// player HP loss, of ANY damage type, and it is the LAST modifier -- so a
// 1-damage hit becomes 0 and no HP is lost at all.
TEST(RelicRaresShop, TungstenRodReducesEveryPositiveHpLossByOne) {
    CombatState s = MakeState();
    give(s, RelicId::TUNGSTEN_ROD);
    op_damage(s, 0, kActorPlayer, 10);
    EXPECT_EQ(s.player_hp, 61);
    op_lose_hp(s, kActorPlayer, 5);
    EXPECT_EQ(s.player_hp, 57) << "HP_LOSS routes through the same chain";
    op_damage(s, 0, kActorPlayer, 1);
    EXPECT_EQ(s.player_hp, 57) << "1 -> 0, nothing lost";
}

// Torii runs BEFORE Tungsten Rod (AbstractPlayer.java:1430-1435), so a 5-damage
// hit is floored to 1 and then reduced to 0 -- not 5 - 1 = 4.
TEST(RelicRaresShop, ToriiThenTungstenRodStackInJavaOrder) {
    CombatState s = MakeState();
    Relics r;
    r.add(RelicId::TORII);
    r.add(RelicId::TUNGSTEN_ROD);
    install(s, r);
    op_damage(s, 0, kActorPlayer, 5);
    EXPECT_EQ(s.player_hp, 70);
}

// --- The Boot ---------------------------------------------------------------

// Boot.onAttackToChangeDamage (Boot.java:30-38): a PLAYER-sourced hit whose
// value is 1..4 becomes 5. 5 and above are untouched, and 0 is not raised.
TEST(RelicRaresShop, BootRaisesSmallPlayerHitsToFive) {
    const struct { int base; int expect_loss; } cases[] = {
        {1, 5}, {2, 5}, {4, 5}, {5, 5}, {6, 6}, {30, 30},
    };
    for (const auto& c : cases) {
        CombatState s = MakeState();
        give(s, RelicId::BOOT);
        op_damage(s, kActorPlayer, 0, c.base);
        EXPECT_EQ(s.monsters[0].hp, 50 - c.expect_loss) << "base=" << c.base;
    }
}

// THE PLACEMENT TEST. onAttackToChangeDamage runs AFTER decrementBlock at BOTH
// call sites (AbstractMonster.java:639-643, AbstractPlayer.java:1399-1403), so
// Boot reads the UNBLOCKED residue. A 7-damage hit into 5 block leaves 2, which
// Boot raises to 5 -- 50 - 5 = 45. A pre-block site would have seen 7, which is
// outside the 1..4 window, raised nothing, and dealt the bare 2 for 48. The two
// answers differ, which is what makes the site observable rather than a matter
// of taste.
TEST(RelicRaresShop, BootReadsThePostBlockRemainder) {
    CombatState s = MakeState();
    give(s, RelicId::BOOT);
    s.monsters[0].block = 5;
    op_damage(s, kActorPlayer, 0, 7);
    EXPECT_EQ(s.monsters[0].block, 0);
    EXPECT_EQ(s.monsters[0].hp, 45) << "7 - 5 = 2, raised to 5";
}

// `damageAmount > 0` (Boot.java:32): a hit fully soaked by block is NOT raised.
// This is the case option (b) of the pre-block reading would have broken.
TEST(RelicRaresShop, BootDoesNotResurrectAFullyBlockedHit) {
    CombatState s = MakeState();
    give(s, RelicId::BOOT);
    s.monsters[0].block = 3;
    op_damage(s, kActorPlayer, 0, 3);
    EXPECT_EQ(s.monsters[0].block, 0);
    EXPECT_EQ(s.monsters[0].hp, 50) << "0 unblocked damage stays 0";
}

// The type guard (Boot.java:32) and the attacker gate (the enclosing
// `if (info.owner == AbstractDungeon.player)`).
TEST(RelicRaresShop, BootSkipsThornsHpLossAndMonsterSourcedHits) {
    CombatState thorns = MakeState();
    give(thorns, RelicId::BOOT);
    op_damage(thorns, kActorPlayer, 0, 3, 1, DamageType::THORNS);
    EXPECT_EQ(thorns.monsters[0].hp, 47);

    CombatState loss = MakeState();
    give(loss, RelicId::BOOT);
    op_lose_hp(loss, kActorPlayer, 3);
    EXPECT_EQ(loss.player_hp, 67);

    // A MONSTER-sourced hit on the player is not the player attacking, so the
    // relic never sees it.
    CombatState incoming = MakeState();
    give(incoming, RelicId::BOOT);
    op_damage(incoming, 0, kActorPlayer, 3);
    EXPECT_EQ(incoming.player_hp, 67);
}

// Boot composed with the victim's Buffer, and the case that discriminates the
// insertion point a SECOND way. Block 3 against base 3 leaves 0 unblocked, so
// Boot's `damageAmount > 0` fails and Buffer is handed a 0 -- which spends no
// stack (apply_buffer only queues the ReducePowerAction for a positive amount).
// Had Boot been placed BEFORE decrementBlock it would have raised the 3 to 5,
// block would have soaked 3, and Buffer would have been handed 2 and spent a
// stack. So the absence of the REDUCE_POWER is the observable.
//
// Stated honestly: Boot's order relative to Buffer ITSELF is NOT observable in
// S1 -- Buffer zeroes the number either way and its stack is spent for any
// positive input, so `Boot then Buffer` and `Buffer then Boot` agree on every
// reachable input. The Java order (AbstractMonster.java:643 before :646) is
// reproduced anyway because it is free; this test does not claim to prove it.
TEST(RelicRaresShop, BootAndBufferComposeAndAFullyBlockedHitSpendsNoStack) {
    CombatState s = MakeState();
    give(s, RelicId::BOOT);
    s.monsters[0].block = 3;
    s.monsters[0].powers[0].power_id = static_cast<uint16_t>(PowerId::BUFFER);
    s.monsters[0].powers[0].amount = 1;
    s.monsters[0].power_count = 1;
    op_damage(s, kActorPlayer, 0, 3);
    EXPECT_EQ(s.monsters[0].hp, 50);
    EXPECT_EQ(s.monsters[0].block, 0);
    EXPECT_EQ(s.action_count, 0) << "Buffer was handed a 0, so no stack is spent";

    // With no block the same hit IS raised, and the Buffer then eats the 5.
    CombatState open = MakeState();
    give(open, RelicId::BOOT);
    open.monsters[0].powers[0].power_id = static_cast<uint16_t>(PowerId::BUFFER);
    open.monsters[0].powers[0].amount = 1;
    open.monsters[0].power_count = 1;
    op_damage(open, kActorPlayer, 0, 3);
    EXPECT_EQ(open.monsters[0].hp, 50);
    ASSERT_EQ(open.action_count, 1);
    EXPECT_EQ(queued(open, 0).opcode, kOp(Opcode::REDUCE_POWER));
}

// LizardTail (LizardTail.java:28-45 via AbstractPlayer.java:1487-1493): a
// lethal hit leaves the player at maxHealth/2 instead of dead, once.
TEST(RelicRaresShop, LizardTailRevivesOnceAtHalfMaxHp) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::LIZARD_TAIL, /*counter=*/-1);
    op_damage(s, 0, kActorPlayer, 999);
    EXPECT_EQ(s.player_hp, 40) << "maxHealth/2 with max 80";
    EXPECT_EQ(rv.relics[0].counter, -2) << "used up";

    op_damage(s, 0, kActorPlayer, 999);
    EXPECT_EQ(s.player_hp, 0) << "a used-up Lizard Tail does not fire again";
}

// ============================================================================
// Fairy in a Bottle -- the OTHER revive source at the same site
// ============================================================================
//
// AbstractPlayer.damage (:1482-1497):
//     if (!hasRelic("Mark of the Bloom")) {
//         if (hasPotion("FairyPotion")) { ...consume ONE...; return; }
//         else if (hasRelic("Lizard Tail") && counter == -1) { ...; return; }
//     }
// The combat layer has no belt, so the ARMED COUNT is mirrored into
// CombatState.flags at combat entry (kCombatFlagFairyArmedShift).
//
// FairyPotion.use (FairyPotion.java:36-45) heals
// (int)((float)maxHealth * potency/100f), floored UP to 1, onto an HP already
// pinned at 0 -- so the result IS the heal, never hp + heal. potency is 30.

TEST(RelicRaresShop, FairyInABottleRevivesAtThirtyPercentOfMaxHp) {
    CombatState s = MakeState();
    ASSERT_EQ(s.player_max_hp, 80);
    s.flags = with_combat_fairy_armed(s.flags, 1);

    op_damage(s, 0, kActorPlayer, 999);

    EXPECT_EQ(s.player_hp, 24) << "(int)(80 * 0.30f), not hp + 24";
    EXPECT_EQ(combat_fairy_armed(s.flags), 0) << "exactly one consumed";
}

// The Java loop `return`s on the first match, so ONE fairy is spent per lethal
// event and a second survives for a later one.
TEST(RelicRaresShop, FairyInABottleSpendsExactlyOnePerLethalEvent) {
    CombatState s = MakeState();
    s.flags = with_combat_fairy_armed(s.flags, 2);

    op_damage(s, 0, kActorPlayer, 999);
    EXPECT_EQ(s.player_hp, 24);
    EXPECT_EQ(combat_fairy_armed(s.flags), 1) << "one left";

    op_damage(s, 0, kActorPlayer, 999);
    EXPECT_EQ(s.player_hp, 24) << "the second fairy fires too";
    EXPECT_EQ(combat_fairy_armed(s.flags), 0);

    op_damage(s, 0, kActorPlayer, 999);
    EXPECT_EQ(s.player_hp, 0) << "and then the player dies";
}

// The Lizard Tail arm is an `else if` on `hasPotion`, so while a Fairy is held
// the relic is not even CONSULTED -- its counter must be untouched.
TEST(RelicRaresShop, FairyInABottleBeatsLizardTailAndLeavesItArmed) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::LIZARD_TAIL, /*counter=*/-1);
    s.flags = with_combat_fairy_armed(s.flags, 1);

    op_damage(s, 0, kActorPlayer, 999);

    EXPECT_EQ(s.player_hp, 24) << "the FAIRY's 30%, not the tail's 50%";
    EXPECT_EQ(rv.relics[0].counter, -1) << "the Lizard Tail is still armed";

    // With the fairy gone, the tail takes the next lethal hit.
    op_damage(s, 0, kActorPlayer, 999);
    EXPECT_EQ(s.player_hp, 40) << "maxHealth/2";
    EXPECT_EQ(rv.relics[0].counter, -2);
}

// The revive is a player heal on a COMBAT site, so Magic Flower's phase-gated
// x1.5 applies exactly as it does to the Lizard Tail's: round(24 * 1.5) = 36.
TEST(RelicRaresShop, MagicFlowerMultipliesTheFairyRevive) {
    CombatState s = MakeState();
    give(s, RelicId::MAGIC_FLOWER);
    s.flags = with_combat_fairy_armed(s.flags, 1);
    op_damage(s, 0, kActorPlayer, 999);
    EXPECT_EQ(s.player_hp, 36);
}

// It fires from the HP-LOSS path too, not only from an attack: the Java hook is
// on AbstractPlayer.damage, which LoseHPAction also routes through
// (LoseHPAction.java:41).
TEST(RelicRaresShop, FairyInABottleAlsoFiresOnASelfInflictedHpLoss) {
    CombatState s = MakeState();
    s.flags = with_combat_fairy_armed(s.flags, 1);
    op_lose_hp(s, kActorPlayer, 999);
    EXPECT_EQ(s.player_hp, 24);
    EXPECT_EQ(combat_fairy_armed(s.flags), 0);
}

// The revive is a player HEAL, so Magic Flower multiplies it
// (MagicFlower.onPlayerHeal, MagicFlower.java:30-37): 40 -> round(60.0) = 60.
TEST(RelicRaresShop, MagicFlowerMultipliesTheLizardTailRevive) {
    CombatState s = MakeState();
    Relics r;
    r.add(RelicId::LIZARD_TAIL, -1);
    r.add(RelicId::MAGIC_FLOWER);
    install(s, r);
    op_damage(s, 0, kActorPlayer, 999);
    EXPECT_EQ(s.player_hp, 60);
}

// MagicFlower.onPlayerHeal is MathUtils.round(amount * 1.5f), NOT a truncation:
// Burning Blood's 6 becomes 9 and Blood Vial's 2 becomes 3, while an odd product
// like 5 * 1.5 = 7.5 rounds UP to 8.
TEST(RelicRaresShop, MagicFlowerRoundsHalfUpAndAppliesToEveryCombatHeal) {
    CombatState s = MakeState();
    s.player_hp = 10;
    Relics r;
    r.add(RelicId::BURNING_BLOOD);
    r.add(RelicId::MAGIC_FLOWER);
    const RelicView rv = install(s, r);
    dispatch_relics_on_victory(s, rv.relics, rv.count);
    EXPECT_EQ(s.player_hp, 19) << "6 * 1.5 = 9";

    CombatState v = MakeState();
    v.player_hp = 10;
    Relics rv2;
    rv2.add(RelicId::BLOOD_VIAL);
    rv2.add(RelicId::MAGIC_FLOWER);
    const RelicView view2 = install(v, rv2);
    dispatch_relics_at_battle_start(v, view2.relics, view2.count);
    EXPECT_EQ(v.player_hp, 13) << "2 * 1.5 = 3";

    // Half-up: 5 * 1.5 == 7.5 -> 8 (MathUtils.round, MathUtils.java:233-235).
    CombatState half = MakeState();
    half.player_hp = 10;
    give(half, RelicId::MAGIC_FLOWER);
    heal_player_with_relics(half, 5);
    EXPECT_EQ(half.player_hp, 18);
}

// The heal still clamps at max HP -- Magic Flower changes the amount, not the
// HealAction clamp.
TEST(RelicRaresShop, MagicFlowerHealStillClampsToMaxHp) {
    CombatState s = MakeState();
    s.player_hp = 78;
    give(s, RelicId::MAGIC_FLOWER);
    heal_player_with_relics(s, 6);
    EXPECT_EQ(s.player_hp, 80);
}

// ============================================================================
// Rare -- ApplyPowerAction interception
// ============================================================================

// Ginger (ApplyPowerAction.java:119-124) and Turnip (:125-130) reject Weak and
// Frail on the PLAYER, and only on the player -- a monster still gets them.
TEST(RelicRaresShop, GingerAndTurnipRejectPlayerWeakAndFrail) {
    CombatState s = MakeState();
    Relics r;
    r.add(RelicId::GINGER);
    r.add(RelicId::TURNIP);
    install(s, r);
    op_apply_power(s, 0, kActorPlayer, PowerId::WEAK, 2);
    op_apply_power(s, 0, kActorPlayer, PowerId::FRAIL, 2);
    EXPECT_EQ(player_power(s, PowerId::WEAK), nullptr);
    EXPECT_EQ(player_power(s, PowerId::FRAIL), nullptr);
    // Vulnerable is neither, so it still lands.
    op_apply_power(s, 0, kActorPlayer, PowerId::VULNERABLE, 2);
    ASSERT_NE(player_power(s, PowerId::VULNERABLE), nullptr);
    // And a monster is unaffected by either relic.
    op_apply_power(s, kActorPlayer, 0, PowerId::WEAK, 1);
    EXPECT_NE(monster_power(s, 0, PowerId::WEAK), nullptr);
}

// The rejection happens BEFORE the Artifact nullify (ApplyPowerAction.java:
// 119-131), so a Ginger-rejected Weak does NOT spend an Artifact stack.
TEST(RelicRaresShop, GingerRejectionDoesNotConsumeArtifact) {
    CombatState s = MakeState();
    give(s, RelicId::GINGER);
    add_power(s, PowerId::ARTIFACT, 1);
    op_apply_power(s, 0, kActorPlayer, PowerId::WEAK, 2);
    ASSERT_NE(player_power(s, PowerId::ARTIFACT), nullptr);
    EXPECT_EQ(player_power(s, PowerId::ARTIFACT)->amount, 1) << "not spent";
}

// ChampionsBelt (ApplyPowerAction.java:111-113 -> ChampionsBelt.java:32-35):
// a player-sourced Vulnerable on a monster also queues Weak 1 on it.
TEST(RelicRaresShop, ChampionBeltAddsWeakToPlayerAppliedVulnerable) {
    CombatState s = MakeState();
    give(s, RelicId::CHAMPION_BELT);
    op_apply_power(s, kActorPlayer, 0, PowerId::VULNERABLE, 2);
    drain(s);
    ASSERT_NE(monster_power(s, 0, PowerId::VULNERABLE), nullptr);
    EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE)->amount, 2);
    ASSERT_NE(monster_power(s, 0, PowerId::WEAK), nullptr);
    EXPECT_EQ(monster_power(s, 0, PowerId::WEAK)->amount, 1);
}

// The gate is narrow: source must be the player, target must not be the source,
// the power must be Vulnerable, and the target must not already hold Artifact.
TEST(RelicRaresShop, ChampionBeltGateRejectsArtifactAndNonPlayerSources) {
    // Artifact on the target: the Vulnerable is nullified and no Weak is queued.
    {
        CombatState s = MakeState();
        give(s, RelicId::CHAMPION_BELT);
        s.monsters[0].powers[0].power_id =
            static_cast<uint16_t>(PowerId::ARTIFACT);
        s.monsters[0].powers[0].amount = 1;
        s.monsters[0].power_count = 1;
        op_apply_power(s, kActorPlayer, 0, PowerId::VULNERABLE, 2);
        EXPECT_EQ(s.action_count, 0) << "no Weak queued";
        drain(s);
        EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE), nullptr);
        EXPECT_EQ(monster_power(s, 0, PowerId::WEAK), nullptr);
    }
    // A monster-sourced Vulnerable onto the player is not the player's doing.
    {
        CombatState s = MakeState();
        give(s, RelicId::CHAMPION_BELT);
        op_apply_power(s, 0, kActorPlayer, PowerId::VULNERABLE, 2);
        EXPECT_EQ(s.action_count, 0);
        drain(s);
        EXPECT_EQ(player_power(s, PowerId::WEAK), nullptr);
    }
    // A non-Vulnerable debuff does not trigger it either.
    {
        CombatState s = MakeState();
        give(s, RelicId::CHAMPION_BELT);
        op_apply_power(s, kActorPlayer, 0, PowerId::WEAK, 1);
        EXPECT_EQ(s.action_count, 0);
    }
}

// ============================================================================
// Rare -- the queue-mechanics relics
// ============================================================================

// CharonsAshes.onExhaust (CharonsAshes.java:36-43): 3 THORNS to every enemy,
// queued at the TOP -- ahead of anything already waiting.
TEST(RelicRaresShop, CharonsAshesQueuesThreeThornsAtTheTopOnExhaust) {
    CombatState s = MakeState(2, /*monster_hp=*/40);
    const RelicView rv = give(s, RelicId::CHARONS_ASHES);
    ActionQueueItem marker{};
    marker.opcode = kOp(Opcode::NOP);
    add_to_bottom(s, marker);
    dispatch_relics_on_exhaust(s, rv.relics, rv.count,
                               static_cast<uint16_t>(CardId::STRIKE));
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::DAMAGE))
        << "addToTop puts it ahead of the pre-existing action";
    EXPECT_EQ(queued(s, 0).amount, 3);
    drain(s);
    EXPECT_EQ(s.monsters[0].hp, 37);
    EXPECT_EQ(s.monsters[1].hp, 37);
}

// Abacus.onShuffle (Abacus.java:18-22): 6 block on every reshuffle, and the
// direct GainBlockAction takes no Dexterity.
TEST(RelicRaresShop, AbacusGivesSixBlockOnShuffleWithoutDexterity) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::THE_ABACUS);
    add_power(s, PowerId::DEXTERITY, 3);
    dispatch_relics_on_shuffle(s, rv.relics, rv.count);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_NE(queued(s, 0).flags & kBlockNoPowers, 0u);
    drain(s);
    EXPECT_EQ(s.player_block, 6) << "6, not 9";
}

// BirdFacedUrn.onUseCard (BirdFacedUrn.java:33-40): 2 HP when a POWER is played,
// and nothing for an ATTACK or a SKILL.
TEST(RelicRaresShop, BirdFacedUrnHealsTwoOnlyForPowerCards) {
    CombatState s = MakeState();
    s.player_hp = 40;
    const RelicView rv = give(s, RelicId::BIRD_FACED_URN);
    dispatch_relics_on_use_card(s, rv.relics, rv.count,
                                static_cast<uint16_t>(CardId::INFLAME), 0);
    EXPECT_EQ(s.player_hp, 42);
    dispatch_relics_on_use_card(s, rv.relics, rv.count,
                                static_cast<uint16_t>(CardId::STRIKE), 0);
    EXPECT_EQ(s.player_hp, 42) << "ATTACK does not heal";
    dispatch_relics_on_use_card(s, rv.relics, rv.count,
                                static_cast<uint16_t>(CardId::DEFEND), 0);
    EXPECT_EQ(s.player_hp, 42) << "SKILL does not heal";
}

// Pocketwatch (Pocketwatch.java:32-60): no draw on the opening turn, then 3
// cards at the start of any turn following one where 3 or fewer were played.
TEST(RelicRaresShop, PocketwatchDrawsThreeAfterAQuietTurnButNotOnTurnOne) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::POCKETWATCH);
    dispatch_relics_at_battle_start(s, rv.relics, rv.count);

    // Turn 1's post-draw phase: firstTurn is armed, so nothing is drawn.
    dispatch_relics_at_turn_start_post_draw(s, rv.relics, rv.count);
    EXPECT_EQ(s.action_count, 0) << "the opening turn never draws";

    // Turn 1: play three cards, then turn 2 draws.
    for (int i = 0; i < 3; ++i) {
        dispatch_relics_on_play_card(s, rv.relics, rv.count,
                                     static_cast<uint16_t>(CardId::STRIKE));
    }
    dispatch_relics_at_turn_start_post_draw(s, rv.relics, rv.count);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::DRAW));
    EXPECT_EQ(queued(s, 0).amount, 3);
    drain(s);

    // Turn 2: play four cards -> turn 3 draws nothing.
    for (int i = 0; i < 4; ++i) {
        dispatch_relics_on_play_card(s, rv.relics, rv.count,
                                     static_cast<uint16_t>(CardId::STRIKE));
    }
    dispatch_relics_at_turn_start_post_draw(s, rv.relics, rv.count);
    EXPECT_EQ(s.action_count, 0) << "four cards played is over the threshold";

    // Turn 3: play nothing -> turn 4 draws again.
    dispatch_relics_at_turn_start_post_draw(s, rv.relics, rv.count);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).amount, 3);
}

// The post-draw relic phase is wired into the real start-of-turn sequence
// (GameActionManager.java:361-362) BEHIND the hand draw, so Pocketwatch's 3
// cards resolve after the ordinary 5.
TEST(RelicRaresShop, PocketwatchIsWiredBehindTheStartOfTurnDraw) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::POCKETWATCH);
    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    rv.relics[0].counter = 0;  // a completed quiet turn, firstTurn cleared
    s.turn_has_ended = 1;
    s.monster_attacks_queued = 1;
    (void)pump_step(s, default_monster_turn);  // step 6 -> start_of_turn
    ASSERT_GE(s.action_count, 2);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::DRAW));
    EXPECT_EQ(queued(s, 0).amount, kStartOfTurnDrawCount);
    EXPECT_EQ(queued(s, 1).opcode, kOp(Opcode::DRAW));
    EXPECT_EQ(queued(s, 1).amount, 3) << "Pocketwatch resolves after the draw";
}

// ============================================================================
// Rare -- Calipers and Ice Cream (the two stage-a simplifications they retire)
// ============================================================================

// GameActionManager.java:353-359: without Calipers the player loses ALL block at
// the start of a turn; with it, exactly 15, clamped at zero.
TEST(RelicRaresShop, CalipersLosesFifteenBlockInsteadOfAll) {
    struct Case { int16_t start; int16_t plain; int16_t calipers; };
    const Case cases[] = {{40, 0, 25}, {15, 0, 0}, {9, 0, 0}, {0, 0, 0}};
    for (const Case& c : cases) {
        CombatState plain = MakeState();
        plain.player_block = c.start;
        plain.turn_has_ended = 1;
        plain.monster_attacks_queued = 1;
        (void)pump_step(plain, default_monster_turn);
        EXPECT_EQ(plain.player_block, c.plain) << "start=" << c.start;

        CombatState cal = MakeState();
        give(cal, RelicId::CALIPERS);
        cal.player_block = c.start;
        cal.turn_has_ended = 1;
        cal.monster_attacks_queued = 1;
        (void)pump_step(cal, default_monster_turn);
        EXPECT_EQ(cal.player_block, c.calipers) << "start=" << c.start;
    }
}

// EnergyManager.recharge (EnergyManager.java:25-40): setEnergy(base) by default
// -- unspent energy is LOST -- but addEnergy(base) with Ice Cream, so it carries.
TEST(RelicRaresShop, IceCreamCarriesUnspentEnergyInsteadOfSetting) {
    CombatState plain = MakeState();
    plain.player_energy = 2;
    plain.turn_has_ended = 1;
    plain.monster_attacks_queued = 1;
    (void)pump_step(plain, default_monster_turn);
    EXPECT_EQ(plain.player_energy, kIroncladBaseEnergy) << "SET, not carried";

    CombatState ice = MakeState();
    give(ice, RelicId::ICE_CREAM);
    ice.player_energy = 2;
    ice.turn_has_ended = 1;
    ice.monster_attacks_queued = 1;
    (void)pump_step(ice, default_monster_turn);
    EXPECT_EQ(ice.player_energy, 2 + kIroncladBaseEnergy);

    // Three more quiet turns accumulate rather than plateau. The action queue is
    // cleared between them because start_of_turn leaves its DrawCardAction
    // behind, and pump_step would drain that before reaching step 6 again.
    for (int turn = 0; turn < 3; ++turn) {
        ice.action_count = 0;
        ice.action_head = 0;
        ice.action_tail = 0;
        ice.turn_has_ended = 1;
        ice.monster_attacks_queued = 1;
        (void)pump_step(ice, default_monster_turn);
    }
    EXPECT_EQ(ice.player_energy, 2 + 4 * kIroncladBaseEnergy);
}

// EnergyPanel.addEnergy caps the total at 999 (EnergyPanel.java:59-68).
TEST(RelicRaresShop, IceCreamEnergyCarryCapsAt999) {
    CombatState s = MakeState();
    give(s, RelicId::ICE_CREAM);
    s.player_energy = 998;
    s.turn_has_ended = 1;
    s.monster_attacks_queued = 1;
    (void)pump_step(s, default_monster_turn);
    EXPECT_EQ(s.player_energy, 999);
}

// ============================================================================
// Shop -- card-play relics
// ============================================================================

// AbstractCard.canUse (AbstractCard.java:916-918): Medical Kit makes STATUS
// cards playable, and only STATUS cards -- a curse still needs Blue Candle.
TEST(RelicRaresShop, MedicalKitMakesStatusCardsPlayable) {
    CombatState s = MakeState();
    put_in_hand(s, CardId::WOUND);
    put_in_hand(s, CardId::REGRET);
    ActionMask mask{};

    legal_actions(s, mask);
    EXPECT_FALSE(mask.can_play[0]) << "an unplayable status without the relic";
    EXPECT_FALSE(mask.can_play[1]);

    give(s, RelicId::MEDICAL_KIT);
    legal_actions(s, mask);
    EXPECT_TRUE(mask.can_play[0]) << "Medical Kit unlocks the status";
    EXPECT_FALSE(mask.can_play[1]) << "but not the curse -- that is Blue Candle";
}

// MedicalKit.onUseCard (MedicalKit.java:35-41): the played status exhausts
// rather than going to the discard pile, and runs no effect program.
TEST(RelicRaresShop, MedicalKitExhaustsThePlayedStatus) {
    CombatState s = MakeState();
    give(s, RelicId::MEDICAL_KIT);
    const CardPoolIndex pi = put_in_hand(s, CardId::WOUND);
    CardQueueItem item{};
    item.card_index = pi;
    resolve_card_play(s, item);
    EXPECT_EQ(s.hand_count, 0);
    EXPECT_EQ(s.action_count, 1)
        << "a status runs no ON_PLAY program -- the one queued item is the "
           "USE_CARD filing action (the played card is in limbo until then)";
    ASSERT_EQ(s.limbo_count, 1);
    EXPECT_EQ(s.limbo[0], pi);
    drain(s);  // resolve the queued USE_CARD (UseCardAction.update)
    EXPECT_EQ(s.discard_count, 0);
    ASSERT_EQ(s.exhaust_count, 1);
    EXPECT_EQ(s.exhaust[0], pi);
    EXPECT_EQ(s.limbo_count, 0);
}

// StrangeSpoon (UseCardAction.java:109-131): an exhausting non-POWER play rolls
// ONE cardRandomRng boolean; on true the card goes to the discard pile instead.
// Both outcomes are reachable, and the roll consumes exactly one draw.
TEST(RelicRaresShop, StrangeSpoonRollsOncePerExhaustingPlay) {
    int discarded = 0;
    int exhausted = 0;
    for (int64_t seed = 1; seed <= 12; ++seed) {
        CombatState s = MakeState();
        give(s, RelicId::STRANGE_SPOON);
        s.card_random_rng = from_seed(seed);
        const RngStream before = s.card_random_rng;
        const CardPoolIndex pi = put_in_hand(s, CardId::TRUE_GRIT);
        s.card_pool[pi].flags |= card_flag_bit(CardFlag::EXHAUST);
        CardQueueItem item{};
        item.card_index = pi;
        resolve_card_play(s, item);
        drain(s);  // the Strange Spoon roll lives in queued UseCardAction.update
        EXPECT_EQ(s.card_random_rng.counter, before.counter + 1)
            << "exactly one boolean, seed=" << seed;
        if (s.discard_count == 1) {
            ++discarded;
        } else {
            EXPECT_EQ(s.exhaust_count, 1);
            ++exhausted;
        }
    }
    EXPECT_GT(discarded, 0) << "the spoon-proc branch is reachable";
    EXPECT_GT(exhausted, 0) << "the ordinary exhaust branch is reachable";
}

// The roll is guarded by exhaustCard FIRST (UseCardAction.java:111), so a play
// that would NOT exhaust consumes no RNG at all -- the guard order is
// RNG-visible.
TEST(RelicRaresShop, StrangeSpoonConsumesNoRngOnANonExhaustingPlay) {
    CombatState s = MakeState();
    give(s, RelicId::STRANGE_SPOON);
    s.card_random_rng = from_seed(5);
    const int32_t before = s.card_random_rng.counter;
    const CardPoolIndex pi = put_in_hand(s, CardId::DEFEND);
    CardQueueItem item{};
    item.card_index = pi;
    resolve_card_play(s, item);
    drain(s);  // queued UseCardAction files the non-exhausting card
    EXPECT_EQ(s.card_random_rng.counter, before) << "no draw for a plain play";
    EXPECT_EQ(s.discard_count, 1);
}

// ChemicalX (ChemicalX.java:12-30, BOOST = 2): an X-cost card acts as though it
// had 2 more energy, while the energy actually spent is unchanged (all of it).
TEST(RelicRaresShop, ChemicalXAddsTwoRepetitionsWithoutSpendingEnergy) {
    CombatState plain = MakeState(1, /*monster_hp=*/200);
    plain.player_energy = 2;
    const CardPoolIndex p0 = put_in_hand(plain, CardId::WHIRLWIND);
    ASSERT_TRUE(has_card_flag(plain.card_pool[p0].flags, CardFlag::XCOST))
        << "the test needs an X-cost row";
    CardQueueItem it0{};
    it0.card_index = p0;
    resolve_card_play(plain, it0);
    const uint8_t plain_actions = plain.action_count;
    ASSERT_GE(plain_actions, 1);
    EXPECT_EQ(static_cast<Opcode>(
                  plain.action_queue[(plain.action_head + plain_actions - 1) %
                                     kActionQueueCap]
                      .opcode),
              Opcode::USE_CARD);
    EXPECT_EQ(plain.player_energy, 0);

    CombatState boosted = MakeState(1, /*monster_hp=*/200);
    give(boosted, RelicId::CHEMICAL_X);
    boosted.player_energy = 2;
    const CardPoolIndex p1 = put_in_hand(boosted, CardId::WHIRLWIND);
    CardQueueItem it1{};
    it1.card_index = p1;
    resolve_card_play(boosted, it1);
    // Both queues have one non-program item: the trailing UseCardAction.
    EXPECT_EQ(boosted.action_count - 1, (plain_actions - 1) * 2)
        << "2 energy -> 4 repetitions, plus one filing action per play";
    EXPECT_EQ(boosted.player_energy, 0) << "still spends only the real energy";
}

// At zero energy the boost still applies: energyOnUse clamps to 0 first, then
// the +2 is added (WhirlwindAction.update's ordering).
TEST(RelicRaresShop, ChemicalXStillGivesTwoRepetitionsAtZeroEnergy) {
    CombatState s = MakeState(1, /*monster_hp=*/200);
    give(s, RelicId::CHEMICAL_X);
    s.player_energy = 0;
    const CardPoolIndex pi = put_in_hand(s, CardId::WHIRLWIND);
    CardQueueItem item{};
    item.card_index = pi;
    resolve_card_play(s, item);
    EXPECT_GT(s.action_count, 0);
    drain(s);
    EXPECT_LT(s.monsters[0].hp, 200);
}

// HandDrill.onBlockBroken (HandDrill.java:31-35 via AbstractCreature.java:
// 159-183): breaking a MONSTER's block applies Vulnerable 2 to it.
TEST(RelicRaresShop, HandDrillAppliesVulnerableWhenAMonsterBlockBreaks) {
    CombatState s = MakeState(1, /*monster_hp=*/60);
    give(s, RelicId::HAND_DRILL);
    s.monsters[0].block = 5;
    op_damage(s, kActorPlayer, 0, 9);
    drain(s);
    EXPECT_EQ(s.monsters[0].block, 0);
    ASSERT_NE(monster_power(s, 0, PowerId::VULNERABLE), nullptr);
    EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE)->amount, 2);
}

// The gate is exact: block that survives does not fire it, a monster that had no
// block does not fire it, and the PLAYER's own broken block never does (the
// `this instanceof AbstractMonster` guard, AbstractCreature.java:160).
TEST(RelicRaresShop, HandDrillGateNeedsAMonsterWhoseBlockActuallyBreaks) {
    // Block survives.
    {
        CombatState s = MakeState(1, /*monster_hp=*/60);
        give(s, RelicId::HAND_DRILL);
        s.monsters[0].block = 20;
        op_damage(s, kActorPlayer, 0, 9);
        drain(s);
        EXPECT_EQ(s.monsters[0].block, 11);
        EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE), nullptr);
    }
    // No block to break.
    {
        CombatState s = MakeState(1, /*monster_hp=*/60);
        give(s, RelicId::HAND_DRILL);
        op_damage(s, kActorPlayer, 0, 9);
        drain(s);
        EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE), nullptr);
    }
    // The player's own block breaking is not a monster's.
    {
        CombatState s = MakeState();
        give(s, RelicId::HAND_DRILL);
        s.player_block = 4;
        op_damage(s, 0, kActorPlayer, 9);
        drain(s);
        EXPECT_EQ(s.player_block, 0);
        EXPECT_EQ(player_power(s, PowerId::VULNERABLE), nullptr);
    }
}

// Hand Drill's Vulnerable is PLAYER-sourced (HandDrill.java:34 passes the player
// as the source), so Champion Belt sees it and adds its Weak.
TEST(RelicRaresShop, HandDrillVulnerableIsPlayerSourcedSoChampionBeltSeesIt) {
    CombatState s = MakeState(1, /*monster_hp=*/60);
    Relics r;
    r.add(RelicId::HAND_DRILL);
    r.add(RelicId::CHAMPION_BELT);
    install(s, r);
    s.monsters[0].block = 5;
    op_damage(s, kActorPlayer, 0, 9);
    drain(s);
    ASSERT_NE(monster_power(s, 0, PowerId::VULNERABLE), nullptr);
    ASSERT_NE(monster_power(s, 0, PowerId::WEAK), nullptr);
    EXPECT_EQ(monster_power(s, 0, PowerId::WEAK)->amount, 1);
}

// ============================================================================
// Pickup surfaces -- canSpawn gates and onEquip effects
// ============================================================================

// Mango.onEquip (Mango.java:29-31): +14 max HP AND +14 current.
// Waffle.onEquip (Waffle.java:28-31): +7 max HP, then heal to FULL.
// OldCoin.onEquip (OldCoin.java:31-34): +300 gold.
TEST(RelicRaresShop, EquipEffectsMoveHpAndGold) {
    RunState rs{};
    rs.hp = 30;
    rs.max_hp = 80;
    rs.gold = 99;
    RngStream misc = from_seed(1);
    ASSERT_EQ(acquire_relic(rs, misc, RelicId::MANGO),
              RelicAcquireResult::ACQUIRED);
    EXPECT_EQ(rs.max_hp, 94);
    EXPECT_EQ(rs.hp, 44);

    ASSERT_EQ(acquire_relic(rs, misc, RelicId::LEES_WAFFLE),
              RelicAcquireResult::ACQUIRED);
    EXPECT_EQ(rs.max_hp, 101);
    EXPECT_EQ(rs.hp, 101) << "heal to full";

    ASSERT_EQ(acquire_relic(rs, misc, RelicId::OLD_COIN),
              RelicAcquireResult::ACQUIRED);
    EXPECT_EQ(rs.gold, 399);
    EXPECT_EQ(misc.counter, 0) << "none of the three touches miscRng";
}

// Wing Boots is seeded with its three path-jump charges at pickup
// (WingBoots.java:18-21).
TEST(RelicRaresShop, WingBootsArrivesWithThreeCharges) {
    RunState rs{};
    RngStream misc = from_seed(1);
    ASSERT_EQ(acquire_relic(rs, misc, RelicId::WING_BOOTS),
              RelicAcquireResult::ACQUIRED);
    EXPECT_EQ(rs.relics[0].counter, 3);
}

// The floor gates: Prayer Wheel / Old Coin at <= 48, Wing Boots at <= 40, and
// Old Coin additionally never in a shop -- which Endless does NOT bypass.
TEST(RelicRaresShop, FloorAndShopSpawnGates) {
    RelicSpawnContext ctx{};
    ctx.floor = 48;
    EXPECT_TRUE(relic_can_spawn(RelicId::PRAYER_WHEEL, ctx));
    EXPECT_TRUE(relic_can_spawn(RelicId::OLD_COIN, ctx));
    EXPECT_FALSE(relic_can_spawn(RelicId::WING_BOOTS, ctx));
    ctx.floor = 49;
    EXPECT_FALSE(relic_can_spawn(RelicId::PRAYER_WHEEL, ctx));
    EXPECT_FALSE(relic_can_spawn(RelicId::OLD_COIN, ctx));
    ctx.endless = true;
    EXPECT_TRUE(relic_can_spawn(RelicId::PRAYER_WHEEL, ctx));
    EXPECT_TRUE(relic_can_spawn(RelicId::OLD_COIN, ctx));
    EXPECT_TRUE(relic_can_spawn(RelicId::WING_BOOTS, ctx));
    ctx.in_shop = true;
    EXPECT_FALSE(relic_can_spawn(RelicId::OLD_COIN, ctx))
        << "endless bypasses the floor clause, never the shop clause";
    EXPECT_TRUE(relic_can_spawn(RelicId::PRAYER_WHEEL, ctx));
}

// The campfire trio (Girya / Peace Pipe / Shovel, byte-identical canSpawn
// bodies): a >= 48 EARLY RETURN -- so floor 48 is EXCLUDED here, unlike the
// <= 48 gates above -- and a shared "fewer than two owned" cap.
TEST(RelicRaresShop, CampfireTrioGateIsShared) {
    const RelicId trio[] = {RelicId::GIRYA, RelicId::PEACE_PIPE,
                            RelicId::SHOVEL};
    RelicSpawnContext ctx{};
    ctx.floor = 47;
    for (RelicId id : trio) {
        EXPECT_TRUE(relic_can_spawn(id, ctx));
    }
    ctx.floor = 48;
    for (RelicId id : trio) {
        EXPECT_FALSE(relic_can_spawn(id, ctx)) << "floor >= 48 is the early out";
    }
    ctx.endless = true;
    for (RelicId id : trio) {
        EXPECT_TRUE(relic_can_spawn(id, ctx));
    }

    // The owned-count cap is shared across all three, and counted from RunState.
    RunState rs{};
    RngStream misc = from_seed(1);
    RelicSpawnContext c2{};
    c2.floor = 10;
    fill_campfire_relic_count(rs, c2);
    EXPECT_EQ(c2.campfire_relic_count, 0);
    for (RelicId id : trio) {
        EXPECT_TRUE(relic_can_spawn(id, c2));
    }

    (void)acquire_relic(rs, misc, RelicId::GIRYA);
    fill_campfire_relic_count(rs, c2);
    EXPECT_EQ(c2.campfire_relic_count, 1);
    for (RelicId id : trio) {
        EXPECT_TRUE(relic_can_spawn(id, c2)) << "one owned is still under the cap";
    }

    (void)acquire_relic(rs, misc, RelicId::SHOVEL);
    fill_campfire_relic_count(rs, c2);
    EXPECT_EQ(c2.campfire_relic_count, 2);
    for (RelicId id : trio) {
        EXPECT_FALSE(relic_can_spawn(id, c2)) << "two owned closes all three";
    }
    // An unrelated relic does not count toward the cap.
    RunState other{};
    (void)acquire_relic(other, misc, RelicId::MANGO);
    RelicSpawnContext c3{};
    fill_campfire_relic_count(other, c3);
    EXPECT_EQ(c3.campfire_relic_count, 0);
}

// No SHOP relic defines canSpawn -- none of the seventeen files has one, so each
// takes AbstractRelic's `return true`. A spurious gate here would change the
// relicRng draw order, so the absence is asserted rather than assumed.
TEST(RelicRaresShop, NoShopRelicGatesItsOwnSpawn) {
    RelicSpawnContext late{};
    late.floor = 55;
    late.in_shop = true;
    for (const sts::registry::RelicDef* d : sts::registry::kRelicDefs) {
        if (d->tier != sts::registry::RelicTier::SHOP) {
            continue;
        }
        EXPECT_TRUE(relic_can_spawn(static_cast<RelicId>(d->id), late))
            << "shop relic id " << static_cast<int>(d->id);
    }
}

// ============================================================================
// Deliberately inert rows -- asserted inert, so implementing one fails here
// first rather than changing behaviour nobody was watching.
// ============================================================================

// THIS LIST IS NOW EMPTY. Dead Branch, Sling of Courage and Orange Pellets left
// it in the relic-tail stage; Gambling Chip left it here, when the optional
// multi-select screen its deferral named as the blocker turned out to have
// already landed. The scaffold is kept rather than deleted so the next deferred
// native body has a home to go into.
TEST(RelicRaresShop, DeferredNativeBodiesQueueNothingAndTouchNoRng) {
    struct Case { RelicId id; RelicHook hook; };
    const std::vector<Case> cases{};
    for (const Case& c : cases) {
        CombatState s = MakeState();
        const RelicView rv = give(s, c.id);
        s.card_random_rng = from_seed(3);
        const RngStream before = s.card_random_rng;
        RelicHookContext ctx{};
        ctx.card_id = static_cast<uint16_t>(CardId::STRIKE);
        dispatch_relic_hook(s, rv.relics, rv.count, c.hook, ctx);
        EXPECT_EQ(s.action_count, 0)
            << "relic " << static_cast<int>(c.id) << " should be inert";
        EXPECT_EQ(s.player_hp, 70);
        EXPECT_EQ(s.card_random_rng.counter, before.counter);
        EXPECT_EQ(rv.relics[0].counter, -1) << "and mutates no counter";
    }
    EXPECT_TRUE(cases.empty()) << "no relic native body is deferred any more";
}

// ============================================================================
// Gambling Chip -- ONCE per combat, and the same action Gambler's Brew queues
// ============================================================================
//
// GamblingChip.java in full (48 lines): a private `activated` field (:18)
// cleared by atBattleStartPreDraw (:30-32), and
//
//   public void atTurnStartPostDraw() {                              (:34-42)
//       if (!this.activated) {
//           this.activated = true;
//           this.flash();
//           this.addToBot(new RelicAboveCreatureAction(player, this));
//           this.addToBot(new GamblingChipAction(AbstractDungeon.player));
//       }
//   }

TEST(RelicRaresShop, GamblingChipQueuesTheOptionalDiscardScreenOnceOnly) {
    CombatState s = MakeState();
    put_in_hand(s, CardId::STRIKE);
    put_in_hand(s, CardId::DEFEND);
    const RelicView rv = give(s, RelicId::GAMBLING_CHIP);
    ASSERT_EQ(rv.relics[0].counter, -1) << "-1 unset == not activated";
    s.card_random_rng = from_seed(3);
    s.turn = 1;  // first atTurnStartPostDraw call
    const RngStream before = s.card_random_rng;
    RelicHookContext ctx{};

    dispatch_relic_hook(s, rv.relics, rv.count,
                        RelicHook::AT_TURN_START_POST_DRAW, ctx);

    ASSERT_EQ(s.action_count, 1);
    const ActionQueueItem it = queued(s, 0);
    EXPECT_EQ(it.opcode, kOp(Opcode::CHOOSE_CARD));
    EXPECT_EQ(choose_kind_from_flags(it.flags),
              ChoiceKind::HAND_TO_DISCARD_THEN_DRAW);
    EXPECT_TRUE(choose_is_optional(it.flags));
    EXPECT_EQ(it.amount, 99);
    EXPECT_EQ(s.card_random_rng.counter, before.counter)
        << "the relic itself spends no rng";
    EXPECT_EQ(rv.relics[0].counter, -1)
        << "activated is private; AbstractRelic.counter is untouched";

    // Every LATER turn start finds the latch set and does nothing. That is the
    // fact the relic is entirely made of, and the one a per-turn reading loses.
    s.turn = 2;
    dispatch_relic_hook(s, rv.relics, rv.count,
                        RelicHook::AT_TURN_START_POST_DRAW, ctx);
    s.turn = 3;
    dispatch_relic_hook(s, rv.relics, rv.count,
                        RelicHook::AT_TURN_START_POST_DRAW, ctx);
    EXPECT_EQ(s.action_count, 1) << "once per combat, not once per turn";
}

// The relic has NO empty-hand guard -- that guard is Gambler's Brew's, on the
// potion (GamblersBrew.java:38). GamblingChipAction opens its screen regardless,
// and an empty hand is simply "nothing to show", so the pump does not block.
TEST(RelicRaresShop, GamblingChipQueuesEvenOnAnEmptyHandAndThenBlocksOnNothing) {
    CombatState s = MakeState();
    ASSERT_EQ(s.hand_count, 0);
    const RelicView rv = give(s, RelicId::GAMBLING_CHIP);
    s.turn = 1;
    RelicHookContext ctx{};

    dispatch_relic_hook(s, rv.relics, rv.count,
                        RelicHook::AT_TURN_START_POST_DRAW, ctx);

    ASSERT_EQ(s.action_count, 1) << "no guard on the relic side";
    const ActionQueueItem it = queued(s, 0);
    EXPECT_FALSE(choice_requires_user(s, it)) << "an empty hand shows nothing";
    drain(s);
    EXPECT_EQ(s.hand_count, 0);
    EXPECT_EQ(s.discard_count, 0);
}

// The relic and the potion queue the IDENTICAL item: GamblingChipAction is ONE
// action and `notChip` selects only a prompt string
// (GamblingChipAction.java:42-46). If these two ever diverge, the shared builder
// has been forked -- which is exactly how the same discard-then-draw-back logic
// would end up written twice.
TEST(RelicRaresShop, GamblingChipAndGamblersBrewQueueTheSameItem) {
    CombatState relic_state = MakeState();
    put_in_hand(relic_state, CardId::STRIKE);
    const RelicView rv = give(relic_state, RelicId::GAMBLING_CHIP);
    relic_state.turn = 1;
    RelicHookContext ctx{};
    dispatch_relic_hook(relic_state, rv.relics, rv.count,
                        RelicHook::AT_TURN_START_POST_DRAW, ctx);
    ASSERT_EQ(relic_state.action_count, 1);
    const ActionQueueItem from_relic = queued(relic_state, 0);

    CombatState potion_state = MakeState();
    put_in_hand(potion_state, CardId::STRIKE);
    ASSERT_TRUE(use_potion(potion_state, PotionId::GAMBLERS_BREW, 0));
    ASSERT_EQ(potion_state.action_count, 1);
    const ActionQueueItem from_potion =
        potion_state.action_queue[potion_state.action_head];

    EXPECT_EQ(from_relic.opcode, from_potion.opcode);
    EXPECT_EQ(from_relic.src, from_potion.src);
    EXPECT_EQ(from_relic.tgt, from_potion.tgt);
    EXPECT_EQ(from_relic.amount, from_potion.amount);
    EXPECT_EQ(from_relic.flags, from_potion.flags);
}

// Sling of Courage: Strength 2 at battle start, ELITE ROOMS ONLY
// (Sling.java:30-37). The marker is kCombatFlagEliteRoom; a boss room does NOT
// set it (MonsterRoomBoss.java:22-24), which is the whole reason this relic and
// Slaver's Collar are not twins.
TEST(RelicRaresShop, SlingOfCourageGrantsStrengthOnlyInAnEliteRoom) {
    {
        CombatState s = MakeState();
        s.flags |= kCombatFlagEliteRoom;
        const RelicView rv = give(s, RelicId::SLING_OF_COURAGE);
        RelicHookContext ctx{};
        dispatch_relic_hook(s, rv.relics, rv.count, RelicHook::AT_BATTLE_START,
                            ctx);
        ASSERT_EQ(s.action_count, 1);
        const ActionQueueItem it = queued(s, 0);
        EXPECT_EQ(it.opcode, kOp(Opcode::APPLY_POWER));
        EXPECT_EQ(it.src, kActorPlayer);
        EXPECT_EQ(it.tgt, kActorPlayer);
        EXPECT_EQ(it.amount, 2);
        EXPECT_EQ(it.flags, make_apply_power_flags(PowerId::STRENGTH));
        drain(s);
        ASSERT_EQ(s.player_power_count, 1);
        EXPECT_EQ(s.player_powers[0].power_id,
                  static_cast<uint16_t>(PowerId::STRENGTH));
        EXPECT_EQ(s.player_powers[0].amount, 2);
        // The counter is never touched (Sling.counter stays AbstractRelic's -1).
        EXPECT_EQ(rv.relics[0].counter, -1);
    }
    {
        // A non-elite combat -- which is also what a BOSS room looks like to the
        // flag -- queues nothing at all.
        CombatState s = MakeState();
        const RelicView rv = give(s, RelicId::SLING_OF_COURAGE);
        RelicHookContext ctx{};
        dispatch_relic_hook(s, rv.relics, rv.count, RelicHook::AT_BATTLE_START,
                            ctx);
        EXPECT_EQ(s.action_count, 0);
        EXPECT_EQ(s.player_power_count, 0);
    }
}

// --- Dead Branch -------------------------------------------------------------

// DeadBranch.onExhaust (DeadBranch.java:24-31): while any monster is still in
// the fight, put a copy of a truly-random combat-pool card into hand. The
// cardRandomRng pick is spent AT QUEUE TIME -- returnTrulyRandomCardInCombat()
// is an argument of the MakeTempCardInHandAction constructor.
TEST(RelicRaresShop, DeadBranchDrawsAtQueueTimeAndMakesACardInHand) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::DEAD_BRANCH);
    s.card_random_rng = from_seed(11);
    const RngStream before = s.card_random_rng;
    RelicHookContext ctx{};
    ctx.card_id = static_cast<uint16_t>(CardId::STRIKE);
    dispatch_relic_hook(s, rv.relics, rv.count, RelicHook::ON_EXHAUST, ctx);

    EXPECT_EQ(s.card_random_rng.counter, before.counter + 1)
        << "exactly one cardRandomRng draw, spent while the HOOK runs";
    ASSERT_EQ(s.action_count, 1);
    const ActionQueueItem it = queued(s, 0);
    EXPECT_EQ(it.opcode, kOp(Opcode::MAKE_CARD));
    EXPECT_EQ(it.src, static_cast<uint8_t>(CardPile::HAND));
    EXPECT_EQ(it.amount, 1);
    EXPECT_FALSE(make_card_upgraded_from_flags(it.flags))
        << "MakeTempCardInHandAction(..., false) -- a base library copy";

    // The chosen id is a member of the HEALING-filtered combat pool -- the same
    // 70-row list Discovery draws from, not the ATTACK-only pool.
    const CardId chosen = static_cast<CardId>(make_card_id_from_flags(it.flags));
    bool in_pool = false;
    for (unsigned i = 0; i < static_cast<unsigned>(kIroncladCombatPoolCount); ++i) {
        in_pool = in_pool || kIroncladCombatPool[i] == chosen;
    }
    EXPECT_TRUE(in_pool);

    drain(s);
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.card_pool[s.hand[0]].card_id,
              static_cast<uint16_t>(chosen));
    EXPECT_EQ(rv.relics[0].counter, -1) << "the counter is never touched";
}

// areMonstersBasicallyDead (MonsterGroup.java:90-95) gates it: an exhaust after
// the fight is over draws NOTHING, which is the point of testing the stream
// rather than just the queue.
TEST(RelicRaresShop, DeadBranchDrawsNothingWhenTheFightIsOver) {
    CombatState s = MakeState(/*monster_count=*/2);
    s.monsters[0].hp = 0;
    s.monsters[1].hp = 0;
    const RelicView rv = give(s, RelicId::DEAD_BRANCH);
    s.card_random_rng = from_seed(11);
    const RngStream before = s.card_random_rng;
    RelicHookContext ctx{};
    ctx.card_id = static_cast<uint16_t>(CardId::STRIKE);
    dispatch_relic_hook(s, rv.relics, rv.count, RelicHook::ON_EXHAUST, ctx);
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(s.card_random_rng.counter, before.counter);

    // An ESCAPED monster counts as out of the fight too (isEscaping), so a lone
    // escapee is the same answer.
    CombatState e = MakeState();
    e.monsters[0].flags |= kMonsterFlagEscaped;
    const RelicView erv = give(e, RelicId::DEAD_BRANCH);
    e.card_random_rng = from_seed(11);
    dispatch_relic_hook(e, erv.relics, erv.count, RelicHook::ON_EXHAUST, ctx);
    EXPECT_EQ(e.action_count, 0);
    EXPECT_EQ(e.card_random_rng.counter, 0);
}

// --- Orange Pellets ----------------------------------------------------------

namespace {

void add_player_power(CombatState& s, PowerId id, int16_t amount) {
    s.player_powers[s.player_power_count].power_id = static_cast<uint16_t>(id);
    s.player_powers[s.player_power_count].amount = amount;
    ++s.player_power_count;
}

void play(CombatState& s, const RelicView& rv, CardId id) {
    RelicHookContext ctx{};
    ctx.card_id = static_cast<uint16_t>(id);
    dispatch_relic_hook(s, rv.relics, rv.count, RelicHook::ON_USE_CARD, ctx);
}

}  // namespace

// OrangePellets.onUseCard (OrangePellets.java:41-58): the three type latches
// arm independently and the removal fires only once all three are set. Bash is
// an ATTACK, Defend a SKILL, Inflame a POWER.
TEST(RelicRaresShop, OrangePelletsFiresOnlyOnceAllThreeTypesArePlayed) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::ORANGE_PELLETS);
    play(s, rv, CardId::BASH);
    EXPECT_EQ(s.action_count, 0);
    play(s, rv, CardId::DEFEND);
    EXPECT_EQ(s.action_count, 0);
    play(s, rv, CardId::BASH) ;  // a repeat of an armed type changes nothing
    EXPECT_EQ(s.action_count, 0);
    play(s, rv, CardId::INFLAME);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::REMOVE_DEBUFFS));
    EXPECT_EQ(queued(s, 0).tgt, kActorPlayer);
    EXPECT_EQ(rv.relics[0].counter, -1) << "the counter is never touched";
    // Cleared ON FIRE, so it is re-armable inside the same turn.
    EXPECT_EQ(s.flags & kCombatFlagOrangePelletsMask, 0u);
}

// It can fire MORE THAN ONCE PER TURN -- the latches are cleared when it fires,
// not at turn end (OrangePellets.java:56-58).
TEST(RelicRaresShop, OrangePelletsReArmsWithinTheSameTurn) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::ORANGE_PELLETS);
    for (int round = 0; round < 2; ++round) {
        play(s, rv, CardId::BASH);
        play(s, rv, CardId::DEFEND);
        play(s, rv, CardId::INFLAME);
    }
    EXPECT_EQ(s.action_count, 2) << "fired twice in one turn";
}

// atTurnStart (OrangePellets.java:34-39) is the ONLY clear. atPreBattle does not
// touch the latches; a value-initialised CombatState stands in for that only
// because turn 1's atTurnStart precedes any card play.
TEST(RelicRaresShop, OrangePelletsLatchesAreClearedAtTurnStart) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::ORANGE_PELLETS);
    play(s, rv, CardId::BASH);
    play(s, rv, CardId::DEFEND);
    ASSERT_NE(s.flags & kCombatFlagOrangePelletsMask, 0u);
    dispatch_relics_at_turn_start(s, rv.relics, rv.count);
    EXPECT_EQ(s.flags & kCombatFlagOrangePelletsMask, 0u);
    EXPECT_EQ(s.action_count, 0);
    // The POWER alone no longer completes the set.
    play(s, rv, CardId::INFLAME);
    EXPECT_EQ(s.action_count, 0);
}

// REMOVE_DEBUFFS enumerates at RESOLVE time and removes every DEBUFF-typed
// power, leaving the buffs. The predicate is the LIVE-INSTANCE type, so a
// NEGATIVE Strength stack is a debuff and IS removed while a positive one is not
// (StrengthPower.java:81-89).
TEST(RelicRaresShop, RemoveDebuffsStripsDebuffsIncludingNegativeStrength) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::ORANGE_PELLETS);
    add_player_power(s, PowerId::VULNERABLE, 2);
    add_player_power(s, PowerId::STRENGTH, -3);
    add_player_power(s, PowerId::THORNS, 3);
    add_player_power(s, PowerId::WEAK, 1);
    add_player_power(s, PowerId::FRAIL, 2);
    play(s, rv, CardId::BASH);
    play(s, rv, CardId::DEFEND);
    play(s, rv, CardId::INFLAME);
    drain(s);
    EXPECT_EQ(player_power(s, PowerId::VULNERABLE), nullptr);
    EXPECT_EQ(player_power(s, PowerId::WEAK), nullptr);
    EXPECT_EQ(player_power(s, PowerId::FRAIL), nullptr);
    EXPECT_EQ(player_power(s, PowerId::STRENGTH), nullptr)
        << "a negative Strength stack is DEBUFF-typed and goes";
    const PowerSlot* thorns = player_power(s, PowerId::THORNS);
    ASSERT_NE(thorns, nullptr) << "buffs stay";
    EXPECT_EQ(thorns->amount, 3);
}

// ...and a POSITIVE Strength is a BUFF and survives -- the other half of the
// same two-term predicate.
TEST(RelicRaresShop, RemoveDebuffsKeepsAPositiveStrengthStack) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::ORANGE_PELLETS);
    add_player_power(s, PowerId::STRENGTH, 3);
    add_player_power(s, PowerId::VULNERABLE, 2);
    play(s, rv, CardId::BASH);
    play(s, rv, CardId::DEFEND);
    play(s, rv, CardId::INFLAME);
    drain(s);
    const PowerSlot* str = player_power(s, PowerId::STRENGTH);
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->amount, 3);
    EXPECT_EQ(player_power(s, PowerId::VULNERABLE), nullptr);
}

// The enumeration happens when the ACTION resolves, not when it is queued --
// which is exactly why this needed its own opcode. A debuff applied AFTER the
// queue but BEFORE the resolve is still removed.
TEST(RelicRaresShop, RemoveDebuffsEnumeratesAtResolveTimeNotQueueTime) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::ORANGE_PELLETS);
    add_player_power(s, PowerId::VULNERABLE, 2);
    play(s, rv, CardId::BASH);
    play(s, rv, CardId::DEFEND);
    play(s, rv, CardId::INFLAME);
    ASSERT_EQ(s.action_count, 1);
    // ... a Weak lands between the queue and the drain.
    add_player_power(s, PowerId::WEAK, 1);
    drain(s);
    EXPECT_EQ(player_power(s, PowerId::VULNERABLE), nullptr);
    EXPECT_EQ(player_power(s, PowerId::WEAK), nullptr)
        << "a queue-time expansion would have missed this one";
}

// Prismatic Shard, Frozen Eye, Toolbox, Unceasing Top and the four shop
// run-layer relics carry NO hook bindings at all: their rows exist for the pool
// slot and the relicRng draws, and a stray binding would be inert code.
TEST(RelicRaresShop, MarkerRowsCarryNoCombatHooks) {
    const RelicId markers[] = {
        RelicId::PRISMATIC_SHARD, RelicId::FROZEN_EYE, RelicId::TOOLBOX,
        RelicId::UNCEASING_TOP,   RelicId::CAULDRON,   RelicId::ORRERY,
        RelicId::DOLLYS_MIRROR,   RelicId::MEMBERSHIP_CARD,
        // The pipeline/marker relics whose bodies live at a cited engine site.
        RelicId::CALIPERS,        RelicId::ICE_CREAM,  RelicId::CHEMICAL_X,
        RelicId::STRANGE_SPOON,   RelicId::TORII,      RelicId::TUNGSTEN_ROD,
        RelicId::GINGER,          RelicId::TURNIP,     RelicId::CHAMPION_BELT,
        RelicId::LIZARD_TAIL,     RelicId::MAGIC_FLOWER,
        RelicId::ODD_MUSHROOM,
    };
    for (RelicId id : markers) {
        const sts::registry::RelicDef* d = relic_def(id);
        ASSERT_NE(d, nullptr) << static_cast<int>(id);
        EXPECT_EQ(d->hook_count, 0) << "relic " << static_cast<int>(id);
        EXPECT_FALSE(d->native) << "relic " << static_cast<int>(id);
    }
}

// Prismatic Shard is a documented no-op whose ROW must nonetheless be exact: it
// holds a real SHOP pool slot, has no canSpawn gate, and neither equips nor
// triggers anything. Its real effect (cross-colour card rewards) needs a second
// character to exist.
TEST(RelicRaresShop, PrismaticShardOccupiesItsPoolSlotAndDoesNothingElse) {
    const sts::registry::RelicDef* d = relic_def(RelicId::PRISMATIC_SHARD);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->tier, sts::registry::RelicTier::SHOP);
    EXPECT_GE(d->pool_order, 0) << "it must consume a pool slot";
    EXPECT_EQ(d->initial_counter, -1);

    RunState rs{};
    rs.hp = 50;
    rs.max_hp = 80;
    rs.gold = 10;
    RngStream misc = from_seed(1);
    ASSERT_EQ(acquire_relic(rs, misc, RelicId::PRISMATIC_SHARD),
              RelicAcquireResult::ACQUIRED);
    EXPECT_EQ(rs.hp, 50);
    EXPECT_EQ(rs.max_hp, 80);
    EXPECT_EQ(rs.gold, 10);
    EXPECT_EQ(misc.counter, 0);

    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::PRISMATIC_SHARD);
    for (int h = 0; h < kRelicHookCount; ++h) {
        dispatch_relic_hook(s, rv.relics, rv.count, static_cast<RelicHook>(h),
                            RelicHookContext{});
    }
    EXPECT_EQ(s.action_count, 0);
    EXPECT_EQ(s.player_hp, 70);
}

// ============================================================================
// Acquisition-order dispatch holds for the new tiers too (stage-a trap 8).
// ============================================================================

TEST(RelicRaresShop, BattleStartRelicsFireInAcquisitionOrder) {
    CombatState a = MakeState();
    Relics ra;
    ra.add(RelicId::THREAD_AND_NEEDLE);
    ra.add(RelicId::CLOCKWORK_SOUVENIR);
    const RelicView va = install(a, ra);
    dispatch_relics_at_battle_start(a, va.relics, va.count);
    ASSERT_EQ(a.action_count, 2);
    EXPECT_EQ(queued(a, 0).flags,
              make_apply_power_flags(PowerId::PLATED_ARMOR));
    EXPECT_EQ(queued(a, 1).flags, make_apply_power_flags(PowerId::ARTIFACT));

    CombatState b = MakeState();
    Relics rb;
    rb.add(RelicId::CLOCKWORK_SOUVENIR);
    rb.add(RelicId::THREAD_AND_NEEDLE);
    const RelicView vb = install(b, rb);
    dispatch_relics_at_battle_start(b, vb.relics, vb.count);
    ASSERT_EQ(b.action_count, 2);
    EXPECT_EQ(queued(b, 0).flags, make_apply_power_flags(PowerId::ARTIFACT));
    EXPECT_EQ(queued(b, 1).flags,
              make_apply_power_flags(PowerId::PLATED_ARMOR));
}

}  // namespace
}  // namespace sts::engine
