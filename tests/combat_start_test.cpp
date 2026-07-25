// How a combat STARTS -- the turn-1 block, and the guard that both
// combat-construction paths keep running the same one.
//
// THE DEFECT THIS PINS. Turn 1 used to be primed by setting turn_has_ended and
// letting getNextAction's step-6 branch (GameActionManager.java:329-366) do the
// setup. That branch opens with monsters.applyEndOfTurnPowers()
// (GameActionManager.java:331 -> MonsterGroup.java:290-304 ->
// AbstractCreature.applyEndOfTurnTriggers, AbstractCreature.java:547-553), so
// every end-of-round hook fired once before the player had taken a turn.
//
// The game cannot reach that branch on turn 1. AbstractRoom.update
// (AbstractRoom.java:236-243) sets turnHasEnded = true and in the very next line
// queues GainEnergyAndEnableControlsAction, whose update clears it again
// (GainEnergyAndEnableControlsAction.java:35); the action queue is never empty
// while that item is pending, so getNextAction takes step 1 and the step-6 test
// is false by the time it is reached. The turn-1 block does the start-of-turn
// work itself, and it has no applyEndOfTurnPowers line.
//
// THE REPRODUCER. A sleeping Lagavulin's usePreBattleAction gives it 8 block AND
// a MetallicizePower(8) (Lagavulin.java:102-114, ARMOR_AMT :66), and
// MetallicizePower binds atEndOfTurnPreEndTurnCards (MetallicizePower.java:38-42)
// -- an end-of-round hook, on a power that exists before the player acts. Monster
// block never decays (MonsterStartTurnAction is uncalled), so the armour is a
// running total and the spurious pass is directly visible: 16 on turn 1 instead
// of 8, and +8 on every later turn.
//
// Provenance (read in full): AbstractRoom.update (AbstractRoom.java:229-258);
// GainEnergyAndEnableControlsAction.java:22-38; GameActionManager.getNextAction
// (GameActionManager.java:303-366) + clear() (:420-435) + field initialisers
// (:74-76); MonsterGroup.applyEndOfTurnPowers (MonsterGroup.java:290-304);
// AbstractCreature.applyEndOfTurnTriggers (AbstractCreature.java:547-553);
// MetallicizePower.java:19-42; Lagavulin.java:60-66,102-114,116-174,212-227.

#include "sts/engine/action_queue.hpp"

#include <cstdint>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "sts/engine/advance.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_state.hpp"  // kMapCols
#include "sts/engine/types.hpp"

#ifndef STS_SOURCE_ROOT
#error "STS_SOURCE_ROOT must be defined by tests/CMakeLists.txt"
#endif

namespace sts::engine {
namespace {

constexpr uint8_t kA20 = 20;
constexpr int64_t kSeed = 12345;

// ARMOR_AMT (Lagavulin.java:66) -- the pre-battle block, the Metallicize stack,
// and therefore the size of one armour tick.
constexpr int16_t kArmor = 8;

const Action kProceed = make_action(ActionVerb::CHOOSE);

void step(RunController& rc, Action a) {
    StepResult res{};
    advance(std::span<RunController>(&rc, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&res, 1));
}

uint8_t first_start_column(const RunController& rc) {
    RunActionMask m{};
    legal_actions(rc, m);
    for (uint8_t x = 0; x < kMapCols; ++x) {
        if (m.can_choose_node[x]) return x;
    }
    ADD_FAILURE() << "no legal start column";
    return 0;
}

// Walk the REAL run path (run_begin -> map pick -> onPlayerEntry -> enter_combat)
// into a floor-1 combat against `encounter`. Substituting the encounter key is
// the only shortcut: it puts an arbitrary encounter in front of the genuine
// combat-construction path instead of hunting a seed whose map happens to open
// on the elite we need.
RunController enter_floor_one_combat(std::string_view encounter) {
    RunController rc = run_begin(kSeed, kA20);
    rc.lists.monster_list[0] = encounter;
    step(rc, kProceed);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT))
        << "floor-1 room did not build a combat for encounter " << encounter;
    return rc;
}

