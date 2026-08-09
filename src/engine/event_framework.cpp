// The ?-room roll, event selection and dialog dispatch. See
// event_framework.hpp for the RNG contract, provenance, and design.

#include "sts/engine/event_framework.hpp"

#include <algorithm>
#include <cstdint>
#include <span>

#include "relics/relic_pickup.hpp"    // gain_gold / heal_out_of_combat doors
#include "sts/engine/rng_jdk.hpp"     // JdkRandom / jdk_shuffle (colorless order)
#include "sts/engine/card_pools.hpp"  // transform_card (the ONE transformCard list)
#include "sts/engine/cards.hpp"       // card_def / CardType (isCursed)
#include "sts/engine/potions.hpp"     // potion_def / PotionId (Fairy in a Bottle)
#include "sts/engine/relics.hpp"      // RelicId (Tiny Chest / Juzu Bracelet)
#include "sts/engine/rng_stream.hpp"  // random() wrappers
#include "sts/engine/rest_sites.hpp"  // shared purge/upgrade legality
#include "sts/engine/run_advance.hpp" // RunController for dialog bodies
#include "sts/engine/run_deck.hpp"    // remove_master_deck_card
#include "sts/registry/event_table.hpp" // generated implemented-body dispatch

namespace sts::engine {

namespace {

// run_has_relic's shape (combat_rewards.cpp has its own copy too); local so
// this TU depends only on RunState.
[[nodiscard]] bool has_relic(const RunState& rs, RelicId id) noexcept {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

}  // namespace

// --- The roll table ----------------------------------------------------------

void build_event_roll_table(int monster_size, int shop_size, int treasure_size,
                            EventRoomResult out[100]) noexcept {
    // Arrays.fill(possibleResults, RoomResult.EVENT) (EventHelper.java:134).
    for (int i = 0; i < 100; ++i) {
        out[i] = EventRoomResult::EVENT;
    }
    // Each Java fill is [from, to) with from = min(99, fillIndex) and
    // to = min(100, fillIndex + size) -- the asymmetric clamp pair
    // (EventHelper.java:140-142). Replicated literally, including the
    // slot-99 overwrite a later fill performs once fillIndex >= 100.
    const auto fill = [&out](int from, int to, EventRoomResult v) noexcept {
        for (int i = from; i < to; ++i) {
            out[i] = v;
        }
    };
    const auto min_i = [](int a, int b) noexcept { return a < b ? a : b; };
    int fill_index = 0;
    // The two DeadlyEvents ELITE fills (:135-139) are mod-gated and skipped;
    // fillIndex stays 0 into the MONSTER fill exactly as in the unmodded game.
    fill(min_i(99, fill_index), min_i(100, fill_index + monster_size),
         EventRoomResult::MONSTER);
    fill_index += monster_size;
    fill(min_i(99, fill_index), min_i(100, fill_index + shop_size),
         EventRoomResult::SHOP);
    fill_index += shop_size;
    fill(min_i(99, fill_index), min_i(100, fill_index + treasure_size),
         EventRoomResult::TREASURE);
}

// --- The room-entry relic fan-out ---------------------------------------------

void dispatch_on_enter_room_relics(RunState& rs, RoomType room) noexcept {
    // AbstractDungeon.nextRoomTransition iterates every relic's onEnterRoom
    // against nextRoom.room (AbstractDungeon.java:1755-1757) -- unconditional on
    // the room kind, BEFORE the ? roll replaces an EventRoom (:1766-1781) and
    // before setCurrMapNode (:1783). The full contract, and why this must NOT
    // be reached by on_player_entry's ? recursion, is in event_framework.hpp.
    //
    // `nextRoom != null` is the Java's only guard (:1754); RoomType::None is
    // that null room.
    if (room == RoomType::None) {
        return;
    }
    // A fan-out, not getRelic: a malformed duplicate import fires once per copy
    // as the Java relic iteration does.
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        switch (static_cast<RelicId>(rs.relics[i].relic_id)) {
            case RelicId::SSSERPENT_HEAD:
                // `if (room instanceof EventRoom)` (SsserpentHead.java:29-35).
                // The PRE-roll room is what is tested, so a ? that later
                // becomes a monster, shop or chest still pays.
                if (room == RoomType::Event) {
                    gain_gold(rs, 50);
                }
                break;
            case RelicId::MAW_BANK:
                // `if (!this.usedUp) { flash(); player.gainGold(12); }`
                // (MawBank.java:31-36) -- NO room condition whatsoever, so
                // every kind pays, boss node included. usedUp is encoded as
                // counter == -2 (setCounter, MawBank.java:47-53), the same
                // encoding dispatch_relics_on_spend_gold writes.
                if (rs.relics[i].counter != -2) {
                    gain_gold(rs, 12);
                }
                break;
            case RelicId::ETERNAL_FEATHER:
                // `if (room instanceof RestRoom) { flash();
                //    int amountToGain = player.masterDeck.size() / 5 * 3;
                //    player.heal(amountToGain); }`
                // (EternalFeather.java:29-35). INTEGER DIVISION FIRST, then x3:
                // a 14-card deck heals 6, not 8.
                //
                // This is the room ENTRY, not the campfire. RestRoom's own
                // onPlayerEntry -- which builds the CampfireUI and fires
                // onEnterRestRoom (RestRoom.java:33-43) -- runs later, at
                // AbstractDungeon.java:1800. So the heal lands before the menu
                // exists and whether or not the player then rests, and it is
                // NOT rest_apply_heal (which is the REST option's body).
                //
                // Magic Flower cannot scale it: MagicFlower.onPlayerHeal
                // (MagicFlower.java:30-37) is gated on
                // `getCurrRoom().phase == RoomPhase.COMBAT`, and this fan-out
                // precedes even setCurrMapNode. heal_out_of_combat names that
                // fan-out and the not-bloodied cross in full.
                if (room == RoomType::Rest) {
                    heal_out_of_combat(
                        rs, static_cast<int32_t>(rs.master_deck_count) / 5 * 3);
                }
                break;
            default:
                break;
        }
    }
}

EventRoomResult event_room_roll(RunState& rs, bool leaving_shop) noexcept {
    // float roll = eventRng.random() (EventHelper.java:102) -- THE one
    // committed eventRng draw. Everything after this line is draw-free.
    const float roll = random(rs.event_rng);

    // Tiny Chest (:104-113): counter++ AFTER the draw; force at == 4
    // (equality -- an imported counter past 4 keeps climbing, exactly as the
    // game's would), then reset to 0. getRelic returns the first instance.
    bool force_chest = false;
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id != static_cast<uint16_t>(RelicId::TINY_CHEST)) {
            continue;
        }
        ++rs.relics[i].counter;
        if (rs.relics[i].counter == 4) {
            rs.relics[i].counter = 0;
            force_chest = true;
        }
        break;
    }

    // Sizes (:119-131): C-style (int) truncation of chance * 100.0f, float
    // multiply (trap 19 -- map_rooms.hpp float discipline). eliteSize is 0:
    // its only non-zero assignment is DeadlyEvents-gated (:120-122), and the
    // floorNum < 6 zeroing (:123-125) -- which reads the NEW floor, already
    // incremented at nextRoomTransition:1741 -- can only re-zero it.
    const int monster_size = static_cast<int>(rs.event_pity_monster * 100.0f);
    int shop_size = static_cast<int>(rs.event_pity_shop * 100.0f);
    if (leaving_shop) {
        // getCurrRoom() instanceof ShopRoom (:128-130): setCurrMapNode runs
        // after the roll (AbstractDungeon.java:1783), so this is the room
        // being LEFT -- "no shop out of a ? if you just came from a shop".
        shop_size = 0;
    }
    const int treasure_size = static_cast<int>(rs.event_pity_treasure * 100.0f);

    EventRoomResult table[100];
    build_event_roll_table(monster_size, shop_size, treasure_size, table);

    // choice = possibleResults[(int)(roll * 100.0f)] (:143), then the Tiny
    // Chest override (:144-146) BEFORE any pity update -- all four updates
    // below observe the forced TREASURE.
    EventRoomResult choice = table[static_cast<int>(roll * 100.0f)];
    if (force_chest) {
        choice = EventRoomResult::TREASURE;
    }

    // ELITE pity (:147-154): updates ELITE_CHANCE, which is deliberately not
    // stored (header note) -- without DeadlyEvents it can never reach a table.

    // MONSTER pity (:155-163), in Java statement order: the predicate sees the
    // pre-Juzu choice; the Juzu Bracelet conversion (:158) runs INSIDE the
    // branch and BEFORE the reset write (:160). The net effect here is
    // identical either way, but the order is load-bearing on the endless
    // TREASURE branch (:165-177), so it is written as the Java writes it.
    if (choice == EventRoomResult::MONSTER) {
        if (has_relic(rs, RelicId::JUZU_BRACELET)) {
            choice = EventRoomResult::EVENT;                    // :158
        }
        rs.event_pity_monster = kEventBaseMonsterChance;        // :160
    } else {
        rs.event_pity_monster += kEventRampMonsterChance;       // :162
    }

    // SHOP pity (:164) -- observes the post-Juzu choice.
    if (choice == EventRoomResult::SHOP) {
        rs.event_pity_shop = kEventBaseShopChance;
    } else {
        rs.event_pity_shop += kEventRampShopChance;
    }

    // TREASURE pity: the Settings.isEndless MimicInfestation block (:165-177)
    // is out of scope, leaving the plain branch (:178-185); the DeadlyEvents
    // extra ramp (:182-184) is mod-gated and skipped. Observes the post-Juzu,
    // post-force choice.
    if (choice == EventRoomResult::TREASURE) {
        rs.event_pity_treasure = kEventBaseTreasureChance;
    } else {
        rs.event_pity_treasure += kEventRampTreasureChance;
    }

    return choice;
}

