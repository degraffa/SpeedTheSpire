// S2.32's five Act-2 eventList bodies: The Library, The Mausoleum, Vampires,
// and the two combat embeds (Colosseum -- the game's one two-fight sequence --
// and Masked Bandits), plus the Bandit trio's monster behaviour and the
// deathReact verified-negative pin. Per-event provenance is on the bodies
// (src/engine/events/city_events_ii.cpp, monster_bandits.hpp).

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include <gtest/gtest.h>

#include "sts/engine/action_queue.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_bandits.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"

namespace sts::engine {
namespace {

constexpr int64_t kSeed = 5150312;

RunController event_controller(EventId id, int ascension = 0) {
    RunController rc = run_begin(kSeed, static_cast<uint8_t>(ascension));
    rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
    rc.room_type = static_cast<uint8_t>(RoomType::Event);
    rc.run.act = 2;
    rc.run.floor = 22;
    rc.event = EventDialogState{};
    rc.event.event_id = static_cast<uint16_t>(id);
    rc.rewards = RewardScreen{};
    rc.rewards.open_card_item = kNoOpenCardReward;
    const EventDialogImpl* impl = event_dialog_impl(rc.event.event_id);
    EXPECT_NE(impl, nullptr);
    impl->on_enter(rc, rc.event);
    return rc;
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

void give_relic(RunState& rs, RelicId id, int16_t counter = -1) {
    ASSERT_LT(rs.relic_count, kRelicCap);
    rs.relics[rs.relic_count].relic_id = static_cast<uint16_t>(id);
    rs.relics[rs.relic_count].counter = counter;
    ++rs.relic_count;
}

int deck_count_of(const RunState& rs, CardId id) {
    int n = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        n += rs.master_deck[i].card_id == static_cast<uint16_t>(id);
    }
    return n;
}

// Kill the live combat and step once so finish_combat_after_action runs (the
// Mushrooms test's victory shape).
void win_combat(RunController& rc) {
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    for (uint8_t i = 0; i < rc.combat.monster_count; ++i) {
        rc.combat.monsters[i].hp = 0;
    }
    rc.combat.phase = static_cast<uint8_t>(CombatPhase::COMBAT_OVER);
    choose(rc, 0);
}

// =============================================================================
// The Library
// =============================================================================

TEST(TheLibrary, SleepHealsTheFrozenRoundedThirdAndA15HealsAFifth) {
    RunController rc = event_controller(EventId::THE_LIBRARY);
    rc.run.max_hp = 80;
    rc.run.hp = 10;
    const EventDialogImpl* impl = event_dialog_impl(rc.event.event_id);
    impl->on_enter(rc, rc.event);  // re-freeze against the edited sheet
    // MathUtils.round(80 * 0.33f) == 26 (TheLibrary.java:39).
    EXPECT_EQ(rc.event.scratch0, 26);
    choose(rc, 1);
    EXPECT_EQ(rc.run.hp, 36);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    RunController a15 = event_controller(EventId::THE_LIBRARY, 15);
    a15.run.max_hp = 80;
    a15.run.hp = 10;
    event_dialog_impl(a15.event.event_id)->on_enter(a15, a15.event);
    EXPECT_EQ(a15.event.scratch0, 16);  // round(80 * 0.2f)
    choose(a15, 1);
    EXPECT_EQ(a15.run.hp, 26);
}

TEST(TheLibrary, ReadDealsTwentyUniqueCardsAndThePickIsObtained) {
    RunController rc = event_controller(EventId::THE_LIBRARY);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    const int16_t blizz_before = rc.run.card_blizz_randomizer;
    const int32_t card_counter_before = rc.run.card_rng.counter;

    choose(rc, 0);  // READ
    ASSERT_EQ(rc.event.screen, 1);
    // Twenty dealt cards, unique by id (the re-roll loop's contract).
    for (int i = 0; i < 20; ++i) {
        ASSERT_NE(rc.event.board[i].card_id, 0u) << i;
        for (int j = 0; j < i; ++j) {
            EXPECT_NE(rc.event.board[i].card_id, rc.event.board[j].card_id)
                << i << " vs " << j;
        }
    }
    // Every attempt costs exactly TWO cardRng draws (rarity + pool index), so
    // the total is 40 plus 2 per duplicate re-roll -- never fewer.
    EXPECT_GE(rc.run.card_rng.counter, card_counter_before + 40);
    EXPECT_EQ((rc.run.card_rng.counter - card_counter_before) % 2, 0);
    // rollRarity NEVER mutates the blizzard pity -- that write lives only in
    // getRewardCards (AbstractDungeon.java:1435-1451).
    EXPECT_EQ(rc.run.card_blizz_randomizer, blizz_before);

    // The board is the option surface: twenty enabled options.
    const EventDialogMenu board = menu(rc);
    ASSERT_EQ(board.count, 20);
    for (int i = 0; i < 20; ++i) {
        EXPECT_TRUE(board.enabled[i]);
    }

    const uint16_t picked = rc.event.board[7].card_id;
    choose(rc, 7);
    EXPECT_EQ(rc.run.master_deck_count, 2);
    EXPECT_EQ(rc.run.master_deck[1].card_id, picked);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(TheLibrary, NlothsGiftTriplesTheRareThresholdInTheRarityPass) {
    RunState rs{};
    // roll 8: below neither base threshold's rare band (3) nor... with the
    // gift the EventRoom rare band is 3 * 3 == 9, so 8 flips RARE.
    EXPECT_EQ(reward_card_rarity_with_relics(rs, 8, RoomType::Event),
              RewardCardRarity::UNCOMMON);
    give_relic(rs, RelicId::NLOTHS_GIFT);
    EXPECT_EQ(reward_card_rarity_with_relics(rs, 8, RoomType::Event),
              RewardCardRarity::RARE);
    // Elite: 10 -> 30 (AbstractRoom alternation over the elite thresholds).
    EXPECT_EQ(reward_card_rarity_with_relics(rs, 29, RoomType::Elite),
              RewardCardRarity::RARE);
    // Boss rooms never see the pass (MonsterRoomBoss.java:40-42).
    EXPECT_EQ(reward_card_rarity_with_relics(rs, 99, RoomType::Boss),
              RewardCardRarity::RARE);
    // The uncommon band keeps its base width above the tripled rare band.
    EXPECT_EQ(reward_card_rarity_with_relics(rs, 45, RoomType::Event),
              RewardCardRarity::UNCOMMON);
    EXPECT_EQ(reward_card_rarity_with_relics(rs, 46, RoomType::Event),
              RewardCardRarity::COMMON);
}

// =============================================================================
// The Mausoleum
// =============================================================================

TEST(TheMausoleum, OpenAlwaysDrawsTheBooleanAndAlwaysPaysARelic) {
    RunController rc = event_controller(EventId::THE_MAUSOLEUM);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    RngStream probe = rc.combat.misc_rng;
    const bool cursed = random_boolean(probe);
    const uint8_t relics_before = rc.run.relic_count;

    choose(rc, 0);
    EXPECT_EQ(rc.combat.misc_rng.counter, probe.counter);
    EXPECT_EQ(rc.run.relic_count, relics_before + 1);
    EXPECT_EQ(deck_count_of(rc.run, CardId::WRITHE), cursed ? 1 : 0);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(TheMausoleum, A15ForcesTheWritheButStillSpendsTheDraw) {
    RunController rc = event_controller(EventId::THE_MAUSOLEUM, 15);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    const int32_t misc_before = rc.combat.misc_rng.counter;
    choose(rc, 0);
    // The randomBoolean is drawn FIRST and OVERWRITTEN at A15
    // (TheMausoleum.java:61-64): one draw either way, Writhe always.
    EXPECT_EQ(rc.combat.misc_rng.counter, misc_before + 1);
    EXPECT_EQ(deck_count_of(rc.run, CardId::WRITHE), 1);
}

TEST(TheMausoleum, AnArmedOmamoriEatsTheWritheButNotTheRelic) {
    RunController rc = event_controller(EventId::THE_MAUSOLEUM, 15);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    give_relic(rc.run, RelicId::OMAMORI, 2);
    const uint8_t relics_before = rc.run.relic_count;
    choose(rc, 0);
    EXPECT_EQ(deck_count_of(rc.run, CardId::WRITHE), 0);
    EXPECT_EQ(rc.run.relic_count, relics_before + 1);
    // One charge spent, at ShowCardAndObtainEffect construction time.
    for (uint8_t i = 0; i < rc.run.relic_count; ++i) {
        if (rc.run.relics[i].relic_id ==
            static_cast<uint16_t>(RelicId::OMAMORI)) {
            EXPECT_EQ(rc.run.relics[i].counter, 1);
        }
    }
}

TEST(TheMausoleum, LeaveTakesNothing) {
    RunController rc = event_controller(EventId::THE_MAUSOLEUM);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    const uint8_t relics_before = rc.run.relic_count;
    const int32_t misc_before = rc.combat.misc_rng.counter;
    choose(rc, 1);
    EXPECT_EQ(rc.run.relic_count, relics_before);
    EXPECT_EQ(rc.run.master_deck_count, 1);
    EXPECT_EQ(rc.combat.misc_rng.counter, misc_before);  // no draw on leave
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

// =============================================================================
// Vampires
// =============================================================================

TEST(Vampires, AcceptCostsCeilThirtyPercentMaxHpAndSwapsStrikesForFiveBites) {
    RunController rc = event_controller(EventId::VAMPIRES);
    rc.run.max_hp = 80;
    rc.run.hp = 80;
    event_dialog_impl(rc.event.event_id)->on_enter(rc, rc.event);
    EXPECT_EQ(rc.event.scratch0, 24);  // MathUtils.ceil(80 * 0.3f)
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::STRIKE, 1},
                      {CardId::POMMEL_STRIKE, 0}, {CardId::TWIN_STRIKE, 0},
                      {CardId::BASH, 0}});
    // No vial: two options only (accept / refuse).
    EXPECT_EQ(menu(rc).count, 2);

    choose(rc, 0);
    EXPECT_EQ(rc.run.max_hp, 56);
    EXPECT_EQ(rc.run.hp, 56);
    // STARTER_STRIKE == Strike_R at ANY upgrade; Pommel/Twin Strike carry only
    // the STRIKE tag and stay (Vampires.java:104-108).
    EXPECT_EQ(deck_count_of(rc.run, CardId::STRIKE), 0);
    EXPECT_EQ(deck_count_of(rc.run, CardId::POMMEL_STRIKE), 1);
    EXPECT_EQ(deck_count_of(rc.run, CardId::TWIN_STRIKE), 1);
    EXPECT_EQ(deck_count_of(rc.run, CardId::BITE), 5);
    EXPECT_EQ(rc.run.master_deck_count, 8);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(Vampires, CtorClampsTheCostToMaxHpMinusOne) {
    RunController rc = event_controller(EventId::VAMPIRES);
    rc.run.max_hp = 3;  // ceil(0.9f) == 1 < 3; use 2: ceil(0.6) == 1... use 1
    rc.run.hp = 3;
    event_dialog_impl(rc.event.event_id)->on_enter(rc, rc.event);
    EXPECT_EQ(rc.event.scratch0, 1);  // ceil(0.9f) == 1, no clamp needed
    rc.run.max_hp = 1;
    rc.run.hp = 1;
    event_dialog_impl(rc.event.event_id)->on_enter(rc, rc.event);
    // ceil(0.3f) == 1 >= maxHealth -> maxHealth - 1 == 0 (Vampires.java:42-44).
    EXPECT_EQ(rc.event.scratch0, 0);
}

TEST(Vampires, BloodVialOptionTradesTheVialInsteadOfMaxHp) {
    RunController rc = event_controller(EventId::VAMPIRES);
    rc.run.max_hp = 80;
    rc.run.hp = 60;
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::BASH, 0}});
    give_relic(rc.run, RelicId::BLOOD_VIAL);
    event_dialog_impl(rc.event.event_id)->on_enter(rc, rc.event);
    ASSERT_EQ(menu(rc).count, 3);

