#pragma once

// Potion registry + USE mechanics (design doc §5.4). Two pieces:
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
// LAYER BOUNDARY. Potion-SLOT storage -- the held-potion inventory
// and the A11 "one fewer slot" count -- lives in RunState, not here. This module
// deliberately does NOT touch
// RunState or CombatState layout: use_potion() takes the PotionId directly (the
// run layer looks it up in the slot inventory and hands it here), and the slot
// COUNT is exposed as the pure function potion_slot_count() below, so the run
// layer stores what this computes.
// The run-level USE_POTION/discard action wiring lives in run_advance.cpp;
// discarding a potion has no combat effect (it is a
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
// that STORES this is RunState.potion_slots; this is the derivation it and its
// tests use. At the S1 A20 bracket this returns 2.
[[nodiscard]] constexpr int potion_slot_count(int ascension_level) noexcept {
    return ascension_level >= 11 ? 2 : 3;
}

// --- The combat USE verb (design §5.4) ---------------------------------------
// Queue potion `id`'s USE effect program onto the action queue (add_to_bottom),
// substituting `target` for CARD_TARGET steps (the used-on monster; ignored for
// self-target potions). A `native` potion is routed to dispatch_native_potion
// instead. Effects are QUEUED, not applied inline (matching AbstractPotion.use's
// addToBot) -- they resolve through the pump.
//
// Returns FALSE, queuing and mutating nothing, when `id` has no registry row OR
// when potion_use_implemented(id) is false. The second case is the important
// one: a deferred potion must FAIL, not quietly do nothing, because the caller
// (run_advance's step_potion) treats a false return as "the use did not happen"
// and therefore does NOT consume the slot. Silently swallowing the potion would
// be a wrong answer, not a missing feature.
bool use_potion(CombatState& state, PotionId id, uint8_t target) noexcept;

// --- Implemented-ness gate (the potion legality trap) -------------------------
//
// Is this potion's USE actually implemented ANYWHERE in the engine -- as a data
// effect program, as a combat native body (dispatch_native_potion), or as a
// run-layer native body (run_advance.cpp's step_potion)?
//
// WHY THIS EXISTS. Held potions do not only come from the engine's own drops:
// the oracle translator fills RunState.potions[] straight from real captures
// (tools/oracle_bridge/translator/src/translate.cpp, parse_potions) for ANY
// potion with a registry row. The legality gate used to ask only "is there a
// registry row, and is the phase right", so an imported state holding a
// still-deferred potion would have offered USE_POTION as a legal action and
// then consumed the slot for no effect. That is a WRONG ANSWER. Only the
// missing `replay` generalisation keeps it unreachable today.
//
// SELF-HEALING. The answer is derived from the registry wherever it can be: a
// non-`native` row is a data program and always runs, so the moment a deferred
// potion is un-deferred in registry/potions.yaml (drop `native: true`, add
// `effects:`) it becomes legal with no code change here. Only `native` rows --
// which by definition mean hand-written code -- need naming, and that list
// lives next to the switch that implements them (potions.cpp) so it cannot
// drift away from the bodies it describes.
[[nodiscard]] bool potion_use_implemented(PotionId id) noexcept;

// The native USE escape hatch (the same convention as the power/relic natives).
// Handles potions whose effect the opcode set cannot express, or which the
// generator's potion domain cannot author. Implemented here today:
// BLOOD_POTION (percent heal), BLESSING_OF_THE_FORGE (the Armaments+
// CHOOSE_CARD item), and SMOKE_BOMB (the combat-state player-escape latch).
// FRUIT_JUICE and ENTROPIC_BREW are implemented at the RUN layer instead
// (run_advance.cpp intercepts them before use_potion), so a direct combat-layer
// call for those two is a documented no-op. Every remaining native potion's
// body is DEFERRED to its dependency (an in-combat CHOOSE verb, recursive play,
// the out-of-combat revive) -- and use_potion now REFUSES those rather than
// no-op'ing, see potion_use_implemented. Exposed for the tier-2 test.
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

// PotionHelper.getRandomPotion() (PotionHelper.java:169-172) -- the UNGATED
// draw underneath the identity roll above: `potions.get(potionRng.random(size -
// 1))`, exactly ONE draw, uniform over the whole 33-entry class pool, with no
// tier gate and none of trap 14's rejection sampling. A caller that wants the
// 65/25/10 distribution wants return_random_potion; a caller that reaches for
// PotionHelper directly -- Neow's three-potion blessing does
// (NeowReward.java:270-272) -- wants this. Exposed rather than left file-local
// so both spellings share one pool-index -> PotionId mapping.
[[nodiscard]] PotionId get_random_potion(RngStream& potion_rng) noexcept;

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
