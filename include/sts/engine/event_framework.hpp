#pragma once

// event_framework.hpp -- the ?-room resolution roll, event selection, and the
// reusable event dialog framework. Event BODIES are separate follow-on
// content tasks; this module owns the roll, the shrine/event split, the pool
// bookkeeping, and the dialog phase plumbing that those bodies plug into.
//
// THE RNG CONTRACT (the analogue of combat_rewards.hpp's, and stronger):
// `eventRng` advances by exactly +1 counter / one nextFloat per ?-room entered
// -- the room-type roll -- and by nothing else, ever. Every event-SELECTION
// draw happens on a throwaway stream reconstructed from (seed,
// eventRng.counter) and is discarded:
//   * nextRoomTransition builds a duplicate `new Random(Settings.seed,
//     eventRng.counter)` (AbstractDungeon.java:1766), EventHelper.roll draws
//     ONE float from it (EventHelper.java:102), and the duplicate is assigned
//     back (`eventRng = eventRngDuplicate`, AbstractDungeon.java:1770) -- that
//     assignment is the only commit.
//   * EventRoom.onPlayerEntry (EventRoom.java:28) then builds a SECOND
//     duplicate from the post-commit counter, generateEvent draws the shrine
//     split and the pool index from it (AbstractDungeon.java:1865, 1937,
//     1986), and that duplicate goes out of scope unassigned -- discarded.
// The game's "duplicate" is a COUNTER REPLAY (Random.java:28-33 re-seeds and
// replays `counter` draws of nextInt(1000)), not Random.copy() (which exists,
// Random.java:35-40, and is deliberately not used here). Under the one-draw
// invariant (stage-a design 3.2) a counter replay lands on the identical
// engine state, so the port's duplicate is a plain RngStream struct copy.
// Port shape: event_room_roll draws straight from rs.event_rng (the roll IS
// the commit, sanctioned by stage-a design 3.4); generate_event snapshots
// rs.event_rng into a local, draws from the local, and discards it, leaving
// rs.event_rng byte-identical. Never draw-then-rewind.
//
// NON-RNG STATE THAT DOES COMMIT even though the selection stream does not:
// the pool-membership removals (getShrine removes from BOTH shrineList and
// specialOneTimeEventList, AbstractDungeon.java:1938-1939; getEvent from
// eventList, :1987), the three pity floats (EventHelper.java:147-185), the
// Tiny Chest counter (EventHelper.java:107-109), and RunState.event_flags
// (the engine-side record of the game's saveFileLastEventChoice write at
// EventHelper.java:228 -- see generate_event below).
//
// Provenance (every method read in full from
// D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * EventHelper.roll(Random)            EventHelper.java:100-187
//   * EventHelper.resetProbabilities      EventHelper.java:189-195
//   * EventRoom.onPlayerEntry             EventRoom.java:25-31
//   * AbstractDungeon.nextRoomTransition  AbstractDungeon.java:1687-1813
//     (?-roll block :1763-1779, generateRoom :1823-1840)
//   * AbstractDungeon.generateEvent       AbstractDungeon.java:1864-1880
//   * AbstractDungeon.getShrine           AbstractDungeon.java:1882-1942
//   * AbstractDungeon.getEvent            AbstractDungeon.java:1944-1990
//   * AbstractDungeon.initializeSpecialOneTimeEventList /
//     isNoteForYourselfAvailable          AbstractDungeon.java:1340-1379
//   * Exordium.initializeEventList / initializeShrineList
//                                         Exordium.java:223-246
//   * TheCity.initializeEventList / initializeShrineList
//                                         TheCity.java:184-198, 209-217
//   * TheBeyond.initializeEventList / initializeShrineList
//                                         TheBeyond.java:178-187, 197-205
//   * AbstractDungeon.dungeonTransitionSetup (the eventList/shrineList clears)
//                                         AbstractDungeon.java:2562-2604
//   * AbstractDungeon.<init> (the rebuild order)
//                                         AbstractDungeon.java:268-300
//   * CardCrawlGame.getDungeon (specialOneTimeEventList by identity)
//                                         CardCrawlGame.java:1102-1119
//   * Random(Long,int) counter replay     Random.java:28-33
//   * TinyChest                           TinyChest.java:19-42
//   * SsserpentHead.onEnterRoom           SsserpentHead.java:29-35
//   * MawBank.onEnterRoom                 MawBank.java:31-36
//   * MawBank.setCounter (the -2 usedUp)  MawBank.java:47-53
//   * EternalFeather.onEnterRoom          EternalFeather.java:29-35
//   * RestRoom.onPlayerEntry              RestRoom.java:33-43
//   * MagicFlower.onPlayerHeal            MagicFlower.java:30-37
//   * DungeonMap boss-node transition     DungeonMap.java:77-87
//   * AbstractPlayer.isCursed             AbstractPlayer.java:741-748
//
// Transient screen state (EventDialogState) lives in RunController, not
// RunState, exactly like RestSiteState (rationale at run_advance.hpp:13-21);
// dialog options are rebuilt from scratch on every legal_actions call, never
// cached. The membership bitsets / pity floats / event_flags are save-parity
// and already exist in RunState -- no schema bump. S2.13 added a SECOND flags
// word (event_flags_hi) for ids 32..51 by carving it out of declared padding,
// which is also not a schema bump: no offset and no sizeof moved (the proof
// static_asserts live beside the member in run_state.hpp).

