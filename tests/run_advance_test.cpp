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
#include "sts/engine/monster_dispatch.hpp"  // kMonsterAscension (ctor-draw derivations)
#include "sts/engine/monster_looter.hpp"  // kLooterGoldAmt / looter_steal_count
#include "sts/registry/game_ids.hpp"       // monster_from_game_id (ctor-draw derivations)
#include "sts/registry/monster_table.hpp"  // monster_def / MonsterRollTiming
#include "sts/engine/potions.hpp"
#include "sts/engine/power_hooks.hpp"  // dispatch_was_hp_lost (Centennial Puzzle)
#include "sts/engine/public_view.hpp"  // second_boss_reserved (A20 double boss)
#include "sts/registry/encounter_table.hpp"  // encounter_by_game_id (same test)
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

PotionId hand_unlimited_potion_roll(RngStream& rng) {
    // AbstractDungeon.returnRandomPotion() -- the NO-ARG overload -- is
    // returnRandomPotion(false) (AbstractDungeon.java:825-827): d100 tier roll,
    // then reject-sample by rarity alone. No spam check, no first-candidate
    // discard, and Fruit Juice is a legal result. limited therefore changes the
    // DRAW COUNT, not just the identity: the limited loop always redraws at
    // least once.
    const PotionRarity tier = potion_tier_for_roll(random(rng, 0, 99));
    PotionId candidate =
        static_cast<PotionId>(random(rng, kPotionPoolSize - 1) + 1);
    while (potion_def(candidate)->rarity != tier) {
        candidate = static_cast<PotionId>(random(rng, kPotionPoolSize - 1) + 1);
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

// The opcode bodies are internal to sts_engine, so a test drives them the way
// the pump does: build the queue item and execute it. Enough damage to make the
// HP write lethal, which is what AbstractPlayer.damage's revive block reads.
void deal_lethal_to_player(CombatState& s) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    it.src = 0;
    it.tgt = kActorPlayer;
    it.amount = 9999;
    execute_opcode(s, it);
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

// The act boss's EncounterId, mirrored into save-parity state. The translator
// writes `boss_ids[act-1]` from the capture's `act_boss` through the encounter
// registry (the "Translator: real act_boss" ledger row), and the differ
// compares the field -- so the run layer must record its own rolled boss
// (`boss_list[0]`, setBoss(bossList.get(0)) in Exordium.initializeBoss) in the
// SAME id space: the EncounterId, which is what boss_list[] holds and
// enter_combat takes.
TEST(RunBegin, BossIdsMirrorsTheRolledActBoss) {
    const RunController rc = run_begin(kSeed, kA20);
    ASSERT_GT(rc.lists.boss_list_count, 0);
    const sts::registry::EncounterDef* boss =
        sts::registry::encounter_by_game_id(rc.lists.boss_list[0]);
    ASSERT_NE(boss, nullptr);
    EXPECT_EQ(rc.run.boss_ids[0], static_cast<uint16_t>(boss->id));
    EXPECT_NE(rc.run.boss_ids[0], 0) << "0 is the 'unset' sentinel";
    for (int i = 1; i < kBossIdCap; ++i) {
        EXPECT_EQ(rc.run.boss_ids[i], 0) << "acts beyond S1 stay unset";
    }
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
// PICK candidates' construction draws (the STS01789 class)
// =============================================================================
//
// MonsterHelper's bottomGet* helpers build an ArrayList of CONSTRUCTED monsters
// and then keep one (MonsterHelper.java:799-822): by the time random(0, n-1)
// selects, every candidate's constructor has already drawn its max HP off
// monsterHpRng (setHp -> AbstractDungeon.monsterHpRng, AbstractMonster.java),
// and a Louse constructor has drawn its biteDamage too (LouseNormal.java:60 /
// LouseDefensive.java:63). The discarded candidates' draws are permanently
// consumed, and the kept monster's HP comes from its POSITION in that
// construction order -- not from the front of the stream.
//
// STS01789 is the live pin: its floor-10 "Exordium Thugs" rolled
// {Acid Slime (M) 30, Slaver 49} in the game, while a sim that rolled only the
// two kept members got {33, 51} -- the slime survived the killing blow with 3
// hp, took its queued turn, and the replay's first RunState evidence was the
// player 8 hp short at seq 130 (the sim then died at seq 133 while the capture
// fought on).

// One candidate construction: the ctor's monster_hp_rng draws, returning the
// HP roll (the value a kept candidate would spawn with).
int32_t ctor_walk_draw(RngStream& hp, std::string_view game_id) {
    const sts::registry::MonsterDef* def = sts::registry::monster_def(
        static_cast<MonsterId>(sts::registry::monster_from_game_id(game_id)));
    EXPECT_NE(def, nullptr) << game_id;
    const int32_t rolled = random(hp, def->hp_min(kMonsterAscension),
                                  def->hp_max(kMonsterAscension));
    for (uint8_t i = 0; i < def->roll_count; ++i) {
        const sts::registry::MonsterRollDef& r = def->rolls[i];
        if (r.timing == sts::registry::MonsterRollTiming::CONSTRUCTOR_AFTER_HP &&
            r.stream == sts::registry::MonsterRollStream::MONSTER_HP) {
            (void)random(hp, r.min(kMonsterAscension), r.max(kMonsterAscension));
        }
    }
    return rolled;
}

TEST(RunCombatSpawn, ExordiumThugsDiscardedCandidatesBurnTheirCtorRolls) {
    for (int64_t seed = 900; seed < 908; ++seed) {
        RunController rc = run_begin(seed, kA20);
        leave_neow(rc);

        // Derive the composition and the full construction walk independently,
        // off copies of the streams the spawn will consume.
        RngStream misc = rc.combat.misc_rng;
        RngStream hp = rc.combat.monster_hp_rng;
        const int32_t ai_before = rc.combat.ai_rng.counter;

        const bool louse_normal = random_boolean(misc);
        const std::string_view louse =
            louse_normal ? "FuzzyLouseNormal" : "FuzzyLouseDefensive";
        const int32_t wsel = random(misc, 0, 2);
        const bool slaver_red = random_boolean(misc);
        const std::string_view slaver = slaver_red ? "SlaverRed" : "SlaverBlue";
        const int32_t ssel = random(misc, 0, 2);

        const int32_t weak_hp[3] = {ctor_walk_draw(hp, louse),
                                    ctor_walk_draw(hp, "SpikeSlime_M"),
                                    ctor_walk_draw(hp, "AcidSlime_M")};
        const int32_t strong_hp[3] = {ctor_walk_draw(hp, "Cultist"),
                                      ctor_walk_draw(hp, slaver),
                                      ctor_walk_draw(hp, "Looter")};

        ASSERT_TRUE(enter_event_combat(rc, "Exordium Thugs")) << "seed=" << seed;
        ASSERT_EQ(rc.combat.monster_count, 2) << "seed=" << seed;
        EXPECT_EQ(rc.combat.monsters[0].hp, weak_hp[wsel])
            << "seed=" << seed << ": the kept weak member's HP is its "
            << "construction-order draw, after the discarded candidates'";
        EXPECT_EQ(rc.combat.monsters[1].hp, strong_hp[ssel]) << "seed=" << seed;
        // Stream totals: 7 ctor draws (louse hp+bite, 5 more hp rolls), plus the
        // kept louse's PRE_BATTLE Curl Up roll when the weak pick IS the louse.
        EXPECT_EQ(rc.combat.monster_hp_rng.counter,
                  hp.counter + (wsel == 0 ? 1 : 0))
            << "seed=" << seed;
        // rollMove only for SPAWNED monsters -- a discarded candidate is never
        // init()ed, so exactly two aiRng draws.
        EXPECT_EQ(rc.combat.ai_rng.counter, ai_before + 2) << "seed=" << seed;
    }
}

TEST(RunCombatSpawn, ExordiumWildlifeDiscardedCandidatesBurnTheirCtorRolls) {
    for (int64_t seed = 900; seed < 908; ++seed) {
        RunController rc = run_begin(seed, kA20);
        leave_neow(rc);

        RngStream misc = rc.combat.misc_rng;
        RngStream hp = rc.combat.monster_hp_rng;

        // bottomGetStrongWildlife: [FungiBeast, JawWorm], no coin, random(0,1).
        const int32_t ssel = random(misc, 0, 1);
        // bottomGetWeakWildlife: getLouse coin, then random(0,2).
        const bool louse_normal = random_boolean(misc);
        const std::string_view louse =
            louse_normal ? "FuzzyLouseNormal" : "FuzzyLouseDefensive";
        const int32_t wsel = random(misc, 0, 2);

        const int32_t strong_hp[2] = {ctor_walk_draw(hp, "FungiBeast"),
                                      ctor_walk_draw(hp, "JawWorm")};
        const int32_t weak_hp[3] = {ctor_walk_draw(hp, louse),
                                    ctor_walk_draw(hp, "SpikeSlime_M"),
                                    ctor_walk_draw(hp, "AcidSlime_M")};

        ASSERT_TRUE(enter_event_combat(rc, "Exordium Wildlife")) << "seed=" << seed;
        ASSERT_EQ(rc.combat.monster_count, 2) << "seed=" << seed;
        EXPECT_EQ(rc.combat.monsters[0].hp, strong_hp[ssel]) << "seed=" << seed;
        EXPECT_EQ(rc.combat.monsters[1].hp, weak_hp[wsel]) << "seed=" << seed;
        EXPECT_EQ(rc.combat.monster_hp_rng.counter,
                  hp.counter + (wsel == 0 ? 1 : 0))
            << "seed=" << seed;
    }
}

// Negative control: an encounter with NO pick step constructs exactly its kept
// members -- the burn must not touch a plain composition. "2 Louse" spawns two
// louses whose six monster_hp_rng draws (hp+bite each, then a Curl Up each) are
// already pinned by LousePreBattleAndInnateResolveBeforePlayerControl above;
// this pins the same property through enter_event_combat's spawn glue.
TEST(RunCombatSpawn, APlainCompositionBurnsNothing) {
    RunController rc = run_begin(kSeed, kA20);
    leave_neow(rc);
    const int32_t hp_before = rc.combat.monster_hp_rng.counter;
    ASSERT_TRUE(enter_event_combat(rc, "2 Louse"));
    ASSERT_EQ(rc.combat.monster_count, 2);
    EXPECT_EQ(rc.combat.monster_hp_rng.counter, hp_before + 6)
        << "hp+bite per louse ctor, one Curl Up each -- and nothing else";
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

// Replace the run's relics with `ids`, in acquisition order. Each slot's
// counter is seeded from the registry row's `initial_counter`, exactly as
// acquire_relic does (relic_pools.cpp `slot.counter = def->initial_counter`)
// -- the AbstractRelic ctor's counter (-1 default; 0/N for counting relics).
// The helper used to hardcode 0, so run-level relic tests could silently
// disagree with acquisition about starting counters (the Centennial Puzzle
// defect class, one layer up -- ledger: "Run-level relic tests seed counters
// by hand, not from the registry").
void set_run_relics(RunController& rc, std::initializer_list<RelicId> ids) {
    uint8_t i = 0;
    for (const RelicId id : ids) {
        const RelicDef* def = relic_def(id);
        ASSERT_NE(def, nullptr) << "unknown relic id in set_run_relics";
        rc.run.relics[i] = RelicSlot{static_cast<uint16_t>(id),
                                     def->initial_counter};
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

// --- The elite-room marker's PRODUCER ---------------------------------------
//
// kCombatFlagEliteRoom (combat_state.hpp) is AbstractRoom.eliteTrigger. Only
// MonsterRoomElite's ctor sets it among ROOM kinds (MonsterRoomElite.java:33);
// MonsterRoomBoss does not (MonsterRoomBoss.java:22-24), and an ordinary
// MonsterRoom never did. The consumers are Sling of Courage, Preserved Insect
// and Slaver's Collar.
TEST(RunCombatBattleStart, OnlyEliteRoomsMarkTheCombat) {
    for (const RoomType kind : {RoomType::Monster, RoomType::Elite}) {
        RunController rc = run_begin(find_jaw_worm_seed(), kA20);
        leave_neow(rc);
        const uint8_t x = first_start_column(rc);
        rc.run.map[run_state_map_index(x, 0)].room_type =
            static_cast<uint8_t>(kind);
        step(rc, make_action(ActionVerb::CHOOSE, x));
        ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT))
            << "room kind " << static_cast<int>(kind);
        EXPECT_EQ(combat_is_elite_room(rc.combat.flags),
                  kind == RoomType::Elite);
    }
}

// End-to-end through the real run path: Sling of Courage's Strength 2 reaches
// the live combat in an elite room and nowhere else (Sling.java:30-37).
TEST(RunCombatBattleStart, SlingOfCourageReachesTheLiveEliteCombat) {
    for (const RoomType kind : {RoomType::Monster, RoomType::Elite}) {
        RunController rc = run_begin(find_jaw_worm_seed(), kA20);
        set_run_relics(rc, {RelicId::SLING_OF_COURAGE});
        leave_neow(rc);
        const uint8_t x = first_start_column(rc);
        rc.run.map[run_state_map_index(x, 0)].room_type =
            static_cast<uint8_t>(kind);
        step(rc, make_action(ActionVerb::CHOOSE, x));
        ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
        const PowerSlot* str = player_power(rc.combat, PowerId::STRENGTH);
        if (kind == RoomType::Elite) {
            ASSERT_NE(str, nullptr) << "no Strength in an elite room";
            EXPECT_EQ(str->amount, 2);
        } else {
            EXPECT_EQ(str, nullptr) << "Strength granted outside an elite room";
        }
    }
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
// addToBot GainBlockAction(player, 10). The block decay (the loseBlock of
// GameActionManager.java:352-359) belongs to step 6 alone -- Java's turn-1
// block has no loseBlock line -- and start_of_turn gates it to
// kSubsequentTurn accordingly, so the 10 granted at battle start must reach
// the player's first decision intact. The dispatch sits inside the shared
// turn-1 block, behind the queued DrawCardAction (AbstractRoom.java:242 vs
// :245), which is where the addToBot body resolves in the Java too.
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

// A combat can end inside the entry pump before the player receives control.
// Neow's Lament sets every enemy to 1 HP at battle start, then Mercury
// Hourglass's turn-start damage kills the whole group. AbstractRoom's battle
// update opens the reward screen directly; the run layer must not leave a
// COMBAT/COMBAT_OVER state whose legal-action mask is empty.
TEST(RunCombatBattleStart, EntryKillOpensRewardsWithoutADeadCombatStep) {
    RunController rc = run_begin(find_two_louse_seed(), kA20);
    set_run_relics(
        rc, {RelicId::NEOWS_LAMENT, RelicId::MERCURY_HOURGLASS});
    leave_neow(rc);

    const StepResult result = step_with_result(
        rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));

    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(rc.combat.phase,
              static_cast<uint8_t>(CombatPhase::COMBAT_OVER));
    EXPECT_EQ(rc.combat_outcome,
              static_cast<uint8_t>(RunCombatOutcome::KILLED));
    EXPECT_EQ(rc.combat.monster_count, 2);
    EXPECT_EQ(rc.combat.monsters[0].hp, 0);
    EXPECT_EQ(rc.combat.monsters[1].hp, 0);
    EXPECT_EQ(rc.run.relics[0].counter, 2);
    EXPECT_FALSE(result.terminal);

    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_TRUE(mask.can_proceed);
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

// THE ORDER-WITHIN-TURN-ONE TESTS (G6 campaign 2 spot-diff §8.0, STS00683).
// AbstractRoom's turn-1 block is a fixed line sequence: the opening
// DrawCardAction is queued (:242), then applyStartOfCombatLogic fires every
// relic's atBattleStart (:245), and only then does applyStartOfTurnRelics fire
// atTurnStart (:253). A relic whose atBattleStart writes state INLINE therefore
// sees its write happen BEFORE the turn-1 atTurnStart -- Stone Calendar's
// counter reads 0-then-++ == 1 at the moment control reaches the player.
// The engine used to run the whole start-of-turn sequence first and dispatch
// AT_BATTLE_START afterwards, which inverted every counter relic by one turn
// for the whole fight (the sim lost STS00683's floor-12 Sentry fight the game
// wins). These walk the REAL entry path; the relic BODIES are pinned by
// relic_rares_shop_test's direct-call loops, which cannot see the wiring.

// StoneCalendar (StoneCalendar.java:36-68): atBattleStart counter = 0,
// atTurnStart ++counter, onPlayerEndTurn fires 52 THORNS at counter == 7,
// onVictory counter = -1. Out of combat the counter is -1 (pickup default and
// the onVictory latch), which is exactly what makes the wrong order visible:
// ++(-1) then =0 reads 0; =0 then ++ reads 1. The capture reads 1 on turn 1
// (STS00683 seq 141, both campaigns).
TEST(RunCombatBattleStart, StoneCalendarCounterSequenceMatchesTheJavaOrder) {
    RunController rc = run_begin(kSeed, kA20);
    // STS00683's floor-12 witness fight: three Sentries, killed outright by the
    // 52 THORNS in the capture. Sentries never gain block or change their own
    // HP, so the fight's whole damage ledger on their side is the calendar's.
    rc.lists.monster_list[0] = "3 Sentries";
    set_run_relics(rc, {RelicId::STONE_CALENDAR});
    rc.run.relics[0].counter = -1;  // the out-of-combat value
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    EXPECT_EQ(rc.combat.relics[0].counter, 1)
        << "atBattleStart (counter = 0) must run BEFORE turn 1's atTurnStart "
           "++ -- AbstractRoom.java:245 vs :253";

    // Never act; just survive. A20 Sentries max at 45 HP (Sentry.java:63), so
    // the end-of-turn-7 52 THORNS (unblocked -- Sentries gain no block) kills
    // all three, exactly as the capture's fight ends.
    rc.combat.player_hp = 999;
    const int16_t hp0 = rc.combat.monsters[0].hp;
    for (int turn = 1; turn <= 6; ++turn) {
        ASSERT_EQ(rc.combat.relics[0].counter, turn) << "turn " << turn;
        step(rc, make_action(ActionVerb::END_TURN));
        ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
        EXPECT_EQ(rc.combat.monsters[0].hp, hp0)
            << "no thorns before the end of turn 7 (turn " << turn << ")";
    }
    ASSERT_EQ(rc.combat.relics[0].counter, 7)
        << "the game fires at the END of turn 7; the sim must reach 7 by then";
    step(rc, make_action(ActionVerb::END_TURN));  // ends turn 7 -> 52 THORNS
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD))
        << "StoneCalendar.onPlayerEndTurn at counter == 7 kills every Sentry "
           "(StoneCalendar.java:53; the STS00683 fight the sim used to LOSE)";
    EXPECT_EQ(rc.run.relics[0].counter, -1)
        << "onVictory latches -1 back into the run (StoneCalendar.java:65-68)";
}

// HornCleat (HornCleat.java:36-53): "at the start of your 2nd turn, gain 14
// Block." atBattleStart arms counter = 0; atTurnStart ++counter and fires at 2.
// Under the inverted order turn 1's ++ was overwritten by the arm, so the block
// arrived one turn late (turn 3) -- same shape as the Stone Calendar defect,
// silently wrong rather than fight-losing.
TEST(RunCombatBattleStart, HornCleatBlocksOnTurnTwoNotTurnThree) {
    RunController rc = run_begin(find_jaw_worm_seed(), kA20);
    set_run_relics(rc, {RelicId::HORN_CLEAT});
    rc.run.relics[0].counter = -1;  // out-of-combat value
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.combat.player_block, 0) << "nothing on turn 1";
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.turn, 2);
    EXPECT_EQ(rc.combat.player_block, 14)
        << "HornCleat.java:43 addToBot GainBlockAction(14) when the turn-start "
           "++ lands on 2 -- turn 2 under the Java order";
}

// Pocketwatch (Pocketwatch.java:20-60): counter == -1 is the armed firstTurn
// flag (atBattleStart sets it), the post-draw check treats a negative counter
// as "this is turn 1, no bonus", and a non-negative counter <= 3 plays draws 3.
// Under the inverted order the turn-1 post-draw check ran BEFORE atBattleStart
// re-armed the flag, so turn 2 read the -1 and the <=3-plays bonus draw was
// silently suppressed.
TEST(RunCombatBattleStart, PocketwatchGrantsTheTurnTwoBonusDraw) {
    RunController rc = run_begin(find_jaw_worm_seed(), kA20);
    set_run_relics(rc, {RelicId::POCKETWATCH});
    rc.run.relics[0].counter = -1;  // out-of-combat value
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.combat.hand_count, 5)
        << "turn 1 is the armed firstTurn: no bonus draw (Pocketwatch.java:39-43)";
    step(rc, make_action(ActionVerb::END_TURN));  // 0 cards played <= 3
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.turn, 2);
    EXPECT_EQ(rc.combat.hand_count, 8)
        << "0 plays on turn 1 -> the turn-2 post-draw check draws 3 "
           "(Pocketwatch.java:40)";
}

