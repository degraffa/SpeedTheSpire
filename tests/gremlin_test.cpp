// The five Act-1 gremlins: native AI, turn bodies, Angry, and the Tsundere's
// block-an-ally logic (B3.16).
//
// Two fixture batteries, both independent of the engine (written from the
// decompiled Java by tests/fixtures/gen_gremlin_fixture.py, 32 seed-battery
// seeds x 20 turns):
//   * gremlin_{warrior,thief,fat,tsundere,wizard}_fixture.tsv -- solo: the HP
//     roll (monsterHpRng) and the exact aiRng draw sequence + move sequence.
//   * gremlin_protect_fixture.tsv -- a 4-gremlin gang whose Tsundere protects
//     every turn, pinning the aiRng block-TARGET pick.
//
// DRAW ACCOUNTING at A20 (monster_gremlin.hpp): every ctor = 1 monsterHpRng
// draw; every init() = 1 aiRng.random(99), discarded. Per turn: Warrior, Thief,
// Wizard and a solo Tsundere draw NOTHING; GremlinFat draws one aiRng.random(99)
// through a queued RollMoveAction; a Tsundere with at least one live ally draws
// one aiRng pick for the block target.

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_gremlin.hpp"
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

struct TurnRow {
    int turn = 0;
    int value = 0;  // solo: the executed move id; protect: the target slot
    uint64_t ai_s0 = 0;
    uint64_t ai_s1 = 0;
    int32_t ai_counter = 0;
};

struct SeedCase {
    std::string label;
    int64_t seed = 0;
    std::vector<int> hp;
    uint64_t hp_s0 = 0, hp_s1 = 0;
    int32_t hp_counter = 0;
    std::vector<TurnRow> turns;
};

// Shared loader for both batteries: a `seed` row carries one or more HP values
// followed by the monsterHpRng state, and each `turn` row one integer plus the
// aiRng state.
std::vector<SeedCase> LoadFixture(const std::string& file, size_t hp_values) {
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
            for (size_t i = 0; i < hp_values; ++i) {
                c.hp.push_back(std::stoi(col()));
            }
            c.hp_s0 = std::stoull(col());
            c.hp_s1 = std::stoull(col());
            c.hp_counter = std::stoi(col());
            cases.push_back(std::move(c));
        } else if (kind == "turn") {
            SeedCase& c = cases.back();
            TurnRow r;
            r.turn = std::stoi(col());
            r.value = std::stoi(col());
            r.ai_s0 = std::stoull(col());
            r.ai_s1 = std::stoull(col());
            r.ai_counter = std::stoi(col());
            c.turns.push_back(r);
        }
    }
    return cases;
}

CombatState MakeState(int64_t seed, uint8_t monsters = 1) {
    CombatState s{};
    s.player_hp = 200;
    s.player_max_hp = 200;
    s.monster_count = monsters;
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

int16_t monster_power(const CombatState& s, uint8_t mi, PowerId id) {
    const MonsterState& m = s.monsters[mi];
    for (uint8_t i = 0; i < m.power_count; ++i) {
        if (m.powers[i].power_id == static_cast<uint16_t>(id)) {
            return m.powers[i].amount;
        }
    }
    return -1;  // absent
}

int16_t player_power(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return s.player_powers[i].amount;
        }
    }
    return -1;
}

// A plain NORMAL attack from the player onto monster `mi`, run through the real
// DAMAGE opcode so the onAttacked dispatch (Angry) fires exactly as in combat.
void player_attacks(CombatState& s, uint8_t mi, int32_t base) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    it.src = kActorPlayer;
    it.tgt = mi;
    it.amount = base;
    execute_opcode(s, it);
    drain(s);  // Angry's addToTop ApplyPowerAction
}

using InitFn = void (*)(CombatState&, uint8_t);
using TurnFn = void (*)(CombatState&, uint8_t);

