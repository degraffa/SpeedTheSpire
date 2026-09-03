// T0.5 -- the hidden-twin leak gate (docs/training-plan.md §2.6a/§2.6c).
//
// THE GATE IN ONE SENTENCE: `encode_public_view` of a state and of its
// `make_hidden_twin` must be byte-identical, MASK CHANNEL INCLUDED, in every
// phase. A difference means an observation field -- or a legality bit -- is a
// function of something the player has not seen.
//
// The suite has four parts:
//
//   1. THE SWEEP (`TwinSweep`). Twin equality over >= 1,000 states harvested
//      from the fuzz harness's own run loop (tools/fuzz), across five policies,
//      with a PER-PHASE COUNT asserted for every reachable RunPhase. The count
//      is the load-bearing half: "covering every RunPhase" is a claim, and a
//      sweep that quietly never reached SHOP would pass a phase-blind test.
//      The states come from `fuzz::run_case` + `StepObserver` rather than a
//      private loop, for the reason fuzz_run.hpp gives -- a forked policy loop
//      is a copy that drifts.
//
//   2. REVEAL TIMING (`TwinRevealTiming`). Three hidden-until-revealed
//      quantities -- the treasure chest's contents, a Louse's construction
//      roll, a Match-and-Keep board slot -- each asserted twice: masked and
//      twin-varying BEFORE the revealing action, carried and twin-PINNED after
//      it. Both halves matter. A field that is masked forever passes half of
//      this and is useless; a field that is carried early is the leak.
//
//   3. MASK INVARIANCE (`TwinMask`). The mask is compared directly as well as
//      through the view. It is already a member of PublicView, so part 1 covers
//      it -- but a direct comparison is what makes a mask failure SAY it is a
//      mask failure instead of "byte 5691 differs".
//
//   4. THE KNOWN LEAK (`TwinDrawChoiceLeak`). One characterisation test for the
//      draw-source CHOOSE mask defect that twin.hpp's `draw_choice_pending` pin
//      works around. It asserts the leak STILL EXISTS, so it turns red on the
//      day somebody fixes the action space -- which is the day the pin must be
//      deleted.
//
// VACUITY DISCIPLINE. Every "twin equality holds" assertion in parts 2-4 is
// paired with a check that the twin ACTUALLY MOVED the hidden bytes in
// question. A twin that changed nothing satisfies byte equality perfectly and
// proves nothing, and that is the failure mode a leak gate is most likely to
// rot into.

#include "sts/engine/twin.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/knowledge.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/neow.hpp"
#include "sts/engine/public_view.hpp"
#include "sts/engine/resample.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/treasure_rooms.hpp"
#include "sts/fuzz/case_id.hpp"
#include "sts/fuzz/fuzz_run.hpp"
#include "sts/fuzz/policy.hpp"
#include "sts/registry/event_table.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kA20 = 20;

// --- twin equality, as one reusable assertion --------------------------------

// Encode both views and report the FIRST differing byte with the PublicView
// member that owns it. `where` is the caller's context (phase, seed, step).
::testing::AssertionResult TwinViewsAgree(const RunController& truth,
                                          const RunController& twin,
                                          const std::string& where) {
    PublicView a{};
    PublicView b{};
    encode_public_view(truth, a);
    encode_public_view(twin, b);
    const PublicViewDiff d = public_view_first_difference(a, b);
    if (d.equal) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure()
           << "PublicView differs across a hidden twin at byte " << d.offset
           << " (PublicView." << d.field << ") -- " << where
           << ". That byte is a function of hidden state: either the encoder "
              "carries a hidden field, or a mask bit is computed from one.";
}

// --- run helpers (same shape as resample_test / run_advance_test) -------------

void step(RunController& rc, Action a) {
    StepResult res{};
    advance(std::span<RunController>(&rc, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&res, 1));
}

int64_t find_first_encounter_seed(std::string_view key) {
    for (int64_t s = 1; s < 4000; ++s) {
        RngStream m = from_seed(s);
        MonsterLists ml{};
        generate_monster_lists(1, m, ml);
        if (ml.monster_list_count > 0 &&
            ml.monster_list[0] == encounter_key_id(key)) {
            return s;
        }
    }
    ADD_FAILURE() << "no seed found whose first encounter is " << key;
    return 1;
}

