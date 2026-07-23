// B3.3 red common attacks (tier-2, constructed states). One table test per card,
// BOTH upgrade rows, every expected value hand-computed from the cited use() in
// D:\STS_BG_Mod\SlayTheSpireDecompiled\...\cards\red -- never read back from the
// implementation. New engine mechanics exercised (all cited at their card):
//   * Body Slam         -- DAMAGE_BLOCK: base == current player block (BodySlam.java:96).
//   * Heavy Blade       -- DAMAGE_STR_MULT: Strength x magic (HeavyBlade.java:426-435).
//   * Perfected Strike  -- DAMAGE_PER_STRIKE: +magic per "Strike"-named card, source
//                          excluded (PerfectedStrike.java:565-607).
//   * Anger             -- MAKE_CARD self-copy to discard, upgrade preserved
//                          (Anger.java:42; makeStatEquivalentCopy).
//   * Wild Strike       -- MAKE_CARD Wound to a random draw-pile spot (WildStrike.java:840).
//   * Cleave/Thunderclap-- ALL_ENEMY AoE (separate DamageInfo per live monster).
//   * Sword Boomerang   -- RANDOM_ENEMY per-hit roll (one card_random_rng draw/hit).
//   * Headbutt          -- CHOOSE_CARD discard_to_draw_top, just-played source
//                          excluded (DiscardPileToTopOfDeckAction; useCard:1369-1375).
//   * Clash             -- canUse: playable only if every hand card is an Attack.

#include <cstdint>
#include <span>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

// One monster, `id` (cost `cost`) as the sole hand card at pool index 0.
CombatState MakeState(CardId id, uint8_t cost, int16_t energy = 3,
                      int16_t monster_hp = 50) {
    CombatState s{};
    s.card_pool[0].card_id = static_cast<uint16_t>(id);
    s.card_pool[0].cost_now = cost;
    s.hand[0] = 0;
    s.hand_count = 1;
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = energy;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = monster_hp;
    s.monsters[0].max_hp = monster_hp;
    s.monster_attacks_queued = 1;
    s.turn_has_ended = 0;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

// Three monsters (all hp 30) with `id` as the sole hand card (pool 0).
CombatState Make3(CardId id, uint8_t cost) {
    CombatState s{};
    s.card_pool[0].card_id = static_cast<uint16_t>(id);
    s.card_pool[0].cost_now = cost;
    s.hand[0] = 0;
    s.hand_count = 1;
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = 3;
    s.monster_count = 3;
    for (int i = 0; i < 3; ++i) {
        s.monsters[i].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
        s.monsters[i].hp = 30;
        s.monsters[i].max_hp = 30;
    }
    s.monster_attacks_queued = 1;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

void AddPower(CombatState& s, uint8_t actor, PowerId id, int16_t amount) {
    PowerSlot* slots;
    uint8_t* count;
    if (actor == kActorPlayer) {
        slots = s.player_powers;
        count = &s.player_power_count;
    } else {
        slots = s.monsters[actor].powers;
        count = &s.monsters[actor].power_count;
    }
    slots[*count].power_id = static_cast<uint16_t>(id);
    slots[*count].amount = amount;
    ++*count;
}

StepResult Step(CombatState& s, Action a) {
    StepResult r{};
    advance(std::span<CombatState>(&s, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&r, 1));
    return r;
}

bool DiscardHas(const CombatState& s, CardId id) {
    for (uint8_t i = 0; i < s.discard_count; ++i) {
        if (s.card_pool[s.discard[i]].card_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

// --- Anger (id 11): 6 dmg + a copy of Anger to discard; upgrade 8, upgraded copy.
TEST(CardAttacksAnger, DamageAndBaseCopyToDiscard) {
    CombatState s = MakeState(CardId::ANGER, 0);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 6);
    ASSERT_EQ(s.discard_count, 2);  // the played source + the created copy
    EXPECT_EQ(s.card_pool[s.discard[0]].card_id, static_cast<uint16_t>(CardId::ANGER));
    EXPECT_EQ(s.card_pool[s.discard[1]].card_id, static_cast<uint16_t>(CardId::ANGER));
    EXPECT_EQ(s.card_pool[s.discard[1]].upgrade, 0) << "base Anger clones a base copy";
}

TEST(CardAttacksAnger, UpgradedDamageAndUpgradedCopy) {
    CombatState s = MakeState(CardId::ANGER, 0);
    s.card_pool[0].upgrade = 1;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 8);
    ASSERT_EQ(s.discard_count, 2);
    // makeStatEquivalentCopy preserves timesUpgraded: the created copy is upgraded.
    EXPECT_EQ(s.card_pool[s.discard[1]].card_id, static_cast<uint16_t>(CardId::ANGER));
    EXPECT_EQ(s.card_pool[s.discard[1]].upgrade, 1)
        << "upgraded Anger clones an upgraded copy";
}

// --- Body Slam (id 12): damage == current block; upgrade cost 1 -> 0.
TEST(CardAttacksBodySlam, DealsCurrentBlock) {
    CombatState s = MakeState(CardId::BODY_SLAM, 1);
    s.player_block = 13;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 13);
    EXPECT_EQ(s.player_block, 13) << "block itself is not spent";
}

TEST(CardAttacksBodySlam, BlockBaseScalesWithStrength) {
    CombatState s = MakeState(CardId::BODY_SLAM, 1);
    s.player_block = 10;
    AddPower(s, kActorPlayer, PowerId::STRENGTH, 3);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 13) << "(10 block + 3 Strength) via the pipeline";
}

TEST(CardAttacksBodySlam, UpgradeIsCostZeroSameProgram) {
    const CardDef* d = card_def(CardId::BODY_SLAM);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(card_cost(*d, 0), 1);
    EXPECT_EQ(card_cost(*d, 1), 0);
    // Program unchanged: an upgraded Body Slam still deals block-derived damage.
    CombatState s = MakeState(CardId::BODY_SLAM, 0);
    s.card_pool[0].upgrade = 1;
    s.player_block = 9;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 9);
}

