// The two Act-1 slavers: native AI, turn bodies, and the Red Slaver's Entangle.
//
// The fixture battery is independent of the engine (written from the decompiled
// Java by tests/fixtures/gen_slaver_fixture.py, 32 seed-battery seeds x 20
// turns): slaver_{blue,red}_fixture.tsv carry the HP roll (monsterHpRng), the
// executed move and the exact aiRng draw sequence.
//
// DRAW ACCOUNTING at A20 (monster_slaver.hpp): one monsterHpRng draw per ctor,
// one aiRng.random(99) at init(), and one more per turn through the queued
// RollMoveAction both takeTurn bodies end in. BOTH getMove overrides READ the
// rolled num, so the move column of the fixture is roll-driven -- these
// batteries check the decision trees, not just a draw count.

#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_slaver.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/monster_table.hpp"

#ifndef STS_FIXTURE_DIR
#error "STS_FIXTURE_DIR must be defined by the build (see tests/CMakeLists.txt)"
#endif

namespace sts::engine {
namespace {

constexpr int kNumTurns = 20;
constexpr int32_t kA20 = kMonsterAscension;

constexpr uint8_t kBlueStab = sts::registry::kSlaverBlueMoveStab;
constexpr uint8_t kBlueRake = sts::registry::kSlaverBlueMoveRake;
constexpr uint8_t kRedStab = sts::registry::kSlaverRedMoveStab;
constexpr uint8_t kRedEntangle = sts::registry::kSlaverRedMoveEntangle;
constexpr uint8_t kRedScrape = sts::registry::kSlaverRedMoveScrape;

struct TurnRow {
    int turn = 0;
    int move = 0;
    uint64_t ai_s0 = 0;
    uint64_t ai_s1 = 0;
    int32_t ai_counter = 0;
};

struct SeedCase {
    std::string label;
    int64_t seed = 0;
    int hp = 0;
    uint64_t hp_s0 = 0, hp_s1 = 0;
    int32_t hp_counter = 0;
    std::vector<TurnRow> turns;
};

std::vector<SeedCase> LoadFixture(const std::string& file) {
    std::vector<SeedCase> cases;
    std::ifstream in(std::string(STS_FIXTURE_DIR) + "/" + file);
    if (!in.is_open()) {
        return cases;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream ss(line);
        std::string kind;
        std::getline(ss, kind, '\t');
        auto col = [&ss]() {
            std::string v;
            std::getline(ss, v, '\t');
            return v;
        };
        if (kind == "seed") {
            SeedCase c;
            c.label = col();
            c.seed = std::stoll(col());
            c.hp = std::stoi(col());
            c.hp_s0 = std::stoull(col());
            c.hp_s1 = std::stoull(col());
            c.hp_counter = std::stoi(col());
            cases.push_back(std::move(c));
        } else if (kind == "turn") {
            SeedCase& c = cases.back();
            TurnRow r;
            r.turn = std::stoi(col());
            r.move = std::stoi(col());
            r.ai_s0 = std::stoull(col());
            r.ai_s1 = std::stoull(col());
            r.ai_counter = std::stoi(col());
            c.turns.push_back(r);
        }
    }
    return cases;
}

CombatState MakeState(int64_t seed) {
    CombatState s{};
    s.player_hp = 400;   // deep enough that 20 turns of stabs cannot end it
    s.player_max_hp = 400;
    s.monster_count = 1;
    s.monster_hp_rng = from_seed(seed);
    s.ai_rng = from_seed(seed);
    return s;
}

void drain(CombatState& s) {
    while (s.action_count > 0) {
        const ActionQueueItem it = s.action_queue[s.action_head];
        s.action_head =
            static_cast<uint8_t>((s.action_head + 1) % kActionQueueCap);
        --s.action_count;
        execute_opcode(s, it);
    }
}

int16_t player_power(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return s.player_powers[i].amount;
        }
    }
    return -1;  // absent
}

using InitFn = void (*)(CombatState&, uint8_t);
using TurnFn = void (*)(CombatState&, uint8_t);