// =============================================================================
// atPreBattle relics, through the REAL combat-entry path
// =============================================================================
//
// applyPreCombatLogic (AbstractPlayer.java:1885-1890) is the LAST line of
// preBattlePrep (:1607) and therefore fires BEFORE AbstractRoom's turn-1 block
// queues the opening DrawCardAction (:242). enter_combat carries the call at its
// step (8b), between the relic-mirror copy and begin_first_turn.
//
// Same argument as the atBattleStart section above: the dispatcher had direct
// call coverage in relic_boss_special_test and passed all of it while the RUN
// entry point never called it, so only entry through enter_combat can tell
// "wired" from "unreachable".

// Snecko Eye is the only registered relic that binds the hook. Its Confusion
// must be on the player before the opening hand is drawn -- that is the entire
// difference between atPreBattle and atBattleStart -- so the opening hand is
// cost-rolled, and each rolled card costs one cardRandomRng draw.
TEST(RunCombatPreBattle, SneckoEyeConfusesTheRunLayerOpeningHand) {
    RunController base = enter_jaw_worm_holding({});
    ASSERT_EQ(base.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(base.combat.hand_count, 5);
    EXPECT_EQ(base.combat.card_random_rng.counter, 0)
        << "no relic -- nothing should touch cardRandomRng at combat start";

    RunController rc = enter_jaw_worm_holding({RelicId::SNECKO_EYE});
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));

    // The power itself reached the live combat.
    const PowerSlot* conf = player_power(rc.combat, PowerId::CONFUSION);
    ASSERT_NE(conf, nullptr) << "Confusion never reached the run-layer combat";

    // masterHandSize += 2 lands on the opening hand as well.
    ASSERT_EQ(rc.combat.hand_count, 7);

    // Exactly one cardRandomRng draw per drawn card with cost >= 0 -- i.e. every
    // drawn card that is neither X-cost nor unplayable (Ascender's Bane at A20 is
    // the one that can qualify out). The count is derived from the hand that
    // actually landed, not assumed.
    int32_t expect_rolls = 0;
    for (uint8_t i = 0; i < rc.combat.hand_count; ++i) {
        const CardInstance& c = rc.combat.card_pool[rc.combat.hand[i]];
        if (!has_card_flag(c.flags, CardFlag::XCOST) &&
            !has_card_flag(c.flags, CardFlag::UNPLAYABLE)) {
            ++expect_rolls;
            EXPECT_LE(c.cost_now, 3) << "hand slot " << static_cast<int>(i)
                                     << " escaped the random(3) roll";
        }
    }
    EXPECT_GT(expect_rolls, 0) << "an all-unplayable opening hand proves nothing";
    EXPECT_EQ(rc.combat.card_random_rng.counter, expect_rolls);
}

// The ORDER pin, and the reason the call site is where it is. atPreBattle sits
// between the relic-mirror copy (it reads the mirror) and begin_first_turn (the
// opening draw must see the power). Moving it after begin_first_turn would leave
// the opening hand un-rolled and spend zero cardRandomRng draws before the
// player's first action, so the first hand would escape -- exactly the bug
// atPreBattle exists to prevent.
TEST(RunCombatPreBattle, TheOpeningHandIsRolledNotJustLaterDraws) {
    RunController base = enter_jaw_worm_holding({});
    RunController rc = enter_jaw_worm_holding({RelicId::SNECKO_EYE});
    ASSERT_EQ(base.combat.hand_count, 5);
    ASSERT_EQ(rc.combat.hand_count, 7);

    // Same shuffle, same draw order: Snecko Eye consumes no shuffleRng and the
    // first five slots hold the same card instances as the base run.
    for (uint8_t i = 0; i < 5; ++i) {
        ASSERT_EQ(rc.combat.hand[i], base.combat.hand[i])
            << "draw order moved -- the comparison below would be meaningless";
    }
    // At least one of them has a cost the base run does not have. With a 4-way
    // roll over >= 5 playable cards this is not a coin flip, and the exact
    // per-card values are pinned by the unit tests over ConfusionPower.
    bool any_changed = false;
    for (uint8_t i = 0; i < 5; ++i) {
        any_changed = any_changed ||
                      rc.combat.card_pool[rc.combat.hand[i]].cost_now !=
                          base.combat.card_pool[base.combat.hand[i]].cost_now;
    }
    EXPECT_TRUE(any_changed) << "the opening hand escaped Confusion";
}

// =============================================================================
// The Wave-C coupling: Snecko Eye's hand size x the innate overflow threshold
// =============================================================================
//
// wave-combat derived the per-combat hand size at the draw lines
// (game_hand_size: Snecko Eye's masterHandSize += 2, SneckoEye.java:31);
// wave-runlayer landed initializeDeck's overflow draw. In the Java those read
// ONE field -- CardGroup.initializeDeck:951-953 thresholds and sizes the
// preTurnActions draw on AbstractDungeon.player.masterHandSize, the same field
// preBattlePrep snapshots into gameHandSize (AbstractPlayer.java:1579) -- so
// on the union the overflow threshold must move together with the turn draw.
// A constant-5 threshold (each track's correct-for-itself value) would queue a
// spurious DRAW where the game queues none.

TEST(RunCombatBottle, SneckoEyeRaisesTheInnateOverflowThresholdWithTheDraw) {
    RunController rc = run_begin(kSeed, kA20);
    set_run_relics(rc, {RelicId::SNECKO_EYE});
    // Four registry-innate Writhes + two bottled instances = the same 6-card
    // placeOnTop collection SixTopPlacedCardsAllOpenInHandViaTheOverflowDraw
    // proves overflows a 5-card hand. Under Snecko Eye the hand is 7, so
    // 6 <= 7: the game queues NO overflow draw and the ordinary turn-1 draw
    // of 7 already contains all six.
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

    EXPECT_EQ(rc.combat.hand_count, 7)
        << "game_hand_size under Snecko Eye is 7; a constant-5 overflow "
           "threshold would queue a spurious DRAW(1) and open 8";
    for (uint8_t pi = 1; pi <= 6; ++pi) {
        EXPECT_TRUE(hand_holds_pool_index(rc.combat, pi))
            << "top-placed pool index " << static_cast<int>(pi)
            << " must be in the opening hand";
    }
    EXPECT_EQ(rc.combat.draw_count,
              static_cast<uint8_t>(rc.run.master_deck_count - 7));
    EXPECT_EQ(rc.combat.action_count, 0);
}