#include <cstdint>
#include <type_traits>

#include "sts/engine/map_rooms.hpp"  // RoomType (the onEnterRoom fan-out's gate)
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"      // CardId (event_draw_colorless_uncommon)

namespace sts::engine {

struct RunController;

// Generated event ids (registry/events.yaml; ids 1-31 in canonical Java list
// insertion order). Aliased here the same way potions.hpp aliases PotionId.
using EventId = sts::registry::EventId;

// --- The ?-room roll ---------------------------------------------------------

// EventHelper.RoomResult, restricted to what roll() can actually return.
// ELITE is enumerated for completeness but is UNREACHABLE in this engine's
// scope: the ELITE table fill is inside `if (ModHelper.isModEnabled(
// "DeadlyEvents"))` (EventHelper.java:135-139) and the only other producer is
// the Settings.isEndless MimicInfestation conversion (:165-177); neither mod
// state exists here.
enum class EventRoomResult : uint8_t {
    EVENT = 0,
    MONSTER = 1,
    SHOP = 2,
    TREASURE = 3,
    ELITE = 4,
};

// The pity constants (EventHelper.java:77-88). The RESET_* values for the
// three modelled chances equal their BASE_* values; ELITE_CHANCE is
// deliberately NOT stored in RunState: it only ever feeds `eliteSize`, which
// is zero without the DeadlyEvents daily mod (EventHelper.java:120-125), so
// it is unobservable here.
inline constexpr float kEventBaseMonsterChance = 0.1f;
inline constexpr float kEventBaseShopChance = 0.03f;
inline constexpr float kEventBaseTreasureChance = 0.02f;
inline constexpr float kEventRampMonsterChance = 0.1f;
inline constexpr float kEventRampShopChance = 0.03f;
inline constexpr float kEventRampTreasureChance = 0.02f;

// AbstractDungeon.shrineChance: a protected static float field
// (AbstractDungeon.java:143) assigned exactly once, in the static initializer
// (AbstractDungeon.java:2715), and never touched by any act's
// initializeLevelSpecificChances -- NOT an inline literal in generateEvent.
inline constexpr float kShrineChance = 0.25f;

// The 100-slot roll table (EventHelper.java:132-142), replicated fill-by-fill
// including the ASYMMETRIC clamps: each Arrays.fill runs [min(99, fillIndex),
// min(100, fillIndex + size)). When accumulated sizes reach 100 the from-index
// clamps to 99 while the to-index clamps to 100, so a LATER fill overwrites
// slot 99 even when its own size is 0 -- e.g. sizes (50, 50, 0) put TREASURE
// at slot 99. Exposed so the fill semantics can be pinned exhaustively.
void build_event_roll_table(int monster_size, int shop_size, int treasure_size,
                            EventRoomResult out[100]) noexcept;

// EventHelper.roll (EventHelper.java:100-187): the ?-room room-type roll.
// Draws EXACTLY ONE committed float from rs.event_rng (the only eventRng
// advance in the game, ever), then applies -- in Java statement order --
//   * the Tiny Chest counter++ AFTER the draw, forcing TREASURE at == 4
//     (equality, not >=) and resetting to 0 (:104-113, :144-146). The forced
//     TREASURE is observed by ALL the pity updates below, so a force perturbs
//     pity rather than acting as a transparent post-filter.
//   * table sizes: (int)(chance * 100.0f) C-style truncation (:126-131);
//     `leaving_shop` is `getCurrRoom() instanceof ShopRoom` (:128-130) --
//     setCurrMapNode runs AFTER the roll (:1783), so this is the room being
//     LEFT. eliteSize is 0 (DeadlyEvents-only; also zeroed at floorNum < 6,
//     :123-125, where floorNum is the NEW floor -- already ++'d at :1741).
//   * MONSTER pity (:155-163) in Java order: the predicate sees the pre-Juzu
//     choice, the Juzu Bracelet MONSTER->EVENT conversion at :158 runs INSIDE
//     the branch and BEFORE the reset write at :160.
//   * SHOP pity (:164) and TREASURE pity (:178-185) observe the post-Juzu,
//     post-force choice. The Settings.isEndless MimicInfestation block
//     (:165-177) and every DeadlyEvents ramp are out of scope (mods).
// Pity floats stay float with float arithmetic throughout (trap 19,
// stage-b design 10.19).
[[nodiscard]] EventRoomResult event_room_roll(RunState& rs,
                                              bool leaving_shop) noexcept;

// THE AbstractRelic.onEnterRoom FAN-OUT -- for EVERY room kind, not just `?`.
//
// AbstractDungeon.nextRoomTransition runs `for (r : player.relics)
// r.onEnterRoom(nextRoom.room)` at AbstractDungeon.java:1755-1757. Three facts
// about that line decide this function's whole shape:
//
//  1. IT IS UNCONDITIONAL ON THE ROOM KIND. Its only guard is `nextRoom != null
//     && !isLoadingPostCombatSave` (:1754). Monster, Elite, Rest, Shop,
//     Treasure, Event -- and the BOSS, which DungeonMap.java:77-87 reaches by
//     assigning `nextRoom` a fresh MonsterRoomBoss and calling
//     nextRoomTransitionStart(). RoomType::None IS the `nextRoom == null` case
//     and fires nothing.
//  2. IT SEES THE PRE-ROLL ROOM. The `?` replacement (:1766-1781) and
//     setCurrMapNode (:1783) both run AFTER it, so a `?` is still an EventRoom
//     here however it later resolves. That is why Ssserpent Head
//     (SsserpentHead.java:29-35, `room instanceof EventRoom`) pays on a `?`
//     that becomes a shop, and why `room` must be the ARRIVING map node's kind,
//     never the resolved one.
//  3. IT FIRES EXACTLY ONCE PER TRANSITION. This is a DIFFERENT hook from
//     justEnteredRoom (:1785-1789, dispatch_just_entered_room_relics in
//     shop.hpp), which runs post-roll, post-setCurrMapNode -- and which
//     on_player_entry's `?` recursion therefore runs a second time on purpose.
//     This fan-out must NOT be reached by that recursion: Maw Bank has no room
//     gate, so a second call would pay 12 gold twice on a ?->Shop.
//
// The three S1 bodies, in one acquisition-order loop (a fan-out, not a
// getRelic: a duplicate imported copy fires once per copy, as the Java's relic
// iteration does):
//   * Ssserpent Head  Event only, +50 gold.
//   * Maw Bank        every kind, +12 gold while counter != -2 (MawBank.java:
//                     31-36 has no room condition at all).
//   * Eternal Feather Rest only, heals (masterDeck.size() / 5) * 3.
// Gold goes through gain_gold and HP through heal_out_of_combat, so Ectoplasm's
// suppression and the named onPlayerHeal / onNotBloodied fan-outs apply.
void dispatch_on_enter_room_relics(RunState& rs, RoomType room) noexcept;

// --- Pool membership ---------------------------------------------------------

// Bit index <-> EventId mapping for the three RunState membership bitsets
// (run_state.hpp): bit i of a pool's bitset is a fixed EventId, act-independent
// and defined by REGISTRY ORDER. Only the RUNTIME index of the filtered draw
// list shifts (the draw index is over the filtered list built at draw time,
// never over bit positions).
//
// BIT MEANING AND DRAW POSITION ARE TWO DIFFERENT MAPPINGS, and S2.13 had to
// separate them deliberately:
//
//  * For the EVENT pool the two COINCIDE. Each act's event ids are dense and
//    laid out in Java add order (Exordium 1..11, TheCity 32..44, TheBeyond
//    45..51 -- registry/events.yaml, authored from Exordium.java:223-236 /
//    TheCity.java:184-198 / TheBeyond.java:178-187), so bit i IS
//    `event_list_first_id(act) + i` and IS draw position i. Nothing else is
//    needed. Do not generalise this to shrines.
//
//  * For the SHRINE pool they DIVERGE. Exordium's list ends with Wheel of
//    Change (Exordium.java:238-246) while TheCity's and TheBeyond's -- which
//    are byte-identical to each other -- put it SECOND
//    (TheCity.java:210-218 == TheBeyond.java:198-206). Same six keys, two
//    orders. `shrine_membership`'s bit i therefore keeps its EXORDIUM/registry
//    meaning (bit i == EventId 12 + i) in EVERY act, and the per-act order
//    lives in kShrineDrawOrder* below, consulted ONLY when appending to the
//    draw list. That way round because the bitset is save-parity state: it is
//    byte-compared by the diff harness and mirrored into PublicView, and
//    NOTHING in either path knows the act at compare time, so a bit whose
//    meaning changed at floor 17 would make those comparisons silently
//    act-dependent. It also leaves Act 1 bit-for-bit unchanged (Act 1's order
//    table is the identity).
//
// The per-act EVENT list. Acts 2 and 3 are S2.02's registry ids; the counts are
// the Java list lengths, re-read in full at those lines. `act` is 1..3
// (kFinalAct == 3, run_advance.hpp) and anything else falls to Act 1 -- Act 4 /
// TheEnding is out of S2 scope (s2-design §1) and has no eventList of its own.
[[nodiscard]] constexpr uint16_t event_list_first_id(int act) noexcept {
    return act == 2 ? 32 : (act == 3 ? 45 : 1);
}
[[nodiscard]] constexpr int event_list_count(int act) noexcept {
    return act == 2 ? 13 : (act == 3 ? 7 : 11);
}
// The widest event list (Act 2's 13) bounds every `tmp[]` and every
// `event_membership` mask. uint16_t event_membership covers it with room.
inline constexpr int kEventListMaxCount = 13;
static_assert(event_list_count(1) == 11 && event_list_count(2) == 13 &&
                  event_list_count(3) == 7,
              "per-act event list lengths (Exordium/TheCity/TheBeyond)");
static_assert(event_list_count(1) <= kEventListMaxCount &&
              event_list_count(2) <= kEventListMaxCount &&
              event_list_count(3) <= kEventListMaxCount);
static_assert(kEventListMaxCount <= 16, "event_membership is a uint16_t");
// The last id of each act's list, so a renumber in events.yaml cannot silently
// shift a pool: 11 / 44 / 51.
static_assert(event_list_first_id(1) + event_list_count(1) - 1 == 11);
static_assert(event_list_first_id(2) + event_list_count(2) - 1 == 44);
static_assert(event_list_first_id(3) + event_list_count(3) - 1 == 51);

// Retained Act-1 spellings: the ONE canonical Act-1 list, still named because
// run_begin, the translator and several tier-2 tests speak Act 1 directly.
inline constexpr uint16_t kEventListFirstId = 1;    // EventId 1..11 <-> bits 0..10
inline constexpr int kEventListCount = 11;          // Exordium.java:223-236
inline constexpr uint16_t kShrineListFirstId = 12;  // EventId 12..17 <-> bits 0..5
inline constexpr int kShrineListCount = 6;          // Exordium.java:238-246
inline constexpr uint16_t kSpecialListFirstId = 18; // EventId 18..31 <-> bits 0..13
inline constexpr int kSpecialListCount = 14;        // AbstractDungeon.java:1340-1358
inline constexpr int kNoteForYourselfBit = 9;       // canonical NFY-present position
static_assert(event_list_first_id(1) == kEventListFirstId);
static_assert(event_list_count(1) == kEventListCount);

// Shrine DRAW ORDER: position -> EventId, per act. Act 1 is the identity;
// Acts 2-3 move Wheel of Change from position 5 to position 1 and shift the
// middle four down one. Both lists read in full at the cited lines.
inline constexpr uint16_t kShrineDrawOrderExordium[kShrineListCount] = {
    12,  // Match and Keep!      Exordium.java:240
    13,  // Golden Shrine        :241
    14,  // Transmorgrifier      :242
    15,  // Purifier             :243
    16,  // Upgrade Shrine       :244
    17,  // Wheel of Change      :245
};
inline constexpr uint16_t kShrineDrawOrderCityBeyond[kShrineListCount] = {
    12,  // Match and Keep!      TheCity.java:212 == TheBeyond.java:200
    17,  // Wheel of Change      :213 == :201   <-- the divergence
    13,  // Golden Shrine        :214 == :202
    14,  // Transmorgrifier      :215 == :203
    15,  // Purifier             :216 == :204
    16,  // Upgrade Shrine       :217 == :205
};

// Position -> EventId for the act's shrine draw list. Bit index for the same
// EventId is always `id - kShrineListFirstId`, whatever this returns.
[[nodiscard]] constexpr uint16_t shrine_draw_order_id(int act,
                                                      int position) noexcept {
    return act == 1 ? kShrineDrawOrderExordium[position]
                    : kShrineDrawOrderCityBeyond[position];
}

// isNoteForYourselfAvailable (AbstractDungeon.java:1360-1379), FOUR branches:
// daily run -> false (never modelled); ascensionLevel >= 15 -> false;
// ascensionLevel == 0 -> true; ascensionLevel < prefs("ASCENSION_LEVEL") ->
// true; else false. The A1-A14 case reads a PLAYER-PROFILE preference, so
// membership is not a pure function of (seed, ascension) in general; this
// engine pins the frozen audited reference profile (fully unlocked, highest
// ascension 20 -- the same profile audit the oracle bridge runs against),
// under which A1-A14 resolve to true, collapsing the four branches to
// `ascension < 15`.
[[nodiscard]] constexpr bool note_for_yourself_available(int ascension) noexcept {
    return ascension < 15;
}

// THE ACT-CROSSING ASYMMETRY, and why there are two init functions.
//
// dungeonTransitionSetup CLEARS eventList and shrineList
// (AbstractDungeon.java:2576-2577) and the new dungeon's constructor then
// REBUILDS both (initializeEventList :291, initializeShrineList :293).
// specialOneTimeEventList is ABSENT from that clear list: it is handed to the
// new dungeon BY IDENTITY -- `getDungeon` passes
// `AbstractDungeon.specialOneTimeEventList` to the TheCity / TheBeyond /
// TheEnding constructors (CardCrawlGame.java:1102-1119), and
// initializeSpecialOneTimeEventList has exactly one call site,
// Exordium.<init> (Exordium.java:54). So, per act crossing:
//
//   eventList   cleared + rebuilt -> FULL membership again, with the NEW act's ids
//   shrineList  cleared + rebuilt -> ALL SIX SHRINES RETURN TO THE POOL
//   specialList carried by reference -> an Act-1 draw stays removed for Acts 2-3
//
// The last row is the "one-time pool cross-act depletion" everyone expects.
// The middle row is the one that is easy to get backwards: a shrine drawn in
// Act 1 is drawable again in Act 2 and again in Act 3.
//
// Hence the split. init_event_pools is the run_begin path and fills all three;
// reinit_act_event_pools is the act-crossing path and MUST NOT TOUCH
// special_membership. init_event_pools delegates the shared half so the two
// cannot drift. The NFY conditional is meaningful only for the special list,
// which is a second reason it appears in the run_begin path alone.
void init_event_pools(RunState& rs) noexcept;

// The act-crossing rebuild: event + shrine membership ONLY, sized/shaped by
// rs.act. Call it AFTER rs.act has been advanced (act_transition step (1)) --
// calling it before would install the previous act's list width.
void reinit_act_event_pools(RunState& rs) noexcept;

// --- The one-shot FIRED bitset (ids 1..63 across two words) -------------------

// RunState::event_flags holds ids 1..31 at bit (id-1); RunState::event_flags_hi
// holds ids 32..63 at bit (id-32). The split exists because widening the first
// word in place is illegal in PublicView and would be an unplanned
// SCHEMA_VERSION bump in RunState (run_state.hpp's carve note). ALWAYS go
// through these two; no call site may open-code the shift. An id outside
// [1, 63] is a no-op / false rather than a UB shift.
void event_flag_set(RunState& rs, uint16_t id) noexcept;
[[nodiscard]] bool event_flag_test(const RunState& rs, uint16_t id) noexcept;

// AbstractPlayer.isCursed (AbstractPlayer.java:741-748): any master-deck card
// of type CURSE EXCEPT Necronomicurse, CurseOfTheBell and AscendersBane.
// Ascender's Bane is EXCLUDED -- an A10+ run's starting curse does NOT make
// the player cursed for the Fountain of Cleansing gate. Necronomicurse has no
// S1 registry row; the other two exclusions are represented here.
[[nodiscard]] bool event_player_is_cursed(const RunState& rs) noexcept;

// The filtered draw lists, rebuilt at draw time exactly as the game builds its
// `tmp` ArrayLists. Returns the entry count; writes at most `cap` EventId
// values (as uint16_t) into `out`, in the ACT'S list order. Size `out` for
// build_event_pool to kEventListMaxCount and for build_shrine_pool to
// kShrineListCount + kSpecialListCount.
//
//   * build_event_pool: the act's eventList in add order with ALL SIX gates of
//     AbstractDungeon.getEvent (:1946-1982), every one of which is now live
//     because Acts 2-3 draw:
//       Dead Adventurer  floorNum > 6            :1950-1953   (act 1)
//       Mushrooms        floorNum > 6            :1955-1958   (act 1)
//       The Cleric       gold >= 35              :1965-1968   (act 1)
//       The Moai Head    hasRelic("Golden Idol") || (float)hp/(float)maxHp
//                        <= 0.5f                 :1960-1963   (act 3)
//       Beggar           gold >= 75              :1970-1973   (act 2)
//       Colosseum        currMapNode != null && currMapNode.y > map.size()/2
//                                                :1975-1978   (act 2)
//     Every other row falls through to the unconditional add (:1981).
//     Moai Head's ratio IS a float divide compared against 0.5f, written that
//     way rather than as `hp*2 <= maxHp` (trap 19 float discipline).
//     Colosseum's `map.size()` is the ROW COUNT, 15, so the integer divide is
//     7 and the gate is row >= 8; the row is `run_cur_row`'s arithmetic
//     (floor - act_floor_base(act) - 1, S2.12) derived here from RunState
//     alone -- `currMapNode` is assigned at :1783, BEFORE EventRoom
//     .onPlayerEntry runs, so it is the ARRIVING node. S2.33 (Colosseum's
//     body) inherits this gate rather than re-deriving it.
//
//   * build_shrine_pool: shrineList in THE ACT'S DRAW ORDER
//     (shrine_draw_order_id, :1884 over a list whose order the act's
//     initializeShrineList decided), then specialOneTimeEventList with the
//     per-key gates of getShrine (:1886-1936): Fountain of Cleansing needs
//     isCursed and The Woman in Blue gold >= 50 (both act-independent);
//     FaceTrader is acts 1-2 (NOT act 3); Designer and Duplicator are acts
//     2-3; Knowing Skull, N'loth and The Joust are act 2 ONLY -- N'loth's
//     test is literally written twice against "TheCity" (:1915), which is one
//     test, not two, so it is Act 2 and not "any act but 2". SecretPortal is
//     pinned false in every act; see below.
// Colosseum's row, derived from RunState alone. Identical arithmetic to
// run_cur_row (run_advance.hpp) -- `floor - act_floor_base(act) - 1`, saturated
// at the -1 pre-first-pick sentinel -- restated here because run_advance.hpp
// includes THIS header. Pinned equal to run_cur_row by tier-2 test.
[[nodiscard]] int event_map_row(const RunState& rs) noexcept;

[[nodiscard]] int build_event_pool(const RunState& rs, uint16_t* out,
                                   int cap) noexcept;
[[nodiscard]] int build_shrine_pool(const RunState& rs, uint16_t* out,
                                    int cap) noexcept;

// EventRoom.onPlayerEntry (EventRoom.java:28) + AbstractDungeon.generateEvent
// (:1864-1880): draws the shrine/event split (rng.random(1.0f) < shrineChance)
// and the pool index (rng.random(tmp.size()-1), INCLUSIVE upper bound -- trap
// 3) from a THROWAWAY copy of rs.event_rng, which is discarded on return --
// rs.event_rng is left byte-identical. Normally 2 draws; 1 draw when the
// shrine branch finds both shrine pools raw-empty and falls to the event
// list emptiness checks; 3 draws only through getEvent's empty-filtered-tmp
// fallback into getShrine (:1983-1985).
//
// COMMITS (even though the stream does not): the selected id's pool bit
// (getShrine removes from BOTH shrineList and specialOneTimeEventList,
// :1938-1939; getEvent from eventList, :1987) and the matching FIRED bit via
// event_flag_set -- the engine-side record mirroring the game's
// saveFileLastEventChoice write (EventHelper.java:228). Ids 32..51 land in
// event_flags_hi; see the accessors above.
//
// Returns the selected EventId as uint16_t, or 0 when every pool is empty
// (the game logs "No events or shrines left" and returns null, :1872-1873).
//
// THE RAW-NONEMPTY / FILTERED-EMPTY SHRINE STATE. generate_event's shrine
// branch tests the RAW lists (:1866); getShrine then filters. When the filter
// empties `tmp` the game evaluates `tmp.get(rng.random(-1))` (:1937), and
// `Random.random(int)` increments its counter and then calls nextInt(0), which
// libgdx's RandomXS128 rejects -- i.e. THE GAME CRASHES, after burning one
// counter tick. This port returns 0 without drawing instead: a documented
// defensive deviation.
//
// That state used to be justified as structurally unreachable ("the Act-1 pool
// depths cannot reach it"). THAT JUSTIFICATION DIES IN ACT 3 and the honest
// statement is now this: reaching it needs shrineList empty (all six shrines
// drawn WITHIN the current act -- they are restored at every crossing) AND
// every surviving special filtered out. In Act 3 the filter is at its
// strongest (FaceTrader is act-excluded, Knowing Skull / N'loth / The Joust
// are act-2 only, SecretPortal is pinned false), so the state is constructible
// rather than impossible -- low probability, not zero. The deviation is
// therefore a real behavioural difference from the game on a reachable state,
// not a formality, and it is pinned by test rather than argued away.
[[nodiscard]] uint16_t generate_event(RunState& rs) noexcept;

// --- The dialog framework ----------------------------------------------------

// Cap on simultaneously offered dialog options. Dialog-button screens top out
// at 4; the cap is set by the two BOARD screens that expose one option per
// slot: Match and Keep's 12-card board and The Library's 20-card read pile
// (TheLibrary.java:66-91 -- twenty unique-by-cardID getCard(rollRarity())
// draws, pick exactly one). Raised 12 -> 20 by S2.32 for the Library; this cap
// sizes RunActionMask.can_choose_event_option and therefore the PublicView
// mask channel, so moving it is a PUBLIC_VIEW_VERSION event (v6).
inline constexpr int kEventOptionCap = 20;

// One dialog screen's options, rebuilt from scratch on every legal_actions
// call (never cached), exactly like build_rest_menu. `enabled[i]` for
// i < count mirrors the game's greying-out of conditional options.
struct EventDialogMenu {
    uint8_t count;
    uint8_t pad[3];
    bool enabled[kEventOptionCap];
};

static_assert(std::is_trivially_copyable_v<EventDialogMenu>);

// Match and Keep deals a fixed twelve-card board (six identities, each dealt
// twice) and the player flips ten of them across five attempts
// (GremlinMatchGame.java:55-92, 179-244). That board is per-visit screen state
// the game holds in the event object, so it lives here beside `scratch*`
// rather than in the save-parity RunState. S2.32's The Library is the second
// board user -- twenty rolled cards, pick one (TheLibrary.java:66-91) -- and
// it is what moved the cap 12 -> 20 (all M&K indices are unchanged; the eight
// extra slots simply stay empty for it). The board is mirrored into
// PublicView, so this cap is part of the v6 layout.
inline constexpr int kEventBoardCap = 20;

// Match and Keep's OWN board size -- the twelve dealt cards
// (GremlinMatchGame.java:55-92). Split from kEventBoardCap when the Library
// widened the cap: every M&K loop (the deal, the option surface, the pick
// bound, the resampler's hidden-slot permutation) runs over THIS constant, so
// the cap's spare slots can never enter its board.
inline constexpr int kMatchBoardSize = 12;
static_assert(kMatchBoardSize <= kEventBoardCap);

struct EventBoardCard {
    uint16_t card_id;  // CardId; 0 (CardId::NONE) in an unused slot
    uint8_t upgrade;   // makeStatEquivalentCopy preserves the preview upgrade
    uint8_t taken;     // 1 once the pair was matched and left the board
};

static_assert(std::is_trivially_copyable_v<EventBoardCard>);

// The transient dialog state: which event is live and where its dialog is.
// Lives in RunController (transient screen flow), NOT RunState -- the game
// derives it (a reload reconstructs the event from (seed, eventRng.counter)
// via EventRoom.onPlayerEntry). `screen`/`scratch*`/`board` are event-defined
// storage for multi-page dialogs, per-visit counters and the one card board
// (each event body owns their meanings); value-init means "no event".
struct EventDialogState {
    uint16_t event_id;  // EventId as u16 (0 = none); kSyntheticEventId in tests
    uint8_t screen;     // event-defined page index (0 = entry screen)
    uint8_t grid_kind;  // EventGridKind; 0 while an ordinary dialog is visible
    int16_t scratch0;   // event-defined
    int16_t scratch1;   // event-defined
    int16_t scratch2;   // event-defined (We Meet Again offers three trades)
    int16_t scratch3;   // event-defined
    EventBoardCard board[kEventBoardCap];  // Match and Keep only
};

static_assert(std::is_trivially_copyable_v<EventDialogState>);
static_assert(sizeof(EventDialogState) == 12 + 4 * kEventBoardCap);

// Reusable event-origin master-deck grid. The current body selects the screen
// transition; this enum supplies the common legality predicate and mutation
// door. Cleric/Golden Wing exercise PURGE now; UPGRADE/TRANSFORMABLE reserve
// the same non-overlapping screen shape for Living Wall and shrine bodies.
// TRANSFORMABLE_ANY is TRANSFORMABLE's Act-2 sibling and exists because Drug
// Dealer is the ONE event grid in the game that does not exclude bottled cards:
// `gridSelectScreen.open(player.masterDeck.getPurgeableCards(), 2, ...)`
// (DrugDealer.java:128) has no getGroupWithoutBottledCards wrapper, unlike
// Living Wall's Change (LivingWall.java:102-103) and Transmogrifier
// (Transmogrifier.java:65). The two kinds differ ONLY in that clause; the
// mutation is the same transform_card door. It also takes TWO picks, which is
// the body's business (it keeps the grid open across the first), not this
// enum's.
// S2.32 adds two:
//   DUPLICATE -- the Duplicator's grid opens over the WHOLE master deck
//     (gridSelectScreen.open(player.masterDeck, 1, ...), Duplicator.java:87):
//     no purgeable filter, no bottled exclusion, so every index is legal.
//   TRANSFORM_PAIR_SECOND -- the second pick of the Designer's two-card
//     transform (Designer.java:200, numCards == 2 over the unbottled
//     purgeable group): same legality as TRANSFORMABLE minus the card already
//     selected, whose deck index the body parks in scratch3. The only
//     consumer is the Designer; the scratch3 read is documented there.
enum class EventGridKind : uint8_t {
    NONE = 0,
    PURGE = 1,
    UPGRADE = 2,
    TRANSFORMABLE = 3,
    TRANSFORMABLE_ANY = 4,
    DUPLICATE = 5,
    TRANSFORM_PAIR_SECOND = 6,
};

void open_event_grid(EventDialogState& es, EventGridKind kind) noexcept;
void close_event_grid(EventDialogState& es) noexcept;
[[nodiscard]] bool event_grid_card_legal(const RunState& rs,
                                         const EventDialogState& es,
                                         uint16_t deck_index) noexcept;
[[nodiscard]] bool event_grid_remove_card(RunState& rs,
                                          EventDialogState& es,
                                          uint16_t deck_index) noexcept;
[[nodiscard]] bool event_grid_upgrade_card(RunState& rs,
                                           EventDialogState& es,
                                           uint16_t deck_index) noexcept;
[[nodiscard]] bool event_grid_transform_card(RunState& rs,
                                             EventDialogState& es,
                                             RngStream& misc_rng,
                                             uint16_t deck_index) noexcept;

// Event damage keeps the Java DamageInfo owner explicit even though the two
// owners used by this batch currently produce the same RunState HP delta.
enum class EventDamageOwner : uint8_t {
    NULL_SOURCE = 0,
    PLAYER = 1,
};

[[nodiscard]] bool apply_event_damage(RunController& rc, int32_t amount,
                                      EventDamageOwner owner) noexcept;

// What a dialog choice did to the flow.
enum class EventDialogStatus : uint8_t {
    CONTINUE = 0,     // stay in EVENT_DIALOG (a new screen/page is up)
    FINISHED = 1,     // the event is over; the run proceeds to the map
    TRANSITIONED = 2, // the body changed RunController phase/screen itself
};

// One event body's dialog implementation. Bodies receive the whole transient
// RunController because the immediate content batches need floor-scoped RNG,
// reward/grid state, and combat/screen transitions -- none live in RunState.
// A body returns TRANSITIONED after installing such a phase/screen; step_one
// leaves the controller exactly as the body set it. FINISHED is the ordinary
// proceed-to-map path and CONTINUE keeps the dialog up.
struct EventDialogImpl {
    void (*on_enter)(RunController& rc, EventDialogState& es);
    void (*build_menu)(const RunController& rc, const EventDialogState& es,
                       EventDialogMenu& out);
    EventDialogStatus (*choose)(RunController& rc, EventDialogState& es,
                                uint8_t option);
};

// The per-event dispatch. Implemented native rows are emitted by the registry;
// an unimplemented id returns nullptr and parks the run at ROOM_UNIMPLEMENTED,
// but only AFTER generate_event has committed the exact selection and
// pool-removal bookkeeping.
[[nodiscard]] const EventDialogImpl* event_dialog_impl(uint16_t event_id) noexcept;

// AbstractDungeon.returnColorlessCard(UNCOMMON) (AbstractDungeon.java:
// 1100-1113): ONE shuffleRng.randomLong (rc.combat.shuffle_rng, the floor
// stream) drives a JDK shuffle of the LIVE colorlessCardPool IN PLACE, then
// the FIRST entry of the requested rarity wins; SwiftStrike is the never-taken
// fallback. The live pool's order is rc.colorless_order (run_advance.hpp) --
// persistent across calls within an act because the game's shuffle mutates the
// list it reads, which is observable from the SECOND call on (Knowing Skull
// can buy several cards in one visit). Consumers: Match and Keep's A<15 slot
// (shrines.cpp, which used a local-copy shuffle until this second consumer
// forced the persistent order) and Knowing Skull's CARD purchase.
// Implemented in event_framework.cpp beside the other pool machinery.
[[nodiscard]] CardId event_draw_colorless_uncommon(RunController& rc) noexcept;

// initializeCardPools' colorless fill (AbstractDungeon.java:1203-1210): the
// LIVE pool returns to plain CardLibrary library order -- the REVERSED emitted
// kColorlessPool -- in EVERY dungeon constructor, i.e. at run start and at
// each act crossing. run_begin and act_transition both call this.
void init_colorless_order(RunController& rc) noexcept;

// --- The Colosseum reopen seam (city_events_ii.cpp) --------------------------
//
// Colosseum.reopen (Colosseum.java:100-110) is the ONE non-empty
// AbstractEvent.reopen override in the game, and Colosseum.buttonEffect:55 is
// the one `rewardAllowed = false` writer -- so the "combat ended, return to
// the event dialog instead of a reward screen" edge exists for exactly one
// event and is keyed on it by name in finish_combat_after_action.
//
// event_combat_reopens: true iff the just-finished event combat belongs to a
// Colosseum whose dialog state says reopen() would re-enter the image (screen
// != LEAVE -- i.e. the Slavers fight, whatever its survivor outcome: victory,
// Smoke Bomb, or a mug). The caller has already handled the defeat branch.
//
// event_combat_reopen: the battle-over + reopen tail, run AFTER fold_back:
//   (1) dropReward() -- AbstractRoom's body is EMPTY for an EventRoom
//       (AbstractRoom.java:454-455); nothing to do.
//   (2) addPotionToRewards() (AbstractRoom.java:330-341 run it even with
//       rewardAllowed false): one potionRng chance roll at 40 + blizzard mod
//       (EventRoom arm), the +/-10 ratchet, and on a hit the returnRandomPotion
//       draws -- whose item reopen()'s rewards.clear() then throws away.
//   (3) reopen(): resetPlayer (transient), preBattlePrep -- whose ONE
//       state-visible line is drawPile.initializeDeck's
//       shuffleRng.randomLong() (CardGroup.java:929-931), advancing the floor
//       stream the Nobs fight will inherit -- then the atPreBattle relic pass
//       (fired against the folded-back combat mirror: Snecko's Confusion lands
//       on a dead CombatState and is discarded, exactly as the game's lands on
//       a player the next preBattlePrep resets; a future atPreBattle body that
//       draws floor RNG at dispatch time advances the preserved streams, which
//       is the point), monsters.usePreBattleAction on the dead Slavers (both
//       inherit the empty base body -- no-op), and enterImageFromCombat's
//       rewards.clear().
// Leaves rc in EVENT_DIALOG at the POST_COMBAT screen.
[[nodiscard]] bool event_combat_reopens(const RunController& rc) noexcept;
void event_combat_reopen(RunController& rc) noexcept;

// The framework's proof body (the "minimal synthetic seam"): a two-screen
// dialog with a conditional (gold-gated) option, wired to a reserved id that
// no pool contains and generate_event can never return. It exists so the
// EVENT_DIALOG phase plumbing is exercised by tests instead of lying dead
// until the first real event body lands; it is unreachable in production play.
inline constexpr uint16_t kSyntheticEventId = 0xFFFF;

}  // namespace sts::engine
