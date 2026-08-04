// KnowledgeState + observability transforms (training-plan §2.2/§2.3).
//
// The named information-layer acceptance cases:
//   * Headbutt place -> known top (exact position knowledge);
//   * shuffle clears order knowledge;
//   * Wild Strike after Headbutt weakens known-top to a RELATIVE-order (not
//     absolute) constraint -- the declared interleaving contract
//     (knowledge.hpp's contract note);
//   * Frozen Eye reveals the full draw order and the reveal survives
//     in-combat draws;
// plus the reveal path for monster construction rolls (Louse bite damage at
// the BITE telegraph, suppressed under Runic Dome), the generated
// observability membership table, and the zero-perturbation guarantee
// (recording knowledge never changes CombatState bytes or any RNG stream).

#include <cstdint>
#include <cstring>
#include <span>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/knowledge.hpp"
#include "sts/engine/monster_louse.hpp"
#include "sts/engine/piles.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/state_hash.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"
#include "sts/registry/relic_table.hpp"

namespace sts::engine {
namespace {

using sts::registry::ObservabilityTransform;

// One Jaw Worm; `id` as the sole hand card at pool index 0 (the shape the
// card-behavior suites use).
CombatState MakeState(CardId id, uint8_t cost) {
    CombatState s{};
    s.card_pool[0].card_id = static_cast<uint16_t>(id);
    s.card_pool[0].cost_now = cost;
    s.hand[0] = 0;
    s.hand_count = 1;
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = 3;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = 50;
    s.monsters[0].max_hp = 50;
    s.monster_attacks_queued = 1;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    // Streams seeded so card_random_rng draws are well-defined.
    s.shuffle_rng = from_seed(7);
    s.card_random_rng = from_seed(7);
    s.misc_rng = from_seed(7);
    return s;
}

void AddRelic(CombatState& s, RelicId id) {
    s.relics[s.relic_count].relic_id = static_cast<uint16_t>(id);
    s.relics[s.relic_count].counter = -1;
    ++s.relic_count;
}

// Seed pool rows 5.. with `n` filler Strikes into the DRAW pile (unknown
// cards below/above whatever the test then places).
void SeedDraw(CombatState& s, int n) {
    for (int i = 0; i < n; ++i) {
        const int pool = 5 + i;
        s.card_pool[pool].card_id = static_cast<uint16_t>(CardId::STRIKE);
        s.card_pool[pool].cost_now = 1;
        s.draw[s.draw_count++] = static_cast<CardPoolIndex>(pool);
    }
}

// Play hand slot 0 at monster 0 and pump to quiescence.
void PlayCardZero(CombatState& s) {
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
}

// --- Headbutt: place -> known top --------------------------------------------

TEST(Knowledge, HeadbuttPlaceKnownTop) {
    CombatState s = MakeState(CardId::HEADBUTT, 1);
    SeedDraw(s, 3);  // three unknown cards already in the pile
    // One retrievable discard card (pool 20) -> the choice is forced/auto.
    s.card_pool[20].card_id = static_cast<uint16_t>(CardId::DEFEND);
    s.discard[0] = 20;
    s.discard_count = 1;

    KnowledgeState k{};
    KnowledgeScope scope(&k);
    PlayCardZero(s);

    ASSERT_EQ(s.draw_count, 4);
    ASSERT_EQ(s.draw[s.draw_count - 1], 20) << "Defend moved to the pile top";
    ASSERT_EQ(k.chain_count, 1);
    EXPECT_EQ(k.chain[0], 20);
    EXPECT_EQ(k.exact_prefix, 1) << "the placed card's position is EXACT (top)";
    EXPECT_EQ(k.full_order, 0) << "the three filler cards stay unknown";
}

TEST(Knowledge, HeadbuttPromptedChoiceAlsoKnownTop) {
    CombatState s = MakeState(CardId::HEADBUTT, 1);
    SeedDraw(s, 2);
    s.card_pool[20].card_id = static_cast<uint16_t>(CardId::DEFEND);
    s.card_pool[21].card_id = static_cast<uint16_t>(CardId::BASH);
    s.discard[0] = 20;
    s.discard[1] = 21;
    s.discard_count = 2;  // >= 2 -> a real gridSelect prompt

    KnowledgeState k{};
    KnowledgeScope scope(&k);
    PlayCardZero(s);

    ActionMask m{};
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_pending);
    ASSERT_TRUE(m.choice_from_discard);
    Action choose = make_action(ActionVerb::CHOOSE, 1);  // pick pool 21 (Bash)
    StepResult r{};
    advance(std::span<CombatState>(&s, 1), std::span<const Action>(&choose, 1),
            std::span<StepResult>(&r, 1));