int16_t monster_power_amount(const CombatState& s, uint8_t mi, PowerId id) {
    const MonsterState& m = s.monsters[mi];
    for (uint8_t i = 0; i < m.power_count; ++i) {
        if (m.powers[i].power_id == static_cast<uint16_t>(id)) {
            return m.powers[i].amount;
        }
    }
    return -1;
}

// =============================================================================
// The regression test: an end-of-round power present at combat start
// =============================================================================

TEST(CombatStart, SleepingLagavulinHasOnlyItsPreBattleArmourOnTurnOne) {
    RunController rc = enter_floor_one_combat("Lagavulin");
    const CombatState& s = rc.combat;

    ASSERT_EQ(s.monster_count, 1);
    ASSERT_EQ(s.monsters[0].monster_id, static_cast<uint16_t>(MonsterId::LAGAVULIN));
    EXPECT_EQ(monster_power_amount(s, 0, PowerId::METALLICIZE), kArmor)
        << "usePreBattleAction must have applied Metallicize before turn 1";

    // GainBlockAction(this, this, ARMOR_AMT) and nothing else. A second tick here
    // means the combat-start priming ran the end-of-round pass.
    EXPECT_EQ(s.monsters[0].block, kArmor)
        << "the turn-1 block (AbstractRoom.java:236-258) has no "
           "applyEndOfTurnPowers line -- Metallicize must not have triggered yet";
    EXPECT_EQ(s.turn, 1);
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
}

TEST(CombatStart, LagavulinArmourTicksOncePerCompletedRound) {
    RunController rc = enter_floor_one_combat("Lagavulin");
    ASSERT_EQ(rc.combat.monsters[0].block, kArmor);

    // Each END_TURN closes a round: sentinel -> monster turn (asleep: IDLE) ->
    // step 6, whose applyEndOfTurnPowers pass is where Metallicize belongs.
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.combat.turn, 2);
    EXPECT_EQ(rc.combat.monsters[0].block, static_cast<int16_t>(2 * kArmor));

    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.combat.turn, 3);
    EXPECT_EQ(rc.combat.monsters[0].block, static_cast<int16_t>(3 * kArmor));
}

// =============================================================================
// The two combat-construction paths must agree
// =============================================================================

// The observable turn-1 contract. combat_begin (advance.cpp) and enter_combat
// (run_advance.cpp) build different decks against different encounters, so the
// states differ everywhere else -- these are the fields the shared turn-1 block
// owns.
struct Priming {
    int32_t turn = 0;
    uint8_t phase = 0;
    uint8_t turn_has_ended = 0;
    uint8_t monster_attacks_queued = 0;
    int16_t player_energy = 0;
    uint8_t hand_count = 0;
    uint8_t cards_played_this_turn = 0;
    uint8_t action_count = 0;
    uint8_t pre_turn_count = 0;
    uint8_t card_queue_count = 0;
    uint8_t monster_queue_count = 0;

    bool operator==(const Priming&) const = default;
};

Priming priming_of(const CombatState& s) {
    Priming p;
    p.turn = static_cast<int32_t>(s.turn);
    p.phase = s.phase;
    p.turn_has_ended = s.turn_has_ended;
    p.monster_attacks_queued = s.monster_attacks_queued;
    p.player_energy = s.player_energy;
    p.hand_count = s.hand_count;
    p.cards_played_this_turn = s.cards_played_this_turn;
    p.action_count = s.action_count;
    p.pre_turn_count = s.pre_turn_count;
    p.card_queue_count = s.card_queue_count;
    p.monster_queue_count = s.monster_queue_count;
    return p;
}

