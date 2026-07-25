// Tier-2 suite for the BOSS relic pool (registry/relics.yaml ids 112-133) and
// the Act-1-event-reachable SPECIAL tier (ids 134-142). One or more tests per
// relic whose behaviour is live, hand-derived from the cited decompiled Java;
// every relic whose body is deliberately deferred, and every marker row, is
// asserted to be exactly INERT -- so a later task that implements one sees this
// suite fail rather than silently changing behaviour nobody was watching.
//
// The registry table itself (tiers, pool_order contiguity, hook bindings) is
// covered by registry_gen_test; the shuffled pool ORDER against the live oracle
// captures is covered by relic_pools_test. What is here is the mechanics.

#include <cstdint>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"      // legal_actions (Velvet Choker's veto)
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/piles.hpp"        // discard_hand_at_end_of_turn (Runic Pyramid)
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/relic_hooks.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/relics.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"  // relic_from_game_id (the translator join)

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

const PowerSlot* monster_power(const CombatState& s, uint8_t m, PowerId id) {
    for (uint8_t i = 0; i < s.monsters[m].power_count; ++i) {
        if (s.monsters[m].powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.monsters[m].powers[i];
        }
    }
    return nullptr;
}

CardPoolIndex fresh_card(CombatState& s, CardId id) {
    CardPoolIndex pi = 0;
    while (pi < kCardPoolCap &&
           s.card_pool[pi].card_id != static_cast<uint16_t>(CardId::NONE)) {
        ++pi;
    }
    const CardDef* def = card_def(id);
    s.card_pool[pi].card_id = static_cast<uint16_t>(id);
    s.card_pool[pi].cost_now = card_cost(*def, 0);
    s.card_pool[pi].flags = card_flags(*def, 0);
    return pi;
}

CardPoolIndex put_in_hand(CombatState& s, CardId id) {
    const CardPoolIndex pi = fresh_card(s, id);
    s.hand[s.hand_count++] = pi;
    return pi;
}

// Append to the draw pile. draw[draw_count - 1] is the TOP (drawn first), so the
// LAST id passed here is the first one drawn.
CardPoolIndex put_in_draw(CombatState& s, CardId id) {
    const CardPoolIndex pi = fresh_card(s, id);
    s.draw[s.draw_count++] = pi;
    return pi;
}

void op_draw(CombatState& s, int amount) {
    ActionQueueItem it{};
    it.opcode = kOp(Opcode::DRAW);
    it.src = kActorPlayer;
    it.tgt = kActorPlayer;
    it.amount = amount;
    execute_opcode(s, it);
}

void add_player_power(CombatState& s, PowerId id, int16_t amount) {
    s.player_powers[s.player_power_count].power_id = static_cast<uint16_t>(id);
    s.player_powers[s.player_power_count].amount = amount;
    ++s.player_power_count;
}

// ============================================================================
// Registry shape -- the two tiers are complete and their rows carry what the
// mechanics below assume.
// ============================================================================

// The BOSS roster is the live oracle's relicPools.boss: 22 rows, pool_order
// dense over 0..21. SPECIAL rows are in NO pool and must carry pool_order -1 --
// a stray pool_order there would insert an extra id into a shuffled pool and
// move every later relicRng draw.
TEST(RelicBossSpecial, TierRostersAreExactlyTheLivePools) {
    int boss = 0;
    int special = 0;
    bool boss_slots[22] = {};
    for (const sts::registry::RelicDef* d : sts::registry::kRelicDefs) {
        if (d->tier == sts::registry::RelicTier::BOSS) {
            ++boss;
            ASSERT_GE(d->pool_order, 0);
            ASSERT_LT(d->pool_order, 22);
            EXPECT_FALSE(boss_slots[d->pool_order]) << "duplicate boss slot";
            boss_slots[d->pool_order] = true;
        } else if (d->tier == sts::registry::RelicTier::SPECIAL) {
            ++special;
            EXPECT_EQ(d->pool_order, -1)
                << "a SPECIAL relic belongs to no shuffled pool";
        }
    }
    EXPECT_EQ(boss, 22);
    // 9 Act-1 event specials + Circlet (the pool-exhaustion fallback) + Odd
    // Mushroom (landed with the rare batch).
    EXPECT_EQ(special, 11);
    for (bool slot : boss_slots) {
        EXPECT_TRUE(slot) << "boss pool_order must be dense 0..21";
    }
}