void RunSolo(const std::string& file, InitFn init_fn, TurnFn turn_fn,
             int hp_min, int hp_max) {
    const std::vector<SeedCase> cases = LoadFixture(file);
    ASSERT_EQ(cases.size(), 32u) << "fixture missing/malformed: " << file;

    for (const auto& c : cases) {
        ASSERT_EQ(c.turns.size(), static_cast<size_t>(kNumTurns)) << c.label;
        CombatState s = MakeState(c.seed);
        init_fn(s, 0);

        // ctor: setHp(min, max) -- exactly one monsterHpRng draw.
        EXPECT_EQ(s.monsters[0].hp, c.hp) << file << " " << c.label;
        EXPECT_EQ(s.monsters[0].hp, s.monsters[0].max_hp) << file << " " << c.label;
        EXPECT_GE(s.monsters[0].hp, hp_min) << file << " " << c.label;
        EXPECT_LE(s.monsters[0].hp, hp_max) << file << " " << c.label;
        EXPECT_EQ(s.monster_hp_rng.s0, c.hp_s0) << file << " " << c.label;
        EXPECT_EQ(s.monster_hp_rng.s1, c.hp_s1) << file << " " << c.label;
        EXPECT_EQ(s.monster_hp_rng.counter, c.hp_counter) << file << " " << c.label;
        // init() -> rollMove: exactly one aiRng draw.
        EXPECT_EQ(s.ai_rng.counter, 1) << file << " " << c.label;

        for (size_t k = 0; k < c.turns.size(); ++k) {
            const TurnRow& row = c.turns[k];
            EXPECT_EQ(static_cast<int>(s.monsters[0].move_history[0]), row.move)
                << file << " " << c.label << " turn " << row.turn;
            turn_fn(s, 0);
            // The queued RollMoveAction only takes effect once the action queue
            // drains -- exactly as in the pump.
            drain(s);
            EXPECT_EQ(s.ai_rng.s0, row.ai_s0)
                << file << " " << c.label << " turn " << row.turn;
            EXPECT_EQ(s.ai_rng.s1, row.ai_s1)
                << file << " " << c.label << " turn " << row.turn;
            EXPECT_EQ(s.ai_rng.counter, row.ai_counter)
                << file << " " << c.label << " turn " << row.turn;
        }
    }
}

// --- Fixture batteries -------------------------------------------------------

TEST(SlaverFixture, BlueMatchesFixture) {
    RunSolo("slaver_blue_fixture.tsv", &slaver_blue_init, &slaver_blue_take_turn,
            /*hp_min=*/48, /*hp_max=*/52);   // SlaverBlue.java:51
}

TEST(SlaverFixture, RedMatchesFixture) {
    RunSolo("slaver_red_fixture.tsv", &slaver_red_init, &slaver_red_take_turn,
            /*hp_min=*/48, /*hp_max=*/52);   // SlaverRed.java:61
}

// Both slavers roll once per turn on top of init's draw (SlaverBlue.java:89 /
// SlaverRed.java:110). Read straight off the fixtures.
TEST(SlaverFixture, BothDrawOneAiRngPerTurn) {
    for (const char* file : {"slaver_blue_fixture.tsv", "slaver_red_fixture.tsv"}) {
        const auto cases = LoadFixture(file);
        ASSERT_EQ(cases.size(), 32u) << file;
        for (const auto& c : cases) {
            for (const auto& row : c.turns) {
                EXPECT_EQ(row.ai_counter, row.turn + 1)
                    << file << " " << c.label << " turn " << row.turn;
            }
        }
    }
}

