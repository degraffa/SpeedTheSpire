// The Guardian's offensive/defensive mode state machine. See the public header
// for the full Java provenance, the queue/RNG contract, and the reasoning behind
// the pad0 HP-baseline accumulator.

#include "sts/engine/monster_guardian.hpp"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/registry/monster_table.hpp"
#include "powers/power_native.hpp"  // find_power (the Mode Shift slot)

namespace sts::engine {
namespace {

// Move ids, TheGuardian.java:78-84.
constexpr uint8_t kCloseUp = 1;
constexpr uint8_t kFierceBash = 2;
constexpr uint8_t kRollAttack = 3;
constexpr uint8_t kTwinSlam = 4;
constexpr uint8_t kWhirlwind = 5;
constexpr uint8_t kChargeUp = 6;
constexpr uint8_t kVentSteam = 7;

// dmgThresholdIncrease (TheGuardian.java:61), added on every Defensive-Mode
// entry (:245) -- the growth that makes each successive flip cost more damage.
constexpr int32_t kThresholdIncrease = 10;

// DEFENSIVE_BLOCK (TheGuardian.java:72), gained on entering Defensive Mode (:240).
constexpr int32_t kDefensiveBlock = 20;

// The largest flip count kMonsterFlagGuardianShiftMask can hold. Unreachable in
// play (see the combat_state.hpp note); the increment saturates here rather than
// wrapping into the neighbouring latches.
constexpr uint32_t kMaxShiftCount =
    kMonsterFlagGuardianShiftMask >> kMonsterFlagGuardianShiftShift;

[[nodiscard]] uint32_t guardian_shift_count(const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagGuardianShiftMask) >>
           kMonsterFlagGuardianShiftShift;
}

// Re-glue the accumulator baseline to the current HP, so that the derived
// `dmgTaken` (baseline - hp) reads zero. This is what stands in for Java's
// `dmgTaken = 0` and for every damage event its guard rejects; see the header.
void guardian_reset_accumulator(MonsterState& m) noexcept {
    const int32_t hp = m.hp < 0 ? 0 : static_cast<int32_t>(m.hp);
    m.pad0 = static_cast<uint8_t>(hp > 255 ? 255 : hp);
}

// changeState "Defensive Mode" (TheGuardian.java:237-251). The Java action does
// three things synchronously -- grow the threshold, setMove(CLOSE_UP) and clear
// isOpen -- and queues the power removal and block gain at the bottom, so the
// resolution order is setMove, RemoveSpecificPower, GainBlock. Nothing runs
// between this call and that resolution, so the two latches and the counter are
// applied here rather than through a queued item.
void queue_defensive_mode(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];

    // dmgThreshold += dmgThresholdIncrease (:245), saturating (see kMaxShiftCount).
    const uint32_t shifts = guardian_shift_count(m);
    if (shifts < kMaxShiftCount) {
        m.flags = (m.flags & ~kMonsterFlagGuardianShiftMask) |
                  ((shifts + 1u) << kMonsterFlagGuardianShiftShift);
    }
    m.flags &= ~kMonsterFlagGuardianOpen;  // isOpen = false (:248)

    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::SET_MOVE);
    it.src = mi;
    it.tgt = mi;
    it.amount = kCloseUp;
    it.flags = static_cast<uint32_t>(MonsterIntent::BUFF);
    add_to_bottom(s, it);  // setMove(CLOSEUP_NAME, CLOSE_UP, Intent.BUFF) (:246)

    it = ActionQueueItem{};
    it.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
    it.src = mi;
    it.tgt = mi;
    it.flags = make_apply_power_flags(PowerId::MODE_SHIFT);
    add_to_bottom(s, it);  // RemoveSpecificPowerAction("Mode Shift") (:238)

    it = ActionQueueItem{};
    it.opcode = static_cast<uint16_t>(Opcode::BLOCK);
    it.src = mi;
    it.tgt = mi;
    it.amount = kDefensiveBlock;
    it.flags = kBlockNoPowers;  // a direct GainBlockAction: no card applyPowers
    add_to_bottom(s, it);       // GainBlockAction(this, this, 20) (:240)
}

