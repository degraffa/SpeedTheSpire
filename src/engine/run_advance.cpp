// The RUN-LEVEL batch API implementation (Stage B, task B4.4). See
// run_advance.hpp for the design, scope boundaries, and provenance.
//
// The combat construction (enter_combat) deliberately MIRRORS combat_begin
// (advance.cpp) field-for-field so that the single-Jaw-Worm path remains
// byte-identical to combat_begin(seed, floor, deck). That equivalence is pinned
// by a named test even though Cultist and both louse variants are now live
// (run_advance_test's RunCombatMatchesCombatBegin), so any future drift in the
// combat-start sequence is caught rather than silently diverging. The one
// intentional difference is that enter_combat first resolves the encounter
// composition on miscRng (the game's getEncounter, MonsterHelper.java) -- for the
// single-emit "Jaw Worm" encounter that consumes ZERO miscRng draws, preserving
// the byte-equivalence.

#include "sts/engine/run_advance.hpp"

#include <cassert>
#include <cstdint>
#include <span>
#include <string_view>

#include "sts/engine/action_queue.hpp"     // pump
#include "sts/engine/cards.hpp"            // card_def / card_cost / card_flags
#include "sts/engine/encounters.hpp"       // generate_monster_lists / resolve_encounter
#include "sts/engine/map_gen.hpp"          // generate_map / encode_paths_into_run_state / kBossCol / kEdge*
#include "sts/engine/map_rooms.hpp"        // assign_room_types / encode_rooms_into_run_state / RoomType
#include "sts/engine/monster_dispatch.hpp" // spawn_group / dispatch_monster_turn / monster_init_fn
#include "sts/engine/observation.hpp"      // encode_observation
#include "sts/engine/potions.hpp"          // PotionId / use_potion / slot count
#include "sts/engine/relic_hooks.hpp"      // dispatch_relics_on_victory
#include "sts/engine/rng_jdk.hpp"          // JdkRandom / jdk_shuffle
#include "sts/engine/rng_stream.hpp"       // floor_stream / random_long / from_seed
#include "sts/registry/game_ids.hpp"       // monster_from_game_id

