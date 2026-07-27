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
#include "sts/registry/event_table.hpp"
#include "sts/registry/manifest.hpp"
#include "sts/registry/monster_table.hpp"
#include "sts/registry/power_table.hpp"
#include "sts/registry/relic_table.hpp"

namespace {

// Compile-time sanity: the pinned skeleton ids and manifest counts are usable
// with nothing but the generated headers in scope.
static_assert(static_cast<int>(sts::registry::CardId::STRIKE) == 1);
static_assert(static_cast<int>(sts::registry::PowerId::VULNERABLE) == 2);
static_assert(static_cast<int>(sts::registry::MonsterId::JAW_WORM) == 1);
// 91 (through the red rares) + the 20 colorless UNCOMMON rows at ids 92-111.
// 18 of them landed first; the last two -- 101 Forethought and 109 Purity --
// were held back until the engine had an OPTIONAL zero-to-N hand selection to
// express them with, and now fill those interior ids, so the block is complete
// and gapless. + B3.11 stage A's 4 colorless RARE rows (112 Apotheosis, 116
// Master of Strategy, 120 Sadistic Nature, 124 Thinking Ahead) and stage B's 3
// (121 Secret Technique, 122 Secret Weapon, 126 Violence) and stage C's 5 (113
// Chrysalis, 115 Magnetism, 117 Mayhem, 118 Metamorphosis, 125 Transmutation);
// stage D's 3, which FILL the 114/119/123 gaps the earlier stages reserved
// (114 Hand of Greed, 119 Panache, 123 The Bomb) -- with them the colorless RARE
// block 112-126 is complete and holds no gap.
static_assert(sts::registry::manifest::kCardsCount == 126);
static_assert(sts::registry::kPoolableCurseCount == 10);    // CardLibrary.getCurse
static_assert(sts::registry::kMaxCardSteps == 5);  // B3.5: upgraded Pummel, 5 hits
// Infernal Blade's in-combat ATTACK pool. The red rares add FIVE attacks but only
// THREE pool members: Feed and Reaper carry CardTags.HEALING, which
// returnTrulyRandomCardInCombat excludes (AbstractDungeon.java:964-979). The
// colorless uncommons add NONE: the pool builder is `color == "RED"`-gated, and
// all fourteen are COLORLESS, so the count is unchanged by that batch.
static_assert(sts::registry::kIroncladAttackPoolCount == 28);
// Chrysalis's SKILL sibling of that pool (B3.11 stage C), derived by the SAME
// generator rule with CardType.SKILL substituted -- returnTrulyRandomCardIn-
// Combat(SKILL). Also RED-gated, so the colorless batches add none, and its
// membership is disjoint from the ATTACK pool's by construction. (The two
// counts landing on the same number is a coincidence of the Ironclad card set,
// not a shared constant.)
static_assert(sts::registry::kIroncladSkillPoolCount == 28);

// Power table (B3.2): the constexpr PowerDef evaluates at compile time with
// nothing but the generated headers in scope.
static_assert(sts::registry::kStrengthPower.hook_count == 0);
static_assert(sts::registry::kFeelNoPainPower.hook_count == 1);
static_assert(sts::registry::kFeelNoPainPower.hooks[0].hook ==
              sts::registry::Hook::ON_EXHAUST);
static_assert(sts::registry::kFeelNoPainPower.hooks[0].step_count == 1);
static_assert(sts::registry::kCorruptionPower.native);
static_assert(sts::registry::kCorruptionPower.hook_count == 2);

// Relic table (B3.24): the constexpr RelicDef evaluates at compile time with
// nothing but the generated headers in scope.
static_assert(static_cast<int>(sts::registry::RelicId::BURNING_BLOOD) == 1);
static_assert(sts::registry::kBurningBloodRelic.native);
static_assert(sts::registry::kBurningBloodRelic.tier ==
              sts::registry::RelicTier::STARTER);
static_assert(sts::registry::kAnchorRelic.hook_count == 1);
static_assert(sts::registry::kAnchorRelic.hooks[0].hook ==
              sts::registry::RelicHook::AT_BATTLE_START);
static_assert(sts::registry::kAnchorRelic.hooks[0].steps[0].amount == 10);
static_assert(sts::registry::kWhetstoneRelic.hook_count == 0);  // non-combat

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
[[maybe_unused]] const sts::registry::PowerDef* kPowerProbe =
    sts::registry::power_def(sts::registry::PowerId::CORRUPTION);
[[maybe_unused]] const sts::registry::RelicDef* kRelicProbe =
    sts::registry::relic_def(sts::registry::RelicId::NUNCHAKU);

}  // namespace
