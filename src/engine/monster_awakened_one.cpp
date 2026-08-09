// The Awakened One: native move selection over two disjoint trees, the
// damage()-driven phase transition, and a die() the room's cannotLose latch
// suppresses outright. See monster_awakened_one.hpp for provenance, the five
// traps and the one recorded deviation.

#include "sts/engine/monster_awakened_one.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom / add_to_top, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effect(s), move-history helpers, kMonsterAscension
#include "sts/engine/power_hooks.hpp"       // dispatch_on_death
#include "sts/engine/powers.hpp"            // power_def / PowerType (the purge's DEBUFF test)
#include "sts/engine/relic_hooks.hpp"       // player_relics, dispatch_relics_on_monster_death
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kSlash = sts::registry::kAwakenedOneMoveSlash;            // 1
constexpr uint8_t kSoulStrike = sts::registry::kAwakenedOneMoveSoulStrike;  // 2
constexpr uint8_t kRebirth = sts::registry::kAwakenedOneMoveRebirth;        // 3
constexpr uint8_t kDarkEcho = sts::registry::kAwakenedOneMoveDarkEcho;      // 5
constexpr uint8_t kSludge = sts::registry::kAwakenedOneMoveSludge;          // 6
constexpr uint8_t kTackle = sts::registry::kAwakenedOneMoveTackle;          // 8

// The REBIRTH row's two steps, in changeState's own addToBottom order
// (AwakenedOne.java:225-226). Named rather than bare indices so the coupling to
// monsters.yaml id 62 is visible from here.
constexpr uint8_t kRebirthStepHeal = 0;
constexpr uint8_t kRebirthStepCanLose = 1;

// usePreBattleAction's four amounts (AwakenedOne.java:144-153). These are
// PRE-BATTLE grants, not move effects, and the row schema carries only moves --
// so they live here with their citations, the Snake Plant / Byrd precedent.
constexpr int32_t kRegenAmount = 10;      // REGEN_AMT (:90); 15 from A19 (:145)
constexpr int32_t kRegenAmountA19 = 15;
constexpr int32_t kCuriosityAmount = 1;   // STR_AMT (:91); 2 from A19 (:146)
constexpr int32_t kCuriosityAmountA19 = 2;
constexpr int32_t kUnawakenedAmount = -1;  // UnawakenedPower.java:21 -- a MARKER,
                                           // not a stack count; canGoNegative is
                                           // false so the game renders nothing.
constexpr int32_t kA4StrengthAmount = 2;   // A_4_STR (:82), applied at A4+ (:152-153)

[[nodiscard]] bool form1(const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagAwakenedForm1) != 0u;
}
[[nodiscard]] bool first_turn(const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagAwakenedFirstTurn) != 0u;
}

void queue_apply(CombatState& s, uint8_t mi, PowerId id, int32_t amount) noexcept {
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;
    apply.tgt = mi;
    apply.amount = amount;
    apply.flags = make_apply_power_flags(id);
    add_to_bottom(s, apply);
}

// The Rebirth purge (AwakenedOne.java:302-308):
//
//     Iterator s = this.powers.iterator();
//     while (s.hasNext()) {
//         AbstractPower p = s.next();
//         if (p.type != DEBUFF && !p.ID.equals("Curiosity")
//             && !p.ID.equals("Unawakened") && !p.ID.equals("Shackled")) continue;
//         s.remove();
//     }
//
// So: every DEBUFF, plus those three by ID. Shackled is itself a DEBUFF, so the
// third name is redundant and is kept only because the Java keeps it.
//
// STRENGTH AND REGENERATE SURVIVE. Both are BUFFs and neither is named, which is
// the live gameplay fact behind the whole move: an A4+ boss walks into phase 2
// still carrying +2 Strength and whatever Curiosity bought it, and its Regenerate
// keeps healing it every turn.
//
// COMPACTED IN PLACE, NOT THROUGH remove_slot_at, and that is deliberate: the
// Java uses Iterator.remove() on the live list and never calls onRemove(), so
// Hook::ON_POWER_REMOVED must NOT fire. Unobservable today -- that hook's only
// binders are Plated Armor and Flight, neither of which can be on this boss --
// but firing it would be wrong, and the choke-point rule exists precisely so that
// a bypass is a deliberate, argued act rather than an oversight.
void purge_for_rebirth(MonsterState& m) noexcept {
    uint8_t out = 0;
    for (uint8_t i = 0; i < m.power_count && i < kPowerCap; ++i) {
        const PowerId id = static_cast<PowerId>(m.powers[i].power_id);
        const PowerDef* def = power_def(id);
        const bool is_debuff = def != nullptr && def->type == PowerType::DEBUFF;
        const bool named = id == PowerId::CURIOSITY || id == PowerId::UNAWAKENED ||
                           id == PowerId::SHACKLED;
        if (is_debuff || named) {
            continue;  // dropped
        }
        m.powers[out] = m.powers[i];
        ++out;
    }
    for (uint8_t i = out; i < m.power_count && i < kPowerCap; ++i) {
        m.powers[i] = PowerSlot{};  // zero the vacated tail rows (byte-hash stability)
    }
    m.power_count = out;
}

}  // namespace