namespace sts::engine {

namespace {

// The Ironclad starting deck (Ironclad.getStartingDeck, Ironclad.java:92-104):
// 5 Strike, 4 Defend, 1 Bash, in that order (the order is load-bearing -- it is
// the pre-shuffle master-deck order the combat-start shuffle_rng permutes). The
// A10 AscendersBane curse is NOT added here (curse content + the A10 modifier are
// B4.15); documented in run_advance.hpp's scope note.
constexpr CardId kIroncladStartDeck[] = {
    CardId::STRIKE, CardId::STRIKE, CardId::STRIKE, CardId::STRIKE, CardId::STRIKE,
    CardId::DEFEND, CardId::DEFEND, CardId::DEFEND, CardId::DEFEND,
    CardId::BASH,
};

// Reseed the five floor-scoped streams to floor_stream(seed, floor) (trap 7 --
// the caller has already ++'d floor). Writes them into the combat state, which is
// the canonical floor-stream holder (design §3.4) for combat AND non-combat rooms.
void reseed_floor_streams(CombatState& s, int64_t seed, int32_t floor) noexcept {
    s.monster_hp_rng = floor_stream(seed, floor);
    s.ai_rng = floor_stream(seed, floor);
    s.shuffle_rng = floor_stream(seed, floor);
    s.card_random_rng = floor_stream(seed, floor);
    s.misc_rng = floor_stream(seed, floor);
}

// Fold CombatState-owned persistent fields back into RunState. Gold, potion
// slots, and the master deck are already canonical in rc.run while combat is
// live (CombatState deliberately has no duplicate copies), so combat mutations
// to those fields route there directly. HP/max-HP and relic counters are the
// actual mirrors and must be copied on both kill and Smoke Bomb escape.
void fold_back_combat(RunController& rc) noexcept {
    rc.run.hp = rc.combat.player_hp;
    rc.run.max_hp = rc.combat.player_max_hp;
    const uint8_t n =
        rc.combat.relic_count < rc.run.relic_count ? rc.combat.relic_count
                                                   : rc.run.relic_count;
    for (uint8_t i = 0; i < n; ++i) {
        rc.run.relics[i].counter = rc.combat.relics[i].counter;
    }
}

bool potion_requires_target(const PotionDef& def) noexcept {
    for (uint8_t i = 0; i < def.step_count; ++i) {
        if (def.steps[i].target == StepTarget::CARD_TARGET) {
            return true;
        }
    }
    return false;
}

bool live_target(const CombatState& s, uint8_t target) noexcept {
    return target < s.monster_count && s.monsters[target].hp > 0;
}

bool combat_potion_legal(const RunController& rc, uint8_t slot,
                         uint8_t target, bool validate_target) noexcept {
    if (slot >= rc.run.potion_slots || slot >= kPotionCap ||
        rc.combat.phase != static_cast<uint8_t>(CombatPhase::WAITING_ON_USER)) {
        return false;
    }
    const PotionId id = static_cast<PotionId>(rc.run.potions[slot]);
    const PotionDef* def = potion_def(id);
    if (def == nullptr || id == PotionId::FAIRY_POTION) {
        return false;
    }
    if (id == PotionId::SMOKE_BOMB &&
        rc.room_type == static_cast<uint8_t>(RoomType::Boss)) {
        return false;  // SmokeBomb.canUse rejects bosses.
    }
    bool any_alive = false;
    for (uint8_t m = 0; m < rc.combat.monster_count; ++m) {
        any_alive = any_alive || rc.combat.monsters[m].hp > 0;
    }
    if (!any_alive) {
        return false;
    }
    if (!potion_requires_target(*def)) {
        return true;
    }
    return !validate_target || live_target(rc.combat, target);
}

bool noncombat_potion_legal(const RunController& rc, uint8_t slot) noexcept {
    if (slot >= rc.run.potion_slots || slot >= kPotionCap) {
        return false;
    }
    const PotionId id = static_cast<PotionId>(rc.run.potions[slot]);
    return id == PotionId::FRUIT_JUICE || id == PotionId::ENTROPIC_BREW;
}

void clear_potion_slot(RunState& rs, uint8_t slot) noexcept {
    rs.potions[slot] = static_cast<uint16_t>(PotionId::NONE);
}

void use_fruit_juice(RunController& rc, uint8_t slot) noexcept {
    const int amount = potion_def(PotionId::FRUIT_JUICE)->potency;
    if (rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
        rc.combat.player_max_hp = static_cast<int16_t>(rc.combat.player_max_hp + amount);
        rc.combat.player_hp = static_cast<int16_t>(rc.combat.player_hp + amount);
        rc.run.max_hp = rc.combat.player_max_hp;
    } else {
        rc.run.max_hp = static_cast<int16_t>(rc.run.max_hp + amount);
        rc.run.hp = static_cast<int16_t>(rc.run.hp + amount);
    }
    clear_potion_slot(rc.run, slot);
}

void use_entropic_brew(RunController& rc, uint8_t slot) noexcept {
    // EntropicBrew.use constructs potionSlots random-potion actions/effects
    // before PotionPopUp destroys the Brew. All draws therefore happen even if
    // only one resulting potion fits after the consumed slot opens.
    PotionId rolls[kPotionCap]{};
    const uint8_t count = rc.run.potion_slots < kPotionCap
                              ? rc.run.potion_slots
                              : static_cast<uint8_t>(kPotionCap);
    for (uint8_t i = 0; i < count; ++i) {
        rolls[i] = return_random_potion(rc.run.potion_rng, true);
    }
    clear_potion_slot(rc.run, slot);
    for (uint8_t i = 0; i < count; ++i) {
        for (uint8_t dst = 0; dst < count; ++dst) {
            if (rc.run.potions[dst] == static_cast<uint16_t>(PotionId::NONE)) {
                rc.run.potions[dst] = static_cast<uint16_t>(rolls[i]);
                break;
            }
        }
    }
}

void dispatch_run_relics_on_use_potion(RunController& rc) noexcept {
    // PotionPopUp invokes every relic.onUsePotion after potion.use and before
    // destroying the slot. Toy Ornithopter is the only registered S1 consumer.
    for (uint8_t i = 0; i < rc.run.relic_count; ++i) {
        if (static_cast<RelicId>(rc.run.relics[i].relic_id) !=
            RelicId::TOY_ORNITHOPTER) {
            continue;
        }
        if (rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
            int hp = static_cast<int>(rc.combat.player_hp) + 5;
            if (hp > rc.combat.player_max_hp) {
                hp = rc.combat.player_max_hp;
            }
            rc.combat.player_hp = static_cast<int16_t>(hp);
        } else {
            int hp = static_cast<int>(rc.run.hp) + 5;
            if (hp > rc.run.max_hp) {
                hp = rc.run.max_hp;
            }
            rc.run.hp = static_cast<int16_t>(hp);
        }
    }
}

void fill_combat_result(const CombatState& s, StepResult& r) noexcept {
    bool any_alive = false;
    for (uint8_t m = 0; m < s.monster_count; ++m) {
        any_alive = any_alive || s.monsters[m].hp > 0;
    }
    r = StepResult{};
    r.terminal = s.player_hp <= 0 || !any_alive;
    r.reward = s.player_hp <= 0 ? -1.0f : (!any_alive ? 1.0f : 0.0f);
    encode_observation(s, r.obs);
}

void enter_combat_reward(RunController& rc, RunCombatOutcome outcome,
                         StepResult& res) noexcept {
    // AbstractRoom.endBattle calls player.onVictory for ordinary victory and
    // Smoke Bomb alike, before the reward screen opens.
    dispatch_relics_on_victory(rc.combat, rc.combat.relics,
                               rc.combat.relic_count);
    fold_back_combat(rc);
    rc.combat_outcome = static_cast<uint8_t>(outcome);
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT_REWARD);
    const float combat_reward = res.reward;
    res = StepResult{};  // reward screens have no combat observation view.
    res.reward = combat_reward;
    res.terminal = false;  // the run continues at a CHOOSE/proceed screen.
}

// Build the live combat for `enc_key` and set rc.phase accordingly. Returns true
// iff a real combat was entered (phase COMBAT); false parks the run at
// ROOM_UNIMPLEMENTED (unknown encounter / a member monster not yet implemented).
// The five floor streams are re-derived here (identical to the caller's reseed) so
// the build is a pure function of (run, floor, encounter).
bool enter_combat(RunController& rc, std::string_view enc_key,
                  RoomType room) noexcept {
    const int64_t seed = rc.run.run_seed;
    const int32_t floor = static_cast<int32_t>(rc.run.floor);

    CombatState s{};                              // value-init: byte-clean scratch
    reseed_floor_streams(s, seed, floor);

    // (1) Composition (miscRng): MonsterHelper.getEncounter (B3.12 resolver).
    ResolvedGroup grp{};
    if (!resolve_encounter(enc_key, s.misc_rng, grp) || grp.count == 0) {
        rc.combat = s;
        rc.phase = static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED);
        rc.room_type = static_cast<uint8_t>(room);
        return false;
    }

