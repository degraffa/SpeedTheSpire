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
#include "sts/engine/map_gen.hpp"          // generate_map / encode_paths_into_run_state / kBossCol / kEdge*
#include "sts/engine/map_rooms.hpp"        // assign_room_types / encode_rooms_into_run_state / RoomType
#include "sts/engine/monster_dispatch.hpp" // spawn_group / dispatch_monster_turn / monster_init_fn
#include "sts/engine/monster_looter.hpp"   // looter_stolen_gold (settlement)
#include "sts/engine/observation.hpp"      // encode_observation
#include "sts/engine/potions.hpp"          // PotionId / use_potion / slot count
#include "sts/engine/relic_hooks.hpp"      // dispatch_relics_at_battle_start / _on_victory
#include "sts/engine/relic_pools.hpp"      // pool init + acquisition/on-pickup
#include "sts/engine/rng_jdk.hpp"          // JdkRandom / jdk_shuffle
#include "sts/engine/rng_stream.hpp"       // floor_stream / random_long / from_seed
#include "sts/engine/run_deck.hpp"         // add_card_to_master_deck (the obtain door)
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
    if (id == PotionId::SMOKE_BOMB &&
        rc.room_type == static_cast<uint8_t>(RoomType::Boss)) {
        return false;  // SmokeBomb.canUse rejects bosses.
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
    encode_observation(s, r.obs);
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
// remaining gold, bypassing gainGold and its relic hooks); CombatState carries
// no gold field, so the engine accrues the UNCLAMPED count on the monster
// record (looter_stolen_gold) and settles min(total, gold) here instead --
// equal to the game's per-steal clamps whenever the thief's steals are the
// combat's only gold movement, which holds at this layer: every Act-1 group
// fields at most one thief, and no landed combat effect touches RunState.gold
// while a combat is live (gold moves again only at reward claim, after this).
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
                            rc.rewards, stolen_return);

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

    // (6) Spawn the resolved group (monster_hp_rng HP rolls + ai_rng rollMove, in
    //     spawn order).
    spawn_group(s, std::span<const MonsterId>(ids, grp.count));

    // (7) Monster pre-battle actions run after every member is spawned and
    //     before turn 1. Louses roll and apply Curl Up here.
    use_pre_battle_actions(s);

    // (8) Combat relic mirror: RunState.relics -> CombatState.relics (the seam,
    //     filled at combat spawn per its Log; folded back at combat end).
    for (uint8_t i = 0; i < rc.run.relic_count; ++i) {
        s.relics[i] = rc.run.relics[i];
    }
    s.relic_count = rc.run.relic_count;

    // (9) The game's turn-1 block, into WAITING_ON_USER. Literally the same
    //     function combat_begin (advance.cpp) calls, so the two combat-construction
    //     paths cannot disagree about how a combat starts; the derivation of why
    //     turn 1 is not getNextAction step 6 is on the declaration in
    //     action_queue.hpp.
    begin_first_turn(s, dispatch_monster_turn);

    // (10) applyStartOfCombatLogic -> every relic's atBattleStart, in acquisition
    //      order (AbstractPlayer.applyStartOfCombatLogic,
    //      AbstractPlayer.java:1892-1901). Its one call site is the turn-1
    //      combat-start block of AbstractRoom.update (AbstractRoom.java:236-258),
    //      a fixed line sequence:
    //
    //        addToBottom(GainEnergyAndEnableControlsAction)  (AbstractRoom.java:239)
    //        applyStartOfCombatPreDrawLogic()                (AbstractRoom.java:241)
    //        addToBottom(DrawCardAction(gameHandSize))       (AbstractRoom.java:242)
    //        addToBottom(EnableEndTurnButtonAction())        (AbstractRoom.java:243)
    //        applyStartOfCombatLogic()                       (AbstractRoom.java:245)
    //        applyStartOfTurnRelics()                        (AbstractRoom.java:253)
    //
    //      so atBattleStart is invoked with the opening DrawCardAction ALREADY
    //      queued, and an addToBot relic body lands behind it. queue_relic_step
    //      (relic_hooks.cpp) is addToBot-only, so the faithful placement is after
    //      the opening draw has resolved -- which is exactly where the pump above
    //      leaves the combat.
    //
    //      Placing it BEFORE the pump would not merely reorder, it would silently
    //      cancel effects. This engine folds Java's turn-1 block into
    //      start_of_turn (action_queue.cpp), which SETS player_energy before it
    //      queues the draw. Anything queued ahead of the pump executes ahead of
    //      start_of_turn, so an atBattleStart relic that grants ENERGY would be
    //      paid out and then overwritten by that SET -- a relic that fires and does
    //      nothing, the same class of dead effect as never firing at all.
    //
    //      This paragraph used to make the same argument about player_block and
    //      Anchor's 10 (Anchor.atBattleStart, Anchor.java:32-36), citing the
    //      loseBlock of GameActionManager.java:352-359 "which the turn-1 block does
    //      not actually run". That reading was right, and the code has now caught
    //      up with it: the block decay is gated to TurnStart::kSubsequentTurn, so it
    //      no longer runs at combat start and no longer threatens Anchor. The energy
    //      SET above is genuinely every-turn (AbstractRoom.java:241's
    //      GainEnergyAndEnableControlsAction), so the ordering requirement stands on
    //      that half alone -- and on plain faithfulness to AbstractRoom.java:245,
    //      where applyStartOfCombatLogic follows the queued draw.
    //
    //      Known deviation, currently unobservable: the addToTop bodies
    //      (BloodVial.java:33, Vajra.java:33, BronzeScales.java:32,
    //      OddlySmoothStone.java:33, Pantograph.java:36) resolve BEFORE the opening
    //      draw in Java. They are heals and player powers; the draw reads neither,
    //      and no card is played between the draw and the return of control, so the
    //      state handed to the player is identical either way. The distinction only
    //      becomes visible if a relic whose atBattleStart body is addToTop ever
    //      touches the draw pile or energy.
    //
    //      atBattleStartPreDraw is a SEPARATE hook, not a conflation of this one:
    //      AT_BATTLE_START_PRE_DRAW already exists in the hook inventory
    //      (relic_hooks.hpp) and is fired from applyStartOfCombatPreDrawLogic
    //      (AbstractPlayer.java:1903-1908) at AbstractRoom.java:241, genuinely
    //      before the draw. No dispatch is wired for it because no row in
    //      registry/relics.yaml binds it -- in the Java its only holders are
    //      GamblingChip, HolyWater, NinjaScroll, PureWater and Toolbox, none of
    //      which is registered. When one lands it needs its own call, ahead of the
    //      pump's draw rather than here.
    {
        const RelicView rv = player_relics(s);
        dispatch_relics_at_battle_start(s, rv.relics, rv.count);
    }
    // Drain what the hook queued. A no-op (and byte-identical) when no relic
    // responds: the queue is empty, so the pump falls straight through to
    // WAITING_ON_USER, which is already the phase.
    pump(s, dispatch_monster_turn);

    rc.combat = s;
    rc.room_type = static_cast<uint8_t>(room);
    rc.combat_outcome = static_cast<uint8_t>(RunCombatOutcome::NONE);
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT);
    return true;
}

