#pragma once

// The Giant Head (registry/monsters.yaml id 58, MonsterId::GIANT_HEAD) -- the
// Act-3 solo elite whose whole fight is a countdown (GiantHead.java, 175 lines,
// read in full).
//
// WHAT IS NON-OBVIOUS HERE, in the order a reader meets it:
//
// (1) `count` IS THE MONSTER. It starts at the field initializer 5 (:53), and
//     every getMove writes it: while it is >= 2 the ordinary arm decrements it
//     and picks Glare or Count off `num`; once it reaches 1 or less the monster
//     is locked into IT_IS_TIME forever and each decision decrements again until
//     the floor at -6 (:155-160). So the fight is: a fixed number of harmless
//     turns, then an escalating unblockable-sized smash every turn. It lives in
//     MonsterState::pad0 with the bias below, for the Book of Stabbing's reason
//     (a per-instance integer, not a latch, and no flag bit can hold it).
//
// (2) THE A18 PRE-BATTLE DECREMENT LANDS AFTER THE FIRST ROLL, NOT BEFORE IT.
//     usePreBattleAction (:80-86) does `--this.count` at ascension >= 18, and
//     usePreBattleAction runs in the pre-battle phase, i.e. AFTER every ctor and
//     init() (design §5.2; monster_dispatch.cpp's spawn_group then
//     use_pre_battle_actions, and MonsterGroup does the same). So at A20 the
//     OPENING getMove sees count == 5 and leaves 4, and the pre-battle then
//     takes it to 3 -- the A18 arm removes a turn from the MIDDLE of the
//     countdown, not from its start. Getting this backwards costs the player one
//     whole turn of preparation, and no test would notice: both orders produce a
//     legal-looking opening telegraph.
//
// (3) THE IT_IS_TIME RAMP IS ARITHMETIC AND THE ROW OWNS ITS BASE. The ctor
//     builds EIGHT DamageInfos (:70-77) -- index 0 is the flat Count damage 13,
//     indices 1..7 are startingDeathDmg + 0,5,10,...,30 -- and takeTurn picks
//     `index = 1 - count`, CLAMPED at 7 (:106-109). Since index i (i >= 1) holds
//     sdd + (i-1)*5, the damage is exactly `sdd - count*5`, which is also the
//     number getMove telegraphs (:159). This module therefore reads the single
//     tiered amount out of the registry row (30, 40 from A3) and adds the step,
//     instead of re-authoring the 30/40 branch in code or spelling eight rows.
//     The clamp is spelled TWICE in the Java -- once as the index cap, once as
//     the `count > -6` floor -- and both are reproduced, because they are only
//     equivalent while nothing else writes count.
//
// (4) THE SLOW IS THE POINT OF THE PRE-BATTLE ACTION, and it arrives at AMOUNT
//     ZERO (:82, `new SlowPower(this, 0)`). Slow (powers.yaml 106) grows by one
//     on every card the player plays and resets to zero at end of round, so the
//     Giant Head takes +10% per card played THIS TURN and the zero is the
//     correct starting value, not a missing argument. See power_slow.hpp.
//
// (5) NOTHING HERE DRAWS A SEEDED STREAM EXCEPT THE ROLLMOVE. playSfx (:116-125)
//     and getTimeQuote (:138-145) both use UNSEEDED MathUtils, as does
//     playDeathSfx (:127-136) -- so die() is presentation and the Giant Head
//     registers an explicit nullptr MonsterDieFn (the Taskmaster reading).

#include <cstdint>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// `count`'s storage bias. The Java value ranges over 5..-6 inclusive (start 5,
// floored by `if (count > -6) --count`), so it does not fit an unsigned byte
// directly. pad0 holds `count + 6`, i.e. 11 down to 0. A bias rather than an
// int8_t round-trip because every read/write then stays inside the unsigned
// domain and needs no signed-conversion cast under -Wconversion.
inline constexpr uint8_t kGiantHeadCountBias = 6u;
// GiantHead.count's field initializer (GiantHead.java:53).
inline constexpr int32_t kGiantHeadStartCount = 5;
// The floor: `if (this.count > -6) --this.count;` (:156-158).
inline constexpr int32_t kGiantHeadMinCount = -6;
// `index = 1 - count; if (index > 7) index = 7;` (:106-109) -- the same floor
// seen from the damage table's side.
inline constexpr int32_t kGiantHeadMaxDamageIndex = 7;
// The ascension at which usePreBattleAction spends a countdown turn (:83).
inline constexpr int32_t kGiantHeadCountAscension = 18;

[[nodiscard]] inline int32_t giant_head_count(const MonsterState& m) noexcept {
    return static_cast<int32_t>(m.pad0) - static_cast<int32_t>(kGiantHeadCountBias);
}
inline void giant_head_set_count(MonsterState& m, int32_t count) noexcept {
    m.pad0 = static_cast<uint8_t>(count + static_cast<int32_t>(kGiantHeadCountBias));
}

void giant_head_init(CombatState& state, uint8_t monster_index) noexcept;
void giant_head_use_pre_battle_action(CombatState& state,
                                      uint8_t monster_index) noexcept;
void giant_head_take_turn(CombatState& state, uint8_t monster_index) noexcept;
void giant_head_roll_move(CombatState& state, uint8_t monster_index) noexcept;

// getMove (:153-174), exposed so tier-2 tests can drive both sides of the A3
// startingDeathDmg branch and every `num` arm without a seed search. `num` is
// the rolled 0..99; `ascension` resolves the damage column the IT_IS_TIME arm
// telegraphs. MUTATES `count`, which is the whole reason the row is ai: native.
void giant_head_decide_move(CombatState& state, uint8_t monster_index,
                            int32_t num, int32_t ascension) noexcept;

// The IT_IS_TIME damage for a given post-decrement `count`, at `ascension`:
// `startingDeathDmg - count * 5`, with startingDeathDmg read from the registry
// row's own tier column and `count` clamped by the index cap. Shared by the
// telegraph and by takeTurn so the two cannot disagree.
[[nodiscard]] int32_t giant_head_it_is_time_damage(int32_t count,
                                                   int32_t ascension) noexcept;

}  // namespace sts::engine