    choose(rc, 1);
    EXPECT_FALSE(owns(rc.run, RelicId::BLOOD_VIAL));
    EXPECT_EQ(rc.run.max_hp, 80);
    EXPECT_EQ(rc.run.hp, 60);
    EXPECT_EQ(deck_count_of(rc.run, CardId::STRIKE), 0);
    EXPECT_EQ(deck_count_of(rc.run, CardId::BITE), 5);
}

TEST(Vampires, RefuseKeepsEverythingAndOptionOneRefusesWithoutTheVial) {
    RunController rc = event_controller(EventId::VAMPIRES);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    // Without the vial, option 1 IS the refuse fall-through
    // (Vampires.java:71-72 `if (!this.hasVial) break;`).
    choose(rc, 1);
    EXPECT_EQ(deck_count_of(rc.run, CardId::STRIKE), 1);
    EXPECT_EQ(deck_count_of(rc.run, CardId::BITE), 0);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

// =============================================================================
// Masked Bandits
// =============================================================================

TEST(MaskedBandits, PayLosesAllGoldAndWalksThreeContinuePages) {
    RunController rc = event_controller(EventId::MASKED_BANDITS);
    rc.run.gold = 234;
    choose(rc, 0);
    EXPECT_EQ(rc.run.gold, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::EVENT_DIALOG));
    choose(rc, 0);  // PAID_1 -> PAID_2
    choose(rc, 0);  // PAID_2 -> PAID_3
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::EVENT_DIALOG));
    choose(rc, 0);  // PAID_3 press opens the map (MaskedBandits.java:99-104)
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(MaskedBandits, FightStocksGoldAndRedMaskAndEntersTheTrio) {
    RunController rc = event_controller(EventId::MASKED_BANDITS);
    set_deck(rc.run, {{CardId::STRIKE, 0}, {CardId::DEFEND, 0},
                      {CardId::BASH, 0}});
    rc.combat.misc_rng = from_seed(424242);
    RngStream expected_rng = rc.combat.misc_rng;
    const int expected_gold = random(expected_rng, 25, 35);

    choose(rc, 1);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.monster_count, 3);
    EXPECT_EQ(rc.combat.monsters[0].monster_id,
              static_cast<uint16_t>(MonsterId::BANDIT_POINTY));
    EXPECT_EQ(rc.combat.monsters[1].monster_id,
              static_cast<uint16_t>(MonsterId::BANDIT_LEADER));
    EXPECT_EQ(rc.combat.monsters[2].monster_id,
              static_cast<uint16_t>(MonsterId::BANDIT_BEAR));
    // An event fight, not an elite one: no room elite flag.
    EXPECT_EQ(rc.combat.flags & kCombatFlagEliteRoom, 0u);
    ASSERT_EQ(rc.rewards.count, 2);
    EXPECT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::GOLD));
    EXPECT_EQ(rc.rewards.items[0].gold, expected_gold);
    EXPECT_EQ(rc.rewards.items[1].kind,
              static_cast<uint8_t>(RewardItemKind::RELIC));
    EXPECT_EQ(rc.rewards.items[1].id,
              static_cast<uint16_t>(RelicId::RED_MASK));

    // rewardAllowed stays TRUE for this embed: the win screen appends the
    // potion roll + card reward behind the pre-stocked pair.
    win_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    ASSERT_GE(rc.rewards.count, 3);
    EXPECT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::GOLD));
    EXPECT_EQ(rc.rewards.items[1].kind,
              static_cast<uint8_t>(RewardItemKind::RELIC));
    EXPECT_EQ(rc.rewards.items[rc.rewards.count - 1].kind,
              static_cast<uint8_t>(RewardItemKind::CARDS));
}

