#pragma once

// Power registry -- re-export of the GENERATED power hook->effect-program table
// (Stage B design §4.3; B3.2). The constexpr `PowerDef` table is emitted by
// tools/registry_gen/gen.py from registry/powers.yaml into
// <build>/generated/sts/registry/power_table.hpp; this header aliases it into
// sts::engine so the hook framework (power_hooks.cpp) consumes it unchanged.
// There is NO hand-written power table -- registry/powers.yaml is the single
// source of truth (per-entry provenance + hook bindings live there), and the
// generated header re-pins every id via ids.hpp (append-only, §4.4).
//
// A PowerDef carries the power's type (BUFF/DEBUFF -- the APPLY_POWER
// interception reads it), its stacking behaviour, a `native` flag (the escape
// hatch), and its Hook->effect-program bindings. The three skeleton powers carry
// zero bindings: their behaviour is the native DAMAGE-pipeline hooks in
// interp.cpp, which are float-exact/hot and deliberately NOT routed through this
// data table.
//
// The generated table mirrors the engine's Opcode / StepTarget / Hook / CardFlag
// vocabularies (so it compiles standalone); the static_asserts below pin them
// byte-equal to the authoritative engine enums so codegen cannot drift.

#include <cstdint>

#include "sts/engine/cards.hpp"           // CardEffectStep (reused by hook programs), Opcode pins
#include "sts/engine/power_hooks.hpp"     // Hook (engine authoritative)
#include "sts/engine/types.hpp"           // PowerId
#include "sts/registry/power_table.hpp"   // generated: the power table (build tree)

namespace sts::engine {

// --- Re-exported table types (generated) -------------------------------------

using PowerType = sts::registry::PowerType;   // BUFF / DEBUFF
using PowerStack = sts::registry::PowerStack;  // INTENSITY / NONE
using PowerHookBinding = sts::registry::PowerHookBinding;
using PowerDef = sts::registry::PowerDef;

using sts::registry::kMaxPowerHooks;
using sts::registry::kMaxPowerHookSteps;
using sts::registry::kPowerHookCount;

// Lookup by PowerId. Returns nullptr for PowerId::NONE or any id without a
// registry row (defensive; hook dispatch simply skips a power with no def).
using sts::registry::power_def;

// --- Drift pins --------------------------------------------------------------
// The generated table indexes hooks by sts::registry::Hook (its standalone mirror
// of power_hooks.hpp's Hook) and encodes ops via the shared CardEffectStep. Pin
// the Hook enum byte-equal to the engine's so the generator's hook vocabulary
// cannot silently diverge from the framework's dispatch. (Opcode/StepTarget/
// CardFlag are already pinned in cards.hpp, included above.)

static_assert(kPowerHookCount == kHookCount,
              "generated kPowerHookCount must equal the engine's kHookCount "
              "(power_hooks.hpp Hook enum)");

static_assert(
    static_cast<uint8_t>(sts::registry::Hook::ON_PLAY_CARD) ==
            static_cast<uint8_t>(Hook::ON_PLAY_CARD) &&
        static_cast<uint8_t>(sts::registry::Hook::ON_USE_CARD) ==
            static_cast<uint8_t>(Hook::ON_USE_CARD) &&
        static_cast<uint8_t>(sts::registry::Hook::ON_EXHAUST) ==
            static_cast<uint8_t>(Hook::ON_EXHAUST) &&
        static_cast<uint8_t>(sts::registry::Hook::ON_CARD_DRAW) ==
            static_cast<uint8_t>(Hook::ON_CARD_DRAW) &&
        static_cast<uint8_t>(sts::registry::Hook::AT_END_OF_TURN_PRE_CARD) ==
            static_cast<uint8_t>(Hook::AT_END_OF_TURN_PRE_CARD) &&
        static_cast<uint8_t>(sts::registry::Hook::AT_END_OF_TURN) ==
            static_cast<uint8_t>(Hook::AT_END_OF_TURN) &&
        static_cast<uint8_t>(sts::registry::Hook::AT_END_OF_ROUND) ==
            static_cast<uint8_t>(Hook::AT_END_OF_ROUND) &&
        static_cast<uint8_t>(sts::registry::Hook::AT_START_OF_TURN) ==
            static_cast<uint8_t>(Hook::AT_START_OF_TURN) &&
        static_cast<uint8_t>(sts::registry::Hook::AT_START_OF_TURN_POST_DRAW) ==
            static_cast<uint8_t>(Hook::AT_START_OF_TURN_POST_DRAW) &&
        static_cast<uint8_t>(sts::registry::Hook::ON_GAINED_BLOCK) ==
            static_cast<uint8_t>(Hook::ON_GAINED_BLOCK) &&
        static_cast<uint8_t>(sts::registry::Hook::ON_ATTACKED) ==
            static_cast<uint8_t>(Hook::ON_ATTACKED) &&
        static_cast<uint8_t>(sts::registry::Hook::ON_APPLY_POWER) ==
            static_cast<uint8_t>(Hook::ON_APPLY_POWER) &&
        static_cast<uint8_t>(sts::registry::Hook::WAS_HP_LOST) ==
            static_cast<uint8_t>(Hook::WAS_HP_LOST) &&
        static_cast<uint8_t>(sts::registry::Hook::ON_DEATH) ==
            static_cast<uint8_t>(Hook::ON_DEATH),
    "generated sts::registry::Hook must stay byte-equal to power_hooks.hpp's "
    "Hook (design doc §4.4; append-only)");

}  // namespace sts::engine
