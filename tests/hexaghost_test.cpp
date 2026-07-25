// Tier-2 Hexaghost coverage (B3.22).
//
// Named cases pin the ascension columns (including the A19 column, and the A4
// column that is easy to misread from the `A_2_`/`A_4_` field names), the
// forced two-turn ACTIVATE -> DIVIDER opener, Divider's base damage as the
// PLAYER-HP-DERIVED expression at Hexaghost.java:151 rather than a table of
// magic numbers, the move cycle hand-derived across 16 turns, the orb count
// living in spare MonsterState flag bits, and the Burn upgrade -- both halves:
// Inferno's BurnIncreaseAction on the piles, and the latch that makes every
// later Sear create an upgraded Burn.
//
// getMove(int num) never reads `num` (Hexaghost.java:218-254), exactly as with
// Lagavulin and the Slime Boss, so there is no roll-driven branch for an
// independent-XS128 fixture to cover. What is observable is the DRAW COUNT, and
// that is what the RNG cases pin -- together with a case that runs the same
// script under 32 unrelated seeds and asserts an identical move history.
//
// Provenance: Hexaghost.java:50-95,96-128,130-134,136-143,145-216,218-254,
// 256-292; HexaghostOrb.java:19-59; HexaghostBody.java:18-66;
// Burn.java:31-35,56-64; BurnIncreaseAction.java:25-51;
// ShowCardAndAddToDiscardEffect.java:41-49; RollMoveAction.java:17-21;
// AbstractMonster.java:99,431-437,465-467.

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_hexaghost.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

// Hexaghost.java:79-84.
constexpr uint8_t kDivider = 1;
constexpr uint8_t kTackle = 2;
constexpr uint8_t kInflame = 3;
constexpr uint8_t kSear = 4;
constexpr uint8_t kActivate = 5;
constexpr uint8_t kInferno = 6;

// The A20 sheet the engine actually plays (kMonsterAscension == 20): the A9 HP
// column and the A19 stat column are both live.
constexpr int32_t kA20Hp = 264;
constexpr int32_t kA20TackleDmg = 6;
constexpr int32_t kA20SearDmg = 6;
constexpr int32_t kA20InfernoDmg = 3;
constexpr int32_t kA20BurnCount = 2;
constexpr int32_t kA20Strength = 3;
constexpr int32_t kInflameBlock = 12;

// A player HP that is NOT a multiple of 12, so the Divider formula's
// truncation is actually exercised.
constexpr int16_t kTestPlayerHp = 500;

CombatState make_hexaghost_state(int64_t seed,
                                 int16_t player_hp = kTestPlayerHp) {
    CombatState s{};
    s.player_hp = player_hp;
    s.player_max_hp = player_hp;
    s.monster_count = 1;
    s.monster_hp_rng = from_seed(seed);
    s.ai_rng = from_seed(seed);
    hexaghost_init(s, 0);
    return s;
}

void drain_actions(CombatState& s) {
    while (s.action_count > 0) {
        ActionQueueItem item{};
        ASSERT_TRUE(pop_action_front(s, item));
        execute_opcode(s, item);
    }
}

// Run Hexaghost's decided move through takeTurn and settle everything it
// queued, including the folded ROLL_MOVE tail.
void take_one_turn(CombatState& s) {
    hexaghost_take_turn(s, 0);
    drain_actions(s);
}

// The move Hexaghost is about to play.
uint8_t decided_move(const CombatState& s) {
    return s.monsters[0].move_history[0];
}

// Put a card straight into a pile, the way a status-generating effect would.
CardPoolIndex add_card(CombatState& s, CardId id, uint8_t upgrade,
                       CardPoolIndex* pile, uint8_t& count) {
    const CardDef* def = card_def(id);
    EXPECT_NE(def, nullptr);
    CardPoolIndex slot = 0;
    for (int i = 0; i < kCardPoolCap; ++i) {
        if (s.card_pool[i].card_id == static_cast<uint16_t>(CardId::NONE)) {
            slot = static_cast<CardPoolIndex>(i);
            break;
        }
    }
    s.card_pool[slot].card_id = static_cast<uint16_t>(id);
    s.card_pool[slot].upgrade = upgrade;
    s.card_pool[slot].cost_now = card_cost(*def, upgrade);
    s.card_pool[slot].flags = card_flags(*def, upgrade);
    pile[count++] = slot;
    return slot;
}

// Count cards of `id` in a pile; `upgrade` < 0 means "any upgrade level".
int count_in_pile(const CombatState& s, const CardPoolIndex* pile, uint8_t count,
                  CardId id, int upgrade) {
    int n = 0;
    for (uint8_t i = 0; i < count; ++i) {
        const CardInstance& c = s.card_pool[pile[i]];
        if (c.card_id == static_cast<uint16_t>(id) &&
            (upgrade < 0 || static_cast<int>(c.upgrade) == upgrade)) {
            ++n;
        }
    }
    return n;
}

