// Tier-2 Act-2 City boss coverage (S2.24): the Bronze Automaton (+ Bronze Orb
// and the Stasis card theft), the Champ, and The Collector (+ Torch Head).
//
// WHAT THIS FILE PINS, and why each part is here rather than assumed:
//
//   * STAT/MOVE TABLES, per boss, at EVERY ascension branch the Java has --
//     including the branches that move on DIFFERENT boundaries inside one move
//     (the Automaton's BOOST: block on >= 9, Strength on >= 4) and the pinned
//     NON-changes (the Champ's flat 10 EXECUTE; the Collector's megaDebuff
//     staying 3 across the A4 rung).
//   * MOVE SELECTION: the Automaton's two-arm-only numTurns counter and its
//     A19-swapped recovery turn; the Orb's decision-time usedStasis latch; the
//     Champ's every-call counter, threshold one-shot, lastMoveBefore EXECUTE
//     pattern and A19-widened forge bound; the Collector's takeTurn-written
//     latches and the derived revive-slot map.
//   * THE SUMMON STREAM ORDER: both minion ctors draw monster_hp_rng at QUEUE
//     time (super-arg then setHp, per head, in slot order), each spawn's ONE
//     ai_rng init roll at RESOLVE, the summoner's own roll THIRD, and the
//     Minion applies in SpawnMonsterAction's addToTop position -- the item
//     right behind each spawn, NOT SummonGremlinAction's trailing addToBot.
//   * STASIS end to end: the rarity cascade over LIVE CardRarity (a BASIC
//     Strike never matches, a Wound is COMMON), the STABLE cardID sort, the
//     draw-vs-discard pile preference, the zero-draw empty case, the
//     unfiltered pile-order fallback, the limbo park, the counter-carried pool
//     index, the onDeath give-back with both hand reads (queue-time choice,
//     resolve-time spill), the boss-sweep return, and the knowledge-chain
//     removal.
//   * BOSS-FLAG TYPING: enemy_type BOSS on exactly the three bosses,
//     Pantograph healing through the boss record and NOT through a minion's.
//
// The A13 boss-gold economics and the un-parked Act-2 boss room are run-layer
// facts and are pinned in run_advance_test.cpp / combat_rewards_test.cpp; this
// file owns the fights themselves.
//
// Provenance: BronzeAutomaton.java:41-191; BronzeOrb.java:29-102;
// Champ.java:47-319; TheCollector.java:50-243; TorchHead.java:27-85;
// ApplyStasisAction.java:19-81; StasisPower.java:16-45;
// SpawnMonsterAction.java:28-73; SuicideAction.java:12-38;
// CardGroup.java:498-500,:526-538; AbstractCard.java:2583-2584;
// MakeTempCardInHandAction.java:57-82; MonsterGroup.java:108-115;
// Pantograph.java:32-40.

#include <cstdint>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/knowledge.hpp"
#include "sts/engine/monster_bronze_automaton.hpp"
#include "sts/engine/monster_bronze_orb.hpp"
#include "sts/engine/monster_champ.hpp"
#include "sts/engine/monster_collector.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/monster_torch_head.hpp"
#include "sts/engine/piles.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/relic_hooks.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

namespace r = sts::registry;
constexpr int32_t kA20 = kMonsterAscension;

// --- shared helpers (the beyond_bosses idiom) --------------------------------

CombatState MakeState(uint8_t monsters = 1) {
    CombatState s{};
    s.player_hp = 400;
    s.player_max_hp = 400;
    s.player_energy = 3;
    s.monster_count = monsters;
    return s;
}

CombatState MakeSeeded(int64_t seed, uint8_t monsters = 1) {
    CombatState s = MakeState(monsters);
    s.monster_hp_rng = from_seed(seed);
    s.ai_rng = from_seed(seed);
    s.card_random_rng = from_seed(seed);
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

const ActionQueueItem& queued(const CombatState& s, uint8_t i) {
    return s.action_queue[(s.action_head + i) % kActionQueueCap];
}

const PowerSlot* find_monster_power(const CombatState& s, uint8_t mi,
                                    PowerId id) {
    const MonsterState& m = s.monsters[mi];
    for (uint8_t i = 0; i < m.power_count; ++i) {
        if (m.powers[i].power_id == static_cast<uint16_t>(id)) {
            return &m.powers[i];
        }
    }
    return nullptr;
}

int16_t monster_power(const CombatState& s, uint8_t mi, PowerId id) {
    const PowerSlot* p = find_monster_power(s, mi, id);
    return p == nullptr ? -1 : p->amount;
}

int16_t player_power_amount(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return s.player_powers[i].amount;
        }
    }
    return -1;
}

void player_attacks(CombatState& s, uint8_t mi, int32_t base) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    it.src = kActorPlayer;
    it.tgt = mi;
    it.amount = base;
    it.flags = make_damage_flags(DamageType::NORMAL);
    execute_opcode(s, it);
}

int32_t step_amount(const r::MonsterDef& def, uint8_t move, uint8_t k,
                    int32_t asc) {
    const r::MonsterMove* mv = def.move(move);
    return mv == nullptr ? -1 : mv->effects[k].amount.at(asc);
}

uint8_t step_count(const r::MonsterDef& def, uint8_t move) {
    const r::MonsterMove* mv = def.move(move);
    return mv == nullptr ? 0 : mv->effect_count;
}

// One card_pool row with the given id, appended to the given pile array.
uint8_t add_pool_card(CombatState& s, CardId id, CardPoolIndex* pile,
                      uint8_t& count) {
    int slot = -1;
    for (int i = 0; i < kCardPoolCap; ++i) {
        if (s.card_pool[i].card_id == static_cast<uint16_t>(CardId::NONE)) {
            slot = i;
            break;
        }
    }
    EXPECT_GE(slot, 0);
    s.card_pool[slot].card_id = static_cast<uint16_t>(id);
    pile[count++] = static_cast<CardPoolIndex>(slot);
    return static_cast<uint8_t>(slot);
}

uint8_t add_to_draw(CombatState& s, CardId id) {
    return add_pool_card(s, id, s.draw, s.draw_count);
}
uint8_t add_to_discard(CombatState& s, CardId id) {
    return add_pool_card(s, id, s.discard, s.discard_count);
}

bool in_pile(const CombatState& s, const CardPoolIndex* pile, uint8_t count,
             uint8_t pool_index) {
    for (uint8_t i = 0; i < count; ++i) {
        if (pile[i] == pool_index) {
            return true;
        }
    }
    return false;
}

// Steal with the orb at `mi` (the APPLY_STASIS item the STASIS move authors),
// then drain the addToTop'd apply.
void run_stasis(CombatState& s, uint8_t mi) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::APPLY_STASIS);
    it.src = mi;
    it.tgt = mi;
    execute_opcode(s, it);
    drain(s);
}

// ============================================================================
// 1. Registry -- stat and move tables, every ascension branch, per monster
// ============================================================================