uint8_t first_legal_column(const RunController& rc) {
    RunActionMask m{};
    legal_actions(rc, m);
    for (uint8_t x = 0; x < kMapCols; ++x) {
        if (m.can_choose_node[x]) return x;
    }
    ADD_FAILURE() << "no legal map column";
    return 0;
}

// Leave Neow without taking a blessing: a payout would move streams, the deck
// and the relic pools underneath the test (the run_advance_test rationale).
RunController at_map_choice(int64_t seed) {
    RunController rc = run_begin(seed, kA20);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::DONE);
    step(rc, make_action(ActionVerb::CHOOSE));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    return rc;
}

RunController in_combat(int64_t seed) {
    RunController rc = at_map_choice(seed);
    step(rc, make_action(ActionVerb::CHOOSE, first_legal_column(rc)));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    return rc;
}

// =============================================================================
// 1. The sweep
// =============================================================================

struct Sweep {
    std::map<uint8_t, int> per_phase;  // RunPhase -> states observed
    int states = 0;
    int failures = 0;
    std::string first_failure;
    // The immediately preceding controller, so the ONE duplicate the
    // StepObserver contract allows (the terminal state is reported twice) is
    // not counted twice. Every property below is idempotent anyway; the dedupe
    // exists so the ">= 1,000 states" figure is honest.
    std::vector<unsigned char> prev;
};

void observe(const RunController& rc, void* ctx) noexcept {
    Sweep& s = *static_cast<Sweep*>(ctx);
    const auto* bytes = reinterpret_cast<const unsigned char*>(&rc);
    if (s.prev.size() == sizeof(RunController) &&
        std::memcmp(s.prev.data(), bytes, sizeof(RunController)) == 0) {
        return;  // the allowed duplicate terminal observation
    }
    s.prev.assign(bytes, bytes + sizeof(RunController));

    ++s.states;
    ++s.per_phase[rc.phase];

    // One twin seed per state, mixed from the state's own step index so the
    // sweep exercises many twins rather than one repeated draw. It is a plain
    // counter, not an engine value: nothing here may consume an engine stream.
    const int64_t twin_seed = 0x51D5'7A11 + static_cast<int64_t>(s.states) * 7919;
    const RunController twin = make_hidden_twin(rc, twin_seed);

    PublicView a{};
    PublicView b{};
    encode_public_view(rc, a);
    encode_public_view(twin, b);
    const PublicViewDiff d = public_view_first_difference(a, b);
    if (!d.equal) {
        ++s.failures;
        if (s.first_failure.empty()) {
            s.first_failure = "phase " + std::to_string(rc.phase) +
                              ", state " + std::to_string(s.states) +
                              ", byte " + std::to_string(d.offset) +
                              " (PublicView." + d.field + ")";
        }
    }
}

