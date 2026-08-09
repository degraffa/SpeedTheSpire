// The Bronze Automaton: native move selection, the two-orb summon (queue-time
// ctor draws, resolve-time init rolls, addToTop Minion applies) and the
// post-super suicide sweep. See monster_bronze_automaton.hpp for provenance and
// the five readings the body leans on.

#include "sts/engine/monster_bronze_automaton.hpp"

#include <cassert>

#include "sts/engine/action_queue.hpp"      // add_to_bottom/add_to_top, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags, make_spawn_monster_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move helpers, kMinionAppliedAmount
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kFlail = sts::registry::kBronzeAutomatonMoveFlail;          // 1
constexpr uint8_t kHyperBeam = sts::registry::kBronzeAutomatonMoveHyperBeam;  // 2
constexpr uint8_t kStunned = sts::registry::kBronzeAutomatonMoveStunned;      // 3
constexpr uint8_t kSpawnOrbs = sts::registry::kBronzeAutomatonMoveSpawnOrbs;  // 4
constexpr uint8_t kBoost = sts::registry::kBronzeAutomatonMoveBoost;          // 5

void queue_roll_move(CombatState& s, uint8_t src, uint8_t tgt) noexcept {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    it.src = src;
    it.tgt = tgt;
    add_to_bottom(s, it);
}

// The SPAWN_ORBS body (takeTurn case 4, BronzeAutomaton.java:110-124). Both
// BronzeOrb ctors run at QUEUE time (they are SpawnMonsterAction constructor
// arguments), so both orbs' monster_hp_rng pairs are drawn HERE, in spawn
// order; each orb's ai_rng init roll happens at its spawn's RESOLVE, and the
// boss's trailing RollMoveAction lands third on ai_rng because it was queued
// behind both spawns. The same local insertion simulation as the Gremlin
// Leader's RALLY (monster_gremlin_leader.cpp queue_rally): smart positioning
// reads only draw_x, which nothing between queue and resolve mutates, and the
// only records inserted in the window are these two spawns.
void queue_spawn_orbs(CombatState& s, uint8_t mi) noexcept {
    const sts::registry::MonsterDef* def =
        sts::registry::monster_def(sts::registry::MonsterId::BRONZE_ORB);
    assert(def != nullptr && "BronzeOrb has no registry row");
    if (def == nullptr) {
        return;
    }
    // The simulated position-key list, as the group will look at each spawn's
    // resolve. Two extra entries because this turn inserts two records.
    int16_t xs[kMonsterCap + 2] = {};
    uint8_t n = s.monster_count;
    for (uint8_t i = 0; i < n && i < kMonsterCap; ++i) {
        xs[i] = s.monsters[i].draw_x;
    }
    uint8_t boss_index = mi;

    for (int orb = 0; orb < 2; ++orb) {
        // The ctor's TWO monster_hp_rng draws, both at queue time: the
        // super(...) argument (the registry roll row -- flat (52,58) at every
        // ascension, BronzeOrb.java:48) whose value is discarded, then the
        // tiered setHp (:49-53). Ranges come from the same registry rows
        // burn_unspawned_ctor_rolls reads, so the two paths cannot drift.
        const sts::registry::MonsterRollDef* super_arg = def->roll(0);
        assert(super_arg != nullptr && "BronzeOrb row lost its SUPER_ARG_HP");
        if (super_arg != nullptr) {
            (void)random(s.monster_hp_rng, super_arg->min(kMonsterAscension),
                         super_arg->max(kMonsterAscension));
        }
        const int32_t hp = random(s.monster_hp_rng,
                                  def->hp_min(kMonsterAscension),
                                  def->hp_max(kMonsterAscension));
        const int16_t x = kBronzeOrbSlotX[orb];

        // Smart positioning against the simulated list (SpawnMonsterAction.
        // java:50-56): strict `>` walk, break at the first failure.
        uint8_t pos = 0;
        while (pos < n && x > xs[pos]) {
            ++pos;
        }
        for (uint8_t i = n; i > pos; --i) {
            xs[i] = xs[i - 1];
        }
        xs[pos] = x;
        ++n;
        if (pos <= boss_index) {
            ++boss_index;  // the insert shifted the boss one to the right
        }

        ActionQueueItem spawn{};
        spawn.opcode = static_cast<uint16_t>(Opcode::SPAWN_MONSTER);
        spawn.src = mi;
        spawn.tgt = pos;
        spawn.amount = hp;
        // NO kSpawnApplyMinion and NO kSpawnRunPreBattle: SpawnMonsterAction
        // applies its Minion addToTop at ITS OWN resolve (:67-69) -- modelled
        // as the very next item below -- and never runs usePreBattleAction
        // (the orb has none anyway).
        spawn.flags = make_spawn_monster_flags(
            static_cast<uint16_t>(MonsterId::BRONZE_ORB), x);
        add_to_bottom(s, spawn);

        // The addToTop'd ApplyPowerAction(m, m, new MinionPower(m)) (:67-69).
        // Queued immediately behind its spawn, which is the position an
        // addToTop at the spawn's resolve occupies: the spawn's own resolution
        // queues nothing ahead of it (interp.hpp kSpawnApplyMinion, the S2.24
        // amendment). The tgt is the orb's post-insertion slot, computed from
        // the SAME simulation -- for THIS spawn's resolve moment, i.e. before
        // any LATER insert can shift it, which is exactly when the item runs.
        ActionQueueItem minion{};
        minion.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        minion.src = pos;
        minion.tgt = pos;
        minion.amount = kMinionAppliedAmount;  // -1, never assigned by the ctor
        minion.flags = make_apply_power_flags(PowerId::MINION);
        add_to_bottom(s, minion);
        // Note minion 1's tgt deliberately does NOT account for spawn 2's
        // insert: it resolves BEFORE spawn 2 does, against exactly the list
        // state the simulation had at this iteration.
    }

    queue_roll_move(s, mi, boss_index);
}

}  // namespace

