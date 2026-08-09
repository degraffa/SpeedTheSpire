// The Champ: native move selection over the threshold latch, the every-call
// turn counter and the A19-widened forge arm. See monster_champ.hpp for
// provenance and the four readings.

#include "sts/engine/monster_champ.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move helpers
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kHeavySlash = sts::registry::kChampMoveHeavySlash;  // 1
constexpr uint8_t kStance = sts::registry::kChampMoveDefensiveStance; // 2
constexpr uint8_t kExecute = sts::registry::kChampMoveExecute;        // 3
constexpr uint8_t kFaceSlap = sts::registry::kChampMoveFaceSlap;      // 4
constexpr uint8_t kGloat = sts::registry::kChampMoveGloat;            // 5
constexpr uint8_t kTaunt = sts::registry::kChampMoveTaunt;            // 6
constexpr uint8_t kAnger = sts::registry::kChampMoveAnger;            // 7

[[nodiscard]] bool threshold_reached(const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagChampThreshold) != 0u;
}

void set_num_turns(MonsterState& m, uint8_t v) noexcept {
    m.pad0 = static_cast<uint8_t>((m.pad0 & 0xF0u) | (v & 0x0Fu));
}
void set_forge_times(MonsterState& m, uint8_t v) noexcept {
    m.pad0 = static_cast<uint8_t>((m.pad0 & 0x0Fu) |
                                  (static_cast<unsigned>(v & 0x0Fu) << 4));
}

}  // namespace

void champ_decide_move(CombatState& s, uint8_t mi, int32_t num) noexcept {
    MonsterState& m = s.monsters[mi];
    // `++this.numTurns;` (:262) -- FIRST, on EVERY call, the init roll
    // included. Saturating at the nibble; the ==4 test below is unreachable
    // past 4 without the reset anyway (header note (1)).
    if (champ_num_turns(m) < 15) {
        set_num_turns(m, static_cast<uint8_t>(champ_num_turns(m) + 1));
    }
    // (:263-267): the below-half one-shot, latched at decision time.
    if (m.hp < m.max_hp / 2 && !threshold_reached(m)) {
        m.flags |= kMonsterFlagChampThreshold;
        set_monster_move(m, kAnger, MonsterIntent::BUFF);
        return;
    }
    // (:268-272): EXECUTE whenever neither of the last two decisions was it.
    // The addToTop TalkAction is presentation on an unseeded picker.
    if (!last_move_is(m, kExecute) && !last_move_before_is(m, kExecute) &&
        threshold_reached(m)) {
        set_monster_move(m, kExecute, MonsterIntent::ATTACK);
        return;
    }
    // (:273-277): the TAUNT cadence, dead once the threshold latches.
    if (champ_num_turns(m) == 4 && !threshold_reached(m)) {
        set_monster_move(m, kTaunt, MonsterIntent::DEBUFF);
        set_num_turns(m, 0);
        return;
    }
    // (:278-288): the forge arm -- roll bound 30 at A19+, 15 below; both arms
    // spelled, the fixed A20 taking the first. ++forgeTimes at DECISION time.
    {
        const int32_t bound = kMonsterAscension >= 19 ? 30 : 15;
        if (!last_move_is(m, kStance) && champ_forge_times(m) < 2 &&
            num <= bound) {
            set_forge_times(m, static_cast<uint8_t>(champ_forge_times(m) + 1));
            set_monster_move(m, kStance, MonsterIntent::DEFEND_BUFF);
            return;
        }
    }
    // (:289-292).
    if (!last_move_is(m, kGloat) && !last_move_is(m, kStance) && num <= 30) {
        set_monster_move(m, kGloat, MonsterIntent::BUFF);
        return;
    }
    // (:293-296).
    if (!last_move_is(m, kFaceSlap) && num <= 55) {
        set_monster_move(m, kFaceSlap, MonsterIntent::ATTACK_DEBUFF);
        return;
    }
    // (:297-301).
    if (!last_move_is(m, kHeavySlash)) {
        set_monster_move(m, kHeavySlash, MonsterIntent::ATTACK);
    } else {
        set_monster_move(m, kFaceSlap, MonsterIntent::ATTACK_DEBUFF);
    }
}

void champ_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::CHAMP);
    // super HP argument is a LITERAL 420 (:95) -- no draw; setHp(440)/setHp(420)
    // (:103-107) is the single-arg overload: ONE degenerate monster_hp_rng draw.
    const auto& def = sts::registry::kChamp;
    const int32_t hp = random(s.monster_hp_rng, def.hp_min(kMonsterAscension),
                              def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = m.hp;
    m.draw_x = kChampDrawX;  // the ctor's offsetX (:95)
    // init() -> rollMove -> getMove(aiRng.random(99)). At full HP with an
    // empty history the reachable openers are STANCE (num <= bound), GLOAT
    // (num <= 30), FACE_SLAP (num <= 55) and HEAVY_SLASH -- and numTurns is
    // already 1 after this call (header note (1)).
    champ_decide_move(s, mi, random(s.ai_rng, 99));
}

void champ_roll_move(CombatState& s, uint8_t mi) noexcept {
    champ_decide_move(s, mi, random(s.ai_rng, 99));
}

void champ_take_turn(CombatState& s, uint8_t mi) noexcept {
    // Every case is its registry program -- ANGER's REMOVE_DEBUFFS /
    // REMOVE_POWER(Shackled) / Strength included (:169-171). The firstTurn
    // Champion Belt TalkAction (:156-161) and every SFX/VFX/Shout are
    // presentation on unseeded pickers.
    const uint8_t move = s.monsters[mi].move_history[0];
    queue_monster_move_effects(s, mi, sts::registry::kChamp, move);
    // RollMoveAction (:214), OUTSIDE the switch.
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