TEST(RelicBossSpecial, GameIdsJoinTheOracleStrings) {
    // The join keys the translator uses. Four of these are NOT the class name --
    // getting one wrong makes relicPools translation throw rather than silently
    // mismatch, which is why they are pinned here.
    EXPECT_EQ(sts::registry::relic_from_game_id("SlaversCollar"),
              RelicId::SLAVERS_COLLAR);
    EXPECT_EQ(sts::registry::relic_from_game_id("SacredBark"),
              RelicId::SACRED_BARK);
    EXPECT_EQ(sts::registry::relic_from_game_id("NeowsBlessing"),
              RelicId::NEOWS_LAMENT);
    EXPECT_EQ(sts::registry::relic_from_game_id("WarpedTongs"),
              RelicId::WARPED_TONGS);
    EXPECT_EQ(sts::registry::relic_from_game_id("Philosopher's Stone"),
              RelicId::PHILOSOPHERS_STONE);
    EXPECT_EQ(sts::registry::relic_from_game_id("SsserpentHead"),
              RelicId::SSSERPENT_HEAD);
}

// ============================================================================
// Snecko Eye -- the per-draw cost roll stream and its draw-order accounting.
// ============================================================================

// SneckoEye.atPreBattle (SneckoEye.java:39-43) queues the Confusion application
// at the BOTTOM, and it binds AT_PRE_BATTLE -- not AT_BATTLE_START. That is the
// difference between the opening hand being cost-randomised and escaping.
TEST(RelicBossSpecial, SneckoEyeQueuesConfusionAtPreBattleOnly) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::SNECKO_EYE);

    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    EXPECT_EQ(s.action_count, 0) << "atBattleStart is the wrong hook";

    dispatch_relics_at_pre_battle(s, rv.relics, rv.count);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::APPLY_POWER));
    EXPECT_EQ(queued(s, 0).tgt, kActorPlayer);
    EXPECT_EQ(queued(s, 0).amount, 1);
    EXPECT_EQ(queued(s, 0).flags, make_apply_power_flags(PowerId::CONFUSION));

    drain(s);
    bool found = false;
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        found = found || s.player_powers[i].power_id ==
                             static_cast<uint16_t>(PowerId::CONFUSION);
    }
    EXPECT_TRUE(found);
}

// ConfusionPower.onCardDraw (ConfusionPower.java:38-48): one cardRandomRng
// random(3) per drawn card whose cost >= 0, IN DRAW ORDER, and none at all for a
// card whose cost is negative (X-cost / unplayable). The expected values are
// re-derived from an independent stream, not read back out of the engine.
TEST(RelicBossSpecial, ConfusionRollsOneCostPerDrawnCardInDrawOrder) {
    CombatState s = MakeState();
    add_player_power(s, PowerId::CONFUSION, 1);
    // draw[] tail is the top: STRIKE is drawn first, then WOUND, then DEFEND.
    put_in_draw(s, CardId::DEFEND);
    const CardPoolIndex wound = put_in_draw(s, CardId::WOUND);
    put_in_draw(s, CardId::STRIKE);

    s.card_random_rng = from_seed(7);
    RngStream ref = from_seed(7);
    const int32_t first = random(ref, 3);
    const int32_t second = random(ref, 3);

    op_draw(s, 3);

    ASSERT_EQ(s.hand_count, 3);
    // Hand order == draw order: STRIKE, WOUND, DEFEND.
    EXPECT_EQ(s.card_pool[s.hand[0]].card_id,
              static_cast<uint16_t>(CardId::STRIKE));
    EXPECT_EQ(s.card_pool[s.hand[0]].cost_now, first);
    EXPECT_EQ(s.card_pool[s.hand[2]].card_id,
              static_cast<uint16_t>(CardId::DEFEND));
    EXPECT_EQ(s.card_pool[s.hand[2]].cost_now, second);
    // The Wound is UNPLAYABLE (the generator's cost -2 sentinel), i.e. Java
    // `card.cost < 0`: no draw, no cost change.
    EXPECT_TRUE(has_card_flag(s.card_pool[wound].flags, CardFlag::UNPLAYABLE));
    EXPECT_EQ(s.card_pool[wound].cost_now, 0);
    // EXACTLY two draws for three drawn cards.
    EXPECT_EQ(s.card_random_rng.counter, 2);
    EXPECT_EQ(s.card_random_rng.s0, ref.s0);
    EXPECT_EQ(s.card_random_rng.s1, ref.s1);
}