void bronze_automaton_decide_move(CombatState& s, uint8_t mi,
                                  int32_t num) noexcept {
    (void)num;  // read by NO arm (:149-174); the roll that produced it still
                // moved ai_rng, which is why every caller draws it.
    MonsterState& m = s.monsters[mi];
    // numTurns (pad0). ++ only on the two fall-through arms (:173).
    if (m.pad0 == 4) {
        // (:155-159): the beam turn, and the reset.
        set_monster_move(m, kHyperBeam, MonsterIntent::ATTACK);
        m.pad0 = 0;
        return;
    }
    if (last_move_is(m, kHyperBeam)) {
        // (:160-167): the recovery turn -- BOOST at A19+, STUNNED below.
        if (kMonsterAscension >= 19) {
            set_monster_move(m, kBoost, MonsterIntent::DEFEND_BUFF);
        } else {
            set_monster_move(m, kStunned, MonsterIntent::STUN);
        }
        return;
    }
    if (last_move_is(m, kStunned) || last_move_is(m, kBoost) ||
        last_move_is(m, kSpawnOrbs)) {
        // (:168-169): setMove(1, ATTACK, dmg[0].base, 2, true).
        set_monster_move(m, kFlail, MonsterIntent::ATTACK);
    } else {
        set_monster_move(m, kBoost, MonsterIntent::DEFEND_BUFF);  // (:171)
    }
    if (m.pad0 < 255) {
        ++m.pad0;  // (:173) -- reached only by the two arms above
    }
}

