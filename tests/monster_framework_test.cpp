// S2.2F -- the shared Act-2/3 monster framework, one directed case per surface.
//
// Every surface here lands with NO producer: the monster batches that consume
// them (S2.23-S2.28) come later, and the whole point of the task is that they do
// not each invent their own. So each case drives its surface through a
// SYNTHETIC fixture -- a hand-built CombatState, a directly-executed opcode, a
// hand-set flag -- rather than through content. That is deliberate: a framework
// surface whose only test is a future monster is a surface nobody has run.
//
// Provenance for the behaviour pinned below:
//   AbstractCreature.isDeadOrEscaped        :780-790
//   MonsterGroup.areMonstersBasicallyDead   :90-95
//   MonsterGroup.queueMonsters              :117-122
//   GameActionManager (turn gate / duringTurn) :310, :322-323
//   AbstractMonster.die / SuicideAction     :925-937 / SuicideAction.java:17-36
//   Darkling.die (the veto)                 :239-243
//   AwakenedOne.die (the veto)              :356-375
//   Reptomancer.die (post-super)            :157-165
//   SpawnMonsterAction.getSmartPosition     :50-56
//   SummonGremlinAction.update (pre-battle) :isDone arm
//   UseCardAction.update (onAfterUseCard)   :79-88
//   AbstractPlayer.damage (onInflictDamage) :1449-1453
//   AddCardToDeckAction / ShowCardAndObtainEffect :83-88 / :30-45
//   GameActionManager.callEndTurnEarlySequence    :379-392
//   OrbWalker (CONSTRUCTOR_BEFORE_HP)       :53-58

#include <cstdint>
#include <cstring>  // memcmp (the unbound-hook byte comparison)
#include <span>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/schema.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

// A minimal live combat: one monster at `hp`, player healthy, nothing queued.
CombatState make_one_monster(int16_t hp = 20) {
    CombatState s{};
    s.player_hp = 60;
    s.player_max_hp = 80;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = hp;
    s.monsters[0].max_hp = 44;
    return s;
}

ActionQueueItem op(Opcode o) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(o);
    return it;
}

// =============================================================================
// The schema bump itself
// =============================================================================

TEST(MonsterFramework, SchemaVersionAndCapAreTheOnesTheLayoutWasProvedAt) {
    // The version and the cap travel together: the fixtures were regenerated at
    // v7 == (cap 23), so a cap change without a version change is the
    // stop-the-line case schema.hpp's version log describes. Later bumps that
    // leave CombatState alone ride on top -- v8 (S2.47) grew only RunState, so
    // the cap stays proved at its v7 layout and this pin follows the constant.
    EXPECT_EQ(SCHEMA_VERSION, 8u);
    EXPECT_EQ(kMonsterCap, 23);
    // 23 is the largest cap the 8192 B ceiling admits -- 24 measures 8304.
    // Pinned as an inequality on the NEXT slot so the reason survives, not just
    // the number.
    EXPECT_LE(sizeof(CombatState), 8192u);
    EXPECT_GT(sizeof(CombatState) + sizeof(MonsterState), 8192u)
        << "kMonsterCap could be raised again without moving the ceiling -- "
           "re-derive the choice recorded in combat_state.hpp";
}

TEST(MonsterFramework, DrawXAndTheObtainAccumulatorCostNoBytes) {
    // Both new fields took over declared padding, which is the whole reason the
    // v7 bump is one array extension rather than three layout changes.
    EXPECT_EQ(sizeof(MonsterState), 20u + 8u * kPowerCap);
    EXPECT_EQ(offsetof(MonsterState, powers), 20u);
}

// =============================================================================
// kMonsterCap: growth past the old 7-record bound
// =============================================================================

TEST(MonsterFramework, RecordsGrowPastTheOldSevenRecordBound) {
    // The old cap was sized for the fully-split Slime Boss (7 records). Three
    // Act-2/3 spawners exceed it, so the framework must actually hold more --
    // this drives the record array past 7 through the real spawn path.
    CombatState s = make_one_monster();
    s.monsters[0].draw_x = 0;
    for (int i = 0; i < 12; ++i) {
        const uint8_t slot = smart_position_for(s, static_cast<int16_t>(-1));
        spawn_monster_at_slot(s, slot, MonsterId::SPIKE_SLIME_MEDIUM, 10);
    }
    EXPECT_EQ(s.monster_count, 13);
    EXPECT_GT(s.monster_count, 7)
        << "a cap of 7 would have hard-asserted before here";
}

