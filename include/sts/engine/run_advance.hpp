#pragma once

// run_advance.hpp -- the RUN-LEVEL batch API. This is the
// layer ABOVE combat: run_begin() builds the Neow-pending initial run state,
// advance()/legal_actions() drive the floor loop (map choice -> room
// entry -> combat -> reward/proceed -> next floor), and next_room_transition()
// implements the floor++ / floor-stream reseed (trap 7). Combat itself is
// delegated to the existing CombatState machinery (advance.hpp): while a
// RunController is in the COMBAT phase, PLAY_CARD / END_TURN / CHOOSE are
// dispatched into its embedded CombatState and USE_POTION is routed through the
// RunState-owned slot inventory into the combat potion interpreter.
//
// WHY A SEPARATE CONTROLLER STRUCT (not RunState). RunState is the SAVE-PARITY
// persistent state (schema-versioned, hashed, traced -- re-derive its size with
// sizeof, never from a doc; its layout moves only at a planned schema bump).
// The transient "where am I in the screen/room flow"
// bookkeeping (current map column, run phase, the live combat, the generated
// encounter lists + their consumption cursors) is NOT save state -- the game
// derives it -- so it lives here in RunController, which embeds a RunState by
// value. This keeps RunState's byte layout untouched (no schema bump) while
// giving the run loop the state it needs. RunController is trivially copyable
// (POD) so a heterogeneous batch of them steps with no allocation.
//
// SCOPE / HONEST BOUNDARIES. This layer is the run-loop critical path; ROOM
// CONTENT is largely not modelled yet. What is LIVE here:
//   * run start RNG order (monsterRng lists, relicRng pool-shuffle draws, mapRng
//     map) -> the Neow-pending stream state.
//   * the floor loop + trap-7 reseed at every floor entry.
//   * monster-room combat entry via the encounter framework (encounters.hpp)
//     + fold-back.
//   * COMBAT REWARDS (combat_rewards.hpp): assembly at reward-screen
//     open (gold / elite relic / potion / card rolls with pity + the no-dupe
//     re-roll + the Act-1 zero-chance upgrade draws), then a claim flow over
//     CHOOSE -- claim item, pick/skip/sing on the card screen, proceed. A
//     Smoke Bomb consumes the battle-over draws but offers nothing.
//   * TREASURE ROOMS: fixed-row chest construction consumes the exact size +
//     shared contents rolls on entry; open/skip, chest gold/relic rewards and
//     the three registered chest relic hooks are live.
//   * ESCAPE at the run layer: a combat the pump ended with the room mugged
//     settles the thief's stolen gold against RunState.gold (the game deducted
//     it at steal time), keeps the screen claimable, and suppresses exactly
//     what the game suppresses (RewardOutcome, combat_rewards.hpp); a KILLED
//     thief's stolen gold comes back as a claimable STOLEN_GOLD item.
//   * potion-slot legality/consumption in combat plus Fruit Juice / Entropic
//     Brew run mutations outside combat.
//   * rest sites: Rest/Smith plus the Girya, Peace Pipe and Shovel options,
//     Regal Pillow and Dream Catcher hooks, all through CHOOSE.
//   * NEOW (neow.hpp): the four-category blessing rolled at run start off the
//     event-scoped neow stream, every payout, and the card / master-deck-grid /
//     reward sub-screens they open -- all inside RunPhase::NEOW.
//   * SHOPS (shop.hpp): the merchant's whole stock is built on room entry
//     (cardRng identities, merchantRng prices + sale + relic tiers, potionRng
//     potions, relic-pool END pops), priced through the ascension and shop-relic
//     discount chain, and then bought through CHOOSE; the card-removal service
//     runs off the run-persistent RunState.purge_cost and ramps it. A ? room
//     that rolls SHOP enters the same phase as a map shop node.
//   * the BOSS CHEST (boss_chest.hpp): the boss reward screen's proceed enters
//     a real TreasureRoomBoss through next_room_transition -- floor++, the
//     trap-7 five-stream reseed and the relic room-entry fan-outs all included,
//     because goToTreasureRoom really does run the full transition
//     (ProceedButton.java:179-187 -> nextRoomTransitionStart ->
//     AbstractDungeon.updateFading :2317-2325 -> nextRoomTransition). Entry pops
//     three BOSS relics with no RNG; open / pick / skip / proceed are live, and
//     a picked relic's on_equip_screen body is presented at this site.
//   * the ACT TRANSITION (S2.12): the boss chest's proceed runs
//     dungeonTransitionSetup and constructs the next dungeon --
//     ++act, the cardRng counter snap, the ?-room pity and blizzardPotionMod
//     resets, the A5-or-full heal, then generateMonsters/initializeBoss off the
//     continuing monsterRng, the per-act mapRng (seed + actNum*K), a fresh map
//     with its own setEmeraldElite draw, and the act's BGM miscRng draw -- and
//     lands at MAP_CHOICE on the new act's row 0. Floor numbering is continuous
//     (Act 2 opens at floor 17, Act 3 at 34; see act_floor_base).
//   * the VICTORY terminal: the ACT-3 BOSS kill (RunPhase::RUN_OVER,
//     run_is_victory() true). It opens no reward screen and no chest
//     (AbstractRoom.java:327), so the gold add at :286-297 is the last thing
//     that happens.
// ROOM CONTENT is fully live in Acts 2-3 as of the S2.2x/S2.3x waves: the
// Act-2/3 monsters, elites and bosses landed with S2.21-S2.28, the per-act
// event/shrine lists with S2.13 and the event bodies with S2.31-S2.33, so no
// combat or ? room parks at ROOM_UNIMPLEMENTED anymore (the S2.41 soak
// measured that census cell at 0). The run-layer machinery around them (map,
// lists, streams, floors, rewards, rest/shop/treasure) was act-general all
// along.
// What is DEFERRED (routed to an explicit ROOM_UNIMPLEMENTED / documented seam,
// never faked):
//   * the EMERALD_KEY reward item -- follows the emerald-flag scoping
//     (combat_rewards.hpp).
//   * ? rooms RESOLVE (event_framework.hpp): the one committed eventRng roll
//     picks MONSTER (a real monster combat, consuming monsterList) / SHOP
//     (parks, like a map shop) / TREASURE (the chest flow) / EVENT, and an
//     EVENT result runs the throwaway-stream selection + pool-removal
//     bookkeeping. Implemented bodies open EVENT_DIALOG; a selected event with
//     no body parks at ROOM_UNIMPLEMENTED with the selection committed and the
//     EventId recorded.
//   * monsters outside the implemented roster (see monster_dispatch.hpp): an
//     encounter whose members are not all implemented resolves its composition
//     (miscRng, as the game does) and then parks at ROOM_UNIMPLEMENTED, rather
//     than asserting in spawn_group.
//
// Provenance (read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * AbstractDungeon.nextRoomTransition  (AbstractDungeon.java:1687-1813): the
//     floorNum++ (1741) THEN the 5-stream reseed (1747-1751, trap 7),
//     monsterList/eliteList.remove(0) on room EXIT (1694-1707), onPlayerEntry
//     (1800).
//   * AbstractDungeon.initializeRelicList (AbstractDungeon.java:1221-1256): 5
//     unconditional relicRng.randomLong() pool-shuffle draws.
//   * Exordium ctor / generateMonsters / initializeBoss (Exordium.java:36-221):
//     the run-start monsterRng draw order (via generate_monster_lists).
//   * MonsterRoom.onPlayerEntry (MonsterRoom.java:53-61): getMonsterForRoomCreation
//     -> getEncounter(monsterList.get(0)) -> monsters.init().
//   * AbstractRoom.update turn-1 combat-start block (AbstractRoom.java:236-258):
//     the opening DrawCardAction is queued (:242), then applyStartOfCombatLogic
//     (:245) fires every relic's atBattleStart (AbstractPlayer.java:1892-1901),
//     then applyStartOfTurnRelics (:253) fires atTurnStart. Both dispatches live
//     INSIDE the shared turn-1 block (start_of_turn, action_queue.cpp), in that
//     order, so an immediate atBattleStart body precedes turn 1's atTurnStart
//     while a queued one lands behind the opening draw -- see the dispatch-site
//     comment for the derivation and the G6 §8.0 Stone Calendar witness. The
//     separate pre-draw hook (applyStartOfCombatPreDrawLogic, :241) has no
//     registered relic and so no call site yet.
//   * AbstractRoom.update battle-over (:277-357): the lifecycle transition points
//     (COMPLETE -> reward screen); the reward ASSEMBLY itself lives in
//     combat_rewards.hpp/.cpp and runs from enter_combat_reward.
//   * AbstractDungeon.<init> (AbstractDungeon.java:268-308) + dungeonTransitionSetup
//     (:2562-2604) + AbstractPlayer.<init> (AbstractPlayer.java:201-221) +
//     AbstractPlayer.initializeStarterDeck (:357-394): the run-setup ascension
//     modifier order -- see the block comment on run_setup_max_hp below.

