// The Reptomancer and its SnakeDaggers: the double ctor draw, the queue-time
// dagger planning, the derived slot map and the post-super suicide sweep. See
// monster_reptomancer.hpp for provenance and the seven readings this body leans
// on.

#include "sts/engine/monster_reptomancer.hpp"

#include <cassert>

#include "sts/engine/action_queue.hpp"      // add_to_bottom/top, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_*_flags
#include "sts/engine/monster_dispatch.hpp"  // move helpers, spawn/positioning
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kSnakeStrike = sts::registry::kReptomancerMoveSnakeStrike;  // 1
constexpr uint8_t kSpawnDagger = sts::registry::kReptomancerMoveSpawnDagger;  // 2
constexpr uint8_t kBigBite = sts::registry::kReptomancerMoveBigBite;          // 3

constexpr uint8_t kDaggerWound = sts::registry::kSnakeDaggerMoveWound;      // 1
constexpr uint8_t kDaggerExplode = sts::registry::kSnakeDaggerMoveExplode;  // 2

// EXPLODE's two authored steps (monsters.yaml id 61): 0 is the flat 25 damage,
// 1 is the LOSE_HP placeholder whose amount the module substitutes.
constexpr uint8_t kExplodeDamageStep = 0;

void queue_roll_move(CombatState& s, uint8_t src, uint8_t tgt) noexcept {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    it.src = src;
    it.tgt = tgt;
    add_to_bottom(s, it);
}

// daggersPerSpawn (:67), resolved at the engine's fixed ascension.
[[nodiscard]] constexpr int32_t daggers_per_spawn(int32_t ascension) noexcept {
    return ascension >= kReptomancerDaggersAscension
               ? kReptomancerDaggersPerSpawnA18
               : kReptomancerDaggersPerSpawn;
}

// The SPAWN_DAGGER body (:117-127). ChangeStateAction("SUMMON") and WaitAction
// (:118-119) are presentation and roll nothing.
//
// Everything below is the Java's `for (int i = 0; daggersSpawned < perSpawn &&
// i < daggers.length; ++i)` with `daggers[i]` replaced by the draw_x derivation
// (header note (3)). `i` strictly increases, so no slot is visited twice within
// one turn and the Java's `daggers[i] = daggerToSpawn` write needs no local
// mirror -- but the record the FIRST spawn inserts is visible to the second
// one's positioning walk, which is why the list is simulated.
void queue_spawn_daggers(CombatState& s, uint8_t mi) noexcept {
    // The list of position keys as the group will look at each spawn's resolve,
    // plus a liveness bit per entry so the free-slot scan sees the daggers this
    // turn already planted. Four extra entries because at most four are added.
    int16_t xs[kMonsterCap + 4] = {};
    bool live[kMonsterCap + 4] = {};
    uint8_t n = 0;
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        xs[n] = s.monsters[i].draw_x;
        // `!daggers[i].isDeadOrEscaped()` -- the TARGETING predicate, which a
        // half-dead record also fails (combat_state.hpp monster_dead_or_escaped).
        live[n] = !monster_dead_or_escaped(s.monsters[i]);
        ++n;
    }
    uint8_t self_index = mi;

    const int32_t per_spawn = daggers_per_spawn(kMonsterAscension);
    int32_t spawned = 0;
    for (int32_t slot = 0; spawned < per_spawn && slot < 4; ++slot) {
        const int16_t x = kReptomancerDaggerX[slot];
        bool occupied = false;
        for (uint8_t i = 0; i < n; ++i) {
            if (live[i] && xs[i] == x) {
                occupied = true;
                break;
            }
        }
        if (occupied) {
            continue;  // `if (daggers[i] != null && !isDeadOrEscaped) continue;`
        }
        // `new SnakeDagger(POSX[i], POSY[i])` -- the ctor's monster_hp_rng draw,
        // AT QUEUE TIME (header note (2)). The range is the dagger's own registry
        // column, so the summon path and the encounter path cannot drift apart.
        const int32_t hp =
            random(s.monster_hp_rng,
                   sts::registry::kSnakeDagger.hp_min(kMonsterAscension),
                   sts::registry::kSnakeDagger.hp_max(kMonsterAscension));

        // SpawnMonsterAction's smart positioning -- the COUNT form (header note
        // (4)), run against the simulated list.
        uint8_t pos = 0;
        for (uint8_t i = 0; i < n; ++i) {
            if (x > xs[i]) {
                ++pos;
            }
        }
        for (uint8_t i = n; i > pos; --i) {
            xs[i] = xs[i - 1];
            live[i] = live[i - 1];
        }
        xs[pos] = x;
        live[pos] = true;
        ++n;
        if (pos <= self_index) {
            ++self_index;  // the insert shifted the Reptomancer one to the right
        }

        ActionQueueItem spawn{};
        spawn.opcode = static_cast<uint16_t>(Opcode::SPAWN_MONSTER);
        spawn.src = mi;
        spawn.tgt = pos;
        spawn.amount = hp;
        // isMinion = true (:125) -> the Minion application, at the queue TOP
        // (header note (5)). run_pre_battle stays false: SpawnMonsterAction does
        // not call usePreBattleAction, and a SnakeDagger has none anyway.
        spawn.flags = make_spawn_monster_flags(
            static_cast<uint16_t>(MonsterId::SNAKE_DAGGER), x,
            /*run_pre_battle=*/false, /*apply_minion=*/true,
            /*minion_at_top=*/true);
        add_to_bottom(s, spawn);
        ++spawned;
    }

    // The trailing RollMoveAction (:136) targets the Reptomancer's POST-insertion
    // index; pending queue items are not remapped across a spawn.
    queue_roll_move(s, mi, self_index);
}

}  // namespace

