// beyond_events_test.cpp -- S2.33 tier-2: the seven TheBeyond eventList
// bodies (Falling, Mind Bloom, The Moai Head, Mysterious Sphere, Sensory
// Stone, Tomb of Lord Red Mask, Winding Halls). Every expected value below is
// hand-carried from the cited Java, read in full from
// D:\STS_BG_Mod\SlayTheSpireDecompiled -- see the provenance block at the top
// of src/engine/events/beyond_events.cpp. Twin replays (a copied RngStream /
// RunState driven through the same public helper) pin the stream-sensitive
// facts -- Mind Bloom's miscRng boss shuffle (s2-design trap 6), Mysterious
// Sphere's ctor-draw burn, Sensory Stone's colourless reward rolls -- so a
// drift in either half of the engine goes red here.

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include <gtest/gtest.h>

#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"
#include "sts/registry/event_table.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/ids.hpp"

namespace {

using namespace sts::engine;
namespace r = sts::registry;

constexpr int64_t kSeed = 81726354;

RunController event_controller(EventId id, int ascension = 0) {
    RunController rc = run_begin(kSeed, static_cast<uint8_t>(ascension));
    rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
    rc.room_type = static_cast<uint8_t>(RoomType::Event);
    rc.event = EventDialogState{};
    rc.event.event_id = static_cast<uint16_t>(id);
    rc.rewards = RewardScreen{};
    rc.rewards.open_card_item = kNoOpenCardReward;
    const EventDialogImpl* impl = event_dialog_impl(rc.event.event_id);
    EXPECT_NE(impl, nullptr);
    impl->on_enter(rc, rc.event);
    return rc;
}

// Same shape but with the entry deferred, so a test can shape the deck / gold
// / floor BEFORE the constructor-time draws run (Falling's setCards, the Moai
// Head's and Winding Halls' entry-time amounts).
RunController event_controller_pre_enter(EventId id, int ascension = 0) {
    RunController rc = run_begin(kSeed, static_cast<uint8_t>(ascension));
    rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
    rc.room_type = static_cast<uint8_t>(RoomType::Event);
    rc.event = EventDialogState{};
    rc.event.event_id = static_cast<uint16_t>(id);
    rc.rewards = RewardScreen{};
    rc.rewards.open_card_item = kNoOpenCardReward;
    return rc;
}

void enter(RunController& rc) {
    const EventDialogImpl* impl = event_dialog_impl(rc.event.event_id);
    ASSERT_NE(impl, nullptr);
    impl->on_enter(rc, rc.event);
}

void choose(RunController& rc, uint8_t option) {
    const Action a = make_action(ActionVerb::CHOOSE, option);
    StepResult result{};
    advance(std::span<RunController>(&rc, 1),
            std::span<const Action>(&a, 1),
            std::span<StepResult>(&result, 1));
}

EventDialogMenu menu(const RunController& rc) {
    EventDialogMenu out{};
    const EventDialogImpl* impl = event_dialog_impl(rc.event.event_id);
    EXPECT_NE(impl, nullptr);
    impl->build_menu(rc, rc.event, out);
    return out;
}

void set_deck(RunState& rs,
              std::initializer_list<std::pair<CardId, uint8_t>> cards) {
    rs.master_deck_count = 0;
    for (const auto& [id, upgrade] : cards) {
        ASSERT_TRUE(add_card_to_master_deck(rs, id, upgrade));
    }
}

bool owns(const RunState& rs, RelicId id) {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

void give_relic(RunState& rs, RelicId id) {
    ASSERT_LT(rs.relic_count, kRelicCap);
    rs.relics[rs.relic_count].relic_id = static_cast<uint16_t>(id);
    rs.relics[rs.relic_count].counter = -1;
    ++rs.relic_count;
}

int relic_counter(const RunState& rs, RelicId id) {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id == static_cast<uint16_t>(id)) {
            return rs.relics[i].counter;
        }
    }
    return 0;
}

int deck_count_of(const RunState& rs, CardId id) {
    int n = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (rs.master_deck[i].card_id == static_cast<uint16_t>(id)) {
            ++n;
        }
    }
    return n;
}

// The event bodies' RelicSpawnContext, rebuilt from the public fills exactly
// as events::event_relic_context builds it (event_common.hpp).
RelicSpawnContext spawn_ctx(const RunState& rs) {
    RelicSpawnContext ctx{};
    ctx.floor = rs.floor;
    fill_deck_spawn_gates(rs, ctx);
    fill_campfire_relic_count(rs, ctx);
    fill_boss_spawn_gates(rs, ctx);
    return ctx;
}

// --- Registry projection: the seven rows' body metadata ------------------------

