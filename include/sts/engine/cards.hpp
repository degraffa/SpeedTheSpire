#pragma once

// Card registry -- the five M1 skeleton cards as a hand-coded constexpr table
// (design doc §6 "rules-as-data"; §9 skeleton scope). This is A4.3's registry
// deliverable. Stage B replaces this hand table with the codegen'd YAML-driven
// constexpr tables (design doc §6); the SHAPE here (an array of {op, amount,
// extra, target} steps per card) is deliberately the shape that codegen will
// emit, so nothing downstream changes when the registry pipeline lands.
//
// WHY A DATA TABLE (not a dispatch function per card): design doc §6 freezes
// "effect programs are sequences of {op, target, amount, flags}". A constexpr
// step table is the literal encoding of that decision, keeps all five cards'
// behavior readable in one place next to their provenance, and is exactly what
// the Stage B codegen produces -- so the interpreter (A4.1) and card-play
// resolver (card_play.cpp) stay effect-program-driven rather than growing a
// switch per card. Header-only: the table is `constexpr`, no .cpp needed.
//
// Provenance (read in full from the decompiled tree under
// D:/STS_BG_Mod/SlayTheSpireDecompiled/com/megacrit/cardcrawl/cards/red,
// verified for A4.3 -- each card's use() and its addToBot(...) order):
//   * Strike_Red.use  (Strike_Red.java)  -> DamageAction(6)                [1E, Attack]
//   * Defend_Red.use  (Defend_Red.java)  -> GainBlockAction(5)             [1E, Skill]
//   * Bash.use        (Bash.java)        -> DamageAction(8), then
//                                            ApplyPowerAction(Vulnerable, 2) [2E, Attack]
//   * ShrugItOff.use  (ShrugItOff.java)  -> GainBlockAction(8), then
//                                            DrawCardAction(1)              [1E, Skill]
//   * PommelStrike.use(PommelStrike.java)-> DamageAction(9), then
//                                            DrawCardAction(1)              [1E, Attack]
// Base (un-upgraded) stats only; the upgraded numbers are Stage B (the skeleton
// deck is all base cards, design doc §9). Damage/block AMOUNTS are the card's
// base values -- Strength/Vulnerable modifiers are applied by the DAMAGE opcode
// pipeline (A4.1) at resolution time, never baked into the table.

#include <array>
#include <cstdint>

#include "sts/engine/interp.hpp"   // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"    // CardId, PowerId

namespace sts::engine {

// Card type -- only the Attack/Skill distinction the skeleton needs for target
// validation (an Attack targets a monster; a Skill targets the player/self).
// Powers/Status/Curse are Stage B.
enum class CardType : uint8_t {
    ATTACK = 0,
    SKILL = 1,
};

// Where a single effect step lands. The registry cannot bake in a concrete
// actor index (the target monster isn't known until the card is actually
// played), so each step records WHICH actor it hits symbolically; card_play.cpp
// substitutes the concrete index (kActorPlayer for SELF, the chosen/rolled
// monster slot for CARD_TARGET) at resolution time.
enum class StepTarget : uint8_t {
    SELF = 0,         // the player (block, draw, self-buffs)
    CARD_TARGET = 1,  // the monster the card was played on (or the random roll)
};

// One effect-program step (design doc §6: "{op, target, amount, flags}"). For
// APPLY_POWER, `extra` carries the PowerId via make_apply_power_flags() (the
// same flags encoding the DAMAGE/APPLY_POWER opcodes read, interp.hpp); for
// every other opcode `extra` is 0.
struct CardEffectStep {
    Opcode op;
    int32_t amount;
    uint32_t extra;
    StepTarget target;
};

// Max effect steps any skeleton card needs (Bash/ShrugItOff/PommelStrike = 2).
inline constexpr int kMaxCardSteps = 2;

// A registry entry: cost + type + targeting + the effect program.
struct CardDef {
    CardId id;
    uint8_t base_cost;
    CardType type;
    bool needs_target;   // true for Attacks (choose a monster); false for self Skills
    bool random_target;  // TRAP 10: rolled at dequeue via cardRandomRng. false for
                         // all five skeleton cards; the branch exists so trap 10
                         // has a reachable call site (see card_play.cpp / tests).
    uint8_t step_count;
    std::array<CardEffectStep, kMaxCardSteps> steps;
};

// --- The five skeleton cards ------------------------------------------------
// Each entry mirrors its use()'s addToBot order exactly (see provenance above).

inline constexpr CardDef kStrike{
    CardId::STRIKE, /*cost*/ 1, CardType::ATTACK, /*needs_target*/ true,
    /*random_target*/ false, /*step_count*/ 1,
    {{
        {Opcode::DAMAGE, 6, 0, StepTarget::CARD_TARGET},
        {Opcode::NOP, 0, 0, StepTarget::SELF},
    }}};

inline constexpr CardDef kDefend{
    CardId::DEFEND, 1, CardType::SKILL, false, false, 1,
    {{
        {Opcode::BLOCK, 5, 0, StepTarget::SELF},
        {Opcode::NOP, 0, 0, StepTarget::SELF},
    }}};

inline constexpr CardDef kBash{
    CardId::BASH, 2, CardType::ATTACK, true, false, 2,
    {{
        {Opcode::DAMAGE, 8, 0, StepTarget::CARD_TARGET},
        {Opcode::APPLY_POWER, 2, make_apply_power_flags(PowerId::VULNERABLE),
         StepTarget::CARD_TARGET},
    }}};

inline constexpr CardDef kShrugItOff{
    CardId::SHRUG_IT_OFF, 1, CardType::SKILL, false, false, 2,
    {{
        {Opcode::BLOCK, 8, 0, StepTarget::SELF},
        {Opcode::DRAW, 1, 0, StepTarget::SELF},
    }}};

inline constexpr CardDef kPommelStrike{
    CardId::POMMEL_STRIKE, 1, CardType::ATTACK, true, false, 2,
    {{
        {Opcode::DAMAGE, 9, 0, StepTarget::CARD_TARGET},
        {Opcode::DRAW, 1, 0, StepTarget::SELF},
    }}};

// Lookup by CardId. Returns nullptr for CardId::NONE or any id outside the
// skeleton set (a defensive guard; card_play asserts a real card was played).
[[nodiscard]] inline const CardDef* card_def(CardId id) noexcept {
    switch (id) {
        case CardId::STRIKE:        return &kStrike;
        case CardId::DEFEND:        return &kDefend;
        case CardId::BASH:          return &kBash;
        case CardId::SHRUG_IT_OFF:  return &kShrugItOff;
        case CardId::POMMEL_STRIKE: return &kPommelStrike;
        case CardId::NONE:
        default:                    return nullptr;
    }
}

}  // namespace sts::engine