// --- Clash (id 13): playable only if all hand cards are Attacks; 14 dmg / 18.
TEST(CardAttacksClash, PlayableOnlyWhenEveryHandCardIsAttack) {
    CombatState s = MakeState(CardId::CLASH, 0);
    s.card_pool[1].card_id = static_cast<uint16_t>(CardId::STRIKE);  // Attack
    s.card_pool[1].cost_now = 1;
    s.hand[1] = 1;
    s.hand_count = 2;

    ActionMask m{};
    legal_actions(s, m);
    EXPECT_TRUE(m.can_play[0]) << "Clash + Strike are both Attacks";

    s.card_pool[2].card_id = static_cast<uint16_t>(CardId::DEFEND);  // Skill
    s.card_pool[2].cost_now = 1;
    s.hand[2] = 2;
    s.hand_count = 3;
    legal_actions(s, m);
    EXPECT_FALSE(m.can_play[0]) << "a Skill (Defend) in hand bars Clash";
    EXPECT_TRUE(m.can_play[1]) << "Strike is unaffected by Clash's predicate";
}

TEST(CardAttacksClash, DealsFourteenAndUpgradedEighteen) {
    CombatState s = MakeState(CardId::CLASH, 0);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 14);

    CombatState u = MakeState(CardId::CLASH, 0);
    u.card_pool[0].upgrade = 1;
    ASSERT_TRUE(queue_card_play(u, 0, 0));
    pump(u);
    EXPECT_EQ(u.monsters[0].hp, 50 - 18);
}

// --- Cleave (id 14): 8 AoE over live monsters; upgrade 11.
TEST(CardAttacksCleave, HitsOnlyLiveMonsters) {
    CombatState s = Make3(CardId::CLEAVE, 1);
    s.monsters[1].hp = 0;  // dead -> skipped
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 30 - 8);
    EXPECT_EQ(s.monsters[1].hp, 0);
    EXPECT_EQ(s.monsters[2].hp, 30 - 8);
}

