// B4.4 -- run-level advance + room lifecycle.
//
// Coverage (honestly bounded by what exists -- see run_advance.hpp's scope note):
//   * run_begin's Neow-pending stream state: monsterRng (encounter lists),
//     relicRng (5 pool-shuffle draws -> counter 5), mapRng (end-of-generateMap),
//     verified by INDEPENDENT recomputation (not magic numbers).
//   * the trap-7 floor reseed: next_room_transition reseeds the 5 floor streams to
//     floor_stream(seed, floor) AFTER the ++floor -- a NAMED test that also proves
//     the ordering (reseed uses the post-increment floor).
//   * run-combat entry byte-equivalence with combat_begin (the fixture-verified
//     combat path) for a single Jaw Worm floor, plus integrated louse pre-battle
//     and B3.9 Innate-card behavior.
//   * a full floor cycle for one seed: Neow -> map pick -> Jaw Worm combat ->
//     victory + fold-back -> reward -> proceed -> next floor, stream counters and
//     cursors checked at each boundary.
//   * map-choice legality against the generated edges; non-combat rooms routed to
//     an explicit ROOM_UNIMPLEMENTED stall; heterogeneous batch advance.
//   * ?-room resolution (B4.10): the regression guard that eventRng advances by
//     EXACTLY one draw across a full ?-resolves-to-event flow (selection on a
//     discarded throwaway stream, pool removal committed), each resolved kind's
//     routing (monster combat / chest / shop park), and the fixed
//     monster-cursor consumption for a ? that rolled MONSTER (the resolved
//     room, not the static map node, decides the remove(0)).

#include "sts/engine/run_advance.hpp"

#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <vector>

#include <gtest/gtest.h>

#include "sts/engine/advance.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"       // Opcode::DRAW (Centennial Puzzle)
#include "sts/engine/map_gen.hpp"
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/monster_looter.hpp"  // kLooterGoldAmt / looter_steal_count
#include "sts/engine/potions.hpp"
#include "sts/engine/power_hooks.hpp"  // dispatch_was_hp_lost (Centennial Puzzle)
#include "sts/engine/relic_hooks.hpp"  // RelicHook (the pre-draw emptiness pin)
#include "sts/engine/relics.hpp"       // RelicDef / kRelicDefs
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_deck.hpp"     // the master-deck bottle bits

namespace sts::engine {
namespace {

constexpr int64_t kSeed = 12345;
constexpr uint8_t kA20 = 20;

// --- helpers -----------------------------------------------------------------

bool streams_equal(const RngStream& a, const RngStream& b) noexcept {
    return a.s0 == b.s0 && a.s1 == b.s1 && a.counter == b.counter;
}

// Find a seed whose first weak encounter (monster_list[0], used by the floor-1
// monster room) resolves to the single-Jaw-Worm encounter. This seed is retained
// for the byte-equivalence test even though Cultist and louses are also live.
int64_t find_jaw_worm_seed() {
    for (int64_t s = 1; s < 4000; ++s) {
        RngStream m = from_seed(s);
        MonsterLists ml{};
        generate_monster_lists(1, m, ml);
        if (ml.monster_list_count > 0 && ml.monster_list[0] == "Jaw Worm") {
            return s;
        }
    }
    ADD_FAILURE() << "no Jaw-Worm-first seed found in range";
    return 1;
}

int64_t find_two_louse_seed() {
    for (int64_t s = 1; s < 4000; ++s) {
        RngStream m = from_seed(s);
        MonsterLists ml{};
        generate_monster_lists(1, m, ml);
        if (ml.monster_list_count > 0 && ml.monster_list[0] == "2 Louse") {
            return s;
        }
    }
    ADD_FAILURE() << "no-two-louse-first seed found in range";
    return 1;
}

int16_t monster_power(const CombatState& s, uint8_t mi, PowerId id) {
    const MonsterState& m = s.monsters[mi];
    for (uint8_t i = 0; i < m.power_count; ++i) {
        if (m.powers[i].power_id == static_cast<uint16_t>(id)) {
            return m.powers[i].amount;
        }
    }
    return -1;
}

bool hand_contains(const CombatState& s, CardId id) {
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        if (s.card_pool[s.hand[i]].card_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

// A legal starting column (any connected row-0 node).
uint8_t first_start_column(const RunController& rc) {
    RunActionMask m{};
    legal_actions(rc, m);
    for (uint8_t x = 0; x < kMapCols; ++x) {
        if (m.can_choose_node[x]) return x;
    }
    ADD_FAILURE() << "no legal start column";
    return 0;
}

// Drive a combat to a terminal (win/lose): repeatedly play the first legal
// enemy-target card, else end the turn. Caps iterations to fail loudly on a
// stuck loop rather than hang.
void play_out_combat(RunController& rc) {
    StepResult res{};
    for (int step = 0; step < 800; ++step) {
        if (rc.phase != static_cast<uint8_t>(RunPhase::COMBAT)) return;
        RunActionMask m{};
        legal_actions(rc, m);
        Action a{};
        bool played = false;
        for (int i = 0; i < kHandCap && !played; ++i) {
            for (int t = 0; t < kMonsterCap; ++t) {
                if (m.combat.can_play_target[i][t]) {
                    a = make_action(ActionVerb::PLAY_CARD, static_cast<uint8_t>(i),
                                    static_cast<uint8_t>(t));
                    played = true;
                    break;
                }
            }
        }
        if (!played) a = make_action(ActionVerb::END_TURN);
        advance(std::span<RunController>(&rc, 1),
                std::span<const Action>(&a, 1),
                std::span<StepResult>(&res, 1));
    }
    ADD_FAILURE() << "combat did not terminate within the step cap";
}

const Action kProceed = make_action(ActionVerb::CHOOSE);

void step(RunController& rc, Action a) {
    StepResult res{};
    advance(std::span<RunController>(&rc, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&res, 1));
}

// Leave Neow with floor 0 otherwise untouched. There is no such button in the
// game -- every run takes one of the four blessings -- but these tests are
// about the floor loop, and a blessing payout moves streams, the master deck
// and the relic pools underneath them. Forcing the finished-payout screen and
// pressing the map button exercises exactly the transition the last Neow press
// makes; the blessing itself is neow_test's subject.
void leave_neow(RunController& rc) {
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::DONE);
    step(rc, kProceed);
}

StepResult step_with_result(RunController& rc, Action a) {
    StepResult res{};
    advance(std::span<RunController>(&rc, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&res, 1));
    return res;
}

PotionId hand_limited_potion_roll(RngStream& rng) {
    const PotionRarity tier = potion_tier_for_roll(random(rng, 0, 99));
    auto draw = [&rng]() {
        return static_cast<PotionId>(random(rng, kPotionPoolSize - 1) + 1);
    };
    PotionId candidate = draw();
    bool spam_check = true;
    while (spam_check || potion_def(candidate)->rarity != tier) {
        spam_check = true;
        candidate = draw();
        if (candidate == PotionId::FRUIT_JUICE) {
            continue;
        }
        spam_check = false;
    }
    return candidate;
}

RunController enter_jaw_worm_combat() {
    RunController rc = run_begin(find_jaw_worm_seed(), kA20);
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    return rc;
}

// =============================================================================
// run_begin: Neow-pending stream state
// =============================================================================

TEST(RunBegin, MonsterRngMatchesIndependentListGeneration) {
    RunController rc = run_begin(kSeed, kA20);
    // monsterRng after run_begin == a fresh Random(seed) run through exactly the
    // encounter-list generation (Exordium generateMonsters + initializeBoss).
    RngStream expect = from_seed(kSeed);
    MonsterLists ml{};
    generate_monster_lists(1, expect, ml);
    EXPECT_TRUE(streams_equal(rc.run.monster_rng, expect));
    EXPECT_GT(rc.run.monster_rng.counter, 0);
    // The generated lists are stored on the controller for room consumption.
    EXPECT_EQ(rc.lists.monster_list_count, ml.monster_list_count);
    EXPECT_EQ(rc.lists.boss_list_count, ml.boss_list_count);
}

TEST(RunBegin, NeowHasGenerateSeedsFloorStreamsAtFloorZero) {
    const RunController rc = run_begin(kSeed, kA20);
    const RngStream floor0 = floor_stream(kSeed, 0);
    EXPECT_TRUE(streams_equal(rc.combat.monster_hp_rng, floor0));
    EXPECT_TRUE(streams_equal(rc.combat.ai_rng, floor0));
    EXPECT_TRUE(streams_equal(rc.combat.shuffle_rng, floor0));
    EXPECT_TRUE(streams_equal(rc.combat.card_random_rng, floor0));
    // miscRng alone starts ONE draw in: Exordium's constructor ends with
    // changeBGM, whose MainMusic.getSong Exordium arm draws miscRng.random(1)
    // to pick the act's track (Exordium.java:58; MainMusic.java:56-66). Found
    // empirically via STS00052's Astrolabe transforms -- every identity one
    // draw behind the capture with all counters equal -- and floor-0-only,
    // because nextRoomTransition's per-floor reseed discards the offset.
    RngStream misc = floor0;
    (void)random(misc, 1);
    EXPECT_TRUE(streams_equal(rc.combat.misc_rng, misc));
}

TEST(RunBegin, RelicRngConsumesFivePoolShuffleDraws) {
    RunController rc = run_begin(kSeed, kA20);
    RngStream expect = from_seed(kSeed);
    for (int i = 0; i < kRelicTierCount; ++i) (void)random_long(expect);
    EXPECT_EQ(rc.run.relic_rng.counter, kRelicTierCount);  // exactly 5
    EXPECT_TRUE(streams_equal(rc.run.relic_rng, expect));
    // All five Ironclad-obtainable pools are now populated. The stream state
    // asserted above is UNCHANGED by that -- every one of the five draws is
    // unconditional, so an empty tier consumed its shuffle seed exactly as a
    // full one does. This assertion is the reason the last tier could land
    // without moving relicRng anywhere.
    EXPECT_EQ(rc.run.relic_pool_count[0], 33);  // COMMON
    EXPECT_EQ(rc.run.relic_pool_count[1], 30);  // UNCOMMON
    EXPECT_EQ(rc.run.relic_pool_count[2], 28);  // RARE
    EXPECT_EQ(rc.run.relic_pool_count[3], 17);  // SHOP
    EXPECT_EQ(rc.run.relic_pool_count[4], 22);  // BOSS
}

TEST(RunBegin, MapRngAtEndOfGenerateMapAndMapPopulated) {
    RunController rc = run_begin(kSeed, kA20);
    GeneratedMap g = generate_map(kSeed, 1);
    RoomAssignment ra = assign_room_types(g, kA20);
    EXPECT_TRUE(streams_equal(rc.run.map_rng, ra.rng));
    // Fixed rows (AbstractDungeon.java:525-531).
    for (int x = 0; x < kMapCols; ++x) {
        EXPECT_EQ(rc.run.map[run_state_map_index(x, 0)].room_type,
                  static_cast<uint8_t>(RoomType::Monster));
        EXPECT_EQ(rc.run.map[run_state_map_index(x, 8)].room_type,
                  static_cast<uint8_t>(RoomType::Treasure));
        EXPECT_EQ(rc.run.map[run_state_map_index(x, 14)].room_type,
                  static_cast<uint8_t>(RoomType::Rest));
    }
}

TEST(RunBegin, BaseSheetAndStartingRelicAndDeck) {
    RunController rc = run_begin(kSeed, kA20);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::NEOW));
    // The run-setup ascension modifiers are live (a20_modifiers_test owns their
    // rows, thresholds and order): at ascension 20 the sheet is 68/75 with one
    // potion slot lost and the starting curse ahead of the starter cards.
    EXPECT_EQ(rc.run.hp, 68);
    EXPECT_EQ(rc.run.max_hp, 75);
    EXPECT_EQ(rc.run.gold, 99);
    EXPECT_EQ(rc.run.act, 1);
    EXPECT_EQ(rc.run.floor, 0);
    EXPECT_EQ(rc.run.potion_slots, 2);
    EXPECT_EQ(rc.run.master_deck_count, 11);
    EXPECT_EQ(static_cast<CardId>(rc.run.master_deck[0].card_id),
              CardId::ASCENDERS_BANE);
    // 5 Strike, 4 Defend, 1 Bash behind it.
    int strikes = 0, defends = 0, bashes = 0;
    for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
        switch (static_cast<CardId>(rc.run.master_deck[i].card_id)) {
            case CardId::STRIKE: ++strikes; break;
            case CardId::DEFEND: ++defends; break;
            case CardId::BASH: ++bashes; break;
            default: break;
        }
    }
    EXPECT_EQ(strikes, 5);
    EXPECT_EQ(defends, 4);
    EXPECT_EQ(bashes, 1);
    // Burning Blood, acquisition index 0.
    ASSERT_EQ(rc.run.relic_count, 1);
    EXPECT_EQ(rc.run.relics[0].relic_id, static_cast<uint16_t>(RelicId::BURNING_BLOOD));
}

// run_begin's full Neow-pending stream state cross-checked against a REAL live
// A20 Ironclad run from the G4 oracle corpus (campaign b13_on20b, run STS00001,
// Settings.seed = 1790050543751). The three run-start streams -- monsterRng
// (encounter-list generation), relicRng (5 pool-shuffle draws), mapRng (full
// generateMap) -- match the fork's floor-0 {counter,s0,s1} dump BIT-FOR-BIT.
// These are golden constants captured from the live game (not the sim), so the
// test is self-contained (no corpus dependency at build time).
TEST(RunBegin, MatchesLiveOracleFloorZeroStreams) {
    RunController rc = run_begin(1790050543751LL, kA20);
    EXPECT_EQ(rc.run.monster_rng.counter, 41);
    EXPECT_EQ(rc.run.monster_rng.s0, static_cast<uint64_t>(3388898780908912053LL));
    EXPECT_EQ(rc.run.monster_rng.s1, static_cast<uint64_t>(-2195227397617715518LL));
    EXPECT_EQ(rc.run.relic_rng.counter, 5);
    EXPECT_EQ(rc.run.relic_rng.s0, static_cast<uint64_t>(-6368056192266778531LL));
    EXPECT_EQ(rc.run.relic_rng.s1, static_cast<uint64_t>(-2945499761529171947LL));
    EXPECT_EQ(rc.run.map_rng.counter, 94);
    EXPECT_EQ(rc.run.map_rng.s0, static_cast<uint64_t>(8756960311115476284LL));
    EXPECT_EQ(rc.run.map_rng.s1, static_cast<uint64_t>(8714461748465431467LL));
}

