// Tier-2 Act-1 elite coverage: Gremlin Nob + Sentry.
//
// Three independent 32-seed x 20-turn fixtures (tests/fixtures/
// gen_elite_fixture.py) pin HP, move history, full aiRng state and the logical
// draw counter for the Nob and for a Sentry in an even and an odd slot. Focused
// tests pin the registry ascension columns, Anger's SKILL-only trigger, Skull
// Bash's damage-then-Vulnerable order, the Sentry's pre-battle Artifact and its
// debuff nullify, Bolt's Dazed-into-discard count, and the whole 3-Sentry group
// from encounter composition through spawn to alternating telegraphs.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled, read in full):
//   GremlinNob.java:56-84 (ctor / ascension columns / canVuln), :86-113
//     (takeTurn: Anger 3 at A18, Skull Bash damage then Vulnerable 2, Bull Rush),
//     :126-170 (getMove: forced Bellow, then the A18 history tree);
//   Sentry.java:59-77 (ctor / ascension columns), :79-82 (usePreBattleAction
//     Artifact 1), :84-113 (takeTurn: Bolt makes dazedAmt Dazed in the DISCARD
//     pile, Beam deals damage[0]), :134-150 (getMove: slot-parity first move,
//     then strict alternation);
//   AngerPower.java:39-45 (onUseCard, SKILL only, addToTop Strength);
//   ArtifactPower.java:34-44 + ApplyPowerAction.java:131-138 (the DEBUFF nullify
//     that consumes a stack);
//   UseCardAction.java:41-65 (monster powers are the LAST stage of the fan-out);
//   MonsterHelper.java:436-444 ("Gremlin Nob" uses the canVuln 2-arg ctor;
//     "3 Sentries" builds three Sentries in slot order);
//   AbstractMonster.java:431-491, 465-467, 712-715, 765-775.

#include <cstdint>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_gremlin_nob.hpp"
#include "sts/engine/monster_sentry.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/monster_table.hpp"

#ifndef STS_FIXTURE_DIR
#error "STS_FIXTURE_DIR must be defined by tests/CMakeLists.txt"
#endif