TEST(BeyondEvents, RegistryRowsCarryTheAuditedScreenAndA15Counts) {
    struct Row {
        r::EventId id;
        int screens;
        int a15;
    };
    // Screen counts are the options lists in events.yaml, one per dialog page
    // (the reward-screen exits of Sensory Stone are not dialog pages).
    constexpr Row rows[] = {
        {r::EventId::FALLING, 3, 0},
        {r::EventId::MIND_BLOOM, 2, 0},           // the 25/50 split is A13
        {r::EventId::THE_MOAI_HEAD, 2, 1},        // 0.125 -> 0.18
        {r::EventId::MYSTERIOUS_SPHERE, 3, 0},
        {r::EventId::SENSORY_STONE, 2, 0},
        {r::EventId::TOMB_OF_LORD_RED_MASK, 2, 0},
        {r::EventId::WINDING_HALLS, 3, 2},        // damage up, heal DOWN
    };
    for (const Row& row : rows) {
        const r::EventDef* def = r::event_def(row.id);
        ASSERT_NE(def, nullptr);
        EXPECT_TRUE(def->native);
        EXPECT_TRUE(def->implemented);
        EXPECT_EQ(def->screen_count, row.screens)
            << r::event_game_id(row.id);
        EXPECT_EQ(def->a15_change_count, row.a15) << r::event_game_id(row.id);
        EXPECT_NE(event_dialog_impl(static_cast<uint16_t>(row.id)), nullptr);
    }
}

// --- Falling -------------------------------------------------------------------

TEST(Falling, DrawsOncePerPresentTypeInAttackSkillPowerOrder) {
    // Deck: 2 attacks, 1 skill, 1 power (+ a curse, which is no type of the
    // three). Expected picks replayed on a twin stream in the Java's
    // setCards statement order: attack, skill, power (Falling.java:56-64),
    // each one random(size - 1).
    RunController rc = event_controller_pre_enter(EventId::FALLING);
    set_deck(rc.run, {{CardId::STRIKE, 0},
                      {CardId::BASH, 0},
                      {CardId::SHRUG_IT_OFF, 0},
                      {CardId::INFLAME, 0},
                      {CardId::REGRET, 0}});
    RngStream twin = rc.combat.misc_rng;
    const int32_t attack_pick = random(twin, 1);  // 2 attacks
    const int32_t skill_pick = random(twin, 0);   // 1 skill
    const int32_t power_pick = random(twin, 0);   // 1 power
    enter(rc);

    EXPECT_EQ(rc.combat.misc_rng.counter, twin.counter) << "three draws total";
    EXPECT_EQ(rc.event.scratch0, attack_pick);  // attacks sit at deck 0..1
    EXPECT_EQ(rc.event.scratch1, 2 + skill_pick);
    EXPECT_EQ(rc.event.scratch2, 3 + power_pick);

    // CHOICE screen order is skill / power / attack (:78-92).
    choose(rc, 0);  // intro
    const EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 3);
    EXPECT_TRUE(m.enabled[0]);
    EXPECT_TRUE(m.enabled[1]);
    EXPECT_TRUE(m.enabled[2]);

    // Button 1 removes the POWER.
    const uint16_t before = rc.run.master_deck_count;
    choose(rc, 1);
    EXPECT_EQ(rc.run.master_deck_count, before - 1);
    EXPECT_EQ(deck_count_of(rc.run, CardId::INFLAME), 0);

    // RESULT proceed leaves for the map.
    choose(rc, 0);
    EXPECT_EQ(static_cast<RunPhase>(rc.phase), RunPhase::MAP_CHOICE);
}

TEST(Falling, AbsentTypesDrawNothingAndTheirOptionsAreDisabled) {
    RunController rc = event_controller_pre_enter(EventId::FALLING);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::STRIKE, 0}});
    const int32_t before = rc.combat.misc_rng.counter;
    enter(rc);
    // One draw (attack only): hasCardWithType is false for skill and power, so
    // returnCardOfType never runs for them (CardHelper.java:88-103).
    EXPECT_EQ(rc.combat.misc_rng.counter, before + 1);
    EXPECT_GE(rc.event.scratch0, 0);
    EXPECT_EQ(rc.event.scratch1, -1);
    EXPECT_EQ(rc.event.scratch2, -1);

    choose(rc, 0);
    const EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 3);
    EXPECT_FALSE(m.enabled[0]);  // skill
    EXPECT_FALSE(m.enabled[1]);  // power
    EXPECT_TRUE(m.enabled[2]);   // attack
}

TEST(Falling, BottledCardsAreInvisibleToBothThePresenceTestAndTheDraw) {
    // The deck's ONLY attack is bottled: getGroupWithoutBottledCards drops it,
    // so the attack row is ABSENT -- no draw, disabled option -- while the
    // unbottled second skill is drawn from a 1-card list.
    RunController rc = event_controller_pre_enter(EventId::FALLING);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::SHRUG_IT_OFF, 0}});
    rc.run.master_deck[0].flags |= kMasterCardInBottleFlame;
    const int32_t before = rc.combat.misc_rng.counter;
    enter(rc);
    EXPECT_EQ(rc.combat.misc_rng.counter, before + 1);  // skill draw only
    EXPECT_EQ(rc.event.scratch0, -1);
    EXPECT_EQ(rc.event.scratch1, 1);
}