#include <cstddef>  // offsetof (the S2.43 playtime-carve proof below RunController)
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include "sts/engine/advance.hpp"       // ActionMask, StepResult, combat advance/legal_actions
#include "sts/engine/boss_chest.hpp"    // BossChestState / the post-boss chest
#include "sts/engine/cards.hpp"         // kColorlessPoolCount (colorless_order)
#include "sts/engine/combat_rewards.hpp"  // RewardScreen + the claim flow
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"    // MonsterLists
#include "sts/engine/event_framework.hpp"  // EventDialogState / kEventOptionCap
#include "sts/engine/interp.hpp"        // mathutils_round (run_setup_hp)
#include "sts/engine/knowledge.hpp"     // KnowledgeState (player-information layer)
#include "sts/engine/map_rooms.hpp"     // RoomType
#include "sts/engine/neow.hpp"          // NeowState / the blessing screens
#include "sts/engine/rest_sites.hpp"    // RestSiteState / menu constants
#include "sts/engine/run_state.hpp"
#include "sts/engine/shop.hpp"          // ShopState / merchant stock + purge
#include "sts/engine/treasure_rooms.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// --- RunPhase ---------------------------------------------------------------

// The run-level state machine. One enum, all run phases (the run-layer analogue
// of CombatPhase). The value-init default is NONE.
enum class RunPhase : uint8_t {
    NONE = 0,
    NEOW = 1,              // floor 0, the Neow blessing and its payout screens.
    MAP_CHOICE = 2,        // choosing the next map node (an outgoing edge).
    COMBAT = 3,            // inside a combat; delegates to the embedded CombatState.
    COMBAT_REWARD = 4,     // post-combat reward screen (claim / pick / proceed).
    ROOM_UNIMPLEMENTED = 5,// entered a room kind / encounter not yet implemented.
    RUN_OVER = 6,          // terminal: the player died (combat DEFEAT, or an
                           // event kill with outcome NONE), or the VICTORY.
                           // The victory terminal has moved twice as the rooms
                           // behind it were modelled: S1 put it at the Act-1
                           // boss REWARD screen's proceed, S2.11 at the boss
                           // CHEST's proceed, and S2.12 at its real place --
                           // the ACT-3 BOSS kill, which opens no reward screen
                           // at all (AbstractRoom.java:327, s2-design §1). The
                           // Act-1/2 chest proceeds are now act TRANSITIONS,
                           // not terminals. run_is_victory() (below) tells a
                           // win from a death; no separate phase value is
                           // spent on it.
    REST_SITE = 7,         // campfire menu / grid / Dream Catcher card pick.
    TREASURE_ROOM = 8,     // unopened Act-1 non-boss chest (open or skip).
    EVENT_DIALOG = 9,      // a ?-room resolved to an event with a live dialog
                           // body (event_framework.hpp). Most native events
                           // have one (the events.yaml rows marked
                           // `implemented: true`); a selected event with NO
                           // registered body parks at ROOM_UNIMPLEMENTED
                           // instead, with the selection bookkeeping committed
                           // and the EventId retained in rc.event. Value 9 is
                           // the ledger's reserved allocation (stage-b-tasks.md
                           // shared-namespace table); gaps are legal, values
                           // are append-only and never renumbered.
    SHOP = 10,             // the merchant's floor, or its card-removal grid
                           // (shop.hpp). Reached from a static ShopRoom map
                           // node AND from a ? room whose eventRng roll came up
                           // SHOP -- both build the same merchant. Value 10 is
                           // the ledger's reserved allocation (stage-b-tasks.md
                           // shared-namespace table).
    BOSS_TREASURE = 11,    // the post-boss chest room (TreasureRoomBoss): the
                           // three entry-popped boss relics, the pick/skip
                           // screen, a picked relic's onEquip screens, and the
                           // proceed that ends the act -- which since S2.12 is
                           // the ACT TRANSITION, not the run's end
                           // (boss_chest.hpp, on_boss_chest_proceed). Value
                           // 11 is the S2 Wave-2 allocation (docs/
                           // stage-b-tasks.md "S2 Wave-2 allocations"); values
                           // are append-only and never renumbered.
};

// Why combat ended. AbstractRoom keeps two independent end-of-battle room
// flags -- `mugged` (a thief ran: Looter.takeTurn case ESCAPE sets it
// synchronously, Looter.java:128 -> kCombatFlagMugged) and `smoked` (the
// player threw Smoke Bomb, SmokeBomb.java:40) -- and the battle-over block
// picks the screen by checking mugged FIRST (AbstractRoom.update:334-341):
// mugged -> openCombat(TEXT[0]), which STILL assembles claimable items
// (CombatRewardScreen.java:280-285); smoked -> openCombat(TEXT[1], true), a
// bare proceed screen; otherwise the ordinary open(). So MUGGED outranks
// SMOKE_BOMB when both flags are set (the thief escaped, then the player
// smoke-bombed past the survivors). DEFEAT transitions to RUN_OVER instead.
//
// What a MUGGED screen actually offers is NOT a function of this value alone:
// the gold and potion-chance gates read MonsterGroup.haveMonstersEscaped
// (every record escaped, MonsterGroup.java:124-130), not the mug flag -- see
// RewardOutcome (combat_rewards.hpp) for the split.
//
// Value 5 is allocated but deliberately unspent: the only other escape shape
// -- haveMonstersEscaped without a mug, i.e. a non-thief escape -- has no
// Act-1 producer (the gremlins' move-99 escape has no reachable trigger in
// the Exordium-only registry) and lands with whoever implements that trigger.
// Values are append-only; 0-3 are never renumbered.
enum class RunCombatOutcome : uint8_t {
    NONE = 0,
    KILLED = 1,
    SMOKE_BOMB = 2,
    DEFEAT = 3,
    MUGGED = 4,   // the room was mugged: a thief escaped, its stolen gold went
                  // with it (settled against RunState.gold at combat end).
};

// Which relic's raw-master-deck 1-pick grid is up on the pending-deck-pick
// overlay (RunController.pending_deck_pick). Values are append-only; NONE is
// the value-init default, which is what lets the field live in a former
// padding byte without moving any hash.
enum class EquipDeckPick : uint8_t {
    NONE = 0,
    DOLLYS_MIRROR = 1,  // DollysMirror.onEquip (DollysMirror.java:33-42): one
                        // mandatory pick over the UNFILTERED master deck; the
                        // pick is duplicated into the deck
                        // (relic_dollys_mirror_duplicate, relic_pools.hpp).
};