int32_t reptomancer_alive_count(const CombatState& s, uint8_t mi) noexcept {
    // canSpawn (:139-146): `if (m == this || m.isDying) continue; ++aliveCount;`
    // -- NOT restricted to daggers, and NOT excluding escaped records.
    int32_t count = 0;
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        if (i == mi || s.monsters[i].hp <= 0) {
            continue;
        }
        ++count;
    }
    return count;
}

void reptomancer_decide_move(CombatState& s, uint8_t mi, int32_t num) noexcept {
    MonsterState& m = s.monsters[mi];
    if ((m.flags & kMonsterFlagReptomancerFirstMove) != 0u) {
        // (:169-173). Unconditional SPAWN_DAGGER -- `num` is not read, and
        // canSpawn is NOT consulted, so the opening summon always happens.
        m.flags &= ~kMonsterFlagReptomancerFirstMove;
        set_monster_move(m, kSpawnDagger, MonsterIntent::UNKNOWN);
        return;
    }
    // getMove (:174-194). The Java's two tail-recursive calls are the two
    // `continue`s below; each re-draws ai_rng and re-enters the whole function.
    // Termination is probabilistic, not bounded -- `getMove(aiRng.random(65))`
    // from the >= 66 arm WIDENS the range back down, exactly as the Gremlin
    // Leader's second recursion does.
    for (int guard = 0; guard < 1024; ++guard) {
        if (num < 33) {
            if (!last_move_is(m, kSnakeStrike)) {
                set_monster_move(m, kSnakeStrike, MonsterIntent::ATTACK_DEBUFF);
                return;
            }
            num = random(s.ai_rng, 33, 99);  // getMove(random(33, 99)) (:178)
            continue;
        }
        if (num < 66) {
            // (:180-189). canSpawn is read HERE, at decision time.
            if (!last_two_moves_are(m, kSpawnDagger) &&
                reptomancer_alive_count(s, mi) <= kReptomancerMaxOtherAlive) {
                set_monster_move(m, kSpawnDagger, MonsterIntent::UNKNOWN);
            } else {
                set_monster_move(m, kSnakeStrike, MonsterIntent::ATTACK_DEBUFF);
            }
            return;
        }
        if (!last_move_is(m, kBigBite)) {
            set_monster_move(m, kBigBite, MonsterIntent::ATTACK);
            return;
        }
        num = random(s.ai_rng, 65);  // getMove(aiRng.random(65)) (:193)
    }
    assert(false && "Reptomancer.getMove recursion exceeded its depth bound");
}

void reptomancer_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::REPTOMANCER);
    // TWO monster_hp_rng DRAWS, in the Java's order (header note (1)). The first
    // is the `super(...)` argument, carried as the registry roll row so that
    // burn_unspawned_ctor_rolls orders a DISCARDED candidate identically; its
    // value is thrown away here exactly as Java throws it away.
    const sts::registry::MonsterRollDef* super_arg =
        sts::registry::kReptomancer.roll(sts::registry::kReptomancerRollSuperArgHp);
    assert(super_arg != nullptr && "Reptomancer row lost its SUPER_ARG_HP roll");
    (void)random(s.monster_hp_rng, super_arg->min(kMonsterAscension),
                 super_arg->max(kMonsterAscension));
    const int32_t hp =
        random(s.monster_hp_rng,
               sts::registry::kReptomancer.hp_min(kMonsterAscension),
               sts::registry::kReptomancer.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.draw_x = kReptomancerDrawX;  // the ctor's offsetX (:64)
    m.flags |= kMonsterFlagReptomancerFirstMove;  // firstMove = true (:61)
    // init() -> rollMove -> getMove(aiRng.random(99)). The firstMove arm ignores
    // `num`, but the DRAW still happens and still moves the shared stream.
    reptomancer_decide_move(s, mi, random(s.ai_rng, 99));
}