TEST(MaskedBandits, AHeldRedMaskDowngradesTheRewardToACirclet) {
    RunController rc = event_controller(EventId::MASKED_BANDITS);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    give_relic(rc.run, RelicId::RED_MASK);
    choose(rc, 1);
    ASSERT_EQ(rc.rewards.count, 2);
    EXPECT_EQ(rc.rewards.items[1].id,
              static_cast<uint16_t>(RelicId::CIRCLET));
}

TEST(MaskedBandits, RedMaskWeakensEveryBanditAtBattleStartWithoutTheLatch) {
    RunController rc = event_controller(EventId::MASKED_BANDITS);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    give_relic(rc.run, RelicId::RED_MASK);
    choose(rc, 1);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    for (uint8_t m = 0; m < 3; ++m) {
        bool weak_found = false;
        for (uint8_t p = 0; p < rc.combat.monsters[m].power_count; ++p) {
            const PowerSlot& slot = rc.combat.monsters[m].powers[p];
            if (slot.power_id == static_cast<uint16_t>(PowerId::WEAK)) {
                weak_found = true;
                EXPECT_EQ(slot.amount, 1) << "monster " << int(m);
            }
        }
        EXPECT_TRUE(weak_found) << "monster " << int(m);
    }
}

// =============================================================================
// The Bandit trio -- monster behaviour + the deathReact verified negative
// =============================================================================

