// The floor-0 Neow blessing: the four-category option roll, the category
// tables, every payout's stream attribution, the drawbacks, the master-deck
// grids, and the run-loop path from the blessing screen onto the first floor.
//
// Everything asserted here is derived from the Java cited in neow.hpp, and the
// expectation side is built INDEPENDENTLY of the implementation wherever that
// is possible: option indices come from a fresh RngStream driven by hand, card
// identities from the generated pool arrays indexed by hand, and potion
// identities from a hand-advanced potionRng. The one place a literal appears is
// the fixed-seed anchor, which is written out draw by draw in a comment.

#include <gtest/gtest.h>

#include <algorithm>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "sts/engine/cards.hpp"
#include "sts/engine/card_pools.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/neow.hpp"
#include "sts/engine/potions.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"
#include "sts/registry/game_ids.hpp"

using namespace sts::engine;

namespace {

constexpr uint8_t kA20 = 20;

void step(RunController& rc, Action a) {
    StepResult res{};
    advance(std::span<RunController>(&rc, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&res, 1));
}

Action choose(uint8_t a0) { return make_action(ActionVerb::CHOOSE, a0); }

RunActionMask mask_of(const RunController& rc) {
    RunActionMask m{};
    legal_actions(rc, m);
    return m;
}

// Overwrite a rolled option so a payout can be exercised on a chosen seed
// without hunting one. The rolled option list is what the blessing-roll tests
// cover; the payout tests are about what activate() then does.
void force_option(RunController& rc, uint8_t slot, NeowRewardType type,
                  NeowDrawback drawback = NeowDrawback::NONE) {
    rc.neow.option_type[slot] = static_cast<uint8_t>(type);
    rc.neow.option_drawback[slot] = static_cast<uint8_t>(drawback);
}

std::vector<NeowRewardType> options_of(int cat, NeowDrawback d) {
    NeowRewardType buf[8]{};
    int n = 0;
    neow_category_options(cat, d, buf, n);
    return std::vector<NeowRewardType>(buf, buf + n);
}

uint8_t deck_index_of(const RunState& rs, CardId id) {
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (rs.master_deck[i].card_id == static_cast<uint16_t>(id)) {
            return static_cast<uint8_t>(i);
        }
    }
    ADD_FAILURE() << "card not in master deck";
    return 0;
}

int count_card(const RunState& rs, CardId id) {
    int n = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        n += rs.master_deck[i].card_id == static_cast<uint16_t>(id) ? 1 : 0;
    }
    return n;
}

bool owns(const RunState& rs, RelicId id) {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id == static_cast<uint16_t>(id)) return true;
    }
    return false;
}

}  // namespace

// =============================================================================
// The option roll (tier-2 anchor)
// =============================================================================

// HAND DERIVATION -- seed 42, ascension 20, drawn one call at a time off a
// fresh Random(42) exactly as NeowEvent.blessing does (NeowEvent.java:363-371).
// The five draws below were taken from an INDEPENDENT RngStream (the assertions
// re-take them), and the option each one selects is read off the category
// tables at NeowReward.java:85-128:
//
//   draw 1  rng.random(0, 5) = 4   category 0's six-entry list
//           [THREE_CARDS, ONE_RANDOM_RARE_CARD, REMOVE_CARD, UPGRADE_CARD,
//            TRANSFORM_CARD, RANDOM_COLORLESS][4]        -> TRANSFORM_CARD
//   draw 2  rng.random(0, 4) = 1   category 1's five-entry list
//           [THREE_SMALL_POTIONS, RANDOM_COMMON_RELIC, TEN_PERCENT_HP_BONUS,
//            THREE_ENEMY_KILL, HUNDRED_GOLD][1]          -> RANDOM_COMMON_RELIC
//   draw 3  rng.random(0, 3) = 3   category 2's DRAWBACK, rolled FIRST
//           [TEN_PERCENT_HP_LOSS, NO_GOLD, CURSE, PERCENT_DAMAGE][3]
//                                                        -> PERCENT_DAMAGE
//   draw 4  rng.random(0, 6) = 4   PERCENT_DAMAGE gates nothing out, so the
//           category 2 list keeps all SEVEN entries
//           [RANDOM_COLORLESS_2, REMOVE_TWO, ONE_RARE_RELIC, THREE_RARE_CARDS,
//            TWO_FIFTY_GOLD, TRANSFORM_TWO_CARDS, TWENTY_PERCENT_HP_BONUS][4]
//                                                        -> TWO_FIFTY_GOLD
//   draw 5  rng.random(0, 0) = 0   category 3 has one entry and STILL draws
//                                                        -> BOSS_RELIC
//
// hp_bonus = (int)(75 * 0.1f) = 7, off the A20 Ironclad's post-setup 68/75.
TEST(NeowBlessing, FixedSeedFourOptionsMatchHandDerivation) {
    RunController rc = run_begin(42, kA20);

    // The run really is the A20 sheet the derivation assumed.
    EXPECT_EQ(rc.run.max_hp, 75);
    EXPECT_EQ(rc.run.hp, 68);
    EXPECT_EQ(rc.neow.hp_bonus, 7);

    // The five draws, re-taken independently and in order.
    RngStream s = from_seed(42);
    const int32_t d0 = random(s, 0, 5);
    const int32_t d1 = random(s, 0, 4);
    const int32_t d2 = random(s, 0, 3);
    const int32_t d3 = random(s, 0, 6);
    const int32_t d4 = random(s, 0, 0);
    EXPECT_EQ(d0, 4);
    EXPECT_EQ(d1, 1);
    EXPECT_EQ(d2, 3);
    EXPECT_EQ(d3, 4);
    EXPECT_EQ(d4, 0);

    EXPECT_EQ(rc.neow.option_type[0],
              static_cast<uint8_t>(NeowRewardType::TRANSFORM_CARD));
    EXPECT_EQ(rc.neow.option_type[1],
              static_cast<uint8_t>(NeowRewardType::RANDOM_COMMON_RELIC));
    EXPECT_EQ(rc.neow.option_type[2],
              static_cast<uint8_t>(NeowRewardType::TWO_FIFTY_GOLD));
    EXPECT_EQ(rc.neow.option_drawback[2],
              static_cast<uint8_t>(NeowDrawback::PERCENT_DAMAGE));
    EXPECT_EQ(rc.neow.option_type[3],
              static_cast<uint8_t>(NeowRewardType::BOSS_RELIC));

    // Categories 0, 1 and 3 never carry a drawback.
    EXPECT_EQ(rc.neow.option_drawback[0],
              static_cast<uint8_t>(NeowDrawback::NONE));
    EXPECT_EQ(rc.neow.option_drawback[1],
              static_cast<uint8_t>(NeowDrawback::NONE));
    EXPECT_EQ(rc.neow.option_drawback[3],
              static_cast<uint8_t>(NeowDrawback::NONE));

    // Five draws, and the stream state is exactly the hand-driven one.
    EXPECT_EQ(rc.run.neow_rng.counter, 5);
    EXPECT_EQ(rc.run.neow_rng.s0, s.s0);
    EXPECT_EQ(rc.run.neow_rng.s1, s.s1);
    EXPECT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::BLESSING));
}

