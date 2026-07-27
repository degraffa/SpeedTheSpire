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
// persistent state (schema-versioned, hashed, traced -- 2184 B, and its layout
// is FROZEN). The transient "where am I in the screen/room flow"
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
// What is DEFERRED (routed to an explicit ROOM_UNIMPLEMENTED / documented seam,
// never faked):
//   * relic POOLS and acquisition are live, and combat/chest/Neow reward draw
//     sites consume them; the shop's draw sites are separate work.
//   * the EMERALD_KEY reward item -- follows the emerald-flag scoping
//     (combat_rewards.hpp).
//   * ? rooms RESOLVE (event_framework.hpp): the one committed eventRng roll
//     picks MONSTER (a real monster combat, consuming monsterList) / SHOP
//     (parks, like a map shop) / TREASURE (the chest flow) / EVENT, and an
//     EVENT result runs the throwaway-stream selection + pool-removal
//     bookkeeping. Implemented bodies open EVENT_DIALOG; a selected event with
//     no body parks at ROOM_UNIMPLEMENTED with the selection committed and the
//     EventId recorded.
//   * shops: no room content (entering one reseeds the floor streams then
//     parks at ROOM_UNIMPLEMENTED with the stalling RoomType recorded).
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
//     the opening DrawCardAction is queued (:242) BEFORE
//     applyStartOfCombatLogic (:245) fires every relic's atBattleStart
//     (AbstractPlayer.java:1892-1901), which is why enter_combat dispatches that
//     hook after its turn-1 pump rather than before it. The separate pre-draw
//     hook (applyStartOfCombatPreDrawLogic, :241) has no registered relic and so
//     no call site yet. Full reasoning at the dispatch in run_advance.cpp.
//   * AbstractRoom.update battle-over (:277-357): the lifecycle transition points
//     (COMPLETE -> reward screen); the reward ASSEMBLY itself lives in
//     combat_rewards.hpp/.cpp and runs from enter_combat_reward.
//   * AbstractDungeon.<init> (AbstractDungeon.java:268-308) + dungeonTransitionSetup
//     (:2562-2604) + AbstractPlayer.<init> (AbstractPlayer.java:201-221) +
//     AbstractPlayer.initializeStarterDeck (:357-394): the run-setup ascension
//     modifier order -- see the block comment on run_setup_max_hp below.

#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include "sts/engine/advance.hpp"       // ActionMask, StepResult, combat advance/legal_actions
#include "sts/engine/combat_rewards.hpp"  // RewardScreen + the claim flow
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"    // MonsterLists
#include "sts/engine/event_framework.hpp"  // EventDialogState / kEventOptionCap
#include "sts/engine/interp.hpp"        // mathutils_round (run_setup_hp)
#include "sts/engine/map_rooms.hpp"     // RoomType
#include "sts/engine/neow.hpp"          // NeowState / the blessing screens
#include "sts/engine/rest_sites.hpp"    // RestSiteState / menu constants
#include "sts/engine/run_state.hpp"
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
    RUN_OVER = 6,          // player dead (loss) -- terminal.
    REST_SITE = 7,         // campfire menu / grid / Dream Catcher card pick.
    TREASURE_ROOM = 8,     // unopened Act-1 non-boss chest (open or skip).
    EVENT_DIALOG = 9,      // a ?-room resolved to an event with a live dialog
                           // body (event_framework.hpp). Today only the
                           // synthetic proof body reaches it; every native
                           // event parks at ROOM_UNIMPLEMENTED until its
                           // content-task body lands. Value 9 is the ledger's
                           // reserved allocation (stage-b-tasks.md shared-
                           // namespace table); gaps are legal, values are
                           // append-only and never renumbered.
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

// CHOOSE arg0 sentinel for "take the boss edge" at a MAP_CHOICE (the boss node is
// not a grid column, so it needs a value outside 0..kMapCols-1).
inline constexpr uint8_t kChooseBoss = static_cast<uint8_t>(kMapCols);  // 7