    // (2) Map composition game_ids -> MonsterIds; require every member implemented
    //     (Jaw Worm, Cultist, and both louse variants are live; B3.14-B3.22 add
    //     the rest). If any is unimplemented
    //     we have still consumed miscRng exactly as the game would, then park.
    MonsterId ids[kMonsterCap] = {};
    bool all_impl = true;
    for (uint8_t i = 0; i < grp.count; ++i) {
        const MonsterId id =
            static_cast<MonsterId>(sts::registry::monster_from_game_id(grp.members[i]));
        if (id == MonsterId::NONE || monster_init_fn(id) == nullptr) {
            all_impl = false;
        }
        ids[i] = id;
    }
    if (!all_impl) {
        rc.combat = s;
        rc.phase = static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED);
        rc.room_type = static_cast<uint8_t>(room);
        return false;
    }

    // (3) Combat construction -- mirrors combat_begin (advance.cpp). Card pool
    //     from the run's master deck (in deck order), base cost/flags per the
    //     registry.
    const int n = static_cast<int>(rc.run.master_deck_count);
    for (int i = 0; i < n; ++i) {
        const CardId cid = static_cast<CardId>(rc.run.master_deck[i].card_id);
        const CardDef* def = card_def(cid);
        assert(def != nullptr && "master deck holds an unknown CardId");
        const uint8_t up = rc.run.master_deck[i].upgrade;
        s.card_pool[i].card_id = static_cast<uint16_t>(cid);
        s.card_pool[i].upgrade = up;
        s.card_pool[i].cost_now = card_cost(*def, up);
        s.card_pool[i].flags = card_flags(*def, up);
        s.card_pool[i].misc = 0;
    }

    // (4) Shuffle the deck into the draw pile (one shuffle_rng.random_long() +
    //     JDK Fisher-Yates -- identical to combat_begin).
    for (int i = 0; i < n; ++i) {
        s.draw[i] = static_cast<CardPoolIndex>(i);
    }
    if (n > 1) {
        const int64_t sd = random_long(s.shuffle_rng);
        JdkRandom jr(sd);
        jdk_shuffle(std::span<CardPoolIndex>(s.draw, static_cast<std::size_t>(n)), jr);
    }
    // CardGroup.initializeDeck collects Innate cards after the one shuffle and
    // puts them on top. This must match combat_begin, including B3.9's Writhe.
    CardPoolIndex innate[kDrawCap]{};
    uint8_t innate_count = 0;
    uint8_t normal_count = 0;
    for (int i = 0; i < n; ++i) {
        const CardPoolIndex pi = s.draw[i];
        if (has_card_flag(s.card_pool[pi].flags, CardFlag::INNATE)) {
            innate[innate_count++] = pi;
        } else {
            s.draw[normal_count++] = pi;
        }
    }
    for (uint8_t i = 0; i < innate_count; ++i) {
        s.draw[static_cast<uint8_t>(normal_count + i)] = innate[i];
    }
    s.draw_count = static_cast<uint8_t>(normal_count + innate_count);

    // (5) Player sheet from the run (hp/max_hp carried across combats).
    s.player_hp = rc.run.hp;
    s.player_max_hp = rc.run.max_hp;
    s.player_block = 0;

    // (6) Spawn the resolved group (monster_hp_rng HP rolls + ai_rng rollMove, in
    //     spawn order).
    spawn_group(s, std::span<const MonsterId>(ids, grp.count));

    // (7) Monster pre-battle actions run after every member is spawned and
    //     before turn 1. B3.13 louses roll and apply Curl Up here.
    use_pre_battle_actions(s);

    // (8) Combat relic mirror: RunState.relics -> CombatState.relics (B4.3 seam,
    //     filled at combat spawn per its Log; folded back at combat end).
    for (uint8_t i = 0; i < rc.run.relic_count; ++i) {
        s.relics[i] = rc.run.relics[i];
    }
    s.relic_count = rc.run.relic_count;

    // (9) Prime the turn-1 invariants and pump once into WAITING_ON_USER (same
    //     mechanism combat_begin uses; see its implementation note).
    s.turn = 0;
    s.monster_attacks_queued = 1;
    s.turn_has_ended = 1;
    pump(s, dispatch_monster_turn);

    rc.combat = s;
    rc.room_type = static_cast<uint8_t>(room);
    rc.combat_outcome = static_cast<uint8_t>(RunCombatOutcome::NONE);
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT);
    return true;
}

