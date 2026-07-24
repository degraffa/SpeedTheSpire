// Tier-2 large slime + split framework coverage (B3.17).
//
// Two independent 32-seed x 20-turn fixtures pin move selection, full aiRng
// state, and logical draw counters (incl. Acid L's 0.6f/0.4f nextFloat
// tiebreaks). Focused tests pin the exact split threshold, the child records'
// HP/slots, the queue state after a split (incl. Spike L's posthumous
// RollMoveAction on the dead parent), both interrupt timings (player turn and
// the monster's own turn), and the cannotLose termination window.
//
// Provenance: monster_slime_large.hpp header block (AcidSlime_L.java /
// SpikeSlime_L.java / SlimeBoss.java / SpawnMonsterAction.java /
// SuicideAction.java / CannotLoseAction.java / CanLoseAction.java /
// RollMoveAction.java / SetMoveAction.java / SplitPower.java, all read in full).

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_slime.hpp"
#include "sts/engine/monster_slime_large.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/monster_table.hpp"

#ifndef STS_FIXTURE_DIR
#error "STS_FIXTURE_DIR must be defined by tests/CMakeLists.txt"
#endif

namespace sts::engine {
namespace {

constexpr int kTurns = 20;
constexpr uint8_t kSplitMove = 3;
constexpr uint8_t kIntentUnknown = 7;  // MonsterIntent::UNKNOWN (pinned)

struct TurnRow {
    uint8_t move = 0;
    uint64_t ai_s0 = 0;
    uint64_t ai_s1 = 0;
    int32_t ai_counter = 0;
};

struct SeedCase {
    std::string label;
    int64_t seed = 0;
    int hp = 0;
    uint64_t hp_s0 = 0;
    uint64_t hp_s1 = 0;
    int32_t hp_counter = 0;
    std::vector<TurnRow> turns;
};

std::vector<SeedCase> load_fixture(const std::string& name) {
    std::ifstream in(std::string(STS_FIXTURE_DIR) + "/" + name);
    std::vector<SeedCase> out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string kind;
        std::getline(ss, kind, '\t');
        auto col = [&]() {
            std::string value;
            std::getline(ss, value, '\t');
            return value;
        };
        if (kind == "seed") {
            SeedCase c;
            c.label = col();
            c.seed = std::stoll(col());
            c.hp = std::stoi(col());
            c.hp_s0 = std::stoull(col());
            c.hp_s1 = std::stoull(col());
            c.hp_counter = std::stoi(col());
            out.push_back(std::move(c));
        } else if (kind == "turn") {
            (void)col();  // one-based turn number
            TurnRow row;
            row.move = static_cast<uint8_t>(std::stoi(col()));
            row.ai_s0 = std::stoull(col());
            row.ai_s1 = std::stoull(col());
            row.ai_counter = std::stoi(col());
            out.back().turns.push_back(row);
        }
    }
    return out;
}

CombatState make_state(int64_t seed) {
    CombatState s{};
    s.player_hp = 999;  // survives 20 turns of tackles in the fixture battery
    s.player_max_hp = 999;
    s.monster_count = 1;
    s.monster_hp_rng = from_seed(seed);
    s.ai_rng = from_seed(seed);
    return s;
}

// Direct queue drain (no pump gate) -- the slime_test harness pattern. The
// large slimes' queued ROLL_MOVE items roll HERE, at dequeue time.
void drain(CombatState& s) {
    while (s.action_count > 0) {
        const ActionQueueItem item = s.action_queue[s.action_head];
        s.action_head = static_cast<uint8_t>((s.action_head + 1) % kActionQueueCap);
        --s.action_count;
        execute_opcode(s, item);
    }
}

ActionQueueItem damage_item(uint8_t tgt, int32_t amount,
                            DamageType type = DamageType::NORMAL) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    it.src = kActorPlayer;
    it.tgt = tgt;
    it.amount = amount;
    it.flags = make_damage_flags(type);
    return it;
}

const PowerSlot* player_power(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.player_powers[i];
        }
    }
    return nullptr;
}

using InitFn = void (*)(CombatState&, uint8_t);
using TurnFn = void (*)(CombatState&, uint8_t);

