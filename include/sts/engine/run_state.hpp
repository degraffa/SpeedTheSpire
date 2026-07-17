#pragma once

// RunState -- the whole-run game state (design doc §4.3), budget <= 8192 bytes.
// CombatState is *derived* from RunState at combat start and folded back at
// combat end; the two never alias (design doc §4.4). A full RunState->CombatState
// derivation is not yet implemented; this struct is the storage shape. Field
// naming deliberately matches CombatState where the same conceptual data appears
// in both (hp / max_hp), so the future fold is a plain field copy.
//
// The M1 walking skeleton (design doc §9) has no run layer at all -- it is a
// single combat. Every field here is therefore forward design per the §4.3
// list: the skeleton constructs a CombatState directly and never populates the
// map / relics / potions / run-scoped streams. Fields whose exact encoding the
// design doc leaves open (boss ids, keys, event/shop one-shot flags) are
// minimal documented placeholders, sized small; later stages (Stage B events/
// shops/relics) define their semantics. Same §4.1 principles as CombatState:
// trivially copyable, pointer-free, fixed-capacity, plain aggregate so `{}`
// value-init zero-fills including padding.
//
// STREAM COUNT (design doc §3.4 / §3.6): RunState carries the 7 run-scoped
// streams (monsterRng, eventRng, merchantRng, cardRng, treasureRng, relicRng,
// potionRng) plus the act-scoped mapRng.

#include <cstdint>
#include <type_traits>

#include "sts/engine/schema.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// --- Capacities (design doc §4.3) -------------------------------------------

inline constexpr int kMasterDeckCap = 128;
inline constexpr int kRelicCap = 40;
inline constexpr int kPotionCap = 5;
inline constexpr int kMapRows = 7;
inline constexpr int kMapCols = 15;
inline constexpr int kBossIdCap = 4;   // placeholder: up to one boss id per act

// --- RelicSlot (design doc §4.3) --------------------------------------------

// One relic, stored in acquisition order (== trigger order, design doc §4.3 /
// trap 8), so the array is simply insertion-ordered with a count -- no separate
// ordering field. `counter` is the relic's per-instance counter (e.g. charge
// state). The skeleton has zero relics (RelicId is sentinel-only), so this
// array starts empty.
struct RelicSlot {
    uint16_t relic_id;   // RelicId; NONE == empty
    int16_t counter;
};

static_assert(std::is_trivially_copyable_v<RelicSlot>);
static_assert(sizeof(RelicSlot) == 4,
              "RelicSlot must be 4 bytes (design doc §4.3: u16+i16)");

// --- MapNode (design doc §4.3: 7×15 node grid) ------------------------------

// One node of the act map. `room_type` is the room-kind id (monster/elite/rest/
// shop/treasure/event/boss -- enumerated by Stage B's map generator, opaque
// here). `edges` is a bitfield of outgoing connections to the next row's nodes.
// Minimal placeholder shape `{room_type: u8, edges: u8}` exactly per §4.3;
// mapRng consumption order and the concrete encodings are deferred to Stage B
// (design doc §11).
struct MapNode {
    uint8_t room_type;
    uint8_t edges;
};

static_assert(std::is_trivially_copyable_v<MapNode>);
static_assert(sizeof(MapNode) == 2);

// --- RunState ---------------------------------------------------------------

struct RunState {
    // Schema stamp (design doc §8) -- compile-time, no per-instance storage.
    static constexpr uint32_t kSchemaVersion = SCHEMA_VERSION;

    // -- run seed (design doc §4.3): the run's master seed. All run-scoped and
    //    floor-scoped streams derive from it (design doc §3.4). --
    int64_t run_seed;

    // -- master deck (design doc §4.3): 128 card instances + count. --
    CardInstance master_deck[kMasterDeckCap];
    uint16_t master_deck_count;

    // -- character sheet (design doc §4.3). hp/max_hp match CombatState's player
    //    widths (int16) so combat_begin()/fold is a plain copy. --
    int16_t hp;
    int16_t max_hp;
    int32_t gold;
    uint8_t ascension;                // A0..A20
    uint8_t act;                      // 1..4
    uint16_t floor;                   // up to ~55 (golden vectors); u16 is ample

    // -- relics (design doc §4.3): acquisition-ordered, count-terminated. --
    RelicSlot relics[kRelicCap];
    uint8_t relic_count;
    uint8_t pad_relic;                // explicit padding

    // -- potions (design doc §4.3): 5 positional slots. Minimal representation:
    //    a potion id per slot (PotionId NONE == empty). Potion identity rolls
    //    are Stage B (design doc §11); the skeleton uses none. --
    uint16_t potions[kPotionCap];

    // -- map (design doc §4.3): 7×15 node grid. Flattened row-major. --
    MapNode map[kMapRows * kMapCols];

    // -- placeholders (design doc §4.3), not exercised by the skeleton --
    uint16_t boss_ids[kBossIdCap];    // one boss id per act (0 = unset)
    uint8_t keys;                     // emerald/ruby/sapphire key bitflags
    uint8_t pad_keys;                 // explicit padding
    uint32_t event_flags;             // one-shot event bitset (Stage B)
    uint32_t shop_flags;              // one-shot shop bitset (Stage B)

    // -- pity counters riding on the streams, persisted in the save file so they
    //    are RunState fields (design doc §3.4). Real starting values
    //    (cardBlizzRandomizer = 5) are set at run-start by later stages;
    //    value-init leaves them 0. --
    int16_t card_blizz_randomizer;    // rare/uncommon reward bias (starts 5)
    int16_t blizzard_potion_mod;      // ±10% potion-drop ratchet

    // -- RNG: the 7 run-scoped streams (design doc §3.4) + the act-scoped mapRng
    //    (design doc §3.6). See the STREAM COUNT note at the top of this file.
    //    RngStream is 8-byte aligned, so RunState is 8-byte aligned; the
    //    compiler inserts (value-init-zeroed) padding ahead of this block. --
    RngStream monster_rng;            // encounter rolls
    RngStream event_rng;              // ?-room resolution, event rolls
    RngStream merchant_rng;           // shop stock/prices
    RngStream card_rng;               // reward card pools, rarity, upgrades
    RngStream treasure_rng;           // chest contents
    RngStream relic_rng;              // relic pool shuffles & rolls
    RngStream potion_rng;             // potion drops & identity
    RngStream map_rng;                // map generation (act-scoped seed)
};

static_assert(std::is_trivially_copyable_v<RunState>,
              "RunState must be trivially copyable (design doc §4.1: "
              "snapshot = memcpy)");
static_assert(sizeof(RunState) <= 8192,
              "RunState exceeds its 8 KB budget (design doc §4.3)");

}  // namespace sts::engine
