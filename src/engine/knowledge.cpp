// KnowledgeState hook bodies (see knowledge.hpp for the layer decision, the
// attachment mechanism, and the relative-order contract note). Everything here
// is a pure observer of CombatState: no state writes, no RNG draws.

#include "sts/engine/knowledge.hpp"

#include <cassert>

namespace sts::engine {

namespace {

// The per-thread attachment (knowledge.hpp "HOW THE ENGINE'S EVENT SITES REACH
// IT"). A plain pointer, not state: set/restored by KnowledgeScope only.
thread_local KnowledgeState* g_knowledge = nullptr;

// Recompute full_order after a structural edit: the chain is the whole pile in
// exact order iff it covers every card and every entry is position-exact.
void normalize_full_order(KnowledgeState& k, const CombatState& s) noexcept {
    k.full_order = (k.chain_count > 0 && k.chain_count == s.draw_count &&
                    k.exact_prefix == k.chain_count)
                       ? 1
                       : 0;
}

// Record the ENTIRE pile top-first (the REVEAL_DRAW_ORDER reveal). The pile's
// top is s.draw[s.draw_count - 1] (piles.hpp convention), so the chain walks
// the array backwards.
void record_full_order(KnowledgeState& k, const CombatState& s) noexcept {
    k.chain_count = s.draw_count;
    for (uint8_t i = 0; i < s.draw_count; ++i) {
        k.chain[i] = s.draw[static_cast<uint8_t>(s.draw_count - 1 - i)];
    }
    k.exact_prefix = k.chain_count;
    k.full_order = k.chain_count > 0 ? 1 : 0;
}

void clear_order(KnowledgeState& k) noexcept {
    k.chain_count = 0;
    k.exact_prefix = 0;
    k.full_order = 0;
}

// The chain index of pool instance `pi`, or -1.
[[nodiscard]] int chain_find(const KnowledgeState& k, CardPoolIndex pi) noexcept {
    for (uint8_t i = 0; i < k.chain_count; ++i) {
        if (k.chain[i] == pi) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void chain_remove_at(KnowledgeState& k, int idx) noexcept {
    for (int j = idx + 1; j < static_cast<int>(k.chain_count); ++j) {
        k.chain[j - 1] = k.chain[j];
    }
    --k.chain_count;
    if (idx < static_cast<int>(k.exact_prefix)) {
        // Entries above idx keep their from-top positions; entries that were
        // exact below it shift up by exactly one and stay exact, so the
        // contiguous exact prefix just shortens by the removed member.
        --k.exact_prefix;
    }
}

}  // namespace

KnowledgeState* knowledge_attachment() noexcept { return g_knowledge; }

KnowledgeScope::KnowledgeScope(KnowledgeState* k) noexcept : prev_(g_knowledge) {
    g_knowledge = k;
}

KnowledgeScope::~KnowledgeScope() { g_knowledge = prev_; }

void knowledge_reset() noexcept {
    KnowledgeState* k = g_knowledge;
    if (k == nullptr) {
        return;
    }
    *k = KnowledgeState{};
}

void knowledge_on_combat_ready(const CombatState& s) noexcept {
    KnowledgeState* k = g_knowledge;
    if (k == nullptr) {
        return;
    }
    // Construction spawns telegraph first moves BEFORE the relic mirror is
    // copied (run_advance.cpp step order -- moving the mirror earlier would
    // change which relic hooks see the pre-battle actions, and that order is
    // oracle-verified). A telegraph reveal recorded under an empty mirror is
    // therefore retro-gated here, once the mirror is final.
    if (combat_hides_intent(s)) {
        for (uint8_t i = 0; i < kMonsterCap; ++i) {
            k->monster_roll_known[i] = 0;
            k->monster_roll[i] = 0;
        }
    }
    clear_order(*k);
    if (combat_reveals_draw_order(s)) {
        record_full_order(*k, s);
    }
}

void knowledge_on_shuffle(const CombatState& s) noexcept {
    KnowledgeState* k = g_knowledge;
    if (k == nullptr) {
        return;
    }
    clear_order(*k);
    if (combat_reveals_draw_order(s)) {
        record_full_order(*k, s);
    }
}

void knowledge_on_draw_top(const CombatState& s, CardPoolIndex pi) noexcept {
    KnowledgeState* k = g_knowledge;
    if (k == nullptr) {
        return;
    }
    if (k->chain_count == 0) {
        return;
    }
    if (k->chain[0] == pi) {
        chain_remove_at(*k, 0);
        normalize_full_order(*k, s);
        return;
    }
    // An unknown card was above every chain member, so nothing position-exact
    // could have been true; the relative chain itself is untouched (its
    // members are still in the pile, still in order). A chain member drawn
    // while NOT at the chain front would mean a mutation site missed its hook
    // -- that is a bug in the engine wiring, not a knowledge model case.
    assert(chain_find(*k, pi) < 0 &&
           "a chain member left the pile without its hook firing");
    assert(k->exact_prefix == 0 &&
           "an exact-top constraint survived past a foreign top card");
    // Defensive (release): if the asserts above are wrong, degrade to no
    // knowledge rather than carry a false constraint.
    const int idx = chain_find(*k, pi);
    if (idx >= 0 || k->exact_prefix != 0) {
        clear_order(*k);
    }
}

void knowledge_on_place_top(const CombatState& s, CardPoolIndex pi) noexcept {
    KnowledgeState* k = g_knowledge;
    if (k == nullptr) {
        return;
    }
    if (combat_reveals_draw_order(s)) {
        record_full_order(*k, s);
        return;
    }
    // The card may already be a chain member (e.g. a known card recalled from
    // the pile and placed back on top); drop the stale entry first.
    const int existing = chain_find(*k, pi);
    if (existing >= 0) {
        chain_remove_at(*k, existing);
    }
    if (k->chain_count >= kKnowledgeChainCap) {
        return;  // unreachable: chain members are distinct pile cards
    }
    for (int j = k->chain_count; j > 0; --j) {
        k->chain[j] = k->chain[j - 1];
    }
    k->chain[0] = pi;
    ++k->chain_count;
    // The new card is exactly on top; a previously exact prefix sits exactly
    // one deeper, so the whole prefix extends by one. A zero prefix becomes 1
    // (only the new top is position-exact).
    k->exact_prefix = static_cast<uint8_t>(k->exact_prefix + 1);
    normalize_full_order(*k, s);
}

void knowledge_on_place_bottom(const CombatState& s, CardPoolIndex pi) noexcept {
    KnowledgeState* k = g_knowledge;
    if (k == nullptr) {
        return;
    }
    if (combat_reveals_draw_order(s)) {
        record_full_order(*k, s);
        return;
    }
    const int existing = chain_find(*k, pi);
    if (existing >= 0) {
        chain_remove_at(*k, existing);
    }
    if (k->chain_count >= kKnowledgeChainCap) {
        return;  // unreachable, as above
    }
    // Below everything: relative order appends at the tail; exactness of the
    // from-top prefix is untouched (nothing above it moved).
    k->chain[k->chain_count] = pi;
    ++k->chain_count;
    normalize_full_order(*k, s);
}

void knowledge_on_insert_random(const CombatState& s, CardPoolIndex pi) noexcept {
    KnowledgeState* k = g_knowledge;
    if (k == nullptr) {
        return;
    }
    if (combat_reveals_draw_order(s)) {
        // Frozen Eye watches the insertion land: the reveal simply refreshes.
        record_full_order(*k, s);
        return;
    }
    if (s.draw_count == 1) {
        // The pile was empty, so the "random" spot is the only spot: the
        // inserted card is the entire pile, exactly known.
        k->chain_count = 1;
        k->chain[0] = pi;
        k->exact_prefix = 1;
        k->full_order = 1;
        return;
    }
    // The declared posterior is a uniform interleaving preserving relative
    // order (the contract note in knowledge.hpp): every absolute position
    // claim dies, the chain's relative order survives, and the inserted card
    // itself joins no chain (its position carries no relative constraint).
    k->exact_prefix = 0;
    k->full_order = 0;
}

void knowledge_on_remove_known(const CombatState& s, CardPoolIndex pi) noexcept {
    KnowledgeState* k = g_knowledge;
    if (k == nullptr) {
        return;
    }
    const int idx = chain_find(*k, pi);
    if (idx >= 0) {
        chain_remove_at(*k, idx);
    }
    normalize_full_order(*k, s);
}

void knowledge_reveal_monster_roll(const CombatState& s, uint8_t monster_index,
                                   uint8_t value) noexcept {
    KnowledgeState* k = g_knowledge;
    if (k == nullptr) {
        return;
    }
    if (monster_index >= kMonsterCap) {
        return;
    }
    if (combat_hides_intent(s)) {
        return;  // HIDE_INTENT: the telegraph is never shown, nothing revealed
    }
    k->monster_roll_known[monster_index] = 1;
    k->monster_roll[monster_index] = value;
}

}  // namespace sts::engine