TEST(CityBossesRegistry, BronzeAutomatonTableAcrossEveryAscensionBranch) {
    EXPECT_EQ(static_cast<int>(r::MonsterId::BRONZE_AUTOMATON), 40);
    EXPECT_EQ(r::monster_from_game_id("BronzeAutomaton"),
              r::MonsterId::BRONZE_AUTOMATON);
    const auto& d = r::kBronzeAutomaton;
    EXPECT_TRUE(d.ai_native);
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::BOSS);  // BronzeAutomaton.java:75
    EXPECT_EQ(d.roll_count, 0) << "the super HP argument is a literal 300 -- "
                                  "no extra ctor roll (contrast its own orb)";

    // setHp branch at >= 9 (:78-84). Degenerate ranges; the draw still happens.
    EXPECT_EQ(d.hp_min(0), 300);
    EXPECT_EQ(d.hp_min(8), 300) << "the HP branch is >= 9, not >= 4";
    EXPECT_EQ(d.hp_min(9), 320);
    EXPECT_EQ(d.hp_max(kA20), 320);

    // FLAIL: TWO separate DamageActions (:127-128), 7/8 at the >= 4 boundary.
    EXPECT_EQ(step_count(d, r::kBronzeAutomatonMoveFlail), 2);
    EXPECT_EQ(step_amount(d, r::kBronzeAutomatonMoveFlail, 0, 3), 7);
    EXPECT_EQ(step_amount(d, r::kBronzeAutomatonMoveFlail, 0, 4), 8);
    EXPECT_EQ(step_amount(d, r::kBronzeAutomatonMoveFlail, 1, kA20), 8);

    // HYPER_BEAM: one hit, 45/50 on the SAME >= 4 branch (:85-93) -- the
    // dispatch brief guessed a beam-only tier; the source says one ladder.
    EXPECT_EQ(step_count(d, r::kBronzeAutomatonMoveHyperBeam), 1);
    EXPECT_EQ(step_amount(d, r::kBronzeAutomatonMoveHyperBeam, 0, 3), 45);
    EXPECT_EQ(step_amount(d, r::kBronzeAutomatonMoveHyperBeam, 0, 4), 50);

    // BOOST: TWO DIFFERENT boundaries in one move -- block rides the HP's
    // >= 9 (:80,:83), Strength the damage ladder's >= 4 (:88,:92).
    const r::MonsterMove* boost = d.move(r::kBronzeAutomatonMoveBoost);
    ASSERT_NE(boost, nullptr);
    EXPECT_EQ(boost->intent, MonsterIntent::DEFEND_BUFF);
    ASSERT_EQ(boost->effect_count, 2);
    EXPECT_EQ(boost->effects[0].op, r::Opcode::BLOCK);
    EXPECT_EQ(boost->effects[0].amount.at(8), 9);
    EXPECT_EQ(boost->effects[0].amount.at(9), 12);
    EXPECT_EQ(boost->effects[1].op, r::Opcode::APPLY_POWER);
    EXPECT_EQ(boost->effects[1].extra, static_cast<uint32_t>(PowerId::STRENGTH));
    EXPECT_EQ(boost->effects[1].amount.at(3), 3);
    EXPECT_EQ(boost->effects[1].amount.at(4), 4);

    // STUNNED telegraphs Intent.STUN (:165) -- the BELOW-A19 recovery arm; the
    // A19 swap to BOOST is move selection, pinned in section 2. SPAWN_ORBS is
    // UNKNOWN (:151). Both bodies are native/presentation -> authored NOP.
    const r::MonsterMove* stunned = d.move(r::kBronzeAutomatonMoveStunned);
    ASSERT_NE(stunned, nullptr);
    EXPECT_EQ(stunned->intent, MonsterIntent::STUN);
    EXPECT_EQ(stunned->effects[0].op, r::Opcode::NOP);
    const r::MonsterMove* spawn = d.move(r::kBronzeAutomatonMoveSpawnOrbs);
    ASSERT_NE(spawn, nullptr);
    EXPECT_EQ(spawn->intent, MonsterIntent::UNKNOWN);
}

TEST(CityBossesRegistry, BronzeOrbTableAndItsDiscardedSuperArgDraw) {
    EXPECT_EQ(static_cast<int>(r::MonsterId::BRONZE_ORB), 41);
    EXPECT_EQ(r::monster_from_game_id("BronzeOrb"), r::MonsterId::BRONZE_ORB);
    const auto& d = r::kBronzeOrb;
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL)
        << "BronzeOrb.java assigns no this.type -- a minion inside a boss "
           "fight is still NORMAL, and Pantograph must not key on it";

    EXPECT_EQ(d.hp_min(0), 52);
    EXPECT_EQ(d.hp_max(8), 58) << "the HP branch is >= 9";
    EXPECT_EQ(d.hp_min(9), 54);
    EXPECT_EQ(d.hp_max(kA20), 60);

    // The discarded super(...) draw (:48): FLAT (52, 58) at every ascension --
    // only the setHp under it tiers. Timing CONSTRUCTOR_BEFORE_HP so the burn
    // walk orders it AHEAD of the setHp draw.
    ASSERT_EQ(d.roll_count, 1);
    const r::MonsterRollDef* roll = d.roll(0);
    ASSERT_NE(roll, nullptr);
    EXPECT_EQ(roll->timing, r::MonsterRollTiming::CONSTRUCTOR_BEFORE_HP);
    EXPECT_EQ(roll->min(0), 52);
    EXPECT_EQ(roll->max(kA20), 58) << "the super-arg literal never tiers";

    EXPECT_EQ(step_amount(d, r::kBronzeOrbMoveBeam, 0, 0), 8);
    EXPECT_EQ(step_amount(d, r::kBronzeOrbMoveBeam, 0, kA20), 8);
    const r::MonsterMove* support = d.move(r::kBronzeOrbMoveSupportBeam);
    ASSERT_NE(support, nullptr);
    EXPECT_EQ(support->intent, MonsterIntent::DEFEND);
    EXPECT_EQ(support->effects[0].op, r::Opcode::BLOCK);
    EXPECT_EQ(support->effects[0].amount.at(kA20), 12);
    const r::MonsterMove* stasis = d.move(r::kBronzeOrbMoveStasis);
    ASSERT_NE(stasis, nullptr);
    EXPECT_EQ(stasis->intent, MonsterIntent::STRONG_DEBUFF);
    EXPECT_EQ(stasis->effects[0].op, r::Opcode::APPLY_STASIS)
        << "the theft is the authored opcode-71 step (vocab.py APPLY_STASIS)";
}

TEST(CityBossesRegistry, ChampTableAndItsThreeIndependentTierLadders) {
    EXPECT_EQ(static_cast<int>(r::MonsterId::CHAMP), 42);
    EXPECT_EQ(r::monster_from_game_id("Champ"), r::MonsterId::CHAMP);
    const auto& d = r::kChamp;
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::BOSS);  // Champ.java:96
    EXPECT_EQ(d.roll_count, 0);
    EXPECT_EQ(d.hp_min(8), 420);
    EXPECT_EQ(d.hp_min(9), 440);

    // Ladder 1 (>= 4): slash 16 -> 18, slap 12 -> 14, and NEITHER moves again
    // at 9 or 19 (:108-136 -- the four-way chain keeps 18/14 on every upper
    // rung). EXECUTE is 10 on EVERY rung: the pinned non-change.
    EXPECT_EQ(step_amount(d, r::kChampMoveHeavySlash, 0, 3), 16);
    EXPECT_EQ(step_amount(d, r::kChampMoveHeavySlash, 0, 4), 18);
    EXPECT_EQ(step_amount(d, r::kChampMoveHeavySlash, 0, kA20), 18);
    EXPECT_EQ(step_amount(d, r::kChampMoveFaceSlap, 0, 3), 12);
    EXPECT_EQ(step_amount(d, r::kChampMoveFaceSlap, 0, 4), 14);
    EXPECT_EQ(step_count(d, r::kChampMoveExecute), 2);
    for (const int32_t asc : {0, 4, 9, 19, 20}) {
        EXPECT_EQ(step_amount(d, r::kChampMoveExecute, 0, asc), 10)
            << "executeDmg never tiers (asc " << asc << ")";
    }

    // Ladder 2 (>= 9, >= 19): block 15/18/20 and Metallicize 5/6/7 -- and the
    // A4-8 rung KEEPS 15/5, which is what distinguishes this ladder from the
    // damage one.
    const r::MonsterMove* stance = d.move(r::kChampMoveDefensiveStance);
    ASSERT_NE(stance, nullptr);
    EXPECT_EQ(stance->intent, MonsterIntent::DEFEND_BUFF);
    EXPECT_EQ(stance->effects[0].amount.at(4), 15);
    EXPECT_EQ(stance->effects[0].amount.at(9), 18);
    EXPECT_EQ(stance->effects[0].amount.at(19), 20);
    EXPECT_EQ(stance->effects[1].extra,
              static_cast<uint32_t>(PowerId::METALLICIZE));
    EXPECT_EQ(stance->effects[1].amount.at(4), 5);
    EXPECT_EQ(stance->effects[1].amount.at(9), 6);
    EXPECT_EQ(stance->effects[1].amount.at(19), 7);

    // Ladder 3 (>= 4, >= 19): strAmt 2/3/3/4 -- the A9 rung KEEPS 3 -- and
    // ANGER is strAmt * 3 on the same ladder (6/9/9/12).
    EXPECT_EQ(step_amount(d, r::kChampMoveGloat, 0, 3), 2);
    EXPECT_EQ(step_amount(d, r::kChampMoveGloat, 0, 4), 3);
    EXPECT_EQ(step_amount(d, r::kChampMoveGloat, 0, 9), 3);
    EXPECT_EQ(step_amount(d, r::kChampMoveGloat, 0, 19), 4);
    const r::MonsterMove* anger = d.move(r::kChampMoveAnger);
    ASSERT_NE(anger, nullptr);
    EXPECT_EQ(anger->intent, MonsterIntent::BUFF);
    ASSERT_EQ(anger->effect_count, 3);
    EXPECT_EQ(anger->effects[0].op, r::Opcode::REMOVE_DEBUFFS);
    EXPECT_EQ(anger->effects[1].op, r::Opcode::REMOVE_POWER);
    EXPECT_EQ(anger->effects[1].extra, static_cast<uint32_t>(PowerId::SHACKLED))
        << "the redundant Shackled remove the Java queues anyway (:170) -- "
           "the Time Eater HASTE precedent";
    EXPECT_EQ(anger->effects[2].amount.at(0), 6);
    EXPECT_EQ(anger->effects[2].amount.at(4), 9);
    EXPECT_EQ(anger->effects[2].amount.at(19), 12);

    // FACE_SLAP's debuffs and TAUNT are flat 2s (:75).
    const r::MonsterMove* slap = d.move(r::kChampMoveFaceSlap);
    ASSERT_NE(slap, nullptr);
    EXPECT_EQ(slap->intent, MonsterIntent::ATTACK_DEBUFF);
    ASSERT_EQ(slap->effect_count, 3);
    EXPECT_EQ(slap->effects[1].extra, static_cast<uint32_t>(PowerId::FRAIL));
    EXPECT_EQ(slap->effects[2].extra,
              static_cast<uint32_t>(PowerId::VULNERABLE));
    const r::MonsterMove* taunt = d.move(r::kChampMoveTaunt);
    ASSERT_NE(taunt, nullptr);
    EXPECT_EQ(taunt->intent, MonsterIntent::DEBUFF);
    EXPECT_EQ(taunt->effects[0].extra, static_cast<uint32_t>(PowerId::WEAK));
    EXPECT_EQ(taunt->effects[0].amount.at(kA20), 2);
}

