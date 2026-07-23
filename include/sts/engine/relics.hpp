#pragma once

// Relic registry -- re-export of the GENERATED relic hook->effect-program table
// (Stage B design §4.3, §5.3; B3.24). The constexpr `RelicDef` table is emitted by
// tools/registry_gen/gen.py from registry/relics.yaml into
// <build>/generated/sts/registry/relic_table.hpp; this header aliases it into
// sts::engine so the relic-hook framework (relic_hooks.cpp) consumes it unchanged.
// There is NO hand-written relic table -- registry/relics.yaml is the single source
// of truth (per-entry provenance + hook bindings live there), and the generated
// header re-pins every id via ids.hpp (append-only, §4.4).
//
// A RelicDef carries the relic's tier (RelicTier -- reward/shop pools gate on it),
// a `native` flag (the escape hatch), and its RelicHook->effect-program bindings.
// Non-combat relics (equip/run-layer hooks) carry zero bindings.
//
// The generated table mirrors the engine's Opcode / StepTarget / RelicHook /
// CardFlag vocabularies (so it compiles standalone); the static_asserts below pin
// RelicHook byte-equal to the authoritative engine enum so codegen cannot drift.

#include <cstdint>

#include "sts/engine/cards.hpp"           // CardEffectStep (reused by hook programs), Opcode pins
#include "sts/engine/relic_hooks.hpp"     // RelicHook (engine authoritative)
#include "sts/engine/types.hpp"           // RelicId
#include "sts/registry/relic_table.hpp"   // generated: the relic table (build tree)

namespace sts::engine {

// --- Re-exported table types (generated) -------------------------------------

using RelicTier = sts::registry::RelicTier;
using RelicHookBinding = sts::registry::RelicHookBinding;
using RelicDef = sts::registry::RelicDef;

using sts::registry::kMaxRelicHooks;
using sts::registry::kMaxRelicHookSteps;
// NOTE: kRelicHookCount is NOT re-exported here -- the engine already declares its
// own sts::engine::kRelicHookCount (relic_hooks.hpp), and the drift pin below
// asserts the generated sts::registry::kRelicHookCount equals it.

// Lookup by RelicId. Returns nullptr for RelicId::NONE or any id without a registry
// row (defensive; hook dispatch simply skips a relic with no def).
using sts::registry::relic_def;

// --- Drift pins --------------------------------------------------------------
// The generated table indexes hooks by sts::registry::RelicHook (its standalone
// mirror of relic_hooks.hpp's RelicHook) and encodes ops via the shared
// CardEffectStep. Pin the RelicHook enum byte-equal to the engine's so the
// generator's hook vocabulary cannot silently diverge from the framework's
// dispatch. (Opcode/StepTarget/CardFlag are already pinned in cards.hpp.)

static_assert(sts::registry::kRelicHookCount == sts::engine::kRelicHookCount,
              "generated kRelicHookCount must equal the engine's "
              "(relic_hooks.hpp RelicHook enum)");

static_assert(
    static_cast<uint8_t>(sts::registry::RelicHook::AT_PRE_BATTLE) ==
            static_cast<uint8_t>(RelicHook::AT_PRE_BATTLE) &&
        static_cast<uint8_t>(sts::registry::RelicHook::AT_BATTLE_START) ==
            static_cast<uint8_t>(RelicHook::AT_BATTLE_START) &&
        static_cast<uint8_t>(sts::registry::RelicHook::AT_BATTLE_START_PRE_DRAW) ==
            static_cast<uint8_t>(RelicHook::AT_BATTLE_START_PRE_DRAW) &&
        static_cast<uint8_t>(sts::registry::RelicHook::AT_TURN_START) ==
            static_cast<uint8_t>(RelicHook::AT_TURN_START) &&
        static_cast<uint8_t>(sts::registry::RelicHook::AT_TURN_START_POST_DRAW) ==
            static_cast<uint8_t>(RelicHook::AT_TURN_START_POST_DRAW) &&
        static_cast<uint8_t>(sts::registry::RelicHook::ON_PLAYER_END_TURN) ==
            static_cast<uint8_t>(RelicHook::ON_PLAYER_END_TURN) &&
        static_cast<uint8_t>(sts::registry::RelicHook::ON_USE_CARD) ==
            static_cast<uint8_t>(RelicHook::ON_USE_CARD) &&
        static_cast<uint8_t>(sts::registry::RelicHook::ON_PLAY_CARD) ==
            static_cast<uint8_t>(RelicHook::ON_PLAY_CARD) &&
        static_cast<uint8_t>(sts::registry::RelicHook::ON_EXHAUST) ==
            static_cast<uint8_t>(RelicHook::ON_EXHAUST) &&
        static_cast<uint8_t>(sts::registry::RelicHook::ON_CARD_DRAW) ==
            static_cast<uint8_t>(RelicHook::ON_CARD_DRAW) &&
        static_cast<uint8_t>(sts::registry::RelicHook::ON_GAINED_BLOCK) ==
            static_cast<uint8_t>(RelicHook::ON_GAINED_BLOCK) &&
        static_cast<uint8_t>(sts::registry::RelicHook::WAS_HP_LOST) ==
            static_cast<uint8_t>(RelicHook::WAS_HP_LOST) &&
        static_cast<uint8_t>(sts::registry::RelicHook::ON_ATTACK) ==
            static_cast<uint8_t>(RelicHook::ON_ATTACK) &&
        static_cast<uint8_t>(sts::registry::RelicHook::ON_VICTORY) ==
            static_cast<uint8_t>(RelicHook::ON_VICTORY),
    "generated sts::registry::RelicHook must stay byte-equal to relic_hooks.hpp's "
    "RelicHook (design doc §4.4; append-only)");

}  // namespace sts::engine