// onPlayerEntry dispatch for the room just entered (AbstractDungeon.java:1800).
// Combat rooms build the combat via the encounter framework; every other room
// kind is not yet implemented (B4.7-B4.10) so it reseeds the floor streams (for
// oracle-reseed visibility) and parks at ROOM_UNIMPLEMENTED.
void on_player_entry(RunController& rc, RoomType room) noexcept {
    const int64_t seed = rc.run.run_seed;
    const int32_t floor = static_cast<int32_t>(rc.run.floor);

    rc.room_type = static_cast<uint8_t>(room);
    rc.combat_outcome = static_cast<uint8_t>(RunCombatOutcome::NONE);

    auto stall = [&](RoomType r) noexcept {
        rc.combat = CombatState{};
        reseed_floor_streams(rc.combat, seed, floor);
        rc.phase = static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED);
        rc.room_type = static_cast<uint8_t>(r);
    };

    switch (room) {
        case RoomType::Monster:
            if (rc.monster_cursor < rc.lists.monster_list_count) {
                enter_combat(rc, rc.lists.monster_list[rc.monster_cursor], room);
            } else {
                stall(room);  // list exhausted (beyond Act-1 length; not expected)
            }
            break;
        case RoomType::Elite:
            if (rc.elite_cursor < rc.lists.elite_list_count) {
                enter_combat(rc, rc.lists.elite_list[rc.elite_cursor], room);
            } else {
                stall(room);
            }
            break;
        case RoomType::Boss:
            if (rc.boss_cursor < rc.lists.boss_list_count) {
                enter_combat(rc, rc.lists.boss_list[rc.boss_cursor], room);
            } else {
                stall(room);
            }
            break;
        case RoomType::Event:
        case RoomType::Shop:
        case RoomType::Rest:
        case RoomType::Treasure:
        case RoomType::None:
        default:
            stall(room);  // B4.7-B4.10 room content
            break;
    }
}

