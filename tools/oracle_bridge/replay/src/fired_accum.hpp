#pragma once

// fired_accum.hpp -- cross-record accumulation of the capture-derived event
// FIRED bitset, closing the translator's act-local derivation gap for --replay.
//
// INTERNAL header, same rationale as `command_map.hpp` / `mk_board.hpp` next
// door: its consumers are this tool's `main.cpp` and its own gtest.
//
// ---------------------------------------------------------------------------
// THE GAP THIS CLOSES (the S2.13 deferred-obligations row, owner S2.43).
//
// The translator reconstructs "fired" per record as "initially in the act's
// list and now absent" (translate.cpp, the eventList/shrineList blocks). That
// derivation is complete only while a list is never refilled -- and from Act 2
// on it is not: dungeonTransitionSetup CLEARS eventList and shrineList
// (AbstractDungeon.java:2576-2577) and the new dungeon's constructor rebuilds
// them (:291, :293). So an Act-2/3 record's capture-side RunState cannot
// witness an Act-1 event or shrine fire, while the simulator's whole-run
// `event_flags` rightly still carries it -- a false RED in diff_run_states on
// the first Act-2 record after any Act-1 fire.
//
// WHY UNION-OVER-RECORDS IS EXACT, not an approximation:
//
//  - A fire is visible in the very next record of its own act: every fire
//    removes its id from the live list at the moment it happens, records are
//    per-action, and an event fires on ?-room entry -- always followed by
//    same-act records (its own dialog) before the act crossing. No fire can
//    hide in the crossing gap, so at record k the union over records <= k of
//    the act-local derivations IS the set of all fires up to k -- exactly the
//    simulator's semantics for the two words.
//  - Within one act the derivation is MONOTONE (lists only shrink), so the
//    union over an Act-1-only capture equals the last record's own derivation
//    and the accumulator is a byte-exact no-op for every landed Act-1
//    verification, the committed 50-seed corpus included.
//  - No false green is possible: substitution only ever ADDS a bit the capture
//    itself attested earlier. A fire the sim missed stays set on the expected
//    side (still a diff); a sim fire the capture never derived stays missing
//    from it (still a diff).
//
// The alternative the deferred row named -- a narrow recognizer in the b14
// RACE mould -- was REJECTED: shrine ids occupy the same six bits in every act
// (`id - kShrineListFirstId`), so a positional mask cannot distinguish "fired
// in Act 1, legitimately absent from this act's derivation" from "the sim
// wrongly believes this fired in Act 2". Masking would forfeit exactly the
// Act-2/3 shrine coverage S2-G2 item 4 exists to witness; accumulation keeps
// the comparison at full strength.
//
// SCOPE: --replay only. The claim/purchase flows seed BOTH sides from captured
// records, so their derivations are act-local-symmetric and diff clean without
// help. --replay is also structurally a floor-1 walk (it re-drives from
// run_begin), which is the precondition the union argument needs; a capture
// that started mid-run could not be driven by --replay at all.

#include <cstdint>

#include "sts/engine/run_state.hpp"

namespace sts::replay {

// Folds one record's capture-derived FIRED words into the running union and
// substitutes that union back, giving the expected side the whole-run view the
// simulator holds. Call once per record, in record order, on the EXPECTED
// state -- before diff_run_states and after translation (the neutralizers do
// not touch these words, so order against them is immaterial).
struct FiredAccum {
    uint32_t lo = 0;
    uint32_t hi = 0;

    void fold(sts::engine::RunState& expected) noexcept {
        lo |= expected.event_flags;
        hi |= expected.event_flags_hi;
        expected.event_flags = lo;
        expected.event_flags_hi = hi;
    }
};

}  // namespace sts::replay