// Play turns until Hexaghost is telegraphing the `occurrence`-th `move`.
void run_until_telegraphing(CombatState& s, uint8_t move, int occurrence) {
    for (int guard = 0; guard < 200; ++guard) {
        if (decided_move(s) == move && --occurrence == 0) {
            return;
        }
        take_one_turn(s);
    }
    ADD_FAILURE() << "never reached move " << static_cast<int>(move);
}

// ===========================================================================
// Registry
// ===========================================================================

TEST(HexaghostRegistry, IdsIntentsAndTheAscensionColumnsMatchTheCtor) {
    namespace r = sts::registry;

    EXPECT_EQ(static_cast<int>(r::MonsterId::HEXAGHOST), 22)
        << "appended after The Guardian (21); MonsterId 14 stays a legal gap";
    EXPECT_EQ(r::monster_game_id(r::MonsterId::HEXAGHOST), "Hexaghost");
    EXPECT_EQ(r::monster_from_game_id("Hexaghost"), r::MonsterId::HEXAGHOST);

    const r::MonsterDef& h = r::kHexaghost;
    EXPECT_TRUE(h.ai_native);
    EXPECT_EQ(h.roll_count, 0)
        << "setHp(int) on both branches: no monsterHpRng draw";
    EXPECT_EQ(h.enemy_type, r::MonsterEnemyType::BOSS);  // Hexaghost.java:98
    EXPECT_TRUE(h.is_boss());

    // HP: 250, or 264 from A9 (:59-60,102-106). The A_2_HP field NAME is
    // misleading -- the branch is `>= 9`.
    EXPECT_EQ(h.hp_min(0), 250);
    EXPECT_EQ(h.hp_min(8), 250);
    EXPECT_EQ(h.hp_min(9), 264);
    EXPECT_EQ(h.hp_min(19), 264);
    EXPECT_EQ(h.hp_max(kMonsterAscension), kA20Hp);
    EXPECT_EQ(h.hp_min(kMonsterAscension), h.hp_max(kMonsterAscension))
        << "a fixed sheet, not a range";

    struct MoveRow {
        uint8_t id;
        const char* name;
        r::MonsterIntent intent;
        uint8_t effects;
    };
    const MoveRow rows[] = {
        // ACTIVATE and DIVIDER carry an explicit single NOP: their whole bodies
        // are native (a state change, and a player-HP-derived damage).
        {kDivider, "DIVIDER", r::MonsterIntent::ATTACK, 1},
        {kTackle, "TACKLE", r::MonsterIntent::ATTACK, 2},
        {kInflame, "INFLAME", r::MonsterIntent::DEFEND_BUFF, 2},
        {kSear, "SEAR", r::MonsterIntent::ATTACK_DEBUFF, 2},
        {kActivate, "ACTIVATE", r::MonsterIntent::UNKNOWN, 1},
        {kInferno, "INFERNO", r::MonsterIntent::ATTACK_DEBUFF, 6},
    };
    for (const MoveRow& row : rows) {
        const r::MonsterMove* mv = h.move(row.id);
        ASSERT_NE(mv, nullptr) << row.name;
        EXPECT_EQ(mv->intent, row.intent) << row.name;
        EXPECT_EQ(mv->effect_count, row.effects) << row.name;
    }
    EXPECT_EQ(h.move(kDivider)->effects[0].op, r::Opcode::NOP);
    EXPECT_EQ(h.move(kActivate)->effects[0].op, r::Opcode::NOP);

    // Hexaghost needs NO new MonsterIntent: all four telegraphs it uses were
    // already in the vocabulary, so 13-16 stay unallocated.
    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::ATTACK), 1);
    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::DEFEND_BUFF), 2);
    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::ATTACK_DEBUFF), 6);
    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::UNKNOWN), 7);

    // Tackle: fireTackleCount == 2 SEPARATE hits of fireTackleDmg, 5 or 6 from
    // A4 (:64,68,76,110,115,120,173-174). Two hits, not one doubled hit.
    const r::MonsterMove* tackle = h.move(kTackle);
    for (uint8_t i = 0; i < 2; ++i) {
        EXPECT_EQ(tackle->effects[i].op, r::Opcode::DAMAGE);
        EXPECT_EQ(tackle->effects[i].target, r::MonsterMoveTarget::PLAYER);
        EXPECT_EQ(tackle->effects[i].amount.at(3), 5);
        EXPECT_EQ(tackle->effects[i].amount.at(4), 6);
        EXPECT_EQ(tackle->effects[i].amount.at(19), 6)
            << "A19 does not raise fireTackleDmg again (:110 vs :115)";
        EXPECT_EQ(tackle->effects[i].amount.at(kMonsterAscension), kA20TackleDmg);
    }

    // Inferno: infernoHits == 6 SEPARATE hits of infernoDmg, 2 or 3 from A4
    // (:63,67,78,116,121,127,201-203).
    const r::MonsterMove* inferno = h.move(kInferno);
    for (uint8_t i = 0; i < 6; ++i) {
        EXPECT_EQ(inferno->effects[i].op, r::Opcode::DAMAGE);
        EXPECT_EQ(inferno->effects[i].amount.at(3), 2);
        EXPECT_EQ(inferno->effects[i].amount.at(4), 3);
        EXPECT_EQ(inferno->effects[i].amount.at(kMonsterAscension),
                  kA20InfernoDmg);
    }

    // Sear: a flat 6 (searDmg has no ascension branch, :62,123) then the Burns,
    // 1 or 2 at A19+ (:65,69,109,114,120,186).
    const r::MonsterMove* sear = h.move(kSear);
    EXPECT_EQ(sear->effects[0].op, r::Opcode::DAMAGE);
    EXPECT_EQ(sear->effects[0].amount.at(0), kA20SearDmg);
    EXPECT_EQ(sear->effects[0].amount.at(kMonsterAscension), kA20SearDmg);
    EXPECT_EQ(sear->effects[1].op, r::Opcode::MAKE_CARD);
    EXPECT_EQ(sear->effects[1].extra & 0xFFFFu,
              static_cast<uint32_t>(CardId::BURN));
    EXPECT_EQ((sear->effects[1].extra >> 16) & 0xFFu,
              static_cast<uint32_t>(CardPile::DISCARD));
    EXPECT_EQ(sear->effects[1].amount.at(18), 1);
    EXPECT_EQ(sear->effects[1].amount.at(19), kA20BurnCount);

    // Inflame: a flat 12 block then Strength 2, or 3 at A19+
    // (:66,70,72,108,113,119,193-194). The Strength row already existed --
    // Hexaghost adds no power rows.
    const r::MonsterMove* inflame = h.move(kInflame);
    EXPECT_EQ(inflame->effects[0].op, r::Opcode::BLOCK);
    EXPECT_EQ(inflame->effects[0].target, r::MonsterMoveTarget::SELF);
    EXPECT_EQ(inflame->effects[0].amount.at(0), kInflameBlock);
    EXPECT_EQ(inflame->effects[0].amount.at(kMonsterAscension), kInflameBlock);
    EXPECT_EQ(inflame->effects[1].op, r::Opcode::APPLY_POWER);
    EXPECT_EQ(inflame->effects[1].target, r::MonsterMoveTarget::SELF);
    EXPECT_EQ(inflame->effects[1].extra & 0xFFFFu,
              static_cast<uint32_t>(r::PowerId::STRENGTH));
    EXPECT_EQ(inflame->effects[1].amount.at(18), 2);
    EXPECT_EQ(inflame->effects[1].amount.at(19), kA20Strength);

    EXPECT_EQ(monster_init_fn(MonsterId::HEXAGHOST), &hexaghost_init);
    EXPECT_EQ(monster_turn_fn(MonsterId::HEXAGHOST), &hexaghost_take_turn);
    EXPECT_EQ(monster_roll_move_fn(MonsterId::HEXAGHOST), &hexaghost_roll_move);
    EXPECT_EQ(monster_pre_battle_fn(MonsterId::HEXAGHOST), nullptr)
        << "usePreBattleAction is UnlockTracker + a BGM precache only (:130-134)";
    EXPECT_EQ(monster_spawn_at_hp_fn(MonsterId::HEXAGHOST), nullptr)
        << "nothing spawns Hexaghost mid-combat";
}

