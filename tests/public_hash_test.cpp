// T0.7 -- public_hash: the identity of a public information set.
//
// The property under test is the one the training loop depends on: the hash is
// a function of the PUBLIC state and nothing else.
//   * TWIN EQUALITY -- a particle drawn by resample_hidden (T0.4) agrees with
//     the truth on every public quantity and redraws every hidden one, so the
//     two must hash equal. That is the strongest available twin generator in
//     the tree today; T0.5's make_hidden_twin will be a second one.
//   * PUBLIC SENSITIVITY -- anything the player CAN see (hp, gold, phase, a
//     legality bit) must change the hash, or the hash is not an identity.
//   * BYTE DETERMINISM -- two encodes of one state hash equal even when the
//     destination buffers held different garbage first. That is the property
//     the raw-byte hash rests on (public_view.hpp's public_hash comment states
//     the argument: no implicit padding, and an encoder that assigns every
//     byte), and it is why this file pre-dirties its buffers deliberately.
//
// The mask deserves its own case: RunActionMask is embedded in PublicView
// precisely so nothing can hash the view and drop the legality channel, and
// MaskBytesAreInsideTheHashedRegion is what proves the bytes are actually
// covered rather than merely adjacent.

#include <cstdint>
#include <cstring>
#include <span>

#include <gtest/gtest.h>

#include "sts/engine/advance.hpp"
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/public_view.hpp"
#include "sts/engine/resample.hpp"
#include "sts/engine/run_advance.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kA20 = 20;

// --- run helpers (the same shape resample_test uses) -------------------------