// The threshold is the ENLARGED hand in both directions: eight top-placed
// cards under Snecko Eye still overflow, by exactly 8 - 7 = 1.
TEST(RunCombatBottle, EightTopPlacedUnderSneckoEyeOverflowByExactlyOne) {
    RunController rc = run_begin(kSeed, kA20);
    set_run_relics(rc, {RelicId::SNECKO_EYE});
    for (uint16_t i = 1; i <= 8; ++i) {
        rc.run.master_deck[i].card_id = static_cast<uint16_t>(CardId::WRITHE);
    }
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.phase,
              static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));

    EXPECT_EQ(rc.combat.hand_count, 8)
        << "7 from the turn-1 draw + the (8 - 7)-card preTurnActions overflow";
    int writhes = 0;
    for (uint8_t i = 0; i < rc.combat.hand_count; ++i) {
        if (rc.combat.card_pool[rc.combat.hand[i]].card_id ==
            static_cast<uint16_t>(CardId::WRITHE)) {
            ++writhes;
        }
    }
    EXPECT_EQ(writhes, 8) << "every top-placed card opens in hand";
    EXPECT_EQ(rc.combat.action_count, 0);
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

// Blood Potion is not combat-only. Its canUse override permits a reward-screen
// drink, and use() heals the persistent player synchronously outside combat
// (BloodPotion.java:39-59). STS300092 does exactly this at seq 28.
TEST(RunPotion, BloodPotionHealsPersistentHpOnACombatRewardScreen) {
    RunController rc = run_begin(kSeed, kA20);
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT_REWARD);
    rc.run.hp = 15;
    rc.run.max_hp = 80;
    rc.combat.player_hp = 3;  // stale fight state must not be the heal target.
    rc.combat.player_max_hp = 75;
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::BLOOD_POTION);

    RunActionMask mask{};
    legal_actions(rc, mask);
    ASSERT_TRUE(mask.can_use_potion[0]);
    step(rc, make_action(ActionVerb::USE_POTION, 0));

    EXPECT_EQ(rc.run.hp, 31) << "floor(80 * 20%) heals the persistent player";
    EXPECT_EQ(rc.combat.player_hp, 3);
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
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

TEST(RunPotion, SacredBarkDoublesFruitJuiceInCombat) {
    RunController rc = enter_jaw_worm_combat();
    rc.run.relics[rc.run.relic_count] =
        RelicSlot{static_cast<uint16_t>(RelicId::SACRED_BARK), -1};
    ++rc.run.relic_count;
    rc.combat.relics[rc.combat.relic_count] =
        RelicSlot{static_cast<uint16_t>(RelicId::SACRED_BARK), -1};
    ++rc.combat.relic_count;
    rc.combat.player_hp = 33;
    rc.combat.player_max_hp = 75;
    rc.run.hp = 33;
    rc.run.max_hp = 75;
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FRUIT_JUICE);

    step(rc, make_action(ActionVerb::USE_POTION, 0));

    EXPECT_EQ(rc.combat.player_hp, 43);
    EXPECT_EQ(rc.combat.player_max_hp, 85);
    EXPECT_EQ(rc.run.max_hp, 85);
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE));
}

// ToyOrnithopter.onUsePotion IN a COMBAT-phase room is two addToBots
// (ToyOrnithopter.java:31-41): the +5 is a QUEUED HealAction, landing BEHIND
// whatever the potion's own use() queued -- not an inline hp write. Elixir is
// the observable: its blocking optional exhaust screen (ExhaustAction) holds
// the queue head, so the game's heal waits for the confirm button. STS03352
// seq 143-144 diverged by exactly that window's 5 hp (capture 58, sim 63)
// before reconverging at the confirm.
TEST(RunPotion, ToyOrnithopterHealWaitsBehindElixirsOpenScreenInCombat) {
    RunController rc = enter_jaw_worm_combat();
    ASSERT_GT(rc.combat.hand_count, 0) << "the opening hand feeds the screen";
    rc.combat.player_hp = 40;
    rc.run.relics[rc.run.relic_count] =
        RelicSlot{static_cast<uint16_t>(RelicId::TOY_ORNITHOPTER), 0};
    ++rc.run.relic_count;
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::ELIXIR);

    step(rc, make_action(ActionVerb::USE_POTION, 0));
    RunActionMask mask{};
    legal_actions(rc, mask);
    ASSERT_TRUE(mask.combat.choice_pending);
    ASSERT_TRUE(mask.combat.can_confirm_choice);
    EXPECT_EQ(rc.combat.player_hp, 40)
        << "the HealAction is addToBot'd behind the open ExhaustAction "
           "(PotionPopUp order: potion.use first, then onUsePotion)";

    step(rc, make_action(ActionVerb::CONFIRM));
    EXPECT_EQ(rc.combat.player_hp, 45) << "the confirm unblocks the queued heal";
}

// Control: with nothing blocking the queue the same heal lands before control
// returns -- the queued form is not a deferral to the next action.
TEST(RunPotion, ToyOrnithopterHealLandsAtOnceWhenNothingBlocksTheQueue) {
    RunController rc = enter_jaw_worm_combat();
    rc.combat.player_hp = 40;
    rc.run.relics[rc.run.relic_count] =
        RelicSlot{static_cast<uint16_t>(RelicId::TOY_ORNITHOPTER), 0};
    ++rc.run.relic_count;
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FIRE_POTION);

    step(rc, make_action(ActionVerb::USE_POTION, 0, 0));
    EXPECT_EQ(rc.combat.player_hp, 45);
}

// The queued heal goes through the HEAL opcode and therefore through
// heal_player_with_relics, so Magic Flower's onPlayerHeal pass scales it in
// combat exactly as it scales every other HealAction:
// MathUtils.round(5 * 1.5f) == 8 (MagicFlower.java:30-37). The old inline
// write skipped the pass.
TEST(RunPotion, ToyOrnithopterHealInCombatIsScaledByMagicFlower) {
    RunController rc = enter_jaw_worm_combat();
    rc.combat.player_hp = 40;
    rc.run.relics[rc.run.relic_count] =
        RelicSlot{static_cast<uint16_t>(RelicId::TOY_ORNITHOPTER), 0};
    ++rc.run.relic_count;
    // Magic Flower's check reads the COMBAT relic mirror (player_has_relic), so
    // the test plants it in both lists the way enter_combat's fold would have.
    rc.run.relics[rc.run.relic_count] =
        RelicSlot{static_cast<uint16_t>(RelicId::MAGIC_FLOWER), -1};
    ++rc.run.relic_count;
    rc.combat.relics[rc.combat.relic_count] =
        RelicSlot{static_cast<uint16_t>(RelicId::MAGIC_FLOWER), -1};
    ++rc.combat.relic_count;
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FIRE_POTION);

    step(rc, make_action(ActionVerb::USE_POTION, 0, 0));
    EXPECT_EQ(rc.combat.player_hp, 48);
}

// OUT of combat the Java is a plain `player.heal(5)` (ToyOrnithopter.java:39),
// and AbstractPlayer.heal ends with the not-bloodied cross
// (AbstractCreature.heal:404-408) -- so a Toy Ornithopter heal that carries the
// player past half disarms Red Skull's private isActive, exactly as a rest's
// heal does.  That write is behaviorally inert at the run layer (onVictory and
// the next atBattleStart clear it before another read), and must never leak
// into AbstractRelic.counter. Numbers chosen so Fruit Juice's own +5 does NOT
// cross (41 * 2 < 85) and the Ornithopter's +5 does (46 * 2 > 85).
TEST(RunPotion, ToyOrnithopterOutOfCombatCrossKeepsRedSkullCounterHidden) {
    RunController rc = run_begin(kSeed, kA20);
    leave_neow(rc);
    rc.run.hp = 36;
    rc.run.max_hp = 80;
    rc.run.relics[rc.run.relic_count] =
        RelicSlot{static_cast<uint16_t>(RelicId::TOY_ORNITHOPTER), 0};
    ++rc.run.relic_count;
    rc.run.relics[rc.run.relic_count] =
        RelicSlot{static_cast<uint16_t>(RelicId::RED_SKULL), -1};
    ++rc.run.relic_count;
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FRUIT_JUICE);

    step(rc, make_action(ActionVerb::USE_POTION, 0));
    EXPECT_EQ(rc.run.max_hp, 85);
    EXPECT_EQ(rc.run.hp, 46);
    const RelicSlot& skull = rc.run.relics[rc.run.relic_count - 1];
    ASSERT_EQ(skull.relic_id, static_cast<uint16_t>(RelicId::RED_SKULL));
    EXPECT_EQ(skull.counter, -1)
        << "private isActive is not the oracle-visible relic counter";
}

TEST(RunPotion, EntropicBrewOutOfCombatDrawsAreUnlimited) {
    // EntropicBrew.use (EntropicBrew.java:46-48): OUT of combat the non-Sozu
    // branch rolls potionSlots x returnRandomPotion() -- the no-arg overload,
    // limited=false (AbstractDungeon.java:825-827). Only the IN-combat branch
    // (:40-42) passes limited=true.
    //
    // The two flags OFTEN coincide: when a candidate mismatches the rolled
    // tier anyway, the limited discard overlaps the rarity rejection. So a
    // fixed seed can silently pin nothing -- hunt for a seed whose sequence
    // DISTINGUISHES the flag (first candidate already matching the tier, or a
    // Fruit Juice result) and assert the engine takes the unlimited branch
    // there.
    int64_t seed = 0;
    RngStream expected_rng{};
    PotionId first = PotionId::NONE;
    PotionId second = PotionId::NONE;
    for (int64_t cand = 1; cand <= 500 && seed == 0; ++cand) {
        RunController probe = run_begin(cand, kA20);
        leave_neow(probe);
        RngStream unlimited = probe.run.potion_rng;
        RngStream limited = probe.run.potion_rng;
        const PotionId u0 = hand_unlimited_potion_roll(unlimited);
        const PotionId u1 = hand_unlimited_potion_roll(unlimited);
        (void)hand_limited_potion_roll(limited);
        (void)hand_limited_potion_roll(limited);
        if (!streams_equal(unlimited, limited)) {
            seed = cand;
            expected_rng = unlimited;
            first = u0;
            second = u1;
        }
    }
    ASSERT_NE(seed, 0) << "no seed in 1..500 distinguishes the limited flag";

    RunController rc = run_begin(seed, kA20);
    leave_neow(rc);
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::ENTROPIC_BREW);
    rc.run.potions[1] = static_cast<uint16_t>(PotionId::BLOOD_POTION);

    step(rc, make_action(ActionVerb::USE_POTION, 0));
    EXPECT_TRUE(streams_equal(rc.run.potion_rng, expected_rng))
        << "out-of-combat draws must be limited=false (seed " << seed << ")";
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(first));
    EXPECT_EQ(rc.run.potions[1], static_cast<uint16_t>(PotionId::BLOOD_POTION));
    // The second unlimited roll happened even though no slot was open for it
    // (the Java constructs every effect before any obtain resolves); its
    // identity is pinned by the stream compare above.
    (void)second;
}

TEST(RunPotion, EntropicBrewWithSozuOutOfCombatRollsNothing) {
    // EntropicBrew.use:43-45: out of combat the Sozu check comes BEFORE any
    // roll -- potionRng does not move at all and nothing is obtained; the Brew
    // itself is still consumed (PotionPopUp destroys the used potion
    // regardless of which use() branch ran).
    RunController rc = run_begin(kSeed, kA20);
    leave_neow(rc);
    rc.run.relics[rc.run.relic_count] =
        RelicSlot{static_cast<uint16_t>(RelicId::SOZU), -1};
    ++rc.run.relic_count;
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::ENTROPIC_BREW);
    rc.run.potions[1] = static_cast<uint16_t>(PotionId::BLOOD_POTION);
    const RngStream rng_before = rc.run.potion_rng;

    step(rc, make_action(ActionVerb::USE_POTION, 0));
    EXPECT_TRUE(streams_equal(rc.run.potion_rng, rng_before))
        << "the Sozu branch precedes every potionRng draw";
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE));
    EXPECT_EQ(rc.run.potions[1], static_cast<uint16_t>(PotionId::BLOOD_POTION));
}

TEST(RunPotion, EntropicBrewInCombatUnderSozuRollsButObtainsNothing) {
    // In combat the rolls are NOT gated on Sozu: EntropicBrew.use:39-42 rolls
    // returnRandomPotion(true) potionSlots times unconditionally; each queued
    // ObtainPotionAction is then suppressed at resolve while Sozu is owned
    // (ObtainPotionAction.java:29-38 -- flash, no obtainPotion). Net: the
    // stream moves by the full limited sequence, the belt gains nothing.
    RunController rc = enter_jaw_worm_combat();
    rc.run.relics[rc.run.relic_count] =
        RelicSlot{static_cast<uint16_t>(RelicId::SOZU), -1};
    ++rc.run.relic_count;
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::ENTROPIC_BREW);

    RngStream expected_rng = rc.run.potion_rng;
    (void)hand_limited_potion_roll(expected_rng);
    (void)hand_limited_potion_roll(expected_rng);  // A20: two slots.

    RunActionMask mask{};
    legal_actions(rc, mask);
    ASSERT_TRUE(mask.can_use_potion[0]);
    step(rc, make_action(ActionVerb::USE_POTION, 0));
    EXPECT_TRUE(streams_equal(rc.run.potion_rng, expected_rng));
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE));
    EXPECT_EQ(rc.run.potions[1], static_cast<uint16_t>(PotionId::NONE));
    EXPECT_EQ(combat_fairy_armed(rc.combat.flags), 0)
        << "a suppressed obtain places nothing and must not arm the mirror";
}