TEST(Falling, AnEmptyHarvestOffersOneButtonAndRemovesNothing) {
    RunController rc = event_controller_pre_enter(EventId::FALLING);
    set_deck(rc.run, {{CardId::REGRET, 0}, {CardId::ASCENDERS_BANE, 0}});
    const int32_t before = rc.combat.misc_rng.counter;
    enter(rc);
    EXPECT_EQ(rc.combat.misc_rng.counter, before);  // zero draws

    choose(rc, 0);
    const EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 1);  // OPTIONS[8] (Falling.java:74-76)
    EXPECT_TRUE(m.enabled[0]);
    const uint16_t deck = rc.run.master_deck_count;
    choose(rc, 0);
    EXPECT_EQ(rc.run.master_deck_count, deck);  // nothing removed (:101-104)
    choose(rc, 0);
    EXPECT_EQ(static_cast<RunPhase>(rc.phase), RunPhase::MAP_CHOICE);
}

// --- Mind Bloom ------------------------------------------------------------------

// The trap-6 pin: ONE miscRng.randomLong seeds a JDK Random, Collections
// .shuffle orders the three ACT-1 boss keys, and the fight is list.get(0)
// (MindBloom.java:66-71). The twin replays the identical draw + shuffle
// through the same JdkRandom/jdk_shuffle pair and predicts the slot-0
// monster.
TEST(MindBloom, WarShufflesTheActOneBossesOnOneMiscDrawAndFightsTheFirst) {
    for (const int32_t floor : {36, 41, 47}) {
        RunController rc = event_controller(EventId::MIND_BLOOM);
        rc.run.floor = static_cast<uint16_t>(floor);
        rc.run.act = 3;

        RngStream twin = rc.combat.misc_rng;
        std::array<r::MonsterId, 3> order = {
            r::MonsterId::THE_GUARDIAN,  // "The Guardian"  MindBloom.java:67
            r::MonsterId::HEXAGHOST,     // "Hexaghost"     :68
            r::MonsterId::SLIME_BOSS,    // "Slime Boss"    :69
        };
        JdkRandom jr(random_long(twin));
        jdk_shuffle(std::span<r::MonsterId>(order), jr);

        choose(rc, 0);

        ASSERT_EQ(static_cast<RunPhase>(rc.phase), RunPhase::COMBAT)
            << "floor " << floor;
        ASSERT_GE(rc.combat.monster_count, 1);
        EXPECT_EQ(rc.combat.monsters[0].monster_id,
                  static_cast<uint16_t>(order[0]))
            << "floor " << floor;
        // An EventRoom combat: room kind stays Event (no monsterList pop on
        // exit) and eliteTrigger is NOT set (unlike Dead Adventurer).
        EXPECT_EQ(static_cast<RoomType>(rc.room_type), RoomType::Event);
        EXPECT_EQ(rc.combat.flags & kCombatFlagEliteRoom, 0u);
        EXPECT_EQ(rc.event.event_id, 0);  // dialog cleared behind the fight
    }
}

TEST(MindBloom, WarSeedsFixedGoldAndOneRareRelicRewardBeforeTheFight) {
    RunController rc = event_controller(EventId::MIND_BLOOM);
    rc.run.floor = 38;
    rc.run.act = 3;

    // Twin: the relic is a plain returnRandomRelic(RARE) pool pop
    // (AbstractRoom.java:541-543), NOT the screenless variant.
    RunState twin = rc.run;
    const RelicId expected =
        return_random_relic_key(twin, RelicTier::RARE, spawn_ctx(twin));

    choose(rc, 0);

    ASSERT_GE(rc.rewards.count, 2);
    EXPECT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::GOLD));
    EXPECT_EQ(rc.rewards.items[0].gold, 50);  // below A13 (MindBloom.java:76)
    EXPECT_EQ(rc.rewards.items[1].kind,
              static_cast<uint8_t>(RewardItemKind::RELIC));
    EXPECT_EQ(rc.rewards.items[1].id, static_cast<uint16_t>(expected));
    // The pool cursor moved identically: the twin and the live run agree on
    // the whole RARE pool tail.
    EXPECT_EQ(rc.run.relic_pool_count[static_cast<int>(RelicPool::RARE)],
              twin.relic_pool_count[static_cast<int>(RelicPool::RARE)]);

    // A13+: the fixed payout drops to 25 with no extra draw (a20.yaml row 13's
    // Mind Bloom clause -- fixed, unlike roll_boss_gold's miscRng draw).
    RunController a13 = event_controller(EventId::MIND_BLOOM, 13);
    a13.run.floor = 38;
    a13.run.act = 3;
    const RngStream misc_before = a13.combat.misc_rng;
    choose(a13, 0);
    ASSERT_GE(a13.rewards.count, 2);
    EXPECT_EQ(a13.rewards.items[0].gold, 25);
    // Exactly ONE misc draw happened: the shuffle's randomLong. Fixed gold
    // draws nothing.
    RngStream one = misc_before;
    (void)random_long(one);
    EXPECT_EQ(a13.combat.misc_rng.counter, one.counter);
}