TEST(CityBossesRegistry, CollectorTableIncludingTheComposedA19Block) {
    EXPECT_EQ(static_cast<int>(r::MonsterId::THE_COLLECTOR), 43);
    EXPECT_EQ(r::monster_from_game_id("TheCollector"),
              r::MonsterId::THE_COLLECTOR);
    const auto& d = r::kTheCollector;
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::BOSS);  // TheCollector.java:88
    EXPECT_EQ(d.hp_min(8), 282);
    EXPECT_EQ(d.hp_min(9), 300);

    EXPECT_EQ(step_amount(d, r::kTheCollectorMoveFireball, 0, 3), 18);
    EXPECT_EQ(step_amount(d, r::kTheCollectorMoveFireball, 0, 4), 21);

    // BUFF: the block column COMPOSES the A19 takeTurn-time +5 onto the A9
    // blockAmt (:141-145): 15 / 18 / 23, not 20. The Strength template tiers
    // 3/4/5 and is fanned natively over the whole live group.
    const r::MonsterMove* buff = d.move(r::kTheCollectorMoveBuff);
    ASSERT_NE(buff, nullptr);
    EXPECT_EQ(buff->intent, MonsterIntent::DEFEND_BUFF);
    ASSERT_EQ(buff->effect_count, 2);
    EXPECT_EQ(buff->effects[0].amount.at(8), 15);
    EXPECT_EQ(buff->effects[0].amount.at(9), 18);
    EXPECT_EQ(buff->effects[0].amount.at(19), 23)
        << "blockAmt + 5 at A19 (TheCollector.java:141-142) -- composed, not "
           "a restated literal";
    EXPECT_EQ(buff->effects[1].extra, static_cast<uint32_t>(PowerId::STRENGTH));
    EXPECT_EQ(buff->effects[1].amount.at(3), 3);
    EXPECT_EQ(buff->effects[1].amount.at(4), 4);
    EXPECT_EQ(buff->effects[1].amount.at(19), 5);

    // MEGA_DEBUFF: Weak, Vulnerable, Frail in that order (:156-158), each 3 --
    // STILL 3 at A4 (only rakeDmg and strAmt move on that rung) -- and 5 at
    // A19.
    const r::MonsterMove* mega = d.move(r::kTheCollectorMoveMegaDebuff);
    ASSERT_NE(mega, nullptr);
    EXPECT_EQ(mega->intent, MonsterIntent::STRONG_DEBUFF);
    ASSERT_EQ(mega->effect_count, 3);
    EXPECT_EQ(mega->effects[0].extra, static_cast<uint32_t>(PowerId::WEAK));
    EXPECT_EQ(mega->effects[1].extra,
              static_cast<uint32_t>(PowerId::VULNERABLE));
    EXPECT_EQ(mega->effects[2].extra, static_cast<uint32_t>(PowerId::FRAIL));
    for (uint8_t k = 0; k < 3; ++k) {
        EXPECT_EQ(mega->effects[k].amount.at(4), 3)
            << "the A4 rung keeps megaDebuffAmt 3 (step " << int(k) << ")";
        EXPECT_EQ(mega->effects[k].amount.at(19), 5);
    }

    const r::MonsterMove* spawn = d.move(r::kTheCollectorMoveSpawn);
    ASSERT_NE(spawn, nullptr);
    EXPECT_EQ(spawn->intent, MonsterIntent::UNKNOWN);
    const r::MonsterMove* revive = d.move(r::kTheCollectorMoveRevive);
    ASSERT_NE(revive, nullptr);
    EXPECT_EQ(revive->intent, MonsterIntent::UNKNOWN);
}

TEST(CityBossesRegistry, TorchHeadTable) {
    EXPECT_EQ(static_cast<int>(r::MonsterId::TORCH_HEAD), 44);
    EXPECT_EQ(r::monster_from_game_id("TorchHead"), r::MonsterId::TORCH_HEAD);
    const auto& d = r::kTorchHead;
    EXPECT_EQ(d.enemy_type, r::MonsterEnemyType::NORMAL);
    EXPECT_EQ(d.hp_min(0), 38);
    EXPECT_EQ(d.hp_max(8), 40);
    EXPECT_EQ(d.hp_min(9), 40);
    EXPECT_EQ(d.hp_max(kA20), 45);
    ASSERT_EQ(d.roll_count, 1);
    EXPECT_EQ(d.roll(0)->timing, r::MonsterRollTiming::CONSTRUCTOR_BEFORE_HP);
    EXPECT_EQ(d.roll(0)->min(kA20), 38);
    EXPECT_EQ(d.roll(0)->max(kA20), 40);
    EXPECT_EQ(step_count(d, r::kTorchHeadMoveTackle), 1);
    EXPECT_EQ(step_amount(d, r::kTorchHeadMoveTackle, 0, kA20), 7);
}

TEST(CityBossesRegistry, TheThreeBossEncountersResolveAsSingleEmits) {
    // TheCity.initializeBoss (TheCity.java:168-170) -> MonsterHelper's three
    // single-member cases (:522-530). S2.01 landed the rows; registering the
    // init fns is what un-parks them (the S2.23 precedent).
    for (const char* key : {"Automaton", "Collector", "Champ"}) {
        RngStream misc = from_seed(7);
        ResolvedGroup g{};
        ASSERT_TRUE(resolve_encounter(key, misc, g)) << key;
        EXPECT_EQ(g.count, 1) << key;
        EXPECT_EQ(misc.counter, 0) << key << ": a single EMIT draws nothing";
    }
    RngStream misc = from_seed(7);
    ResolvedGroup g{};
    ASSERT_TRUE(resolve_encounter("Automaton", misc, g));
    EXPECT_EQ(r::monster_from_game_id(g.members[0]),
              r::MonsterId::BRONZE_AUTOMATON);
}

// ============================================================================
// 2. Move selection
// ============================================================================

