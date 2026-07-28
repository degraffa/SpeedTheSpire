#pragma once

// Relic dungeon pools + RunState acquisition.
//
// Provenance:
//   AbstractDungeon.initializeRelicList (AbstractDungeon.java:1221-1256):
//     populate five tier pools, then one relicRng.randomLong() -> JDK shuffle
//     per tier, unconditionally and in Common/Uncommon/Rare/Shop/Boss order.
//   AbstractDungeon.returnRandomRelicKey/returnEndRandomRelicKey
//     (AbstractDungeon.java:704-819): front/end removal, empty-tier fallback,
//     canSpawn recheck, and the 50/33/17 reward-tier roll.
//   AbstractRelic.instantObtain/obtain (AbstractRelic.java:219-291): append in
//     acquisition order before onEquip; ordinary duplicate ids remain distinct;
//     Circlet duplicates increment the existing Circlet instead.

#include <cstdint>

#include "sts/engine/map_rooms.hpp"   // RoomType (RelicEquipContext.reward_room)
#include "sts/engine/relics.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_deck.hpp"    // MasterBottleKind + the bottle bits
#include "sts/engine/run_state.hpp"

namespace sts::engine {

struct RewardScreen;  // combat_rewards.hpp; RelicEquipContext holds a reference
                      // so this header does not drag the reward layer into
                      // every pool consumer.

enum class RelicPool : uint8_t {
    COMMON = 0,
    UNCOMMON = 1,
    RARE = 2,
    SHOP = 3,
    BOSS = 4,
};

struct RelicSpawnContext {
    uint16_t floor = 0;
    bool in_shop = false;
    bool endless = false;
    // Deck-content canSpawn gates (the Bottled trio). These carry the
    // MASTER-DECK facts the Java gates read; fill_deck_spawn_gates computes
    // them from a RunState. Defaults (false) match the Ironclad starting deck
    // (all-BASIC, no POWER cards) -- and the Bottled canSpawn checks apply even
    // in Endless (no Settings.isEndless clause in those canSpawn bodies).
    bool deck_has_nonbasic_attack = false;  // BottledFlame.canSpawn (:93-99)
    bool deck_has_nonbasic_skill = false;   // BottledLightning.canSpawn (:93-99)
    // BottledTornado.canSpawn (:93-95) -> CardHelper.hasCardType(POWER)
    // (CardHelper.java:80-86): any POWER-type master-deck card, ANY rarity.
    bool deck_has_power = false;
    // Girya / PeacePipe / Shovel canSpawn (Girya.java:538-549 and the two
    // byte-identical siblings): the number of OWNED relics that are one of those
    // three -- each spawns only while that count is < 2. Filled by
    // fill_campfire_relic_count; a draw that does not fill it sees 0, which is
    // the fresh-run answer.
    uint8_t campfire_relic_count = 0;
    // Ectoplasm.canSpawn (Ectoplasm.java:50-53): `actNum <= 1` -- the only
    // act-gated canSpawn in the S1 relic set. Default 1 because S1 IS Act 1
    // (design §6); RunState.act is the producer when a caller has one.
    uint8_t act = 1;
    // BlackBlood.canSpawn (BlackBlood.java:33-36): hasRelic("Burning Blood").
    // Default TRUE, not false -- the Ironclad ALWAYS starts with Burning Blood
    // (Ironclad.getStartingRelics, Ironclad.java:113-115), so `true` is the
    // fresh-run answer in exactly the way `0` is for campfire_relic_count.
    // Filled by fill_boss_spawn_gates.
    bool has_burning_blood = true;
};

// Count the owned Girya / Peace Pipe / Shovel relics into `ctx`. It lives beside
// fill_deck_spawn_gates rather than inside the three canSpawn bodies because
// canSpawn is a PURE predicate over the context (relics/relic_pickup.hpp) -- the
// bodies must not each re-scan RunState.
void fill_campfire_relic_count(const RunState& rs,
                               RelicSpawnContext& ctx) noexcept;

// Fill the BOSS-tier gates from the run: `act` from RunState.act and
// `has_burning_blood` from the owned relic list (BlackBlood.canSpawn). Separate
// from fill_campfire_relic_count for the same reason that one is separate from
// fill_deck_spawn_gates -- canSpawn bodies are PURE predicates over the context
// and must not re-scan RunState themselves.
void fill_boss_spawn_gates(const RunState& rs, RelicSpawnContext& ctx) noexcept;

// Fill the deck-content gates above from the run's master deck. "Non-basic" ==
// CardRarity != BASIC; the only BASIC red rows are Strike/Defend/Bash
// (Strike_Red/Defend_Red/Bash constructors -- CardRarity.BASIC), so the scan
// keys on type + those three ids. deck_has_power is NOT rarity-filtered:
// CardHelper.hasCardType (CardHelper.java:80-86) is a plain type scan, so any
// POWER-type card in the master deck sets it. The registry has 8 POWER cards
// (Combust/Dark Embrace/Evolve/Feel No Pain/Fire Breathing/Inflame/
// Metallicize/Rupture), so the Bottled Tornado gate can open.
void fill_deck_spawn_gates(const RunState& rs, RelicSpawnContext& ctx) noexcept;

enum class RelicAcquireResult : uint8_t {
    ACQUIRED = 0,
    CIRCLET_STACKED = 1,
    INVALID_ID = 2,
    RELIC_CAP_REACHED = 3,
    COUNTER_CAP_REACHED = 4,
    // The id's onEquip is an `on_equip_screen` body and the caller used the
    // plain 3-argument acquire_relic: REFUSED before any mutation. See
    // RelicEquipContext below -- this is the fail-loud half of that design.
    NEEDS_EQUIP_CONTEXT = 5,
};

// --- Screen-opening onEquip bodies (Wave-C track 2 architecture decision) ----
//
// THE PROBLEM. `RelicOnEquipSig` is (RunState&, miscRng, RelicSlot&), and three
// of the five BOSS onEquip bodies need more: Pandora's Box draws cardRandomRng
// (a CombatState-owned floor stream), Tiny House and Calling Bell assemble a
// REWARD SCREEN (cardRng draws included), and Astrolabe / Empty Cage must open
// a master-deck GRID -- i.e. transient screen state no RunState-level callee
// can own. The two candidate shapes were (a) widening RelicOnEquipSig to take
// the RunController, which drags the controller header into every relic pickup
// TU and couples pure bodies to the run loop, or (b) a second, parallel
// dispatch surface whose bodies receive exactly the extra pieces they need and
// REQUEST a screen from the call site instead of opening one themselves.
//
// THE DECISION IS (b), as a generated `pickup: on_equip_screen` surface:
//   * The body gets a RelicEquipContext -- the two missing inputs
//     (cardRandomRng, a RewardScreen to assemble into, plus the room identity
//     the discarded/kept setupItemReward card row rolls under) and two OUT
//     fields that name the screen the call site must now present. The bodies
//     therefore stay ignorant of WHO owns the screen: at Neow (the only S1
//     producer -- the boss swap) the caller translates the request onto
//     NeowState's existing GRID / ITEM_REWARD sub-screens, and a future Act-2
//     boss-chest site translates the same request onto whatever screen state
//     it owns. Re-homing is a call-site change, never a body change.
//   * FAIL-LOUD BY CONSTRUCTION: the plain 3-argument acquire_relic REFUSES
//     (NEEDS_EQUIP_CONTEXT, nothing mutated) any id whose row lists this
//     surface, so an acquisition path that cannot present screens -- claim,
//     shop, chest, event, a bare test -- can never run such a body half-way or
//     silently skip it. claim_reward already treats a non-ACQUIRED result as a
//     refused claim, keeping the item on the screen.
//   * Namespace cost: zero. The grid reuses NeowState (kNeowGridPickCap 2->3
//     and one new NeowGridMode), so the Wave-C RunPhase 11-12 and MoveCat
//     27-29 allocations stay unspent, and the fuzzer's mask-driven NEOW arm
//     covers the new screens with no new MoveCat.
enum class RelicEquipScreen : uint8_t {
    NONE = 0,                    // body completed synchronously; no screen
    GRID_REMOVE = 1,             // Empty Cage: `grid_picks`-pick purge grid
                                 // over the purgeable master deck
    GRID_TRANSFORM_UPGRADE = 2,  // Astrolabe: `grid_picks`-pick grid; each pick
                                 // is transformCard(pick, autoUpgrade=true,
                                 // miscRng) applied when the set completes
    ITEM_REWARD = 3,             // Tiny House / Calling Bell: ctx.rewards now
                                 // holds an assembled claimable screen
    GRID_BOTTLE = 4,             // Bottled Flame/Lightning/Tornado: ONE
                                 // mandatory pick over the purgeable
                                 // type-matched master deck (ctx.bottle names
                                 // which bottle); the pick sets the instance's
                                 // bottle bit (run_deck.hpp). Unlike the boss
                                 // grids this request arises at CLAIM sites
                                 // (reward claim incl. chests, shop purchase),
                                 // so the run layer presents it as the
                                 // phase-independent pending-bottle overlay
                                 // (RunController.pending_bottle), the sim's
                                 // rendering of the game's modal
                                 // gridSelectScreen + RoomPhase.INCOMPLETE
                                 // (BottledFlame.java:49-51).
};

struct RelicEquipContext {
    RngStream& card_random_rng;  // the floor-scoped cardRandomRng
                                 // (CombatState-owned; rc.combat at the run
                                 // layer). Pandora's Box's draws.
    RewardScreen& rewards;       // screen storage the CALLER owns (rc.rewards
                                 // at the run layer). Tiny House / Calling
                                 // Bell assemble into it; untouched otherwise.
    // The room identity CombatRewardScreen.open's setupItemReward card row
    // rolls under (its gate reads the CURRENT room: no row in a TreasureRoom /
    // RestRoom or under an event with noCardsInRewards). At Neow this is
    // kNeowRewardScreenRoom (RoomType::Event -- NeowRoom has an event with the
    // flag at its false default, NeowRoom.java:16-20, AbstractEvent.java:63).
    RoomType reward_room = RoomType::Event;
    // OUT: what the acquisition site must present next. NONE when the body
    // finished synchronously.
    RelicEquipScreen screen = RelicEquipScreen::NONE;
    uint8_t grid_picks = 0;      // GRID_*: how many picks the grid wants
    // OUT (GRID_BOTTLE only): which bottle wants the pick. NONE otherwise.
    MasterBottleKind bottle = MasterBottleKind::NONE;
};

// One master-deck row's eligibility on a bottle's grid:
// getPurgeableCards().getCardsOfType(type) (BottledFlame.java:43/:51 and
// siblings) == rest_card_purgeable AND the kind's card type. NO already-bottled
// exclusion (getPurgeableCards has none; the three grids are type-disjoint
// anyway). Defined in relic_pools.cpp; shared by the bottle onEquip bodies'
// emptiness guard, the overlay's action mask, and the pick dispatch.
[[nodiscard]] bool bottle_pick_legal(const RunState& rs, MasterBottleKind kind,
                                     uint16_t deck_index) noexcept;

// `getPurgeableCards().getX().size() > 0` -- the :41 guard. When false the
// body requests NO screen and the relic stays permanently unbottled.
[[nodiscard]] bool bottle_has_eligible_card(const RunState& rs,
                                            MasterBottleKind kind) noexcept;

// CardGroup.getGroupWithoutBottledCards(getPurgeableCards()) membership for
// one master-deck row (CardGroup.java:1084-1091 over :978-985): purgeable AND
// not bottled. This is the filter of every S1 purge/transform grid the Java
// passes through getGroupWithoutBottledCards -- the shop purge
// (ShopScreen.java:973), Peace Pipe's Toke grid and option gate
// (CampfireTokeEffect.java:57, PeacePipe.java:48), and the event grids
// (Cleric.java:76-78, LivingWall.java:96-103, Bonfire.java:101-102,
// PurificationShrine.java:62, Transmogrifier.java:65,
// GremlinWheelGame.java:286-287, NoteForYourself.java:65,
// GoldenWing.java:110). NOT every grid: Neow's remove/transform
// (NeowReward.java:253-290), Astrolabe/Empty Cage (Astrolabe.java:39,
// EmptyCage.java:45/:55) and every UPGRADE grid (CampfireSmithEffect.java:62,
// LivingWall.java:109) read the un-excluded groups and keep bottled cards
// eligible -- do not widen this predicate onto them.
[[nodiscard]] bool master_card_purgeable_unbottled(
    const CardInstance& card) noexcept;

// Which bottle a relic id is, or NONE for every other relic.
[[nodiscard]] constexpr MasterBottleKind bottle_kind_for_relic(
    RelicId id) noexcept {
    switch (id) {
        case RelicId::BOTTLED_FLAME:
            return MasterBottleKind::FLAME;
        case RelicId::BOTTLED_LIGHTNING:
            return MasterBottleKind::LIGHTNING;
        case RelicId::BOTTLED_TORNADO:
            return MasterBottleKind::TORNADO;
        default:
            return MasterBottleKind::NONE;
    }
}

void initialize_relic_pools(RunState& rs) noexcept;

[[nodiscard]] constexpr RelicTier relic_tier_for_roll(int32_t roll) noexcept {
    return roll < 50 ? RelicTier::COMMON
                     : (roll < 83 ? RelicTier::UNCOMMON : RelicTier::RARE);
}
[[nodiscard]] RelicTier return_random_relic_tier(RunState& rs) noexcept;

[[nodiscard]] RelicId return_random_relic_key(
    RunState& rs, RelicTier tier, RelicSpawnContext ctx = {}) noexcept;
[[nodiscard]] RelicId return_end_random_relic_key(
    RunState& rs, RelicTier tier, RelicSpawnContext ctx = {}) noexcept;

// AbstractDungeon.returnRandomScreenlessRelic(tier) (AbstractDungeon.java:
// 681-689): returnRandomRelicKey, RE-POPPED (never put back) while the key is
// one of the four relics with no screenless presentation -- Bottled Flame /
// Bottled Lightning / Bottled Tornado / Whetstone. Each rejected pop is
// CONSUMED from its pool; canSpawn failures are handled inside
// return_random_relic_key itself (:800-803 end-pop reroute). Two S1 callers:
// event bodies (draw_event_relic rolls its own tier first) and Calling Bell's
// three FIXED-tier rows, which is why the tier is a parameter here rather
// than rolled inside.
[[nodiscard]] RelicId return_random_screenless_relic(
    RunState& rs, RelicTier tier, RelicSpawnContext ctx = {}) noexcept;

[[nodiscard]] bool relic_can_spawn(RelicId id,
                                   RelicSpawnContext ctx) noexcept;

// Whether acquire_relic can complete without hitting an invalid-id, fixed-slot,
// or Circlet-counter capacity guard. A Circlet can still stack while the relic
// array itself is full, provided its existing counter has room.
[[nodiscard]] bool relic_acquire_legal(const RunState& rs,
                                       RelicId id) noexcept;

[[nodiscard]] RelicAcquireResult acquire_relic(
    RunState& rs, RngStream& misc_rng, RelicId id) noexcept;

// The screen-capable overload (see RelicEquipContext above): identical to the
// plain form for every relic WITHOUT an on_equip_screen body, and the ONLY
// door through which one WITH such a body can be acquired. On return, read
// ctx.screen for the screen the body asked the call site to present.
[[nodiscard]] RelicAcquireResult acquire_relic(
    RunState& rs, RngStream& misc_rng, RelicId id,
    RelicEquipContext& ctx) noexcept;

// AbstractPlayer.loseRelic (AbstractPlayer.java:2014-2031): run onUnequip on
// EVERY owned copy of `id`, then remove the LAST one (the loop overwrites
// `toRemove` rather than breaking) and reorganize, which for a plain ArrayList
// removal is order-preserving for the survivors -- so acquisition order, and
// hence trigger order (trap 8), is preserved with one entry deleted. Returns
// false and changes nothing when the run does not own `id`.
//
// NO S1-REACHABLE RELIC LOSS RUNS AN onUnequip BODY, so the per-copy pass is
// named rather than written: the only S1 caller is Neow's boss-relic swap,
// which removes relics[0] -- the Ironclad's Burning Blood, whose only
// override is onVictory (BurningBlood.java:30). The Bottled trio DOES
// override onUnequip in the Java (BottledFlame.java:55-61 clears the chosen
// instance's flag), but that body is INERT in S1 by reachability: bottles
// cannot be owned at floor 0, and no later S1 path loses a relic -- so the
// master-deck bottle bit (run_deck.hpp) has no un-set path and needs none.
// If an Act-2+ relic-loss door ever lands (Ectoparasite-style events, the
// boss-swap chest), its owner adds the onUnequip fan-out here, with the
// bottles' flag-clear as its first S1-relic body.
[[nodiscard]] bool lose_relic(RunState& rs, RelicId id) noexcept;

}  // namespace sts::engine