// The draw happens BEFORE the `card.cost != newCost` comparison, so a card that
// rolls its own current cost still consumes one draw. Pinning the DRAW COUNT
// rather than only the resulting costs is what catches an implementation that
// "optimises" the equal case away.
TEST(RelicBossSpecial, ConfusionSpendsADrawEvenWhenTheCostIsUnchanged) {
    // Find a seed whose first random(3) is 1 -- the base cost of Strike -- so the
    // Java's `if (card.cost != newCost)` body is skipped while the draw is not.
    int64_t seed = 0;
    for (int64_t candidate = 1; candidate < 200; ++candidate) {
        RngStream probe = from_seed(candidate);
        if (random(probe, 3) == 1) {
            seed = candidate;
            break;
        }
    }
    ASSERT_NE(seed, 0) << "no seed in range rolls a 1 first";

    CombatState s = MakeState();
    add_player_power(s, PowerId::CONFUSION, 1);
    const CardPoolIndex strike = put_in_draw(s, CardId::STRIKE);
    ASSERT_EQ(s.card_pool[strike].cost_now, 1);
    s.card_random_rng = from_seed(seed);

    op_draw(s, 1);

    EXPECT_EQ(s.card_pool[strike].cost_now, 1) << "cost is unchanged";
    EXPECT_EQ(s.card_random_rng.counter, 1) << "but the draw still happened";
}

// Confusion writes card.cost, not just costForTurn, so the new cost must survive
// the end-of-turn reset. COST_MODIFIED_FOR_TURN is therefore cleared, not set.
TEST(RelicBossSpecial, ConfusionCostSurvivesTheEndOfTurnCostReset) {
    CombatState s = MakeState();
    add_player_power(s, PowerId::CONFUSION, 1);
    const CardPoolIndex bash = put_in_draw(s, CardId::BASH);
    s.card_random_rng = from_seed(11);
    RngStream ref = from_seed(11);
    const int32_t rolled = random(ref, 3);

    op_draw(s, 1);
    ASSERT_EQ(s.card_pool[bash].cost_now, rolled);
    EXPECT_FALSE(
        has_card_flag(s.card_pool[bash].flags, CardFlag::COST_MODIFIED_FOR_TURN));

    reset_cost_for_turn(s, bash);
    EXPECT_EQ(s.card_pool[bash].cost_now, rolled)
        << "a this-turn-only reset must not restore the registry cost";
}

// ============================================================================
// Velvet Choker
// ============================================================================

TEST(RelicBossSpecial, VelvetChokerCountsPlaysAndClampsAtSix) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::VELVET_CHOKER);

    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    EXPECT_EQ(rv.relics[0].counter, 0);
    for (int i = 0; i < 9; ++i) {
        dispatch_relics_on_play_card(s, rv.relics, rv.count,
                                     static_cast<uint16_t>(CardId::STRIKE));
    }
    EXPECT_EQ(rv.relics[0].counter, 6) << "the `< 6` guard clamps the counter";
    EXPECT_EQ(s.action_count, 0) << "it queues nothing, ever";

    dispatch_relics_at_turn_start(s, rv.relics, rv.count);
    EXPECT_EQ(rv.relics[0].counter, 0) << "every turn start rearms it";

    dispatch_relics_on_victory(s, rv.relics, rv.count);
    EXPECT_EQ(rv.relics[0].counter, -1);
}

TEST(RelicBossSpecial, VelvetChokerVetoesEveryCardOnceSpent) {
    CombatState s = MakeState();
    put_in_hand(s, CardId::STRIKE);
    put_in_hand(s, CardId::DEFEND);
    give(s, RelicId::VELVET_CHOKER, 5);
    s.cards_played_this_turn = 5;

    ActionMask m{};
    legal_actions(s, m);
    EXPECT_TRUE(m.can_play[0]);
    EXPECT_TRUE(m.can_play[1]);

    s.relics[0].counter = 6;
    s.cards_played_this_turn = 6;
    legal_actions(s, m);
    EXPECT_FALSE(m.can_play[0]);
    EXPECT_FALSE(m.can_play[1]);
    EXPECT_TRUE(m.can_end_turn) << "the veto is on PLAYING, not on ending";
}

