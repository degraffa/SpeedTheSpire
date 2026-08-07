#pragma once

// THE OMNISCIENT (FULL-STATE) OBSERVATION -- NOT the training observation.
//
// Everything in this header is spelled `omniscient_*` / `Omniscient*` on
// purpose (task T0.7). This encoder reads CombatState WHOLESALE: it carries the
// true monster intent whenever Runic Dome is absent, and it is filled from a
// state whose hidden realizations (draw order, unconsumed encounter suffix,
// unopened chest) the reader can also reach directly through the same
// CombatState reference. It is the right tool for a debug dump, a diff harness,
// or an OMNISCIENT baseline agent, and the WRONG tool for anything that trains
// or acts under the information contract -- those read PublicView
// (public_view.hpp) and hash it with public_hash().
//
// The spelling is the enforcement point, not a naming preference: it is a
// distinct token nothing else in the tree uses, so
// tools/check_omniscient_boundary.sh can prove by grep that no training-facing
// file reaches any of it (that script's header names exactly what it scans, and
// tests/omniscient_boundary_test.cpp proves it fires). The boundary marker sits
// on the ACCESS POINTS -- this header's name, the encoder, the record it fills,
// and StepResult::omniscient_obs, the one place a filled record is handed out.
// The element types (ObsMonster, ObsPower) and the kObs* capacities are inert
// shapes that carry no state access, so they keep their original names; a file
// that mentions one but none of the access points has read nothing.
//
// Observation encoder stub (design doc §7, decision D0.3). Flattens a
// CombatState into a fixed-size, trivially-copyable POD (`OmniscientObsBuffer`)
// that an eventual out-of-engine NN feature encoder consumes. Per D0.3 the
// encoding "lives inside the simulator ... one pass over the flat state with no
// intermediate allocation, and Python never touches hot-path bytes":
// omniscient_encode_observation() is a single linear pass that writes directly
// into the caller-owned `out` buffer -- no heap allocation of any kind.
//
// SCOPE: this is the observation stub -- it lands the flat, fixed layout the
// batch API needs (hp/energy/block, hand card ids+costs, monster
// hp/intent/powers), NOT the quantization / int8-fp16 tensor-dtype machinery
// D0.3 mentions for later. Values are copied out at their native
// CombatState widths (mostly int16); a downstream encoder normalizes/quantizes.
//
// VERSIONING: OmniscientObsBuffer reuses the single engine SCHEMA_VERSION
// constant
// (include/sts/engine/schema.hpp), the same stamp CombatState/RunState expose,
// rather than a separate observation-schema number. Rationale: the observation
// is a pure projection of CombatState's fields, so any CombatState layout change
// that would alter the observation is already a SCHEMA_VERSION bump by that
// header's own rule; a second independent version would be redundant to keep in
// sync. Unlike the state structs (whose stamp is compile-time-only to stay
// value-init/hash-stable, design doc §4.1), OmniscientObsBuffer is not hashed
// or
// value-init-for-hashing, so it stores the stamp as a real field -- matching
// design doc §8's trajectory-container convention of a version-stamped record.

#include <cstdint>
#include <type_traits>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/knowledge.hpp"    // combat_hides_intent (HIDE_INTENT)
#include "sts/engine/relic_hooks.hpp"  // player_has_relic
#include "sts/engine/schema.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// --- Observation capacities -------------------------------------------------

// Hand and monster slot counts mirror CombatState's own capacities so the
// observation is a fixed-width array regardless of the live hand/monster count.
inline constexpr int kObsHandCap = kHandCap;        // 10
inline constexpr int kObsMonsterCap = kMonsterCap;  // == kMonsterCap (7)

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
// type so OmniscientObsBuffer does not depend on PowerSlot's layout staying
// identical.
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
    uint8_t intent;       // telegraphed next-move id (opaque)
    uint8_t occupied;     // 1 = live monster in this slot, 0 = empty
    uint8_t power_count;  // true CombatState power count (pre-truncation)
    uint8_t pad0;         // explicit padding, kept deterministic
    ObsPower powers[kObsMonsterPowerCap];
};

