// Lagavulin native AI: the sleep/wake state machine, its pre-battle armour, and
// the damage() wake interrupt. See the public header for the full Java
// provenance and the "monster block never decays" note that makes the sleeping
// armour accumulate.

#include "sts/engine/monster_lagavulin.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem, kActorPlayer
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags, kBlockNoPowers
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move-history helpers
#include "sts/engine/rng_stream.hpp"
#include "sts/registry/monster_table.hpp"   // kLagavulin def + move-id constants

namespace sts::engine {
namespace {

constexpr uint8_t kDebuff = sts::registry::kLagavulinMoveDebuff;         // 1
constexpr uint8_t kStrongAtk = sts::registry::kLagavulinMoveStrongAtk;   // 3
constexpr uint8_t kOpen = sts::registry::kLagavulinMoveOpen;             // 4
constexpr uint8_t kIdle = sts::registry::kLagavulinMoveIdle;             // 5

// ARMOR_AMT (Lagavulin.java:66): the pre-battle GainBlockAction amount, the
// MetallicizePower stack it is applied with (:106-107), and the amount the
// ReducePowerAction sheds on opening (:188) are all this one constant. It is
// not a move effect, and the registry's per-monster numeric columns cover HP
// ranges, native RNG rolls and move-effect amounts -- none of which a fixed
// usePreBattleAction amount is -- so it lives here with its citation.
constexpr int32_t kArmorAmount = 8;

// The third idle is what opens the shell (Lagavulin.java:138 `idleCount >= 3`).
constexpr uint8_t kIdleCountToOpen = 3;

// getMove switches to the debuff once two Strong Attacks have landed
// (Lagavulin.java:215 `debuffTurnCount < 2`).
constexpr uint8_t kAttacksBeforeDebuff = 2;

// --- pad0: idleCount (low nibble) | debuffTurnCount (high nibble) ------------

[[nodiscard]] uint8_t idle_count(const MonsterState& m) noexcept {
    return static_cast<uint8_t>(m.pad0 & 0x0Fu);
}

void set_idle_count(MonsterState& m, uint8_t v) noexcept {
    m.pad0 = static_cast<uint8_t>((m.pad0 & 0xF0u) | (v & 0x0Fu));
}

[[nodiscard]] uint8_t debuff_turn_count(const MonsterState& m) noexcept {
    return static_cast<uint8_t>((m.pad0 >> 4) & 0x0Fu);
}

void set_debuff_turn_count(MonsterState& m, uint8_t v) noexcept {
    m.pad0 = static_cast<uint8_t>((m.pad0 & 0x0Fu) |
                                  static_cast<uint8_t>((v & 0x0Fu) << 4));
}

// Both counters only ever ++; saturating at the nibble keeps a hypothetical
// runaway from wrapping into the neighbouring counter. Neither can reach 15 in
// the real machine (idleCount stops being read past 3, debuffTurnCount is reset
// to 0 by the DEBUFF turn that always follows its second increment).
[[nodiscard]] uint8_t saturating_inc(uint8_t v) noexcept {
    return v < 0x0Fu ? static_cast<uint8_t>(v + 1) : v;
}

[[nodiscard]] bool has_flag(const MonsterState& m, uint32_t bit) noexcept {
    return (m.flags & bit) != 0u;
}

void set_flag(MonsterState& m, uint32_t bit) noexcept {
    m.flags |= bit;
}

// getMove (Lagavulin.java:212-227). The rollMove argument is drawn but never
// read by this class, so the decision is a pure function of the machine's state.
void lagavulin_get_move(MonsterState& m) noexcept {
    if (!has_flag(m, kMonsterFlagLagavulinIsOut)) {
        set_monster_move(m, kIdle, MonsterIntent::SLEEP);
        return;
    }
    if (debuff_turn_count(m) < kAttacksBeforeDebuff) {
        // The lastTwoMoves(STRONG_ATK) arm cannot be reached: takeTurn increments
        // debuffTurnCount on every STRONG_ATK, so two consecutive attacks always
        // leave the counter at 2 and take the else below. It is reproduced
        // verbatim anyway -- the counter's reset rule is what makes it dead, and
        // that is not a property to bake in by hand.
        if (last_two_moves_are(m, kStrongAtk)) {
            set_monster_move(m, kDebuff, MonsterIntent::STRONG_DEBUFF);
        } else {
            set_monster_move(m, kStrongAtk, MonsterIntent::ATTACK);
        }
        return;
    }
    set_monster_move(m, kDebuff, MonsterIntent::STRONG_DEBUFF);
}

// RollMoveAction -> rollMove(): one aiRng.random(99) draw whose value getMove
// discards (AbstractMonster.java:712-714). The draw itself is observable, so it
// happens even though nothing reads it.
void roll_move(CombatState& s, MonsterState& m) noexcept {
    (void)random(s.ai_rng, 99);
    lagavulin_get_move(m);
}

// changeState("OPEN") minus presentation (Lagavulin.java:184-193).
//
// The Java routes this through a queued ChangeStateAction whose only
// state-changing consequence is the ReducePowerAction it appends when it
// resolves. Nothing can be queued between those two, so queueing the
// REDUCE_POWER at the point where ChangeStateAction would have RESOLVED
// reproduces the resolution order exactly. The isOut write is synchronous here
// rather than at resolve time; nothing reads isOut in between (the only reader
// is getMove, which every path reaches strictly later).
//
// `!this.isDying` (:184) is the guard that makes a LETHAL hit latch
// isOutTriggered without ever opening the shell or shedding the Metallicize.
void change_state_open(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    if (m.hp <= 0) {
        return;  // isDying: changeState falls through the OPEN branch entirely
    }
    set_flag(m, kMonsterFlagLagavulinIsOut);
    ActionQueueItem reduce{};
    reduce.opcode = static_cast<uint16_t>(Opcode::REDUCE_POWER);
    reduce.src = mi;
    reduce.tgt = mi;
    reduce.amount = kArmorAmount;
    reduce.flags = make_apply_power_flags(PowerId::METALLICIZE);
    add_to_bottom(s, reduce);
}

void lagavulin_init_impl(CombatState& s, uint8_t mi, bool asleep) noexcept {
    const auto& def = sts::registry::kLagavulin;
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::LAGAVULIN);