TEST(MindBloom, AwakeUpgradesEveryUpgradableCardAndGrantsMarkOfTheBloom) {
    RunController rc = event_controller(EventId::MIND_BLOOM);
    rc.run.floor = 38;
    set_deck(rc.run, {{CardId::STRIKE, 0},
                      {CardId::BASH, 1},          // already upgraded: untouched
                      {CardId::SEARING_BLOW, 2},  // canUpgrade forever: 2 -> 3
                      {CardId::REGRET, 0}});      // curse: untouched

    choose(rc, 1);

    EXPECT_EQ(rc.run.master_deck[0].upgrade, 1);
    EXPECT_EQ(rc.run.master_deck[1].upgrade, 1);
    EXPECT_EQ(rc.run.master_deck[2].upgrade, 3);
    EXPECT_EQ(rc.run.master_deck[3].upgrade, 0);
    EXPECT_TRUE(owns(rc.run, RelicId::MARK_OF_THE_BLOOM));

    // LEAVE page, then the map.
    choose(rc, 0);
    EXPECT_EQ(static_cast<RunPhase>(rc.phase), RunPhase::MAP_CHOICE);
}

TEST(MindBloom, ThirdOptionSplitsOnFloorMod50AtExactlyFortyOne) {
    // floorNum % 50 <= 40 (MindBloom.java:49, :108): Act-3 ? floors 36..40
    // pay 999 gold + 2 Normality; 41..49 full-heal + Doubt.
    RunController gold = event_controller(EventId::MIND_BLOOM);
    gold.run.floor = 40;
    gold.run.hp = 10;
    const int32_t purse = gold.run.gold;
    choose(gold, 2);
    EXPECT_EQ(gold.run.gold, purse + 999);
    EXPECT_EQ(deck_count_of(gold.run, CardId::NORMALITY), 2);
    EXPECT_EQ(gold.run.hp, 10);  // no heal on the gold arm

    RunController healed = event_controller(EventId::MIND_BLOOM);
    healed.run.floor = 41;
    healed.run.hp = 10;
    const int32_t purse2 = healed.run.gold;
    choose(healed, 2);
    EXPECT_EQ(healed.run.gold, purse2);
    EXPECT_EQ(healed.run.hp, healed.run.max_hp);  // full heal (:126)
    EXPECT_EQ(deck_count_of(healed.run, CardId::DOUBT), 1);
}

TEST(MindBloom, OmamoriEatsTheCursesAndTheHealRunsBeforeTheDoubt) {
    // Gold arm: BOTH Normality obtains run the Omamori gate, one charge each.
    RunController rc = event_controller(EventId::MIND_BLOOM);
    rc.run.floor = 38;
    give_relic(rc.run, RelicId::OMAMORI);
    rc.run.relics[rc.run.relic_count - 1].counter = 2;
    choose(rc, 2);
    EXPECT_EQ(deck_count_of(rc.run, CardId::NORMALITY), 0);
    EXPECT_EQ(relic_counter(rc.run, RelicId::OMAMORI), 0);

    // Heal arm with Mark of the Bloom already owned (a second Mind Bloom this
    // act): heal(maxHealth) is suppressed to zero, the Doubt still lands.
    RunController marked = event_controller(EventId::MIND_BLOOM);
    marked.run.floor = 41;
    marked.run.hp = 10;
    give_relic(marked.run, RelicId::MARK_OF_THE_BLOOM);
    choose(marked, 2);
    EXPECT_EQ(marked.run.hp, 10);
    EXPECT_EQ(deck_count_of(marked.run, CardId::DOUBT), 1);
}

// The sim half of the S2.33 directed-capture bar: win the re-fight and walk
// the reward screen. The oracle-side zero-diff capture is deferred to S2.43
// (the next capture campaign) -- see the ledger's deferred-obligations row.
TEST(MindBloom, VictoryKeepsThePreseededRowsAndProceedGoesToTheMapNotAChest) {
    RunController rc = event_controller(EventId::MIND_BLOOM);
    rc.run.floor = 38;
    rc.run.act = 3;
    RunState twin = rc.run;
    const RelicId expected_relic =
        return_random_relic_key(twin, RelicTier::RARE, spawn_ctx(twin));

    choose(rc, 0);
    ASSERT_EQ(static_cast<RunPhase>(rc.phase), RunPhase::COMBAT);

    // Force the kill (the boss bodies have their own combat suites); the
    // battle-over assembly must PRESERVE the two pre-seeded rows and append
    // the potion/card rows behind them -- the Mushrooms/DeadAdventurer shape.
    rc.combat.phase = static_cast<uint8_t>(CombatPhase::COMBAT_OVER);
    rc.combat.player_hp = rc.run.hp;
    choose(rc, 0);
    ASSERT_EQ(static_cast<RunPhase>(rc.phase), RunPhase::COMBAT_REWARD);
    ASSERT_GE(rc.rewards.count, 3);
    EXPECT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::GOLD));
    EXPECT_EQ(rc.rewards.items[0].gold, 50);
    EXPECT_EQ(rc.rewards.items[1].kind,
              static_cast<uint8_t>(RewardItemKind::RELIC));
    // The last row is the ordinary EVENT-room card reward (base odds, not the
    // MonsterRoomBoss all-RARE row -- the room kind stayed Event).
    EXPECT_EQ(rc.rewards.items[rc.rewards.count - 1].kind,
              static_cast<uint8_t>(RewardItemKind::CARDS));

    // Claim both payout rows through the one claim surface.
    const int32_t purse = rc.run.gold;
    choose(rc, 0);
    EXPECT_EQ(rc.run.gold, purse + 50);
    choose(rc, 0);  // items shift down after a claim: the relic is row 0 now
    EXPECT_TRUE(owns(rc.run, expected_relic));

    // Proceed: MAP_CHOICE -- an EventRoom is not a MonsterRoomBoss, so no
    // boss chest and no act transition follow the re-fight.
    choose(rc, kChooseProceed);
    EXPECT_EQ(static_cast<RunPhase>(rc.phase), RunPhase::MAP_CHOICE);
}