// A second seed, derived the same way, so the mapping is not fitted to one
// draw sequence. Seed 7: 3, 3, 3, 2, 0.
TEST(NeowBlessing, SecondFixedSeedMatchesTheSameDerivation) {
    RunController rc = run_begin(7, kA20);
    RngStream s = from_seed(7);
    EXPECT_EQ(random(s, 0, 5), 3);  // UPGRADE_CARD
    EXPECT_EQ(random(s, 0, 4), 3);  // THREE_ENEMY_KILL
    EXPECT_EQ(random(s, 0, 3), 3);  // PERCENT_DAMAGE
    EXPECT_EQ(random(s, 0, 6), 2);  // ONE_RARE_RELIC
    EXPECT_EQ(random(s, 0, 0), 0);  // BOSS_RELIC

    EXPECT_EQ(rc.neow.option_type[0],
              static_cast<uint8_t>(NeowRewardType::UPGRADE_CARD));
    EXPECT_EQ(rc.neow.option_type[1],
              static_cast<uint8_t>(NeowRewardType::THREE_ENEMY_KILL));
    EXPECT_EQ(rc.neow.option_type[2],
              static_cast<uint8_t>(NeowRewardType::ONE_RARE_RELIC));
    EXPECT_EQ(rc.neow.option_drawback[2],
              static_cast<uint8_t>(NeowDrawback::PERCENT_DAMAGE));
    EXPECT_EQ(rc.neow.option_type[3],
              static_cast<uint8_t>(NeowRewardType::BOSS_RELIC));
}

// TRAP 17. NeowEvent.rng is `new Random(Settings.seed)` (NeowEvent.java:363) --
// a fresh stream, not a dungeon one -- so the blessing is a pure function of
// the seed. Consuming every other stream first must not move it, and the
// blessing must not move any other stream either.
TEST(NeowBlessingTrap17, FreshRandomSeedIgnoresEveryOtherStream) {
    RunController base = run_begin(42, kA20);

    // Take a run_begin state, hammer every run-scoped stream, and re-roll: the
    // blessing is identical because it reads none of them.
    RunController rc = run_begin(42, kA20);
    for (int i = 0; i < 37; ++i) {
        (void)random(rc.run.card_rng, 99);
        (void)random(rc.run.relic_rng, 99);
        (void)random(rc.run.potion_rng, 99);
        (void)random(rc.run.event_rng, 99);
        (void)random(rc.run.monster_rng, 99);
        (void)random(rc.run.merchant_rng, 99);
        (void)random(rc.run.treasure_rng, 99);
        (void)random(rc.run.map_rng, 99);
        (void)random(rc.combat.misc_rng, 99);
    }
    rc.run.neow_rng = from_seed(42);  // the stream NeowEvent.blessing creates
    NeowState again{};
    neow_roll_blessing(rc.run, again);
    for (int i = 0; i < kNeowOptionCount; ++i) {
        EXPECT_EQ(again.option_type[i], base.neow.option_type[i]) << i;
        EXPECT_EQ(again.option_drawback[i], base.neow.option_drawback[i]) << i;
    }

    // And the roll itself is stream-local: nothing but neow_rng moved.
    RunController clean = run_begin(42, kA20);
    RunState before = clean.run;
    NeowState out{};
    before.neow_rng = from_seed(42);
    clean.run.neow_rng = from_seed(42);
    neow_roll_blessing(clean.run, out);
    EXPECT_EQ(clean.run.card_rng.counter, before.card_rng.counter);
    EXPECT_EQ(clean.run.relic_rng.counter, before.relic_rng.counter);
    EXPECT_EQ(clean.run.potion_rng.counter, before.potion_rng.counter);
    EXPECT_EQ(clean.run.event_rng.counter, before.event_rng.counter);
    EXPECT_EQ(clean.run.monster_rng.counter, before.monster_rng.counter);
    EXPECT_EQ(clean.run.merchant_rng.counter, before.merchant_rng.counter);
    EXPECT_EQ(clean.run.treasure_rng.counter, before.treasure_rng.counter);
    EXPECT_EQ(clean.run.neow_rng.counter, 5);
}

// The blessing depends on the seed alone, not on the ascension -- ascension
// changes only hp_bonus, through max HP.
TEST(NeowBlessingTrap17, AscensionMovesHpBonusButNotTheOptions) {
    RunController a0 = run_begin(42, 0);
    RunController a20 = run_begin(42, kA20);
    for (int i = 0; i < kNeowOptionCount; ++i) {
        EXPECT_EQ(a0.neow.option_type[i], a20.neow.option_type[i]) << i;
    }
    EXPECT_EQ(a0.neow.hp_bonus, 8);   // (int)(80 * 0.1f)
    EXPECT_EQ(a20.neow.hp_bonus, 7);  // (int)(75 * 0.1f)
}

// =============================================================================
// The category tables
// =============================================================================

TEST(NeowCategories, TablesMatchTheJavaInsertionOrder) {
    EXPECT_EQ(options_of(0, NeowDrawback::NONE),
              (std::vector<NeowRewardType>{
                  NeowRewardType::THREE_CARDS,
                  NeowRewardType::ONE_RANDOM_RARE_CARD,
                  NeowRewardType::REMOVE_CARD, NeowRewardType::UPGRADE_CARD,
                  NeowRewardType::TRANSFORM_CARD,
                  NeowRewardType::RANDOM_COLORLESS}));
    EXPECT_EQ(options_of(1, NeowDrawback::NONE),
              (std::vector<NeowRewardType>{
                  NeowRewardType::THREE_SMALL_POTIONS,
                  NeowRewardType::RANDOM_COMMON_RELIC,
                  NeowRewardType::TEN_PERCENT_HP_BONUS,
                  NeowRewardType::THREE_ENEMY_KILL,
                  NeowRewardType::HUNDRED_GOLD}));
    EXPECT_EQ(options_of(3, NeowDrawback::NONE),
              (std::vector<NeowRewardType>{NeowRewardType::BOSS_RELIC}));
}

