// The RUN-LEVEL batch API implementation. See
// run_advance.hpp for the design, scope boundaries, and provenance.
//
// The combat construction (enter_combat) deliberately MIRRORS combat_begin
// (advance.cpp) field-for-field so that the single-Jaw-Worm path remains
// byte-identical to combat_begin(seed, floor, deck). That equivalence is pinned
// by a named test (run_advance_test's RunCombatMatchesCombatBegin), so any
// future drift in the combat-start sequence is caught rather than silently
// diverging. WHICH encounters can be built here is deliberately not written
// down in this file: the enter_combat gate asks the dispatch switch
// (monster_init_fn(id) != nullptr, per member), so the answer changes as
// monster cases land without any edit here -- an enumerated roster in a
// comment would only rot. The one intentional difference from combat_begin is
// that enter_combat first resolves the encounter composition on miscRng (the
// game's getEncounter, MonsterHelper.java) -- for the single-emit "Jaw Worm"
// encounter that consumes ZERO miscRng draws, preserving the byte-equivalence.

#include "sts/engine/run_advance.hpp"

#include <cassert>
#include <cstdint>
#include <span>
#include <string_view>

#include "sts/engine/action_queue.hpp"     // pump / begin_first_turn
#include "sts/engine/cards.hpp"            // card_def / card_cost / card_flags
#include "sts/engine/combat_rewards.hpp"   // reward assembly + the claim flow
#include "sts/engine/encounters.hpp"       // generate_monster_lists / resolve_encounter
#include "sts/engine/event_framework.hpp"  // ?-room roll + selection + dialog dispatch
#include "sts/engine/interp.hpp"           // Opcode::HEAL (Toy Ornithopter's queued heal)
#include "sts/engine/knowledge.hpp"        // KnowledgeScope + combat-start hooks
#include "sts/engine/map_gen.hpp"          // generate_map / encode_paths_into_run_state / kBossCol / kEdge*
#include "sts/engine/map_rooms.hpp"        // assign_room_types / encode_rooms_into_run_state / RoomType
#include "sts/engine/monster_dispatch.hpp" // spawn_group / dispatch_monster_turn / monster_init_fn
#include "sts/engine/monster_lagavulin.hpp" // event ctor's awake variant
#include "sts/engine/monster_looter.hpp"   // looter_stolen_gold (settlement)
#include "sts/engine/neow.hpp"             // the floor-0 blessing + its screens
#include "sts/engine/omniscient_observation.hpp"      // omniscient_encode_observation
#include "sts/engine/potions.hpp"          // PotionId / use_potion / slot count
#include "sts/engine/relic_hooks.hpp"      // dispatch_relics_on_victory / room-entry hooks
#include "sts/engine/relic_pools.hpp"      // pool init + acquisition/on-pickup
#include "sts/engine/rng_jdk.hpp"          // JdkRandom / jdk_shuffle
#include "sts/engine/rng_stream.hpp"       // floor_stream / random_long / from_seed
#include "sts/engine/run_deck.hpp"         // add_card_to_master_deck (the obtain door)
#include "sts/engine/shop.hpp"             // merchant stock / purchases / purge
#include "relics/relic_pickup.hpp"         // gain_gold (the one run-layer gold door)
#include "interp/interp_powers.hpp"        // op_apply_power (emerald elite buff)
#include "sts/registry/monster_table.hpp"  // monster_def / MonsterDef::is_boss (Smoke Bomb)
#include "sts/engine/treasure_rooms.hpp"   // fixed-row chest lifecycle
#include "sts/registry/game_ids.hpp"       // monster_from_game_id