// --- The Moai Head ---------------------------------------------------------------

TEST(MoaiHead, EntryFreezesTheTieredRoundedMaxHpCost) {
    RunController base = event_controller_pre_enter(EventId::THE_MOAI_HEAD);
    base.run.max_hp = 80;
    base.run.hp = 30;
    enter(base);
    EXPECT_EQ(base.event.scratch0, 10);  // round(80 * 0.125f)

    RunController a15 = event_controller_pre_enter(EventId::THE_MOAI_HEAD, 15);
    a15.run.max_hp = 80;
    a15.run.hp = 30;
    enter(a15);
    EXPECT_EQ(a15.event.scratch0, 14);  // MathUtils.round(80 * 0.18f) = 14.4 ->

    // The heal arm: maxHealth -= hpAmt, then a FULL heal (MoaiHead.java:54-61).
    choose(base, 0);
    EXPECT_EQ(base.run.max_hp, 70);
    EXPECT_EQ(base.run.hp, 70);
    choose(base, 0);
    EXPECT_EQ(static_cast<RunPhase>(base.phase), RunPhase::MAP_CHOICE);
}

TEST(MoaiHead, MarkOfTheBloomKeepsTheCostAndCancelsTheHeal) {
    RunController rc = event_controller_pre_enter(EventId::THE_MOAI_HEAD);
    rc.run.max_hp = 80;
    rc.run.hp = 30;
    give_relic(rc.run, RelicId::MARK_OF_THE_BLOOM);
    enter(rc);
    choose(rc, 0);
    EXPECT_EQ(rc.run.max_hp, 70);
    EXPECT_EQ(rc.run.hp, 30);  // heal(maxHealth) -> onPlayerHeal -> 0
}

TEST(MoaiHead, TheIdolTradeIsGatedAndPays333) {
    RunController without = event_controller(EventId::THE_MOAI_HEAD);
    EXPECT_FALSE(menu(without).enabled[1]);  // !hasRelic disables (:37-41)
    EXPECT_TRUE(menu(without).enabled[0]);
    EXPECT_TRUE(menu(without).enabled[2]);

    RunController with = event_controller_pre_enter(EventId::THE_MOAI_HEAD);
    give_relic(with.run, RelicId::GOLDEN_IDOL);
    enter(with);
    EXPECT_TRUE(menu(with).enabled[1]);
    const int32_t purse = with.run.gold;
    choose(with, 1);
    EXPECT_FALSE(owns(with.run, RelicId::GOLDEN_IDOL));  // loseRelic first (:72)
    EXPECT_EQ(with.run.gold, purse + 333);               // then gainGold (:74)
}

// --- Mysterious Sphere -------------------------------------------------------------

TEST(MysteriousSphere, TheFightSeedsRolledGoldAndAScreenlessRareRelic) {
    RunController rc = event_controller(EventId::MYSTERIOUS_SPHERE);
    rc.run.floor = 38;
    rc.run.act = 3;

    RngStream misc_twin = rc.combat.misc_rng;
    RunState run_twin = rc.run;

    choose(rc, 0);  // "Open it" -> the confirm page
    ASSERT_EQ(static_cast<RunPhase>(rc.phase), RunPhase::EVENT_DIALOG);
    ASSERT_EQ(menu(rc).count, 1);

    // Twin replay of the confirm press (MysteriousSphere.java:74-88): gold
    // miscRng.random(45, 55), then returnRandomScreenlessRelic(RARE).
    const int32_t expected_gold = random(misc_twin, 45, 55);
    const RelicId expected_relic = return_random_screenless_relic(
        run_twin, RelicTier::RARE, spawn_ctx(run_twin));

    choose(rc, 0);

    ASSERT_EQ(static_cast<RunPhase>(rc.phase), RunPhase::COMBAT);
    ASSERT_EQ(rc.combat.monster_count, 2);
    EXPECT_EQ(rc.combat.monsters[0].monster_id,
              static_cast<uint16_t>(r::MonsterId::ORB_WALKER));
    EXPECT_EQ(rc.combat.monsters[1].monster_id,
              static_cast<uint16_t>(r::MonsterId::ORB_WALKER));
    EXPECT_EQ(rc.combat.flags & kCombatFlagEliteRoom, 0u);
    ASSERT_GE(rc.rewards.count, 2);
    EXPECT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::GOLD));
    EXPECT_EQ(rc.rewards.items[0].gold, expected_gold);
    EXPECT_GE(expected_gold, 45);
    EXPECT_LE(expected_gold, 55);
    EXPECT_EQ(rc.rewards.items[1].kind,
              static_cast<uint8_t>(RewardItemKind::RELIC));
    EXPECT_EQ(rc.rewards.items[1].id, static_cast<uint16_t>(expected_relic));
}

