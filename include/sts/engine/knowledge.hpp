#pragma once

// KnowledgeState -- the player-information layer's draw-position knowledge
// (training-plan §2.2) plus revealed monster construction rolls, maintained
// INSIDE the engine at placement/reveal/shuffle events (only the engine knows
// when a reveal happens). Consumed by the PublicView projection and the belief
// sampler (training-plan §2.1/§2.4); nothing in the combat rules ever reads it
// back, so recording knowledge can never change engine behavior.
//
// --- WHERE THIS STATE LIVES (the snapshot-discipline decision) ---------------
//
// KnowledgeState is deliberately NOT a member of CombatState or RunState. Both
// are byte-hashed over their full sizeof (state_hash.cpp) and the 20 committed
// combat fixtures pin those hashes, so ANY layout change there is a
// SCHEMA_VERSION bump plus a fixture regeneration -- exactly the class of
// change the information layer must not force, because knowledge is a pure
// OBSERVER of the state, not part of it. Instead KnowledgeState follows the
// RunController precedent for transient derived state (run_advance.hpp: "this
// keeps RunState's byte layout untouched"): it is its own trivially-copyable
// POD, owned BY VALUE by RunController, so a memcpy of the controller
// snapshots the player's knowledge exactly as it snapshots the reward screen.
// RunController is content-hashed (tools/fuzz hash_controller), never
// byte-hashed against a committed golden, so growing it is layout-safe.
//
// --- HOW THE ENGINE'S EVENT SITES REACH IT (the attachment) ------------------
//
// The mutation sites (piles.cpp draws/shuffles, interp_cards.cpp placements,
// monster reveal telegraphs) sit many calls below advance(), and CombatState
// may hold no pointers (design §4.1). Rather than threading a parameter
// through every opcode body -- a churn every future op author would have to
// remember -- the hooks read a per-thread attachment set by an RAII
// KnowledgeScope around one state's step. No attachment (the default) makes
// every hook a no-op: fixture replay, the fuzz soak, and every pre-existing
// caller run byte-identically with zero overhead beyond a thread-local read.
// The attachment is NOT state: it lives exactly as long as one call, is never
// snapshotted, and each batch entry is stepped under its own scope (the run
// layer attaches &rc.knowledge per entry in step dispatch).
//
// --- THE CONSTRAINT MODEL (relative order, not absolute indices) -------------
//
// One chain of pool indices, chain[0] nearest the top, whose members are known
// to sit in the draw pile in that relative order; exact_prefix says how many
// leading chain entries are additionally at exactly-known positions counted
// from the top (chain[i] IS the (i+1)-th card for i < exact_prefix).
// full_order means the chain covers the entire pile exactly (Frozen Eye).
//
//   * Headbutt-style placement on top: push front, ++exact_prefix.
//   * Shuffle: cleared (then re-revealed in full under REVEAL_DRAW_ORDER).
//   * Random-position insertion (Wild Strike / Reckless Charge): exact_prefix
//     and full_order drop to zero; the chain's RELATIVE order survives -- the
//     declared posterior is a uniform interleaving preserving relative order
//     (training-plan §2.2). DELIBERATE CONTRACT COARSENING, recorded here so
//     nobody "fixes" it: the real mechanic, CardGroup.addToRandomSpot
//     (CardGroup.java:463-468), inserts at cardRandomRng.random(size-1) --
//     list index size (the top slot) is unreachable, so a single insertion
//     can never displace the current top card, and the exact posterior would
//     keep a Headbutt'd card known-top. The knowledge contract declares the
//     weaker interleaving constraint anyway; the plan's sampler and
//     distribution tests condition on the DECLARED contract, not seed-filtered
//     reality (training-plan §2.6d), and a weaker-than-exact constraint set
//     is sound (it only widens the belief). The engine mechanic itself stays
//     exact -- only what the player is modeled to retain is coarser.
//
// Monster construction rolls: per-slot value+known pair, recorded when the
// roll is first REVEALED -- e.g. a Louse's bite damage (rolled at spawn,
// LouseNormal.java:60) becomes public the first time a BITE intent is
// telegraphed, because the intent banner displays the number. HIDE_INTENT
// (Runic Dome) suppresses telegraph-time reveals.

#include <cstdint>
#include <type_traits>

#include "sts/engine/combat_state.hpp"
#include "sts/registry/relic_table.hpp"  // ObservabilityTransform + membership

namespace sts::engine {

// Chain capacity == the draw pile's own capacity: entries are distinct pool
// indices currently in the draw pile, so kDrawCap bounds them exactly.
inline constexpr int kKnowledgeChainCap = kDrawCap;

struct KnowledgeState {
    uint8_t chain_count;    // live length of chain[]
    uint8_t exact_prefix;   // leading chain entries at exact from-top positions
    uint8_t full_order;     // 0/1: chain is the ENTIRE pile in exact order
    uint8_t pad0;           // explicit padding, value-init zeroed
    CardPoolIndex chain[kKnowledgeChainCap];  // [0] nearest the top

