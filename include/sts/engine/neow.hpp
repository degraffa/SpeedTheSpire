#pragma once

// neow.hpp -- the floor-0 Neow blessing: the four option slots, their payouts,
// and the sub-screens two thirds of those payouts open. This is run-layer
// dialog content, not a new run phase: the controller stays in RunPhase::NEOW
// for the whole thing and this state says which screen of it is up (the same
// shape the rest site uses for its Smith / Toke / Dream Catcher screens).
//
// Provenance (every method read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * NeowEvent.blessing (NeowEvent.java:361-378): `rng = new
//     Random(Settings.seed)` and then FOUR NeowRewards, categories 0,1,2,3 in
//     that order, whose optionLabels become the dialog's four buttons.
//   * NeowEvent.buttonEffect (NeowEvent.java:162-242): which screen leads to
//     blessing() vs miniBlessing() (see THE MINI-BLESSING GATE below), and
//     screenNum 3's `rewards.get(i).activate()` -> screenNum 99, whose next
//     button press opens the map.
//   * NeowReward.<init>(int) (NeowReward.java:65-74): hp_bonus = (int)(maxHealth
//     * 0.1f), getRewardOptions(category), then ONE
//     `rng.random(0, possibleRewards.size() - 1)` pick.
//   * NeowReward.getRewardDrawbackOptions / getRewardOptions
//     (NeowReward.java:76-128): the per-category tables, and category 2's
//     DRAWBACK-FIRST roll order plus the three drawback-conditional option
//     omissions.
//   * NeowReward.activate (NeowReward.java:190-307): the drawback switch, then
//     the payout switch.
//   * NeowReward.update (NeowReward.java:130-188): the grid-selection payouts
//     (upgrade / remove 1 / remove 2 / transform 1 / transform 2) and the CURSE
//     drawback's deferred card obtain.
//   * NeowReward.getRewardCards / getColorlessRewardCards / rollRarity / getCard
//     (NeowReward.java:309-391): the three-card offers.
//
// THE MINI-BLESSING GATE -- NOT AN UNLOCK CHECK, AND UNREACHABLE HERE.
// The branch is `bossCount == 0 && !Settings.isTestingNeow -> miniBlessing()`
// (NeowEvent.java:178-183, and its mirror at :168-173). `bossCount` is set in
// the constructor (NeowEvent.java:75-80) and reads the profile preference
// `<CLASS>_SPIRITS` ONLY on the `Settings.isStandardRun()` branch; otherwise it
// is `Settings.seedSet ? 1 : 0`. And `isStandardRun()` is `!isDailyRun &&
// !isTrial && !seedSet` (Settings.java:633-634). A SEEDED run therefore takes
// the else-branch and gets bossCount == 1 -- the full four-option blessing --
// with no reference to the profile at all. Every simulated run and every oracle
// capture is seeded, so the mini-blessing (two fixed options, NeowEvent.java:
// 349-359) is not reachable in this model's domain and is deliberately not
// encoded. This is a STRONGER result than the frozen "fully-unlocked profile =>
// full blessing" assumption, and it does not depend on UnlockTracker, which the
// gate never consults.
//
// STREAM ATTRIBUTION -- the part that is easy to get wrong.
//   * Option rolls, the card RARITY rolls, the RED card-pool draws, the
//     one-rare-card draw and the transform draws are `NeowEvent.rng`
//     (RunState.neow_rng): a FRESH Random(seed) whose counter starts at 0 and
//     is independent of every dungeon stream (trap 17).
//   * The COLORLESS card identities are `cardRng`: getColorlessCardFromPool
//     goes through CardGroup.getRandomCard(true, rarity), whose `true` means
//     AbstractDungeon.cardRng (CardGroup.java:509-524) -- the caller's rng is
//     not threaded through. So a colorless blessing mixes both streams.
//   * The CURSE drawback's card is also `cardRng`: getCardWithoutRng(CURSE)
//     delegates to returnRandomCurse -> CardLibrary.getCurse(), which draws
//     `cardRng.random(0, size - 1)` (AbstractDungeon.java:1519-1536, :1125-1129;
//     CardLibrary.java:1022-1029). "WithoutRng" names the MathUtils-vs-cardRng
//     distinction for the other rarities and is a false friend here.
//   * Relics come from the relicRng-shuffled dungeon pools by front-pop, which
//     is draw-free (trap 15); potions are `potionRng`.
//   * The three-potion blessing ALSO moves cardRng and the card pity counter --
//     see kNeowRewardScreenRoom below.
//
// NEOW REWARD TYPE ORDER IS OURS, NOT THE GAME'S. NeowReward$NeowRewardType is
// an inner enum that the decompiled tree does not carry as a source file, so
// its ordinal order is not recoverable; what IS recoverable is each constant's
// BEHAVIOUR, from the two switch statements over it. The enum below is
// therefore ordered by the category tables that produce it, and nothing depends
// on its numeric values matching the game's.