// onPlayerEntry dispatch for the room just entered (AbstractDungeon.java:1800).
// Combat rooms build the combat via the encounter framework; a RestRoom opens
// its campfire menu; every other non-combat room kind is not yet implemented,
// so it reseeds the floor streams (for oracle-reseed visibility) and parks at
// ROOM_UNIMPLEMENTED.
void on_player_entry(RunController& rc, RoomType room) noexcept {
    const int64_t seed = rc.run.run_seed;
    const int32_t floor = static_cast<int32_t>(rc.run.floor);

    rc.room_type = static_cast<uint8_t>(room);
    rc.combat_outcome = static_cast<uint8_t>(RunCombatOutcome::NONE);

    auto stall = [&](RoomType r) noexcept {
        rc.combat = CombatState{};
        reseed_floor_streams(rc.combat, seed, floor);
        rc.rest = RestSiteState{};
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
        case RoomType::Rest:
            rc.combat = CombatState{};
            reseed_floor_streams(rc.combat, seed, floor);
            rc.rewards = RewardScreen{};
            rc.rewards.open_card_item = kNoOpenCardReward;
            rc.rest = RestSiteState{};
            rc.rest.screen = static_cast<uint8_t>(RestScreen::MENU);
            rc.phase = static_cast<uint8_t>(RunPhase::REST_SITE);
            break;
        case RoomType::Event:
        case RoomType::Shop:
        case RoomType::Treasure:
        case RoomType::None:
        default:
            stall(room);  // no room content for this kind yet
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
    //     (Exordium.java:110-221; generate_monster_lists).
    generate_monster_lists(/*act=*/1, rs.monster_rng, rc.lists);

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
    return rc;
}

// --- legal_actions (run overload) -------------------------------------------

void legal_actions(const RunController& rc, RunActionMask& out) noexcept {
    out = RunActionMask{};
    out.phase = rc.phase;

    switch (static_cast<RunPhase>(rc.phase)) {
        case RunPhase::NEOW:
            out.can_proceed = true;
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
                const RestMenu menu = build_rest_menu(rc.run);
                for (uint8_t i = 0; i < menu.count; ++i) {
                    out.can_choose_rest[i] = menu.entries[i].usable;
                }
            } else if (screen == RestScreen::SMITH) {
                for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
                    out.can_choose_master_deck[i] =
                        rest_card_upgradeable(rc.run.master_deck[i]);
                }
            } else if (screen == RestScreen::TOKE) {
                for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
                    out.can_choose_master_deck[i] =
                        rest_card_purgeable(rc.run.master_deck[i]);
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

    if (id == PotionId::FRUIT_JUICE) {
        use_fruit_juice(rc, slot);
        dispatch_run_relics_on_use_potion(rc);
    } else if (id == PotionId::ENTROPIC_BREW) {
        use_entropic_brew(rc, slot);
        dispatch_run_relics_on_use_potion(rc);
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

void step_one(RunController& rc, Action a, StepResult& res) noexcept {
    if (step_potion(rc, a, res)) {
        return;
    }
    switch (static_cast<RunPhase>(rc.phase)) {
        case RunPhase::NEOW: {
            // The Neow blessing itself is not modelled; here CHOOSE proceeds onto
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
                    rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
                } else {
                    // Claim item a0 (CARDS opens the pick screen). Relic
                    // onEquip bodies draw the floor-scoped miscRng.
                    (void)claim_reward(rc.run, rc.combat.misc_rng, s, a0);
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
                    const RestMenu menu = build_rest_menu(rc.run);
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
                        }
                    }
                } else if (screen == RestScreen::SMITH) {
                    if (rest_upgrade_card(rc.run, a0)) {
                        finish_rest_site(rc);
                    }
                } else if (screen == RestScreen::TOKE) {
                    if (rest_purge_card(rc.run, a0)) {
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