RunController bandit_fight() {
    RunController rc = event_controller(EventId::MASKED_BANDITS);
    RunState& rs = rc.run;
    rs.master_deck_count = 0;
    EXPECT_TRUE(add_card_to_master_deck(rs, CardId::STRIKE));
    EXPECT_TRUE(add_card_to_master_deck(rs, CardId::DEFEND));
    choose(rc, 1);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    return rc;
}

// Drain the queue after a forced monster turn, exactly as the module tests do.
void run_monster_turn(CombatState& s, uint8_t mi) {
    switch (static_cast<MonsterId>(s.monsters[mi].monster_id)) {
        case MonsterId::BANDIT_POINTY:
            bandit_pointy_take_turn(s, mi);
            break;
        case MonsterId::BANDIT_LEADER:
            bandit_leader_take_turn(s, mi);
            break;
        case MonsterId::BANDIT_BEAR:
            bandit_bear_take_turn(s, mi);
            break;
        default:
            FAIL() << "not a bandit";
    }
    pump(s, dispatch_monster_turn);
}

TEST(BanditTrio, OpenersAndHpRollsMatchTheCtors) {
    RunController rc = bandit_fight();
    const CombatState& s = rc.combat;
    // A20 (kMonsterAscension): Pointy setHp(34) degenerate, Leader 37-41,
    // Bear 40-44.
    EXPECT_EQ(s.monsters[0].hp, 34);
    EXPECT_GE(s.monsters[1].hp, 37);
    EXPECT_LE(s.monsters[1].hp, 41);
    EXPECT_GE(s.monsters[2].hp, 40);
    EXPECT_LE(s.monsters[2].hp, 44);
    // Openers: Pointy telegraphs its 2-hit attack, the Leader MOCK (UNKNOWN),
    // the Bear BEAR_HUG (STRONG_DEBUFF).
    EXPECT_EQ(s.monsters[0].intent,
              static_cast<uint8_t>(MonsterIntent::ATTACK));
    EXPECT_EQ(s.monsters[1].intent,
              static_cast<uint8_t>(MonsterIntent::UNKNOWN));
    EXPECT_EQ(s.monsters[2].intent,
              static_cast<uint8_t>(MonsterIntent::STRONG_DEBUFF));
}

