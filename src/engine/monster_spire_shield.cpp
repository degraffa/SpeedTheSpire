// SpireShield: the moveCount % 3 cycle, the short-circuited BASH orb branch, the
// FORTIFY all-allies fan-out, and the pre-battle Surrounded that arms the whole
// back-attack mechanic. See monster_spire_shield.hpp for provenance and the six
// readings this body leans on. The shared die() body both guards register lives
// at the bottom of this file.

#include "sts/engine/monster_spire_shield.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/back_attack.hpp"       // the facing/marker module
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // move helpers, queue_monster_move_effect*
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kBash = sts::registry::kSpireShieldMoveBash;        // 1
constexpr uint8_t kFortify = sts::registry::kSpireShieldMoveFortify;  // 2
constexpr uint8_t kSmash = sts::registry::kSpireShieldMoveSmash;      // 3

// getMove (SpireShield.java:113-137) -- header note (3). `num` is not a parameter
// because the Java body never reads it; the draw that produced it is spent by the
// caller, which is the whole point of the (void) casts at both call sites.
void spire_shield_decide_move(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    switch (spire_guard_move_count(m)) {
        case 0:
            // `if (aiRng.randomBoolean()) setMove(FORTIFY, DEFEND);
            //  else setMove(BASH, ATTACK_DEBUFF, damage.get(0).base);` (:116-123)
            // -- the EXTRA aiRng draw, on one arm of the cycle in three.
            if (random_boolean(s.ai_rng)) {
                set_monster_move(m, kFortify, MonsterIntent::DEFEND);
            } else {
                set_monster_move(m, kBash, MonsterIntent::ATTACK_DEBUFF);
            }
            break;
        case 1:
            // `if (!lastMove(BASH)) setMove(BASH, ...); else setMove(FORTIFY, ...)`
            // (:124-131). last_move_is reads the PREVIOUS decision -- set_monster_move
            // below pushes this one -- which is the Java's moveHistory ordering.
            if (!last_move_is(m, kBash)) {
                set_monster_move(m, kBash, MonsterIntent::ATTACK_DEBUFF);
            } else {
                set_monster_move(m, kFortify, MonsterIntent::DEFEND);
            }
            break;
        default:
            // `setMove(SMASH, ATTACK_DEFEND, damage.get(1).base)` (:132-134),
            // unconditional and drawless.
            set_monster_move(m, kSmash, MonsterIntent::ATTACK_DEFEND);
            break;
    }
    spire_guard_bump_move_count(m);  // `++this.moveCount` (:136)
}

}  // namespace

void spire_shield_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::SPIRE_SHIELD);
    // The `super(...)` HP argument is the LITERAL 110 (:49) -- no draw -- and the
    // setHp under it (:55-59) is the ONE monster_hp_rng draw. Both arms take the
    // SINGLE-argument overload, which is literally setHp(hp, hp)
    // (AbstractMonster.java:777-779), so the range is degenerate and the draw
    // still happens: header note (1), s3-design section 5 trap 4.
    const int32_t hp =
        random(s.monster_hp_rng,
               sts::registry::kSpireShield.hp_min(kMonsterAscension),
               sts::registry::kSpireShield.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    // The ctor's offsetX (:49). It is a per-TYPE constant here -- unlike the
    // summoned gremlins' and daggers', which are per-SPAWNER/per-SLOT
    // (monster_dispatch.hpp "WHO SETS draw_x") -- because the two guards appear in
    // exactly one encounter, constructed by MonsterHelper at fixed coordinates
    // (MonsterHelper.java:599-601), and nothing spawns either of them mid-combat.
    m.draw_x = kSpireShieldDrawX;
    // `moveCount = 0` (:41) is already the MonsterState{} default; the mod-3
    // encoding makes the field's zero and the cycle's case 0 the same value.
    //
    // init() -> rollMove -> getMove(aiRng.random(99)) (AbstractMonster.java:
    // 465-467). getMove IGNORES `num` on every arm, but the draw is still SPENT
    // -- the Taskmaster / Looter / Guardian precedent, and the easy draw to lose.
    (void)random(s.ai_rng, 99);
    spire_shield_decide_move(s, mi);
}