// =============================================================================
// The liveness split
// =============================================================================

TEST(MonsterFramework, HalfDeadIsDeadForTargetingAndAliveForTheFight) {
    // The two Java predicates DISAGREE on halfDead, and that disagreement is the
    // entire mechanism behind REBIRTH / REINCARNATE.
    MonsterState m{};
    m.hp = 0;
    m.flags = kMonsterFlagHalfDead;

    EXPECT_TRUE(monster_dead_or_escaped(m))   // isDeadOrEscaped :780-790
        << "a half-dead monster must not be targetable";
    EXPECT_FALSE(monster_basically_dead(m))   // areMonstersBasicallyDead :90-95
        << "a half-dead monster is still IN the fight -- this is what lets it "
           "reach the turn that revives it";

    // A plain corpse is dead under both; an escaped monster likewise.
    MonsterState dead{};
    dead.hp = 0;
    EXPECT_TRUE(monster_dead_or_escaped(dead));
    EXPECT_TRUE(monster_basically_dead(dead));

    MonsterState fled{};
    fled.hp = 5;
    fled.flags = kMonsterFlagEscaped;
    EXPECT_TRUE(monster_dead_or_escaped(fled));
    EXPECT_TRUE(monster_basically_dead(fled));
}

TEST(MonsterFramework, HalfDeadMonsterKeepsTheCombatOpenAndTakesItsTurn) {
    // The classified B-side sites, driven through the PUMP rather than called
    // directly (queue_monsters and apply_pre_turn_logic are file-local to
    // action_queue.cpp): a half-dead monster is the only thing left, and every
    // one of those sites must treat it as still in the fight.
    CombatState s = make_one_monster();
    s.monsters[0].hp = 0;
    s.monsters[0].flags |= kMonsterFlagHalfDead;
    s.monsters[0].block = 12;
    s.phase = static_cast<uint8_t>(CombatPhase::RESOLVING);
    s.turn_has_ended = 1;

    bool queued = false;
    bool ran_monster = false;
    bool started_turn = false;
    for (int i = 0; i < 12; ++i) {
        const PumpStepResult r = pump_step(s, dispatch_monster_turn);
        ASSERT_NE(r.outcome, PumpOutcome::COMBAT_OVER)
            << "step " << i << ": a half-dead monster keeps the fight open. "
               "Under the TARGETING predicate the combat would end here and the "
               "revival turn could never happen";
        if (r.outcome == PumpOutcome::QUEUED_MONSTERS) {
            queued = true;
            EXPECT_EQ(s.monster_queue_count, 1)
                << "queueMonsters' `&& !halfDead` term -- it IS queued";
        }
        if (r.outcome == PumpOutcome::RAN_MONSTER) { ran_monster = true; }
        if (r.outcome == PumpOutcome::STARTED_TURN) { started_turn = true; }
        if (r.outcome == PumpOutcome::WAITING_ON_USER) { break; }
    }
    EXPECT_TRUE(queued) << "step 4 must enqueue the half-dead monster";
    EXPECT_TRUE(ran_monster)
        << "step 5's `!isDeadOrEscaped() || halfDead` gate -- this is the turn "
           "on which the Darkling reincarnates and the Awakened One is reborn";
    EXPECT_TRUE(started_turn);
}

TEST(MonsterFramework, HalfDeadMonsterStillLosesBlockAtPreTurn) {
    // applyPreTurnLogic (MonsterGroup.java:98-105) skips only isDying/isEscaping
    // -- no halfDead term -- so a half-dead monster DOES lose its block and DOES
    // run its start-of-turn powers. It is reached through the queued
    // kOpcodeMonsterStartTurn marker rather than by calling the file-local
    // helper.
    CombatState s = make_one_monster();
    s.monsters[0].hp = 0;
    s.monsters[0].flags |= kMonsterFlagHalfDead;
    s.monsters[0].block = 12;
    s.phase = static_cast<uint8_t>(CombatPhase::RESOLVING);

    ActionQueueItem marker{};
    marker.opcode = kOpcodeMonsterStartTurn;
    add_to_bottom(s, marker);
    const PumpStepResult r = pump_step(s, dispatch_monster_turn);

    EXPECT_EQ(r.outcome, PumpOutcome::RAN_ACTION);
    EXPECT_EQ(s.monsters[0].block, 0)
        << "under the targeting predicate the half-dead record would have been "
           "skipped and kept its block";
}