// --- Pool membership ---------------------------------------------------------

void reinit_act_event_pools(RunState& rs) noexcept {
    // initializeEventList (AbstractDungeon.java:291) / initializeShrineList
    // (:293), run by EVERY dungeon constructor after dungeonTransitionSetup
    // cleared both lists (:2576-2577). Both are draw-free. Deliberately does
    // NOT touch special_membership: that list is carried by reference across
    // the crossing (CardCrawlGame.java:1102-1119) -- see the header.
    const int act = static_cast<int>(rs.act);
    rs.event_membership =
        static_cast<uint16_t>((1u << event_list_count(act)) - 1u);
    rs.shrine_membership =
        static_cast<uint8_t>((1u << kShrineListCount) - 1u);        // bits 0..5
}

void init_event_pools(RunState& rs) noexcept {
    // run_begin only. The event + shrine half is the same code the crossing
    // runs, delegated so the two cannot drift; the special half is the
    // Exordium-only initializeSpecialOneTimeEventList (Exordium.java:54).
    reinit_act_event_pools(rs);
    uint16_t special = static_cast<uint16_t>((1u << kSpecialListCount) - 1u);
    if (!note_for_yourself_available(rs.ascension)) {
        // No placeholder slot: the add itself is guarded
        // (AbstractDungeon.java:1351-1353). The BIT keeps its canonical
        // NFY-present meaning and is simply never set, so bit meanings never
        // shift with ascension; only the filtered draw list shortens.
        special = static_cast<uint16_t>(special & ~(1u << kNoteForYourselfBit));
    }
    rs.special_membership = special;
}