TEST(TwinSweep, PublicViewAndMaskAreByteIdenticalInEveryRunPhase) {
    Sweep sweep;
    fuzz::StepObserver obs;
    obs.fn = &observe;
    obs.ctx = &sweep;

    fuzz::RunLimits limits;

    // Five policies x 24 seeds. The policy mix is what buys phase coverage:
    // ALWAYS_EVENT steers into ?-rooms and shops, HOARD_GOLD refuses cards and
    // survives longer, the greedy pair reach rewards and campfires. The seed
    // span is fixed so the sweep is reproducible.
    const fuzz::PolicyKind policies[] = {
        fuzz::PolicyKind::RANDOM, fuzz::PolicyKind::GREEDY_DAMAGE,
        fuzz::PolicyKind::GREEDY_BLOCK, fuzz::PolicyKind::HOARD_GOLD,
        fuzz::PolicyKind::ALWAYS_EVENT};

    for (fuzz::PolicyKind p : policies) {
        for (int64_t seed = 1; seed <= 24; ++seed) {
            fuzz::CaseId id;
            id.run_seed = seed;
            id.ascension = kA20;
            id.policy = p;
            id.policy_seed = static_cast<uint64_t>(seed) * 1000003ull + 17ull;
            fuzz::CaseResult result;
            // verify_repro=false: pass C re-drives pass A's literal action log,
            // which is triage machinery this suite is not exercising and would
            // cost a third of its wall clock. The observer only ever sees pass
            // A either way (fuzz_run.hpp).
            fuzz::run_case(id, limits, nullptr, result, /*verify_repro=*/false,
                           fuzz::Inject{}, obs);
        }
    }

    EXPECT_EQ(sweep.failures, 0)
        << sweep.failures << " of " << sweep.states
        << " states leaked. First: " << sweep.first_failure;

    // The acceptance bar: >= 1,000 states, every reachable phase represented.
    EXPECT_GE(sweep.states, 1000);

    // RunPhase::NONE is not a live phase -- run_begin lands on NEOW and nothing
    // returns to 0 -- so it is asserted ABSENT rather than covered. If it ever
    // appears, a phase transition lost its destination.
    EXPECT_EQ(sweep.per_phase[static_cast<uint8_t>(RunPhase::NONE)], 0)
        << "RunPhase::NONE is not supposed to be reachable";

    // RunPhase::ROOM_UNIMPLEMENTED is the park for content this engine has not
    // translated (run_advance.cpp's `stall`, and an event with no registered
    // body). At S1 completeness NOTHING in Act 1 stalls, so a policy walk
    // cannot reach it -- 10k+ harvested states produced none. It is covered by
    // the directed `TwinPhaseCoverage.UnimplementedRoomParkIsTwinInvariant`
    // below instead of by widening the sweep until a rarity turns up, and it is
    // deliberately NOT asserted absent: content work retires stalls, it does
    // not create them, but a future act may stall again and that is not a
    // failure of this test.

    struct Expect {
        RunPhase phase;
        const char* name;
    };
    const Expect required[] = {
        {RunPhase::NEOW, "NEOW"},
        {RunPhase::MAP_CHOICE, "MAP_CHOICE"},
        {RunPhase::COMBAT, "COMBAT"},
        {RunPhase::COMBAT_REWARD, "COMBAT_REWARD"},
        {RunPhase::RUN_OVER, "RUN_OVER"},
        {RunPhase::REST_SITE, "REST_SITE"},
        {RunPhase::TREASURE_ROOM, "TREASURE_ROOM"},
        {RunPhase::EVENT_DIALOG, "EVENT_DIALOG"},
        {RunPhase::SHOP, "SHOP"},
    };
    for (const Expect& e : required) {
        EXPECT_GT(sweep.per_phase[static_cast<uint8_t>(e.phase)], 0)
            << "no state was harvested in RunPhase::" << e.name
            << " -- the 'every RunPhase' claim is unchecked for it. Widen the "
               "seed span or the policy mix rather than dropping the row.";
    }

    // Printed, not asserted, beyond the bars above: the per-phase table is the
    // number a reader of this task's Log wants.
    std::string table;
    for (const auto& kv : sweep.per_phase) {
        table += " phase" + std::to_string(kv.first) + "=" +
                 std::to_string(kv.second);
    }
    GTEST_LOG_(INFO) << "twin sweep: " << sweep.states << " states;" << table;
}

TEST(TwinSweep, SameSeedYieldsAnIdenticalTwin) {
    const RunController truth = in_combat(find_first_encounter_seed("Jaw Worm"));
    const RunController a = make_hidden_twin(truth, 12345);
    const RunController b = make_hidden_twin(truth, 12345);
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(RunController)), 0)
        << "make_hidden_twin must be a pure function of (state, seed)";
    const RunController c = make_hidden_twin(truth, 12346);
    EXPECT_NE(std::memcmp(&a, &c, sizeof(RunController)), 0)
        << "two different twin seeds produced the same twin";
}

