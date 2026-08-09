// The Maw: native move selection and turn body. See monster_maw.hpp for
// provenance, the turnCount walkthrough and the zero-HP-draw ctor shape.

#include "sts/engine/monster_maw.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effect(s), move-history helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kRoar = sts::registry::kMawMoveRoar;        // 2
constexpr uint8_t kSlam = sts::registry::kMawMoveSlam;        // 3
constexpr uint8_t kDrool = sts::registry::kMawMoveDrool;      // 4
constexpr uint8_t kNom = sts::registry::kMawMoveNomnomnom;    // 5

// Maw's HP: the ctor's super() argument (Maw.java:41,61), flat at every
// ascension. Read from the row so the number has ONE home; the row's single tier
// column is the whole truth here.
[[nodiscard]] int16_t maw_hp() noexcept {
    return static_cast<int16_t>(
        sts::registry::kMaw.hp_min(kMonsterAscension));
}

}  // namespace

// getMove (Maw.java:117-136). The `++this.turnCount` at :118 is the FIRST thing
// the method does, before any gate, and it happens on EVERY call -- including
// the one AbstractMonster.init makes. That is why the increment lives here
// rather than in the callers: it is part of the decision, and a caller that
// forgot it would produce a Maw whose bites never grow.
void maw_decide_move(MonsterState& m, int32_t num) noexcept {
    // ++turnCount, SATURATING at 255 rather than wrapping. The Java field is an
    // unbounded int; 255 would be 127 bites of 5 in one turn before Strength, so
    // a combat that gets there ended long ago. Saturating states the case is
    // unreachable; wrapping would silently shrink the bite count instead.
    if (m.pad0 < 0xFFu) {
        m.pad0 = static_cast<uint8_t>(m.pad0 + 1);
    }
    // (1) The forced opener (:119-122). `roared` is set by takeTurn, NOT here,
    // so this gate holds until the ROAR has actually resolved -- and since init's
    // rollMove runs before any turn, the opening telegraph is always ROAR.
    if ((m.flags & kMonsterFlagMawRoared) == 0u) {
        set_monster_move(m, kRoar, MonsterIntent::STRONG_DEBUFF);
        return;
    }
    // (2) The only gate that reads the roll (:123-129). The Java's if/else here
    // picks between the 3-arg and 6-arg setMove overloads -- a TELEGRAPH
    // difference (a bare number versus "5 x N"), not a mechanical one -- so both
    // arms collapse to the same decision.
    if (num < 50 && !last_move_is(m, kNom)) {
        set_monster_move(m, kNom, MonsterIntent::ATTACK);
        return;
    }
    // (3) After a Slam or a Nom, buff (:131-134). Note this is lastMove twice,
    // NOT lastTwoMoves: one preceding attack is enough.
    if (last_move_is(m, kSlam) || last_move_is(m, kNom)) {
        set_monster_move(m, kDrool, MonsterIntent::BUFF);
        return;
    }
    // (4) The fallthrough (:135).
    set_monster_move(m, kSlam, MonsterIntent::ATTACK);
}

void maw_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::MAW);
    // NO monster_hp_rng DRAW. setHp is never called (Maw.java:60-82 has no such
    // line); the ctor passes maxHealth 300 to super and AbstractMonster's ctor
    // (AbstractMonster.java:135-155) sets `currentHealth = maxHealth` without
    // touching any RNG. Rolling a degenerate 300..300 range would LOOK
    // equivalent and would consume a value.
    const int16_t hp = maw_hp();
    m.hp = hp;
    m.max_hp = hp;
    m.block = 0;
    // `private boolean roared = false;` (:55) -- clear.
    m.flags = 0;
    m.power_count = 0;
    // `private int turnCount = 1;` (:56). The init rollMove below immediately
    // pre-increments it to 2.
    m.pad0 = kMawInitialTurnCount;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). The draw
    // happens and its value is offered to gate 2 -- but gate 1 answers first
    // from a clear `roared`, so the opening telegraph is ROAR regardless. The
    // turnCount side effect DOES land: 1 -> 2.
    maw_decide_move(m, random(s.ai_rng, 99));
}

void maw_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    maw_decide_move(s.monsters[mi], num);
}

void maw_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (Maw.java:85-114). The SFXAction / ShoutAction /
    // AnimateSlowAttackAction / BiteEffect VFX around the real steps are
    // presentation; the BiteEffect's MathUtils position jitter is UNSEEDED and
    // costs nothing. The RollMoveAction at :113 sits OUTSIDE the switch, so all
    // four cases reach it.
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];
    if (move == kNom) {
        // NOMNOMNOM FANS ONE AUTHORED STEP OUT `turnCount / 2` TIMES (:106-109).
        // The count is per-instance and unbounded, which no effect list can
        // carry, so the row authors the template and this emits it -- through
        // the SHARED per-step helper, not a new one (the Healer's precedent).
        //
        // Read the count from the record NOW: the loop queues, it does not
        // resolve, and nothing in the loop changes turnCount.
        const int32_t bites = static_cast<int32_t>(m.pad0) / 2;
        for (int32_t i = 0; i < bites; ++i) {
            queue_monster_move_effect(s, mi, sts::registry::kMaw, kNom,
                                      /*effect_index=*/0, kMoveTargetFromStep);
        }
    } else {
        queue_monster_move_effects(s, mi, sts::registry::kMaw, move);
    }
    if (move == kRoar) {
        // `this.roared = true;` (:92) -- SYNCHRONOUS, set inside takeTurn AFTER
        // the two ApplyPowerActions are queued but BEFORE they resolve. It does
        // not matter that they have not landed: nothing reads `roared` except
        // getMove, and the next getMove is the trailing roll below.
        m.flags |= kMonsterFlagMawRoared;
    }
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