namespace sts::engine {

namespace {

// The Ironclad starting deck (Ironclad.getStartingDeck, Ironclad.java:92-104):
// 5 Strike, 4 Defend, 1 Bash, in that order (the order is load-bearing -- it is
// the pre-shuffle master-deck order the combat-start shuffle_rng permutes).
// AbstractPlayer.initializeStarterDeck (AbstractPlayer.java:386-390) appends them
// with masterDeck.addToTop, which CardGroup.addToTop (CardGroup.java:455-457)
// implements as a plain ArrayList group.add -- an APPEND, not a head insert
// (addToBottom, :459-461, is the group.add(0, c) one). So this array lands at the
// END of whatever the master deck already holds, which at ascension 10+ is the
// Ascender's Bane the dungeon constructor put there first.
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

// WingBoots / MapRoomNode.wingedIsConnectedTo. A live charge makes every
// non-empty node on the NEXT row selectable; a charge is spent only when the
// chosen node is not connected by a real edge. WingBoots.setCounter turns the
// last `1 -> 0` into `-2` (used up), which is the counter CommunicationMod
// publishes after the third jump.
[[nodiscard]] int wing_boots_slot(const RunState& rs) noexcept {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id ==
            static_cast<uint16_t>(RelicId::WING_BOOTS)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

[[nodiscard]] bool wing_boots_active(const RunState& rs) noexcept {
    const int slot = wing_boots_slot(rs);
    return slot >= 0 && rs.relics[slot].counter > 0;
}

[[nodiscard]] bool map_edge_connects(const RunController& rc,
                                     uint8_t dst_x) noexcept {
    if (rc.run.floor == 0 || dst_x >= kMapCols) {
        return false;
    }
    const int y = run_cur_row(rc);
    if (y < 0 || y >= kMapRows) {
        return false;
    }
    const uint8_t edges =
        rc.run.map[run_state_map_index(rc.cur_x, y)].edges;
    if (dst_x + 1u == rc.cur_x) {
        return (edges & kEdgeLeft) != 0u;
    }
    if (dst_x == rc.cur_x) {
        return (edges & kEdgeCenter) != 0u;
    }
    if (dst_x == rc.cur_x + 1u) {
        return (edges & kEdgeRight) != 0u;
    }
    return false;
}

void spend_wing_boots_charge(RunState& rs) noexcept {
    const int slot = wing_boots_slot(rs);
    if (slot < 0 || rs.relics[slot].counter <= 0) {
        return;
    }
    --rs.relics[slot].counter;
    if (rs.relics[slot].counter == 0) {
        rs.relics[slot].counter = -2;
    }
}

// Fold CombatState-owned persistent fields back into RunState. Potion slots and
// the master deck are already canonical in rc.run while combat is live
// (CombatState deliberately has no duplicate copies), so combat mutations to
// those route there directly. HP/max-HP and relic counters are the actual
// mirrors and must be copied on both kill and Smoke Bomb escape.
//
// THE SINGLE COMBAT FOLD-BACK, and therefore the one place combat-earned gold
// settles. It is called on EVERY combat-end path and on each exactly once:
// enter_combat_reward (victory / mugged / Smoke Bomb) and
// finish_combat_after_action's defeat branch. Gold is NOT a mirror -- there is
// no CombatState copy of the purse to fold -- but an in-combat PRODUCER
// (Hand of Greed, opcode DAMAGE_GREED) has nowhere else to put its payout, so it
// accrues in CombatState.combat_gold and is settled here through gain_gold, the
// one run-layer gold door (Ectoplasm's suppression and the onGainGold relic
// fan-out live behind it, relics/relic_pickup.hpp -- a raw `rs.gold +=` here
// would silently ignore a registered boss relic). The accumulator is ZEROED as
// it settles, so the settlement is idempotent even if a future caller folds
// twice.
//
// The game gains this gold DURING combat (AbstractPlayer.gainGold the instant
// GreedAction sees the kill, GreedAction.java:38), so settling at the fold is
// the same total; a combat-only replay, which never folds, simply carries the
// accumulator and leaves RunState alone.
// How many Fairy in a Bottle potions the belt holds, in slot order over the
// OCCUPIED slots (`potion_slots`, the A11-reduced count). `hasPotion` walks the
// same list (AbstractPlayer.java:1484), and the consuming loop returns on the
// FIRST match (:1486-1493), so leftmost-first is the observable order.
[[nodiscard]] uint8_t count_belt_fairies(const RunState& rs) noexcept {
    uint8_t n = 0;
    const uint8_t slots =
        rs.potion_slots < kPotionCap ? rs.potion_slots : kPotionCap;
    for (uint8_t i = 0; i < slots; ++i) {
        if (static_cast<PotionId>(rs.potions[i]) == PotionId::FAIRY_POTION) {
            ++n;
        }
    }
    return n;
}

// Burn the fairies the combat consumed. The combat mirror is a COUNT, not a slot
// map, so the number spent is (what the belt still holds) - (what the mirror has
// left); the slots are cleared LEFTMOST FIRST, which is the order
// AbstractPlayer.damage's loop consumes them in. Exactly-once by construction:
// this runs inside fold_back_combat, and the mirror is re-derived from the belt
// at the next enter_combat rather than carried across.
void burn_consumed_fairies(RunController& rc) noexcept {
    const uint8_t held = count_belt_fairies(rc.run);
    const uint8_t left = combat_fairy_armed(rc.combat.flags);
    uint8_t to_burn = held > left ? static_cast<uint8_t>(held - left) : 0u;
    const uint8_t slots =
        rc.run.potion_slots < kPotionCap ? rc.run.potion_slots : kPotionCap;
    for (uint8_t i = 0; i < slots && to_burn > 0; ++i) {
        if (static_cast<PotionId>(rc.run.potions[i]) == PotionId::FAIRY_POTION) {
            // topPanel.destroyPotion(slot) (AbstractPlayer.java:1491, and again
            // inside FairyPotion.use:44 -- destroyPotion is idempotent
            // (TopPanel.java:529-531), so this is ONE event, not two).
            rc.run.potions[i] = static_cast<uint16_t>(PotionId::NONE);
            --to_burn;
        }
    }
}

void fold_back_combat(RunController& rc) noexcept {
    burn_consumed_fairies(rc);
    rc.run.hp = rc.combat.player_hp;
    rc.run.max_hp = rc.combat.player_max_hp;
    if (rc.combat.combat_gold > 0) {
        gain_gold(rc.run, static_cast<int32_t>(rc.combat.combat_gold));
        rc.combat.combat_gold = 0;
    }
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

// "Alive" for targeting is the game's dying-or-escaping walk, never hp alone:
// an ESCAPED monster keeps positive hp but has left the screen entirely
// (updateEscapeAnimation, AbstractMonster.java:894-906), and every targeting
// read skips isDying/isEscaping records (MonsterGroup.java:164,180,204,220).
// The combat-layer can_play_target grid already uses this predicate.
bool live_target(const CombatState& s, uint8_t target) noexcept {
    return target < s.monster_count &&
           !monster_dead_or_escaped(s.monsters[target]);
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
    // A potion whose USE is not implemented must NOT be offered. RunState.potions
    // is populated from real captures by the oracle translator (parse_potions,
    // tools/oracle_bridge/translator/src/translate.cpp) for ANY potion with a
    // registry row, so an imported state can hold a still-deferred potion. With
    // only the row-exists + phase checks above, USE_POTION would be legal, the
    // slot would be cleared and NOTHING would happen -- a wrong answer, not a
    // missing feature. Registry-driven (potion_use_implemented, potions.hpp), so
    // it re-opens by itself as potions get implemented.
    if (!potion_use_implemented(id)) {
        return false;
    }
    // SmokeBomb.canUse (SmokeBomb.java:50-63) asks the MONSTERS, never the room:
    //
    //     for (AbstractMonster m : getCurrRoom().monsters.monsters) {
    //         if (m.hasPower("BackAttack")) return false;
    //         if (m.type != AbstractMonster.EnemyType.BOSS) continue;
    //         return false;
    //     }
    //
    // This used to test `rc.room_type == RoomType::Boss` instead. The two agree
    // in every state the S1 run layer can currently PRODUCE -- all three
    // BOSS-typed rows (SLIME_BOSS, THE_GUARDIAN, HEXAGHOST) are Act-1 bosses and
    // only ever appear in a Boss room, and a Boss room in S1 always holds one --
    // so this changes no reachable outcome today. It is still the wrong test:
    // RunState/CombatState are populated from real captures by the oracle
    // translator, so an imported state can pair either half with the other, and
    // the moment a BOSS-typed monster appears outside a boss room (an Act-2+
    // encounter, or an event spawn) the room test silently offers an escape the
    // game refuses. The exact test is available -- `enemy_type` is a live
    // registry column with a MonsterDef::is_boss() accessor -- so it is used.
    //
    // NO LIVENESS GATE, deliberately: the Java walks `monsters.monsters`, the
    // whole group, and a dead or escaped monster is still a member of it. That
    // matters for the Slime Boss, which stays in the group after it splits.
    // (The general "anything left to fight" test below is AbstractPotion.canUse's
    // areMonstersBasicallyDead, a different clause.)
    //
    // BackAttack is an Act-3 power (Snecko / Spiker ambush) with no S1 registry
    // row, so that first clause is constant-false here. Named rather than
    // invented as state; whoever registers BackAttack owns adding it.
    if (id == PotionId::SMOKE_BOMB) {
        for (uint8_t m = 0; m < rc.combat.monster_count; ++m) {
            const auto* mdef = sts::registry::monster_def(
                static_cast<MonsterId>(rc.combat.monsters[m].monster_id));
            if (mdef != nullptr && mdef->is_boss()) {
                return false;
            }
        }
    }
    // "Any monster left to use it on" is the in-the-fight predicate, matching
    // every other liveness read. (The WAITING_ON_USER gate above already makes
    // an all-out-of-the-fight state unreachable here; keep the shared
    // predicate anyway so no hp-only read survives in this file.)
    bool any_in_fight = false;
    for (uint8_t m = 0; m < rc.combat.monster_count; ++m) {
        any_in_fight =
            any_in_fight || !monster_dead_or_escaped(rc.combat.monsters[m]);
    }
    if (!any_in_fight) {
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
    // BloodPotion overrides AbstractPotion.canUse: unlike ordinary combat
    // potions it is legal in every room phase (except We Meet Again) and its
    // use() has a synchronous out-of-combat heal branch
    // (BloodPotion.java:39-59). STS300092 drinks one from a combat-reward
    // screen before claiming gold.
    const bool blocked_by_we_meet_again =
        rc.phase == static_cast<uint8_t>(RunPhase::EVENT_DIALOG) &&
        static_cast<EventId>(rc.event.event_id) == EventId::WE_MEET_AGAIN;
    return !blocked_by_we_meet_again &&
           (id == PotionId::BLOOD_POTION ||
            id == PotionId::FRUIT_JUICE ||
            id == PotionId::ENTROPIC_BREW);
}

void clear_potion_slot(RunState& rs, uint8_t slot) noexcept {
    rs.potions[slot] = static_cast<uint16_t>(PotionId::NONE);
}

// AbstractPotion.canDiscard (AbstractPotion.java:398-400) in full: the belt's
// throw-away button is live for any occupied slot, in any room, in combat and
// out of it, and the one thing that takes it away is a We Meet Again dialog --
// that event confiscates the belt for the duration of its offer, and canUse
// (:404-410) carries the same clause ahead of its own combat gates.
// CommandExecutor rejects an empty slot before it ever reaches canDiscard
// (`selectedPotion instanceof PotionSlot`), which is the row-exists test here.
//
// Note what is deliberately NOT tested: potion_use_implemented. A deferred
// potion BODY is no obstacle to throwing the potion away, because a discard
// never runs the body -- destroyPotion is the whole effect. So this door is
// open across the entire still-deferred potion set instead of waiting on it.
bool potion_discard_legal(const RunController& rc, uint8_t slot) noexcept {
    if (slot >= rc.run.potion_slots || slot >= kPotionCap) {
        return false;
    }
    if (rc.run.potions[slot] == static_cast<uint16_t>(PotionId::NONE)) {
        return false;
    }
    return !(rc.phase == static_cast<uint8_t>(RunPhase::EVENT_DIALOG) &&
             static_cast<EventId>(rc.event.event_id) == EventId::WE_MEET_AGAIN);
}

void use_fruit_juice(RunController& rc, uint8_t slot) noexcept {
    const int amount =
        potion_def(PotionId::FRUIT_JUICE)->potency *
        (run_has_relic(rc.run, RelicId::SACRED_BARK) ? 2 : 1);
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
    // EntropicBrew.use (EntropicBrew.java:38-50), three branches:
    //
    //  IN COMBAT (:39-42): potionSlots x ObtainPotionAction(
    //  returnRandomPotion(true)) -- limited=true, and the rolls are NOT gated
    //  on Sozu; while Sozu is owned each obtain is then suppressed at resolve
    //  (ObtainPotionAction.java:29-38 -- flash, no obtainPotion), so the
    //  stream moves by the full limited sequence and the belt gains nothing.
    //
    //  OUT OF COMBAT with Sozu (:43-45): the check comes BEFORE any roll --
    //  potionRng does not move at all and nothing is obtained.
    //
    //  OUT OF COMBAT otherwise (:46-48): potionSlots x ObtainPotionEffect(
    //  returnRandomPotion()) -- the NO-ARG overload, i.e. limited=false
    //  (AbstractDungeon.java:825-827). `limited` is RNG-visible, not
    //  cosmetic: the limited spam-check loop always redraws at least once
    //  and rejects Fruit Juice, so the two flags spend different draw counts.
    //
    // Every use() branch runs before PotionPopUp destroys the Brew, so the
    // draws all happen even if only one resulting potion fits after the
    // consumed slot opens.
    const bool in_combat = rc.phase == static_cast<uint8_t>(RunPhase::COMBAT);
    const bool sozu = run_has_relic(rc.run, RelicId::SOZU);
    if (!in_combat && sozu) {
        clear_potion_slot(rc.run, slot);
        return;
    }
    PotionId rolls[kPotionCap]{};
    const uint8_t count = rc.run.potion_slots < kPotionCap
                              ? rc.run.potion_slots
                              : static_cast<uint8_t>(kPotionCap);
    for (uint8_t i = 0; i < count; ++i) {
        rolls[i] = return_random_potion(rc.run.potion_rng, /*limited=*/in_combat);
    }
    clear_potion_slot(rc.run, slot);
    if (sozu) {
        return;  // in combat: every ObtainPotionAction resolves as a flash only
    }
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
    //
    // ToyOrnithopter.onUsePotion (ToyOrnithopter.java:31-41) forks on the
    // room's phase:
    //
    //   COMBAT: addToBot(new HealAction(player, player, 5)) -- a QUEUED heal,
    //     landing BEHIND whatever the potion's own use() queued, because
    //     PotionPopUp runs potion.use first (PotionPopUp.java:234-239). The
    //     ordering is observable: Elixir's blocking optional exhaust screen
    //     holds the queue head, so the game's +5 waits for the confirm button
    //     -- STS03352's capture reads 5 hp under the sim for exactly the two
    //     records its screen is open (seq 143-144), reconverging at the
    //     confirm. The old inline write here healed at use time, and also
    //     skipped heal_player_with_relics -- i.e. Magic Flower's onPlayerHeal
    //     pass -- which a real HealAction goes through (HEAL opcode, op_heal).
    //
    //   otherwise: a plain player.heal(5) (:39), whose tail is the
    //     not-bloodied cross (AbstractCreature.heal:404-408) --
    //     heal_out_of_combat IS that shape (clamp, then the cross), so an
    //     Ornithopter heal past half disarms an active Red Skull exactly as a
    //     rest's heal does. Magic Flower does not scale here: its override is
    //     gated on the room being in RoomPhase.COMBAT (MagicFlower.java:30-37).
    //
    // The caller owns the pump: every in-combat step_potion branch pumps after
    // this dispatch, so a heal nothing blocks resolves before control returns.
    for (uint8_t i = 0; i < rc.run.relic_count; ++i) {
        if (static_cast<RelicId>(rc.run.relics[i].relic_id) !=
            RelicId::TOY_ORNITHOPTER) {
            continue;
        }
        if (rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
            ActionQueueItem heal{};
            heal.opcode = static_cast<uint16_t>(Opcode::HEAL);
            heal.src = kActorPlayer;
            heal.tgt = kActorPlayer;
            heal.amount = 5;  // HEAL_AMT (ToyOrnithopter.java:22)
            add_to_bottom(rc.combat, heal);
        } else {
            heal_out_of_combat(rc.run, 5);
        }
    }
}

// Mirrors advance.cpp's fill_result: "in the fight" is monster_dead_or_escaped
// (an escaped monster keeps positive hp but ends the battle like a dead one),
// and a player escape is terminal without being a loss.
void fill_combat_result(const CombatState& s, StepResult& r) noexcept {
    const bool player_dead = s.player_hp <= 0;
    const bool player_escaped = (s.flags & kCombatFlagPlayerEscaped) != 0u;
    bool any_in_fight = false;
    for (uint8_t m = 0; m < s.monster_count; ++m) {
        any_in_fight = any_in_fight || !monster_dead_or_escaped(s.monsters[m]);
    }
    r = StepResult{};
    r.terminal = player_dead || player_escaped || !any_in_fight;
    if (player_dead) {
        r.reward = -1.0f;
    } else if (player_escaped) {
        r.reward = 0.0f;
    } else if (!any_in_fight) {
        r.reward = 1.0f;
    }
    omniscient_encode_observation(s, r.omniscient_obs);
}

// MonsterGroup.haveMonstersEscaped (MonsterGroup.java:124-130): true iff EVERY
// monster's `escaped` is set. A dead monster is NOT escaped, so any kill in
// the group keeps this false.
bool have_monsters_escaped(const CombatState& s) noexcept {
    for (uint8_t m = 0; m < s.monster_count; ++m) {
        if (!monster_escaped(s.monsters[m])) {
            return false;
        }
    }
    return true;
}

// Settle the thieves' stolen gold against the run's purse at combat end.
// The game deducts at STEAL time (DamageAction.stealGold, DamageAction.java:
// 98-114 -- a direct target.gold write, clamped per steal to the player's
// remaining gold, bypassing gainGold and its relic hooks); the engine instead
// accrues the UNCLAMPED count on the monster
// record (looter_stolen_gold) and settles min(total, gold) here -- a
// DELIBERATE deviation that preserves exactly-once settlement on every
// combat-end path (orchestrator decision 2026-07-28: the deviation stands;
// the faithful steal-time signed purse delta is re-scoped to an owner-approved
// Act-2-adjacent task alongside the Mugger work flagged below). This header
// used to say "CombatState carries no gold field"; that has been stale since
// schema 6 -- CombatState.combat_gold exists, but it is the Hand of Greed
// GAIN accumulator (settled once through gain_gold at fold-back, next
// paragraph), not a purse mirror a steal-time clamp could read. The clamp is
// equal to the game's per-steal clamps whenever the thief's steals are the
// combat's only gold movement. Every Act-1 group fields at most one thief, and
// gold moves again only at reward claim, after this. ONE combat effect now also
// produces gold -- Hand of Greed (DAMAGE_GREED) -- and its accumulator is
// settled by fold_back_combat, which runs just BEFORE this on both combat-end
// paths. So a Greed payout is already in the purse this clamp reads, which
// models the game whenever the greed kill preceded the steal (the game's
// gainGold is immediate) and over-credits the thief only in the corner where the
// steal came first AND the purse was below the steal amount. Nothing else in the
// combat layer touches RunState.gold while a combat is live.
//
// Returns the portion carried by DEAD thieves -- exactly what Looter.die()
// feeds addStolenGoldToRewards (its clamped accrual, Looter.java:170-172),
// i.e. the claimable STOLEN_GOLD return. An ESCAPED thief's share is simply
// gone. Called on EVERY combat end, defeat included: the game's deduction
// happened at steal time, so a dead player's final purse is short too.
int32_t settle_stolen_gold(RunController& rc) noexcept {
    int32_t total = 0;
    int32_t dead = 0;
    for (uint8_t m = 0; m < rc.combat.monster_count; ++m) {
        const MonsterState& ms = rc.combat.monsters[m];
        if (static_cast<MonsterId>(ms.monster_id) != MonsterId::LOOTER) {
            continue;
        }
        const int32_t g = looter_stolen_gold(ms);
        total += g;
        if (ms.hp <= 0) {
            dead += g;
        }
    }
    if (total <= 0) {
        return 0;
    }
    const int32_t settled = total < rc.run.gold ? total : rc.run.gold;
    rc.run.gold -= settled;
    return dead < settled ? dead : settled;
}

// THE reward gate: which RewardOutcome a finished combat earned. Explicit and
// exhaustive -- "combat over" does not imply a kill, and a kill is not the
// only screen shape. DEFEAT/NONE never reach the reward path
// (finish_combat_after_action routes death to RUN_OVER first).
//
// `monsters_escaped` is haveMonstersEscaped -- and IT, not the mugged flag, is
// what the battle-over block's gold gate (AbstractRoom.java:319) and
// potion-chance gate (:585-589) read. A MUGGED combat whose other members
// were killed (or that the player then smoke-bombed past) therefore assembles
// the FULL kill shape: the mug screen still calls setupItemReward
// (CombatRewardScreen.java:280-285), and only the banner -- which this engine
// does not model -- differs.
RewardOutcome reward_outcome_for(RunCombatOutcome outcome,
                                 bool monsters_escaped) noexcept {
    switch (outcome) {
        case RunCombatOutcome::SMOKE_BOMB:
            return RewardOutcome::PLAYER_ESCAPED;
        case RunCombatOutcome::MUGGED:
            return monsters_escaped ? RewardOutcome::MONSTERS_ESCAPED
                                    : RewardOutcome::KILLED;
        case RunCombatOutcome::KILLED:
        default:
            return RewardOutcome::KILLED;
    }
}

void enter_combat_reward(RunController& rc, RunCombatOutcome outcome,
                         StepResult& res) noexcept {
    // AbstractRoom.endBattle (AbstractRoom.java:413-421): Meat on the Bone's
    // onTrigger fires FIRST (:418-420, heal 12 at <= half HP), THEN
    // player.onVictory runs every relic's onVictory -- for ordinary victory and
    // Smoke Bomb alike, before the reward screen opens. The explicit pre-step
    // keeps Meat ahead of Burning Blood regardless of acquisition order.
    apply_meat_on_the_bone_pre_victory(rc.combat);
    dispatch_relics_on_victory(rc.combat, rc.combat.relics,
                               rc.combat.relic_count);
    fold_back_combat(rc);
    // Stolen-gold settlement: the game's purse already reflects every steal by
    // now (deducted at steal time), so settle before anything reads rs.gold.
    // The dead-thief portion returns as a claimable STOLEN_GOLD item below --
    // and on a PLAYER_ESCAPED end the assembly discards it unclaimed, exactly
    // as the smoked screen never shows the room's reward list.
    const int32_t stolen_return = settle_stolen_gold(rc);
    rc.combat_outcome = static_cast<uint8_t>(outcome);
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT_REWARD);

    // The battle-over reward block + screen open, assembled HERE -- after
    // endBattle's Meat-on-the-Bone / onVictory pass above, exactly the game's
    // order (AbstractRoom.endBattle:413-421 runs before the update loop reaches
    // the battle-over block at :277-357). Boss gold draws the floor-scoped
    // miscRng (rc.combat.misc_rng -- trap 18); a Smoke Bomb consumes the
    // gold / elite-relic / potion draws but offers nothing (openCombat's smoked
    // path never calls setupItemReward, CombatRewardScreen.java:267-289).
    assemble_combat_rewards(rc.run, rc.combat.misc_rng,
                            static_cast<RoomType>(rc.room_type),
                            reward_outcome_for(outcome,
                                               have_monsters_escaped(rc.combat)),
                            rc.rewards, stolen_return,
                            static_cast<RoomType>(rc.room_type) ==
                                RoomType::Event);

    const float combat_reward = res.reward;
    res = StepResult{};  // reward screens have no combat observation view.
    res.reward = combat_reward;
    res.terminal = false;  // the run continues at a CHOOSE/proceed screen.
}

void finish_combat_after_action(RunController& rc, StepResult& res) noexcept;

// Build the live combat for `enc_key` and set rc.phase accordingly. Returns true
// iff a real combat was entered; false parks the run at ROOM_UNIMPLEMENTED
// (unknown encounter / a member monster not yet implemented). A battle-start
// effect can kill the whole group before player control, in which case the
// successful entry has already advanced to COMBAT_REWARD when this returns.
// The five floor streams are re-derived here (identical to the caller's reseed) so
// the build is a pure function of (run, floor, encounter).
//
// `elite_trigger` is AbstractRoom.eliteTrigger for an encounter whose ROOM KIND
// does not imply it: the Act-1 Dead Adventurer sets it on an EventRoom
// (DeadAdventurer.java:116). RoomType::Elite implies it on its own
// (MonsterRoomElite.java:33); RoomType::Boss deliberately does NOT
// (MonsterRoomBoss.java:22-24 never touches the field).
bool enter_combat(RunController& rc, std::string_view enc_key,
                  RoomType room, bool preserve_floor_streams = false,
                  EventCombatVariant variant = EventCombatVariant::NONE,
                  bool elite_trigger = false) noexcept {
    const int64_t seed = rc.run.run_seed;
    const int32_t floor = static_cast<int32_t>(rc.run.floor);

    // The run's knowledge records through the construction below (nested
    // no-op when step_one already attached the same target; live for direct
    // enter_combat / enter_event_combat callers).
    KnowledgeScope kscope(&rc.knowledge);

    CombatState s{};                              // value-init: byte-clean scratch
    // The room's eliteTrigger, set before ANY consumer runs: energy_master
    // (Slaver's Collar) is read by begin_first_turn's recharge line, and Sling /
    // Preserved Insect read the bit from atBattleStart -- both inside the shared
    // turn-1 block begin_first_turn reaches at step (11).
    if (room == RoomType::Elite || elite_trigger) {
        s.flags |= kCombatFlagEliteRoom;
    }
    // Mirror the BELT's Fairy in a Bottle count into the combat. FairyPotion
    // fires from AbstractPlayer.damage on ANY lethal HP write and the combat
    // layer has no belt to consult -- see kCombatFlagFairyArmedShift
    // (combat_state.hpp) for the full rationale, including why a post-hoc
    // run-layer revive would be observably wrong. This is the ONLY producer: a
    // bare combat_begin (advance.cpp) has no RunState and correctly leaves the
    // count at 0, the same answer the game gives a player holding no potions.
    s.flags = with_combat_fairy_armed(s.flags, count_belt_fairies(rc.run));
    if (preserve_floor_streams) {
        s.monster_hp_rng = rc.combat.monster_hp_rng;
        s.ai_rng = rc.combat.ai_rng;
        s.shuffle_rng = rc.combat.shuffle_rng;
        s.card_random_rng = rc.combat.card_random_rng;
        s.misc_rng = rc.combat.misc_rng;
    } else {
        reseed_floor_streams(s, seed, floor);
    }

    // (1) Composition (miscRng): MonsterHelper.getEncounter (encounters.hpp).
    ResolvedGroup grp{};
    if (!resolve_encounter(enc_key, s.misc_rng, grp) || grp.count == 0) {
        rc.combat = s;
        rc.phase = static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED);
        rc.room_type = static_cast<uint8_t>(room);
        return false;
    }

    // (2) Map composition game_ids -> MonsterIds; require every member implemented.
    //     "Implemented" is asked of the dispatch table, never of a list kept here:
    //     a member is live iff monster_init_fn(id) returns non-null, which is true
    //     exactly when monster_dispatch.cpp has a case for it. The game has no
    //     counterpart to this gate -- MonsterHelper.getEncounter
    //     (MonsterHelper.java:389) constructs a real AbstractMonster for every
    //     member -- so the gate exists only to park what this engine has not
    //     translated yet, and it retires itself as cases land. If any member is
    //     unimplemented we have still consumed miscRng exactly as the game would,
    //     then park.
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
    // The construction trace (kept members plus every discarded PICK
    // candidate, in ctor order -- see ResolvedGroup). A discarded candidate
    // needs only a registry def (its ctor draws burn off the ranges); an
    // unknown one would corrupt the monster_hp_rng accounting, so it parks
    // exactly as an unimplemented member does.
    MonsterId trace_ids[kConstructedCap] = {};
    for (uint8_t i = 0; i < grp.constructed_count; ++i) {
        const MonsterId id = static_cast<MonsterId>(
            sts::registry::monster_from_game_id(grp.constructed[i]));
        if (id == MonsterId::NONE ||
            sts::registry::monster_def(id) == nullptr) {
            all_impl = false;
        }
        trace_ids[i] = id;
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
        // A bottled master-deck instance (run_deck.hpp bottle bits) joins the
        // opening-draw top placement exactly as an Innate card does:
        // CardGroup.initializeDeck's collection is `if (c.isInnate) placeOnTop
        // ... else if (inBottleFlame || inBottleLightning || inBottleTornado)
        // placeOnTop` (CardGroup.java:933-941) -- one list, shuffle order
        // preserved, and the if/else-if means a card that is BOTH Innate and
        // bottled is added once, which OR-ing into the same flag reproduces.
        // The bottle bits themselves stay master-deck-only: combat flags are
        // registry-derived (the run_deck.hpp encoding note), so nothing below
        // ever reads the master bits again.
        if (master_card_bottled(rc.run.master_deck[i])) {
            s.card_pool[i].flags |= static_cast<uint16_t>(CardFlag::INNATE);
        }
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
    // puts them on top. This must match combat_begin, including Writhe.
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

    // Knowledge observer (knowledge.hpp): fresh combat -> fresh knowledge,
    // BEFORE the spawns below can telegraph a reveal. The hand-rolled shuffle
    // above fires no hook; initial pile knowledge is armed at step (9b).
    knowledge_reset();

    // (6) Spawn the resolved group's CONSTRUCTION trace: kept members roll HP
    //     (monster_hp_rng) + rollMove (ai_rng) at their construction-order
    //     positions; discarded PICK candidates burn their ctor draws in place
    //     (spawn_group_trace -- the STS01789 class).
    if (variant == EventCombatVariant::LAGAVULIN_AWAKE &&
        grp.count == 1 && ids[0] == MonsterId::LAGAVULIN) {
        s.monster_count = 1;
        lagavulin_init_awake(s, 0);
    } else {
        spawn_group_trace(
            s, std::span<const MonsterId>(trace_ids, grp.constructed_count),
            grp.kept_mask);
    }

    // (7) Monster pre-battle actions run after every member is spawned and
    //     before turn 1. Louses roll and apply Curl Up here.
    use_pre_battle_actions(s);

    // (8) Combat relic mirror: RunState.relics -> CombatState.relics (the seam,
    //     filled at combat spawn per its Log; folded back at combat end).
    for (uint8_t i = 0; i < rc.run.relic_count; ++i) {
        s.relics[i] = rc.run.relics[i];
    }
    s.relic_count = rc.run.relic_count;

    // (9) The emerald-key elite entry roll (G6 campaign 2 spot-diff §8.1).
    //     AbstractPlayer.preBattlePrep (AbstractPlayer.java:1602-1605), right
    //     after monsters.usePreBattleAction() -- i.e. between step (7) above and
    //     the turn-1 block below:
    //
    //       if (Settings.isFinalActAvailable && getCurrMapNode().hasEmeraldKey)
    //           getCurrRoom().applyEmeraldEliteBuff();
    //
    //     The base AbstractRoom body is empty (AbstractRoom.java:129-131); only
    //     MonsterRoomElite overrides it (MonsterRoomElite.java:39-68), re-tests
    //     the same gate, rolls AbstractDungeon.mapRng.random(0, 3) -- the only
    //     mid-run mapRng consumer; the other three consumers all run at act
    //     start -- and addToBottom's ONE buff onto every member of the group.
    //     Those queued actions resolve during the pre-turn-1 drain
    //     (AbstractRoom.java:229-235), before the player ever acts, so applying
    //     them synchronously here is faithful; the ApplyPowerAction interception
    //     chain is inert for a monster buffing itself (the Philosopher's Stone
    //     derivation, relics_boss.cpp) and Sentry's Artifact only eats DEBUFFS.
    //
    //     hasEmeraldKey is true on exactly the node the setEmeraldElite draw
    //     chose (AbstractDungeon.java:542-556; recorded at run_begin as
    //     rc.emerald_x/emerald_y), and isFinalActAvailable passes on the frozen
    //     fully-unlocked profile (the map_rooms.hpp step-5 live-oracle finding).
    //     Settings.hasEmeraldKey does NOT gate the entry roll -- only the
    //     placement -- and an event-combat cannot reach here with room == Elite
    //     (a ? node carries no key flag and resolves with RoomType::Event).
    //     Deviation, unreachable in S1: an elite whose encounter PARKED above
    //     (unimplemented member) returns before this roll, while the game rolls
    //     regardless; every Act-1 elite encounter is implemented.
    if (room == RoomType::Elite && rc.emerald_x != kNoEmeraldNode &&
        rc.cur_x == rc.emerald_x &&
        run_cur_row(rc) == static_cast<int>(rc.emerald_y)) {
        const int32_t act = static_cast<int32_t>(rc.run.act);
        switch (random(rc.run.map_rng, 0, 3)) {
            case 0:  // StrengthPower(actNum + 1)     (MonsterRoomElite.java:42-46)
                for (uint8_t i = 0; i < s.monster_count; ++i) {
                    op_apply_power(s, i, i, PowerId::STRENGTH,
                                   act + 1);
                }
                break;
            case 1:  // IncreaseMaxHpAction(0.25f)    (MonsterRoomElite.java:48-52)
                // this.target.increaseMaxHp(MathUtils.round((float)maxHealth *
                // 0.25f), true) (IncreaseMaxHpAction.java:27-31);
                // AbstractCreature.increaseMaxHp (:199-208) raises maxHealth and
                // heals the same amount -- at this point every member is at full
                // HP (nothing has damaged it), so hp lands on the new max.
                for (uint8_t i = 0; i < s.monster_count; ++i) {
                    MonsterState& m = s.monsters[i];
                    const int32_t amt = mathutils_round(
                        static_cast<float>(m.max_hp) * 0.25f);
                    m.max_hp = static_cast<int16_t>(m.max_hp + amt);
                    const int32_t healed =
                        static_cast<int32_t>(m.hp) + amt;
                    m.hp = static_cast<int16_t>(
                        healed > m.max_hp ? m.max_hp : healed);
                }
                break;
            case 2:  // MetallicizePower(actNum*2+2)  (MonsterRoomElite.java:54-58)
                for (uint8_t i = 0; i < s.monster_count; ++i) {
                    op_apply_power(s, i, i, PowerId::METALLICIZE,
                                   act * 2 + 2);
                }
                break;
            case 3:  // RegenerateMonsterPower(1 + actNum*2)  (:60-64)
                // "Regenerate" (RegenerateMonsterPower.java:17) is a distinct
                // power from the player's "Regeneration" (REGEN, id 18, which
                // decrements; the monster one never does) -- registered as
                // PowerId::REGENERATE_MONSTER, id 91 (registry/powers.yaml).
                // NOTE the arithmetic: 1 + actNum*2, NOT arm 2's actNum*2+2.
                for (uint8_t i = 0; i < s.monster_count; ++i) {
                    op_apply_power(s, i, i, PowerId::REGENERATE_MONSTER,
                                   act * 2 + 1);
                }
                break;
        }
    }

    // (9b) Knowledge observer: construction is final (pile built at (4),
    //      relic mirror final at (8), spawns telegraphed at (6)). Arms the
    //      REVEAL_DRAW_ORDER full-order record and retro-gates any telegraph
    //      reveal recorded while the mirror was still empty -- see
    //      knowledge_on_combat_ready's contract for why this cannot move the
    //      mirror copy itself (the (6)/(8) order is oracle-verified).
    knowledge_on_combat_ready(s);

    // (10) applyPreCombatLogic (AbstractPlayer.java:1885-1890), the LAST line of
    //      preBattlePrep (:1607) -- the same call combat_begin (advance.cpp)
    //      makes, at the same point in the sequence: AFTER the emerald entry
    //      roll (:1602-1605, step 9 above), which is why it sits on this side of
    //      it. The slot is forced from both sides: player_relics reads the
    //      mirror, so it must follow (8); atPreBattle must precede the opening
    //      DrawCardAction, so it must precede (11). Nothing is drained here --
    //      what this queues sits at the front of the queue begin_first_turn's
    //      own pump() drains, ahead of the draw item start_of_turn queues, which
    //      is the Java's pre-turn-1 drain position (AbstractRoom.java:229-235).
    //
    //      This is the call that gives a RUN-layer combat its Snecko Eye
    //      Confusion, and it is RNG-VISIBLE going forward: ConfusionPower
    //      .onCardDraw (ConfusionPower.java:38-48) spends one cardRandomRng
    //      random(3) per drawn card with cost >= 0, from the opening hand on.
    //      That is the point of the hook -- the first hand is exactly what
    //      atPreBattle exists to catch -- not a side effect to be avoided.
    {
        const RelicView rv = player_relics(s);
        dispatch_relics_at_pre_battle(s, rv.relics, rv.count);
    }

    // (11) The game's turn-1 block, into WAITING_ON_USER. Literally the same
    //      function combat_begin (advance.cpp) calls, so the two
    //      combat-construction paths cannot disagree about how a combat starts
    //      -- INCLUDING the relic battle-start hooks: the turn-1 block itself
    //      (start_of_turn, action_queue.cpp) queues the opening DrawCardAction
    //      (AbstractRoom.java:242) and then dispatches AT_BATTLE_START (:245)
    //      and AT_TURN_START (:253) in the Java's order, so an immediate body
    //      (Stone Calendar's counter = 0) lands before turn 1's ++ while an
    //      addToBot body still resolves behind the opening draw. The full
    //      derivation, including why the energy SET makes the ordering benign
    //      for queued ENERGY grants, sits on the dispatch site itself.
    //
    //      atBattleStartPreDraw is a SEPARATE hook, not a conflation of this
    //      one: AT_BATTLE_START_PRE_DRAW already exists in the hook inventory
    //      (relic_hooks.hpp) and is fired from applyStartOfCombatPreDrawLogic
    //      (AbstractPlayer.java:1903-1908) at AbstractRoom.java:241, genuinely
    //      before the draw. No dispatch is wired for it because no row in
    //      registry/relics.yaml binds it -- in the Java its only holders are
    //      GamblingChip, HolyWater, NinjaScroll, PureWater and Toolbox.
    //      GamblingChip IS a registered, live row, but it deliberately does not
    //      bind this hook: its atBattleStartPreDraw only clears a private
    //      `activated` latch (GamblingChip.java:30-32), and the engine carries
    //      that latch in RelicSlot.counter, whose -1 unset default IS the
    //      cleared state at every fresh CombatState -- binding a hook with no
    //      dispatch site would be inert code (see the row's provenance). The
    //      other four are unregistered. When a row genuinely needs the hook it
    //      needs its own call, ahead of the draw item, inside the shared block.
    begin_first_turn(s, dispatch_monster_turn);

    // (12) initializeDeck's overflow draw when the innate/bottled placeOnTop
    //      collection exceeds masterHandSize (CardGroup.java:951-953) -- the
    //      Snecko-enlarged field, derived here as game_hand_size(s), NOT the
    //      constant 5 (see queue_innate_overflow_draw). It rides
    //      preTurnActions, which drain only after `actions` empties
    //      (GameActionManager.java:190-191) -- i.e. after the turn-1 block AND
    //      the atBattleStart bodies it now dispatches internally (step 11),
    //      which is exactly this position. See queue_innate_overflow_draw's
    //      declaration (advance.hpp).
    queue_innate_overflow_draw(s, innate_count);

    rc.combat = s;
    rc.room_type = static_cast<uint8_t>(room);
    rc.combat_outcome = static_cast<uint8_t>(RunCombatOutcome::NONE);
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT);
    // The turn-1 pump can itself finish combat: for example, Neow's Lament
    // puts the first three groups at 1 HP and Mercury Hourglass's queued
    // at-turn-start damage kills them before the player receives control.
    // AbstractRoom opens rewards from that same battle-update pass. Collapse
    // the run phase here as well, rather than exposing a live COMBAT phase with
    // a COMBAT_OVER child and therefore an empty legal-action set.
    StepResult entry_result{};
    finish_combat_after_action(rc, entry_result);
    return true;
}

// onPlayerEntry dispatch for the room just entered (AbstractDungeon.java:1800).
// Combat rooms build the combat via the encounter framework; a RestRoom opens
// its campfire menu; Treasure constructs its chest immediately (two
// treasureRng calls) and waits for open/skip; an Event (?) room first resolves
// its real kind on the one committed eventRng roll and re-enters as that kind.
// Room kinds without content (shops) reseed the floor streams (for
// oracle-reseed visibility) and park at ROOM_UNIMPLEMENTED.
//
// `left_room` is the RESOLVED kind of the room being exited (RoomType::None at
// Neow) -- the ?-roll's shop gate reads it, because in the game the roll runs
// before setCurrMapNode (AbstractDungeon.java:1767 vs :1783) so
// `getCurrRoom() instanceof ShopRoom` sees the departed room.
//
// `fire_on_enter_room` is FALSE on exactly one path: the ? recursion below,
// which re-enters as the RESOLVED kind. The game fires onEnterRoom once per
// nextRoomTransition, against the PRE-roll EventRoom (AbstractDungeon.java:
// 1755-1757 vs the roll at :1766-1781), so the outer entry already ran it and
// the inner one must not. It is a parameter rather than a `room != Event` test
// precisely because that predicate is the one the recursion breaks: the inner
// call arrives as Monster/Treasure/Shop and would pass it. Maw Bank has no room
// gate, so a second call would pay 12 gold twice on a ?->Shop.
void on_player_entry_impl(RunController& rc, RoomType room, RoomType left_room,
                          bool fire_on_enter_room) noexcept {
    const int64_t seed = rc.run.run_seed;
    const int32_t floor = static_cast<int32_t>(rc.run.floor);

    rc.room_type = static_cast<uint8_t>(room);
    rc.combat_outcome = static_cast<uint8_t>(RunCombatOutcome::NONE);
    rc.event = EventDialogState{};  // no dialog survives a room boundary
    rc.shop = ShopState{};          // nor does a merchant

    auto stall = [&](RoomType r) noexcept {
        rc.combat = CombatState{};
        reseed_floor_streams(rc.combat, seed, floor);
        rc.rest = RestSiteState{};
        rc.phase = static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED);
        rc.room_type = static_cast<uint8_t>(r);
    };

    // AbstractRelic.onEnterRoom, against the room being ARRIVED at and before
    // anything else in this transition (AbstractDungeon.java:1755-1757): the
    // pre-roll, pre-setCurrMapNode, once-per-transition hook. Ssserpent Head,
    // Maw Bank and Eternal Feather live here; the contract is written out at
    // dispatch_on_enter_room_relics (event_framework.hpp).
    if (fire_on_enter_room) {
        dispatch_on_enter_room_relics(rc.run, room);
    }

    // AbstractRelic.justEnteredRoom for the room actually arrived in
    // (AbstractDungeon.java:1785-1789). An EventRoom is EXCLUDED here on
    // purpose: the game replaces that room object with its rolled result
    // (:1763-1779) and calls setCurrMapNode (:1783) BEFORE this fan-out, so the
    // ? branch below dispatches it after resolving -- which is exactly what
    // makes Meal Ticket heal on a ?->Shop as it does on a static ShopRoom.
    if (room != RoomType::Event) {
        dispatch_just_entered_room_relics(rc.run, room == RoomType::Shop);
    }

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
        case RoomType::Rest:
            // RestRoom.onPlayerEntry (RestRoom.java:33-42) fires every relic's
            // onEnterRestRoom at :39-41 -- Ancient Tea Set's arming, and the
            // only implementor in the game. It runs from getCurrRoom()
            // .onPlayerEntry() at AbstractDungeon.java:1800, AFTER the
            // onEnterRoom / justEnteredRoom fan-outs -- which is why Eternal
            // Feather's heal (LIVE, riding dispatch_on_enter_room_relics at
            // the top of this function) has already landed by this line, and
            // lands whether or not the campfire below auto-completes.
            //
            // ONE recorded, unobservable order swap: the Java constructs
            // CampfireUI (whose initializeButtons makes the no-usable-button
            // auto-complete decision, :97-104) at RestRoom.java:38, BEFORE the
            // onEnterRestRoom loop; here the arming runs before the menu
            // check. Equivalent because the only onEnterRestRoom implementor
            // writes a relic counter and no relic's arming reads or edits the
            // button list -- pinned by
            // RestSites.FeatherHealsAndTeaSetArmsEvenWhenTheCampfireAutoCompletes.
            dispatch_relics_on_enter_rest_room(rc.run);
            rc.combat = CombatState{};
            reseed_floor_streams(rc.combat, seed, floor);
            rc.rewards = RewardScreen{};
            rc.rewards.open_card_item = kNoOpenCardReward;
            rc.rest = RestSiteState{};
            rc.rest.screen = static_cast<uint8_t>(RestScreen::MENU);
            rc.phase = static_cast<uint8_t>(RunPhase::REST_SITE);
            // CampfireUI.initializeButtons ends by asking whether ANY button is
            // usable, and when none is it sets waitTimer = 0 and the room's
            // phase = COMPLETE (CampfireUI.java:97-104) -- the campfire resolves
            // with no player decision at all. An empty menu is a ROOM here, not
            // an error: Coffee Dripper with a fully-upgraded, unpurgeable deck
            // reaches it (Rest vetoed, Smith unusable on its own terms), and so
            // does Coffee Dripper together with Fusion Hammer. Without this the
            // run would offer no legal action and a soak would report the floor
            // as a no_legal_moves dead end.
            //
            // It is the LAST thing rest-room entry does, which is also where
            // it belongs relative to entry hooks: the game builds CampfireUI
            // from RestRoom.onPlayerEntry, after the relic onEnterRoom fan-out
            // -- so Eternal Feather's heal (live, fired at the top of this
            // function) and Ancient Tea Set's arming (fired above) have both
            // landed before a skipped campfire resolves, exactly as in the
            // game.
            if (!rest_menu_has_usable_option(build_rest_menu(rc.run))) {
                rc.rest = RestSiteState{};
                rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
            }
            break;
        case RoomType::Treasure:
            rc.combat = CombatState{};
            reseed_floor_streams(rc.combat, seed, floor);
            rc.rewards = RewardScreen{};
            rc.rewards.open_card_item = kNoOpenCardReward;
            rc.treasure_chest = roll_treasure_chest(rc.run);
            rc.phase = static_cast<uint8_t>(RunPhase::TREASURE_ROOM);
            break;
        case RoomType::Event: {
            // Relic.onEnterRoom already ran, above, against RoomType::Event --
            // the ORIGINAL EventRoom, before the game replaces it with the
            // rolled room (:1755-1757 vs :1766-1781). In particular, Ssserpent
            // Head has its 50 gold and an unused Maw Bank its 12 even when this
            // ? becomes a monster, shop or chest, and the recursions below pass
            // fire_on_enter_room=false so neither is paid twice.

            // The ?-room resolution (AbstractDungeon.java:1763-1779). The
            // game rolls on a counter-replay duplicate and assigns it back
            // (:1770) -- under the one-draw invariant that equals drawing the
            // one float straight from the persistent stream, which is what
            // event_room_roll does. This is the ONLY eventRng advance, ever.
            const EventRoomResult roll =
                event_room_roll(rc.run, left_room == RoomType::Shop);
            switch (roll) {
                case EventRoomResult::MONSTER:
                    // generateRoom (:1823-1840) builds a REAL MonsterRoom, so
                    // the combat consumes monsterList and its exit advances
                    // monster_cursor (rc.room_type carries Monster from here).
                    on_player_entry_impl(rc, RoomType::Monster, left_room,
                                         /*fire_on_enter_room=*/false);
                    return;
                case EventRoomResult::TREASURE:
                    // A real TreasureRoom: the ordinary chest flow,
                    // byte-identical to a map treasure node.
                    on_player_entry_impl(rc, RoomType::Treasure, left_room,
                                         /*fire_on_enter_room=*/false);
                    return;
                case EventRoomResult::SHOP:
                    // A real ShopRoom, byte-identical to a map shop node --
                    // including the justEnteredRoom fan-out the recursion
                    // performs, which is where Meal Ticket's heal comes from.
                    on_player_entry_impl(rc, RoomType::Shop, left_room,
                                         /*fire_on_enter_room=*/false);
                    return;
                case EventRoomResult::ELITE:
                    // Unreachable without the DeadlyEvents/endless mods
                    // (event_framework.hpp); park honestly if it ever appears.
                    stall(RoomType::Elite);
                    return;
                case EventRoomResult::EVENT:
                default:
                    break;  // fall through to the event selection below
            }
            // The room stayed an EventRoom, so the deferred justEnteredRoom
            // fan-out runs here with in_shop false -- an explicit no-op rather
            // than an omission, so the ? branch has the call the other branches
            // get through the recursion.
            dispatch_just_entered_room_relics(rc.run, /*in_shop=*/false);

            // EventRoom.onPlayerEntry (EventRoom.java:26-31): selection on a
            // throwaway duplicate (discarded; rs.event_rng byte-identical),
            // pool-removal + event_flags bookkeeping committed.
            rc.combat = CombatState{};
            reseed_floor_streams(rc.combat, seed, floor);
            rc.rest = RestSiteState{};
            rc.rewards = RewardScreen{};
            rc.rewards.open_card_item = kNoOpenCardReward;
            const uint16_t event_id = generate_event(rc.run);
            rc.event = EventDialogState{};
            rc.event.event_id = event_id;
            const EventDialogImpl* impl = event_dialog_impl(event_id);
            if (event_id == 0 || impl == nullptr) {
                // Selected but unimplemented (every native event until its
                // content-task body lands), or every pool empty (id 0): park
                // AFTER the exact selection bookkeeping, with the EventId
                // retained in rc.event for observability.
                rc.phase = static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED);
                rc.room_type = static_cast<uint8_t>(RoomType::Event);
            } else {
                rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
                rc.room_type = static_cast<uint8_t>(RoomType::Event);
                impl->on_enter(rc, rc.event);
            }
            break;
        }
        case RoomType::Shop:
            // ShopRoom.onPlayerEntry (ShopRoom.java:43-50) -> new Merchant()
            // -> ShopScreen.init: the whole stock and price build, in one
            // uninterrupted sequence, before the player can act.
            rc.combat = CombatState{};
            reseed_floor_streams(rc.combat, seed, floor);
            rc.rewards = RewardScreen{};
            rc.rewards.open_card_item = kNoOpenCardReward;
            rc.rest = RestSiteState{};
            rc.shop = generate_shop(rc.run);
            rc.phase = static_cast<uint8_t>(RunPhase::SHOP);
            break;
        case RoomType::None:
        default:
            stall(room);  // no room content for this kind yet
            break;
    }
}