// Without the relic the six-play state is entirely ordinary -- the veto must not
// leak out of the relic-mirror check into a bare cards_played_this_turn test.
TEST(RelicBossSpecial, SixPlaysWithoutVelvetChokerIsNotALock) {
    CombatState s = MakeState();
    put_in_hand(s, CardId::STRIKE);
    s.cards_played_this_turn = 6;
    ActionMask m{};
    legal_actions(s, m);
    EXPECT_TRUE(m.can_play[0]);
}

// ============================================================================
// Philosopher's Stone / Mark of Pain / Runic Cube / Black Blood
// ============================================================================

TEST(RelicBossSpecial, PhilosophersStoneGivesEveryMonsterOneStrengthDirectly) {
    CombatState s = MakeState(3);
    const RelicView rv = give(s, RelicId::PHILOSOPHERS_STONE);
    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    // AbstractCreature.addPower is SYNCHRONOUS -- nothing is queued.
    EXPECT_EQ(s.action_count, 0);
    for (uint8_t m = 0; m < 3; ++m) {
        const PowerSlot* p = monster_power(s, m, PowerId::STRENGTH);
        ASSERT_NE(p, nullptr) << "monster " << static_cast<int>(m);
        EXPECT_EQ(p->amount, 1);
    }
}

TEST(RelicBossSpecial, MarkOfPainShufflesTwoWoundsIntoTheDrawPile) {
    CombatState s = MakeState();
    put_in_draw(s, CardId::STRIKE);
    put_in_draw(s, CardId::DEFEND);
    const RelicView rv = give(s, RelicId::MARK_OF_PAIN);
    s.card_random_rng = from_seed(5);

    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::MAKE_CARD));
    EXPECT_EQ(queued(s, 0).amount, 2);
    EXPECT_EQ(queued(s, 0).src, static_cast<uint8_t>(CardPile::DRAW_RANDOM));
    EXPECT_EQ(make_card_id_from_flags(queued(s, 0).flags),
              static_cast<uint16_t>(CardId::WOUND));

    drain(s);
    EXPECT_EQ(s.draw_count, 4);
    int wounds = 0;
    for (uint8_t i = 0; i < s.draw_count; ++i) {
        wounds += s.card_pool[s.draw[i]].card_id ==
                  static_cast<uint16_t>(CardId::WOUND);
    }
    EXPECT_EQ(wounds, 2);
    // randomSpot == true: one cardRandomRng draw per inserted copy.
    EXPECT_EQ(s.card_random_rng.counter, 2);
}

TEST(RelicBossSpecial, RunicCubeDrawsOnAnyHpLossButNotOnZero) {
    CombatState s = MakeState();
    put_in_draw(s, CardId::STRIKE);
    const RelicView rv = give(s, RelicId::RUNIC_CUBE);

    dispatch_relics_was_hp_lost(s, rv.relics, rv.count, 0);
    EXPECT_EQ(s.action_count, 0) << "a fully blocked hit draws nothing";

    dispatch_relics_was_hp_lost(s, rv.relics, rv.count, 1);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::DRAW));
    EXPECT_EQ(queued(s, 0).amount, 1);
    drain(s);
    EXPECT_EQ(s.hand_count, 1);
}

TEST(RelicBossSpecial, BlackBloodHealsTwelveOnVictoryAndOnlyWhileAlive) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::BLACK_BLOOD);
    dispatch_relics_on_victory(s, rv.relics, rv.count);
    EXPECT_EQ(s.player_hp, 80) << "70 + 12 clamps to max 80";

    CombatState dead = MakeState();
    dead.player_hp = 0;
    const RelicView dv = give(dead, RelicId::BLACK_BLOOD);
    dispatch_relics_on_victory(dead, dv.relics, dv.count);
    EXPECT_EQ(dead.player_hp, 0) << "the `currentHealth > 0` guard";
}

TEST(RelicBossSpecial, BlackBloodHealGoesThroughTheMagicFlowerSeam) {
    CombatState s = MakeState();
    s.player_hp = 40;
    Relics r;
    r.add(RelicId::MAGIC_FLOWER);
    r.add(RelicId::BLACK_BLOOD);
    const RelicView rv = install(s, r);
    dispatch_relics_on_victory(s, rv.relics, rv.count);
    // MathUtils.round(12 * 1.5f) == 18.
    EXPECT_EQ(s.player_hp, 58);
}

// ============================================================================
// Runic Pyramid
// ============================================================================