    // setHp(min, max): one inclusive monsterHpRng draw; currentHealth == maxHealth.
    const int32_t rolled = random(s.monster_hp_rng,
                                  def.hp_min(kMonsterAscension),
                                  def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = static_cast<int16_t>(rolled);

    m.block = 0;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // The !asleep ctor branch (Lagavulin.java:88-91) pre-sets BOTH isOut and
    // isOutTriggered, which is what makes the awake variant skip the wake
    // interrupt and telegraph an attack straight out of init().
    m.flags = asleep ? kMonsterFlagLagavulinAsleep
                     : (kMonsterFlagLagavulinIsOut |
                        kMonsterFlagLagavulinOutTriggered);

    roll_move(s, m);  // init() -> rollMove(): IDLE if asleep, else STRONG_ATK
}

}  // namespace

void lagavulin_init(CombatState& s, uint8_t mi) noexcept {
    lagavulin_init_impl(s, mi, /*asleep=*/true);
}

void lagavulin_init_awake(CombatState& s, uint8_t mi) noexcept {
    lagavulin_init_impl(s, mi, /*asleep=*/false);
}

void lagavulin_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    if (!has_flag(m, kMonsterFlagLagavulinAsleep)) {
        // The awake branch is pure presentation plus one setMove
        // (Lagavulin.java:108-113). setMove pushes move history again, so the
        // init roll's STRONG_ATK telegraph stays in slot [1].
        set_monster_move(m, kDebuff, MonsterIntent::STRONG_DEBUFF);
        return;
    }
    // GainBlockAction(this, this, 8): a direct addBlock, so no
    // AbstractCard.applyPowers pass -> kBlockNoPowers.
    ActionQueueItem block{};
    block.opcode = static_cast<uint16_t>(Opcode::BLOCK);
    block.src = mi;
    block.tgt = mi;
    block.amount = kArmorAmount;
    block.flags = kBlockNoPowers;
    add_to_bottom(s, block);