TEST(BronzeAutomatonMoves, InitConsumesOneRollAndOpensSpawnOrbs) {
    CombatState s = MakeSeeded(11);
    bronze_automaton_init(s, 0);
    EXPECT_EQ(s.ai_rng.counter, 1) << "getMove's firstTurn arm reads num on no "
                                      "path, but rollMove drew it";
    EXPECT_EQ(s.monster_hp_rng.counter, 1) << "one DEGENERATE setHp draw";
    EXPECT_EQ(s.monsters[0].hp, 320);  // the a9 column at the fixed A20
    EXPECT_EQ(s.monsters[0].move_history[0], r::kBronzeAutomatonMoveSpawnOrbs);
    EXPECT_EQ(s.monsters[0].intent,
              static_cast<uint8_t>(MonsterIntent::UNKNOWN));
    EXPECT_EQ(s.monsters[0].draw_x, kBronzeAutomatonDrawX);
}

TEST(BronzeAutomatonMoves, OnlyFlailAndBoostAdvanceTheBeamCounter) {
    // The counted cycle after the opener, hand-derived from getMove
    // (:149-174): FLAIL and BOOST alternate (each forbids itself via the
    // last-move tests) and ONLY those two arms ++numTurns; the 4th counted
    // decision telegraphs HYPER_BEAM and resets; the post-beam arm (no ++)
    // telegraphs BOOST at the fixed A20 (the A19 swap -- below 19 it is
    // STUNNED/STUN, an arm the module spells and the fixed ascension cannot
    // reach; the row's STUNNED intent column carries that side).
    CombatState s = MakeSeeded(12);
    bronze_automaton_init(s, 0);
    MonsterState& m = s.monsters[0];
    const uint8_t expect[] = {
        r::kBronzeAutomatonMoveFlail,      // after SPAWN_ORBS   (numTurns 1)
        r::kBronzeAutomatonMoveBoost,      //                    (numTurns 2)
        r::kBronzeAutomatonMoveFlail,      //                    (numTurns 3)
        r::kBronzeAutomatonMoveBoost,      //                    (numTurns 4)
        r::kBronzeAutomatonMoveHyperBeam,  // numTurns == 4 -> beam, reset
        r::kBronzeAutomatonMoveBoost,      // post-beam recovery (A19+ arm), no ++
        r::kBronzeAutomatonMoveFlail,      // lastMove(BOOST)    (numTurns 1)
    };
    for (const uint8_t want : expect) {
        bronze_automaton_decide_move(s, 0, 0);  // num is read by NO arm
        EXPECT_EQ(m.move_history[0], want);
    }
    EXPECT_EQ(m.pad0, 1);
}

TEST(BronzeOrbMoves, StasisIsOncePerCombatAndLatchedAtDecision) {
    CombatState s = MakeState();
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    // num >= 25 with the latch clear -> STASIS, and the latch flips NOW.
    bronze_orb_decide_move(s, 0, 25);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kBronzeOrbMoveStasis);
    EXPECT_EQ(s.monsters[0].intent,
              static_cast<uint8_t>(MonsterIntent::STRONG_DEBUFF));
    EXPECT_NE(s.monsters[0].flags & kMonsterFlagBronzeOrbUsedStasis, 0u);
    // The same num never steals again: >= 70 branches to SUPPORT_BEAM...
    bronze_orb_decide_move(s, 0, 70);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kBronzeOrbMoveSupportBeam);
    // ...and 25..69 falls through to BEAM.
    bronze_orb_decide_move(s, 0, 69);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kBronzeOrbMoveBeam);
}

TEST(BronzeOrbMoves, AlternationGuardsUseLastTwoMoves) {
    CombatState s = MakeState();
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    s.monsters[0].flags |= kMonsterFlagBronzeOrbUsedStasis;
    MonsterState& m = s.monsters[0];
    // Two SUPPORT_BEAMs, then num >= 70 must fall through to BEAM.
    bronze_orb_decide_move(s, 0, 99);
    bronze_orb_decide_move(s, 0, 99);
    EXPECT_EQ(m.move_history[0], r::kBronzeOrbMoveSupportBeam);
    EXPECT_EQ(m.move_history[1], r::kBronzeOrbMoveSupportBeam);
    bronze_orb_decide_move(s, 0, 99);
    EXPECT_EQ(m.move_history[0], r::kBronzeOrbMoveBeam);
    // Two BEAMs, then num < 70 lands on the final SUPPORT_BEAM arm (:100).
    bronze_orb_decide_move(s, 0, 0);
    EXPECT_EQ(m.move_history[0], r::kBronzeOrbMoveBeam);
    bronze_orb_decide_move(s, 0, 0);
    EXPECT_EQ(m.move_history[0], r::kBronzeOrbMoveSupportBeam);
}

TEST(ChampMoves, ThresholdLatchesAngerThenTheExecutePattern) {
    CombatState s = MakeSeeded(13);
    champ_init(s, 0);
    MonsterState& m = s.monsters[0];
    ASSERT_EQ(m.hp, 440);
    m.hp = 219;  // < 440 / 2 (integer division)
    champ_decide_move(s, 0, 99);
    EXPECT_EQ(m.move_history[0], r::kChampMoveAnger);
    EXPECT_NE(m.flags & kMonsterFlagChampThreshold, 0u);
    // Healing back above half does NOT disarm the latch...
    m.hp = 440;
    // ...and the EXECUTE arm fires whenever NEITHER of the last two decisions
    // was EXECUTE (num 99 keeps every roll arm out of the way): the pattern is
    // EXECUTE, filler, filler, EXECUTE -- lastMoveBefore (:268) makes it every
    // THIRD decision, not every other. The two fillers are the tail arms:
    // HEAVY_SLASH (not-last-1), then -- with lastMove == 1 -- FACE_SLAP (:300).
    champ_decide_move(s, 0, 99);
    EXPECT_EQ(m.move_history[0], r::kChampMoveExecute);
    champ_decide_move(s, 0, 99);
    EXPECT_EQ(m.move_history[0], r::kChampMoveHeavySlash)
        << "lastMove(3) blocks an immediate repeat";
    champ_decide_move(s, 0, 99);
    EXPECT_EQ(m.move_history[0], r::kChampMoveFaceSlap)
        << "lastMoveBefore(3) still blocks it one decision later";
    champ_decide_move(s, 0, 99);
    EXPECT_EQ(m.move_history[0], r::kChampMoveExecute)
        << "history [4, 1] -- neither slot is EXECUTE, the arm re-fires";
}

TEST(ChampMoves, TauntCadenceCountsEveryDecisionIncludingInit) {
    CombatState s = MakeSeeded(14);
    champ_init(s, 0);  // ++numTurns ran on the init roll already
    MonsterState& m = s.monsters[0];
    ASSERT_EQ(champ_num_turns(m), 1);
    // ++numTurns runs FIRST, so the call that takes the counter to 4 IS the
    // taunt (:262,:273): two non-taunt fillers, then TAUNT on the third
    // decision after init. num 99 keeps every roll arm out of the way.
    champ_decide_move(s, 0, 99);
    ASSERT_NE(m.move_history[0], r::kChampMoveTaunt);  // numTurns 2
    champ_decide_move(s, 0, 99);
    ASSERT_NE(m.move_history[0], r::kChampMoveTaunt);  // numTurns 3
    champ_decide_move(s, 0, 99);                       // ++ -> 4 -> TAUNT
    EXPECT_EQ(m.move_history[0], r::kChampMoveTaunt);
    EXPECT_EQ(m.intent, static_cast<uint8_t>(MonsterIntent::DEBUFF));
    EXPECT_EQ(champ_num_turns(m), 0) << "the taunt arm resets the counter";
    champ_decide_move(s, 0, 99);
    EXPECT_NE(m.move_history[0], r::kChampMoveTaunt);
    EXPECT_EQ(champ_num_turns(m), 1);
}