void RunSolo(const std::string& file, InitFn init_fn, TurnFn turn_fn,
             int hp_min, int hp_max) {
    const std::vector<SeedCase> cases = LoadFixture(file, 1);
    ASSERT_EQ(cases.size(), 32u) << "fixture missing/malformed: " << file;

    for (const auto& c : cases) {
        ASSERT_EQ(c.turns.size(), static_cast<size_t>(kNumTurns)) << c.label;
        CombatState s = MakeState(c.seed);
        init_fn(s, 0);

        // ctor: setHp(min, max) -- exactly one monsterHpRng draw.
        EXPECT_EQ(s.monsters[0].hp, c.hp[0]) << file << " " << c.label;
        EXPECT_EQ(s.monsters[0].hp, s.monsters[0].max_hp) << file << " " << c.label;
        EXPECT_GE(s.monsters[0].hp, hp_min) << file << " " << c.label;
        EXPECT_LE(s.monsters[0].hp, hp_max) << file << " " << c.label;
        EXPECT_EQ(s.monster_hp_rng.s0, c.hp_s0) << file << " " << c.label;
        EXPECT_EQ(s.monster_hp_rng.s1, c.hp_s1) << file << " " << c.label;
        EXPECT_EQ(s.monster_hp_rng.counter, c.hp_counter) << file << " " << c.label;
        // init() -> rollMove: one aiRng draw, whatever the getMove override does
        // with it.
        EXPECT_EQ(s.ai_rng.counter, 1) << file << " " << c.label;

        for (size_t k = 0; k < c.turns.size(); ++k) {
            const TurnRow& row = c.turns[k];
            EXPECT_EQ(static_cast<int>(s.monsters[0].move_history[0]), row.value)
                << file << " " << c.label << " turn " << row.turn;
            turn_fn(s, 0);
            // A queued SetMoveAction / RollMoveAction only takes effect once the
            // action queue drains -- exactly as in the pump.
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

TEST(GremlinFixture, WarriorMatchesFixture) {
    RunSolo("gremlin_warrior_fixture.tsv", &gremlin_warrior_init,
            &gremlin_warrior_take_turn, /*hp_min=*/21, /*hp_max=*/25);
}

TEST(GremlinFixture, ThiefMatchesFixture) {
    RunSolo("gremlin_thief_fixture.tsv", &gremlin_thief_init,
            &gremlin_thief_take_turn, /*hp_min=*/11, /*hp_max=*/15);
}

TEST(GremlinFixture, FatMatchesFixture) {
    RunSolo("gremlin_fat_fixture.tsv", &gremlin_fat_init,
            &gremlin_fat_take_turn, /*hp_min=*/14, /*hp_max=*/18);
}

TEST(GremlinFixture, TsundereSoloMatchesFixture) {
    RunSolo("gremlin_tsundere_fixture.tsv", &gremlin_tsundere_init,
            &gremlin_tsundere_take_turn, /*hp_min=*/13, /*hp_max=*/17);
}

TEST(GremlinFixture, WizardMatchesFixture) {
    RunSolo("gremlin_wizard_fixture.tsv", &gremlin_wizard_init,
            &gremlin_wizard_take_turn, /*hp_min=*/22, /*hp_max=*/26);
}

// Only GremlinFat's takeTurn ends in a RollMoveAction (GremlinFat.java:80). Read
// straight off the fixtures: the other four never move the aiRng counter past
// the single init() draw, Fat moves it once per turn.
TEST(GremlinFixture, OnlyFatDrawsAiRngPerTurn) {
    for (const char* file : {"gremlin_warrior_fixture.tsv",
                             "gremlin_thief_fixture.tsv",
                             "gremlin_tsundere_fixture.tsv",
                             "gremlin_wizard_fixture.tsv"}) {
        const auto cases = LoadFixture(file, 1);
        ASSERT_EQ(cases.size(), 32u) << file;
        for (const auto& c : cases) {
            for (const auto& row : c.turns) {
                EXPECT_EQ(row.ai_counter, 1) << file << " " << c.label
                                             << " turn " << row.turn;
            }
        }
    }
    const auto fat = LoadFixture("gremlin_fat_fixture.tsv", 1);
    ASSERT_EQ(fat.size(), 32u);
    for (const auto& c : fat) {
        for (const auto& row : c.turns) {
            EXPECT_EQ(row.ai_counter, row.turn + 1) << c.label;
        }
    }
}

// --- The Tsundere's block-an-ally logic (the B3.16 acceptance) ---------------

// GainBlockRandomMonsterAction picks among the live monsters that are NOT the
// source, with one aiRng draw, and the block lands on that ALLY. The battery
// keeps all three allies alive, so the Tsundere protects forever
// (aliveCount == 4 > 1) and every turn draws exactly one pick.
TEST(GremlinTsundere, ProtectTargetMatchesFixture) {
    const auto cases = LoadFixture("gremlin_protect_fixture.tsv", 4);
    ASSERT_EQ(cases.size(), 32u) << "gremlin_protect_fixture.tsv missing";
    const MonsterId gang[] = {MonsterId::GREMLIN_WARRIOR,
                              MonsterId::GREMLIN_TSUNDERE,
                              MonsterId::GREMLIN_THIEF,
                              MonsterId::GREMLIN_WIZARD};
    constexpr uint8_t kTsundere = 1;

    for (const auto& c : cases) {
        ASSERT_EQ(c.turns.size(), static_cast<size_t>(kNumTurns)) << c.label;
        CombatState s = MakeState(c.seed, 0);
        spawn_group(s, gang);
        ASSERT_EQ(s.monster_count, 4);
        for (uint8_t i = 0; i < 4; ++i) {
            EXPECT_EQ(s.monsters[i].hp, c.hp[i]) << c.label << " slot " << i;
        }
        EXPECT_EQ(s.monster_hp_rng.s0, c.hp_s0) << c.label;
        EXPECT_EQ(s.monster_hp_rng.s1, c.hp_s1) << c.label;
        EXPECT_EQ(s.monster_hp_rng.counter, c.hp_counter) << c.label;
        EXPECT_EQ(s.ai_rng.counter, 4) << c.label;  // one init() roll each

        for (const auto& row : c.turns) {
            // Still telegraphing Protect: three allies are alive.
            EXPECT_EQ(s.monsters[kTsundere].intent,
                      static_cast<uint8_t>(MonsterIntent::DEFEND))
                << c.label << " turn " << row.turn;
            for (uint8_t i = 0; i < 4; ++i) {
                s.monsters[i].block = 0;
            }
            gremlin_tsundere_take_turn(s, kTsundere);
            EXPECT_EQ(s.ai_rng.s0, row.ai_s0) << c.label << " turn " << row.turn;
            EXPECT_EQ(s.ai_rng.s1, row.ai_s1) << c.label << " turn " << row.turn;
            EXPECT_EQ(s.ai_rng.counter, row.ai_counter)
                << c.label << " turn " << row.turn;
            drain(s);
            const uint8_t target = static_cast<uint8_t>(row.value);
            EXPECT_NE(target, kTsundere) << c.label << " turn " << row.turn;
            for (uint8_t i = 0; i < 4; ++i) {
                EXPECT_EQ(s.monsters[i].block, i == target ? 11 : 0)
                    << c.label << " turn " << row.turn << " slot " << i;
            }
        }
    }
}

// Alone in the group, validMonsters is empty and the action's own fallback
// blocks the SOURCE (GainBlockRandomMonsterAction.java:35) -- with NO aiRng
// draw. aliveCount is then 1, so the > 1 test fails and it switches to Bash.
TEST(GremlinTsundere, AloneBlocksItselfWithoutDrawingAndSwitchesToBash) {
    CombatState s = MakeState(99, 1);
    gremlin_tsundere_init(s, 0);
    const int32_t after_init = s.ai_rng.counter;
    ASSERT_EQ(s.monsters[0].move_history[0], 1);

    gremlin_tsundere_take_turn(s, 0);
    EXPECT_EQ(s.ai_rng.counter, after_init) << "no valid ally -> no aiRng draw";
    drain(s);
    EXPECT_EQ(s.monsters[0].block, 11);  // A17 blockAmt (GremlinTsundere.java:54)
    EXPECT_EQ(s.monsters[0].move_history[0], 2);
    EXPECT_EQ(s.monsters[0].intent, static_cast<uint8_t>(MonsterIntent::ATTACK));

    // Bash: 8 damage at A2+ (GremlinTsundere.java:62), then a QUEUED
    // SetMoveAction re-asserting Bash -- it never returns to Protect.
    const int16_t hp_before = s.player_hp;
    gremlin_tsundere_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.player_hp, hp_before - 8);
    EXPECT_EQ(s.monsters[0].move_history[0], 2);
    EXPECT_EQ(s.ai_rng.counter, after_init);
}

// aliveCount counts the Tsundere ITSELF (GremlinTsundere.java:74-78), so exactly
// one live ally keeps it protecting and zero live allies flips it to Bash. A
// dying ally is excluded from BOTH the aliveCount and the valid-target list.
TEST(GremlinTsundere, DeadAlliesLeaveTheCountAndTheTargetList) {
    const MonsterId gang[] = {MonsterId::GREMLIN_TSUNDERE,
                              MonsterId::GREMLIN_THIEF,
                              MonsterId::GREMLIN_FAT};
    CombatState s = MakeState(7, 0);
    spawn_group(s, gang);

    // Kill slot 2: aliveCount is 2 (self + slot 1) -> still Protect, and slot 1
    // is the only valid target, so the pick is forced (random(0) still draws).
    s.monsters[2].hp = 0;
    const int32_t before = s.ai_rng.counter;
    gremlin_tsundere_take_turn(s, 0);
    EXPECT_EQ(s.ai_rng.counter, before + 1);
    drain(s);
    EXPECT_EQ(s.monsters[1].block, 11);
    EXPECT_EQ(s.monsters[2].block, 0);
    EXPECT_EQ(s.monsters[0].block, 0);
    EXPECT_EQ(s.monsters[0].move_history[0], 1) << "one live ally -> keep Protect";

    // Now kill slot 1 too: aliveCount is 1 -> Bash, and the empty valid list
    // sends the block back to the source with no draw.
    s.monsters[1].hp = 0;
    s.monsters[1].block = 0;
    const int32_t before2 = s.ai_rng.counter;
    gremlin_tsundere_take_turn(s, 0);
    EXPECT_EQ(s.ai_rng.counter, before2);
    drain(s);
    EXPECT_EQ(s.monsters[0].block, 11);
    EXPECT_EQ(s.monsters[0].move_history[0], 2);
}

// The Protect block is a direct addBlock (GainBlockRandomMonsterAction.java:38),
// so it must NOT run the card block-modifier pass -- a Frail-style multiplier
// would apply otherwise. Pinned via the queued item's kBlockNoPowers flag.
TEST(GremlinTsundere, ProtectQueuesADirectBlockGain) {
    const MonsterId gang[] = {MonsterId::GREMLIN_TSUNDERE,
                              MonsterId::GREMLIN_THIEF};
    CombatState s = MakeState(11, 0);
    spawn_group(s, gang);
    gremlin_tsundere_take_turn(s, 0);
    ASSERT_EQ(s.action_count, 1);
    const ActionQueueItem& it = s.action_queue[s.action_head];
    EXPECT_EQ(it.opcode, static_cast<uint16_t>(Opcode::BLOCK));
    EXPECT_EQ(it.tgt, 1);
    EXPECT_EQ(it.amount, 11);
    EXPECT_NE(it.flags & kBlockNoPowers, 0u);
}

// --- Angry (GremlinWarrior's pre-battle power) -------------------------------

TEST(GremlinWarrior, PreBattleAngryStackAndStrengthOnBeingHit) {
    CombatState s = MakeState(5150, 1);
    gremlin_warrior_init(s, 0);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::GREMLIN_WARRIOR),
              &gremlin_warrior_use_pre_battle_action);

    const int32_t hp_draws = s.monster_hp_rng.counter;
    gremlin_warrior_use_pre_battle_action(s, 0);
    ASSERT_EQ(s.action_count, 1) << "usePreBattleAction queues one ApplyPower";
    EXPECT_EQ(s.monster_hp_rng.counter, hp_draws) << "Angry draws no RNG";
    drain(s);
    // AngryPower(this, ascension >= 17 ? 2 : 1) -- 2 at A20.
    EXPECT_EQ(monster_power(s, 0, PowerId::ANGRY), 2);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), -1);

    // A real hit: +2 Strength, and it keeps stacking hit after hit.
    const int16_t hp0 = s.monsters[0].hp;
    player_attacks(s, 0, 3);
    EXPECT_EQ(s.monsters[0].hp, hp0 - 3);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), 2);
    player_attacks(s, 0, 2);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), 4);
}

