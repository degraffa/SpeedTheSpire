// Monster init/turn dispatch + spawn (B3.12). See monster_dispatch.hpp for the
// scope/rationale. The dispatch tables are a plain switch on MonsterId (data-
// oriented, no virtual dispatch); each monster batch (B3.13+) adds its case.

#include "sts/engine/monster_dispatch.hpp"

#include <cassert>
#include <cstddef>

#include "sts/engine/monster_jaw_worm.hpp"  // jaw_worm_init / jaw_worm_take_turn

namespace sts::engine {

MonsterInitFn monster_init_fn(MonsterId id) noexcept {
    switch (id) {
        case MonsterId::JAW_WORM:
            return &jaw_worm_init;
        default:
            return nullptr;  // not yet implemented (B3.13-B3.22)
    }
}

MonsterTurnFn monster_turn_fn(MonsterId id) noexcept {
    switch (id) {
        case MonsterId::JAW_WORM:
            return &jaw_worm_take_turn;
        default:
            return &default_monster_turn;  // no-op until the monster's batch lands
    }
}

void dispatch_monster_turn(CombatState& state, uint8_t monster_index) noexcept {
    const MonsterId id =
        static_cast<MonsterId>(state.monsters[monster_index].monster_id);
    const MonsterTurnFn fn = monster_turn_fn(id);
    fn(state, monster_index);
}

void spawn_group(CombatState& state, std::span<const MonsterId> group) noexcept {
    assert(group.size() <= static_cast<std::size_t>(kMonsterCap) &&
           "spawn_group: group exceeds kMonsterCap");
    state.monster_count = static_cast<uint8_t>(group.size());
    for (uint8_t i = 0; i < group.size(); ++i) {
        const MonsterInitFn init = monster_init_fn(group[i]);
        assert(init != nullptr &&
               "spawn_group: monster not yet implemented (lands in B3.13-B3.22)");
        init(state, i);
    }
}

}  // namespace sts::engine