TEST(ChampMoves, ForgeArmCountsToTwoAtTheWideA19Bound) {
    CombatState s = MakeSeeded(15);
    champ_init(s, 0);
    MonsterState& m = s.monsters[0];
    // At the fixed A20 the forge bound is num <= 30 (below A19 it is 15 --
    // the module spells both arms; this drives the live one at its edge).
    // Reset any opener state: force a non-STANCE history.
    set_monster_move(m, r::kChampMoveHeavySlash, MonsterIntent::ATTACK);
    set_monster_move(m, r::kChampMoveHeavySlash, MonsterIntent::ATTACK);
    m.pad0 = 0;  // numTurns 0 / forgeTimes 0 (isolate the arm under test)
    champ_decide_move(s, 0, 30);
    EXPECT_EQ(m.move_history[0], r::kChampMoveDefensiveStance)
        << "30 <= 30 at A19+; at A18 this same num would fall through";
    EXPECT_EQ(champ_forge_times(m), 1);
    // lastMove(STANCE) blocks a repeat...
    champ_decide_move(s, 0, 30);
    EXPECT_NE(m.move_history[0], r::kChampMoveDefensiveStance);
    // ...one more is allowed after a gap...
    champ_decide_move(s, 0, 30);
    EXPECT_EQ(m.move_history[0], r::kChampMoveDefensiveStance);
    EXPECT_EQ(champ_forge_times(m), 2);
    // ...and the threshold of 2 then retires the arm for the combat.
    champ_decide_move(s, 0, 30);
    ASSERT_NE(m.move_history[0], r::kChampMoveDefensiveStance);
    champ_decide_move(s, 0, 30);
    EXPECT_NE(m.move_history[0], r::kChampMoveDefensiveStance);
}

TEST(CollectorMoves, OpeningSpawnThenMegaDebuffAfterThreeTurns) {
    CombatState s = MakeSeeded(16);
    collector_init(s, 0);
    MonsterState& m = s.monsters[0];
    EXPECT_EQ(m.hp, 300);  // a9 column
    EXPECT_EQ(m.move_history[0], r::kTheCollectorMoveSpawn);
    EXPECT_EQ(m.intent, static_cast<uint8_t>(MonsterIntent::UNKNOWN));
    EXPECT_NE(m.flags & kMonsterFlagCollectorInitialSpawn, 0u);
    // The latch clears in takeTurn, not at decision time; until then every
    // re-roll re-telegraphs SPAWN.
    collector_decide_move(s, 0, 99);
    EXPECT_EQ(m.move_history[0], r::kTheCollectorMoveSpawn);
    m.flags &= ~kMonsterFlagCollectorInitialSpawn;  // takeTurn case 1 (:133)
    // turnsTaken >= 3 && !ultUsed -> MEGA_DEBUFF, whatever the roll.
    m.pad0 = 3;
    collector_decide_move(s, 0, 99);
    EXPECT_EQ(m.move_history[0], r::kTheCollectorMoveMegaDebuff);
    EXPECT_EQ(m.intent, static_cast<uint8_t>(MonsterIntent::STRONG_DEBUFF));
    // ultUsed (a takeTurn write) retires the arm; num 99 then reaches the
    // BUFF/FIREBALL tail.
    m.flags |= kMonsterFlagCollectorUltUsed;
    collector_decide_move(s, 0, 99);
    EXPECT_EQ(m.move_history[0], r::kTheCollectorMoveBuff);
    collector_decide_move(s, 0, 99);
    EXPECT_EQ(m.move_history[0], r::kTheCollectorMoveFireball)
        << "lastMove(BUFF) -> the :201 else arm";
}

TEST(CollectorMoves, ReviveNeedsADeadSlotAndTheDerivedMapIsExact) {
    CombatState s = MakeSeeded(17);
    collector_init(s, 0);
    MonsterState& m = s.monsters[0];
    m.flags &= ~kMonsterFlagCollectorInitialSpawn;
    EXPECT_FALSE(collector_is_minion_dead(s))
        << "never-spawned slots have no map entry (isMinionDead over an empty "
           "map is false)";
    collector_decide_move(s, 0, 0);
    EXPECT_NE(m.move_history[0], r::kTheCollectorMoveRevive);

    // Hand-build the post-spawn group: [t2(-470), t1(-285), collector(60)].
    CombatState g = MakeSeeded(18, 3);
    collector_init(g, 2);
    g.monsters[2].flags &= ~kMonsterFlagCollectorInitialSpawn;
    g.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::TORCH_HEAD);
    g.monsters[0].hp = g.monsters[0].max_hp = 40;
    g.monsters[0].draw_x = kTorchHeadSlotX[1];
    g.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::TORCH_HEAD);
    g.monsters[1].hp = g.monsters[1].max_hp = 40;
    g.monsters[1].draw_x = kTorchHeadSlotX[0];
    EXPECT_FALSE(collector_is_minion_dead(g));
    g.monsters[1].hp = 0;  // slot key 1's occupant died
    EXPECT_TRUE(collector_is_minion_dead(g));
    collector_decide_move(g, 2, 25);
    EXPECT_EQ(g.monsters[2].move_history[0], r::kTheCollectorMoveRevive);
    // ...but never twice in a row (:192's !lastMove(5)).
    collector_decide_move(g, 2, 25);
    EXPECT_NE(g.monsters[2].move_history[0], r::kTheCollectorMoveRevive);
}

// ============================================================================
// 3. Turn bodies -- stream order, spawn layout, programs
// ============================================================================

TEST(BronzeAutomatonTurn, PreBattleQueuesArtifactThreeFlat) {
    CombatState s = MakeSeeded(19);
    bronze_automaton_init(s, 0);
    bronze_automaton_use_pre_battle_action(s, 0);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER));
    EXPECT_EQ(queued(s, 0).amount, 3) << "a FLAT 3 at every ascension "
                                         "(BronzeAutomaton.java:103)";
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::ARTIFACT), 3);
}

TEST(BronzeAutomatonTurn, SpawnOrbsDrawsAtQueueTimeAndAppliesMinionAddToTop) {
    constexpr int64_t kSeed = 20;
    CombatState s = MakeSeeded(kSeed);
    bronze_automaton_init(s, 0);
    ASSERT_EQ(s.monsters[0].move_history[0], r::kBronzeAutomatonMoveSpawnOrbs);

    const int32_t hp_before = s.monster_hp_rng.counter;
    const int32_t ai_before = s.ai_rng.counter;
    bronze_automaton_take_turn(s, 0);

    // QUEUE time: both orbs' ctor pairs (super-arg + setHp) and NOTHING else.
    EXPECT_EQ(s.monster_hp_rng.counter, hp_before + 4);
    EXPECT_EQ(s.ai_rng.counter, ai_before) << "the init rolls are resolve-time";

    // The queue is [spawn(0)][minion(0)][spawn(2)][minion(2)][roll(1)] -- the
    // Minion applies sit in SpawnMonsterAction's addToTop position (the item
    // right behind each spawn), and the boss's own roll is pre-aimed at its
    // post-insertion index 1.
    ASSERT_EQ(s.action_count, 5);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::SPAWN_MONSTER));
    EXPECT_EQ(queued(s, 0).tgt, 0);
    EXPECT_EQ(queued(s, 1).opcode, static_cast<uint16_t>(Opcode::APPLY_POWER));
    EXPECT_EQ(queued(s, 1).tgt, 0);
    EXPECT_EQ(queued(s, 1).amount, kMinionAppliedAmount);
    EXPECT_EQ(queued(s, 2).opcode, static_cast<uint16_t>(Opcode::SPAWN_MONSTER));
    EXPECT_EQ(queued(s, 2).tgt, 2);
    EXPECT_EQ(queued(s, 3).tgt, 2);
    EXPECT_EQ(queued(s, 4).opcode, static_cast<uint16_t>(Opcode::ROLL_MOVE));
    EXPECT_EQ(queued(s, 4).tgt, 1);

    // Re-derive both orbs' HP by hand off the same seed: the boss's degenerate
    // draw, then per orb the FLAT (52,58) super-arg (discarded) and the a9
    // (54,60) setHp.
    RngStream replay = from_seed(kSeed);
    (void)random(replay, 320, 320);
    (void)random(replay, 52, 58);
    const int32_t orb1_hp = random(replay, 54, 60);
    (void)random(replay, 52, 58);
    const int32_t orb2_hp = random(replay, 54, 60);

    drain(s);
    ASSERT_EQ(s.monster_count, 3);
    // Smart positioning: orb 1 (-300) inserts BEFORE the boss (-50), orb 2
    // (+200) AFTER it -- the on-screen [orb, boss, orb] layout.
    EXPECT_EQ(s.monsters[0].monster_id,
              static_cast<uint16_t>(MonsterId::BRONZE_ORB));
    EXPECT_EQ(s.monsters[0].draw_x, kBronzeOrbSlotX[0]);
    EXPECT_EQ(s.monsters[1].monster_id,
              static_cast<uint16_t>(MonsterId::BRONZE_AUTOMATON));
    EXPECT_EQ(s.monsters[2].monster_id,
              static_cast<uint16_t>(MonsterId::BRONZE_ORB));
    EXPECT_EQ(s.monsters[2].draw_x, kBronzeOrbSlotX[1]);
    EXPECT_EQ(s.monsters[0].hp, orb1_hp);
    EXPECT_EQ(s.monsters[2].hp, orb2_hp);
    EXPECT_EQ(monster_power(s, 0, PowerId::MINION), kMinionAppliedAmount);
    EXPECT_EQ(monster_power(s, 2, PowerId::MINION), kMinionAppliedAmount);
    EXPECT_EQ(monster_power(s, 1, PowerId::MINION), -1) << "never the boss";

    // RESOLVE time: orb 1's init roll, orb 2's init roll, the boss's roll --
    // three ai_rng draws, the boss's LAST (it re-telegraphs off lastMove(4)).
    EXPECT_EQ(s.ai_rng.counter, ai_before + 3);
    EXPECT_EQ(s.monsters[1].move_history[0], r::kBronzeAutomatonMoveFlail);
    // Each orb's opener came from ITS OWN draw: STASIS at num >= 25 (latched),
    // else BEAM.
    for (const uint8_t oi : {uint8_t{0}, uint8_t{2}}) {
        const uint8_t mv = s.monsters[oi].move_history[0];
        EXPECT_TRUE(mv == r::kBronzeOrbMoveStasis || mv == r::kBronzeOrbMoveBeam)
            << int(mv);
        EXPECT_EQ((s.monsters[oi].flags & kMonsterFlagBronzeOrbUsedStasis) != 0u,
                  mv == r::kBronzeOrbMoveStasis);
    }
}