TEST(RunPotion, EntropicBrewInCombatDrawsLimitedAndFills) {
    // The in-combat branch keeps limited=true (EntropicBrew.java:40-42) and,
    // without Sozu, every obtain lands in the belt front-first.
    RunController rc = enter_jaw_worm_combat();
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::ENTROPIC_BREW);

    RngStream expected_rng = rc.run.potion_rng;
    const PotionId first = hand_limited_potion_roll(expected_rng);
    const PotionId second = hand_limited_potion_roll(expected_rng);

    RunActionMask mask{};
    legal_actions(rc, mask);
    ASSERT_TRUE(mask.can_use_potion[0]);
    step(rc, make_action(ActionVerb::USE_POTION, 0));
    EXPECT_TRUE(streams_equal(rc.run.potion_rng, expected_rng));
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(first));
    EXPECT_EQ(rc.run.potions[1], static_cast<uint16_t>(second));
}

// Advance `rng` until its NEXT limited roll is a Fairy in a Bottle, probing
// with a copy so the fairy roll itself is left unconsumed. Bounded so a pool
// change fails loudly instead of hanging.
static bool advance_until_next_limited_roll_is_fairy(RngStream& rng) {
    for (int i = 0; i < 10000; ++i) {
        RngStream probe = rng;
        if (hand_limited_potion_roll(probe) == PotionId::FAIRY_POTION) {
            return true;
        }
        (void)hand_limited_potion_roll(rng);
    }
    return false;
}

// The S2.43 triage's STS432580: Entropic Brew's in-combat branch is a
// mid-combat BELT GAIN (EntropicBrew.java:39-42 -> ObtainPotionAction ->
// AbstractPlayer.obtainPotion), and a Fairy it places is LIVE -- the revive
// reads hasPotion("FairyPotion") off the belt (AbstractPlayer.java:1485-1493).
// The combat's armed-fairy mirror must therefore arm on the obtain; before it
// did, burn_consumed_fairies at the very same step boundary computed
// held(1) - armed(0) and ate the brand-new potion.
TEST(RunPotion, EntropicBrewInCombatKeepsAFairyItRolls) {
    RunController rc = enter_jaw_worm_combat();
    ASSERT_TRUE(advance_until_next_limited_roll_is_fairy(rc.run.potion_rng))
        << "no fairy in 10,000 limited rolls -- pool changed?";
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::ENTROPIC_BREW);

    step(rc, make_action(ActionVerb::USE_POTION, 0));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::FAIRY_POTION))
        << "the burn at this step's boundary must not eat the fresh Fairy";
    uint8_t fairies_on_belt = 0;
    for (uint8_t i = 0; i < rc.run.potion_slots; ++i) {
        if (static_cast<PotionId>(rc.run.potions[i]) ==
            PotionId::FAIRY_POTION) {
            ++fairies_on_belt;
        }
    }
    EXPECT_EQ(combat_fairy_armed(rc.combat.flags), fairies_on_belt)
        << "every placed Fairy arms the mirror";

    // Idempotence: a further boundary with no revive burns nothing.
    step(rc, make_action(ActionVerb::END_TURN));
    if (rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
        EXPECT_EQ(rc.run.potions[0],
                  static_cast<uint16_t>(PotionId::FAIRY_POTION));
    }
}

// ...and the armed Fairy actually revives: the game's read is live, so dying
// with only the Brew-obtained Fairy on the belt is a save, not a death.
TEST(RunPotion, AFairyObtainedMidCombatStillRevives) {
    RunController rc = enter_jaw_worm_combat();
    ASSERT_TRUE(advance_until_next_limited_roll_is_fairy(rc.run.potion_rng));
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::ENTROPIC_BREW);
    step(rc, make_action(ActionVerb::USE_POTION, 0));
    ASSERT_EQ(rc.run.potions[0],
              static_cast<uint16_t>(PotionId::FAIRY_POTION));

    rc.combat.player_hp = 1;
    step(rc, make_action(ActionVerb::END_TURN));  // Jaw Worm opens with Chomp.
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT))
        << "the Fairy must have saved the run";
    EXPECT_GT(rc.combat.player_hp, 0);
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE))
        << "the spent Fairy leaves the belt at its own step boundary";
    EXPECT_EQ(combat_fairy_armed(rc.combat.flags), 0);
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

    // Gold settlement: every steal deducted live, min(goldAmt, purse) at its
    // own step boundary (sync_live_gold, S2.48) -- with 99 covering all three
    // possible steals the total is steals * goldAmt either way. Both machine
    // paths steal before escaping -- 2 mugs, or 2 mugs + a lunge.
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
    // Two END_TURNs: Mug, Mug -- steal count 2, and the purse is LIVE (S2.48):
    // each steal leaves RunState.gold at its own step boundary, exactly as
    // DamageAction.stealGold deducts at resolve (DamageAction.java:98-114).
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.run.gold, 99 - kLooterGoldAmt) << "first Mug deducts at once";
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(looter_steal_count(rc.combat.monsters[0]), 2);
    ASSERT_EQ(rc.run.gold, 99 - 2 * kLooterGoldAmt);

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

// SmokeBomb.canUse (SmokeBomb.java:50-63) asks the MONSTERS, not the room:
//
//     for (AbstractMonster m : getCurrRoom().monsters.monsters) {
//         if (m.hasPower("BackAttack")) return false;
//         if (m.type != AbstractMonster.EnemyType.BOSS) continue;
//         return false;
//     }
//
// combat_potion_legal tested `room_type == RoomType::Boss` instead. The two
// agree in every state the S1 run layer can currently PRODUCE -- all three
// BOSS-typed rows are Act-1 bosses that only appear in a Boss room, and a Boss
// room always holds one -- so these two tests deliberately construct the
// disagreement directly, which is the only way to see it. They are the
// regression guard for the day an Act-2+ encounter or an event spawn puts a
// BOSS-typed monster somewhere else.

TEST(RunPotion, SmokeBombReadsTheMonsterTypeNotTheRoomType) {
    RunController rc = enter_jaw_worm_combat();
    ASSERT_NE(rc.room_type, static_cast<uint8_t>(RoomType::Boss));
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::SMOKE_BOMB);

    RunActionMask mask{};
    legal_actions(rc, mask);
    ASSERT_TRUE(mask.can_use_potion[0]) << "an ordinary fight allows the escape";

    // Same non-boss ROOM, but the group now holds a BOSS-typed monster.
    rc.combat.monsters[0].monster_id =
        static_cast<uint16_t>(MonsterId::SLIME_BOSS);
    RunActionMask boss_mask{};
    legal_actions(rc, boss_mask);
    EXPECT_FALSE(boss_mask.can_use_potion[0])
        << "m.type == EnemyType.BOSS refuses it wherever the monster is";
}

// The loop in canUse has NO liveness gate: it walks `monsters.monsters`, the
// whole group, and a dead or escaped monster is still a member. That is not
// hypothetical for the Slime Boss, which stays in the group after it splits.
TEST(RunPotion, SmokeBombIsStillRefusedByADeadBossInTheGroup) {
    RunController rc = enter_jaw_worm_combat();
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::SMOKE_BOMB);
    ASSERT_GE(rc.combat.monster_count, 1);
    // A live ordinary monster to fight, plus a dead boss still in the group.
    rc.combat.monsters[rc.combat.monster_count].monster_id =
        static_cast<uint16_t>(MonsterId::SLIME_BOSS);
    rc.combat.monsters[rc.combat.monster_count].hp = 0;
    rc.combat.monsters[rc.combat.monster_count].max_hp = 140;
    ++rc.combat.monster_count;

    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_FALSE(mask.can_use_potion[0])
        << "the Java loop never asks whether the boss is alive";
}

// --- Fairy in a Bottle: the belt <-> combat mirror ----------------------------
//
// FairyPotion is never USED (canUse() is false) -- it fires from
// AbstractPlayer.damage on any lethal HP write. The belt lives in RunState and
// CombatState has none, so enter_combat mirrors the COUNT into
// CombatState.flags, the combat consumes from the mirror, and fold_back_combat
// burns the real slots by comparing the belt against what is left.

TEST(RunPotion, EnteringCombatArmsTheHeldFairies) {
    RunController rc = enter_jaw_worm_combat();
    EXPECT_EQ(combat_fairy_armed(rc.combat.flags), 0)
        << "no fairy held -> nothing armed";

    RunController armed = run_begin(find_jaw_worm_seed(), kA20);
    leave_neow(armed);
    armed.run.potions[0] = static_cast<uint16_t>(PotionId::FAIRY_POTION);
    armed.run.potions[1] = static_cast<uint16_t>(PotionId::FAIRY_POTION);
    step(armed, make_action(ActionVerb::CHOOSE, first_start_column(armed)));
    ASSERT_EQ(armed.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(combat_fairy_armed(armed.combat.flags), 2);
}

// A fairy consumed IN COMBAT burns its real slot at fold-back -- leftmost first,
// and only the number actually spent.
TEST(RunPotion, AConsumedFairyBurnsItsSlotAtFoldBack) {
    RunController rc = run_begin(find_jaw_worm_seed(), kA20);
    leave_neow(rc);
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FAIRY_POTION);
    rc.run.potions[1] = static_cast<uint16_t>(PotionId::FAIRY_POTION);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(combat_fairy_armed(rc.combat.flags), 2);

    // One lethal hit: the mirror drops to 1 and the player survives it.
    deal_lethal_to_player(rc.combat);
    ASSERT_EQ(combat_fairy_armed(rc.combat.flags), 1);
    ASSERT_GT(rc.combat.player_hp, 0);
    const int16_t revived_hp = rc.combat.player_hp;

    // End the fight so the real fold-back runs.
    rc.combat.monsters[0].hp = 0;
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));

    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE))
        << "leftmost first";
    EXPECT_EQ(rc.run.potions[1], static_cast<uint16_t>(PotionId::FAIRY_POTION))
        << "only the one that fired is burned";
    EXPECT_GE(rc.run.hp, revived_hp) << "the revive survived the fold-back";
}

// A combat in which nothing fired must burn nothing -- fold_back_combat runs on
// every combat end, including a defeat.
TEST(RunPotion, AnUnusedFairySurvivesFoldBack) {
    RunController rc = run_begin(find_jaw_worm_seed(), kA20);
    leave_neow(rc);
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FAIRY_POTION);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));

    rc.combat.monsters[0].hp = 0;
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::FAIRY_POTION));
}

// A Fairy is DISCARDABLE in combat (AbstractPotion.canDiscard has no combat
// gate), and that is the only mid-combat belt mutation there is. Left alone,
// the mirror would let a thrown-away fairy still revive.
TEST(RunPotion, DiscardingAFairyInCombatDisarmsIt) {
    RunController rc = run_begin(find_jaw_worm_seed(), kA20);
    leave_neow(rc);
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FAIRY_POTION);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(combat_fairy_armed(rc.combat.flags), 1);

    RunActionMask mask{};
    legal_actions(rc, mask);
    ASSERT_TRUE(mask.can_discard_potion[0]) << "a fairy is throwable in combat";
    step(rc, make_action(ActionVerb::DISCARD_POTION, 0));

    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE));
    EXPECT_EQ(combat_fairy_armed(rc.combat.flags), 0)
        << "a discarded fairy must not revive";
    deal_lethal_to_player(rc.combat);
    EXPECT_EQ(rc.combat.player_hp, 0);
}

// combat_potion_legal rejects FAIRY_POTION by name and must keep doing so: the
// revive is not a USE (canUse() is `return false`, FairyPotion.java:47-50).
TEST(RunPotion, AFairyIsNeverAUsableAction) {
    RunController rc = enter_jaw_worm_combat();
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FAIRY_POTION);
    RunActionMask mask{};
    legal_actions(rc, mask);
    EXPECT_FALSE(mask.can_use_potion[0]);
    EXPECT_TRUE(mask.can_discard_potion[0]);
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
    // DISTILLED_CHAOS left this list: PLAY_CARD + kPlayCardFromDrawTop was
    // PlayTopCardAction all along, so its body landed with no new opcode.
    // DUPLICATION_POTION left it when PowerId::DUPLICATION was registered
    // (its row became a data APPLY_POWER program). The DEFERRED-potion set is
    // now empty; FAIRY_POTION keeps this trap's coverage alive -- it is
    // IMPLEMENTED but never USED (canUse() is `return false`,
    // FairyPotion.java:47-50), so the gate must still refuse it by name.
    for (PotionId id : {PotionId::FAIRY_POTION}) {
        rc.run.potions[0] = static_cast<uint16_t>(id);
        RunActionMask mask{};
        legal_actions(rc, mask);
        EXPECT_FALSE(mask.can_use_potion[0])
            << "unusable potion id " << static_cast<int>(id)
            << " must not be a legal action";
    }
}

TEST(RunPotion, ImplementedPotionsAreStillOfferedInCombat) {
    RunController rc = enter_jaw_worm_combat();
    for (PotionId id : {PotionId::BLOCK_POTION,           // data program
                        PotionId::FIRE_POTION,            // data program, targeted
                        PotionId::BLOOD_POTION,           // combat native
                        PotionId::BLESSING_OF_THE_FORGE,  // combat native
                        PotionId::ELIXIR,                 // combat native (CHOOSE)
                        PotionId::ATTACK_POTION,          // combat native (DISCOVERY)
                        PotionId::SKILL_POTION,           // combat native (DISCOVERY)
                        PotionId::POWER_POTION,           // combat native (DISCOVERY)
                        PotionId::COLORLESS_POTION,       // combat native (DISCOVERY)
                        PotionId::LIQUID_MEMORIES,        // combat native (CHOOSE)
                        PotionId::GAMBLERS_BREW,          // combat native (CHOOSE)
                        PotionId::DISTILLED_CHAOS,        // combat native (PLAY_CARD top)
                        PotionId::DUPLICATION_POTION,     // data program (power 92)
                        PotionId::SNECKO_OIL,             // data program (op 60)
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
    // the slot is not consumed and combat state is untouched. (With the
    // deferred set now empty, FAIRY_POTION -- refused by name, never USED --
    // is what keeps this trap exercised.)
    RunController rc = enter_jaw_worm_combat();
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FAIRY_POTION);
    const int hp_before = rc.combat.monsters[0].hp;
    const int16_t player_hp_before = rc.combat.player_hp;

    step(rc, make_action(ActionVerb::USE_POTION, 0));

    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::FAIRY_POTION))
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
    // The use door is closed on an unusable body (potion_use_implemented);
    // the discard door is not, because a discard never runs the body.
    // The example was SNECKO_OIL until RANDOMIZE_HAND_COST (opcode 60) landed,
    // then DUPLICATION_POTION until PowerId::DUPLICATION registered; with the
    // deferred set now empty, FAIRY_POTION -- never USABLE, always
    // discardable outside a We Meet Again dialog -- carries the trap.
    RunController rc = enter_jaw_worm_combat();
    rc.run.potions[0] = static_cast<uint16_t>(PotionId::FAIRY_POTION);
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