// --- The one-shot FIRED bitset ------------------------------------------------

void event_flag_set(RunState& rs, uint16_t id) noexcept {
    if (id >= 1u && id <= 31u) {
        rs.event_flags |= 1u << (id - 1u);
    } else if (id >= 32u && id <= 63u) {
        rs.event_flags_hi |= 1u << (id - 33u);
    }
    // Anything else (0 == EventId::NONE, or a future id past 63) is a no-op
    // rather than a UB shift. A 64th id needs a third word, not a wider shift.
}

bool event_flag_test(const RunState& rs, uint16_t id) noexcept {
    if (id >= 1u && id <= 31u) {
        return (rs.event_flags & (1u << (id - 1u))) != 0u;
    }
    if (id >= 32u && id <= 63u) {
        return (rs.event_flags_hi & (1u << (id - 33u))) != 0u;
    }
    return false;
}

bool event_player_is_cursed(const RunState& rs) noexcept {
    // AbstractPlayer.isCursed (AbstractPlayer.java:741-748). Exclusions, all
    // three now registry rows: Necronomicurse, CurseOfTheBell, AscendersBane.
    // The list is complete against :744 -- there is no fourth name there, and
    // Pride (excluded from the CURSE POOLS by name) is deliberately absent from
    // this one, so a Pride holder counts as cursed even though the pools skip it.
    // Note the Java compares with `==` on interned ID literals rather than
    // equals(); the cardIDs come from the classes' compile-time String
    // constants, so the reference comparison does hold.
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        const CardId id = static_cast<CardId>(rs.master_deck[i].card_id);
        if (id == CardId::ASCENDERS_BANE ||
            id == CardId::CURSE_OF_THE_BELL ||
            id == CardId::NECRONOMICURSE) {
            continue;
        }
        const CardDef* def = card_def(id);
        if (def != nullptr && def->type == CardType::CURSE) {
            return true;
        }
    }
    return false;
}

int event_map_row(const RunState& rs) noexcept {
    // run_cur_row's arithmetic (run_advance.hpp), spelled over RunState alone
    // so build_event_pool needs no RunController: the 0-based row of the node
    // being ARRIVED at is `floor - act_floor_base(act) - 1`, saturating at -1
    // for the pre-first-pick sentinel. act_floor_base is (act-1) * 17 (S2.12,
    // kActFloorSpan) -- restated here rather than included because
    // run_advance.hpp includes this header, not the other way round. The two
    // spellings are pinned equal across every act x floor by
    // S213EventGates.EventMapRowAgreesWithRunCurRow, which is the control that
    // stops them drifting and silently moving Colosseum's gate.
    const int base = (static_cast<int>(rs.act) - 1) * 17;
    const int row = static_cast<int>(rs.floor) - base - 1;
    return row < -1 ? -1 : row;
}

