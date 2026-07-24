#pragma once

// Potion registry + USE mechanics (design doc §5.4; task B3.23). Two pieces:
//
//   1. Re-export of the GENERATED potion table (registry/potions.yaml ->
//      tools/registry_gen/gen.py -> <build>/generated/sts/registry/
//      potion_table.hpp). As with cards.hpp/powers.hpp there is NO hand-written
//      potion table -- the YAML is the single source of truth (per-entry
//      provenance + potency + rarity + USE program live there), and the
//      generated header re-pins every id via ids.hpp (append-only, §4.4).
//
//   2. The combat-side USE verb: use_potion() queues a potion's USE effect
//      program onto the action queue (exactly how a card's use() queues its
//      effects, card_play.cpp), or routes a `native` potion to the escape hatch
//      (dispatch_native_potion, potions.cpp). Effects resolve later through the
//      normal pump priority order.
//
// LAYER BOUNDARY (B4.3 seam). Potion-SLOT storage -- the held-potion inventory
// and the A11 "one fewer slot" count -- is RunState, and that FIELD lands in
// B4.3 (RunState population, schema v2). This module deliberately does NOT touch
// RunState or CombatState layout: use_potion() takes the PotionId directly (the
// run layer looks it up in the slot inventory and hands it here), and the slot
// COUNT is exposed as the pure function potion_slot_count() below so B4.3 can
// store what this computes without this task depending on the field existing.
// The run-level USE_POTION/discard action wiring is B4.4 (advance/legal_actions,
// "USE_POTION both layers"); discarding a potion has no combat effect (it is a
// pure run-state slot removal), so there is no combat-layer discard function --
// use_potion is the only combat verb.
//
// Provenance (read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * AbstractPotion.use / getPotency + each potion class -- cited per row in
//     registry/potions.yaml.
//   * AbstractPlayer.<init> potionSlots, -1 at A11 (AbstractPlayer.java:211-213).
//   * AbstractDungeon.returnRandomPotion (AbstractDungeon.java:829-850) -- the
//     65/25/10 tier roll + the trap-14 rejection-sampling identity roll.

#include <cstdint>

#include "sts/engine/cards.hpp"           // CardEffectStep (reused by USE programs)
#include "sts/engine/combat_state.hpp"    // CombatState
#include "sts/engine/rng_stream.hpp"      // RngStream (potionRng)
#include "sts/engine/types.hpp"           // PotionId
#include "sts/registry/potion_table.hpp"  // generated: the potion table (build tree)

namespace sts::engine {

// --- Re-exported table types (generated) -------------------------------------

// PotionId is generated (ids.hpp) but, unlike CardId/PowerId/MonsterId, is not
// aliased by types.hpp (nothing in the skeleton referenced it). Potions are now
// a first-class engine concept, so bring it into sts::engine here -- the natural
// home, mirroring how cards.hpp/powers.hpp own their domain's surface.
using PotionId = sts::registry::PotionId;

using PotionRarity = sts::registry::PotionRarity;  // COMMON / UNCOMMON / RARE
using PotionDef = sts::registry::PotionDef;

using sts::registry::kMaxPotionSteps;

// Lookup by PotionId. Returns nullptr for PotionId::NONE or any id without a
// registry row (defensive).
using sts::registry::potion_def;

// --- Potion-slot count (A11 seam; design §5.4, AbstractPlayer.java:211-213) ---
// Base 3 potion slots, minus one at ascension >= 11. Pure -- the RunState field
// that STORES this lands in B4.3; this is the derivation both this task's test
// and B4.3 use. At the S1 A20 bracket this returns 2.
[[nodiscard]] constexpr int potion_slot_count(int ascension_level) noexcept {
    return ascension_level >= 11 ? 2 : 3;
}

// --- The combat USE verb (design §5.4) ---------------------------------------
// Queue potion `id`'s USE effect program onto the action queue (add_to_bottom),
// substituting `target` for CARD_TARGET steps (the used-on monster; ignored for
// self-target potions). A `native` potion is routed to dispatch_native_potion
// instead. Effects are QUEUED, not applied inline (matching AbstractPotion.use's
// addToBot) -- they resolve through the pump. Returns false (queues nothing) if
// `id` has no registry row.
bool use_potion(CombatState& state, PotionId id, uint8_t target) noexcept;

// The native USE escape hatch (B3.2 convention). Handles potions whose effect
// the opcode set cannot express. Implemented today: BLOOD_POTION (percent heal).
// Every other native potion's body is DEFERRED to its dependency (a potion-
// granted power in powers.yaml, an in-combat CHOOSE verb, recursive play, a
// run-layer mutation, or the combat-escape path) and is currently a documented
// no-op -- see potions.cpp and the B3.23 Log. Exposed for the tier-2 test.
void dispatch_native_potion(CombatState& state, PotionId id, int potency,
                            uint8_t target) noexcept;

// --- Identity roll (trap 14; AbstractDungeon.java:829-850) --------------------
// Model AbstractDungeon.returnRandomPotion(false): roll potionRng.random(0,99),
// pick the tier (COMMON < 65 <= UNCOMMON < 90 <= RARE), then rejection-sample
// getRandomPotion() (one potionRng.random(poolSize-1) draw each) until the
// rolled potion's rarity matches the tier. Consumes a VARIABLE number of draws
// (`rng.counter` advances by exactly that many); returns the chosen PotionId.
// The Ironclad pool is PotionId 1..33 in pool order (potions.yaml id ordering),
// so the drawn index i maps to PotionId(i+1).
// `limited=true` is Entropic Brew's overload: its Java loop deliberately
// discards the first candidate and rejects Fruit Juice thereafter in addition
// to the rarity check (AbstractDungeon.java:829-850).
[[nodiscard]] PotionId return_random_potion(RngStream& potion_rng,
                                            bool limited = false) noexcept;

// The tier a d100 roll (0..99) selects, via the 65/25/10 gate
// (PotionHelper.POTION_COMMON_CHANCE / POTION_UNCOMMON_CHANCE). Exposed so the
// identity-roll test can assert the gate boundaries directly.
[[nodiscard]] constexpr PotionRarity potion_tier_for_roll(int roll) noexcept {
    if (roll < 65) {  // POTION_COMMON_CHANCE
        return PotionRarity::COMMON;
    }
    if (roll < 90) {  // + POTION_UNCOMMON_CHANCE (25)
        return PotionRarity::UNCOMMON;
    }
    return PotionRarity::RARE;
}

// Size of the Ironclad potion pool (PotionHelper.getPotions(IRONCLAD,false)):
// the 33 registry rows. getRandomPotion draws random(kPotionPoolSize - 1).
inline constexpr int kPotionPoolSize = 33;

}  // namespace sts::engine
