// Standalone-compilation check for the generated registry headers (B2.1
// acceptance: "generated headers compile standalone"). This translation unit
// includes ONLY the generated headers -- no engine headers -- so if it compiles
// and links, each generated header is self-contained (pulls in exactly the
// standard headers it needs and nothing from sts::engine).
//
// The runtime assertions live in registry_gen_test.cpp; this file's job is purely
// to prove the headers stand on their own at compile time.

#include "sts/registry/ids.hpp"
#include "sts/registry/card_table.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/manifest.hpp"
#include "sts/registry/monster_table.hpp"

namespace {

// Compile-time sanity: the pinned skeleton ids and manifest counts are usable
// with nothing but the generated headers in scope.
static_assert(static_cast<int>(sts::registry::CardId::STRIKE) == 1);
static_assert(static_cast<int>(sts::registry::PowerId::VULNERABLE) == 2);
static_assert(static_cast<int>(sts::registry::MonsterId::JAW_WORM) == 1);
static_assert(sts::registry::manifest::kCardsCount == 5);
static_assert(sts::registry::kMaxCardSteps == 2);

// Monster table (B2.2): the constexpr tier lookups evaluate at compile time
// with nothing but the generated headers in scope.
static_assert(sts::registry::kJawWorm.move_count == 3);
static_assert(sts::registry::kJawWorm.hp_min(20) == 42);
static_assert(sts::registry::kJawWorm.hp_max(6) == 44);
static_assert(sts::registry::kJawWorm.move(sts::registry::kJawWormMoveChomp)
                  ->effects[0].amount.at(20) == 12);
static_assert(sts::registry::kJawWorm.move(9) == nullptr);

// Odr-use one generated accessor of each table so the TU also links standalone.
[[maybe_unused]] const sts::registry::CardDef* kProbe =
    sts::registry::card_def(sts::registry::CardId::BASH);
[[maybe_unused]] const sts::registry::MonsterDef* kMonsterProbe =
    sts::registry::monster_def(sts::registry::MonsterId::JAW_WORM);

}  // namespace