TEST(TwinSweep, ResolutionQueuesAndTurnFlagAreNeverPerturbed) {
    // T0.2's binding note: the queues and `turn_has_ended` are DERIVED and
    // legitimately move the mask, so they are not twin material.
    const RunController truth = in_combat(find_first_encounter_seed("Jaw Worm"));
    for (int64_t seed = 1; seed <= 20; ++seed) {
        const RunController t = make_hidden_twin(truth, seed);
        EXPECT_EQ(std::memcmp(t.combat.action_queue, truth.combat.action_queue,
                              sizeof(truth.combat.action_queue)),
                  0);
        EXPECT_EQ(t.combat.action_head, truth.combat.action_head);
        EXPECT_EQ(t.combat.action_tail, truth.combat.action_tail);
        EXPECT_EQ(t.combat.action_count, truth.combat.action_count);
        EXPECT_EQ(std::memcmp(t.combat.pre_turn_actions,
                              truth.combat.pre_turn_actions,
                              sizeof(truth.combat.pre_turn_actions)),
                  0);
        EXPECT_EQ(t.combat.turn_has_ended, truth.combat.turn_has_ended);
        EXPECT_EQ(std::memcmp(t.combat.card_queue, truth.combat.card_queue,
                              sizeof(truth.combat.card_queue)),
                  0);
        EXPECT_EQ(std::memcmp(t.combat.monster_queue, truth.combat.monster_queue,
                              sizeof(truth.combat.monster_queue)),
                  0);
    }
}

// The one phase a policy walk cannot reach at S1 completeness. Both shapes the
// engine parks in are built the way run_advance.cpp's `stall` and
// event_framework.cpp's no-body branch build them: a zeroed combat plus the
// phase, and a resolved event whose EventId is retained so the room the player
// is standing in stays observable (audit §8.5's gate).
TEST(TwinPhaseCoverage, UnimplementedRoomParkIsTwinInvariant) {
    RunController stalled = at_map_choice(313);
    stalled.combat = CombatState{};
    stalled.phase = static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED);
    stalled.room_type = static_cast<uint8_t>(RoomType::Monster);

    RunController parked_event = at_map_choice(314);
    parked_event.combat = CombatState{};
    parked_event.phase = static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED);
    parked_event.room_type = static_cast<uint8_t>(RoomType::Event);
    parked_event.event.event_id =
        static_cast<uint16_t>(sts::registry::EventId::THE_CLERIC);

    for (const RunController* truth : {&stalled, &parked_event}) {
        for (int64_t seed = 1; seed <= 25; ++seed) {
            const RunController t = make_hidden_twin(*truth, seed);
            ASSERT_TRUE(TwinViewsAgree(*truth, t,
                                       "ROOM_UNIMPLEMENTED park, room_type " +
                                           std::to_string(truth->room_type) +
                                           ", seed " + std::to_string(seed)));
        }
    }
}

// =============================================================================
// 2. Reveal timing
// =============================================================================

RunController at_unopened_chest(int64_t seed) {
    RunController rc = at_map_choice(seed);
    rc.phase = static_cast<uint8_t>(RunPhase::TREASURE_ROOM);
    rc.room_type = static_cast<uint8_t>(RoomType::Treasure);
    rc.treasure_chest = TreasureChest{static_cast<uint8_t>(ChestSize::MEDIUM),
                                      static_cast<uint8_t>(RelicTier::COMMON),
                                      /*has_gold=*/1, /*opened=*/0};
    return rc;
}

