// The Shelled Parasite: native move selection (including the one-level getMove
// recursion), the pre-battle armour, and the turn body. See
// monster_shelled_parasite.hpp for provenance, the armour-break edge, and the
// draw accounting.

#include "sts/engine/monster_shelled_parasite.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem, kBlockNoPowers
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move-history helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kFell = sts::registry::kShelledParasiteMoveFell;                  // 1
constexpr uint8_t kDoubleStrike = sts::registry::kShelledParasiteMoveDoubleStrike;  // 2
constexpr uint8_t kLifeSuck = sts::registry::kShelledParasiteMoveLifeSuck;          // 3
constexpr uint8_t kStunned = sts::registry::kShelledParasiteMoveStunned;            // 4

// getMove (ShelledParasite.java:172-204), MINUS the firstMove branch (handled in
// init). `num` is the draw the caller already made.
//
// THE RECURSION IS REPRODUCED AS A RECURSION, not flattened into a loop or an
// early re-roll, because its ai_rng cost is what the stream sees: one extra
// aiRng.random(20, 99) (:191), over a range that EXCLUDES the first arm, so the
// re-entry always terminates in the second or third arm and the depth is exactly
// one. `s` is threaded for that draw.
void parasite_get_move(CombatState& s, MonsterState& m, int32_t num) noexcept {
    if (num < 20) {                                        // (:187-192)
        if (!last_move_is(m, kFell)) {
            set_monster_move(m, kFell, MonsterIntent::ATTACK_DEBUFF);
            return;
        }
        // `this.getMove(AbstractDungeon.aiRng.random(20, 99))` (:191) -- the
        // TWO-argument random, an INCLUSIVE 20..99, not a bare random(99).
        const int32_t again = random(s.ai_rng, 20, 99);
        parasite_get_move(s, m, again);
        return;
    }
    if (num < 60) {                                        // (:193-198)
        if (!last_two_moves_are(m, kDoubleStrike)) {
            set_monster_move(m, kDoubleStrike, MonsterIntent::ATTACK);
        } else {
            set_monster_move(m, kLifeSuck, MonsterIntent::ATTACK_BUFF);
        }
        return;
    }
    if (!last_two_moves_are(m, kLifeSuck)) {               // (:199-203)
        set_monster_move(m, kLifeSuck, MonsterIntent::ATTACK_BUFF);
    } else {
        set_monster_move(m, kDoubleStrike, MonsterIntent::ATTACK);
    }
}

}  // namespace

void shelled_parasite_init(CombatState& s, uint8_t mi) noexcept {
    // The ctor is `super(...)` + setHp(min, max): exactly one monster_hp_rng
    // inclusive draw, currentHealth == maxHealth (AbstractMonster.java:765-775).
    // At A20 the A7 column (70, 75) is live (ShelledParasite.java:82).
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::SHELLED_PARASITE);
    const int32_t hp = random(
        s.monster_hp_rng,
        sts::registry::kShelledParasite.hp_min(kMonsterAscension),
        sts::registry::kShelledParasite.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). The draw
    // always happens and the firstMove branch discards it: at A17+ the opener is
    // Fell DETERMINISTICALLY (:176-179), with NO second draw. The sub-A17 coin
    // (`aiRng.randomBoolean()`, :180) is on the OTHER side of that branch and is
    // NOT spent at A20 -- spending it here would shift every later draw on the
    // stream, which is why the ascension test is transcribed rather than assumed.
    (void)random(s.ai_rng, 99);
    set_monster_move(m, kFell, MonsterIntent::ATTACK_DEBUFF);
}

void shelled_parasite_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (:104-108), in order and both addToBottom:
    //   ApplyPowerAction(this, this, new PlatedArmorPower(this, 14))
    //   GainBlockAction(this, this, 14)
    // No RNG draw. The block is a DIRECT GainBlockAction, so it does NOT go
    // through a card's applyPowers and takes no Dexterity -- kBlockNoPowers, the
    // same flag Plated Armor's own end-of-turn block carries.
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;
    apply.tgt = mi;
    apply.amount = kShelledParasitePlatedArmor;
    apply.flags = make_apply_power_flags(PowerId::PLATED_ARMOR);
    add_to_bottom(s, apply);

    ActionQueueItem blk{};
    blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
    blk.src = mi;
    blk.tgt = mi;
    blk.amount = kShelledParasitePlatedArmor;
    blk.flags = kBlockNoPowers;
    add_to_bottom(s, blk);
}

void shelled_parasite_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    parasite_get_move(s, s.monsters[mi], num);
}

void shelled_parasite_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (ShelledParasite.java:110-141).
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];

    if (move == kStunned) {
        // (:135-138): a SYNCHRONOUS setMove(FELL, ATTACK_DEBUFF) at :136, then a
        // TextAboveCreatureAction (presentation). Crucially this case does NOT
        // return -- it falls out of the switch into the trailing RollMoveAction
        // below, which RE-DECIDES with the Fell just pushed onto the history
        // ring. So the telegraph set here is overwritten before it is ever acted
        // on, and its only lasting effect is on `lastMove`. (Contrast the Byrd's
        // HEADBUTT, which setMoves and returns.)
        set_monster_move(m, kFell, MonsterIntent::ATTACK_DEBUFF);
    }

    // The decided move's program. Fell's AnimateSlowAttack/Wait, Double Strike's
    // per-hit AnimateHop/Wait, and Life Suck's ChangeState("ATTACK")/Wait/
    // BiteEffect are all presentation; the BiteEffect's two MathUtils.random
    // calls (:131) are the UNSEEDED libGDX generator and cost no seeded draw.
    // STUNNED carries a NOP program.
    //
    // NOTE the move read is `move`, captured BEFORE the setMove above: the
    // STUNNED turn queues STUNNED's (empty) program, not Fell's. The Java has the
    // same shape -- the switch already chose its case before :136 ran.
    queue_monster_move_effects(s, mi, sts::registry::kShelledParasite, move);

    // The trailing RollMoveAction (:140), OUTSIDE the switch -- all four cases.
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
