#pragma once

// THE BOSS CHEST (TreasureRoomBoss) -- the room that follows every act boss's
// combat-reward screen, and the boss-relic pick it presents.
//
// WHAT MAKES IT DIFFERENT FROM AN ORDINARY CHEST (treasure_rooms.hpp): the
// three relics are drawn AT ROOM ENTRY, not at open time; they cost NO RNG at
// all; and the chest's open path is a FULL OVERRIDE of AbstractChest.open with
// no `super` call -- so none of the ordinary chest's machinery (the treasureRng
// contents roll, gold, curses, addRelicToRewards, the sapphire-key append, the
// onChestOpen/onChestOpenAfter relic bodies, combatRewardScreen) runs here.
//
// Provenance (every method read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   ProceedButton.update / goToTreasureRoom   ProceedButton.java:97-169, 179-187
//   ProceedButton.goToNextDungeon             ProceedButton.java:231-252
//   TreasureRoomBoss ctor / onPlayerEntry     TreasureRoomBoss.java:31-36, 56-64
//   TreasureRoomBoss.update                   TreasureRoomBoss.java:66-71
//   BossChest ctor / open / close             BossChest.java:30-47, 49-63, 65-69
//   AbstractChest.open (the OVERRIDDEN body)  AbstractChest.java:62-102
//   AbstractDungeon.returnRandomRelicKey      AbstractDungeon.java:757-808
//   AbstractDungeon.returnEndRandomRelicKey   AbstractDungeon.java:704-755
//   BossRelicSelectScreen.open                BossRelicSelectScreen.java:342-373
//   BossRelicSelectScreen.update (isDone)     BossRelicSelectScreen.java:88-110
//   BossRelicSelectScreen.relicObtainLogic    BossRelicSelectScreen.java:181-200
//   BossRelicSelectScreen.relicSkipLogic      BossRelicSelectScreen.java:202-212
//   BossRelicSelectScreen.noPick              BossRelicSelectScreen.java:240-248
//   AbstractRelic.update (BOSS_REWARD arm)    AbstractRelic.java:340-374
//   AbstractRelic.bossObtainLogic             AbstractRelic.java:391-398
//   AbstractRelic.instantObtain               AbstractRelic.java:230-234
//
// THE THREE POPS HAPPEN AT ENTRY, UNCONDITIONALLY. `new BossChest()` is the
// LAST statement of TreasureRoomBoss.onPlayerEntry (:63), and the constructor's
// non-blight arm (BossChest.java:35-39) clears the list and adds exactly three
// `AbstractDungeon.returnRandomRelic(RelicTier.BOSS)`. Nothing gates that on the
// chest ever being opened -- a player who walks straight past the chest has
// still burned three relics out of the boss pool, and the burn is visible many
// floors later at the next boss chest or a Neow swap. That is trap 3.
//
// NO RNG. For BOSS tier BOTH returnRandomRelicKey (:792-798) and the
// canSpawn-rejection reroute returnEndRandomRelicKey (:739-745) are `remove(0)`
// -- a front pop. The rejected relic is popped and PERMANENTLY CONSUMED before
// the recursion (:804-806), so a canSpawn gate costs the pool an extra entry and
// still yields no random draw. On an exhausted pool the key is "Red Circlet",
// which RelicLibrary never registered, so getRelic falls through to a plain
// Circlet (:626) -- already modelled at relic_pools.cpp:386-389.
//
// SKIP IS A REVERSIBLE SCREEN CLOSE, NOT A DISCARD. relicSkipLogic (:202-212)
// calls chest.close(), which only sets isOpen = false (BossChest.java:65-69) and
// does NOT clear BossChest.relics; TreasureRoomBoss.update (:66-71) keeps
// chest.update() live, so the chest can be clicked again and reopens with THE
// SAME THREE relics (BossRelicSelectScreen.open re-clears its own list and
// re-adds from the one it is handed, :342-373). Nothing is recorded and the pool
// is untouched. What is irreversible is the ENTRY burn, which already happened.
//
// LEAVING WITHOUT PICKING IS METRICS-ONLY. goToNextDungeon (:231-234) calls
// bossRelicScreen.noPick() when TreasureRoomBoss.choseRelic is false, and
// noPick's whole body (:240-248) appends a metrics map. No state, no pool.
//
// PICKING DROPS THE OTHER TWO FOR GOOD. relicObtainLogic (:181-200) records the
// pick, sets choseRelic, and the isDone block (:101-108) clears the screen list;
// the two unpicked relics are never returned to any pool.

#include <cstdint>
#include <type_traits>

#include "sts/engine/combat_rewards.hpp"  // RewardScreen (the equip-context door)
#include "sts/engine/neow.hpp"            // NeowState: the re-homed equip screens
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/relics.hpp"
#include "sts/engine/run_state.hpp"

