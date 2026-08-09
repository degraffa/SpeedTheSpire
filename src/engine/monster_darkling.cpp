// The Darkling: a recursive, slot-parity-reading move tree and the engine's
// first half-death / revival machine. See monster_darkling.hpp for the shape and
// for the five details worth reading before this file.

#include "sts/engine/monster_darkling.hpp"

#include <cassert>

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem, kActorPlayer
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/knowledge.hpp"         // knowledge_reveal_monster_roll
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effect(s), history helpers
#include "sts/engine/power_hooks.hpp"       // dispatch_on_death
#include "sts/engine/relic_hooks.hpp"       // player_relics, dispatch_relics_on_monster_death
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kChomp = sts::registry::kDarklingMoveChomp;              // 1
constexpr uint8_t kHarden = sts::registry::kDarklingMoveHarden;            // 2
constexpr uint8_t kNip = sts::registry::kDarklingMoveNip;                  // 3
constexpr uint8_t kCount = sts::registry::kDarklingMoveCount;              // 4
constexpr uint8_t kReincarnate = sts::registry::kDarklingMoveReincarnate;  // 5
constexpr uint8_t kNipRoll = sts::registry::kDarklingRollNipDamage;

// HARDEN's telegraph is ascension-switched: DEFEND_BUFF at A17+ (Darkling.java:
// 152,171), plain DEFEND below (:154,173). The registry's intent column carries
// the A20 value; this is the branch, so the module stays exact if a real
// ascension is ever threaded through kMonsterAscension.
[[nodiscard]] constexpr MonsterIntent harden_intent() noexcept {
    return kMonsterAscension >= 17 ? MonsterIntent::DEFEND_BUFF
                                   : MonsterIntent::DEFEND;
}

// getMove (Darkling.java:143-183), MINUS the firstMove branch (:149-161), which
// fires only on init()'s rollMove and is handled there -- the Snecko / Chosen /
// Red-Slaver precedent.
//
// `num` is the value the caller drew. THE RECURSION IS REAL RECURSION: both
// re-entries draw a FRESH aiRng value with their own bounds, and the whole point
// is that the number of draws a single decision costs is input-dependent.
// Termination: a 40..99 re-entry cannot reach the `num < 40` arm at all, and a
// 0..99 re-entry that lands there can spend at most one more 40..99 draw, so
// only the `!lastTwoMoves(NIP)` failure can chain -- with probability 0.3 per
// level, exactly as the Java does. No bound is imposed, because imposing one
// would be a different program.
void darkling_get_move(CombatState& s, uint8_t mi, int32_t num) noexcept {
    MonsterState& m = s.monsters[mi];
    if (monster_half_dead(m)) {
        // (:145-148) -- checked BEFORE firstMove, so a Darkling that half-dies
        // before ever acting still telegraphs REINCARNATE.
        set_monster_move(m, kReincarnate, MonsterIntent::BUFF);
        return;
    }
    if (num < 40) {
        // (:163) `!lastMove(CHOMP) && monsters.lastIndexOf(this) % 2 == 0`. The
        // second conjunct is the SLOT PARITY: `mi` IS lastIndexOf(this), because
        // the group list is never compacted (MonsterGroup.java:35-45) and this
        // engine keeps dead records in place for exactly that reason.
        if (!last_move_is(m, kChomp) && (mi % 2u) == 0u) {
            set_monster_move(m, kChomp, MonsterIntent::ATTACK);  // (:164)
        } else {
            darkling_get_move(s, mi, random(s.ai_rng, 40, 99));  // (:166)
        }
        return;
    }
    if (num < 70) {
        if (!last_move_is(m, kHarden)) {
            set_monster_move(m, kHarden, harden_intent());  // (:169-174)
        } else {
            set_monster_move(m, kNip, MonsterIntent::ATTACK);  // (:176)
        }
        return;
    }
    if (!last_two_moves_are(m, kNip)) {
        set_monster_move(m, kNip, MonsterIntent::ATTACK);  // (:179)
        return;
    }
    darkling_get_move(s, mi, random(s.ai_rng, 0, 99));  // (:181)
}