// AngryPower.onAttacked's `damageAmount > 0` guard reads the POST-block damage,
// so a hit the gremlin fully blocks grants nothing. (Curl Up, by contrast, has
// no such symmetry -- Thorns fires on a fully blocked hit; Angry does not.)
TEST(GremlinWarrior, AngryDoesNotTriggerOnAFullyBlockedHit) {
    CombatState s = MakeState(5151, 1);
    gremlin_warrior_init(s, 0);
    gremlin_warrior_use_pre_battle_action(s, 0);
    drain(s);
    s.monsters[0].block = 10;
    const int16_t hp0 = s.monsters[0].hp;
    player_attacks(s, 0, 6);
    EXPECT_EQ(s.monsters[0].hp, hp0);
    EXPECT_EQ(s.monsters[0].block, 4);
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), -1);
}

// The Strength Angry grants scales the very next Scratch (5 base at A2+).
TEST(GremlinWarrior, ScratchDamageAndStrengthScaling) {
    CombatState s = MakeState(5152, 1);
    gremlin_warrior_init(s, 0);
    ASSERT_EQ(s.monsters[0].move_history[0], 1);
    ASSERT_EQ(s.monsters[0].intent, static_cast<uint8_t>(MonsterIntent::ATTACK));

    const int16_t hp0 = s.player_hp;
    gremlin_warrior_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.player_hp, hp0 - 5);
    EXPECT_EQ(s.monsters[0].move_history[0], 1) << "queued SetMoveAction (:82)";

    gremlin_warrior_use_pre_battle_action(s, 0);
    drain(s);
    player_attacks(s, 0, 1);
    ASSERT_EQ(monster_power(s, 0, PowerId::STRENGTH), 2);
    const int16_t hp1 = s.player_hp;
    gremlin_warrior_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.player_hp, hp1 - 7);  // 5 base + 2 Strength
}

