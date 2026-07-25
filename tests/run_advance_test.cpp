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

#include "sts/engine/run_advance.hpp"

#include <cstring>
#include <initializer_list>

#include <gtest/gtest.h>

#include "sts/engine/advance.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/map_gen.hpp"
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/potions.hpp"
#include "sts/engine/relic_hooks.hpp"  // RelicHook (the pre-draw emptiness pin)
#include "sts/engine/relics.hpp"       // RelicDef / kRelicDefs
#include "sts/engine/rng_stream.hpp"

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
    step(rc, kProceed);
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
    EXPECT_TRUE(streams_equal(rc.combat.misc_rng, floor0));
}

TEST(RunBegin, RelicRngConsumesFivePoolShuffleDraws) {
    RunController rc = run_begin(kSeed, kA20);
    RngStream expect = from_seed(kSeed);
    for (int i = 0; i < kRelicTierCount; ++i) (void)random_long(expect);
    EXPECT_EQ(rc.run.relic_rng.counter, kRelicTierCount);  // exactly 5
    EXPECT_TRUE(streams_equal(rc.run.relic_rng, expect));
    // B4.6 populated the complete common pool and B3.25 the uncommon pool;
    // B3.26/B3.27 own rare/shop/boss. Empty tiers still consumed their shuffle
    // seed above (all five draws are unconditional).
    EXPECT_EQ(rc.run.relic_pool_count[0], 33);
    EXPECT_EQ(rc.run.relic_pool_count[1], 30);  // B3.25 uncommons
    for (int t = 2; t < kRelicTierCount; ++t) {
        EXPECT_EQ(rc.run.relic_pool_count[t], 0);
    }
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
    EXPECT_EQ(rc.run.hp, 80);
    EXPECT_EQ(rc.run.max_hp, 80);
    EXPECT_EQ(rc.run.gold, 99);
    EXPECT_EQ(rc.run.act, 1);
    EXPECT_EQ(rc.run.floor, 0);
    EXPECT_EQ(rc.run.potion_slots, 2);  // A11 applies at A20 (B4.3 handoff).
    EXPECT_EQ(rc.run.master_deck_count, 10);
    // 5 Strike, 4 Defend, 1 Bash in order.
    int strikes = 0, defends = 0, bashes = 0;
    for (int i = 0; i < 10; ++i) {
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
    // Force the floor-1 room to a non-combat kind so onPlayerEntry reseeds the
    // floor streams and then does NOTHING to them (a clean reseed snapshot). Force
    // every row-0 column so whichever start is picked is the Event room.
    for (int col = 0; col < kMapCols; ++col) {
        rc.run.map[run_state_map_index(col, 0)].room_type =
            static_cast<uint8_t>(RoomType::Event);
    }
    step(rc, kProceed);                 // NEOW -> MAP_CHOICE (floor 0)
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    uint8_t x = first_start_column(rc);
    step(rc, make_action(ActionVerb::CHOOSE, x));

    EXPECT_EQ(rc.run.floor, 1);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED));
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::Event));

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
    // Force rows 0 and 1 to Event so both transitions cleanly stall (no consumption).
    for (int x = 0; x < kMapCols; ++x) {
        rc.run.map[run_state_map_index(x, 0)].room_type =
            static_cast<uint8_t>(RoomType::Event);
        rc.run.map[run_state_map_index(x, 1)].room_type =
            static_cast<uint8_t>(RoomType::Event);
    }
    step(rc, kProceed);
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

    step(rc, kProceed);
    uint8_t x = first_start_column(rc);
    step(rc, make_action(ActionVerb::CHOOSE, x));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT))
        << "floor-1 Jaw Worm room should enter combat";

    // combat_begin over the same seed/floor/deck must be byte-identical.
    CardId deck[10];
    for (int i = 0; i < 10; ++i) deck[i] = static_cast<CardId>(rc.run.master_deck[i].card_id);
    CombatState ref = combat_begin(seed, 1, std::span<const CardId>(deck, 10));

    EXPECT_EQ(std::memcmp(&rc.combat, &ref, sizeof(CombatState)), 0)
        << "run-combat entry drifted from combat_begin for a single Jaw Worm";
}

TEST(RunCombat, LousePreBattleAndInnateResolveBeforePlayerControl) {
    RunController rc = run_begin(find_two_louse_seed(), kA20);
    // Replace one starting card with Writhe: run-created combat must honor the
    // B3.9 Innate flag even though the deck is shuffled before encounter entry.
    rc.run.master_deck[9].card_id = static_cast<uint16_t>(CardId::WRITHE);

    step(rc, kProceed);
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
    step(rc, kProceed);
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
    EXPECT_EQ(rc.combat.player_hp, 80);
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
    step(rc, kProceed);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.combat.player_hp, 52);

    // At full HP the HealAction clamps and the combat starts at max.
    RunController full = enter_jaw_worm_holding({RelicId::BLOOD_VIAL});
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
    step(rc, kProceed);
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
// USE_POTION through run and combat layers
// =============================================================================

