#pragma once

// resample_hidden -- the belief sampler (docs/training-plan.md §2.4).
//
// Given a RunController holding the TRUE state, produce a PARTICLE: a state
// that agrees with the true state on every PUBLIC quantity and draws every
// HIDDEN quantity from its conditional law given the public history. Because
// every observation in this game is a noiseless deterministic reveal and every
// hidden quantity's conditional law is available in closed form, constrained
// re-seeding IS exact posterior sampling under the declared contract -- there
// is no particle filtering, no importance weight and no degeneracy (plan §2.4).
//
// --- THE CONTRACT, NOT THE MECHANIC (plan §2.6d) -----------------------------
//
// The sampler conditions on the knowledge CONTRACT recorded in knowledge.hpp,
// which is deliberately COARSER than the JDK mechanic in the places that
// header names (CardGroup.addToRandomSpot can never displace the top card; the
// contract declares the weaker uniform-interleaving constraint anyway). A
// weaker constraint set only WIDENS the belief, which is sound; the T0.6
// distribution suite therefore tests this sampler against the contract's
// closed-form conditionals, never against seed-filtered reality. Two further
// deliberate coarsenings this file introduces are flagged `CONTRACT COARSENING`
// at their site (the Match-and-Keep miss memory, and relic-pool membership
// under pop-time canSpawn).
//
// --- SAMPLER-PRIVATE RANDOMNESS ----------------------------------------------
//
// Every decision below is drawn from a SamplerRng, never from a stream stored
// in the state. Spending an engine stream would desync a particle's counters
// from the public history -- the counters ARE public (they are what a save file
// records), so a particle whose merchantRng counter differs from the truth is
// not a member of the belief at all. SamplerRng is a distinct type rather than
// a bare RngStream precisely so that "does this sampler consume an engine
// draw?" is answered by grep and by the type system rather than by care.
//
// The state's own streams are OVERWRITTEN (re-seeded from sampler draws), which
// is not the same thing as consuming them: a fresh stream is the declared
// posterior for a lazily-drawn source, and every stream this file writes lands
// at counter 0. `SamplerFreshStreams.EveryRefreshedStreamIsAtCounterZero`
// (tests/resample_test.cpp) pins that property, and the poisoned-seed canary
// pins the stronger one: the particle is a function of (public state, sampler
// seed) ALONE, so no hidden seed or stream state can reach it.
//
// --- WHAT "PUBLIC" MEANS HERE ------------------------------------------------
//
// Everything encode_public_view (public_view.hpp) carries, plus the run-level
// quantities T0.2 will project: the map, master deck, relics, gold/hp/floor,
// pity counters, pool-membership bitsets, the consumed encounter prefix, the
// live screen structs (reward / shop / rest / Neow / event) and chest SIZE.
// `SamplerPublicQuantities.PublicViewIsByteIdenticalAcrossResample` asserts the
// first half directly; the per-row tests assert the rest field by field.

#include <cstdint>

#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"

namespace sts::engine {

// The sampler's private randomness. Wrapping the stream in its own type keeps
// it un-assignable to any engine stream slot by accident and makes the
// zero-engine-draw property greppable (see the header note above).
struct SamplerRng {
    RngStream stream;
};

[[nodiscard]] constexpr SamplerRng sampler_rng_from_seed(int64_t seed) noexcept {
    return SamplerRng{from_seed(seed)};
}

// Draw one particle IN PLACE. `rc` must hold a true (or already-particled)
// state; on return every public quantity is unchanged and every hidden one has
// been redrawn per the plan §2.4 table. Deterministic in `rng`: the same
// sampler stream state over the same input yields a byte-identical particle.
void resample_hidden(RunController& rc, SamplerRng& rng) noexcept;

// Convenience: one particle from one seed, by value.
[[nodiscard]] RunController resample_hidden(const RunController& rc,
                                            int64_t sampler_seed) noexcept;

// --- The individual table rows, exposed for the per-row tests ----------------
//
// resample_hidden is exactly the composition of these plus the stream refresh;
// they are public so each row's preserve/condition/fresh behaviour can be
// asserted on a hand-constructed state without walking a whole run.

// Row "Draw-pile order": uniformly permute the pile's unknown slots subject to
// the KnowledgeState constraints -- the `exact_prefix` leading positions stay
// put, the remaining chain members keep their RELATIVE order, and everything
// else is uniform over the interleavings. A full-order pile (Frozen Eye) is
// left untouched. No-op when the pile has fewer than two cards.
void resample_draw_order(CombatState& s, const KnowledgeState& k,
                         SamplerRng& rng) noexcept;

// Row "Monster HP / construction rolls": public once revealed. A live monster
// whose registry def declares a CONSTRUCTOR_AFTER_HP roll (the Louse bite
// damage) and whose roll the player has NOT seen telegraphed gets a fresh draw
// over the registry range; a revealed roll and every monster's HP/max HP (both
// public) are preserved.
void resample_monster_construction_rolls(CombatState& s,
                                         const KnowledgeState& k,
                                         SamplerRng& rng) noexcept;

// Row "Relic-pool remainders": uniformly re-permute each tier's unrevealed
// `[0, count)` window. See the CONTRACT COARSENING note in the .cpp for what
// pop-time canSpawn does to the remainder and why the sampler permutes the
// STORED window rather than reconstructing one from observations.
void resample_relic_pool_remainders(RunState& rs, SamplerRng& rng) noexcept;

// Row "Mid-event hidden state (Match & Keep board)": pin the revealed flips
// (matched-and-taken pairs, plus the one card currently face up) and uniformly
// permute the face-down remainder. No-op for any other event.
void resample_match_and_keep_board(EventDialogState& es,
                                   SamplerRng& rng) noexcept;

// The unopened-chest half of the row "chest size ... public -- copy": size and
// opened state are preserved, the contents roll is redrawn CONDITIONED on the
// size band (the player pre-open sees only the size -- plan §2.1). No-op on an
// opened chest, whose contents are public.
void resample_treasure_chest_contents(TreasureChest& chest,
                                      SamplerRng& rng) noexcept;

}  // namespace sts::engine