void run_large_fixture(const std::string& file, MonsterId id, InitFn init,
                       TurnFn take_turn, int hp_min, int hp_max,
                       bool expect_tiebreak_draw) {
    const std::vector<SeedCase> cases = load_fixture(file);
    ASSERT_EQ(cases.size(), 32u) << file;
    bool saw_extra_ai_draw = false;
    for (const SeedCase& c : cases) {
        ASSERT_EQ(c.turns.size(), static_cast<size_t>(kTurns)) << c.label;
        CombatState s = make_state(c.seed);
        init(s, 0);
        EXPECT_EQ(s.monsters[0].monster_id, static_cast<uint16_t>(id));
        EXPECT_EQ(s.monsters[0].hp, c.hp) << c.label;
        EXPECT_EQ(s.monsters[0].max_hp, c.hp) << c.label;
        EXPECT_GE(c.hp, hp_min);
        EXPECT_LE(c.hp, hp_max);
        EXPECT_EQ(s.monster_hp_rng.s0, c.hp_s0) << c.label;
        EXPECT_EQ(s.monster_hp_rng.s1, c.hp_s1) << c.label;
        EXPECT_EQ(s.monster_hp_rng.counter, c.hp_counter) << c.label;
        EXPECT_EQ(s.ai_rng.counter, 1) << c.label
            << ": init() is exactly one random(99); no tiebreak on empty history";
        // ctor adds SplitPower (amount -1) -- SplitPower.java:19-26.
        ASSERT_EQ(s.monsters[0].power_count, 1) << c.label;
        EXPECT_EQ(s.monsters[0].powers[0].power_id,
                  static_cast<uint16_t>(PowerId::SPLIT));
        EXPECT_EQ(s.monsters[0].powers[0].amount, -1);

        int32_t prior_counter = s.ai_rng.counter;
        for (size_t k = 0; k < c.turns.size(); ++k) {
            const TurnRow& row = c.turns[k];
            EXPECT_EQ(s.monsters[0].move_history[0], row.move)
                << c.label << " turn " << (k + 1);
            take_turn(s, 0);
            // The next-move roll is a QUEUED RollMoveAction (unlike B3.14's
            // inline rolls), so the aiRng advances during the drain.
            drain(s);
            EXPECT_EQ(s.ai_rng.s0, row.ai_s0) << c.label << " turn " << (k + 1);
            EXPECT_EQ(s.ai_rng.s1, row.ai_s1) << c.label << " turn " << (k + 1);
            EXPECT_EQ(s.ai_rng.counter, row.ai_counter)
                << c.label << " turn " << (k + 1);
            if (row.ai_counter - prior_counter > 1) saw_extra_ai_draw = true;
            prior_counter = row.ai_counter;
        }
    }
    EXPECT_EQ(saw_extra_ai_draw, expect_tiebreak_draw)
        << "Acid L must exercise a nextFloat tiebreak across the battery; "
           "Spike L must never draw one";
}

// --- Fixtures ----------------------------------------------------------------

TEST(LargeSlimeFixture, SpikeLargeMatchesIndependentOracle) {
    run_large_fixture("slime_spike_large_fixture.tsv",
                      MonsterId::SPIKE_SLIME_LARGE, &spike_slime_large_init,
                      &spike_slime_large_take_turn, 67, 73,
                      /*expect_tiebreak_draw=*/false);
}

TEST(LargeSlimeFixture, AcidLargeMatchesIndependentOracle) {
    run_large_fixture("slime_acid_large_fixture.tsv",
                      MonsterId::ACID_SLIME_LARGE, &acid_slime_large_init,
                      &acid_slime_large_take_turn, 68, 72,
                      /*expect_tiebreak_draw=*/true);
}

// --- Move effects (registry columns at the fixed A20 difficulty) -------------