void awakened_one_decide_move(CombatState& s, uint8_t mi, int32_t num) noexcept {
    MonsterState& m = s.monsters[mi];
    if (form1(m)) {
        // Phase 1 (:244-260).
        if (first_turn(m)) {
            set_monster_move(m, kSlash, MonsterIntent::ATTACK);  // (:246)
            m.flags &= ~kMonsterFlagAwakenedFirstTurn;           // (:247) -- CLEARED here
            return;
        }
        if (num < 25) {
            // `!lastMove(2) ? SOUL_STRIKE : SLASH` (:251-255).
            set_monster_move(m, last_move_is(m, kSoulStrike) ? kSlash : kSoulStrike,
                             MonsterIntent::ATTACK);
            return;
        }
        // `!lastTwoMoves(1) ? SLASH : SOUL_STRIKE` (:256-259).
        set_monster_move(m, last_two_moves_are(m, kSlash) ? kSoulStrike : kSlash,
                         MonsterIntent::ATTACK);
        return;
    }
    // Phase 2 (:261-277).
    if (first_turn(m)) {
        // (:262-265) -- returns WITHOUT clearing the latch, unlike phase 1's arm.
        // takeTurn case 5 clears it instead (:183), which is why the roll between
        // the Rebirth turn and Dark Echo still sees it set (and still spends its
        // ai_rng draw).
        set_monster_move(m, kDarkEcho, MonsterIntent::ATTACK);
        return;
    }
    if (num < 50) {
        // `!lastTwoMoves(6) ? SLUDGE : TACKLE` (:267-271).
        set_monster_move(m, last_two_moves_are(m, kSludge) ? kTackle : kSludge,
                         last_two_moves_are(m, kSludge) ? MonsterIntent::ATTACK
                                                        : MonsterIntent::ATTACK_DEBUFF);
        return;
    }
    // `!lastTwoMoves(8) ? TACKLE : SLUDGE` (:272-276).
    const bool blocked = last_two_moves_are(m, kTackle);
    set_monster_move(m, blocked ? kSludge : kTackle,
                     blocked ? MonsterIntent::ATTACK_DEBUFF : MonsterIntent::ATTACK);
}

void awakened_one_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::AWAKENED_ONE);
    // setHp(320) / setHp(300) (:110-114) is the SINGLE-ARG overload, i.e.
    // setHp(hp, hp) (AbstractMonster.java:777-779), and the two-arg body draws
    // monsterHpRng.random(min, max) unconditionally -- Random.random(int,int)
    // increments the counter and calls nextInt(1) even for a degenerate range
    // (Random.java:58-61). The value can only be the endpoint; the OBSERVABLE IS
    // THE STREAM. Same reading as the Hexaghost's row.
    const auto& def = sts::registry::kAwakenedOne;
    const int32_t rolled = random(s.monster_hp_rng, def.hp_min(kMonsterAscension),
                                  def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = m.hp;
    m.block = 0;
    // form1 = true (:75) and firstTurn = true (:76), both FIELD INITIALIZERS.
    m.flags = kMonsterFlagAwakenedForm1 | kMonsterFlagAwakenedFirstTurn;
    m.power_count = 0;
    m.pad0 = 0;  // unused by this monster
    m.draw_x = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). getMove's
    // phase-1 firstTurn arm ignores `num`, so the opener is always SLASH -- but
    // the draw is consumed either way, and it is stream state.
    awakened_one_decide_move(s, mi, random(s.ai_rng, 99));
}