TEST(CardAttacksCleave, UpgradedHitsForEleven) {
    CombatState s = Make3(CardId::CLEAVE, 1);
    s.card_pool[0].upgrade = 1;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 30 - 11);
    EXPECT_EQ(s.monsters[1].hp, 30 - 11);
    EXPECT_EQ(s.monsters[2].hp, 30 - 11);
}

// --- Clothesline (id 15): 12 dmg + Weak 2; upgrade 14 dmg + Weak 3.
TEST(CardAttacksClothesline, DamageThenWeak) {
    CombatState s = MakeState(CardId::CLOTHESLINE, 2);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 12);  // Weak does not reduce this hit
    ASSERT_EQ(s.monsters[0].power_count, 1);
    EXPECT_EQ(s.monsters[0].powers[0].power_id, static_cast<uint16_t>(PowerId::WEAK));
    EXPECT_EQ(s.monsters[0].powers[0].amount, 2);
}

TEST(CardAttacksClothesline, UpgradedDamageAndWeak) {
    CombatState s = MakeState(CardId::CLOTHESLINE, 2);
    s.card_pool[0].upgrade = 1;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 14);
    ASSERT_EQ(s.monsters[0].power_count, 1);
    EXPECT_EQ(s.monsters[0].powers[0].amount, 3);
}

// --- Headbutt (id 16): 9 dmg + a discard card to top of draw; upgrade 12.
TEST(CardAttacksHeadbutt, EmptyDiscardOnlyDamages) {
    CombatState s = MakeState(CardId::HEADBUTT, 1);
    s.card_pool[5].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.draw[0] = 5;
    s.draw_count = 1;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 9);
    EXPECT_EQ(s.draw_count, 1) << "the just-played Headbutt is excluded, nothing retrieved";
    ASSERT_EQ(s.discard_count, 1) << "only the source Headbutt";
    EXPECT_EQ(s.card_pool[s.discard[0]].card_id, static_cast<uint16_t>(CardId::HEADBUTT));
}

TEST(CardAttacksHeadbutt, SingleOtherDiscardCardAutoMovesToDrawTop) {
    CombatState s = MakeState(CardId::HEADBUTT, 1);
    s.card_pool[5].card_id = static_cast<uint16_t>(CardId::DEFEND);
    s.discard[0] = 5;
    s.discard_count = 1;  // one other card -> forced auto-move (source excluded)
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 9);
    ASSERT_EQ(s.draw_count, 1);
    EXPECT_EQ(s.card_pool[s.draw[s.draw_count - 1]].card_id,
              static_cast<uint16_t>(CardId::DEFEND));
    ASSERT_EQ(s.discard_count, 1);  // the moved card left; source Headbutt remains
    EXPECT_EQ(s.card_pool[s.discard[0]].card_id, static_cast<uint16_t>(CardId::HEADBUTT));
}

TEST(CardAttacksHeadbutt, TwoDiscardCardsPromptAChoice) {
    CombatState s = MakeState(CardId::HEADBUTT, 1);
    s.card_pool[5].card_id = static_cast<uint16_t>(CardId::DEFEND);
    s.card_pool[6].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.discard[0] = 5;
    s.discard[1] = 6;
    s.discard_count = 2;  // >= 2 others -> gridSelect prompt

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));
    EXPECT_EQ(s.monsters[0].hp, 50 - 9);
    // discard now = [5, 6, 0(source Headbutt)].
    ActionMask m{};
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_pending);
    EXPECT_TRUE(m.choice_from_discard);
    EXPECT_TRUE(m.can_choose[0]);   // slot 0 == pool 5
    EXPECT_TRUE(m.can_choose[1]);   // slot 1 == pool 6
    EXPECT_FALSE(m.can_choose[2]);  // slot 2 == the source Headbutt (excluded)

    Step(s, make_action(ActionVerb::CHOOSE, 1));  // retrieve pool 6 (Strike)
    ActionMask m2{};
    legal_actions(s, m2);
    EXPECT_FALSE(m2.choice_pending);
    ASSERT_EQ(s.draw_count, 1);
    EXPECT_EQ(s.card_pool[s.draw[s.draw_count - 1]].card_id,
              static_cast<uint16_t>(CardId::STRIKE));
}