TEST(BronzeAutomatonTurn, AStunnedTurnStillSpendsTheTrailingRoll) {
    CombatState s = MakeSeeded(21);
    bronze_automaton_init(s, 0);
    set_monster_move(s.monsters[0], r::kBronzeAutomatonMoveStunned,
                     MonsterIntent::STUN);
    const int32_t ai_before = s.ai_rng.counter;
    bronze_automaton_take_turn(s, 0);
    // The authored NOP plus the roll -- the RollMoveAction sits OUTSIDE the
    // Java switch (:145), so a stunned turn is not a free turn on the stream.
    drain(s);
    EXPECT_EQ(s.ai_rng.counter, ai_before + 1);
    // Post-STUNNED the decision is FLAIL (:168).
    EXPECT_EQ(s.monsters[0].move_history[0], r::kBronzeAutomatonMoveFlail);
}

TEST(CollectorTurn, OpeningSpawnPutsBothTorchHeadsLeftOfTheBoss) {
    constexpr int64_t kSeed = 22;
    CombatState s = MakeSeeded(kSeed);
    collector_init(s, 0);
    ASSERT_EQ(s.monsters[0].move_history[0], r::kTheCollectorMoveSpawn);

    const int32_t hp_before = s.monster_hp_rng.counter;
    collector_take_turn(s, 0);
    EXPECT_EQ(s.monster_hp_rng.counter, hp_before + 4)
        << "both TorchHead ctor pairs at queue time, key order 1 then 2";
    EXPECT_EQ(s.monsters[0].flags & kMonsterFlagCollectorInitialSpawn, 0u)
        << "initialSpawn = false is a takeTurn-time write (:133)";
    EXPECT_EQ(s.monsters[0].pad0, 1) << "++turnsTaken sits outside the switch";

    // Re-derive both heads' HP: FLAT (38,40) super-arg then a9 (40,45) setHp.
    RngStream replay = from_seed(kSeed);
    (void)random(replay, 282, 282);
    (void)random(replay, 38, 40);
    const int32_t t1_hp = random(replay, 40, 45);
    (void)random(replay, 38, 40);
    const int32_t t2_hp = random(replay, 40, 45);

    drain(s);
    ASSERT_EQ(s.monster_count, 3);
    // Key 1's head (-285) inserts at 0 -> [t1, coll]; key 2's (-470) is left
    // of it -> [t2, t1, coll].
    EXPECT_EQ(s.monsters[0].draw_x, kTorchHeadSlotX[1]);
    EXPECT_EQ(s.monsters[0].hp, t2_hp);
    EXPECT_EQ(s.monsters[1].draw_x, kTorchHeadSlotX[0]);
    EXPECT_EQ(s.monsters[1].hp, t1_hp);
    EXPECT_EQ(s.monsters[2].monster_id,
              static_cast<uint16_t>(MonsterId::THE_COLLECTOR));
    EXPECT_EQ(monster_power(s, 0, PowerId::MINION), kMinionAppliedAmount);
    EXPECT_EQ(monster_power(s, 1, PowerId::MINION), kMinionAppliedAmount);
    // Both heads telegraph off their ctor push + init re-push: [1, 1].
    for (uint8_t i = 0; i < 2; ++i) {
        EXPECT_EQ(s.monsters[i].move_history[0], r::kTorchHeadMoveTackle);
        EXPECT_EQ(s.monsters[i].move_history[1], r::kTorchHeadMoveTackle);
        EXPECT_EQ(s.monsters[i].intent,
                  static_cast<uint8_t>(MonsterIntent::ATTACK));
    }
}

TEST(CollectorTurn, ReviveRefillsOnlyTheDeadSlotBeforeTheDeadRecord) {
    constexpr int64_t kSeed = 23;
    CombatState s = MakeSeeded(kSeed);
    collector_init(s, 0);
    collector_take_turn(s, 0);
    drain(s);  // [t2, t1, coll]
    ASSERT_EQ(s.monster_count, 3);
    s.monsters[1].hp = 0;  // slot key 1's occupant (x -285) dies

    set_monster_move(s.monsters[2], r::kTheCollectorMoveRevive,
                     MonsterIntent::UNKNOWN);
    const int32_t hp_before = s.monster_hp_rng.counter;
    collector_take_turn(s, 2);
    EXPECT_EQ(s.monster_hp_rng.counter, hp_before + 2)
        << "ONE replacement head only -- the live slot is not respawned";
    drain(s);
    ASSERT_EQ(s.monster_count, 4);
    // The newcomer (-285) walks past t2 (-470), stops at the DEAD t1 (-285):
    // strict `>` inserts BEFORE an equal draw_x -- position recycling.
    EXPECT_EQ(s.monsters[1].draw_x, kTorchHeadSlotX[0]);
    EXPECT_GT(s.monsters[1].hp, 0) << "the replacement";
    EXPECT_EQ(s.monsters[2].draw_x, kTorchHeadSlotX[0]);
    EXPECT_EQ(s.monsters[2].hp, 0) << "the corpse keeps its record and slot x";
    EXPECT_EQ(s.monsters[3].monster_id,
              static_cast<uint16_t>(MonsterId::THE_COLLECTOR));
    EXPECT_FALSE(collector_is_minion_dead(s))
        << "a live occupant at each spawned x again";
}

TEST(CollectorTurn, BuffFansStrengthOverEveryLiveRecordIncludingItself) {
    CombatState s = MakeSeeded(24);
    collector_init(s, 0);
    collector_take_turn(s, 0);
    drain(s);  // [t2, t1, coll]
    ASSERT_EQ(s.monster_count, 3);
    s.monsters[0].hp = 0;  // one dead head: skipped by the walk
    set_monster_move(s.monsters[2], r::kTheCollectorMoveBuff,
                     MonsterIntent::DEFEND_BUFF);
    collector_take_turn(s, 2);
    drain(s);
    EXPECT_EQ(s.monsters[2].block, 23) << "blockAmt + 5 at the fixed A20";
    EXPECT_EQ(monster_power(s, 2, PowerId::STRENGTH), 5) << "itself included";
    EXPECT_EQ(monster_power(s, 1, PowerId::STRENGTH), 5);
    EXPECT_EQ(find_monster_power(s, 0, PowerId::STRENGTH), nullptr)
        << "isDying records are skipped (:147)";
}