TEST(LargeSlimeEffects, TacklesQueueDamageThenTwoSlimedThenRoll) {
    struct Case {
        InitFn init;
        TurnFn turn;
        uint8_t move;
        int damage;
        int slimed;
    };
    // A2 damage columns (SpikeSlime_L.java:83 / AcidSlime_L.java:88-89);
    // WOUND_COUNT 2 for both tackle-with-Slimed moves.
    const Case cases[] = {
        {&spike_slime_large_init, &spike_slime_large_take_turn, 1, 18, 2},
        {&acid_slime_large_init, &acid_slime_large_take_turn, 1, 12, 2},
        {&acid_slime_large_init, &acid_slime_large_take_turn, 2, 18, 0},
    };
    for (const Case& c : cases) {
        CombatState s = make_state(5001 + c.move + c.damage);
        c.init(s, 0);
        set_monster_move(s.monsters[0], c.move,
                         c.slimed > 0 ? MonsterIntent::ATTACK_DEBUFF
                                      : MonsterIntent::ATTACK);
        c.turn(s, 0);
        const int expected_items = c.slimed > 0 ? 3 : 2;  // +ROLL_MOVE tail
        ASSERT_EQ(s.action_count, expected_items);
        const uint8_t first = s.action_head;
        EXPECT_EQ(s.action_queue[first].opcode,
                  static_cast<uint16_t>(Opcode::DAMAGE));
        EXPECT_EQ(s.action_queue[first].amount, c.damage);
        if (c.slimed > 0) {
            const uint8_t second =
                static_cast<uint8_t>((first + 1) % kActionQueueCap);
            EXPECT_EQ(s.action_queue[second].opcode,
                      static_cast<uint16_t>(Opcode::MAKE_CARD));
            EXPECT_EQ(s.action_queue[second].amount, c.slimed);
            EXPECT_EQ(s.action_queue[second].src,
                      static_cast<uint8_t>(CardPile::DISCARD));
            EXPECT_EQ(make_card_id_from_flags(s.action_queue[second].flags),
                      static_cast<uint16_t>(CardId::SLIMED));
        }
        const uint8_t last = static_cast<uint8_t>(
            (first + expected_items - 1) % kActionQueueCap);
        EXPECT_EQ(s.action_queue[last].opcode,
                  static_cast<uint16_t>(Opcode::ROLL_MOVE));
        const int16_t hp_before = s.player_hp;
        drain(s);
        EXPECT_EQ(s.player_hp, hp_before - c.damage);
        ASSERT_EQ(s.discard_count, c.slimed);
        for (uint8_t i = 0; i < s.discard_count; ++i) {
            EXPECT_EQ(s.card_pool[s.discard[i]].card_id,
                      static_cast<uint16_t>(CardId::SLIMED));
        }
    }
}

TEST(LargeSlimeEffects, LicksApplyFrail3AndWeak2AtA20) {
    // Spike L A17+: FrailPower(player, 3, true) (SpikeSlime_L.java:101-103);
    // Acid L: WeakPower(player, 2, true) at every ascension (AcidSlime_L.java:109).
    CombatState spike = make_state(6001);
    spike_slime_large_init(spike, 0);
    set_monster_move(spike.monsters[0], 4, MonsterIntent::DEBUFF);
    spike_slime_large_take_turn(spike, 0);
    drain(spike);
    ASSERT_NE(player_power(spike, PowerId::FRAIL), nullptr);
    EXPECT_EQ(player_power(spike, PowerId::FRAIL)->amount, 3);
    EXPECT_NE(spike.flags & kCombatFlagFrailJustApplied, 0u);

    CombatState acid = make_state(6002);
    acid_slime_large_init(acid, 0);
    set_monster_move(acid.monsters[0], 4, MonsterIntent::DEBUFF);
    acid_slime_large_take_turn(acid, 0);
    drain(acid);
    ASSERT_NE(player_power(acid, PowerId::WEAK), nullptr);
    EXPECT_EQ(player_power(acid, PowerId::WEAK)->amount, 2);
}

// --- Split trigger threshold --------------------------------------------------