// Fill a non-combat StepResult: no observation (obs is combat-only), terminal iff
// the run is parked (ROOM_UNIMPLEMENTED) or over (RUN_OVER).
void fill_run_result(const RunController& rc, StepResult& r) noexcept {
    r = StepResult{};
    r.terminal = rc.phase == static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED) ||
                 rc.phase == static_cast<uint8_t>(RunPhase::RUN_OVER);
    r.reward = 0.0f;
}

}  // namespace

// --- next_room_transition ----------------------------------------------------

void next_room_transition(RunController& rc, uint8_t dst_x, bool to_boss) noexcept {
    RunState& rs = rc.run;

    // (1) Remove the LEFT room's encounter from its list (AbstractDungeon.java:
    //     1694-1707): leaving a MonsterRoom/Elite advances that list's cursor.
    //     (Modelled as a cursor bump == remove(0).) Neow / non-combat rooms: none.
    if (rs.floor >= 1 && rc.cur_x != kNeowColumn) {
        const int y = run_cur_row(rc);
        const RoomType left =
            static_cast<RoomType>(rs.map[run_state_map_index(rc.cur_x, y)].room_type);
        if (left == RoomType::Monster) {
            ++rc.monster_cursor;
        } else if (left == RoomType::Elite) {
            ++rc.elite_cursor;
        }
    }

    // (2) ++floorNum (AbstractDungeon.java:1741) -- BEFORE the reseed (trap 7).
    ++rs.floor;

    // (3) Reseed the 5 floor-scoped streams (AbstractDungeon.java:1747-1751).
    reseed_floor_streams(rc.combat, rs.run_seed, static_cast<int32_t>(rs.floor));

    // (4) Move to the destination node.
    RoomType room;
    if (to_boss) {
        rc.cur_x = static_cast<uint8_t>(kBossCol);
        room = RoomType::Boss;
    } else {
        rc.cur_x = dst_x;
        const int y = run_cur_row(rc);
        room = static_cast<RoomType>(rs.map[run_state_map_index(dst_x, y)].room_type);
    }

    // (5) onPlayerEntry (AbstractDungeon.java:1800).
    on_player_entry(rc, room);
}

// --- run_begin ---------------------------------------------------------------