TEST(TorchHeadTurn, OneLifetimeDrawAndTheSetMoveRetelegraph) {
    CombatState s = MakeSeeded(25, 1);
    s.monsters[0] = MonsterState{};
    torch_head_spawn_at_hp(s, 0, 42);
    EXPECT_EQ(s.ai_rng.counter, 1);
    EXPECT_EQ(s.monsters[0].move_history[0], r::kTorchHeadMoveTackle);
    EXPECT_EQ(s.monsters[0].move_history[1], r::kTorchHeadMoveTackle)
        << "the ctor telegraph (:45) plus init's re-push (:82)";
    torch_head_take_turn(s, 0);
    ASSERT_EQ(s.action_count, 2);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::DAMAGE));
    EXPECT_EQ(queued(s, 0).amount, 7);
    EXPECT_EQ(queued(s, 1).opcode, static_cast<uint16_t>(Opcode::SET_MOVE))
        << "no trailing RollMoveAction (:63) -- the Transient's shape";
    drain(s);
    EXPECT_EQ(s.ai_rng.counter, 1) << "zero draws per turn, forever";
    EXPECT_EQ(s.monsters[0].move_history[0], r::kTorchHeadMoveTackle);
}

TEST(ChampTurn, AngerStripsDebuffsRemovesShackledAndTriplesStrength) {
    CombatState s = MakeSeeded(26);
    champ_init(s, 0);
    // Give the Champ two debuffs, Shackled, and a buff that must survive.
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    it.src = kActorPlayer;
    it.tgt = 0;
    it.amount = 2;
    it.flags = make_apply_power_flags(PowerId::VULNERABLE, 0, false);
    execute_opcode(s, it);
    it.flags = make_apply_power_flags(PowerId::WEAK, 0, false);
    execute_opcode(s, it);
    it.flags = make_apply_power_flags(PowerId::SHACKLED);
    execute_opcode(s, it);
    it.flags = make_apply_power_flags(PowerId::METALLICIZE);
    execute_opcode(s, it);
    set_monster_move(s.monsters[0], r::kChampMoveAnger, MonsterIntent::BUFF);
    champ_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(monster_power(s, 0, PowerId::VULNERABLE), -1);
    EXPECT_EQ(monster_power(s, 0, PowerId::WEAK), -1);
    EXPECT_EQ(monster_power(s, 0, PowerId::SHACKLED), -1);
    EXPECT_EQ(monster_power(s, 0, PowerId::METALLICIZE), 2)
        << "ANGER strips DEBUFFS (plus Shackled by name); buffs survive";
    EXPECT_EQ(monster_power(s, 0, PowerId::STRENGTH), 12)
        << "strAmt * 3 == 12 at the fixed A20";
}

TEST(ChampTurn, FaceSlapAppliesFrailThenVulnerable) {
    CombatState s = MakeSeeded(27);
    champ_init(s, 0);
    set_monster_move(s.monsters[0], r::kChampMoveFaceSlap,
                     MonsterIntent::ATTACK_DEBUFF);
    champ_take_turn(s, 0);
    drain(s);
    EXPECT_EQ(s.player_hp, 400 - 14);
    EXPECT_EQ(player_power_amount(s, PowerId::FRAIL), 2);
    EXPECT_EQ(player_power_amount(s, PowerId::VULNERABLE), 2);
}

// ============================================================================
// 4. Stasis -- the theft, the hold, the give-back
// ============================================================================

TEST(Stasis, RarityCascadePrefersRareAndSortsByCardId) {
    constexpr int64_t kSeed = 30;
    CombatState s = MakeSeeded(kSeed);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    s.monsters[0].hp = s.monsters[0].max_hp = 56;
    // Draw pile, bottom to top: a BASIC, then the two rares in REVERSE
    // alphabetical order, then a COMMON status. The candidate view is the two
    // rares, sorted by cardID: ["Barricade", "Impervious"] -- NOT pile order.
    (void)add_to_draw(s, CardId::STRIKE);
    const uint8_t imperv = add_to_draw(s, CardId::IMPERVIOUS);
    const uint8_t barricade = add_to_draw(s, CardId::BARRICADE);
    (void)add_to_draw(s, CardId::WOUND);

    RngStream replay = from_seed(kSeed);
    const int32_t pick = random(replay, 1);  // ONE draw over the 2-entry view
    const uint8_t expect = pick == 0 ? barricade : imperv;

    run_stasis(s, 0);
    EXPECT_EQ(s.card_random_rng.counter, 1)
        << "the RARE pass matched, so UNCOMMON/COMMON/unfiltered never drew";
    EXPECT_EQ(s.draw_count, 3);
    EXPECT_FALSE(in_pile(s, s.draw, s.draw_count, expect));
    ASSERT_EQ(s.limbo_count, 1);
    EXPECT_EQ(s.limbo[0], expect);
    const PowerSlot* p = find_monster_power(s, 0, PowerId::STASIS);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->amount, -1) << "StasisPower.java:27 -- explicit -1";
    EXPECT_EQ(p->counter, static_cast<int16_t>(expect + 1))
        << "the stolen pool index rides the slot counter, +1-biased";
}

TEST(Stasis, BasicsNeverMatchAndStatusesAreCommon) {
    CombatState s = MakeSeeded(31);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    s.monsters[0].hp = s.monsters[0].max_hp = 56;
    (void)add_to_draw(s, CardId::STRIKE);      // BASIC -- invisible to all passes
    const uint8_t wound = add_to_draw(s, CardId::WOUND);  // STATUS == COMMON
    (void)add_to_draw(s, CardId::STRIKE);
    run_stasis(s, 0);
    EXPECT_EQ(s.card_random_rng.counter, 1)
        << "RARE and UNCOMMON views were empty -- null WITHOUT a draw -- and "
           "COMMON matched the Wound alone";
    ASSERT_EQ(s.limbo_count, 1);
    EXPECT_EQ(s.limbo[0], wound);
}

TEST(Stasis, UnfilteredFallbackIndexesPileOrderUnsorted) {
    constexpr int64_t kSeed = 32;
    CombatState s = MakeSeeded(kSeed);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    s.monsters[0].hp = s.monsters[0].max_hp = 56;
    // Basics + a SPECIAL curse: no RARE/UNCOMMON/COMMON anywhere, so the pick
    // is getRandomCard(rng) over PILE ORDER (CardGroup.java:498-500).
    uint8_t pool[3];
    pool[0] = add_to_draw(s, CardId::STRIKE);
    pool[1] = add_to_draw(s, CardId::ASCENDERS_BANE);
    pool[2] = add_to_draw(s, CardId::STRIKE);
    RngStream replay = from_seed(kSeed);
    const int32_t idx = random(replay, 2);
    run_stasis(s, 0);
    EXPECT_EQ(s.card_random_rng.counter, 1)
        << "three empty filtered views cost nothing; the fallback drew once";
    ASSERT_EQ(s.limbo_count, 1);
    EXPECT_EQ(s.limbo[0], pool[idx]);
}

TEST(Stasis, EmptyPilesStealNothingAndDrawNothing) {
    CombatState s = MakeSeeded(33);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    s.monsters[0].hp = s.monsters[0].max_hp = 56;
    run_stasis(s, 0);
    EXPECT_EQ(s.card_random_rng.counter, 0);
    EXPECT_EQ(s.limbo_count, 0);
    EXPECT_EQ(find_monster_power(s, 0, PowerId::STASIS), nullptr)
        << "ApplyStasisAction.java:34-37 -- done before the power is built";
}

TEST(Stasis, DiscardIsTheSourceOnlyWhenTheDrawPileIsEmpty) {
    CombatState s = MakeSeeded(34);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    s.monsters[0].hp = s.monsters[0].max_hp = 56;
    const uint8_t pummel = add_to_discard(s, CardId::PUMMEL);  // UNCOMMON
    (void)add_to_discard(s, CardId::STRIKE);
    run_stasis(s, 0);
    ASSERT_EQ(s.limbo_count, 1);
    EXPECT_EQ(s.limbo[0], pummel);
    EXPECT_EQ(s.discard_count, 1);
    // ...and with BOTH piles populated the draw pile wins outright.
    CombatState t = MakeSeeded(35);
    t.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    t.monsters[0].hp = t.monsters[0].max_hp = 56;
    const uint8_t in_draw = add_to_draw(t, CardId::SHRUG_IT_OFF);
    (void)add_to_discard(t, CardId::PUMMEL);
    run_stasis(t, 0);
    ASSERT_EQ(t.limbo_count, 1);
    EXPECT_EQ(t.limbo[0], in_draw) << "the discard is untouched (:39)";
    EXPECT_EQ(t.discard_count, 1);
}

