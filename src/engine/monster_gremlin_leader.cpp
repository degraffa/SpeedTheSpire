// The Gremlin Leader: native move selection (with the recursive re-draw), the
// SummonGremlinAction machinery, the asymmetric ENCOURAGE walk and the
// post-super escape fan-out. See monster_gremlin_leader.hpp for provenance and
// the seven readings the body leans on.

#include "sts/engine/monster_gremlin_leader.hpp"

#include <cassert>

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem, kActorPlayer
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags, make_spawn_monster_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effect(s), move helpers, kMinionAppliedAmount
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kRally = sts::registry::kGremlinLeaderMoveRally;          // 2
constexpr uint8_t kEncourage = sts::registry::kGremlinLeaderMoveEncourage;  // 3
constexpr uint8_t kStab = sts::registry::kGremlinLeaderMoveStab;            // 4

// ENCOURAGE's two template steps (monsters.yaml id 37): 0 is the Strength the
// walk gives EVERY live member including the leader, 1 the block it gives
// everyone BUT the leader. Named rather than bare indices so the coupling to the
// row is visible from here (the Healer's kTemplateStep precedent).
constexpr uint8_t kEncourageStrengthStep = 0;
constexpr uint8_t kEncourageBlockStep = 1;

// getRandomGremlin's key pool (SummonGremlinAction.java:59-67), rebuilt fresh on
// every call and drawn WITH replacement: one `aiRng.random(0, 7)`, no removal.
// Identical MEMBERSHIP to MonsterHelper.spawnGremlin's (MonsterHelper.java:
// 768-777) and a DIFFERENT STREAM -- see header note (4). The duplicate Warrior/
// Thief/Fat entries are the weighting and are reproduced literally.
constexpr MonsterId kSummonPool[8] = {
    MonsterId::GREMLIN_WARRIOR, MonsterId::GREMLIN_WARRIOR,
    MonsterId::GREMLIN_THIEF,   MonsterId::GREMLIN_THIEF,
    MonsterId::GREMLIN_FAT,     MonsterId::GREMLIN_FAT,
    MonsterId::GREMLIN_TSUNDERE, MonsterId::GREMLIN_WIZARD,
};

// The HP range a summoned gremlin's constructor rolls over -- the registry row's
// own column, so the summon path and the encounter path cannot drift apart.
[[nodiscard]] const sts::registry::MonsterDef& summon_def(MonsterId id) noexcept {
    const sts::registry::MonsterDef* def = sts::registry::monster_def(id);
    assert(def != nullptr && "summon pool named a monster with no registry row");
    return *def;
}

void queue_roll_move(CombatState& s, uint8_t src, uint8_t tgt) noexcept {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    it.src = src;
    it.tgt = tgt;
    add_to_bottom(s, it);
}