TEST(CardAttacksHeadbutt, UpgradedDealsTwelve) {
    CombatState s = MakeState(CardId::HEADBUTT, 1);
    s.card_pool[0].upgrade = 1;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 12);
}

// --- Heavy Blade (id 17): 14 base, Strength counted x3; upgrade x5.
TEST(CardAttacksHeavyBlade, NoStrengthIsBase) {
    CombatState s = MakeState(CardId::HEAVY_BLADE, 2);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 14);
}

TEST(CardAttacksHeavyBlade, StrengthCountsTriple) {
    CombatState s = MakeState(CardId::HEAVY_BLADE, 2);
    AddPower(s, kActorPlayer, PowerId::STRENGTH, 3);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - (14 + 3 * 3));  // 23
}

TEST(CardAttacksHeavyBlade, UpgradedStrengthCountsQuintuple) {
    CombatState s = MakeState(CardId::HEAVY_BLADE, 2);
    s.card_pool[0].upgrade = 1;
    AddPower(s, kActorPlayer, PowerId::STRENGTH, 3);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - (14 + 3 * 5));  // 29
}

TEST(CardAttacksHeavyBlade, StrengthTripledThenVulnerable) {
    CombatState s = MakeState(CardId::HEAVY_BLADE, 2);
    AddPower(s, kActorPlayer, PowerId::STRENGTH, 2);
    AddPower(s, 0, PowerId::VULNERABLE, 2);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    // (14 + 2*3) * 1.5 == 30: Strength scales inside atDamageGive, Vulnerable after.
    EXPECT_EQ(s.monsters[0].hp, 50 - 30);
}

// --- Iron Wave (id 18): 5 block + 5 dmg; upgrade 7/7.
TEST(CardAttacksIronWave, BlockThenDamage) {
    CombatState s = MakeState(CardId::IRON_WAVE, 1);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.player_block, 5);
    EXPECT_EQ(s.monsters[0].hp, 50 - 5);
}

TEST(CardAttacksIronWave, UpgradedSevenSeven) {
    CombatState s = MakeState(CardId::IRON_WAVE, 1);
    s.card_pool[0].upgrade = 1;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.player_block, 7);
    EXPECT_EQ(s.monsters[0].hp, 50 - 7);
}

// --- Perfected Strike (id 19): 6 + 2 per "Strike" card (self excluded); upgrade +3.
TEST(CardAttacksPerfectedStrike, NoOtherStrikesIsBase) {
    CombatState s = MakeState(CardId::PERFECTED_STRIKE, 2);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 6) << "6 + 2*0 (the source Strike is excluded)";
}

TEST(CardAttacksPerfectedStrike, CountsStrikesInHandDrawDiscardExcludingSelf) {
    CombatState s = MakeState(CardId::PERFECTED_STRIKE, 2);
    // hand: +Strike (pool 1). draw: Twin Strike (2) + Defend non-strike (4).
    // discard: Pommel Strike (3). Strikes (excl the source PS): 3.
    s.card_pool[1].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.hand[1] = 1;
    s.hand_count = 2;
    s.card_pool[2].card_id = static_cast<uint16_t>(CardId::TWIN_STRIKE);
    s.card_pool[4].card_id = static_cast<uint16_t>(CardId::DEFEND);
    s.draw[0] = 2;
    s.draw[1] = 4;
    s.draw_count = 2;
    s.card_pool[3].card_id = static_cast<uint16_t>(CardId::POMMEL_STRIKE);
    s.discard[0] = 3;
    s.discard_count = 1;

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - (6 + 2 * 3));  // 12
}

TEST(CardAttacksPerfectedStrike, UpgradedPerStrikeIsThree) {
    CombatState s = MakeState(CardId::PERFECTED_STRIKE, 2);
    s.card_pool[0].upgrade = 1;
    s.card_pool[1].card_id = static_cast<uint16_t>(CardId::WILD_STRIKE);
    s.card_pool[2].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.hand[1] = 1;
    s.hand[2] = 2;
    s.hand_count = 3;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - (6 + 3 * 2));  // 12: two other Strikes, +3 each
}

