// Boss chest construction + the pure predicates its screens are masked with.
// See boss_chest.hpp for the provenance block; the room flow itself (entry,
// open, pick, skip, proceed) lives in run_advance.cpp beside the other phases.

#include "sts/engine/boss_chest.hpp"

#include "sts/engine/relic_pools.hpp"

namespace sts::engine {

BossChestState roll_boss_chest(RunState& rs) noexcept {
    // BossChest.<init> (BossChest.java:35-39):
    //     this.relics.clear();
    //     for (int i = 0; i < 3; ++i)
    //         this.relics.add(returnRandomRelic(RelicTier.BOSS));
    //
    // The context is rebuilt per pop rather than hoisted, because a pop can
    // CHANGE it: picking is later, but Black Blood's gate reads the owned relic
    // list and Ectoplasm's reads rs.act, and rebuilding is what makes this loop
    // read like the Java's three independent calls. It is also free -- both
    // fills are small scans.
    BossChestState out{};
    for (int i = 0; i < kBossChestOfferCount; ++i) {
        RelicSpawnContext ctx{};
        ctx.floor = rs.floor;
        fill_boss_spawn_gates(rs, ctx);
        out.relics[i] = static_cast<uint16_t>(
            return_random_relic_key(rs, RelicTier::BOSS, ctx));
    }
    out.screen = static_cast<uint8_t>(BossChestScreen::CLOSED);
    return out;
}

bool boss_chest_open_legal(const BossChestState& chest) noexcept {
    // A live chest always has three offers -- the constructor loop is
    // unconditional and the empty-pool arm still yields a Circlet -- so a zero
    // in slot 0 means "no chest here", not "an empty chest".
    return chest.relics[0] != static_cast<uint16_t>(RelicId::NONE) &&
           static_cast<BossChestScreen>(chest.screen) ==
               BossChestScreen::CLOSED;
}

bool boss_chest_pick_legal(const RunState& rs, const BossChestState& chest,
                           uint8_t index) noexcept {
    if (static_cast<BossChestScreen>(chest.screen) !=
        BossChestScreen::RELIC_SELECT) {
        return false;
    }
    if (index >= kBossChestOfferCount ||
        chest.relics[index] == static_cast<uint16_t>(RelicId::NONE)) {
        return false;
    }
    // The four starter-swap relics REPLACE relics[0] rather than appending
    // (AbstractRelic.bossObtainLogic :391-398 -> instantObtain(player, 0, true),
    // :230-234), so they cost no slot -- but gating on the identity would put an
    // unopened chest's contents into the mask, so the plain capacity check is
    // used for every offer. See the header for why that is sound.
    return rs.relic_count < kRelicCap;
}

}  // namespace sts::engine