// WingBoots / MapRoomNode.wingedIsConnectedTo: every non-empty node on the
// next row is exposed while a charge is live, but only a destination lacking a
// real edge spends one. The final spend uses the relic's exhausted sentinel
// -2, not a visible zero.
TEST(MapChoice, WingBootsOffersRemoteNodesAndSpendsOnlyOnAJump) {
    RunController base = run_begin(kSeed, kA20);
    leave_neow(base);
    base.run.floor = 1;
    base.cur_x = 3;
    base.room_type = static_cast<uint8_t>(RoomType::Shop);
    base.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);

    for (int x = 0; x < kMapCols; ++x) {
        base.run.map[run_state_map_index(x, 1)] = MapNode{};
    }
    base.run.map[run_state_map_index(3, 0)].edges = kEdgeCenter;
    for (const int x : {0, 3}) {
        MapNode& dst = base.run.map[run_state_map_index(x, 1)];
        dst.edges = kEdgeCenter;
        dst.room_type = static_cast<uint8_t>(RoomType::Shop);
    }

    RunActionMask none{};
    legal_actions(base, none);
    EXPECT_FALSE(none.can_choose_node[0]);
    EXPECT_TRUE(none.can_choose_node[3]);

    RunController jump = base;
    set_run_relics(jump, {RelicId::WING_BOOTS});
    RunActionMask offered{};
    legal_actions(jump, offered);
    EXPECT_TRUE(offered.can_choose_node[0]);
    EXPECT_TRUE(offered.can_choose_node[3]);
    step(jump, make_action(ActionVerb::CHOOSE, 0));
    ASSERT_EQ(jump.run.relic_count, 1);
    EXPECT_EQ(jump.run.relics[0].counter, 2);
    EXPECT_EQ(jump.run.floor, 2);

    RunController connected = base;
    set_run_relics(connected, {RelicId::WING_BOOTS});
    step(connected, make_action(ActionVerb::CHOOSE, 3));
    EXPECT_EQ(connected.run.relics[0].counter, 3)
        << "a real map edge is free";

    RunController last = base;
    set_run_relics(last, {RelicId::WING_BOOTS});
    last.run.relics[0].counter = 1;
    step(last, make_action(ActionVerb::CHOOSE, 0));
    EXPECT_EQ(last.run.relics[0].counter, -2);
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
        // (MawBank.java:38-44), and that is a ShopRoom-only fan-out. The
        // counter therefore still reads the ACQUISITION value: -1, the
        // AbstractRelic default (MawBank's ctor sets none; registry row has
        // no initial_counter). This expectation read 0 while set_run_relics
        // hardcoded 0 -- exactly the silent-reliance the ledger row warned
        // about; usedUp is the counter == -2 encoding, so -1 vs 0 is
        // test-seeding only, not a behavior difference (the engine gate is
        // `counter != -2`, event_framework.cpp).
        EXPECT_EQ(rc.run.relics[0].counter, -1);
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

// Ectoplasm's gate MOVED to the accrual (S2.34): AbstractPlayer.gainGold
// returns before the += AND before the onGainGold fan-out, and the game runs
// that seam AT THE KILL (GreedAction.java:38) -- so op_damage_greed accrues
// NOTHING under Ectoplasm, and the fold-back settle is a raw += with nothing
// to settle. The gate is exercised at the combat layer here; the run-level
// consequence (an untouched purse after a full combat) is what this pins.
TEST(RunCombatGold, EctoplasmSuppressesTheAccrualAtTheKill) {
    RunController rc = enter_jaw_worm_combat();
    ASSERT_EQ(rc.run.gold, 99);
    ASSERT_LT(rc.combat.relic_count, kRelicCap);
    rc.combat.relics[rc.combat.relic_count].relic_id =
        static_cast<uint16_t>(RelicId::ECTOPLASM);
    rc.combat.relics[rc.combat.relic_count].counter = -1;
    ++rc.combat.relic_count;

    // A Hand-of-Greed kill against the live combat: the payout site runs
    // gainGold's early return, so nothing ever reaches the accumulator.
    rc.combat.monsters[0].hp = 1;
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::DAMAGE_GREED);
    it.src = kActorPlayer;
    it.tgt = 0;
    it.amount = 20;
    it.flags = 25;
    execute_opcode(rc.combat, it);
    EXPECT_EQ(rc.combat.combat_gold, 0)
        << "Ectoplasm suppresses the gain at the kill, not at the settle";

    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(rc.run.gold, 99) << "nothing accrued, nothing settled";
}

// =============================================================================
// S2.48 -- the LIVE purse: steal-time clamp vs Hand-of-Greed gain ordering
// =============================================================================
//
// The game's purse moves DURING combat: DamageAction.stealGold clamps and
// deducts at each steal's resolve (DamageAction.java:98-114, one queue slot
// behind the thief's own `stolenGold += min(goldAmt, player.gold)` accrual,
// Looter$1/Mugger$1), and GreedAction runs player.gainGold the instant its
// damage kills (GreedAction.java:37-38). The engine now applies both at step
// boundaries (sync_live_gold). These tests pin the four orderings -- steal
// before/after the Greed kill x purse below/above the steal amount -- on BOTH
// combat-end paths (the victory reward entry and the defeat settlement), each
// through the real run flow, plus the same-step ordering rule directly.
//
// The old model (fold-back banks ALL greed gold, then one combat-end replay)
// agreed with the game on three quadrants and OVER-CREDITED the thieves on
// steal-first-purse-below: the replay read a purse already inflated by greed
// gold the game gained only AFTER the steal. That was the standing
// ~110-disposition class the owner directed S2.48 to close.

// Put a playable card into the LIVE run combat's hand (the card-batch tests'
// AddHand shape, applied to rc.combat). Returns the HAND slot for PLAY_CARD.
uint8_t AddRunHand(RunController& rc, CardId id) {
    CombatState& s = rc.combat;
    uint8_t pi = 0;
    while (pi < kCardPoolCap &&
           s.card_pool[pi].card_id != static_cast<uint16_t>(CardId::NONE)) {
        ++pi;
    }
    const CardDef* d = card_def(id);
    EXPECT_NE(d, nullptr);
    s.card_pool[pi].card_id = static_cast<uint16_t>(id);
    s.card_pool[pi].upgrade = 0;
    s.card_pool[pi].cost_now = card_cost(*d, 0);
    s.card_pool[pi].flags = card_flags(*d, 0);
    const uint8_t hand_slot = s.hand_count;
    s.hand[s.hand_count++] = pi;
    return hand_slot;
}

RunController enter_two_thieves_combat() {
    RunController rc = run_begin(kSeed, kA20);
    rc.lists.monster_list[0] = "2 Thieves";  // Looter slot 0, Mugger slot 1
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.combat.monster_count, 2);
    EXPECT_EQ(rc.combat.monsters[0].monster_id,
              static_cast<uint16_t>(MonsterId::LOOTER));
    EXPECT_EQ(rc.combat.monsters[1].monster_id,
              static_cast<uint16_t>(MonsterId::MUGGER));
    return rc;
}

// VICTORY x steal-first x purse BELOW -- the quadrant the old model got wrong.
// Turn 1: the Mug takes min(20, 10) = 10, emptying the purse. Turn 2: Hand of
// Greed kills the Looter; its 20 banks at the kill. The thief died holding the
// CLAMPED 10 (Looter.java:57's accrual), so die() returns 10 through the
// screen (addStolenGoldToRewards, :170-172) -- where the old fold-first
// settlement read a purse of 30 and returned 20.
TEST(RunStolenGoldOrdering, VictoryStealBeforeGreedKillPurseBelow) {
    RunController rc = enter_looter_combat();
    rc.run.gold = 10;
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(looter_steal_count(rc.combat.monsters[0]), 1);
    EXPECT_EQ(rc.run.gold, 0) << "the steal is clamped by the live 10";

    rc.combat.monsters[0].hp = 5;  // in Hand of Greed's kill range
    const uint8_t slot = AddRunHand(rc, CardId::HAND_OF_GREED);
    step(rc, make_action(ActionVerb::PLAY_CARD, slot, 0));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(rc.combat_outcome,
              static_cast<uint8_t>(RunCombatOutcome::KILLED));
    EXPECT_EQ(rc.run.gold, 20)
        << "the greed 20 banked at the kill; the steal took only 10";
    ASSERT_GE(rc.rewards.count, 1);
    ASSERT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::STOLEN_GOLD));
    EXPECT_EQ(rc.rewards.items[0].gold, 10)
        << "the dead thief returns its CLAMPED take -- the old ordering "
           "over-credited this to 20";
    step(rc, make_action(ActionVerb::CHOOSE, 0));
    EXPECT_EQ(rc.run.gold, 30);
}

// VICTORY x steal-first x purse ABOVE -- the regression quadrant: with the
// purse covering the steal, the clamp never bites and the two orderings agree.
TEST(RunStolenGoldOrdering, VictoryStealBeforeGreedKillPurseAbove) {
    RunController rc = enter_looter_combat();
    rc.run.gold = 100;
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(looter_steal_count(rc.combat.monsters[0]), 1);
    EXPECT_EQ(rc.run.gold, 80);

    rc.combat.monsters[0].hp = 5;
    const uint8_t slot = AddRunHand(rc, CardId::HAND_OF_GREED);
    step(rc, make_action(ActionVerb::PLAY_CARD, slot, 0));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(rc.run.gold, 100);
    ASSERT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::STOLEN_GOLD));
    EXPECT_EQ(rc.rewards.items[0].gold, kLooterGoldAmt);
    step(rc, make_action(ActionVerb::CHOOSE, 0));
    EXPECT_EQ(rc.run.gold, 120);
}

// VICTORY x greed-first x purse BELOW. The Greed kill lands on turn 1's player
// phase (gainGold immediate), so the turn-1 steal clamps against a purse the
// greed gold already entered -- the ordering the old model happened to match.
TEST(RunStolenGoldOrdering, VictoryGreedKillBeforeStealPurseBelow) {
    RunController rc = enter_two_thieves_combat();
    rc.run.gold = 0;
    rc.combat.monsters[1].hp = 5;  // the Mugger dies to Hand of Greed
    const uint8_t slot = AddRunHand(rc, CardId::HAND_OF_GREED);
    step(rc, make_action(ActionVerb::PLAY_CARD, slot, 1));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.run.gold, 20) << "banked at the PLAY_CARD boundary";

    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(looter_steal_count(rc.combat.monsters[0]), 1);
    ASSERT_EQ(mugger_steal_count(rc.combat.monsters[1]), 0)
        << "the Mugger died before its first steal";
    EXPECT_EQ(rc.run.gold, 0)
        << "the Looter's steal reads the greed-inflated purse: min(20, 20)";

    rc.combat.monsters[0].hp = 0;  // kill the Looter -> full-kill victory
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(rc.run.gold, 0);
    ASSERT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::STOLEN_GOLD));
    EXPECT_EQ(rc.rewards.items[0].gold, 20)
        << "the dead Looter returns its full take; the dead Mugger holds 0";
    step(rc, make_action(ActionVerb::CHOOSE, 0));
    EXPECT_EQ(rc.run.gold, 20);
}

// VICTORY x greed-first x purse ABOVE.
TEST(RunStolenGoldOrdering, VictoryGreedKillBeforeStealPurseAbove) {
    RunController rc = enter_two_thieves_combat();
    rc.run.gold = 100;
    rc.combat.monsters[1].hp = 5;
    const uint8_t slot = AddRunHand(rc, CardId::HAND_OF_GREED);
    step(rc, make_action(ActionVerb::PLAY_CARD, slot, 1));
    EXPECT_EQ(rc.run.gold, 120);
    step(rc, make_action(ActionVerb::END_TURN));
    EXPECT_EQ(rc.run.gold, 100);
    rc.combat.monsters[0].hp = 0;
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    ASSERT_EQ(rc.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::STOLEN_GOLD));
    EXPECT_EQ(rc.rewards.items[0].gold, kLooterGoldAmt);
    step(rc, make_action(ActionVerb::CHOOSE, 0));
    EXPECT_EQ(rc.run.gold, 120);
}

// DEFEAT x steal-first x purse BELOW. The dead player's purse walks the same
// live trajectory -- 10 stolen (clamped), +20 greed, then the Mugger's
// round-2 steal soaks the greed gold -- and no return is reachable past a
// defeat (the game deducted at steal time; there is no reward screen).
TEST(RunStolenGoldOrdering, DefeatStealBeforeGreedKillPurseBelow) {
    RunController rc = enter_two_thieves_combat();
    rc.run.gold = 10;
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(looter_steal_count(rc.combat.monsters[0]), 1);
    ASSERT_EQ(mugger_steal_count(rc.combat.monsters[1]), 1);
    EXPECT_EQ(rc.run.gold, 0)
        << "slot order: the Looter takes 10, the Mugger's steal finds 0";

    rc.combat.monsters[0].hp = 5;  // Hand of Greed kills the Looter
    const uint8_t slot = AddRunHand(rc, CardId::HAND_OF_GREED);
    step(rc, make_action(ActionVerb::PLAY_CARD, slot, 0));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.run.gold, 20);

    rc.combat.player_hp = 1;  // the Mugger's next Mug is lethal
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    EXPECT_EQ(rc.combat_outcome,
              static_cast<uint8_t>(RunCombatOutcome::DEFEAT));
    ASSERT_EQ(mugger_steal_count(rc.combat.monsters[1]), 2)
        << "the lethal Mug's steal resolved one slot ahead of its damage";
    EXPECT_EQ(rc.run.gold, 0)
        << "the round-2 steal soaked the greed gold before the death";
}