TEST(RelicBossSpecial, RunicPyramidKeepsTheHandButStillExhaustsEtherealCards) {
    CombatState s = MakeState();
    const CardPoolIndex a = put_in_hand(s, CardId::STRIKE);
    const CardPoolIndex b = put_in_hand(s, CardId::DEFEND);
    // Mark one instance ETHEREAL: the sweep that exhausts it sits OUTSIDE the
    // Runic Pyramid guard in the Java (triggerOnEndOfPlayerTurn).
    s.card_pool[b].flags = static_cast<uint16_t>(s.card_pool[b].flags |
                                                 card_flag_bit(CardFlag::ETHEREAL));
    give(s, RelicId::RUNIC_PYRAMID);

    discard_hand_at_end_of_turn(s);

    EXPECT_EQ(s.discard_count, 0) << "the discard loop is skipped entirely";
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], a);
    ASSERT_EQ(s.exhaust_count, 1);
    EXPECT_EQ(s.exhaust[0], b);
}

TEST(RelicBossSpecial, WithoutRunicPyramidTheHandIsDiscarded) {
    CombatState s = MakeState();
    put_in_hand(s, CardId::STRIKE);
    put_in_hand(s, CardId::DEFEND);
    discard_hand_at_end_of_turn(s);
    EXPECT_EQ(s.hand_count, 0);
    EXPECT_EQ(s.discard_count, 2);
}

// ============================================================================
// Ectoplasm: the act gate and the gold suppression
// ============================================================================

TEST(RelicBossSpecial, EctoplasmSpawnsOnlyInActOne) {
    RelicSpawnContext act1{};
    act1.act = 1;
    RelicSpawnContext act2{};
    act2.act = 2;
    EXPECT_TRUE(relic_can_spawn(RelicId::ECTOPLASM, act1));
    EXPECT_FALSE(relic_can_spawn(RelicId::ECTOPLASM, act2));
    // No Settings.isEndless disjunct in Ectoplasm.canSpawn -- endless does NOT
    // bypass the act clause.
    act2.endless = true;
    EXPECT_FALSE(relic_can_spawn(RelicId::ECTOPLASM, act2));
    // Default context: S1 is Act 1, so the fresh answer is true.
    EXPECT_TRUE(relic_can_spawn(RelicId::ECTOPLASM, RelicSpawnContext{}));
}

TEST(RelicBossSpecial, EctoplasmSuppressesEveryRunLayerGoldGain) {
    // Old Coin's onEquip is a 300-gold gainGold; Ceramic Fish's onObtainCard is
    // a 9-gold one. Both go through the shared door, so both see Ectoplasm.
    RunState rs{};
    rs.gold = 99;
    RngStream misc = from_seed(1);
    ASSERT_EQ(acquire_relic(rs, misc, RelicId::ECTOPLASM),
              RelicAcquireResult::ACQUIRED);
    ASSERT_EQ(acquire_relic(rs, misc, RelicId::OLD_COIN),
              RelicAcquireResult::ACQUIRED);
    EXPECT_EQ(rs.gold, 99) << "gainGold returns before the +=";

    RunState without{};
    without.gold = 99;
    RngStream misc2 = from_seed(1);
    ASSERT_EQ(acquire_relic(without, misc2, RelicId::OLD_COIN),
              RelicAcquireResult::ACQUIRED);
    EXPECT_EQ(without.gold, 399);
}

// ============================================================================
// Black Blood's canSpawn
// ============================================================================

TEST(RelicBossSpecial, BlackBloodSpawnsOnlyWithBurningBlood) {
    RunState rs{};
    RelicSpawnContext ctx{};
    fill_boss_spawn_gates(rs, ctx);
    EXPECT_FALSE(ctx.has_burning_blood);
    EXPECT_FALSE(relic_can_spawn(RelicId::BLACK_BLOOD, ctx));

    RngStream misc = from_seed(1);
    ASSERT_EQ(acquire_relic(rs, misc, RelicId::BURNING_BLOOD),
              RelicAcquireResult::ACQUIRED);
    fill_boss_spawn_gates(rs, ctx);
    EXPECT_TRUE(ctx.has_burning_blood);
    EXPECT_TRUE(relic_can_spawn(RelicId::BLACK_BLOOD, ctx));

    // The context DEFAULT is the fresh-Ironclad answer, so a draw that never
    // fills the gate still lets Black Blood spawn -- which is what keeps the
    // relicRng draw order right for an unfilled context.
    EXPECT_TRUE(relic_can_spawn(RelicId::BLACK_BLOOD, RelicSpawnContext{}));
}