// `AbstractMonster.die()` for ONE member of the all-dead sweep at
// Darkling.java:228-230, minus presentation.
//
// The Java body (AbstractMonster.java:925-950) is, in order: an `if (!isDying)`
// guard; `isDying = true`; the power onDeath walk, gated on `currentHealth <= 0`;
// the relic onMonsterDeath walk, ungated; the limbo exhaust; a clamp of NEGATIVE
// health to 0. The sweep reaches it through each member's own die() override, so
// the veto is consulted per member -- which is why this calls the dispatch pair
// rather than the fan-outs directly.
//
// THE ONE PLACE THIS ENGINE CANNOT BE LITERAL. `isDying` has no storage here: it
// is MODELLED as `hp <= 0 && !halfDead` (combat_state.hpp). So "set isDying" has
// to be expressed as clearing halfDead on a member already at 0 HP -- which is
// what the sweep does, and which is also what makes the fight actually end
// (`any_monster_alive` reads monster_basically_dead, and a half-dead record
// counts as alive by design). For a member with POSITIVE HP the Java would set
// isDying and leave the HP alone; that state is not representable, so the HP is
// zeroed, which is the same thing to every consumer this engine has. That case
// is UNREACHABLE in landed content -- "3 Darklings" is the only Darkling group
// and the all-dead test (:214-217) ignores non-Darklings entirely -- and it is
// written correctly rather than left as a hole.
void sweep_die(CombatState& s, uint8_t i) noexcept {
    MonsterState& o = s.monsters[i];
    if (o.hp <= 0 && !monster_half_dead(o)) {
        return;  // `if (!this.isDying)` -- already dying, die() is a no-op
    }
    const bool at_zero = o.hp <= 0;  // gates the POWER walk (:928)
    o.flags &= ~kMonsterFlagHalfDead;
    o.hp = 0;                        // isDying, in this engine's model
    if (dispatch_monster_die(s, i)) {
        return;  // this member's own die() vetoed super.die()
    }
    if (at_zero) {
        dispatch_on_death(s, i);
    }
    const RelicView rv = player_relics(s);
    dispatch_relics_on_monster_death(s, rv.relics, rv.count, i);
    dispatch_monster_die_after(s, i);
}

}  // namespace

void darkling_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::DARKLING);

    // (1) setHp (Darkling.java:77-81), the A7 column at kMonsterAscension. The
    // `super(..., 56, ...)` argument (:72) is a literal and costs no draw.
    const int32_t rolled = random(s.monster_hp_rng,
                                  sts::registry::kDarkling.hp_min(kMonsterAscension),
                                  sts::registry::kDarkling.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = static_cast<int16_t>(rolled);

    // (2) nipDmg: a SECOND monster_hp_rng draw, inside the ascension >= 2 branch
    // (:84,87) and therefore AFTER setHp -- the Louse biteDamage shape, with the
    // ranges and the CONSTRUCTOR_AFTER_HP timing as registry data. Note the two
    // branches are gated differently: HP on >= 7, this on >= 2.
    const sts::registry::MonsterRollDef* nip_roll =
        sts::registry::kDarkling.roll(kNipRoll);
    assert(nip_roll != nullptr);
    assert(nip_roll->stream == sts::registry::MonsterRollStream::MONSTER_HP);
    assert(nip_roll->timing ==
           sts::registry::MonsterRollTiming::CONSTRUCTOR_AFTER_HP);
    const int32_t nip = random(s.monster_hp_rng, nip_roll->min(kMonsterAscension),
                               nip_roll->max(kMonsterAscension));

    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = static_cast<uint8_t>(nip);
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;

    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). THIS is the
    // firstMove branch (Darkling.java:149-161) and the only place it runs: the
    // flag is consumed here and the Java never re-sets it, so a revived Darkling
    // does NOT get a second opening move.
    const int32_t num = random(s.ai_rng, 99);
    if (num < 50) {
        set_monster_move(m, kHarden, harden_intent());  // (:151-155)
    } else {
        set_monster_move(m, kNip, MonsterIntent::ATTACK);  // (:157)
    }

    // Knowledge observer (knowledge.hpp): a NIP telegraph displays the
    // per-instance rolled damage on the intent banner -- setMove passes
    // damage.get(1).base (:157) -- which reveals the construction roll. The
    // Louse BITE precedent.
    if (m.move_history[0] == kNip) {
        knowledge_reveal_monster_roll(s, mi, m.pad0);
    }
}