// --- Sword Boomerang (id 20): 3 dmg x3 at random live enemies; upgrade x4 hits.
TEST(CardAttacksSwordBoomerang, ThreeHitsOneDrawEachTotalNine) {
    CombatState s = Make3(CardId::SWORD_BOOMERANG, 1);
    s.card_random_rng = from_seed(555);
    const int32_t before = s.card_random_rng.counter;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.card_random_rng.counter, before + 3) << "one card_random_rng draw per hit";
    const int total = (30 - s.monsters[0].hp) + (30 - s.monsters[1].hp) +
                      (30 - s.monsters[2].hp);
    EXPECT_EQ(total, 9);  // 3 dmg x 3 hits
}

TEST(CardAttacksSwordBoomerang, NeverHitsDeadMonsters) {
    CombatState s = Make3(CardId::SWORD_BOOMERANG, 1);
    s.monsters[1].hp = 0;
    s.card_random_rng = from_seed(999);
    const int32_t before = s.card_random_rng.counter;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[1].hp, 0);
    EXPECT_EQ(s.card_random_rng.counter, before + 3);
    const int total = (30 - s.monsters[0].hp) + (30 - s.monsters[2].hp);
    EXPECT_EQ(total, 9);
}

TEST(CardAttacksSwordBoomerang, UpgradedFourHits) {
    CombatState s = Make3(CardId::SWORD_BOOMERANG, 1);
    s.card_pool[0].upgrade = 1;
    s.card_random_rng = from_seed(555);
    const int32_t before = s.card_random_rng.counter;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.card_random_rng.counter, before + 4);
    const int total = (30 - s.monsters[0].hp) + (30 - s.monsters[1].hp) +
                      (30 - s.monsters[2].hp);
    EXPECT_EQ(total, 12);  // 3 dmg x 4 hits
}

// --- Thunderclap (id 21): 4 AoE + Vulnerable 1 to all; upgrade 7 dmg.
TEST(CardAttacksThunderclap, AoeDamageThenVulnerableToAll) {
    CombatState s = Make3(CardId::THUNDERCLAP, 1);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(s.monsters[i].hp, 30 - 4) << "monster " << i;
        ASSERT_EQ(s.monsters[i].power_count, 1) << "monster " << i;
        EXPECT_EQ(s.monsters[i].powers[0].power_id,
                  static_cast<uint16_t>(PowerId::VULNERABLE));
        EXPECT_EQ(s.monsters[i].powers[0].amount, 1);
    }
}

TEST(CardAttacksThunderclap, UpgradedSevenDamageStillVulnerable) {
    CombatState s = Make3(CardId::THUNDERCLAP, 1);
    s.card_pool[0].upgrade = 1;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(s.monsters[i].hp, 30 - 7) << "monster " << i;
        ASSERT_EQ(s.monsters[i].power_count, 1);
        EXPECT_EQ(s.monsters[i].powers[0].amount, 1);
    }
}

// --- Twin Strike (id 22): 5 dmg x2; upgrade 7 x2.
TEST(CardAttacksTwinStrike, HitsTwice) {
    CombatState s = MakeState(CardId::TWIN_STRIKE, 1);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 10);
}

TEST(CardAttacksTwinStrike, UpgradedHitsTwiceForSeven) {
    CombatState s = MakeState(CardId::TWIN_STRIKE, 1);
    s.card_pool[0].upgrade = 1;
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 14);
}

// --- Wild Strike (id 23): 12 dmg + a Wound at a random draw spot; upgrade 17.
TEST(CardAttacksWildStrike, DamageAndShufflesAWound) {
    CombatState s = MakeState(CardId::WILD_STRIKE, 1);
    s.card_pool[5].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.card_pool[6].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.draw[0] = 5;
    s.draw[1] = 6;
    s.draw_count = 2;
    s.card_random_rng = from_seed(42);
    const int32_t before = s.card_random_rng.counter;

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 12);
    EXPECT_EQ(s.card_random_rng.counter, before + 1)
        << "addToRandomSpot consumes one card_random_rng draw";
    ASSERT_EQ(s.draw_count, 3);
    bool found_wound = false;
    for (uint8_t i = 0; i < s.draw_count; ++i) {
        const CardInstance& c = s.card_pool[s.draw[i]];
        if (c.card_id == static_cast<uint16_t>(CardId::WOUND)) {
            found_wound = true;
            EXPECT_EQ(c.upgrade, 0);
            EXPECT_TRUE(has_card_flag(c.flags, CardFlag::UNPLAYABLE));
        }
    }
    EXPECT_TRUE(found_wound) << "a Wound was shuffled into the draw pile";
}