// A spurious canSpawn override would change the relicRng draw order, so the
// ABSENCE of one on the other twenty boss rows is pinned -- behaviourally, by
// asking each relic under a context hostile to every gate the S1 relic set has
// (late floor, in a shop, a later act, no Burning Blood, deck empty of
// everything, the campfire trio already held). Only the two rows that DO gate
// may answer false.
TEST(RelicBossSpecial, BossTierGatesOnlyEctoplasmAndBlackBlood) {
    RelicSpawnContext hostile{};
    hostile.floor = 99;
    hostile.in_shop = true;
    hostile.endless = false;
    hostile.campfire_relic_count = 2;
    hostile.act = 4;
    hostile.has_burning_blood = false;
    for (const sts::registry::RelicDef* d : sts::registry::kRelicDefs) {
        if (d->tier != sts::registry::RelicTier::BOSS) {
            continue;
        }
        const RelicId id = static_cast<RelicId>(d->id);
        const bool gated = id == RelicId::ECTOPLASM || id == RelicId::BLACK_BLOOD;
        EXPECT_EQ(relic_can_spawn(id, hostile), !gated)
            << "relic " << static_cast<int>(id);
    }
}

// ============================================================================
// The SPECIAL tier
// ============================================================================

TEST(RelicBossSpecial, NeowsLamentSetsEveryEnemyToOneHpForThreeCombats) {
    CombatState s = MakeState(2, 60);
    const RelicView rv = give(s, RelicId::NEOWS_LAMENT, 3);

    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    EXPECT_EQ(rv.relics[0].counter, 2);
    EXPECT_EQ(s.monsters[0].hp, 1);
    EXPECT_EQ(s.monsters[1].hp, 1);
    EXPECT_EQ(s.monsters[0].max_hp, 60) << "max HP is untouched";
    EXPECT_EQ(s.action_count, 0) << "a raw field write, not damage";

    CombatState c2 = MakeState(1, 60);
    Relics r2;
    r2.add(RelicId::NEOWS_LAMENT, 2);
    const RelicView v2 = install(c2, r2);
    dispatch_relics_at_battle_start(c2, v2.relics, v2.count);
    EXPECT_EQ(v2.relics[0].counter, 1);
    EXPECT_EQ(c2.monsters[0].hp, 1);

    // Third use: the counter reaches 0, so setCounter(-2) marks it used up -- and
    // the effect STILL fires on this combat.
    CombatState c3 = MakeState(1, 60);
    Relics r3;
    r3.add(RelicId::NEOWS_LAMENT, 1);
    const RelicView v3 = install(c3, r3);
    dispatch_relics_at_battle_start(c3, v3.relics, v3.count);
    EXPECT_EQ(v3.relics[0].counter, -2);
    EXPECT_EQ(c3.monsters[0].hp, 1);

    // Fourth: spent.
    CombatState c4 = MakeState(1, 60);
    Relics r4;
    r4.add(RelicId::NEOWS_LAMENT, -2);
    const RelicView v4 = install(c4, r4);
    dispatch_relics_at_battle_start(c4, v4.relics, v4.count);
    EXPECT_EQ(c4.monsters[0].hp, 60);
}

TEST(RelicBossSpecial, NeowsLamentRowStartsWithThreeCharges) {
    const sts::registry::RelicDef* d = relic_def(RelicId::NEOWS_LAMENT);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->initial_counter, 3);
    EXPECT_EQ(relic_def(RelicId::NLOTHS_MASK)->initial_counter, 1);
}

TEST(RelicBossSpecial, FaceOfClericAddsOneMaxHpAfterEveryCombat) {
    CombatState s = MakeState();
    s.player_hp = 70;
    s.player_max_hp = 80;
    const RelicView rv = give(s, RelicId::FACE_OF_CLERIC);
    dispatch_relics_on_victory(s, rv.relics, rv.count);
    EXPECT_EQ(s.player_max_hp, 81);
    EXPECT_EQ(s.player_hp, 71);

    // At full HP the heal keeps the player topped up at the NEW max.
    CombatState full = MakeState();
    full.player_hp = 80;
    full.player_max_hp = 80;
    const RelicView fv = give(full, RelicId::FACE_OF_CLERIC);
    dispatch_relics_on_victory(full, fv.relics, fv.count);
    EXPECT_EQ(full.player_max_hp, 81);
    EXPECT_EQ(full.player_hp, 81);
}

