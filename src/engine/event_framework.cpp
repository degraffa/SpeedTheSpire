// The ?-room roll, event selection and dialog dispatch. See
// event_framework.hpp for the RNG contract, provenance, and design.

#include "sts/engine/event_framework.hpp"

#include <algorithm>
#include <cstdint>

#include "relics/relic_pickup.hpp"    // gain_gold (Ectoplasm door)
#include "sts/engine/card_pools.hpp"  // transform_card (the ONE transformCard list)
#include "sts/engine/cards.hpp"       // card_def / CardType (isCursed)
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

// --- The roll ----------------------------------------------------------------

void dispatch_event_room_entry_relics(RunState& rs) noexcept {
    // AbstractDungeon.nextRoomTransition iterates every relic's onEnterRoom
    // against nextRoom.room BEFORE an EventRoom is rolled/replaced
    // (AbstractDungeon.java:1754-1779). SsserpentHead.onEnterRoom
    // (SsserpentHead.java:29-35) therefore fires for every ? entry, including
    // one that later resolves to combat, shop or treasure. MawBank.onEnterRoom
    // (MawBank.java:31-36) has no room-type condition and likewise fires here
    // while unused. This is a fan-out, not getRelic: a malformed duplicate
    // import fires once per copy as the Java relic iteration does.
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        const RelicId id = static_cast<RelicId>(rs.relics[i].relic_id);
        if (id == RelicId::SSSERPENT_HEAD) {
            gain_gold(rs, 50);
        } else if (id == RelicId::MAW_BANK && rs.relics[i].counter != -2) {
            gain_gold(rs, 12);
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

void init_event_pools(RunState& rs) noexcept {
    rs.event_membership =
        static_cast<uint16_t>((1u << kEventListCount) - 1u);        // bits 0..10
    rs.shrine_membership =
        static_cast<uint8_t>((1u << kShrineListCount) - 1u);        // bits 0..5
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

bool event_player_is_cursed(const RunState& rs) noexcept {
    // AbstractPlayer.isCursed (AbstractPlayer.java:741-748). Exclusions:
    // Necronomicurse, CurseOfTheBell, AscendersBane. Only Ascender's Bane has
    // a registry row today; the other two join this check with their rows.
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        const CardId id = static_cast<CardId>(rs.master_deck[i].card_id);
        if (id == CardId::ASCENDERS_BANE) {
            continue;
        }
        const CardDef* def = card_def(id);
        if (def != nullptr && def->type == CardType::CURSE) {
            return true;
        }
    }
    return false;
}

int build_event_pool(const RunState& rs, uint16_t* out, int cap) noexcept {
    // AbstractDungeon.getEvent's tmp build (:1946-1982) over eventList in
    // canonical order. floorNum here is rs.floor -- the new floor, already
    // incremented before onPlayerEntry runs.
    int n = 0;
    for (int i = 0; i < kEventListCount; ++i) {
        if ((rs.event_membership & (1u << i)) == 0u) {
            continue;
        }
        const uint16_t id = static_cast<uint16_t>(kEventListFirstId + i);
        bool eligible = true;
        switch (static_cast<EventId>(id)) {
            case EventId::DEAD_ADVENTURER:  // floorNum <= 6 -> skip (:1950-1953)
            case EventId::MUSHROOMS:        // same gate (:1955-1958)
                eligible = rs.floor > 6;
                break;
            case EventId::THE_CLERIC:       // gold < 35 -> skip (:1965-1968)
                eligible = rs.gold >= 35;
                break;
            default:
                // The Moai Head / Beggar / Colosseum cases (:1960-1979) guard
                // act-2/3 list keys Exordium's list never holds; the other 8
                // Act-1 rows fall through to the unconditional add (:1981).
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
    int n = 0;
    // tmp.addAll(shrineList) (:1884) -- unconditional, canonical order.
    for (int i = 0; i < kShrineListCount; ++i) {
        if ((rs.shrine_membership & (1u << i)) == 0u) {
            continue;
        }
        if (n < cap) {
            out[n] = static_cast<uint16_t>(kShrineListFirstId + i);
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
                // playtime >= 800s && TheBeyond. Wall-clock playtime is not
                // modelled; the act gate alone already excludes it from S1,
                // so this stays false until the act-3 owner models playtime.
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
    uint16_t tmp[kEventListCount];
    const int n = build_event_pool(rs, tmp, kEventListCount);
    if (n == 0) {
        return pick_shrine(rs, rng);
    }
    const int idx = random(rng, n - 1);
    const uint16_t id = tmp[idx];
    rs.event_membership = static_cast<uint16_t>(
        rs.event_membership & ~(1u << (id - kEventListFirstId)));
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
        // The engine-side "fired" record (bit id-1; EventId 1..31 fits u32),
        // mirroring the game's saveFileLastEventChoice write at
        // EventHelper.getEvent (EventHelper.java:228).
        rs.event_flags |= 1u << (id - 1u);
    }
    return id;
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
            return rest_card_purgeable(rs.master_deck[deck_index]);
        case EventGridKind::UPGRADE:
            return rest_card_upgradeable(rs.master_deck[deck_index]);
        case EventGridKind::TRANSFORMABLE:
            // Living Wall uses getPurgeableCards for this screen too
            // (LivingWall.java:68-72).
            return rest_card_purgeable(rs.master_deck[deck_index]);
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
        // AbstractPlayer.damage (:1494-1497) consumes an armed Lizard Tail
        // before declaring death. LizardTail.onTrigger (LizardTail.java:36-45)
        // heals maxHealth/2 (minimum 1) and marks the relic used. Magic Flower
        // does not amplify this heal in an EventRoom: its onPlayerHeal body is
        // explicitly phase==COMBAT-gated (MagicFlower.java:31-38).
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