// CHOOSE arg0 sentinel for "take the boss edge" at a MAP_CHOICE (the boss node is
// not a grid column, so it needs a value outside 0..kMapCols-1).
inline constexpr uint8_t kChooseBoss = static_cast<uint8_t>(kMapCols);  // 7

// cur_x sentinel: the controller is at Neow (floor 0), with no grid column yet.
inline constexpr uint8_t kNeowColumn = 0xFF;

// emerald_x / emerald_y sentinel: the act placed no burning elite (only
// possible with zero Elite nodes, which the Act-1 quota never produces).
inline constexpr uint8_t kNoEmeraldNode = 0xFF;

// CHOOSE arg0 sentinels at a COMBAT_REWARD. Small arg0 values are claim /
// pick indices, so the named buttons live at the top of the u8 range:
//   * kChooseProceed -- the Proceed button: leave the screen for the map,
//     abandoning anything unclaimed (an assembled-but-skipped relic stays
//     popped from its pool, exactly as in the game).
//   * kChooseSkipCard -- the card screen's Skip: close the pick screen, the
//     CARD item stays claimable.
//   * kChooseSing -- the Singing Bowl button: +2 max HP instead of a card.
// Neow's card screen reuses kChooseSkipCard and kChooseSing (the game opens the
// same CardRewardScreen), and its finished-payout screen reuses kChooseProceed.
// kChooseSkipCard itself is DEFINED in advance.hpp (included above): the Skip
// button also exists on the in-combat typed-DISCOVERY screen, so the sentinel
// lives at the lowest layer that consumes it.
inline constexpr uint8_t kChooseProceed = 0xFF;
inline constexpr uint8_t kChooseSing = 0xFD;
inline constexpr uint8_t kChooseCancelGrid = 0xFC;
inline constexpr uint8_t kChooseOpenChest = 0;

// CHOOSE arg0 layout on the shop floor (RunPhase::SHOP, ShopScreenKind::MENU).
// One dense index space over every purchasable row plus the purge service, so
// a single `can_buy_shop_item[]` covers the screen; kChooseProceed leaves.
// The bases are FIXED, not derived from what is left in stock -- a bought row
// keeps its index for the rest of the visit (see ShopSlot's note on why slots
// are not compacted). On ShopScreenKind::PURGE_GRID the arg0 is a master-deck
// index instead, through can_choose_master_deck[].
inline constexpr uint8_t kChooseShopColoredBase = 0;    // 0..4
inline constexpr uint8_t kChooseShopColorlessBase =
    kChooseShopColoredBase + kShopColoredCount;         // 5..6
inline constexpr uint8_t kChooseShopRelicBase =
    kChooseShopColorlessBase + kShopColorlessCount;     // 7..9
inline constexpr uint8_t kChooseShopPotionBase =
    kChooseShopRelicBase + kShopRelicCount;             // 10..12
inline constexpr uint8_t kChooseShopPurge =
    kChooseShopPotionBase + kShopPotionCount;           // 13
inline constexpr int kShopItemCount = kChooseShopPurge; // 13 purchasable rows

// --- RunController -----------------------------------------------------------

// RunController.colorless_order's slot count: kColorlessPoolCount rounded up
// to a multiple of four u16s (one 8-byte unit), so adding the field moves no
// tail-padding arithmetic. The surplus slots stay 0 and are never read.
inline constexpr int kColorlessOrderCap =
    ((kColorlessPoolCount + 3) / 4) * 4;

// THE LIVE-PURSE GOLD TRACKER (S2.48). The game moves the purse DURING combat:
// each thief steal deducts min(goldAmt, player.gold) the instant its queued
// accrual + 3-arg DamageAction resolve (Looter$1/Mugger$1 + DamageAction.
// stealGold, DamageAction.java:98-114), and a Hand of Greed kill pays
// player.gainGold at the kill (GreedAction.java:37-38). The combat layer has no
// RunState (combat_gold's boundary), so the RUN layer applies both to
// RunState.gold at the first command boundary after they happen --
// sync_live_gold, called after every in-combat step and again inside
// fold_back_combat. This struct is that sync's bookkeeping: what has already
// been applied, so a second look applies nothing twice.
//
// Combat-scoped TRANSIENT state on the KnowledgeState precedent: it lives
// HERE, not in CombatState/RunState (whose byte layouts are hashed against
// committed fixtures / are save-parity), is reset by enter_combat, and is
// stale-but-unread outside COMBAT. A memcpy of the controller snapshots it
// like every other member, so mid-combat snapshot/resume replays identically.
struct StolenGoldLive {
    // Per monster SLOT: the gold this thief has actually taken, each steal
    // clamped against the live purse at its own boundary -- the engine's copy
    // of the Java's private per-thief `stolenGold` accrual (Looter.java:57 /
    // Mugger.java:55). Non-thief slots stay 0.
    int32_t taken[kMonsterCap];
    // How much of CombatState.combat_gold has already been banked into
    // RunState.gold. combat_gold only grows while a combat is live (the one
    // producer is op_damage_greed), so `combat_gold - banked` is exactly the
    // not-yet-applied gain; fold_back_combat zeroes both together.
    uint16_t banked;
    // Per monster SLOT: the steal count (MonsterState.pad0) as of the last
    // sync -- counts above this are steals not yet charged to the purse.
    uint8_t prev_steals[kMonsterCap];
    uint8_t pad[3];  // explicit padding: the controller is byte-hashed.
};
static_assert(static_cast<int>(sizeof(StolenGoldLive)) ==
                  kMonsterCap * 4 + 2 + kMonsterCap + 3,
              "StolenGoldLive layout: kMonsterCap*i32 + u16 + kMonsterCap*u8 "
              "+ 3 pad, no implicit padding");
static_assert(sizeof(StolenGoldLive) % 8 == 0,
              "StolenGoldLive is a whole number of 8-byte units, so placed "
              "8-aligned at RunController's tail it adds no implicit padding");

// The whole run-loop state: a RunState (persistent) + a CombatState (the live
// combat / the canonical floor-stream holder) + the transient screen-flow
// bookkeeping. Trivially copyable so a batch of runs steps with no allocation.
struct RunController {
    RunState run;           // persistent save-parity state (map/deck/relics/streams).
    CombatState combat;     // live combat (COMBAT/COMBAT_REWARD); also holds the
                            // reseeded floor streams for every room (design §3.4).

    uint8_t phase;          // RunPhase.
    uint8_t cur_x;          // current map column 0..kMapCols-1; kNeowColumn at Neow.
    uint8_t room_type;      // current RoomType (also identifies an unimplemented stall).
    uint8_t combat_outcome; // RunCombatOutcome; meaningful on reward/run-over.