// The A19 column is the acceptance-named one and is live at the engine's fixed
// A20, so every damage number in this file depends on it. Pinned as the whole
// DESCENDING chain (Hexaghost.java:107-122) rather than one column.
TEST(HexaghostRegistry, A19ColumnIsTheLiveOneAtTheEnginesFixedAscension) {
    namespace r = sts::registry;
    const r::MonsterDef& h = r::kHexaghost;

    struct Column {
        int32_t asc;
        int32_t tackle;
        int32_t inferno;
        int32_t burns;
        int32_t strength;
        int32_t hp;
    };
    const Column columns[] = {
        {0, 5, 2, 1, 2, 250},   // the else branch (:118-122)
        {3, 5, 2, 1, 2, 250},
        {4, 6, 3, 1, 2, 250},   // ascensionLevel >= 4 (:112-116)
        {8, 6, 3, 1, 2, 250},
        {9, 6, 3, 1, 2, 264},   // HP alone moves at 9 (:102)
        {18, 6, 3, 1, 2, 264},
        {19, 6, 3, 2, 3, 264},  // ascensionLevel >= 19 (:107-111)
        {kMonsterAscension, kA20TackleDmg, kA20InfernoDmg, kA20BurnCount,
         kA20Strength, kA20Hp},
    };
    for (const Column& c : columns) {
        EXPECT_EQ(h.move(kTackle)->effects[0].amount.at(c.asc), c.tackle)
            << "ascension " << c.asc;
        EXPECT_EQ(h.move(kInferno)->effects[0].amount.at(c.asc), c.inferno)
            << "ascension " << c.asc;
        EXPECT_EQ(h.move(kSear)->effects[1].amount.at(c.asc), c.burns)
            << "ascension " << c.asc;
        EXPECT_EQ(h.move(kInflame)->effects[1].amount.at(c.asc), c.strength)
            << "ascension " << c.asc;
        EXPECT_EQ(h.hp_min(c.asc), c.hp) << "ascension " << c.asc;
        // searDmg and strengthenBlockAmt have no ascension branch at all.
        EXPECT_EQ(h.move(kSear)->effects[0].amount.at(c.asc), kA20SearDmg);
        EXPECT_EQ(h.move(kInflame)->effects[0].amount.at(c.asc), kInflameBlock);
    }
}