TEST(RelicBossSpecial, GremlinMaskWeakensThePlayerAtBattleStart) {
    CombatState s = MakeState();
    const RelicView rv = give(s, RelicId::GREMLIN_MASK);
    dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, kOp(Opcode::APPLY_POWER));
    EXPECT_EQ(queued(s, 0).tgt, kActorPlayer);
    EXPECT_EQ(queued(s, 0).amount, 1);
    EXPECT_EQ(queued(s, 0).flags, make_apply_power_flags(PowerId::WEAK));
    drain(s);
    bool weak = false;
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        weak = weak || s.player_powers[i].power_id ==
                           static_cast<uint16_t>(PowerId::WEAK);
    }
    EXPECT_TRUE(weak);
}

// ============================================================================
// Deliberate no-ops -- every one is pinned so implementing it fails HERE first.
// ============================================================================

TEST(RelicBossSpecial, DeferredNativeBodiesQueueNothingAndTouchNoRng) {
    struct Case { RelicId id; RelicHook hook; };
    const Case cases[] = {
        {RelicId::WARPED_TONGS, RelicHook::AT_TURN_START_POST_DRAW},
    };
    for (const Case& c : cases) {
        CombatState s = MakeState();
        put_in_hand(s, CardId::STRIKE);
        const RelicView rv = give(s, c.id);
        s.shuffle_rng = from_seed(3);
        s.card_random_rng = from_seed(3);
        const RngStream shuffle_before = s.shuffle_rng;
        dispatch_relic_hook(s, rv.relics, rv.count, c.hook, RelicHookContext{});
        EXPECT_EQ(s.action_count, 0)
            << "relic " << static_cast<int>(c.id) << " should be inert";
        EXPECT_EQ(s.shuffle_rng.counter, shuffle_before.counter);
        EXPECT_EQ(s.card_random_rng.counter, 0);
        EXPECT_EQ(s.card_pool[s.hand[0]].upgrade, 0) << "and upgrades nothing";
        EXPECT_EQ(rv.relics[0].counter, -1) << "and mutates no counter";
    }
}

// The five boss relics that DECLARE an onEquip override and whose body is
// deferred: acquiring one must move nothing at all -- not RunState, not miscRng.
TEST(RelicBossSpecial, DeferredOnEquipBodiesChangeNothing) {
    const RelicId ids[] = {
        RelicId::PANDORAS_BOX, RelicId::TINY_HOUSE, RelicId::ASTROLABE,
        RelicId::EMPTY_CAGE,   RelicId::CALLING_BELL,
    };
    for (RelicId id : ids) {
        RunState rs{};
        rs.hp = 50;
        rs.max_hp = 80;
        rs.gold = 10;
        rs.master_deck_count = 1;
        rs.master_deck[0].card_id = static_cast<uint16_t>(CardId::STRIKE);
        RngStream misc = from_seed(1);
        // That each of these DECLARES the override is pinned by the build, not
        // by an assertion: the generated STS_REGISTRY_RELIC_ON_EQUIP table
        // odr-uses a handler per `pickup: on_equip` row, so dropping the body
        // would be a link error. What is asserted here is that the declared body
        // is EXACTLY empty.
        ASSERT_EQ(acquire_relic(rs, misc, id), RelicAcquireResult::ACQUIRED)
            << static_cast<int>(id);
        EXPECT_EQ(rs.hp, 50) << static_cast<int>(id);
        EXPECT_EQ(rs.max_hp, 80) << static_cast<int>(id);
        EXPECT_EQ(rs.gold, 10) << static_cast<int>(id);
        EXPECT_EQ(rs.master_deck_count, 1) << static_cast<int>(id);
        EXPECT_EQ(rs.master_deck[0].upgrade, 0) << static_cast<int>(id);
        EXPECT_EQ(misc.counter, 0) << static_cast<int>(id);
    }
}