// =============================================================================
// Trap 7: floor reseed AFTER ++floor
// =============================================================================

TEST(FloorReseed, Trap7ReseedsFiveStreamsWithPostIncrementFloor) {
    RunController rc = run_begin(kSeed, kA20);
    // Force the floor-1 room to a Shop. Its whole build reads only RUN-scoped
    // streams (merchantRng / cardRng / potionRng) and never touches a
    // FLOOR-scoped one, so the five reseeded streams are still a pristine
    // post-increment snapshot after the merchant exists. (An Event room would
    // resolve its ?-roll instead -- an eventRng draw and possibly a combat.)
    // Force every row-0 column so whichever start is picked is the Shop room.
    for (int col = 0; col < kMapCols; ++col) {
        rc.run.map[run_state_map_index(col, 0)].room_type =
            static_cast<uint8_t>(RoomType::Shop);
    }
    leave_neow(rc);                 // NEOW -> MAP_CHOICE (floor 0)
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    uint8_t x = first_start_column(rc);
    step(rc, make_action(ActionVerb::CHOOSE, x));

    EXPECT_EQ(rc.run.floor, 1);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::SHOP));
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Shop));

    // The reseed uses floor 1 (post-increment). A reseed with the OLD floor (0)
    // would produce floor_stream(seed, 0) -- assert it is floor 1, proving trap 7.
    const RngStream fs1 = floor_stream(kSeed, 1);
    const RngStream fs0 = floor_stream(kSeed, 0);
    EXPECT_TRUE(streams_equal(rc.combat.monster_hp_rng, fs1));
    EXPECT_TRUE(streams_equal(rc.combat.ai_rng, fs1));
    EXPECT_TRUE(streams_equal(rc.combat.shuffle_rng, fs1));
    EXPECT_TRUE(streams_equal(rc.combat.card_random_rng, fs1));
    EXPECT_TRUE(streams_equal(rc.combat.misc_rng, fs1));
    EXPECT_FALSE(streams_equal(rc.combat.monster_hp_rng, fs0));
}

TEST(FloorReseed, ReseedTracksFloorAcrossMultipleTransitions) {
    RunController rc = run_begin(kSeed, kA20);
    // Force rows 0 and 1 to Shop: a merchant consumes no FLOOR-scoped stream,
    // so both transitions leave a pristine reseed (an Event room would consume
    // an eventRng roll).
    for (int x = 0; x < kMapCols; ++x) {
        rc.run.map[run_state_map_index(x, 0)].room_type =
            static_cast<uint8_t>(RoomType::Shop);
        rc.run.map[run_state_map_index(x, 1)].room_type =
            static_cast<uint8_t>(RoomType::Shop);
    }
    leave_neow(rc);
    uint8_t x0 = first_start_column(rc);
    step(rc, make_action(ActionVerb::CHOOSE, x0));  // -> floor 1
    EXPECT_TRUE(streams_equal(rc.combat.misc_rng, floor_stream(kSeed, 1)));

    // Manually advance to floor 2 via a legal edge (reset phase to MAP_CHOICE as
    // the reward/proceed flow would).
    rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
    RunActionMask m{};
    legal_actions(rc, m);
    uint8_t x1 = kMapCols;
    for (uint8_t x = 0; x < kMapCols; ++x) {
        if (m.can_choose_node[x]) { x1 = x; break; }
    }
    ASSERT_LT(x1, kMapCols);
    step(rc, make_action(ActionVerb::CHOOSE, x1));  // -> floor 2
    EXPECT_EQ(rc.run.floor, 2);
    EXPECT_TRUE(streams_equal(rc.combat.misc_rng, floor_stream(kSeed, 2)));
}

// =============================================================================
// Run-combat entry == combat_begin (the fixture-verified path)
// =============================================================================

TEST(RunCombat, MatchesCombatBeginForJawWormFloor) {
    const int64_t seed = find_jaw_worm_seed();
    RunController rc = run_begin(seed, kA20);
    // Clear the starting relic so the combat mirror matches combat_begin's empty
    // relic list; the mirror is exercised separately.
    rc.run.relic_count = 0;
    rc.run.relics[0] = RelicSlot{};
    // combat_begin is the STANDALONE entry point and applies the base Ironclad
    // sheet (80/80, advance.hpp), while the run layer seeds HP from the run --
    // which now carries the run-setup ascension modifiers. Level the sheet so
    // this test stays about the combat-construction SEQUENCE; the modifiers
    // themselves have their own suite.
    rc.run.hp = 80;
    rc.run.max_hp = 80;

    leave_neow(rc);
    uint8_t x = first_start_column(rc);
    step(rc, make_action(ActionVerb::CHOOSE, x));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT))
        << "floor-1 Jaw Worm room should enter combat";

    // combat_begin over the same seed/floor/deck must be byte-identical. The deck
    // is read out of the run rather than rebuilt, so it carries whatever the
    // run-setup ascension modifiers put there (at ascension 20, the curse first).
    const std::size_t deck_n = rc.run.master_deck_count;
    std::vector<CardId> deck;
    deck.reserve(deck_n);
    for (std::size_t i = 0; i < deck_n; ++i) {
        deck.push_back(static_cast<CardId>(rc.run.master_deck[i].card_id));
    }
    CombatState ref = combat_begin(seed, 1, std::span<const CardId>(deck));

    EXPECT_EQ(std::memcmp(&rc.combat, &ref, sizeof(CombatState)), 0)
        << "run-combat entry drifted from combat_begin for a single Jaw Worm";
}

TEST(RunCombat, LousePreBattleAndInnateResolveBeforePlayerControl) {
    RunController rc = run_begin(find_two_louse_seed(), kA20);
    // Replace one starting card with Writhe: run-created combat must honor the
    // B3.9 Innate flag even though the deck is shuffled before encounter entry.
    rc.run.master_deck[9].card_id = static_cast<uint16_t>(CardId::WRITHE);

    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));

    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    ASSERT_EQ(rc.combat.monster_count, 2);
    // Each louse consumes HP + bite in its constructor, then one Curl Up roll.
    EXPECT_EQ(rc.combat.monster_hp_rng.counter, 6);
    for (uint8_t i = 0; i < rc.combat.monster_count; ++i) {
        const int16_t curl = monster_power(rc.combat, i, PowerId::CURL_UP);
        EXPECT_GE(curl, 9);
        EXPECT_LE(curl, 12);
    }
    EXPECT_EQ(rc.combat.action_count, 0);
    EXPECT_EQ(rc.combat.pre_turn_count, 0);
    EXPECT_EQ(rc.combat.card_queue_count, 0);
    EXPECT_EQ(rc.combat.monster_queue_count, 0);
    EXPECT_TRUE(hand_contains(rc.combat, CardId::WRITHE));
}

// =============================================================================
// The Bottled trio's master-deck marker (run_deck.hpp) -- combat construction
// =============================================================================
//
// CardGroup.initializeDeck (CardGroup.java:928-955): after the one shuffle,
// `if (c.isInnate) placeOnTop; else if (inBottleFlame || inBottleLightning ||
// inBottleTornado) placeOnTop;` -- Bottled and Innate share ONE top-placement
// list, and :951-954 queues DrawCardAction(placeOnTop.size() - masterHandSize)
// into preTurnActions when that list exceeds the hand size.

// Count how many hand+draw slots reference card-pool row `pi`.
int pool_index_occurrences(const CombatState& s, uint8_t pi) {
    int cnt = 0;
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        if (s.hand[i] == pi) ++cnt;
    }
    for (uint8_t i = 0; i < s.draw_count; ++i) {
        if (s.draw[i] == pi) ++cnt;
    }
    return cnt;
}

bool hand_holds_pool_index(const CombatState& s, uint8_t pi) {
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        if (s.hand[i] == pi) return true;
    }
    return false;
}

TEST(RunCombatBottle, BottledMasterCardOpensInHandLikeInnate) {
    RunController rc = run_begin(kSeed, kA20);
    // A20 deck: Ascender's Bane at 0, then 5 Strikes / 4 Defends / Bash.
    // Bottle the first Strike (a Strike IS offerable by the game's bottle
    // grid: getPurgeableCards().getAttacks() has no rarity clause -- only
    // canSpawn reads rarity, BottledFlame.java:43 vs :93-99).
    ASSERT_EQ(rc.run.master_deck[1].card_id,
              static_cast<uint16_t>(CardId::STRIKE));
    rc.run.master_deck[1].flags = kMasterCardInBottleFlame;

    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));

    // The combat instance carries INNATE (OR-ed in at pool build; the builder
    // is index-aligned with the master deck), and the bottled instance is in
    // the opening hand.
    EXPECT_TRUE(has_card_flag(rc.combat.card_pool[1].flags, CardFlag::INNATE));
    EXPECT_TRUE(hand_holds_pool_index(rc.combat, 1));
    // The master-deck bottle bit itself must NOT leak into combat flags: the
    // pool flags are the registry's plus INNATE, nothing else.
    const CardDef* strike = card_def(CardId::STRIKE);
    ASSERT_NE(strike, nullptr);
    EXPECT_EQ(rc.combat.card_pool[1].flags,
              static_cast<uint16_t>(card_flags(*strike, 0) |
                                    static_cast<uint16_t>(CardFlag::INNATE)));
    // The run-side marker survives the combat construction untouched.
    EXPECT_EQ(rc.run.master_deck[1].flags, kMasterCardInBottleFlame);
}

TEST(RunCombatBottle, ABottledCardThatIsAlsoInnateIsAddedOnce) {
    RunController rc = run_begin(kSeed, kA20);
    // Writhe is Innate by registry; stamping a bottle bit on the same instance
    // exercises the Java's if/else-if single-add (CardGroup.java:933-941).
    rc.run.master_deck[1].card_id = static_cast<uint16_t>(CardId::WRITHE);
    rc.run.master_deck[1].flags = kMasterCardInBottleLightning;

    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));

    EXPECT_EQ(pool_index_occurrences(rc.combat, 1), 1)
        << "innate+bottled must place the instance exactly once";
    EXPECT_TRUE(hand_holds_pool_index(rc.combat, 1));
    const int deck_n = static_cast<int>(rc.run.master_deck_count);
    EXPECT_EQ(static_cast<int>(rc.combat.hand_count) +
                  static_cast<int>(rc.combat.draw_count),
              deck_n);
}

TEST(RunCombatBottle, SixTopPlacedCardsAllOpenInHandViaTheOverflowDraw) {
    RunController rc = run_begin(kSeed, kA20);
    // Four Writhes (registry-innate) + two bottled instances = a 6-card
    // placeOnTop collection; masterHandSize is 5, so initializeDeck queues the
    // 1-card overflow draw (CardGroup.java:951-954).
    for (uint16_t i = 1; i <= 4; ++i) {
        rc.run.master_deck[i].card_id = static_cast<uint16_t>(CardId::WRITHE);
    }
    ASSERT_EQ(rc.run.master_deck[5].card_id,
              static_cast<uint16_t>(CardId::STRIKE));
    rc.run.master_deck[5].flags = kMasterCardInBottleFlame;
    ASSERT_EQ(rc.run.master_deck[6].card_id,
              static_cast<uint16_t>(CardId::DEFEND));
    rc.run.master_deck[6].flags = kMasterCardInBottleLightning;

    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.phase,
              static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));

    EXPECT_EQ(rc.combat.hand_count, 6)
        << "the 6th top-placed card is drawn by the preTurnActions overflow";
    EXPECT_TRUE(hand_holds_pool_index(rc.combat, 5));
    EXPECT_TRUE(hand_holds_pool_index(rc.combat, 6));
    int writhes_in_hand = 0;
    for (uint8_t i = 0; i < rc.combat.hand_count; ++i) {
        if (rc.combat.card_pool[rc.combat.hand[i]].card_id ==
            static_cast<uint16_t>(CardId::WRITHE)) {
            ++writhes_in_hand;
        }
    }
    EXPECT_EQ(writhes_in_hand, 4);
    EXPECT_EQ(rc.combat.draw_count,
              static_cast<uint8_t>(rc.run.master_deck_count - 6));
    // The overflow draw resolved before control returned: nothing pending,
    // energy already recharged by the ordinary turn-1 block.
    EXPECT_EQ(rc.combat.action_count, 0);
    EXPECT_EQ(rc.combat.player_energy, 3);
}

// =============================================================================
// The pending-bottle overlay (claim -> modal grid -> pick), run_advance.hpp
// =============================================================================

