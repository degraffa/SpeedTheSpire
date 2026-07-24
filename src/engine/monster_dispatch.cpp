// Monster init/turn dispatch + spawn (B3.12; extended B3.13). See
// monster_dispatch.hpp for the scope/rationale. The dispatch tables are a plain
// switch on MonsterId (data-oriented, no virtual dispatch); each monster batch
// (B3.13+) adds its case.

#include "sts/engine/monster_dispatch.hpp"

#include <cassert>
#include <cstddef>

#include "sts/engine/action_queue.hpp"     // add_to_bottom, ActionQueueItem, kActorPlayer
#include "sts/engine/monster_cultist.hpp"  // cultist_init / cultist_take_turn (B3.13)
#include "sts/engine/monster_jaw_worm.hpp" // jaw_worm_init / jaw_worm_take_turn
#include "sts/engine/monster_louse.hpp"    // louse_* init / take_turn / pre_battle (B3.13)
#include "sts/engine/monster_slime.hpp"    // small/medium slime init + turns (B3.14)
#include "sts/engine/monster_slime_large.hpp"  // large slimes + split framework (B3.17)
#include "sts/engine/monster_slime_boss.hpp"   // Slime Boss native AI/split (B3.20)

namespace sts::engine {

void queue_monster_move_effects(CombatState& state, uint8_t mi,
                                const sts::registry::MonsterDef& def,
                                uint8_t move) noexcept {
    const sts::registry::MonsterMove* mv = def.move(move);
    if (mv == nullptr) {
        return;  // unknown/empty move id: nothing decided yet (defensive)
    }
    for (uint8_t i = 0; i < mv->effect_count; ++i) {
        const sts::registry::MonsterMoveEffect& e = mv->effects[i];
        ActionQueueItem it{};
        // sts::registry::Opcode is pinned byte-equal to interp.hpp's Opcode
        // (static_asserts in cards.hpp), so the raw cast dispatches correctly.
        it.opcode = static_cast<uint16_t>(e.op);
        it.src = mi;
        it.tgt = (e.target == sts::registry::MonsterMoveTarget::SELF)
                     ? mi
                     : kActorPlayer;
        it.amount = e.amount.at(kMonsterAscension);
        it.flags = e.extra;  // APPLY_POWER: PowerId (make_apply_power_flags packing)
        if (e.op == sts::registry::Opcode::MAKE_CARD) {
            // Monster-authored MAKE_CARD uses the same generated packing as card
            // programs. The interpreter expects CardPile in src and CardId in
            // flags; tgt remains the player to avoid dynamic enemy fan-out.
            it.src = static_cast<uint8_t>((e.extra >> 16) & 0xFFu);
            it.tgt = kActorPlayer;
        }
        add_to_bottom(state, it);
    }
}

MonsterInitFn monster_init_fn(MonsterId id) noexcept {
    switch (id) {
        case MonsterId::JAW_WORM:
            return &jaw_worm_init;
        case MonsterId::CULTIST:
            return &cultist_init;
        case MonsterId::LOUSE_NORMAL:
            return &louse_normal_init;
        case MonsterId::LOUSE_DEFENSIVE:
            return &louse_defensive_init;
        case MonsterId::SPIKE_SLIME_SMALL:
            return &spike_slime_small_init;
        case MonsterId::SPIKE_SLIME_MEDIUM:
            return &spike_slime_medium_init;
        case MonsterId::ACID_SLIME_SMALL:
            return &acid_slime_small_init;
        case MonsterId::ACID_SLIME_MEDIUM:
            return &acid_slime_medium_init;
        case MonsterId::SPIKE_SLIME_LARGE:
            return &spike_slime_large_init;
        case MonsterId::ACID_SLIME_LARGE:
            return &acid_slime_large_init;
        case MonsterId::SLIME_BOSS:
            return &slime_boss_init;
        default:
            return nullptr;  // not yet implemented (B3.15-B3.22)
    }
}

MonsterTurnFn monster_turn_fn(MonsterId id) noexcept {
    switch (id) {
        case MonsterId::JAW_WORM:
            return &jaw_worm_take_turn;
        case MonsterId::CULTIST:
            return &cultist_take_turn;
        case MonsterId::LOUSE_NORMAL:
            return &louse_normal_take_turn;
        case MonsterId::LOUSE_DEFENSIVE:
            return &louse_defensive_take_turn;
        case MonsterId::SPIKE_SLIME_SMALL:
            return &spike_slime_small_take_turn;
        case MonsterId::SPIKE_SLIME_MEDIUM:
            return &spike_slime_medium_take_turn;
        case MonsterId::ACID_SLIME_SMALL:
            return &acid_slime_small_take_turn;
        case MonsterId::ACID_SLIME_MEDIUM:
            return &acid_slime_medium_take_turn;
        case MonsterId::SPIKE_SLIME_LARGE:
            return &spike_slime_large_take_turn;
        case MonsterId::ACID_SLIME_LARGE:
            return &acid_slime_large_take_turn;
        case MonsterId::SLIME_BOSS:
            return &slime_boss_take_turn;
        default:
            return &default_monster_turn;  // no-op until the monster's batch lands
    }
}

MonsterRollMoveFn monster_roll_move_fn(MonsterId id) noexcept {
    switch (id) {
        case MonsterId::SPIKE_SLIME_LARGE:
            return &spike_slime_large_roll_move;
        case MonsterId::ACID_SLIME_LARGE:
            return &acid_slime_large_roll_move;
        default:
            return nullptr;  // rolls inline in its MonsterTurnFn; no queued rolls
    }
}

void roll_monster_move(CombatState& state, uint8_t monster_index) noexcept {
    if (monster_index >= kMonsterCap) {
        return;
    }
    const MonsterId id =
        static_cast<MonsterId>(state.monsters[monster_index].monster_id);
    const MonsterRollMoveFn fn = monster_roll_move_fn(id);
    if (fn != nullptr) {
        fn(state, monster_index);  // no liveness gate (RollMoveAction.java:17-21)
    }
}

MonsterSpawnAtHpFn monster_spawn_at_hp_fn(MonsterId id) noexcept {
    switch (id) {
        case MonsterId::SPIKE_SLIME_MEDIUM:
            return &spike_slime_medium_spawn_at_hp;
        case MonsterId::ACID_SLIME_MEDIUM:
            return &acid_slime_medium_spawn_at_hp;
        case MonsterId::SPIKE_SLIME_LARGE:  // B3.20 Slime Boss split children
            return &spike_slime_large_spawn_at_hp;
        case MonsterId::ACID_SLIME_LARGE:
            return &acid_slime_large_spawn_at_hp;
        default:
            return nullptr;  // not mid-combat spawnable
    }
}

void spawn_monster_at_slot(CombatState& state, uint8_t slot, MonsterId id,
                           int16_t hp) noexcept {
    assert(state.monster_count < kMonsterCap &&
           "spawn_monster_at_slot: monster record overflow (kMonsterCap sized "
           "for the fully-split Slime Boss, combat_state.hpp)");
    if (slot > state.monster_count) {
        slot = state.monster_count;  // defensive clamp (addMonster clamps < 0)
    }
    // List-insert: shift records at >= slot up one (MonsterGroup.addMonster,
    // MonsterGroup.java:35-40). Dead records shift too -- index identity among
    // ALL records is what smart positioning counted.
    for (uint8_t i = state.monster_count; i > slot; --i) {
        state.monsters[i] = state.monsters[i - 1];
    }
    state.monsters[slot] = MonsterState{};
    ++state.monster_count;
    // Remap pending monster-turn queue entries (the game's monsterQueue holds
    // object references, immune to list insertion; this engine holds indices).
    for (uint8_t i = 0; i < state.monster_queue_count; ++i) {
        if (state.monster_queue[i].monster_index >= slot) {
            ++state.monster_queue[i].monster_index;
        }
    }
    const MonsterSpawnAtHpFn fn = monster_spawn_at_hp_fn(id);
    assert(fn != nullptr && "spawn_monster_at_slot: monster is not mid-combat "
                            "spawnable (monster_spawn_at_hp_fn)");
    fn(state, slot, hp);  // m.init(): the child's aiRng roll, at resolve time
}

void on_monster_damaged(CombatState& state, uint8_t monster_index) noexcept {
    if (monster_index >= kMonsterCap) {
        return;
    }
    switch (static_cast<MonsterId>(state.monsters[monster_index].monster_id)) {
        case MonsterId::SPIKE_SLIME_LARGE:
        case MonsterId::ACID_SLIME_LARGE:
            large_slime_on_damaged(state, monster_index);
            return;
        case MonsterId::SLIME_BOSS:
            slime_boss_on_damaged(state, monster_index);
            return;
        default:
            return;  // no damage() override
    }
}

MonsterPreBattleFn monster_pre_battle_fn(MonsterId id) noexcept {
    switch (id) {
        case MonsterId::LOUSE_NORMAL:
        case MonsterId::LOUSE_DEFENSIVE:
            return &louse_use_pre_battle_action;  // curl-up roll (monster_hp_rng)
        default:
            return nullptr;  // no pre-battle action
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
               "spawn_group: monster not yet implemented (lands in B3.14-B3.22)");
        init(state, i);
    }
}

void use_pre_battle_actions(CombatState& state) noexcept {
    // preBattlePrep runs usePreBattleAction over the group in spawn order
    // (MonsterRoom.onPlayerEntry -> player.preBattlePrep, AbstractPlayer.java:1602).
    // monster_hp_rng is thus consumed in two phases (ctor HP rolls, then curl-ups),
    // both in spawn order -- the stream sees the concatenation.
    for (uint8_t i = 0; i < state.monster_count; ++i) {
        const MonsterId id =
            static_cast<MonsterId>(state.monsters[i].monster_id);
        const MonsterPreBattleFn fn = monster_pre_battle_fn(id);
        if (fn != nullptr) {
            fn(state, i);
        }
    }
}

}  // namespace sts::engine