TEST(LargeSlimeSplit, TriggersAtExactHalfHpThreshold) {
    CombatState s = make_state(7001);
    acid_slime_large_init(s, 0);
    MonsterState& m = s.monsters[0];
    m.max_hp = 70;
    m.hp = 37;
    const uint8_t pre_move = m.move_history[0];

    // 37 -> 36: (float)36 <= 35.0f is false -- NO telegraph.
    execute_opcode(s, damage_item(0, 1));
    EXPECT_EQ(m.hp, 36);
    EXPECT_EQ(m.move_history[0], pre_move);
    EXPECT_EQ(m.flags & kMonsterFlagSplitTriggered, 0u);
    EXPECT_EQ(s.action_count, 0);

    // 36 -> 35: (float)35 <= 35.0f is true -- telegraph SPLIT.
    execute_opcode(s, damage_item(0, 1));
    EXPECT_EQ(m.hp, 35);
    EXPECT_EQ(m.move_history[0], kSplitMove) << "synchronous setMove(SPLIT)";
    EXPECT_EQ(m.intent, kIntentUnknown);
    EXPECT_NE(m.flags & kMonsterFlagSplitTriggered, 0u);
    ASSERT_EQ(s.action_count, 1) << "one queued SetMoveAction";
    EXPECT_EQ(s.action_queue[s.action_head].opcode,
              static_cast<uint16_t>(Opcode::SET_MOVE));
    EXPECT_EQ(s.action_queue[s.action_head].tgt, 0);
    EXPECT_EQ(s.action_queue[s.action_head].amount, kSplitMove);

    // The splitTriggered latch is one-shot: further damage re-queues nothing.
    execute_opcode(s, damage_item(0, 1));
    EXPECT_EQ(s.action_count, 1);

    // The queued SetMoveAction pushes a SECOND 3 onto the history (setMove
    // appends, AbstractMonster.java:431-437).
    drain(s);
    EXPECT_EQ(m.move_history[0], kSplitMove);
    EXPECT_EQ(m.move_history[1], kSplitMove);
}

TEST(LargeSlimeSplit, LethalHitDoesNotTelegraph) {
    // The Java guard requires !isDying: a hit that kills never telegraphs
    // (AcidSlime_L.java:145). THORNS/HP_LOSS types trigger like NORMAL ones
    // (the override wraps every damage() call) -- exercised via THORNS here.
    CombatState s = make_state(7002);
    spike_slime_large_init(s, 0);
    MonsterState& m = s.monsters[0];
    m.max_hp = 70;
    m.hp = 5;
    execute_opcode(s, damage_item(0, 9, DamageType::THORNS));
    EXPECT_EQ(m.hp, 0);
    EXPECT_EQ(m.flags & kMonsterFlagSplitTriggered, 0u);
    EXPECT_EQ(s.action_count, 0);

    // Control: the same THORNS hit that leaves it alive under half triggers.
    CombatState t = make_state(7003);
    spike_slime_large_init(t, 0);
    t.monsters[0].max_hp = 70;
    t.monsters[0].hp = 40;
    execute_opcode(t, damage_item(0, 9, DamageType::THORNS));
    EXPECT_EQ(t.monsters[0].hp, 31);
    EXPECT_EQ(t.monsters[0].move_history[0], kSplitMove);
    EXPECT_NE(t.monsters[0].flags & kMonsterFlagSplitTriggered, 0u);
}

// --- Interrupt during the monster's OWN turn ----------------------------------