// DEFEAT x steal-first x purse ABOVE.
TEST(RunStolenGoldOrdering, DefeatStealBeforeGreedKillPurseAbove) {
    RunController rc = enter_two_thieves_combat();
    rc.run.gold = 100;
    step(rc, make_action(ActionVerb::END_TURN));
    EXPECT_EQ(rc.run.gold, 60) << "both turn-1 steals paid in full";
    rc.combat.monsters[0].hp = 5;
    const uint8_t slot = AddRunHand(rc, CardId::HAND_OF_GREED);
    step(rc, make_action(ActionVerb::PLAY_CARD, slot, 0));
    EXPECT_EQ(rc.run.gold, 80);
    rc.combat.player_hp = 1;
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    EXPECT_EQ(rc.run.gold, 60)
        << "the Mugger's round-2 steal took its full 20 before the kill";
}

// DEFEAT x greed-first x purse BELOW.
TEST(RunStolenGoldOrdering, DefeatGreedKillBeforeStealPurseBelow) {
    RunController rc = enter_two_thieves_combat();
    rc.run.gold = 0;
    rc.combat.monsters[1].hp = 5;  // the Mugger dies to Hand of Greed, turn 1
    const uint8_t slot = AddRunHand(rc, CardId::HAND_OF_GREED);
    step(rc, make_action(ActionVerb::PLAY_CARD, slot, 1));
    EXPECT_EQ(rc.run.gold, 20);
    step(rc, make_action(ActionVerb::END_TURN));
    EXPECT_EQ(rc.run.gold, 0) << "the Looter's turn-1 steal reads the 20";
    rc.combat.player_hp = 1;  // the Looter's second Mug is lethal
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    ASSERT_EQ(looter_steal_count(rc.combat.monsters[0]), 2);
    EXPECT_EQ(rc.run.gold, 0) << "the round-2 steal found an empty purse";
}

// DEFEAT x greed-first x purse ABOVE.
TEST(RunStolenGoldOrdering, DefeatGreedKillBeforeStealPurseAbove) {
    RunController rc = enter_two_thieves_combat();
    rc.run.gold = 100;
    rc.combat.monsters[1].hp = 5;
    const uint8_t slot = AddRunHand(rc, CardId::HAND_OF_GREED);
    step(rc, make_action(ActionVerb::PLAY_CARD, slot, 1));
    EXPECT_EQ(rc.run.gold, 120);
    step(rc, make_action(ActionVerb::END_TURN));
    EXPECT_EQ(rc.run.gold, 100);
    rc.combat.player_hp = 1;
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    EXPECT_EQ(rc.run.gold, 80);
}

// THE SAME-STEP ORDER, pinned directly on sync_live_gold. One END_TURN advance
// can carry BOTH a monster-phase steal and a greed gain that resolved after it
// (Mayhem's turn-start Hand of Greed): one sync call must charge the steal
// against the PRE-greed purse and only then bank the greed remainder.
TEST(RunStolenGoldOrdering, SyncChargesSameStepStealsBeforeSameStepGreed) {
    RunController rc{};
    rc.run.gold = 10;
    rc.combat.monster_count = 1;
    rc.combat.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::LOOTER);
    rc.combat.monsters[0].hp = 40;
    rc.combat.monsters[0].max_hp = 40;
    rc.combat.monsters[0].pad0 = 1;  // one uncharged steal ...
    rc.combat.combat_gold = 20;      // ... and unbanked greed, one step
    sync_live_gold(rc);
    EXPECT_EQ(rc.stolen_live.taken[0], 10)
        << "the steal is clamped by the pre-greed 10";
    EXPECT_EQ(rc.run.gold, 20) << "10 - 10 + 20";
    sync_live_gold(rc);  // idempotent between events
    EXPECT_EQ(rc.stolen_live.taken[0], 10);
    EXPECT_EQ(rc.run.gold, 20);
}

// The converse boundaries: greed banked at its OWN earlier step makes a later
// steal read the inflated purse -- gainGold is live the moment the kill lands
// (GreedAction.java:37-38).
TEST(RunStolenGoldOrdering, SyncBanksEarlierGreedBeforeALaterStealBoundary) {
    RunController rc{};
    rc.run.gold = 10;
    rc.combat.monster_count = 1;
    rc.combat.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::LOOTER);
    rc.combat.monsters[0].hp = 40;
    rc.combat.monsters[0].max_hp = 40;
    rc.combat.combat_gold = 20;
    sync_live_gold(rc);  // the greed kill's own PLAY_CARD boundary
    EXPECT_EQ(rc.run.gold, 30);
    rc.combat.monsters[0].pad0 = 1;
    sync_live_gold(rc);  // the steal's END_TURN boundary
    EXPECT_EQ(rc.stolen_live.taken[0], 20) << "the full 20 was available";
    EXPECT_EQ(rc.run.gold, 10);
}

// The run-persistent misc fold-back (S2.34): a Ritual Dagger kill grows the
// COMBAT instance's misc (op_ritual_dagger), and the fold copies it to the
// master-deck row -- pool row i is master row i -- exactly for rows whose
// CardDef.initial_misc != 0. RitualDaggerAction's masterDeck-by-uuid write
// (RitualDaggerAction.java:40-46) by the combat_gold-precedent road.
TEST(RunCombatGold, RitualDaggerKillFoldsItsGrownMiscToTheMasterDeck) {
    RunController rc = run_begin(find_jaw_worm_seed(), kA20);
    ASSERT_TRUE(add_card_to_master_deck(rc.run, CardId::RITUAL_DAGGER));
    const uint16_t dagger_row =
        static_cast<uint16_t>(rc.run.master_deck_count - 1);
    ASSERT_EQ(rc.run.master_deck[dagger_row].misc, 15);
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.card_pool[dagger_row].card_id,
              static_cast<uint16_t>(CardId::RITUAL_DAGGER));
    EXPECT_EQ(rc.combat.card_pool[dagger_row].misc, 15)
        << "enter_combat seeds the pool instance from the master row";

    rc.combat.monsters[0].hp = 5;
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::RITUAL_DAGGER);
    it.src = kActorPlayer;
    it.tgt = 0;
    it.amount = 3;
    it.flags = dagger_row;
    execute_opcode(rc.combat, it);
    ASSERT_EQ(rc.combat.card_pool[dagger_row].misc, 18);

    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(rc.run.master_deck[dagger_row].misc, 18)
        << "the growth reaches the master card at the fold";
    // ...and every OTHER master row's misc stayed put (combat-scratch misc,
    // e.g. Rampage's accumulator, must never fold).
    for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
        if (i != dagger_row) {
            EXPECT_EQ(rc.run.master_deck[i].misc, 0) << i;
        }
    }
}

// The LIVE half of the same mirror (S2.43 triage, seed STS432354): the Java's
// masterDeck write is MID-ACTION (RitualDaggerAction.java:39-46), so the
// capture's master row is grown at the very next record after the kill -- not
// at combat end. sync_run_persistent_misc therefore runs at every in-combat
// command boundary, and a run that DIES before the fold still carries the
// grown row to its terminal records. Pinned through real steps, not a direct
// helper call: the boundary IS the contract.
TEST(RunCombatGold, RitualDaggerGrowthReachesTheMasterDeckAtTheNextBoundary) {
    RunController rc = run_begin(find_jaw_worm_seed(), kA20);
    ASSERT_TRUE(add_card_to_master_deck(rc.run, CardId::RITUAL_DAGGER));
    const uint16_t dagger_row =
        static_cast<uint16_t>(rc.run.master_deck_count - 1);
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.combat.card_pool[dagger_row].card_id,
              static_cast<uint16_t>(CardId::RITUAL_DAGGER));

    // The state a mid-fight dagger kill leaves behind: the pool instance has
    // grown, the combat continues (in the capture, two elites were still up).
    rc.combat.card_pool[dagger_row].misc = 18;
    EXPECT_EQ(rc.run.master_deck[dagger_row].misc, 15)
        << "no boundary has passed yet";

    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT))
        << "the fight must still be live for the boundary claim to mean "
           "anything";
    EXPECT_EQ(rc.run.master_deck[dagger_row].misc, 18)
        << "the growth reaches the master card at the step boundary, before "
           "any fold";
    // Combat-scratch misc still never syncs, per-step included.
    for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
        if (i != dagger_row) {
            EXPECT_EQ(rc.run.master_deck[i].misc, 0) << i;
        }
    }

    // The capture's exact shape: the player dies before any fold. The master
    // row must already hold the growth at the terminal. (Bounded retry: a
    // rolled non-damaging monster turn just repeats the boundary.)
    for (int t = 0; t < 8 &&
                    rc.phase == static_cast<uint8_t>(RunPhase::COMBAT);
         ++t) {
        rc.combat.player_hp = 1;
        step(rc, make_action(ActionVerb::END_TURN));
    }
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    EXPECT_EQ(rc.run.master_deck[dagger_row].misc, 18);
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
// The victory terminal -- now the BOSS CHEST's proceed (S2.11)
// =============================================================================
//
// In the game the boss reward's Proceed never opens the map: at a COMBAT_REWARD
// in a MonsterRoomBoss it goes to the boss chest (ProceedButton.update,
// ProceedButton.java:111-113 -> goToTreasureRoom :179-187, a TreasureRoomBoss).
// Until S2.11 that room was unmodelled and the reward proceed WAS the terminal;
// now it enters the chest, and the terminal moved to the chest's own proceed
// (on_boss_chest_proceed, the seam S2.12 fills with the act transition).
// run_is_victory() moved with it and reads room_type == TreasureBoss.
//
// The regression these tests pin was found by a 300-seed always_event fuzz
// probe (seed 116): the proceed used to route to MAP_CHOICE, where the boss
// column has no outgoing map edges, so the run advertised an EMPTY action
// mask while claiming not to be terminal -- the soak's no_legal_moves. The
// property that matters is unchanged: the boss reward's proceed must never
// leave the run non-terminal with nothing legal.

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

TEST(BossVictory, BossRewardProceedEntersTheBossChestNotRunOver) {
    RunController rc = enter_boss_combat(kSeed);
    weaken_all_monsters(rc);
    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    ASSERT_EQ(rc.combat_outcome, static_cast<uint8_t>(RunCombatOutcome::KILLED));
    EXPECT_FALSE(run_is_victory(rc))
        << "the reward screen is still up -- not terminal yet";
    const uint16_t boss_floor = rc.run.floor;

    RunActionMask m{};
    legal_actions(rc, m);
    ASSERT_TRUE(m.can_proceed);
    const StepResult res =
        step_with_result(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));

    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::BOSS_TREASURE))
        << "the boss reward's proceed goes to the boss chest "
           "(ProceedButton.java:111-113 -> :179-187), not to the map the boss "
           "column has no edges into, and no longer straight to RUN_OVER";
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::TreasureBoss));
    EXPECT_FALSE(run_is_victory(rc)) << "the chest room is not terminal";
    EXPECT_FALSE(res.terminal);
    EXPECT_EQ(res.reward, 0.0f) << "the win is paid at the chest's proceed";
    EXPECT_EQ(rc.rewards.count, 0) << "the screen cleared on the way out";
    EXPECT_EQ(rc.run.floor, boss_floor + 1)
        << "goToTreasureRoom runs the whole nextRoomTransition, so the chest "
           "is its own floor (AbstractDungeon.java:2317-2325 -> :1687-1813)";
}

TEST(BossVictory, TheChestProceedOpensTheNextActRatherThanEndingTheRun) {
    // S2.12 moved the terminal off this edge entirely: the chest's proceed is
    // the ACT TRANSITION now, and only the Act-3 boss ends the run (s2-design
    // §1). What has to keep holding is the seed-116 property this whole section
    // exists for -- a non-terminal phase must never advertise an empty mask.
    RunController rc = enter_boss_combat(kSeed);
    // The boss and chest floors have to be the real ones: run_cur_row() is a
    // function of BOTH act and floor since S2.12, so an (act 2, floor 2)
    // controller is not a state the game can be in.
    rc.run.floor = static_cast<uint16_t>(kActFloorSpan - 1);
    weaken_all_monsters(rc);
    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::BOSS_TREASURE));
    ASSERT_EQ(rc.run.floor, kActFloorSpan);
    // Walk straight past the chest.
    const StepResult res =
        step_with_result(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.run.act, 2);
    EXPECT_FALSE(res.terminal);
    EXPECT_FALSE(run_is_victory(rc));

    RunActionMask m{};
    legal_actions(rc, m);
    bool any = m.can_choose_boss;
    for (int x = 0; x < kMapCols; ++x) any = any || m.can_choose_node[x];
    EXPECT_TRUE(any)
        << "the boss column has no outgoing edges, so a proceed that did not "
           "regenerate the map would advertise an empty non-terminal mask -- "
           "the seed-116 no_legal_moves regression this section is named for";
}