    ActionQueueItem metallicize{};
    metallicize.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    metallicize.src = mi;
    metallicize.tgt = mi;
    metallicize.amount = kArmorAmount;
    metallicize.flags = make_apply_power_flags(PowerId::METALLICIZE);
    add_to_bottom(s, metallicize);
}

void lagavulin_take_turn(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    const auto& def = sts::registry::kLagavulin;

    switch (m.move_history[0]) {
        case kDebuff:
            // debuffTurnCount = 0, then Dexterity and Strength in that order.
            set_debuff_turn_count(m, 0);
            queue_monster_move_effects(s, mi, def, kDebuff);
            roll_move(s, m);
            return;

        case kStrongAtk:
            set_debuff_turn_count(m, saturating_inc(debuff_turn_count(m)));
            queue_monster_move_effects(s, mi, def, kStrongAtk);
            roll_move(s, m);
            return;

        case kIdle: {
            const uint8_t idles = saturating_inc(idle_count(m));
            set_idle_count(m, idles);
            if (idles < kIdleCountToOpen) {
                // TalkAction then RollMoveAction (Lagavulin.java:146-157). The
                // re-telegraph at :144 and the roll's getMove both decide IDLE.
                set_monster_move(m, kIdle, MonsterIntent::SLEEP);
                roll_move(s, m);
                return;
            }
            // Third idle: latch, open, and hand the next move over to a queued
            // SetMoveAction. The inner switch has no `case 3`, so this branch
            // queues NO RollMoveAction -- one fewer aiRng draw than every other
            // Lagavulin turn.
            set_flag(m, kMonsterFlagLagavulinOutTriggered);
            ActionQueueItem set_move{};
            set_move.opcode = static_cast<uint16_t>(Opcode::SET_MOVE);
            set_move.src = mi;
            set_move.tgt = mi;
            set_move.amount = kStrongAtk;
            set_move.flags = static_cast<uint32_t>(MonsterIntent::ATTACK);
            add_to_bottom(s, set_move);
            // ChangeStateAction is queued BEFORE the SetMoveAction, but the
            // ReducePowerAction it appends lands at the bottom when it resolves
            // -- i.e. after the SetMoveAction. Queue order here is resolution
            // order (see change_state_open).
            change_state_open(s, mi);
            return;
        }

        case kOpen:
            // TextAboveCreatureAction(STUNNED) is presentation; the roll is not.
            roll_move(s, m);
            return;

        default:
            return;  // move 6 is unreachable (registry/monsters.yaml); 0 is the
                     // empty-history sentinel
    }
}

void lagavulin_on_damaged(CombatState& s, uint8_t mi, int32_t hp_lost) noexcept {
    MonsterState& m = s.monsters[mi];
    // `currentHealth != previousHealth && !isOutTriggered` (Lagavulin.java:201).
    // A hit the sleeping armour absorbs entirely moves no HP and does not wake
    // it; the else-branch is the Hit animation, which has no state.
    if (hp_lost == 0 || has_flag(m, kMonsterFlagLagavulinOutTriggered)) {
        return;
    }
    set_monster_move(m, kOpen, MonsterIntent::STUN);  // setMove + createIntent
    set_flag(m, kMonsterFlagLagavulinOutTriggered);
    change_state_open(s, mi);                         // ChangeStateAction("OPEN")
}

}  // namespace sts::engine