TEST(BanditTrio, PointyHitsTwiceForSixAtAscensionTwoPlus) {
    RunController rc = bandit_fight();
    CombatState& s = rc.combat;
    s.player_block = 0;
    const int16_t hp_before = s.player_hp;
    run_monster_turn(s, 0);
    // Two 6-damage hits (A2 column), and the re-telegraph keeps move 1.
    EXPECT_EQ(s.player_hp, hp_before - 12);
    EXPECT_EQ(s.monsters[0].move_history[0],
              sts::registry::kBanditPointyMovePointySpecial);
}

TEST(BanditTrio, LeaderMocksThenAgonizesThenChainSlashesAtA17) {
    RunController rc = bandit_fight();
    CombatState& s = rc.combat;
    const uint8_t li = 1;
    // MOCK: no damage, no player debuff, chains into AGONIZING_SLASH.
    const int16_t hp0 = s.player_hp;
    run_monster_turn(s, li);
    EXPECT_EQ(s.player_hp, hp0);
    EXPECT_EQ(s.monsters[li].move_history[0],
              sts::registry::kBanditLeaderMoveAgonizingSlash);
    EXPECT_EQ(s.monsters[li].intent,
              static_cast<uint8_t>(MonsterIntent::ATTACK_DEBUFF));
    // AGONIZING_SLASH: 12 (A2) + Weak 3 (A17), then CROSS_SLASH.
    s.player_block = 0;
    const int16_t hp1 = s.player_hp;
    run_monster_turn(s, li);
    EXPECT_EQ(s.player_hp, hp1 - 12);
    bool weak = false;
    for (uint8_t p = 0; p < s.player_power_count; ++p) {
        if (s.player_powers[p].power_id ==
            static_cast<uint16_t>(PowerId::WEAK)) {
            weak = true;
            EXPECT_EQ(s.player_powers[p].amount, 3);
        }
    }
    EXPECT_TRUE(weak);
    EXPECT_EQ(s.monsters[li].move_history[0],
              sts::registry::kBanditLeaderMoveCrossSlash);
    // First CROSS_SLASH at A17+: lastTwoMoves(1) is false (history 1,3), so
    // the leader KEEPS slashing (BanditLeader.java:118-121)...
    s.player_block = 120;  // absorb, we only watch the telegraph
    run_monster_turn(s, li);
    EXPECT_EQ(s.monsters[li].move_history[0],
              sts::registry::kBanditLeaderMoveCrossSlash);
    // ...and after the SECOND slash in a row it telegraphs Agonizing again.
    run_monster_turn(s, li);
    EXPECT_EQ(s.monsters[li].move_history[0],
              sts::registry::kBanditLeaderMoveAgonizingSlash);
}