TEST(BottleOverlay, ClaimingABottleOpensTheModalGridAndThePickBottlesTheCard) {
    RunController rc = run_begin(kSeed, kA20);
    // Park the controller on a reward screen holding a Bottled Flame row --
    // the shape elite rewards, chests (open_treasure_chest routes here) and
    // event reward screens all produce.
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT_REWARD);
    rc.rewards = RewardScreen{};
    rc.rewards.open_card_item = kNoOpenCardReward;
    rc.rewards.count = 1;
    rc.rewards.items[0].kind = static_cast<uint8_t>(RewardItemKind::RELIC);
    rc.rewards.items[0].id = static_cast<uint16_t>(RelicId::BOTTLED_FLAME);
    const uint8_t relics_before = rc.run.relic_count;

    RunActionMask mask{};
    legal_actions(rc, mask);
    ASSERT_TRUE(mask.can_claim_reward[0]);

    step(rc, make_action(ActionVerb::CHOOSE, 0));
    // Relic granted (append order), the item consumed, the overlay up.
    ASSERT_EQ(rc.run.relic_count, relics_before + 1);
    EXPECT_EQ(rc.run.relics[relics_before].relic_id,
              static_cast<uint16_t>(RelicId::BOTTLED_FLAME));
    EXPECT_EQ(rc.pending_bottle,
              static_cast<uint8_t>(MasterBottleKind::FLAME));
    EXPECT_EQ(rc.rewards.count, 0);

    legal_actions(rc, mask);
    EXPECT_FALSE(mask.can_proceed)
        << "the grid is modal (RoomPhase.INCOMPLETE, BottledFlame.java:49)";
    // Eligible rows = purgeable ATTACKs. A20 deck: Ascender's Bane 0 (not
    // purgeable), Strikes 1-5 and Bash 10 (attacks), Defends 6-9 (skills).
    EXPECT_FALSE(mask.can_choose_master_deck[0]);
    EXPECT_TRUE(mask.can_choose_master_deck[1]);
    EXPECT_FALSE(mask.can_choose_master_deck[6]);
    EXPECT_TRUE(mask.can_choose_master_deck[10]);

    // An ineligible pick is a non-corrupting no-op; the overlay stays.
    step(rc, make_action(ActionVerb::CHOOSE, 6));
    EXPECT_EQ(rc.pending_bottle,
              static_cast<uint8_t>(MasterBottleKind::FLAME));
    EXPECT_EQ(rc.run.master_deck[6].flags, 0);

    // The pick bottles the INSTANCE and closes the overlay; the phase and the
    // relic's counter (-1, never written) are untouched.
    step(rc, make_action(ActionVerb::CHOOSE, 10));
    EXPECT_EQ(rc.pending_bottle,
              static_cast<uint8_t>(MasterBottleKind::NONE));
    EXPECT_EQ(rc.run.master_deck[10].flags, kMasterCardInBottleFlame);
    EXPECT_EQ(rc.run.relics[relics_before].counter, -1);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    legal_actions(rc, mask);
    EXPECT_TRUE(mask.can_proceed) << "the reward screen is back";
}

TEST(BottleOverlay, ABottleWithNoEligibleCardIsClaimedScreenlessAndUnbottled) {
    RunController rc = run_begin(kSeed, kA20);
    // Bottled Tornado with no POWER card in the deck: the :41 guard opens no
    // screen; the relic is granted, permanently unbottled.
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT_REWARD);
    rc.rewards = RewardScreen{};
    rc.rewards.open_card_item = kNoOpenCardReward;
    rc.rewards.count = 1;
    rc.rewards.items[0].kind = static_cast<uint8_t>(RewardItemKind::RELIC);
    rc.rewards.items[0].id = static_cast<uint16_t>(RelicId::BOTTLED_TORNADO);
    const uint8_t relics_before = rc.run.relic_count;

    step(rc, make_action(ActionVerb::CHOOSE, 0));
    ASSERT_EQ(rc.run.relic_count, relics_before + 1);
    EXPECT_EQ(rc.pending_bottle,
              static_cast<uint8_t>(MasterBottleKind::NONE));
    for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
        EXPECT_EQ(rc.run.master_deck[i].flags, 0);
    }
    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_TRUE(mask.can_proceed);
}

TEST(RunCombatBottle, StandaloneCombatBeginRunsTheSameOverflowDraw) {
    // combat_begin has no master-deck instances, so a bottle cannot be
    // expressed here -- but six registry-innate Writhes reach the same
    // initializeDeck overflow, and the two builders must not drift.
    std::vector<CardId> deck;
    for (int i = 0; i < 6; ++i) deck.push_back(CardId::WRITHE);
    for (int i = 0; i < 6; ++i) deck.push_back(CardId::STRIKE);
    CombatState s = combat_begin(kSeed, 1, std::span<const CardId>(deck));

    ASSERT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    EXPECT_EQ(s.hand_count, 6);
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        EXPECT_EQ(s.card_pool[s.hand[i]].card_id,
                  static_cast<uint16_t>(CardId::WRITHE));
    }
    EXPECT_EQ(s.draw_count, 6);
    EXPECT_EQ(s.action_count, 0);
}

// =============================================================================
// atBattleStart relics, through the REAL combat-entry path
// =============================================================================
//
// applyStartOfCombatLogic (AbstractPlayer.java:1892-1901) fires every relic's
// atBattleStart in acquisition order; its ONE call site is the turn-1
// combat-start block of AbstractRoom.update (AbstractRoom.java:236-258), where
// the opening DrawCardAction (AbstractRoom.java:242) is queued before the hook
// is invoked (AbstractRoom.java:245). enter_combat therefore dispatches it after
// its turn-1 pump; the full derivation lives at that call in run_advance.cpp.
//
// Every test below walks run_begin -> map pick -> enter_combat and then reads
// the resulting CombatState. NONE of them calls dispatch_relics_at_battle_start.
// That is the whole point: the dispatcher already carried direct-call coverage
// in relic_hooks_test.cpp and passed all of it while having zero production call
// sites, because a direct-call test cannot distinguish "wired" from
// "unreachable". Only entry through enter_combat can.

// Replace the run's relics with `ids`, in acquisition order.
void set_run_relics(RunController& rc, std::initializer_list<RelicId> ids) {
    uint8_t i = 0;
    for (const RelicId id : ids) {
        rc.run.relics[i] = RelicSlot{static_cast<uint16_t>(id), 0};
        ++i;
    }
    rc.run.relic_count = i;
}

// Walk the real path into the floor-1 Jaw Worm combat holding exactly `ids`.
RunController enter_jaw_worm_holding(std::initializer_list<RelicId> ids) {
    RunController rc = run_begin(find_jaw_worm_seed(), kA20);
    set_run_relics(rc, ids);
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    return rc;
}

const PowerSlot* player_power(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.player_powers[i];
        }
    }
    return nullptr;
}

// The action `add_to_top` most recently prepended.
ActionQueueItem front_action(const CombatState& s) {
    return s.action_queue[s.action_head % kActionQueueCap];
}

const PowerSlot* monster_power_slot(const CombatState& s, uint8_t mi, PowerId id) {
    const MonsterState& m = s.monsters[mi];
    for (uint8_t i = 0; i < m.power_count; ++i) {
        if (m.powers[i].power_id == static_cast<uint16_t>(id)) {
            return &m.powers[i];
        }
    }
    return nullptr;
}

// Control: with no battle-start relic the combat is untouched. Establishes the
// baseline the ordering tests below are read against.
TEST(RunCombatBattleStart, NoBattleStartRelicLeavesTheOpeningStateUntouched) {
    RunController rc = enter_jaw_worm_holding({RelicId::BURNING_BLOOD});
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    EXPECT_EQ(rc.combat.hand_count, 5);
    EXPECT_EQ(rc.combat.player_block, 0);
    EXPECT_EQ(rc.combat.player_hp, 68);  // the ascension-20 run-setup sheet
}

// Burning Blood binds only onVictory (registry/relics.yaml), so holding it must
// produce the same combat as holding nothing at all. This is the reason the 20
// golden combat fixtures and the pinned relic-pool oracles do not move when the
// at_battle_start dispatch goes live: their only relic has no binding for it.
TEST(RunCombatBattleStart, BurningBloodEntersCombatIdenticallyToNoRelic) {
    RunController with_relic = enter_jaw_worm_holding({RelicId::BURNING_BLOOD});
    RunController without = enter_jaw_worm_holding({});
    ASSERT_EQ(with_relic.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(without.phase, static_cast<uint8_t>(RunPhase::COMBAT));

    // Blank the relic mirror itself (that field is expected to differ) and
    // require every other combat byte to match.
    CombatState a = with_relic.combat;
    CombatState b = without.combat;
    std::memset(a.relics, 0, sizeof(a.relics));
    std::memset(b.relics, 0, sizeof(b.relics));
    a.relic_count = 0;
    b.relic_count = 0;
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(CombatState)), 0)
        << "a relic with no at_battle_start binding must not perturb combat entry";
}

// THE ORDERING TEST. Anchor.atBattleStart (Anchor.java:32-36) is
// addToBot GainBlockAction(player, 10). start_of_turn (action_queue.cpp) zeroes
// player_block before it queues the opening draw -- the loseBlock of
// GameActionManager.java:352-359, which Java's turn-1 block does not actually
// run. So a dispatch placed BEFORE the turn-1 pump would grant the block and
// then have it wiped in the same pump: the relic fires and nothing happens,
// which is the same dead effect as never firing. Dispatching after the pump --
// where Java's own call site sits, behind the queued DrawCardAction -- keeps it.
TEST(RunCombatBattleStart, AnchorBlockSurvivesTheTurnOneBlockReset) {
    RunController rc = enter_jaw_worm_holding({RelicId::ANCHOR});
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.combat.player_block, 10);
    // ...and it is in place BEFORE the player's first action: turn 1, control
    // handed back, nothing left queued behind it.
    EXPECT_EQ(rc.combat.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    EXPECT_EQ(rc.combat.turn, 1);
    EXPECT_EQ(rc.combat.action_count, 0);
    EXPECT_EQ(rc.combat.pre_turn_count, 0);
    EXPECT_EQ(rc.combat.card_queue_count, 0);
    EXPECT_EQ(rc.combat.cards_played_this_turn, 0);
}

// The draw-ordering pin. BagOfPreparation.atBattleStart
// (BagOfPreparation.java:30-34) is addToBot DrawCardAction(player, 2), queued
// behind the opening DrawCardAction(gameHandSize) -- so the opening five are
// drawn first and the two land on top of them. Both draws come off the same
// draw pile in one uninterrupted run, so the resulting hand must be the opening
// hand PLUS two, in that order, with the pile down by seven.
TEST(RunCombatBattleStart, BagOfPreparationDrawsTwoOnTopOfTheOpeningHand) {
    RunController base = enter_jaw_worm_holding({});
    RunController rc = enter_jaw_worm_holding({RelicId::BAG_OF_PREPARATION});
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(base.combat.hand_count, 5);

    EXPECT_EQ(rc.combat.hand_count, 7);
    EXPECT_EQ(rc.combat.draw_count, base.combat.draw_count - 2);
    // The opening five are unchanged and still first: the relic drew AFTER them,
    // not before. A pre-draw dispatch would draw the pile's top two first and
    // push the opening hand down.
    for (uint8_t i = 0; i < 5; ++i) {
        EXPECT_EQ(rc.combat.hand[i], base.combat.hand[i])
            << "opening-hand slot " << static_cast<int>(i) << " moved";
    }
    // The two extra cards are the ones that were still on top of the base run's
    // draw pile, taken in pile order.
    ASSERT_GE(base.combat.draw_count, 2);
    EXPECT_EQ(rc.combat.hand[5],
              base.combat.draw[base.combat.draw_count - 1]);
    EXPECT_EQ(rc.combat.hand[6],
              base.combat.draw[base.combat.draw_count - 2]);
}

// A native at_battle_start body reached through the real path: BloodVial
// .atBattleStart (BloodVial.java:30-34) heals 2, clamped at max HP. The heal
// changes the HP the combat STARTS at, so it is visible before any action.
TEST(RunCombatBattleStart, BloodVialHealsIntoTheStartingHpOfTheCombat) {
    RunController rc = run_begin(find_jaw_worm_seed(), kA20);
    set_run_relics(rc, {RelicId::BLOOD_VIAL});
    rc.run.hp = 50;
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.combat.player_hp, 52);

    // At full HP the HealAction clamps and the combat starts at max. The run
    // does not begin at full HP any more (the run-setup 90 %-of-max rewrite), so
    // top it up explicitly rather than relying on the starting sheet.
    RunController full = run_begin(find_jaw_worm_seed(), kA20);
    set_run_relics(full, {RelicId::BLOOD_VIAL});
    full.run.hp = full.run.max_hp;
    leave_neow(full);
    step(full, make_action(ActionVerb::CHOOSE, first_start_column(full)));
    ASSERT_EQ(full.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(full.combat.player_hp, full.combat.player_max_hp);
}

// The two DATA at_battle_start shapes -- a SELF power and an ALL_ENEMY power --
// both routed through the real entry. Vajra.atBattleStart (Vajra.java:31-35)
// applies Strength 1 to the player; BagOfMarbles.atBattleStart
// (BagOfMarbles.java:34-40) applies Vulnerable 1 to every monster.
TEST(RunCombatBattleStart, VajraAndBagOfMarblesApplyTheirPowersBeforeTurnOne) {
    RunController rc =
        enter_jaw_worm_holding({RelicId::VAJRA, RelicId::BAG_OF_MARBLES});
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));

    const PowerSlot* str = player_power(rc.combat, PowerId::STRENGTH);
    ASSERT_NE(str, nullptr) << "Vajra's Strength never reached the live combat";
    EXPECT_EQ(str->amount, 1);

    ASSERT_GT(rc.combat.monster_count, 0);
    for (uint8_t m = 0; m < rc.combat.monster_count; ++m) {
        const PowerSlot* vuln = monster_power_slot(rc.combat, m, PowerId::VULNERABLE);
        ASSERT_NE(vuln, nullptr) << "monster " << static_cast<int>(m)
                                 << " has no Bag of Marbles Vulnerable";
        EXPECT_EQ(vuln->amount, 1);
    }
}

// Acquisition order drives the dispatch (relic_hooks.cpp iterates the mirror
// 0..count-1) and the mirror is filled from RunState in that order, so a
// multi-relic hand lands every effect. Held together they must not cancel each
// other or the turn-1 sequence.
TEST(RunCombatBattleStart, SeveralBattleStartRelicsAllLandBeforeTheFirstAction) {
    RunController rc = run_begin(find_jaw_worm_seed(), kA20);
    set_run_relics(rc, {RelicId::ANCHOR, RelicId::BAG_OF_PREPARATION,
                        RelicId::VAJRA, RelicId::BLOOD_VIAL});
    rc.run.hp = 60;
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));

    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    EXPECT_EQ(rc.combat.player_block, 10);   // Anchor
    EXPECT_EQ(rc.combat.hand_count, 7);      // Bag of Preparation
    EXPECT_EQ(rc.combat.player_hp, 62);      // Blood Vial
    const PowerSlot* str = player_power(rc.combat, PowerId::STRENGTH);
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->amount, 1);               // Vajra
    // The turn-1 invariants are intact underneath all of it.
    EXPECT_EQ(rc.combat.turn, 1);
    EXPECT_EQ(rc.combat.action_count, 0);
    EXPECT_EQ(rc.combat.player_energy, 3);
}

