// S2.43 pre-work -- cross-record FIRED accumulation for --replay
// (tools/oracle_bridge/replay/src/fired_accum.hpp).
//
// WHY THIS SUITE EXISTS. The translator's per-record FIRED derivation is
// act-local (translate.cpp: "fired" == "initially in the act's list and now
// absent"), which is complete only until dungeonTransitionSetup rebuilds the
// event and shrine lists at an act crossing (AbstractDungeon.java:2576-2577,
// :291, :293). The accumulator is the one piece that turns those act-local
// views back into the whole-run view the simulator's two words hold -- and it
// is exactly the kind of helper that can be wrong without failing: fold in the
// wrong direction and every Act-1 capture still replays green (the union is a
// no-op there) while the first Act-2 record silently compares a widened ACTUAL
// side instead of a widened EXPECTED side. So the fold's direction, its
// exactness claims and its no-op guarantee each get a pin with no artifact and
// no JSON layer, in the mould of replay_command_map_test next door.

#include <gtest/gtest.h>

#include "fired_accum.hpp"
#include "sts/engine/event_framework.hpp"

using sts::engine::event_flag_set;
using sts::engine::RunState;
using sts::replay::FiredAccum;

namespace {

// Within one act the derivation is monotone (lists only shrink), so folding a
// sequence of Act-1 records must leave every record's words byte-identical --
// the guarantee that keeps the committed 50-seed Act-1 corpus, and every other
// landed Act-1 verification, exactly where it was.
TEST(ReplayFiredAccum, ActOneMonotoneSequenceIsAByteExactNoOp) {
    FiredAccum acc;
    RunState r1{};
    event_flag_set(r1, 3);  // an Act-1 eventList fire
    RunState r2 = r1;
    event_flag_set(r2, 14);  // a shrine fire on a later floor
    RunState r3 = r2;
    event_flag_set(r3, 20);  // a one-time special on a later floor still

    for (RunState* r : {&r1, &r2, &r3}) {
        const uint32_t lo = r->event_flags;
        const uint32_t hi = r->event_flags_hi;
        acc.fold(*r);
        EXPECT_EQ(r->event_flags, lo) << "monotone fold must not move lo";
        EXPECT_EQ(r->event_flags_hi, hi) << "monotone fold must not move hi";
    }
}

// The gap itself: an Act-2 record's act-local derivation has LOST the Act-1
// eventList fire and the Act-1 shrine fire (rebuilt lists cannot witness
// them), while it carries its own Act-2 fires. After the fold the expected
// side must hold the union -- the simulator's whole-run semantics.
TEST(ReplayFiredAccum, AnActTwoRecordRegainsTheActOneFires) {
    FiredAccum acc;

    RunState act1{};
    event_flag_set(act1, 3);   // Act-1 event (lo word)
    event_flag_set(act1, 14);  // shrine, fired in Act 1 (lo word)
    acc.fold(act1);

    RunState act2{};                // rebuilt lists: Act-1 fires underivable
    event_flag_set(act2, 34);       // Act-2 event (hi word)
    const RunState act2_local = act2;
    acc.fold(act2);

    RunState expected_union = act2_local;
    event_flag_set(expected_union, 3);
    event_flag_set(expected_union, 14);
    EXPECT_EQ(act2.event_flags, expected_union.event_flags)
        << "the Act-1 fires must come back on the expected side";
    EXPECT_EQ(act2.event_flags_hi, expected_union.event_flags_hi)
        << "the record's own Act-2 fire must survive the fold";
}

// A shrine RE-fired in Act 2 occupies the same bit as its Act-1 fire (`id -
// kShrineListFirstId` in every act) -- the very aliasing that made the
// narrow-recognizer alternative unworkable. The union must hold exactly one
// copy and no other bit.
TEST(ReplayFiredAccum, AShrineRefiredAcrossActsFoldsToOneBit) {
    FiredAccum acc;
    RunState act1{};
    event_flag_set(act1, 14);
    acc.fold(act1);

    RunState act2{};
    event_flag_set(act2, 14);  // the same shrine, fired again from the rebuilt list
    acc.fold(act2);

    RunState one_bit{};
    event_flag_set(one_bit, 14);
    EXPECT_EQ(act2.event_flags, one_bit.event_flags);
    EXPECT_EQ(act2.event_flags_hi, 0u);
}

// No false green: the fold only ever ADDS bits the capture itself attested
// earlier. A bit only the SIM holds (a fire the capture never derived in any
// record) must still be missing from the folded expected side, so
// diff_run_states still reports it.
TEST(ReplayFiredAccum, AFireTheCaptureNeverAttestedStaysMissing) {
    FiredAccum acc;
    RunState rec{};
    event_flag_set(rec, 5);
    acc.fold(rec);

    RunState sim{};
    event_flag_set(sim, 5);
    event_flag_set(sim, 41);  // sim-only: never in any capture derivation

    EXPECT_NE(rec.event_flags_hi, sim.event_flags_hi)
        << "the fold must not manufacture the sim-only bit";
}

// Direction pin: fold() widens the EXPECTED side toward the union; it must
// never be a pure read. A fresh record folded after a richer one carries the
// accumulated history -- that is the substitution doing its job.
TEST(ReplayFiredAccum, FoldSubstitutesTheUnionNotTheRecord) {
    FiredAccum acc;
    RunState rich{};
    event_flag_set(rich, 1);
    event_flag_set(rich, 45);
    acc.fold(rich);

    RunState fresh{};  // value-init: derives nothing on its own
    acc.fold(fresh);
    RunState want{};
    event_flag_set(want, 1);
    event_flag_set(want, 45);
    EXPECT_EQ(fresh.event_flags, want.event_flags);
    EXPECT_EQ(fresh.event_flags_hi, want.event_flags_hi);
}

}  // namespace
