#pragma once

// Core id enums and small POD instance/action types shared by the state
// structs (design doc §4.2) and the effect interpreter (design doc §6, §7).
//
// The `CardId`/`PowerId`/`MonsterId`/`RelicId` enums are GENERATED from the
// `registry/` YAML by tools/registry_gen/gen.py (Stage B design §4.3) and
// re-exported into sts::engine here via using-aliases, so every existing
// consumer keeps naming `sts::engine::CardId` etc. unchanged. The generated
// header re-pins every id with a `static_assert` (Stage B design §4.4: ids
// are append-only and load-bearing on disk -- fixtures/traces store the raw
// u16 values). `Action`/`ActionVerb` and the POD instance/slot types below
// stay hand-written: they are engine API, not registry content.
//
// All types here are fixed-width (<cstdint>), trivially copyable, pointer-
// free, and header-only, matching the conventions established by
// rng_stream.hpp. Namespace sts::engine, per the existing headers.

#include <cstdint>
#include <type_traits>

#include "sts/registry/ids.hpp"  // generated: the id enums (build tree)

namespace sts::engine {

// --- Id enums (generated, re-exported) ---------------------------------------
//
// Convention (frozen, enforced by the generator): every id enum reserves 0 as
// `NONE`, a sentinel meaning "no id" / "empty slot" -- e.g. an unused
// card-instance-pool row or a null cardQueue entry distinct from the end-turn
// sentinel (design doc §5.1, which is a separate flag/opcode, not a CardId
// value). Real ids start at 1 so that a value-initialized (zeroed) struct is
// trivially "empty" without needing an extra `bool` flag -- consistent with
// design doc §4.1's value-initialization/hash-stability principle.
//
// PotionId/EventId are also generated but not aliased here: nothing in the
// engine consumes them yet (they are translator-facing until the run layer
// lands); reach them as sts::registry::PotionId/EventId.

// Card set (registry/cards.yaml). u16 per design doc §4.2's card-instance-pool
// row (`card_id: u16`).
using CardId = sts::registry::CardId;

// Power set (registry/powers.yaml). u16 per design doc §4.2's power-slot row.
using PowerId = sts::registry::PowerId;

// Monster set (registry/monsters.yaml). `NONE` doubles as "no monster" for an
// empty monster slot; the player is never represented as a MonsterId (player
// state lives in CombatState's dedicated player fields per design doc §4.2,
// not the monster array).
using MonsterId = sts::registry::MonsterId;

// Relic set (registry/relics.yaml) -- currently `NONE`-only (the skeleton has
// zero relics, design doc §9); rows land with Stage B's relic content tasks.
using RelicId = sts::registry::RelicId;

// --- CardFlag (per-card / per-instance flag bits) ----------------------------

// Named bits for `CardInstance.flags` and the generated `CardDef.flags`
// Both fields are raw `uint16_t`; these constants give the bit
// positions meaning. Values are APPEND-ONLY (design doc §4.4 opcode/id rule
// applied to flags) and MIRRORED in tools/registry_gen/gen.py's `CARD_FLAGS`;
// the generated header emits matching `kCardFlag*` constants that cards.hpp
// static_asserts equal to these, so the codegen vocabulary cannot silently
// diverge from the engine's.
//
// Semantics. EXHAUST routing and UNPLAYABLE/XCOST at play/legality are wired;
// ETHEREAL/INNATE/RETAIN are named reserved bits whose end-of-turn /
// combat-begin sweeps land with their first content consumer per §5.4's frozen
// ordering):
//   EXHAUST    -- on play the card goes to the exhaust pile, not discard
//                 (AbstractCard.exhaust; UseCardAction.java).
//   ETHEREAL   -- auto-exhaust at end of turn if still in hand
//                 (AbstractCard.isEthereal, AbstractCard.java:110/2177).
//   INNATE     -- starts in the opening hand (AbstractCard.isInnate:86).
//   UNPLAYABLE -- never a legal play; status/curse clutter with cost < -1
//                 (AbstractCard.cantUseMessage / canUse, AbstractCard.java:917,920).
//   RETAIN     -- not discarded at end of turn (AbstractCard.selfRetain/retain:81-82).
//   XCOST      -- cost is ALL current energy at play; the effect program is
//                 repeated `energyOnUse` times and energy is zeroed
//                 (Whirlwind/WhirlwindAction: cost -1, energyOnUse). Encoded as
//                 a flag rather than a negative `base_cost` (which is unsigned);
//                 the generator maps YAML `cost: -1` to this bit.
//   COST_MODIFIED_FOR_TURN -- (per-INSTANCE runtime bit, never authored
//                 in YAML) this instance's cost_now is a this-turn-only value
//                 (AbstractCard.setCostForTurn / isCostModifiedForTurn). Set by
//                 Infernal Blade's generated attack; cleared -- with cost_now
//                 restored to the registry cost -- by the end-turn sweep
//                 (AbstractRoom.endTurn:397-405 resets costForTurn = cost on
//                 every draw/discard/hand card) and on exhaust
//                 (ExhaustCardEffect.update:41-43 resetAttributes).
//   PURGE_ON_USE -- (per-INSTANCE runtime bit, never authored in YAML)
//                 AbstractCard.purgeOnUse: the instance is DESTROYED when it
//                 resolves. UseCardAction.update tests it FIRST and returns
//                 before the discard / exhaust branches (UseCardAction.java:
//                 89-94), so the card lands in no pile at all. Set on the
//                 temporary limbo copy a replay power creates
//                 (DoubleTapPower.onUseCard, DoubleTapPower.java:59) and re-read
//                 by that same power's guard (`!card.purgeOnUse`, :44), which is
//                 what stops a replayed copy from being replayed again.
//   EXHAUST_ON_USE_ONCE -- (per-INSTANCE runtime bit, never authored in YAML)
//                 AbstractCard.exhaustOnUseOnce. PlayTopCardAction (Havoc)
//                 sets it before moving the draw-top card into limbo; the
//                 UseCardAction snapshots it into exhaustCard and clears it
//                 after the discard/exhaust decision (:132). Corruption's
//                 action-local exhaust redirect uses the same one-play
//                 lifetime; unlike EXHAUST, this bit must not survive a
//                 Strange Spoon save or an Exhume/replay cycle.
//   AUTOPLAY_X_ENERGY -- transient marker on a fresh PURGE_ON_USE replay copy.
//                 Its misc word holds the original X card's energyOnUse because
//                 the original spends energy before the copy dequeues. Draw-top
//                 autoplay never needs storage (it is front-queued before any
//                 intervening spend). The marked instance necessarily poofs.
//   SAVED_BASE_COST -- transient companion to COST_MODIFIED_FOR_TURN. When a
//                 permanent combat cost writer (Confusion / Enlightenment+)
//                 has moved AbstractCard.cost away from the registry row and a
//                 later effect changes costForTurn only, bits 11..13 preserve
//                 that combat base (all S1 card costs fit in three bits) so
//                 resetAttributes restores the right value. Bits 14..15 remain
//                 available to later card mechanics.
//                 The encoding is private engine state, never authorable YAML.
//   FREE_TO_PLAY_ONCE -- (per-INSTANCE runtime bit, never authored in YAML)
//                 AbstractCard.freeToPlayOnce. The card costs NO energy for its
//                 next play and is playable regardless of the energy on hand:
//                 AbstractCard.hasEnoughEnergy admits it (`|| this.freeToPlay()`,
//                 AbstractCard.java:888) and AbstractPlayer.useCard's spend is
//                 suppressed by the same predicate (:1378). It is a ONE-PLAY
//                 lifetime exactly like EXHAUST_ON_USE_ONCE above and clears at
//                 the same seam -- UseCardAction.update:87, before the pile
//                 decision, so a Strange Spoon save cannot carry it forward.
//                 Confusion's redraw also clears it (ConfusionPower.java:46) and
//                 so does a targeted exhaust (ExhaustSpecificCardAction.java:40).
//                 Granted by ForethoughtAction to each moved hand card whose
//                 AbstractCard.cost is > 0 (ForethoughtAction.java:39-41,57-59);
//                 a 0-cost card is left alone, which is why the grant reads the
//                 reconstructed combat BASE cost, not cost_now.
enum class CardFlag : uint16_t {
    NONE       = 0,
    EXHAUST    = 1u << 0,
    ETHEREAL   = 1u << 1,
    INNATE     = 1u << 2,
    UNPLAYABLE = 1u << 3,
    RETAIN     = 1u << 4,
    XCOST      = 1u << 5,
    COST_MODIFIED_FOR_TURN = 1u << 6,
    PURGE_ON_USE = 1u << 7,
    EXHAUST_ON_USE_ONCE = 1u << 8,
    AUTOPLAY_X_ENERGY = 1u << 9,
    SAVED_BASE_COST = 1u << 10,
    // 11..13 are SAVED_BASE_COST's three-bit payload (kSavedBaseCostMask below).
    FREE_TO_PLAY_ONCE = 1u << 14,
};

inline constexpr uint16_t kSavedBaseCostShift = 11u;
inline constexpr uint16_t kSavedBaseCostMask = 0x3800u;

[[nodiscard]] constexpr uint16_t card_flag_bit(CardFlag f) noexcept {
    return static_cast<uint16_t>(f);
}
[[nodiscard]] constexpr bool has_card_flag(uint16_t flags, CardFlag f) noexcept {
    return (flags & card_flag_bit(f)) != 0u;
}
[[nodiscard]] constexpr uint8_t saved_base_cost(uint16_t flags) noexcept {
    return static_cast<uint8_t>((flags & kSavedBaseCostMask) >>
                                kSavedBaseCostShift);
}

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
    // The hand-select screen's CONFIRM button (CardSelectConfirmButton), the one
    // verb an OPTIONAL selection needs and no mandatory one does. A mandatory
    // choice ends when its count is satisfied, so the button is never the thing
    // that ends it; a zero-to-N screen (anyNumber + canPickZero) has its button
    // enabled from the moment it opens and an EMPTY confirm is a legal, and
    // often the only sensible, answer (HandCardSelectScreen.update:88-101,
    // .open:495-501 -- canPickZero enables the button up front). CHOOSE then
    // TOGGLES a card in or out of the selection instead of committing it, and
    // this verb is what resolves whatever accumulated. No args.
    CONFIRM = 4,
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
//
// `counter` is the SECOND per-instance number a power may carry. Several real
// AbstractPower subclasses hold a private field beside `amount`, and the two are
// independent: PanachePower keeps `amount` = the 5-card countdown and a private
// `damage` that stackPower alone grows (PanachePower.java:28,34-35,46-50), and
// TheBombPower keeps `amount` = the fuse in turns and a private `damage` fixed at
// construction (TheBombPower.java:26,29-38). Both numbers are observable -- the
// oracle emits `amount` and, by reflection over the field name, `damage`
// (GameStateConverter.convertCreaturePowersToJson:895-903) -- so the second one
// belongs in the serialized slot, not in a side table.
//
// CONVENTION: `amount` stays the ORACLE-VISIBLE stack number for every power, so
// every existing power keeps `counter == 0` with unchanged semantics and every
// amount-based read (damage hooks, Artifact charges, reduce/remove) is untouched.
// A power that uses `counter` says what it means in its registry row.
//
// The `counter`-carrying powers are the ONLY consumers; Combust's private hpLoss
// deliberately stays where it is (CombatState.flags, combat_state.hpp) -- those
// bits are load-bearing on committed behaviour and moving them is out of scope.
//
// A SECOND MEANING, for the three DURATION debuffs. Vulnerable (2), Weak (3) and
// Frail (21) use `counter` as their `justApplied` latch (0 / 1) rather than as a
// number: the Java keeps a private boolean beside `amount` in each class
// (VulnerablePower.java:25, WeakPower.java:25, FrailPower.java:22) that makes the
// round a debuff arrived cost it no duration. It is per-INSTANCE for the same
// reason Panache's damage is -- the player and every monster can hold one at
// once, which is precisely what the retired single CombatState.flags bit could
// not express. It is also always 0 at a WAITING_ON_USER boundary: set during the
// monster phase, consumed by dispatch_at_end_of_round in the same pump. So it
// never reaches a snapshot, a state hash, a committed fixture or an oracle diff.
// See src/engine/powers/power_duration_debuff.hpp.
struct PowerSlot {
    uint16_t power_id;  // PowerId; NONE marks an empty slot.
    int16_t amount;
    int16_t counter;    // second per-instance number (0 for every power that
                        // does not declare a meaning for it)
    int16_t pad0;       // explicit padding: keeps the row a deliberate 8 bytes
                        // (a power-of-two stride for the two 24-slot arrays)
                        // rather than an incidental 6, and is value-init zeroed
                        // so byte-hashing stays padding-stable (design §4.1).
};

static_assert(std::is_trivially_copyable_v<PowerSlot>,
              "PowerSlot must be trivially copyable (design doc §4.1: "
              "snapshot = memcpy)");
static_assert(sizeof(PowerSlot) == 8,
              "PowerSlot must be exactly 8 bytes (u16+i16+i16+i16; was u16+i16 "
              "== 4 before the schema-6 counter widening)");

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
