#pragma once

// Field-by-field CombatState differ (design doc §8: "states compared
// field-by-field with named-field diff output"). The other permanent half of
// the diff harness: the oracle adapter is provisional (fixture now,
// CommunicationMod later), but the differ never changes.
//
// This is a DEBUGGING tool, not a hot-path routine: it returns human-readable
// named diffs (`"monsters[0].powers[1].amount"`, `"ai_rng.counter"`) a person
// can act on, not an opaque byte comparison. Strings and heap allocation are
// fine here (offline, not the zero-alloc advance() loop).
//
// FAST PATH (design doc §4.1 value-init guarantee): if
// hash_state(expected) == hash_state(actual), the two states are byte-identical
// (both value-initialized, padding-deterministic), so diff_states returns an
// empty report immediately WITHOUT the expensive field walk. The full walk runs
// only when the 64-bit hashes actually differ.
//
// SEMANTIC (not byte) diff: the differ reports divergences in LOGICAL game
// state. It compares each pile / queue over its live [0, count) range (walking
// ring buffers via their head cursor), not the stale scratch bytes past `count`
// that a fixed-capacity array retains after a pop (cards_test documents that
// vacated slots keep old bytes). It likewise does not compare internal ring
// cursors (action_tail etc.) or padding: those can differ between two states
// with identical logical content (e.g. reached via different push/pop
// rotations) and are not game state. Consequence: the differ may return an empty
// report for two states whose raw hashes differ but which are logically
// equivalent (same live content, different scratch/rotation). That is the
// intended behavior for a human-facing debugger; the raw hash remains the
// byte-exact equality oracle (state_hash.hpp) when byte identity is what matters.

#include <cstddef>
#include <string>
#include <vector>

namespace sts::engine {
struct CombatState;
}

namespace sts::diff {

// One named field divergence. All three are human-readable strings: `field_name`
// is a debuggable path (e.g. "player.hp", "monsters[0].hp", "hand[3]",
// "shuffle_rng.s0"); the reprs render enum values by name where known
// (e.g. "VULNERABLE(2)") and fall back to the raw number otherwise.
struct FieldDiff {
    std::string field_name;
    std::string expected_repr;
    std::string actual_repr;
};

// The result of a field-by-field comparison: the list of divergences (empty ==
// the states are logically equal). Only genuinely-differing fields are listed;
// equal fields produce no entry.
struct DiffReport {
    std::vector<FieldDiff> diffs;

    [[nodiscard]] bool empty() const noexcept { return diffs.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return diffs.size(); }

    // True iff any diff's field_name contains `substr` -- convenience for tests
    // and log grepping ("did the divergence land in ai_rng?").
    [[nodiscard]] bool mentions(const std::string& substr) const;

    // Multi-line "field_name: expected -> actual" rendering for logs / reproducer
    // annotations. Empty string when there are no diffs.
    [[nodiscard]] std::string to_string() const;
};

// Compare `expected` vs `actual` field by field. Fast-path on equal hashes (see
// the header note); otherwise walks every field group of CombatState (header,
// player scalars + powers, card pool, all five piles, monsters scalars + powers,
// the four queues, and each of the five RNG streams individually) and appends a
// named FieldDiff for every element that differs.
[[nodiscard]] DiffReport diff_states(const engine::CombatState& expected,
                                     const engine::CombatState& actual);

}  // namespace sts::diff
