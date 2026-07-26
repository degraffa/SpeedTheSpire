#pragma once

// Act-1 treasure-room chest construction and opening.
//
// A chest is constructed on TreasureRoom.onPlayerEntry, before the player
// chooses whether to open it. Construction consumes exactly two treasureRng
// wrapper calls: AbstractDungeon.getRandomChest's size roll, then
// AbstractChest.randomizeReward's ONE shared contents roll. Opening is a later
// action and is the only point that consumes the optional gold-amount draw and
// dispatches onChestOpen / onChestOpenAfter.
//
// Provenance (methods read in full from the decompiled game):
//   AbstractDungeon.getRandomChest             AbstractDungeon.java:499-508
//   AbstractChest.randomizeReward/open          AbstractChest.java:54-102
//   Small/Medium/LargeChest constructors        *Chest.java:18-22
//   Matryoshka.onChestOpen                      Matryoshka.java:30-47
//   CursedKey.onChestOpen                       CursedKey.java:52-59
//   NlothsMask.onChestOpenAfter                 NlothsMask.java:23-35
//   ShowCardAndObtainEffect constructor         ShowCardAndObtainEffect.java:30-45

#include <cstdint>
#include <type_traits>

#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/relics.hpp"
#include "sts/engine/run_state.hpp"

namespace sts::engine {

enum class ChestSize : uint8_t {
    NONE = 0,
    SMALL = 1,
    MEDIUM = 2,
    LARGE = 3,
};

// The chest's constructor-time result. This is transient screen/room state, not
// save-parity RunState storage. It stays POD so RunController remains a
// memcpy-safe batch entry and is included in the fuzz controller content hash.
struct TreasureChest {
    uint8_t size;       // ChestSize
    uint8_t relic_tier; // RelicTier
    uint8_t has_gold;   // bool encoded as 0/1
    uint8_t opened;     // bool encoded as 0/1
};

static_assert(std::is_trivially_copyable_v<TreasureChest>);
static_assert(sizeof(TreasureChest) == 4);

// Pure threshold table used after the two wrapper calls. Exposed so every one
// of the 100 contents rolls can be exhaustively pinned without searching seeds.
[[nodiscard]] constexpr TreasureChest treasure_chest_for_rolls(
    int32_t size_roll, int32_t contents_roll) noexcept {
    TreasureChest out{};
    if (size_roll < 50) {
        out.size = static_cast<uint8_t>(ChestSize::SMALL);
        out.has_gold = static_cast<uint8_t>(contents_roll < 50);
        out.relic_tier = static_cast<uint8_t>(
            contents_roll < 75 ? RelicTier::COMMON : RelicTier::UNCOMMON);
    } else if (size_roll < 83) {
        out.size = static_cast<uint8_t>(ChestSize::MEDIUM);
        out.has_gold = static_cast<uint8_t>(contents_roll < 35);
        out.relic_tier = static_cast<uint8_t>(
            contents_roll < 35
                ? RelicTier::COMMON
                : (contents_roll < 85 ? RelicTier::UNCOMMON : RelicTier::RARE));
    } else {
        out.size = static_cast<uint8_t>(ChestSize::LARGE);
        out.has_gold = static_cast<uint8_t>(contents_roll < 50);
        out.relic_tier = static_cast<uint8_t>(
            contents_roll < 75 ? RelicTier::UNCOMMON : RelicTier::RARE);
    }
    return out;
}

// TreasureRoom.onPlayerEntry -> getRandomChest -> chest constructor.
[[nodiscard]] TreasureChest roll_treasure_chest(RunState& rs) noexcept;

// The single authority for whether an ordinary chest can be opened. Besides
// requiring the constructor's exact descriptor domain -- the (size, tier)
// pairs derived from treasure_chest_for_rolls, so SMALL+RARE / LARGE+COMMON
// and every non-chest tier are rejected -- this preflights the fixed
// RewardScreen capacity for every active Matryoshka plus gold/base relic, and
// the master-deck slots consumed by acquisition-ordered Cursed Keys that
// Omamori (first match only, one charge per block) does not block.
// A false result guarantees that open_treasure_chest is a byte-stable no-op.
[[nodiscard]] bool treasure_chest_open_legal(
    const RunState& rs, const TreasureChest& chest) noexcept;

// Public hook seams keep the boss/non-boss gates independently testable even
// though this module owns only ordinary TreasureRoom chests. BossChest itself
// belongs to the post-boss/act-transition task. Both are transactions: false
// means malformed counts or insufficient reward capacity -- or, for the open
// hook, a Cursed Key curse that cannot join a full master deck -- and leaves
// both arguments byte-stable.
[[nodiscard]] bool dispatch_relics_on_chest_open(
    RunState& rs, RewardScreen& out, bool boss_chest) noexcept;
[[nodiscard]] bool dispatch_relics_on_chest_open_after(
    RunState& rs, RewardScreen& out, bool boss_chest) noexcept;

// AbstractChest.open(false), collapsed to its state-visible result. Reward
// relics are popped immediately but acquired only when claimed through the
// existing reward/acquisition door. The hook pass is acquisition ordered:
// Matryoshka and Cursed Key before gold/base relic; N'loth's Mask afterwards.
// Returns false without changing any argument when the descriptor/capacity
// preflight fails.
//
// No miscRng parameter: the gold roll is `treasureRng.random(GOLD_AMT*0.9f,
// GOLD_AMT*1.1f)` (AbstractChest.java:72) and nothing else on the open path --
// neither hook pass, nor `returnRandomCurse`, nor the pool front-pop -- reads
// miscRng. It is first touched later, at claim time, by `acquire_relic`'s
// onEquip bodies (combat_rewards.hpp).
[[nodiscard]] bool open_treasure_chest(
    RunState& rs, TreasureChest& chest, RewardScreen& out) noexcept;

}  // namespace sts::engine