void step(RunController& rc, Action a) {
    StepResult res{};
    advance(std::span<RunController>(&rc, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&res, 1));
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
// and the relic pools underneath the test.
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

// Encode into a buffer that already holds `fill`, so a byte the encoder failed
// to assign would show up as a hash difference rather than as luck.
uint64_t hash_encoded_into_dirty_buffer(const RunController& rc, int fill) {
    PublicView view;
    std::memset(&view, fill, sizeof(view));
    encode_public_view(rc, view);
    return public_hash(view);
}

// =============================================================================
// Byte determinism
// =============================================================================

TEST(PublicHash, EncodingTwiceIntoDirtyBuffersHashesEqual) {
    const RunController rc = in_combat(11);
    // 0x00 / 0xAB / 0xFF: if any byte of PublicView were left unassigned by the
    // encoder -- an implicit padding hole, a pad member the encoder skipped --
    // at least one of these would differ from the others.
    const uint64_t a = hash_encoded_into_dirty_buffer(rc, 0x00);
    const uint64_t b = hash_encoded_into_dirty_buffer(rc, 0xAB);
    const uint64_t c = hash_encoded_into_dirty_buffer(rc, 0xFF);
    EXPECT_EQ(a, b);
    EXPECT_EQ(a, c);
}

TEST(PublicHash, TheRunControllerOverloadMatchesTheEncodedView) {
    for (const int64_t seed : {11LL, 97LL, 4242LL}) {
        const RunController rc = in_combat(seed);
        PublicView view{};
        encode_public_view(rc, view);
        EXPECT_EQ(public_hash(rc), public_hash(view)) << "seed " << seed;
    }
}

TEST(PublicHash, HashingIsPureAcrossPhases) {
    // Two independently built controllers at the same point of the same run
    // hash equal -- the hash carries no address, allocation or history.
    for (const int64_t seed : {11LL, 97LL}) {
        EXPECT_EQ(public_hash(at_map_choice(seed)), public_hash(at_map_choice(seed)));
        EXPECT_EQ(public_hash(in_combat(seed)), public_hash(in_combat(seed)));
    }
}

// =============================================================================
// Twin equality -- states differing only in hidden bytes
// =============================================================================

// The T0.4 sampler is the twin generator: it preserves every public quantity
// and redraws every hidden one (draw order, unconsumed encounter suffix, pool
// remainders, every stream). A particle that hashed differently from its truth
// would mean the view -- or the hash's coverage of it -- carries a hidden byte.
TEST(PublicHash, HiddenTwinsHashEqualInCombat) {
    const RunController truth = in_combat(11);
    const uint64_t want = public_hash(truth);
    for (const int64_t sampler_seed : {1LL, 2LL, 7LL, 99LL, 123456LL}) {
        const RunController particle = resample_hidden(truth, sampler_seed);
        EXPECT_EQ(public_hash(particle), want) << "sampler seed " << sampler_seed;
    }
}

TEST(PublicHash, HiddenTwinsHashEqualAtMapChoice) {
    for (const int64_t seed : {11LL, 97LL, 4242LL}) {
        const RunController truth = at_map_choice(seed);
        const uint64_t want = public_hash(truth);
        for (const int64_t sampler_seed : {3LL, 42LL}) {
            const RunController particle = resample_hidden(truth, sampler_seed);
            EXPECT_EQ(public_hash(particle), want)
                << "run seed " << seed << ", sampler seed " << sampler_seed;
        }
    }
}

// A particle really is a different state -- otherwise the equality above would
// be vacuous. RunController is compared by bytes only after a memcpy clone in
// resample_test; here it is enough that SOME sampler seed moves the raw bytes.
TEST(PublicHash, TwinsAreNotTriviallyIdenticalStates) {
    const RunController truth = in_combat(11);
    bool saw_a_different_state = false;
    for (const int64_t sampler_seed : {1LL, 2LL, 7LL, 99LL, 123456LL}) {
        const RunController particle = resample_hidden(truth, sampler_seed);
        if (particle.run.run_seed != truth.run.run_seed ||
            particle.combat.shuffle_rng.s0 != truth.combat.shuffle_rng.s0) {
            saw_a_different_state = true;
        }
    }
    EXPECT_TRUE(saw_a_different_state)
        << "the sampler produced no hidden difference at all -- the twin "
           "equality above would then prove nothing";
}

// =============================================================================
// Public sensitivity
// =============================================================================

TEST(PublicHash, APublicScalarDifferenceChangesTheHash) {
    const RunController base = in_combat(11);
    const uint64_t want = public_hash(base);

    RunController gold = base;
    gold.run.gold += 1;
    EXPECT_NE(public_hash(gold), want) << "gold is public";

    RunController hp = base;
    hp.combat.player_hp = static_cast<int16_t>(hp.combat.player_hp - 1);
    EXPECT_NE(public_hash(hp), want) << "player hp is public";

    RunController floor = base;
    floor.run.floor = static_cast<uint16_t>(floor.run.floor + 1);
    EXPECT_NE(public_hash(floor), want) << "the floor number is public";
}

TEST(PublicHash, TwoDifferentRunsHashDifferently) {
    EXPECT_NE(public_hash(at_map_choice(11)), public_hash(at_map_choice(97)));
    EXPECT_NE(public_hash(in_combat(11)), public_hash(in_combat(97)));
}

TEST(PublicHash, TheRunPhaseIsPartOfTheIdentity) {
    const RunController map = at_map_choice(11);
    const RunController fight = in_combat(11);
    EXPECT_NE(public_hash(map), public_hash(fight));
}

// The mask channel is embedded in PublicView so it cannot be hashed separately
// or forgotten. Flipping one byte of it must move the hash: that proves the
// mask bytes lie inside the hashed region, not beside it.
TEST(PublicHash, MaskBytesAreInsideTheHashedRegion) {
    const RunController rc = in_combat(11);
    PublicView view{};
    encode_public_view(rc, view);
    const uint64_t want = public_hash(view);

    PublicView flipped = view;
    flipped.action_mask.mask.combat.can_end_turn =
        !view.action_mask.mask.combat.can_end_turn;
    EXPECT_NE(public_hash(flipped), want);

    // And the view is otherwise identical, so nothing but the mask moved.
    flipped.action_mask.mask.combat.can_end_turn = view.action_mask.mask.combat.can_end_turn;
    EXPECT_EQ(public_hash(flipped), want);
}

TEST(PublicHash, TheVersionStampIsPartOfTheIdentity) {
    const RunController rc = in_combat(11);
    PublicView view{};
    encode_public_view(rc, view);
    PublicView bumped = view;
    bumped.public_view_version = PUBLIC_VIEW_VERSION + 1;
    EXPECT_NE(public_hash(bumped), public_hash(view));
}

}  // namespace
}  // namespace sts::engine