// The fixtures must actually exercise both branches of each decision tree,
// otherwise "matches the fixture" would be vacuous for a roll-driven getMove.
TEST(SlaverFixture, BatteriesCoverEveryMove) {
    const auto blue = LoadFixture("slaver_blue_fixture.tsv");
    ASSERT_EQ(blue.size(), 32u);
    int blue_stab = 0, blue_rake = 0;
    for (const auto& c : blue) {
        for (const auto& row : c.turns) {
            blue_stab += row.move == kBlueStab ? 1 : 0;
            blue_rake += row.move == kBlueRake ? 1 : 0;
        }
    }
    EXPECT_GT(blue_stab, 0);
    EXPECT_GT(blue_rake, 0);
    EXPECT_EQ(blue_stab + blue_rake, 32 * kNumTurns);

    const auto red = LoadFixture("slaver_red_fixture.tsv");
    ASSERT_EQ(red.size(), 32u);
    int red_stab = 0, red_scrape = 0;
    for (const auto& c : red) {
        // Entangle is once per combat at most, and the `num >= 75` gate is
        // eventually met over 20 turns for every seed in the battery.
        int entangles = 0;
        for (const auto& row : c.turns) {
            entangles += row.move == kRedEntangle ? 1 : 0;
            red_stab += row.move == kRedStab ? 1 : 0;
            red_scrape += row.move == kRedScrape ? 1 : 0;
        }
        EXPECT_LE(entangles, 1) << c.label << ": usedEntangle is never cleared";
    }
    EXPECT_GT(red_stab, 0);
    EXPECT_GT(red_scrape, 0);
}

// --- Blue Slaver move effects ------------------------------------------------

// SlaverBlue.takeTurn (SlaverBlue.java:69-90) at the engine's fixed A20: Stab is
// damage.get(0) == 13 (the A2 branch, :56-58) and Rake is damage.get(1) == 8 plus
// WeakPower(player, weakAmt + 1 == 2, true) (:81-84).
TEST(SlaverBlue, StabAndRakeNumbersAtA20) {
    ASSERT_GE(kA20, 17);
    CombatState s = MakeState(12345);
    slaver_blue_init(s, 0);

    // Force each move rather than waiting for the roll to produce it.
    set_monster_move(s.monsters[0], kBlueStab, MonsterIntent::ATTACK);
    const int16_t hp_before = s.player_hp;
    slaver_blue_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(hp_before - s.player_hp, 13);
    EXPECT_EQ(player_power(s, PowerId::WEAK), -1);

    CombatState r = MakeState(12345);
    slaver_blue_init(r, 0);
    set_monster_move(r.monsters[0], kBlueRake, MonsterIntent::ATTACK_DEBUFF);
    const int16_t rake_hp_before = r.player_hp;
    slaver_blue_take_turn(r, 0);
    drain(r);
    EXPECT_EQ(rake_hp_before - r.player_hp, 8);
    EXPECT_EQ(player_power(r, PowerId::WEAK), 2);
}

// --- Red Slaver: Entangle ----------------------------------------------------

// SlaverRed.takeTurn case ENTANGLE (SlaverRed.java:83-92): ApplyPowerAction of a
// single Entangled stack onto the player, and usedEntangle latched SYNCHRONOUSLY
// -- before the queued RollMoveAction behind it resolves, which is the call that
// has to see it.
TEST(SlaverRed, EntangleAppliesOneStackAndLatchesBeforeTheRoll) {
    CombatState s = MakeState(9876);
    slaver_red_init(s, 0);
    // getMove's firstTurn branch forces the opening Stab regardless of the roll.
    EXPECT_EQ(s.monsters[0].move_history[0], kRedStab);
    EXPECT_EQ(s.monsters[0].pad0 & kSlaverRedPad0UsedEntangle, 0u);

    set_monster_move(s.monsters[0], kRedEntangle, MonsterIntent::STRONG_DEBUFF);
    slaver_red_take_turn(s, 0);
    // The latch is set inside takeTurn, with the actions still queued.
    EXPECT_NE(s.monsters[0].pad0 & kSlaverRedPad0UsedEntangle, 0u);
    drain(s);
    EXPECT_EQ(player_power(s, PowerId::ENTANGLE), 1);

    // The Entangle branch of getMove is now dead for the rest of the combat, no
    // matter what the roll is (SlaverRed.java:145 -- usedEntangle is never
    // cleared). Drive 60 further rolls and check none of them re-telegraphs it.
    for (int i = 0; i < 60; ++i) {
        slaver_red_roll_move(s, 0);
        EXPECT_NE(s.monsters[0].move_history[0], kRedEntangle);
    }
}