TEST(MysteriousSphere, LeavingBurnsExactlyTheCtorDrawsTheFightWouldSpend) {
    // The game constructs "2 Orb Walkers" in the event CONSTRUCTOR
    // (MysteriousSphere.java:39), so monsterHpRng advances by the two ctor
    // pairs on BOTH exits. The two paths must therefore leave the stream at
    // the same position.
    RunController leave2 = event_controller(EventId::MYSTERIOUS_SPHERE);
    leave2.run.floor = 38;
    leave2.run.act = 3;
    RunController fight2 = leave2;

    choose(leave2, 1);  // INTRO "Leave"
    choose(fight2, 0);  // INTRO "Open it"
    choose(fight2, 0);  // confirm -> combat construction

    EXPECT_EQ(leave2.combat.monster_hp_rng.counter,
              fight2.combat.monster_hp_rng.counter)
        << "leave path must burn the same four ctor draws (2x super-arg + "
           "2x setHp, monster_orb_walker.hpp)";
    // And the burn is exactly four draws.
    RunController fresh = event_controller(EventId::MYSTERIOUS_SPHERE);
    fresh.run.floor = 38;
    fresh.run.act = 3;
    const int32_t before = fresh.combat.monster_hp_rng.counter;
    choose(fresh, 1);
    EXPECT_EQ(fresh.combat.monster_hp_rng.counter, before + 4);
    // END page proceeds to the map.
    choose(fresh, 0);
    EXPECT_EQ(static_cast<RunPhase>(fresh.phase), RunPhase::MAP_CHOICE);
}

// --- Sensory Stone -----------------------------------------------------------------

// One memory choice, twin-replayed end to end: ONE miscRng.randomLong (the
// cosmetic shuffle's seed -- the DRAW is the state), then per RewardItem three
// colourless cards on cardRng -- rollRareOrUncommon(0.3f) each, RARE resets
// the red pity counter, no-dupe redraws from the same rarity, NO upgrade roll
// (AbstractDungeon.java:1381-1421).
TEST(SensoryStone, MemoriesRollTheColorlessRewardExactlyAsTheJavaDoes) {
    for (const int opt : {0, 1, 2}) {
        const auto option = static_cast<uint8_t>(opt);
        RunController rc = event_controller(EventId::SENSORY_STONE);
        rc.run.hp = 60;
        choose(rc, 0);  // intro

        RngStream misc_twin = rc.combat.misc_rng;
        RngStream card_twin = rc.run.card_rng;
        int16_t blizz_twin = rc.run.card_blizz_randomizer;

        choose(rc, option);

        // The misc draw happened exactly once, whatever the choice.
        (void)random_long(misc_twin);
        EXPECT_EQ(rc.combat.misc_rng.counter, misc_twin.counter);

        const int rows = option + 1;
        ASSERT_EQ(rc.rewards.count, rows) << int(option);
        for (int item = 0; item < rows; ++item) {
            ASSERT_EQ(rc.rewards.items[item].kind,
                      static_cast<uint8_t>(RewardItemKind::CARDS));
            ASSERT_EQ(rc.rewards.items[item].card_count, 3);
            uint16_t seen[3] = {};
            for (int i = 0; i < 3; ++i) {
                const RewardCardRarity rarity =
                    random_boolean(card_twin, kColorlessRareChance)
                        ? RewardCardRarity::RARE
                        : RewardCardRarity::UNCOMMON;
                if (rarity == RewardCardRarity::RARE) {
                    blizz_twin = 5;  // cardBlizzStartOffset (:1396)
                }
                CardId id;
                bool dupe;
                do {
                    id = draw_colorless_card_from_pool(card_twin, rarity);
                    dupe = false;
                    for (int j = 0; j < i; ++j) {
                        dupe = dupe || seen[j] == static_cast<uint16_t>(id);
                    }
                } while (dupe);
                seen[i] = static_cast<uint16_t>(id);
                EXPECT_EQ(rc.rewards.items[item].card_ids[i], seen[i])
                    << "option " << int(option) << " item " << item
                    << " card " << i;
                EXPECT_EQ(rc.rewards.items[item].card_upgrades[i], 0)
                    << "colourless offers are never upgraded";
            }
        }
        EXPECT_EQ(rc.run.card_rng.counter, card_twin.counter);
        EXPECT_EQ(rc.run.card_blizz_randomizer, blizz_twin);

        // The HP cost lands AFTER the rolls: 0 / 5 / 10 HP_LOSS.
        EXPECT_EQ(rc.run.hp, 60 - (option == 0 ? 0 : option == 1 ? 5 : 10));
        EXPECT_EQ(static_cast<RunPhase>(rc.phase), RunPhase::COMBAT_REWARD);
        EXPECT_EQ(rc.event.event_id, 0);
    }
}

