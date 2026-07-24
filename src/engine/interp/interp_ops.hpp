#pragma once

// Opcode-body plumbing (internal to src/engine -- NOT public API). The effect
// interpreter used to be one 1,420-line interp.cpp: ~20 `op_*` bodies plus the
// card-manipulation helpers in a single anonymous namespace, under a 224-line
// execute_opcode switch. The bodies now live in per-domain translation units
// under src/engine/interp/ and interp.cpp keeps only the dispatcher (the
// dynamic-target fan-out + the switch), mirroring the powers/relics split
// (per-batch TUs + a dispatch site; see powers/power_native.hpp).
//
// Each op body is a free function declared in its domain header
// (interp_damage.hpp, interp_block.hpp, interp_powers.hpp, interp_cards.hpp).
// Adding an opcode is: one body in the domain TU that owns it, one declaration
// in that domain's header, one case in interp.cpp -- no edits to the files the
// other domains own.
//
// This header carries only what MORE THAN ONE of those TUs (or execute_opcode
// itself) needs: the actor views that were file-local statics in interp.cpp.
// They stay `inline` in the header rather than becoming cross-TU calls because
// they sit inside the per-power loops of the DAMAGE/BLOCK pipelines -- the
// hottest code in the simulator.

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // kActorPlayer
#include "sts/engine/combat_state.hpp"  // CombatState, PowerSlot, kMonsterCap

namespace sts::engine {

// --- Actor power views ------------------------------------------------------

struct PowerView {
    const PowerSlot* slots;  // may be null when actor is out of range
    uint8_t count;
};

// Powers of an actor (kActorPlayer -> player_powers, 0..4 -> that monster).
// Out-of-range actors yield an empty view so callers iterate nothing and never
// index OOB (defensive against junk src/tgt in malformed items).
[[nodiscard]] inline PowerView actor_powers(const CombatState& s,
                                            uint8_t actor) noexcept {
    if (actor == kActorPlayer) {
        return PowerView{s.player_powers, s.player_power_count};
    }
    if (actor < kMonsterCap) {
        return PowerView{s.monsters[actor].powers, s.monsters[actor].power_count};
    }
    return PowerView{nullptr, 0};
}

// Mutable hp/block cells for an actor (null when out of range).
[[nodiscard]] inline int16_t* actor_hp(CombatState& s, uint8_t actor) noexcept {
    if (actor == kActorPlayer) {
        return &s.player_hp;
    }
    if (actor < kMonsterCap) {
        return &s.monsters[actor].hp;
    }
    return nullptr;
}

[[nodiscard]] inline int16_t* actor_block(CombatState& s, uint8_t actor) noexcept {
    if (actor == kActorPlayer) {
        return &s.player_block;
    }
    if (actor < kMonsterCap) {
        return &s.monsters[actor].block;
    }
    return nullptr;
}

}  // namespace sts::engine