TEST(HexaghostRegistry, TheBossEncounterRowResolvesToThisMonster) {
    RngStream misc = from_seed(22001);
    ResolvedGroup group{};
    ASSERT_TRUE(resolve_encounter("Hexaghost", misc, group));
    ASSERT_EQ(group.count, 1);
    EXPECT_EQ(group.members[0], "Hexaghost");

    // The un-park gate is `monster_init_fn(id) == nullptr` -- registering an
    // init fn is what lets a run-created combat with this group play out.
    EXPECT_NE(monster_init_fn(MonsterId::HEXAGHOST), nullptr);

    // With this row, every Act-1 BOSS encounter now has a live monster.
    for (const MonsterId id : {MonsterId::THE_GUARDIAN, MonsterId::SLIME_BOSS,
                               MonsterId::HEXAGHOST}) {
        const sts::registry::MonsterDef* def = sts::registry::monster_def(id);
        ASSERT_NE(def, nullptr);
        EXPECT_TRUE(def->is_boss());
        EXPECT_NE(monster_init_fn(id), nullptr);
    }
}

// ===========================================================================
// Orb-count storage: the recorded modelling decision
// ===========================================================================

// The six HexaghostOrbs are rendering objects (HexaghostOrb.java:19-59) -- not
// AbstractMonsters, never in the MonsterGroup, never damaged. Their whole
// combat-relevant residue is the scalar orbActiveCount, which fits three spare
// bits of the existing MonsterState.flags word. This case is the guard that the
// choice stays clear of the neighbouring monsters' bits, so nothing here needs
// a new CombatState field (and therefore no SCHEMA_VERSION bump).
TEST(HexaghostState, OrbCountAndBurnLatchLiveInSpareFlagBitsOnly) {
    // Disjoint from every previously allocated per-monster flag bit.
    constexpr uint16_t kTaken = static_cast<uint16_t>(
        kMonsterFlagRitualSkip | kMonsterFlagCurlUpTriggered |
        kMonsterFlagSplitTriggered | kMonsterFlagLagavulinAsleep |
        kMonsterFlagLagavulinIsOut | kMonsterFlagLagavulinOutTriggered |
        kMonsterFlagGuardianOpen | kMonsterFlagGuardianCloseUpTriggered |
        kMonsterFlagGuardianShiftMask);
    EXPECT_EQ(kTaken & kMonsterFlagHexaghostOrbMask, 0u);
    EXPECT_EQ(kTaken & kMonsterFlagHexaghostBurnUpgraded, 0u);
    EXPECT_EQ(kMonsterFlagHexaghostOrbMask & kMonsterFlagHexaghostBurnUpgraded,
              0u);
    // Three bits: exactly enough for changeState's 0..6 range (:268,278-280,289).
    EXPECT_EQ(kMonsterFlagHexaghostOrbMask >> kMonsterFlagHexaghostOrbShift, 7u);

    // No monster slot, no power slot and no card-pool row is spent on an orb.
    CombatState s = make_hexaghost_state(22002);
    EXPECT_EQ(s.monster_count, 1) << "the orbs occupy no monster slot";
    EXPECT_EQ(s.monsters[0].power_count, 0) << "the orbs are not powers";
    EXPECT_EQ(hexaghost_orb_count(s.monsters[0]), 0);
    EXPECT_FALSE(hexaghost_burn_upgraded(s.monsters[0]));
}

// ===========================================================================
// Init + the two-turn opener
// ===========================================================================

TEST(HexaghostInit, FixedHpAndOneIgnoredAiRollForcingTheActivateOpener) {
    CombatState s{};
    s.monster_count = 1;
    s.monster_hp_rng = from_seed(22003);
    s.ai_rng = from_seed(22003);
    const RngStream hp_before = s.monster_hp_rng;

    hexaghost_init(s, 0);

    const MonsterState& h = s.monsters[0];
    EXPECT_EQ(h.monster_id, static_cast<uint16_t>(MonsterId::HEXAGHOST));
    EXPECT_EQ(h.hp, kA20Hp);
    EXPECT_EQ(h.max_hp, kA20Hp);
    EXPECT_EQ(s.monster_hp_rng.counter, hp_before.counter)
        << "setHp(int) is fixed: no monsterHpRng draw (:102-106)";
    EXPECT_EQ(s.monster_hp_rng.s0, hp_before.s0);
    EXPECT_EQ(s.monster_hp_rng.s1, hp_before.s1);
    EXPECT_EQ(s.ai_rng.counter, 1)
        << "init rollMove consumes random(99); getMove then ignores num";
    EXPECT_EQ(h.move_history[0], kActivate)
        << "`activated` is false at construction, so the opener is forced "
           "(:220-222)";
    EXPECT_EQ(h.intent, static_cast<uint8_t>(MonsterIntent::UNKNOWN));
    EXPECT_EQ(hexaghost_orb_count(h), 0);
    EXPECT_EQ(hexaghost_divider_damage(h), 0);
}