// changeState "Offensive Mode" (TheGuardian.java:253-266), reached only from
// Twin Slam. The Java ChangeStateAction is queued FIRST by useTwinSmash and so
// resolves before the two slams; its own three actions go to the BOTTOM and
// therefore land after them. Reproduced by setting the latches here (nothing
// runs in between) and queueing the three items after the move's data program.
// "Reset Threshold" (:255,268-270) needs no queued item: the accumulator is
// gated on the Mode Shift power being present, so damage reflected into the gap
// before ApplyPower resolves is ignored -- exactly what Java's reset discards.
void queue_offensive_mode_tail(CombatState& s, uint8_t mi,
                               int32_t block_at_entry) noexcept {
    MonsterState& m = s.monsters[mi];

    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    it.src = mi;
    it.tgt = mi;
    it.amount = guardian_mode_shift_threshold(m);  // the GROWN threshold (:254)
    it.flags = make_apply_power_flags(PowerId::MODE_SHIFT);
    add_to_bottom(s, it);

    // LoseBlockAction(this, this, currentBlock), and ONLY when block != 0
    // (:256-258). The amount is captured at the change of state, as in the Java.
    if (block_at_entry != 0) {
        it = ActionQueueItem{};
        it.opcode = static_cast<uint16_t>(Opcode::BLOCK);
        it.src = mi;
        it.tgt = mi;
        it.amount = -block_at_entry;
        it.flags = kBlockNoPowers;
        add_to_bottom(s, it);
    }
}

}  // namespace

int32_t guardian_base_mode_shift_threshold(int32_t ascension) noexcept {
    // The ctor's DESCENDING chain (TheGuardian.java:97-106). Kept native rather
    // than as a registry column because monsters.yaml carries tier columns only
    // for HP and for move-effect amounts, and this is neither; expressing it as
    // a `rolls:` entry would misfile a deterministic constant as an RNG draw.
    if (ascension >= 19) {
        return 40;  // A_19_DMG_THRESHOLD (:59, :99)
    }
    if (ascension >= 9) {
        return 35;  // A_2_DMG_THRESHOLD (:58, :102)
    }
    return 30;      // DMG_THRESHOLD (:57, :106)
}

int32_t guardian_mode_shift_threshold(const MonsterState& m) noexcept {
    return guardian_base_mode_shift_threshold(kMonsterAscension) +
           kThresholdIncrease * static_cast<int32_t>(guardian_shift_count(m));
}

void guardian_init(CombatState& s, uint8_t mi) noexcept {
    const auto& def = sts::registry::kTheGuardian;
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::THE_GUARDIAN);
    // setHp(int) is a FIXED SHEET but NOT a free one. All three ctor branches
    // (TheGuardian.java:98, :101, :104) call setHp(int) == `setHp(hp, hp)`
    // (AbstractMonster.java:777-779), and the two-arg overload draws
    // `monsterHpRng.random(minHp, maxHp)` unconditionally (:765-767).
    // Random.random(int,int) increments the counter and calls
    // nextInt(end - start + 1) even when start == end (Random.java:58-61), so
    // the degenerate range still consumes one draw and advances the stream.
    const int32_t rolled = random(s.monster_hp_rng, def.hp_min(kMonsterAscension),
                                  def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = m.hp;
    m.block = 0;
    m.flags = kMonsterFlagGuardianOpen;  // isOpen = true (:76)
    m.power_count = 0;                   // Mode Shift arrives at pre-battle
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    guardian_reset_accumulator(m);       // dmgTaken = 0 (:62 default)

    // AbstractMonster.init -> rollMove -> aiRng.random(99), then getMove
    // (:226-232) ignores `num`: isOpen is still true, so the opener is forced to
    // CHARGE_UP. The draw is consumed either way -- it is stream state.
    (void)random(s.ai_rng, 99);
    set_monster_move(m, kChargeUp, MonsterIntent::DEFEND);
}

void guardian_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (TheGuardian.java:122-132) minus presentation and the
    // UnlockTracker call: ApplyPowerAction(this, this, ModeShiftPower(this,
    // dmgThreshold)) then ChangeStateAction(RESET_THRESH). Applied directly
    // rather than queued -- pre-battle runs before the first pump step, so there
    // is nothing for the queue to order it against, and the Louse curl-up
    // (monster_louse.cpp) sets the same precedent.
    MonsterState& m = s.monsters[mi];
    if (m.power_count >= kPowerCap) {
        return;
    }
    m.powers[m.power_count].power_id = static_cast<uint16_t>(PowerId::MODE_SHIFT);
    m.powers[m.power_count].amount =
        static_cast<int16_t>(guardian_mode_shift_threshold(m));
    ++m.power_count;
    guardian_reset_accumulator(m);  // ChangeStateAction(RESET_THRESH) (:130,269)
}