int build_event_pool(const RunState& rs, uint16_t* out, int cap) noexcept {
    // AbstractDungeon.getEvent's tmp build (:1946-1982) over THE ACT'S
    // eventList in add order. floorNum here is rs.floor -- the new floor,
    // already incremented before onPlayerEntry runs.
    const int act = static_cast<int>(rs.act);
    const uint16_t first_id = event_list_first_id(act);
    const int count = event_list_count(act);
    int n = 0;
    for (int i = 0; i < count; ++i) {
        if ((rs.event_membership & (1u << i)) == 0u) {
            continue;
        }
        const uint16_t id = static_cast<uint16_t>(first_id + i);
        bool eligible = true;
        switch (static_cast<EventId>(id)) {
            case EventId::DEAD_ADVENTURER:  // floorNum <= 6 -> skip (:1950-1953)
            case EventId::MUSHROOMS:        // same gate (:1955-1958)
                eligible = rs.floor > 6;
                break;
            case EventId::THE_CLERIC:       // gold < 35 -> skip (:1965-1968)
                eligible = rs.gold >= 35;
                break;
            case EventId::THE_MOAI_HEAD:
                // `if (!hasRelic("Golden Idol") && (float)currentHealth /
                //     (float)maxHealth > 0.5f) continue;` (:1960-1963).
                // Written as the Java writes it: a FLOAT divide compared
                // against 0.5f, not hp*2 <= maxHp (trap 19). max_hp is never 0
                // on a live run, but guard the divide anyway.
                eligible = has_relic(rs, RelicId::GOLDEN_IDOL) ||
                           rs.max_hp <= 0 ||
                           static_cast<float>(rs.hp) /
                                   static_cast<float>(rs.max_hp) <= 0.5f;
                break;
            case EventId::BEGGAR:           // gold < 75 -> skip (:1970-1973)
                // Real, and s2-design §2.3 omitted it -- recorded by S2.02
                // when the registry row was authored from source.
                eligible = rs.gold >= 75;
                break;
            case EventId::COLOSSEUM:
                // `if (currMapNode == null || currMapNode.y <= map.size() / 2)
                //     continue;` (:1975-1978). map is the ArrayList of 15 ROWS,
                // so map.size() / 2 == 7 by integer divide and the gate is
                // row >= 8. currMapNode is assigned at :1783, BEFORE
                // EventRoom.onPlayerEntry, so `y` is the ARRIVING node's row.
                // The `== null` half is the pre-first-pick sentinel, which
                // event_map_row returns as -1 and which fails the same test.
                eligible = event_map_row(rs) > 7;
                break;
            default:
                // Every other row in every act's list falls through to the
                // unconditional add (:1981). getEvent has exactly six cases and
                // all six are written above; do not read this arm as "act-1
                // only" -- it stopped being that when Acts 2-3 started drawing.
                break;
        }
        if (!eligible) {
            continue;
        }
        if (n < cap) {
            out[n] = id;
        }
        ++n;
    }
    return n;
}

int build_shrine_pool(const RunState& rs, uint16_t* out, int cap) noexcept {
    const int act = static_cast<int>(rs.act);
    int n = 0;
    // tmp.addAll(shrineList) (:1884) -- unconditional, IN THE ACT'S LIST ORDER.
    // The iteration is over DRAW POSITIONS, and each position's bit index is
    // derived from the id it holds; Act 1's table is the identity so this loop
    // emits exactly what it emitted before S2.13. (Header: bit meaning is
    // act-independent, draw position is not.)
    for (int pos = 0; pos < kShrineListCount; ++pos) {
        const uint16_t id = shrine_draw_order_id(act, pos);
        const int bit = static_cast<int>(id - kShrineListFirstId);
        if ((rs.shrine_membership & (1u << bit)) == 0u) {
            continue;
        }
        if (n < cap) {
            out[n] = id;
        }
        ++n;
    }
    // specialOneTimeEventList with the per-key gates (:1886-1936). The
    // dungeon-id equality checks map to rs.act (Exordium=1, TheCity=2,
    // TheBeyond=3).
    for (int i = 0; i < kSpecialListCount; ++i) {
        if ((rs.special_membership & (1u << i)) == 0u) {
            continue;
        }
        const uint16_t id = static_cast<uint16_t>(kSpecialListFirstId + i);
        bool eligible = true;
        switch (static_cast<EventId>(id)) {
            case EventId::FOUNTAIN_OF_CLEANSING:  // :1889-1893
                eligible = event_player_is_cursed(rs);
                break;
            case EventId::DESIGNER:               // :1894-1898
                eligible = (rs.act == 2 || rs.act == 3) && rs.gold >= 75;
                break;
            case EventId::DUPLICATOR:             // :1899-1903
                eligible = rs.act == 2 || rs.act == 3;
                break;
            case EventId::FACE_TRADER:            // :1904-1908 (TheCity or Exordium)
                eligible = rs.act == 1 || rs.act == 2;
                break;
            case EventId::KNOWING_SKULL:          // :1909-1913
                eligible = rs.act == 2 && rs.hp > 12;
                break;
            case EventId::NLOTH:                  // :1914-1918
                eligible = rs.act == 2 && rs.relic_count >= 2;
                break;
            case EventId::THE_JOUST:              // :1919-1923
                eligible = rs.act == 2 && rs.gold >= 50;
                break;
            case EventId::THE_WOMAN_IN_BLUE:      // :1924-1928 (act-independent)
                eligible = rs.gold >= 50;
                break;
            case EventId::SECRET_PORTAL:          // :1929-1933
                // `if (!(CardCrawlGame.playtime >= 800.0f) ||
                //      !id.equals("TheBeyond")) continue;`
                //
                // PINNED FALSE IN EVERY ACT, and from S2.13 on the ACT HALF IS
                // NO LONGER WHAT DOES THE WORK: in Act 3 `id.equals("TheBeyond")`
                // is satisfied, so the `false` is carried SOLELY by the
                // unmodelled wall-clock playtime. CardCrawlGame.playtime is
                // real-time seconds since the run started (zeroed at
                // AbstractDungeon.java:2600 in the Exordium floorNum <= 1
                // block), and this engine has no clock at all.
                //
                // That makes this a REAL BEHAVIOURAL DEVIATION, not an act
                // exclusion: any Act-3 run past 800 s of wall clock offers
                // SecretPortal in the game and never here. It is deliberate --
                // modelling a clock would make the simulator nondeterministic
                // in (seed, actions), which is the one property everything
                // else rests on. Whoever ever models playtime must delete this
                // pin; see the deferred-obligations row.
                eligible = false;
                break;
            default:
                // Accursed Blacksmith, Bonfire Elementals, Lab,
                // NoteForYourself, WeMeetAgain -- unconditional add (:1935).
                break;
        }
        if (!eligible) {
            continue;
        }
        if (n < cap) {
            out[n] = id;
        }
        ++n;
    }
    return n;
}