TEST(SensoryStone, ALethalMemoryStillMovedCardRngBeforeTheDeath) {
    RunController rc = event_controller(EventId::SENSORY_STONE);
    rc.run.hp = 8;
    choose(rc, 0);
    const int32_t card_before = rc.run.card_rng.counter;
    choose(rc, 2);  // 10 HP_LOSS on 8 hp, no potion belt: death
    EXPECT_EQ(static_cast<RunPhase>(rc.phase), RunPhase::RUN_OVER);
    EXPECT_GT(rc.run.card_rng.counter, card_before)
        << "reward() runs before the damage (SensoryStone.java:81-83)";
}

TEST(SensoryStone, RewardCountRelicsApplyAndTheClaimIsTheOrdinaryDoor) {
    // Question Card +1 / Busted Crown -2 flow through
    // changeNumberOfCardsInReward exactly as in a combat reward (:1383-1386).
    RunController plus = event_controller(EventId::SENSORY_STONE);
    give_relic(plus.run, RelicId::QUESTION_CARD);
    choose(plus, 0);
    choose(plus, 0);
    ASSERT_EQ(plus.rewards.count, 1);
    EXPECT_EQ(plus.rewards.items[0].card_count, 4);

    RunController minus = event_controller(EventId::SENSORY_STONE);
    give_relic(minus.run, RelicId::BUSTED_CROWN);
    choose(minus, 0);
    choose(minus, 0);
    ASSERT_EQ(minus.rewards.count, 1);
    EXPECT_EQ(minus.rewards.items[0].card_count, 1);

    // Claiming pays through the one claim surface: open the CARDS row, take
    // the first offer, and the master deck gains that colourless card.
    RunController rc = event_controller(EventId::SENSORY_STONE);
    choose(rc, 0);
    choose(rc, 0);
    ASSERT_EQ(static_cast<RunPhase>(rc.phase), RunPhase::COMBAT_REWARD);
    const uint16_t offered = rc.rewards.items[0].card_ids[0];
    const uint16_t deck_before = rc.run.master_deck_count;
    choose(rc, 0);  // claim item 0 -> opens the pick screen
    choose(rc, 0);  // take card 0
    ASSERT_EQ(rc.run.master_deck_count, deck_before + 1);
    EXPECT_EQ(rc.run.master_deck[deck_before].card_id, offered);
}

// --- Tomb of Lord Red Mask ----------------------------------------------------------

TEST(TombRedMask, WithoutTheMaskTheWearRowIsDisabledAndBuyingTakesAllGold) {
    RunController rc = event_controller(EventId::TOMB_OF_LORD_RED_MASK);
    rc.run.gold = 137;
    const EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 3);
    EXPECT_FALSE(m.enabled[0]);  // OPTIONS[1], disabled (:37)
    EXPECT_TRUE(m.enabled[1]);
    EXPECT_TRUE(m.enabled[2]);

    choose(rc, 1);
    EXPECT_EQ(rc.run.gold, 0);
    EXPECT_TRUE(owns(rc.run, RelicId::RED_MASK));
    choose(rc, 0);
    EXPECT_EQ(static_cast<RunPhase>(rc.phase), RunPhase::MAP_CHOICE);
}

TEST(TombRedMask, BuyingAtZeroGoldIsLegalAndStillPaysTheMask) {
    // No gold gate anywhere in the class: loseGold(0) is a purse no-op and
    // spawnRelicAndObtain still runs (:52-57).
    RunController rc = event_controller(EventId::TOMB_OF_LORD_RED_MASK);
    rc.run.gold = 0;
    EXPECT_TRUE(menu(rc).enabled[1]);
    choose(rc, 1);
    EXPECT_TRUE(owns(rc.run, RelicId::RED_MASK));
    EXPECT_EQ(rc.run.gold, 0);
}

