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
//   * Random(Long,int) counter replay     Random.java:28-33
//   * TinyChest                           TinyChest.java:19-42
//   * SsserpentHead.onEnterRoom           SsserpentHead.java:29-35
//   * MawBank.onEnterRoom                 MawBank.java:31-36
//   * AbstractPlayer.isCursed             AbstractPlayer.java:741-748
//
// Transient screen state (EventDialogState) lives in RunController, not
// RunState, exactly like RestSiteState (rationale at run_advance.hpp:13-21);
// dialog options are rebuilt from scratch on every legal_actions call, never
// cached. The membership bitsets / pity floats / event_flags are save-parity
// and already exist in RunState (run_state.hpp:135,160-162,181-184) -- no
// schema bump.

#include <cstdint>
#include <type_traits>

#include "sts/engine/run_state.hpp"

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

// The AbstractRelic.onEnterRoom fan-out for the ORIGINAL map room runs before
// EventHelper.roll replaces an EventRoom with its resolved room
// (AbstractDungeon.nextRoomTransition, AbstractDungeon.java:1754-1779).
// Therefore Ssserpent Head gains 50 gold and an unused Maw Bank gains 12 on
// every ? entry even when the roll becomes MONSTER / SHOP / TREASURE. One
// call per held copy, in relic order; gain_gold carries Ectoplasm's
// suppression. Other room types remain owned by their room-entry tasks.
void dispatch_event_room_entry_relics(RunState& rs) noexcept;

// --- Pool membership ---------------------------------------------------------

// Bit index <-> EventId mapping for the three RunState membership bitsets
// (run_state.hpp:181-184): bit i of a pool's bitset is the entry at position i
// of the act's canonical (NFY-PRESENT) init list == registry order.
// EventId is the identity; only the RUNTIME index of the filtered draw list
// shifts (the draw index is over the filtered list built at draw time, never
// over bit positions).
inline constexpr uint16_t kEventListFirstId = 1;    // EventId 1..11 <-> bits 0..10
inline constexpr int kEventListCount = 11;          // Exordium.java:223-236
inline constexpr uint16_t kShrineListFirstId = 12;  // EventId 12..17 <-> bits 0..5
inline constexpr int kShrineListCount = 6;          // Exordium.java:238-246
inline constexpr uint16_t kSpecialListFirstId = 18; // EventId 18..31 <-> bits 0..13
inline constexpr int kSpecialListCount = 14;        // AbstractDungeon.java:1340-1358
inline constexpr int kNoteForYourselfBit = 9;       // canonical NFY-present position

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

// Populate the three membership bitsets with the full Act-1 canonical lists
// (initializeEventList / initializeShrineList / the NFY-conditional
// initializeSpecialOneTimeEventList). Called from run_begin; the RunState
// storage already existed, so this is population only -- no schema change.
void init_event_pools(RunState& rs) noexcept;

// AbstractPlayer.isCursed (AbstractPlayer.java:741-748): any master-deck card
// of type CURSE EXCEPT Necronomicurse, CurseOfTheBell and AscendersBane.
// Ascender's Bane is EXCLUDED -- an A10+ run's starting curse does NOT make
// the player cursed for the Fountain of Cleansing gate. Of the three
// exclusions only Ascender's Bane has a registry row today; the other two
// must be added to the exclusion when their rows land.
[[nodiscard]] bool event_player_is_cursed(const RunState& rs) noexcept;

// The filtered draw lists, rebuilt at draw time exactly as the game builds its
// `tmp` ArrayLists. Returns the entry count; writes at most `cap` EventId
// values (as uint16_t) into `out`, in list order.
//   * build_event_pool: eventList in canonical order with the Act-1 gates of
//     AbstractDungeon.getEvent (:1949-1982): Dead Adventurer and Mushrooms
//     need floorNum > 6, The Cleric needs gold >= 35; the other 8 rows are
//     unconditional. (The Moai Head / Beggar / Colosseum cases guard act-2/3
//     list keys that Exordium's list never holds.)
//   * build_shrine_pool: shrineList unconditionally (:1884), then
//     specialOneTimeEventList with the per-key gates of getShrine
//     (:1886-1936): Fountain of Cleansing needs isCursed; The Woman in Blue
//     needs gold >= 50; FaceTrader is act 1/2; Designer, Duplicator, Knowing
//     Skull, N'loth, The Joust and SecretPortal are act-2/3-gated (SecretPortal
//     additionally needs playtime >= 800s -- unmodelled, act-gated out of S1).
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
// :1938-1939; getEvent from eventList, :1987) and the matching
// RunState.event_flags bit (id-1) -- the engine-side "fired" record mirroring
// the game's saveFileLastEventChoice write (EventHelper.java:228).
//
// Returns the selected EventId as uint16_t, or 0 when every pool is empty
// (the game logs "No events or shrines left" and returns null, :1872-1873).
// A raw-nonempty-but-filtered-empty shrine pool would make the game index
// tmp.get(rng.random(-1)) and throw (:1937); this port returns 0 without
// drawing instead -- a documented defensive deviation on a state the Act-1
// pool depths cannot reach through this API.
[[nodiscard]] uint16_t generate_event(RunState& rs) noexcept;

// --- The dialog framework ----------------------------------------------------

// Cap on simultaneously offered dialog options. Act-1 dialog screens top out
// well below this (largest is 4 buttons); grid-style screens (e.g. Match and
// Keep's 12-card board) may instead reuse the card-screen machinery -- their
// owning content task decides.
inline constexpr int kEventOptionCap = 12;

// One dialog screen's options, rebuilt from scratch on every legal_actions
// call (never cached), exactly like build_rest_menu. `enabled[i]` for
// i < count mirrors the game's greying-out of conditional options.
struct EventDialogMenu {
    uint8_t count;
    uint8_t pad[3];
    bool enabled[kEventOptionCap];
};

static_assert(std::is_trivially_copyable_v<EventDialogMenu>);

// The transient dialog state: which event is live and where its dialog is.
// Lives in RunController (transient screen flow), NOT RunState -- the game
// derives it (a reload reconstructs the event from (seed, eventRng.counter)
// via EventRoom.onPlayerEntry). `screen`/`scratch*` are event-defined
// storage for multi-page dialogs and per-visit counters (each event body
// owns their meanings); value-init means "no event".
struct EventDialogState {
    uint16_t event_id;  // EventId as u16 (0 = none); kSyntheticEventId in tests
    uint8_t screen;     // event-defined page index (0 = entry screen)
    uint8_t pad;        // explicit padding
    int16_t scratch0;   // event-defined
    int16_t scratch1;   // event-defined
};

static_assert(std::is_trivially_copyable_v<EventDialogState>);
static_assert(sizeof(EventDialogState) == 8);

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

// The per-event dispatch. Returns nullptr for every event id whose body is
// not implemented -- ALL 31 native events today (their bodies are the
// follow-on content tasks). A null here parks the run at ROOM_UNIMPLEMENTED,
// but only AFTER generate_event has committed the exact selection and
// pool-removal bookkeeping, so the parked state is byte-identical to the
// pre-body prefix of the eventual full flow.
[[nodiscard]] const EventDialogImpl* event_dialog_impl(uint16_t event_id) noexcept;

// The framework's proof body (the "minimal synthetic seam"): a two-screen
// dialog with a conditional (gold-gated) option, wired to a reserved id that
// no pool contains and generate_event can never return. It exists so the
// EVENT_DIALOG phase plumbing is exercised by tests instead of lying dead
// until the first real event body lands; it is unreachable in production play.
inline constexpr uint16_t kSyntheticEventId = 0xFFFF;

}  // namespace sts::engine
