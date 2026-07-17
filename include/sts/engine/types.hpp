#pragma once

// Core id enums and small POD instance/action types shared by the state
// structs (design doc §4.2) and the effect interpreter (design doc §6, §7).
//
// This header is hand-written scaffolding for the M1 walking skeleton only
// (design doc §9: Ironclad vs. Jaw Worm, five cards, three powers, zero
// relics). The `CardId`/`PowerId`/`MonsterId`/`RelicId` enums here list only
// the skeleton's members plus a `NONE` sentinel each; the full game's id
// tables are Stage B+ work, generated from the `registry/` YAML by the
// codegen step described in design doc §6 (not built yet). Extending an enum
// later is additive (new values appended before nothing needs to move,
// existing values are stable) so nothing downstream breaks when the registry
// pipeline lands.
//
// All types here are fixed-width (<cstdint>), trivially copyable, pointer-
// free, and header-only, matching the conventions established by
// rng_stream.hpp. Namespace sts::engine, per the existing headers.

#include <cstdint>
#include <type_traits>

namespace sts::engine {

// --- Id enums ---------------------------------------------------------------
//
// Convention (frozen for this file): every id enum reserves 0 as `NONE`, a
// sentinel meaning "no id" / "empty slot" -- e.g. an unused card-instance-pool
// row, a monster's absent relic-immune slot, or (for CardId) a null cardQueue
// entry distinct from the end-turn sentinel (design doc §5.1, which is a
// separate flag/opcode, not a CardId value). Real ids start at 1 so that a
// value-initialized (zeroed) struct is trivially "empty" without needing an
// extra `bool` flag -- consistent with design doc §4.1's value-initialization/
// hash-stability principle.

// Skeleton card set (design doc §9). Backed by u16 per design doc §4.2's
// card-instance-pool row (`card_id: u16`).
enum class CardId : uint16_t {
    NONE = 0,
    STRIKE,
    DEFEND,
    BASH,
    SHRUG_IT_OFF,
    POMMEL_STRIKE,
};

// Powers exercised by the skeleton (design doc §9: Strength, Vulnerable,
// Weak). Backed by u16 per design doc §4.2's power-slot row.
enum class PowerId : uint16_t {
    NONE = 0,
    STRENGTH,
    VULNERABLE,
    WEAK,
};

// The skeleton's one enemy (design doc §9: Jaw Worm). `NONE` doubles as "no
// monster" for an empty monster slot; the player is never represented as a
// MonsterId (player state lives in CombatState's dedicated player fields per
// design doc §4.2, not the monster array).
enum class MonsterId : uint16_t {
    NONE = 0,
    JAW_WORM,
};

// The skeleton has zero relics (design doc §9). This enum exists only so
// RelicId is a real type other headers (e.g. a future RunState's relic list,
// design doc §4.3) can name now; it is deliberately unpopulated beyond the
// sentinel pending Stage B's registry-driven relic table.
enum class RelicId : uint16_t {
    NONE = 0,
};

// --- ActionVerb --------------------------------------------------------------

// The batch API's action verb set (design doc §7): "one enum, all phases".
// The skeleton exercises PLAY_CARD and END_TURN; USE_POTION and CHOOSE are
// listed now per §7's frozen encoding even though nothing in M1 scope emits
// them yet (no potions, no choice prompts in the 5-card micro-game).
enum class ActionVerb : uint8_t {
    PLAY_CARD = 0,   // arg0 = hand index, arg1 = target monster slot
    END_TURN = 1,    // no args
    USE_POTION = 2,  // arg0 = potion slot, arg1 = target monster slot
    CHOOSE = 3,      // arg0 = option index
};

// --- CardInstance -------------------------------------------------------------

// One row of the card-instance pool (design doc §4.2: capacity 160). Piles
// (hand/draw/discard/exhaust/limbo) reference instances by index into this
// pool, never by pointer (design doc §4.1).
struct CardInstance {
    uint16_t card_id;  // CardId; NONE marks an empty pool slot.
    uint8_t upgrade;   // upgrade count/flag; 0 = base, 1 = upgraded (the
                        // skeleton's 5 cards are single-upgrade like the real
                        // game, but this is a plain count so a future card
                        // with multiple upgrade tiers doesn't need a new
                        // field).
    uint8_t cost_now;  // current energy cost after modifiers (e.g. a future
                        // cost-reduction power); base cost lives in the
                        // registry's per-CardId constexpr table (design doc
                        // §6), not here -- this is the per-instance runtime
                        // value actually charged on play.
    uint16_t flags;    // reserved bitfield for per-instance state bits (e.g.
                        // exhaust/ethereal/innate-style flags a future card
                        // needs); unused by all five skeleton cards today,
                        // kept as plain reserved storage rather than named
                        // bits to avoid over-engineering ahead of need.
    uint16_t misc;      // reserved scratch field for future per-instance data
                        // (e.g. a card-specific counter); unused by the
                        // skeleton set.
};

static_assert(std::is_trivially_copyable_v<CardInstance>,
              "CardInstance must be trivially copyable (design doc §4.1: "
              "snapshot = memcpy)");
static_assert(sizeof(CardInstance) == 8,
              "CardInstance must be exactly 8 bytes (design doc §4.2 card-"
              "instance-pool row: u16+u8+u8+u16+u16)");

// --- PowerSlot ----------------------------------------------------------------

// One row of a powers list (design doc §4.2: 24 slots on the player, 24 per
// monster). `amount` is signed because some real powers (not in the
// skeleton) can go negative in-game bookkeeping; the three skeleton powers
// (Strength, Vulnerable, Weak) are all non-negative stack counts in practice.
struct PowerSlot {
    uint16_t power_id;  // PowerId; NONE marks an empty slot.
    int16_t amount;
};

static_assert(std::is_trivially_copyable_v<PowerSlot>,
              "PowerSlot must be trivially copyable (design doc §4.1: "
              "snapshot = memcpy)");
static_assert(sizeof(PowerSlot) == 4,
              "PowerSlot must be exactly 4 bytes (design doc §4.2: u16+i16)");

// --- Action ---------------------------------------------------------------

// The batch API's action encoding (design doc §7, verbatim):
//   struct Action { uint32_t bits; };  // verb:8 | arg0:8 | arg1:8 | arg2:8
//
// Bit layout (this file picks the concrete packing design doc §7 leaves
// implicit in its field order): verb occupies the low byte, then arg0, arg1,
// arg2 in ascending byte order --
//   bits = verb | (arg0 << 8) | (arg1 << 16) | (arg2 << 24)
// Use make_action()/action_verb()/action_arg{0,1,2}() rather than raw shifts
// so this choice is centralized in one place.
struct Action {
    uint32_t bits;
};

static_assert(std::is_trivially_copyable_v<Action>,
              "Action must be trivially copyable (design doc §4.1/§7: POD, "
              "memcpy'able trajectory record)");
static_assert(sizeof(Action) == 4,
              "Action must be exactly 4 bytes (design doc §7: packed u32)");

[[nodiscard]] constexpr Action make_action(ActionVerb verb, uint8_t arg0 = 0,
                                            uint8_t arg1 = 0,
                                            uint8_t arg2 = 0) noexcept {
    const uint32_t bits =
        static_cast<uint32_t>(static_cast<uint8_t>(verb)) |
        (static_cast<uint32_t>(arg0) << 8) |
        (static_cast<uint32_t>(arg1) << 16) |
        (static_cast<uint32_t>(arg2) << 24);
    return Action{bits};
}

[[nodiscard]] constexpr ActionVerb action_verb(Action a) noexcept {
    return static_cast<ActionVerb>(static_cast<uint8_t>(a.bits & 0xFFu));
}

[[nodiscard]] constexpr uint8_t action_arg0(Action a) noexcept {
    return static_cast<uint8_t>((a.bits >> 8) & 0xFFu);
}

[[nodiscard]] constexpr uint8_t action_arg1(Action a) noexcept {
    return static_cast<uint8_t>((a.bits >> 16) & 0xFFu);
}

[[nodiscard]] constexpr uint8_t action_arg2(Action a) noexcept {
    return static_cast<uint8_t>((a.bits >> 24) & 0xFFu);
}

}  // namespace sts::engine