void reptomancer_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (:89-102) -- ONE walk over the group doing two things.
    //
    //     for (m : getMonsters().monsters) {
    //         if (!m.id.equals(this.id))
    //             addToBottom(new ApplyPowerAction(m, m, new MinionPower(this)));
    //         if (!(m instanceof SnakeDagger)) continue;
    //         if (indexOf(m) > indexOf(this)) daggers[0] = m; else daggers[1] = m;
    //     }
    //
    // The Minion arm tests the ID STRING, not the class, so it would spare a
    // second Reptomancer; no group in the roster has one, and the test is
    // written as the Java writes it. It covers EVERY non-Reptomancer record,
    // dagger or not.
    //
    // The dagger arm is also where the two ENCOUNTER daggers' `draw_x` is
    // written, and this is the only place it can be: the positions are an
    // ENCOUNTER property (MonsterHelper.java:536-539 passes Reptomancer.POSX[1]
    // to the dagger before the Reptomancer and POSX[0] to the one after it), not
    // a property of the dagger type, so the shared dagger init cannot know them.
    // The Java licenses exactly this index comparison here and nowhere else --
    // the Gremlin Leader's usePreBattleAction is the same shape for the same
    // reason.
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        if (i == mi) {
            continue;  // `!m.id.equals(this.id)` fails for the Reptomancer itself
        }
        ActionQueueItem minion{};
        minion.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        minion.src = i;  // ApplyPowerAction(m, m, ...) -- source IS the target
        minion.tgt = i;
        minion.amount = kMinionAppliedAmount;  // -1, never assigned by the ctor
        minion.flags = make_apply_power_flags(PowerId::MINION);
        add_to_bottom(s, minion);

        if (s.monsters[i].monster_id !=
            static_cast<uint16_t>(MonsterId::SNAKE_DAGGER)) {
            continue;  // `if (!(m instanceof SnakeDagger)) continue;`
        }
        s.monsters[i].draw_x = i > mi ? kReptomancerDaggerX[0]   // daggers[0]
                                      : kReptomancerDaggerX[1];  // daggers[1]
    }
}

void reptomancer_take_turn(CombatState& s, uint8_t mi) noexcept {
    const uint8_t move = s.monsters[mi].move_history[0];
    if (move == kSpawnDagger) {
        // queue_spawn_daggers queues its own trailing ROLL_MOVE, because only it
        // knows the Reptomancer's post-insertion index.
        queue_spawn_daggers(s, mi);
        return;
    }
    // SNAKE_STRIKE (:107-115) and BIG_BITE (:130-134) are pure registry
    // programs; the ChangeState/Wait/AnimateFastAttack items and the BiteEffect
    // VFX (whose MathUtils.random(-50f, 50f) arguments are UNSEEDED) are
    // presentation.
    if (move == kSnakeStrike || move == kBigBite) {
        queue_monster_move_effects(s, mi, sts::registry::kReptomancer, move);
    }
    // RollMoveAction (:136) sits OUTSIDE the switch. Neither of these two moves
    // inserts a record, so the Reptomancer's index is still mi.
    queue_roll_move(s, mi, mi);
}

void reptomancer_roll_move(CombatState& s, uint8_t mi) noexcept {
    reptomancer_decide_move(s, mi, random(s.ai_rng, 99));
}

void reptomancer_die_after(CombatState& s, uint8_t mi) noexcept {
    // die() AFTER super.die() (:157-165) -- header note (7). `super.die()` has
    // already zeroed this record's HP, so the `isDead || isDying` test excludes
    // the Reptomancer without an `m == this` term, which is exactly why this
    // body is on the post-super side.
    (void)mi;
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        if (s.monsters[i].hp <= 0) {
            continue;
        }
        // addToTop(HideHealthBarAction) is presentation and is dropped; the
        // SuicideAction is addToTop too, so pushing one per survivor in list
        // order makes them RESOLVE IN REVERSE list order -- the Java's order.
        ActionQueueItem suicide{};
        suicide.opcode = static_cast<uint16_t>(Opcode::SUICIDE);
        suicide.src = i;
        suicide.tgt = i;
        // flags bit 0 == triggerRelics, and it is SET: the ONE-ARG SuicideAction
        // ctor defaults it to TRUE (SuicideAction.java:17-19) -- unlike the slime
        // splits' explicit false. So each minion pays the full death edge: its
        // die() body, its powers' onDeath, the player's relics' onMonsterDeath.
        suicide.flags = 1u;
        add_to_top(s, suicide);
    }
}

