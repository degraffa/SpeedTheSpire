#pragma once

// Observation encoder stub (design doc §7, decision D0.3). Flattens a
// CombatState into a fixed-size, trivially-copyable POD (`ObsBuffer`) that an
// eventual out-of-engine NN feature encoder consumes. Per D0.3 the encoding
// "lives inside the simulator ... one pass over the flat state with no
// intermediate allocation, and Python never touches hot-path bytes":
// encode_observation() is a single linear pass that writes directly into the
// caller-owned `out` buffer -- no heap allocation of any kind.
//
// SCOPE: this is the "observation stub" of task A5.2 -- it lands the flat,
// fixed layout the batch API needs (hp/energy/block, hand card ids+costs,
// monster hp/intent/powers), NOT the quantization / int8-fp16 tensor-dtype
// machinery D0.3 mentions for later. Values are copied out at their native
// CombatState widths (mostly int16); a downstream encoder normalizes/quantizes.
//
// VERSIONING: ObsBuffer reuses the single engine SCHEMA_VERSION constant
// (include/sts/engine/schema.hpp), the same stamp CombatState/RunState expose,
// rather than a separate observation-schema number. Rationale: the observation
// is a pure projection of CombatState's fields, so any CombatState layout change
// that would alter the observation is already a SCHEMA_VERSION bump by that
// header's own rule; a second independent version would be redundant to keep in
// sync. Unlike the state structs (whose stamp is compile-time-only to stay
// value-init/hash-stable, design doc §4.1), ObsBuffer is not hashed or
// value-init-for-hashing, so it stores the stamp as a real field -- matching
// design doc §8's trajectory-container convention of a version-stamped record.

#include <cstdint>
#include <type_traits>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/schema.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// --- Observation capacities -------------------------------------------------

// Hand and monster slot counts mirror CombatState's own capacities so the
// observation is a fixed-width array regardless of the live hand/monster count.
inline constexpr int kObsHandCap = kHandCap;        // 10
inline constexpr int kObsMonsterCap = kMonsterCap;  // 5

// Per-monster power slots carried in the observation. Deliberately MUCH smaller
// than CombatState's full 24-slot per-monster power array: replicating all 24
// (× 5 monsters) would bloat this "stub" buffer by ~480 bytes for slots the
// skeleton never fills. The M1 skeleton's Jaw Worm carries at most Strength,
// Vulnerable, and Weak simultaneously (design doc §9), i.e. 3 live powers; 4
// slots gives one slot of headroom while staying compact. Monsters with more
// than kObsMonsterPowerCap live powers are truncated to the first that many
// (power_count still reports the true CombatState count so a consumer can
// detect truncation).
inline constexpr int kObsMonsterPowerCap = 4;

// --- Sentinels --------------------------------------------------------------

// Unused hand slots read card id NONE (0, matching the CardId::NONE / empty
// pool-row convention in types.hpp) and cost kObsEmptyCost. Unused monster
// slots read `occupied == 0` with all fields zeroed.
inline constexpr uint16_t kObsEmptyCardId = 0;  // == CardId::NONE
inline constexpr int16_t kObsEmptyCost = -1;    // impossible real cost -> sentinel

// --- ObsPower ---------------------------------------------------------------

// One power entry in the observation. Same shape as PowerSlot; kept as its own
// type so ObsBuffer does not depend on PowerSlot's layout staying identical.
struct ObsPower {
    uint16_t power_id;  // PowerId; NONE (0) == empty slot
    int16_t amount;
};

static_assert(std::is_trivially_copyable_v<ObsPower>);
static_assert(sizeof(ObsPower) == 4);

// --- ObsMonster -------------------------------------------------------------

// One monster slot in the observation. `occupied` is 1 when this slot maps to a
// live monster array entry (index < monster_count), else 0 (all other fields
// zeroed). `power_count` is the monster's TRUE live power count from
// CombatState (may exceed kObsMonsterPowerCap, in which case `powers` holds only
// the first kObsMonsterPowerCap and the rest are truncated).
struct ObsMonster {
    uint16_t monster_id;  // MonsterId; NONE (0) == empty slot
    int16_t hp;
    int16_t max_hp;
    uint8_t intent;       // telegraphed next-move id (opaque; A3.2 defines it)
    uint8_t occupied;     // 1 = live monster in this slot, 0 = empty
    uint8_t power_count;  // true CombatState power count (pre-truncation)
    uint8_t pad0;         // explicit padding, kept deterministic
    ObsPower powers[kObsMonsterPowerCap];
};

static_assert(std::is_trivially_copyable_v<ObsMonster>);
static_assert(sizeof(ObsMonster) == 10 + 4 * kObsMonsterPowerCap,
              "ObsMonster layout drifted");