void spire_shield_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (:69-77), header note (4). THREE items in this order.

    // (a) `ApplyPowerAction(AbstractDungeon.player, this, new SurroundedPower(
    //      AbstractDungeon.player))` (:71). The target is the PLAYER and the
    //     source is the Shield -- the rare monster-applied power that lives on the
    //     player's list, and the ONLY source of Surrounded in the game.
    ActionQueueItem sur{};
    sur.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    sur.src = mi;
    sur.tgt = kActorPlayer;
    sur.amount = kSurroundedAppliedAmount;  // the ctor's -1 (SurroundedPower.java:20)
    sur.flags = make_apply_power_flags(PowerId::SURROUNDED);
    add_to_bottom(s, sur);

    // (b) NOT IN THIS JAVA CLASS -- the framework's BackAttack marker, which
    //     AbstractMonster.applyPowers addToTop's when (a) resolves and
    //     onModifyPower re-runs the group's applyPowers. addToTop at that moment
    //     lands it ahead of (c) and behind (a); addToBottom HERE occupies the
    //     identical position, so the resolve order is the same. Header note (4).
    queue_pre_battle_back_attack_markers(s);

    // (c) `ApplyPowerAction(this, this, new ArtifactPower(this, 2))` at
    //     ascension >= 18, else `(this, 1)` (:72-76). Resolved at
    //     kMonsterAscension 20, so the A18 arm is the live one; both are written
    //     out because the branch is the Java's, not a tier column.
    ActionQueueItem art{};
    art.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    art.src = mi;
    art.tgt = mi;
    art.amount = (kMonsterAscension >= 18) ? 2 : 1;
    art.flags = make_apply_power_flags(PowerId::ARTIFACT);
    add_to_bottom(s, art);
}

void spire_shield_take_turn(CombatState& s, uint8_t mi) noexcept {
    const uint8_t move = s.monsters[mi].move_history[0];
    const sts::registry::MonsterDef& def = sts::registry::kSpireShield;

    if (move == kBash) {
        // (:82-92). ChangeStateAction("ATTACK") and WaitAction(0.35f) are
        // presentation. Step 0 is the DamageAction on damage.get(0).
        queue_monster_move_effect(s, mi, def, kBash, 0, kMoveTargetFromStep);
        // THE SHORT CIRCUIT, WRITTEN OUT (header note 2):
        //     if (!player.orbs.isEmpty() && aiRng.randomBoolean()) { Focus(-1); }
        //     else { Strength(-1); }
        // C++ `&&` short-circuits exactly as Java's does, so with kPlayerHasOrbs
        // false the randomBoolean() is NEVER EVALUATED and NO aiRng draw is spent
        // -- which is the modelled behaviour, not merely the outcome. FocusPower
        // is deliberately unregistered (powers.yaml; S4, with the Defect), so the
        // orb arm queues nothing and cannot be reached to try.
        const bool orb_arm = kPlayerHasOrbs && random_boolean(s.ai_rng);
        if (!orb_arm) {
            // Step 1: ApplyPowerAction(player, this, StrengthPower(player, -1), -1)
            // (:90).
            queue_monster_move_effect(s, mi, def, kBash, 1, kMoveTargetFromStep);
        }
    } else if (move == kFortify) {
        // (:93-98): `for (m : getMonsters().monsters) addToBottom(new
        //  GainBlockAction(m, this, 30));` -- EVERY member of the group, ITSELF
        // INCLUDED, and with NO liveness filter (the Java walks the list whole).
        // A step's target vocabulary is SELF or PLAYER, so the row authors ONE
        // SELF-targeted 30-block template and this body retargets it per member --
        // the Healer's HEAL/BUFF precedent, and the schema limitation the row
        // records at the step.
        for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
            queue_monster_move_effect(s, mi, def, kFortify, 0, i);
        }
    } else if (move == kSmash) {
        // (:99-108). ChangeStateAction("OLD_ATTACK") / WaitAction(0.5f) are
        // presentation; step 0 is the DamageAction on damage.get(1) and step 1 the
        // GainBlockAction on self. At A18+ that block is the flat 99 (:103-105),
        // which is the row's a18 column and the live one at kMonsterAscension 20;
        // BELOW A18 it is `damage.get(1).output` (:107) -- the POST-power output,
        // a runtime read no tier column can carry, which is why the row's base
        // column is a declared schema NULL. If a real ascension is ever threaded
        // through combat_begin, THIS is the site that must compute the post-power
        // output instead of reading the column (back_attack.hpp note 3c explains
        // why the back attack is part of that number).
        queue_monster_move_effects(s, mi, def, kSmash);
    }
    // `AbstractDungeon.actionManager.addToBottom(new RollMoveAction(this))`
    // (:110) -- OUTSIDE the switch, so EVERY move body reaches it (including an
    // unrecognised move id, which the Java's switch would also fall through).
    // This is what makes the class a monster_roll_move_fn registrant: the roll
    // resolves as a queued item, spending its aiRng.random(99) after the move's
    // own effects rather than inline.
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