RunController run_begin(int64_t seed, uint8_t ascension) noexcept {
    RunController rc{};
    RunState& rs = rc.run;

    rs.run_seed = seed;
    rs.ascension = ascension;
    rs.act = 1;
    rs.floor = 0;

    // generateSeeds (AbstractDungeon.java:398-412): every run-scoped stream +
    // neowRng is a fresh Random(seed), counter 0. mapRng is NOT seeded here.
    rs.monster_rng = from_seed(seed);
    rs.event_rng = from_seed(seed);
    rs.merchant_rng = from_seed(seed);
    rs.card_rng = from_seed(seed);
    rs.treasure_rng = from_seed(seed);
    rs.relic_rng = from_seed(seed);
    rs.potion_rng = from_seed(seed);
    rs.neow_rng = from_seed(seed);
    // generateSeeds also constructs the five floor-scoped streams from seed.
    // At Neow/floor 0 this is exactly floor_stream(seed, 0); the floor-1 room
    // transition replaces them only after incrementing floor.
    reseed_floor_streams(rc.combat, seed, 0);

    // dungeonTransitionSetup: EventHelper.resetProbabilities (act-scoped pity
    // floats) + blizzardPotionMod = 0; cardBlizzRandomizer starts at +5.
    rs.event_pity_monster = 0.1f;
    rs.event_pity_shop = 0.03f;
    rs.event_pity_treasure = 0.02f;
    rs.card_blizz_randomizer = 5;
    rs.blizzard_potion_mod = 0;

    // (1) monsterRng: generateMonsters + initializeBoss -> the encounter lists
    //     (Exordium.java:110-221; generate_monster_lists, B3.12).
    generate_monster_lists(/*act=*/1, rs.monster_rng, rc.lists);

    // (2) relicRng: initializeRelicList's 5 pool-shuffle draws
    //     (AbstractDungeon.java:1237-1241) -- one randomLong() per tier,
    //     UNCONDITIONAL (evaluated as the java.util.Random ctor arg regardless of
    //     pool size). Consumed here so relicRng lands at counter 5 for the oracle;
    //     the complete pool CONTENTS/order (relic_pools[]) are B4.6.
    for (int i = 0; i < kRelicTierCount; ++i) {
        (void)random_long(rs.relic_rng);
    }

    // (3) mapRng: the act map (Exordium.java:56-57). generate_map seeds mapRng =
    //     Random(seed + actNum) internally; encode both edges and room types plus
    //     the end-of-generateMap mapRng state into RunState.map.
    GeneratedMap g = generate_map(seed, /*act_num=*/1);
    const RoomAssignment ra = assign_room_types(g, static_cast<int>(ascension));
    encode_paths_into_run_state(g, rs);   // edges (+ post-path mapRng)
    encode_rooms_into_run_state(ra, rs);  // room types (+ end-of-generateMap mapRng)

    // Base Ironclad sheet (CharSelectInfo: 80/80 HP, 99 gold; Ironclad.java:114).
    // A11 potion slots are live here through potion_slot_count(). The remaining
    // A20 run-setup modifiers (A6 90% HP, A10 curse, A14 -5 max) are B4.15.
    rs.hp = 80;
    rs.max_hp = 80;
    rs.gold = 99;
    rs.potion_slots = static_cast<uint8_t>(potion_slot_count(ascension));

    // Starting deck (5 Strike / 4 Defend / 1 Bash) into the master deck.
    constexpr int kDeckN = static_cast<int>(sizeof(kIroncladStartDeck) /
                                            sizeof(kIroncladStartDeck[0]));
    for (int i = 0; i < kDeckN; ++i) {
        rs.master_deck[i].card_id = static_cast<uint16_t>(kIroncladStartDeck[i]);
        rs.master_deck[i].upgrade = 0;
        rs.master_deck[i].cost_now = 0;
        rs.master_deck[i].flags = 0;
        rs.master_deck[i].misc = 0;
    }
    rs.master_deck_count = static_cast<uint16_t>(kDeckN);

    // Starting relic: Burning Blood (Ironclad.getStartingRelics, Ironclad.java:86),
    // acquisition index 0 (== trigger order, trap 8). Its onVictory heal fires
    // through the combat mirror at the battle-over fold-back.
    rs.relics[0].relic_id = static_cast<uint16_t>(RelicId::BURNING_BLOOD);
    rs.relics[0].counter = 0;
    rs.relic_count = 1;

    rc.phase = static_cast<uint8_t>(RunPhase::NEOW);
    rc.cur_x = kNeowColumn;
    rc.room_type = static_cast<uint8_t>(RoomType::None);
    return rc;
}

// --- legal_actions (run overload) -------------------------------------------

