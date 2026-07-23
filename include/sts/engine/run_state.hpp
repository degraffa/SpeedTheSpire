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
// potionRng), the act-scoped mapRng, and (B4.3) the event-scoped neowRng -- the
// 14th stream of design §2.5 #1/#2.

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
// Map dims in GAME-NATIVE orientation (design §2.6 schema-v2 reorientation, B4.3):
// the act map is MAP_HEIGHT=15 floors (rows, y=0..14) x MAP_WIDTH=7 columns
// (x=0..6) (AbstractDungeon.java:210-211). Stage A named these transposed
// (kMapRows=7 / kMapCols=15); B4.1/B4.2 built game-oriented logic over an index
// adapter. This is the promised rename: the 105-node backing array is unchanged
// (7*15 == 15*7 == 105) and the row-major index `floor*kMapCols + col` is
// byte-identical to the old `y*7 + x`, so NO map bytes move -- only the names now
// read the way the game does. map_gen.hpp / map_rooms.hpp adapt accordingly.
inline constexpr int kMapRows = 15;    // map height: floors  (game MAP_HEIGHT)
inline constexpr int kMapCols = 7;     // map width:  columns (game MAP_WIDTH)
inline constexpr int kBossIdCap = 4;   // placeholder: up to one boss id per act

// Relic-pool storage (design §2.5 #8, B4.3): the five relic tiers, each a pool
// shuffled once at dungeon init then popped (front for rewards, end for shop --
// trap 15). kRelicTierCount is the tier count; kRelicPoolCap bounds one tier's
// initialized dungeon pool (S1 Ironclad's largest is the common pool at 33 --
// oracle golden; 48 leaves headroom). Tier index convention (matches the game's
// AbstractRelic.RelicTier ordering used at pool init): 0=Common, 1=Uncommon,
// 2=Rare, 3=Shop, 4=Boss.
inline constexpr int kRelicTierCount = 5;
inline constexpr int kRelicPoolCap = 48;

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

// --- MapNode (design doc §4.3: 15×7 node grid -- 15 floors × 7 columns) ------

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

    // -- map (design doc §4.3): 15×7 node grid (15 floors × 7 columns), flattened
    //    row-major as `floor*kMapCols + col` (game-native after the B4.3 rename). --
    MapNode map[kMapRows * kMapCols];

    // -- placeholders (design doc §4.3), not exercised by the skeleton --
    uint16_t boss_ids[kBossIdCap];    // one boss id per act (0 = unset)
    uint8_t keys;                     // emerald/ruby/sapphire key bitflags
    uint8_t pad_keys;                 // explicit padding
    uint32_t event_flags;             // one-shot event *fired* bitset (Stage B)
    uint32_t shop_flags;              // one-shot shop bitset (Stage B)

    // -- pity counters riding on the streams, persisted in the save file so they
    //    are RunState fields (design doc §3.4). Real starting values
    //    (cardBlizzRandomizer = 5) are set at run-start by later stages;
    //    value-init leaves them 0. --
    int16_t card_blizz_randomizer;    // rare/uncommon reward bias (starts 5)
    int16_t blizzard_potion_mod;      // ±10% potion-drop ratchet

    // ========================================================================
    // schema-v3 additive run inventory (B4.3, design §2.6 / §2.5 items 2,5,6,7,8)
    // ========================================================================
    // Additive per stage-a §12: POD, trivially copyable, capacity-bounded, and
    // value-init zero-fills them so pre-population states are byte-clean. The
    // translator (tools/oracle_bridge/translator) writes the NUMERIC/COUNT ones
    // now; the id-list ones (relic pools, event/shrine/special membership) need
    // registry enums that do not exist at HEAD (relics.yaml/events.yaml empty)
    // and are populated by B4.6 (relics) / B4.10-B4.13 (events) -- storage lands
    // here so those tasks need no further schema bump (design §2.6 front-loading).

    // Event-pity chances (EventHelper.MONSTER/SHOP/TREASURE_CHANCE, floats; grow
    // 0.1/0.03/0.02 per miss, reset on hit; §2.5 #5). Stored as float so the
    // game's float literals reproduce bit-for-bit. Consumed/advanced by B4.10.
    float event_pity_monster;
    float event_pity_shop;
    float event_pity_treasure;

    // Shop purge cost (ShopScreen.purgeCost, base 75, +25 per purge; run-
    // persistent; §2.5 #6). Consumed by B4.8. u16-range is ample.
    int16_t purge_cost;

    // Potion-slot count: how many slots the player actually has (base 3; A11
    // removes one -> 2). Distinct from kPotionCap (the storage ceiling). Set at
    // run_begin (A11) / by potion-belt relics later.
    uint8_t potion_slots;
    uint8_t pad_potion_slots;         // explicit padding

    // Remaining event/shrine/special POOL-membership bitsets (§2.5 #7): bit set
    // == that entry is still in the draw pool (cleared on use). Bit index =
    // position in the act's canonical init list; that list is defined by the
    // event registry (B4.10-B4.13), which also populates these. Widths cover the
    // Act-1 lists (11 events / 6 shrines / 14 specials). These are the "remaining
    // pool" view; event_flags above stays the one-shot "already fired" view.
    uint16_t event_membership;
    uint16_t special_membership;
    uint8_t  shrine_membership;
    uint8_t  pad_membership;

    // Remaining relic-pool ORDER for all 5 tiers (§2.5 #8, trap 15): each tier is
    // an ordered list of RelicId (== relics[].relic_id widths), with a live count;
    // front-pop (rewards) / end-pop (shop) act on [0, count). Shuffled once at
    // init by B4.6 (relicRng); popped by B4.6/B4.7/B4.8. Tier index per the
    // kRelicTierCount comment above (0=Common..4=Boss).
    uint16_t relic_pools[kRelicTierCount][kRelicPoolCap];
    uint8_t  relic_pool_count[kRelicTierCount];
    uint8_t  pad_relic_pools[3];      // pad kRelicTierCount(5) -> 8

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
    RngStream neow_rng;               // NeowEvent.rng: event-scoped 14th stream,
                                      // fresh Random(seed) at run start (§2.5 #2,
                                      // B4.14). Only meaningful at floor 0; other
                                      // dumps carry a value-init (zero) stream.
};

static_assert(std::is_trivially_copyable_v<RunState>,
              "RunState must be trivially copyable (design doc §4.1: "
              "snapshot = memcpy)");
static_assert(sizeof(RunState) <= 8192,
              "RunState exceeds its 8 KB budget (design doc §4.3)");

}  // namespace sts::engine