void awakened_one_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (:139-156). The BGM/scene calls at :140-142 and the
    // UnlockTracker at :155 are presentation.
    //
    // `AbstractDungeon.getCurrRoom().cannotLose = true;` (:143) is a DIRECT FIELD
    // WRITE, not a queued CannotLoseAction -- so it is synchronous here, ahead of
    // the four queued grants below. That latch is what keeps the combat open while
    // the boss sits half-dead with both Cultists already dead, and it is also the
    // condition die() reads to suppress itself.
    s.flags |= kCombatFlagCannotLose;

    const bool a19 = kMonsterAscension >= 19;
    // addToBottom order (:145-153).
    queue_apply(s, mi, PowerId::REGENERATE_MONSTER,
                a19 ? kRegenAmountA19 : kRegenAmount);
    queue_apply(s, mi, PowerId::CURIOSITY,
                a19 ? kCuriosityAmountA19 : kCuriosityAmount);
    // UnawakenedPower(this) -- the 1-arg ctor hard-codes amount = -1
    // (UnawakenedPower.java:21), and ApplyPowerAction's 3-arg form takes the
    // stack amount FROM the constructed power, so -1 is what lands. It is a
    // marker, not a count: nothing in combat reads it, and the Rebirth purge
    // removes it by id.
    queue_apply(s, mi, PowerId::UNAWAKENED, kUnawakenedAmount);
    if (kMonsterAscension >= 4) {
        queue_apply(s, mi, PowerId::STRENGTH, kA4StrengthAmount);  // (:152-153)
    }
}

void awakened_one_roll_move(CombatState& s, uint8_t mi) noexcept {
    awakened_one_decide_move(s, mi, random(s.ai_rng, 99));
}

void awakened_one_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (:159-208). Every SFXAction / ChangeStateAction / WaitAction /
    // VFXAction / AnimateFastAttackAction in the switch is presentation on
    // unseeded clocks.
    MonsterState& m = s.monsters[mi];
    const uint8_t move = m.move_history[0];

    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;

    if (move == kRebirth) {
        // Case 3 (:174-178) queues ONLY a ChangeStateAction("REBIRTH"); the
        // trailing RollMoveAction (:207) goes behind it. changeState (:213-227)
        // then addToBottom's the heal and the CanLose when that action RESOLVES,
        // i.e. BEHIND the roll. So the resolve order is
        //     [roll] [heal] [can_lose]
        // and queueing the three in that order here is byte-identical: nothing
        // else can append to the bottom in between (the ChangeStateAction is the
        // very next item, and applyTurnPowers has no binder on this boss).
        //
        // maxHealth's re-assert at :214 is a no-op -- STAGE_2_HP == STAGE_1_HP
        // and A_9_STAGE_2_HP == A_9_STAGE_1_HP (:78-81) -- and the isEndless /
        // MonsterHunter arms at :215-221 are pinned false.
        //
        // `halfDead = false` (:223) is NOT done here; op_heal clears the flag when
        // the heal below lands. See the deviation note in the header.
        add_to_bottom(s, roll);
        queue_monster_move_effect(s, mi, sts::registry::kAwakenedOne, move,
                                  kRebirthStepHeal, kMoveTargetFromStep);
        queue_monster_move_effect(s, mi, sts::registry::kAwakenedOne, move,
                                  kRebirthStepCanLose, kMoveTargetFromStep);
        return;
    }

    if (move == kDarkEcho) {
        // `this.firstTurn = false;` at :183 -- SYNCHRONOUS, mid-case, and this is
        // the ONLY place phase 2's latch is ever cleared.
        m.flags &= ~kMonsterFlagAwakenedFirstTurn;
    }
    queue_monster_move_effects(s, mi, sts::registry::kAwakenedOne, move);
    add_to_bottom(s, roll);  // RollMoveAction (:207), OUTSIDE the switch
}