// The marker rows: no hook bindings at all, and no combat effect through ANY
// hook. Their correctness is entirely their tier, their pool slot and (for the
// boss tier) their relicRng draw.
TEST(RelicBossSpecial, MarkerRowsCarryNoCombatHooks) {
    const RelicId markers[] = {
        // Boss: energyMaster-only, run-layer, or observation-layer.
        RelicId::FUSION_HAMMER,  RelicId::RUNIC_DOME,   RelicId::SLAVERS_COLLAR,
        RelicId::PANDORAS_BOX,   RelicId::CURSED_KEY,   RelicId::BUSTED_CROWN,
        RelicId::ECTOPLASM,      RelicId::TINY_HOUSE,   RelicId::SOZU,
        RelicId::ASTROLABE,      RelicId::BLACK_STAR,   RelicId::SACRED_BARK,
        RelicId::EMPTY_CAGE,     RelicId::RUNIC_PYRAMID, RelicId::CALLING_BELL,
        RelicId::COFFEE_DRIPPER,
        // Special: run-layer or pure flavour.
        RelicId::GOLDEN_IDOL,    RelicId::SPIRIT_POOP,  RelicId::CULTIST_MASK,
        RelicId::NLOTHS_MASK,    RelicId::SSSERPENT_HEAD,
    };
    for (RelicId id : markers) {
        const sts::registry::RelicDef* d = relic_def(id);
        ASSERT_NE(d, nullptr) << static_cast<int>(id);
        EXPECT_EQ(d->hook_count, 0) << "relic " << static_cast<int>(id);
        EXPECT_FALSE(d->native) << "relic " << static_cast<int>(id);

        CombatState s = MakeState();
        const RelicView rv = give(s, id);
        for (int h = 0; h < kRelicHookCount; ++h) {
            dispatch_relic_hook(s, rv.relics, rv.count,
                                static_cast<RelicHook>(h), RelicHookContext{});
        }
        EXPECT_EQ(s.action_count, 0) << "relic " << static_cast<int>(id);
        EXPECT_EQ(s.player_hp, 70) << "relic " << static_cast<int>(id);
    }
}

// Runic Dome is the batch's purest deliberate no-op: its entire non-energy
// effect (hiding enemy intents) is an OBSERVATION concern that changes no
// simulated outcome. Its row must still be exact, because it holds a boss pool
// slot and therefore a relicRng position.
TEST(RelicBossSpecial, RunicDomeOccupiesItsPoolSlotAndDoesNothingElse) {
    const sts::registry::RelicDef* d = relic_def(RelicId::RUNIC_DOME);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->tier, sts::registry::RelicTier::BOSS);
    EXPECT_EQ(d->pool_order, 2);
    EXPECT_EQ(d->initial_counter, -1);
    // No canSpawn gate: it spawns under a context hostile to every S1 gate.
    RelicSpawnContext hostile{};
    hostile.floor = 99;
    hostile.in_shop = true;
    hostile.act = 4;
    hostile.has_burning_blood = false;
    EXPECT_TRUE(relic_can_spawn(RelicId::RUNIC_DOME, hostile));

    RunState rs{};
    rs.hp = 50;
    rs.max_hp = 80;
    rs.gold = 10;
    RngStream misc = from_seed(1);
    ASSERT_EQ(acquire_relic(rs, misc, RelicId::RUNIC_DOME),
              RelicAcquireResult::ACQUIRED);
    EXPECT_EQ(rs.hp, 50);
    EXPECT_EQ(rs.gold, 10);
    EXPECT_EQ(misc.counter, 0);
}

// ============================================================================
// Acquisition-order dispatch holds for the new tiers too (stage-a trap 8).
// ============================================================================

TEST(RelicBossSpecial, BattleStartRelicsFireInAcquisitionOrder) {
    CombatState a = MakeState();
    Relics ra;
    ra.add(RelicId::GREMLIN_MASK);
    ra.add(RelicId::MARK_OF_PAIN);
    const RelicView va = install(a, ra);
    dispatch_relics_at_battle_start(a, va.relics, va.count);
    ASSERT_EQ(a.action_count, 2);
    EXPECT_EQ(queued(a, 0).opcode, kOp(Opcode::APPLY_POWER));
    EXPECT_EQ(queued(a, 1).opcode, kOp(Opcode::MAKE_CARD));

    CombatState b = MakeState();
    Relics rb;
    rb.add(RelicId::MARK_OF_PAIN);
    rb.add(RelicId::GREMLIN_MASK);
    const RelicView vb = install(b, rb);
    dispatch_relics_at_battle_start(b, vb.relics, vb.count);
    ASSERT_EQ(b.action_count, 2);
    EXPECT_EQ(queued(b, 0).opcode, kOp(Opcode::MAKE_CARD));
    EXPECT_EQ(queued(b, 1).opcode, kOp(Opcode::APPLY_POWER));
}

}  // namespace
}  // namespace sts::engine