// atBattleStartPreDraw is a DISTINCT hook (AT_BATTLE_START_PRE_DRAW), fired from
// applyStartOfCombatPreDrawLogic (AbstractPlayer.java:1903-1908) at
// AbstractRoom.java:241 -- genuinely before the opening draw, unlike the
// atBattleStart site wired above. The engine does not conflate the two: they are
// separate enum values and dispatch_relics_at_battle_start fires only
// AT_BATTLE_START. It has no call site because NO registered relic binds it --
// in the Java its only holders are Gambling Chip, Holy Water, Ninja Scroll, Pure
// Water and Toolbox, none of which is in registry/relics.yaml. This test pins
// that emptiness, so the day a pre-draw relic is registered this fails and the
// second dispatch (ahead of the pump's draw) has to be written deliberately
// rather than folded into the post-draw one.
TEST(RunCombatBattleStart, NoRegisteredRelicBindsThePreDrawHook) {
    ASSERT_FALSE(sts::registry::kRelicDefs.empty()) << "no relic rows -- probe is wrong";
    bool any_battle_start = false;
    for (const sts::registry::RelicDef* d : sts::registry::kRelicDefs) {
        if (d->hook_binding(sts::registry::RelicHook::AT_BATTLE_START) != nullptr) {
            any_battle_start = true;
        }
        EXPECT_EQ(d->hook_binding(
                      sts::registry::RelicHook::AT_BATTLE_START_PRE_DRAW),
                  nullptr)
            << "relic id " << static_cast<int>(d->id)
            << " binds at_battle_start_pre_draw, which has no dispatch site; "
               "enter_combat must gain one BEFORE its turn-1 pump";
    }
    EXPECT_TRUE(any_battle_start)
        << "no relic binds at_battle_start -- the wiring under test would be moot";
}

// =============================================================================
// wasHPLost relics across a run: Centennial Puzzle's per-combat re-arm
// =============================================================================
//
// CentennialPuzzle gates on a `private static boolean usedThisCombat`
// (CentennialPuzzle.java:21) that atPreBattle sets back to false (:34), so the
// draw is once per COMBAT, not once per run. The engine used to keep that flag
// in RelicSlot.counter, which is run-persistent and folded back into RunState --
// so it never re-armed AND it produced the STS00068 `relics[1].counter: -1 -> 0`
// capture divergence. The flag now lives in kCombatFlagCentennialPuzzleUsed, and
// its reset is structural: enter_combat value-initializes a fresh CombatState.
//
// This walks TWO real combats of one run through enter_combat, which is the only
// way to observe the reset -- a direct-call test constructs its own
// CombatStates and so cannot tell "reset by the real entry" from "never set".
TEST(RunCombatWasHpLost, CentennialPuzzleReArmsInASecondCombat) {
    const int64_t seed = find_jaw_worm_seed();
    RunController rc = run_begin(seed, kA20);
    // Seeded the way acquire_relic seeds it -- AbstractRelic's untouched -1.
    rc.run.relics[0] =
        RelicSlot{static_cast<uint16_t>(RelicId::CENTENNIAL_PUZZLE), -1};
    rc.run.relic_count = 1;
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));

    // -- combat 1 --
    ASSERT_EQ(rc.combat.flags & kCombatFlagCentennialPuzzleUsed, 0u)
        << "atPreBattle: usedThisCombat starts false";
    const uint8_t actions_before = rc.combat.action_count;
    dispatch_was_hp_lost(rc.combat, kActorPlayer, kActorPlayer, /*amount=*/5);
    ASSERT_EQ(rc.combat.action_count, actions_before + 1)
        << "first HP loss of combat 1 must queue the draw";
    EXPECT_EQ(front_action(rc.combat).opcode, static_cast<uint16_t>(Opcode::DRAW));
    EXPECT_EQ(front_action(rc.combat).amount, 3);  // NUM_CARDS
    EXPECT_NE(rc.combat.flags & kCombatFlagCentennialPuzzleUsed, 0u);
    // A second loss inside the SAME combat adds nothing.
    const uint8_t after_first = rc.combat.action_count;
    dispatch_was_hp_lost(rc.combat, kActorPlayer, kActorPlayer, /*amount=*/4);
    EXPECT_EQ(rc.combat.action_count, after_first);
    EXPECT_EQ(rc.run.relics[0].counter, -1) << "STS00068: the counter never moves";

    // -- win combat 1, claim nothing, walk to a second monster room --
    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD))
        << "expected a win (player survived the Jaw Worm)";
    // fold_back_combat has now copied the mirrored counter into RunState. This is
    // exactly where the old design leaked its flag out to the capture-compared
    // field.
    EXPECT_EQ(rc.run.relics[0].counter, -1) << "fold-back must not carry a flag";

    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    RunActionMask m{};
    legal_actions(rc, m);
    uint8_t next = kMapCols;
    for (uint8_t x = 0; x < kMapCols; ++x) {
        if (m.can_choose_node[x]) { next = x; break; }
    }
    ASSERT_LT(next, kMapCols);
    // Force the destination to a monster room, and pin its encounter so the test
    // does not depend on what the seed's second monster-list entry happens to be
    // (an unimplemented member would park in ROOM_UNIMPLEMENTED). The subject is
    // the flag's lifetime, not encounter selection.
    rc.run.map[run_state_map_index(next, 1)].room_type =
        static_cast<uint8_t>(RoomType::Monster);
    ASSERT_LT(rc.monster_cursor, rc.lists.monster_list.size());
    rc.lists.monster_list[rc.monster_cursor] = "Jaw Worm";
    step(rc, make_action(ActionVerb::CHOOSE, next));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT))
        << "second combat did not open";
    EXPECT_EQ(rc.run.floor, 2);

    // -- combat 2: the relic is armed again --
    EXPECT_EQ(rc.combat.flags & kCombatFlagCentennialPuzzleUsed, 0u)
        << "enter_combat's fresh CombatState IS atPreBattle's reset";
    ASSERT_EQ(rc.combat.relic_count, 1);
    EXPECT_EQ(rc.combat.relics[0].counter, -1) << "the mirror carries -1 in too";
    const uint8_t actions_before_2 = rc.combat.action_count;
    dispatch_was_hp_lost(rc.combat, kActorPlayer, kActorPlayer, /*amount=*/5);
    ASSERT_EQ(rc.combat.action_count, actions_before_2 + 1)
        << "Centennial Puzzle must fire again in the second combat";
    EXPECT_EQ(front_action(rc.combat).opcode, static_cast<uint16_t>(Opcode::DRAW));
    EXPECT_EQ(front_action(rc.combat).amount, 3);
    EXPECT_EQ(rc.run.relics[0].counter, -1);
}

// =============================================================================
// USE_POTION through run and combat layers
// =============================================================================

TEST(RunPotion, FruitJuiceIsLegalOutsideCombatAndMutatesPersistentHp) {
    RunController rc = run_begin(kSeed, kA20);
    leave_neow(rc);  // stable non-combat MAP_CHOICE state
    rc.run.hp = 50;
    rc.run.max_hp = 80;
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FRUIT_JUICE);

    RunActionMask mask{};
    legal_actions(rc, mask);
    ASSERT_TRUE(mask.can_use_potion[0]);

    step(rc, make_action(ActionVerb::USE_POTION, 0));
    EXPECT_EQ(rc.run.hp, 55);
    EXPECT_EQ(rc.run.max_hp, 85);
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

TEST(RunPotion, ToyOrnithopterTriggersOutsideCombat) {
    RunController rc = run_begin(kSeed, kA20);
    leave_neow(rc);
    rc.run.hp = 50;
    rc.run.max_hp = 80;
    rc.run.relics[1] =
        RelicSlot{static_cast<uint16_t>(RelicId::TOY_ORNITHOPTER), 0};
    rc.run.relic_count = 2;
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FRUIT_JUICE);

    step(rc, make_action(ActionVerb::USE_POTION, 0));
    EXPECT_EQ(rc.run.max_hp, 85);
    EXPECT_EQ(rc.run.hp, 60);  // Fruit Juice +5, then Toy Ornithopter +5.
}

TEST(RunPotion, EntropicBrewUsesLimitedDrawsThenFillsOpenedSlots) {
    RunController rc = run_begin(kSeed, kA20);
    leave_neow(rc);
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::ENTROPIC_BREW);
    rc.run.potions[1] = static_cast<uint16_t>(PotionId::BLOOD_POTION);

    RngStream expected_rng = rc.run.potion_rng;
    const PotionId first = hand_limited_potion_roll(expected_rng);
    (void)hand_limited_potion_roll(expected_rng);  // A20 has two slots -> two rolls.

    step(rc, make_action(ActionVerb::USE_POTION, 0));
    EXPECT_TRUE(streams_equal(rc.run.potion_rng, expected_rng));
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(first));
    EXPECT_EQ(rc.run.potions[1], static_cast<uint16_t>(PotionId::BLOOD_POTION));
}

TEST(RunPotion, TargetPotionDelegatesToCombatPumpAndConsumesSlot) {
    RunController rc = enter_jaw_worm_combat();
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FIRE_POTION);
    const int hp_before = rc.combat.monsters[0].hp;

    RunActionMask mask{};
    legal_actions(rc, mask);
    ASSERT_TRUE(mask.can_use_potion[0]);
    ASSERT_TRUE(mask.can_use_potion_target[0][0]);

    step(rc, make_action(ActionVerb::USE_POTION, 0, 0));
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE));
    EXPECT_EQ(rc.combat.monsters[0].hp, hp_before - 20);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
}

TEST(RunPotion, SmokeBombEscapeIsNotAKillAndOpensProceedChoice) {
    RunController rc = enter_jaw_worm_combat();
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::SMOKE_BOMB);
    rc.combat.player_hp = 40;
    const int monster_hp = rc.combat.monsters[0].hp;

    const StepResult result =
        step_with_result(rc, make_action(ActionVerb::USE_POTION, 0));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(rc.combat_outcome,
              static_cast<uint8_t>(RunCombatOutcome::SMOKE_BOMB));
    EXPECT_NE(rc.combat.flags & kCombatFlagPlayerEscaped, 0u)
        << "SmokeBomb.use must latch the player's escape before rewards open";
    EXPECT_EQ(rc.combat.monsters[0].hp, monster_hp);  // monster was not killed.
    EXPECT_EQ(rc.run.hp, 46);  // AbstractRoom.endBattle still fires Burning Blood.
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE));
    EXPECT_FALSE(result.terminal);
    EXPECT_FLOAT_EQ(result.reward, 0.0f);

    RunActionMask reward_mask{};
    legal_actions(rc, reward_mask);
    EXPECT_TRUE(reward_mask.can_proceed);
    // B4.5: a Smoke Bomb offers nothing claimable, but the battle-over block
    // still consumed the gold + unconditional-potion draws (combat_rewards_test
    // pins the exact accounting; here we pin "nothing on the screen").
    EXPECT_EQ(rc.rewards.count, 0);
    for (int i = 0; i < kRewardItemCap; ++i) {
        EXPECT_FALSE(reward_mask.can_claim_reward[i]);
    }
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

// =============================================================================
// Escape at the run layer
// =============================================================================

// An ESCAPED monster keeps positive hp but is out of the fight: every combat-
// layer targeting read already excludes it (monster_dead_or_escaped -- the
// game's isDying/isEscaping walks, MonsterGroup.java:164,180,204,220, and an
// escaped monster has left the screen entirely, AbstractMonster.
// updateEscapeAnimation:894-906). The RUN-layer potion-target mask must apply
// the same predicate: a targeted potion can never name an escaped monster.
TEST(RunEscape, TargetedPotionRefusesAnEscapedMonster) {
    RunController rc = run_begin(find_two_louse_seed(), kA20);
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.monster_count, 2);

    // The record shape the ESCAPE opcode leaves behind: kMonsterFlagEscaped
    // set, hp untouched (alive, gone).
    rc.combat.monsters[0].flags |= kMonsterFlagEscaped;
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FIRE_POTION);

    RunActionMask mask{};
    legal_actions(rc, mask);
    ASSERT_TRUE(mask.can_use_potion[0]);
    EXPECT_FALSE(mask.can_use_potion_target[0][0])
        << "a targeted potion must not be able to name an escaped monster";
    EXPECT_TRUE(mask.can_use_potion_target[0][1])
        << "the monster still in the fight must stay targetable";
}

int count_reward_kind(const RewardScreen& s, RewardItemKind k) {
    int n = 0;
    for (uint8_t i = 0; i < s.count; ++i) {
        n += s.items[i].kind == static_cast<uint8_t>(k) ? 1 : 0;
    }
    return n;
}

// The REAL run path into a floor-1 solo-Looter combat (the encounter-key
// substitution trick from combat_start_test: it puts the encounter in front of
// the genuine construction path instead of hunting a seed whose map opens on
// a strong-pool floor).
RunController enter_looter_combat() {
    RunController rc = run_begin(kSeed, kA20);
    rc.lists.monster_list[0] = "Looter";
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.combat.monster_count, 1);
    return rc;
}