void darkling_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (Darkling.java:94-98). The cannotLose latch is a BARE
    // FIELD ASSIGNMENT at :96, not a queued CannotLoseAction, so it takes effect
    // immediately -- before anything else in preBattlePrep and before the first
    // card is played. The CANNOT_LOSE / CAN_LOSE opcodes (25/26) exist for the
    // slime split and are deliberately NOT used here.
    s.flags |= kCombatFlagCannotLose;

    // addToBottom ApplyPowerAction(this, this, new RegrowPower(this)) (:97).
    // 3-arg, so the stack amount is powerToApply.amount -- 1. Regrow is a pure
    // marker with no hooks at all (powers.yaml id 99); the revival lives in this
    // file, not in the power.
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;
    apply.tgt = mi;
    apply.amount = kDarklingRegrowAmount;
    apply.flags = make_apply_power_flags(PowerId::REGROW);
    add_to_bottom(s, apply);
}

void darkling_roll_move(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];

    // ChangeStateAction("REVIVE") (Darkling.java:132 -> :193-196): `halfDead =
    // false`. It is queued between the REINCARNATE HealAction and the trailing
    // RollMoveAction, and this is where its effect lands.
    //
    // WHY IT CAN SIT HERE, spelled out because "fold a queued state change into
    // the next queued item" is only safe when nothing observes it in between.
    // The REINCARNATE turn queues exactly [HEAL, ChangeState, ApplyPower(Regrow),
    // ROLL_MOVE]. The HEAL is the one action whose behaviour DEPENDS on halfDead
    // -- AbstractMonster.heal returns early on isDying, and isDying is
    // `hp <= 0 && !halfDead`, so clearing the flag before the heal would silently
    // skip it -- and it resolves BEFORE the clear, as the Java orders it. The
    // only thing between the clear and this roll is the Regrow ApplyPower, which
    // reads no liveness at all (and by then the heal has put the record back
    // above 0 HP regardless). So the gate below -- the decided move IS
    // REINCARNATE -- selects exactly the ROLL_MOVE that a ChangeState("REVIVE")
    // precedes. The Hexaghost's queued-roll state changes use the same shape.
    if (m.move_history[0] == kReincarnate) {
        m.flags &= ~kMonsterFlagHalfDead;
    }

    // The clear above is what makes this roll take the NORMAL tree rather than
    // the halfDead -> REINCARNATE arm, which is the Java's ordering exactly.
    const int32_t num = random(s.ai_rng, 99);
    darkling_get_move(s, mi, num);

    if (m.move_history[0] == kNip) {
        knowledge_reveal_monster_roll(s, mi, m.pad0);
    }
}