TEST(Stasis, DeathReturnsTheOriginalInstanceToTheHand) {
    CombatState s = MakeSeeded(36);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    s.monsters[0].hp = s.monsters[0].max_hp = 20;
    const uint8_t stolen = add_to_draw(s, CardId::SHRUG_IT_OFF);
    s.card_pool[stolen].upgrade = 1;  // the ORIGINAL row moves: upgrade survives
    run_stasis(s, 0);
    ASSERT_EQ(s.limbo_count, 1);
    player_attacks(s, 0, 25);  // lethal -> the death edge -> Stasis onDeath
    ASSERT_EQ(s.monsters[0].hp, 0);
    drain(s);  // the queued STASIS_RETURN
    EXPECT_EQ(s.limbo_count, 0);
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], stolen);
    EXPECT_EQ(s.card_pool[stolen].upgrade, 1);
}

TEST(Stasis, HandFullAtDeathSendsTheCardToTheDiscardInstead) {
    CombatState s = MakeSeeded(37);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    s.monsters[0].hp = s.monsters[0].max_hp = 20;
    const uint8_t stolen = add_to_draw(s, CardId::SHRUG_IT_OFF);
    run_stasis(s, 0);
    for (int i = 0; i < kHandCap; ++i) {
        (void)add_pool_card(s, CardId::STRIKE, s.hand, s.hand_count);
    }
    player_attacks(s, 0, 25);
    drain(s);
    EXPECT_EQ(s.hand_count, kHandCap);
    ASSERT_EQ(s.discard_count, 1);
    EXPECT_EQ(s.discard[0], stolen)
        << "hand.size() == 10 at the onDeath read -> the discard action "
           "(StasisPower.java:39-43)";
}

TEST(Stasis, HandFillingBetweenQueueAndResolveSpillsAtResolve) {
    CombatState s = MakeSeeded(38);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    s.monsters[0].hp = s.monsters[0].max_hp = 20;
    const uint8_t stolen = add_to_draw(s, CardId::SHRUG_IT_OFF);
    run_stasis(s, 0);
    for (int i = 0; i < 9; ++i) {
        (void)add_pool_card(s, CardId::STRIKE, s.hand, s.hand_count);
    }
    player_attacks(s, 0, 25);  // queue-time read: hand 9 != 10 -> HAND arm
    ASSERT_EQ(s.action_count, 1);
    EXPECT_NE(queued(s, 0).flags & kStasisReturnToHandBit, 0u);
    // The hand fills before the item resolves...
    (void)add_pool_card(s, CardId::STRIKE, s.hand, s.hand_count);
    drain(s);
    // ...so MakeTempCardInHandAction's own cap check spills to the discard.
    EXPECT_EQ(s.hand_count, kHandCap);
    ASSERT_EQ(s.discard_count, 1);
    EXPECT_EQ(s.discard[0], stolen);
}

TEST(Stasis, TheBossDeathSweepReturnsEveryStolenCard) {
    CombatState s = MakeSeeded(39, 2);
    // [orb, boss] -- the sweep's forward walk + add_to_top gives reverse slot
    // order, invisible here with one survivor but pinned structurally.
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    s.monsters[0].hp = s.monsters[0].max_hp = 56;
    s.monsters[0].draw_x = kBronzeOrbSlotX[0];
    bronze_automaton_init(s, 1);
    s.monsters[1].hp = 5;
    const uint8_t stolen = add_to_draw(s, CardId::SHRUG_IT_OFF);
    run_stasis(s, 0);
    ASSERT_EQ(s.limbo_count, 1);

    player_attacks(s, 1, 10);  // kill the BOSS
    ASSERT_EQ(s.monsters[1].hp, 0);
    // die_after queued one SUICIDE (relicTrigger TRUE) for the orb.
    ASSERT_GE(s.action_count, 1);
    EXPECT_EQ(queued(s, 0).opcode, static_cast<uint16_t>(Opcode::SUICIDE));
    EXPECT_EQ(queued(s, 0).flags & 1u, 1u);
    drain(s);
    EXPECT_EQ(s.monsters[0].hp, 0) << "the orb was swept";
    EXPECT_EQ(s.limbo_count, 0);
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], stolen)
        << "SuicideAction(m) -> die(true) -> the orb's powers' onDeath -> the "
           "give-back, exactly the killing-the-Automaton flow";
}

TEST(StasisKnowledge, TheftRemovesTheStolenIndexFromTheChain) {
    KnowledgeState k{};
    KnowledgeScope scope(&k);
    CombatState s = MakeSeeded(40);
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    s.monsters[0].hp = s.monsters[0].max_hp = 56;
    // Only one RARE, so the steal is deterministic; the player knows the top.
    const uint8_t rare = add_to_draw(s, CardId::IMPERVIOUS);
    const uint8_t top = add_to_draw(s, CardId::STRIKE);
    knowledge_on_place_top(s, top);
    ASSERT_EQ(k.chain_count, 1);
    ASSERT_EQ(k.exact_prefix, 1);

    run_stasis(s, 0);
    ASSERT_EQ(s.limbo_count, 1);
    ASSERT_EQ(s.limbo[0], rare);
    // The stolen card was NOT the known top, and an exact top position
    // excludes it having sat above the known card -- the constraint survives.
    EXPECT_EQ(k.chain_count, 1);
    EXPECT_EQ(k.chain[0], top);
    EXPECT_EQ(k.exact_prefix, 1);

    // Steal again: the only card left that matches ANY pass is the known top
    // (a BASIC matches nothing; use a fresh orb record for the second latch).
    s.monsters[0].flags &= ~kMonsterFlagBronzeOrbUsedStasis;
    (void)add_to_draw(s, CardId::WOUND);
    run_stasis(s, 0);
    // The Wound (COMMON) was stolen, not the Strike -- but if the chain entry
    // ever leaves the pile its hook must clear it; pin the invariant that the
    // chain never names a card outside the draw pile.
    for (uint8_t i = 0; i < k.chain_count; ++i) {
        EXPECT_TRUE(in_pile(s, s.draw, s.draw_count, k.chain[i]));
    }
}

// ============================================================================
// 5. Boss-flag typing -- the Pantograph-style consumer
// ============================================================================

TEST(CityBossesPantograph, HealsThroughTheBossRecordNotTheMinions) {
    // Pantograph.atBattleStart (Pantograph.java:32-40) scans the GROUP for
    // m.type == EnemyType.BOSS. In the live Automaton fight the minions do not
    // exist at battle start; this synthetic order stresses the walk anyway --
    // the boss found PAST a NORMAL-typed minion still heals, once.
    CombatState s = MakeState(2);
    s.player_hp = 300;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::BRONZE_ORB);
    s.monsters[1].monster_id =
        static_cast<uint16_t>(MonsterId::BRONZE_AUTOMATON);
    RelicSlot pantograph{};
    pantograph.relic_id = static_cast<uint16_t>(RelicId::PANTOGRAPH);
    pantograph.counter = -1;
    dispatch_relics_at_battle_start(s, &pantograph, 1);
    EXPECT_EQ(s.player_hp, 325);

    // A lone minion is NOT a boss fight -- the NORMAL enemy_type is the live
    // datum Pantograph reads through MonsterDef::is_boss().
    CombatState t = MakeState(1);
    t.player_hp = 300;
    t.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::TORCH_HEAD);
    dispatch_relics_at_battle_start(t, &pantograph, 1);
    EXPECT_EQ(t.player_hp, 300);

    // All three bosses satisfy the scan.
    for (const MonsterId boss : {MonsterId::BRONZE_AUTOMATON, MonsterId::CHAMP,
                                 MonsterId::THE_COLLECTOR}) {
        CombatState b = MakeState(1);
        b.player_hp = 300;
        b.monsters[0].monster_id = static_cast<uint16_t>(boss);
        dispatch_relics_at_battle_start(b, &pantograph, 1);
        EXPECT_EQ(b.player_hp, 325) << static_cast<int>(boss);
    }
}

}  // namespace
}  // namespace sts::engine