    // The run's generated encounter-key lists (monsterRng) + per-list
    // consumption cursors. A monster room uses monster_list[monster_cursor], an
    // elite uses elite_list[elite_cursor], the boss uses boss_list[boss_cursor];
    // the cursor advances when the room is LEFT (nextRoomTransition remove(0)).
    // CardCrawlGame.playtime -- WALL-CLOCK seconds since the run started
    // (CardCrawlGame.java:177) -- CARVED OUT OF THE 4 BYTES that MonsterLists'
    // 8-byte alignment (std::string_view) inserts here and that used to be
    // `pad_lists_align[4]` (S2.43, 2026-08-27). Those bytes were
    // `ByteClass::PADDING` and `{}`-zeroed on every path that produces a
    // RunController, so NO OFFSET MOVES, `sizeof(RunController)` is unchanged,
    // and -- 0.0f being four zero bytes -- every controller that hashed a
    // value before hashes the same value now. Same terms as the S2.13
    // `event_flags_hi` carve (run_state.hpp) and the CombatState carve-outs.
    //
    // An INPUT, never advanced by the engine. The simulator has no clock and
    // must not grow one (determinism in (seed, actions) is what everything
    // else rests on), so this stays 0.0f for every simulator trajectory. The
    // one rule that reads it is SecretPortal's getShrine gate
    // (AbstractDungeon.java:1929-1933); an ORACLE REPLAY sets it per record
    // from the capture's `oracle.playtime` so an Act-3 shrine draw reproduces
    // the game's index. Full write-up: event_framework.hpp's PLAYTIME block.
    //
    // The offset/size arithmetic is ASSERTED below the struct, not trusted.
    float playtime_seconds{0.0f};
    MonsterLists lists;
    uint8_t monster_cursor;
    uint8_t elite_cursor;
    uint8_t boss_cursor;
    uint8_t pad1;           // explicit padding (hashed by the fuzz soak).
    // The act's burning (emerald-key) elite node, copied from the placement
    // draw's RoomAssignment at run_begin (map_rooms.hpp step 5). Transient
    // derived state exactly like the cursors -- the game re-derives the flag
    // from (seed, mapRng) at map generation -- so it lives here, never in the
    // frozen RunState. Entering THIS elite node rolls mapRng.random(0, 3) and
    // buffs the group (MonsterRoomElite.applyEmeraldEliteBuff,
    // MonsterRoomElite.java:39-68; gate at AbstractPlayer.java:1603).
    uint8_t emerald_x;      // column, or kNoEmeraldNode
    uint8_t emerald_y;      // row (floor - 1), or kNoEmeraldNode
    uint8_t pad_emerald[2]; // explicit padding

    // The live COMBAT_REWARD screen: assembled once when the reward
    // screen opens, mutated by claims. Transient screen-flow state exactly like
    // the cursors above -- the game derives it too (a POST_COMBAT save re-rolls
    // the cards from the saved cardRng counter), so RunState stays untouched
    // (no new storage, no schema bump).
    RewardScreen rewards;
    RestSiteState rest;

    // The live merchant while phase == SHOP. Transient for the same reason as
    // everything else here: the game rebuilds a shop's whole stock from (seed,
    // merchantRng.counter) on reload, so it is derived state. The one piece the
    // game DOES persist -- the ramping purge cost -- is already a RunState
    // field, not a member here.
    ShopState shop;

    // The constructor-time chest rolls while phase == TREASURE_ROOM, retained
    // through its reward screen for replay/hash observability. Transient like
    // RewardScreen; never added to the frozen RunState schema.
    TreasureChest treasure_chest;

    // The post-boss chest state -- the three relics popped at room entry, which
    // screen is up, and TreasureRoomBoss.choseRelic -- is NOT here: it lives in
    // `run.boss_chest` (run_state.hpp, schema v8 / S2.47). It used to be a
    // transient member beside treasure_chest, but the three offers are what
    // design §6 S2-G2 item 2 diffs, and the translator/differ see only
    // RunState/CombatState -- so the storage moved rather than being mirrored,
    // keeping exactly ONE source of truth. The room-flow functions are still
    // boss_chest.hpp's.

    // The live event dialog while phase == EVENT_DIALOG; also carries the
    // selected EventId while parked at ROOM_UNIMPLEMENTED for a
    // resolved-but-unimplemented event, so the selection is observable,
    // deterministic and hash-stable. Transient (the game re-derives the event
    // from (seed, eventRng.counter) on reload -- EventRoom.java:28); never in
    // RunState.
    EventDialogState event;

    // The floor-0 Neow blessing: the four rolled options and which of its
    // screens is up. Transient for the same reason as everything above it --
    // NeowEvent rebuilds the whole blessing from `new Random(Settings.seed)`
    // (NeowEvent.java:363), so it is derived state, never saved.
    NeowState neow;

    // The PENDING-BOTTLE OVERLAY: MasterBottleKind (run_deck.hpp), non-NONE
    // while a just-acquired bottle relic's mandatory 1-pick card grid is up.
    // A Bottled relic's onEquip opens the grid AT THE CLAIM SITE -- combat /
    // chest / event reward claim, shop purchase -- over whatever screen is
    // showing, and parks the room INCOMPLETE until the pick
    // (BottledFlame.java:41-53). That is a MODAL overlay orthogonal to
    // RunPhase, so it lives here rather than inside any one phase's screen
    // struct: while non-NONE, legal_actions offers ONLY the eligible
    // can_choose_master_deck[] rows (bottle_pick_legal) and step_one applies
    // the pick before any phase dispatch. Transient like every screen field
    // above; the phase itself never changes while the overlay is up.
    uint8_t pending_bottle;

    // THE PENDING-DECK-PICK OVERLAY: EquipDeckPick (below), non-NONE while a
    // just-acquired relic's mandatory 1-pick grid over the RAW master deck is
    // up. Dolly's Mirror is its only member (DollysMirror.java:33-42): the
    // grid it opens is neither purgeable-filtered nor type-filtered, so it
    // cannot share bottle_pick_legal, but it is the same SHAPE as the
    // pending-bottle overlay above -- raised at a claim/purchase site, modal
    // over whatever screen is showing, cancel-less, room parked INCOMPLETE
    // until the pick -- and so it is carried the same way: legal_actions
    // offers ONLY master-deck rows while it is non-NONE, and step_one applies
    // the pick before any phase dispatch. The phase never changes while it is
    // up, which is how the player lands back on the merchant afterwards.
    //
    // It occupies ONE BYTE OF THE FORMER pad2, deliberately: the controller is
    // byte-hashed, an explicit pad is value-init zeroed, and EquipDeckPick's
    // NONE is 0 -- so every state that did not raise this overlay hashes
    // exactly as it did before the field existed, and no fixture moves.
    uint8_t pending_deck_pick;
    uint8_t pad2[2];  // explicit padding (value-init zeroed, hash-stable).

    // THE LIVE colorlessCardPool's ORDER (CardId as u16 per slot; slots at
    // index >= kColorlessPoolCount are 0 and never read -- the cap is rounded
    // up so the field is a whole number of 8-byte units and the tail-padding
    // arithmetic is untouched). AbstractDungeon.returnColorlessCard(rarity)
    // shuffles `colorlessCardPool.group` IN PLACE with a
    // shuffleRng.randomLong-seeded JDK shuffle and reads the first match
    // (AbstractDungeon.java:1100-1113), so the order PERSISTS into the next
    // reader -- observable from the second same-act draw on (Knowing Skull can
    // buy several colorless cards in one visit; Match and Keep's A<15 slot is
    // the other consumer). It lives HERE, not in RunState, because the game
    // itself rebuilds the pool to plain library order on save/load
    // (initializeCardPools runs in every dungeon constructor, :294), i.e. it
    // is genuinely session-transient, not save-parity -- the same argument as
    // every screen field above. Initialized to live library order (the
    // REVERSED emitted kColorlessPool -- see event_draw_colorless_uncommon's
    // provenance) at run_begin and again at every act crossing.
    uint16_t colorless_order[kColorlessOrderCap];

    // The player-information layer's in-combat knowledge (knowledge.hpp):
    // draw-order constraints + revealed monster construction rolls. It lives
    // HERE -- not in CombatState/RunState, whose byte layouts are hashed
    // against committed fixtures -- following this struct's own precedent for
    // transient derived state; a memcpy of the controller snapshots it like
    // every other field. Combat-scoped: reset by enter_combat's
    // knowledge_reset(), stale-but-unread outside COMBAT. The engine's event
    // hooks reach it through the KnowledgeScope attachment that step dispatch
    // and enter_combat set (knowledge.hpp's attachment contract).
    KnowledgeState knowledge;

