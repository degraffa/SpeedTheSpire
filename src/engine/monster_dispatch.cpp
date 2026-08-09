// Monster init/turn dispatch + spawn. See
// monster_dispatch.hpp for the scope/rationale. The dispatch tables are a plain
// switch on MonsterId (data-oriented, no virtual dispatch); each new monster
// module adds its case.

#include "sts/engine/monster_dispatch.hpp"

#include <cassert>
#include <cstddef>

#include "interp/interp_powers.hpp"        // op_apply_power (Philosopher's Stone)
#include "sts/engine/action_queue.hpp"     // add_to_bottom, ActionQueueItem, kActorPlayer
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags (the spawn Minion)
#include "sts/engine/monster_book_of_stabbing.hpp"  // the growing stab counter
#include "sts/engine/monster_awakened_one.hpp"  // the Awakened One: the halfDead phase transition
#include "sts/engine/monster_byrd.hpp"     // the Byrd: Flight + the airborne latch
#include "sts/engine/monster_centurion.hpp"  // the Centurion: aliveCount-driven tree
#include "sts/engine/monster_chosen.hpp"   // the Chosen: Hex opener + roll tree
#include "sts/engine/monster_cultist.hpp"  // cultist_init / cultist_take_turn
#include "sts/engine/monster_darkling.hpp"  // the Darkling: half-death + revival
#include "sts/engine/monster_exploder.hpp"  // the Exploder: a fuse, not a move
#include "sts/engine/monster_donu_deca.hpp"  // the out-of-phase Act-3 pair
#include "sts/engine/monster_fungi_beast.hpp"  // Fungi Beast + its Spore Cloud
#include "sts/engine/monster_gremlin.hpp"  // the five Act-1 gremlins
#include "sts/engine/monster_gremlin_leader.hpp"  // the summoning Act-2 elite
#include "sts/engine/monster_gremlin_nob.hpp"  // gremlin_nob_init / _take_turn
#include "sts/engine/monster_guardian.hpp" // The Guardian's mode state machine
#include "sts/engine/monster_healer.hpp"   // the Healer: needToHeal + group fan-out
#include "sts/engine/monster_hexaghost.hpp"  // Hexaghost orb-count cycle
#include "sts/engine/monster_jaw_worm.hpp" // jaw_worm_init / jaw_worm_take_turn
#include "sts/engine/monster_lagavulin.hpp" // Lagavulin sleep/wake machine
#include "sts/engine/monster_looter.hpp"   // the Looter: steal + escape machine
#include "sts/engine/monster_maw.hpp"      // the Maw: roared latch + turnCount bites
#include "sts/engine/monster_louse.hpp"    // louse_* init / take_turn / pre_battle
#include "sts/engine/monster_mugger.hpp"   // the Mugger: seeded sfx + steal + escape
#include "sts/engine/monster_orb_walker.hpp"  // the Orb Walker: the discarded HP draw
#include "sts/engine/monster_repulsor.hpp" // the Repulsor: two Dazed per DAZE
#include "sts/engine/monster_spiker.hpp"   // the Spiker: the composing Thorns opener
#include "sts/engine/monster_sentry.hpp"   // sentry_* init / take_turn / pre_battle
#include "sts/engine/monster_shelled_parasite.hpp"  // Plated Armor + the recursive roll
#include "sts/engine/monster_slaver.hpp"   // the Blue and Red slavers
#include "sts/engine/monster_time_eater.hpp"  // the Time Eater: recursive getMove + Time Warp
#include "sts/engine/monster_snake_plant.hpp"  // the Snake Plant: Malleable + lastMoveBefore
#include "sts/engine/monster_snecko.hpp"   // the Snecko: Confusion opener + gated Weak
#include "sts/engine/monster_slime.hpp"    // small/medium slime init + turns
#include "sts/engine/monster_slime_large.hpp"  // large slimes + split framework
#include "sts/engine/monster_slime_boss.hpp"   // Slime Boss native AI/split
#include "sts/engine/monster_spheric_guardian.hpp"  // the zero-HP-draw Barricade sphere
#include "sts/engine/monster_taskmaster.hpp"  // the double-ctor-draw Slavers elite
#include "sts/engine/monster_spire_growth.hpp"  // the Spire Growth: a PLAYER-power query
#include "sts/engine/monster_transient.hpp"  // the Transient: Fading ramp, one ai draw
#include "sts/engine/monster_writhing_mass.hpp"  // the Writhing Mass: recursive getMove
#include "sts/registry/manifest.hpp"           // generated kMonstersCount