namespace sts::engine {
namespace {

// Move ids, read symbolically from the generated table (never as literals).
constexpr uint8_t kBullRush = sts::registry::kGremlinNobMoveBullRush;
constexpr uint8_t kSkullBash = sts::registry::kGremlinNobMoveSkullBash;
constexpr uint8_t kBellow = sts::registry::kGremlinNobMoveBellow;
constexpr uint8_t kBolt = sts::registry::kSentryMoveBolt;
constexpr uint8_t kBeam = sts::registry::kSentryMoveBeam;

constexpr int kTurns = 20;

// The engine's fixed S1 difficulty; every expected number below is the column
// this ascension resolves to.
constexpr int32_t kA = kMonsterAscension;

// --- Fixture plumbing (same TSV shape as the slime batteries) ----------------

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

// A state whose two monster streams are independently seeded, with `slot_count`
// monster records so a Sentry can be driven from a non-zero slot.
CombatState make_state(int64_t seed, uint8_t slot_count) {
    CombatState s{};
    s.player_hp = 400;
    s.player_max_hp = 400;
    s.monster_count = slot_count;
    s.monster_hp_rng = from_seed(seed);
    s.ai_rng = from_seed(seed);
    return s;
}

void drain(CombatState& s) {
    while (s.action_count > 0) {
        const ActionQueueItem item = s.action_queue[s.action_head];
        s.action_head = static_cast<uint8_t>((s.action_head + 1) % kActionQueueCap);
        --s.action_count;
        execute_opcode(s, item);
    }
}

using InitFn = void (*)(CombatState&, uint8_t);
using TurnFn = void (*)(CombatState&, uint8_t);

// Replay one fixture file against the engine. `slot` is the monster record the
// module is driven from (the Sentry's opening move keys on it).
void run_fixture(const std::string& file, MonsterId id, InitFn init,
                 TurnFn take_turn, uint8_t slot, int hp_min, int hp_max) {
    const std::vector<SeedCase> cases = load_fixture(file);
    ASSERT_EQ(cases.size(), 32u) << file << " did not load";
    for (const SeedCase& c : cases) {
        CombatState s = make_state(c.seed, static_cast<uint8_t>(slot + 1));
        init(s, slot);
        const MonsterState& m = s.monsters[slot];
        EXPECT_EQ(m.monster_id, static_cast<uint16_t>(id)) << c.label;
        EXPECT_EQ(m.hp, c.hp) << file << " " << c.label;
        EXPECT_EQ(m.max_hp, c.hp) << file << " " << c.label;
        EXPECT_GE(m.hp, hp_min) << c.label;
        EXPECT_LE(m.hp, hp_max) << c.label;
        EXPECT_EQ(s.monster_hp_rng.s0, c.hp_s0) << c.label;
        EXPECT_EQ(s.monster_hp_rng.s1, c.hp_s1) << c.label;
        EXPECT_EQ(s.monster_hp_rng.counter, c.hp_counter) << c.label;

        ASSERT_EQ(c.turns.size(), static_cast<std::size_t>(kTurns));
        for (int t = 0; t < kTurns; ++t) {
            const TurnRow& row = c.turns[static_cast<std::size_t>(t)];
            EXPECT_EQ(s.monsters[slot].move_history[0], row.move)
                << file << " " << c.label << " turn " << (t + 1);
            take_turn(s, slot);
            drain(s);
            EXPECT_EQ(s.ai_rng.s0, row.ai_s0) << c.label << " turn " << (t + 1);
            EXPECT_EQ(s.ai_rng.s1, row.ai_s1) << c.label << " turn " << (t + 1);
            EXPECT_EQ(s.ai_rng.counter, row.ai_counter)
                << c.label << " turn " << (t + 1)
                << ": rollMove must draw exactly once per turn";
            // The player is deliberately given 400 HP so 20 turns of Bull Rush
            // never kill them and the battery runs to completion.
            ASSERT_GT(s.player_hp, 0) << c.label;
        }
    }
}

const PowerSlot* power_of(const CombatState& s, uint8_t actor, PowerId id) {
    const PowerSlot* slots =
        (actor == kActorPlayer) ? s.player_powers : s.monsters[actor].powers;
    const uint8_t count =
        (actor == kActorPlayer) ? s.player_power_count : s.monsters[actor].power_count;
    for (uint8_t i = 0; i < count; ++i) {
        if (slots[i].power_id == static_cast<uint16_t>(id)) {
            return &slots[i];
        }
    }
    return nullptr;
}

int count_in_discard(const CombatState& s, CardId id) {
    int n = 0;
    for (uint8_t i = 0; i < s.discard_count; ++i) {
        if (s.card_pool[s.discard[i]].card_id == static_cast<uint16_t>(id)) {
            ++n;
        }
    }
    return n;
}

// A Gremlin Nob in slot 0 with `card` as the sole hand card at pool index 0.
CombatState make_nob_with_card(CardId card, uint8_t cost) {
    CombatState s{};
    s.card_pool[0].card_id = static_cast<uint16_t>(card);
    s.card_pool[0].cost_now = cost;
    s.hand[0] = 0;
    s.hand_count = 1;
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = 3;
    s.monster_count = 1;
    s.monster_hp_rng = from_seed(1234567);
    s.ai_rng = from_seed(1234567);
    gremlin_nob_init(s, 0);
    s.monster_attacks_queued = 1;
    s.turn_has_ended = 0;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

// =============================================================================
// 1. Registry columns -- every number resolved at the fixed difficulty and at
//    the sub-threshold ascensions, hand-read from the cited ctor branches.
// =============================================================================

TEST(EliteRegistry, GremlinNobColumns) {
    const auto& def = sts::registry::kGremlinNob;
    EXPECT_EQ(def.enemy_type, sts::registry::MonsterEnemyType::ELITE);

    // setHp: (82,86) below A8, (85,90) from A8 (GremlinNob.java:67-71).
    EXPECT_EQ(def.hp_min(0), 82);
    EXPECT_EQ(def.hp_max(0), 86);
    EXPECT_EQ(def.hp_min(7), 82);
    EXPECT_EQ(def.hp_min(8), 85);
    EXPECT_EQ(def.hp_max(8), 90);
    EXPECT_EQ(def.hp_min(kA), 85);
    EXPECT_EQ(def.hp_max(kA), 90);

    // damage[0] = rushDmg 14, 16 from A3; damage[1] = bashDmg 6, 8 from A3
    // (GremlinNob.java:72-80).
    const sts::registry::MonsterMove* rush = def.move(kBullRush);
    ASSERT_NE(rush, nullptr);
    EXPECT_EQ(rush->intent, sts::registry::MonsterIntent::ATTACK);
    ASSERT_EQ(rush->effect_count, 1);
    EXPECT_EQ(rush->effects[0].op, sts::registry::Opcode::DAMAGE);
    EXPECT_EQ(rush->effects[0].amount.at(0), 14);
    EXPECT_EQ(rush->effects[0].amount.at(2), 14);
    EXPECT_EQ(rush->effects[0].amount.at(3), 16);
    EXPECT_EQ(rush->effects[0].amount.at(kA), 16);

    const sts::registry::MonsterMove* bash = def.move(kSkullBash);
    ASSERT_NE(bash, nullptr);
    // canVuln is true for MonsterHelper's group, so the telegraph is the
    // ATTACK_DEBUFF one (GremlinNob.java:136,144).
    EXPECT_EQ(bash->intent, sts::registry::MonsterIntent::ATTACK_DEBUFF);
    ASSERT_EQ(bash->effect_count, 2);
    EXPECT_EQ(bash->effects[0].op, sts::registry::Opcode::DAMAGE);
    EXPECT_EQ(bash->effects[0].amount.at(0), 6);
    EXPECT_EQ(bash->effects[0].amount.at(3), 8);
    EXPECT_EQ(bash->effects[0].amount.at(kA), 8);
    // DEBUFF_AMT = 2, flat at every ascension (GremlinNob.java:46,103).
    EXPECT_EQ(bash->effects[1].op, sts::registry::Opcode::APPLY_POWER);
    EXPECT_EQ(bash->effects[1].extra,
              static_cast<uint32_t>(PowerId::VULNERABLE));
    EXPECT_EQ(bash->effects[1].amount.at(0), 2);
    EXPECT_EQ(bash->effects[1].amount.at(kA), 2);

    // AngerPower(this, 2), 3 from A18 (GremlinNob.java:92-96).
    const sts::registry::MonsterMove* bellow = def.move(kBellow);
    ASSERT_NE(bellow, nullptr);
    EXPECT_EQ(bellow->intent, sts::registry::MonsterIntent::BUFF);
    ASSERT_EQ(bellow->effect_count, 1);
    EXPECT_EQ(bellow->effects[0].op, sts::registry::Opcode::APPLY_POWER);
    EXPECT_EQ(bellow->effects[0].target, sts::registry::MonsterMoveTarget::SELF);
    EXPECT_EQ(bellow->effects[0].extra, static_cast<uint32_t>(PowerId::ANGER));
    EXPECT_EQ(bellow->effects[0].amount.at(0), 2);
    EXPECT_EQ(bellow->effects[0].amount.at(17), 2);
    EXPECT_EQ(bellow->effects[0].amount.at(18), 3);
    EXPECT_EQ(bellow->effects[0].amount.at(kA), 3);
}

TEST(EliteRegistry, SentryColumns) {
    const auto& def = sts::registry::kSentry;
    EXPECT_EQ(def.enemy_type, sts::registry::MonsterEnemyType::ELITE);

    // setHp: (38,42) below A8, (39,45) from A8 (Sentry.java:62-66).
    EXPECT_EQ(def.hp_min(0), 38);
    EXPECT_EQ(def.hp_max(0), 42);
    EXPECT_EQ(def.hp_min(7), 38);
    EXPECT_EQ(def.hp_min(8), 39);
    EXPECT_EQ(def.hp_max(8), 45);
    EXPECT_EQ(def.hp_min(kA), 39);
    EXPECT_EQ(def.hp_max(kA), 45);

    // beamDmg 9, 10 from A3 (Sentry.java:67).
    const sts::registry::MonsterMove* beam = def.move(kBeam);
    ASSERT_NE(beam, nullptr);
    EXPECT_EQ(beam->intent, sts::registry::MonsterIntent::ATTACK);
    ASSERT_EQ(beam->effect_count, 1);
    EXPECT_EQ(beam->effects[0].op, sts::registry::Opcode::DAMAGE);
    EXPECT_EQ(beam->effects[0].amount.at(0), 9);
    EXPECT_EQ(beam->effects[0].amount.at(2), 9);
    EXPECT_EQ(beam->effects[0].amount.at(3), 10);
    EXPECT_EQ(beam->effects[0].amount.at(kA), 10);

    // dazedAmt 2, 3 from A18, into the DISCARD pile (Sentry.java:68,96).
    const sts::registry::MonsterMove* bolt = def.move(kBolt);
    ASSERT_NE(bolt, nullptr);
    EXPECT_EQ(bolt->intent, sts::registry::MonsterIntent::DEBUFF);
    ASSERT_EQ(bolt->effect_count, 1);
    EXPECT_EQ(bolt->effects[0].op, sts::registry::Opcode::MAKE_CARD);
    EXPECT_EQ(bolt->effects[0].extra & 0xFFFFu,
              static_cast<uint32_t>(CardId::DAZED));
    EXPECT_EQ((bolt->effects[0].extra >> 16) & 0xFFu,
              static_cast<uint32_t>(CardPile::DISCARD));
    EXPECT_EQ(bolt->effects[0].amount.at(0), 2);
    EXPECT_EQ(bolt->effects[0].amount.at(17), 2);
    EXPECT_EQ(bolt->effects[0].amount.at(18), 3);
    EXPECT_EQ(bolt->effects[0].amount.at(kA), 3);
}

TEST(EliteRegistry, GameIdsJoin) {
    EXPECT_EQ(sts::registry::monster_from_game_id("GremlinNob"),
              MonsterId::GREMLIN_NOB);
    EXPECT_EQ(sts::registry::monster_from_game_id("Sentry"),
              MonsterId::SENTRY);
}

// =============================================================================
// 2. Independent fixture batteries (move selection + RNG accounting)
// =============================================================================

TEST(EliteFixtures, GremlinNobMatchesIndependentOracle) {
    run_fixture("gremlin_nob_fixture.tsv", MonsterId::GREMLIN_NOB,
                &gremlin_nob_init, &gremlin_nob_take_turn, 0, 85, 90);
}

TEST(EliteFixtures, SentryEvenSlotMatchesIndependentOracle) {
    run_fixture("sentry_slot0_fixture.tsv", MonsterId::SENTRY, &sentry_init,
                &sentry_take_turn, 0, 39, 45);
}

TEST(EliteFixtures, SentryOddSlotMatchesIndependentOracle) {
    run_fixture("sentry_slot1_fixture.tsv", MonsterId::SENTRY, &sentry_init,
                &sentry_take_turn, 1, 39, 45);
}

// =============================================================================
// 3. Gremlin Nob behaviour
// =============================================================================

// getMove: the very first decision is a forced Bellow, then the A18 branch's
// history tree yields Skull Bash, Bull Rush, Bull Rush, Skull Bash, ...
// (GremlinNob.java:128-150). Nothing here reads the drawn num.
TEST(EliteGremlinNob, A18MoveCycleIsHistoryDriven) {
    CombatState s = make_state(999, 1);
    gremlin_nob_init(s, 0);
    static constexpr uint8_t kExpected[] = {
        kBellow,   kSkullBash, kBullRush, kBullRush, kSkullBash, kBullRush,
        kBullRush, kSkullBash, kBullRush, kBullRush, kSkullBash, kBullRush,
    };
    for (uint8_t expected : kExpected) {
        EXPECT_EQ(s.monsters[0].move_history[0], expected);
        gremlin_nob_take_turn(s, 0);
        drain(s);
    }
    // The Nob never repeats Skull Bash back-to-back: `!lastMove(2)` gates it.
    EXPECT_EQ(s.ai_rng.counter, 1 + static_cast<int32_t>(std::size(kExpected)));
}

TEST(EliteGremlinNob, BellowAppliesTheA18AngerAmount) {
    CombatState s = make_state(42, 1);
    gremlin_nob_init(s, 0);
    ASSERT_EQ(s.monsters[0].move_history[0], kBellow);
    gremlin_nob_take_turn(s, 0);
    drain(s);
    const PowerSlot* anger = power_of(s, 0, PowerId::ANGER);
    ASSERT_NE(anger, nullptr) << "Bellow must apply Anger to the Nob itself";
    EXPECT_EQ(anger->amount, 3) << "ANGRY_LEVEL is 3 from A18 (GremlinNob.java:93)";
    EXPECT_EQ(power_of(s, kActorPlayer, PowerId::ANGER), nullptr)
        << "Anger targets SELF, never the player";
}

// The acceptance case: Anger fires on SKILL plays and on nothing else. Defend is
// the SKILL, Strike the ATTACK, Metallicize the POWER (chosen over Inflame
// because Inflame's own effect is player Strength, which would blur the
// "the Strength goes to the OWNER" assertion below).
TEST(EliteGremlinNob, AngerTriggersOnSkillPlaysOnly) {
    struct Case { CardId card; uint8_t cost; CardType type; int expect_str; };
    static const Case kCases[] = {
        {CardId::DEFEND,      1, CardType::SKILL,  3},
        {CardId::STRIKE,      1, CardType::ATTACK, 0},
        {CardId::METALLICIZE, 1, CardType::POWER,  0},
    };
    for (const Case& c : kCases) {
        CombatState s = make_nob_with_card(c.card, c.cost);
        ASSERT_EQ(card_def(c.card)->type, c.type)
            << "the case table's card type must match the registry";
        // Give the Nob the A18 Anger stack directly, so this test isolates the
        // trigger from the Bellow turn that would otherwise grant it.
        s.monsters[0].powers[0].power_id = static_cast<uint16_t>(PowerId::ANGER);
        s.monsters[0].powers[0].amount = 3;
        s.monsters[0].power_count = 1;

        ASSERT_TRUE(queue_card_play(s, 0, 0));
        pump(s);

        const PowerSlot* str = power_of(s, 0, PowerId::STRENGTH);
        const int got = (str == nullptr) ? 0 : str->amount;
        EXPECT_EQ(got, c.expect_str)
            << "AngerPower.onUseCard's `card.type == SKILL` guard "
               "(AngerPower.java:40)";
        EXPECT_EQ(power_of(s, kActorPlayer, PowerId::STRENGTH), nullptr)
            << "the Strength goes to the power's OWNER, not the player";
    }
}

// Two skills in one turn stack Anger's Strength twice (each UseCardAction runs
// the monster power-list fan-out again).
TEST(EliteGremlinNob, AngerStacksPerSkillPlayed) {
    CombatState s = make_nob_with_card(CardId::DEFEND, 1);
    s.card_pool[1].card_id = static_cast<uint16_t>(CardId::DEFEND);
    s.card_pool[1].cost_now = 1;
    s.hand[1] = 1;
    s.hand_count = 2;
    s.monsters[0].powers[0].power_id = static_cast<uint16_t>(PowerId::ANGER);
    s.monsters[0].powers[0].amount = 3;
    s.monsters[0].power_count = 1;

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    const PowerSlot* str = power_of(s, 0, PowerId::STRENGTH);
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->amount, 6);
}

TEST(EliteGremlinNob, SkullBashDamagesThenAppliesVulnerable) {
    CombatState s = make_state(77, 1);
    s.player_hp = 200;
    s.player_max_hp = 200;
    gremlin_nob_init(s, 0);
    // Advance to the Skull Bash decision (the Bellow turn is first).
    gremlin_nob_take_turn(s, 0);
    drain(s);
    ASSERT_EQ(s.monsters[0].move_history[0], kSkullBash);

    const int16_t before = s.player_hp;
    gremlin_nob_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(before - s.player_hp, 8)
        << "bashDmg is 8 from A3 (GremlinNob.java:73)";
    const PowerSlot* vuln = power_of(s, kActorPlayer, PowerId::VULNERABLE);
    ASSERT_NE(vuln, nullptr);
    EXPECT_EQ(vuln->amount, 2) << "DEBUFF_AMT (GremlinNob.java:46,103)";
}

TEST(EliteGremlinNob, BullRushDealsTheA3RushDamage) {
    CombatState s = make_state(77, 1);
    s.player_hp = 200;
    s.player_max_hp = 200;
    gremlin_nob_init(s, 0);
    for (int i = 0; i < 2; ++i) {  // Bellow, then Skull Bash
        gremlin_nob_take_turn(s, 0);
        drain(s);
    }
    ASSERT_EQ(s.monsters[0].move_history[0], kBullRush);
    // Vulnerable from the preceding Skull Bash is still on the player, so clear
    // it to read the raw base damage rather than the 1.5x multiple.
    s.player_power_count = 0;
    const int16_t before = s.player_hp;
    gremlin_nob_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(before - s.player_hp, 16)
        << "rushDmg is 16 from A3 (GremlinNob.java:74)";
}

// =============================================================================
// 4. Sentry behaviour
// =============================================================================

TEST(EliteSentry, PreBattleGrantsExactlyOneArtifactAndDrawsNoRng) {
    CombatState s = make_state(31337, 1);
    sentry_init(s, 0);
    const int32_t hp_counter = s.monster_hp_rng.counter;
    const int32_t ai_counter = s.ai_rng.counter;

    sentry_use_pre_battle_action(s, 0);
    drain(s);

    const PowerSlot* art = power_of(s, 0, PowerId::ARTIFACT);
    ASSERT_NE(art, nullptr);
    EXPECT_EQ(art->amount, 1) << "new ArtifactPower(this, 1) (Sentry.java:81)";
    EXPECT_EQ(s.monster_hp_rng.counter, hp_counter)
        << "usePreBattleAction draws no RNG (contrast the Louse curl-up roll)";
    EXPECT_EQ(s.ai_rng.counter, ai_counter);
}

// The Artifact stack nullifies the first debuff aimed at the Sentry and is spent
// doing it (ApplyPowerAction.java:131-138); the second debuff lands.
TEST(EliteSentry, ArtifactNullifiesTheFirstDebuffOnly) {
    CombatState s = make_state(31337, 1);
    sentry_init(s, 0);
    sentry_use_pre_battle_action(s, 0);
    drain(s);

    ActionQueueItem vuln{};
    vuln.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    vuln.src = kActorPlayer;
    vuln.tgt = 0;
    vuln.amount = 2;
    vuln.flags = make_apply_power_flags(PowerId::VULNERABLE);
    add_to_bottom(s, vuln);
    drain(s);
    EXPECT_EQ(power_of(s, 0, PowerId::VULNERABLE), nullptr)
        << "the first debuff is nullified";

    add_to_bottom(s, vuln);
    drain(s);
    const PowerSlot* landed = power_of(s, 0, PowerId::VULNERABLE);
    ASSERT_NE(landed, nullptr) << "Artifact was consumed, so this one lands";
    EXPECT_EQ(landed->amount, 2);
}

TEST(EliteSentry, BoltMakesTheA18DazedCountInTheDiscardPile) {
    CombatState s = make_state(555, 1);
    sentry_init(s, 0);
    ASSERT_EQ(s.monsters[0].move_history[0], kBolt) << "slot 0 opens on Bolt";
    sentry_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(count_in_discard(s, CardId::DAZED), 3)
        << "A_18_DAZED_AMT (Sentry.java:56,68)";
    EXPECT_EQ(s.hand_count, 0)
        << "MakeTempCardInDiscardAction goes to the DISCARD pile, not the hand";
}

TEST(EliteSentry, BeamDealsTheA3BeamDamage) {
    CombatState s = make_state(555, 2);
    sentry_init(s, 1);  // odd slot opens on Beam
    ASSERT_EQ(s.monsters[1].move_history[0], kBeam);
    const int16_t before = s.player_hp;
    sentry_take_turn(s, 1);
    drain(s);
    EXPECT_EQ(before - s.player_hp, 10) << "beamDmg is 10 from A3 (Sentry.java:67)";
}

// The acceptance case: the opening move is decided by the monster's POSITION,
// and each Sentry then strictly alternates from its own opening
// (Sentry.java:136-149).
TEST(EliteSentry, AlternatingMovesKeyOnGroupPosition) {
    CombatState s = make_state(2468, 3);
    for (uint8_t i = 0; i < 3; ++i) {
        sentry_init(s, i);
    }
    static constexpr uint8_t kOpening[3] = {kBolt, kBeam, kBolt};
    for (uint8_t i = 0; i < 3; ++i) {
        EXPECT_EQ(s.monsters[i].move_history[0], kOpening[i])
            << "slot " << static_cast<int>(i)
            << ": lastIndexOf(this) % 2 == 0 -> Bolt (Sentry.java:137)";
    }
    // Six further turns: every Sentry flips every turn, and the even/odd pair
    // stays exactly out of phase.
    for (int t = 0; t < 6; ++t) {
        for (uint8_t i = 0; i < 3; ++i) {
            const uint8_t before = s.monsters[i].move_history[0];
            sentry_take_turn(s, i);
            drain(s);
            const uint8_t after = s.monsters[i].move_history[0];
            EXPECT_NE(after, before) << "Bolt and Beam strictly alternate";
            EXPECT_EQ(after, before == kBeam ? kBolt : kBeam);
        }
        EXPECT_EQ(s.monsters[0].move_history[0], s.monsters[2].move_history[0]);
        EXPECT_NE(s.monsters[0].move_history[0], s.monsters[1].move_history[0]);
    }
}

// =============================================================================
// 5. Encounter -> spawn wiring (the B3.12 composition programs)
// =============================================================================

// "3 Sentries" resolves to three Sentry members with no miscRng draw, and
// spawning that group gives three ELITE records whose HP rolls consume
// monsterHpRng in slot order and whose opening telegraphs alternate by slot.
TEST(EliteEncounters, ThreeSentriesComposeAndSpawn) {
    RngStream misc = from_seed(8080);
    ResolvedGroup g{};
    ASSERT_TRUE(resolve_encounter("3 Sentries", misc, g));
    ASSERT_EQ(g.count, 3);
    EXPECT_EQ(misc.counter, 0)
        << "the composition is three fixed emits -- no miscRng draw "
           "(MonsterHelper.java:442-444)";
    MonsterId ids[3]{};
    for (uint8_t i = 0; i < 3; ++i) {
        EXPECT_EQ(g.members[i], "Sentry");
        ids[i] = static_cast<MonsterId>(
            sts::registry::monster_from_game_id(g.members[i]));
        ASSERT_EQ(ids[i], MonsterId::SENTRY);
        ASSERT_NE(monster_init_fn(ids[i]), nullptr)
            << "an unimplemented member would park the run instead";
    }

    // Reference HP rolls off an independent copy of the same stream, in the same
    // spawn order (the ctor phase of monsterHpRng).
    RngStream ref = from_seed(4242);
    const int32_t expect_hp[3] = {
        random(ref, sts::registry::kSentry.hp_min(kA),
               sts::registry::kSentry.hp_max(kA)),
        random(ref, sts::registry::kSentry.hp_min(kA),
               sts::registry::kSentry.hp_max(kA)),
        random(ref, sts::registry::kSentry.hp_min(kA),
               sts::registry::kSentry.hp_max(kA)),
    };

    CombatState s{};
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.monster_hp_rng = from_seed(4242);
    s.ai_rng = from_seed(4242);
    spawn_group(s, std::span<const MonsterId>(ids, 3));
    use_pre_battle_actions(s);
    drain(s);

    ASSERT_EQ(s.monster_count, 3);
    static constexpr uint8_t kOpening[3] = {kBolt, kBeam, kBolt};
    for (uint8_t i = 0; i < 3; ++i) {
        EXPECT_EQ(s.monsters[i].monster_id,
                  static_cast<uint16_t>(MonsterId::SENTRY));
        EXPECT_EQ(s.monsters[i].hp, expect_hp[i]) << "slot " << static_cast<int>(i);
        const PowerSlot* art = power_of(s, i, PowerId::ARTIFACT);
        ASSERT_NE(art, nullptr) << "each Sentry gets its own Artifact";
        EXPECT_EQ(art->amount, 1);
        EXPECT_EQ(s.monsters[i].move_history[0], kOpening[i]);
    }
    EXPECT_EQ(s.monster_hp_rng.counter, 3)
        << "exactly one setHp draw per Sentry, no pre-battle draw";
    EXPECT_EQ(s.ai_rng.counter, 3) << "one rollMove per Sentry at init()";
}

TEST(EliteEncounters, GremlinNobComposesAndSpawns) {
    RngStream misc = from_seed(8080);
    ResolvedGroup g{};
    ASSERT_TRUE(resolve_encounter("Gremlin Nob", misc, g));
    ASSERT_EQ(g.count, 1);
    EXPECT_EQ(g.members[0], "GremlinNob");
    EXPECT_EQ(misc.counter, 0);

    const MonsterId id = static_cast<MonsterId>(
        sts::registry::monster_from_game_id(g.members[0]));
    ASSERT_EQ(id, MonsterId::GREMLIN_NOB);
    ASSERT_NE(monster_init_fn(id), nullptr);

    CombatState s{};
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.monster_hp_rng = from_seed(4242);
    s.ai_rng = from_seed(4242);
    spawn_group(s, std::span<const MonsterId>(&id, 1));
    use_pre_battle_actions(s);
    drain(s);

    ASSERT_EQ(s.monster_count, 1);
    EXPECT_GE(s.monsters[0].hp, 85);
    EXPECT_LE(s.monsters[0].hp, 90);
    EXPECT_EQ(s.monsters[0].move_history[0], kBellow);
    EXPECT_EQ(s.monsters[0].power_count, 0)
        << "GremlinNob does not override usePreBattleAction";
    EXPECT_EQ(s.monster_hp_rng.counter, 1);
    EXPECT_EQ(s.ai_rng.counter, 1);
}

// The two elites reach their own turn functions through the id dispatch table
// (a wrong wiring here would silently run default_monster_turn).
TEST(EliteDispatch, TurnFunctionsAreRegistered) {
    EXPECT_EQ(monster_init_fn(MonsterId::GREMLIN_NOB), &gremlin_nob_init);
    EXPECT_EQ(monster_turn_fn(MonsterId::GREMLIN_NOB), &gremlin_nob_take_turn);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::GREMLIN_NOB), nullptr);
    EXPECT_EQ(monster_init_fn(MonsterId::SENTRY), &sentry_init);
    EXPECT_EQ(monster_turn_fn(MonsterId::SENTRY), &sentry_take_turn);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::SENTRY),
              &sentry_use_pre_battle_action);
}

}  // namespace
}  // namespace sts::engine