// SlaverRed.takeTurn case SCRAPE (:99-108) at A20: damage.get(1) == 9 (the A2
// branch, :65-70) plus VulnerablePower(player, VULN_AMT + 1 == 2, true).
TEST(SlaverRed, ScrapeNumbersAtA20) {
    ASSERT_GE(kA20, 17);
    CombatState s = MakeState(4242);
    slaver_red_init(s, 0);
    set_monster_move(s.monsters[0], kRedScrape, MonsterIntent::ATTACK_DEBUFF);
    const int16_t hp_before = s.player_hp;
    slaver_red_take_turn(s, 0);
    drain(s);
    // The Vulnerable lands with the damage, but ApplyPowerAction resolves AFTER
    // the DamageAction (takeTurn's addToBottom order), so this hit is unamplified.
    EXPECT_EQ(hp_before - s.player_hp, 9);
    EXPECT_EQ(player_power(s, PowerId::VULNERABLE), 2);
}

// SlaverRed.takeTurn case STAB (:93-98) at A20: damage.get(0) == 14.
TEST(SlaverRed, StabNumbersAtA20) {
    CombatState s = MakeState(4242);
    slaver_red_init(s, 0);
    const int16_t hp_before = s.player_hp;
    slaver_red_take_turn(s, 0);   // the forced opening Stab
    drain(s);
    EXPECT_EQ(hp_before - s.player_hp, 14);
}

// --- Entangled the POWER: the attack lock and its one-turn life ---------------

CombatState MakeHandState(std::initializer_list<CardId> hand) {
    CombatState s{};
    uint8_t i = 0;
    for (const CardId id : hand) {
        const CardDef* def = card_def(id);
        EXPECT_NE(def, nullptr);
        s.card_pool[i].card_id = static_cast<uint16_t>(id);
        s.card_pool[i].cost_now = card_cost(*def, 0);
        s.card_pool[i].flags = card_flags(*def, 0);
        s.hand[i] = i;
        ++i;
    }
    s.hand_count = i;
    s.player_hp = 100;
    s.player_max_hp = 100;
    s.player_energy = 3;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::SLAVER_RED);
    s.monsters[0].hp = 50;
    s.monsters[0].max_hp = 50;
    s.monster_attacks_queued = 1;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

void apply_entangle(CombatState& s) {
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = 0;
    apply.tgt = kActorPlayer;
    apply.amount = 1;
    apply.flags = make_apply_power_flags(PowerId::ENTANGLE);
    execute_opcode(s, apply);
}

// AbstractCard.hasEnoughEnergy:872-875 -- an ATTACK is unplayable while the
// player has Entangled; every other card type is untouched.
TEST(Entangle, LocksAttacksOnlyWhileHeld) {
    CombatState s = MakeHandState({CardId::STRIKE, CardId::DEFEND});
    ASSERT_EQ(card_def(CardId::STRIKE)->type, CardType::ATTACK);
    ASSERT_EQ(card_def(CardId::DEFEND)->type, CardType::SKILL);

    ActionMask mask{};
    legal_actions(s, mask);
    EXPECT_TRUE(mask.can_play[0]);
    EXPECT_TRUE(mask.can_play[1]);

    apply_entangle(s);
    ASSERT_EQ(player_power(s, PowerId::ENTANGLE), 1);
    legal_actions(s, mask);
    EXPECT_FALSE(mask.can_play[0]) << "Entangled must veto the ATTACK";
    EXPECT_TRUE(mask.can_play[1]) << "and only the ATTACK";
    EXPECT_TRUE(mask.can_end_turn);
    // The per-target grid follows can_play, so no target is offered either.
    for (int t = 0; t < kMonsterCap; ++t) {
        EXPECT_FALSE(mask.can_play_target[0][t]);
    }
}