void darkling_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (Darkling.java:100-141). Every case falls through to the trailing
    // RollMoveAction at :140, which is QUEUED rather than rolled inline: a
    // half-death mid-turn (a Thorns reflect, say) queues a SetMoveAction that
    // must resolve BEFORE the roll, and only a queued ROLL_MOVE gets that order.
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    const sts::registry::MonsterDef& def = sts::registry::kDarkling;

    if (move == kHarden) {
        // (:110-115) GainBlockAction(this, this, 12) unconditionally, then --
        // ONLY at A17+ -- ApplyPowerAction(this, this, StrengthPower(this, 2), 2).
        // An effect list carries per-tier AMOUNTS but not per-tier PRESENCE, so
        // the two steps are queued one at a time and step 1 is skipped below A17.
        // The Snecko TAIL precedent (S2.22), through the same per-step helper.
        queue_monster_move_effect(s, mi, def, move, 0, kMoveTargetFromStep);
        if (kMonsterAscension >= 17) {
            queue_monster_move_effect(s, mi, def, move, 1, kMoveTargetFromStep);
        }
    } else if (move == kNip) {
        // (:116-119) one DamageAction on damage.get(1) == the per-instance rolled
        // nipDmg (pad0). Not a table column, so queued directly; the registry's
        // placeholder amount is 0. AnimateFastAttack is presentation.
        ActionQueueItem dmg{};
        dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
        dmg.src = mi;
        dmg.tgt = kActorPlayer;
        dmg.amount = static_cast<int32_t>(m.pad0);
        add_to_bottom(s, dmg);
    } else if (move == kReincarnate) {
        // (:125-137), in queue order.
        //
        // The SFX coin at :126-129 is UNSEEDED MathUtils and draws nothing.
        //
        // HealAction(this, this, this.maxHealth / 2) (:131) -- INTEGER division,
        // and not a roll: there is no HP re-roll on revival. maxHealth is
        // per-instance, so the amount cannot be a table column.
        ActionQueueItem heal{};
        heal.opcode = static_cast<uint16_t>(Opcode::HEAL);
        heal.src = mi;
        heal.tgt = mi;
        heal.amount = m.max_hp / 2;
        add_to_bottom(s, heal);

        // ChangeStateAction("REVIVE") (:132) -- see darkling_roll_move for where
        // its `halfDead = false` lands and why that is the same program.

        // ApplyPowerAction(this, this, new RegrowPower(this), 1) (:133). Every
        // revival re-grants it and nothing ever removes it, so the stack counts
        // the revivals.
        ActionQueueItem regrow{};
        regrow.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        regrow.src = mi;
        regrow.tgt = mi;
        regrow.amount = kDarklingRegrowAmount;
        regrow.flags = make_apply_power_flags(PowerId::REGROW);
        add_to_bottom(s, regrow);

        // `for (AbstractRelic r : player.relics) r.onSpawnMonster(this);`
        // (:134-136) -- a BARE LOOP inside takeTurn, so it runs SYNCHRONOUSLY at
        // queue time, ahead of the three items queued above it. Philosopher's
        // Stone is the only relic with content there and it likewise calls
        // `monster.addPower(new StrengthPower(monster, 1))` DIRECTLY rather than
        // queueing (PhilosopherStone.java:50-54), so the +1 Strength is already
        // on the record before the heal resolves -- and it is re-granted on
        // EVERY revival, uncapped.
        dispatch_on_spawn_monster_relics(s, mi);
    } else {
        // CHOMP (:103-108, two DAMAGE steps) and COUNT (:121-123, a
        // TextAboveCreatureAction the registry carries as a NOP).
        queue_monster_move_effects(s, mi, def, move);
    }

    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

bool darkling_die(CombatState& s, uint8_t mi) noexcept {
    (void)mi;
    // `public void die() { if (!getCurrRoom().cannotLose) super.die(); }`
    // (Darkling.java:239-243). The return value is the framework's VETO: true ==
    // SUPPRESS super.die(), i.e. no isDying, no power onDeath, no relic
    // onMonsterDeath, no post-super body. darkling_on_damaged re-fires the two
    // fan-outs by hand exactly once, which is the whole reason the veto exists.
    return (s.flags & kCombatFlagCannotLose) != 0u;
}