// --- Selection ---------------------------------------------------------------

namespace {

// getShrine (:1882-1942): one rng.random(tmp.size()-1) index draw over the
// filtered list, then removal from BOTH source lists. Returns 0 (no draw) on
// an empty filtered list -- the documented defensive deviation (header).
[[nodiscard]] uint16_t pick_shrine(RunState& rs, RngStream& rng) noexcept {
    uint16_t tmp[kShrineListCount + kSpecialListCount];
    const int n = build_shrine_pool(rs, tmp, kShrineListCount + kSpecialListCount);
    if (n == 0) {
        return 0;
    }
    const int idx = random(rng, n - 1);  // inclusive bound (trap 3)
    const uint16_t id = tmp[idx];
    // shrineList.remove(tmpKey); specialOneTimeEventList.remove(tmpKey)
    // (:1938-1939). A key lives in exactly one list; both removals are
    // attempted as the game attempts them.
    if (id >= kShrineListFirstId &&
        id < kShrineListFirstId + kShrineListCount) {
        rs.shrine_membership = static_cast<uint8_t>(
            rs.shrine_membership & ~(1u << (id - kShrineListFirstId)));
    }
    if (id >= kSpecialListFirstId &&
        id < kSpecialListFirstId + kSpecialListCount) {
        rs.special_membership = static_cast<uint16_t>(
            rs.special_membership & ~(1u << (id - kSpecialListFirstId)));
    }
    return id;
}

// getEvent (:1944-1990): filtered tmp over eventList; EMPTY filtered tmp falls
// through to getShrine (:1983-1985, a further draw on the same throwaway
// stream); otherwise one index draw + eventList removal (:1987).
[[nodiscard]] uint16_t pick_event(RunState& rs, RngStream& rng) noexcept {
    uint16_t tmp[kEventListMaxCount];
    const int n = build_event_pool(rs, tmp, kEventListMaxCount);
    if (n == 0) {
        // The 3-draw path. Ordinary rather than exotic in Act 3, whose event
        // list is only 7 entries across ~15 map rows -- and Moai Head's
        // idol/HP gate can empty the filtered list earlier still. The extra
        // draw is invisible in rs.event_rng (throwaway stream) but it changes
        // WHICH id is selected, so it is pinned by test.
        return pick_shrine(rs, rng);
    }
    const int idx = random(rng, n - 1);
    const uint16_t id = tmp[idx];
    // bit == id - the ACT's first id: each act's event ids are dense and in
    // add order, so position and bit coincide (unlike shrines).
    rs.event_membership = static_cast<uint16_t>(
        rs.event_membership &
        ~(1u << (id - event_list_first_id(static_cast<int>(rs.act)))));
    return id;
}

}  // namespace

uint16_t generate_event(RunState& rs) noexcept {
    // EventRoom.onPlayerEntry:28 -- duplicate #2, built from the POST-commit
    // counter. The game's counter replay (Random.java:28-33) equals a struct
    // copy under the one-draw invariant; the local dies at return, so
    // rs.event_rng is left byte-identical (never draw-then-rewind).
    RngStream rng = rs.event_rng;

    uint16_t id = 0;
    // generateEvent (:1864-1880). The split draw is rng.random(1.0f) -- the
    // float-RANGE overload (nextFloat() * range), one draw -- against the
    // shrineChance FIELD (header note).
    if (random(rng, 1.0f) < kShrineChance) {
        // The emptiness checks here are on the RAW lists (:1866, :1869), not
        // the filtered tmp -- membership bitsets are exactly the raw lists.
        if (rs.shrine_membership != 0u || rs.special_membership != 0u) {
            id = pick_shrine(rs, rng);
        } else if (rs.event_membership != 0u) {
            id = pick_event(rs, rng);
        } else {
            return 0;  // "No events or shrines left" (:1872-1873)
        }
    } else {
        id = pick_event(rs, rng);
        // The retVal == null re-draw (:1876-1878) is unreachable with a
        // complete registry: every key in every pool has a getEvent case.
    }

    if (id != 0) {
        // The engine-side "fired" record, mirroring the game's
        // saveFileLastEventChoice write at EventHelper.getEvent
        // (EventHelper.java:228). Ids 1..31 go to event_flags, 32..63 to
        // event_flags_hi -- routed by the accessor, never open-coded.
        event_flag_set(rs, id);
    }
    return id;
}