// S2.24 un-parked the Act-2 boss rooms: registering the three boss init fns is
// what turned "Automaton" / "Collector" / "Champ" (encounters.yaml 38-40, all
// landed by S2.01) from ROOM_UNIMPLEMENTED parks into real combats -- the
// S2.23 shape, one act later. And because Acts 1-2 boss kills DO open a reward
// screen, this is the first fight whose A13-scaled gold actually reaches a
// claimable item (a20.yaml row 13's S2.24 share; the Act-3 draw-and-discard
// half was S2.28's).
TEST(BossVictory, TheActTwoBossRoomEntersARealCombatAndPaysA13ScaledGold) {
    RunController rc = enter_boss_combat(kSeed);  // run_begin(seed, kA20)
    rc.run.floor = static_cast<uint16_t>(kActFloorSpan - 1);
    weaken_all_monsters(rc);
    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::BOSS_TREASURE));
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    ASSERT_EQ(rc.run.act, 2);

    // Aim the transition straight at the Act-2 boss room.
    next_room_transition(rc, 0, /*to_boss=*/true);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT))
        << "the Act-2 boss room must be a REAL combat now, not a "
           "ROOM_UNIMPLEMENTED park";
    ASSERT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Boss));
    ASSERT_GE(rc.combat.monster_count, 1);
    const uint16_t boss_id = rc.combat.monsters[0].monster_id;
    EXPECT_TRUE(boss_id == static_cast<uint16_t>(MonsterId::BRONZE_AUTOMATON) ||
                boss_id == static_cast<uint16_t>(MonsterId::CHAMP) ||
                boss_id == static_cast<uint16_t>(MonsterId::THE_COLLECTOR))
        << boss_id << " is not an Act-2 registry boss";
    EXPECT_TRUE(sts::registry::monster_def(static_cast<MonsterId>(boss_id))
                    ->is_boss())
        << "the enemy_type column is what Pantograph-style consumers read";

    weaken_all_monsters(rc);
    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD))
        << "an Act-2 boss kill opens a reward screen -- the :327 guard is "
           "TheBeyond-only";
    // The gold item is the A13 branch's: round(0.75 * (100 + d)) with d in
    // [-5, 5] gives [71, 79] -- disjoint from the unscaled [95, 105], so the
    // band alone proves the x0.75 applied on a REAL Act-2 boss payout.
    int gold = -1;
    for (uint8_t i = 0; i < rc.rewards.count; ++i) {
        if (rc.rewards.items[i].kind ==
            static_cast<uint8_t>(RewardItemKind::GOLD)) {
            gold = rc.rewards.items[i].gold;
        }
    }
    ASSERT_GE(gold, 0) << "no gold item on the Act-2 boss reward screen";
    EXPECT_GE(gold, 71);
    EXPECT_LE(gold, 79);

    // And past the reward screen the ordinary chest -> act-3 route stands.
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::BOSS_TREASURE));
}

TEST(BossVictory, TheActThreeBossIsTheTerminalAndItsMaskIsEmpty) {
    // AbstractRoom.java:327: on a non-endless TheBeyond boss the whole
    // dropReward / addPotionToRewards / combatRewardScreen.open block is
    // skipped, so the kill IS the end of the run. The Act-3 boss ENCOUNTERS are
    // S2.28's, so the room is reached here by aiming the public transition at an
    // Act-1 boss encounter with the run already in Act 3 -- the combat content
    // is irrelevant to the terminal, which keys on (act, room kind) alone.
    RunController act3 = enter_boss_combat(kSeed);
    act3.run.act = 3;
    act3.run.floor = static_cast<uint16_t>(act_floor_base(3) + kActFloorSpan - 1);
    // BELOW A20 the first Act-3 boss IS the terminal: ProceedButton's
    // double-boss arm needs `ascensionLevel >= 20` (ProceedButton.java:101-104),
    // so at A19 the kill falls straight through to goToVictoryRoomOrTheDoor.
    // The A20 route -- a SECOND boss room before the terminal -- is the sibling
    // test below.
    act3.run.ascension = 19;
    weaken_all_monsters(act3);
    // The Act-2 twin, identical but for the act (and it deliberately KEEPS
    // ascension 20: an A20 Act-2 boss kill must still open a reward screen,
    // because the double-boss gate is TheBeyond-only however the remaining
    // bossList count reads -- the named Act-2 negative the run_advance.cpp
    // double-boss comment points at). The differential still pins "the gold
    // DRAW still happens, the gold ITSELF never lands": roll_boss_gold spends
    // exactly one miscRng draw at 19 and at 20 alike (the A13 branch changes
    // the discarded value, never the stream).
    RunController act2 = act3;
    act2.run.ascension = 20;
    act2.run.act = 2;
    act2.run.floor = static_cast<uint16_t>(act_floor_base(2) + kActFloorSpan - 1);
    const int32_t gold_before = act3.run.gold;

    play_out_combat(act3);
    play_out_combat(act2);
    RunController& rc = act3;

    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER))
        << "no reward screen follows the Act-3 boss";
    EXPECT_TRUE(run_is_victory(rc));
    ASSERT_EQ(act2.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD))
        << "the Act-2 boss still opens one -- the guard is act-specific";
    EXPECT_EQ(rc.rewards.count, 0) << "dropReward() never ran";
    EXPECT_GT(act2.rewards.count, 0);
    EXPECT_EQ(rc.combat.misc_rng.counter, act2.combat.misc_rng.counter)
        << "the gold add at AbstractRoom.java:286-297 is AHEAD of the :327 "
           "guard, so its single miscRng draw fires in both acts";
    EXPECT_EQ(rc.run.gold, gold_before)
        << "and with no screen to claim it on, that gold never reaches the "
           "purse (addGoldToRewards, AbstractRoom.java:610-617)";

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

// play_out_combat loops while phase == COMBAT, and the double-boss crossing
// lands in ANOTHER combat -- so the first Act-3 fight needs an explicit
// floor-change stop instead, or the helper plays the second boss un-weakened.
// Shared by the two double-boss tests below.
void play_out_combat_until_the_floor_changes(RunController& rc,
                                             uint16_t first_floor) {
    StepResult res{};
    for (int step = 0; step < 800; ++step) {
        if (rc.phase != static_cast<uint8_t>(RunPhase::COMBAT) ||
            rc.run.floor != first_floor) {
            break;
        }
        RunActionMask m{};
        legal_actions(rc, m);
        Action a = make_action(ActionVerb::END_TURN);
        bool played = false;
        for (int i = 0; i < kHandCap && !played; ++i) {
            for (int t = 0; t < kMonsterCap; ++t) {
                if (m.combat.can_play_target[i][t]) {
                    a = make_action(ActionVerb::PLAY_CARD,
                                    static_cast<uint8_t>(i),
                                    static_cast<uint8_t>(t));
                    played = true;
                    break;
                }
            }
        }
        advance(std::span<RunController>(&rc, 1),
                std::span<const Action>(&a, 1),
                std::span<StepResult>(&res, 1));
    }
}

// The A20 route to that terminal has ONE MORE ROOM in it (a20.yaml row 20,
// S2.28). ProceedButton.update:99-104 -- `ascensionLevel >= 20 &&
// bossList.size() == 2`, read AFTER MonsterRoomBoss.onPlayerEntry popped this
// room's boss -- sends the first Act-3 boss kill through goToDoubleBoss
// (:210-220): a synthetic MonsterRoomBoss and a FULL nextRoomTransitionStart.
// Only the SECOND kill is the terminal, and only it pays the +1.
TEST(BossVictory, TheA20DoubleBossInterposesASecondBossRoomBeforeTheTerminal) {
    RunController rc = enter_boss_combat(kSeed);  // run_begin(seed, kA20)
    rc.run.act = 3;
    rc.run.floor = static_cast<uint16_t>(act_floor_base(3) + kActFloorSpan - 1);
    ASSERT_EQ(rc.run.ascension, 20);
    ASSERT_EQ(rc.lists.boss_list_count, 3)
        << "TheBeyond.initializeBoss keeps three keys; the gate's remaining "
           "count is data, not a constant";
    ASSERT_EQ(rc.boss_cursor, 0);
    const uint16_t first_floor = rc.run.floor;
    const int32_t gold_before = rc.run.gold;

    weaken_all_monsters(rc);
    play_out_combat_until_the_floor_changes(rc, first_floor);

    // Kill #1: NOT a terminal. The crossing is a full room transition --
    // ++floor, the five-stream reseed, boss_cursor advances on the way out --
    // into a COMBAT in a fresh Boss room whose encounter is boss_list[1].
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT))
        << "goToDoubleBoss is a room transition, not a screen";
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Boss));
    EXPECT_FALSE(run_is_victory(rc));
    EXPECT_EQ(rc.run.floor, first_floor + 1)
        << "nextRoomTransitionStart runs in full, so the synthetic room is its "
           "own floor (the S2.11 rule: never hand-roll a second transition)";
    EXPECT_EQ(rc.boss_cursor, 1)
        << "the cursor means 'boss rooms COMPLETED' and bumps on room exit";
    EXPECT_EQ(rc.run.gold, gold_before)
        << "the first room's boss-gold draw is discarded, not banked "
           "(AbstractRoom.java:286-297 pays to an unclaimable reward list)";

    // The second boss's identity is now PUBLIC -- the player is looking at it --
    // so the reserved v5 slot carries it, and it is boss_list[1] exactly.
    {
        PublicView pv{};
        encode_public_view(rc, pv);
        const sts::registry::EncounterDef* second =
            sts::registry::encounter_by_game_id(rc.lists.boss_list[1]);
        ASSERT_NE(second, nullptr);
        EXPECT_EQ(pv.second_boss_reserved, second->id)
            << "second_boss_reserved (v5) carries the revealed second boss";
        EXPECT_EQ(pv.second_boss_reserved, pv.current_encounter_id)
            << "while standing in the second room the two fields agree; only "
               "second_boss_reserved survives into RUN_OVER";
    }

    // Kill #2: the terminal, exactly once, with the +1 paid here and not before.
    weaken_all_monsters(rc);
    play_out_combat(rc);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER))
        << "after the second room's entry pop the remaining count is 1, the "
           "gate fails, and the kill falls through to the terminal";
    EXPECT_TRUE(run_is_victory(rc));
    EXPECT_EQ(rc.rewards.count, 0) << "no reward screen at either Act-3 boss";

    // The v5 slot SURVIVES the terminal -- at RUN_OVER it is the only record of
    // which second boss decided the run (current_encounter_id is combat-scoped).
    PublicView pv{};
    encode_public_view(rc, pv);
    const sts::registry::EncounterDef* second =
        sts::registry::encounter_by_game_id(rc.lists.boss_list[1]);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(pv.second_boss_reserved, second->id);
    EXPECT_EQ(pv.current_encounter_id, 0)
        << "combat-scoped, absent at RUN_OVER -- the non-redundancy the v5 "
           "field note claims";
}

// goToDoubleBoss's FIRST line, which nothing mirrored until the S2.43/S2.V2
// depth captures made the handoff record comparable:
//
//     AbstractDungeon.bossKey = AbstractDungeon.bossList.get(0);   (:211)
//
// `bossKey` is the same single field setBoss writes at act construction
// (AbstractDungeon.java:349-350) -- getBoss() feeds it to
// MonsterHelper.getEncounter for the room about to open (:1992-1995) and
// SaveFile persists it as the run's boss (SaveFile.java:246) -- so the act's
// boss identity genuinely CHANGES mid-act here, and `boss_ids[act-1]` is the
// engine's mirror of it (the field the translator fills from a capture's
// `act_boss` and the differ compares by name). `bossList.get(0)` at that
// instant is the SECOND boss: the first room's entry already popped its own key
// (MonsterRoomBoss.java:27-36).
//
// The FIGHT was never wrong -- on_player_entry takes its encounter from
// `boss_list[boss_cursor]` -- which is exactly why the stale mirror survived
// S2.28: it is invisible to every sim-side observation of the second combat and
// only a live A20 Act-3 capture past floor 50 can see it.
TEST(BossVictory, TheA20DoubleBossHandoffMovesTheActsBossIdToTheSecondBoss) {
    RunController rc = enter_boss_combat(kSeed);  // run_begin(seed, kA20)
    rc.run.act = 3;
    rc.run.floor = static_cast<uint16_t>(act_floor_base(3) + kActFloorSpan - 1);
    ASSERT_EQ(rc.run.ascension, 20);
    ASSERT_EQ(rc.lists.boss_list_count, 3);
    ASSERT_EQ(rc.boss_cursor, 0);
    const uint16_t first_floor = rc.run.floor;

    // run_begin mirrored setBoss(bossList.get(0)) into slot 0; this walk moved
    // the controller to act 3 by hand, so seed the act-3 slot the same way the
    // act transition would (rs.boss_ids[next_act - 1] = boss_list[0]).
    const sts::registry::EncounterDef* first =
        sts::registry::encounter_by_game_id(rc.lists.boss_list[0]);
    const sts::registry::EncounterDef* second =
        sts::registry::encounter_by_game_id(rc.lists.boss_list[1]);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(first->id, second->id)
        << "TheBeyond's three keys are distinct, so the reassignment is "
           "observable rather than a no-op";
    rc.run.boss_ids[2] = static_cast<uint16_t>(first->id);

    weaken_all_monsters(rc);
    play_out_combat_until_the_floor_changes(rc, first_floor);

    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    ASSERT_EQ(rc.run.floor, first_floor + 1);
    ASSERT_EQ(rc.boss_cursor, 1);

    EXPECT_EQ(rc.run.boss_ids[2], static_cast<uint16_t>(second->id))
        << "ProceedButton.java:211 reassigns bossKey to bossList.get(0), which "
           "after the first room's entry pop is the SECOND boss";
    EXPECT_NE(rc.run.boss_ids[2], static_cast<uint16_t>(first->id))
        << "the pre-fix engine kept naming the first boss for the whole second "
           "room and to the terminal";
    // The other two acts' slots are untouched: bossKey is per-DUNGEON and this
    // crossing does not leave TheBeyond.
    EXPECT_EQ(rc.run.boss_ids[0], static_cast<uint16_t>(first->id))
        << "slot 0 is Act 1's own mirror from run_begin, and this walk borrowed "
           "the same list -- the crossing must not have touched it";
    EXPECT_EQ(rc.run.boss_ids[1], 0);

    // And it STAYS moved through the second kill's terminal -- the field the
    // capture keeps attesting for every record from the crossing on.
    weaken_all_monsters(rc);
    play_out_combat(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER));
    EXPECT_EQ(rc.run.boss_ids[2], static_cast<uint16_t>(second->id));
}