namespace sts::engine {

void queue_monster_move_effect(CombatState& state, uint8_t mi,
                               const sts::registry::MonsterDef& def,
                               uint8_t move, uint8_t effect_index,
                               uint8_t target_override) noexcept {
    const sts::registry::MonsterMove* mv = def.move(move);
    if (mv == nullptr || effect_index >= mv->effect_count) {
        return;  // unknown/empty move id, or a step past the end (defensive)
    }
    const sts::registry::MonsterMoveEffect& e = mv->effects[effect_index];
    ActionQueueItem it{};
    // sts::registry::Opcode is pinned byte-equal to interp.hpp's Opcode
    // (static_asserts in cards.hpp), so the raw cast dispatches correctly.
    it.opcode = static_cast<uint16_t>(e.op);
    it.src = mi;
    it.tgt = (target_override != kMoveTargetFromStep)
                 ? target_override
                 : ((e.target == sts::registry::MonsterMoveTarget::SELF)
                        ? mi
                        : kActorPlayer);
    it.amount = e.amount.at(kMonsterAscension);
    it.flags = e.extra;  // APPLY_POWER: PowerId (make_apply_power_flags packing)
    if (e.op == sts::registry::Opcode::MAKE_CARD) {
        // Monster-authored MAKE_CARD uses the same generated packing as card
        // programs. The interpreter expects CardPile in src and CardId in flags;
        // tgt remains the player to avoid dynamic enemy fan-out -- an override
        // cannot redirect a card into a monster, and is deliberately ignored
        // here rather than silently producing a malformed item.
        it.src = static_cast<uint8_t>((e.extra >> 16) & 0xFFu);
        it.tgt = kActorPlayer;
    }
    add_to_bottom(state, it);
}

void queue_monster_move_effects(CombatState& state, uint8_t mi,
                                const sts::registry::MonsterDef& def,
                                uint8_t move) noexcept {
    const sts::registry::MonsterMove* mv = def.move(move);
    if (mv == nullptr) {
        return;  // unknown/empty move id: nothing decided yet (defensive)
    }
    for (uint8_t i = 0; i < mv->effect_count; ++i) {
        queue_monster_move_effect(state, mi, def, move, i, kMoveTargetFromStep);
    }
}

// Every MonsterId in the registry has a module, so this switch is exhaustive and
// carries no `default:`: -Wswitch (via -Wall) is what tells you a newly generated
// enumerator needs an init function here, instead of the silent nullptr a
// `default:` used to hand back. The trailing return is NOT the old default -- it
// is unreachable for any declared id, and exists because callers reach this
// through `static_cast<MonsterId>(state.monsters[i].monster_id)`, so a corrupt
// record can present a value that is in no case label at all.
MonsterInitFn monster_init_fn(MonsterId id) noexcept {
    switch (id) {
        case MonsterId::NONE:
            break;  // the empty-slot sentinel (ids.hpp), never a spawnable monster
        case MonsterId::JAW_WORM:
            return &jaw_worm_init;
        case MonsterId::CULTIST:
            return &cultist_init;
        case MonsterId::LOUSE_NORMAL:
            return &louse_normal_init;
        case MonsterId::LOUSE_DEFENSIVE:
            return &louse_defensive_init;
        case MonsterId::SPIKE_SLIME_SMALL:
            return &spike_slime_small_init;
        case MonsterId::SPIKE_SLIME_MEDIUM:
            return &spike_slime_medium_init;
        case MonsterId::ACID_SLIME_SMALL:
            return &acid_slime_small_init;
        case MonsterId::ACID_SLIME_MEDIUM:
            return &acid_slime_medium_init;
        case MonsterId::SPIKE_SLIME_LARGE:
            return &spike_slime_large_init;
        case MonsterId::ACID_SLIME_LARGE:
            return &acid_slime_large_init;
        case MonsterId::SLIME_BOSS:
            return &slime_boss_init;
        case MonsterId::GREMLIN_NOB:
            return &gremlin_nob_init;
        case MonsterId::SENTRY:
            return &sentry_init;
        case MonsterId::LAGAVULIN:
            // The ELITE encounter's `new Lagavulin(true)` (MonsterHelper.java:
            // 439-441). lagavulin_init_awake is the "Lagavulin Event" ctor and has
            // no encounter to spawn it yet.
            return &lagavulin_init;
        case MonsterId::GREMLIN_WARRIOR:
            return &gremlin_warrior_init;
        case MonsterId::GREMLIN_THIEF:
            return &gremlin_thief_init;
        case MonsterId::GREMLIN_FAT:
            return &gremlin_fat_init;
        case MonsterId::GREMLIN_TSUNDERE:
            return &gremlin_tsundere_init;
        case MonsterId::GREMLIN_WIZARD:
            return &gremlin_wizard_init;
        case MonsterId::THE_GUARDIAN:
            return &guardian_init;
        case MonsterId::HEXAGHOST:
            return &hexaghost_init;
        case MonsterId::SLAVER_BLUE:
            return &slaver_blue_init;
        case MonsterId::SLAVER_RED:
            return &slaver_red_init;
        case MonsterId::FUNGI_BEAST:
            return &fungi_beast_init;
        case MonsterId::LOOTER:
            // Registering this init fn is the WHOLE of what un-parks the
            // "Looter" and "Exordium Thugs" encounters: the run layer's gate is
            // monster_init_fn(id) == nullptr, asked of this switch directly.
            return &looter_init;
        // S2.21 -- the four Act-2 city normals. Registering these init fns is
        // what un-parks their encounters, exactly as the Looter's did.
        case MonsterId::CHOSEN:
            return &chosen_init;
        case MonsterId::BYRD:
            return &byrd_init;
        case MonsterId::SHELLED_PARASITE:
            return &shelled_parasite_init;
        case MonsterId::SPHERIC_GUARDIAN:
            // The only init here that makes NO monster_hp_rng draw -- its Java
            // ctor never calls setHp (monster_spheric_guardian.hpp).
            return &spheric_guardian_init;
        // S2.22 -- the five Act-2 city normals of the second batch. Registering
        // these init fns is what un-parks "Snake Plant", "Snecko", "Centurion and
        // Healer" and the second half of "2 Thieves" (whose Looter was already
        // live), exactly as the Looter's did for its own two groups.
        case MonsterId::MUGGER:
            return &mugger_init;
        case MonsterId::SNAKE_PLANT:
            return &snake_plant_init;
        case MonsterId::SNECKO:
            return &snecko_init;
        case MonsterId::CENTURION:
            return &centurion_init;
        case MonsterId::HEALER:
            return &healer_init;
        case MonsterId::GREMLIN_LEADER:
            return &gremlin_leader_init;
        case MonsterId::TASKMASTER:
            // TWO monster_hp_rng draws, in this order: the `super(...)` argument
            // (registry roll SUPER_ARG_HP, timing CONSTRUCTOR_BEFORE_HP) and then
            // setHp (Taskmaster.java:50-56). See monster_taskmaster.hpp note (1).
            return &taskmaster_init;
        case MonsterId::BOOK_OF_STABBING:
            return &book_of_stabbing_init;
        // S2.25 -- the five Act-3 beyond normals of the first batch. Registering
        // these init fns is what un-parks "3 Darklings" (BOTH pool rows, ids 44
        // and 53), "Orb Walker", "3 Shapes", "4 Shapes", the event group
        // "2 Orb Walkers", and -- because its SphericGuardian third member landed
        // with S2.21 -- "Sphere and 2 Shapes".
        case MonsterId::DARKLING:
            return &darkling_init;
        case MonsterId::ORB_WALKER:
            // The only init here that makes TWO monster_hp_rng draws, the first
            // of them DISCARDED: the super-argument roll evaluates before the
            // ctor body (monster_orb_walker.hpp).
            return &orb_walker_init;
        case MonsterId::REPULSOR:
            return &repulsor_init;
        case MonsterId::EXPLODER:
            return &exploder_init;
        case MonsterId::SPIKER:
            return &spiker_init;
        // S2.26 -- the four Act-3 "Beyond" normals of the second batch.
        // Registering these un-parks "Spire Growth", "Transient", "Maw" and
        // "Writhing Mass". THEY SPLIT TWO-TWO ON THE HP DRAW, which is the thing
        // to check when reading them: the Spire Growth and the Writhing Mass call
        // the ONE-ARG setHp, which is setHp(hp, hp) and still draws; the Transient
        // and the Maw never call setHp at all and draw NOTHING, the Spheric
        // Guardian shape above. The batch's fifth monster, the Jaw Worm Horde, is
        // not here: it reuses JAW_WORM through a bespoke second init selected at
        // the combat-start site (run_advance.cpp), the Lagavulin pattern.
        case MonsterId::SPIRE_GROWTH:
            return &spire_growth_init;
        case MonsterId::TRANSIENT:
            // No monster_hp_rng draw (monster_transient.hpp).
            return &transient_init;
        case MonsterId::MAW:
            // No monster_hp_rng draw (monster_maw.hpp).
            return &maw_init;
        case MonsterId::WRITHING_MASS:
            return &writhing_mass_init;
        // S2.28 -- the four Act-3 Beyond bosses. Every one of them draws exactly
        // ONE monster_hp_rng roll over a DEGENERATE range: single-arg setHp is
        // setHp(hp, hp) (AbstractMonster.java:777-779) and the two-arg body draws
        // unconditionally. Fixed HP, real stream movement.
        case MonsterId::AWAKENED_ONE:
            return &awakened_one_init;
        case MonsterId::TIME_EATER:
            return &time_eater_init;
        case MonsterId::DONU:
            return &donu_init;
        case MonsterId::DECA:
            return &deca_init;
    }
    return nullptr;  // NONE, or an id no case label covers (see above)
}

// Exhaustive for the same reason as monster_init_fn; see that comment.
MonsterTurnFn monster_turn_fn(MonsterId id) noexcept {
    switch (id) {
        case MonsterId::NONE:
            break;  // the empty-slot sentinel (ids.hpp), never a spawnable monster
        case MonsterId::JAW_WORM:
            return &jaw_worm_take_turn;
        case MonsterId::CULTIST:
            return &cultist_take_turn;
        case MonsterId::LOUSE_NORMAL:
            return &louse_normal_take_turn;
        case MonsterId::LOUSE_DEFENSIVE:
            return &louse_defensive_take_turn;
        case MonsterId::SPIKE_SLIME_SMALL:
            return &spike_slime_small_take_turn;
        case MonsterId::SPIKE_SLIME_MEDIUM:
            return &spike_slime_medium_take_turn;
        case MonsterId::ACID_SLIME_SMALL:
            return &acid_slime_small_take_turn;
        case MonsterId::ACID_SLIME_MEDIUM:
            return &acid_slime_medium_take_turn;
        case MonsterId::SPIKE_SLIME_LARGE:
            return &spike_slime_large_take_turn;
        case MonsterId::ACID_SLIME_LARGE:
            return &acid_slime_large_take_turn;
        case MonsterId::SLIME_BOSS:
            return &slime_boss_take_turn;
        case MonsterId::GREMLIN_NOB:
            return &gremlin_nob_take_turn;
        case MonsterId::SENTRY:
            return &sentry_take_turn;
        case MonsterId::LAGAVULIN:
            return &lagavulin_take_turn;
        case MonsterId::GREMLIN_WARRIOR:
            return &gremlin_warrior_take_turn;
        case MonsterId::GREMLIN_THIEF:
            return &gremlin_thief_take_turn;
        case MonsterId::GREMLIN_FAT:
            return &gremlin_fat_take_turn;
        case MonsterId::GREMLIN_TSUNDERE:
            return &gremlin_tsundere_take_turn;
        case MonsterId::GREMLIN_WIZARD:
            return &gremlin_wizard_take_turn;
        case MonsterId::THE_GUARDIAN:
            return &guardian_take_turn;
        case MonsterId::HEXAGHOST:
            return &hexaghost_take_turn;
        case MonsterId::SLAVER_BLUE:
            return &slaver_blue_take_turn;
        case MonsterId::SLAVER_RED:
            return &slaver_red_take_turn;
        case MonsterId::FUNGI_BEAST:
            return &fungi_beast_take_turn;
        case MonsterId::LOOTER:
            return &looter_take_turn;
        case MonsterId::CHOSEN:
            return &chosen_take_turn;
        case MonsterId::BYRD:
            return &byrd_take_turn;
        case MonsterId::SHELLED_PARASITE:
            return &shelled_parasite_take_turn;
        case MonsterId::SPHERIC_GUARDIAN:
            return &spheric_guardian_take_turn;
        case MonsterId::MUGGER:
            return &mugger_take_turn;
        case MonsterId::SNAKE_PLANT:
            return &snake_plant_take_turn;
        case MonsterId::SNECKO:
            return &snecko_take_turn;
        case MonsterId::CENTURION:
            return &centurion_take_turn;
        case MonsterId::HEALER:
            return &healer_take_turn;
        case MonsterId::GREMLIN_LEADER:
            return &gremlin_leader_take_turn;
        case MonsterId::TASKMASTER:
            return &taskmaster_take_turn;
        case MonsterId::BOOK_OF_STABBING:
            return &book_of_stabbing_take_turn;
        case MonsterId::DARKLING:
            return &darkling_take_turn;
        case MonsterId::ORB_WALKER:
            return &orb_walker_take_turn;
        case MonsterId::REPULSOR:
            return &repulsor_take_turn;
        case MonsterId::EXPLODER:
            return &exploder_take_turn;
        case MonsterId::SPIKER:
            return &spiker_take_turn;
        case MonsterId::SPIRE_GROWTH:
            return &spire_growth_take_turn;
        case MonsterId::TRANSIENT:
            return &transient_take_turn;
        case MonsterId::MAW:
            return &maw_take_turn;
        case MonsterId::WRITHING_MASS:
            return &writhing_mass_take_turn;
        case MonsterId::AWAKENED_ONE:
            return &awakened_one_take_turn;
        case MonsterId::TIME_EATER:
            return &time_eater_take_turn;
        case MonsterId::DONU:
            return &donu_take_turn;
        case MonsterId::DECA:
            return &deca_take_turn;
    }
    // dispatch_monster_turn calls the result unconditionally, so this must be a
    // live no-op rather than nullptr.
    return &default_monster_turn;  // NONE, or an id no case label covers
}

MonsterRollMoveFn monster_roll_move_fn(MonsterId id) noexcept {
    static_assert(sts::registry::manifest::kMonstersCount == 50,
                  "new monster: does its turn QUEUE a ROLL_MOVE item (rather "
                  "than rolling inline)? Only then does it register here.");
    // Checked for S2.22's five, and they split FOUR-ONE. The Snake Plant, the
    // Snecko, the Centurion and the Healer each end takeTurn in a RollMoveAction
    // that sits AFTER the switch, so every move body reaches it
    // (SnakePlant.java:114, Snecko.java:120, Centurion.java:107,
    // Healer.java:124) -- all four register below, and all four getMove
    // overrides READ the rolled num on at least one arm.
    //
    // The MUGGER registers NONE, for the Looter's reason: takeTurn
    // (Mugger.java:86-136) has no trailing RollMoveAction at all -- every case
    // decides the next move itself with a direct setMove or a queued
    // SetMoveAction -- and getMove (:167-170) runs only from init's rollMove and
    // discards its num. Its ai_rng draws come from playSfx, the talk gate and the
    // Smoke-Bomb coin instead (monster_mugger.hpp), so it stays with the
    // `default:` and a ROLL_MOVE item aimed at one would be a safe no-op.
    // Checked for The Guardian: it queues none. getMove (TheGuardian.java:
    // 226-232) runs only from init's rollMove; every later transition is a
    // direct setMove, so no ROLL_MOVE item ever targets it.
    // Checked for the Looter: it queues none either -- takeTurn (Looter.java:
    // 88-135) has no trailing RollMoveAction; every case decides the next move
    // itself (setMove or a queued SetMoveAction), and getMove (:176-179) runs
    // only from init's rollMove, discarding its num.
    // Checked for Hexaghost: it queues one on FIVE of its six move bodies
    // (Hexaghost.java:167,176,188,196,209); only the ACTIVATE opener
    // re-telegraphs with a direct setMove (:153).
    switch (id) {
        case MonsterId::SPIKE_SLIME_LARGE:
            return &spike_slime_large_roll_move;
        case MonsterId::ACID_SLIME_LARGE:
            return &acid_slime_large_roll_move;
        // GremlinFat is the only gremlin whose takeTurn ends in a real
        // RollMoveAction (GremlinFat.java:80); the other four re-telegraph with a
        // direct setMove or a queued SetMoveAction and roll nothing.
        case MonsterId::GREMLIN_FAT:
            return &gremlin_fat_roll_move;
        // Hexaghost's queued roll also carries the ChangeStateAction that
        // immediately precedes it, and Inferno's BurnIncreaseAction ahead of
        // that -- the whole run is atomic in the Java queue
        // (monster_hexaghost.hpp).
        case MonsterId::HEXAGHOST:
            return &hexaghost_roll_move;
        // The two slavers and the Fungi Beast all end takeTurn in a RollMoveAction
        // that sits
        // AFTER the switch, so every move body reaches it (SlaverBlue.java:89,
        // SlaverRed.java:110, FungiBeast.java:97). Unlike GremlinFat's, whose
        // getMove discards the value, these three getMove overrides READ the
        // rolled num -- the draw both moves the stream and picks the move.
        case MonsterId::SLAVER_BLUE:
            return &slaver_blue_roll_move;
        case MonsterId::SLAVER_RED:
            return &slaver_red_roll_move;
        case MonsterId::FUNGI_BEAST:
            return &fungi_beast_roll_move;
        // S2.21: ALL FOUR city normals end takeTurn in a RollMoveAction that
        // sits AFTER the switch, so every move body reaches it (Chosen.java:137,
        // Byrd.java:145, ShelledParasite.java:140, SphericGuardian.java:120) --
        // with ONE exception that is a case in the turn body rather than here:
        // the Byrd's HEADBUTT returns early (Byrd.java:121) and queues no roll at
        // all, so a Byrd's turn sometimes spends no ai_rng draw. Three of the
        // four getMove overrides READ the rolled num; the Spheric Guardian's does
        // NOT (it is fully deterministic) and still registers, because the draw
        // itself moves the shared stream.
        case MonsterId::CHOSEN:
            return &chosen_roll_move;
        case MonsterId::BYRD:
            return &byrd_roll_move;
        case MonsterId::SHELLED_PARASITE:
            // The one roll fn that can spend TWO draws: getMove recurses once
            // with a fresh random(20, 99) (ShelledParasite.java:191).
            return &shelled_parasite_roll_move;
        case MonsterId::SPHERIC_GUARDIAN:
            return &spheric_guardian_roll_move;
        // S2.22: four of the five (see the note above; the Mugger queues none).
        case MonsterId::SNAKE_PLANT:
            return &snake_plant_roll_move;
        case MonsterId::SNECKO:
            return &snecko_roll_move;
        case MonsterId::CENTURION:
            // The one roll fn whose result depends on OTHER monsters' liveness
            // (aliveCount, Centurion.java:134-138), so it takes the whole state
            // rather than just its own record.
            return &centurion_roll_move;
        case MonsterId::HEALER:
            // Likewise: needToHeal sums the group's missing HP
            // (Healer.java:157-160).
            return &healer_roll_move;
        // S2.23: ALL THREE city elites register. Each ends takeTurn in a
        // RollMoveAction that sits AFTER the switch, so every move body reaches
        // it (GremlinLeader.java:133, Taskmaster.java:78,
        // BookOfStabbing.java:102).
        case MonsterId::GREMLIN_LEADER:
            // The one roll fn that can spend an UNBOUNDED number of draws:
            // getMove recurses with a fresh random(50, 99) / random(0, 80) on two
            // arms (:163,:174), and a re-drawn 80 re-enters the same arm. It also
            // reads OTHER monsters' liveness (numAliveGremlins), and on a RALLY
            // turn its ITEM is aimed at a pre-computed post-insertion index --
            // see monster_gremlin_leader.hpp notes (1) and (3).
            return &gremlin_leader_roll_move;
        case MonsterId::TASKMASTER:
            // getMove discards the num (Taskmaster.java:81-84) and still
            // registers, for the Spheric Guardian's reason: the draw moves the
            // shared stream even when its value decides nothing.
            return &taskmaster_roll_move;
        case MonsterId::BOOK_OF_STABBING:
            // The one roll fn that WRITES per-instance state: getMove
            // ++stabCount's on three of four paths, and on all four at A18+
            // (BookOfStabbing.java:129-150).
            return &book_of_stabbing_roll_move;
        // S2.25: ALL FIVE end takeTurn in a RollMoveAction that sits AFTER the
        // switch, so every move body reaches it (Darkling.java:140,
        // OrbWalker.java:97, Repulsor.java:72, Exploder.java:82, Spiker.java:93)
        // -- including the Darkling's COUNT and the Exploder's empty move 2,
        // which is how a half-dead Darkling ever reaches REINCARNATE. Four of
        // the five getMove overrides READ the rolled num; the EXPLODER's does
        // NOT (it is a bare turnCount test) and still registers, because the
        // draw itself moves the shared stream -- the Spheric Guardian precedent.
        case MonsterId::DARKLING:
            // The one roll fn with UNBOUNDED recursion: getMove re-enters on a
            // fresh draw at TWO sites with DIFFERENT bounds -- random(40, 99) at
            // Darkling.java:166 and random(0, 99) at :181 -- so a single decision
            // can spend one, two or more ai_rng values. It also carries the
            // REINCARNATE turn's ChangeState("REVIVE") halfDead clear
            // (monster_darkling.hpp).
            return &darkling_roll_move;
        case MonsterId::ORB_WALKER:
            return &orb_walker_roll_move;
        case MonsterId::REPULSOR:
            return &repulsor_roll_move;
        case MonsterId::EXPLODER:
            return &exploder_roll_move;
        case MonsterId::SPIKER:
            return &spiker_roll_move;
        // S2.26: three of the four. The Spire Growth, the Maw and the Writhing
        // Mass each end takeTurn in a RollMoveAction sitting AFTER the switch
        // (SpireGrowth.java:97, Maw.java:114, WrithingMass.java:122).
        //
        // The TRANSIENT registers NONE, and its reason is unlike either of the
        // two the Mugger and the Looter gave: takeTurn does not merely lack a
        // RollMoveAction, it re-telegraphs the monster BY HAND with its own
        // setMove (Transient.java:81). So the Transient decides its next move
        // exactly once per turn, inside its turn body, and never through this
        // seam at all -- one ai_rng draw for the whole combat, at init.
        case MonsterId::SPIRE_GROWTH:
            return &spire_growth_roll_move;
        case MonsterId::MAW:
            // Its roll has a SIDE EFFECT the others do not: getMove
            // pre-increments turnCount (Maw.java:118) on every call, which is
            // what grows the NOMNOMNOM bite count.
            return &maw_roll_move;
        case MonsterId::WRITHING_MASS:
            // The heaviest roll fn in the engine: getMove is RECURSIVE and each
            // level spends a fresh random(a, b), plus up to one randomBoolean
            // tiebreak per level (WrithingMass.java:158-193). It is also the only
            // roll fn a POWER queues -- ReactivePower fires one per real hit
            // during the PLAYER's turn -- which is precisely why the Writhing
            // Mass rolls through this seam instead of inline.
            return &writhing_mass_roll_move;
        // S2.28: ALL FOUR Act-3 bosses end takeTurn in a RollMoveAction that sits
        // AFTER the switch, so every move body reaches it (AwakenedOne.java:207,
        // TimeEater.java:154, Donu.java:121, Deca.java:131) -- including the
        // Awakened One's REBIRTH turn, which still rolls while the boss is at 0 HP.
        //
        // FOR THE AWAKENED ONE THE QUEUED FORM IS LOAD-BEARING, not a style
        // choice. Its phase transition sets move 3 SYNCHRONOUSLY and ALSO queues a
        // SetMoveAction(3) at the bottom (AwakenedOne.java:309,312); the queued one
        // exists precisely to land BEHIND a RollMoveAction that takeTurn had
        // already queued, which is the state a boss downed during its own turn is
        // in. Roll inline and there is no roll for it to land behind.
        //
        // Donu's and Deca's getMoves IGNORE the rolled num entirely (both key off
        // isAttacking) and they register anyway -- the Spheric Guardian's reason:
        // the draw itself moves the shared ai_rng stream.
        case MonsterId::AWAKENED_ONE:
            return &awakened_one_roll_move;
        case MonsterId::TIME_EATER:
            // The one roll fn in this batch that can spend more than one draw:
            // getMove re-enters with a fresh random(50,99) or random(74), and the
            // middle band can spend a randomBoolean instead (TimeEater.java:188,
            // :196, :206).
            return &time_eater_roll_move;
        case MonsterId::DONU:
            return &donu_roll_move;
        case MonsterId::DECA:
            return &deca_roll_move;
        default:
            return nullptr;  // rolls inline in its MonsterTurnFn; no queued rolls
    }
}

void roll_monster_move(CombatState& state, uint8_t monster_index) noexcept {
    if (monster_index >= kMonsterCap) {
        return;
    }
    const MonsterId id =
        static_cast<MonsterId>(state.monsters[monster_index].monster_id);
    const MonsterRollMoveFn fn = monster_roll_move_fn(id);
    if (fn != nullptr) {
        fn(state, monster_index);  // no liveness gate (RollMoveAction.java:17-21)
    }
}

MonsterSpawnAtHpFn monster_spawn_at_hp_fn(MonsterId id) noexcept {
    static_assert(sts::registry::manifest::kMonstersCount == 50,
                  "new monster: can anything spawn it mid-combat (a split, a "
                  "summon)? Only then does it need a spawn-at-fixed-HP init "
                  "here; spawn_monster_at_slot hard-asserts without one.");
    // Checked for S2.22's five city normals: NONE of them is mid-combat
    // spawnable. Every Act-2 group that fields one builds it at spawn time
    // (MonsterHelper.java's "2 Thieves" :462-464, "Snake Plant" :492-494,
    // "Snecko" :495-497 and "Centurion and Healer" :498-500), and not one of the
    // five classes declares a SpawnMonsterAction, a SplitPower or a summon list.
    // The Mugger is the closest thing to a mid-combat arrival/departure and it is
    // a DEPARTURE -- it ESCAPES, exactly as the Looter does, which removes a
    // record from the fight rather than adding one.
    // Checked for S2.21's four city normals: NONE of them is mid-combat
    // spawnable. Every Act-2/3 group that fields one builds it at spawn time
    // (MonsterHelper.java's "Chosen" / "3 Byrds" / "Chosen and Byrds" /
    // "Shell Parasite" / "Shelled Parasite and Fungi" / "Spheric Guardian" /
    // "Sentry and Sphere" / "Cultist and Chosen" / "Sphere and 2 Shapes" cases),
    // and not one of the four classes splits or summons: none declares a
    // SpawnMonsterAction, a SplitPower or a summon list. The Byrd's GO_AIRBORNE
    // is the closest thing to a "return", and it re-powers the SAME record
    // rather than creating one.
    // Checked for the Looter: nothing spawns it mid-combat. Both encounters
    // that field one build it at spawn time ("Looter", MonsterHelper.java:
    // 400-402; Exordium Thugs' bottomGetStrongHumanoid, :816-829); it neither
    // splits nor summons, and it leaves by ESCAPING, not by spawning anything.
    // Checked for the two slavers and the Fungi Beast: nothing spawns any of
    // them mid-combat. Every group that contains one builds it at spawn time
    // (MonsterHelper.java:391-393,406-408,427-429 and the Exordium Thugs /
    // Exordium Wildlife bottom-* helpers, :780-829); none splits or summons.
    // ...AND THAT PARAGRAPH IS STILL TRUE OF THE SLAVERS after S2.23 -- the
    // Taskmaster joins their group but summons nothing (Taskmaster.java declares
    // takeTurn, getMove, playSfx, playDeathSfx and die, and nothing else).
    //
    // S2.23: THE FIVE GREMLINS BECOME MID-COMBAT SPAWNABLE, and they are the
    // first monsters in the roster to get here by SUMMON rather than by split.
    // GremlinLeader's RALLY queues two SummonGremlinActions
    // (GremlinLeader.java:108-109), each of which draws a key from an 8-entry
    // pool containing every one of the five (SummonGremlinAction.java:59-67) --
    // so all five need an entry, not just the ones a given fight happens to
    // produce. Their HP arrives PRE-DRAWN because the gremlin's constructor runs
    // inside the summon action's constructor, at addToBottom time; see
    // monster_gremlin.hpp.
    // Checked for the three S2.23 elites themselves: none of them is mid-combat
    // spawnable. The Gremlin Leader is built by the encounter
    // (MonsterHelper.java:507-509) and is the SUMMONER, never the summoned; the
    // Taskmaster is built by "Slavers" (:510-512) and "Colosseum Nobs" (:516-518);
    // the Book of Stabbing is a solo encounter (:504-506) and declares no spawn
    // machinery at all.
    // Checked for The Guardian: nothing spawns it mid-combat. It is a solo boss
    // encounter (encounters.yaml "The Guardian") and neither splits nor summons.
    // Same for Hexaghost: a solo boss encounter (encounters.yaml "Hexaghost")
    // whose six orbs are rendering objects, not monster records
    // (HexaghostOrb.java:19-59) -- nothing is ever spawned mid-combat.
    switch (id) {
        case MonsterId::SPIKE_SLIME_MEDIUM:
            return &spike_slime_medium_spawn_at_hp;
        case MonsterId::ACID_SLIME_MEDIUM:
            return &acid_slime_medium_spawn_at_hp;
        case MonsterId::SPIKE_SLIME_LARGE:  // Slime Boss split children
            return &spike_slime_large_spawn_at_hp;
        case MonsterId::ACID_SLIME_LARGE:
            return &acid_slime_large_spawn_at_hp;
        // S2.23: the Gremlin Leader's summon pool, all five members.
        case MonsterId::GREMLIN_WARRIOR:
            return &gremlin_warrior_spawn_at_hp;
        case MonsterId::GREMLIN_THIEF:
            return &gremlin_thief_spawn_at_hp;
        case MonsterId::GREMLIN_FAT:
            return &gremlin_fat_spawn_at_hp;
        case MonsterId::GREMLIN_TSUNDERE:
            return &gremlin_tsundere_spawn_at_hp;
        case MonsterId::GREMLIN_WIZARD:
            return &gremlin_wizard_spawn_at_hp;
        default:
            return nullptr;  // not mid-combat spawnable
    }
}

// The onSpawnMonster relic fan-out, in acquisition order. Philosopher's Stone is
// the whole of it: PhilosopherStone.onSpawnMonster (PhilosopherStone.java:50-54)
// is
//     monster.addPower(new StrengthPower(monster, 1));
//     AbstractDungeon.onModifyPower();
// -- a DIRECT AbstractCreature.addPower, synchronous, not an ApplyPowerAction,
// exactly like its atBattleStart sibling (relics/relics_boss.cpp), so it applies
// rather than queues. op_apply_power is reused for the slot append/stack, and
// the same inertness argument the atBattleStart body spells out holds verbatim
// for this call shape (src == tgt == the monster; Strength(+1) is a BUFF, so the
// Artifact nullify cannot fire; Champion Belt needs Vulnerable; Ginger/Turnip
// need the player).
//
// Duplicates are per SLOT, matching the Java's `for (AbstractRelic r : relics)`.
void dispatch_on_spawn_monster_relics(CombatState& state,
                                      uint8_t monster_index) noexcept {
    for (uint8_t i = 0; i < state.relic_count; ++i) {
        if (state.relics[i].relic_id ==
            static_cast<uint16_t>(RelicId::PHILOSOPHERS_STONE)) {
            op_apply_power(state, monster_index, monster_index, PowerId::STRENGTH,
                           1);
        }
    }
}

uint8_t smart_position_for(const CombatState& state, int16_t draw_x) noexcept {
    uint8_t position = 0;
    for (uint8_t i = 0; i < state.monster_count; ++i) {
        // `if (!(m.drawX > mo.drawX)) break;` -- strict, and it BREAKS rather
        // than continuing, so an out-of-order list stops the walk early exactly
        // as the Java's does.
        if (!(draw_x > state.monsters[i].draw_x)) {
            break;
        }
        ++position;
    }
    return position;
}

void spawn_monster_at_slot(CombatState& state, uint8_t slot, MonsterId id,
                           int16_t hp, bool run_pre_battle, bool apply_minion,
                           int16_t draw_x) noexcept {
    assert(state.monster_count < kMonsterCap &&
           "spawn_monster_at_slot: monster record overflow. kMonsterCap is 23 "
           "-- the largest the CombatState size ceiling admits, NOT a derived "
           "bound: Gremlin Leader, The Collector and Reptomancer all grow their "
           "record count without limit as a fight lasts. See the kMonsterCap "
           "comment in combat_state.hpp before raising it.");
    if (slot > state.monster_count) {
        slot = state.monster_count;  // defensive clamp (addMonster clamps < 0)
    }
    // List-insert: shift records at >= slot up one (MonsterGroup.addMonster,
    // MonsterGroup.java:35-40). Dead records shift too -- index identity among
    // ALL records is what smart positioning counted.
    for (uint8_t i = state.monster_count; i > slot; --i) {
        state.monsters[i] = state.monsters[i - 1];
    }
    state.monsters[slot] = MonsterState{};
    ++state.monster_count;
    // Remap pending monster-turn queue entries (the game's monsterQueue holds
    // object references, immune to list insertion; this engine holds indices).
    for (uint8_t i = 0; i < state.monster_queue_count; ++i) {
        if (state.monster_queue[i].monster_index >= slot) {
            ++state.monster_queue[i].monster_index;
        }
    }
    const MonsterSpawnAtHpFn fn = monster_spawn_at_hp_fn(id);
    assert(fn != nullptr && "spawn_monster_at_slot: monster is not mid-combat "
                            "spawnable (monster_spawn_at_hp_fn)");
    fn(state, slot, hp);  // m.init(): the child's aiRng roll, at resolve time
    // The position key the SPAWNER supplied, written after the init because the
    // init assigns every field of a fresh record (see the header's amended "WHO
    // SETS draw_x" paragraph). Zero for every caller that names only a
    // MonsterId, which is exactly what those records carried before.
    state.monsters[slot].draw_x = draw_x;

    // relics.onSpawnMonster (SpawnMonsterAction.update, SpawnMonsterAction.java:
    // 44-50). A hand-written fan-out rather than a registry RelicHook: the
    // generated hook table has no on_spawn_monster surface, Philosopher's Stone
    // is its ONLY implementor in the whole game (`grep -rn onSpawnMonster com/`
    // finds AbstractRelic's empty base and this one override), and adding a hook
    // id would move kRelicHookCount for a single relic. Velvet Choker's canPlay
    // veto is the same precedent.
    //
    // PLACEMENT vs the Java, which runs the relic loop BEFORE m.init(): here the
    // fan-out is AFTER, and that is forced and equivalent, both of which matter:
    //   * forced -- the monster RECORD does not exist until the lines above
    //     write it, and the spawn-at-hp init zeroes power_count
    //     (spawn_fields_at_hp, monster_slime.cpp), so a Strength written first
    //     would be erased.
    //   * equivalent -- the two things the Java does between the relic loop and
    //     the slot insert are init()'s rollMove, which does not read Strength
    //     (so the aiRng draw is unaffected), and applyPowers(), which bakes the
    //     telegraphed damage. This engine has no bake: monster damage is
    //     computed when the move's effects are queued at take-turn time
    //     (queue_monster_move_effects above), which is strictly after this.
    dispatch_on_spawn_monster_relics(state, slot);

    // SummonGremlinAction.update's own addToBot(ApplyPowerAction(m, m, new
    // MinionPower(m))) (SummonGremlinAction.java:114). QUEUED, not applied: the
    // Java queues it and it resolves after everything already in the queue --
    // notably after the summoner's own trailing RollMoveAction. Its amount is
    // -1, MinionPower's un-assigned AbstractPower field initialiser
    // (MinionPower.java:21-28; AbstractPower.java:65), NOT 1 (the Confusion
    // adjudication, monster_snecko.hpp kConfusionAppliedAmount).
    //
    // BEFORE the pre-battle block below, because the Java's isDone arm (:117-121)
    // runs usePreBattleAction after this addToBot -- so a summoned Gremlin
    // Warrior's items land [Minion, Angry], in that order.
    if (apply_minion) {
        ActionQueueItem minion{};
        minion.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        minion.src = slot;
        minion.tgt = slot;
        minion.amount = kMinionAppliedAmount;
        minion.flags = make_apply_power_flags(PowerId::MINION);
        add_to_bottom(state, minion);
    }

    // SummonGremlinAction.update runs the child's usePreBattleAction at isDone
    // (a summoned Gremlin Warrior's Angry power); SpawnMonsterAction does not.
    // Opt-in for that reason -- see the header. It runs AFTER the relic fan-out
    // above for the same forced-and-equivalent reason the fan-out runs after
    // init: the record must exist and be fully written first.
    if (run_pre_battle) {
        const MonsterPreBattleFn pre = monster_pre_battle_fn(id);
        if (pre != nullptr) {
            pre(state, slot);
        }
    }
}

void on_monster_damaged(CombatState& state, uint8_t monster_index,
                        int32_t hp_lost) noexcept {
    static_assert(sts::registry::manifest::kMonstersCount == 50,
                  "new monster: does its Java class override damage()? Only "
                  "then does it register a post-damage hook here.");
    // Checked for the Looter: NO damage() override at all -- Looter.java
    // declares usePreBattleAction, takeTurn, playSfx, playDeathSfx, die and
    // getMove, nothing else -- so it stays with the `default:`. Its die()
    // override (:159-174) is playDeathSfx (MathUtils, unseeded) plus the
    // stolen-gold return to rewards, which is a READ of the surviving record
    // (looter_stolen_gold) by the reward layer, not a combat-time hook.
    if (monster_index >= kMonsterCap) {
        return;
    }
    switch (static_cast<MonsterId>(state.monsters[monster_index].monster_id)) {
        case MonsterId::SPIKE_SLIME_LARGE:
        case MonsterId::ACID_SLIME_LARGE:
            large_slime_on_damaged(state, monster_index);
            return;
        case MonsterId::SLIME_BOSS:
            slime_boss_on_damaged(state, monster_index);
            return;
        case MonsterId::LAGAVULIN:
            // The only override that reads how much HP actually moved
            // (Lagavulin.java:199-205); the slime interrupts test resulting HP.
            lagavulin_on_damaged(state, monster_index, hp_lost);
            return;

        // Sentry.damage (Sentry.java:115-122) DOES override damage(), but its
        // whole body after super.damage() is the "hit" spine animation, gated on
        // a non-THORNS hit with output > 0. Nothing there touches combat state or
        // draws RNG, so an empty hook is the complete translation -- hp_lost is
        // deliberately unread. Spelled as a case rather than left to `default:`
        // so the omission is checkable.
        case MonsterId::SENTRY:
            return;

        // TheGuardian.damage (TheGuardian.java:275-292) -- unlike the split
        // interrupts above, this override reads the SIZE of the hit
        // (`tmpHealth - currentHealth`), not the resulting HP. It reconstructs
        // that delta from its own HP baseline (monster_guardian.hpp). This hook
        // now DOES carry the lost HP -- the `hp_lost` parameter Lagavulin's wake
        // test needed -- so the Guardian's private reconstruction is redundant.
        // It is left as written because the two are equivalent here and swapping
        // them is a behaviour-preserving cleanup that wants its own test run,
        // not a change made in passing.
        case MonsterId::THE_GUARDIAN:
            guardian_on_damaged(state, monster_index);
            return;

        // Hexaghost does NOT override damage() (Hexaghost.java declares
        // takeTurn/getMove/changeState/die/update/render and nothing else), so
        // there is genuinely no hook to register. Spelled as a case for the
        // same reason as Sentry above.
        case MonsterId::HEXAGHOST:
            return;

        // FungiBeast.damage (FungiBeast.java:126-133) DOES override damage(),
        // and -- like Sentry's -- its whole body past super.damage() is the
        // "Hit" spine animation, gated on a non-THORNS hit with output > 0.
        // Nothing there touches combat state or draws RNG, so an empty hook is
        // the complete translation; hp_lost is deliberately unread. Its Spore
        // Cloud is an ON_DEATH power, dispatched at the death edge in
        // interp_damage.cpp, not here. NEITHER SLAVER overrides damage() at all
        // (SlaverBlue.java / SlaverRed.java declare takeTurn, playSfx,
        // playDeathSfx, getMove, die and -- for the Red -- changeState, and
        // nothing else), so both stay with the `default:`.
        case MonsterId::FUNGI_BEAST:
            return;

        // S2.21. THREE of the four DO override damage() and all three are empty
        // here, for the Sentry's reason: Chosen.damage (Chosen.java:199-207),
        // ShelledParasite.damage (:163-170) and SphericGuardian.damage (:136-143)
        // are each `super.damage(info)` followed ONLY by the "Hit" spine
        // animation, gated on a non-THORNS hit with output > 0. Nothing there
        // touches combat state or draws RNG, so an empty hook is the COMPLETE
        // translation and hp_lost is deliberately unread. Spelled as cases rather
        // than left to the `default:` so the omission is checkable.
        //
        // The Byrd does NOT override damage() at all (Byrd.java declares
        // usePreBattleAction, takeTurn, playRandomBirdSFx, changeState, getMove
        // and die, and nothing else), so it stays with the `default:` -- which is
        // worth stating because the Byrd is the one of the four that visibly
        // REACTS to being attacked. That reaction is FlightPower.onAttacked, a
        // POWER hook dispatched from op_damage, not a monster damage() override.
        case MonsterId::CHOSEN:
        case MonsterId::SHELLED_PARASITE:
        case MonsterId::SPHERIC_GUARDIAN:
            return;

        // S2.22. FOUR of the five DO override damage() and all four are empty
        // here, for the Sentry's reason: SnakePlant.damage (SnakePlant.java:
        // 85-91), Snecko.damage (:138-144), Centurion.damage (:163-170) and
        // Healer.damage (:186-194) are each `super.damage(info)` followed ONLY by
        // the "Hit" spine animation, gated on a non-THORNS hit with output > 0
        // (the Centurion's and the Healer's add a time-scale line, which is also
        // animation). Nothing there touches combat state or draws RNG, so an
        // empty hook is the COMPLETE translation and hp_lost is deliberately
        // unread. Spelled as cases so the omission is checkable.
        //
        // The MUGGER does NOT override damage() at all (Mugger.java declares
        // usePreBattleAction, takeTurn, playSfx, playDeathSfx, die and getMove,
        // and nothing else), so it stays with the `default:` -- the Looter's
        // answer, for the Looter's reason. Its die() DOES carry state (a seeded
        // aiRng draw), and that is the MonsterDieFn seam below, not this one.
        //
        // The SNAKE PLANT is the one of the five that visibly REACTS to being
        // attacked, and -- like the Byrd -- that reaction is a POWER hook
        // (MalleablePower.onAttacked, dispatched from op_damage), not a damage()
        // override. Worth stating so the empty case is not read as a hole.
        case MonsterId::SNAKE_PLANT:
        case MonsterId::SNECKO:
        case MonsterId::CENTURION:
        case MonsterId::HEALER:
            return;

        // S2.23. TWO of the three city elites override damage() and both are
        // empty here, for the Sentry's reason: GremlinLeader.damage
        // (GremlinLeader.java:215-222) and BookOfStabbing.damage
        // (BookOfStabbing.java:120-127) are each `super.damage(info)` followed
        // ONLY by the "Hit" spine animation, gated on a non-THORNS hit with
        // output > 0. Nothing there touches combat state or draws RNG, so an
        // empty hook is the COMPLETE translation and hp_lost is deliberately
        // unread. The TASKMASTER does not override damage() at all
        // (Taskmaster.java declares takeTurn, getMove, playSfx, playDeathSfx and
        // die, and nothing else), so it stays with the `default:`.
        //
        // The Gremlin Leader's real reaction to being killed is not here either:
        // it is die(), on the POST-super side, and it registers in
        // monster_die_after_fn below.
        case MonsterId::GREMLIN_LEADER:
        case MonsterId::BOOK_OF_STABBING:
            return;

        // S2.25. THE DARKLING IS THE FIRST damage() OVERRIDE SINCE THE SLIME
        // SPLITS WITH REAL CONTENT, and it is the largest one in the roster:
        // Darkling.damage (Darkling.java:200-236) is the whole half-death
        // machine -- the halfDead latch, the by-hand power/relic fan-outs that
        // its VETOED die() did not run, the power-list clear, the group-wide
        // "are all the Darklings down" test, and either the COUNT telegraph or a
        // synchronous group kill. hp_lost is deliberately unread: the override's
        // own `else if` arm (:232-235) is the hit animation, and the half-death
        // arm reads only resulting state.
        case MonsterId::DARKLING:
            darkling_on_damaged(state, monster_index, hp_lost);
            return;

        // The ORB WALKER overrides damage() (OrbWalker.java:115-122) and is
        // empty here for the Sentry's reason: `super.damage(info)` followed ONLY
        // by the "Hit" spine animation, gated on a non-THORNS hit with output
        // > 0. The REPULSOR, the EXPLODER and the SPIKER do NOT override
        // damage() at all -- Repulsor.java declares takeTurn and getMove;
        // Exploder.java adds usePreBattleAction; Spiker.java the same -- and
        // they are spelled out with the Orb Walker so the whole batch is
        // checkable in one place. The SPIKER is the one that visibly REACTS to
        // being attacked, and, like the Byrd and the Snake Plant, that reaction
        // is a POWER hook (ThornsPower.onAttacked, dispatched from op_damage),
        // not a damage() override.
        case MonsterId::ORB_WALKER:
        case MonsterId::REPULSOR:
        case MonsterId::EXPLODER:
        case MonsterId::SPIKER:
        // S2.26. THREE of the four declare damage() and all three are empty
        // here. SpireGrowth.damage (SpireGrowth.java:121-129) and
        // Transient.damage (:88-96) run `super.damage(info)` and THEN the hurt
        // animation; WrithingMass.damage (:125-132) runs the animation FIRST and
        // then super.damage -- the reverse order, and presentation either way,
        // so the difference costs nothing here. All three gate on a non-THORNS
        // hit with output > 0 and touch no combat state and no RNG, so an empty
        // hook is the COMPLETE translation and hp_lost is deliberately unread.
        //
        // The MAW does not override damage() at all (Maw.java declares takeTurn,
        // getMove and die, and nothing else), so it would fall to the `default:`
        // -- it is listed here anyway, with that said, because a reader checking
        // this batch should not have to re-open the file to learn which of the
        // four the absence belongs to.
        //
        // The WRITHING MASS is the one of the four that visibly REACTS to being
        // attacked -- twice over, Malleable's block and Reactive's re-roll -- and
        // both reactions are POWER hooks dispatched from op_damage, not damage()
        // overrides. The TRANSIENT likewise reacts through Shifting. Worth
        // stating so these empty cases are not read as holes.
        case MonsterId::SPIRE_GROWTH:
        case MonsterId::TRANSIENT:
        case MonsterId::MAW:
        case MonsterId::WRITHING_MASS:
            return;

        // S2.28. All four Act-3 bosses override damage(); exactly ONE of them has
        // content, and that split is the whole shape of the batch.
        case MonsterId::AWAKENED_ONE:
            // AwakenedOne.damage (:281-320) is super.damage(info), the hit
            // animation, and then THE PHASE TRANSITION -- the half-death, the two
            // hand-fired death fan-outs, the card-queue clear, the selective power
            // purge and the double setMove. It fires on the phase-1 half-death AND
            // AGAIN on the real phase-2 death; both are the Java's own behaviour
            // and the second is documented at the body.
            awakened_one_on_damaged(state, monster_index, hp_lost);
            return;
        case MonsterId::TIME_EATER:
        case MonsterId::DONU:
        case MonsterId::DECA:
            // The other three are super.damage(info) followed ONLY by the Hit
            // spine animation, gated on a non-THORNS hit with output > 0
            // (TimeEater.java:158-166, Donu.java:83-90, Deca.java:86-94). Nothing
            // there touches combat state or draws RNG, so an empty hook is the
            // COMPLETE translation and hp_lost is deliberately unread. Spelled as
            // cases so the omission is checkable.
            return;
        default:
            return;  // no damage() override
    }
}

MonsterPreBattleFn monster_pre_battle_fn(MonsterId id) noexcept {
    static_assert(sts::registry::manifest::kMonstersCount == 50,
                  "new monster: does it override usePreBattleAction? Read the "
                  "method and either register it here or add an explicit "
                  "nullptr case recording why it needs no engine behaviour.");
    switch (id) {
        // S2.23. TWO of the three city elites declare usePreBattleAction; all
        // three are spelled out rather than left to the `default:`.
        case MonsterId::GREMLIN_LEADER:
            // gremlins[0]/[1] = monsters.get(0)/get(1), gremlins[2] = null, then
            // ApplyPowerAction(m, m, MinionPower(this)) over all three -- the
            // third target is null and no-ops (GremlinLeader.java:93-101;
            // ApplyPowerAction.java:96-99), so TWO items are queued. It is also
            // where the two minions' `draw_x` is written, which is the one place
            // that can know it -- see monster_gremlin_leader.hpp note (5). No RNG.
            return &gremlin_leader_use_pre_battle_action;
        case MonsterId::BOOK_OF_STABBING:
            // ApplyPowerAction(this, this, new PainfulStabsPower(this)) --
            // self-applied at amount -1 (BookOfStabbing.java:78-81;
            // PainfulStabsPower.java:29). No RNG.
            return &book_of_stabbing_use_pre_battle_action;
        case MonsterId::TASKMASTER:
            // Taskmaster.java does not declare the method at all -- it declares
            // takeTurn, getMove, playSfx, playDeathSfx and die, and nothing else
            // -- so it inherits AbstractMonster's empty body
            // (AbstractMonster.java:953-954). Explicit nullptr, the Chosen's
            // precedent.
            return nullptr;

        // S2.22. TWO of the five declare usePreBattleAction and three do not;
        // all five are spelled out rather than left to the `default:`.
        case MonsterId::MUGGER:
            // ApplyPowerAction(this, this, ThieveryPower(this, goldAmt)) --
            // 20 at A20 (Mugger.java:81-84, :61). The same marker power the
            // Looter applies, from a separate class. No RNG.
            return &mugger_use_pre_battle_action;
        case MonsterId::SNAKE_PLANT:
            // ApplyPowerAction(this, this, new MalleablePower(this)) -- the
            // 1-ARG ctor, so amount 3 (SnakePlant.java:69-72;
            // MalleablePower.java:22,24-26). No RNG.
            return &snake_plant_use_pre_battle_action;
        case MonsterId::SNECKO:
        case MonsterId::CENTURION:
        case MonsterId::HEALER:
            // None of these three declares the method at all -- Snecko.java,
            // Centurion.java and Healer.java each declare takeTurn / changeState
            // / getMove / damage / die (plus their private sound helpers) and
            // nothing else -- so they inherit AbstractMonster's empty body
            // (AbstractMonster.java:953-954). Explicit nullptr, the Chosen's
            // precedent.
            return nullptr;
        // S2.21. THREE of the four override usePreBattleAction with real combat
        // content; the Chosen has no such method at all (Chosen.java declares
        // takeTurn, changeState, getMove, damage and die), which is why it gets
        // an explicit nullptr case below rather than the `default:`.
        case MonsterId::BYRD:
            // ApplyPowerAction(self, self, FlightPower(flightAmt)) -- 4 at A20
            // (Byrd.java:102-104,83). No RNG.
            return &byrd_use_pre_battle_action;
        case MonsterId::SHELLED_PARASITE:
            // PlatedArmor(14) THEN a direct GainBlock(14), in that order
            // (ShelledParasite.java:104-108). No RNG.
            return &shelled_parasite_use_pre_battle_action;
        case MonsterId::SPHERIC_GUARDIAN:
            // Barricade, Artifact(3), GainBlock(40), in that order
            // (SphericGuardian.java:77-82). No RNG.
            return &spheric_guardian_use_pre_battle_action;
        case MonsterId::CHOSEN:
            return nullptr;  // no usePreBattleAction in the class at all

        // S2.25. FOUR of the five override usePreBattleAction with real combat
        // content; the Repulsor has no such method at all, which is why it gets
        // an explicit nullptr case rather than the `default:`.
        case MonsterId::DARKLING:
            // The ONLY pre-battle in the roster that writes COMBAT-WIDE state:
            // `getCurrRoom().cannotLose = true` is a bare field assignment
            // (Darkling.java:96), not a queued CannotLoseAction, and it is what
            // vetoes every Darkling's die() for the rest of the fight. Then
            // ApplyPowerAction(self, self, RegrowPower) at amount 1 (:97). No RNG.
            return &darkling_use_pre_battle_action;
        case MonsterId::ORB_WALKER:
            // ApplyPowerAction(self, self, GenericStrengthUpPower(MOVES[0],
            // A17 ? 5 : 3)) -- 5 at A20 (OrbWalker.java:73-80). No RNG.
            return &orb_walker_use_pre_battle_action;
        case MonsterId::EXPLODER:
            // ApplyPowerAction(self, self, ExplosivePower(3))
            // (Exploder.java:64-67) -- the fuse, not a buff. No RNG.
            return &exploder_use_pre_battle_action;
        case MonsterId::SPIKER:
            // ApplyPowerAction(self, self, ThornsPower(A17 ? startingThorns + 3
            // : startingThorns)) -- SEVEN at A20, because the A17 arm composes
            // with the already-tiered A2 value rather than restating a literal
            // (Spiker.java:72-79, :62-68). No RNG.
            return &spiker_use_pre_battle_action;
        case MonsterId::REPULSOR:
            // Repulsor.java declares only takeTurn and getMove, so it inherits
            // AbstractMonster's empty body (AbstractMonster.java:953-954).
            // Explicit nullptr, the Chosen's precedent.
            return nullptr;

        // S2.28. All four Act-3 bosses declare usePreBattleAction with real
        // combat content, and none of the four draws any RNG.
        case MonsterId::AWAKENED_ONE:
            // The room's cannotLose latch (a DIRECT field write, :143), then four
            // addToBottom grants: Regenerate 10/15, Curiosity 1/2, Unawakened
            // (amount -1, a marker) and, from A4, Strength 2 (:144-153).
            return &awakened_one_use_pre_battle_action;
        case MonsterId::TIME_EATER:
            // ApplyPowerAction(this, this, new TimeWarpPower(this)) -- the 1-arg
            // ctor, so the counter starts at 0 (TimeEater.java:107;
            // TimeWarpPower.java:26). No ascension branch.
            return &time_eater_use_pre_battle_action;
        case MonsterId::DONU:
            // ArtifactPower(this, asc >= 19 ? 3 : 2) on ITSELF only -- no group
            // fan-out (Donu.java:93-99).
            return &donu_use_pre_battle_action;
        case MonsterId::DECA:
            // The same Artifact grant (Deca.java:97-108). Deca's method
            // additionally carries the BGM lines Donu's does not; presentation,
            // and the pair's only pre-battle asymmetry.
            return &deca_use_pre_battle_action;
        case MonsterId::LOUSE_NORMAL:
        case MonsterId::LOUSE_DEFENSIVE:
            return &louse_use_pre_battle_action;  // curl-up roll (monster_hp_rng)

        case MonsterId::SENTRY:
            return &sentry_use_pre_battle_action;  // Artifact 1 (no RNG draw)

        case MonsterId::LAGAVULIN:
            // Asleep: 8 Block + Metallicize(8) (Lagavulin.java:104-107). Awake:
            // a bare setMove(DEBUFF) (:112). No RNG on either branch.
            return &lagavulin_use_pre_battle_action;

        // TheGuardian.usePreBattleAction (TheGuardian.java:122-132) is another
        // pre-battle override with real combat content: past the BGM/ambiance
        // and UnlockTracker lines it applies ModeShiftPower(this, dmgThreshold)
        // to itself and resets the damage accumulator. No RNG.
        case MonsterId::THE_GUARDIAN:
            return &guardian_use_pre_battle_action;

        // Hexaghost.usePreBattleAction (Hexaghost.java:130-134) is entirely
        // meta-progression and presentation: UnlockTracker.markBossAsSeen
        // ("GHOST") and a BGM precache. It touches no combat state and draws no
        // RNG, so -- as with the Slime Boss below -- this nullptr is the
        // complete, correct translation. Spelled as a case rather than left to
        // the `default:` so the omission is checkable.
        case MonsterId::HEXAGHOST:
            return nullptr;

        // The two monsters below DO override usePreBattleAction; both are
        // deliberately nullptr here, and the reasons differ. They are spelled out
        // as cases rather than left to the `default:` so the omission is a
        // recorded decision that a reader can check, not an invisible hole.

        // JawWorm.usePreBattleAction (JawWorm.java:110-118) queues Strength
        // (bellowStr) + Block (bellowBlock) before turn 1, but ONLY when
        // hardMode is set -- and the `if (this.hardMode)` IS the whole body.
        //
        // THIS CASE USED TO BE nullptr WITH A NOTE FOR WHOEVER ADDED ACT 3. That
        // is S2.26, and BOTH halves the note demanded are implemented: the
        // pre-battle powers here, and the `firstMove = false` half -- the one
        // that silently shifts the opening decision -- in jaw_worm_init_hard.
        // See monster_jaw_worm.hpp for the whole argument.
        //
        // REGISTERING IT IS SAFE FOR THE ORDINARY WORM, which is the property the
        // Stage-A fixtures depend on: the body's first statement is the hardMode
        // latch test, an Exordium worm's init writes pad0 = 0, and a pre-battle
        // fn that queues nothing and draws nothing is indistinguishable from the
        // nullptr it replaces. The alternative -- keying the dispatch itself off
        // the variant -- would have needed a second MonsterId or a schema column
        // for one caller.
        case MonsterId::JAW_WORM:
            return &jaw_worm_use_pre_battle_action;

        // SlimeBoss.usePreBattleAction (SlimeBoss.java:109-117) is entirely
        // presentation and meta-progression: unsilence BGM, fade ambiance, play
        // the boss track, UnlockTracker.markBossAsSeen("SLIME"). It touches no
        // combat state and draws no RNG, so a headless simulator has nothing to
        // reproduce -- this nullptr is the complete, correct translation.
        case MonsterId::SLIME_BOSS:
            return nullptr;

        // GremlinWarrior.usePreBattleAction (GremlinWarrior.java:63-70) is the
        // real thing: addToBottom ApplyPowerAction(self, AngryPower(self,
        // ascension >= 17 ? 2 : 1)). It draws no RNG. None of the other four
        // gremlins declares the method (they inherit the empty base body), so
        // they fall through to the default below.
        case MonsterId::GREMLIN_WARRIOR:
            return &gremlin_warrior_use_pre_battle_action;

        // FungiBeast.usePreBattleAction (FungiBeast.java:75-78) is the real
        // thing: addToBottom ApplyPowerAction(self, SporeCloudPower(self, 2)),
        // the power it releases onto the player when it dies. It draws no RNG.
        // NEITHER SLAVER declares the method (both inherit the empty base body),
        // so they fall through to the default below.
        case MonsterId::FUNGI_BEAST:
            return &fungi_beast_use_pre_battle_action;

        // Looter.usePreBattleAction (Looter.java:84-86) is the real thing:
        // addToBottom ApplyPowerAction(this, this, ThieveryPower(this,
        // goldAmt)) -- the marker power the steal rides past. It draws no RNG.
        case MonsterId::LOOTER:
            return &looter_use_pre_battle_action;

        // S2.26. TWO of the four declare the method, and they are the two whose
        // whole character is set before turn 1.
        //
        // Transient.usePreBattleAction (Transient.java:65-73) applies FadingPower
        // (6 at A17+, else 5) and THEN ShiftingPower -- order is load-bearing,
        // because it is the power-list order every hook fan-out walks. Fading is
        // what kills the monster; without this the 999 HP would be a real health
        // bar. No RNG draw.
        case MonsterId::TRANSIENT:
            return &transient_use_pre_battle_action;
        // WrithingMass.usePreBattleAction (WrithingMass.java:80-83) applies
        // ReactivePower and THEN MalleablePower, again in that order. No RNG
        // draw.
        case MonsterId::WRITHING_MASS:
            return &writhing_mass_use_pre_battle_action;
        // NEITHER the Spire Growth NOR the Maw declares the method at all
        // (SpireGrowth.java has takeTurn/getMove/damage/changeState; Maw.java has
        // takeTurn/getMove/die), so both inherit AbstractMonster's empty body
        // (AbstractMonster.java:953-954). Explicit nullptr, the Chosen's
        // precedent -- an omission a reader can check rather than infer.
        case MonsterId::SPIRE_GROWTH:
        case MonsterId::MAW:
            return nullptr;

        default:
            // Checked, not assumed: outside the cases above, the only registry
            // monsters that declare the method at all are JawWorm,
            // LouseNormal, LouseDefensive, SlimeBoss, Sentry, Lagavulin,
            // GremlinWarrior, TheGuardian, Hexaghost, FungiBeast and the Looter
            // (plus S2.21/S2.22's Chosen-batch entries and S2.26's Transient and
            // Writhing Mass, all named above). The Act-1 remainder (Cultist, GremlinNob, the four
            // small/medium slimes, the two large slimes, the Thief / Fat /
            // Tsundere / Wizard gremlins, and the two slavers) inherit
            // AbstractMonster's empty body (AbstractMonster.java:953-954), so
            // there is genuinely nothing to run for them.
            //
            // The sibling hook AbstractMonster.useUniversalPreBattleAction
            // (:956-968, called from MonsterGroup.java:78) is likewise absent by
            // design: every branch in it is gated on a daily-run modifier
            // ("Lethality", "Time Dilation") or on player blights, none of which
            // exist in the A20 runs this engine simulates.
            return nullptr;
    }
}

MonsterDieFn monster_die_fn(MonsterId id) noexcept {
    static_assert(sts::registry::manifest::kMonstersCount == 50,
                  "new monster: does its Java class override die()? Read the "
                  "method. If everything before `super.die()` is presentation -- "
                  "a sound on an UNSEEDED generator, a shake, a time-scale -- it "
                  "needs no entry. If ANY of it draws a seeded stream or writes "
                  "combat state, register it here; the Mugger's seeded "
                  "playDeathSfx is why this table exists.");
    // The full survey behind the single entry below, so the emptiness is a
    // recorded reading rather than an assumption. TEN monsters in the roster
    // override die(); every one of them except the Mugger is presentation only:
    //
    //   * Looter (:159-174)      playDeathSfx on UNSEEDED MathUtils, plus the
    //                            stolen-gold return -- which is a READ of the
    //                            surviving record by the reward layer
    //                            (settle_stolen_gold, run_advance.cpp), not a
    //                            combat-time write. The Mugger's twin of that
    //                            same half is likewise handled there.
    //   * Chosen / Byrd / the two slavers / Fungi Beast / Snecko / Healer
    //                            a sound on MathUtils, and nothing else.
    //   * Centurion (:172-176)   a time-scale and a shake. No sound at all.
    //   * Slime Boss / the large slimes
    //                            their split machinery runs from damage(), not
    //                            die() (monster_slime_large.cpp).
    //
    // So the seam is real but sparse, which is expected: it exists because Acts
    // 2-4 add more monsters whose die() carries content, and because a SEEDED
    // draw at the death edge is invisible until something downstream desyncs.
    switch (id) {
        case MonsterId::MUGGER:
            // playDeathSfx' aiRng.random(2) (Mugger.java:147-154, called at
            // :158). SEEDED, unconditional, once per death.
            return &mugger_die;

        // S2.23. All three city elites override die() and NONE of them belongs
        // here; the three explicit nullptrs are readings, not omissions.
        case MonsterId::TASKMASTER:
            // `super.die(); this.playDeathSfx();` (Taskmaster.java:104-108).
            // Two things put it here rather than in the table: playDeathSfx'
            // MathUtils.random(1) (:96) is UNSEEDED libGDX -- the Looter's
            // answer, not the Mugger's -- and it runs AFTER super.die() anyway,
            // so even a seeded draw would belong in monster_die_after_fn.
            return nullptr;
        case MonsterId::BOOK_OF_STABBING:
            // `super.die(); CardCrawlGame.sound.play("STAB_BOOK_DEATH");`
            // (BookOfStabbing.java:152-156) -- a bare sound, no generator at
            // all, and again on the post-super side.
            return nullptr;
        case MonsterId::GREMLIN_LEADER:
            // die() (GremlinLeader.java:224-241) has REAL CONTENT -- the
            // EscapeAction fan-out -- but every line of it runs AFTER
            // `super.die()` (:226), and the ordering is load-bearing: the
            // fan-out's `if (m.isDying) continue;` is what excludes the leader
            // ITSELF, and it only does so because super.die() has already set
            // isDying. So the body registers in monster_die_after_fn below, and
            // this nullptr is the pre-super half being genuinely empty.
            return nullptr;
        case MonsterId::DARKLING:
            // THE FIRST VETO USER. `if (!getCurrRoom().cannotLose) super.die();`
            // (Darkling.java:239-243): while the room latch its own pre-battle
            // set is up, a Darkling at 0 HP does not die at all -- no isDying,
            // no power onDeath, no relic onMonsterDeath. Its damage() override
            // re-fires those two by hand exactly once, which is the whole reason
            // the veto exists. Draws no RNG.
            return &darkling_die;
        // The other four of S2.25 declare NO die() override at all -- OrbWalker,
        // Repulsor, Exploder and Spiker each declare (at most)
        // usePreBattleAction / takeTurn / getMove / damage / changeState and
        // nothing else -- so they stay with the `default:`. The Exploder's
        // self-kill goes through SuicideAction (triggerRelics TRUE), which runs
        // the ordinary death edge, not a die() body of its own.
        // S2.26. TWO of the four declare die() and NEITHER carries combat
        // content, so both are explicit nullptrs rather than `default:` --
        // the reading is then checkable.
        //
        // Transient.die (Transient.java:106-110) is `super.die()` followed by
        // UnlockTracker.unlockAchievement("TRANSIENT"): meta-progression, no
        // combat state, no seeded draw. Note it runs AFTER super.die(), so even
        // if it had content it would belong in monster_die_after_fn.
        //
        // Maw.die (Maw.java:138-142) is `super.die()` plus
        // CardCrawlGame.sound.play("MAW_DEATH") -- one UNSEEDED sound. This is
        // the Looter/Snecko side of the Mugger split, not the Mugger side: the
        // Mugger's playDeathSfx picks its sound with a SEEDED aiRng.random(2)
        // and therefore moves the shared stream; a bare sound.play does not.
        //
        // The Spire Growth and the Writhing Mass declare no die() at all.
        case MonsterId::TRANSIENT:
        case MonsterId::MAW:
            return nullptr;

        // S2.28.
        case MonsterId::AWAKENED_ONE:
            // NO pre-super content -- super.die() is the first statement inside
            // the guard (:357-358) -- but the GUARD is the point: the whole body
            // sits inside `if (!getCurrRoom().cannotLose)`, so while that latch is
            // set NOTHING of super.die() may run. This entry exists solely to
            // answer the VETO, which is what stops the phase-1 half-death firing
            // the two death fan-outs twice (the damage() override re-fires them by
            // hand, exactly once).
            return &awakened_one_die;
        case MonsterId::TIME_EATER:
        case MonsterId::DONU:
        case MonsterId::DECA:
            // All three DO override die(), and all three are presentation on the
            // pre-super side: TimeEater (:211-221) a shake and a rumble, Donu
            // (:134-144) and Deca (:146-156) nothing at all -- both call
            // super.die() as their FIRST statement. Their content is on the
            // POST-super side and is likewise not sim-visible; see
            // monster_die_after_fn. Explicit nullptr cases rather than the
            // `default:`, because all three classes do declare the method.
            //
            // The Time Eater's own `!cannotLose` guard can never fire: nothing in
            // a Time Eater room ever sets that latch (the Awakened One's
            // usePreBattleAction is its only producer in Act 3).
            return nullptr;
        default:
            return nullptr;  // die() is presentation only, or absent
    }
}

// The POST-`super.die()` half. S2.2F landed it EMPTY, ahead of the four Act-2/3
// batches that were about to need it. The Gremlin Leader (S2.23) is the FIRST
// entry, and of the four consumers its header names -- Reptomancer, Bronze
// Automaton, The Collector, Awakened One -- the last is S2.28's, the only one of
// that batch's four bosses whose post-super half survives the reading: the other
// three carry achievements and victory bookkeeping only. The slot exists because
// the batches that need it run in parallel and must not each invent their own.
MonsterDieAfterFn monster_die_after_fn(MonsterId id) noexcept {
    static_assert(sts::registry::manifest::kMonstersCount == 50,
                  "new monster: does its Java die() override do anything AFTER "
                  "`super.die()`? Read the method. Content before super.die() "
                  "belongs in monster_die_fn; content after it belongs here, "
                  "and the difference is load-bearing -- Reptomancer's post-super "
                  "suicide sweep skips itself only because super.die() has "
                  "already set isDying.");
    switch (id) {
        case MonsterId::GREMLIN_LEADER:
            // `super.die()`, two presentation ShoutAction loops, then
            // `for (m : monsters) if (!m.isDying) addToBottom(new
            // EscapeAction(m))` (GremlinLeader.java:224-241). The leader's own
            // record is skipped by the isDying test alone -- there is no
            // `m == this` term -- which is exactly Reptomancer's shape and
            // exactly why this is the post-super slot.
            return &gremlin_leader_die_after;
        case MonsterId::AWAKENED_ONE:
            // `for (m : getCurrRoom().monsters.monsters) { if (m.isDying ||
            //  !(m instanceof Cultist)) continue; addToBottom(EscapeAction(m)); }`
            // (AwakenedOne.java:366-369) -- every surviving Cultist FLEES when the
            // boss finally dies. Strictly post-super: super.die() runs at :358 and
            // sets isDying, which is what keeps the boss out of its own walk
            // (it is not a Cultist either, so here the filter is belt-and-braces
            // -- unlike Reptomancer's, where the ordering is the only thing
            // preventing an infinite regress).
            return &awakened_one_die_after;
        case MonsterId::TIME_EATER:
        case MonsterId::DONU:
        case MonsterId::DECA:
            // All three have post-super content and NONE of it is sim-visible.
            // TimeEater (:216-220): onBossVictoryLogic, two UnlockTracker calls,
            // onFinalBossVictoryLogic. Donu (:135-143) / Deca (:147-155): the same
            // tail, gated on getMonsters().areMonstersBasicallyDead() so it fires
            // ONCE, on the SECOND of the pair to die -- a gate recorded precisely
            // because it looks like it should matter and does not.
            // onBossVictoryLogic is achievements + StatsScreen;
            // onFinalBossVictoryLogic (AbstractMonster.java:1058-1085) is
            // achievements + stopClock, and it skips its whole body outright at
            // A20 with two bosses left. Explicit nullptr cases.
            return nullptr;
        default:
            return nullptr;  // no post-super content (or no die() at all)
    }
}

bool dispatch_monster_die(CombatState& state, uint8_t monster_index) noexcept {
    if (monster_index >= kMonsterCap) {
        return false;
    }
    const MonsterId id =
        static_cast<MonsterId>(state.monsters[monster_index].monster_id);
    const MonsterDieFn fn = monster_die_fn(id);
    if (fn == nullptr) {
        return false;  // presentation-only die(), or none: never a veto
    }
    return fn(state, monster_index);
}

void dispatch_monster_die_after(CombatState& state,
                                uint8_t monster_index) noexcept {
    if (monster_index >= kMonsterCap) {
        return;
    }
    const MonsterId id =
        static_cast<MonsterId>(state.monsters[monster_index].monster_id);
    const MonsterDieAfterFn fn = monster_die_after_fn(id);
    if (fn != nullptr) {
        fn(state, monster_index);
    }
}

void dispatch_monster_turn(CombatState& state, uint8_t monster_index) noexcept {
    const MonsterId id =
        static_cast<MonsterId>(state.monsters[monster_index].monster_id);
    const MonsterTurnFn fn = monster_turn_fn(id);
    fn(state, monster_index);
}

namespace {

// THE CONSTRUCT-ALL-THEN-INIT-ALL PLACEHOLDER.
//
// The game builds a group in two passes: the encounter's `new MonsterGroup(new
// AbstractMonster[]{ ... })` constructs EVERY member (MonsterGroup.java:31-33),
// and MonsterRoom then calls `monsters.init()`, which loops the finished list
// calling each member's init() (:62-66 -> AbstractMonster.init, :705-715). This
// engine folds ctor and init into ONE MonsterInitFn per monster, which is
// stream-equivalent -- monster_hp_rng still sees the HP rolls in spawn order and
// ai_rng the rollMoves in spawn order -- but NOT state-equivalent for a getMove
// that reads the GROUP rather than itself.
//
// Two S2.22 monsters do exactly that, and both are in the same encounter:
// the Centurion's aliveCount (Centurion.java:134-138) and the Healer's
// needToHeal (Healer.java:157-160). Without this pre-pass the Centurion at slot 0
// would roll its opening move against a still-zeroed slot 1, see aliveCount == 1,
// and be unable to open on PROTECT -- a wrong turn-1 telegraph in the live
// "Centurion and Healer" group.
//
// The fix is to give every slot the state a freshly CONSTRUCTED monster has
// before the init pass runs: present, alive, and at full health. hp == max_hp == 1
// is enough for both readers (alive_count counts it; need_to_heal adds
// max_hp - hp == 0) and is fully overwritten by that slot's own init, which
// assigns every field of the record. Nothing else in the engine reads another
// monster's record during init, so no existing monster's behaviour moves: the
// twenty combat fixtures and every Act-1 spawn test are byte-unchanged.
//
// The real HP is NOT knowable here -- it is the init's own monster_hp_rng draw,
// and drawing it early would reorder the stream. A placeholder is therefore the
// exact available model, not a shortcut: the two readers ask only "is it there,
// and is it whole", and at construction time the answer is yes to both.
void mark_group_constructed(CombatState& state, uint8_t count) noexcept {
    for (uint8_t i = 0; i < count && i < kMonsterCap; ++i) {
        MonsterState& m = state.monsters[i];
        m = MonsterState{};
        m.hp = 1;
        m.max_hp = 1;
    }
}

}  // namespace

void spawn_group(CombatState& state, std::span<const MonsterId> group) noexcept {
    assert(group.size() <= static_cast<std::size_t>(kMonsterCap) &&
           "spawn_group: group exceeds kMonsterCap");
    state.monster_count = static_cast<uint8_t>(group.size());
    mark_group_constructed(state, state.monster_count);
    for (uint8_t i = 0; i < group.size(); ++i) {
        const MonsterInitFn init = monster_init_fn(group[i]);
        assert(init != nullptr &&
               "spawn_group: no init fn for this id -- every registry monster "
               "has one, so this is MonsterId::NONE or a corrupt id");
        init(state, i);
    }
}

void burn_unspawned_ctor_rolls(CombatState& state, MonsterId id) noexcept {
    const sts::registry::MonsterDef* def = sts::registry::monster_def(id);
    assert(def != nullptr &&
           "burn_unspawned_ctor_rolls: no registry def for a constructed "
           "candidate -- the composition named an unknown monster");
    if (def == nullptr) {
        return;
    }
    // THREE PHASES, IN THE JAVA'S ORDER. This is a two-pass walk around the
    // setHp draw rather than one filtered loop, because the roll timings are
    // ordered and the stream position is the entire product of this function.
    //
    // A CONSTRUCTOR_BEFORE_HP roll lives in the `super(...)` ARGUMENT LIST,
    // which Java evaluates before the constructor body -- so it draws BEFORE
    // setHp does (OrbWalker.java:53-58). Filtering it in with the AFTER rolls
    // would burn the same NUMBER of draws in the WRONG ORDER, which is
    // invisible in a solo encounter and wrong in every group one.
    const auto burn = [&](sts::registry::MonsterRollTiming when) {
        for (uint8_t i = 0; i < def->roll_count; ++i) {
            const sts::registry::MonsterRollDef& r = def->rolls[i];
            if (r.timing == when &&
                r.stream == sts::registry::MonsterRollStream::MONSTER_HP) {
                (void)random(state.monster_hp_rng, r.min(kMonsterAscension),
                             r.max(kMonsterAscension));
            }
        }
    };
    // 1. super-argument draws, ahead of the ctor body.
    burn(sts::registry::MonsterRollTiming::CONSTRUCTOR_BEFORE_HP);
    // 2. setHp: one inclusive draw over the A7 column at the skeleton A20 --
    //    identical bounds to the roll the candidate would have kept.
    (void)random(state.monster_hp_rng, def->hp_min(kMonsterAscension),
                 def->hp_max(kMonsterAscension));
    // 3. constructor-body extras on the same stream (Louse biteDamage,
    //    LouseNormal.java:60 / LouseDefensive.java:63). PRE_BATTLE rolls are not
    //    ctor draws and never fire for a discarded candidate.
    burn(sts::registry::MonsterRollTiming::CONSTRUCTOR_AFTER_HP);
}

void spawn_group_trace(CombatState& state,
                       std::span<const MonsterId> constructed,
                       uint16_t kept_mask) noexcept {
    state.monster_count = 0;
    // Same construct-all-then-init-all pre-pass as spawn_group (see
    // mark_group_constructed): the KEPT members are the group the game built, and
    // they occupy slots [0, kept). A discarded PICK candidate was constructed and
    // dropped before the group existed, so it is never in that list and never
    // gets a slot -- only its ctor draws are burned.
    uint8_t kept = 0;
    for (std::size_t i = 0; i < constructed.size(); ++i) {
        if ((kept_mask & (1u << i)) != 0u) {
            ++kept;
        }
    }
    mark_group_constructed(state, kept);
    for (std::size_t i = 0; i < constructed.size(); ++i) {
        if ((kept_mask & (1u << i)) != 0u) {
            assert(state.monster_count < kMonsterCap &&
                   "spawn_group_trace: kept members exceed kMonsterCap");
            const MonsterInitFn init = monster_init_fn(constructed[i]);
            assert(init != nullptr &&
                   "spawn_group_trace: no init fn for a kept member");
            const uint8_t slot = state.monster_count++;
            init(state, slot);
        } else {
            burn_unspawned_ctor_rolls(state, constructed[i]);
        }
    }
}

void use_pre_battle_actions(CombatState& state) noexcept {
    // preBattlePrep runs usePreBattleAction over the group in spawn order
    // (MonsterRoom.onPlayerEntry -> player.preBattlePrep, AbstractPlayer.java:1602).
    // monster_hp_rng is thus consumed in two phases (ctor HP rolls, then curl-ups),
    // both in spawn order -- the stream sees the concatenation.
    for (uint8_t i = 0; i < state.monster_count; ++i) {
        const MonsterId id =
            static_cast<MonsterId>(state.monsters[i].monster_id);
        const MonsterPreBattleFn fn = monster_pre_battle_fn(id);
        if (fn != nullptr) {
            fn(state, i);
        }
    }
}

}  // namespace sts::engine