    // Revealed construction rolls, per monster slot (parallel to
    // CombatState.monsters). monster_roll[i] is meaningful only while
    // monster_roll_known[i] != 0.
    uint8_t monster_roll_known[kMonsterCap];
    uint8_t monster_roll[kMonsterCap];
    uint8_t pad1[2];        // explicit padding, value-init zeroed
};

static_assert(std::is_trivially_copyable_v<KnowledgeState>,
              "KnowledgeState must be trivially copyable (memcpy snapshot, "
              "same discipline as every other engine POD)");
static_assert(sizeof(KnowledgeState) ==
                  4 + kKnowledgeChainCap + 2 * kMonsterCap + 2,
              "KnowledgeState layout drifted -- padding must stay explicit");

// --- Attachment ---------------------------------------------------------------

// The KnowledgeState the current thread's engine hooks record into, or nullptr
// (hooks no-op). Read-only for callers; set it via KnowledgeScope.
[[nodiscard]] KnowledgeState* knowledge_attachment() noexcept;

// RAII attachment: hooks fired on this thread record into *k for the scope's
// lifetime. Nests (restores the previous attachment on destruction), so a
// caller stepping a batch attaches each entry's knowledge around that entry's
// dispatch. Pass nullptr to deliberately suspend recording within a scope.
class KnowledgeScope {
public:
    explicit KnowledgeScope(KnowledgeState* k) noexcept;
    ~KnowledgeScope();
    KnowledgeScope(const KnowledgeScope&) = delete;
    KnowledgeScope& operator=(const KnowledgeScope&) = delete;

private:
    KnowledgeState* prev_;
};

// --- Observability transforms (registry-declared membership) -------------------

// Whether any mirrored relic declares `transform`. Scans the combat relic
// mirror (CombatState.relics), so it answers for the state actually being
// stepped -- the standalone combat_begin path simply has an empty mirror.
[[nodiscard]] inline bool combat_has_observability(
    const CombatState& s, sts::registry::ObservabilityTransform t) noexcept {
    for (uint8_t i = 0; i < s.relic_count; ++i) {
        if (sts::registry::relic_observability(static_cast<sts::registry::RelicId>(
                s.relics[i].relic_id)) == t) {
            return true;
        }
    }
    return false;
}

// HIDE_INTENT (Runic Dome): monster intents are not shown, so telegraph-time
// reveals do not happen and the observation encoder blanks ObsMonster::intent.
[[nodiscard]] inline bool combat_hides_intent(const CombatState& s) noexcept {
    return combat_has_observability(
        s, sts::registry::ObservabilityTransform::HIDE_INTENT);
}

// REVEAL_DRAW_ORDER (Frozen Eye): the draw pile is visible in true order, so
// every shuffle/placement event re-reveals the full order.
[[nodiscard]] inline bool combat_reveals_draw_order(
    const CombatState& s) noexcept {
    return combat_has_observability(
        s, sts::registry::ObservabilityTransform::REVEAL_DRAW_ORDER);
}

// --- Hooks (called by the engine's event sites; no-ops with no attachment) ----
//
// EVERY hook is a pure observer: none touches CombatState, and none consumes
// any engine RNG stream -- the zero-RNG property the fixture replay pins.
// Hooks that record a full-order reveal read s.draw AFTER the mutation, so
// call sites fire them once the pile is in its final shape.

// Clear the attached knowledge outright (fresh combat, before monsters spawn).
void knowledge_reset() noexcept;

// Combat construction finished (pile built, relic mirror final, pre-battle
// spawns done): arm the REVEAL_DRAW_ORDER full-order record, and retro-gate
// telegraph reveals recorded while the mirror was still empty -- construction
// spawns telegraph first moves BEFORE the mirror is copied (run_advance.cpp
// step order, which is behavior-pinned), so a HIDE_INTENT run wipes them here.
void knowledge_on_combat_ready(const CombatState& s) noexcept;

// A reshuffle rewrote the draw pile (piles.cpp). Clears all order knowledge,
// then re-records the full order under REVEAL_DRAW_ORDER.
void knowledge_on_shuffle(const CombatState& s) noexcept;

// The top card `pi` was just drawn (it has left the pile).
void knowledge_on_draw_top(const CombatState& s, CardPoolIndex pi) noexcept;

// `pi` was just placed on TOP of the draw pile with the player watching
// (Headbutt's discard-to-top, PUT_ON_DRAW_TOP, MAKE_CARD to DRAW).
void knowledge_on_place_top(const CombatState& s, CardPoolIndex pi) noexcept;

// `pi` was just placed on the BOTTOM of the draw pile (Forethought-style).
void knowledge_on_place_bottom(const CombatState& s, CardPoolIndex pi) noexcept;

// `pi` was just inserted at a RANDOM draw-pile position (Wild Strike /
// Reckless Charge -- MAKE_CARD to DRAW_RANDOM). Weakens absolute knowledge to
// the relative-order constraint per the contract note above.
void knowledge_on_insert_random(const CombatState& s, CardPoolIndex pi) noexcept;

// `pi` left the draw pile from a known, player-visible selection (a browse
// screen pick -- DRAW_TO_HAND). Removes it from the chain; entries above keep
// their exactness, entries below keep relative order.
void knowledge_on_remove_known(const CombatState& s, CardPoolIndex pi) noexcept;

// Monster slot `monster_index` just telegraphed an intent that displays a
// construction roll (e.g. Louse BITE damage). Suppressed under HIDE_INTENT.
void knowledge_reveal_monster_roll(const CombatState& s, uint8_t monster_index,
                                   uint8_t value) noexcept;

}  // namespace sts::engine