void awakened_one_on_damaged(CombatState& s, uint8_t mi, int32_t hp_lost) noexcept {
    (void)hp_lost;  // the Java tests currentHealth, not the delta
    MonsterState& m = s.monsters[mi];
    // `if (this.currentHealth <= 0 && !this.halfDead)` (:291).
    //
    // IT FIRES TWICE OVER A FULL FIGHT, and both are intended:
    //   * PHASE 1 -- cannotLose is set, so die() suppressed super.die() entirely
    //     and this block is the ONLY thing that runs the two death fan-outs. It
    //     latches halfDead and turns the boss around.
    //   * PHASE 2 -- Rebirth's CanLoseAction cleared the latch, so the real death
    //     ran super.die() (fan-outs #1) and then FELL BACK INTO THIS BLOCK, which
    //     runs them again (#2). halfDead is false by then, so nothing stops it.
    //     Gremlin Horn binds onMonsterDeath, so it pays twice. Faithful, and
    //     deliberately not "fixed".
    if (m.hp > 0 || monster_half_dead(m)) {
        return;
    }
    // `if (getCurrRoom().cannotLose) halfDead = true;` (:292-294). CONDITIONAL:
    // on the phase-2 death the latch is gone and the boss stays plain dead.
    if ((s.flags & kCombatFlagCannotLose) != 0u) {
        m.flags |= kMonsterFlagHalfDead;
    }
    // `for (p : this.powers) p.onDeath();` (:295-297) then
    // `for (r : player.relics) r.onMonsterDeath(this);` (:298-300) -- the two
    // fan-outs super.die() would have done, re-fired by hand. Same order.
    dispatch_on_death(s, mi);
    const RelicView rv = player_relics(s);
    dispatch_relics_on_monster_death(s, rv.relics, rv.count, mi);

    // `addToTop(new ClearCardQueueAction())` (:301) -- the TOP, so it lands ahead
    // of everything already queued and the cards the player lined up behind the
    // killing blow never resolve.
    ActionQueueItem clear{};
    clear.opcode = static_cast<uint16_t>(Opcode::CLEAR_CARD_QUEUE);
    clear.src = mi;
    clear.tgt = mi;
    add_to_top(s, clear);

    purge_for_rebirth(m);  // (:302-308) -- see the helper for what survives

    // `setMove((byte)3, Intent.UNKNOWN); this.createIntent();` (:309-310) --
    // SYNCHRONOUS. createIntent is presentation.
    set_monster_move(m, kRebirth, MonsterIntent::UNKNOWN);
    // ShoutAction (:311) is presentation. SetMoveAction(this, 3, UNKNOWN) (:312)
    // is NOT redundant with the synchronous set above: if the boss was downed
    // during its OWN turn, its takeTurn already queued a RollMoveAction at the
    // bottom, which would overwrite the synchronous set -- and this item, landing
    // behind that roll, wins. Model both.
    ActionQueueItem set_move{};
    set_move.opcode = static_cast<uint16_t>(Opcode::SET_MOVE);
    set_move.src = mi;
    set_move.tgt = mi;
    set_move.amount = kRebirth;
    set_move.flags = static_cast<uint32_t>(MonsterIntent::UNKNOWN);
    add_to_bottom(s, set_move);

    // applyPowers() (:313) recomputes the telegraphed intent damage -- presentation.
    // `firstTurn = true; form1 = false;` (:314-315).
    m.flags |= kMonsterFlagAwakenedFirstTurn;
    m.flags &= ~kMonsterFlagAwakenedForm1;
    // The GameActionManager.turn <= 1 achievement (:316-318) is not sim-visible.
}

bool awakened_one_die(CombatState& s, uint8_t mi) noexcept {
    (void)mi;
    // die() (:356-375): `if (!AbstractDungeon.getCurrRoom().cannotLose) { ... }`
    // wraps the ENTIRE body, super.die() included (:357-358). So there is no
    // pre-super content at all, and the only thing this slot does is answer the
    // veto: while the room is cannotLose, NOTHING of super.die() may run -- not
    // isDying, not the powers' onDeath, not the relics' onMonsterDeath. The
    // damage() override above re-fires those two by hand, exactly once.
    return (s.flags & kCombatFlagCannotLose) != 0u;
}

void awakened_one_die_after(CombatState& s, uint8_t mi) noexcept {
    (void)mi;
    // The post-`super.die()` half (:359-373), reached only when the veto did NOT
    // fire -- i.e. the phase-2 death, after Rebirth cleared cannotLose.
    //
    // useFastShakeAnimation / screenShake.rumble (:359-360) are presentation. The
    // `saidPower` speech (:361-365) is UNREACHABLE dead code -- see the header.
    //
    //     for (AbstractMonster m : getCurrRoom().monsters.monsters)
    //         if (m.isDying || !(m instanceof Cultist)) continue;
    //         addToBottom(new EscapeAction(m));                       (:366-369)
    //
    // The surviving Cultists FLEE. Note what the filter does NOT test: isEscaping
    // is not checked, so a Cultist that already left gets a second (idempotent)
    // ESCAPE item; and `isDying` is the true predicate, not `hp <= 0` -- though a
    // Cultist can never be half-dead, so the two coincide for this walk. Group
    // (slot) order, one item each.
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        const MonsterState& c = s.monsters[i];
        if (c.monster_id != static_cast<uint16_t>(MonsterId::CULTIST)) {
            continue;
        }
        if (c.hp <= 0 && !monster_half_dead(c)) {
            continue;  // `if (m.isDying ...) continue;`
        }
        ActionQueueItem esc{};
        esc.opcode = static_cast<uint16_t>(Opcode::ESCAPE);
        esc.src = mi;
        esc.tgt = i;
        add_to_bottom(s, esc);
    }
    // onBossVictoryLogic (:370) and onFinalBossVictoryLogic (:373) carry NO
    // sim-visible content: the former is achievements + StatsScreen, the latter
    // (AbstractMonster.java:1058-1085) is achievements + stopClock and skips its
    // whole body anyway at A20 with two bosses left. s2-design section 4.4 asked
    // for this re-check at task time; that is the answer, and it is a negative.
}

}  // namespace sts::engine