static_assert(std::is_trivially_copyable_v<ObsMonster>);
static_assert(sizeof(ObsMonster) == 10 + 4 * kObsMonsterPowerCap,
              "ObsMonster layout drifted");

// --- OmniscientObsBuffer ----------------------------------------------------

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
// No hard ceiling is specified for OmniscientObsBuffer (it is not
// CombatState/RunState, so there is no §4.2/§4.3 budget row), but it is kept
// compact per D0.3's
// no-intermediate-allocation intent.
struct OmniscientObsBuffer {
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

static_assert(std::is_trivially_copyable_v<OmniscientObsBuffer>,
              "OmniscientObsBuffer must be trivially copyable (POD observation record)");
static_assert(sizeof(OmniscientObsBuffer) == 656,
              "OmniscientObsBuffer size changed -- update the layout comment and, if this "
              "reflects a CombatState field change, SCHEMA_VERSION");
// 188 -> 240 when kMonsterCap grew: kObsMonsterCap tracks it (5 -> 7), so the
// per-monster ObsMonster block grew by 2 slots (2 * 26 B). This mirrors the
// CombatState kMonsterCap growth (SCHEMA_VERSION 3 -> 4).
// 240 -> 656 for the same reason at SCHEMA_VERSION 6 -> 7: kMonsterCap 7 -> 23
// adds 16 more ObsMonster slots (16 * 26 B). This buffer is the OMNISCIENT
// observation, so unlike PublicView it carries no version stamp of its own --
// the SCHEMA_VERSION bump is its stamp.

// --- omniscient_encode_observation -----------------------------------------------------

// Single linear pass over `state`, writing directly into `out`. No heap
// allocation (verified by observation_test's counting-allocator check). `out`
// is fully overwritten -- every fixed slot is assigned, padded ones included --
// so the caller need not pre-zero it.
inline void omniscient_encode_observation(const CombatState& state,
                                          OmniscientObsBuffer& out) noexcept {
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
    //
    // RUNIC DOME. "You can no longer see enemy Intents" is TWO rendering guards
    // in the game and nothing else -- AbstractMonster.java:258 (the intent-alpha
    // update) and :749 (the intent render), both a negated
    // `player.hasRelic("Runic Dome")`, and `grep -rn "Runic Dome" com/` finds
    // only those two plus the relic's own ID constant. Neither touches game
    // state, so this is the ONE place it belongs: the observation is this
    // engine's view-of-the-board, and hiding it here makes OmniscientObsBuffer a strictly
    // lossier projection of CombatState rather than changing CombatState.
    //
    // WHERE IT MUST NOT REACH -- and this is the whole reason the suppression is
    // a single expression at one write site. MonsterState::intent holds the
    // game's move_id byte, not the display banner, and the diff pipeline is
    // built on that: the oracle fork already forces the DISPLAY intent string to
    // NONE under Runic Dome, and the translator deliberately ignores that string
    // and anchors on move_id (translate.cpp `fr.ignore("intent")`), so
    // differ.cpp's MonsterState .intent comparison stays correct on both sides.
    // Hiding the move in MonsterState -- or "fixing" the translator to match the
    // fork's string -- would corrupt every capture diff.
    //
    // The sentinel is free: registry/monsters.yaml documents move_id as unique
    // per monster and NEVER 0 (0 is the move_history empty-slot marker), and no
    // row uses it -- so 0 reads unambiguously as "hidden", the same value an
    // unoccupied slot carries.
    // Membership comes from the registry's `observability:` field through the
    // generated table (HIDE_INTENT -- Runic Dome is its only S1 member), so
    // this write site needs no edit when another intent-hiding source lands.
    const bool hide_intents = combat_hides_intent(state);
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
            om.intent = hide_intents ? 0 : ms.intent;
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
