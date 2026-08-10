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
// potionRng), the act-scoped mapRng, and the event-scoped neowRng -- the
// 14th stream of design §2.5 #1/#2.

#include <cstddef>  // offsetof (the S2.13 pad-carve proof below the struct)
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
// Map dims in GAME-NATIVE orientation (design §2.6 schema-v2 reorientation):
// the act map is MAP_HEIGHT=15 floors (rows, y=0..14) x MAP_WIDTH=7 columns
// (x=0..6) (AbstractDungeon.java:210-211). These were originally named
// transposed (kMapRows=7 / kMapCols=15), with the map-generation logic reaching
// them through an index adapter. The rename that fixed it moved NO bytes: the
// 105-node backing array is unchanged
// (7*15 == 15*7 == 105) and the row-major index `floor*kMapCols + col` is
// byte-identical to the old `y*7 + x`, so NO map bytes move -- only the names now
// read the way the game does. map_gen.hpp / map_rooms.hpp adapt accordingly.
inline constexpr int kMapRows = 15;    // map height: floors  (game MAP_HEIGHT)
inline constexpr int kMapCols = 7;     // map width:  columns (game MAP_WIDTH)
inline constexpr int kBossIdCap = 4;   // placeholder: up to one boss id per act

// Relic-pool storage (design §2.5 #8): the five relic tiers, each a pool
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

// --- BossChestState (S2.47: the boss-chest boss-relic offers) ----------------
//
// MOVED HERE FROM RunController (schema v8). The full provenance block -- every
// cited BossChest.java / BossRelicSelectScreen.java method, the entry-pop trap,
// the skip-is-reversible note -- stays in boss_chest.hpp with the functions that
// operate on this struct; this comment carries only what the STORAGE needs.
//
// WHY IT IS RUN STATE AND NOT CONTROLLER STATE. The three offers are popped
// from rs.relic_pools[4] at room entry (BossChest.java:35-39) and are the
// evidence design §6 S2-G2 item 2 scores: a ZERO-DIFF boss-relic pick. The
// oracle translator emits RunState/CombatState and the differ compares those,
// so offers held only in transient controller state were invisible to
// differential verification -- the exact gap the s2-tasks.md deferred row
// "`BOSS_REWARD.screen_state.relics` -- schema storage" records. Storage here
// is the ONE source of truth: RunController has no copy of its own.

// BossChest.java:37 -- `for (int i = 0; i < 3; ++i)`.
inline constexpr int kBossChestOfferCount = 3;

// Which screen the boss-chest room is showing. The controller's phase stays
// RunPhase::BOSS_TREASURE for all of them (the NeowScreen precedent). See
// boss_chest.hpp for the per-value provenance.
enum class BossChestScreen : uint8_t {
    CLOSED = 0,            // chest on the floor (also what a SKIP returns to)
    RELIC_SELECT = 1,      // BOSS_REWARD screen up, three offers shown
    EQUIP_GRID = 2,        // picked relic's onEquip master-deck grid
    EQUIP_ITEM_REWARD = 3, // picked relic's onEquip claimable reward screen
    DONE = 4,              // pick resolved; proceed is the act terminal
};

struct BossChestState {
    // The three relics popped at room entry, in pop order. RelicId; 0 (NONE)
    // means "no live chest". Public only once `seen` -- see byte_class.hpp.
    uint16_t relics[kBossChestOfferCount];
    uint8_t screen;       // BossChestScreen
    // 1 once the chest has EVER been opened. Distinct from `screen == CLOSED`,
    // which a skip returns to: the player has still SEEN the three relics, so
    // this -- not the screen -- is the information-layer reveal flag.
    uint8_t seen;
    // TreasureRoomBoss.choseRelic (TreasureRoomBoss.java:29, set at
    // BossRelicSelectScreen.java:186). One bit of room state, kept here rather
    // than in a shared flags word so it cannot collide with anything.
    uint8_t chose_relic;
    // Explicit padding (byte-compared struct; conventions §8). SEVEN bytes:
    // rounding the struct to 16 keeps its size a multiple of RunState's own
    // 8-byte alignment, so the tail append below adds NO undeclared padding --
    // which is exactly what the byte_class.hpp tiling tripwire checks.
    uint8_t pad[7];
};

