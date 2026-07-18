#pragma once

// Card registry -- re-export of the GENERATED effect-program tables (Stage B
// design §4.3/§4.4). The constexpr `CardDef`/`CardEffectStep` table is emitted
// by tools/registry_gen/gen.py from registry/cards.yaml into
// <build>/generated/sts/registry/card_table.hpp; this header aliases it into
// sts::engine so the interpreter (interp.hpp) and card-play resolver
// (card_play.cpp) consume it unchanged. There is NO hand-written card table
// anymore -- registry/cards.yaml is the single source of truth (per-entry
// provenance lives there), and the generated header re-pins every id with a
// `static_assert` (ids are append-only, Stage B design §4.4).
//
// WHY A DATA TABLE (not a dispatch function per card): design doc §6 freezes
// "effect programs are sequences of {op, target, amount, flags}". A constexpr
// step table is the literal encoding of that decision -- the interpreter and
// card-play resolver stay effect-program-driven rather than growing a switch
// per card. Header-only: the table is `constexpr`, no .cpp needed.
//
// The generated table carries its own Opcode mirror (sts::registry::Opcode) so
// the header compiles standalone; the engine's authoritative Opcode (with the
// §6 numbering rationale) stays hand-written in interp.hpp. The static_asserts
// below pin the two byte-equal so they cannot drift.

#include <cstdint>

#include "sts/engine/interp.hpp"          // Opcode (engine authoritative set)
#include "sts/engine/types.hpp"           // CardId, PowerId
#include "sts/registry/card_table.hpp"    // generated: the card tables (build tree)

namespace sts::engine {

// --- Re-exported table types (generated) -------------------------------------

// Card type -- Attack/Skill for target validation (an Attack targets a monster;
// a Skill targets the player/self). Powers/Status/Curse land with Stage B
// content tasks.
using CardType = sts::registry::CardType;

// Where a single effect step lands, symbolically; card_play.cpp substitutes the
// concrete actor index (kActorPlayer for SELF, the chosen/rolled monster slot
// for CARD_TARGET) at resolution time.
using StepTarget = sts::registry::StepTarget;

// One effect-program step (design doc §6: "{op, target, amount, flags}"). For
// APPLY_POWER, `extra` carries the PowerId (the make_apply_power_flags
// encoding, pinned below); for every other opcode `extra` is 0.
using CardEffectStep = sts::registry::CardEffectStep;

// A registry entry: cost + type + targeting + the effect program.
using CardDef = sts::registry::CardDef;

using sts::registry::kMaxCardSteps;

// --- The card table (generated from registry/cards.yaml) ---------------------
// Each entry mirrors its use()'s addToBot order exactly; provenance is cited
// per-entry in the YAML.

using sts::registry::kStrike;
using sts::registry::kDefend;
using sts::registry::kBash;
using sts::registry::kShrugItOff;
using sts::registry::kPommelStrike;

// Lookup by CardId. Returns nullptr for CardId::NONE or any id without a
// registry row (a defensive guard; card_play asserts a real card was played).
using sts::registry::card_def;

// --- Drift pins ---------------------------------------------------------------
// The generated table encodes ops as sts::registry::Opcode (its standalone
// mirror of interp.hpp's set) and APPLY_POWER power ids in `extra` via the
// make_apply_power_flags packing. Pin both equal to the engine's so the
// generator's vocabulary cannot silently diverge from the interpreter's.

static_assert(
    static_cast<uint16_t>(sts::registry::Opcode::NOP) ==
            static_cast<uint16_t>(Opcode::NOP) &&
        static_cast<uint16_t>(sts::registry::Opcode::DAMAGE) ==
            static_cast<uint16_t>(Opcode::DAMAGE) &&
        static_cast<uint16_t>(sts::registry::Opcode::BLOCK) ==
            static_cast<uint16_t>(Opcode::BLOCK) &&
        static_cast<uint16_t>(sts::registry::Opcode::APPLY_POWER) ==
            static_cast<uint16_t>(Opcode::APPLY_POWER) &&
        static_cast<uint16_t>(sts::registry::Opcode::DRAW) ==
            static_cast<uint16_t>(Opcode::DRAW) &&
        static_cast<uint16_t>(sts::registry::Opcode::GAIN_ENERGY) ==
            static_cast<uint16_t>(Opcode::GAIN_ENERGY) &&
        static_cast<uint16_t>(sts::registry::Opcode::SHUFFLE_IN) ==
            static_cast<uint16_t>(Opcode::SHUFFLE_IN) &&
        static_cast<uint16_t>(sts::registry::Opcode::EXHAUST) ==
            static_cast<uint16_t>(Opcode::EXHAUST) &&
        static_cast<uint16_t>(sts::registry::Opcode::ROLL_MOVE) ==
            static_cast<uint16_t>(Opcode::ROLL_MOVE),
    "generated sts::registry::Opcode must stay byte-equal to interp.hpp's "
    "Opcode (design doc §6 numbering; append-only)");

static_assert(kBash.steps[1].extra == make_apply_power_flags(PowerId::VULNERABLE),
              "generated APPLY_POWER `extra` must use the make_apply_power_flags "
              "packing (interp.hpp)");

}  // namespace sts::engine