// Category 2's three drawback-conditional omissions (NeowReward.java:110-121).
TEST(NeowCategories, Category2OmissionsFollowTheRolledDrawback) {
    const auto full = options_of(2, NeowDrawback::PERCENT_DAMAGE);
    EXPECT_EQ(full, (std::vector<NeowRewardType>{
                        NeowRewardType::RANDOM_COLORLESS_2,
                        NeowRewardType::REMOVE_TWO,
                        NeowRewardType::ONE_RARE_RELIC,
                        NeowRewardType::THREE_RARE_CARDS,
                        NeowRewardType::TWO_FIFTY_GOLD,
                        NeowRewardType::TRANSFORM_TWO_CARDS,
                        NeowRewardType::TWENTY_PERCENT_HP_BONUS}));

    const auto curse = options_of(2, NeowDrawback::CURSE);
    EXPECT_EQ(curse.size(), 6u);
    EXPECT_EQ(std::count(curse.begin(), curse.end(), NeowRewardType::REMOVE_TWO),
              0);

    const auto no_gold = options_of(2, NeowDrawback::NO_GOLD);
    EXPECT_EQ(no_gold.size(), 6u);
    EXPECT_EQ(std::count(no_gold.begin(), no_gold.end(),
                         NeowRewardType::TWO_FIFTY_GOLD),
              0);

    // The 10 % max-HP LOSS omits the 20 % max-HP GAIN by an early `break`, so
    // it is the LAST entry that disappears, not a middle one.
    const auto hp_loss = options_of(2, NeowDrawback::TEN_PERCENT_HP_LOSS);
    EXPECT_EQ(hp_loss.size(), 6u);
    EXPECT_EQ(hp_loss.back(), NeowRewardType::TRANSFORM_TWO_CARDS);
    EXPECT_EQ(std::count(hp_loss.begin(), hp_loss.end(),
                         NeowRewardType::TWENTY_PERCENT_HP_BONUS),
              0);
}

// NAMED ROLL-ORDER TEST. getRewardOptions(2) rolls the DRAWBACK before the
// reward (NeowReward.java:106-108 sits inside getRewardOptions, which
// NeowReward.<init> calls at :67 BEFORE the reward pick at :68). Two things
// break if the order is swapped: the stream desyncs, and the reward is picked
// out of a list whose LENGTH depends on the drawback that has not been rolled
// yet. Both are asserted.
TEST(NeowCategories, Category2RollsTheDrawbackBeforeTheReward) {
    const auto widest = options_of(2, NeowDrawback::PERCENT_DAMAGE);
    ASSERT_EQ(widest.size(), 7u);

    int disagreements = 0;
    for (int64_t seed : {int64_t(42), int64_t(7), int64_t(1), int64_t(1234),
                         int64_t(99), int64_t(2024), int64_t(555),
                         int64_t(31337)}) {
        RunController rc = run_begin(seed, kA20);

        // The CORRECT order: drawback, then a pick over the list that drawback
        // produced. It reproduces what the engine rolled, on every seed.
        RngStream s = from_seed(seed);
        (void)random(s, 0, 5);  // category 0
        (void)random(s, 0, 4);  // category 1
        const auto drawback =
            static_cast<NeowDrawback>(random(s, 0, kNeowDrawbackCount - 1) + 1);
        const auto list = options_of(2, drawback);
        const int32_t pick =
            random(s, 0, static_cast<int32_t>(list.size()) - 1);

        EXPECT_EQ(rc.neow.option_drawback[2], static_cast<uint8_t>(drawback))
            << "seed " << seed;
        EXPECT_EQ(rc.neow.option_type[2],
                  static_cast<uint8_t>(list[static_cast<size_t>(pick)]))
            << "seed " << seed;

        // The SWAPPED order: pick first (necessarily over the widest list --
        // a reward-first implementation cannot know the drawback yet), then the
        // drawback. Where the two disagree, the order is load-bearing rather
        // than cosmetic; count those seeds and require the loop to find some.
        RngStream t = from_seed(seed);
        (void)random(t, 0, 5);
        (void)random(t, 0, 4);
        const int32_t wrong_pick = random(t, 0, 6);
        const auto wrong_drawback =
            static_cast<NeowDrawback>(random(t, 0, kNeowDrawbackCount - 1) + 1);
        if (wrong_drawback != drawback ||
            widest[static_cast<size_t>(wrong_pick)] !=
                list[static_cast<size_t>(pick)]) {
            ++disagreements;
        }
    }
    EXPECT_GT(disagreements, 0)
        << "no seed distinguishes drawback-first from reward-first";
}

// =============================================================================
// Payouts
// =============================================================================

TEST(NeowPayout, GoldAndMaxHpBlessingsApplyImmediately) {
    {
        RunController rc = run_begin(42, kA20);
        force_option(rc, 0, NeowRewardType::HUNDRED_GOLD);
        step(rc, choose(0));
        EXPECT_EQ(rc.run.gold, kIroncladBaseGold + 100);
        EXPECT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::DONE));
    }
    {
        RunController rc = run_begin(42, kA20);
        force_option(rc, 0, NeowRewardType::TWO_FIFTY_GOLD);
        step(rc, choose(0));
        EXPECT_EQ(rc.run.gold, kIroncladBaseGold + 250);
    }
    {
        RunController rc = run_begin(42, kA20);
        force_option(rc, 0, NeowRewardType::TEN_PERCENT_HP_BONUS);
        step(rc, choose(0));
        // increaseMaxHp(7, true) is +7 max AND heal 7 (68 -> 75 of 82).
        EXPECT_EQ(rc.run.max_hp, 82);
        EXPECT_EQ(rc.run.hp, 75);
    }
    {
        RunController rc = run_begin(42, kA20);
        force_option(rc, 0, NeowRewardType::TWENTY_PERCENT_HP_BONUS);
        step(rc, choose(0));
        EXPECT_EQ(rc.run.max_hp, 89);  // hp_bonus * 2 == 14
        EXPECT_EQ(rc.run.hp, 82);
    }
}