void guardian_take_turn(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    const auto& def = sts::registry::kTheGuardian;
    switch (m.move_history[0]) {
        // --- offensive cycle ---
        case kChargeUp:  // useChargeUp (:218-223): block 9, then Fierce Bash
            queue_monster_move_effects(s, mi, def, kChargeUp);
            set_monster_move(m, kFierceBash, MonsterIntent::ATTACK);
            return;
        case kFierceBash:  // useFierceBash (:171-175): damage.get(0), then Vent Steam
            queue_monster_move_effects(s, mi, def, kFierceBash);
            set_monster_move(m, kVentSteam, MonsterIntent::STRONG_DEBUFF);
            return;
        case kVentSteam:  // useVentSteam (:177-181): Weak 2 + Vulnerable 2, then Whirlwind
            queue_monster_move_effects(s, mi, def, kVentSteam);
            set_monster_move(m, kWhirlwind, MonsterIntent::ATTACK);
            return;
        case kWhirlwind:  // useWhirlwind (:207-216): 4 x damage.get(2), then Charge Up
            queue_monster_move_effects(s, mi, def, kWhirlwind);
            set_monster_move(m, kChargeUp, MonsterIntent::DEFEND);
            return;

        // --- defensive cycle ---
        case kCloseUp:  // useCloseUp (:183-191): Sharp Hide on itself, then Roll
            queue_monster_move_effects(s, mi, def, kCloseUp);
            set_monster_move(m, kRollAttack, MonsterIntent::ATTACK);
            return;
        case kRollAttack:  // useRollAttack (:201-205): damage.get(1), then Twin Slam
            queue_monster_move_effects(s, mi, def, kRollAttack);
            set_monster_move(m, kTwinSlam, MonsterIntent::ATTACK_BUFF);
            return;
        case kTwinSlam: {
            // useTwinSmash (:193-199). The change of state is queued FIRST in the
            // Java and resolves before the slams, so its synchronous half (the
            // latches) happens here; its queued half lands after the move's data
            // program, which is exactly where the Java's addToBottom puts it.
            const int32_t block_at_entry = m.block;
            m.flags |= kMonsterFlagGuardianOpen;
            m.flags &= ~kMonsterFlagGuardianCloseUpTriggered;  // (:262-263)
            queue_monster_move_effects(s, mi, def, kTwinSlam);
            queue_offensive_mode_tail(s, mi, block_at_entry);
            set_monster_move(m, kWhirlwind, MonsterIntent::ATTACK);
            return;
        }
        default:
            return;
    }
}

void guardian_on_damaged(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    // TheGuardian.damage, after super.damage (:279): isOpen && !closeUpTriggered
    // && tmpHealth > currentHealth && !isDying. The Mode Shift slot stands in for
    // the last of Java's two redundant accumulators, so its absence also gates
    // the block -- see the header for why that is faithful and not a shortcut.
    PowerSlot* mode_shift = find_power(s, mi, PowerId::MODE_SHIFT);
    const bool open = (m.flags & kMonsterFlagGuardianOpen) != 0;
    const bool close_up_triggered =
        (m.flags & kMonsterFlagGuardianCloseUpTriggered) != 0;
    if (!open || close_up_triggered || mode_shift == nullptr || m.hp <= 0) {
        guardian_reset_accumulator(m);
        return;
    }
    const int32_t taken = static_cast<int32_t>(m.pad0) - static_cast<int32_t>(m.hp);
    if (taken <= 0) {
        return;  // fully blocked: `tmpHealth > currentHealth` is false (:279)
    }
    const int32_t threshold = guardian_mode_shift_threshold(m);
    // getPower("Mode Shift").amount -= tmpHealth - currentHealth (:281-284).
    mode_shift->amount = static_cast<int16_t>(threshold - taken);
    if (taken < threshold) {
        return;
    }
    guardian_reset_accumulator(m);  // dmgTaken = 0 (:286)
    m.flags |= kMonsterFlagGuardianCloseUpTriggered;  // (:289)
    queue_defensive_mode(s, mi);  // ChangeStateAction(DEFENSIVE_MODE) (:288)
}

}  // namespace sts::engine
