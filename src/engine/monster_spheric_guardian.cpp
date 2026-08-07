// The Spheric Guardian: native (but randomness-free) move selection, the
// zero-draw init, and the pre-battle Barricade/Artifact/block. See
// monster_spheric_guardian.hpp for provenance and for why a monster that reads
// no randomness still has to be native.

#include "sts/engine/monster_spheric_guardian.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem, kBlockNoPowers
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move-history helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kBigAttack = sts::registry::kSphericGuardianMoveBigAttack;      // 1
constexpr uint8_t kInitialBlock =
    sts::registry::kSphericGuardianMoveInitialBlockGain;                          // 2
constexpr uint8_t kBlockAttack = sts::registry::kSphericGuardianMoveBlockAttack;  // 3
constexpr uint8_t kFrailAttack = sts::registry::kSphericGuardianMoveFrailAttack;  // 4

// getMove (SphericGuardian.java:145-162), MINUS the firstMove branch (handled in
// init). It takes no `num` parameter AT ALL, because the Java never reads its
// own: the caller draws and discards, which is where that draw is spelled out.
void spheric_get_move(MonsterState& m) noexcept {
    if ((m.flags & kMonsterFlagSphericSecondMove) != 0u) {
        // (:152-155): the second decision is a forced Frail Attack, once.
        m.flags &= ~kMonsterFlagSphericSecondMove;
        set_monster_move(m, kFrailAttack, MonsterIntent::ATTACK_DEBUFF);
        return;
    }
    if (last_move_is(m, kBigAttack)) {
        // (:157-158): strict alternation from here on.
        set_monster_move(m, kBlockAttack, MonsterIntent::ATTACK_DEFEND);
        return;
    }
    // (:160): setMove(BIG_ATTACK, ATTACK, dmg, 2, true) -- the `2, true` is the
    // multi-hit TELEGRAPH (SLAM_AMT, :49); the two hits are the registry
    // program's two DAMAGE steps.
    set_monster_move(m, kBigAttack, MonsterIntent::ATTACK);
}

}  // namespace

void spheric_guardian_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::SPHERIC_GUARDIAN);
    // NO monster_hp_rng DRAW. setHp is never called (SphericGuardian.java:66-75
    // has no such line); the ctor passes maxHealth 20 to super, and
    // AbstractMonster's ctor (AbstractMonster.java:135-155) sets
    // `currentHealth = maxHealth` without touching any RNG. Calling random() over
    // a degenerate 20..20 range here would LOOK equivalent and would not be -- it
    // would consume a value and shift every later HP draw in the group.
    m.hp = kSphericGuardianHp;
    m.max_hp = kSphericGuardianHp;
    m.block = 0;
    // `private boolean secondMove = true;` (:60). Its sibling `firstMove` (:59)
    // needs no bit: it is consumed by the init rollMove below.
    m.flags = kMonsterFlagSphericSecondMove;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). getMove
    // never reads num on ANY branch, so this draw is discarded -- but it still
    // happens, and it still moves the stream every other monster in the group
    // shares (the Guardian / Red Slaver / Looter precedent).
    (void)random(s.ai_rng, 99);
    set_monster_move(m, kInitialBlock, MonsterIntent::DEFEND);  // (:147-150)
}

void spheric_guardian_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (:77-82), three addToBottom actions IN THIS ORDER:
    //   ApplyPowerAction(this, this, new BarricadePower(this))
    //   ApplyPowerAction(this, this, new ArtifactPower(this, 3))
    //   GainBlockAction(this, this, 40)
    // No RNG draw on any of them.
    ActionQueueItem barricade{};
    barricade.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    barricade.src = mi;
    barricade.tgt = mi;
    // The -1 MARKER BarricadePower's ctor sets (:22), not a magnitude -- the
    // same amount the Barricade card's own APPLY_POWER step authors.
    barricade.amount = kBarricadeMarkerAmount;
    barricade.flags = make_apply_power_flags(PowerId::BARRICADE);
    add_to_bottom(s, barricade);

    ActionQueueItem artifact{};
    artifact.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    artifact.src = mi;
    artifact.tgt = mi;
    artifact.amount = kSphericGuardianArtifact;
    artifact.flags = make_apply_power_flags(PowerId::ARTIFACT);
    add_to_bottom(s, artifact);

    // A DIRECT GainBlockAction -> kBlockNoPowers: it does not go through a card's
    // applyPowers and takes no Dexterity. Barricade (queued above, so it lands
    // first) is what keeps this 40 from decaying at the start of every turn --
    // the monster-side guard in apply_pre_turn_logic.
    ActionQueueItem blk{};
    blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
    blk.src = mi;
    blk.tgt = mi;
    blk.amount = kSphericGuardianStartingBlock;
    blk.flags = kBlockNoPowers;
    add_to_bottom(s, blk);
}

void spheric_guardian_roll_move(CombatState& s, uint8_t mi) noexcept {
    // The draw is made and DISCARDED (see spheric_get_move).
    (void)random(s.ai_rng, 99);
    spheric_get_move(s.monsters[mi]);
}

void spheric_guardian_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (SphericGuardian.java:84-121). Presentation throughout --
    // ChangeState("ATTACK")/Wait on Big Attack, the Wait and MathUtils-coin SFX
    // on Initial Block Gain (an UNSEEDED generator, so no seeded draw),
    // AnimateFastAttack on Block Attack, AnimateSlowAttack on Frail Attack -- and
    // the registry program otherwise. Note Block Attack's block lands BEFORE its
    // damage (:109-111) and Initial Block Gain carries the batch's only
    // in-takeTurn ascension branch (:95-99); both live in the move's step order
    // and tier columns. The RollMoveAction at :120 is outside the switch.
    queue_monster_move_effects(s, mi, sts::registry::kSphericGuardian,
                               s.monsters[mi].move_history[0]);
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