TEST(RunPotion, FruitJuiceIsLegalOutsideCombatAndMutatesPersistentHp) {
    RunController rc = run_begin(kSeed, kA20);
    step(rc, kProceed);  // stable non-combat MAP_CHOICE state
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
    step(rc, kProceed);
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
    step(rc, kProceed);
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
    EXPECT_EQ(rc.combat.monsters[0].hp, monster_hp);  // monster was not killed.
    EXPECT_EQ(rc.run.hp, 46);  // AbstractRoom.endBattle still fires Burning Blood.
    EXPECT_EQ(rc.run.potions[0], static_cast<uint16_t>(PotionId::NONE));
    EXPECT_FALSE(result.terminal);
    EXPECT_FLOAT_EQ(result.reward, 0.0f);

    RunActionMask reward_mask{};
    legal_actions(rc, reward_mask);
    EXPECT_TRUE(reward_mask.can_proceed);
    step(rc, kProceed);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
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

// =============================================================================
// Full floor cycle
// =============================================================================

TEST(FullFloorCycle, MapPickCombatRewardNextFloor) {
    const int64_t seed = find_jaw_worm_seed();
    RunController rc = run_begin(seed, kA20);

    // Neow -> map.
    step(rc, kProceed);
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
    EXPECT_TRUE(streams_equal(rc.run.monster_rng, persistent_before.monster_rng));
    EXPECT_TRUE(streams_equal(rc.run.event_rng, persistent_before.event_rng));
    EXPECT_TRUE(streams_equal(rc.run.merchant_rng, persistent_before.merchant_rng));
    EXPECT_TRUE(streams_equal(rc.run.card_rng, persistent_before.card_rng));
    EXPECT_TRUE(streams_equal(rc.run.treasure_rng, persistent_before.treasure_rng));
    EXPECT_TRUE(streams_equal(rc.run.relic_rng, persistent_before.relic_rng));
    EXPECT_TRUE(streams_equal(rc.run.potion_rng, persistent_before.potion_rng));

    const CombatState reward_boundary = rc.combat;
    RunActionMask reward_mask{};
    legal_actions(rc, reward_mask);
    EXPECT_TRUE(reward_mask.can_proceed);

    // Proceed past the reward screen -> back to the map (still floor 1).
    step(rc, kProceed);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
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
    // Make the destination a no-consumption room so the floor-2 boundary is the
    // pristine post-increment reseed, independently comparable stream-by-stream.
    rc.run.map[run_state_map_index(next, 1)].room_type =
        static_cast<uint8_t>(RoomType::Event);
    step(rc, make_action(ActionVerb::CHOOSE, next));
    EXPECT_EQ(rc.run.floor, 2);
    EXPECT_EQ(rc.monster_cursor, 1);  // floor-1 monster room removed on exit
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED));
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
    step(rc, kProceed);
    RunActionMask m{};
    legal_actions(rc, m);
    // Floor 0: legal starts == connected row-0 nodes.
    for (int x = 0; x < kMapCols; ++x) {
        const bool connected = rc.run.map[run_state_map_index(x, 0)].edges != 0;
        EXPECT_EQ(m.can_choose_node[x], connected) << "row-0 col " << x;
    }
}

TEST(RoomRouting, NonCombatRoomsParkAtUnimplemented) {
    for (RoomType kind : {RoomType::Event, RoomType::Shop, RoomType::Rest,
                          RoomType::Treasure}) {
        RunController rc = run_begin(kSeed, kA20);
        for (int x = 0; x < kMapCols; ++x) {
            rc.run.map[run_state_map_index(x, 0)].room_type =
                static_cast<uint8_t>(kind);
        }
        step(rc, kProceed);
        uint8_t x = first_start_column(rc);
        step(rc, make_action(ActionVerb::CHOOSE, x));
        EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED));
        EXPECT_EQ(rc.room_type, static_cast<uint8_t>(kind));
        // Reseed still happened (design: floor streams valid even in unimpl rooms).
        EXPECT_TRUE(streams_equal(rc.combat.misc_rng, floor_stream(kSeed, 1)));
    }
}

TEST(BatchHeterogeneity, MixedPhasesStepIndependently) {
    // A batch with different phases and verbs: NEOW CHOOSE, MAP CHOOSE, combat
    // PLAY_CARD/END_TURN, and non-combat USE_POTION.
    const int64_t jw = find_jaw_worm_seed();
    RunController a = run_begin(kSeed, kA20);                 // NEOW
    RunController b = run_begin(kSeed, kA20);
    step(b, kProceed);                                        // MAP_CHOICE
    RunController c = run_begin(jw, kA20);
    step(c, kProceed);
    step(c, make_action(ActionVerb::CHOOSE, first_start_column(c)));  // COMBAT
    RunController d = run_begin(kSeed + 1, kA20);
    step(d, kProceed);
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
    EXPECT_EQ(runs[3].run.max_hp, 85);
    EXPECT_EQ(runs[3].run.potions[0], static_cast<uint16_t>(PotionId::NONE));
}

}  // namespace
}  // namespace sts::engine
