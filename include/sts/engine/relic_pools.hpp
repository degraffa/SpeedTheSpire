#pragma once

// Relic dungeon pools + RunState acquisition.
//
// Provenance:
//   AbstractDungeon.initializeRelicList (AbstractDungeon.java:1221-1256):
//     populate five tier pools, then one relicRng.randomLong() -> JDK shuffle
//     per tier, unconditionally and in Common/Uncommon/Rare/Shop/Boss order.
//   AbstractDungeon.returnRandomRelicKey/returnEndRandomRelicKey
//     (AbstractDungeon.java:704-819): front/end removal, empty-tier fallback,
//     canSpawn recheck, and the 50/33/17 reward-tier roll.
//   AbstractRelic.instantObtain/obtain (AbstractRelic.java:219-291): append in
//     acquisition order before onEquip; ordinary duplicate ids remain distinct;
//     Circlet duplicates increment the existing Circlet instead.

#include <cstdint>

#include "sts/engine/relics.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_state.hpp"

namespace sts::engine {

enum class RelicPool : uint8_t {
    COMMON = 0,
    UNCOMMON = 1,
    RARE = 2,
    SHOP = 3,
    BOSS = 4,
};

struct RelicSpawnContext {
    uint16_t floor = 0;
    bool in_shop = false;
    bool endless = false;
    // Deck-content canSpawn gates (the Bottled trio). These carry the
    // MASTER-DECK facts the Java gates read; fill_deck_spawn_gates computes
    // them from a RunState. Defaults (false) match the Ironclad starting deck
    // (all-BASIC, no POWER cards) -- and the Bottled canSpawn checks apply even
    // in Endless (no Settings.isEndless clause in those canSpawn bodies).
    bool deck_has_nonbasic_attack = false;  // BottledFlame.canSpawn (:93-99)
    bool deck_has_nonbasic_skill = false;   // BottledLightning.canSpawn (:93-99)
    // BottledTornado.canSpawn (:93-95) -> CardHelper.hasCardType(POWER)
    // (CardHelper.java:80-86): any POWER-type master-deck card, ANY rarity.
    bool deck_has_power = false;
    // Girya / PeacePipe / Shovel canSpawn (Girya.java:538-549 and the two
    // byte-identical siblings): the number of OWNED relics that are one of those
    // three -- each spawns only while that count is < 2. Filled by
    // fill_campfire_relic_count; a draw that does not fill it sees 0, which is
    // the fresh-run answer.
    uint8_t campfire_relic_count = 0;
    // Ectoplasm.canSpawn (Ectoplasm.java:50-53): `actNum <= 1` -- the only
    // act-gated canSpawn in the S1 relic set. Default 1 because S1 IS Act 1
    // (design §6); RunState.act is the producer when a caller has one.
    uint8_t act = 1;
    // BlackBlood.canSpawn (BlackBlood.java:33-36): hasRelic("Burning Blood").
    // Default TRUE, not false -- the Ironclad ALWAYS starts with Burning Blood
    // (Ironclad.getStartingRelics, Ironclad.java:113-115), so `true` is the
    // fresh-run answer in exactly the way `0` is for campfire_relic_count.
    // Filled by fill_boss_spawn_gates.
    bool has_burning_blood = true;
};

// Count the owned Girya / Peace Pipe / Shovel relics into `ctx`. It lives beside
// fill_deck_spawn_gates rather than inside the three canSpawn bodies because
// canSpawn is a PURE predicate over the context (relics/relic_pickup.hpp) -- the
// bodies must not each re-scan RunState.
void fill_campfire_relic_count(const RunState& rs,
                               RelicSpawnContext& ctx) noexcept;

// Fill the BOSS-tier gates from the run: `act` from RunState.act and
// `has_burning_blood` from the owned relic list (BlackBlood.canSpawn). Separate
// from fill_campfire_relic_count for the same reason that one is separate from
// fill_deck_spawn_gates -- canSpawn bodies are PURE predicates over the context
// and must not re-scan RunState themselves.
void fill_boss_spawn_gates(const RunState& rs, RelicSpawnContext& ctx) noexcept;

// Fill the deck-content gates above from the run's master deck. "Non-basic" ==
// CardRarity != BASIC; the only BASIC red rows are Strike/Defend/Bash
// (Strike_Red/Defend_Red/Bash constructors -- CardRarity.BASIC), so the scan
// keys on type + those three ids. deck_has_power is NOT rarity-filtered:
// CardHelper.hasCardType (CardHelper.java:80-86) is a plain type scan, so any
// POWER-type card in the master deck sets it. The registry has 8 POWER cards
// (Combust/Dark Embrace/Evolve/Feel No Pain/Fire Breathing/Inflame/
// Metallicize/Rupture), so the Bottled Tornado gate can open.
void fill_deck_spawn_gates(const RunState& rs, RelicSpawnContext& ctx) noexcept;

enum class RelicAcquireResult : uint8_t {
    ACQUIRED = 0,
    CIRCLET_STACKED = 1,
    INVALID_ID = 2,
    RELIC_CAP_REACHED = 3,
    COUNTER_CAP_REACHED = 4,
};

void initialize_relic_pools(RunState& rs) noexcept;

[[nodiscard]] constexpr RelicTier relic_tier_for_roll(int32_t roll) noexcept {
    return roll < 50 ? RelicTier::COMMON
                     : (roll < 83 ? RelicTier::UNCOMMON : RelicTier::RARE);
}
[[nodiscard]] RelicTier return_random_relic_tier(RunState& rs) noexcept;

[[nodiscard]] RelicId return_random_relic_key(
    RunState& rs, RelicTier tier, RelicSpawnContext ctx = {}) noexcept;
[[nodiscard]] RelicId return_end_random_relic_key(
    RunState& rs, RelicTier tier, RelicSpawnContext ctx = {}) noexcept;

[[nodiscard]] bool relic_can_spawn(RelicId id,
                                   RelicSpawnContext ctx) noexcept;

// Whether acquire_relic can complete without hitting an invalid-id, fixed-slot,
// or Circlet-counter capacity guard. A Circlet can still stack while the relic
// array itself is full, provided its existing counter has room.
[[nodiscard]] bool relic_acquire_legal(const RunState& rs,
                                       RelicId id) noexcept;

[[nodiscard]] RelicAcquireResult acquire_relic(
    RunState& rs, RngStream& misc_rng, RelicId id) noexcept;

// AbstractPlayer.loseRelic (AbstractPlayer.java:2014-2031): run onUnequip on
// EVERY owned copy of `id`, then remove the LAST one (the loop overwrites
// `toRemove` rather than breaking) and reorganize, which for a plain ArrayList
// removal is order-preserving for the survivors -- so acquisition order, and
// hence trigger order (trap 8), is preserved with one entry deleted. Returns
// false and changes nothing when the run does not own `id`.
//
// NO S1 RELIC HAS AN onUnequip BODY, so the per-copy pass is named rather than
// written: the only S1 caller is Neow's boss-relic swap, which removes
// relics[0] -- the Ironclad's Burning Blood, whose only override is onVictory
// (BurningBlood.java:30). A relic row that later needs onUnequip adds its
// dispatch here, beside acquire_relic's onEquip fan-out.
[[nodiscard]] bool lose_relic(RunState& rs, RelicId id) noexcept;

}  // namespace sts::engine