// The RALLY body (:106-110). ChangeStateAction("CALL") (:107) is presentation and
// rolls nothing; the two SummonGremlinActions are everything.
//
// WHY BOTH SUMMONS ARE FULLY PLANNED HERE, AT QUEUE TIME. Three of the four
// things SummonGremlinAction does really are constructor work -- the slot pick,
// the aiRng key draw and the gremlin ctor's monster_hp_rng draw all happen at
// `addToBottom` (:33-46) -- so they MUST be here or the stream is wrong. The
// fourth, `getSmartPosition()` (:92-99), is genuinely resolve-time work, and it
// is planned here anyway because the answer is already determined:
//   * the walk reads only `drawX`, which no action mutates;
//   * dead records stay in the list and keep their drawX, so a death between
//     queue and resolve changes nothing;
//   * the only records that can be INSERTED between these two spawns are these
//     two spawns, which is exactly what the local simulation below models.
// A monster's turn is reached only with the action, pre-turn and card queues
// empty (action_queue.cpp step 5), so nothing else can run in the window.
//
// The same simulation is what gives the leader's own post-insertion index for
// the trailing RollMoveAction: pending action_queue items are NOT remapped
// across a spawn (monster_dispatch.hpp), which is the hazard
// monster_slime_large.cpp pre-computes around for the identical reason.
void queue_rally(CombatState& s, uint8_t mi) noexcept {
    // The list of position keys as the group will look at each spawn's resolve.
    // Two extra entries because this turn inserts two records.
    int16_t xs[kMonsterCap + 2] = {};
    uint8_t n = s.monster_count;
    for (uint8_t i = 0; i < n && i < kMonsterCap; ++i) {
        xs[i] = s.monsters[i].draw_x;
    }
    uint8_t leader_index = mi;

    // identifySlot's view of `gremlins[]`, derived from the live records --
    // header note (5). A slot claimed by the FIRST summon is claimed for the
    // second one too, because the Java's ctor writes `gremlins[slot] = this.m`
    // (:42) before the second action is ever constructed.
    bool slot_taken[3] = {false, false, false};
    for (uint8_t k = 0; k < 3; ++k) {
        for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
            // A slot's occupant sits at POSX[k] -- or POSX[k] - 35 when the
            // occupant is a Gremlin Wizard, whose ctor passes `x - 35.0f` up
            // (GremlinWizard.java:48); see kGremlinWizardXOffset.
            if (s.monsters[i].hp > 0 &&
                (s.monsters[i].draw_x == kGremlinLeaderSlotX[k] ||
                 s.monsters[i].draw_x ==
                     kGremlinLeaderSlotX[k] - kGremlinWizardXOffset)) {
                slot_taken[k] = true;
                break;
            }
        }
    }

    for (int summon = 0; summon < 2; ++summon) {
        // identifySlot (:48-54): the first null-or-dying entry. The `-1` return
        // (:53) is UNREACHABLE and deliberately has no body: RALLY is only ever
        // chosen when numAliveGremlins() < 2 (getMove's first two arms), so at
        // most one of the three slots can hold a live gremlin when the first
        // summon is constructed and at most two when the second is. Asserted
        // rather than handled, because the Java's own -1 arm logs and then NPEs
        // in update().
        uint8_t slot = 3;
        for (uint8_t k = 0; k < 3; ++k) {
            if (!slot_taken[k]) {
                slot = k;
                break;
            }
        }
        assert(slot < 3 &&
               "SummonGremlinAction.identifySlot returned -1: RALLY was chosen "
               "with all three gremlin slots live, which getMove cannot do");
        if (slot >= 3) {
            return;
        }
        slot_taken[slot] = true;

        // getRandomGremlin (:56-90): ONE aiRng draw over the 8-entry pool.
        const int32_t pick = random(s.ai_rng, 0, 7);
        const MonsterId id = kSummonPool[pick];
        // MonsterHelper.getGremlin -> the gremlin's full ctor -> setHp: ONE
        // monster_hp_rng inclusive draw over that gremlin's own A7 column
        // (AbstractMonster.java:765-775). At QUEUE time, because the ctor runs
        // inside SummonGremlinAction's ctor.
        const sts::registry::MonsterDef& def = summon_def(id);
        const int32_t hp = random(s.monster_hp_rng, def.hp_min(kMonsterAscension),
                                  def.hp_max(kMonsterAscension));
        // The position KEY is species-dependent: GremlinWizard's ctor passes
        // `x - 35.0f` to super (GremlinWizard.java:48; the other four gremlins
        // pass x unmodified), and getSmartPosition sorts by drawX -- so a
        // Wizard summoned into a slot sits strictly LEFT of every other
        // occupant of that slot, and a later same-slot summon inserts AFTER a
        // dead Wizard record instead of tying with it. Missing this offset
        // rotated the sim's monster list from rally 2 on and made every
        // positional replay target hit the wrong minion (seed STS431071, the
        // S2.43 triage). draw_x is offsetX-units and the scan is monotone, so
        // the integer -35 is exact; the shifted values collide with no slot
        // key and not the leader's +35.
        int16_t x = kGremlinLeaderSlotX[slot];
        if (id == MonsterId::GREMLIN_WIZARD) {
            x = static_cast<int16_t>(x - kGremlinWizardXOffset);
        }

        // getSmartPosition (:92-99): walk the list and BREAK at the first record
        // the newcomer is not strictly right of. Identical to
        // smart_position_for, run against the simulated list.
        uint8_t pos = 0;
        while (pos < n && x > xs[pos]) {
            ++pos;
        }
        for (uint8_t i = n; i > pos; --i) {
            xs[i] = xs[i - 1];
        }
        xs[pos] = x;
        ++n;
        if (pos <= leader_index) {
            ++leader_index;  // the insert shifted the leader one to the right
        }

        ActionQueueItem spawn{};
        spawn.opcode = static_cast<uint16_t>(Opcode::SPAWN_MONSTER);
        spawn.src = mi;
        spawn.tgt = pos;
        spawn.amount = hp;
        // run_pre_battle: SummonGremlinAction.update calls m.usePreBattleAction()
        // at isDone (:120) -- a summoned Warrior's Angry. apply_minion: the
        // action's own addToBot(ApplyPowerAction(m, m, MinionPower(m))) (:114),
        // which the Java queues FIRST, so the spawn path queues it first too.
        spawn.flags = make_spawn_monster_flags(static_cast<uint16_t>(id), x,
                                               /*run_pre_battle=*/true,
                                               /*apply_minion=*/true);
        add_to_bottom(s, spawn);
    }

    queue_roll_move(s, mi, leader_index);
}