TEST(TwinRevealTiming, ChestContentsAreTwinVariantBeforeTheOpenAndPinnedAfter) {
    const RunController closed = at_unopened_chest(41);

    std::set<int> tiers;
    std::set<int> golds;
    for (int64_t seed = 1; seed <= 40; ++seed) {
        const RunController t = make_hidden_twin(closed, seed);
        ASSERT_TRUE(TwinViewsAgree(closed, t, "unopened chest, seed " +
                                                  std::to_string(seed)));
        EXPECT_EQ(t.treasure_chest.size, closed.treasure_chest.size)
            << "the chest SIZE is drawn on the room and must be preserved";
        EXPECT_EQ(t.treasure_chest.opened, 0);
        tiers.insert(t.treasure_chest.relic_tier);
        golds.insert(t.treasure_chest.has_gold);
    }
    // Not vacuous: the twin really did redraw the masked contents.
    EXPECT_GT(tiers.size(), 1u)
        << "no twin changed the unopened chest's relic tier -- the equality "
           "above would then be trivially true";
    EXPECT_EQ(golds.size(), 2u);

    // The reveal. Once opened, the contents ARE the observation: they are
    // carried by the view and must be identical in every twin.
    RunController opened = closed;
    opened.treasure_chest.opened = 1;
    PublicView pv{};
    encode_public_view(opened, pv);
    EXPECT_EQ(pv.chest_opened, 1);
    EXPECT_EQ(pv.chest_relic_tier, opened.treasure_chest.relic_tier);
    EXPECT_EQ(pv.chest_has_gold, opened.treasure_chest.has_gold);

    for (int64_t seed = 1; seed <= 20; ++seed) {
        const RunController t = make_hidden_twin(opened, seed);
        EXPECT_EQ(t.treasure_chest.relic_tier, opened.treasure_chest.relic_tier)
            << "an OPENED chest's contents are public and must be pinned";
        EXPECT_EQ(t.treasure_chest.has_gold, opened.treasure_chest.has_gold);
        ASSERT_TRUE(TwinViewsAgree(opened, t, "opened chest"));
    }
}

TEST(TwinRevealTiming, LouseConstructionRollIsTwinVariantUntilItIsTelegraphed) {
    RunController truth = in_combat(find_first_encounter_seed("2 Louse"));
    ASSERT_GE(truth.combat.monster_count, 2);

    // Slot 0 telegraphed its bite (public forever), slot 1 has not.
    truth.knowledge.monster_roll_known[0] = 1;
    truth.knowledge.monster_roll[0] = truth.combat.monsters[0].pad0;
    truth.knowledge.monster_roll_known[1] = 0;

    PublicView pv{};
    encode_public_view(truth, pv);
    EXPECT_EQ(pv.monster_roll_known[0], 1);
    EXPECT_EQ(pv.monster_roll[0], truth.combat.monsters[0].pad0);
    EXPECT_EQ(pv.monster_roll_known[1], 0);
    EXPECT_EQ(pv.monster_roll[1], 0)
        << "an unrevealed construction roll leaked into PublicView";

    std::set<int> unrevealed;
    for (int64_t seed = 1; seed <= 40; ++seed) {
        const RunController t = make_hidden_twin(truth, seed);
        ASSERT_TRUE(TwinViewsAgree(truth, t, "Louse roll, seed " +
                                                 std::to_string(seed)));
        EXPECT_EQ(t.combat.monsters[0].pad0, truth.combat.monsters[0].pad0)
            << "a telegraphed roll is public and must be pinned";
        unrevealed.insert(t.combat.monsters[1].pad0);
    }
    EXPECT_GT(unrevealed.size(), 1u)
        << "the unrevealed roll was never redrawn -- the equality above would "
           "then be trivially true";
}

TEST(TwinRevealTiming, MatchAndKeepFaceDownSlotsAreTwinVariantAndFlipsArePinned) {
    RunController truth = at_map_choice(77);
    truth.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
    truth.room_type = static_cast<uint8_t>(RoomType::Event);
    EventDialogState& es = truth.event;
    es = EventDialogState{};
    es.event_id =
        static_cast<uint16_t>(sts::registry::EventId::MATCH_AND_KEEP);
    es.screen = 2;    // PLAY
    es.scratch0 = 4;  // attempts left
    es.scratch1 = 5;  // slot 5 is face up
    const uint16_t ids[kEventBoardCap] = {10, 11, 12, 13, 14, 15,
                                          10, 11, 12, 13, 14, 15};
    for (int i = 0; i < kEventBoardCap; ++i) {
        es.board[i].card_id = ids[i];
    }
    es.board[3].taken = 1;
    es.board[9].taken = 1;

    PublicView pv{};
    encode_public_view(truth, pv);
    EXPECT_EQ(pv.event.board[5].revealed, 1) << "the face-up card is on screen";
    EXPECT_EQ(pv.event.board[3].revealed, 1) << "a matched pair left the board";
    EXPECT_EQ(pv.event.board[0].revealed, 0);
    EXPECT_EQ(pv.event.board[0].card_id, 0u)
        << "a face-down Match-and-Keep card leaked into PublicView";

    bool moved = false;
    for (int64_t seed = 1; seed <= 30; ++seed) {
        const RunController t = make_hidden_twin(truth, seed);
        ASSERT_TRUE(TwinViewsAgree(truth, t, "Match and Keep, seed " +
                                                 std::to_string(seed)));
        EXPECT_EQ(t.event.board[5].card_id, truth.event.board[5].card_id)
            << "the face-up card is revealed and must be pinned";
        EXPECT_EQ(t.event.board[3].card_id, truth.event.board[3].card_id);
        for (int i = 0; i < kEventBoardCap; ++i) {
            if (t.event.board[i].card_id != truth.event.board[i].card_id) {
                moved = true;
            }
        }
    }
    EXPECT_TRUE(moved)
        << "no twin permuted the face-down board -- the equality above would "
           "then be trivially true";
}