void darkling_on_damaged(CombatState& s, uint8_t mi, int32_t hp_lost) noexcept {
    // damage() (Darkling.java:200-236). `hp_lost` is unread: the Java's own
    // `else if` arm at :232-235 is the hit animation and models nothing, and the
    // half-death arm reads only resulting state.
    (void)hp_lost;
    MonsterState& m = s.monsters[mi];
    if (!(m.hp <= 0 && !monster_half_dead(m))) {
        return;  // (:203) -- above zero, or already half dead (a damage sponge:
                 // further hits into a half-dead Darkling do nothing at all)
    }

    m.flags |= kMonsterFlagHalfDead;  // (:204)

    // (:205-210) the two fan-outs `super.die()` would have run, BY HAND and in
    // die()'s own order -- powers first, then the player's relics. They reach
    // here rather than through the interpreter's death edge because
    // darkling_die vetoed that edge; running both there and here is precisely
    // the double-fire the veto prevents.
    dispatch_on_death(s, mi);
    const RelicView rv = player_relics(s);
    dispatch_relics_on_monster_death(s, rv.relics, rv.count, mi);

    // (:211) `this.powers.clear()` -- something the base die() NEVER does. It is
    // why the power onDeath walk does not fire again in the group sweep below,
    // while the relic walk does.
    m.power_count = 0;

    // (:213-217) "are all the Darklings down". A member is IGNORED unless its id
    // is Darkling, and a Darkling with halfDead false makes the answer no. This
    // monster set its own bit four lines up, so it excludes itself. Non-Darkling
    // members are INVISIBLE to the test -- irrelevant for the homogeneous
    // "3 Darklings" group, but it is transcribed as written.
    bool all_dead = true;
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        const MonsterState& o = s.monsters[i];
        if (o.monster_id != static_cast<uint16_t>(MonsterId::DARKLING) ||
            monster_half_dead(o)) {
            continue;
        }
        all_dead = false;
    }

    if (!all_dead) {
        // (:219-224) the half-death telegraph, guarded on nextMove != COUNT.
        // BOTH halves run, and both push move 4 onto the 3-slot history:
        // setMove appends on every call (AbstractMonster.java:431-437), and the
        // queued SetMoveAction does it a second time when it resolves. The
        // resulting [4, 4, <pre-death move>] is what a revived Darkling's first
        // decision reads. createIntent (:222) is presentation.
        if (m.move_history[0] != kCount) {
            set_monster_move(m, kCount, MonsterIntent::UNKNOWN);
            ActionQueueItem set_move{};
            set_move.opcode = static_cast<uint16_t>(Opcode::SET_MOVE);
            set_move.src = mi;
            set_move.tgt = mi;
            set_move.amount = kCount;
            set_move.flags = static_cast<uint32_t>(MonsterIntent::UNKNOWN);
            add_to_bottom(s, set_move);
        }
        return;
    }

    // (:225-231) the last Darkling is down: drop the room latch, un-half-die,
    // and kill EVERY member with a DIRECT call -- not a queued action -- in slot
    // order. Each of those now passes the veto (cannotLose is false) and runs
    // `super.die()`, which is where the relic onMonsterDeath fan-out fires a
    // SECOND time for every Darkling in the group. So the whole group's deaths
    // land inside this one op_damage.
    s.flags &= ~kCombatFlagCannotLose;
    // `this.halfDead = false;` (:227) IS NOT WRITTEN HERE, and the omission is
    // the load-bearing part. In the Java that line is invisible to the sweep --
    // die()'s only guard is `!isDying`, which halfDead does not feed -- so
    // `this` still runs its own die() and fires the relic walk a second time
    // like every sibling. In THIS engine isDying has no storage and is modelled
    // as `hp <= 0 && !halfDead`, so clearing the bit a line early would make
    // this record look ALREADY DYING to sweep_die and silently swallow its
    // second fan-out. sweep_die clears the bit for every member, self included,
    // at the point where the Java sets isDying -- which is the same program,
    // and the only spelling of it that keeps both facts true.
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        sweep_die(s, i);
    }
}

}  // namespace sts::engine