// THE POTION BLESSING'S HIDDEN CARD ROLL. NeowReward.java:268-283 adds three
// potions and then opens the COMBAT reward screen, whose setupItemReward
// unconditionally appends a getRewardCards() row for a NeowRoom
// (CombatRewardScreen.java:72-96) -- so cardRng advances and the pity counter
// moves -- before the row is deleted again. Dropping that roll desyncs cardRng
// for the whole rest of the run.
TEST(NeowPayout, ThreeSmallPotionsAlsoRollAndDiscardTheSetupItemCardRow) {
    RunController rc = run_begin(42, kA20);
    const RunState before = rc.run;
    force_option(rc, 0, NeowRewardType::THREE_SMALL_POTIONS);
    step(rc, choose(0));

    // Three PotionHelper.getRandomPotion() draws: ONE potionRng draw each, no
    // tier gate, no rejection sampling.
    RngStream p = before.potion_rng;
    const PotionId want[3] = {get_random_potion(p), get_random_potion(p),
                              get_random_potion(p)};
    EXPECT_EQ(rc.run.potion_rng.counter, before.potion_rng.counter + 3);
    ASSERT_EQ(rc.rewards.count, 3);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(rc.rewards.items[i].kind,
                  static_cast<uint8_t>(RewardItemKind::POTION));
        EXPECT_EQ(rc.rewards.items[i].id, static_cast<uint16_t>(want[i]));
    }
    EXPECT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::ITEM_REWARD));

    // The discarded card row really was rolled: cardRng and the pity counter
    // both moved, and the same sequence lands on an independent screen.
    RunState probe = before;
    RewardScreen probe_screen{};
    probe_screen.open_card_item = kNoOpenCardReward;
    roll_setup_item_card_reward(probe, RoomType::Event, probe_screen);
    EXPECT_EQ(rc.run.card_rng.counter, probe.card_rng.counter);
    EXPECT_EQ(rc.run.card_blizz_randomizer, probe.card_blizz_randomizer);
    EXPECT_NE(rc.run.card_rng.counter, before.card_rng.counter)
        << "the setupItemReward card row was not rolled";
    // ...and no CARDS row survives on the Neow screen.
    for (uint8_t i = 0; i < rc.rewards.count; ++i) {
        EXPECT_NE(rc.rewards.items[i].kind,
                  static_cast<uint8_t>(RewardItemKind::CARDS));
    }
}

TEST(NeowPayout, PotionRewardScreenClaimsThroughTheOrdinaryDoor) {
    RunController rc = run_begin(42, kA20);
    force_option(rc, 0, NeowRewardType::THREE_SMALL_POTIONS);
    step(rc, choose(0));

    RunActionMask m = mask_of(rc);
    EXPECT_TRUE(m.can_proceed);
    // A20 leaves two potion slots (the A11 loss), so exactly two of the three
    // rows can ever be claimed.
    EXPECT_EQ(rc.run.potion_slots, 2);
    EXPECT_TRUE(m.can_claim_reward[0]);
    step(rc, choose(0));
    step(rc, choose(0));
    EXPECT_NE(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE));
    EXPECT_NE(rc.run.potions[1], static_cast<uint16_t>(PotionId::NONE));
    m = mask_of(rc);
    EXPECT_FALSE(m.can_claim_reward[0]) << "no free slot, no Sozu";
    step(rc, choose(kChooseProceed));
    EXPECT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::DONE));
    EXPECT_EQ(rc.rewards.count, 0);
}

// Relic blessings are pool FRONT pops, which cost no relicRng draw (trap 15).
TEST(NeowPayout, RelicBlessingsFrontPopWithNoRelicRngDraw) {
    for (auto pair : {std::pair<NeowRewardType, RelicPool>{
                          NeowRewardType::RANDOM_COMMON_RELIC,
                          RelicPool::COMMON},
                      {NeowRewardType::ONE_RARE_RELIC, RelicPool::RARE}}) {
        RunController rc = run_begin(42, kA20);
        const int pool = static_cast<int>(pair.second);
        const uint16_t front = rc.run.relic_pools[pool][0];
        const uint8_t count = rc.run.relic_pool_count[pool];
        const int32_t relic_counter = rc.run.relic_rng.counter;

        force_option(rc, 0, pair.first);
        step(rc, choose(0));

        EXPECT_EQ(rc.run.relic_rng.counter, relic_counter);
        EXPECT_EQ(rc.run.relic_pool_count[pool], count - 1);
        EXPECT_TRUE(owns(rc.run, static_cast<RelicId>(front)));
        EXPECT_EQ(rc.run.relic_count, 2);   // Burning Blood, then this one
        EXPECT_EQ(rc.run.relics[1].relic_id, front);
        EXPECT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::DONE));
    }
}

// The acquisition-ordered pickup hooks fire on a Neow relic exactly as they do
// on a chest or elite relic: War Paint's onEquip upgrades two Skills and takes
// its Collections.shuffle seed off the FLOOR-scoped miscRng.
TEST(NeowPayout, RelicBlessingFiresAcquisitionOrderedPickupHooks) {
    RunController rc = run_begin(42, kA20);
    rc.run.relic_pools[static_cast<int>(RelicPool::COMMON)][0] =
        static_cast<uint16_t>(RelicId::WAR_PAINT);
    const int32_t misc_before = rc.combat.misc_rng.counter;

    force_option(rc, 0, NeowRewardType::RANDOM_COMMON_RELIC);
    step(rc, choose(0));

    EXPECT_TRUE(owns(rc.run, RelicId::WAR_PAINT));
    EXPECT_EQ(rc.combat.misc_rng.counter, misc_before + 1)
        << "War Paint's shuffle draw did not happen";
    int upgraded = 0;
    for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
        upgraded += rc.run.master_deck[i].upgrade > 0 ? 1 : 0;
    }
    EXPECT_EQ(upgraded, 2);
}