    // The 2 bytes stolen_live's 4-alignment inserts after knowledge (which
    // ends 2 mod 4). DECLARED for the same reason CombatState.pad_monsters is:
    // RunController is memcpy'd and memcmp'd (the resample/twin suites compare
    // whole controllers), and conventions §8's incident is precisely an
    // undeclared gap in a byte-compared struct. These are the SAME two bytes
    // the pre-S2.48 layout declared as `pad_tail` (T0.5): stolen_live ends
    // 8-aligned, so the tail pad moved here rather than being joined by a new
    // gap, and sizeof(RunController) grew by exactly sizeof(StolenGoldLive).
    uint8_t pad_live_align[2];

    // The live-purse gold tracker (see StolenGoldLive above): combat-scoped
    // transient bookkeeping for the steal-time / greed-kill-time RunState.gold
    // updates, on the KnowledgeState placement precedent. 4-aligned and a
    // whole number of 8-byte units (both static_asserted at the struct), so
    // the struct ends flush -- no implicit tail padding (the T0.5 tripwire
    // and the tiling static_assert in byte_class.hpp keep that true).
    StolenGoldLive stolen_live;
};

static_assert(std::is_trivially_copyable_v<RunController>,
              "RunController must be trivially copyable (POD batch entry)");

// The S2.43 playtime carve (see the member's comment): it must land exactly
// where `pad_lists_align[4]` did -- 4-aligned, 4 bytes wide, immediately ahead
// of `lists` with no gap. If either assert fires, the carve does not fit and
// the answer is to surface the extra storage deliberately, not to reorder
// members.
static_assert(sizeof(float) == 4,
              "the playtime carve assumes a 4-byte float");
static_assert(offsetof(RunController, playtime_seconds) % 4 == 0,
              "playtime_seconds is not 4-aligned -- the S2.43 pad carve does "
              "not fit; see the member's comment");
static_assert(offsetof(RunController, playtime_seconds) + sizeof(float) ==
                  offsetof(RunController, lists),
              "the playtime carve must consume exactly the alignment slack "
              "ahead of `lists` -- no gap may open behind it");

// Native event combats reuse the ordinary combat constructor while preserving
// the already-advanced floor streams and keeping RoomType::Event. The latter is
// what prevents next_room_transition from consuming monster_cursor and what
// selects EventRoom reward semantics. LAGAVULIN_AWAKE is the one constructor
// variant MonsterHelper's "Lagavulin Event" key requests.
enum class EventCombatVariant : uint8_t {
    NONE = 0,
    LAGAVULIN_AWAKE = 1,
};

// `elite_trigger` carries AbstractRoom.eliteTrigger for the ONE Act-1 event that
// sets it on its own EventRoom before fighting: DeadAdventurer.java:116. It
// reaches CombatState as kCombatFlagEliteRoom (combat_state.hpp), which is what
// makes Sling of Courage / Preserved Insect / Slaver's Collar fire on that
// event combat exactly as they do in a MonsterRoomElite.
[[nodiscard]] bool enter_event_combat(
    RunController& rc, std::string_view encounter_key,
    EventCombatVariant variant = EventCombatVariant::NONE,
    bool elite_trigger = false) noexcept;

// --- RunActionMask -----------------------------------------------------------

// The run-level legal-action set (the run analogue of ActionMask). Which fields
// are meaningful depends on `phase`:
//   NEOW                 : depends on rc.neow.screen -- BLESSING offers
//                          can_choose_neow_option[0..3] (CHOOSE i); CARD_REWARD
//                          offers can_take_card / can_skip_card / can_sing;
//                          a card-select GRID offers
//                          can_choose_master_deck[i], while Pandora / Calling
//                          Bell confirmation grids offer can_proceed;
//                          ITEM_REWARD offers can_claim_reward[i] + can_proceed;
//                          DONE offers can_proceed, which opens the map.
//   TREASURE_ROOM        : can_open_chest (CHOOSE kChooseOpenChest) and
//                          can_proceed (CHOOSE kChooseProceed) to skip it.
//   BOSS_TREASURE        : depends on rc.run.boss_chest.screen (boss_chest.hpp) --
//                          CLOSED offers can_open_chest (CHOOSE
//                          kChooseOpenChest) + can_proceed (leave without
//                          picking, the noPick path); RELIC_SELECT offers
//                          can_claim_reward[0..2] (CHOOSE i, one per offered
//                          relic) + can_cancel_grid (CHOOSE kChooseCancelGrid,
//                          the screen's cancel button = SKIP) and NOT
//                          can_proceed, because bossRelicScreen.open hides the
//                          proceed button (BossRelicSelectScreen.java:354);
//                          EQUIP_GRID offers can_choose_master_deck[i] (or
//                          can_proceed for the two choice-free confirmation
//                          grids); EQUIP_ITEM_REWARD offers
//                          can_claim_reward[i] + can_proceed; DONE offers
//                          can_proceed, which is the ACT TRANSITION (S2.12:
//                          it opens the NEXT act's map, and only the Act-3
//                          boss ends the run). NO new mask
//                          field is spent -- the per-phase reading of these
//                          flags is the same convention NEOW and REST_SITE use.
//   COMBAT_REWARD        : with no card screen open -- can_proceed (CHOOSE
//                          kChooseProceed) + can_claim_reward[i] (CHOOSE i);
//                          with a CARD item open -- can_take_card[j] (CHOOSE j),
//                          can_skip_card (kChooseSkipCard), can_sing
//                          (kChooseSing); proceed is NOT legal until the pick
//                          screen closes.
//   MAP_CHOICE           : can_choose_node[x] over next-row columns + can_choose_boss.
//                          The CHOOSE action's arg0 is the destination column
//                          (0..kMapCols-1) or kChooseBoss.
//   COMBAT               : `combat` holds PLAY_CARD / END_TURN / CHOOSE;
//                          run-owned potion masks below hold USE_POTION.
//   REST_SITE            : menu buttons, a Smith/Toke master-deck grid (cards
//                          plus can_cancel_grid), or Dream Catcher's direct
//                          card-pick screen.
//   EVENT_DIALOG         : can_choose_event_option[i] (CHOOSE i) over a dialog,
//                          or can_choose_master_deck[i] over an event grid.
//   SHOP                 : can_buy_shop_item[i] (CHOOSE i) over the fixed row
//                          layout, can_purge (CHOOSE kChooseShopPurge) and
//                          can_proceed (CHOOSE kChooseProceed); once the purge
//                          grid is open, can_choose_master_deck[i] plus
//                          can_cancel_grid.
//   ROOM_UNIMPLEMENTED / RUN_OVER : nothing legal (the run is parked/terminal).
struct RunActionMask {
    uint8_t phase;                     // RunPhase echo (== controller.phase).
    bool can_proceed;                  // NEOW / TREASURE_ROOM / reward proceed.
    // NEOW, blessing screen: the four dialog buttons NeowEvent.blessing built
    // (NeowEvent.java:372-376). CHOOSE arg0 is the option index.
    bool can_choose_neow_option[kNeowOptionCount];
    bool can_open_chest;               // TREASURE_ROOM: open the fixed-row chest.
    bool can_choose_node[kMapCols];    // MAP_CHOICE: legal next-node columns.
    bool can_choose_boss;              // MAP_CHOICE: the boss edge is available.
    // COMBAT_REWARD. Claim legality mirrors RewardItem.claimReward's
    // failure cases (a POTION with full slots and no Sozu is not claimable).
    bool can_claim_reward[kRewardItemCap];
    bool can_take_card[kRewardCardCap];
    bool can_skip_card;
    bool can_sing;
    // REST_SITE. Menu indices preserve CampfireUI's insertion order; Smith and
    // Toke grids address the stable master-deck index directly. Dream Catcher
    // reuses the card-pick fields above.
    bool can_choose_rest[kRestOptionCap];
    bool can_choose_master_deck[kMasterDeckCap];
    bool can_cancel_grid;  // Smith/Toke/shop-purge modal -> parent menu
    // EVENT_DIALOG: the current screen's options, rebuilt from the event's
    // build_menu on every call (never cached). CHOOSE arg0 is the option index.
    bool can_choose_event_option[kEventOptionCap];
    // SHOP, menu screen: one flag per purchasable row, indexed by the
    // kChooseShop*Base layout above; can_purge opens the removal grid and
    // can_proceed leaves for the map. The grid screen instead uses
    // can_choose_master_deck[] and offers nothing else -- the same shape the
    // rest site's Smith/Toke grids use. can_cancel_grid is their and the shop
    // purge grid's Cancel button, encoded as CHOOSE(kChooseCancelGrid).
    bool can_buy_shop_item[kShopItemCount];
    bool can_purge;
    // USE_POTION owns a RunState slot, so its legality lives at this layer.
    // Fruit Juice and Entropic Brew can be used in stable non-combat phases.
    // During COMBAT, can_use_potion_target[slot][monster] enumerates target-
    // required potions; non-target potions use can_use_potion[slot] directly.
    bool can_use_potion[kPotionCap];
    bool can_use_potion_target[kPotionCap][kMonsterCap];
    // DISCARD_POTION (the belt's throw-away button) is far wider than USE: it
    // needs no target, no implemented body and no combat, because
    // TopPanel.destroyPotion just empties the slot. Its only gate is
    // AbstractPotion.canDiscard (AbstractPotion.java:398-400) -- refused inside
    // a We Meet Again dialog, allowed everywhere else -- so this is a separate
    // flag rather than a mode of can_use_potion, none of whose gates (an
    // implemented body, a live target, the FRUIT_JUICE/ENTROPIC_BREW
    // out-of-combat pair) apply here.
    bool can_discard_potion[kPotionCap];
    ActionMask combat;                 // COMBAT: delegated combat legal actions.
};

