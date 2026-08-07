// The Snecko: native move selection and turn body, including the one
// ascension-gated STEP in the roster. See monster_snecko.hpp for provenance, the
// draw accounting, why `firstTurn` needs no storage, and the Confusion-amount
// evidence.

#include "sts/engine/monster_snecko.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effect(s), move-history helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kGlare = sts::registry::kSneckoMoveGlare;  // 1
constexpr uint8_t kBite = sts::registry::kSneckoMoveBite;    // 2
constexpr uint8_t kTail = sts::registry::kSneckoMoveTail;    // 3

// TAIL's three authored steps, by index into the registry row's effect list.
// Named because the middle one is SKIPPED below A17 and an unexplained `1` at
// the skip site would be exactly the kind of magic number that rots.
constexpr uint8_t kTailStepDamage = 0;
constexpr uint8_t kTailStepWeak = 1;      // A17+ ONLY (Snecko.java:112-114)
constexpr uint8_t kTailStepVulnerable = 2;

// getMove (Snecko.java:141-158), MINUS step 1 -- the `firstTurn` GLARE opener,
// which only ever fires on init()'s rollMove and is telegraphed there directly
// (see the header). Everything below is the method as written. There is no
// ascension branch anywhere in it. `num` is the aiRng.random(99) already drawn.
void snecko_get_move(MonsterState& m, int32_t num) noexcept {
    if (num < 40) {
        // (:147-150). Note the telegraph is ATTACK_DEBUFF, not ATTACK: Tail
        // carries the Vulnerable (and, at A17, the Weak) as well as damage.
        set_monster_move(m, kTail, MonsterIntent::ATTACK_DEBUFF);
        return;
    }
    if (last_two_moves_are(m, kBite)) {
        set_monster_move(m, kTail, MonsterIntent::ATTACK_DEBUFF);  // (:152-153)
    } else {
        set_monster_move(m, kBite, MonsterIntent::ATTACK);         // (:155)
    }
}

}  // namespace

void snecko_init(CombatState& s, uint8_t mi) noexcept {
    // The ctor is `super(...)` + setHp(min, max): exactly one monster_hp_rng
    // inclusive draw, currentHealth == maxHealth (AbstractMonster.java:765-775);
    // at A20 the A7 column (120, 125) is the live one (Snecko.java:73-77). The
    // no-arg ctor MonsterHelper uses (:60-62) only forwards a position.
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::SNECKO);
    const int32_t hp =
        random(s.monster_hp_rng, sts::registry::kSnecko.hp_min(kMonsterAscension),
               sts::registry::kSnecko.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;  // unused: `firstTurn` needs no storage (monster_snecko.hpp)
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). The draw
    // ALWAYS happens; getMove then returns from the `firstTurn` branch
    // (:142-146) without ever reading num -- so the opening move is GLARE at
    // every seed and the stream still advances by one.
    (void)random(s.ai_rng, 99);
    set_monster_move(m, kGlare, MonsterIntent::STRONG_DEBUFF);
}

void snecko_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    snecko_get_move(s.monsters[mi], num);
}

void snecko_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (Snecko.java:89-121). The RollMoveAction at :120 sits OUTSIDE the
    // switch, so all three cases reach it.
    const uint8_t move = s.monsters[mi].move_history[0];
    if (move == kTail) {
        // THE ONE MOVE THAT CANNOT USE THE WHOLE-PROGRAM HELPER. The Java is
        //     DamageAction(damage.get(1));
        //     if (ascensionLevel >= 17) ApplyPowerAction(Weak 2, true);
        //     ApplyPowerAction(Vulnerable 2, true);
        // (:108-118) -- so the Weak step's PRESENCE, not its amount, is what the
        // ascension decides, and it sits BETWEEN the other two. The registry row
        // carries all three steps (it is the pinned amount source at every
        // tier); the skip lives here, which keeps the engine exact at every
        // ascension instead of applying a 0-amount Weak below A17. See the
        // monsters.yaml row and monster_snecko.hpp note (5).
        queue_monster_move_effect(s, mi, sts::registry::kSnecko, kTail,
                                  kTailStepDamage, kMoveTargetFromStep);
        if (snecko_tail_applies_weak(kMonsterAscension)) {
            queue_monster_move_effect(s, mi, sts::registry::kSnecko, kTail,
                                      kTailStepWeak, kMoveTargetFromStep);
        }
        queue_monster_move_effect(s, mi, sts::registry::kSnecko, kTail,
                                  kTailStepVulnerable, kMoveTargetFromStep);
    } else {
        // GLARE (:90-98) and BITE (:100-106) are each presentation plus the
        // registry program: ChangeState / SFX / IntimidateEffect / FastShake for
        // the glare, ChangeState / Wait / BiteEffect for the bite. None of it
        // draws seeded RNG -- the BiteEffect's position jitter is unseeded
        // MathUtils.
        queue_monster_move_effects(s, mi, sts::registry::kSnecko, move);
    }
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