// --- Thief / Fat / Wizard turn bodies ---------------------------------------

TEST(GremlinThief, PunctureDamageAndFixedTelegraph) {
    CombatState s = MakeState(3131, 1);
    gremlin_thief_init(s, 0);
    ASSERT_EQ(s.monsters[0].move_history[0], 1);
    for (int turn = 0; turn < 3; ++turn) {
        const int16_t hp = s.player_hp;
        gremlin_thief_take_turn(s, 0);
        drain(s);
        EXPECT_EQ(s.player_hp, hp - 10) << "thiefDamage 10 at A2+ (:52)";
        EXPECT_EQ(s.monsters[0].move_history[0], 1);
        EXPECT_EQ(s.ai_rng.counter, 1) << "no RollMoveAction";
    }
}

TEST(GremlinFat, BluntQueuesDamageWeakFrailThenRolls) {
    CombatState s = MakeState(1818, 1);
    gremlin_fat_init(s, 0);
    ASSERT_EQ(s.monsters[0].move_history[0], 2);
    ASSERT_EQ(s.monsters[0].intent,
              static_cast<uint8_t>(MonsterIntent::ATTACK_DEBUFF));

    gremlin_fat_take_turn(s, 0);
    // addToBottom order (GremlinFat.java:70-80): Damage, Weak, Frail (A17+),
    // RollMove.
    ASSERT_EQ(s.action_count, 4);
    const auto at = [&s](uint8_t i) {
        return s.action_queue[(s.action_head + i) % kActionQueueCap];
    };
    EXPECT_EQ(at(0).opcode, static_cast<uint16_t>(Opcode::DAMAGE));
    EXPECT_EQ(at(0).amount, 5);  // A2+ blunt damage (:57)
    EXPECT_EQ(at(1).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER));
    EXPECT_EQ(apply_power_id_from_flags(at(1).flags), PowerId::WEAK);
    EXPECT_EQ(at(2).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER));
    EXPECT_EQ(apply_power_id_from_flags(at(2).flags), PowerId::FRAIL);
    EXPECT_EQ(at(3).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE));

    const int16_t hp = s.player_hp;
    const int32_t ai_before = s.ai_rng.counter;
    drain(s);
    EXPECT_EQ(s.player_hp, hp - 5);
    EXPECT_EQ(player_power(s, PowerId::WEAK), 1);
    EXPECT_EQ(player_power(s, PowerId::FRAIL), 1);
    EXPECT_EQ(s.ai_rng.counter, ai_before + 1) << "the queued RollMoveAction";
    EXPECT_EQ(s.monsters[0].move_history[0], 2) << "getMove is unconditional";
}