TEST(MonsterFramework, FeedAndGreedPayNothingForAHalfDeadTarget) {
    // Both opcodes' Java gates carry `|| halfDead`; both terms were documented
    // as structurally inert until this task and are now live. Driven through
    // execute_opcode -- the op_* bodies live in an interp-internal header.
    CombatState s = make_one_monster(4);
    s.monsters[0].flags |= kMonsterFlagHalfDead;
    const int16_t before_max = s.player_max_hp;
    ActionQueueItem feed = op(Opcode::DAMAGE_FEED);
    feed.src = kActorPlayer;
    feed.tgt = 0;
    feed.amount = 10;
    feed.flags = 3;  // the max HP a real kill would grant
    execute_opcode(s, feed);
    EXPECT_EQ(s.monsters[0].hp, 0);
    EXPECT_EQ(s.player_max_hp, before_max)
        << "Feed grants no max HP when the hit only half-killed";

    CombatState g = make_one_monster(4);
    g.monsters[0].flags |= kMonsterFlagHalfDead;
    ActionQueueItem greed = op(Opcode::DAMAGE_GREED);
    greed.src = kActorPlayer;
    greed.tgt = 0;
    greed.amount = 10;
    greed.flags = make_damage_greed_flags(20);
    execute_opcode(g, greed);
    EXPECT_EQ(g.monsters[0].hp, 0);
    EXPECT_EQ(g.combat_gold, 0)
        << "Hand of Greed pays nothing when nothing died";
}

// =============================================================================
// Death edges: the veto and the post-super phase
// =============================================================================

TEST(MonsterFramework, DieVetoSuppressesTheDeathFanOutsOnBothEdges) {
    // No landed monster vetoes (the Mugger returns false), so the veto is driven
    // through the dispatcher's own contract: with no die() body the answer must
    // be "no veto", and the fan-outs must run.
    CombatState s = make_one_monster(1);
    EXPECT_FALSE(dispatch_monster_die(s, 0))
        << "a monster with no die() body never vetoes";

    // Out-of-range is a safe non-veto rather than a crash.
    EXPECT_FALSE(dispatch_monster_die(s, kMonsterCap));
    dispatch_monster_die_after(s, kMonsterCap);  // must not fault
}

TEST(MonsterFramework, SuicideRunsTheDeathEdgeOnlyWithTheTriggerRelicsBit) {
    // SuicideAction's 1-arg ctor defaults relicTrigger to TRUE; the slime split
    // passes false. Both arms are now real. The observable difference used here
    // is the die() body's SEEDED aiRng draw -- the Mugger is the one monster
    // whose death moves a stream.
    CombatState off = make_one_monster(10);
    off.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::MUGGER);
    ActionQueueItem a = op(Opcode::SUICIDE);
    a.tgt = 0;
    a.flags = 0;  // die(false)
    const int32_t before_off = off.ai_rng.counter;
    execute_opcode(off, a);
    EXPECT_EQ(off.monsters[0].hp, 0);
    EXPECT_EQ(off.ai_rng.counter, before_off)
        << "die(false) skips the whole death edge, the die() body included";

    CombatState on = make_one_monster(10);
    on.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::MUGGER);
    ActionQueueItem b = op(Opcode::SUICIDE);
    b.tgt = 0;
    b.flags = 1;  // die(true)
    const int32_t before_on = on.ai_rng.counter;
    execute_opcode(on, b);
    EXPECT_EQ(on.monsters[0].hp, 0);
    EXPECT_GT(on.ai_rng.counter, before_on)
        << "die(true) runs the death edge -- the Mugger's seeded playDeathSfx "
           "draw is the witness";
}