void legal_actions(const RunController& rc, RunActionMask& out) noexcept {
    out = RunActionMask{};
    out.phase = rc.phase;

    switch (static_cast<RunPhase>(rc.phase)) {
        case RunPhase::NEOW:
        case RunPhase::COMBAT_REWARD:
            out.can_proceed = true;
            break;

        case RunPhase::MAP_CHOICE: {
            if (rc.run.floor == 0) {
                // First pick: any connected row-0 node is a valid start.
                for (int x = 0; x < kMapCols; ++x) {
                    if (rc.run.map[run_state_map_index(x, 0)].edges != 0) {
                        out.can_choose_node[x] = true;
                    }
                }
            } else {
                const int y = run_cur_row(rc);
                const uint8_t e =
                    rc.run.map[run_state_map_index(rc.cur_x, y)].edges;
                if ((e & kEdgeLeft) && rc.cur_x > 0) {
                    out.can_choose_node[rc.cur_x - 1] = true;
                }
                if (e & kEdgeCenter) {
                    out.can_choose_node[rc.cur_x] = true;
                }
                if ((e & kEdgeRight) && rc.cur_x + 1 < kMapCols) {
                    out.can_choose_node[rc.cur_x + 1] = true;
                }
                if (e & kEdgeBoss) {
                    out.can_choose_boss = true;
                }
            }
            break;
        }

        case RunPhase::COMBAT: {
            legal_actions(rc.combat, out.combat);
            for (uint8_t slot = 0; slot < rc.run.potion_slots && slot < kPotionCap;
                 ++slot) {
                if (!combat_potion_legal(rc, slot, 0, false)) {
                    continue;
                }
                out.can_use_potion[slot] = true;
                const PotionDef* def =
                    potion_def(static_cast<PotionId>(rc.run.potions[slot]));
                if (def != nullptr && potion_requires_target(*def)) {
                    for (uint8_t target = 0; target < rc.combat.monster_count;
                         ++target) {
                        out.can_use_potion_target[slot][target] =
                            live_target(rc.combat, target);
                    }
                }
            }
            break;
        }

        case RunPhase::ROOM_UNIMPLEMENTED:
        case RunPhase::RUN_OVER:
        case RunPhase::NONE:
        default:
            break;  // nothing legal (parked / terminal)
    }

    const RunPhase phase = static_cast<RunPhase>(rc.phase);
    if (phase != RunPhase::COMBAT && phase != RunPhase::ROOM_UNIMPLEMENTED &&
        phase != RunPhase::RUN_OVER && phase != RunPhase::NONE) {
        for (uint8_t slot = 0; slot < rc.run.potion_slots && slot < kPotionCap;
             ++slot) {
            out.can_use_potion[slot] = noncombat_potion_legal(rc, slot);
        }
    }
}

// --- advance (run overload) -------------------------------------------------