// The one entry point every real room transition uses -- the OUTER entry, which
// is the one nextRoomTransition fires onEnterRoom for. Only the ? resolution
// re-enters, and it calls the _impl form with the hook suppressed.
void on_player_entry(RunController& rc, RoomType room, RoomType left_room) noexcept {
    on_player_entry_impl(rc, room, left_room, /*fire_on_enter_room=*/true);
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

bool enter_event_combat(RunController& rc, std::string_view encounter_key,
                        EventCombatVariant variant, bool elite_trigger) noexcept {
    // AbstractEvent.enterCombat keeps the EventRoom object alive. Preserve the
    // constructor/buttonEffect draws already consumed from the five floor
    // streams, and retain RoomType::Event so leaving never pops monsterList.
    return enter_combat(rc, encounter_key, RoomType::Event,
                        /*preserve_floor_streams=*/true, variant, elite_trigger);
}

// --- next_room_transition ----------------------------------------------------

void next_room_transition(RunController& rc, uint8_t dst_x, bool to_boss) noexcept {
    RunState& rs = rc.run;

    // (1) Remove the LEFT room's encounter from its list (AbstractDungeon.java:
    //     1694-1707): leaving a MonsterRoom/Elite advances that list's cursor.
    //     (Modelled as a cursor bump == remove(0).) Neow / non-combat rooms:
    //     none. The kind tested is the RESOLVED room (`getCurrRoom()
    //     instanceof MonsterRoom` on the live room object), NOT the static map
    //     node: generateRoom (:1823-1840) turns a MONSTER-rolling ? into a
    //     real MonsterRoom, which consumes monsterList on exit while rs.map
    //     still says Event. rc.room_type carries the resolved kind (set by
    //     every on_player_entry branch), so it is the correct source; reading
    //     rs.map here was a real bug once ? rooms resolve.
    const RoomType left_room = static_cast<RoomType>(rc.room_type);
    if (rs.floor >= 1 && rc.cur_x != kNeowColumn) {
        if (left_room == RoomType::Monster) {
            ++rc.monster_cursor;
        } else if (left_room == RoomType::Elite) {
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

    // (5) onPlayerEntry (AbstractDungeon.java:1800), with the ?-roll block
    //     (:1763-1779) folded into the Event branch. `left_room` feeds the
    //     roll's leaving-a-shop gate (:128-130 of EventHelper.java).
    on_player_entry(rc, room, left_room);
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

    // THE ACT-MUSIC DRAW -- the first and only run-start miscRng consumer.
    // Exordium.<init> ends with CardCrawlGame.music.changeBGM(id)
    // (Exordium.java:58) -> new MainMusic("Exordium") -> getSong, whose
    // Exordium arm draws AbstractDungeon.miscRng.random(1) to pick between the
    // act's two tracks (MainMusic.java:56-66). That is a real draw off the
    // FLOOR-0 misc stream (miscRng = new Random(Settings.seed),
    // AbstractDungeon.java:411; the per-floor reseed at nextRoomTransition
    // :1751 then discards the offset, which is why floors >= 1 never see it).
    // It was invisible until the boss-swap onEquip bodies landed, because no
    // other floor-0 miscRng consumer existed; STS00052's Astrolabe transforms
    // are what exposed it -- the game's three identities matched draws 2..4 of
    // the sim's stream, one behind on every draw, with every counter equal.
    // (The value is deliberately discarded: which TRACK plays is not run
    // state.) The save-reload path re-draws it off the reloaded floor stream
    // (Exordium.java:83 after the :81 reseed) -- a mid-run-resume concern for
    // that row's owner, not modelled here.
    (void)random(rc.combat.misc_rng, 1);

    // dungeonTransitionSetup: EventHelper.resetProbabilities (act-scoped pity
    // floats) + blizzardPotionMod = 0; cardBlizzRandomizer starts at +5.
    // (ELITE_CHANCE's reset has no field here -- event_framework.hpp.)
    rs.event_pity_monster = 0.1f;
    rs.event_pity_shop = 0.03f;
    rs.event_pity_treasure = 0.02f;
    rs.card_blizz_randomizer = 5;
    rs.blizzard_potion_mod = 0;

    // ShopScreen.resetPurgeCost (ShopScreen.java:241-244), called from the
    // dungeon reset that precedes a new run (CardCrawlGame.java:478). The field
    // is a STATIC in the game, so nothing else zeroes it between runs -- which
    // is exactly why the reset exists, and why run_begin has to spell it: a
    // value-initialized RunState would open its first merchant offering card
    // removal for nothing.
    rs.purge_cost = static_cast<int16_t>(kPurgeCostBase);

    // The three event-pool membership bitsets: initializeEventList /
    // initializeShrineList / initializeSpecialOneTimeEventList (the last
    // NFY-conditional), run by dungeonTransitionSetup. Draw-free.
    init_event_pools(rs);

    // (1) monsterRng: generateMonsters + initializeBoss -> the encounter lists
    //     (Exordium.java:110-221; generate_monster_lists).
    generate_monster_lists(/*act=*/1, rs.monster_rng, rc.lists);
    // The act boss, mirrored into save-parity state: setBoss(bossList.get(0))
    // (Exordium.initializeBoss) is what the game persists as the act's boss,
    // and the translator writes the capture's `act_boss` into
    // `boss_ids[act-1]` as an ENCOUNTER id (the space boss_list[] holds and
    // enter_combat takes) -- so the mirror uses the same registry join and the
    // differ compares the field for real.
    if (rc.lists.boss_list_count > 0) {
        const sts::registry::EncounterDef* boss =
            sts::registry::encounter_by_game_id(rc.lists.boss_list[0]);
        if (boss != nullptr) {
            rs.boss_ids[0] = static_cast<uint16_t>(boss->id);
        }
    }

    // (2) relicRng: initializeRelicList population + five unconditional
    //     randomLong-seeded JDK shuffles (AbstractDungeon.java:1221-1241).
    initialize_relic_pools(rs);

    // (3) mapRng: the act map (Exordium.java:56-57). generate_map seeds mapRng =
    //     Random(seed + actNum) internally; encode both edges and room types plus
    //     the end-of-generateMap mapRng state into RunState.map.
    GeneratedMap g = generate_map(seed, /*act_num=*/1);
    const RoomAssignment ra = assign_room_types(g, static_cast<int>(ascension));
    encode_paths_into_run_state(g, rs);   // edges (+ post-path mapRng)
    encode_rooms_into_run_state(ra, rs);  // room types (+ end-of-generateMap mapRng)
    // The burning-elite node the setEmeraldElite draw chose (map_rooms.hpp step
    // 5): the emerald-key ENTRY roll fires on exactly this node (enter_combat).
    rc.emerald_x = ra.emerald_x < 0 ? kNoEmeraldNode
                                    : static_cast<uint8_t>(ra.emerald_x);
    rc.emerald_y = ra.emerald_y < 0 ? kNoEmeraldNode
                                    : static_cast<uint8_t>(ra.emerald_y);

    // The character sheet + the run-setup ascension modifiers, in the game's
    //     own application order (derived in full in run_advance.hpp, above
    //     run_setup_max_hp). Summarised: the potion-slot loss comes from the
    //     AbstractPlayer constructor, so it is first; then dungeonTransitionSetup
    //     runs the (no-op at full HP) between-act heal, the max-HP loss, and the
    //     90 %-of-the-REDUCED-max current HP; then the curse; and only then does
    //     the dungeon constructor call initializeStarterDeck.
    const int asc = static_cast<int>(ascension);

    // Ironclad loadout (Ironclad.getLoadout, Ironclad.java:113-115).
    rs.gold = kIroncladBaseGold;
    rs.potion_slots = static_cast<uint8_t>(potion_slot_count(asc));

    // The between-act heal (AbstractDungeon.java:2582-2586) is a no-op here on
    // BOTH branches: the sheet arrives at full HP, so missing HP is 0 and the
    // else-branch heal(maxHealth) is equally saturated. It is named rather than
    // written because its only observable contribution at run start is its
    // POSITION -- ahead of the max-HP loss below.
    rs.max_hp = static_cast<int16_t>(run_setup_max_hp(asc));
    rs.hp = static_cast<int16_t>(run_setup_hp(asc));

    // The starting curse (AbstractDungeon.java:2597-2600) goes in BEFORE the
    // starting deck, because AbstractDungeon.<init> calls dungeonTransitionSetup
    // (:287) and only then initializeStarterDeck (:295-296) -- the master deck is
    // empty at this point, so Ascender's Bane ends up at index 0.
    //
    // It walks through add_card_to_master_deck (run_deck.hpp), the sanctioned
    // master-deck door, rather than the bulk write below. Two reasons. The door's
    // insert position is an append at master_deck_count, which is exactly what
    // CardGroup.addToTop does (CardGroup.java:455-457). And the obtain-time relic
    // pass the door runs is provably empty at this point: relic_count is still 0
    // here (the starting relic is acquired further down), and even in the game --
    // where initializeStarterRelics has already run in the AbstractPlayer
    // constructor (AbstractPlayer.java:209) -- the call site is a raw CardGroup
    // insert that never reaches obtainCard, and Burning Blood carries no
    // on_obtain_card binding either way. A named test pins that the pass changes
    // nothing so the equivalence cannot rot silently.
    if (run_setup_has_starting_curse(asc)) {
        (void)add_card_to_master_deck(rs, CardId::ASCENDERS_BANE);
    }

    // Starting deck (5 Strike / 4 Defend / 1 Bash) appended after it.
    constexpr int kDeckN = static_cast<int>(sizeof(kIroncladStartDeck) /
                                            sizeof(kIroncladStartDeck[0]));
    for (int i = 0; i < kDeckN; ++i) {
        CardInstance& c = rs.master_deck[rs.master_deck_count];
        c = CardInstance{};
        c.card_id = static_cast<uint16_t>(kIroncladStartDeck[i]);
        ++rs.master_deck_count;
    }

    // Starting relic: Burning Blood (Ironclad.getStartingRelics, Ironclad.java:86),
    // acquisition index 0 (== trigger order, trap 8). Its onVictory heal fires
    // through the combat mirror at the battle-over fold-back.
    (void)acquire_relic(rs, rc.combat.misc_rng, RelicId::BURNING_BLOOD);

    rc.phase = static_cast<uint8_t>(RunPhase::NEOW);
    rc.cur_x = kNeowColumn;
    rc.room_type = static_cast<uint8_t>(RoomType::None);
    rc.rewards.open_card_item = kNoOpenCardReward;  // value-init 0 would mean
                                                    // "item 0's pick screen is
                                                    // open"; keep it closed.

    // The Neow blessing (NeowEvent.blessing, NeowEvent.java:361-378). It is
    // rolled here rather than on the first button press because the two are
    // provably the same roll: neow_rng is a fresh Random(seed) that nothing
    // else in the game reads or writes (trap 17), and NeowEvent's intro
    // screens consume only MathUtils flavour draws. RunPhase::NEOW therefore
    // means "standing at the blessing screen", which is also the state an
    // oracle capture must be taken in. It runs LAST in run_begin because
    // hp_bonus reads the post-ascension max HP.
    neow_roll_blessing(rs, rc.neow);
    return rc;
}

// --- legal_actions (run overload) -------------------------------------------

void legal_actions(const RunController& rc, RunActionMask& out) noexcept {
    out = RunActionMask{};
    out.phase = rc.phase;

    // The pending-bottle overlay (run_advance.hpp) is MODAL over the phase
    // underneath: while a just-claimed bottle's mandatory 1-pick grid is up,
    // only the eligible master-deck rows are legal -- no proceed, no claims,
    // no purchases (the game's RoomPhase.INCOMPLETE + cancel-less grid,
    // BottledFlame.java:41-53). The potion belt below stays live, matching
    // the Smith/Toke and shop-purge grid masks.
    const auto pending_bottle =
        static_cast<MasterBottleKind>(rc.pending_bottle);
    if (pending_bottle != MasterBottleKind::NONE) {
        for (uint16_t i = 0;
             i < rc.run.master_deck_count && i < kMasterDeckCap; ++i) {
            out.can_choose_master_deck[i] =
                bottle_pick_legal(rc.run, pending_bottle, i);
        }
    } else switch (static_cast<RunPhase>(rc.phase)) {
        case RunPhase::NEOW: {
            // Which of NeowEvent's screens is up. The dialog itself has no
            // Proceed button while options are on it (screenNum 3 clears the
            // remaining options and only re-adds one after activate(),
            // NeowEvent.java:193-222), so BLESSING offers the four choices and
            // nothing else.
            const NeowState& n = rc.neow;
            switch (static_cast<NeowScreen>(n.screen)) {
                case NeowScreen::BLESSING:
                    for (int i = 0; i < kNeowOptionCount; ++i) {
                        out.can_choose_neow_option[i] = true;
                    }
                    break;
                case NeowScreen::CARD_REWARD: {
                    const RewardScreen& s = rc.rewards;
                    if (!reward_card_item_open_legal(s)) {
                        break;
                    }
                    const RunRewardItem& item = s.items[s.open_card_item];
                    for (uint8_t j = 0; j < item.card_count && j < kRewardCardCap;
                         ++j) {
                        out.can_take_card[j] =
                            reward_take_card_legal(rc.run, s, j);
                    }
                    // skippable == true on a null-RewardItem open
                    // (CardRewardScreen.java:450-457); the bowl button is shown
                    // on the same condition as everywhere else, which no
                    // floor-0 run can satisfy (Singing Bowl is a pool relic).
                    out.can_skip_card = true;
                    out.can_sing = run_has_relic(rc.run, RelicId::SINGING_BOWL);
                    break;
                }
                case NeowScreen::GRID:
                    if (static_cast<NeowGridMode>(n.grid_mode) ==
                            NeowGridMode::CONFIRM_PANDORA ||
                        static_cast<NeowGridMode>(n.grid_mode) ==
                            NeowGridMode::CONFIRM_CALLING_BELL) {
                        out.can_proceed = true;
                    } else {
                        for (uint16_t i = 0;
                             i < rc.run.master_deck_count &&
                             i < kMasterDeckCap; ++i) {
                            out.can_choose_master_deck[i] =
                                neow_grid_card_legal(rc.run, n, i);
                        }
                    }
                    break;
                case NeowScreen::ITEM_REWARD: {
                    const RewardScreen& s = rc.rewards;
                    if (s.open_card_item != kNoOpenCardReward) {
                        if (!reward_card_item_open_legal(s)) {
                            break;
                        }
                        const RunRewardItem& item = s.items[s.open_card_item];
                        for (uint8_t j = 0;
                             j < item.card_count && j < kRewardCardCap; ++j) {
                            out.can_take_card[j] =
                                reward_take_card_legal(rc.run, s, j);
                        }
                        out.can_skip_card = true;
                        out.can_sing =
                            run_has_relic(rc.run, RelicId::SINGING_BOWL);
                    } else {
                        out.can_proceed = true;
                        for (uint8_t i = 0;
                             i < s.count && i < kRewardItemCap; ++i) {
                            out.can_claim_reward[i] =
                                reward_claim_legal(rc.run, s, i);
                        }
                    }
                    break;
                }
                case NeowScreen::DONE:
                    out.can_proceed = true;  // screenNum 99 -> openMap()
                    break;
            }
            break;
        }

        case RunPhase::TREASURE_ROOM:
            out.can_open_chest =
                treasure_chest_open_legal(rc.run, rc.treasure_chest);
            out.can_proceed = true;  // TreasureRoom is COMPLETE on entry.
            break;

        case RunPhase::COMBAT_REWARD: {
            const RewardScreen& s = rc.rewards;
            if (s.open_card_item != kNoOpenCardReward) {
                // The CARD pick screen is up: pick / skip / (bowl) sing. The
                // Proceed button is hidden while it is open
                // (CardRewardScreen.open hides proceedButton,
                // CardRewardScreen.java:459-460).
                if (!reward_card_item_open_legal(s)) {
                    break;
                }
                const RunRewardItem& item = s.items[s.open_card_item];
                for (uint8_t j = 0; j < item.card_count && j < kRewardCardCap;
                     ++j) {
                    out.can_take_card[j] =
                        reward_take_card_legal(rc.run, s, j);
                }
                out.can_skip_card = true;
                out.can_sing = run_has_relic(rc.run, RelicId::SINGING_BOWL);
            } else {
                out.can_proceed = true;
                for (uint8_t i = 0; i < s.count && i < kRewardItemCap; ++i) {
                    out.can_claim_reward[i] =
                        reward_claim_legal(rc.run, s, i);
                }
            }
            break;
        }

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
                // MapRoomNode.wingedIsConnectedTo returns true for any
                // hasEdges() node on the next row while Wing Boots has a live
                // charge. The ordinary edge-derived choices above stay true;
                // the transition path distinguishes them so they remain free.
                if (wing_boots_active(rc.run) && y + 1 < kMapRows) {
                    for (int x = 0; x < kMapCols; ++x) {
                        if (rc.run.map[run_state_map_index(x, y + 1)].edges !=
                            0u) {
                            out.can_choose_node[x] = true;
                        }
                    }
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

        case RunPhase::REST_SITE: {
            const RestScreen screen = static_cast<RestScreen>(rc.rest.screen);
            if (screen == RestScreen::MENU) {
                const RestMenu menu =
                    build_rest_menu(rc.run);
                for (uint8_t i = 0; i < menu.count; ++i) {
                    out.can_choose_rest[i] = menu.entries[i].usable;
                }
            } else if (screen == RestScreen::SMITH) {
                out.can_cancel_grid = true;
                // getUpgradableCards (CampfireSmithEffect.java:62): NO bottled
                // exclusion -- a bottled card stays smithable.
                for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
                    out.can_choose_master_deck[i] =
                        rest_card_upgradeable(rc.run.master_deck[i]);
                }
            } else if (screen == RestScreen::TOKE) {
                out.can_cancel_grid = true;
                // CampfireTokeEffect.java:57: the Toke grid is
                // getGroupWithoutBottledCards(getPurgeableCards()).
                for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
                    out.can_choose_master_deck[i] =
                        master_card_purgeable_unbottled(
                            rc.run.master_deck[i]);
                }
            } else if (screen == RestScreen::DREAM_CATCHER) {
                const RewardScreen& s = rc.rewards;
                if (reward_card_item_open_legal(s)) {
                    const RunRewardItem& item = s.items[s.open_card_item];
                    for (uint8_t j = 0;
                         j < item.card_count && j < kRewardCardCap; ++j) {
                        out.can_take_card[j] =
                            reward_take_card_legal(rc.run, s, j);
                    }
                    out.can_skip_card = true;
                    out.can_sing =
                        run_has_relic(rc.run, RelicId::SINGING_BOWL);
                }
            }
            break;
        }

        case RunPhase::EVENT_DIALOG: {
            // Options are rebuilt from the event body's build_menu on every
            // call, never cached (the rest-site pattern). A dangling state
            // (no body for the recorded id) exposes nothing -- but cannot
            // arise through on_player_entry, which only opens this phase for
            // a non-null impl.
            const EventDialogImpl* impl = event_dialog_impl(rc.event.event_id);
            if (impl != nullptr) {
                if (rc.event.grid_kind !=
                    static_cast<uint8_t>(EventGridKind::NONE)) {
                    for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
                        out.can_choose_master_deck[i] =
                            event_grid_card_legal(rc.run, rc.event, i);
                    }
                } else {
                    EventDialogMenu menu{};
                    impl->build_menu(rc, rc.event, menu);
                    for (uint8_t i = 0;
                         i < menu.count && i < kEventOptionCap; ++i) {
                        out.can_choose_event_option[i] = menu.enabled[i];
                    }
                }
            }
            break;
        }

        case RunPhase::SHOP: {
            const ShopState& shop = rc.shop;
            if (static_cast<ShopScreenKind>(shop.screen) ==
                ShopScreenKind::PURGE_GRID) {
                out.can_cancel_grid = true;
                // gridSelectScreen is modal over the shop: only the grid's own
                // rows are legal while it is up (the Smith/Toke grid shape).
                for (uint16_t i = 0;
                     i < rc.run.master_deck_count && i < kMasterDeckCap; ++i) {
                    out.can_choose_master_deck[i] =
                        shop_purge_card_legal(rc.run, shop, i);
                }
                break;
            }
            for (int i = 0; i < kShopColoredCount; ++i) {
                out.can_buy_shop_item[kChooseShopColoredBase + i] =
                    shop_buy_card_legal(rc.run, shop, static_cast<uint8_t>(i),
                                        /*colorless=*/false);
            }
            for (int i = 0; i < kShopColorlessCount; ++i) {
                out.can_buy_shop_item[kChooseShopColorlessBase + i] =
                    shop_buy_card_legal(rc.run, shop, static_cast<uint8_t>(i),
                                        /*colorless=*/true);
            }
            for (int i = 0; i < kShopRelicCount; ++i) {
                out.can_buy_shop_item[kChooseShopRelicBase + i] =
                    shop_buy_relic_legal(rc.run, shop, static_cast<uint8_t>(i));
            }
            for (int i = 0; i < kShopPotionCount; ++i) {
                out.can_buy_shop_item[kChooseShopPotionBase + i] =
                    shop_buy_potion_legal(rc.run, shop, static_cast<uint8_t>(i));
            }
            out.can_purge = shop_purge_legal(rc.run, shop);
            // ShopRoom's phase is COMPLETE from the moment it is entered
            // (ShopRoom.java:30), so the proceed button is live throughout.
            out.can_proceed = true;
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

    // The discard button, by contrast, is NOT restricted to the non-combat
    // phases above: TopPanel draws the belt through a fight too, and
    // canDiscard has no combat clause. The parked/terminal phases are still
    // excluded, on this layer's standing rule that nothing is legal there.
    if (phase != RunPhase::ROOM_UNIMPLEMENTED && phase != RunPhase::RUN_OVER &&
        phase != RunPhase::NONE) {
        for (uint8_t slot = 0; slot < rc.run.potion_slots && slot < kPotionCap;
             ++slot) {
            out.can_discard_potion[slot] = potion_discard_legal(rc, slot);
        }
    }
}

// --- advance (run overload) -------------------------------------------------

namespace {

void finish_rest_site(RunController& rc) noexcept {
    rc.rest = RestSiteState{};
    rc.rewards = RewardScreen{};
    rc.rewards.open_card_item = kNoOpenCardReward;
    rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
}

void finish_sleep(RunController& rc) noexcept {
    if (run_has_relic(rc.run, RelicId::DREAM_CATCHER) &&
        open_rest_card_reward(rc.run, rc.rewards)) {
        rc.rest.screen = static_cast<uint8_t>(RestScreen::DREAM_CATCHER);
        return;
    }
    finish_rest_site(rc);
}

void open_dig_reward(RunController& rc) noexcept {
    rc.rewards = RewardScreen{};
    rc.rewards.open_card_item = kNoOpenCardReward;
    rc.rewards.count = 1;
    RunRewardItem& item = rc.rewards.items[0];
    item.kind = static_cast<uint8_t>(RewardItemKind::RELIC);
    item.id = static_cast<uint16_t>(rest_dig_relic(rc.run));
    rc.rest = RestSiteState{};
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT_REWARD);
}

void finish_combat_after_action(RunController& rc, StepResult& res) noexcept {
    if (rc.combat.phase != static_cast<uint8_t>(CombatPhase::COMBAT_OVER)) {
        return;
    }
    if (rc.combat.player_hp <= 0) {
        fold_back_combat(rc);
        // The mugging already cost the gold at steal time in the game's terms,
        // so a dead player's final purse is short too; no return is reachable
        // (no reward screen opens past a defeat).
        (void)settle_stolen_gold(rc);
        rc.combat_outcome = static_cast<uint8_t>(RunCombatOutcome::DEFEAT);
        rc.phase = static_cast<uint8_t>(RunPhase::RUN_OVER);
        res = StepResult{};
        res.terminal = true;
        res.reward = -1.0f;
        return;
    }
    // The survivor's screen pick, in the game's precedence (AbstractRoom.
    // update:334-341): mugged first, then smoked, then the ordinary open. A
    // pump-ended combat is smoked only through an imported/hand-built state
    // (step_potion routes the run-layer Smoke Bomb itself), but the same
    // precedence is honoured wherever the flags come from.
    RunCombatOutcome outcome = RunCombatOutcome::KILLED;
    if ((rc.combat.flags & kCombatFlagMugged) != 0u) {
        outcome = RunCombatOutcome::MUGGED;
    } else if ((rc.combat.flags & kCombatFlagPlayerEscaped) != 0u) {
        outcome = RunCombatOutcome::SMOKE_BOMB;
    }
    enter_combat_reward(rc, outcome, res);
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

    if (id == PotionId::BLOOD_POTION &&
        rc.phase != static_cast<uint8_t>(RunPhase::COMBAT)) {
        // BloodPotion.use's non-combat arm calls player.heal synchronously
        // (BloodPotion.java:39-48), so it mutates the persistent RunState
        // rather than the stale between-fights CombatState. Preserve Java's
        // float/truncation expression exactly; Magic Flower is phase-gated off
        // outside combat, and heal_out_of_combat owns the clamp + Red Skull
        // not-bloodied cross.
        const int potency =
            potion_def(PotionId::BLOOD_POTION)->potency *
            (run_has_relic(rc.run, RelicId::SACRED_BARK) ? 2 : 1);
        const float ratio = static_cast<float>(potency) / 100.0f;
        const int heal =
            static_cast<int>(static_cast<float>(rc.run.max_hp) * ratio);
        heal_out_of_combat(rc.run, heal);
        dispatch_run_relics_on_use_potion(rc);
        clear_potion_slot(rc.run, slot);
    } else if (id == PotionId::FRUIT_JUICE) {
        use_fruit_juice(rc, slot);
        dispatch_run_relics_on_use_potion(rc);
        // Fruit Juice / Entropic Brew are usable IN combat too, where the
        // dispatch queues Toy Ornithopter's HealAction rather than writing hp
        // -- drain it now, exactly as the game's action queue drains once the
        // pop-up closes. Out of combat the queue is untouched and this no-ops.
        if (rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
            pump(rc.combat, dispatch_monster_turn);
        }
    } else if (id == PotionId::ENTROPIC_BREW) {
        use_entropic_brew(rc, slot);
        dispatch_run_relics_on_use_potion(rc);
        if (rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
            pump(rc.combat, dispatch_monster_turn);
        }
    } else if (id == PotionId::SMOKE_BOMB) {
        // SmokeBomb.use marks the room smoked; the player's escape timer calls
        // endBattle, which runs onVictory and opens the battle-over screen. If
        // a thief already escaped this combat the room is ALSO mugged, and
        // mugged wins the screen pick (AbstractRoom.update:334-341 checks it
        // first) -- the mug screen still assembles items, so smoking past the
        // survivors of a mugged combat forfeits the fight but not the rewards.
        // Preserve PotionPopUp.updateInput's order (PotionPopUp.java:234-239):
        // potion.use first, then every relic.onUsePotion, then destroy the
        // slot. The native body latches kCombatFlagPlayerEscaped; the run layer
        // below owns the immediate timer-collapse / reward-screen consequence.
        if (!use_potion(rc.combat, id, target)) {
            fill_combat_result(rc.combat, res);
            return true;
        }
        dispatch_run_relics_on_use_potion(rc);
        // The dispatch queues Toy Ornithopter's HealAction; the game's queue
        // keeps draining while the escape animation plays (the room phase is
        // still COMBAT until endBattle), so the heal lands BEFORE the
        // fold-back reads player_hp. Drain it here for the same reason --
        // collapsing to COMBAT_OVER with the heal still queued would lose it.
        pump(rc.combat, dispatch_monster_turn);
        clear_potion_slot(rc.run, slot);
        rc.combat.phase = static_cast<uint8_t>(CombatPhase::COMBAT_OVER);
        fill_combat_result(rc.combat, res);
        res.reward = 0.0f;  // escape is not a kill.
        const RunCombatOutcome outcome =
            (rc.combat.flags & kCombatFlagMugged) != 0u
                ? RunCombatOutcome::MUGGED
                : RunCombatOutcome::SMOKE_BOMB;
        enter_combat_reward(rc, outcome, res);
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

// DISCARD_POTION, dispatched beside step_potion and ahead of the phase switch
// for the same reason: the potion belt is a RunState-owned inventory that the
// top panel exposes on every screen, so its verbs belong to no single phase.
// The whole effect is TopPanel.destroyPotion (TopPanel.java:529-531) -- the
// slot is emptied and nothing else happens. In particular NO relic hook fires:
// CommandExecutor's `onUsePotion` fan-out and PotionPopUp's are both on the
// USE path, so Toy Ornithopter does not heal for a potion thrown away.
bool step_discard_potion(RunController& rc, Action a, StepResult& res) noexcept {
    if (action_verb(a) != ActionVerb::DISCARD_POTION) {
        return false;
    }
    RunActionMask mask{};
    legal_actions(rc, mask);
    const uint8_t slot = action_arg0(a);
    if (slot < kPotionCap && mask.can_discard_potion[slot]) {
        const bool was_fairy = static_cast<PotionId>(rc.run.potions[slot]) ==
                               PotionId::FAIRY_POTION;
        clear_potion_slot(rc.run, slot);
        // Keep the combat's armed-Fairy mirror in step with the belt. A Fairy
        // is discardable IN COMBAT (canDiscard has no combat gate,
        // AbstractPotion.java:398-400), and it is the only mid-combat belt
        // mutation there is -- a Fairy can never be USED (canUse is false) and
        // Entropic Brew, the only other slot writer, is out-of-combat-only. If
        // the mirror were left alone, a thrown-away Fairy would still revive.
        // DECREMENT, do not recompute from the belt: the mirror is
        // (held - already consumed), and a discard lowers `held` by one without
        // changing what was consumed. Recomputing would resurrect a fairy that
        // had already fired this combat. Floored at 0 for the case where the
        // discarded slot is one the revive had logically already spent (the
        // slots are only cleared at fold-back).
        if (was_fairy && rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
            const uint8_t armed = combat_fairy_armed(rc.combat.flags);
            rc.combat.flags = with_combat_fairy_armed(
                rc.combat.flags,
                armed > 0 ? static_cast<uint8_t>(armed - 1) : 0u);
        }
    }
    // An illegal discard is a non-corrupting no-op, the same contract every
    // other run-layer verb keeps.
    if (rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
        fill_combat_result(rc.combat, res);
    } else {
        fill_run_result(rc, res);
    }
    return true;
}

// Translate an on_equip_screen body's request made at a CLAIM/purchase site
// onto the controller. Only GRID_BOTTLE can arise on these paths in S1: the
// five boss on_equip_screen relics are BOSS-tier, so no claim screen or shop
// slot ever holds one (Neow's boss swap owns them, spawn_relic_and_obtain in
// neow.cpp, which translates onto NeowState's own sub-screens instead).
void apply_claim_equip_request(RunController& rc,
                               const RelicEquipContext& ctx) noexcept {
    switch (ctx.screen) {
        case RelicEquipScreen::NONE:
            break;  // synchronous body, or no on_equip_screen body at all
        case RelicEquipScreen::GRID_BOTTLE:
            // The modal pending-bottle overlay (run_advance.hpp): the game's
            // gridSelectScreen over the reward/shop screen, room INCOMPLETE
            // (BottledFlame.java:49-51).
            rc.pending_bottle = static_cast<uint8_t>(ctx.bottle);
            break;
        case RelicEquipScreen::GRID_REMOVE:
        case RelicEquipScreen::GRID_TRANSFORM_UPGRADE:
        case RelicEquipScreen::ITEM_REWARD:
        case RelicEquipScreen::GRID_CONFIRM_PANDORA:
        case RelicEquipScreen::GRID_CONFIRM_CALLING_BELL:
        default:
            assert(false &&
                   "a BOSS on_equip_screen request reached a claim site");
            break;
    }
}

// The pending-bottle overlay's pick (run_advance.hpp). While the overlay is
// up it is MODAL over the phase underneath -- the game parks the room at
// RoomPhase.INCOMPLETE with no cancel and no confirm on the 1-pick grid
// (BottledFlame.java:41-53) -- so this consumes EVERY action while active:
// a legal CHOOSE applies the bottle bit and closes the overlay; anything
// else is the non-corrupting no-op every run verb promises. Dispatched after
// the potion steps (the belt stays on top of every screen, matching the
// Smith/Toke/purge grids, whose masks also keep the belt live).
bool step_bottle_pick(RunController& rc, Action a, StepResult& res) noexcept {
    const auto kind = static_cast<MasterBottleKind>(rc.pending_bottle);
    if (kind == MasterBottleKind::NONE) {
        return false;
    }
    if (action_verb(a) == ActionVerb::CHOOSE) {
        const uint8_t a0 = action_arg0(a);
        if (bottle_pick_legal(rc.run, kind, a0)) {
            // BottledFlame.update (:63-77): the chosen INSTANCE carries the
            // flag. The relic's own counter stays untouched (AbstractRelic's
            // -1 -- the game never writes it, and neither do we).
            rc.run.master_deck[a0].flags = static_cast<uint16_t>(
                rc.run.master_deck[a0].flags | master_bottle_bit(kind));
            rc.pending_bottle =
                static_cast<uint8_t>(MasterBottleKind::NONE);
        }
    }
    fill_run_result(rc, res);
    return true;
}

void step_one(RunController& rc, Action a, StepResult& res) noexcept {
    // Attach THIS controller's knowledge for everything the step touches
    // (combat pumps, choice resolutions, room-entry combat construction), so
    // the engine's event hooks record into rc.knowledge -- knowledge.hpp's
    // attachment contract. Restored on every return path below.
    KnowledgeScope kscope(&rc.knowledge);
    if (step_potion(rc, a, res)) {
        return;
    }
    if (step_discard_potion(rc, a, res)) {
        return;
    }
    if (step_bottle_pick(rc, a, res)) {
        return;
    }
    switch (static_cast<RunPhase>(rc.phase)) {
        case RunPhase::NEOW: {
            // NeowEvent's dialog and the screens its payouts open. Illegal
            // choices are non-corrupting no-ops, the same contract as
            // MAP_CHOICE and COMBAT_REWARD below.
            if (action_verb(a) == ActionVerb::CHOOSE) {
                NeowState& n = rc.neow;
                const uint8_t a0 = action_arg0(a);
                switch (static_cast<NeowScreen>(n.screen)) {
                    case NeowScreen::BLESSING:
                        if (a0 < kNeowOptionCount) {
                            // card_random_rng rides along for the boss swap's
                            // on_equip_screen bodies (Pandora's Box draws it).
                            neow_activate(rc.run, n, rc.rewards,
                                          rc.combat.misc_rng,
                                          rc.combat.card_random_rng, a0);
                        }
                        break;
                    case NeowScreen::CARD_REWARD:
                        if (a0 == kChooseSkipCard) {
                            neow_skip_card(n, rc.rewards);
                        } else if (a0 == kChooseSing) {
                            if (reward_sing(rc.run, rc.rewards)) {
                                neow_finish_payout(n);
                            }
                        } else if (reward_take_card(rc.run, rc.rewards, a0)) {
                            neow_finish_payout(n);
                        }
                        break;
                    case NeowScreen::GRID:
                        if (a0 == kChooseProceed &&
                            static_cast<NeowGridMode>(n.grid_mode) ==
                                NeowGridMode::CONFIRM_PANDORA) {
                            relic_confirm_pandoras_box(rc.run, rc.rewards);
                            n.grid_mode =
                                static_cast<uint8_t>(NeowGridMode::NONE);
                            neow_finish_payout(n);
                        } else if (
                            a0 == kChooseProceed &&
                            static_cast<NeowGridMode>(n.grid_mode) ==
                                NeowGridMode::CONFIRM_CALLING_BELL) {
                            relic_confirm_calling_bell(
                                rc.run, rc.rewards, kNeowRewardScreenRoom);
                            n.grid_mode =
                                static_cast<uint8_t>(NeowGridMode::NONE);
                            n.screen =
                                static_cast<uint8_t>(NeowScreen::ITEM_REWARD);
                        } else {
                            // misc_rng feeds only TRANSFORM_UPGRADE (Astrolabe);
                            // Neow's own pick grids draw rs.neow_rng.
                            (void)neow_grid_pick(rc.run, n,
                                                 rc.combat.misc_rng, a0);
                        }
                        break;
                    case NeowScreen::ITEM_REWARD:
                        if (rc.rewards.open_card_item !=
                            kNoOpenCardReward) {
                            if (reward_card_item_open_legal(rc.rewards)) {
                                if (a0 == kChooseSkipCard) {
                                    reward_skip_card(rc.rewards);
                                } else if (a0 == kChooseSing) {
                                    (void)reward_sing(rc.run, rc.rewards);
                                } else {
                                    (void)reward_take_card(
                                        rc.run, rc.rewards, a0);
                                }
                            }
                        } else if (a0 == kChooseProceed) {
                            // The reward screen's Proceed returns to Neow's
                            // final dialog button, which is what opens the map.
                            rc.rewards = RewardScreen{};
                            rc.rewards.open_card_item = kNoOpenCardReward;
                            neow_finish_payout(n);
                        } else {
                            // Neow's reward rows are screenless draws
                            // (return_random_screenless_relic excludes the
                            // Bottled trio), so no GRID_BOTTLE can arise here
                            // today -- but the claim goes through the same
                            // equip-context door as every other claim site so
                            // the refusal path can never be the silent one.
                            RelicEquipContext ectx{rc.combat.card_random_rng,
                                                   rc.rewards,
                                                   kNeowRewardScreenRoom};
                            (void)claim_reward(rc.run, rc.combat.misc_rng,
                                               rc.rewards, a0, ectx);
                            apply_claim_equip_request(rc, ectx);
                        }
                        break;
                    case NeowScreen::DONE:
                        rc.rewards = RewardScreen{};
                        rc.rewards.open_card_item = kNoOpenCardReward;
                        rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
                        break;
                }
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
                    if (rc.run.floor > 0 &&
                        !map_edge_connects(rc, a0)) {
                        spend_wing_boots_charge(rc.run);
                    }
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
            // The reward claim flow. Illegal choices are non-corrupting no-ops, the
            // same contract as MAP_CHOICE above.
            if (action_verb(a) == ActionVerb::CHOOSE) {
                RewardScreen& s = rc.rewards;
                const uint8_t a0 = action_arg0(a);
                if (s.open_card_item != kNoOpenCardReward) {
                    if (reward_card_item_open_legal(s)) {
                        if (a0 == kChooseSkipCard) {
                            reward_skip_card(s);
                        } else if (a0 == kChooseSing) {
                            (void)reward_sing(rc.run, s);
                        } else {
                            (void)reward_take_card(rc.run, s, a0);
                        }
                    }
                } else if (a0 == kChooseProceed) {
                    // Leave the screen; anything unclaimed is abandoned (an
                    // assembled relic stays popped from its pool, as in the
                    // game). The screen state is cleared so a stale mask can
                    // never claim across rooms.
                    s = RewardScreen{};
                    s.open_card_item = kNoOpenCardReward;
                    rc.treasure_chest = TreasureChest{};
                    if (static_cast<RoomType>(rc.room_type) == RoomType::Boss) {
                        // The Act-1 boss reward's Proceed is the S1 VICTORY
                        // terminal. In the game this press never opens the
                        // map: at a COMBAT_REWARD in a MonsterRoomBoss it
                        // goes to the boss chest (ProceedButton.update,
                        // ProceedButton.java:111-113 -> goToTreasureRoom
                        // :179-187, a TreasureRoomBoss) and from there to the
                        // next act -- both S2 content by the frozen scope
                        // boundary (stage-b-design §1.1: "the run terminates
                        // when the act-1 boss's combat rewards are claimed").
                        // combat_outcome keeps KILLED and room_type keeps
                        // Boss, which is exactly what run_is_victory() reads;
                        // routing to MAP_CHOICE here was the probe-found
                        // no_legal_moves dead end (the boss column has no
                        // outgoing map edges).
                        rc.phase = static_cast<uint8_t>(RunPhase::RUN_OVER);
                        fill_run_result(rc, res);
                        res.reward = 1.0f;  // the run-level win: the +1
                                            // analogue of the DEFEAT path's
                                            // -1 (finish_combat_after_action)
                        break;
                    }
                    rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
                } else {
                    // Claim item a0 (CARDS opens the pick screen). Relic
                    // onEquip bodies draw the floor-scoped miscRng; a Bottled
                    // trio row's on_equip_screen body instead requests its
                    // grid through the equip context, translated onto the
                    // pending-bottle overlay below. This is the ONE claim
                    // surface -- combat rewards, chests (the treasure open
                    // routes here) and event reward screens all dispatch
                    // through it.
                    RelicEquipContext ectx{
                        rc.combat.card_random_rng, rc.rewards,
                        static_cast<RoomType>(rc.room_type)};
                    (void)claim_reward(rc.run, rc.combat.misc_rng, s, a0,
                                       ectx);
                    apply_claim_equip_request(rc, ectx);
                }
            }
            fill_run_result(rc, res);
            break;
        }

        case RunPhase::REST_SITE: {
            if (action_verb(a) == ActionVerb::CHOOSE) {
                const uint8_t a0 = action_arg0(a);
                const RestScreen screen =
                    static_cast<RestScreen>(rc.rest.screen);
                if (screen == RestScreen::MENU) {
                    const RestMenu menu =
                        build_rest_menu(rc.run);
                    if (a0 < menu.count && menu.entries[a0].usable) {
                        const RestOptionEntry& option = menu.entries[a0];
                        switch (static_cast<RestOptionKind>(option.kind)) {
                            case RestOptionKind::REST:
                                (void)rest_apply_heal(rc.run);
                                finish_sleep(rc);
                                break;
                            case RestOptionKind::SMITH:
                                rc.rest.screen =
                                    static_cast<uint8_t>(RestScreen::SMITH);
                                break;
                            case RestOptionKind::LIFT:
                                if (rest_lift(rc.run, option.relic_index)) {
                                    finish_rest_site(rc);
                                }
                                break;
                            case RestOptionKind::TOKE:
                                rc.rest.screen =
                                    static_cast<uint8_t>(RestScreen::TOKE);
                                break;
                            case RestOptionKind::DIG:
                                open_dig_reward(rc);
                                break;
                            case RestOptionKind::RECALL:
                                // CampfireRecallEffect.update
                                // (CampfireRecallEffect.java:39-53): clear the
                                // room's rewards (a rest room has none;
                                // finish_rest_site resets rc.rewards anyway),
                                // obtain the RED key (ObtainKeyEffect ->
                                // Settings.hasRubyKey = true) and complete the
                                // room -- the campfire action is spent, no
                                // rest or smith at this site.
                                rc.run.keys |= kKeyRuby;
                                finish_rest_site(rc);
                                break;
                        }
                    }
                } else if (screen == RestScreen::SMITH) {
                    if (a0 == kChooseCancelGrid) {
                        rc.rest.screen =
                            static_cast<uint8_t>(RestScreen::MENU);
                    } else if (rest_upgrade_card(rc.run, a0)) {
                        finish_rest_site(rc);
                    }
                } else if (screen == RestScreen::TOKE) {
                    // The bottled exclusion guards the grid HERE because
                    // rest_purge_card's own filter is the plain
                    // getPurgeableCards mirror (rest_sites.cpp) -- the
                    // stronger predicate belongs to the surfaces the Java
                    // routes through getGroupWithoutBottledCards
                    // (CampfireTokeEffect.java:57), and this dispatch guard
                    // is that surface for the Toke grid; the OPTION gate is
                    // folded into build_rest_menu (PeacePipe.java:48).
                    if (a0 == kChooseCancelGrid) {
                        rc.rest.screen =
                            static_cast<uint8_t>(RestScreen::MENU);
                    } else if (a0 < rc.run.master_deck_count &&
                        master_card_purgeable_unbottled(
                            rc.run.master_deck[a0]) &&
                        rest_purge_card(rc.run, a0)) {
                        finish_rest_site(rc);
                    }
                } else if (screen == RestScreen::DREAM_CATCHER) {
                    bool done = false;
                    if (a0 == kChooseSkipCard &&
                        reward_card_item_open_legal(rc.rewards)) {
                        reward_skip_card(rc.rewards);
                        done = true;
                    } else if (a0 == kChooseSing) {
                        done = reward_sing(rc.run, rc.rewards);
                    } else {
                        done = reward_take_card(rc.run, rc.rewards, a0);
                    }
                    if (done) {
                        finish_rest_site(rc);
                    }
                }
            }
            fill_run_result(rc, res);
            break;
        }

        case RunPhase::TREASURE_ROOM: {
            if (action_verb(a) == ActionVerb::CHOOSE) {
                const uint8_t a0 = action_arg0(a);
                if (a0 == kChooseOpenChest &&
                    treasure_chest_open_legal(
                        rc.run, rc.treasure_chest) &&
                    open_treasure_chest(
                        rc.run, rc.treasure_chest, rc.rewards)) {
                    rc.phase = static_cast<uint8_t>(
                        RunPhase::COMBAT_REWARD);
                } else if (a0 == kChooseProceed) {
                    // The map Proceed button is live before the chest is opened;
                    // skipping consumes no open-time RNG or relic hooks.
                    rc.treasure_chest = TreasureChest{};
                    rc.rewards = RewardScreen{};
                    rc.rewards.open_card_item = kNoOpenCardReward;
                    rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
                }
            }
            fill_run_result(rc, res);
            break;
        }

        case RunPhase::EVENT_DIALOG: {
            // Dialog choices; illegal ones are non-corrupting no-ops (the
            // MAP_CHOICE contract). Legality is re-derived from the live
            // menu, so a stale mask can never pick a disabled option.
            if (action_verb(a) == ActionVerb::CHOOSE) {
                const EventDialogImpl* impl =
                    event_dialog_impl(rc.event.event_id);
                const uint8_t a0 = action_arg0(a);
                if (impl != nullptr &&
                    rc.event.grid_kind !=
                        static_cast<uint8_t>(EventGridKind::NONE)) {
                    if (event_grid_card_legal(rc.run, rc.event, a0)) {
                        (void)impl->choose(rc, rc.event, a0);
                    }
                } else if (impl != nullptr && a0 < kEventOptionCap) {
                    EventDialogMenu menu{};
                    impl->build_menu(rc, rc.event, menu);
                    if (a0 < menu.count && menu.enabled[a0] &&
                        impl->choose(rc, rc.event, a0) ==
                            EventDialogStatus::FINISHED) {
                        // The event's proceed: back to the map, dialog state
                        // cleared so nothing leaks across rooms. CONTINUE
                        // keeps EVENT_DIALOG; TRANSITIONED leaves the phase /
                        // screen exactly as the body installed it.
                        rc.event = EventDialogState{};
                        rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
                    }
                }
            }
            fill_run_result(rc, res);
            break;
        }

        case RunPhase::SHOP: {
            // Buying is the MAP_CHOICE contract again: an illegal arg0 is a
            // non-corrupting no-op, and every purchase re-derives its own
            // legality inside shop.cpp so a stale mask cannot spend gold.
            if (action_verb(a) == ActionVerb::CHOOSE) {
                ShopState& shop = rc.shop;
                const uint8_t a0 = action_arg0(a);
                if (static_cast<ShopScreenKind>(shop.screen) ==
                    ShopScreenKind::PURGE_GRID) {
                    if (a0 == kChooseCancelGrid) {
                        shop.screen =
                            static_cast<uint8_t>(ShopScreenKind::MENU);
                    } else {
                        (void)shop_purge_card(rc.run, shop, a0);
                    }
                } else if (a0 == kChooseProceed) {
                    rc.shop = ShopState{};
                    rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
                } else if (a0 == kChooseShopPurge) {
                    // purchasePurge (:969-978) only OPENS the grid; the gold
                    // leaves when a card is confirmed.
                    if (shop_purge_legal(rc.run, shop)) {
                        shop.screen =
                            static_cast<uint8_t>(ShopScreenKind::PURGE_GRID);
                    }
                } else if (a0 >= kChooseShopPotionBase &&
                           a0 < kChooseShopPurge) {
                    (void)shop_buy_potion(
                        rc.run, shop,
                        static_cast<uint8_t>(a0 - kChooseShopPotionBase));
                } else if (a0 >= kChooseShopRelicBase) {
                    // The tier-rolled stock can hold a Bottled trio relic;
                    // its purchase opens the bottle grid over the shop
                    // (StoreRelic.purchaseRelic -> instantObtain -> onEquip),
                    // carried by the same pending-bottle overlay the claim
                    // sites use.
                    RelicEquipContext ectx{rc.combat.card_random_rng,
                                           rc.rewards, RoomType::Shop};
                    (void)shop_buy_relic(
                        rc.run, rc.combat.misc_rng, shop,
                        static_cast<uint8_t>(a0 - kChooseShopRelicBase), ectx);
                    apply_claim_equip_request(rc, ectx);
                } else if (a0 >= kChooseShopColorlessBase) {
                    (void)shop_buy_card(
                        rc.run, shop,
                        static_cast<uint8_t>(a0 - kChooseShopColorlessBase),
                        /*colorless=*/true);
                } else {
                    (void)shop_buy_card(rc.run, shop, a0, /*colorless=*/false);
                }
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
