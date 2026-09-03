#pragma once

// Run-layer card-pool draws that more than one caller needs.
//
// The ordinary random-curse pool. Source: AbstractDungeon.returnRandomCurse()
// delegates to CardLibrary.getCurse(), which excludes Ascender's Bane and the
// special Necronomicurse/Bell Curse/Pride entries. The ten eligible entries are
// authored as curse_pool rows in registry/cards.yaml and this selection consumes
// exactly one card_rng random(0, count-1), matching CardLibrary.getCurse().
//
// ...and `transform_card`, which is the ONE authority for
// AbstractDungeon.transformCard's list. See its own comment below.

#include <cassert>
#include <cstddef>

#include "sts/engine/cards.hpp"
#include "sts/engine/rng_stream.hpp"

namespace sts::engine {

[[nodiscard]] inline CardId return_random_curse(RngStream& card_rng) noexcept {
    static_assert(kPoolableCurseCount == 10,
                  "CardLibrary.getCurse pool has ten ordinary curses");
    const auto index = static_cast<std::size_t>(
        random(card_rng, static_cast<int32_t>(kPoolableCurseCount - 1)));
    return kPoolableCurses[index];
}

// CardDef carries no colour column, so colour is read where it is already
// expressed: membership of the generated colorless pool. That pool IS the set
// transformCard's COLORLESS branch would draw from, so the two agree by
// construction rather than by a second table.
[[nodiscard]] inline bool card_is_colorless(CardId id) noexcept {
    for (int i = 0; i < kColorlessPoolCount; ++i) {
        if (kColorlessPool[static_cast<std::size_t>(i)] == id) {
            return true;
        }
    }
    return false;
}

// AbstractDungeon.transformCard(c, false, rng) (AbstractDungeon.java:860-878)
// branching on the transformed card's COLOR, each branch one draw off the
// passed rng:
//   COLORLESS -> returnTrulyRandomColorlessCardFromAvailable (:1007-1014): the
//                whole colorless pool minus this card's own id.
//   CURSE     -> CardLibrary.getCurse(c, rng) (CardLibrary.java:1031-1038): the
//                ten ordinary curses minus this card's own id.
//   otherwise -> returnTrulyRandomCardFromAvailable (:1016-1045): commonCardPool
//                ++ srcUncommonCardPool ++ srcRareCardPool, minus this card's
//                own id.
// A BASIC card (every Neow-era master-deck row but the starting curse) is in
// none of these pools, so nothing is excluded from its list. Note the `default:`
// arm catches BASIC/SPECIAL and anything else non-COLORLESS non-CURSE, which is
// why this function needs no "unknown colour" branch.
//
// THE THREE-POOL LIST MIXES TWO SPELLINGS, AND THE MIX IS OBSERVABLE. Only the
// first term reads a LIVE pool. The other two read the `src*` copies, and
// initializeCardPools builds every copy with `addToBottom`
// (AbstractDungeon.java:1180-1199), which is `group.add(0, c)` -- a PREPEND
// (CardGroup.java:459-461) -- so a `src*` pool holds its rarity's library order
// BACKWARDS. The list is therefore plain-order commons, then REVERSED
// uncommons, then REVERSED rares. Walking all three forwards was the earlier
// reading, from before the CardLibrary order itself was pinned, and it is
// indistinguishable from the correct one until the pools stop being symmetric:
// the oracle capture is what separated them, on two seeds whose transformed
// identities landed in the uncommon and rare blocks. `src_combat_order` in the
// registry emitter encodes the same reversal for the pools whose only consumer
// is a `src*` read; this call site cannot use those, because its FIRST block is
// the un-copied pool.
//
// ONE AUTHORITY, DELIBERATELY. Every in-game caller of transformCard reaches
// this list: Neow's TRANSFORM_CARD / TRANSFORM_TWO_CARDS (NeowReward.java:
// 157-173) with NeowEvent.rng, and the event grids -- Living Wall
// (LivingWall.java:67-75) and Transmorgrifier (Transmogrifier.java:44-55) --
// with miscRng. The rng is the ONLY thing that differs, so it is the only
// parameter. This used to be two independent renderings, and they drifted: the
// event grids read a separately emitted `kEventTransformRedPool` that walked
// registry rows, which reproduced Iron Wave where the game produced Havoc on
// STS00051's floor-2 Living Wall. Do not re-introduce a second spelling.
// The longest list any branch can produce: the RED branch's three-pool
// concatenation. The two static_asserts below are what keep it a bound rather
// than a guess as content lands.
inline constexpr int kTransformCardListCap =
    kIroncladCommonPoolCount + kIroncladUncommonPoolCount +
    kIroncladRarePoolCount;
static_assert(kColorlessPoolCount <= kTransformCardListCap);
static_assert(kPoolableCurseCount <= kTransformCardListCap);

// Build transformCard's candidate list into `out` (at least
// kTransformCardListCap entries) and return its length. Split out from the draw
// so the ORDER can be pinned by a test without a seed --
// `CardPoolLibraryOrder.TransformCardListIsCommonsThenBothSrcPoolsBackwards`.
[[nodiscard]] inline int transform_card_list(CardId prohibited,
                                             CardId* out) noexcept {
    const CardDef* def = card_def(prohibited);
    const bool is_curse = def != nullptr && def->type == CardType::CURSE;

    int n = 0;
    auto push = [&](CardId id) noexcept {
        if (id != prohibited) {
            out[n++] = id;
        }
    };

    if (is_curse) {
        for (int i = 0; i < kPoolableCurseCount; ++i) {
            push(kPoolableCurses[static_cast<std::size_t>(i)]);
        }
    } else if (card_is_colorless(prohibited)) {
        for (int i = 0; i < kColorlessPoolCount; ++i) {
            push(kColorlessPool[static_cast<std::size_t>(i)]);
        }
    } else {
        for (int i = 0; i < kIroncladCommonPoolCount; ++i) {
            push(kIroncladCommonPool[static_cast<std::size_t>(i)]);  // live pool
        }
        for (int i = kIroncladUncommonPoolCount - 1; i >= 0; --i) {
            push(kIroncladUncommonPool[static_cast<std::size_t>(i)]);  // src copy
        }
        for (int i = kIroncladRarePoolCount - 1; i >= 0; --i) {
            push(kIroncladRarePool[static_cast<std::size_t>(i)]);  // src copy
        }
    }
    return n;
}

[[nodiscard]] inline CardId transform_card(RngStream& rng,
                                           CardId prohibited) noexcept {
    CardId list[kTransformCardListCap]{};
    const int n = transform_card_list(prohibited, list);
    assert(n > 0);
    const int32_t i = random(rng, n - 1);
    return list[static_cast<std::size_t>(i)];
}

}  // namespace sts::engine
