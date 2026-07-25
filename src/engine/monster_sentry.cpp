// Sentry AI + monster turn + the pre-battle Artifact. Move selection is native;
// stats/effects come from the generated table. See monster_sentry.hpp for
// provenance, scope, and the draw-counting convention.

#include "sts/engine/monster_sentry.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move-history helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/registry/monster_table.hpp"   // kSentry + move-id constants

namespace sts::engine {

namespace {

constexpr uint8_t kBolt = sts::registry::kSentryMoveBolt;  // 3
constexpr uint8_t kBeam = sts::registry::kSentryMoveBeam;  // 4

// getMove (Sentry.java:134-150). `num` is drawn by the caller and never read.
// `monster_index` is the engine's stand-in for
// AbstractDungeon.getMonsters().monsters.lastIndexOf(this): both are the
// monster's position in the group's spawn-ordered list, and the whole group is
// constructed before any init() runs, so the index is settled by the time the
// first decision is made.
void sentry_get_move(MonsterState& m, uint8_t monster_index) noexcept {
    if (m.move_history[0] == 0) {
        // firstMove (Sentry.java:136-143): even slot opens on Bolt, odd on Beam.
        // An empty move ring is exactly "getMove has not run yet".
        if ((monster_index % 2u) == 0u) {
            set_monster_move(m, kBolt, MonsterIntent::DEBUFF);
        } else {
            set_monster_move(m, kBeam, MonsterIntent::ATTACK);
        }
        return;
    }
    // Strict alternation afterwards (Sentry.java:145-149).
    if (last_move_is(m, kBeam)) {
        set_monster_move(m, kBolt, MonsterIntent::DEBUFF);
    } else {
        set_monster_move(m, kBeam, MonsterIntent::ATTACK);
    }
}

}  // namespace

void sentry_init(CombatState& state, uint8_t monster_index) noexcept {
    const auto& def = sts::registry::kSentry;
    MonsterState& m = state.monsters[monster_index];
    m.monster_id = static_cast<uint16_t>(MonsterId::SENTRY);

    // setHp(39,45) at A20: one inclusive monster_hp_rng draw, currentHealth ==
    // maxHealth (AbstractMonster.setHp).
    const int32_t rolled = random(state.monster_hp_rng,
                                  def.hp_min(kMonsterAscension),
                                  def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = static_cast<int16_t>(rolled);

    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;

    // init() -> rollMove() -> getMove(aiRng.random(99)); the value is unused in
    // every branch, but rollMove ALWAYS draws.
    (void)random(state.ai_rng, 99);
    sentry_get_move(m, monster_index);
}

void sentry_use_pre_battle_action(CombatState& state,
                                  uint8_t monster_index) noexcept {
    // ApplyPowerAction(this, this, new ArtifactPower(this, 1)) (Sentry.java:81).
    // One stack, applied to the Sentry itself; no RNG draw.
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = monster_index;
    apply.tgt = monster_index;
    apply.amount = 1;
    apply.flags = make_apply_power_flags(PowerId::ARTIFACT);
    add_to_bottom(state, apply);
}

void sentry_take_turn(CombatState& state, uint8_t monster_index) noexcept {
    MonsterState& m = state.monsters[monster_index];
    const uint8_t move = m.move_history[0];

    // (a) The decided move's effects: Bolt's Dazed-into-discard, Beam's damage.
    queue_monster_move_effects(state, monster_index, sts::registry::kSentry,
                               move);

    // (b) The unconditional trailing RollMoveAction (Sentry.java:112) -> one
    //     aiRng.random(99) draw.
    (void)random(state.ai_rng, 99);
    sentry_get_move(m, monster_index);
}

}  // namespace sts::engine