// --- ObsBuffer --------------------------------------------------------------

// Flat, fixed-size, trivially-copyable observation of a CombatState.
//
// Layout (all offsets fixed; a downstream feature encoder reads by field, never
// by live count):
//   schema_version : engine SCHEMA_VERSION at encode time
//   player_*       : hp / max_hp / block / energy, native int16 widths
//   hand_count     : live hand size (0..kObsHandCap)
//   hand_card_id[] : per hand slot, the card's CardId (kObsEmptyCardId padded)
//   hand_cost[]    : per hand slot, the card's cost_now (kObsEmptyCost padded)
//   monster_count  : live monster count (0..kObsMonsterCap)
//   monsters[]     : per monster slot, ObsMonster (occupied==0 for empty slots)
//
// Total size: 188 bytes on the reference toolchain (see static_assert below).
// No hard ceiling is specified for ObsBuffer (it is not CombatState/RunState,
// so there is no §4.2/§4.3 budget row), but it is kept compact per D0.3's
// no-intermediate-allocation intent.
struct ObsBuffer {
    uint32_t schema_version;

    int16_t player_hp;
    int16_t player_max_hp;
    int16_t player_block;
    int16_t player_energy;

    uint16_t hand_count;
    uint16_t hand_card_id[kObsHandCap];
    int16_t hand_cost[kObsHandCap];

    uint16_t monster_count;
    ObsMonster monsters[kObsMonsterCap];
};

static_assert(std::is_trivially_copyable_v<ObsBuffer>,
              "ObsBuffer must be trivially copyable (POD observation record)");
static_assert(sizeof(ObsBuffer) == 188,
              "ObsBuffer size changed -- update the layout comment and, if this "
              "reflects a CombatState field change, SCHEMA_VERSION");

// --- encode_observation -----------------------------------------------------

// Single linear pass over `state`, writing directly into `out`. No heap
// allocation (verified by observation_test's counting-allocator check). `out`
// is fully overwritten -- every fixed slot is assigned, padded ones included --
// so the caller need not pre-zero it.
inline void encode_observation(const CombatState& state, ObsBuffer& out) noexcept {
    out.schema_version = SCHEMA_VERSION;

    out.player_hp = state.player_hp;
    out.player_max_hp = state.player_max_hp;
    out.player_block = state.player_block;
    out.player_energy = state.player_energy;

    // Hand: fixed-width; live slots carry (card_id, cost_now), unused slots the
    // documented sentinels.
    const int hand_n = state.hand_count < kObsHandCap ? state.hand_count : kObsHandCap;
    out.hand_count = static_cast<uint16_t>(state.hand_count);
    for (int i = 0; i < kObsHandCap; ++i) {
        if (i < hand_n) {
            const CardInstance& c = state.card_pool[state.hand[i]];
            out.hand_card_id[i] = c.card_id;
            out.hand_cost[i] = static_cast<int16_t>(c.cost_now);
        } else {
            out.hand_card_id[i] = kObsEmptyCardId;
            out.hand_cost[i] = kObsEmptyCost;
        }
    }

    // Monsters: fixed-width; occupied slots carry hp/intent/powers, empty slots
    // are fully zeroed with occupied == 0.
    const int mon_n =
        state.monster_count < kObsMonsterCap ? state.monster_count : kObsMonsterCap;
    out.monster_count = static_cast<uint16_t>(state.monster_count);
    for (int m = 0; m < kObsMonsterCap; ++m) {
        ObsMonster& om = out.monsters[m];
        if (m < mon_n) {
            const MonsterState& ms = state.monsters[m];
            om.monster_id = ms.monster_id;
            om.hp = ms.hp;
            om.max_hp = ms.max_hp;
            om.intent = ms.intent;
            om.occupied = 1;
            om.power_count = ms.power_count;
            om.pad0 = 0;
            const int pn = ms.power_count < kObsMonsterPowerCap ? ms.power_count
                                                                : kObsMonsterPowerCap;
            for (int p = 0; p < kObsMonsterPowerCap; ++p) {
                if (p < pn) {
                    om.powers[p].power_id = ms.powers[p].power_id;
                    om.powers[p].amount = ms.powers[p].amount;
                } else {
                    om.powers[p].power_id = 0;  // PowerId::NONE
                    om.powers[p].amount = 0;
                }
            }
        } else {
            om.monster_id = 0;  // MonsterId::NONE
            om.hp = 0;
            om.max_hp = 0;
            om.intent = 0;
            om.occupied = 0;
            om.power_count = 0;
            om.pad0 = 0;
            for (int p = 0; p < kObsMonsterPowerCap; ++p) {
                om.powers[p].power_id = 0;
                om.powers[p].amount = 0;
            }
        }
    }
}

}  // namespace sts::engine