static_assert(std::is_trivially_copyable_v<RunActionMask>);

// --- Run-setup ascension modifiers (registry/a20.yaml rows 5, 6, 10, 11, 14) --
//
// THE APPLICATION ORDER IS LOAD-BEARING, and it is derived from the game's own
// call graph rather than from the order any table happens to list the levels in:
//
//   1. AbstractPlayer.<init> (AbstractPlayer.java:211-213): the potion-slot loss,
//      over the field default potionSlots = 3 (AbstractPlayer.java:144). This is
//      a CHARACTER constructor -- it runs before the dungeon object exists, so it
//      is the earliest of the run-setup modifiers. Exposed as potion_slot_count()
//      (potions.hpp), where the potion layer owns it.
//   2. AbstractDungeon.<init> (AbstractDungeon.java:287) calls
//      dungeonTransitionSetup (:2562-2604), whose floorNum <= 1 && Exordium block
//      applies, in THIS order:
//        a. the between-act heal (:2582-2586) -- which is ahead of the block, not
//           in it. At run start the sheet is at full HP (80/80, Ironclad.java:114)
//           so BOTH of its branches are no-ops; its position still matters,
//           because sitting ahead of (b) is what stops it ever seeing a reduced
//           max HP.
//        b. the max-HP loss (:2591-2593) via decreaseMaxHealth
//           (AbstractCreature.java:211-223), which subtracts, floors max HP at 1,
//           and CLAMPS current HP down to the new max. The amount is per
//           character: Ironclad.getAscensionMaxHPLoss (Ironclad.java:168-170)
//           returns 5.
//        c. current HP = MathUtils.round(max HP * 0.9f) (:2594-2596) -- taken of
//           the ALREADY-REDUCED max. An Ironclad at ascension 20 therefore starts
//           68/75: not 72/75 (which is what applying (c) before (b) would give)
//           and not 72/80.
//        d. masterDeck.addToTop(new AscendersBane()) (:2597-2600).
//   3. AbstractDungeon.<init> calls p.initializeStarterDeck only afterwards
//      (:295-296), so the starting 5 Strike / 4 Defend / 1 Bash are appended
//      AFTER the curse and Ascender's Bane is master-deck index 0.

// The base Ironclad sheet (Ironclad.getLoadout's CharSelectInfo, Ironclad.java:
// 113-115): 80 max HP at full, 99 gold, 5-card draw.
inline constexpr int kIroncladBaseMaxHp = 80;
inline constexpr int kIroncladBaseGold = 99;

// Ironclad.getAscensionMaxHPLoss (Ironclad.java:168-170).
inline constexpr int kIroncladAscensionMaxHpLoss = 5;

// libGDX MathUtils.round (MathUtils.java:233-235) is `mathutils_round`, defined
// once in interp.hpp beside its twin mathutils_floor. NOT std::round: it is a
// floor of (v + 0.5) via the 16384 bias, so exact .5 cases go UP and negatives
// round differently. 90 % of 75 lands on exactly 67.5f, so the tie behaviour is
// load-bearing here -- see run_setup_hp below.

// Ironclad max HP after the run-setup max-HP loss, and the current HP after the
// 90 %-of-max rewrite that follows it. Pure, so the tier-2 rows can be checked
// without walking a whole run_begin.
[[nodiscard]] constexpr int run_setup_max_hp(int ascension) noexcept {
    return ascension >= 14 ? kIroncladBaseMaxHp - kIroncladAscensionMaxHpLoss
                           : kIroncladBaseMaxHp;
}
[[nodiscard]] constexpr int run_setup_hp(int ascension) noexcept {
    const int max_hp = run_setup_max_hp(ascension);
    return ascension >= 6 ? mathutils_round(static_cast<float>(max_hp) * 0.9f)
                          : max_hp;
}

// Whether the run starts with the Ascender's Bane curse in the master deck.
[[nodiscard]] constexpr bool run_setup_has_starting_curse(int ascension) noexcept {
    return ascension >= 10;
}

// --- API ---------------------------------------------------------------------

// Build the Neow-pending initial run state for (seed, ascension). Reproduces the
// dungeon-init RNG order so the run streams are at their post-init counters
// before Neow: monsterRng advanced by the encounter-list generation, relicRng by
// the 5 pool-shuffle draws, mapRng at end-of-generateMap (the full act map is
// written into run.map). The returned controller is at phase == NEOW.
//
// Character sheet is the Ironclad sheet (Ironclad.java:113-115) with the
// run-setup ascension modifiers applied in the game's order (see the block above
// run_setup_max_hp): potion slots, then max-HP loss, then the 90 %-of-max current
// HP, then the starting curse, then the starting deck.
[[nodiscard]] RunController run_begin(int64_t seed, uint8_t ascension) noexcept;

// Fill `out` with the current run-level legal actions for `rc` (see RunActionMask).
// This overload completes the one-name/all-phases API promised by stage-a §7.
void legal_actions(const RunController& rc, RunActionMask& out) noexcept;