TEST(TombRedMask, WithTheMaskWearingItPays222AndLeaveExitsAtOnce) {
    RunController rc = event_controller_pre_enter(EventId::TOMB_OF_LORD_RED_MASK);
    give_relic(rc.run, RelicId::RED_MASK);
    enter(rc);
    const EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 2);
    EXPECT_TRUE(m.enabled[0]);
    EXPECT_TRUE(m.enabled[1]);

    const int32_t purse = rc.run.gold;
    choose(rc, 0);
    EXPECT_EQ(rc.run.gold, purse + 222);
    choose(rc, 0);
    EXPECT_EQ(static_cast<RunPhase>(rc.phase), RunPhase::MAP_CHOICE);

    // Leave is immediate: openMap fires in the same press (:58-63).
    RunController leave = event_controller_pre_enter(
        EventId::TOMB_OF_LORD_RED_MASK);
    give_relic(leave.run, RelicId::RED_MASK);
    enter(leave);
    choose(leave, 1);
    EXPECT_EQ(static_cast<RunPhase>(leave.phase), RunPhase::MAP_CHOICE);
}

// --- Winding Halls -------------------------------------------------------------------

TEST(WindingHalls, EntryFreezesTheThreeTieredRoundedAmounts) {
    RunController base = event_controller_pre_enter(EventId::WINDING_HALLS);
    base.run.max_hp = 80;
    base.run.hp = 60;
    enter(base);
    EXPECT_EQ(base.event.scratch0, 10);  // round(80 * 0.125f)
    EXPECT_EQ(base.event.scratch1, 20);  // round(80 * 0.25f)
    EXPECT_EQ(base.event.scratch2, 4);   // round(80 * 0.05f), tier-free

    RunController a15 = event_controller_pre_enter(EventId::WINDING_HALLS, 15);
    a15.run.max_hp = 80;
    a15.run.hp = 60;
    enter(a15);
    EXPECT_EQ(a15.event.scratch0, 14);  // round(80 * 0.18f)
    EXPECT_EQ(a15.event.scratch1, 16);  // round(80 * 0.2f) -- A15 heals LESS
    EXPECT_EQ(a15.event.scratch2, 4);
}

TEST(WindingHalls, MadnessArmDamagesThenObtainsTwoMadness) {
    RunController rc = event_controller_pre_enter(EventId::WINDING_HALLS);
    rc.run.max_hp = 80;
    rc.run.hp = 60;
    enter(rc);
    choose(rc, 0);  // intro
    choose(rc, 0);
    EXPECT_EQ(rc.run.hp, 50);
    EXPECT_EQ(deck_count_of(rc.run, CardId::MADNESS), 2);
    choose(rc, 0);
    EXPECT_EQ(static_cast<RunPhase>(rc.phase), RunPhase::MAP_CHOICE);

    // Lethal: the Java constructs the two obtain effects after the damage
    // regardless, and the run is over.
    RunController lethal = event_controller_pre_enter(EventId::WINDING_HALLS);
    lethal.run.max_hp = 80;
    lethal.run.hp = 9;
    enter(lethal);
    choose(lethal, 0);
    choose(lethal, 0);
    EXPECT_EQ(static_cast<RunPhase>(lethal.phase), RunPhase::RUN_OVER);
}

TEST(WindingHalls, WritheArmHealsThroughTheDoorThenObtains) {
    RunController rc = event_controller_pre_enter(EventId::WINDING_HALLS);
    rc.run.max_hp = 80;
    rc.run.hp = 40;
    enter(rc);
    choose(rc, 0);
    choose(rc, 1);
    EXPECT_EQ(rc.run.hp, 60);  // +20
    EXPECT_EQ(deck_count_of(rc.run, CardId::WRITHE), 1);

    // Mark of the Bloom cancels the heal; the Writhe still lands.
    RunController marked = event_controller_pre_enter(EventId::WINDING_HALLS);
    marked.run.max_hp = 80;
    marked.run.hp = 40;
    give_relic(marked.run, RelicId::MARK_OF_THE_BLOOM);
    enter(marked);
    choose(marked, 0);
    choose(marked, 1);
    EXPECT_EQ(marked.run.hp, 40);
    EXPECT_EQ(deck_count_of(marked.run, CardId::WRITHE), 1);

    // Omamori eats the Writhe; the heal still lands.
    RunController warded = event_controller_pre_enter(EventId::WINDING_HALLS);
    warded.run.max_hp = 80;
    warded.run.hp = 40;
    give_relic(warded.run, RelicId::OMAMORI);
    warded.run.relics[warded.run.relic_count - 1].counter = 2;
    enter(warded);
    choose(warded, 0);
    choose(warded, 1);
    EXPECT_EQ(warded.run.hp, 60);
    EXPECT_EQ(deck_count_of(warded.run, CardId::WRITHE), 0);
    EXPECT_EQ(relic_counter(warded.run, RelicId::OMAMORI), 1);
}

TEST(WindingHalls, PressOnLosesFivePercentMaxHp) {
    RunController rc = event_controller_pre_enter(EventId::WINDING_HALLS);
    rc.run.max_hp = 80;
    rc.run.hp = 79;
    enter(rc);
    choose(rc, 0);
    choose(rc, 2);
    EXPECT_EQ(rc.run.max_hp, 76);
    EXPECT_EQ(rc.run.hp, 76);  // clamped down to the new max
}

}  // namespace