// The escaped-thief terminal, end to end through the run layer: the Looter's
// machine is Mug, Mug, [Smoke Bomb | Lunge, Smoke Bomb], Escape, so doing
// nothing ends the combat by escape on turn 4 or 5. The run must land on the
// MUGGED outcome, actually take the stolen gold out of RunState.gold, roll no
// plain-room gold (treasureRng untouched, AbstractRoom.java:319), run the
// potion roll at chance zero (potionRng moves, the ratchet moves, no item --
// :585-607), and STILL offer the card reward: the mugged openCombat calls
// setupItemReward (CombatRewardScreen.java:280-285).
TEST(RunEscape, LooterEscapeSettlesGoldAndOpensTheMuggedScreen) {
    RunController rc = enter_looter_combat();
    const RngStream treasure_before = rc.run.treasure_rng;
    const RngStream potion_before = rc.run.potion_rng;
    const int32_t card_counter_before = rc.run.card_rng.counter;
    ASSERT_EQ(rc.run.gold, 99);

    for (int turn = 0; turn < 8 &&
                       rc.phase == static_cast<uint8_t>(RunPhase::COMBAT);
         ++turn) {
        step(rc, make_action(ActionVerb::END_TURN));
    }
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD))
        << "the solo Looter's escape must end the combat";
    EXPECT_EQ(rc.combat_outcome, static_cast<uint8_t>(RunCombatOutcome::MUGGED));
    EXPECT_TRUE(monster_escaped(rc.combat.monsters[0]));
    EXPECT_GT(rc.combat.monsters[0].hp, 0);

    // Gold settlement: min(count * goldAmt, gold). Both machine paths steal
    // before escaping -- 2 mugs, or 2 mugs + a lunge.
    const int steals = static_cast<int>(looter_steal_count(rc.combat.monsters[0]));
    ASSERT_GE(steals, 2);
    ASSERT_LE(steals, 3);
    EXPECT_EQ(rc.run.gold, 99 - steals * kLooterGoldAmt)
        << "the escaped thief's stolen gold must actually leave the purse";

    // Screen shape: cards only. No gold roll (treasureRng byte-identical), no
    // stolen-gold return (the thief was not killed), the potion roll consumed
    // at chance zero with the +10 ratchet.
    EXPECT_EQ(count_reward_kind(rc.rewards, RewardItemKind::GOLD), 0);
    EXPECT_EQ(count_reward_kind(rc.rewards, RewardItemKind::STOLEN_GOLD), 0);
    EXPECT_EQ(count_reward_kind(rc.rewards, RewardItemKind::POTION), 0);
    EXPECT_EQ(count_reward_kind(rc.rewards, RewardItemKind::CARDS), 1);
    EXPECT_TRUE(streams_equal(rc.run.treasure_rng, treasure_before));
    EXPECT_EQ(rc.run.potion_rng.counter, potion_before.counter + 1);
    EXPECT_EQ(rc.run.blizzard_potion_mod, 10);
    EXPECT_GT(rc.run.card_rng.counter, card_counter_before);

    // The screen is a live CHOOSE state: the card item is claimable and
    // Proceed leaves for the map.
    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_TRUE(mask.can_proceed);
    EXPECT_TRUE(mask.can_claim_reward[0]);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

// The killed-thief terminal: die() returns the clamped accrual through the
// reward screen (addStolenGoldToRewards, Looter.java:170-172), so the run
// deducts the settled amount AND assembles a claimable STOLEN_GOLD item ahead
// of every battle-over item; claiming it puts the gold back through the
// gainGold door. Abandoning it (Proceed) would keep the purse short, exactly
// like the game.
TEST(RunEscape, KilledLooterReturnsStolenGoldThroughTheScreen) {
    RunController rc = enter_looter_combat();
    // Two END_TURNs: Mug, Mug -- steal count 2, gold still untouched (the
    // engine settles at combat end, provenance on settle_stolen_gold).
    step(rc, make_action(ActionVerb::END_TURN));
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(looter_steal_count(rc.combat.monsters[0]), 2);
    ASSERT_EQ(rc.run.gold, 99);

    // Kill the thief (the record shape a lethal hit leaves: hp 0, the steal
    // count surviving on the dead record) and let the pump see it.
    rc.combat.monsters[0].hp = 0;
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(rc.combat_outcome, static_cast<uint8_t>(RunCombatOutcome::KILLED));

    const int32_t stolen = 2 * kLooterGoldAmt;
    EXPECT_EQ(rc.run.gold, 99 - stolen);
    ASSERT_GE(rc.rewards.count, 2);
    EXPECT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::STOLEN_GOLD))
        << "die() ran during combat, so its item precedes the battle-over gold";
    EXPECT_EQ(rc.rewards.items[0].gold, stolen);
    EXPECT_EQ(rc.rewards.items[0].bonus_gold, 0);
    EXPECT_EQ(count_reward_kind(rc.rewards, RewardItemKind::GOLD), 1)
        << "a kill (not an all-escape) keeps the plain-room gold roll";
    EXPECT_EQ(count_reward_kind(rc.rewards, RewardItemKind::CARDS), 1);

    // Claim the return: the purse is whole again.
    step(rc, make_action(ActionVerb::CHOOSE, 0));
    EXPECT_EQ(rc.run.gold, 99);
    EXPECT_EQ(count_reward_kind(rc.rewards, RewardItemKind::STOLEN_GOLD), 0);
}

// Mugged beats smoked (AbstractRoom.update:334-341 checks mugged FIRST): a
// player who smoke-bombs out of a combat some thief already fled still gets
// the mug screen, and the mug screen assembles the FULL kill shape -- the
// gold/potion gates read haveMonstersEscaped (false here: the survivor is
// alive and un-escaped), not the mug flag.
TEST(RunEscape, SmokeBombAfterAMugKeepsTheMuggedScreenAndItsRewards) {
    RunController rc = enter_jaw_worm_combat();
    rc.combat.flags |= kCombatFlagMugged;  // a thief already ran this combat
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::SMOKE_BOMB);

    step(rc, make_action(ActionVerb::USE_POTION, 0));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(rc.combat_outcome, static_cast<uint8_t>(RunCombatOutcome::MUGGED))
        << "mugged outranks smoked in the screen pick";
    EXPECT_NE(rc.combat.flags & kCombatFlagMugged, 0u);
    EXPECT_NE(rc.combat.flags & kCombatFlagPlayerEscaped, 0u)
        << "the independent smoked flag must survive even when mugged wins";
    EXPECT_EQ(count_reward_kind(rc.rewards, RewardItemKind::GOLD), 1);
    EXPECT_EQ(count_reward_kind(rc.rewards, RewardItemKind::CARDS), 1);
}

// --- The potion legality trap -------------------------------------------------
//
// RunState.potions[] is populated from real captures by the oracle translator
// (parse_potions, tools/oracle_bridge/translator/src/translate.cpp) for ANY
// potion with a registry row, while the legality gate used to check only "row
// exists + phase". An imported state holding a still-deferred potion would have
// offered USE_POTION, consumed the slot and done nothing -- a wrong answer, not
// a missing feature. The gate now asks potion_use_implemented(), so a deferred
// potion is never offered and, if a caller forces the action anyway, the slot
// survives.

TEST(RunPotion, DeferredPotionsAreNotOfferedInCombat) {
    RunController rc = enter_jaw_worm_combat();
    for (PotionId id : {PotionId::ELIXIR, PotionId::ATTACK_POTION,
                        PotionId::SKILL_POTION, PotionId::POWER_POTION,
                        PotionId::COLORLESS_POTION, PotionId::GAMBLERS_BREW,
                        PotionId::LIQUID_MEMORIES, PotionId::SNECKO_OIL,
                        PotionId::DISTILLED_CHAOS,
                        PotionId::DUPLICATION_POTION}) {
        rc.run.potions[0] = static_cast<uint16_t>(id);
        RunActionMask mask{};
        legal_actions(rc, mask);
        EXPECT_FALSE(mask.can_use_potion[0])
            << "deferred potion id " << static_cast<int>(id)
            << " must not be a legal action";
    }
}

TEST(RunPotion, ImplementedPotionsAreStillOfferedInCombat) {
    RunController rc = enter_jaw_worm_combat();
    for (PotionId id : {PotionId::BLOCK_POTION,           // data program
                        PotionId::FIRE_POTION,            // data program, targeted
                        PotionId::BLOOD_POTION,           // combat native
                        PotionId::BLESSING_OF_THE_FORGE,  // combat native
                        PotionId::SMOKE_BOMB}) {          // run-layer native
        rc.run.potions[0] = static_cast<uint16_t>(id);
        RunActionMask mask{};
        legal_actions(rc, mask);
        EXPECT_TRUE(mask.can_use_potion[0])
            << "implemented potion id " << static_cast<int>(id)
            << " must stay legal";
    }
}

TEST(RunPotion, ForcingADeferredPotionKeepsTheSlot) {
    // Belt and braces: even if a caller submits the action the mask refused,
    // the slot is not consumed and combat state is untouched.
    RunController rc = enter_jaw_worm_combat();
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::ELIXIR);
    const int hp_before = rc.combat.monsters[0].hp;
    const int16_t player_hp_before = rc.combat.player_hp;

    step(rc, make_action(ActionVerb::USE_POTION, 0));

    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::ELIXIR))
        << "the slot must survive an illegal use";
    EXPECT_EQ(rc.combat.monsters[0].hp, hp_before);
    EXPECT_EQ(rc.combat.player_hp, player_hp_before);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
}

// --- DISCARD_POTION ----------------------------------------------------------
//
// The belt's throw-away button. `potion discard i` in a capture is
// CommandExecutor.executePotionCommand's non-use branch, which skips
// `potion.use` and the whole relic `onUsePotion` fan-out and goes straight to
// `topPanel.destroyPotion(slot)` -- `potions.set(slot, new PotionSlot(slot))`,
// TopPanel.java:529-531. So the effect is one emptied slot: no RNG, no stream,
// no hook. It is refused only by AbstractPotion.canDiscard
// (AbstractPotion.java:398-400), which is false inside a We Meet Again dialog
// and true everywhere else -- including in combat, which is why this is not
// gated the way can_use_potion is.

TEST(RunPotionDiscard, DiscardingOutOfCombatEmptiesTheSlotAndMovesNothingElse) {
    RunController rc = run_begin(kSeed, kA20);
    leave_neow(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FEAR_POTION);
    rc.run.potions[1] = static_cast<uint16_t>(PotionId::BLOCK_POTION);
    const RunState before = rc.run;

    RunActionMask mask{};
    legal_actions(rc, mask);
    ASSERT_TRUE(mask.can_discard_potion[0]);
    ASSERT_FALSE(mask.can_use_potion[0])
        << "Fear Potion is not usable out of combat -- discard is a wider door";

    step(rc, make_action(ActionVerb::DISCARD_POTION, 0));

    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE));
    EXPECT_EQ(rc.run.potions[1], static_cast<uint16_t>(PotionId::BLOCK_POTION))
        << "the other slots are untouched, and nothing is compacted";
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    // Every stream, the deck, the purse and the relics are unchanged: a
    // discard is the one potion verb that consumes nothing.
    RunState after = rc.run;
    RunState expected = before;
    expected.potions[0] = static_cast<uint16_t>(PotionId::NONE);
    EXPECT_EQ(std::memcmp(&after, &expected, sizeof(RunState)), 0);
}

TEST(RunPotionDiscard, ADeferredPotionBodyIsStillDiscardable) {
    // The use door is closed on a still-deferred body (potion_use_implemented);
    // the discard door is not, because a discard never runs the body.
    RunController rc = enter_jaw_worm_combat();
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::SNECKO_OIL);
    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_FALSE(mask.can_use_potion[0]);
    EXPECT_TRUE(mask.can_discard_potion[0]);

    step(rc, make_action(ActionVerb::DISCARD_POTION, 0));
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT))
        << "a discard is not a turn action: it neither pumps nor ends the fight";
}

TEST(RunPotionDiscard, ToyOrnithopterDoesNotHealForAPotionThrownAway) {
    // PotionPopUp and CommandExecutor both run the relic onUsePotion fan-out on
    // the USE path only, so the one registered S1 consumer stays silent here.
    // The mirror of RunPotion.ToyOrnithopterTriggersOutsideCombat above, whose
    // USE of the same Fruit Juice in the same slot heals for 10.
    RunController rc = run_begin(kSeed, kA20);
    leave_neow(rc);
    rc.run.hp = 50;
    rc.run.max_hp = 80;
    rc.run.relics[1] =
        RelicSlot{static_cast<uint16_t>(RelicId::TOY_ORNITHOPTER), 0};
    rc.run.relic_count = 2;
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FRUIT_JUICE);

    step(rc, make_action(ActionVerb::DISCARD_POTION, 0));

    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE));
    EXPECT_EQ(rc.run.hp, 50) << "no Toy Ornithopter heal on the discard path";
    EXPECT_EQ(rc.run.max_hp, 80) << "and Fruit Juice's own body did not run";
}

TEST(RunPotionDiscard, AnEmptySlotAndAnOutOfRangeSlotAreBothRefused) {
    RunController rc = run_begin(kSeed, kA20);
    leave_neow(rc);
    const RunState before = rc.run;
    RunActionMask mask{};
    legal_actions(rc, mask);
    for (uint8_t i = 0; i < kPotionCap; ++i) {
        EXPECT_FALSE(mask.can_discard_potion[i])
            << "slot " << static_cast<int>(i) << " is empty at run start";
    }
    step(rc, make_action(ActionVerb::DISCARD_POTION, 0));
    step(rc, make_action(ActionVerb::DISCARD_POTION, kPotionCap + 3));
    EXPECT_EQ(std::memcmp(&rc.run, &before, sizeof(RunState)), 0)
        << "an illegal discard is a non-corrupting no-op";
}

TEST(RunPotionDiscard, WeMeetAgainConfiscatesTheBelt) {
    // AbstractPotion.canDiscard's only clause. The run layer's event dialog is
    // the room's `event`, so the gate is (phase == EVENT_DIALOG && the live
    // event is We Meet Again) -- an ordinary event dialog does not close it.
    RunController rc = run_begin(kSeed, kA20);
    leave_neow(rc);
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FEAR_POTION);
    rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);

    rc.event.event_id = static_cast<uint16_t>(EventId::WE_MEET_AGAIN);
    RunActionMask blocked{};
    legal_actions(rc, blocked);
    EXPECT_FALSE(blocked.can_discard_potion[0]);
    step(rc, make_action(ActionVerb::DISCARD_POTION, 0));
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::FEAR_POTION));

    rc.event.event_id = static_cast<uint16_t>(EventId::LIVING_WALL);
    RunActionMask allowed{};
    legal_actions(rc, allowed);
    EXPECT_TRUE(allowed.can_discard_potion[0]);
    step(rc, make_action(ActionVerb::DISCARD_POTION, 0));
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE));
}