// Step a heterogeneous batch of runs by one action each (the run-level analogue
// of advance()). Each index is dispatched INDEPENDENTLY by its own phase: a
// COMBAT entry pumps its embedded CombatState (and folds back on combat end); a
// MAP_CHOICE consumes a CHOOSE(column); NEOW / TREASURE_ROOM /
// COMBAT_REWARD consume their CHOOSE actions. No heap allocation in the loop.
// results[i] carries the
// terminal flag, the combat reward passthrough, and (in COMBAT) the combat
// observation (zeroed otherwise).
void advance(std::span<RunController> runs, std::span<const Action> actions,
             std::span<StepResult> results) noexcept;

// Compatibility spellings (run_-prefixed) for callers that predate the
// overload set. They are thin wrappers over the public overloads above, not a
// second implementation.
inline void run_legal_actions(const RunController& rc,
                              RunActionMask& out) noexcept {
    legal_actions(rc, out);
}
inline void run_advance(std::span<RunController> runs,
                        std::span<const Action> actions,
                        std::span<StepResult> results) noexcept {
    advance(runs, actions, results);
}

// --- Exposed helpers (also unit-tested directly) -----------------------------

// The floor transition: advance the leaving-room cursor, ++run.floor, reseed the
// 5 floor-scoped streams to floor_stream(seed, floor) (trap 7: AFTER the
// increment), move to the destination node, and run its onPlayerEntry. `dst_x`
// is the destination column (ignored when `to_boss`); `to_boss` takes the boss
// edge. Public for the trap-7 named test.
void next_room_transition(RunController& rc, uint8_t dst_x, bool to_boss) noexcept;

// The same transition, to the OFF-MAP boss chest. ProceedButton.goToTreasureRoom
// (ProceedButton.java:179-187) builds a synthetic MapRoomNode(-1, 15) holding a
// TreasureRoomBoss and calls nextRoomTransitionStart(), which fades out and --
// because isDungeonBeaten is still false, it is set only when LEAVING the chest
// (:249-250) -- reaches AbstractDungeon.updateFading's `if (!isDungeonBeaten)
// this.nextRoomTransition()` (AbstractDungeon.java:2317-2325). So this is a FULL
// room transition, not a screen change: ++floorNum, the trap-7 five-stream
// reseed, the relic onEnterRoom / justEnteredRoom fan-outs (Maw Bank pays its 12
// gold HERE, MawBank.java:29-35) and then TreasureRoomBoss.onPlayerEntry's chest
// construction. The only thing it does not do is read a map node, because the
// destination is off-grid.
//
// THE S2 SCOUT DOSSIER SAID THE OPPOSITE ("no floor increment, no RNG, off-map
// and floor-less") and was wrong; the chain above is what the decompiled game
// does. The floor evidence -- boss at 16 / chest at 17, so Act 2's first
// playable room is 18 -- is DISCHARGED by S2.12: see kActFloorSpan /
// act_floor_base below for the full pair and its three separately-verified
// edges (s2-tasks.md deferred obligations, "Exact Act-2/3 entry floors").
void next_room_transition_boss_chest(RunController& rc) noexcept;

// --- The act boundary (S2.12) ------------------------------------------------
//
// FLOOR NUMBERING IS CONTINUOUS ACROSS ACTS. dungeonTransitionSetup
// (AbstractDungeon.java:2562-2604) resets `floorNum` nowhere -- the only reset in
// the class is `reset()` (:2610), which runs for a NEW RUN. So the act boundary
// is a pure act change at an unchanged floor, and every act's rows sit at a
// fixed OFFSET from the run's floor numbering.
//
// THE SPAN IS 17, and it is the sum of three separately-verified edges:
//   * 15 map rows, one floor each (MAP_HEIGHT = 15, AbstractDungeon.java:210);
//   * the BOSS is one more floor -- the row-14 node's only outgoing edge is the
//     synthetic boss node (DungeonMap.java:68-87), taken through an ordinary
//     nextRoomTransition, so ++floorNum fires (:1741);
//   * the BOSS CHEST is one more floor still -- goToTreasureRoom
//     (ProceedButton.java:179-187) runs a FULL nextRoomTransition because
//     `isDungeonBeaten` is set only on the way OUT (:249-250), so
//     AbstractDungeon.updateFading's `if (!isDungeonBeaten) nextRoomTransition()`
//     (:2317-2325) fires. That edge is S2.11's finding and its correction of the
//     scout dossier.
// The CROSSING itself adds nothing: `isDungeonBeaten = true` (:249-250) is
// exactly what makes updateFading skip nextRoomTransition (:2317-2326), so the
// new act is constructed at the OLD act's chest floor, and the +1 comes back on
// the ordinary MapRoomNode transition into the new act's first room.
//
// So, with the run starting at floor 0 (Neow):
//   Act 1   rooms 1-15    boss 16   chest 17
//   Act 2   rooms 18-32   boss 33   chest 34
//   Act 3   rooms 35-49   boss 50   (no chest -- the Act-3 boss opens no reward
//                                    screen at all, s2-design §1)
// The 17/34 of the deferred-obligations row are therefore the floors at which
// the NEXT act is CONSTRUCTED -- what dungeonTransitionSetup, the constructors,
// generateMap, setEmeraldElite and the BGM draw all observe, and the floors the
// un-reseeded floor streams still carry (miscRng == from_seed(seed + 17) /
// from_seed(seed + 34)) -- while 18/35 are the first PLAYABLE rooms of Acts 2
// and 3. Both halves of each pair are pinned by named tests; conflating them is
// the mistake the row exists to prevent.
inline constexpr int kActFloorSpan = 17;

// The last act S2 models. Act 4 (TheEnding / the Heart) is S3 and is reached
// only through the keys + the Door, which S2 does not grant (s2-design §1), so
// the Act-3 boss is the run's terminal.
inline constexpr uint8_t kFinalAct = 3;

// The floor at which `act` was constructed == the floor BELOW its first playable
// room. 0 / 17 / 34 for acts 1 / 2 / 3.
[[nodiscard]] constexpr int act_floor_base(int act) noexcept {
    return (act - 1) * kActFloorSpan;
}
static_assert(act_floor_base(1) == 0);
static_assert(act_floor_base(2) == 17);
static_assert(act_floor_base(3) == 34);

// TRAP 1 -- the cardRng counter snap at dungeonTransitionSetup
// (AbstractDungeon.java:2564-2570). Returns the counter the stream must be
// ADVANCED to (via advance_counter_to, rng_stream.hpp), or `counter` itself when
// no band applies.
//
// The three bands are STRICTLY OPEN on BOTH ends, and that is the whole trap:
//
//     if (counter > 0   && counter < 250) setCounter(250);
//     else if (counter > 250 && counter < 500) setCounter(500);
//     else if (counter > 500 && counter < 750) setCounter(750);
//
// so a counter of exactly 0, 250, 500 or 750 does NOT snap (0 is not > 0, and
// each boundary fails its own band's `<` and the next band's `>`), and there is
// NO fourth band -- anything >= 750 is left exactly where it is. A "round up to
// the next multiple of 250" reading gets three of those four boundaries wrong
// and reads as correct.
[[nodiscard]] constexpr int32_t card_rng_snap_target(int32_t counter) noexcept {
    if (counter > 0 && counter < 250) return 250;
    if (counter > 250 && counter < 500) return 500;
    if (counter > 500 && counter < 750) return 750;
    return counter;
}

