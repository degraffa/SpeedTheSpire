#pragma once

// Relic dungeon pools + RunState acquisition (Stage B B4.6).
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
    // B3.25 deck-content canSpawn gates (the Bottled trio). These carry the
    // MASTER-DECK facts the Java gates read; fill_deck_spawn_gates computes
    // them from a RunState. Defaults (false) match the Ironclad starting deck
    // (all-BASIC, no POWER cards) -- and the Bottled canSpawn checks apply even
    // in Endless (no Settings.isEndless clause in those canSpawn bodies).
    bool deck_has_nonbasic_attack = false;  // BottledFlame.canSpawn (:93-99)
    bool deck_has_nonbasic_skill = false;   // BottledLightning.canSpawn (:93-99)
    bool deck_has_power = false;            // BottledTornado.canSpawn (:93-95)
};

// Fill the deck-content gates above from the run's master deck. "Non-basic" ==
// CardRarity != BASIC; the only BASIC red rows are Strike/Defend/Bash
// (Strike_Red/Defend_Red/Bash constructors -- CardRarity.BASIC), so the scan
// keys on type + those three ids. deck_has_power stays false until B3.7 lands
// POWER-type cards (no POWER CardType exists in the registry yet).
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

[[nodiscard]] RelicAcquireResult acquire_relic(
    RunState& rs, RngStream& misc_rng, RelicId id) noexcept;

}  // namespace sts::engine