// The ENCOURAGE body (:112-123). ShoutAction's argument is
// getEncourageQuote() (:136-142), which is `aiRng.random(0, 2)` over a
// three-element list -- a SEEDED draw taken at QUEUE time because Java evaluates
// the argument before addToBottom. The quote itself is presentation; the DRAW is
// not (the Mugger playDeathSfx precedent), so it is taken and discarded here,
// BEFORE the walk queues anything.
void queue_encourage(CombatState& s, uint8_t mi) noexcept {
    (void)random(s.ai_rng, 0, 2);  // getEncourageQuote's list index (:141)

    // The walk (:114-122), in group order, with the asymmetry intact:
    //     if (m == this) { ApplyPower(Strength); continue; }   // no block, and
    //                                                          // NO isDying guard
    //     if (m.isDying) continue;
    //     ApplyPower(Strength); GainBlock(blockAmt);
    // Liveness is read HERE, at queue time, exactly as the Java's loop does.
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        if (i == mi) {
            queue_monster_move_effect(s, mi, sts::registry::kGremlinLeader,
                                      kEncourage, kEncourageStrengthStep, i);
            continue;
        }
        if (s.monsters[i].hp <= 0) {
            continue;  // `if (m.isDying) continue;`
        }
        queue_monster_move_effect(s, mi, sts::registry::kGremlinLeader,
                                  kEncourage, kEncourageStrengthStep, i);
        queue_monster_move_effect(s, mi, sts::registry::kGremlinLeader,
                                  kEncourage, kEncourageBlockStep, i);
    }
}

}  // namespace

int32_t gremlin_leader_num_alive_gremlins(const CombatState& s,
                                          uint8_t mi) noexcept {
    // numAliveGremlins (:191-198): `m == null || m == this || m.isDying` skips.
    // NOT restricted to gremlins[], and NOT excluding escaped records -- header
    // note (2).
    int32_t count = 0;
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        if (i == mi || s.monsters[i].hp <= 0) {
            continue;
        }
        ++count;
    }
    return count;
}