// The between-act heal (AbstractDungeon.java:2582-2586), as an AMOUNT so the
// arithmetic can be pinned without walking a transition:
//
//     if (ascensionLevel >= 5) player.heal(MathUtils.round((maxHealth - currentHealth) * 0.75f), false);
//     else                     player.heal(maxHealth, false);
//
// Below A5 the argument is maxHealth, which the += / clamp in
// AbstractCreature.heal turns into a FULL heal regardless of how much is
// missing; at A5+ it is 75 % of the MISSING HP, rounded by libGDX MathUtils.round
// (mathutils_round: floor(v + 0.5) on the float widened to double, so exact .5
// goes UP -- 1 missing HP heals 1, and 2 missing heals 2, not 1).
//
// This is the AMOUNT HANDED TO heal(), not the amount that lands: the relic
// onPlayerHeal pass runs inside heal() and Mark of the Bloom zeroes it there
// (relics/relic_pickup.hpp).
[[nodiscard]] constexpr int act_transition_heal_amount(int max_hp, int hp,
                                                       int ascension) noexcept {
    return ascension >= 5
               ? mathutils_round(static_cast<float>(max_hp - hp) * 0.75f)
               : max_hp;
}

// THE ACT TERMINAL SEAM -- FILLED BY S2.12 (S2.11 stubbed it to the victory).
//
// CONTRACT (unchanged from S2.11's). Called from step_one when the player
// presses Proceed on a boss chest whose screen is CLOSED or DONE -- i.e. exactly
// where the game calls ProceedButton.goToNextDungeon (ProceedButton.java:159-164,
// :231-252). On entry:
//   * rc.phase is BOSS_TREASURE and rc.room_type is RoomType::TreasureBoss;
//   * rc.run.boss_chest still holds the three offers and `chose_relic`, and the
//     caller has ALREADY run the noPick bookkeeping (metrics-only in the game,
//     :232-234) -- so this function must not re-read it for state;
//   * `rs` is rc.run, passed explicitly because the body is RunState-heavy;
//   * `res` has NOT been filled yet.
// On return it has set rc.phase and filled `res`, and rc.run.boss_chest is cleared
// by the caller afterwards.
//
// WHAT IT NOW DOES: the whole act transition -- dungeonTransitionSetup plus the
// next dungeon's constructor chain -- landing at RunPhase::MAP_CHOICE on the new
// act's freshly generated map, with `res` non-terminal. A boss chest exists only
// after the Act-1 and Act-2 bosses (the Act-3 boss opens no reward screen and so
// never reaches a chest, s2-design §1), so this seam is never the run's end.
void on_boss_chest_proceed(RunController& rc, RunState& rs,
                           StepResult& res) noexcept;

// Apply the live combat's not-yet-charged gold movements to RunState.gold
// (S2.48): NEW STEALS first -- each thief steal beyond stolen_live.prev_steals,
// replayed round-by-round in slot order, takes min(goldAmt, rc.run.gold) and
// deducts exactly that (DamageAction.stealGold's clamp against the live purse,
// DamageAction.java:98-114, recorded per thief in stolen_live.taken) -- THEN
// the unbanked Hand-of-Greed remainder (combat_gold - banked) is added.
//
// That in-call order is the GAME's order for any single step: within one
// END_TURN advance the only way combat_gold can move is a start-of-NEXT-turn
// card play (Mayhem), which resolves after the monster phase's steals; a
// player-phase Greed kill is its own PLAY_CARD step and was banked at that
// step's boundary, before any later steal. Called after every in-combat step
// (the run advance's COMBAT case and step_potion's pump path) and again inside
// fold_back_combat; idempotent between events, so the repeat is free.
void sync_live_gold(RunController& rc) noexcept;

// Return the portion of the stolen gold carried by DEAD thieves -- the
// claimable STOLEN_GOLD reward amount (die() -> addStolenGoldToRewards,
// Looter.java:170-172 / Mugger.java:161-163). The purse deduction itself
// happened at the steal boundaries (sync_live_gold above); this reads
// stolen_live.taken, after a catch-up sync so a hand-built controller that
// never stepped still settles. An ESCAPED thief's share is simply gone, and a
// dead player's purse stays short -- the game deducted at steal time.
//
// PUBLISHED FOR TESTING. Its real callers are internal (the reward entry, the
// defeat path, the Act-3 terminal and the Colosseum reopen), and all reach it
// only from a combat the run layer built. The attribution case that must stay
// pinned -- two thieves, a purse smaller than their combined take, exactly one
// of them killed -- is a direct call on a hand-built controller.
[[nodiscard]] int32_t settle_stolen_gold(RunController& rc) noexcept;

// The current grid row for a controller on the map, or -1 when it is standing
// BELOW row 0 -- at Neow (floor 0), or on the act-boundary floor from which the
// new act's first node is picked (floor 17 in Act 2, 34 in Act 3, whose
// currMapNode the game sets to MapRoomNode(0, -1), TheCity.java:49).
//
// S1 could spell this `floor - 1` because Act 1's base is 0. From S2.12 it is
// `floor - act_floor_base(act) - 1`, and the -1 sentinel is SATURATED rather
// than allowed to run negative: an off-nominal (act, floor) pair built by a
// directed test must not be able to index the map array below zero.
[[nodiscard]] constexpr int run_cur_row(const RunController& rc) noexcept {
    const int row = static_cast<int>(rc.run.floor) -
                    act_floor_base(static_cast<int>(rc.run.act)) - 1;
    return row < 0 ? -1 : row;
}

// `AbstractDungeon.getCurrMapNode().hasEmeraldKey` -- is the controller standing
// on the burning elite setEmeraldElite marked (map_rooms.hpp step 5,
// AbstractDungeon.java:542-556)?
//
// TWO CONSUMERS, ONE PREDICATE, and they are not the same gate: the ENTRY buff
// (MonsterRoomElite.applyEmeraldEliteBuff, :39-68, via AbstractPlayer
// .preBattlePrep :1602-1605) reads only `isFinalActAvailable && node flag`,
// while the REWARD ROW (addEmeraldKey, :94-98) adds `!Settings.hasEmeraldKey`
// and `!rewards.isEmpty()` on top. So this names the NODE only; each caller
// applies the rest of its own guard. It was inlined at the entry-roll site until
// S3.11 gave it a second reader.
[[nodiscard]] constexpr bool on_emerald_elite_node(
    const RunController& rc) noexcept {
    return rc.emerald_x != kNoEmeraldNode && rc.cur_x == rc.emerald_x &&
           run_cur_row(rc) == static_cast<int>(rc.emerald_y);
}

// Whether a terminal controller is the VICTORY rather than a death.
//
// IT MOVED WITH ITS PRODUCER, TWICE, and that coupling is the point. In S1 the
// terminal was the Act-1 boss REWARD screen's proceed; S2.11 moved it one room
// later to the boss CHEST's proceed; S2.12 moves it to the real one, because the
// chest's proceed now starts Act 2. The run ends when the ACT-3 BOSS kill settles
// its gold (s2-design §1, frozen): AbstractRoom.java:327 suppresses dropReward()
// and combatRewardScreen.open() for a non-endless TheBeyond boss, so no reward
// screen and no chest ever follow it, and ProceedButton's chest branch
// (:111-113) requires screen == COMBAT_REWARD. The terminal surface is the gold
// add of AbstractRoom.java:286-297 -- see the Act-3 arm of
// finish_combat_after_action.
//
// The (act, room) read is what keeps a death out: a death parks at RUN_OVER with
// whatever room it died in, and only a WON Act-3 boss room reaches this shape.
// `combat_outcome == KILLED` is asserted too rather than inferred -- a defeat in
// the Act-3 boss room has exactly the same act and room_type.
[[nodiscard]] constexpr bool run_is_victory(const RunController& rc) noexcept {
    return rc.phase == static_cast<uint8_t>(RunPhase::RUN_OVER) &&
           rc.run.act == kFinalAct &&
           rc.room_type == static_cast<uint8_t>(RoomType::Boss) &&
           rc.combat_outcome == static_cast<uint8_t>(RunCombatOutcome::KILLED);
}

}  // namespace sts::engine