void init_colorless_order(RunController& rc) noexcept {
    // addColorlessCards fills the live pool with addToTop == APPEND
    // (AbstractDungeon.java:1203-1210, CardGroup.java:455-457) over
    // CardLibrary order, while the emitted kColorlessPool models
    // srcColorlessCardPool's PREPENDING addToBottom fill (:1185-1187) --
    // the same membership REVERSED. Reading the emitted array backwards
    // restores the live order (the discipline the RED pools use, and the
    // one the old local-copy draw_colorless_uncommon already followed).
    for (int i = 0; i < kColorlessOrderCap; ++i) {
        rc.colorless_order[i] =
            i < kColorlessPoolCount
                ? static_cast<uint16_t>(kColorlessPool[
                      static_cast<std::size_t>(kColorlessPoolCount - 1 - i)])
                : uint16_t{0};
    }
}

CardId event_draw_colorless_uncommon(RunController& rc) noexcept {
    // returnRandomCard's colorless sibling, returnColorlessCard(UNCOMMON)
    // (AbstractDungeon.java:1100-1113): Collections.shuffle(
    // colorlessCardPool.group, new java.util.Random(shuffleRng.randomLong()))
    // -- ONE floor-stream randomLong -- then the first UNCOMMON entry wins.
    // The shuffle is IN PLACE on the live pool, so rc.colorless_order carries
    // the permutation into the next call; SwiftStrike is the never-taken
    // fallback (the pool always holds uncommons).
    JdkRandom jdk(random_long(rc.combat.shuffle_rng));
    jdk_shuffle(std::span<uint16_t>(rc.colorless_order,
                                    static_cast<std::size_t>(
                                        kColorlessPoolCount)),
                jdk);
    for (int i = 0; i < kColorlessPoolCount; ++i) {
        const CardId id = static_cast<CardId>(rc.colorless_order[i]);
        if (sts::registry::event_card_rarity(id) ==
            sts::registry::EventCardRarity::UNCOMMON) {
            return id;
        }
    }
    return CardId::SWIFT_STRIKE;
}

void open_event_grid(EventDialogState& es, EventGridKind kind) noexcept {
    es.grid_kind = static_cast<uint8_t>(kind);
}

void close_event_grid(EventDialogState& es) noexcept {
    es.grid_kind = static_cast<uint8_t>(EventGridKind::NONE);
}

bool event_grid_card_legal(const RunState& rs, const EventDialogState& es,
                           uint16_t deck_index) noexcept {
    if (deck_index >= rs.master_deck_count) {
        return false;
    }
    switch (static_cast<EventGridKind>(es.grid_kind)) {
        case EventGridKind::PURGE:
            // Every S1 event purge grid passes getGroupWithoutBottledCards
            // over getPurgeableCards -- Cleric.java:76-78,
            // LivingWall.java:96-97 (Forget), Bonfire.java:101-102,
            // PurificationShrine.java:62, GremlinWheelGame.java:286-287,
            // NoteForYourself.java:65, GoldenWing.java:110 -- so a bottled
            // card is not on any of them. (An Act-2 event that does NOT
            // exclude, e.g. DrugDealer.java:128, would need its own kind.)
            return master_card_purgeable_unbottled(rs.master_deck[deck_index]);
        case EventGridKind::UPGRADE:
            // Upgrade grids keep bottled cards: LivingWall.java:109 (Grow)
            // opens getUpgradableCards(), which has NO bottled clause -- do
            // not over-exclude here.
            return rest_card_upgradeable(rs.master_deck[deck_index]);
        case EventGridKind::TRANSFORMABLE:
            // Living Wall's Change (:102-103) and Transmogrifier (:65) both
            // open getGroupWithoutBottledCards(getPurgeableCards()).
            return master_card_purgeable_unbottled(rs.master_deck[deck_index]);
        case EventGridKind::DUPLICATE:
            // Duplicator opens over the WHOLE master deck -- no purgeable
            // filter, no bottled exclusion (Duplicator.java:87). Every index
            // below master_deck_count (checked above) is legal.
            return true;
        case EventGridKind::TRANSFORM_PAIR_SECOND:
            // The Designer's second transform pick (Designer.java:200,
            // numCards == 2): same pool as TRANSFORMABLE minus the already
            // selected card, whose deck index the body parked in scratch3
            // (the enum's declaration documents the coupling).
            return deck_index != static_cast<uint16_t>(es.scratch3) &&
                   master_card_purgeable_unbottled(rs.master_deck[deck_index]);
        case EventGridKind::NONE:
        default:
            return false;
    }
}

bool event_grid_remove_card(RunState& rs, EventDialogState& es,
                            uint16_t deck_index) noexcept {
    if (static_cast<EventGridKind>(es.grid_kind) != EventGridKind::PURGE ||
        !event_grid_card_legal(rs, es, deck_index) ||
        !remove_master_deck_card(rs, deck_index)) {
        return false;
    }
    close_event_grid(es);
    return true;
}