// cur_x sentinel: the controller is at Neow (floor 0), with no grid column yet.
inline constexpr uint8_t kNeowColumn = 0xFF;

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
inline constexpr uint8_t kChooseProceed = 0xFF;
inline constexpr uint8_t kChooseSkipCard = 0xFE;
inline constexpr uint8_t kChooseSing = 0xFD;
inline constexpr uint8_t kChooseOpenChest = 0;

// --- RunController -----------------------------------------------------------

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
    MonsterLists lists;
    uint8_t monster_cursor;
    uint8_t elite_cursor;
    uint8_t boss_cursor;
    uint8_t pad1;           // explicit padding.

    // The live COMBAT_REWARD screen: assembled once when the reward
    // screen opens, mutated by claims. Transient screen-flow state exactly like
    // the cursors above -- the game derives it too (a POST_COMBAT save re-rolls
    // the cards from the saved cardRng counter), so RunState stays untouched
    // (no new storage, no schema bump).
    RewardScreen rewards;
    RestSiteState rest;

    // The constructor-time chest rolls while phase == TREASURE_ROOM, retained
    // through its reward screen for replay/hash observability. Transient like
    // RewardScreen; never added to the frozen RunState schema.
    TreasureChest treasure_chest;

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
};

static_assert(std::is_trivially_copyable_v<RunController>,
              "RunController must be trivially copyable (POD batch entry)");

// Native event combats reuse the ordinary combat constructor while preserving
// the already-advanced floor streams and keeping RoomType::Event. The latter is
// what prevents next_room_transition from consuming monster_cursor and what
// selects EventRoom reward semantics. LAGAVULIN_AWAKE is the one constructor
// variant MonsterHelper's "Lagavulin Event" key requests.
enum class EventCombatVariant : uint8_t {
    NONE = 0,
    LAGAVULIN_AWAKE = 1,
};

[[nodiscard]] bool enter_event_combat(
    RunController& rc, std::string_view encounter_key,
    EventCombatVariant variant = EventCombatVariant::NONE) noexcept;

// --- RunActionMask -----------------------------------------------------------

// The run-level legal-action set (the run analogue of ActionMask). Which fields
// are meaningful depends on `phase`:
//   NEOW                 : depends on rc.neow.screen -- BLESSING offers
//                          can_choose_neow_option[0..3] (CHOOSE i); CARD_REWARD
//                          offers can_take_card / can_skip_card / can_sing;
//                          GRID offers can_choose_master_deck[i]; ITEM_REWARD
//                          offers can_claim_reward[i] + can_proceed; DONE offers
//                          can_proceed, which opens the map.
//   TREASURE_ROOM        : can_open_chest (CHOOSE kChooseOpenChest) and
//                          can_proceed (CHOOSE kChooseProceed) to skip it.
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
//   REST_SITE            : menu buttons, a Smith/Toke master-deck grid, or
//                          Dream Catcher's direct card-pick screen.
//   EVENT_DIALOG         : can_choose_event_option[i] (CHOOSE i) over a dialog,
//                          or can_choose_master_deck[i] over an event grid.
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
    // EVENT_DIALOG: the current screen's options, rebuilt from the event's
    // build_menu on every call (never cached). CHOOSE arg0 is the option index.
    bool can_choose_event_option[kEventOptionCap];
    // USE_POTION owns a RunState slot, so its legality lives at this layer.
    // Fruit Juice and Entropic Brew can be used in stable non-combat phases.
    // During COMBAT, can_use_potion_target[slot][monster] enumerates target-
    // required potions; non-target potions use can_use_potion[slot] directly.
    bool can_use_potion[kPotionCap];
    bool can_use_potion_target[kPotionCap][kMonsterCap];
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

// The current grid row (floor-1) for a controller on the map, or -1 at Neow.
[[nodiscard]] constexpr int run_cur_row(const RunController& rc) noexcept {
    return rc.run.floor == 0 ? -1 : static_cast<int>(rc.run.floor) - 1;
}

}  // namespace sts::engine