namespace sts::engine {

// THE STRUCT ITSELF LIVES IN run_state.hpp (S2.47 / schema v8): the three
// offers are the evidence design §6 S2-G2 item 2 scores through the oracle
// translator and the differ, both of which see RunState -- so BossChestState
// moved out of the transient controller into durable RunState storage
// (rs.boss_chest, a pure tail append). This header keeps the provenance above
// and the room-flow functions below; there is no second copy of the state.
//
// PER-VALUE PROVENANCE for BossChestScreen (run_state.hpp declares the enum):
//   CLOSED            -- chest on the floor, unopened. AbstractRoom.update's
//                        COMPLETE arm has already shown the proceed button
//                        (TreasureRoomBoss.java:33), so BOTH "open" and "leave"
//                        are live. Also the state a SKIP returns to --
//                        chest.close() sets isOpen = false and the chest is
//                        clickable again.
//   RELIC_SELECT      -- AbstractDungeon.bossRelicScreen is up (screen =
//                        BOSS_REWARD, BossRelicSelectScreen.java:353) with the
//                        three relics offered. The proceed button is HIDDEN
//                        while it is (:354), so leaving is not legal here; the
//                        cancel button (:349) is the skip.
//   EQUIP_GRID        -- the picked relic's onEquip asked for a master-deck
//                        grid (RelicEquipScreen::GRID_REMOVE /
//                        GRID_TRANSFORM_UPGRADE / GRID_CONFIRM_*). The grid
//                        itself lives in the controller's NeowState -- see the
//                        re-homing note below.
//   EQUIP_ITEM_REWARD -- the picked relic's onEquip assembled a claimable
//                        reward screen (RelicEquipScreen::ITEM_REWARD -- Tiny
//                        House / Calling Bell). The items live in the
//                        controller's RewardScreen.
//   DONE              -- a relic was picked and its onEquip screens (if any)
//                        are resolved. The proceed button is back
//                        (AbstractRelic.java:345-347) and it is the only
//                        action: this is the act terminal.

// THE RE-HOMED EQUIP SCREENS. Five BOSS-tier relics have `on_equip_screen`
// bodies -- Pandora's Box, Tiny House, Astrolabe, Empty Cage, Calling Bell --
// and every one of them is in the 22-entry boss pool, so every one of them is
// pickable HERE. relic_pools.hpp's RelicEquipContext block (:106-189) named this
// exact site in advance: "a future Act-2 boss-chest site translates the same
// request onto whatever screen state it owns. Re-homing is a call-site change,
// never a body change."
//
// This site's answer is to reuse the controller's EXISTING NeowState grid
// fields (grid_mode / grid_needed / grid_done / grid_picked) and RewardScreen,
// driven by the same neow_grid_card_legal / neow_grid_pick /
// relic_confirm_pandoras_box / relic_confirm_calling_bell handlers the Neow boss
// swap uses (neow.cpp:65-115). Nothing is duplicated and NO namespace value is
// spent: no new NeowGridMode, no new NeowScreen, no new ChoiceKind, no new
// RunActionMask field. `BossChestState.screen` is what says which of the two
// screens is up, because NeowState's own `screen` field means the NEOW phase's
// flow and is left alone here.

// TreasureRoomBoss.onPlayerEntry -> new BossChest() (TreasureRoomBoss.java:63):
// three front pops of the BOSS pool through return_random_relic_key, with the
// canSpawn recursion and the Red Circlet fallback. Mutates `rs.relic_pools`;
// consumes NO rng stream. Returns the chest at BossChestScreen::CLOSED.
//
// The spawn context is fill_boss_spawn_gates' (act + Burning Blood), which is
// the complete BOSS-tier canSpawn gate set: Ectoplasm's `actNum <= 1`
// (Ectoplasm.java:54-57) and the four starter-swap relics' hasRelic checks.
// rs.act is still the OUTGOING act here -- dungeonTransitionSetup runs only
// after this chest's proceed -- which is why Ectoplasm is still offerable at the
// Act-1 chest and rejected at the Act-2 one (trap 9).
[[nodiscard]] BossChestState roll_boss_chest(RunState& rs) noexcept;

// AbstractChest.update's click (:104-110) reaching BossChest.open(false)
// (:49-63). True while the chest exists and its screen is CLOSED -- there is no
// key requirement (AbstractChest.keyRequirement's base body is a no-op `true`,
// :49-52) and no capacity preflight, because opening the boss chest allocates
// nothing: it only shows the screen.
[[nodiscard]] bool boss_chest_open_legal(const BossChestState& chest) noexcept;

// Whether offer `index` can be picked. The Java has no gate at all -- clicking a
// relic on the BOSS_REWARD screen always obtains it -- so the only thing here is
// the engine's own relic-array capacity, and it is deliberately expressed over
// `rs.relic_count` alone (a PUBLIC field, PublicView.relic_count) rather than
// over relic_acquire_legal, whose Circlet branch would make the mask depend on
// the offer's IDENTITY and leak an unopened chest's contents into the hashed
// public serialization. kRelicCap is 40 and no Act-1/2 run can reach it, so the
// two predicates never disagree on a reachable state; the gate exists so a pick
// can never be a half-applied mutation.
[[nodiscard]] bool boss_chest_pick_legal(const RunState& rs,
                                         const BossChestState& chest,
                                         uint8_t index) noexcept;

}  // namespace sts::engine