static_assert(std::is_trivially_copyable_v<BossChestState>);
static_assert(sizeof(BossChestState) == 16);
static_assert(sizeof(BossChestState) % 8 == 0,
              "keep BossChestState a multiple of RunState's alignment -- "
              "see the pad member's comment");

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
//
// PADDING IS DECLARED, NOT LEFT TO THE COMPILER. RunState is compared with
// `memcmp` (tools/diff_harness/src/differ.cpp, and a dozen tests) and hashed
// over its bytes, so every byte in it must be *initialised*. Neither of the two
// ways this struct is normally produced writes a byte that belongs to no
// member: `RunState{}` on an aggregate is aggregate-initialisation, which
// initialises members ([dcl.init.list]/3 reaches the aggregate bullet first),
// and an implicitly-defined copy constructor performs a memberwise copy
// ([class.copy.ctor]/14). Compiler-inserted padding is therefore indeterminate
// -- it merely *tends* to read as zero, because fresh stack frames and fresh
// heap pages tend to be zero.
//
// That tendency held on Linux and did not hold on Windows: two translations of
// one capture produced records differing at exactly one byte,
// Translator.RoundTripDeterministic went red on `win-debug` alone, and the
// report read as translator nondeterminism. It was two undeclared alignment
// gaps, one of which this file's own comment claimed was "value-init-zeroed".
//
// So every gap is an explicit `pad_*` member. That changes no offset and no
// size; it only makes the bytes members, which is what makes them written.
// `StateLayout.RunStateHasNoImplicitPadding` (tests/state_test.cpp) walks the
// declared member list and fails, by name, on any new hole.

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
    uint8_t pad_gold_align[2];        // explicit padding (see the PADDING note)
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
    //    row-major as `floor*kMapCols + col` (game-native orientation). --
    MapNode map[kMapRows * kMapCols];

    // -- placeholders (design doc §4.3), not exercised by the skeleton --
    uint16_t boss_ids[kBossIdCap];    // one boss id per act (0 = unset)
    // The game's three run-scoped key booleans (Settings.hasEmeraldKey /
    // hasRubyKey / hasSapphireKey -- reset per run, carried by the save file),
    // packed as the kKey* bits declared below the struct. The RUBY bit has a
    // live writer: the campfire's RecallOption (rest_sites.hpp,
    // CampfireUI.java:94-96 -> CampfireRecallEffect -> ObtainKeyEffect).
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
    // schema-v3 additive run inventory (design §2.6 / §2.5 items 2,5,6,7,8)
    // ========================================================================
    // Additive per stage-a §12: POD, trivially copyable, capacity-bounded, and
    // value-init zero-fills them so pre-population states are byte-clean. The
    // translator (tools/oracle_bridge/translator) writes the NUMERIC/COUNT ones
    // now; the id-list ones (relic pools, event/shrine/special membership) were
    // blocked on registry enums. Relic pools are populated today; the event /
    // shrine / special bitsets still need an events.yaml. The storage is here
    // either way, so filling them needs no further schema bump (design §2.6
    // front-loading).

    // Event-pity chances (EventHelper.MONSTER/SHOP/TREASURE_CHANCE, floats; grow
    // 0.1/0.03/0.02 per miss, reset on hit; §2.5 #5). Stored as float so the
    // game's float literals reproduce bit-for-bit. Nothing consumes them yet.
    float event_pity_monster;
    float event_pity_shop;
    float event_pity_treasure;

    // Shop purge cost (ShopScreen.purgeCost, base 75, +25 per purge; run-
    // persistent; §2.5 #6). Nothing consumes it yet. u16-range is ample.
    int16_t purge_cost;

    // Potion-slot count: how many slots the player actually has (base 3; A11
    // removes one -> 2). Distinct from kPotionCap (the storage ceiling). Set at
    // run_begin (A11) / by potion-belt relics later.
    uint8_t potion_slots;
    uint8_t pad_potion_slots;         // explicit padding

    // Remaining event/shrine/special POOL-membership bitsets (§2.5 #7): bit set
    // == that entry is still in the draw pool (cleared on use). Bit index =
    // position in the act's canonical init list, defined by the event registry
    // (registry/events.yaml -- it EXISTS; this comment used to predate it):
    // event_framework.cpp populates these at init and clears bits as entries
    // are drawn/used. Widths cover the
    // Act-1 lists (11 events / 6 shrines / 14 specials). These are the "remaining
    // pool" view; event_flags above stays the one-shot "already fired" view.
    uint16_t event_membership;
    uint16_t special_membership;
    uint8_t  shrine_membership;
    uint8_t  pad_membership;

    // Remaining relic-pool ORDER for all 5 tiers (§2.5 #8, trap 15): each tier is
    // an ordered list of RelicId (== relics[].relic_id widths), with a live count;
    // front-pop (rewards) / end-pop (shop) act on [0, count). Shuffled once at
    // dungeon init from relicRng (relic_pools.cpp), then popped as relics are
    // acquired. Tier index per the kRelicTierCount comment above
    // (0=Common..4=Boss).
    uint16_t relic_pools[kRelicTierCount][kRelicPoolCap];
    uint8_t  relic_pool_count[kRelicTierCount];
    uint8_t  pad_relic_pools[3];      // pad kRelicTierCount(5) -> 8

    // The SECOND word of the one-shot event FIRED bitset, CARVED OUT OF THE
    // SIX BYTES OF ALIGNMENT SLACK that used to be `pad_rng_align[6]` (S2.13).
    // `event_flags` above is a uint32_t with bit (id-1), so it addresses ids
    // 1..31 only, and S2.02 allocated ids 32..51 for the Act-2/3 events that
    // S2.13 makes drawable. Widening `event_flags` in place would move
    // `shop_flags` and everything after it (an unplanned SCHEMA_VERSION bump,
    // stop-the-line under conventions.md §5) and is separately ILLEGAL in
    // PublicView, whose contract forbids changing an existing field's width.
    //
    // So: two filler bytes, then a uint32_t in the remaining four. NO OFFSET
    // MOVES, `sizeof(RunState)` is unchanged, and SCHEMA_VERSION stays put --
    // the same terms as the CombatState carve-outs at combat_state.hpp:219,
    // 263, 288. Those bytes were `ByteClass::PADDING` and were value-init
    // zeroed on every path that produces a RunState, so no committed fixture
    // or captured trace carries a nonzero byte here and every old record still
    // reads back byte-identically with `event_flags_hi == 0`.
    //
    // The offset arithmetic is ASSERTED below rather than trusted: if the
    // static_assert on `offsetof(RunState, event_flags_hi) % 4` ever fires,
    // the carve does not fit and the answer is to surface a schema bump, not
    // to reorder members.
    //
    // Bit assignment: id 32..63 -> bit (id - 33). Never open-code that shift --
    // go through event_flag_set / event_flag_test (event_framework.hpp), which
    // route an id to the right word.
    uint8_t  pad_rng_align_lo[2];     // explicit padding (see the PADDING note)
    uint32_t event_flags_hi;          // event FIRED bitset, ids 32..63

    // -- RNG: the 7 run-scoped streams (design doc §3.4) + the act-scoped mapRng
    //    (design doc §3.6). See the STREAM COUNT note at the top of this file.
    //    RngStream is 8-byte aligned, so RunState is 8-byte aligned; the six
    //    bytes of alignment slack ahead of this block are `pad_rng_align_lo`
    //    (2) + `event_flags_hi` (4) rather than compiler padding, so they are
    //    actually initialised. Before S2.13 all six were padding. --
    RngStream monster_rng;            // encounter rolls
    RngStream event_rng;              // ?-room resolution, event rolls
    RngStream merchant_rng;           // shop stock/prices
    RngStream card_rng;               // reward card pools, rarity, upgrades
    RngStream treasure_rng;           // chest contents
    RngStream relic_rng;              // relic pool shuffles & rolls
    RngStream potion_rng;             // potion drops & identity
    RngStream map_rng;                // map generation (act-scoped seed)
    RngStream neow_rng;               // NeowEvent.rng: event-scoped 14th stream,
                                      // fresh Random(seed) at run start
                                      // (§2.5 #2). Only meaningful at floor 0; other
                                      // dumps carry a value-init (zero) stream.

    // ========================================================================
    // schema-v8 tail append (S2.47): the boss-chest boss-relic offers
    // ========================================================================
    // A PURE TAIL APPEND: every pre-existing offset is unchanged, and the two
    // static_asserts below prove both halves of that (no gap ahead of it, no
    // undeclared tail padding behind it). sizeof(RunState) grows 2184 -> 2200,
    // so per design §8 this is the SCHEMA_VERSION 7 -> 8 bump -- the s2-tasks.md
    // S2.47 row is the conventions-§5 planned site for it; the scattered 1-2
    // byte declared pads above cannot legally hold 3×uint16 + 3 flags, so the
    // carve alternative was checked and rejected rather than skipped.
    //
    // Populated by roll_boss_chest at boss-chest room entry, mutated by the
    // open/pick/skip flow (run_advance.cpp), cleared when the room is left at
    // the act transition. Zero everywhere else, which is also what the
    // translator leaves for every non-BOSS_REWARD dump -- the replay differ
    // gates the comparison on the capture attesting the offers (`seen`).
    BossChestState boss_chest;
};