bool event_grid_upgrade_card(RunState& rs, EventDialogState& es,
                             uint16_t deck_index) noexcept {
    if (static_cast<EventGridKind>(es.grid_kind) != EventGridKind::UPGRADE ||
        !event_grid_card_legal(rs, es, deck_index)) {
        return false;
    }
    ++rs.master_deck[deck_index].upgrade;
    close_event_grid(es);
    return true;
}

bool event_grid_transform_card(RunState& rs, EventDialogState& es,
                               RngStream& misc_rng,
                               uint16_t deck_index) noexcept {
    if (static_cast<EventGridKind>(es.grid_kind) !=
            EventGridKind::TRANSFORMABLE ||
        !event_grid_card_legal(rs, es, deck_index)) {
        return false;
    }
    const CardId removed =
        static_cast<CardId>(rs.master_deck[deck_index].card_id);

    // LivingWall.update removes first, then transformCard(..., miscRng)
    // (LivingWall.java:53-61); Transmorgrifier.buttonEffect is the same pair
    // (Transmorgrifier.java:56-64). The list transformCard builds is
    // `transform_card` (card_pools.hpp) -- THE one authority for
    // AbstractDungeon.transformCard (:860-878) and its
    // returnTrulyRandomCardFromAvailable list (:1016-1045), shared with Neow's
    // grid. Only the stream differs: miscRng here, NeowEvent.rng there. This
    // used to be a second, independent rendering over a separately emitted
    // `kEventTransformRedPool`, and the two drifted -- that pool walked
    // registry rows rather than commonCardPool ++ REVERSED srcUncommonCardPool
    // ++ REVERSED srcRareCardPool, so STS00051's floor-2 Living Wall produced
    // Iron Wave where the game produced Havoc. Do not re-introduce one.
    if (!remove_master_deck_card(rs, deck_index)) {
        return false;
    }
    const CardId replacement = transform_card(misc_rng, removed);
    const bool obtained = add_card_to_master_deck(rs, replacement);
    close_event_grid(es);
    return obtained;
}

bool apply_event_damage(RunController& rc, int32_t amount,
                        EventDamageOwner owner) noexcept {
    // AbstractPlayer.damage (AbstractPlayer.java:1387-1502) runs the player's
    // onAttacked relics before onLoseHpLast, then checks Fairy Potion and
    // Lizard Tail after the HP write. Event dialogs have no block or combat
    // powers, but the already-live relic effects remain observable here.
    //
    // GoldenIdol uses DamageInfo(null, n); GoldenWing/Goop use player-owned
    // DamageInfo. Torii.onAttacked (Torii.java:31-38) requires a non-null owner,
    // so retaining owner is load-bearing even though this batch's player-owned
    // hits (7/11) are above its threshold. Boot and Fairy Potion remain their
    // separately recorded deferrals.
    if (amount <= 0 || rc.run.hp <= 0) {
        return rc.run.hp > 0;
    }
    int damage = amount;
    if (owner == EventDamageOwner::PLAYER && damage > 1 && damage <= 5 &&
        has_relic(rc.run, RelicId::TORII)) {
        damage = 1;
    }
    // TungstenRod.onLoseHpLast (TungstenRod.java:26-32) is owner-independent
    // and is the final damage modifier.
    if (damage > 0 && has_relic(rc.run, RelicId::TUNGSTEN_ROD)) {
        --damage;
    }
    if (damage == 0) {
        return true;
    }

    int hp = static_cast<int>(rc.run.hp) - damage;
    if (hp < 0) {
        hp = 0;
    }
    rc.run.hp = static_cast<int16_t>(hp);
    if (hp == 0) {
        // AbstractPlayer.damage (:1482-1497) is ONE site for combat and
        // out-of-combat HP loss alike, and it offers TWO revive sources in a
        // fixed structure:
        //
        //     if (!hasRelic("Mark of the Bloom")) {
        //         if (hasPotion("FairyPotion")) { ...; return; }
        //         else if (hasRelic("Lizard Tail") && counter == -1) { ...; return; }
        //     }
        //
        // A HELD FAIRY THEREFORE BEATS A LIZARD TAIL -- and because the Lizard
        // Tail arm is an `else if` on `hasPotion`, the Lizard Tail is not even
        // CONSULTED, let alone spent, while a Fairy is held. This is the
        // out-of-combat twin of try_player_revive (interp_damage.cpp); here the
        // BELT is directly readable, so no CombatState mirror is involved.
        //
        // FairyPotion.use (FairyPotion.java:36-45): healAmt =
        // (int)((float)maxHealth * potency/100f), clamped UP to 1, applied to a
        // currentHealth already pinned at 0 -- so the result IS healAmt. The
        // slot is destroyed (:44, and again at AbstractPlayer.java:1491; the two
        // are one event, destroyPotion being idempotent). LEFTMOST FAIRY FIRST:
        // the Java loop returns on the first match in slot order, and exactly
        // ONE is consumed.
        //
        // Magic Flower amplifies NEITHER heal here: its onPlayerHeal is
        // explicitly phase==COMBAT-gated (MagicFlower.java:30-37) and an
        // EventRoom is not COMBAT.
        {
            const uint8_t slots =
                rc.run.potion_slots < static_cast<uint8_t>(kPotionCap)
                    ? rc.run.potion_slots
                    : static_cast<uint8_t>(kPotionCap);
            for (uint8_t i = 0; i < slots; ++i) {
                if (static_cast<PotionId>(rc.run.potions[i]) !=
                    PotionId::FAIRY_POTION) {
                    continue;
                }
                const PotionDef* def = potion_def(PotionId::FAIRY_POTION);
                const int potency =
                    (def == nullptr ? 0 : def->potency) *
                    (has_relic(rc.run, RelicId::SACRED_BARK) ? 2 : 1);
                const float percent = static_cast<float>(potency) / 100.0f;
                int heal = static_cast<int>(
                    static_cast<float>(rc.run.max_hp) * percent);
                if (heal < 1) {
                    heal = 1;
                }
                if (heal > static_cast<int>(rc.run.max_hp)) {
                    heal = static_cast<int>(rc.run.max_hp);  // heal() clamps
                }
                rc.run.hp = static_cast<int16_t>(heal);
                rc.run.potions[i] = static_cast<uint16_t>(PotionId::NONE);
                return true;
            }
        }
        // LizardTail.onTrigger (LizardTail.java:36-45) heals maxHealth/2
        // (minimum 1) and marks the relic used.
        for (uint8_t i = 0; i < rc.run.relic_count; ++i) {
            RelicSlot& slot = rc.run.relics[i];
            if (slot.relic_id ==
                    static_cast<uint16_t>(RelicId::LIZARD_TAIL) &&
                slot.counter == -1) {
                slot.counter = -2;
                rc.run.hp = static_cast<int16_t>(
                    std::max(1, static_cast<int>(rc.run.max_hp) / 2));
                return true;
            }
        }
        rc.event = EventDialogState{};
        rc.phase = static_cast<uint8_t>(RunPhase::RUN_OVER);
        return false;
    }
    return true;
}