TEST(LargeSlimeSplit, OwnTurnInterruptRollsThenReasserts) {
    // Thorns-style damage lands DURING the slime's own attack: the interrupt's
    // queued SetMoveAction goes to the queue BOTTOM, AFTER the turn's already-
    // queued RollMoveAction -- which still resolves (and draws aiRng) first,
    // then the SetMoveAction overrides the rolled move back to SPLIT
    // (AcidSlime_L.java:148-149 vs takeTurn's :118 RollMoveAction).
    CombatState s = make_state(8001);
    acid_slime_large_init(s, 0);
    MonsterState& m = s.monsters[0];
    m.max_hp = 70;
    m.hp = 36;
    set_monster_move(m, 1, MonsterIntent::ATTACK_DEBUFF);
    acid_slime_large_take_turn(s, 0);  // queues DAMAGE, MAKE_CARD, ROLL_MOVE
    ASSERT_EQ(s.action_count, 3);

    // Resolve the player-facing DamageAction, then the thorns reflection.
    ActionQueueItem head{};
    ASSERT_TRUE(pop_action_front(s, head));
    execute_opcode(s, head);  // DAMAGE -> player
    execute_opcode(s, damage_item(0, 1, DamageType::THORNS));  // 36 -> 35
    EXPECT_EQ(m.move_history[0], kSplitMove) << "synchronous interrupt";
    EXPECT_EQ(m.intent, kIntentUnknown);
    ASSERT_EQ(s.action_count, 3) << "MAKE_CARD, ROLL_MOVE, then SET_MOVE at bottom";

    const int32_t counter_before = s.ai_rng.counter;
    drain(s);
    // The wasted RollMoveAction drew at least the random(99).
    EXPECT_GT(s.ai_rng.counter, counter_before);
    // History: interrupt 3, then the rolled move, then the SetMoveAction's 3.
    EXPECT_EQ(m.move_history[0], kSplitMove);
    EXPECT_NE(m.move_history[1], kSplitMove) << "the interrupted roll landed";
    EXPECT_NE(m.move_history[1], 0);
    EXPECT_EQ(m.move_history[2], kSplitMove);
    EXPECT_EQ(m.intent, kIntentUnknown) << "SET_MOVE re-asserted the telegraph";
}

// --- The split turn itself -----------------------------------------------------

// Drive one monster turn through the real pump (dispatch_monster_turn) and
// return whether COMBAT_OVER was ever observed mid-sequence.
bool pump_monster_turn(CombatState& s) {
    s.monster_attacks_queued = 1;
    s.monster_queue[0] = MonsterQueueItem{0, 0};
    s.monster_queue_count = 1;
    s.turn_has_ended = 0;
    for (;;) {
        const PumpStepResult r = pump_step(s, dispatch_monster_turn);
        if (r.outcome == PumpOutcome::COMBAT_OVER) {
            return true;
        }
        if (r.outcome == PumpOutcome::WAITING_ON_USER) {
            return false;
        }
    }
}

TEST(LargeSlimeSplit, AcidSplitsIntoTwoMediumsAtCurrentHp) {
    CombatState s = make_state(9001);
    acid_slime_large_init(s, 0);
    MonsterState& m = s.monsters[0];
    m.hp = 30;  // parent's CURRENT hp -- the children's spawn HP
    set_monster_move(m, kSplitMove, MonsterIntent::UNKNOWN);
    m.flags = static_cast<uint16_t>(m.flags | kMonsterFlagSplitTriggered);

    // Hand-derive the children's init() rolls from a copy of the ai stream:
    // AcidSlime_M.getMove A17 on an empty history draws ONE random(99) --
    // num<40 -> WOUND_TACKLE(1), num<80 -> TACKLE(2), else -> LICK(4).
    RngStream ai_copy = s.ai_rng;
    uint8_t expected_child_move[2];
    for (auto& mv : expected_child_move) {
        const int32_t num = random(ai_copy, 99);
        mv = num < 40 ? uint8_t{1} : (num < 80 ? uint8_t{2} : uint8_t{4});
    }
    const int32_t ai_before = s.ai_rng.counter;

    EXPECT_FALSE(pump_monster_turn(s)) << "combat must not end mid-split";

    ASSERT_EQ(s.monster_count, 3);
    // Left child at the parent's old slot, parent dead-in-place at 1, right
    // child at 2 (SpawnMonsterAction smart positioning over the solo layout).
    EXPECT_EQ(s.monsters[0].monster_id,
              static_cast<uint16_t>(MonsterId::ACID_SLIME_MEDIUM));
    EXPECT_EQ(s.monsters[1].monster_id,
              static_cast<uint16_t>(MonsterId::ACID_SLIME_LARGE));
    EXPECT_EQ(s.monsters[2].monster_id,
              static_cast<uint16_t>(MonsterId::ACID_SLIME_MEDIUM));
    EXPECT_EQ(s.monsters[1].hp, 0) << "SuicideAction zeroed the parent";
    EXPECT_EQ(s.monsters[1].move_history[0], kSplitMove);
    for (const uint8_t child : {uint8_t{0}, uint8_t{2}}) {
        EXPECT_EQ(s.monsters[child].hp, 30) << "children HP = parent CURRENT hp";
        EXPECT_EQ(s.monsters[child].max_hp, 30)
            << "4-arg ctor: maxHealth = newHealth (AbstractMonster.java:139,150)";
        EXPECT_EQ(s.monsters[child].block, 0);
        EXPECT_EQ(s.monsters[child].power_count, 0)
            << "mediums have no SplitPower";
    }
    EXPECT_EQ(s.monsters[0].move_history[0], expected_child_move[0]);
    EXPECT_EQ(s.monsters[2].move_history[0], expected_child_move[1]);
    EXPECT_EQ(s.ai_rng.counter, ai_before + 2)
        << "exactly two child init() rolls; Acid L's split case queues NO "
           "RollMoveAction for the parent (AcidSlime_L.java:127-138)";
    EXPECT_EQ(s.flags & kCombatFlagCannotLose, 0u) << "CanLose cleared the latch";
    EXPECT_EQ(s.monster_hp_rng.counter, 1)
        << "no monster_hp_rng draw at split spawn (4-arg ctor)";

    // The children ARE the B3.14 mediums: same dispatch entries.
    EXPECT_EQ(monster_turn_fn(MonsterId::ACID_SLIME_MEDIUM),
              &acid_slime_medium_take_turn);

    // Termination: combat ends only when all descendants are dead.
    execute_opcode(s, damage_item(0, 99));
    EXPECT_FALSE(pump_monster_turn(s)) << "one child still alive";
    execute_opcode(s, damage_item(2, 99));
    CombatState t = s;
    EXPECT_EQ(pump_step(t, dispatch_monster_turn).outcome,
              PumpOutcome::COMBAT_OVER);
}