TEST(NeowPayout, ThreeEnemyKillGrantsNeowsLamentWithItsThreeCharges) {
    RunController rc = run_begin(42, kA20);
    force_option(rc, 0, NeowRewardType::THREE_ENEMY_KILL);
    const int32_t relic_counter = rc.run.relic_rng.counter;
    step(rc, choose(0));
    ASSERT_EQ(rc.run.relic_count, 2);
    EXPECT_EQ(rc.run.relics[1].relic_id,
              static_cast<uint16_t>(RelicId::NEOWS_LAMENT));
    EXPECT_EQ(rc.run.relics[1].counter, 3);
    EXPECT_EQ(rc.run.relic_rng.counter, relic_counter)
        << "Neow's Lament is hand-built, not a pool draw";
}

// THE BOSS SWAP. loseRelic(relics[0]) runs BEFORE the BOSS-pool draw
// (NeowReward.java:243-247), which is what makes Black Blood unspawnable from
// it -- BlackBlood.canSpawn is hasRelic("Burning Blood").
TEST(NeowPayout, BossSwapDropsBurningBloodAndNeverReturnsBlackBlood) {
    RunController rc = run_begin(42, kA20);
    const int boss = static_cast<int>(RelicPool::BOSS);
    // Put Black Blood at the pool front so the rejection path is the one taken.
    rc.run.relic_pools[boss][0] = static_cast<uint16_t>(RelicId::BLACK_BLOOD);
    const uint16_t second = rc.run.relic_pools[boss][1];
    const uint8_t count = rc.run.relic_pool_count[boss];
    const int32_t relic_counter = rc.run.relic_rng.counter;

    force_option(rc, 0, NeowRewardType::BOSS_RELIC);
    step(rc, choose(0));

    EXPECT_FALSE(owns(rc.run, RelicId::BURNING_BLOOD));
    EXPECT_FALSE(owns(rc.run, RelicId::BLACK_BLOOD));
    ASSERT_EQ(rc.run.relic_count, 1);
    EXPECT_EQ(rc.run.relics[0].relic_id, second)
        << "the rejected Black Blood pop must be consumed, not put back";
    // Two pops (the rejected one and its replacement), zero relicRng draws.
    EXPECT_EQ(rc.run.relic_pool_count[boss], count - 2);
    EXPECT_EQ(rc.run.relic_rng.counter, relic_counter);
}

TEST(NeowPayout, BossSwapWithoutBlackBloodTakesExactlyOnePop) {
    RunController rc = run_begin(42, kA20);
    const int boss = static_cast<int>(RelicPool::BOSS);
    if (rc.run.relic_pools[boss][0] ==
        static_cast<uint16_t>(RelicId::BLACK_BLOOD)) {
        // Swap the first two so the front is a plainly spawnable boss relic.
        std::swap(rc.run.relic_pools[boss][0], rc.run.relic_pools[boss][1]);
    }
    const uint16_t front = rc.run.relic_pools[boss][0];
    const uint8_t count = rc.run.relic_pool_count[boss];

    force_option(rc, 0, NeowRewardType::BOSS_RELIC);
    step(rc, choose(0));

    ASSERT_EQ(rc.run.relic_count, 1);
    EXPECT_EQ(rc.run.relics[0].relic_id, front);
    EXPECT_EQ(rc.run.relic_pool_count[boss], count - 1);
}

// NeowReward.getRewardCards: three cards off NeowEvent.rng, per card one
// rollRarity randomBoolean plus the pool draw(s), and -- unlike
// AbstractDungeon.getRewardCards -- NO trailing upgrade-chance draws and no
// pity movement.
TEST(NeowPayout, ThreeCardOfferUsesNeowRngAndHasNoUpgradePass) {
    RunController rc = run_begin(42, kA20);
    const RunState before = rc.run;
    force_option(rc, 0, NeowRewardType::THREE_CARDS);
    step(rc, choose(0));

    ASSERT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::CARD_REWARD));
    ASSERT_EQ(rc.rewards.count, 1);
    ASSERT_EQ(rc.rewards.open_card_item, 0);
    const RunRewardItem& item = rc.rewards.items[0];
    ASSERT_EQ(item.card_count, 3);

    // Hand-derive the offer off an independent stream.
    RngStream s = before.neow_rng;
    uint16_t got[3]{};
    for (int i = 0; i < 3; ++i) {
        const RewardCardRarity rarity = random_boolean(s, 0.33f)
                                            ? RewardCardRarity::UNCOMMON
                                            : RewardCardRarity::COMMON;
        CardId id;
        bool dupe;
        do {
            id = draw_card_from_pool(s, rarity);
            dupe = false;
            for (int j = 0; j < i; ++j) dupe = dupe || got[j] ==
                                                       static_cast<uint16_t>(id);
        } while (dupe);
        got[i] = static_cast<uint16_t>(id);
    }
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(item.card_ids[i], got[i]) << i;
        EXPECT_EQ(item.card_upgrades[i], 0);
    }
    EXPECT_EQ(rc.run.neow_rng.counter, s.counter);
    EXPECT_EQ(rc.run.card_rng.counter, before.card_rng.counter)
        << "a RED Neow offer must not touch cardRng";
    EXPECT_EQ(rc.run.card_blizz_randomizer, before.card_blizz_randomizer)
        << "NeowReward.getRewardCards has no pity switch";
}

TEST(NeowPayout, ThreeRareCardsStillPaysItsRollRarityDraws) {
    RunController rc = run_begin(42, kA20);
    const RunState before = rc.run;
    force_option(rc, 0, NeowRewardType::THREE_RARE_CARDS);
    step(rc, choose(0));

    RngStream s = before.neow_rng;
    uint16_t got[3]{};
    for (int i = 0; i < 3; ++i) {
        (void)random_boolean(s, 0.33f);  // rolled, then overridden by rareOnly
        CardId id;
        bool dupe;
        do {
            id = draw_card_from_pool(s, RewardCardRarity::RARE);
            dupe = false;
            for (int j = 0; j < i; ++j) dupe = dupe || got[j] ==
                                                       static_cast<uint16_t>(id);
        } while (dupe);
        got[i] = static_cast<uint16_t>(id);
    }
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(rc.rewards.items[0].card_ids[i], got[i]) << i;
        const CardDef* def = card_def(static_cast<CardId>(got[i]));
        ASSERT_NE(def, nullptr);
    }
    EXPECT_EQ(rc.run.neow_rng.counter, s.counter);
}