TEST(MonsterFramework, SuicideOnAnAlreadyDeadRecordDoesNotRefireTheEdge) {
    CombatState s = make_one_monster(0);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::MUGGER);
    ActionQueueItem a = op(Opcode::SUICIDE);
    a.tgt = 0;
    a.flags = 1;
    const int32_t before = s.ai_rng.counter;
    execute_opcode(s, a);
    EXPECT_EQ(s.ai_rng.counter, before)
        << "a corpse does not die twice";
}

// =============================================================================
// Hooks 15-17: the dispatch sites exist and are no-ops without a binder
// =============================================================================

TEST(MonsterFramework, HookCountCoversTheThreeNewHooks) {
    EXPECT_EQ(kHookCount, 18);
    EXPECT_EQ(static_cast<int>(Hook::DURING_TURN), 15);
    EXPECT_EQ(static_cast<int>(Hook::ON_AFTER_USE_CARD), 16);
    EXPECT_EQ(static_cast<int>(Hook::ON_INFLICT_DAMAGE), 17);
}

TEST(MonsterFramework, TheThreeNewHooksAreNoOpsUntilSomethingBindsThem) {
    // The regression that matters for this commit: the dispatch sites are live
    // in the pump, the card-play path and the damage path, and every landed
    // fixture must be byte-identical because nothing binds them yet.
    CombatState s = make_one_monster();
    const CombatState before = s;

    dispatch_during_turn(s, 0);
    dispatch_on_after_use_card(s, 0, 0);
    dispatch_on_inflict_damage(s, 0, kActorPlayer, 5);

    EXPECT_EQ(std::memcmp(&s, &before, sizeof(CombatState)), 0)
        << "an unbound hook must not touch the state";
}

TEST(MonsterFramework, OnInflictDamageIsGatedLikeItsJavaCallSite) {
    // The Java call sits inside `if (damageAmount > 0)` and walks
    // `info.owner.powers`, which a null owner cannot supply. Both guards are
    // observable through the bounds: neither may fault.
    CombatState s = make_one_monster();
    dispatch_on_inflict_damage(s, 0, kActorPlayer, 0);            // amount gate
    dispatch_on_inflict_damage(s, 0, kActorPlayer, 5, 0, true);   // null source
    dispatch_during_turn(s, kMonsterCap);                          // bounds
    SUCCEED();
}

// =============================================================================
// Opcodes 68-70
// =============================================================================

TEST(MonsterFramework, ObtainCardAccumulatesAndSaturates) {
    CombatState s = make_one_monster();
    ActionQueueItem a = op(Opcode::OBTAIN_CARD);
    a.flags = static_cast<uint32_t>(CardId::STRIKE);
    execute_opcode(s, a);
    ASSERT_EQ(s.pending_obtain_count, 1);
    EXPECT_EQ(s.pending_obtain[0], static_cast<uint16_t>(CardId::STRIKE));

    // Saturation rather than overflow: dropping beats corrupting the struct.
    for (int i = 0; i < 10; ++i) {
        execute_opcode(s, a);
    }
    EXPECT_EQ(s.pending_obtain_count, kPendingObtainCap);
}

TEST(MonsterFramework, ClearCardQueueDropsPendingPlaysAndSparesTheCardInUse) {
    // The documented trap: the Java walks `player.limbo` (the autoplay group),
    // which this engine does NOT model as its limbo pile -- its limbo is
    // cardInUse, the card currently resolving. Clearing must not touch it.
    CombatState s = make_one_monster();
    s.card_pool[0].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.limbo[0] = 0;
    s.limbo_count = 1;
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    ASSERT_EQ(s.card_queue_count, 1);

    execute_opcode(s, op(Opcode::CLEAR_CARD_QUEUE));
    EXPECT_EQ(s.card_queue_count, 0);
    EXPECT_EQ(s.limbo_count, 1)
        << "the card being played must survive -- porting the Java's limbo loop "
           "literally would exhaust it";
}