TEST(HexaghostOpener, ActivateLightsAllSixOrbsAndTelegraphsDividerWithNoRoll) {
    CombatState s = make_hexaghost_state(22004);
    const int32_t ai_after_init = s.ai_rng.counter;

    take_one_turn(s);

    EXPECT_EQ(decided_move(s), kDivider);
    EXPECT_EQ(s.monsters[0].intent, static_cast<uint8_t>(MonsterIntent::ATTACK));
    EXPECT_EQ(hexaghost_orb_count(s.monsters[0]), 6)
        << "changeState \"Activate\" sets orbActiveCount = 6 (:268)";
    EXPECT_EQ(s.player_hp, kTestPlayerHp) << "the Activate turn deals no damage";
    EXPECT_EQ(s.ai_rng.counter, ai_after_init)
        << "the Activate body has NO RollMoveAction -- setMove is direct (:153)";
}

// ===========================================================================
// Divider: player-HP-derived damage
// ===========================================================================

// The formula itself, pinned against Hexaghost.java:151 rather than a table of
// outputs: `AbstractDungeon.player.currentHealth / 12 + 1`, Java int division.
TEST(HexaghostDivider, BaseIsPlayerHpOverTwelvePlusOneExactlyPerTheCitedLine) {
    struct Row { int32_t hp; int32_t expected; };
    const Row rows[] = {
        {0, 1},    // 0/12 + 1
        {1, 1},    // truncation: 1/12 == 0
        {11, 1},
        {12, 2},   // the first step
        {23, 2},
        {24, 3},
        {75, 7},   // 75/12 == 6
        {80, 7},   // the Ironclad's starting max HP
        {99, 9},
        {kTestPlayerHp, 42},  // 500/12 == 41
    };
    for (const Row& row : rows) {
        EXPECT_EQ(hexaghost_divider_base(row.hp), row.expected)
            << "player HP " << row.hp;
        // Restated as the Java expression, so the case fails if either side is
        // "fixed" to match the other.
        EXPECT_EQ(hexaghost_divider_base(row.hp), row.hp / 12 + 1)
            << "player HP " << row.hp;
    }
}

TEST(HexaghostDivider, TheBaseIsLockedAtTheActivateTurnNotAtDividerTime) {
    CombatState s = make_hexaghost_state(22005, 240);
    take_one_turn(s);  // ACTIVATE: reads the player's CURRENT hp (:151)
    EXPECT_EQ(hexaghost_divider_damage(s.monsters[0]), 21);  // 240/12 + 1

    // Whatever happens to the player afterwards, damage.get(2).base does not
    // move: it is assigned once, and only re-read by the DamageActions.
    s.player_hp = 200;
    take_one_turn(s);  // DIVIDER
    EXPECT_EQ(s.player_hp, 200 - 6 * 21)
        << "six hits at the base captured on the Activate turn";
}

TEST(HexaghostDivider, DealsSixSeparateHitsThenDousesEveryOrb) {
    CombatState s = make_hexaghost_state(22006);
    take_one_turn(s);  // ACTIVATE
    const int32_t base = hexaghost_divider_damage(s.monsters[0]);
    EXPECT_EQ(base, 42);

    // Queue the Divider turn but do NOT drain it, so the six items are visible.
    hexaghost_take_turn(s, 0);
    int damage_items = 0;
    int roll_items = 0;
    std::vector<ActionQueueItem> items;
    while (s.action_count > 0) {
        ActionQueueItem it{};
        ASSERT_TRUE(pop_action_front(s, it));
        items.push_back(it);
        if (it.opcode == static_cast<uint16_t>(Opcode::DAMAGE)) {
            ++damage_items;
            EXPECT_EQ(it.amount, base);
            EXPECT_EQ(it.tgt, kActorPlayer);
        } else if (it.opcode == static_cast<uint16_t>(Opcode::ROLL_MOVE)) {
            ++roll_items;
        }
    }
    EXPECT_EQ(damage_items, 6) << "six separate DamageActions (:157-165)";
    EXPECT_EQ(roll_items, 1);
    ASSERT_EQ(items.size(), 7u);
    EXPECT_EQ(items.back().opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE))
        << "the change of state and the roll come after the hits";

    // Replay from a clean state through the normal path to check the effects.
    CombatState t = make_hexaghost_state(22006);
    take_one_turn(t);  // ACTIVATE
    take_one_turn(t);  // DIVIDER
    EXPECT_EQ(t.player_hp, kTestPlayerHp - 6 * base);
    EXPECT_EQ(hexaghost_orb_count(t.monsters[0]), 0)
        << "changeState \"Deactivate\" (:166,289)";
    EXPECT_EQ(decided_move(t), kSear) << "orbActiveCount 0 -> Sear (:226)";
}