TEST(BanditTrio, BearHugsMinusFourDexThenAlternatesLungeAndMaul) {
    RunController rc = bandit_fight();
    CombatState& s = rc.combat;
    const uint8_t bi = 2;
    run_monster_turn(s, bi);
    // BEAR_HUG at A17+: Dexterity -4 on the player (the negative_stat_flip
    // Artifact predicate is op_apply_power's, pinned there).
    bool dex = false;
    for (uint8_t p = 0; p < s.player_power_count; ++p) {
        if (s.player_powers[p].power_id ==
            static_cast<uint16_t>(PowerId::DEXTERITY)) {
            dex = true;
            EXPECT_EQ(s.player_powers[p].amount, -4);
        }
    }
    EXPECT_TRUE(dex);
    EXPECT_EQ(s.monsters[bi].move_history[0],
              sts::registry::kBanditBearMoveLunge);
    // LUNGE: 10 (A2) + 9 self block, then MAUL.
    s.player_block = 0;
    const int16_t hp = s.player_hp;
    run_monster_turn(s, bi);
    EXPECT_EQ(s.player_hp, hp - 10);
    EXPECT_EQ(s.monsters[bi].block, 9);
    EXPECT_EQ(s.monsters[bi].move_history[0],
              sts::registry::kBanditBearMoveMaul);
    // MAUL: 20 (A2), then LUNGE again.
    s.player_block = 0;
    const int16_t hp2 = s.player_hp;
    run_monster_turn(s, bi);
    EXPECT_EQ(s.player_hp, hp2 - 20);
    EXPECT_EQ(s.monsters[bi].move_history[0],
              sts::registry::kBanditBearMoveLunge);
}