// --- SnakeDagger --------------------------------------------------------------

namespace {

void snake_dagger_decide_move(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    // getMove (:90-98): a firstMove one-shot, then EXPLODE forever. `num` is
    // ignored on both arms -- but the caller still spent the aiRng.random(99)
    // that produced it (the Taskmaster / Looter / Guardian precedent).
    (void)s;
    if ((m.flags & kMonsterFlagSnakeDaggerFirstMove) != 0u) {
        m.flags &= ~kMonsterFlagSnakeDaggerFirstMove;
        set_monster_move(m, kDaggerWound, MonsterIntent::ATTACK_DEBUFF);
        return;
    }
    set_monster_move(m, kDaggerExplode, MonsterIntent::ATTACK);
}

}  // namespace

void snake_dagger_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::SNAKE_DAGGER);
    // The SUPER ARGUMENT is the only monster_hp_rng draw in the class (:46) and
    // there is no setHp under it, so the registry `hp` column IS that draw.
    const int32_t hp =
        random(s.monster_hp_rng,
               sts::registry::kSnakeDagger.hp_min(kMonsterAscension),
               sts::registry::kSnakeDagger.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    // draw_x is deliberately LEFT AT ZERO here: the position is a per-SPAWNER,
    // per-SLOT constant the dagger type cannot know (monster_dispatch.hpp "WHO
    // SETS draw_x"). The encounter daggers get theirs from the Reptomancer's
    // usePreBattleAction; summoned ones from the SPAWN_MONSTER item's operand.
    m.flags |= kMonsterFlagSnakeDaggerFirstMove;  // firstMove = true (:43)
    (void)random(s.ai_rng, 99);  // init() -> rollMove: the draw happens, num is
                                 // discarded by getMove
    snake_dagger_decide_move(s, mi);
}

void snake_dagger_spawn_at_hp(CombatState& s, uint8_t mi, int16_t hp) noexcept {
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::SNAKE_DAGGER);
    m.hp = hp;
    m.max_hp = hp;
    m.flags |= kMonsterFlagSnakeDaggerFirstMove;
    // m.init() at SpawnMonsterAction.update (:48): the child's aiRng roll, at
    // RESOLVE time. The HP arrived PRE-DRAWN because the Java ctor ran inside
    // takeTurn (header note (2)).
    (void)random(s.ai_rng, 99);
    snake_dagger_decide_move(s, mi);
}

void snake_dagger_take_turn(CombatState& s, uint8_t mi) noexcept {
    const uint8_t move = s.monsters[mi].move_history[0];
    if (move == kDaggerExplode) {
        // case EXPLODE (:69-74): DamageAction(damage.get(1)) then
        // LoseHPAction(this, this, this.currentHealth). The LoseHP amount is read
        // at QUEUE time -- `this.currentHealth` is evaluated when the action is
        // constructed -- so a dagger that is damaged by its own attack's
        // downstream effects still loses the HP it had NOW.
        queue_monster_move_effect(s, mi, sts::registry::kSnakeDagger,
                                  kDaggerExplode, kExplodeDamageStep,
                                  kMoveTargetFromStep);
        ActionQueueItem lose{};
        lose.opcode = static_cast<uint16_t>(Opcode::LOSE_HP);
        lose.src = mi;  // LoseHPAction(target, SOURCE, amount) -- both are `this`
        lose.tgt = mi;
        lose.amount = s.monsters[mi].hp;
        add_to_bottom(s, lose);
    } else {
        // case WOUND (:62-67): damage.get(0) then one Wound into the DISCARD.
        queue_monster_move_effects(s, mi, sts::registry::kSnakeDagger,
                                   kDaggerWound);
    }
    // RollMoveAction (:76) sits OUTSIDE the switch, so a dagger that just blew
    // itself up still rolls -- and RollMoveAction has no liveness gate
    // (RollMoveAction.java:17-21).
    queue_roll_move(s, mi, mi);
}

void snake_dagger_roll_move(CombatState& s, uint8_t mi) noexcept {
    (void)random(s.ai_rng, 99);
    snake_dagger_decide_move(s, mi);
}

}  // namespace sts::engine