// ===========================================================================
// The move cycle
// ===========================================================================

// Hand-derived from getMove's orbActiveCount switch (:224-252) and the
// changeState bodies (:256-292): the ACTIVATE/DIVIDER opener, then a 7-long
// cycle -- one orb lit per Sear/Tackle/Inflame, all six doused by Inferno.
TEST(HexaghostMoveCycle, SixteenTurnsMatchTheHandDerivation) {
    struct Turn { uint8_t move; uint8_t orbs_after; };
    const Turn expected[] = {
        {kActivate, 6},  // 1  changeState "Activate": all six (:268)
        {kDivider, 0},   // 2  changeState "Deactivate" (:289)
        {kSear, 1},      // 3  orbActiveCount 0 -> Sear (:226), then +1 (:278)
        {kTackle, 2},    // 4  1 -> Tackle (:230)
        {kSear, 3},      // 5  2 -> Sear (:234)
        {kInflame, 4},   // 6  3 -> Inflame (:238)
        {kTackle, 5},    // 7  4 -> Tackle (:242)
        {kSear, 6},      // 8  5 -> Sear (:246); the sixth orb lights
        {kInferno, 0},   // 9  6 -> Inferno (:250), then "Deactivate" (:208)
        {kSear, 1},      // 10 the cycle repeats, identically
        {kTackle, 2},    // 11
        {kSear, 3},      // 12
        {kInflame, 4},   // 13
        {kTackle, 5},    // 14
        {kSear, 6},      // 15
        {kInferno, 0},   // 16
    };

    CombatState s = make_hexaghost_state(22007, 3000);
    int turn = 0;
    for (const Turn& e : expected) {
        ++turn;
        EXPECT_EQ(decided_move(s), e.move) << "turn " << turn;
        take_one_turn(s);
        EXPECT_EQ(hexaghost_orb_count(s.monsters[0]), e.orbs_after)
            << "turn " << turn;
    }
    EXPECT_EQ(decided_move(s), kSear) << "turn 17 restarts the cycle";
    EXPECT_GT(s.player_hp, 0) << "the fight was not cut short";
}

TEST(HexaghostMoveCycle, EveryTelegraphMatchesTheJavaIntent) {
    CombatState s = make_hexaghost_state(22008, 3000);
    struct Row { uint8_t move; MonsterIntent intent; };
    const Row expected[] = {
        {kActivate, MonsterIntent::UNKNOWN},       // :222
        {kDivider, MonsterIntent::ATTACK},         // :153
        {kSear, MonsterIntent::ATTACK_DEBUFF},     // :226
        {kTackle, MonsterIntent::ATTACK},          // :230
        {kSear, MonsterIntent::ATTACK_DEBUFF},     // :234
        {kInflame, MonsterIntent::DEFEND_BUFF},    // :238
        {kTackle, MonsterIntent::ATTACK},          // :242
        {kSear, MonsterIntent::ATTACK_DEBUFF},     // :246
        {kInferno, MonsterIntent::ATTACK_DEBUFF},  // :250
    };
    for (const Row& row : expected) {
        ASSERT_EQ(decided_move(s), row.move);
        EXPECT_EQ(s.monsters[0].intent, static_cast<uint8_t>(row.intent))
            << "move " << static_cast<int>(row.move);
        take_one_turn(s);
    }
}

// ===========================================================================
// RNG accounting -- getMove never reads its argument
// ===========================================================================

TEST(HexaghostRng, EveryTurnButTheActivateOpenerConsumesExactlyOneAiDraw) {
    CombatState s = make_hexaghost_state(22009, 3000);
    EXPECT_EQ(s.ai_rng.counter, 1) << "init's rollMove";

    take_one_turn(s);  // ACTIVATE -- a DIRECT setMove, no RollMoveAction (:153)
    EXPECT_EQ(s.ai_rng.counter, 1);

    // Every other body ends with a RollMoveAction (:167,176,188,196,209).
    for (int32_t turn = 2; turn <= 16; ++turn) {
        take_one_turn(s);
        EXPECT_EQ(s.ai_rng.counter, turn)
            << "one random(99) per RollMoveAction, after turn " << turn;
    }
}

// The selection is a pure function of orbActiveCount: getMove(int num) does not
// mention `num` anywhere in its body (:218-254). With no roll-driven branch
// there is nothing for an independent-XS128 fixture to pin, so this is the case
// that proves the claim -- 32 unrelated seeds, one identical script.
TEST(HexaghostRng, TheRolledValueIsDiscardedSoTheScriptIsSeedIndependent) {
    std::vector<uint8_t> reference;
    for (int64_t seed = 0; seed < 32; ++seed) {
        CombatState s = make_hexaghost_state(seed * 7919 + 13, 3000);
        std::vector<uint8_t> script;
        for (int turn = 0; turn < 16; ++turn) {
            script.push_back(decided_move(s));
            take_one_turn(s);
        }
        EXPECT_EQ(s.ai_rng.counter, 16)
            << "seed " << seed << ": 1 init roll + 15 RollMoveActions";
        if (reference.empty()) {
            reference = script;
        } else {
            EXPECT_EQ(script, reference) << "seed " << seed;
        }
    }
    ASSERT_EQ(reference.size(), 16u);
    EXPECT_EQ(reference[0], kActivate);
    EXPECT_EQ(reference[8], kInferno);
}