// EntanglePower.atEndOfTurn (EntanglePower.java:50-53): addToBot a
// RemoveSpecificPowerAction, so the lock lasts exactly the turn it was applied
// on. dispatch_at_end_of_turn is applyEndOfTurnTriggers, the player-powers-only
// walk -- which is also why the Java's `if (isPlayer)` guard needs no expression.
TEST(Entangle, RemovesItselfAtTheEndOfThePlayerTurn) {
    CombatState s = MakeHandState({CardId::STRIKE});
    apply_entangle(s);
    ASSERT_EQ(player_power(s, PowerId::ENTANGLE), 1);

    dispatch_at_end_of_turn(s);
    ASSERT_EQ(s.action_count, 1) << "one queued RemoveSpecificPowerAction";
    drain(s);
    EXPECT_EQ(player_power(s, PowerId::ENTANGLE), -1);

    ActionMask mask{};
    legal_actions(s, mask);
    EXPECT_TRUE(mask.can_play[0]) << "the attack lock lifts with the power";
}

// The registry row itself: a DEBUFF (so Artifact nullifies it) that does not
// stack (EntanglePower's ctor takes no amount, EntanglePower.java:20-30).
TEST(Entangle, IsANonStackingDebuff) {
    const PowerDef* def = power_def(PowerId::ENTANGLE);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->type, PowerType::DEBUFF);
    EXPECT_EQ(def->stack, PowerStack::NONE);
    EXPECT_EQ(sts::registry::power_game_id(PowerId::ENTANGLE), "Entangled");
    // The removal is a DATA hook, not a native body: dispatch_at_end_of_turn
    // walks the player's powers only, which is the Java's `if (isPlayer)` guard.
    EXPECT_FALSE(def->native);
    EXPECT_NE(def->hook_binding(sts::registry::Hook::AT_END_OF_TURN), nullptr);
}

// --- Registry identity -------------------------------------------------------

TEST(SlaverRegistry, IdsAndDispatch) {
    namespace r = sts::registry;
    EXPECT_EQ(r::monster_game_id(r::MonsterId::SLAVER_BLUE), "SlaverBlue");
    EXPECT_EQ(r::monster_game_id(r::MonsterId::SLAVER_RED), "SlaverRed");
    EXPECT_EQ(monster_init_fn(MonsterId::SLAVER_BLUE), &slaver_blue_init);
    EXPECT_EQ(monster_init_fn(MonsterId::SLAVER_RED), &slaver_red_init);
    EXPECT_EQ(monster_turn_fn(MonsterId::SLAVER_BLUE), &slaver_blue_take_turn);
    EXPECT_EQ(monster_turn_fn(MonsterId::SLAVER_RED), &slaver_red_take_turn);
    // Both queue their roll (SlaverBlue.java:89 / SlaverRed.java:110)...
    EXPECT_EQ(monster_roll_move_fn(MonsterId::SLAVER_BLUE), &slaver_blue_roll_move);
    EXPECT_EQ(monster_roll_move_fn(MonsterId::SLAVER_RED), &slaver_red_roll_move);
    // ...and neither declares usePreBattleAction or damage().
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::SLAVER_BLUE), nullptr);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::SLAVER_RED), nullptr);
}

// The un-park gate is `monster_init_fn(id) == nullptr` (run_advance.cpp:273) --
// registering an init fn is the WHOLE of what lets a run-created combat with
// these groups play out, with no edit to the run layer. These four Act-1
// encounters go live with this change; "Looter" and "Exordium Thugs" do not,
// because both can emit a Looter.
TEST(SlaverRegistry, TheEncounterRowsThatGoLive) {
    for (const char* key : {"Blue Slaver", "Red Slaver", "2 Fungi Beasts",
                            "Exordium Wildlife"}) {
        RngStream misc = from_seed(4711);
        ResolvedGroup group{};
        ASSERT_TRUE(resolve_encounter(key, misc, group)) << key;
        ASSERT_GT(group.count, 0) << key;
        for (uint8_t i = 0; i < group.count; ++i) {
            const MonsterId id = static_cast<MonsterId>(
                sts::registry::monster_from_game_id(group.members[i]));
            EXPECT_NE(id, MonsterId::NONE) << key << " -> " << group.members[i];
            EXPECT_NE(monster_init_fn(id), nullptr)
                << key << " -> " << group.members[i] << " is still parked";
        }
    }
}

}  // namespace
}  // namespace sts::engine