    ASSERT_EQ(s.draw[s.draw_count - 1], 21);
    ASSERT_EQ(k.chain_count, 1);
    EXPECT_EQ(k.chain[0], 21);
    EXPECT_EQ(k.exact_prefix, 1);
}

// --- Shuffle clears ------------------------------------------------------------

TEST(Knowledge, ShuffleClears) {
    CombatState s = MakeState(CardId::HEADBUTT, 1);
    SeedDraw(s, 3);
    s.card_pool[20].card_id = static_cast<uint16_t>(CardId::DEFEND);
    s.discard[0] = 20;
    s.discard_count = 1;

    KnowledgeState k{};
    KnowledgeScope scope(&k);
    PlayCardZero(s);
    ASSERT_EQ(k.chain_count, 1) << "precondition: Headbutt recorded a known top";

    // The played Headbutt filed to the discard, so the reshuffle is real
    // (non-empty discard -> the pile is rewritten).
    ASSERT_GT(s.discard_count, 0);
    shuffle_discard_into_draw(s);

    EXPECT_EQ(k.chain_count, 0) << "shuffle clears all order knowledge";
    EXPECT_EQ(k.exact_prefix, 0);
    EXPECT_EQ(k.full_order, 0);
}

// --- Wild Strike after Headbutt: relative, not absolute -----------------------

TEST(Knowledge, WildStrikeAfterHeadbuttYieldsRelativeOrderConstraint) {
    CombatState s = MakeState(CardId::HEADBUTT, 1);
    SeedDraw(s, 3);
    s.card_pool[20].card_id = static_cast<uint16_t>(CardId::DEFEND);
    s.discard[0] = 20;
    s.discard_count = 1;

    KnowledgeState k{};
    KnowledgeScope scope(&k);
    PlayCardZero(s);
    ASSERT_EQ(k.chain_count, 1);
    ASSERT_EQ(k.exact_prefix, 1) << "precondition: known top after Headbutt";

    // Wild Strike: damage + a Wound at a random draw-pile spot (one
    // card_random_rng draw -- MAKE_CARD to DRAW_RANDOM).
    s.card_pool[1].card_id = static_cast<uint16_t>(CardId::WILD_STRIKE);
    s.card_pool[1].cost_now = 1;
    s.hand[s.hand_count++] = 1;
    ASSERT_TRUE(queue_card_play(s, s.hand_count - 1, 0));
    pump(s);

    ASSERT_EQ(s.draw_count, 5) << "the Wound joined the pile";
    // The constraint that survives is RELATIVE order membership, not an
    // absolute index: the Defend is still constrained (in the pile, ordered
    // relative to any other chain member), but no position is exact.
    ASSERT_EQ(k.chain_count, 1);
    EXPECT_EQ(k.chain[0], 20) << "the Headbutt'd card keeps its relative slot";
    EXPECT_EQ(k.exact_prefix, 0)
        << "random-position insertion kills every absolute-position claim "
           "(the declared interleaving contract, knowledge.hpp)";
    EXPECT_EQ(k.full_order, 0);
}

TEST(Knowledge, RandomInsertIntoEmptyPileIsExactlyKnown) {
    CombatState s = MakeState(CardId::WILD_STRIKE, 1);
    ASSERT_EQ(s.draw_count, 0);
    KnowledgeState k{};
    KnowledgeScope scope(&k);
    PlayCardZero(s);
    ASSERT_EQ(s.draw_count, 1);
    // The "random" spot in an empty pile is the only spot: full knowledge.
    EXPECT_EQ(k.chain_count, 1);
    EXPECT_EQ(k.chain[0], s.draw[0]);
    EXPECT_EQ(k.exact_prefix, 1);
    EXPECT_EQ(k.full_order, 1);
}

// --- Frozen Eye: full order, surviving draws -----------------------------------

TEST(Knowledge, FrozenEyeRevealsFullOrderAndSurvivesDraws) {
    CombatState s = MakeState(CardId::HEADBUTT, 1);
    AddRelic(s, RelicId::FROZEN_EYE);
    // Six cards in the discard; the reshuffle rewrites the pile.
    for (int i = 0; i < 6; ++i) {
        const int pool = 30 + i;
        s.card_pool[pool].card_id = static_cast<uint16_t>(CardId::STRIKE);
        s.discard[s.discard_count++] = static_cast<CardPoolIndex>(pool);
    }

    KnowledgeState k{};
    KnowledgeScope scope(&k);
    shuffle_discard_into_draw(s);

    ASSERT_EQ(s.draw_count, 6);
    ASSERT_EQ(k.full_order, 1) << "REVEAL_DRAW_ORDER re-reveals after a shuffle";
    ASSERT_EQ(k.chain_count, 6);
    ASSERT_EQ(k.exact_prefix, 6);
    for (uint8_t i = 0; i < 6; ++i) {
        EXPECT_EQ(k.chain[i], s.draw[s.draw_count - 1 - i])
            << "chain[" << int(i) << "] is the (i+1)-th card from the top";
    }

    // Draw twice: the reveal SURVIVES -- the remaining order stays fully known.
    const int drawn = draw_cards(s, 2);
    ASSERT_EQ(drawn, 2);
    ASSERT_EQ(s.draw_count, 4);
    EXPECT_EQ(k.full_order, 1) << "in-combat draws do not lose the reveal";
    ASSERT_EQ(k.chain_count, 4);
    ASSERT_EQ(k.exact_prefix, 4);
    for (uint8_t i = 0; i < 4; ++i) {
        EXPECT_EQ(k.chain[i], s.draw[s.draw_count - 1 - i]);
    }
}

TEST(Knowledge, FrozenEyeWatchesARandomInsertionLand) {
    CombatState s = MakeState(CardId::WILD_STRIKE, 1);
    AddRelic(s, RelicId::FROZEN_EYE);
    SeedDraw(s, 4);
    KnowledgeState k{};
    KnowledgeScope scope(&k);
    PlayCardZero(s);
    ASSERT_EQ(s.draw_count, 5);
    // With the true-order view, the insertion is not information loss -- the
    // player just looks again.
    EXPECT_EQ(k.full_order, 1);
    ASSERT_EQ(k.chain_count, 5);
    for (uint8_t i = 0; i < 5; ++i) {
        EXPECT_EQ(k.chain[i], s.draw[s.draw_count - 1 - i]);
    }
}

// --- Monster construction rolls (Louse bite) -----------------------------------

TEST(Knowledge, LouseBiteRollRevealedAtBiteTelegraph) {
    // Drive real Louse AI across seeds until both first-telegraph branches
    // (BITE and non-BITE) have been exercised.
    bool saw_bite_first = false;
    bool saw_move4_first = false;
    for (int64_t seed = 1; seed <= 40 && !(saw_bite_first && saw_move4_first);
         ++seed) {
        CombatState s{};
        s.monster_count = 1;
        s.monster_hp_rng = from_seed(seed);
        s.ai_rng = from_seed(seed);
        KnowledgeState k{};
        KnowledgeScope scope(&k);
        louse_normal_init(s, 0);
        const MonsterState& m = s.monsters[0];
        const bool bite_first =
            m.move_history[0] == sts::registry::kLouseNormalMoveBite;
        if (bite_first) {
            saw_bite_first = true;
            ASSERT_EQ(k.monster_roll_known[0], 1)
                << "a BITE telegraph displays the rolled damage";
            EXPECT_EQ(k.monster_roll[0], m.pad0);
        } else {
            saw_move4_first = true;
            ASSERT_EQ(k.monster_roll_known[0], 0)
                << "no BITE telegraphed yet -> the roll is still hidden";
            // The first mid-combat BITE telegraph reveals it.
            for (int t = 0; t < 6 && k.monster_roll_known[0] == 0; ++t) {
                louse_normal_take_turn(s, 0);
            }
            ASSERT_EQ(k.monster_roll_known[0], 1)
                << "the Louse AI telegraphs BITE within a few turns";
            EXPECT_EQ(k.monster_roll[0], m.pad0);
        }
    }
    EXPECT_TRUE(saw_bite_first && saw_move4_first)
        << "seed sweep failed to exercise both first-move branches";
}

TEST(Knowledge, RunicDomeSuppressesTelegraphReveal) {
    for (int64_t seed = 1; seed <= 40; ++seed) {
        CombatState s{};
        s.monster_count = 1;
        s.monster_hp_rng = from_seed(seed);
        s.ai_rng = from_seed(seed);
        AddRelic(s, RelicId::RUNIC_DOME);
        KnowledgeState k{};
        KnowledgeScope scope(&k);
        louse_normal_init(s, 0);
        for (int t = 0; t < 6; ++t) {
            louse_normal_take_turn(s, 0);
        }
        ASSERT_EQ(k.monster_roll_known[0], 0)
            << "HIDE_INTENT: the telegraph is never shown, nothing is revealed";
    }
}

TEST(Knowledge, CombatReadyRetroGatesConstructionReveals) {
    // The run layer's construction order telegraphs first moves BEFORE the
    // relic mirror is copied; knowledge_on_combat_ready is the retro-gate.
    CombatState s{};
    s.monster_count = 1;
    s.monster_hp_rng = from_seed(3);
    s.ai_rng = from_seed(3);
    KnowledgeState k{};
    KnowledgeScope scope(&k);
    k.monster_roll_known[0] = 1;  // a reveal recorded under an empty mirror
    k.monster_roll[0] = 6;
    AddRelic(s, RelicId::RUNIC_DOME);  // the mirror copy then lands the Dome
    knowledge_on_combat_ready(s);
    EXPECT_EQ(k.monster_roll_known[0], 0);
}

// --- Observability membership table ---------------------------------------------

TEST(Knowledge, MembershipTableListsExactlyTheDeclaredRows) {
    // Exactly the two declared registry rows, in id order.
    ASSERT_EQ(sts::registry::kRelicObservability.size(), 2u);
    EXPECT_EQ(sts::registry::kRelicObservability[0].id,
              sts::registry::RelicId::FROZEN_EYE);
    EXPECT_EQ(sts::registry::kRelicObservability[0].transform,
              ObservabilityTransform::REVEAL_DRAW_ORDER);
    EXPECT_EQ(sts::registry::kRelicObservability[1].id,
              sts::registry::RelicId::RUNIC_DOME);
    EXPECT_EQ(sts::registry::kRelicObservability[1].transform,
              ObservabilityTransform::HIDE_INTENT);

    // The lookup agrees with the table, and a non-member reads NONE.
    EXPECT_EQ(sts::registry::relic_observability(
                  sts::registry::RelicId::FROZEN_EYE),
              ObservabilityTransform::REVEAL_DRAW_ORDER);
    EXPECT_EQ(sts::registry::relic_observability(
                  sts::registry::RelicId::RUNIC_DOME),
              ObservabilityTransform::HIDE_INTENT);
    EXPECT_EQ(sts::registry::relic_observability(
                  sts::registry::RelicId::BURNING_BLOOD),
              ObservabilityTransform::NONE);

    // The engine-side membership scans agree.
    CombatState s{};
    EXPECT_FALSE(combat_hides_intent(s));
    EXPECT_FALSE(combat_reveals_draw_order(s));
    AddRelic(s, RelicId::RUNIC_DOME);
    EXPECT_TRUE(combat_hides_intent(s));
    EXPECT_FALSE(combat_reveals_draw_order(s));
    AddRelic(s, RelicId::FROZEN_EYE);
    EXPECT_TRUE(combat_reveals_draw_order(s));
}

// --- Zero perturbation: recording knowledge never touches the state -------------

TEST(Knowledge, RecordingNeverPerturbsCombatStateOrRng) {
    // The same scripted sequence, once with recording attached and once
    // without: every CombatState byte (RNG stream counters included) must be
    // identical. This is the property that keeps the 20 committed fixtures
    // bit-exact with the hooks compiled in.
    auto script = [](CombatState& s) {
        SeedDraw(s, 3);
        s.card_pool[20].card_id = static_cast<uint16_t>(CardId::DEFEND);
        s.discard[0] = 20;
        s.discard_count = 1;
        ASSERT_TRUE(queue_card_play(s, 0, 0));  // Headbutt -> known top
        pump(s);
        s.card_pool[1].card_id = static_cast<uint16_t>(CardId::WILD_STRIKE);
        s.card_pool[1].cost_now = 1;
        s.hand[s.hand_count++] = 1;
        ASSERT_TRUE(queue_card_play(s, s.hand_count - 1, 0));  // random insert
        pump(s);
        shuffle_discard_into_draw(s);  // shuffle event
        draw_cards(s, 3);              // draw events
    };

    CombatState recorded = MakeState(CardId::HEADBUTT, 1);
    CombatState plain = recorded;
    {
        KnowledgeState k{};
        KnowledgeScope scope(&k);
        script(recorded);
    }
    ASSERT_EQ(knowledge_attachment(), nullptr)
        << "the scope restored the no-attachment default";
    script(plain);

    EXPECT_EQ(std::memcmp(&recorded, &plain, sizeof(CombatState)), 0)
        << "knowledge recording perturbed CombatState bytes";
    EXPECT_EQ(hash_state(recorded), hash_state(plain));
}

}  // namespace
}  // namespace sts::engine