// =============================================================================
// Full floor cycle
// =============================================================================

TEST(FullFloorCycle, MapPickCombatRewardNextFloor) {
    const int64_t seed = find_jaw_worm_seed();
    RunController rc = run_begin(seed, kA20);

    // Neow -> map.
    leave_neow(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.run.floor, 0);

    // Pick a start -> floor 1 Jaw Worm combat.
    uint8_t start = first_start_column(rc);
    step(rc, make_action(ActionVerb::CHOOSE, start));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.run.floor, 1);
    EXPECT_EQ(rc.cur_x, start);
    EXPECT_EQ(rc.monster_cursor, 0);  // not yet removed (removal is on exit)
    // Jaw Worm floor: composition is a single emit (0 miscRng draws); HP + rollMove
    // consume one draw each; deck shuffle consumes one shuffle_rng draw.
    EXPECT_EQ(rc.combat.misc_rng.counter, 0);
    EXPECT_EQ(rc.combat.monster_hp_rng.counter, 1);
    EXPECT_GE(rc.combat.ai_rng.counter, 1);
    EXPECT_EQ(rc.combat.shuffle_rng.counter, 1);
    ASSERT_EQ(rc.combat.monster_count, 1);

    // Persistent fields whose combat ownership differs: gold/potions/deck stay
    // canonical in RunState, relic counters live in CombatState and fold back.
    rc.run.gold = 321;
    rc.run.potions[1] = static_cast<uint16_t>(PotionId::BLOOD_POTION);
    rc.combat.relics[0].counter = 9;
    CardInstance deck_before[10]{};
    std::memcpy(deck_before, rc.run.master_deck, sizeof(deck_before));
    const RunState persistent_before = rc.run;

    // Fight to victory.
    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD))
        << "expected a win (player survived the Jaw Worm)";
    EXPECT_EQ(rc.combat_outcome, static_cast<uint8_t>(RunCombatOutcome::KILLED));

    // Fold-back: mirrored HP/max-HP/relic counters copy out; canonical
    // gold/potions/deck survive byte-for-byte.
    EXPECT_EQ(rc.run.hp, rc.combat.player_hp);
    EXPECT_EQ(rc.run.max_hp, rc.combat.player_max_hp);
    EXPECT_GT(rc.run.hp, 0);
    EXPECT_LE(rc.run.hp, rc.run.max_hp);
    EXPECT_EQ(rc.run.relics[0].counter, 9);
    EXPECT_EQ(rc.run.gold, 321);
    EXPECT_EQ(rc.run.potions[1], static_cast<uint16_t>(PotionId::BLOOD_POTION));
    EXPECT_EQ(std::memcmp(deck_before, rc.run.master_deck, sizeof(deck_before)), 0);
    // Streams a plain monster room's reward assembly does NOT touch survive
    // byte-for-byte; the three it does touch (B4.5) advanced -- exact draw
    // accounting is pinned in combat_rewards_test, this cycle test pins only
    // the attribution (trap 18: normal-room gold is treasureRng, never miscRng
    // or a relicRng draw).
    EXPECT_TRUE(streams_equal(rc.run.monster_rng, persistent_before.monster_rng));
    EXPECT_TRUE(streams_equal(rc.run.event_rng, persistent_before.event_rng));
    EXPECT_TRUE(streams_equal(rc.run.merchant_rng, persistent_before.merchant_rng));
    EXPECT_TRUE(streams_equal(rc.run.relic_rng, persistent_before.relic_rng));
    EXPECT_EQ(rc.run.treasure_rng.counter,
              persistent_before.treasure_rng.counter + 1);  // one gold roll
    EXPECT_GE(rc.run.potion_rng.counter,
              persistent_before.potion_rng.counter + 1);  // unconditional roll
    EXPECT_GT(rc.run.card_rng.counter, persistent_before.card_rng.counter);
    // The assembled screen: gold first, then (maybe) a potion, then the cards.
    ASSERT_GE(rc.rewards.count, 2);
    EXPECT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::GOLD));
    EXPECT_GE(rc.rewards.items[0].gold, 10);
    EXPECT_LE(rc.rewards.items[0].gold, 20);
    EXPECT_EQ(rc.rewards.items[rc.rewards.count - 1].kind,
              static_cast<uint8_t>(RewardItemKind::CARDS));
    EXPECT_EQ(rc.rewards.items[rc.rewards.count - 1].card_count, 3);

    const CombatState reward_boundary = rc.combat;
    RunActionMask reward_mask{};
    legal_actions(rc, reward_mask);
    EXPECT_TRUE(reward_mask.can_proceed);

    // Proceed past the reward screen (claiming nothing) -> back to the map
    // (still floor 1). Unclaimed rewards are abandoned, and the screen clears.
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.rewards.count, 0);
    EXPECT_EQ(rc.run.floor, 1);
    EXPECT_TRUE(streams_equal(rc.combat.monster_hp_rng,
                              reward_boundary.monster_hp_rng));
    EXPECT_TRUE(streams_equal(rc.combat.ai_rng, reward_boundary.ai_rng));
    EXPECT_TRUE(streams_equal(rc.combat.shuffle_rng, reward_boundary.shuffle_rng));
    EXPECT_TRUE(streams_equal(rc.combat.card_random_rng,
                              reward_boundary.card_random_rng));
    EXPECT_TRUE(streams_equal(rc.combat.misc_rng, reward_boundary.misc_rng));

    // Pick the next node -> floor 2. Leaving the floor-1 monster room advances the
    // monster cursor (remove(0)).
    RunActionMask m{};
    legal_actions(rc, m);
    uint8_t next = kMapCols;
    for (uint8_t x = 0; x < kMapCols; ++x) {
        if (m.can_choose_node[x]) { next = x; break; }
    }
    ASSERT_LT(next, kMapCols);
    // Make the destination a room that touches no FLOOR-scoped stream, so the
    // floor-2 boundary is the pristine post-increment reseed, independently
    // comparable stream-by-stream. (A Shop's build is all run-scoped streams;
    // an Event room would draw eventRng.)
    rc.run.map[run_state_map_index(next, 1)].room_type =
        static_cast<uint8_t>(RoomType::Shop);
    step(rc, make_action(ActionVerb::CHOOSE, next));
    EXPECT_EQ(rc.run.floor, 2);
    EXPECT_EQ(rc.monster_cursor, 1);  // floor-1 monster room removed on exit
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::SHOP));
    const RngStream floor2 = floor_stream(seed, 2);
    EXPECT_TRUE(streams_equal(rc.combat.monster_hp_rng, floor2));
    EXPECT_TRUE(streams_equal(rc.combat.ai_rng, floor2));
    EXPECT_TRUE(streams_equal(rc.combat.shuffle_rng, floor2));
    EXPECT_TRUE(streams_equal(rc.combat.card_random_rng, floor2));
    EXPECT_TRUE(streams_equal(rc.combat.misc_rng, floor2));
}

// =============================================================================
// Map-choice legality + non-combat routing + batch heterogeneity
// =============================================================================

TEST(MapChoice, LegalColumnsMatchGeneratedEdges) {
    RunController rc = run_begin(kSeed, kA20);
    leave_neow(rc);
    RunActionMask m{};
    legal_actions(rc, m);
    // Floor 0: legal starts == connected row-0 nodes.
    for (int x = 0; x < kMapCols; ++x) {
        const bool connected = rc.run.map[run_state_map_index(x, 0)].edges != 0;
        EXPECT_EQ(m.can_choose_node[x], connected) << "row-0 col " << x;
    }
}

TEST(RoomRouting, ShopRoomsOpenTheMerchant) {
    // The last non-combat map kind that used to park now has content: a Shop
    // node builds its merchant on entry and hands the player the shop floor.
    // Every remaining ROOM_UNIMPLEMENTED stall is a MISSING ENCOUNTER or a
    // missing event body, not a missing room kind -- the QuestionMarkRoom suite
    // and the event-framework suite cover those.
    RunController rc = run_begin(kSeed, kA20);
    for (int x = 0; x < kMapCols; ++x) {
        rc.run.map[run_state_map_index(x, 0)].room_type =
            static_cast<uint8_t>(RoomType::Shop);
    }
    leave_neow(rc);
    uint8_t x = first_start_column(rc);
    step(rc, make_action(ActionVerb::CHOOSE, x));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::SHOP));
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Shop));
    // Reseed still happened, and the merchant left it alone.
    EXPECT_TRUE(streams_equal(rc.combat.misc_rng, floor_stream(kSeed, 1)));
}

TEST(RoomRouting, RestRoomsOpenRestSiteMenu) {
    RunController rc = run_begin(kSeed, kA20);
    for (int x = 0; x < kMapCols; ++x) {
        rc.run.map[run_state_map_index(x, 0)].room_type =
            static_cast<uint8_t>(RoomType::Rest);
    }
    leave_neow(rc);
    const uint8_t x = first_start_column(rc);
    step(rc, make_action(ActionVerb::CHOOSE, x));

    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::REST_SITE));
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Rest));
    EXPECT_EQ(rc.rest.screen, static_cast<uint8_t>(RestScreen::MENU));
    EXPECT_TRUE(streams_equal(rc.combat.misc_rng, floor_stream(kSeed, 1)));
}

TEST(BatchHeterogeneity, MixedPhasesStepIndependently) {
    // A batch with different phases and verbs: NEOW CHOOSE, MAP CHOOSE, combat
    // PLAY_CARD/END_TURN, and non-combat USE_POTION.
    const int64_t jw = find_jaw_worm_seed();
    RunController a = run_begin(kSeed, kA20);                 // NEOW
    a.neow.screen = static_cast<uint8_t>(NeowScreen::DONE);   // ready to leave
    RunController b = run_begin(kSeed, kA20);
    leave_neow(b);                                        // MAP_CHOICE
    RunController c = run_begin(jw, kA20);
    leave_neow(c);
    step(c, make_action(ActionVerb::CHOOSE, first_start_column(c)));  // COMBAT
    RunController d = run_begin(kSeed + 1, kA20);
    leave_neow(d);
    d.run.hp = 60;
    d.run.potions[0] = static_cast<uint16_t>(PotionId::FRUIT_JUICE);

    RunController runs[4] = {a, b, c, d};
    RunActionMask mb{};
    legal_actions(runs[1], mb);
    uint8_t bx = 0;
    for (uint8_t x = 0; x < kMapCols; ++x) if (mb.can_choose_node[x]) { bx = x; break; }
    RunActionMask mc{};
    legal_actions(runs[2], mc);
    Action ca = make_action(ActionVerb::END_TURN);
    for (int i = 0; i < kHandCap; ++i)
        for (int t = 0; t < kMonsterCap; ++t)
            if (mc.combat.can_play_target[i][t]) {
                ca = make_action(ActionVerb::PLAY_CARD, static_cast<uint8_t>(i),
                                 static_cast<uint8_t>(t));
                i = kHandCap; break;
            }
    Action acts[4] = {kProceed, make_action(ActionVerb::CHOOSE, bx), ca,
                      make_action(ActionVerb::USE_POTION, 0)};
    StepResult res[4];
    advance(std::span<RunController>(runs, 4), std::span<const Action>(acts, 4),
            std::span<StepResult>(res, 4));

    EXPECT_EQ(runs[0].phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));  // a proceeded
    EXPECT_EQ(runs[1].run.floor, 1);                                      // b advanced a floor
    // c stepped its combat (still COMBAT or moved to reward if it ended fast).
    EXPECT_TRUE(runs[2].phase == static_cast<uint8_t>(RunPhase::COMBAT) ||
                runs[2].phase == static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(runs[3].run.hp, 65);
    EXPECT_EQ(runs[3].run.max_hp, 80);  // Fruit Juice +5 over the ascension-20 75
    EXPECT_EQ(runs[3].run.potions[0], static_cast<uint16_t>(PotionId::NONE));
}

// =============================================================================
// ?-room resolution (B4.10, event_framework.hpp)
// =============================================================================

// The first committed eventRng float of a fresh run of `seed` (event_rng is
// untouched between run_begin and the first ? room). With the fresh pity
// values (0.1/0.03/0.02) the roll table is MONSTER on slots 0-9, SHOP on
// 10-12, TREASURE on 13-14, EVENT on 15-99, so the float alone predicts the
// resolved kind.
float first_event_roll_float(int64_t seed) {
    RngStream s = from_seed(seed);
    return random(s);
}

// Find a seed whose first ?-roll float lands in [lo, hi). (A joint condition
// on the monster list would be unsatisfiable, not just rare: event_rng and
// monster_rng are both fresh Random(seed), so the first event float and the
// first encounter roll are the SAME underlying draw.)
int64_t find_event_roll_seed(float lo, float hi) {
    for (int64_t s = 1; s < 60000; ++s) {
        const float f = first_event_roll_float(s);
        if (f >= lo && f < hi) return s;
    }
    ADD_FAILURE() << "no seed with first event roll in [" << lo << ", " << hi
                  << ") found in range";
    return 1;
}

// Advance rc's event_rng until its NEXT float lands in [lo, hi) -- the state
// several earlier ? rooms would have left behind (pity fields are set by the
// caller). Returns false if no such point exists in a generous window.
bool warm_event_rng_until_next_roll_in(RunController& rc, float lo, float hi) {
    for (int i = 0; i < 4096; ++i) {
        RngStream probe = rc.run.event_rng;
        const float f = random(probe);
        if (f >= lo && f < hi) return true;
        rc.run.event_rng = probe;  // consume the draw and keep looking
    }
    return false;
}