TEST(MonsterFramework, EndPlayerTurnQueuesTheSentinelAndClearsPendingPlays) {
    CombatState s = make_one_monster();
    s.card_pool[0].card_id = static_cast<uint16_t>(CardId::STRIKE);
    CardQueueItem play{};
    play.card_index = 0;
    add_card_to_queue_bottom(s, play);
    ASSERT_EQ(s.card_queue_count, 1);

    execute_opcode(s, op(Opcode::END_PLAYER_TURN));
    ASSERT_EQ(s.card_queue_count, 1)
        << "the pending play is dropped and replaced by the sentinel";
    EXPECT_TRUE(is_end_turn_sentinel(s.card_queue[0]))
        << "callEndTurnEarlySequence's step 4 -- and until now only the "
           "player's own END_TURN verb could produce this";
}

TEST(MonsterFramework, EndPlayerTurnMidQueueActuallyEndsTheTurn) {
    // Driven through the pump rather than asserted on the queue: the sentinel
    // must reach turn_has_ended.
    CombatState s = make_one_monster();
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    execute_opcode(s, op(Opcode::END_PLAYER_TURN));
    for (int i = 0; i < 8 && s.turn_has_ended == 0; ++i) {
        pump_step(s, dispatch_monster_turn);
    }
    EXPECT_EQ(s.turn_has_ended, 1);
}

// =============================================================================
// Spawn conventions
// =============================================================================

TEST(MonsterFramework, SmartPositionReproducesTheJavaBreakNotACount) {
    // getSmartPosition BREAKS at the first record that fails `m.drawX > mo.drawX`
    // rather than counting matches across the list. The two differ whenever the
    // list is not sorted by drawX, which initial groups need not be.
    CombatState s{};
    s.monster_count = 3;
    s.monsters[0].draw_x = -300;
    s.monsters[1].draw_x = 200;   // out of order on purpose
    s.monsters[2].draw_x = -100;

    // A newcomer at -400 is left of everything: stops immediately.
    EXPECT_EQ(smart_position_for(s, -400), 0);
    // At 0: passes -300, stops at 200 -- a COUNT would have said 2 (also
    // counting -100 behind it), the break says 1.
    EXPECT_EQ(smart_position_for(s, 0), 1);
    // Equal x is STRICT: it stops there and inserts BEFORE the equal record.
    EXPECT_EQ(smart_position_for(s, -300), 0);
}

TEST(MonsterFramework, SmartPositionCountsDeadRecordsToo) {
    // The Java walks the whole group list; this engine retains dead records in
    // place precisely so that stays true.
    CombatState s{};
    s.monster_count = 2;
    s.monsters[0].draw_x = -300;
    s.monsters[0].hp = 0;         // a corpse still occupies its position
    s.monsters[1].draw_x = 200;
    EXPECT_EQ(smart_position_for(s, 0), 1);
}

TEST(MonsterFramework, SpawnRemapsTheMonsterQueueButNotTheActionQueue) {
    // THE DISAGREEMENT THE TASK WAS ASKED TO SETTLE, pinned in both directions.
    // Two scouts appeared to contradict each other about whether a spawn remaps
    // pending queue indices. Reading the code settles it: they were describing
    // DIFFERENT queues and both were right.
    CombatState s = make_one_monster();
    s.monster_count = 2;
    s.monsters[1] = s.monsters[0];

    // A pending monster TURN for the record currently at slot 1.
    s.monster_queue[0].monster_index = 1;
    s.monster_queue_count = 1;

    // A pending ACTION aimed at the same record.
    ActionQueueItem pending = op(Opcode::DAMAGE);
    pending.tgt = 1;
    add_to_bottom(s, pending);

    spawn_monster_at_slot(s, 0, MonsterId::SPIKE_SLIME_MEDIUM, 10);

    EXPECT_EQ(s.monster_queue[0].monster_index, 2)
        << "monster_queue indices >= slot ARE remapped -- the Java's queue holds "
           "object references, immune to the list insert";
    EXPECT_EQ(s.action_queue[s.action_head].tgt, 1)
        << "pending action_queue items are NOT remapped; a producer must "
           "pre-compute its post-insertion slot (monster_slime_large.cpp)";
}