TEST(CardAttacksWildStrike, UpgradedDealsSeventeen) {
    CombatState s = MakeState(CardId::WILD_STRIKE, 1);
    s.card_pool[0].upgrade = 1;
    s.card_pool[5].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.draw[0] = 5;
    s.draw_count = 1;
    s.card_random_rng = from_seed(7);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 17);
    EXPECT_TRUE(DiscardHas(s, CardId::WILD_STRIKE));  // the played source goes to discard
    bool found_wound = false;
    for (uint8_t i = 0; i < s.draw_count; ++i) {
        if (s.card_pool[s.draw[i]].card_id == static_cast<uint16_t>(CardId::WOUND)) {
            found_wound = true;
        }
    }
    EXPECT_TRUE(found_wound);
}

// --- Wound (id 24): unplayable STATUS clutter.
TEST(CardAttacksWound, IsUnplayable) {
    CombatState s = MakeState(CardId::WOUND, 0);
    const CardDef* d = card_def(CardId::WOUND);
    ASSERT_NE(d, nullptr);
    s.card_pool[0].flags = card_flags(*d, 0);  // seed the registry flags (UNPLAYABLE)
    ASSERT_TRUE(has_card_flag(s.card_pool[0].flags, CardFlag::UNPLAYABLE));
    EXPECT_EQ(static_cast<int>(d->type), static_cast<int>(CardType::STATUS));

    ActionMask m{};
    legal_actions(s, m);
    EXPECT_FALSE(m.can_play[0]) << "a status card is never a legal play";
}

// --- Directed script through the public advance() API -----------------------
// Two monsters; play Cleave (AoE) then Thunderclap (AoE + Vulnerable), checking
// the cumulative state and that the hand shift + energy spend are correct.
TEST(CardAttacksDirectedScript, CleaveThenThunderclapOverTwoMonsters) {
    CombatState s{};
    s.card_pool[0].card_id = static_cast<uint16_t>(CardId::CLEAVE);
    s.card_pool[0].cost_now = 1;
    s.card_pool[1].card_id = static_cast<uint16_t>(CardId::THUNDERCLAP);
    s.card_pool[1].cost_now = 1;
    s.hand[0] = 0;
    s.hand[1] = 1;
    s.hand_count = 2;
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = 3;
    s.monster_count = 2;
    for (int i = 0; i < 2; ++i) {
        s.monsters[i].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
        s.monsters[i].hp = 30;
        s.monsters[i].max_hp = 30;
    }
    s.monster_attacks_queued = 1;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));  // Cleave: both -8
    EXPECT_EQ(s.monsters[0].hp, 30 - 8);
    EXPECT_EQ(s.monsters[1].hp, 30 - 8);
    EXPECT_EQ(s.player_energy, 2);
    ASSERT_EQ(s.hand_count, 1);  // Thunderclap now at slot 0

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));  // Thunderclap: both -4 + Vuln 1
    EXPECT_EQ(s.monsters[0].hp, 30 - 8 - 4);
    EXPECT_EQ(s.monsters[1].hp, 30 - 8 - 4);
    EXPECT_EQ(s.player_energy, 1);
    for (int i = 0; i < 2; ++i) {
        ASSERT_EQ(s.monsters[i].power_count, 1) << "monster " << i;
        EXPECT_EQ(s.monsters[i].powers[0].power_id,
                  static_cast<uint16_t>(PowerId::VULNERABLE));
    }
}

}  // namespace
}  // namespace sts::engine