// Walk a fresh run into a row-0 ? room (every row-0 node forced to Event).
RunController enter_question_mark(int64_t seed) {
    RunController rc = run_begin(seed, kA20);
    for (int x = 0; x < kMapCols; ++x) {
        rc.run.map[run_state_map_index(x, 0)].room_type =
            static_cast<uint8_t>(RoomType::Event);
    }
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    return rc;
}

int total_pool_bits(const RunState& rs) {
    int n = 0;
    for (int i = 0; i < 16; ++i) n += (rs.event_membership >> i) & 1;
    for (int i = 0; i < 16; ++i) n += (rs.special_membership >> i) & 1;
    for (int i = 0; i < 8; ++i) n += (rs.shrine_membership >> i) & 1;
    return n;
}

// THE regression guard for the B4.10 invariant: across a full
// ?-resolves-to-event flow -- room-type roll, selection, pool removal --
// eventRng is byte-identical to the pre-entry stream advanced by EXACTLY one
// draw. The selection draws happened on a discarded throwaway stream.
TEST(QuestionMarkRoom, EventRngAdvancesByExactlyOneAcrossFullEventResolve) {
    const int64_t seed = find_event_roll_seed(0.15f, 1.0f);  // EVENT
    RunController rc = run_begin(seed, kA20);
    const RngStream before = rc.run.event_rng;
    ASSERT_EQ(before.counter, 0);
    const int pool_before = total_pool_bits(rc.run);
    ASSERT_EQ(pool_before, 11 + 6 + 13);  // A20: NoteForYourself absent

    for (int x = 0; x < kMapCols; ++x) {
        rc.run.map[run_state_map_index(x, 0)].room_type =
            static_cast<uint8_t>(RoomType::Event);
    }
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));

    // Resolved to an event. Implemented bodies open a dialog; later bodies
    // park. Both happen only after the exact selection bookkeeping.
    const RunPhase expected_phase =
        event_dialog_impl(rc.event.event_id) == nullptr
            ? RunPhase::ROOM_UNIMPLEMENTED
            : RunPhase::EVENT_DIALOG;
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(expected_phase));
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Event));
    EXPECT_NE(rc.event.event_id, 0);

    // event_rng == before + exactly one committed draw; counter up by 1.
    RngStream expect = before;
    (void)random(expect);  // the one room-type roll
    EXPECT_TRUE(streams_equal(rc.run.event_rng, expect));
    EXPECT_EQ(rc.run.event_rng.counter, before.counter + 1);

    // Pool removal committed exactly once, and the fired flag matches the id.
    EXPECT_EQ(total_pool_bits(rc.run), pool_before - 1);
    EXPECT_EQ(rc.run.event_flags, 1u << (rc.event.event_id - 1u));

    // Pity floats: an EVENT result misses all three -- float ramps, bit-exact.
    EXPECT_EQ(rc.run.event_pity_monster, 0.1f + 0.1f);
    EXPECT_EQ(rc.run.event_pity_shop, 0.03f + 0.03f);
    EXPECT_EQ(rc.run.event_pity_treasure, 0.02f + 0.02f);
}

TEST(QuestionMarkRoom, SsserpentHeadFiresBeforeEveryResolvedRoomKind) {
    struct Case {
        float lo;
        float hi;
        RoomType resolved;
    };
    const Case cases[] = {
        {0.00f, 0.10f, RoomType::Monster},
        {0.10f, 0.13f, RoomType::Shop},
        {0.13f, 0.15f, RoomType::Treasure},
        {0.15f, 1.00f, RoomType::Event},
    };
    for (const Case& tc : cases) {
        SCOPED_TRACE(static_cast<int>(tc.resolved));
        const int64_t seed = find_event_roll_seed(tc.lo, tc.hi);
        RunController rc = run_begin(seed, kA20);
        const int32_t gold_before = rc.run.gold;
        ASSERT_LT(rc.run.relic_count, kRelicCap);
        rc.run.relics[rc.run.relic_count].relic_id =
            static_cast<uint16_t>(RelicId::SSSERPENT_HEAD);
        ++rc.run.relic_count;
        for (int x = 0; x < kMapCols; ++x) {
            rc.run.map[run_state_map_index(x, 0)].room_type =
                static_cast<uint8_t>(RoomType::Event);
        }
        leave_neow(rc);
        step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));

        // onEnterRoom receives the original EventRoom before generateRoom
        // replaces it, so the gain is independent of the resolved kind.
        EXPECT_EQ(rc.run.gold, gold_before + 50);
        EXPECT_EQ(rc.room_type, static_cast<uint8_t>(tc.resolved));
    }
}

// THE double-fire regression. on_player_entry RECURSES for a ? that rolls
// Monster/Treasure/Shop, but the game fires onEnterRoom exactly once per
// nextRoomTransition, against the PRE-roll EventRoom (AbstractDungeon.java:
// 1755-1757, before the roll at :1766-1781 and setCurrMapNode at :1783).
// Maw Bank has no room-type gate, so wiring it at the justEnteredRoom site --
// or at the top of the recursive entry -- would pay 12 gold TWICE on a
// ?->Shop. Exactly 12, on every resolved kind.
TEST(QuestionMarkRoom, MawBankPaysExactlyTwelveOnceAcrossEveryResolvedKind) {
    struct Case {
        float lo;
        float hi;
        RoomType resolved;
    };
    const Case cases[] = {
        {0.00f, 0.10f, RoomType::Monster},
        {0.10f, 0.13f, RoomType::Shop},
        {0.13f, 0.15f, RoomType::Treasure},
        {0.15f, 1.00f, RoomType::Event},
    };
    for (const Case& tc : cases) {
        SCOPED_TRACE(static_cast<int>(tc.resolved));
        const int64_t seed = find_event_roll_seed(tc.lo, tc.hi);
        RunController rc = run_begin(seed, kA20);
        const int32_t gold_before = rc.run.gold;
        set_run_relics(rc, {RelicId::MAW_BANK});
        for (int x = 0; x < kMapCols; ++x) {
            rc.run.map[run_state_map_index(x, 0)].room_type =
                static_cast<uint8_t>(RoomType::Event);
        }
        leave_neow(rc);
        step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));

        EXPECT_EQ(rc.run.gold, gold_before + 12);
        EXPECT_EQ(rc.room_type, static_cast<uint8_t>(tc.resolved));
        // Entry does NOT use the relic up -- only onSpendGold does
        // (MawBank.java:38-44), and that is a ShopRoom-only fan-out.
        EXPECT_EQ(rc.run.relics[0].counter, 0);
    }
}

// MawBank.onEnterRoom (MawBank.java:31-36) tests nothing about the room, and
// the fan-out at AbstractDungeon.java:1755-1757 is likewise unconditional, so
// every STATIC map node pays too -- the half B4.10 left open.
TEST(RoomEntryRelics, MawBankPaysOnEveryStaticRoomKind) {
    for (const RoomType room :
         {RoomType::Monster, RoomType::Elite, RoomType::Rest, RoomType::Shop,
          RoomType::Treasure}) {
        SCOPED_TRACE(static_cast<int>(room));
        RunController rc = run_begin(kSeed, kA20);
        const int32_t gold_before = rc.run.gold;
        set_run_relics(rc, {RelicId::MAW_BANK});
        for (int x = 0; x < kMapCols; ++x) {
            rc.run.map[run_state_map_index(x, 0)].room_type =
                static_cast<uint8_t>(room);
        }
        leave_neow(rc);
        step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
        EXPECT_EQ(rc.room_type, static_cast<uint8_t>(room));
        EXPECT_EQ(rc.run.gold, gold_before + 12);
    }
}

// The boss node is reached by DungeonMap.java:77-87 assigning `nextRoom` a
// MonsterRoomBoss and calling nextRoomTransitionStart(), so it runs the same
// unconditional onEnterRoom loop. MonsterRoomBoss is NOT exempt.
TEST(RoomEntryRelics, MawBankPaysOnTheBossEntry) {
    RunController rc = run_begin(kSeed, kA20);
    const int32_t gold_before = rc.run.gold;
    set_run_relics(rc, {RelicId::MAW_BANK});
    leave_neow(rc);
    next_room_transition(rc, 0, /*to_boss=*/true);
    ASSERT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Boss));
    EXPECT_EQ(rc.run.gold, gold_before + 12);
}

// EternalFeather.onEnterRoom (EternalFeather.java:29-35) heals
// (masterDeck.size() / 5) * 3 on entering a RestRoom. It is NOT a campfire
// option: the fan-out runs at AbstractDungeon.java:1755-1757, before
// setCurrMapNode (:1783) and before RestRoom.onPlayerEntry (:1800) builds the
// CampfireUI (RestRoom.java:33-43). So the HP is already there when the menu
// opens, and it lands whether or not the player then rests.
TEST(RoomEntryRelics, EternalFeatherHealsBeforeTheCampfireMenuOpens) {
    RunController rc = run_begin(kSeed, kA20);
    set_run_relics(rc, {RelicId::ETERNAL_FEATHER});
    for (int x = 0; x < kMapCols; ++x) {
        rc.run.map[run_state_map_index(x, 0)].room_type =
            static_cast<uint8_t>(RoomType::Rest);
    }
    leave_neow(rc);
    rc.run.hp = 20;
    // A20 Ironclad: the 10-card starter plus Ascender's Bane.
    ASSERT_EQ(rc.run.master_deck_count, 11);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));

    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::REST_SITE));
    ASSERT_EQ(rc.rest.screen, static_cast<uint8_t>(RestScreen::MENU));
    EXPECT_EQ(rc.run.hp, 26);  // 11 / 5 * 3 == 6, integer division FIRST
}

// A ? can never roll REST (EventHelper.RoomResult has no REST arm), so the
// pre-roll/post-roll distinction is invisible for this relic specifically --
// but it shares the fan-out with Maw Bank, for which it is not. Pinned so the
// gate stays on the ARRIVING map node's kind and never on a resolved one.
TEST(RoomEntryRelics, EternalFeatherDoesNotHealOnANonRestEntry) {
    for (const RoomType room :
         {RoomType::Monster, RoomType::Event, RoomType::Shop,
          RoomType::Treasure}) {
        SCOPED_TRACE(static_cast<int>(room));
        RunController rc = run_begin(kSeed, kA20);
        set_run_relics(rc, {RelicId::ETERNAL_FEATHER});
        for (int x = 0; x < kMapCols; ++x) {
            rc.run.map[run_state_map_index(x, 0)].room_type =
                static_cast<uint8_t>(room);
        }
        leave_neow(rc);
        rc.run.hp = 20;
        step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
        EXPECT_EQ(rc.run.hp, 20);
    }
}

// Two floors, one relic: the gain repeats until a shop purchase sets the
// counter to -2 (dispatch_relics_on_spend_gold, relics/relic_pickup.hpp), and
// then never again on any later entry.
TEST(RoomEntryRelics, MawBankRepeatsEveryFloorUntilUsedUp) {
    RunController rc = run_begin(kSeed, kA20);
    const int32_t gold_before = rc.run.gold;
    set_run_relics(rc, {RelicId::MAW_BANK});
    for (int x = 0; x < kMapCols; ++x) {
        rc.run.map[run_state_map_index(x, 0)].room_type =
            static_cast<uint8_t>(RoomType::Rest);
        rc.run.map[run_state_map_index(x, 1)].room_type =
            static_cast<uint8_t>(RoomType::Rest);
    }
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.run.gold, gold_before + 12);

    rc.run.relics[0].counter = -2;  // a shop coin was spent
    next_room_transition(rc, rc.cur_x, /*to_boss=*/false);
    EXPECT_EQ(rc.run.gold, gold_before + 12);
}

// A ? that rolls MONSTER becomes a REAL MonsterRoom (generateRoom,
// AbstractDungeon.java:1823-1840): it consumes monsterList and -- the bug this
// task fixed -- its EXIT advances monster_cursor even though the static map
// node still says Event (nextRoomTransition tests the resolved room object,
// AbstractDungeon.java:1701-1707, never the map symbol).
TEST(QuestionMarkRoom, MonsterRollEntersRealCombatAndConsumesMonsterList) {
    // Jaw Worm first (winnable by play_out_combat), then warm the event
    // stream to a MONSTER-column float -- the two cannot be seed-selected
    // jointly (see warm_event_rng_until_next_roll_in).
    const int64_t seed = find_jaw_worm_seed();
    RunController rc = run_begin(seed, kA20);
    ASSERT_TRUE(warm_event_rng_until_next_roll_in(rc, 0.0f, 0.10f));
    const RngStream before = rc.run.event_rng;
    for (int x = 0; x < kMapCols; ++x) {
        rc.run.map[run_state_map_index(x, 0)].room_type =
            static_cast<uint8_t>(RoomType::Event);
    }
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));

    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Monster));
    ASSERT_EQ(rc.combat.monster_count, 1);  // the Jaw Worm the seed guarantees
    EXPECT_EQ(rc.monster_cursor, 0);        // consumed on EXIT, not entry

    // Exactly the one roll draw; no selection happened on a MONSTER resolve.
    RngStream expect = before;
    (void)random(expect);
    EXPECT_TRUE(streams_equal(rc.run.event_rng, expect));

    // Pity: MONSTER hit resets monster, misses shop/treasure.
    EXPECT_EQ(rc.run.event_pity_monster, 0.1f);
    EXPECT_EQ(rc.run.event_pity_shop, 0.03f + 0.03f);
    EXPECT_EQ(rc.run.event_pity_treasure, 0.02f + 0.02f);
    // No selection => no pool removal, no fired flag.
    EXPECT_EQ(total_pool_bits(rc.run), 11 + 6 + 13);
    EXPECT_EQ(rc.run.event_flags, 0u);

    // Win, proceed, pick the next node: leaving the resolved MonsterRoom
    // consumes monsterList (cursor 0 -> 1) although rs.map still says Event.
    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.run.map[run_state_map_index(rc.cur_x, 0)].room_type,
              static_cast<uint8_t>(RoomType::Event));  // static map unchanged
    RunActionMask m{};
    legal_actions(rc, m);
    uint8_t next = kMapCols;
    for (uint8_t x = 0; x < kMapCols; ++x) {
        if (m.can_choose_node[x]) { next = x; break; }
    }
    ASSERT_LT(next, kMapCols);
    rc.run.map[run_state_map_index(next, 1)].room_type =
        static_cast<uint8_t>(RoomType::Shop);  // draw-free destination
    step(rc, make_action(ActionVerb::CHOOSE, next));
    EXPECT_EQ(rc.monster_cursor, 1);
    EXPECT_EQ(rc.elite_cursor, 0);
}