TEST(MonsterFramework, SpawnRunsPreBattleOnlyWhenAsked) {
    // SpawnMonsterAction does NOT run usePreBattleAction; SummonGremlinAction
    // DOES, at isDone -- which is how a summoned Gremlin Warrior gets its Angry
    // power and a Bronze Orb does not. Hence a parameter, not a policy.
    //
    // RESIDUE, named rather than faked: no monster is BOTH mid-combat spawnable
    // and pre-battle-bearing today. monster_spawn_at_hp_fn covers only the four
    // split slimes, and none of them has a pre-battle body; every monster that
    // does have one is an initial-group member. The first monster to be both is
    // S2.23's summoned gremlin. So what is driven here is the wiring -- the arm
    // runs, dispatches through monster_pre_battle_fn, and is inert for an id
    // with no body -- plus the two halves of the precondition, which is what a
    // future producer will rely on.
    ASSERT_EQ(monster_pre_battle_fn(MonsterId::SPIKE_SLIME_MEDIUM), nullptr)
        << "if a split slime ever gains a pre-battle body, this test starts "
           "measuring something and should assert the stream instead";
    ASSERT_NE(monster_pre_battle_fn(MonsterId::LOUSE_NORMAL), nullptr)
        << "the Louse's Curl Up roll is the pre-battle body a spawnable monster "
           "will eventually look like";

    CombatState off = make_one_monster();
    spawn_monster_at_slot(off, 1, MonsterId::SPIKE_SLIME_MEDIUM, 12,
                          /*run_pre_battle=*/false);
    CombatState on = make_one_monster();
    spawn_monster_at_slot(on, 1, MonsterId::SPIKE_SLIME_MEDIUM, 12,
                          /*run_pre_battle=*/true);

    // Inert for an id with no pre-battle body -- so the bit is safe to set
    // unconditionally by a producer that does not know.
    EXPECT_EQ(off.monster_count, on.monster_count);
    EXPECT_EQ(off.monster_hp_rng.counter, on.monster_hp_rng.counter);
    EXPECT_EQ(std::memcmp(&off, &on, sizeof(CombatState)), 0);
}

TEST(MonsterFramework, SpawnPreBattleBitReachesTheOpcode) {
    // The flags-bit encoding, pinned so a producer authoring the item gets the
    // same behaviour the direct call gives.
    CombatState s = make_one_monster();
    ActionQueueItem spawn = op(Opcode::SPAWN_MONSTER);
    spawn.tgt = 1;
    spawn.amount = 12;
    spawn.flags = static_cast<uint32_t>(MonsterId::SPIKE_SLIME_MEDIUM) |
                  kSpawnRunPreBattle;
    execute_opcode(s, spawn);
    EXPECT_EQ(s.monster_count, 2);
    EXPECT_EQ(s.monsters[1].monster_id,
              static_cast<uint16_t>(MonsterId::SPIKE_SLIME_MEDIUM));
    // The MonsterId occupies the low 16 bits, so the bit must not corrupt it.
    EXPECT_EQ(spawn.flags & 0xFFFFu,
              static_cast<uint32_t>(MonsterId::SPIKE_SLIME_MEDIUM));
}

TEST(MonsterFramework, ConstructorBeforeHpBurnsAheadOfTheSetHpDraw) {
    // A BEFORE roll sits in the super(...) argument list, which Java evaluates
    // before the constructor body -- so it must burn ahead of setHp, not after.
    // No registry row declares one yet, so what is pinned here is the ENUMERATOR
    // and the ordering contract the two-pass walk implements; the first row to
    // use it (Orb Walker) inherits a correct baseline.
    EXPECT_EQ(static_cast<int>(sts::registry::MonsterRollTiming::CONSTRUCTOR_BEFORE_HP),
              2);
    EXPECT_EQ(static_cast<int>(sts::registry::MonsterRollTiming::CONSTRUCTOR_AFTER_HP),
              0);
    EXPECT_EQ(static_cast<int>(sts::registry::MonsterRollTiming::PRE_BATTLE), 1);

    // The burn still spends exactly the setHp draw for a monster with no extra
    // ctor rolls, which is the property the two-pass restructure had to preserve.
    CombatState s{};
    const int32_t before = s.monster_hp_rng.counter;
    burn_unspawned_ctor_rolls(s, MonsterId::JAW_WORM);
    EXPECT_EQ(s.monster_hp_rng.counter - before, 1);
}

}  // namespace
}  // namespace sts::engine