// =============================================================================
// 3. Mask invariance, compared directly
// =============================================================================

TEST(TwinMask, RunAndCombatMasksAreByteIdenticalAcrossTwins) {
    const RunController combat = in_combat(find_first_encounter_seed("Jaw Worm"));
    const RunController map = at_map_choice(101);
    const RunController chest = at_unopened_chest(41);

    for (const RunController* truth : {&combat, &map, &chest}) {
        RunActionMask expected{};
        legal_actions(*truth, expected);
        for (int64_t seed = 1; seed <= 25; ++seed) {
            const RunController t = make_hidden_twin(*truth, seed);
            RunActionMask got{};
            legal_actions(t, got);
            EXPECT_EQ(std::memcmp(&expected, &got, sizeof(RunActionMask)), 0)
                << "a legality bit moved with hidden state (phase "
                << static_cast<int>(truth->phase) << ", twin seed " << seed
                << "). A mask bit computed from hidden state is a leak the "
                   "observation-equality test alone cannot catch (plan 2.1).";
        }
    }
}

// =============================================================================
// 4. The known draw-source CHOOSE leak
// =============================================================================

// Build a combat whose queue holds an open, type-filtered DRAW-source choice.
// Rather than acquire Secret Technique (registry SECRET_TECHNIQUE -- real S1
// content, but a colorless rare a fuzz walk almost never sees), the queue item
// is written directly: `legal_actions` reads the queue head, so this is the
// state that card produces. Nothing here advances the engine, so a hand-built
// queue cannot mislead anything but this test.
bool arm_draw_source_choice(RunController& rc) {
    CombatState& s = rc.combat;

    // Collect every live pool index into ONE pile, so the draw pile holds both
    // SKILLs and ATTACKs and the kHandCap choose window covers all of it.
    std::vector<CardPoolIndex> all;
    const auto take = [&all](const CardPoolIndex* p, uint8_t n) {
        for (uint8_t i = 0; i < n; ++i) all.push_back(p[i]);
    };
    take(s.hand, s.hand_count);
    take(s.draw, s.draw_count);
    take(s.discard, s.discard_count);
    if (all.size() < 4 || all.size() > kDrawCap) {
        return false;
    }

    std::memset(s.hand, 0, sizeof(s.hand));
    std::memset(s.discard, 0, sizeof(s.discard));
    std::memset(s.draw, 0, sizeof(s.draw));
    s.hand_count = 0;
    s.discard_count = 0;
    for (std::size_t i = 0; i < all.size(); ++i) {
        s.draw[i] = all[i];
    }
    s.draw_count = static_cast<uint8_t>(all.size());

    int skills = 0;
    for (uint8_t i = 0; i < s.draw_count; ++i) {
        const CardDef* def = card_def(
            static_cast<CardId>(s.card_pool[s.draw[i]].card_id));
        if (def != nullptr && def->type == CardType::SKILL) {
            ++skills;
        }
    }
    if (skills < 2 || skills == s.draw_count) {
        return false;  // the mask must be able to tell the slots apart
    }

    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    s.action_head = 0;
    s.action_tail = 1;
    s.action_count = 1;
    s.action_queue[0] = ActionQueueItem{
        static_cast<uint16_t>(Opcode::CHOOSE_CARD), 0, 0, 1,
        make_choose_flags(ChoiceKind::DRAW_TO_HAND, /*random=*/false,
                          /*copies=*/1,
                          static_cast<uint8_t>(CardType::SKILL),
                          /*optional=*/false)};
    // The knowledge chain indexes the pile this test just rebuilt; a stale
    // entry would pin a phantom position in resample_draw_order.
    rc.knowledge = KnowledgeState{};
    return true;
}