// The ROLL_MOVE item Fat queues must actually reach gremlin_fat_roll_move --
// a monster with no registered roll fn would drain it as a silent no-op.
TEST(GremlinFat, RegistersAQueuedRollMoveBody) {
    EXPECT_EQ(monster_roll_move_fn(MonsterId::GREMLIN_FAT),
              &gremlin_fat_roll_move);
    for (MonsterId id : {MonsterId::GREMLIN_WARRIOR, MonsterId::GREMLIN_THIEF,
                         MonsterId::GREMLIN_TSUNDERE,
                         MonsterId::GREMLIN_WIZARD}) {
        EXPECT_EQ(monster_roll_move_fn(id), nullptr);
    }
}

// currentCharge starts at 1 and CHARGE_LIMIT is 3, so the wizard charges twice
// and then blasts; at A17+ the blast re-telegraphs itself, so it blasts every
// turn from then on (GremlinWizard.java:69-98).
TEST(GremlinWizard, ChargeTwiceThenBlastEveryTurn) {
    CombatState s = MakeState(2727, 1);
    gremlin_wizard_init(s, 0);
    EXPECT_EQ(s.monsters[0].pad0, 1) << "currentCharge field initialiser (:43)";
    ASSERT_EQ(s.monsters[0].move_history[0], 2);
    ASSERT_EQ(s.monsters[0].intent, static_cast<uint8_t>(MonsterIntent::UNKNOWN));

    const int kExpected[] = {2, 2, 1, 1, 1, 1};
    int16_t hp = s.player_hp;
    for (int turn = 0; turn < 6; ++turn) {
        EXPECT_EQ(static_cast<int>(s.monsters[0].move_history[0]),
                  kExpected[turn])
            << "turn " << (turn + 1);
        gremlin_wizard_take_turn(s, 0);
        drain(s);
        if (kExpected[turn] == 1) {
            EXPECT_EQ(s.player_hp, hp - 30) << "A2+ magic damage (:57)";
            EXPECT_EQ(s.monsters[0].pad0, 0) << "currentCharge reset (:86)";
        } else {
            EXPECT_EQ(s.player_hp, hp) << "charging deals nothing";
        }
        hp = s.player_hp;
    }
    EXPECT_EQ(s.ai_rng.counter, 1) << "the wizard never rolls after init()";
}