TEST(LargeSlimeSplit, SpikeSplitAddsPosthumousParentRoll) {
    CombatState s = make_state(9002);
    spike_slime_large_init(s, 0);
    MonsterState& m = s.monsters[0];
    m.hp = 21;
    m.block = 5;  // SuicideAction bypasses damage(): block survives
    set_monster_move(m, kSplitMove, MonsterIntent::UNKNOWN);
    m.flags = static_cast<uint16_t>(m.flags | kMonsterFlagSplitTriggered);

    // Expected draws, in order: child0 random(99), child1 random(99), then the
    // dead parent's trailing RollMoveAction random(99) (SpikeSlime_L.java:127;
    // RollMoveAction has no liveness check). SpikeSlime_M.getMove on an empty
    // history: num<30 -> FLAME_TACKLE(1), else FRAIL_LICK(4); the parent's
    // SpikeSlime_L.getMove with history [3,3,...]: num<30 -> 1, else 4.
    RngStream ai_copy = s.ai_rng;
    const uint8_t child0 = random(ai_copy, 99) < 30 ? uint8_t{1} : uint8_t{4};
    const uint8_t child1 = random(ai_copy, 99) < 30 ? uint8_t{1} : uint8_t{4};
    const uint8_t parent_roll =
        random(ai_copy, 99) < 30 ? uint8_t{1} : uint8_t{4};
    const int32_t ai_before = s.ai_rng.counter;

    EXPECT_FALSE(pump_monster_turn(s));

    ASSERT_EQ(s.monster_count, 3);
    EXPECT_EQ(s.monsters[0].monster_id,
              static_cast<uint16_t>(MonsterId::SPIKE_SLIME_MEDIUM));
    EXPECT_EQ(s.monsters[1].monster_id,
              static_cast<uint16_t>(MonsterId::SPIKE_SLIME_LARGE));
    EXPECT_EQ(s.monsters[2].monster_id,
              static_cast<uint16_t>(MonsterId::SPIKE_SLIME_MEDIUM));
    EXPECT_EQ(s.monsters[0].hp, 21);
    EXPECT_EQ(s.monsters[2].hp, 21);
    EXPECT_EQ(s.monsters[1].hp, 0);
    EXPECT_EQ(s.monsters[1].block, 5) << "suicide leaves block untouched";
    EXPECT_EQ(s.monsters[0].move_history[0], child0);
    EXPECT_EQ(s.monsters[2].move_history[0], child1);
    EXPECT_EQ(s.ai_rng.counter, ai_before + 3)
        << "two child rolls + the posthumous parent RollMoveAction";
    // The dead parent's history: the posthumous roll lands over the
    // takeTurn-time SPLIT push.
    EXPECT_EQ(s.monsters[1].move_history[0], parent_roll);
    EXPECT_EQ(s.monsters[1].move_history[1], kSplitMove);
    EXPECT_EQ(s.flags & kCombatFlagCannotLose, 0u);
}

