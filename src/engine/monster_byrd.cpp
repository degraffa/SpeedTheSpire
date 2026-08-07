// The Byrd: native move selection, the airborne latch, and the turn body. See
// monster_byrd.hpp for provenance, the two-state machine, the per-branch draw
// accounting, and why isFlying is not derived from the Flight power.

#include "sts/engine/monster_byrd.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move-history helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kPeck = sts::registry::kByrdMovePeck;              // 1
constexpr uint8_t kGoAirborne = sts::registry::kByrdMoveGoAirborne;  // 2
constexpr uint8_t kSwoop = sts::registry::kByrdMoveSwoop;            // 3
constexpr uint8_t kStunned = sts::registry::kByrdMoveStunned;        // 4
constexpr uint8_t kHeadbutt = sts::registry::kByrdMoveHeadbutt;      // 5
constexpr uint8_t kCaw = sts::registry::kByrdMoveCaw;                // 6

// Byrd.flightAmt (Byrd.java:83): 4 from A17, 3 below. Read from the GO_AIRBORNE
// move's APPLY_POWER step rather than restated, so the pre-battle grant and the
// in-combat re-grant cannot drift apart -- they are the SAME field in the Java
// (:103 and :126 both pass this.flightAmt).
[[nodiscard]] int32_t flight_amount() noexcept {
    const sts::registry::MonsterMove* mv =
        sts::registry::kByrd.move(kGoAirborne);
    return mv != nullptr ? mv->effects[0].amount.at(kMonsterAscension) : 0;
}

void queue_apply_flight(CombatState& s, uint8_t mi) noexcept {
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;
    apply.tgt = mi;
    apply.amount = flight_amount();
    apply.flags = make_apply_power_flags(PowerId::FLIGHT);
    add_to_bottom(s, apply);
}

// getMove (Byrd.java:175-219), MINUS the firstMove branch, which fires only on
// init()'s rollMove and is handled there. `num` is the aiRng.random(99) the
// caller already drew; `s` is threaded because three of the four arms may spend
// a SECOND draw.
//
// EVERY randomBoolean BELOW SITS INSIDE ITS ARM'S HISTORY GUARD, exactly as the
// Java nests them -- so the draw is spent only when the guard holds. That is the
// whole reason this cannot be an ai table: the number of ai_rng draws a turn
// costs depends on the move history.
void byrd_get_move(CombatState& s, MonsterState& m, int32_t num) noexcept {
    if (!byrd_is_flying(m)) {
        // (:216-218): a grounded Byrd HEADBUTTs, unconditionally. `num` is
        // already drawn and is never read on this path.
        set_monster_move(m, kHeadbutt, MonsterIntent::ATTACK);
        return;
    }
    if (num < 50) {                                     // (:187-196)
        if (last_two_moves_are(m, kPeck)) {
            if (random_boolean(s.ai_rng, 0.4f)) {       // (:189)
                set_monster_move(m, kSwoop, MonsterIntent::ATTACK);
            } else {
                set_monster_move(m, kCaw, MonsterIntent::BUFF);
            }
        } else {
            set_monster_move(m, kPeck, MonsterIntent::ATTACK);
        }
        return;
    }
    if (num < 70) {                                     // (:197-206)
        if (last_move_is(m, kSwoop)) {
            if (random_boolean(s.ai_rng, 0.375f)) {     // (:199)
                set_monster_move(m, kCaw, MonsterIntent::BUFF);
            } else {
                set_monster_move(m, kPeck, MonsterIntent::ATTACK);
            }
        } else {
            set_monster_move(m, kSwoop, MonsterIntent::ATTACK);
        }
        return;
    }
    if (last_move_is(m, kCaw)) {                        // (:207-212)
        if (random_boolean(s.ai_rng, 0.2857f)) {        // (:208)
            set_monster_move(m, kSwoop, MonsterIntent::ATTACK);
        } else {
            set_monster_move(m, kPeck, MonsterIntent::ATTACK);
        }
        return;
    }
    set_monster_move(m, kCaw, MonsterIntent::BUFF);     // (:214)
}

}  // namespace

void byrd_init(CombatState& s, uint8_t mi) noexcept {
    // The ctor is `super(...)` + setHp(min, max): exactly one monster_hp_rng
    // inclusive draw, currentHealth == maxHealth (AbstractMonster.java:765-775).
    // At A20 the A7 column (26, 33) is live (Byrd.java:79).
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::BYRD);
    const int32_t hp =
        random(s.monster_hp_rng, sts::registry::kByrd.hp_min(kMonsterAscension),
               sts::registry::kByrd.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.block = 0;
    // `private boolean isFlying = true;` (Byrd.java:72) -- a Byrd enters the
    // fight airborne, before usePreBattleAction has granted it any Flight.
    m.flags = kMonsterFlagByrdFlying;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // TWO ai_rng draws, in this order. AbstractMonster.init -> rollMove ->
    // getMove(aiRng.random(99)) makes the first, which the firstMove branch
    // (:177-185) never reads; that branch then makes the second itself, a
    // randomBoolean(0.375) choosing CAW over PECK (:179). Both are on ai_rng and
    // the order matters for a multi-Byrd group's stream.
    (void)random(s.ai_rng, 99);
    if (random_boolean(s.ai_rng, 0.375f)) {
        set_monster_move(m, kCaw, MonsterIntent::BUFF);     // (:180)
    } else {
        set_monster_move(m, kPeck, MonsterIntent::ATTACK);  // (:182)
    }
}

void byrd_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (Byrd.java:101-104): addToBottom ApplyPowerAction(this,
    // this, new FlightPower(this, flightAmt)). No RNG draw. The APPLY_POWER also
    // writes the slot's `counter` (storedAmount) -- that is op_apply_power's
    // new-slot path, not this call site (see power_flight.hpp).
    queue_apply_flight(s, mi);
}

void byrd_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    byrd_get_move(s, s.monsters[mi], num);
}

void byrd_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (Byrd.java:106-146).
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];

    if (move == kHeadbutt) {
        // (:117-122) -- THE ONE CASE THAT RETURNS EARLY. The damage is the
        // registry program (a flat 3, damage.get(2)); then a SYNCHRONOUS
        // setMove(GO_AIRBORNE, UNKNOWN) at :120 and `return` at :121, so the
        // trailing RollMoveAction at :145 is never reached and no ai_rng draw is
        // spent this turn. The setMove pushes move id 2 onto the history ring
        // right here, which is what the next decision reads as `lastMove`.
        queue_monster_move_effects(s, mi, sts::registry::kByrd, move);
        set_monster_move(m, kGoAirborne, MonsterIntent::UNKNOWN);
        return;
    }

    if (move == kGoAirborne) {
        // (:123-128): `isFlying = true` is SYNCHRONOUS at :124, BEFORE the
        // queued ChangeStateAction(FLY_STATE) (animation + hitbox only,
        // :155-160) and before the queued ApplyPowerAction that re-grants Flight
        // at :126 -- which is the registry program below. The ordering is the
        // reason isFlying cannot be read off the Flight power.
        m.flags |= kMonsterFlagByrdFlying;
    }

    // Cases 1 / 2 / 3 / 4 / 6. Peck's per-hit SFX (:112) and the Caw case's
    // SFXAction + TalkAction (:130-131) are presentation on the UNSEEDED libGDX
    // generator; the STUNNED case (:140-143) is presentation in its entirety and
    // carries a NOP program.
    queue_monster_move_effects(s, mi, sts::registry::kByrd, move);
    // The trailing RollMoveAction (:145), OUTSIDE the switch.
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