#include <cstdint>
#include <type_traits>

#include "sts/engine/combat_rewards.hpp"  // RewardScreen
#include "sts/engine/map_rooms.hpp"       // RoomType
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_state.hpp"

namespace sts::engine {

// --- Option content -----------------------------------------------------------

inline constexpr int kNeowOptionCount = 4;   // categories 0..3, one each
inline constexpr int kNeowGridPickCap = 2;   // REMOVE_TWO / TRANSFORM_TWO_CARDS

// GOLD_BONUS / LARGE_GOLD_BONUS (NeowReward.java:54-55).
inline constexpr int kNeowGoldBonus = 100;
inline constexpr int kNeowLargeGoldBonus = 250;

// The room identity the Neow reward screen assembles under. NeowRoom is an
// ordinary AbstractRoom with an event (NeowRoom.java:16-20), so it takes
// AbstractRoom.getCardRarity's BASE 3/37 widths -- the same table an event room
// uses, which is why the RoomType is Event rather than None. It matters only
// for the discarded card row the potion blessing rolls.
inline constexpr RoomType kNeowRewardScreenRoom = RoomType::Event;

enum class NeowRewardType : uint8_t {
    NONE = 0,
    // category 0 (NeowReward.java:88-96)
    THREE_CARDS = 1,
    ONE_RANDOM_RARE_CARD = 2,
    REMOVE_CARD = 3,
    UPGRADE_CARD = 4,
    TRANSFORM_CARD = 5,
    RANDOM_COLORLESS = 6,
    // category 1 (:97-104)
    THREE_SMALL_POTIONS = 7,
    RANDOM_COMMON_RELIC = 8,
    TEN_PERCENT_HP_BONUS = 9,
    THREE_ENEMY_KILL = 10,
    HUNDRED_GOLD = 11,
    // category 2 (:105-122)
    RANDOM_COLORLESS_2 = 12,
    REMOVE_TWO = 13,
    ONE_RARE_RELIC = 14,
    THREE_RARE_CARDS = 15,
    TWO_FIFTY_GOLD = 16,
    TRANSFORM_TWO_CARDS = 17,
    TWENTY_PERCENT_HP_BONUS = 18,
    // category 3 (:123-125)
    BOSS_RELIC = 19,
};

// The four drawbacks, in getRewardDrawbackOptions' INSERTION order
// (NeowReward.java:76-83) -- that order is what the drawback roll indexes, so
// the values below are (list index + 1) and are load-bearing.
enum class NeowDrawback : uint8_t {
    NONE = 0,
    TEN_PERCENT_HP_LOSS = 1,
    NO_GOLD = 2,
    CURSE = 3,
    PERCENT_DAMAGE = 4,
};

inline constexpr int kNeowDrawbackCount = 4;

// Which screen of the Neow flow is up. The controller's phase stays NEOW for
// all of them.
enum class NeowScreen : uint8_t {
    BLESSING = 0,     // the four option buttons (NeowEvent screenNum 3)
    CARD_REWARD = 1,  // cardRewardScreen.open(offer, null, ...)
    GRID = 2,         // gridSelectScreen.open(master deck, n, ...)
    ITEM_REWARD = 3,  // combatRewardScreen.open() -- the three potions
    DONE = 4,         // payout complete (screenNum 99): next CHOOSE opens the map
};

// What the open grid is for. The Java distinguishes them by which list it opens
// over and what update() does with the selection.
enum class NeowGridMode : uint8_t {
    NONE = 0,
    REMOVE = 1,     // getPurgeableCards, masterDeck.removeCard
    TRANSFORM = 2,  // getPurgeableCards, removeCard + transformCard
    UPGRADE = 3,    // getUpgradableCards, card.upgrade()
};

inline constexpr uint8_t kNeowNoChoice = 0xFF;

// The whole Neow screen state. Transient exactly like RewardScreen and
// RestSiteState: the game re-derives it from (seed, floor 0) on reload, so
// RunState's frozen schema is untouched.
struct NeowState {
    uint8_t option_type[kNeowOptionCount];      // NeowRewardType per slot
    uint8_t option_drawback[kNeowOptionCount];  // NeowDrawback per slot
    uint16_t grid_picked[kNeowGridPickCap];     // master-deck indices, pick order
    int16_t hp_bonus;      // (int)(maxHealth * 0.1f), frozen at blessing time
    uint8_t screen;        // NeowScreen
    uint8_t chosen;        // activated option index, or kNeowNoChoice
    uint8_t grid_mode;     // NeowGridMode
    uint8_t grid_needed;   // picks the open grid wants (1 or 2)
    uint8_t grid_done;     // picks made so far
    uint8_t pad;
};

static_assert(std::is_trivially_copyable_v<NeowState>);

// --- Blessing roll ------------------------------------------------------------

// NeowEvent.blessing: build the four options off `rs.neow_rng`, leaving the
// state at NeowScreen::BLESSING. Exactly FIVE draws, in this order --
// category 0 pick, category 1 pick, category 2 DRAWBACK, category 2 pick,
// category 3 pick (a `random(0, 0)` over the one-element boss-relic list, which
// still advances the stream: Random.random(int,int) increments unconditionally,
// Random.java:58-61).
//
// The sim rolls this at run start rather than on the first button press. The
// two are equivalent by construction: the stream is a fresh Random(seed) that
// nothing else touches, and NeowEvent's intro screens (screenNum 0/1/2) consume
// only MathUtils flavour-text and sfx draws, which are not run state. What the
// controller calls RunPhase::NEOW IS the blessing screen.
void neow_roll_blessing(RunState& rs, NeowState& out) noexcept;

// The category `cat` option list a given drawback produces, in insertion order.
// Pure, so the category tables can be checked without a stream; `count` is
// written with the list length. Category 2 is the only one whose list depends on
// the drawback (NeowReward.java:110-121).
void neow_category_options(int cat, NeowDrawback drawback,
                           NeowRewardType* out, int& count) noexcept;

// --- Activation ---------------------------------------------------------------

// NeowReward.activate for option `index`: the drawback body, then the payout
// body, then -- for the CURSE drawback -- the curse card, which the Java obtains
// one update() tick LATER (NeowReward.java:183-186) and therefore always AFTER
// the payout's own draws. That ordering is observable on cardRng whenever a
// colorless payout shares the stream with a CURSE drawback.
//
// `misc_rng` is the floor-scoped miscRng that relic onEquip bodies draw (War
// Paint / Whetstone are COMMON, so a common-relic blessing can reach them).
// Payouts that need a screen leave `st.screen` at CARD_REWARD / GRID /
// ITEM_REWARD and, for the card screens, write the offer into `rewards` as a
// single already-open CARDS item; every other payout finishes at DONE.
void neow_activate(RunState& rs, NeowState& st, RewardScreen& rewards,
                   RngStream& misc_rng, uint8_t index) noexcept;

// --- Sub-screens --------------------------------------------------------------

// Whether master-deck row `deck_index` can be picked on the open grid: it must
// be in the list the grid was opened over (purgeable / upgradable) and not
// already picked. gridSelectScreen has no cancel button on any Neow call site,
// so there is no "pick nothing" action.
[[nodiscard]] bool neow_grid_card_legal(const RunState& rs, const NeowState& st,
                                        uint16_t deck_index) noexcept;

// Pick `deck_index`. The effect is applied only once the grid has all the picks
// it asked for -- NeowReward.update reads `selectedCards` as a completed set --
// after which the state moves to DONE. Returns false (no mutation) on an
// illegal pick.
[[nodiscard]] bool neow_grid_pick(RunState& rs, NeowState& st,
                                  uint16_t deck_index) noexcept;

// The card screen's Skip. Neow opens CardRewardScreen with a null RewardItem
// (NeowReward.java:215/219/223/265), so `skippable` is true and the skip button
// is shown (CardRewardScreen.java:441-457) -- but takeReward's removal is a
// no-op on a null item (:292-301), so the offer simply goes away. That is why
// this is not reward_skip_card, which deliberately keeps a combat CARDS item
// claimable.
void neow_skip_card(NeowState& st, RewardScreen& rewards) noexcept;

// Called after a card was taken / the potion screen was left: move to DONE.
void neow_finish_payout(NeowState& st) noexcept;

}  // namespace sts::engine