// --- Registry pins ----------------------------------------------------------

TEST(GremlinRegistry, StatColumnsMatchTheJava) {
    struct Row {
        MonsterId id;
        const sts::registry::MonsterDef* def;
        const char* game_id;
        int base_lo, base_hi, a20_lo, a20_hi;
    };
    const Row kRows[] = {
        {MonsterId::GREMLIN_WARRIOR, &sts::registry::kGremlinWarrior,
         "GremlinWarrior", 20, 24, 21, 25},
        {MonsterId::GREMLIN_THIEF, &sts::registry::kGremlinThief,
         "GremlinThief", 10, 14, 11, 15},
        {MonsterId::GREMLIN_FAT, &sts::registry::kGremlinFat,
         "GremlinFat", 13, 17, 14, 18},
        {MonsterId::GREMLIN_TSUNDERE, &sts::registry::kGremlinTsundere,
         "GremlinTsundere", 12, 15, 13, 17},
        {MonsterId::GREMLIN_WIZARD, &sts::registry::kGremlinWizard,
         "GremlinWizard", 21, 25, 22, 26},
    };
    for (const Row& row : kRows) {
        EXPECT_EQ(sts::registry::monster_def(row.id), row.def) << row.game_id;
        EXPECT_EQ(row.def->hp_min(0), row.base_lo) << row.game_id;
        EXPECT_EQ(row.def->hp_max(0), row.base_hi) << row.game_id;
        EXPECT_EQ(row.def->hp_min(kA20), row.a20_lo) << row.game_id;
        EXPECT_EQ(row.def->hp_max(kA20), row.a20_hi) << row.game_id;
        EXPECT_TRUE(row.def->ai_native) << row.game_id;
        EXPECT_EQ(row.def->roll_count, 0) << row.game_id
            << ": no gremlin rolls a per-instance stat";
        // Move 99 (ESCAPE) is unreachable in Act 1 and deliberately absent.
        EXPECT_EQ(row.def->move(99), nullptr) << row.game_id;
        EXPECT_EQ(static_cast<uint16_t>(
                      sts::registry::monster_from_game_id(row.game_id)),
                  static_cast<uint16_t>(row.id))
            << row.game_id;
        // The B4.4 parking gate is exactly this predicate (run_advance.cpp):
        // a registered init fn is what un-parks the Gremlin Gang encounter.
        EXPECT_NE(monster_init_fn(row.id), nullptr) << row.game_id;
        EXPECT_NE(monster_turn_fn(row.id), &default_monster_turn) << row.game_id;
    }
}