namespace {

void finish_combat_after_action(RunController& rc, StepResult& res) noexcept {
    if (rc.combat.phase != static_cast<uint8_t>(CombatPhase::COMBAT_OVER)) {
        return;
    }
    if (rc.combat.player_hp <= 0) {
        fold_back_combat(rc);
        rc.combat_outcome = static_cast<uint8_t>(RunCombatOutcome::DEFEAT);
        rc.phase = static_cast<uint8_t>(RunPhase::RUN_OVER);
        res = StepResult{};
        res.terminal = true;
        res.reward = -1.0f;
        return;
    }
    enter_combat_reward(rc, RunCombatOutcome::KILLED, res);
}

bool step_potion(RunController& rc, Action a, StepResult& res) noexcept {
    if (action_verb(a) != ActionVerb::USE_POTION) {
        return false;
    }
    RunActionMask mask{};
    legal_actions(rc, mask);
    const uint8_t slot = action_arg0(a);
    const uint8_t target = action_arg1(a);
    if (slot >= kPotionCap || !mask.can_use_potion[slot]) {
        if (rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
            fill_combat_result(rc.combat, res);
        } else {
            fill_run_result(rc, res);
        }
        return true;  // illegal USE is a non-corrupting no-op.
    }

    const PotionId id = static_cast<PotionId>(rc.run.potions[slot]);
    const PotionDef* def = potion_def(id);
    if (def != nullptr && potion_requires_target(*def) &&
        (target >= kMonsterCap || !mask.can_use_potion_target[slot][target])) {
        if (rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
            fill_combat_result(rc.combat, res);
        } else {
            fill_run_result(rc, res);
        }
        return true;
    }

    if (id == PotionId::FRUIT_JUICE) {
        use_fruit_juice(rc, slot);
        dispatch_run_relics_on_use_potion(rc);
    } else if (id == PotionId::ENTROPIC_BREW) {
        use_entropic_brew(rc, slot);
        dispatch_run_relics_on_use_potion(rc);
    } else if (id == PotionId::SMOKE_BOMB) {
        // SmokeBomb.use marks the room smoked; the player's escape timer calls
        // endBattle, which runs onVictory and opens the smoked proceed screen.
        clear_potion_slot(rc.run, slot);
        dispatch_run_relics_on_use_potion(rc);
        rc.combat.phase = static_cast<uint8_t>(CombatPhase::COMBAT_OVER);
        fill_combat_result(rc.combat, res);
        res.reward = 0.0f;  // escape is not a kill.
        enter_combat_reward(rc, RunCombatOutcome::SMOKE_BOMB, res);
        return true;
    } else {
        if (!use_potion(rc.combat, id, target)) {
            fill_combat_result(rc.combat, res);
            return true;
        }
        clear_potion_slot(rc.run, slot);
        dispatch_run_relics_on_use_potion(rc);
        pump(rc.combat, dispatch_monster_turn);
    }

    if (rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
        fill_combat_result(rc.combat, res);
        finish_combat_after_action(rc, res);
    } else {
        fill_run_result(rc, res);
    }
    return true;
}

void step_one(RunController& rc, Action a, StepResult& res) noexcept {
    if (step_potion(rc, a, res)) {
        return;
    }
    switch (static_cast<RunPhase>(rc.phase)) {
        case RunPhase::NEOW: {
            // The Neow blessing itself is B4.14; here CHOOSE simply proceeds onto
            // the map (blessing skipped, documented).
            if (action_verb(a) == ActionVerb::CHOOSE) {
                rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
            }
            fill_run_result(rc, res);
            break;
        }

        case RunPhase::MAP_CHOICE: {
            if (action_verb(a) == ActionVerb::CHOOSE) {
                RunActionMask m{};
                legal_actions(rc, m);
                const uint8_t a0 = action_arg0(a);
                if (a0 == kChooseBoss && m.can_choose_boss) {
                    next_room_transition(rc, 0, /*to_boss=*/true);
                } else if (a0 < kMapCols && m.can_choose_node[a0]) {
                    next_room_transition(rc, a0, /*to_boss=*/false);
                }
                // else: illegal choice -- no-op (cannot corrupt state).
            }
            fill_run_result(rc, res);
            break;
        }

        case RunPhase::COMBAT: {
            // Delegate the combat step to the combat-level advance() (the exact
            // PLAY_CARD / END_TURN / CHOOSE / USE_POTION dispatch + pump), then
            // read the post-step combat phase for the run transition.
            Action acts[1] = {a};
            StepResult srs[1];
            advance(std::span<CombatState>(&rc.combat, 1),
                    std::span<const Action>(acts, 1),
                    std::span<StepResult>(srs, 1));
            res = srs[0];
            finish_combat_after_action(rc, res);
            break;
        }

        case RunPhase::COMBAT_REWARD: {
            // Rewards are assembled by B4.5; here CHOOSE proceeds back to the map.
            if (action_verb(a) == ActionVerb::CHOOSE) {
                rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
            }
            fill_run_result(rc, res);
            break;
        }

        case RunPhase::ROOM_UNIMPLEMENTED:
        case RunPhase::RUN_OVER:
        case RunPhase::NONE:
        default:
            fill_run_result(rc, res);
            break;
    }
}

}  // namespace

void advance(std::span<RunController> runs, std::span<const Action> actions,
             std::span<StepResult> results) noexcept {
    assert(runs.size() == actions.size() && actions.size() == results.size() &&
           "advance(RunController): runs/actions/results must be equal-length spans");
    for (std::size_t i = 0; i < runs.size(); ++i) {
        step_one(runs[i], actions[i], results[i]);
    }
}

}  // namespace sts::engine