void bronze_automaton_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::BRONZE_AUTOMATON);
    // The super(...) HP argument is a LITERAL 300 (:71) -- no draw. setHp(320)/
    // setHp(300) (:78-84) is the single-arg overload, i.e. setHp(hp, hp), and
    // the two-arg body draws unconditionally even for a degenerate range
    // (Random.java:58-61) -- the Hexaghost reading. ONE draw.
    const auto& def = sts::registry::kBronzeAutomaton;
    const int32_t hp = random(s.monster_hp_rng, def.hp_min(kMonsterAscension),
                              def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = m.hp;
    m.draw_x = kBronzeAutomatonDrawX;  // the ctor's offsetX (:71)
    // init() -> rollMove -> getMove(aiRng.random(99)): the firstTurn arm
    // (:150-154) forces SPAWN_ORBS/UNKNOWN and clears the latch -- consumed on
    // this one call (the Snecko precedent, so no storage), but the draw is
    // taken regardless and the stream moves.
    (void)random(s.ai_rng, 99);
    set_monster_move(m, kSpawnOrbs, MonsterIntent::UNKNOWN);
}

void bronze_automaton_use_pre_battle_action(CombatState& s,
                                            uint8_t mi) noexcept {
    // usePreBattleAction (:99-105): BGM/scene/UnlockTracker are presentation;
    // the content is ONE queued ApplyPowerAction(this, this,
    // ArtifactPower(this, 3)) -- the 3 is a FLAT literal at every ascension
    // (:103; the dispatching brief guessed a branch here, the source says no).
    ActionQueueItem artifact{};
    artifact.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    artifact.src = mi;
    artifact.tgt = mi;
    artifact.amount = 3;
    artifact.flags = make_apply_power_flags(PowerId::ARTIFACT);
    add_to_bottom(s, artifact);
}

void bronze_automaton_roll_move(CombatState& s, uint8_t mi) noexcept {
    bronze_automaton_decide_move(s, mi, random(s.ai_rng, 99));
}

void bronze_automaton_take_turn(CombatState& s, uint8_t mi) noexcept {
    const uint8_t move = s.monsters[mi].move_history[0];
    if (move == kSpawnOrbs) {
        // queue_spawn_orbs queues its own trailing ROLL_MOVE, because only it
        // knows the boss's post-insertion index (header note (4)).
        queue_spawn_orbs(s, mi);
        return;
    }
    // FLAIL / HYPER_BEAM / BOOST are their registry programs; STUNNED's is the
    // authored NOP (takeTurn case 3 is a TextAboveCreatureAction only, :142).
    queue_monster_move_effects(s, mi, sts::registry::kBronzeAutomaton, move);
    // The RollMoveAction at :145 sits OUTSIDE the switch -- every move reaches
    // it, the stunned turn included. None of these moves inserts a record, so
    // the boss's index is still mi.
    queue_roll_move(s, mi, mi);
}

void bronze_automaton_die_after(CombatState& s, uint8_t mi) noexcept {
    // die() AFTER super.die() (:181-187): onBossVictoryLogic (achievements,
    // not sim-visible), then
    //     for (m : monsters) { if (m.isDead || m.isDying) continue;
    //         addToTop(HideHealthBar); addToTop(Suicide(m)); addToTop(VFX); }
    // Only the SuicideAction is sim-visible; it is the 1-ARG ctor, so
    // relicTrigger is TRUE (flags bit 0 SET) and a swept orb runs the full
    // death edge -- its Stasis onDeath returns the stolen card. The forward
    // walk pushing one add_to_top item per survivor reproduces the net LIFO
    // resolve order (reverse slot order). A half-dead record would pass the
    // Java's test and be swept; no Act-2 boss group can hold one, and the
    // engine predicate (hp > 0) says so rather than guessing at the case.
    (void)mi;  // super.die() already zeroed the boss's own HP
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        if (s.monsters[i].hp <= 0) {
            continue;
        }
        ActionQueueItem sweep{};
        sweep.opcode = static_cast<uint16_t>(Opcode::SUICIDE);
        sweep.src = i;
        sweep.tgt = i;
        sweep.flags = 1u;  // die(relicTrigger = true)
        add_to_top(s, sweep);
    }
}

}  // namespace sts::engine