TEST(CityEventsII, BearDeathReactIsPresentationOnly) {
    // The re-pointed deathReact obligation's pin. BanditBear.die()
    // (BanditBear.java:127-133) fans deathReact() over the survivors, and the
    // two overrides it reaches queue ONE TalkAction each -- presentation. So:
    // no die fn is registered for the Bear on either edge, and killing it
    // leaves the other bandits' combat state byte-identical (no move change,
    // no intent change, no queued item, no stream draw) -- which is also why
    // gremlin move 99 stays unreachable in every act (escapeNext() still has
    // no caller anywhere in the tree).
    EXPECT_EQ(monster_die_fn(MonsterId::BANDIT_BEAR), nullptr);
    EXPECT_EQ(monster_die_after_fn(MonsterId::BANDIT_BEAR), nullptr);
    EXPECT_EQ(monster_die_fn(MonsterId::BANDIT_POINTY), nullptr);
    EXPECT_EQ(monster_die_fn(MonsterId::BANDIT_LEADER), nullptr);

    RunController rc = bandit_fight();
    CombatState& s = rc.combat;
    const MonsterState pointy_before = s.monsters[0];
    const MonsterState leader_before = s.monsters[1];
    const RngStream ai_before = s.ai_rng;

    // Kill the Bear through the real death edge.
    ActionQueueItem hit{};
    hit.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    hit.src = kActorPlayer;
    hit.tgt = 2;
    hit.amount = 999;
    add_to_bottom(s, hit);
    pump(s, dispatch_monster_turn);
    EXPECT_LE(s.monsters[2].hp, 0);

    EXPECT_EQ(std::memcmp(&pointy_before, &s.monsters[0],
                          sizeof(MonsterState)), 0);
    EXPECT_EQ(std::memcmp(&leader_before, &s.monsters[1],
                          sizeof(MonsterState)), 0);
    EXPECT_EQ(s.ai_rng.counter, ai_before.counter);
}

// =============================================================================
// The Colosseum -- the two-fight sequence
// =============================================================================

RunController colosseum_at_post_combat(RunController* out_pre = nullptr) {
    RunController rc = event_controller(EventId::COLOSSEUM);
    RunState& rs = rc.run;
    rs.master_deck_count = 0;
    EXPECT_TRUE(add_card_to_master_deck(rs, CardId::STRIKE));
    EXPECT_TRUE(add_card_to_master_deck(rs, CardId::DEFEND));
    EXPECT_TRUE(add_card_to_master_deck(rs, CardId::BASH));
    choose(rc, 0);  // INTRO -> FIGHT page
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::EVENT_DIALOG));
    if (out_pre != nullptr) {
        *out_pre = rc;
    }
    choose(rc, 0);  // FIGHT -> the Slavers
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.combat.monster_count, 2);
    EXPECT_EQ(rc.rewards.count, 0);  // rewards.clear() + rewardAllowed false
    // The dialog SURVIVES the fight -- that is the reopen seam's key.
    EXPECT_EQ(rc.event.event_id, static_cast<uint16_t>(EventId::COLOSSEUM));
    // Kill the Slavers; the survivor path must NOT open a reward screen.
    for (uint8_t i = 0; i < rc.combat.monster_count; ++i) {
        rc.combat.monsters[i].hp = 0;
    }
    rc.combat.phase = static_cast<uint8_t>(CombatPhase::COMBAT_OVER);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::EVENT_DIALOG));
    return rc;
}