// THE NEGATIVE, and it is the one that keeps the fix act-scoped: an Act-2 boss
// kill reaches a remaining count of 2 as well (its bossList is three keys too)
// and is excluded from goToDoubleBoss only by
// `AbstractDungeon.id.equals("TheBeyond")` (ProceedButton.java:101). It must
// therefore leave `boss_ids` alone -- the reassignment lives inside the same
// `act >= kFinalAct` block the whole double-boss branch does, and an Act-2 boss
// goes to its CHEST, not to a second boss room.
TEST(BossVictory, AnActTwoBossVictoryLeavesTheActsBossIdWhereItWas) {
    RunController rc = enter_boss_combat(kSeed);
    rc.run.act = 2;
    rc.run.floor = static_cast<uint16_t>(act_floor_base(2) + kActFloorSpan - 1);
    ASSERT_EQ(rc.run.ascension, 20);
    ASSERT_EQ(rc.boss_cursor, 0);

    const sts::registry::EncounterDef* first =
        sts::registry::encounter_by_game_id(rc.lists.boss_list[0]);
    ASSERT_NE(first, nullptr);
    rc.run.boss_ids[1] = static_cast<uint16_t>(first->id);

    weaken_all_monsters(rc);
    play_out_combat(rc);

    EXPECT_EQ(rc.run.boss_ids[1], static_cast<uint16_t>(first->id))
        << "no reassignment outside TheBeyond";
    EXPECT_NE(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT))
        << "an Act-2 boss kill opens its reward screen; it never crosses into "
           "a second boss room";
}

// =============================================================================
// The emerald-key elite entry roll (G6 campaign 2 spot-diff §8.1)
// =============================================================================
//
// MonsterRoomElite.applyEmeraldEliteBuff (MonsterRoomElite.java:39-68), reached
// from AbstractPlayer.preBattlePrep (AbstractPlayer.java:1602-1605) right after
// monsters.usePreBattleAction(): entering the one elite node setEmeraldElite
// flagged at map generation (AbstractDungeon.java:542-556) rolls
// AbstractDungeon.mapRng.random(0, 3) -- the ONLY mid-run mapRng consumer --
// and queues one buff on every member of the group:
//   0 -> StrengthPower(actNum + 1)            (MonsterRoomElite.java:42-46)
//   1 -> IncreaseMaxHpAction(0.25f)           (:48-52)
//   2 -> MetallicizePower(actNum * 2 + 2)     (:54-58)
//   3 -> RegenerateMonsterPower(1 + actNum*2) (:60-64)
// Six observations across both G6 campaigns (STS00451/STS01068/STS02009); the
// per-seed roll values asserted below are ground truth recovered by inverting
// the captures' post-roll mapRng (s0, s1) one step (XorShift128+ is invertible)
// -- STS00451 rolled 0 (its GremlinNob carries Strength 2 at entry, seq 82),
// STS01068 rolled 0 (Lagavulin: Metallicize 8 + Strength 2, seq 74), STS02009
// rolled 1 (Sentries 49/49/56 == 39/39/45 + round(25%), seq 96).

// The three campaign seeds (artifact headers, crosscheck_ok on all three).
constexpr int64_t kSeedSTS00451 = INT64_C(1790050548826);
constexpr int64_t kSeedSTS01068 = INT64_C(1790050586843);
constexpr int64_t kSeedSTS02009 = INT64_C(1790050629509);

// Enter the act's burning-elite node directly. next_room_transition does not
// re-validate edges (the FloorReseed tests use the same door), so the test can
// walk straight onto the node instead of pathing a whole run to it.
void enter_burning_elite(RunController& rc) {
    ASSERT_NE(rc.emerald_x, kNoEmeraldNode) << "run has no burning elite?";
    rc.run.floor = rc.emerald_y;  // ++floor lands run_cur_row on the node's row
    rc.cur_x = 0;                 // on the grid (not Neow), a no-cursor room kind
    rc.room_type = static_cast<uint8_t>(RoomType::None);
    next_room_transition(rc, rc.emerald_x, /*to_boss=*/false);
}

// All three capture seeds place the burning elite on row 5 -- the captures roll
// on floor 6 (seq 82 / 74 / 96, both campaigns). Pins the placement RECORDING
// (the draw itself was already modelled and oracle-matched, map_rooms.hpp).
TEST(RunEmeraldElite, PlacementMatchesTheCapturesFloorSixEntries) {
    for (const int64_t seed :
         {kSeedSTS00451, kSeedSTS01068, kSeedSTS02009}) {
        RunController rc = run_begin(seed, kA20);
        ASSERT_NE(rc.emerald_x, kNoEmeraldNode) << "seed " << seed;
        EXPECT_EQ(rc.emerald_y, 5) << "seed " << seed;
        EXPECT_EQ(rc.run.map[run_state_map_index(rc.emerald_x, rc.emerald_y)]
                      .room_type,
                  static_cast<uint8_t>(RoomType::Elite))
            << "seed " << seed << ": the recorded node must be an Elite node";
    }
}

// STS00451's roll: 0 -> Strength actNum+1 == 2 on every member. The entry must
// consume EXACTLY one wrapper draw (counter +1, the (s0, s1) of one
// random(0,3)), which is the six-observation §8.1 stream divergence.
TEST(RunEmeraldElite, EntryConsumesOneMapRngDrawAndAppliesStrength) {
    RunController rc = run_begin(kSeedSTS00451, kA20);
    leave_neow(rc);
    RngStream probe = rc.run.map_rng;  // pre-entry (end-of-generateMap) state
    const int32_t expected_roll = random(probe, 0, 3);
    ASSERT_EQ(expected_roll, 0)
        << "capture-derived ground truth for STS00451 (see block comment)";

    enter_burning_elite(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.run.map_rng.counter, probe.counter)
        << "one wrapper draw: MonsterRoomElite.java:41";
    EXPECT_EQ(rc.run.map_rng.s0, probe.s0);
    EXPECT_EQ(rc.run.map_rng.s1, probe.s1);
    ASSERT_GT(rc.combat.monster_count, 0);
    for (uint8_t m = 0; m < rc.combat.monster_count; ++m) {
        const PowerSlot* str = monster_power_slot(rc.combat, m, PowerId::STRENGTH);
        ASSERT_NE(str, nullptr) << "member " << static_cast<int>(m)
                                << " missing the rolled Strength";
        EXPECT_EQ(str->amount, 2) << "actNum + 1 with actNum == 1";
    }
}

// STS02009's roll: 1 -> IncreaseMaxHpAction(0.25f, true) on every member:
// maxHealth += MathUtils.round(maxHealth * 0.25f) and the heal tops current HP
// up by the same amount (AbstractCreature.increaseMaxHp, :199-208). The
// baseline run (burning-elite flag cleared) fights the same HP rolls unbuffed,
// so the pair isolates exactly the buff.
TEST(RunEmeraldElite, MaxHpRollRaisesEveryMemberByTwentyFivePercent) {
    RunController base = run_begin(kSeedSTS02009, kA20);
    RngStream probe = base.run.map_rng;
    ASSERT_EQ(random(probe, 0, 3), 1)
        << "capture-derived ground truth for STS02009 (see block comment)";

    RunController rc = run_begin(kSeedSTS02009, kA20);
    leave_neow(rc);
    enter_burning_elite(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));

    leave_neow(base);
    const uint8_t ex = base.emerald_x;
    const uint8_t ey = base.emerald_y;
    base.emerald_x = kNoEmeraldNode;  // un-flag: same node, no roll
    base.emerald_y = kNoEmeraldNode;
    base.run.floor = ey;
    base.cur_x = 0;
    base.room_type = static_cast<uint8_t>(RoomType::None);
    next_room_transition(base, ex, /*to_boss=*/false);
    ASSERT_EQ(base.phase, static_cast<uint8_t>(RunPhase::COMBAT));

    ASSERT_EQ(rc.combat.monster_count, base.combat.monster_count);
    for (uint8_t m = 0; m < rc.combat.monster_count; ++m) {
        const int16_t unbuffed = base.combat.monsters[m].max_hp;
        const int16_t expect = static_cast<int16_t>(
            unbuffed + mathutils_round(static_cast<float>(unbuffed) * 0.25f));
        EXPECT_EQ(rc.combat.monsters[m].max_hp, expect)
            << "member " << static_cast<int>(m);
        EXPECT_EQ(rc.combat.monsters[m].hp, expect)
            << "increaseMaxHp heals the added amount (hp arrives full)";
    }
    // The baseline consumed no mapRng draw; the buffed entry consumed one.
    EXPECT_EQ(base.run.map_rng.counter + 1, rc.run.map_rng.counter);
}

// A NON-burning elite node rolls nothing -- the gate is the node flag
// (AbstractPlayer.java:1603, getCurrMapNode().hasEmeraldKey), not the room
// kind. Uses whichever other elite node the STS00451 map carries.
TEST(RunEmeraldElite, OtherEliteNodesConsumeNoDraw) {
    RunController rc = run_begin(kSeedSTS00451, kA20);
    leave_neow(rc);
    uint8_t ox = 0xFF, oy = 0xFF;
    for (uint8_t y = 0; y < kMapRows && ox == 0xFF; ++y) {
        for (uint8_t x = 0; x < kMapCols; ++x) {
            if (rc.run.map[run_state_map_index(x, y)].room_type ==
                    static_cast<uint8_t>(RoomType::Elite) &&
                !(x == rc.emerald_x && y == rc.emerald_y)) {
                ox = x;
                oy = y;
                break;
            }
        }
    }
    ASSERT_NE(ox, 0xFF) << "map carries no second elite; pick another seed";
    const RngStream before = rc.run.map_rng;
    rc.run.floor = oy;
    rc.cur_x = 0;
    rc.room_type = static_cast<uint8_t>(RoomType::None);
    next_room_transition(rc, ox, /*to_boss=*/false);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.run.map_rng.counter, before.counter);
    EXPECT_EQ(rc.run.map_rng.s0, before.s0);
    ASSERT_GT(rc.combat.monster_count, 0);
    for (uint8_t m = 0; m < rc.combat.monster_count; ++m) {
        EXPECT_EQ(monster_power_slot(rc.combat, m, PowerId::STRENGTH), nullptr)
            << "no roll happened, so STS00451's Strength arm must not land";
    }
}

// The remaining two arms, found by scanning seeds for the wanted roll value.
// Roll 2 -> Metallicize actNum*2+2 == 4 (stacked ON TOP of any pre-battle
// armour); roll 3 -> RegenerateMonsterPower, whose power id "Regenerate" has NO
// registry row, so the entry consumes the draw and parks at ROOM_UNIMPLEMENTED
// (the documented never-fake seam) rather than fighting a silently wrong fight.
TEST(RunEmeraldElite, MetallicizeRollStacksOnPreBattleArmour) {
    for (int64_t seed = 1; seed < 400; ++seed) {
        RunController rc = run_begin(seed, kA20);
        if (rc.emerald_x == kNoEmeraldNode) continue;
        RngStream probe = rc.run.map_rng;
        if (random(probe, 0, 3) != 2) continue;
        leave_neow(rc);
        const RngStream before = rc.run.map_rng;
        enter_burning_elite(rc);
        ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
        EXPECT_EQ(rc.run.map_rng.counter, before.counter + 1);
        ASSERT_GT(rc.combat.monster_count, 0);
        for (uint8_t m = 0; m < rc.combat.monster_count; ++m) {
            const PowerSlot* met =
                monster_power_slot(rc.combat, m, PowerId::METALLICIZE);
            ASSERT_NE(met, nullptr) << "member " << static_cast<int>(m);
            // Lagavulin already carries Metallicize 8 from usePreBattleAction;
            // ApplyPowerAction STACKS (8 + 4), everyone else reads a plain 4.
            const bool lagavulin =
                rc.combat.monsters[m].monster_id ==
                static_cast<uint16_t>(MonsterId::LAGAVULIN);
            EXPECT_EQ(met->amount, lagavulin ? 12 : 4)
                << "member " << static_cast<int>(m);
        }
        return;  // one witness seed is the test
    }
    FAIL() << "no seed under 400 rolls Metallicize on its burning elite";
}

// Roll 3 -> RegenerateMonsterPower(1 + actNum*2) == 3 at act 1, applied to
// EVERY group member (MonsterRoomElite.java:60-64; PowerId::REGENERATE_MONSTER,
// registry/powers.yaml id 91).
TEST(RunEmeraldElite, RegenerateRollAppliesRegenerateMonsterToEveryMember) {
    for (int64_t seed = 1; seed < 400; ++seed) {
        RunController rc = run_begin(seed, kA20);
        if (rc.emerald_x == kNoEmeraldNode) continue;
        RngStream probe = rc.run.map_rng;
        if (random(probe, 0, 3) != 3) continue;
        leave_neow(rc);
        const RngStream before = rc.run.map_rng;
        enter_burning_elite(rc);
        ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
        EXPECT_EQ(rc.run.map_rng.counter, before.counter + 1)
            << "the draw must still be spent exactly once";
        ASSERT_GT(rc.combat.monster_count, 0);
        for (uint8_t m = 0; m < rc.combat.monster_count; ++m) {
            const PowerSlot* regen = monster_power_slot(
                rc.combat, m, PowerId::REGENERATE_MONSTER);
            ASSERT_NE(regen, nullptr) << "member " << static_cast<int>(m);
            EXPECT_EQ(regen->amount, 3) << "member " << static_cast<int>(m);
        }
        return;  // one witness seed is the test
    }
    FAIL() << "no seed under 400 rolls Regenerate on its burning elite";
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