// --- Dialog dispatch ---------------------------------------------------------

namespace {

// The synthetic proof body (header: kSyntheticEventId). Screen 0 offers
//   0: take 10 gold           (always enabled)
//   1: pay 50 gold, +5 max HP (enabled iff gold >= 50 -- the conditional case)
//   2: leave                  (always enabled)
//   3: body-owned transition  (always enabled; proves TRANSITIONED plumbing)
// options 0/1 advance to screen 1, whose single option leaves. scratch0
// counts screen-0 picks so tests can see event-defined state persist.
void synthetic_on_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void synthetic_build_menu(const RunController& rc, const EventDialogState& es,
                          EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        out.count = 4;
        out.enabled[0] = true;
        out.enabled[1] = rc.run.gold >= 50;
        out.enabled[2] = true;
        out.enabled[3] = true;
    } else {
        out.count = 1;
        out.enabled[0] = true;
    }
}

EventDialogStatus synthetic_choose(RunController& rc, EventDialogState& es,
                                   uint8_t option) {
    if (es.screen == 0) {
        if (option == 0) {
            gain_gold(rc.run, 10);
            ++es.scratch0;
            es.screen = 1;
            return EventDialogStatus::CONTINUE;
        }
        if (option == 1) {
            rc.run.gold -= 50;
            rc.run.max_hp = static_cast<int16_t>(rc.run.max_hp + 5);
            rc.run.hp = static_cast<int16_t>(rc.run.hp + 5);
            ++es.scratch0;
            es.screen = 1;
            return EventDialogStatus::CONTINUE;
        }
        if (option == 3) {
            rc.event = EventDialogState{};
            rc.phase = static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED);
            return EventDialogStatus::TRANSITIONED;
        }
        return EventDialogStatus::FINISHED;  // option 2: leave
    }
    return EventDialogStatus::FINISHED;
}

constexpr EventDialogImpl kSyntheticImpl = {
    &synthetic_on_enter,
    &synthetic_build_menu,
    &synthetic_choose,
};

}  // namespace

#define STS_DECLARE_EVENT_BODY(id, fn) \
    const EventDialogImpl* fn() noexcept;
STS_REGISTRY_NATIVE_EVENTS(STS_DECLARE_EVENT_BODY)
#undef STS_DECLARE_EVENT_BODY

const EventDialogImpl* event_dialog_impl(uint16_t event_id) noexcept {
    if (event_id == kSyntheticEventId) {
        return &kSyntheticImpl;
    }
    switch (static_cast<EventId>(event_id)) {
#define STS_DISPATCH_EVENT_BODY(id, fn) \
        case EventId::id: return fn();
        STS_REGISTRY_NATIVE_EVENTS(STS_DISPATCH_EVENT_BODY)
#undef STS_DISPATCH_EVENT_BODY
        default:
            return nullptr;
    }
}

}  // namespace sts::engine