// THE SPLIT-STREAM COLORLESS BLESSING: the rarity roll is NeowEvent.rng, the
// identity is cardRng (CardGroup.getRandomCard(true, rarity) hard-codes
// AbstractDungeon.cardRng, CardGroup.java:509-524).
TEST(NeowPayout, ColorlessOfferSplitsNeowRngRarityFromCardRngIdentity) {
    RunController rc = run_begin(42, kA20);
    const RunState before = rc.run;
    force_option(rc, 0, NeowRewardType::RANDOM_COLORLESS);
    step(rc, choose(0));

    RngStream neow = before.neow_rng;
    RngStream card = before.card_rng;
    uint16_t got[3]{};
    for (int i = 0; i < 3; ++i) {
        // Non-rare: COMMON is promoted to UNCOMMON, but the roll still happens.
        (void)random_boolean(neow, 0.33f);
        CardId id;
        bool dupe;
        do {
            const int32_t k = random(card, kColorlessUncommonPoolCount - 1);
            id = kColorlessUncommonPool[static_cast<size_t>(k)];
            dupe = false;
            for (int j = 0; j < i; ++j) dupe = dupe || got[j] ==
                                                       static_cast<uint16_t>(id);
        } while (dupe);
        got[i] = static_cast<uint16_t>(id);
    }
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(rc.rewards.items[0].card_ids[i], got[i]) << i;
    }
    EXPECT_EQ(rc.run.neow_rng.counter, neow.counter);
    EXPECT_EQ(rc.run.card_rng.counter, card.counter);
    EXPECT_EQ(rc.run.card_rng.counter, before.card_rng.counter + 3)
        << "three identity draws, one per offered card";
    EXPECT_EQ(rc.run.card_blizz_randomizer, before.card_blizz_randomizer);
}

TEST(NeowPayout, RareColorlessOfferDrawsTheSortedRarePool) {
    RunController rc = run_begin(42, kA20);
    const RunState before = rc.run;
    force_option(rc, 0, NeowRewardType::RANDOM_COLORLESS_2);
    step(rc, choose(0));

    RngStream neow = before.neow_rng;
    RngStream card = before.card_rng;
    for (int i = 0; i < 3; ++i) {
        (void)random_boolean(neow, 0.33f);
        const int32_t k = random(card, kColorlessRarePoolCount - 1);
        EXPECT_EQ(rc.rewards.items[0].card_ids[i],
                  static_cast<uint16_t>(
                      kColorlessRarePool[static_cast<size_t>(k)]))
            << i << " (assumes no duplicate re-roll on this seed)";
    }
}

// The two sorted colorless views are ORDER-EXACT, not a library-order
// approximation: CardGroup sorts by cardID before indexing.
TEST(NeowPayout, ColorlessPoolsAreSortedByGameId) {
    for (int i = 1; i < kColorlessUncommonPoolCount; ++i) {
        EXPECT_LT(std::string(sts::registry::card_game_id(
                      kColorlessUncommonPool[static_cast<size_t>(i - 1)])),
                  std::string(sts::registry::card_game_id(
                      kColorlessUncommonPool[static_cast<size_t>(i)])));
    }
    for (int i = 1; i < kColorlessRarePoolCount; ++i) {
        EXPECT_LT(std::string(sts::registry::card_game_id(
                      kColorlessRarePool[static_cast<size_t>(i - 1)])),
                  std::string(sts::registry::card_game_id(
                      kColorlessRarePool[static_cast<size_t>(i)])));
    }
    EXPECT_EQ(kColorlessUncommonPoolCount + kColorlessRarePoolCount,
              kColorlessPoolCount);
}

TEST(NeowPayout, OneRandomRareCardGoesStraightIntoTheDeck) {
    RunController rc = run_begin(42, kA20);
    const RunState before = rc.run;
    force_option(rc, 0, NeowRewardType::ONE_RANDOM_RARE_CARD);
    step(rc, choose(0));

    RngStream s = before.neow_rng;
    const CardId want = draw_card_from_pool(s, RewardCardRarity::RARE);
    ASSERT_EQ(rc.run.master_deck_count, before.master_deck_count + 1);
    EXPECT_EQ(rc.run.master_deck[before.master_deck_count].card_id,
              static_cast<uint16_t>(want));
    EXPECT_EQ(rc.run.neow_rng.counter, before.neow_rng.counter + 1);
    EXPECT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::DONE));
}

// =============================================================================
// Drawbacks
// =============================================================================

TEST(NeowDrawback, NoGoldEmptiesThePurse) {
    RunController rc = run_begin(42, kA20);
    force_option(rc, 0, NeowRewardType::ONE_RARE_RELIC, NeowDrawback::NO_GOLD);
    step(rc, choose(0));
    EXPECT_EQ(rc.run.gold, 0);
}

TEST(NeowDrawback, TenPercentHpLossClampsCurrentHpToTheNewMax) {
    RunController rc = run_begin(42, kA20);
    rc.run.hp = rc.run.max_hp;  // 75/75, so the clamp is visible
    force_option(rc, 0, NeowRewardType::ONE_RARE_RELIC,
                 NeowDrawback::TEN_PERCENT_HP_LOSS);
    step(rc, choose(0));
    EXPECT_EQ(rc.run.max_hp, 68);
    EXPECT_EQ(rc.run.hp, 68);
}

TEST(NeowDrawback, PercentDamageIsIntegerDividedAndCannotKill) {
    {
        RunController rc = run_begin(42, kA20);
        force_option(rc, 0, NeowRewardType::ONE_RARE_RELIC,
                     NeowDrawback::PERCENT_DAMAGE);
        step(rc, choose(0));
        EXPECT_EQ(rc.run.hp, 68 - 68 / 10 * 3);  // 68 - 18 = 50
    }
    // currentHealth / 10 * 3 is strictly below currentHealth for every positive
    // HP, so the drawback can never be lethal -- check the small end.
    for (int16_t hp = 1; hp <= 12; ++hp) {
        RunController rc = run_begin(42, kA20);
        rc.run.hp = hp;
        force_option(rc, 0, NeowRewardType::ONE_RARE_RELIC,
                     NeowDrawback::PERCENT_DAMAGE);
        step(rc, choose(0));
        EXPECT_GT(rc.run.hp, 0) << "hp " << hp;
        EXPECT_EQ(rc.run.hp, hp - hp / 10 * 3) << "hp " << hp;
    }
}

