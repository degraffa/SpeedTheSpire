#pragma once

// The ordinary random-curse pool. Source: AbstractDungeon.returnRandomCurse()
// delegates to CardLibrary.getCurse(), which excludes Ascender's Bane and the
// special Necronomicurse/Bell Curse/Pride entries. The ten eligible entries are
// authored as curse_pool rows in registry/cards.yaml and this selection consumes
// exactly one card_rng random(0, count-1), matching CardLibrary.getCurse().

#include <cstddef>

#include "sts/engine/cards.hpp"
#include "sts/engine/rng_stream.hpp"

namespace sts::engine {

[[nodiscard]] inline CardId return_random_curse(RngStream& card_rng) noexcept {
    static_assert(kPoolableCurseCount == 10,
                  "B3.9: CardLibrary.getCurse pool has ten ordinary curses");
    const auto index = static_cast<std::size_t>(
        random(card_rng, static_cast<int32_t>(kPoolableCurseCount - 1)));
    return kPoolableCurses[index];
}

}  // namespace sts::engine
