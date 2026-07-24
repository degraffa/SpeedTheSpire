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
};

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