TEST(QuestionMarkRoom, ShopRollOpensTheMerchantAndResetsShopPity) {
    const int64_t seed = find_event_roll_seed(0.10f, 0.13f);  // SHOP
    RunController rc = enter_question_mark(seed);
    // generateRoom hands back a REAL ShopRoom, so a ?->Shop is the same room
    // the map's own $ node builds -- same phase, same merchant.
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::SHOP));
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Shop));
    EXPECT_EQ(rc.shop.screen, static_cast<uint8_t>(ShopScreenKind::MENU));
    EXPECT_EQ(rc.run.merchant_rng.counter, 16);  // one full stock build
    EXPECT_EQ(rc.run.event_pity_monster, 0.1f + 0.1f);
    EXPECT_EQ(rc.run.event_pity_shop, 0.03f);        // hit -> reset
    EXPECT_EQ(rc.run.event_pity_treasure, 0.02f + 0.02f);
    EXPECT_EQ(rc.run.event_rng.counter, 1);
    EXPECT_EQ(total_pool_bits(rc.run), 11 + 6 + 13);  // no selection
}

TEST(QuestionMarkRoom, TreasureRollOpensTheChestFlow) {
    const int64_t seed = find_event_roll_seed(0.13f, 0.15f);  // TREASURE
    RunController rc = run_begin(seed, kA20);
    const int32_t treasure_counter_before = rc.run.treasure_rng.counter;
    for (int x = 0; x < kMapCols; ++x) {
        rc.run.map[run_state_map_index(x, 0)].room_type =
            static_cast<uint8_t>(RoomType::Event);
    }
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));

    // A real TreasureRoom: the B4.7 chest constructor ran (2 treasureRng
    // draws) and the open/skip screen is up.
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::TREASURE_ROOM));
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Treasure));
    EXPECT_EQ(rc.run.treasure_rng.counter, treasure_counter_before + 2);
    EXPECT_EQ(rc.run.event_pity_treasure, 0.02f);    // hit -> reset
    EXPECT_EQ(rc.run.event_pity_monster, 0.1f + 0.1f);
    EXPECT_EQ(rc.run.event_pity_shop, 0.03f + 0.03f);
    EXPECT_EQ(rc.run.event_rng.counter, 1);
}


// =============================================================================
// B3.11 stage D -- the combat-gold accumulator's run-layer settlement
// =============================================================================
//
// Hand of Greed is the first in-combat gold PRODUCER. CombatState keeps no
// purse, so the payout accrues in CombatState.combat_gold and the run layer
// settles it at fold_back_combat -- the SINGLE combat fold-back -- through
// gain_gold, the one run-layer gold door. These tests pin "exactly once", "on
// every combat-end path", and "through the door, not around it"; the combat-side
// accrual itself is covered by card_colorless_rares_test.

TEST(RunCombatGold, VictoryFoldSettlesTheAccumulatorExactlyOnce) {
    RunController rc = enter_jaw_worm_combat();
    ASSERT_EQ(rc.run.gold, 99);
    rc.combat.combat_gold = 45;

    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(rc.run.gold, 99 + 45) << "settled at the fold-back";
    EXPECT_EQ(rc.combat.combat_gold, 0)
        << "zeroed as it settles, so a second fold cannot double-count";
}

TEST(RunCombatGold, DefeatFoldSettlesToo) {
    RunController rc = enter_jaw_worm_combat();
    ASSERT_EQ(rc.run.gold, 99);
    rc.combat.combat_gold = 30;
    // Arrange a loss: the Jaw Worm's next attack is lethal.
    rc.combat.player_hp = 1;
    rc.combat.player_block = 0;
    for (int turn = 0; turn < 8 &&
                       rc.phase == static_cast<uint8_t>(RunPhase::COMBAT);
         ++turn) {
        step(rc, make_action(ActionVerb::END_TURN));
    }
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    EXPECT_EQ(rc.combat_outcome, static_cast<uint8_t>(RunCombatOutcome::DEFEAT));
    EXPECT_EQ(rc.run.gold, 99 + 30)
        << "the game gains this gold DURING combat, so a defeat keeps it";
    EXPECT_EQ(rc.combat.combat_gold, 0);
}

TEST(RunCombatGold, ZeroAccumulatorLeavesThePurseUntouched) {
    RunController rc = enter_jaw_worm_combat();
    ASSERT_EQ(rc.run.gold, 99);
    ASSERT_EQ(rc.combat.combat_gold, 0);
    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(rc.run.gold, 99) << "no producer ran, no gain_gold call";
}

// Routed through the DOOR, not around it: Ectoplasm returns from gainGold before
// the `+=` (AbstractPlayer.gainGold:719-737), so a settlement that wrote
// rs.gold directly would silently ignore a registered boss relic.
TEST(RunCombatGold, EctoplasmSuppressesTheSettlement) {
    RunController rc = enter_jaw_worm_combat();
    ASSERT_EQ(rc.run.gold, 99);
    ASSERT_LT(rc.run.relic_count, kRelicCap);
    rc.run.relics[rc.run.relic_count].relic_id =
        static_cast<uint16_t>(RelicId::ECTOPLASM);
    ++rc.run.relic_count;
    rc.combat.combat_gold = 45;

    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(rc.run.gold, 99) << "Ectoplasm suppresses the gain entirely";
    EXPECT_EQ(rc.combat.combat_gold, 0)
        << "the accumulator is consumed either way -- the suppression happens "
           "inside the door";
}

// The scripted walk a fuzz seed sweep once dead-ended on, pinned by name.
//
// Seed 42 at ascension 20 takes Neow's first blessing (a master-deck grid
// payout), picks deck row 4, presses the finished-payout button onto the map,
// and steps into the floor-1 monster room. Four presses, each of which the mask
// that offered it must call legal -- and then the assertion that matters: the
// combat the fourth press opens has to offer SOMETHING. END_TURN is legal in
// every WAITING_ON_USER combat by construction (legal_actions in advance.cpp
// ends with `out.can_end_turn = waiting`), so an empty legal set here is never
// a content gap; it means the CombatState the run layer just built does not
// read back as the CombatState the mask code expects.
//
// That is not hypothetical. One build tree's static library carried three
// monster translation units compiled against an older CombatState layout
// (MonsterState 116 bytes rather than 212), so a monster's init wrote across
// the live combat and the mask came back completely empty. The sources were
// fine; only the objects were stale. The fuzz seed sweep is the broad net that
// found it, but it reports a policy trajectory rather than a scenario -- this
// test names the scenario, and it fails the same way for any future cause.
TEST(FirstCombatEntry, NeowPayoutWalkOntoFloorOneLeavesALiveCombatMask) {
    constexpr int64_t kSweepSeed = 42;
    constexpr uint16_t kGridRow = 4;
    constexpr uint8_t kMapColumn = 1;

    RunController rc = run_begin(kSweepSeed, kA20);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::NEOW));
    ASSERT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::BLESSING));

    RunActionMask m{};
    legal_actions(rc, m);
    ASSERT_TRUE(m.can_choose_neow_option[0]);
    step(rc, make_action(ActionVerb::CHOOSE, 0));

    // Category 0's grid payouts (remove / upgrade / transform one card) open the
    // master-deck grid; row 4 is one of the starting Strikes.
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::NEOW));
    ASSERT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::GRID));
    legal_actions(rc, m);
    ASSERT_TRUE(m.can_choose_master_deck[kGridRow]);
    step(rc, make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(kGridRow)));

    // One pick was all the grid asked for, so the payout is finished and the
    // next press is the one that opens the map.
    ASSERT_EQ(rc.neow.screen, static_cast<uint8_t>(NeowScreen::DONE));
    legal_actions(rc, m);
    ASSERT_TRUE(m.can_proceed);
    step(rc, kProceed);

    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    legal_actions(rc, m);
    ASSERT_TRUE(m.can_choose_node[kMapColumn]);
    step(rc, make_action(ActionVerb::CHOOSE, kMapColumn));

    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.run.floor, 1);
    ASSERT_EQ(rc.combat.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    EXPECT_GE(rc.combat.monster_count, 1);

    legal_actions(rc, m);
    EXPECT_TRUE(m.combat.can_end_turn)
        << "END_TURN is legal in every WAITING_ON_USER combat; a false here "
           "means the combat state feeding the mask is not the one that was "
           "built";
    bool anything_legal = m.combat.can_end_turn;
    for (int i = 0; i < kHandCap; ++i) {
        anything_legal =
            anything_legal || m.combat.can_play[i] || m.combat.can_choose[i];
        for (int t = 0; t < kMonsterCap; ++t) {
            anything_legal = anything_legal || m.combat.can_play_target[i][t];
        }
    }
    EXPECT_TRUE(anything_legal)
        << "a live combat with no legal action at all is the run-level dead "
           "end this walk reproduces";
}

// =============================================================================
// The Act-1 boss victory terminal (stage-b-design §1.1)
// =============================================================================
//
// "The run terminates when the act-1 boss's combat rewards are claimed"
// (stage-b-design §1.1's S2+ boundary). In the game the boss reward's Proceed
// never opens the map: at a COMBAT_REWARD in a MonsterRoomBoss it goes to the
// boss chest (ProceedButton.update, ProceedButton.java:111-113 ->
// goToTreasureRoom :179-187, a TreasureRoomBoss), and from there to the next
// act -- both S2 content. So the sim's boss-reward Proceed is the run's
// VICTORY terminal: RUN_OVER, with run_is_victory() telling it apart from a
// death by (room_type == Boss, combat_outcome == KILLED).
//
// The regression these tests pin was found by a 300-seed always_event fuzz
// probe (seed 116): the proceed used to route to MAP_CHOICE, where the boss
// column has no outgoing map edges, so the run advertised an EMPTY action
// mask while claiming not to be terminal -- the soak's no_legal_moves.

RunController enter_boss_combat(int64_t seed) {
    RunController rc = run_begin(seed, kA20);
    leave_neow(rc);
    // Jump the floor loop straight onto the boss edge -- next_room_transition
    // is public precisely so a directed test can aim a specific room.
    next_room_transition(rc, 0, /*to_boss=*/true);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Boss));
    return rc;
}

// Make the kill mechanical without bypassing the combat layer: every member
// dies to the first attack, and the win still runs the whole battle-over path
// (onVictory relics, fold-back, reward assembly).
void weaken_all_monsters(RunController& rc) {
    for (uint8_t i = 0; i < rc.combat.monster_count; ++i) {
        rc.combat.monsters[i].hp = 1;
        rc.combat.monsters[i].block = 0;
    }
}

TEST(BossVictory, BossRewardProceedIsTheRunOverVictoryTerminal) {
    RunController rc = enter_boss_combat(kSeed);
    weaken_all_monsters(rc);
    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    ASSERT_EQ(rc.combat_outcome, static_cast<uint8_t>(RunCombatOutcome::KILLED));
    EXPECT_FALSE(run_is_victory(rc))
        << "the reward screen is still up -- not terminal yet";

    RunActionMask m{};
    legal_actions(rc, m);
    ASSERT_TRUE(m.can_proceed);
    const StepResult res =
        step_with_result(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));

    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER))
        << "the boss reward's proceed must END the run (stage-b-design §1.1), "
           "not open a map the boss column has no edges into";
    EXPECT_TRUE(run_is_victory(rc));
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Boss));
    EXPECT_EQ(rc.combat_outcome, static_cast<uint8_t>(RunCombatOutcome::KILLED));
    EXPECT_TRUE(res.terminal);
    EXPECT_EQ(res.reward, 1.0f)
        << "the run-level win is the +1 analogue of the DEFEAT path's -1";
    EXPECT_EQ(rc.rewards.count, 0) << "the screen cleared on the way out";
}

TEST(BossVictory, VictoryMaskIsEmptyBecauseRunOver) {
    RunController rc = enter_boss_combat(kSeed);
    weaken_all_monsters(rc);
    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));

    // The mask is empty BECAUSE the phase is terminal -- byte-identical to a
    // value-initialized mask but for the phase echo.
    RunActionMask m{};
    legal_actions(rc, m);
    RunActionMask none{};
    none.phase = rc.phase;
    EXPECT_EQ(std::memcmp(&m, &none, sizeof m), 0)
        << "RUN_OVER offers nothing; any set flag here is a bug";

    // And a step against the terminal is a non-corrupting no-op that stays
    // terminal (the parked/terminal contract fill_run_result implements).
    const StepResult res = step_with_result(rc, make_action(ActionVerb::CHOOSE, 0));
    EXPECT_TRUE(res.terminal);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    EXPECT_TRUE(run_is_victory(rc));
}

TEST(BossVictory, ADeathAtTheBossIsNotAVictory) {
    RunController rc = enter_boss_combat(kSeed);
    // Arrange a loss: the boss's first attack is lethal.
    rc.combat.player_hp = 1;
    rc.combat.player_block = 0;
    for (int turn = 0;
         turn < 12 && rc.phase == static_cast<uint8_t>(RunPhase::COMBAT);
         ++turn) {
        step(rc, make_action(ActionVerb::END_TURN));
    }
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    EXPECT_EQ(rc.combat_outcome, static_cast<uint8_t>(RunCombatOutcome::DEFEAT));
    EXPECT_FALSE(run_is_victory(rc))
        << "a DEFEAT at the boss shares the phase but never the victory read";
}

}  // namespace
}  // namespace sts::engine