// ===========================================================================
// Move bodies
// ===========================================================================

TEST(HexaghostTackle, DealsTwoSeparateSixDamageHits) {
    CombatState s = make_hexaghost_state(22010, 3000);
    run_until_telegraphing(s, kTackle, 1);
    const int16_t before = s.player_hp;
    take_one_turn(s);
    EXPECT_EQ(s.player_hp, before - 2 * kA20TackleDmg);
}

TEST(HexaghostInflame, GainsTwelveBlockAndStrengthOnItself) {
    CombatState s = make_hexaghost_state(22011, 3000);
    run_until_telegraphing(s, kInflame, 1);
    const int16_t player_before = s.player_hp;

    take_one_turn(s);

    EXPECT_EQ(s.monsters[0].block, kInflameBlock)
        << "a direct GainBlockAction of strengthenBlockAmt (:72,193)";
    EXPECT_EQ(s.player_hp, player_before) << "Inflame deals no damage";
    const PowerSlot* str = nullptr;
    for (uint8_t i = 0; i < s.monsters[0].power_count; ++i) {
        if (s.monsters[0].powers[i].power_id ==
            static_cast<uint16_t>(PowerId::STRENGTH)) {
            str = &s.monsters[0].powers[i];
        }
    }
    ASSERT_NE(str, nullptr) << "StrengthPower(this, strAmount) on ITSELF (:194)";
    EXPECT_EQ(str->amount, kA20Strength);

    // The Strength is real: the next Tackle hits for 6 + 3 per hit.
    run_until_telegraphing(s, kTackle, 1);
    const int16_t before = s.player_hp;
    take_one_turn(s);
    EXPECT_EQ(s.player_hp, before - 2 * (kA20TackleDmg + kA20Strength));
}

TEST(HexaghostSear, DealsSixAndPutsTwoBurnsIntoTheDiscardPile) {
    CombatState s = make_hexaghost_state(22012, 3000);
    run_until_telegraphing(s, kSear, 1);
    const int16_t before = s.player_hp;

    take_one_turn(s);

    EXPECT_EQ(s.player_hp, before - kA20SearDmg);
    EXPECT_EQ(s.discard_count, kA20BurnCount)
        << "MakeTempCardInDiscardAction(new Burn(), searBurnCount) (:186)";
    EXPECT_EQ(count_in_pile(s, s.discard, s.discard_count, CardId::BURN, 0),
              kA20BurnCount)
        << "not upgraded before the first Inferno (:183-185)";
    EXPECT_EQ(s.hand_count, 0) << "into the DISCARD pile, not the hand";
}

// ===========================================================================
// The Burn upgrade -- BurnIncreaseAction and the burnUpgraded latch
// ===========================================================================

// BurnIncreaseAction.java:25-51: upgrade every Burn already in the DISCARD then
// the DRAW pile (not the hand, not exhaust), then add three fresh upgraded
// Burns to the discard.
TEST(HexaghostInferno, BurnIncreaseUpgradesDrawAndDiscardThenAddsThreeMore) {
    CombatState s = make_hexaghost_state(22013, 3000);

    // Seed the piles by hand so the "already there" half is unambiguous.
    add_card(s, CardId::BURN, 0, s.discard, s.discard_count);
    add_card(s, CardId::BURN, 0, s.draw, s.draw_count);
    add_card(s, CardId::BURN, 0, s.hand, s.hand_count);
    const CardPoolIndex strike_slot =
        add_card(s, CardId::STRIKE, 0, s.draw, s.draw_count);

    run_until_telegraphing(s, kInferno, 1);
    const uint8_t discard_before = s.discard_count;
    const int16_t player_before = s.player_hp;

    take_one_turn(s);

    // infernoHits == 6 separate hits (:201-203). The Inflame on turn 6 has
    // already given Hexaghost Strength 3 (:194), and each hit runs the
    // DamageInfo pipeline on its own -- so this is 6 x (3 + 3), not 6 x 3.
    EXPECT_EQ(s.player_hp,
              player_before - 6 * (kA20InfernoDmg + kA20Strength));

    // The pre-existing Burns in draw and discard are upgraded in place.
    EXPECT_EQ(count_in_pile(s, s.draw, s.draw_count, CardId::BURN, 1), 1)
        << "drawPile pass (BurnIncreaseAction.java:32-35)";
    EXPECT_EQ(count_in_pile(s, s.draw, s.draw_count, CardId::BURN, 0), 0);

    // The hand is deliberately NOT swept: the Java walks only the two piles.
    EXPECT_EQ(count_in_pile(s, s.hand, s.hand_count, CardId::BURN, 0), 1)
        << "the hand is untouched by BurnIncreaseAction";

    // Three fresh upgraded Burns land in the discard, on top of everything that
    // was already there (which is now upgraded too).
    EXPECT_EQ(s.discard_count, discard_before + 3);
    EXPECT_EQ(count_in_pile(s, s.discard, s.discard_count, CardId::BURN, 1),
              static_cast<int>(discard_before) + 3)
        << "every Burn in the discard is now upgraded";
    EXPECT_EQ(count_in_pile(s, s.discard, s.discard_count, CardId::BURN, 0), 0);

    // Non-Burn cards are left alone.
    EXPECT_EQ(s.card_pool[strike_slot].upgrade, 0);

    EXPECT_TRUE(hexaghost_burn_upgraded(s.monsters[0]))
        << "burnUpgraded = true (:205-207)";
}

