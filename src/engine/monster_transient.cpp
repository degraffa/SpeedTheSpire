// The Transient: native turn body and the two-power opener. See
// monster_transient.hpp for provenance, the zero-HP-draw ctor shape, the
// one-discarded-ai-draw accounting and the per-instance damage ramp.

#include "sts/engine/monster_transient.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem, kActorPlayer
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags, make_damage_flags
#include "sts/engine/monster_dispatch.hpp"  // set_monster_move, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kAttack = sts::registry::kTransientMoveAttack;  // 1

// Rung zero of the ramp: startingDeathDmg, resolved from the row's ATTACK step
// at the fixed ascension (30, or 40 from A2). Every later rung is this plus
// kTransientDamageStep per resolved attack.
[[nodiscard]] int32_t transient_base_damage() noexcept {
    const sts::registry::MonsterMove* mv =
        sts::registry::kTransient.move(kAttack);
    if (mv == nullptr || mv->effect_count == 0) {
        return 0;  // defensive: the row guarantees one DAMAGE step
    }
    return mv->effects[0].amount.at(kMonsterAscension);
}

}  // namespace

void transient_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::TRANSIENT);
    // NO monster_hp_rng DRAW. setHp is never called (Transient.java:45-61 has no
    // such line); the ctor passes maxHealth 999 to super and AbstractMonster's
    // ctor (AbstractMonster.java:135-155) sets `currentHealth = maxHealth`
    // without touching any RNG. Calling random() over a degenerate 999..999
    // range here would LOOK equivalent and would not be -- it would consume a
    // value and shift every later HP draw on the floor.
    m.hp = kTransientHp;
    m.max_hp = kTransientHp;
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    // `private int count = 0;` (Transient.java:38). pad0 is the monster-type-
    // scoped scratch byte and this monster owns the whole of it; the value never
    // exceeds 6 (see the saturation note in take_turn).
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). getMove
    // (:112-115) never reads num on ANY path -- it has no branches at all -- so
    // this draw is DISCARDED. It still happens, and it still moves the stream
    // (the Guardian / Red Slaver / Looter precedent). It is also the LAST ai_rng
    // draw this monster ever makes: takeTurn re-telegraphs itself and queues no
    // RollMoveAction.
    (void)random(s.ai_rng, 99);
    // setMove(1, ATTACK, startingDeathDmg + count * 10) with count == 0.
    set_monster_move(m, kAttack, MonsterIntent::ATTACK);
}

void transient_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (Transient.java:63-71), TWO addToBottom actions IN THIS
    // ORDER -- and the order is the power-list application order every hook
    // fan-out then walks:
    //   ApplyPowerAction(this, this, new FadingPower(this, asc >= 17 ? 6 : 5))
    //   ApplyPowerAction(this, this, new ShiftingPower(this))
    // Neither draws RNG.
    ActionQueueItem fading{};
    fading.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    fading.src = mi;
    fading.tgt = mi;
    fading.amount = (kMonsterAscension >= 17) ? kTransientFadingTurnsA17
                                              : kTransientFadingTurns;
    fading.flags = make_apply_power_flags(PowerId::FADING);
    add_to_bottom(s, fading);

    ActionQueueItem shifting{};
    shifting.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    shifting.src = mi;
    shifting.tgt = mi;
    // -1: ShiftingPower's ctor assigns no amount, so the object carries
    // AbstractPower's field initializer and the 3-arg ApplyPowerAction hands
    // THAT to addPower (the Confusion precedent, monsters.yaml id 34). The body
    // never reads its own amount, so the value is purely an oracle-visible
    // field -- which is exactly why it must be right.
    shifting.amount = kShiftingNoAmount;
    shifting.flags = make_apply_power_flags(PowerId::SHIFTING);
    add_to_bottom(s, shifting);
}

void transient_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (Transient.java:73-84), ONE case:
    //
    //   ChangeStateAction(this, "ATTACK"); WaitAction(0.4f);        // presentation
    //   DamageAction(player, this.damage.get(this.count), BLUNT_HEAVY);
    //   ++this.count;
    //   this.setMove((byte)1, ATTACK, this.startingDeathDmg + this.count * 10);
    //
    // Note the ORDER: the hit uses the PRE-increment count, and the
    // re-telegraph uses the POST-increment one. There is no trailing
    // RollMoveAction anywhere in this class.
    MonsterState& m = s.monsters[mi];
    const int32_t count = static_cast<int32_t>(m.pad0);
    // The Java indexes a 7-element ArrayList, so an 8th attack would throw. It
    // cannot happen: Fading is 6 at A17+ and 5 below, and the monster dies at
    // the end of that many turns (power_fading.hpp), so the highest index ever
    // reached is 5. The saturation below is therefore a statement that the case
    // is unreachable, not a behaviour -- a named test pins that the ladder stops
    // at six rungs.
    const int32_t rung = (count < kTransientDamageRungs - 1)
                             ? count
                             : (kTransientDamageRungs - 1);
    ActionQueueItem dmg{};
    dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    dmg.src = mi;
    dmg.tgt = kActorPlayer;
    dmg.amount = transient_base_damage() + rung * kTransientDamageStep;
    dmg.flags = make_damage_flags(DamageType::NORMAL);
    add_to_bottom(s, dmg);

    // ++count, SYNCHRONOUS -- the Java increments inside takeTurn, before the
    // queued DamageAction has resolved, so the re-telegraph below already shows
    // the NEXT rung while this turn's hit is still in flight.
    if (count < kTransientDamageRungs - 1) {
        m.pad0 = static_cast<uint8_t>(count + 1);
    }
    // The self re-telegraph (:81). setMove PUSHES MOVE HISTORY
    // (AbstractMonster.java:431-437) even though nothing in this class ever
    // reads it, so the ring genuinely fills with 1s and the oracle sees that.
    set_monster_move(m, kAttack, MonsterIntent::ATTACK);
}

}  // namespace sts::engine