static_assert(std::is_trivially_copyable_v<RunState>,
              "RunState must be trivially copyable (design doc §4.1: "
              "snapshot = memcpy)");
static_assert(sizeof(RunState) <= 8192,
              "RunState exceeds its 8 KB budget (design doc §4.3)");

// THE S2.13 PAD-CARVE PROOF. `event_flags_hi` was cut out of the six bytes of
// alignment slack that `pad_rng_align[6]` declared, so it must land 4-aligned
// inside that hole and leave the following RNG block exactly where it was. If
// either assert fires, the carve does NOT fit: surface a SCHEMA_VERSION bump
// (stop-the-line, conventions.md §5) rather than reordering members.
static_assert(offsetof(RunState, event_flags_hi) % 4 == 0,
              "event_flags_hi is not 4-aligned -- the S2.13 pad carve does not "
              "fit; see the comment on the member");
static_assert(offsetof(RunState, event_flags_hi) + sizeof(uint32_t) ==
                  offsetof(RunState, monster_rng),
              "event_flags_hi must exactly close the alignment hole ahead of "
              "the RNG block -- no offset may move");
static_assert(offsetof(RunState, pad_rng_align_lo) ==
                  offsetof(RunState, pad_relic_pools) + 3,
              "the carve must start where pad_relic_pools ended");