Priming expected_priming() {
    Priming p;
    p.turn = 1;                      // clear(): turn = 1 (GameActionManager.java:432)
    p.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    p.turn_has_ended = 0;            // GainEnergyAndEnableControlsAction.java:35
    p.monster_attacks_queued = 1;    // field initialiser (GameActionManager.java:76)
    p.player_energy = kIroncladBaseEnergy;
    p.hand_count = static_cast<uint8_t>(kStartOfTurnDrawCount);
    return p;                        // every queue drained, nothing played yet
}

TEST(CombatStart, CombatBeginAndEnterCombatPrimeTurnOneIdentically) {
    // The M1 skeleton deck (stage-a design §9) -- combat_begin's own scenario.
    std::vector<CardId> deck;
    for (int i = 0; i < 5; ++i) deck.push_back(CardId::STRIKE);
    for (int i = 0; i < 4; ++i) deck.push_back(CardId::DEFEND);
    deck.push_back(CardId::BASH);
    deck.push_back(CardId::SHRUG_IT_OFF);
    deck.push_back(CardId::POMMEL_STRIKE);
    const CombatState direct = combat_begin(kSeed, 1, std::span<const CardId>(deck));
    const RunController rc = enter_floor_one_combat("Jaw Worm");

    EXPECT_EQ(priming_of(direct), expected_priming());
    EXPECT_EQ(priming_of(rc.combat), expected_priming());
    EXPECT_EQ(priming_of(direct), priming_of(rc.combat))
        << "combat_begin and enter_combat must not disagree about how a combat "
           "starts";
}

// =============================================================================
// ... and must keep agreeing BY CONSTRUCTION
// =============================================================================
//
// The behavioural test above can only compare what both paths can reach, and the
// encounter combat_begin builds (a lone Jaw Worm) binds no end-of-round hook --
// so it would stay green if one path were fixed and the other left priming by
// hand. This is the guard that actually fails in that case: both paths must
// delegate to the one shared turn-1 block, and neither may re-grow its own.
// (conventions.md §7: automate the check, don't re-document the trap.)

std::string read_source(const char* rel) {
    const std::string path = std::string(STS_SOURCE_ROOT) + "/" + rel;
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "cannot read " << path;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The two combat-construction paths, and the shape each must not re-grow.
constexpr const char* kConstructionSources[] = {
    "src/engine/advance.cpp",       // combat_begin
    "src/engine/run_advance.cpp",   // enter_combat
};

TEST(CombatStart, BothConstructionPathsDelegateToTheSharedTurnOneBlock) {
    for (const char* rel : kConstructionSources) {
        const std::string src = read_source(rel);
        EXPECT_NE(src.find("begin_first_turn("), std::string::npos)
            << rel << " no longer calls begin_first_turn.\n"
            << "  Both combat-construction paths must start a combat through the\n"
            << "  same function, or they will disagree about turn 1 -- which is\n"
            << "  how the end-of-round-on-turn-1 defect stayed live in one of\n"
            << "  them. See action_queue.hpp's begin_first_turn declaration.";
    }
}

TEST(CombatStart, NeitherConstructionPathHandRollsTurnOnePriming) {
    // The exact assignments the old hand-rolled priming used. Their return is the
    // regression: setting turn_has_ended before a pump routes combat start
    // through getNextAction step 6 and its applyEndOfTurnPowers pass.
    constexpr const char* kForbidden[] = {
        "turn_has_ended = 1",
        "monster_attacks_queued = 1",
    };
    for (const char* rel : kConstructionSources) {
        const std::string src = read_source(rel);
        for (const char* shape : kForbidden) {
            EXPECT_EQ(src.find(shape), std::string::npos)
                << rel << " primes turn 1 by hand ('" << shape << "').\n"
                << "  Turn-1 priming belongs to begin_first_turn (action_queue.cpp)\n"
                << "  alone; a second copy is free to drift from the first.";
        }
    }
}

}  // namespace
}  // namespace sts::engine