TEST(TwinDrawChoiceLeak, MaskReadsRawDrawSlotsWhileADrawSourcedChoiceIsOpen) {
    RunController rc = in_combat(find_first_encounter_seed("Jaw Worm"));
    if (!arm_draw_source_choice(rc)) {
        GTEST_SKIP() << "no draw-source choice could be armed in this build";
    }
    ASSERT_TRUE(draw_choice_pending(rc));

    RunActionMask before{};
    legal_actions(rc, before);

    // Permute the draw pile the way a twin would if the pin were absent. If the
    // mask is order-independent this is a no-op and the test's premise is gone.
    RunController permuted = rc;
    SamplerRng rng = sampler_rng_from_seed(4242);
    resample_draw_order(permuted.combat, permuted.knowledge, rng);
    ASSERT_NE(std::memcmp(permuted.combat.draw, rc.combat.draw,
                          sizeof(rc.combat.draw)),
              0)
        << "the permutation did not move the pile; widen the test pile";

    RunActionMask after{};
    legal_actions(permuted, after);

    EXPECT_NE(std::memcmp(&before, &after, sizeof(RunActionMask)), 0)
        << "KNOWN LEAK FIXED? `can_choose[i]` for a DRAW-source choice no "
           "longer depends on the draw pile's ARRANGEMENT. That is the repair "
           "twin.hpp's `draw_choice_pending` pin is waiting for: delete the "
           "pin, delete this test, and re-run the sweep with the pile free.";

    // And with the pin in place the gate is green on the state anyway.
    for (int64_t seed = 1; seed <= 10; ++seed) {
        const RunController t = make_hidden_twin(rc, seed);
        EXPECT_EQ(std::memcmp(t.combat.draw, rc.combat.draw,
                              sizeof(rc.combat.draw)),
                  0)
            << "the draw pin did not hold";
        ASSERT_TRUE(TwinViewsAgree(rc, t, "pinned draw-source choice"));
    }
}

// =============================================================================
// The diagnostic table behind every failure message above
// =============================================================================

TEST(TwinDiagnostics, PublicViewFieldTableIsOrderedAndReachesTheEnd) {
    const std::size_t n = public_view_field_count();
    ASSERT_GT(n, 0u);
    EXPECT_EQ(public_view_field(0).offset, 0u)
        << "the field table must start at byte 0";
    for (std::size_t i = 1; i < n; ++i) {
        const PublicViewFieldSpan prev = public_view_field(i - 1);
        const PublicViewFieldSpan cur = public_view_field(i);
        EXPECT_GT(cur.offset, prev.offset)
            << "PublicView field table is out of order at '" << cur.name
            << "' -- every failure message after it would name the wrong field";
    }
    // The last entry must be the last member, so no byte falls off the table.
    // v3 (S2.13) tail-appended event_flags_hi AFTER the mask channel, so the
    // table's last row moved with it -- which is exactly what this assertion
    // exists to force.
    EXPECT_EQ(public_view_field(n - 1).offset,
              offsetof(PublicView, event_flags_hi))
        << "a PublicView member was appended without a diagnostic-table row";
    EXPECT_STREQ(public_view_field_at(sizeof(PublicView) - 1), "event_flags_hi");
    EXPECT_STREQ(public_view_field_at(offsetof(PublicView, action_mask)),
                 "action_mask");
    EXPECT_STREQ(public_view_field_at(sizeof(PublicView)), "<out of range>");
}

}  // namespace
}  // namespace sts::engine