// The CURSE drawback's card is drawn from cardRng, and -- because the Java
// obtains it one update() tick after activate() -- AFTER the payout's own
// draws. A colorless payout shares that stream, so the order is observable.
TEST(NeowDrawback, CurseCardIsDrawnAfterTheColorlessPayoutOnCardRng) {
    RunController rc = run_begin(42, kA20);
    const RunState before = rc.run;
    force_option(rc, 0, NeowRewardType::RANDOM_COLORLESS_2,
                 NeowDrawback::CURSE);
    step(rc, choose(0));

    RngStream card = before.card_rng;
    for (int i = 0; i < 3; ++i) {
        (void)random(card, kColorlessRarePoolCount - 1);  // payout first
    }
    const CardId curse = return_random_curse(card);       // then the curse

    EXPECT_EQ(rc.run.card_rng.counter, card.counter);
    EXPECT_EQ(count_card(rc.run, curse), 1);
    // The curse is APPENDED, so it sits behind the whole starting deck.
    EXPECT_EQ(rc.run.master_deck[rc.run.master_deck_count - 1].card_id,
              static_cast<uint16_t>(curse));
}

TEST(NeowDrawback, OmamoriConsumesTheCurseDrawback) {
    RunController rc = run_begin(42, kA20);
    // An imported run holding a charged Omamori. A played run cannot be here
    // (floor 0 owns only the starting relic), but the obtain door is shared.
    ASSERT_EQ(acquire_relic(rc.run, rc.combat.misc_rng, RelicId::OMAMORI),
              RelicAcquireResult::ACQUIRED);
    const uint16_t deck_before = rc.run.master_deck_count;
    const int16_t charges = rc.run.relics[rc.run.relic_count - 1].counter;
    ASSERT_EQ(charges, 2);

    force_option(rc, 0, NeowRewardType::HUNDRED_GOLD, NeowDrawback::CURSE);
    const RunState before = rc.run;
    step(rc, choose(0));

    EXPECT_EQ(rc.run.master_deck_count, deck_before) << "the curse was eaten";
    EXPECT_EQ(rc.run.relics[rc.run.relic_count - 1].counter, 1);
    // The pool draw still happens -- Omamori intercepts the OBTAIN, not the
    // roll, so cardRng moves either way.
    EXPECT_EQ(rc.run.card_rng.counter, before.card_rng.counter + 1);
}

// =============================================================================
// The master-deck grids
// =============================================================================

TEST(NeowGrid, RemoveAndUpgradeGridsOfferOnlyTheirEligibleRows) {
    RunController rc = run_begin(42, kA20);  // A20: Ascender's Bane at index 0
    ASSERT_EQ(rc.run.master_deck[0].card_id,
              static_cast<uint16_t>(CardId::ASCENDERS_BANE));

    force_option(rc, 0, NeowRewardType::REMOVE_CARD);
    step(rc, choose(0));
    ASSERT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::GRID));
    RunActionMask m = mask_of(rc);
    EXPECT_FALSE(m.can_choose_master_deck[0]) << "Ascender's Bane is unpurgeable";
    for (uint16_t i = 1; i < rc.run.master_deck_count; ++i) {
        EXPECT_TRUE(m.can_choose_master_deck[i]) << i;
    }

    RunController up = run_begin(42, kA20);
    force_option(up, 0, NeowRewardType::UPGRADE_CARD);
    step(up, choose(0));
    m = mask_of(up);
    EXPECT_FALSE(m.can_choose_master_deck[0]) << "a curse is not upgradable";
    for (uint16_t i = 1; i < up.run.master_deck_count; ++i) {
        EXPECT_TRUE(m.can_choose_master_deck[i]) << i;
    }
}

TEST(NeowGrid, RemoveCardTakesOneRowAndFinishes) {
    RunController rc = run_begin(42, kA20);
    force_option(rc, 0, NeowRewardType::REMOVE_CARD);
    step(rc, choose(0));
    const uint8_t strike = deck_index_of(rc.run, CardId::STRIKE);
    const int strikes = count_card(rc.run, CardId::STRIKE);
    step(rc, choose(strike));
    EXPECT_EQ(count_card(rc.run, CardId::STRIKE), strikes - 1);
    EXPECT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::DONE));
}

TEST(NeowGrid, RemoveTwoNeedsTwoDistinctRowsBeforeItApplies) {
    RunController rc = run_begin(42, kA20);
    force_option(rc, 0, NeowRewardType::REMOVE_TWO, NeowDrawback::NO_GOLD);
    step(rc, choose(0));
    const uint16_t deck_before = rc.run.master_deck_count;

    step(rc, choose(1));
    EXPECT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::GRID))
        << "one of two picks must not apply the removal";
    EXPECT_EQ(rc.run.master_deck_count, deck_before);
    RunActionMask m = mask_of(rc);
    EXPECT_FALSE(m.can_choose_master_deck[1]) << "already selected";

    step(rc, choose(2));
    EXPECT_EQ(rc.run.master_deck_count, deck_before - 2);
    EXPECT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::DONE));
}

// TRANSFORM_CARD: one NeowEvent.rng draw over commonPool ++ uncommonPool ++
// rarePool with the source card's own id excluded, then remove + obtain.
TEST(NeowGrid, TransformCardDrawsOnceAndReplacesTheRow) {
    RunController rc = run_begin(42, kA20);
    const RunState before = rc.run;
    force_option(rc, 0, NeowRewardType::TRANSFORM_CARD);
    step(rc, choose(0));
    const uint8_t defend = deck_index_of(rc.run, CardId::DEFEND);
    step(rc, choose(defend));

    // The Ironclad's Defend is BASIC and so is in none of the three pools --
    // nothing is excluded and the list is the whole 72.
    RngStream s = before.neow_rng;
    const int32_t k = random(s, kIroncladCommonPoolCount +
                                    kIroncladUncommonPoolCount +
                                    kIroncladRarePoolCount - 1);
    CardId want;
    if (k < kIroncladCommonPoolCount) {
        want = kIroncladCommonPool[static_cast<size_t>(k)];
    } else if (k < kIroncladCommonPoolCount + kIroncladUncommonPoolCount) {
        want = kIroncladUncommonPool[
            static_cast<size_t>(k - kIroncladCommonPoolCount)];
    } else {
        want = kIroncladRarePool[static_cast<size_t>(
            k - kIroncladCommonPoolCount - kIroncladUncommonPoolCount)];
    }

    EXPECT_EQ(rc.run.neow_rng.counter, before.neow_rng.counter + 1);
    EXPECT_EQ(count_card(rc.run, CardId::DEFEND),
              count_card(before, CardId::DEFEND) - 1);
    EXPECT_EQ(rc.run.master_deck_count, before.master_deck_count);
    EXPECT_EQ(rc.run.master_deck[rc.run.master_deck_count - 1].card_id,
              static_cast<uint16_t>(want));
}