TEST(Colosseum, SlaversVictoryReopensTheDialogAndConsumesTheUnopenedDraws) {
    RunController pre{};
    RunController rc = colosseum_at_post_combat(&pre);

    // Back on the POST_COMBAT page with two options (COWARDICE / VICTORY).
    const EventDialogMenu m = menu(rc);
    ASSERT_EQ(m.count, 2);
    EXPECT_TRUE(m.enabled[0]);
    EXPECT_TRUE(m.enabled[1]);
    EXPECT_EQ(rc.rewards.count, 0);  // reopen's rewards.clear()

    // The battle-over potion roll ran even though no screen opened: the
    // blizzard ratchet moved off pre's value by exactly one step.
    const int diff = rc.run.blizzard_potion_mod - pre.run.blizzard_potion_mod;
    EXPECT_TRUE(diff == 10 || diff == -10) << diff;
    // And reopen's preBattlePrep consumed shuffleRng: at least the
    // initializeDeck randomLong beyond the fight's own opening shuffle.
    EXPECT_GT(rc.combat.shuffle_rng.counter, pre.combat.shuffle_rng.counter);
}

TEST(Colosseum, CowardiceLeavesToTheMapWithNothing) {
    RunController rc = colosseum_at_post_combat();
    const int32_t gold = rc.run.gold;
    const uint8_t relics = rc.run.relic_count;
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.run.gold, gold);
    EXPECT_EQ(rc.run.relic_count, relics);
}

TEST(Colosseum, VictoryStocksRareUncommonAndHundredGoldAndFightsTheNobs) {
    RunController rc = colosseum_at_post_combat();
    choose(rc, 1);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.monster_count, 2);
    EXPECT_EQ(rc.combat.monsters[0].monster_id,
              static_cast<uint16_t>(MonsterId::TASKMASTER));
    EXPECT_EQ(rc.combat.monsters[1].monster_id,
              static_cast<uint16_t>(MonsterId::GREMLIN_NOB));
    // eliteTrigger = true (Colosseum.java:75) -> the room elite flag.
    EXPECT_NE(rc.combat.flags & kCombatFlagEliteRoom, 0u);
    // The fight-2 dialog is gone: reopen() is a no-op at LEAVE.
    EXPECT_EQ(rc.event.event_id, 0u);
    ASSERT_EQ(rc.rewards.count, 3);
    EXPECT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::RELIC));
    EXPECT_EQ(rc.rewards.items[1].kind,
              static_cast<uint8_t>(RewardItemKind::RELIC));
    EXPECT_EQ(rc.rewards.items[2].kind,
              static_cast<uint8_t>(RewardItemKind::GOLD));
    EXPECT_EQ(rc.rewards.items[2].gold, 100);

    // The Nobs win opens the ordinary event-combat reward screen ON TOP of
    // the pre-stock: potion roll + card reward append behind the three items.
    win_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    ASSERT_GE(rc.rewards.count, 4);
    EXPECT_EQ(rc.rewards.items[rc.rewards.count - 1].kind,
              static_cast<uint8_t>(RewardItemKind::CARDS));
}

TEST(Colosseum, ASmokeBombOutOfTheSlaversAlsoReturnsToTheDialog) {
    RunController rc = event_controller(EventId::COLOSSEUM);
    set_deck(rc.run, {{CardId::STRIKE, 0}});
    choose(rc, 0);
    choose(rc, 0);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    // The pump-ended smoked shape: player escaped, combat over, monsters live.
    rc.combat.flags |= kCombatFlagPlayerEscaped;
    rc.combat.phase = static_cast<uint8_t>(CombatPhase::COMBAT_OVER);
    choose(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::EVENT_DIALOG));
    EXPECT_EQ(rc.event.screen, 2);  // POST_COMBAT -- the game's reopen page
    EXPECT_EQ(rc.combat_outcome,
              static_cast<uint8_t>(RunCombatOutcome::SMOKE_BOMB));
}

}  // namespace
}  // namespace sts::engine