void gremlin_leader_decide_move(CombatState& s, uint8_t mi,
                                int32_t num) noexcept {
    MonsterState& m = s.monsters[mi];
    // getMove (:144-189). The Java's two tail-recursive calls are the two
    // `continue`s below; each re-draws ai_rng and re-enters the whole function,
    // including re-reading numAliveGremlins (header note (1)).
    for (;;) {
        const int32_t alive = gremlin_leader_num_alive_gremlins(s, mi);
        if (alive == 0) {
            // (:146-157). No recursion on this arm.
            if (num < 75) {
                if (!last_move_is(m, kRally)) {
                    set_monster_move(m, kRally, MonsterIntent::UNKNOWN);
                } else {
                    set_monster_move(m, kStab, MonsterIntent::ATTACK);
                }
            } else if (!last_move_is(m, kStab)) {
                set_monster_move(m, kStab, MonsterIntent::ATTACK);
            } else {
                set_monster_move(m, kRally, MonsterIntent::UNKNOWN);
            }
            return;
        }
        if (alive < 2) {
            // (:158-175). BOTH recursive arms live here.
            if (num < 50) {
                if (!last_move_is(m, kRally)) {
                    set_monster_move(m, kRally, MonsterIntent::UNKNOWN);
                    return;
                }
                num = random(s.ai_rng, 50, 99);  // getMove(random(50, 99)) (:163)
                continue;
            }
            if (num < 80) {
                if (!last_move_is(m, kEncourage)) {
                    set_monster_move(m, kEncourage, MonsterIntent::DEFEND_BUFF);
                } else {
                    set_monster_move(m, kStab, MonsterIntent::ATTACK);
                }
                return;
            }
            if (!last_move_is(m, kStab)) {
                set_monster_move(m, kStab, MonsterIntent::ATTACK);
                return;
            }
            num = random(s.ai_rng, 0, 80);  // getMove(random(0, 80)) (:174)
            continue;
        }
        // alive > 1 (:176-188). The Java writes this as a third `else if
        // (numAliveGremlins() > 1)` with no trailing else, so a hypothetical
        // fourth case would leave nextMove UNCHANGED -- but the three tests
        // == 0 / < 2 / > 1 are exhaustive over the non-negative integers, so
        // there is nothing to reproduce. No recursion on this arm.
        if (num < 66) {
            if (!last_move_is(m, kEncourage)) {
                set_monster_move(m, kEncourage, MonsterIntent::DEFEND_BUFF);
            } else {
                set_monster_move(m, kStab, MonsterIntent::ATTACK);
            }
        } else if (!last_move_is(m, kStab)) {
            set_monster_move(m, kStab, MonsterIntent::ATTACK);
        } else {
            set_monster_move(m, kEncourage, MonsterIntent::DEFEND_BUFF);
        }
        return;
    }
}

void gremlin_leader_init(CombatState& s, uint8_t mi) noexcept {
    // The ctor's `super(...)` HP argument is a LITERAL 148 -- no draw -- and the
    // setHp chain (:73-77) is the ONE monster_hp_rng draw, over the a8 column at
    // the fixed A20. Contrast the Taskmaster, whose super-argument DOES draw.
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::GREMLIN_LEADER);
    const int32_t hp =
        random(s.monster_hp_rng,
               sts::registry::kGremlinLeader.hp_min(kMonsterAscension),
               sts::registry::kGremlinLeader.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.draw_x = kGremlinLeaderDrawX;  // the ctor's offsetX (:66)
    // init() -> rollMove -> getMove(aiRng.random(99)). getMove READS num on every
    // arm, so the opening move is seed-dependent. At init both encounter minions
    // are alive (spawn_group_trace pre-marks every slot of the group as
    // constructed and alive before running any init, monster_dispatch.cpp), so
    // numAliveGremlins is 2 and the `> 1` arm decides -- ENCOURAGE below 66,
    // STAB at or above it, with the history guards vacuously true.
    gremlin_leader_decide_move(s, mi, random(s.ai_rng, 99));
}