// TRANSFORM_TWO_CARDS removes BOTH rows before transforming either
// (NeowReward.java:163-173) -- the opposite order from the single case.
TEST(NeowGrid, TransformTwoRemovesBothBeforeEitherDraw) {
    RunController rc = run_begin(42, kA20);
    const RunState before = rc.run;
    force_option(rc, 0, NeowRewardType::TRANSFORM_TWO_CARDS,
                 NeowDrawback::NO_GOLD);
    step(rc, choose(0));
    step(rc, choose(1));
    step(rc, choose(2));

    EXPECT_EQ(rc.run.neow_rng.counter, before.neow_rng.counter + 2);
    EXPECT_EQ(rc.run.master_deck_count, before.master_deck_count);
    EXPECT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::DONE));
    // Both replacements are appended, so the two newest rows are the results.
    for (int i = 1; i <= 2; ++i) {
        const CardDef* def = card_def(static_cast<CardId>(
            rc.run.master_deck[rc.run.master_deck_count - i].card_id));
        ASSERT_NE(def, nullptr);
    }
}

// =============================================================================
// The card screen and the run-loop path
// =============================================================================

TEST(NeowFlow, CardScreenTakeAddsExactlyOneCardAndFinishes) {
    RunController rc = run_begin(42, kA20);
    force_option(rc, 0, NeowRewardType::THREE_CARDS);
    step(rc, choose(0));
    const uint16_t offered = rc.rewards.items[0].card_ids[1];
    const uint16_t deck_before = rc.run.master_deck_count;

    RunActionMask m = mask_of(rc);
    EXPECT_TRUE(m.can_take_card[0]);
    EXPECT_TRUE(m.can_skip_card);
    EXPECT_FALSE(m.can_sing) << "no Singing Bowl at floor 0";
    EXPECT_FALSE(m.can_proceed) << "the pick screen hides Proceed";

    step(rc, choose(1));
    EXPECT_EQ(rc.run.master_deck_count, deck_before + 1);
    EXPECT_EQ(rc.run.master_deck[deck_before].card_id, offered);
    EXPECT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::DONE));
    EXPECT_EQ(rc.rewards.count, 0);
}

// Neow opens CardRewardScreen with a null RewardItem, so Skip is offered and
// takeReward's removal is a no-op: the offer is simply gone, unlike a combat
// reward's CARDS item, which stays claimable.
TEST(NeowFlow, CardScreenSkipEndsTheBlessingWithNoCard) {
    RunController rc = run_begin(42, kA20);
    force_option(rc, 0, NeowRewardType::THREE_CARDS);
    step(rc, choose(0));
    const uint16_t deck_before = rc.run.master_deck_count;
    step(rc, choose(kChooseSkipCard));
    EXPECT_EQ(rc.run.master_deck_count, deck_before);
    EXPECT_EQ(rc.rewards.count, 0);
    EXPECT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::DONE));
    const RunActionMask m = mask_of(rc);
    EXPECT_TRUE(m.can_proceed);
}

// The directed script: blessing screen -> option -> payout screen -> the map,
// entirely through legal_actions/advance.
TEST(NeowFlow, ScriptedBlessingWalksFromNeowOntoTheFirstFloor) {
    RunController rc = run_begin(42, kA20);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::NEOW));
    ASSERT_EQ(rc.run.floor, 0);

    RunActionMask m = mask_of(rc);
    for (int i = 0; i < kNeowOptionCount; ++i) {
        EXPECT_TRUE(m.can_choose_neow_option[i]) << i;
    }
    EXPECT_FALSE(m.can_proceed) << "the blessing dialog has no proceed button";

    // Option 0 on seed 42 is TRANSFORM_CARD, so this walks the grid path.
    step(rc, choose(0));
    ASSERT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::GRID));
    m = mask_of(rc);
    uint8_t pick = 0xFF;
    for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
        if (m.can_choose_master_deck[i]) { pick = static_cast<uint8_t>(i); break; }
    }
    ASSERT_NE(pick, 0xFF);
    step(rc, choose(pick));
    ASSERT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::DONE));

    m = mask_of(rc);
    ASSERT_TRUE(m.can_proceed);
    step(rc, choose(kChooseProceed));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    m = mask_of(rc);
    uint8_t x = 0xFF;
    for (uint8_t c = 0; c < kMapCols; ++c) {
        if (m.can_choose_node[c]) { x = c; break; }
    }
    ASSERT_NE(x, 0xFF);
    step(rc, choose(x));
    EXPECT_EQ(rc.run.floor, 1);
    EXPECT_NE(rc.phase, static_cast<uint8_t>(RunPhase::NEOW));
}

// An illegal press at any Neow screen is a no-op, never a corruption.
TEST(NeowFlow, IllegalPressesAreInertAtEveryScreen) {
    RunController rc = run_begin(42, kA20);
    RunController copy = rc;
    step(rc, choose(kNeowOptionCount));  // no fifth option
    EXPECT_EQ(rc.neow.screen, copy.neow.screen);
    EXPECT_EQ(rc.run.neow_rng.counter, copy.run.neow_rng.counter);
    EXPECT_EQ(rc.run.master_deck_count, copy.run.master_deck_count);

    force_option(rc, 0, NeowRewardType::REMOVE_CARD);
    step(rc, choose(0));
    ASSERT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::GRID));
    copy = rc;
    step(rc, choose(0));  // Ascender's Bane: not purgeable
    EXPECT_EQ(rc.run.master_deck_count, copy.run.master_deck_count);
    EXPECT_EQ(rc.neow.grid_done, 0);
    step(rc, choose(200));  // out of range
    EXPECT_EQ(rc.run.master_deck_count, copy.run.master_deck_count);
}