// --- Player-turn trigger -> next monster turn splits ---------------------------

TEST(LargeSlimeSplit, PlayerAttackTriggersThenMonsterTurnSplits) {
    CombatState s = make_state(9003);
    acid_slime_large_init(s, 0);
    MonsterState& m = s.monsters[0];
    m.max_hp = 70;
    m.hp = 40;

    // Player turn: a card attack brings it to half. The interrupt telegraphs
    // SPLIT synchronously and the queued SetMoveAction re-asserts on drain.
    execute_opcode(s, damage_item(0, 5));
    EXPECT_EQ(m.hp, 35);
    EXPECT_EQ(m.move_history[0], kSplitMove);
    drain(s);  // SetMoveAction
    EXPECT_EQ(m.intent, kIntentUnknown);

    // Monster turn: the split executes; children carry the CURRENT hp (35).
    EXPECT_FALSE(pump_monster_turn(s));
    ASSERT_EQ(s.monster_count, 3);
    EXPECT_EQ(s.monsters[0].hp, 35);
    EXPECT_EQ(s.monsters[2].hp, 35);
    EXPECT_EQ(s.monsters[1].hp, 0);
}

TEST(LargeSlimeSplit, KillingParentBeforeSplitTurnEndsCombat) {
    CombatState s = make_state(9004);
    acid_slime_large_init(s, 0);
    MonsterState& m = s.monsters[0];
    m.max_hp = 70;
    m.hp = 36;
    execute_opcode(s, damage_item(0, 1));  // telegraph
    drain(s);
    execute_opcode(s, damage_item(0, 99));  // kill before the split turn
    EXPECT_EQ(m.hp, 0);
    EXPECT_EQ(s.action_count, 0) << "no pending SpawnMonsterAction";
    CombatState t = s;
    EXPECT_EQ(pump_step(t, dispatch_monster_turn).outcome,
              PumpOutcome::COMBAT_OVER)
        << "no descendants pending -> combat over";
    EXPECT_EQ(s.monster_count, 1);
}

// --- Encounter wiring ----------------------------------------------------------

TEST(LargeSlimeEncounter, ProgramDispatchesImplementedActors) {
    CombatState s = make_state(9005);
    RngStream misc = from_seed(9005);
    ResolvedGroup group{};
    ASSERT_TRUE(resolve_encounter("Large Slime", misc, group));
    ASSERT_EQ(group.count, 1);
    MonsterId ids[kMonsterCap]{};
    ids[0] = static_cast<MonsterId>(
        sts::registry::monster_from_game_id(group.members[0]));
    EXPECT_TRUE(ids[0] == MonsterId::ACID_SLIME_LARGE ||
                ids[0] == MonsterId::SPIKE_SLIME_LARGE)
        << group.members[0];
    EXPECT_NE(monster_init_fn(ids[0]), nullptr);
    EXPECT_NE(monster_turn_fn(ids[0]), &default_monster_turn);
    spawn_group(s, std::span<const MonsterId>(ids, 1));
    EXPECT_EQ(s.monster_count, 1);
    EXPECT_EQ(s.monster_hp_rng.counter, 1) << "one ctor HP draw";
    EXPECT_EQ(s.ai_rng.counter, 1) << "one init() random(99)";
    use_pre_battle_actions(s);
    EXPECT_EQ(s.action_count, 0) << "large slimes have no pre-battle action";
}

}  // namespace
}  // namespace sts::engine