void gremlin_leader_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (:93-101):
    //     gremlins[0] = monsters.get(0); gremlins[1] = monsters.get(1);
    //     gremlins[2] = null;
    //     for (m : gremlins) addToBottom(ApplyPowerAction(m, m, MinionPower(this)));
    //
    // The list indices 0 and 1 are HARD-CODED in the Java and positional here for
    // the same reason: MonsterHelper builds the leader THIRD (:507-509), so the
    // two minions are always slots 0 and 1.
    //
    // This is also where the two minions' `draw_x` is written, and this is the
    // only place it can be. The positions are an ENCOUNTER property -- the
    // encounter passes GremlinLeader.POSX[0] and POSX[1] to spawnGremlin -- not a
    // property of the gremlin type, so the shared gremlin init (used by the
    // Gremlin Gang at entirely different coordinates) cannot know them. The Java
    // licenses exactly these two indices here and nowhere else, which is why the
    // write rides along with the Minion loop instead of being invented somewhere
    // more general.
    for (uint8_t k = 0; k < 2; ++k) {
        if (k >= s.monster_count || k >= kMonsterCap) {
            break;
        }
        // An encounter-rolled Wizard minion routes through the same ctor and
        // carries the same -35 x offset (GremlinWizard.java:48;
        // MonsterHelper.java:507-509) -- the slot key discipline queue_rally
        // scans by.
        s.monsters[k].draw_x =
            s.monsters[k].monster_id ==
                    static_cast<uint16_t>(MonsterId::GREMLIN_WIZARD)
                ? static_cast<int16_t>(kGremlinLeaderSlotX[k] -
                                       kGremlinWizardXOffset)
                : kGremlinLeaderSlotX[k];
        ActionQueueItem minion{};
        minion.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        minion.src = k;  // ApplyPowerAction(m, m, ...) -- source IS the target
        minion.tgt = k;
        minion.amount = kMinionAppliedAmount;  // -1, never assigned by the ctor
        minion.flags = make_apply_power_flags(PowerId::MINION);
        add_to_bottom(s, minion);
    }
    // The THIRD iteration is `m == null`, and ApplyPowerAction.update's opening
    // `if (target == null || target.isDeadOrEscaped()) { isDone = true; return; }`
    // (ApplyPowerAction.java:96-99) makes it a NO-OP rather than a crash. Two
    // items are queued, not three, and that is a reading of the null path --
    // header note (6) -- not an off-by-one.
}

void gremlin_leader_roll_move(CombatState& s, uint8_t mi) noexcept {
    gremlin_leader_decide_move(s, mi, random(s.ai_rng, 99));
}

void gremlin_leader_take_turn(CombatState& s, uint8_t mi) noexcept {
    const uint8_t move = s.monsters[mi].move_history[0];
    if (move == kRally) {
        // queue_rally queues its own trailing ROLL_MOVE, because only it knows
        // the leader's post-insertion index.
        queue_rally(s, mi);
        return;
    }
    if (move == kEncourage) {
        queue_encourage(s, mi);
    } else {
        // STAB (:125-131): three separate DamageActions on damage.get(0), the
        // registry program unchanged. ChangeState/Wait are presentation.
        queue_monster_move_effects(s, mi, sts::registry::kGremlinLeader, kStab);
    }
    // The RollMoveAction at :133 sits OUTSIDE the switch. Neither of these two
    // moves inserts a record, so the leader's index is still mi.
    queue_roll_move(s, mi, mi);
}

void gremlin_leader_die_after(CombatState& s, uint8_t mi) noexcept {
    // die() AFTER super.die() (:224-241). The two ShoutAction loops (:228-236)
    // are presentation and draw nothing; the second loop is the content:
    //     for (m : getCurrRoom().monsters.monsters) {
    //         if (m.isDying) continue;
    //         addToBottom(new EscapeAction(m));
    //     }
    // `super.die()` has already zeroed this leader's HP, so the isDying test
    // excludes it without a `m == this` term -- which is exactly why this body
    // is on the post-super side (header note (7)).
    (void)mi;
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        if (s.monsters[i].hp <= 0) {
            continue;
        }
        ActionQueueItem esc{};
        esc.opcode = static_cast<uint16_t>(Opcode::ESCAPE);
        esc.src = i;
        esc.tgt = i;
        add_to_bottom(s, esc);
    }
}

}  // namespace sts::engine