// The acceptance's "Burns upgrade at the cited turn": the latch is set by the
// FIRST Inferno (turn 9 of the fight), so the Sears on turns 3/5/8 make base
// Burns and the Sear on turn 10 -- the first after that Inferno -- makes an
// upgraded one (Hexaghost.java:182-186,205-207).
TEST(HexaghostInferno, SearBurnsAreUpgradedFromTheFirstTurnAfterInferno) {
    CombatState s = make_hexaghost_state(22014, 3000);

    // Turns 1-8: three Sears, all before any Inferno.
    for (int turn = 1; turn <= 8; ++turn) {
        EXPECT_FALSE(hexaghost_burn_upgraded(s.monsters[0]))
            << "turn " << turn << ": no Inferno has resolved yet";
        take_one_turn(s);
    }
    EXPECT_EQ(count_in_pile(s, s.discard, s.discard_count, CardId::BURN, 0),
              3 * kA20BurnCount)
        << "three Sears x 2 Burns, all base";
    EXPECT_EQ(count_in_pile(s, s.discard, s.discard_count, CardId::BURN, 1), 0);

    // Turn 9 is the first Inferno.
    ASSERT_EQ(decided_move(s), kInferno);
    take_one_turn(s);
    EXPECT_TRUE(hexaghost_burn_upgraded(s.monsters[0]));
    const int upgraded_after_inferno =
        count_in_pile(s, s.discard, s.discard_count, CardId::BURN, 1);
    EXPECT_EQ(upgraded_after_inferno, 3 * kA20BurnCount + 3)
        << "the six existing Burns upgraded in place, plus three new ones";
    EXPECT_EQ(count_in_pile(s, s.discard, s.discard_count, CardId::BURN, 0), 0);

    // Turn 10 is the first Sear after it, and its Burns arrive upgraded.
    ASSERT_EQ(decided_move(s), kSear);
    take_one_turn(s);
    EXPECT_EQ(count_in_pile(s, s.discard, s.discard_count, CardId::BURN, 0), 0)
        << "`if (this.burnUpgraded) c.upgrade();` (:183-185)";
    EXPECT_EQ(count_in_pile(s, s.discard, s.discard_count, CardId::BURN, 1),
              upgraded_after_inferno + kA20BurnCount);

    // And it stays set for the rest of the fight.
    for (int turn = 11; turn <= 16; ++turn) {
        take_one_turn(s);
        EXPECT_TRUE(hexaghost_burn_upgraded(s.monsters[0])) << "turn " << turn;
    }
    EXPECT_EQ(count_in_pile(s, s.discard, s.discard_count, CardId::BURN, 0), 0);
}

// An upgraded Burn is the one that hurts: magicNumber 2 -> 4 (Burn.java:33,60).
// The upgrade this monster applies has to reach the card's own program.
TEST(HexaghostInferno, TheUpgradedBurnCarriesTheFourDamageProgram) {
    CombatState s = make_hexaghost_state(22015, 3000);
    run_until_telegraphing(s, kInferno, 1);
    take_one_turn(s);

    ASSERT_GT(s.discard_count, 0);
    const CardInstance& burn = s.card_pool[s.discard[s.discard_count - 1]];
    ASSERT_EQ(burn.card_id, static_cast<uint16_t>(CardId::BURN));
    ASSERT_EQ(burn.upgrade, 1);

    const CardDef* def = card_def(CardId::BURN);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(burn.cost_now, card_cost(*def, 1));
    EXPECT_EQ(burn.flags, card_flags(*def, 1));
    // The base and upgraded programs really do differ, so `upgrade` is
    // load-bearing rather than cosmetic.
    ASSERT_GT(card_effect_steps(*def, 0).count, 0);
    ASSERT_GT(card_effect_steps(*def, 1).count, 0);
    EXPECT_EQ(card_effect_steps(*def, 0).steps[0].amount, 2);
    EXPECT_EQ(card_effect_steps(*def, 1).steps[0].amount, 4);
}

}  // namespace
}  // namespace sts::engine