// THE S2.47 TAIL-APPEND PROOF. boss_chest must start exactly where neow_rng
// ends (no compiler gap ahead of it -- RngStream is 8-aligned and
// BossChestState needs only 2, so none is possible, but the layout asserts it
// rather than trusting the argument) and must itself end the struct (its
// sizeof is a multiple of 8 == alignof(RunState), so no undeclared tail
// padding). Together these are what make the schema-v8 change a pure append:
// every pre-v8 byte keeps its offset.
static_assert(offsetof(RunState, boss_chest) ==
                  offsetof(RunState, neow_rng) + sizeof(RngStream),
              "boss_chest must start exactly where the RNG block ends -- the "
              "schema-v8 tail append may not move or gap");
static_assert(sizeof(RunState) ==
                  offsetof(RunState, boss_chest) + sizeof(BossChestState),
              "boss_chest must be the final member with no tail padding -- "
              "see the pad member inside BossChestState");

// Bit assignment for RunState::keys. The game stores three independent
// booleans (Settings.hasEmeraldKey / hasRubyKey / hasSapphireKey); the packing
// order here is this engine's own and is append-only like every shared
// encoding.
inline constexpr uint8_t kKeyEmerald = 1u << 0;
inline constexpr uint8_t kKeyRuby = 1u << 1;
inline constexpr uint8_t kKeySapphire = 1u << 2;

}  // namespace sts::engine
