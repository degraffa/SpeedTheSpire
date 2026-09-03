#pragma once

// make_hidden_twin -- the leak gate's state generator (docs/training-plan.md
// §2.6a).
//
// A TWIN of a state is another state that a player holding the same
// action-observation history could equally well be in: every PUBLIC quantity is
// identical, every HIDDEN one is redrawn. The gate that consumes it is one
// sentence long -- `encode_public_view` of a state and of its twin must be
// BYTE-IDENTICAL, mask channel included -- and that sentence is what turns
// "the encoder does not leak" from a review opinion into a per-commit test.
//
// TWIN-MAKING IS HIDDEN RESAMPLING. There is no second table of hidden sources
// here: a twin is exactly one draw from the belief `resample_hidden`
// (resample.hpp) defines, so this file DELEGATES the whole plan §2.4 table --
// draw-suffix permutation, fresh streams, relic-pool remainder re-permutation,
// encounter-suffix Markov continuation (a PURE COPY at Act 4, S3.51: TheEnding
// fills all three lists at the crossing with no further monsterRng draw, so
// they are public and constant, not a suffix to continue), Match-and-Keep
// pin-and-permute, chest contents under the public size band, the fresh fake
// run seed. Duplicating
// those rows would create two definitions of "hidden" that could drift, and the
// one that drifts silently is the one the leak gate is built on.
//
// WHAT A TWIN MUST NOT PERTURB, and why it is not simply "everything hidden":
//
//   * RESOLUTION QUEUES and `turn_has_ended` (T0.2's note). They are classified
//     DERIVED, not hidden -- deterministic functions of the observed action
//     history -- and they legitimately MOVE THE MASK (a pending CHOOSE_CARD is
//     what makes the choose bits legal). Perturbing them would produce a state
//     that is not a twin at all and would report a mask difference that is
//     correct behaviour. `resample_hidden` does not touch them, and this file
//     must not start.
//
//   * THE DRAW PILE WHILE A DRAW-SOURCED CHOOSE SCREEN IS OPEN -- see the
//     KNOWN MASK LEAK note on `draw_choice_pending` below. That pin is a
//     workaround for an engine defect this task found and could not fix in
//     scope; it is characterised by an executable test rather than left in
//     prose.
//
// SAMPLER-PRIVATE RANDOMNESS: a twin is drawn from a SamplerRng exactly as a
// particle is, so twin generation consumes ZERO engine-stream draws and a twin
// is a pure function of (public state, twin seed). resample.hpp's header note
// is the full argument.

#include <cstddef>

#include "sts/engine/public_view.hpp"
#include "sts/engine/resample.hpp"
#include "sts/engine/run_advance.hpp"

namespace sts::engine {

// Redraw every hidden quantity of `rc` IN PLACE, preserving every public one.
// Deterministic in `rng`: the same stream state over the same input yields a
// byte-identical twin.
void make_hidden_twin(RunController& rc, SamplerRng& rng) noexcept;

// Convenience: one twin from one seed, by value.
//
// The copy is a MEMCPY of the object representation, not the implicit copy
// constructor. A memberwise copy leaves any byte no member owns unspecified
// (conventions §8's RunState padding incident) -- so a twin built with `=`
// could differ from its source in bytes no member owns, which is exactly the
// kind of difference a byte-comparison test must not be able to see. The
// tripwire's EveryByteIsAMember is what keeps that set empty today (it was
// MonsterLists' string_view alignment slack that put it here); the memcpy is
// belt-and-braces against the next struct that grows some.
[[nodiscard]] RunController make_hidden_twin(const RunController& rc,
                                             int64_t twin_seed) noexcept;

// True while the engine's action space addresses RAW DRAW-PILE SLOTS: a
// type-filtered draw-source CHOOSE screen (Secret Technique / Secret Weapon,
// ChoiceSource::DRAW) is open at the head of the queue.
//
// KNOWN MASK LEAK -- this predicate exists because of a defect, not a rule.
// `legal_actions` fills `can_choose[i]` for such a screen by reading
// `state.draw[i]`'s card TYPE (advance.cpp, `choice_slot_eligible`), so the
// mask channel reports the types of the first kHandCap draw-pile ARRAY SLOTS.
// Draw-pile order is a shuffle realization, so those bits are hidden
// information reaching an observation channel.
//
// The game does not do this: SkillFromDeckToHandAction builds its grid by
// `tmp.addToRandomSpot(c)` over the filtered draw pile and opens the screen on
// `tmp` (SkillFromDeckToHandAction.java:35-40, 66), i.e. the presentation order
// is deliberately RANDOMISED so that pile order is not shown. Our slot-indexed
// action space is the leak; repairing it means giving the draw-source choice
// its own presentation order (and the draws that build it), which is an
// action-space change well outside this task.
//
// Until then a twin PINS the draw pile whenever this is true, so the gate stays
// green on a defect it has already recorded, and
// `TwinDrawChoiceLeak.MaskReadsRawDrawSlotsWhileADrawSourcedChoiceIsOpen`
// (tests/twin_test.cpp) asserts the leak still exists -- that test FAILS the day
// the action space is fixed, which is when this pin must be deleted.
[[nodiscard]] bool draw_choice_pending(const RunController& rc) noexcept;

// --- PublicView difference diagnostic ----------------------------------------

// Where two views first differ. `equal` short-circuits the rest; otherwise
// `offset` is the first differing byte and `field` names the PublicView member
// (or member group) that owns it, so a failing twin assertion says WHICH
// observation field leaked rather than "byte 4471 differs".
struct PublicViewDiff {
    bool equal = true;
    std::size_t offset = 0;
    const char* field = "";
};

[[nodiscard]] PublicViewDiff public_view_first_difference(
    const PublicView& a, const PublicView& b) noexcept;

// The name of the PublicView member (or member group) owning byte `offset`.
// Returns "<out of range>" past the end of the record.
[[nodiscard]] const char* public_view_field_at(std::size_t offset) noexcept;

// The diagnostic's backing table, exposed so a test can check that it is sorted
// and reaches the end of the record. An out-of-order or truncated table would
// mis-name every field after the break, silently -- a failure message that lies
// is worse than one that says "byte 4471".
struct PublicViewFieldSpan {
    std::size_t offset;
    const char* name;
};

[[nodiscard]] std::size_t public_view_field_count() noexcept;
[[nodiscard]] PublicViewFieldSpan public_view_field(std::size_t index) noexcept;

}  // namespace sts::engine