void spire_shield_roll_move(CombatState& s, uint8_t mi) noexcept {
    // RollMoveAction -> rollMove -> getMove(aiRng.random(99)); `num` unread.
    (void)random(s.ai_rng, 99);
    spire_shield_decide_move(s, mi);
}

void spire_guard_die_after(CombatState& s, uint8_t mi) noexcept {
    // THE BYTE-IDENTICAL die() BODY OF BOTH GUARDS (SpireShield.java:164-176 ==
    // SpireSpear.java:171-183), AFTER `super.die()` -- header note (6),
    // s3-design section 5 trap 7.
    //
    // `mi` is unused on purpose: the walk has NO `m == this` term and excludes the
    // dying guard only because super.die() has already zeroed its HP and set
    // isDying. That is the Reptomancer ordering, and it is the reason this body
    // lives in monster_die_after_fn rather than monster_die_fn.
    (void)mi;
    const auto player_surrounded = [&s]() noexcept {
        for (uint8_t i = 0; i < s.player_power_count && i < kPowerCap; ++i) {
            if (s.player_powers[i].power_id ==
                static_cast<uint16_t>(PowerId::SURROUNDED)) {
                return true;
            }
        }
        return false;
    };
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        // `if (m.isDead || m.isDying) continue;` (:168). This engine models both
        // with monster_is_dying (`hp <= 0 && !halfDead`): isDead implies hp <= 0,
        // and neither guard has a halfDead branch, so the two collapse here.
        // ESCAPE is not in the Java's test and is unreachable in this encounter --
        // neither guard has an ESCAPE move.
        if (monster_is_dying(s.monsters[i])) {
            continue;
        }
        // The hasPower test is re-read PER MEMBER, as the Java's is, and it stops
        // being true only when a queued removal RESOLVES -- which is after this
        // whole loop -- so with two guards it fires at most once anyway. Read live
        // rather than hoisted, because the Java reads it live.
        if (player_surrounded()) {
            // `player.flipHorizontal = m.drawX < player.drawX;` (:170) -- turn to
            // face the survivor. Written BEFORE the removal is queued, exactly as
            // the Java writes it, so any predicate read between now and the
            // removal resolving sees the new facing.
            set_player_facing_toward(s, i);
            // `addToBottom(new RemoveSpecificPowerAction(player, player,
            //  "Surrounded"))` (:171).
            ActionQueueItem rem{};
            rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
            rem.src = kActorPlayer;
            rem.tgt = kActorPlayer;
            rem.flags = make_apply_power_flags(PowerId::SURROUNDED);
            add_to_bottom(s, rem);
        }
        // `if (!m.hasPower("BackAttack")) continue;` then
        // `addToBottom(new RemoveSpecificPowerAction(m, m, "BackAttack"))`
        // (:173-174). THIS IS WHERE KILL ORDER BECOMES OBSERVABLE: the marker sits
        // on whichever guard the player is not facing, so the number of items this
        // loop queues depends on which of the two died -- header note (6).
        bool survivor_marked = false;
        for (uint8_t k = 0; k < s.monsters[i].power_count && k < kPowerCap; ++k) {
            if (s.monsters[i].powers[k].power_id ==
                static_cast<uint16_t>(PowerId::BACK_ATTACK)) {
                survivor_marked = true;
                break;
            }
        }
        if (!survivor_marked) {
            continue;
        }
        ActionQueueItem rem{};
        rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
        rem.src = i;
        rem.tgt = i;
        rem.flags = make_apply_power_flags(PowerId::BACK_ATTACK);
        add_to_bottom(s, rem);
    }
}

}  // namespace sts::engine