TEST(GremlinRegistry, MoveIntentsAndDamageColumns) {
    using sts::registry::MonsterMove;
    const MonsterMove* scratch = sts::registry::kGremlinWarrior.move(1);
    ASSERT_NE(scratch, nullptr);
    EXPECT_EQ(scratch->intent, MonsterIntent::ATTACK);
    EXPECT_EQ(scratch->effects[0].amount.at(0), 4);
    EXPECT_EQ(scratch->effects[0].amount.at(kA20), 5);

    const MonsterMove* puncture = sts::registry::kGremlinThief.move(1);
    ASSERT_NE(puncture, nullptr);
    EXPECT_EQ(puncture->intent, MonsterIntent::ATTACK);
    EXPECT_EQ(puncture->effects[0].amount.at(0), 9);
    EXPECT_EQ(puncture->effects[0].amount.at(kA20), 10);

    const MonsterMove* blunt = sts::registry::kGremlinFat.move(2);
    ASSERT_NE(blunt, nullptr);
    EXPECT_EQ(blunt->intent, MonsterIntent::ATTACK_DEBUFF);
    EXPECT_EQ(blunt->effect_count, 2) << "Frail is the A17-gated native step";
    EXPECT_EQ(blunt->effects[0].amount.at(0), 4);
    EXPECT_EQ(blunt->effects[0].amount.at(kA20), 5);

    const MonsterMove* protect = sts::registry::kGremlinTsundere.move(1);
    ASSERT_NE(protect, nullptr);
    EXPECT_EQ(protect->intent, MonsterIntent::DEFEND);
    EXPECT_EQ(protect->effects[0].op, sts::registry::Opcode::BLOCK);
    EXPECT_EQ(protect->effects[0].amount.at(0), 7);    // :60
    EXPECT_EQ(protect->effects[0].amount.at(7), 8);    // :57
    EXPECT_EQ(protect->effects[0].amount.at(kA20), 11);  // :54

    const MonsterMove* bash = sts::registry::kGremlinTsundere.move(2);
    ASSERT_NE(bash, nullptr);
    EXPECT_EQ(bash->intent, MonsterIntent::ATTACK);
    EXPECT_EQ(bash->effects[0].amount.at(0), 6);
    EXPECT_EQ(bash->effects[0].amount.at(kA20), 8);

    const MonsterMove* blast = sts::registry::kGremlinWizard.move(1);
    ASSERT_NE(blast, nullptr);
    EXPECT_EQ(blast->intent, MonsterIntent::ATTACK);
    EXPECT_EQ(blast->effects[0].amount.at(0), 25);
    EXPECT_EQ(blast->effects[0].amount.at(kA20), 30);

    const MonsterMove* charging = sts::registry::kGremlinWizard.move(2);
    ASSERT_NE(charging, nullptr);
    EXPECT_EQ(charging->intent, MonsterIntent::UNKNOWN);
    EXPECT_EQ(charging->effects[0].op, sts::registry::Opcode::NOP);
}

}  // namespace
}  // namespace sts::engine
