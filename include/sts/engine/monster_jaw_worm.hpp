#pragma once

// Jaw Worm monster module. Stats, HP ranges, and move effects are DATA from
// registry/monsters.yaml, code-generated into the constexpr monster table
// (<build>/generated/sts/registry/monster_table.hpp, Stage B design §4.2/§4.3)
// -- there is no hand-written stat table anymore. Move *selection* (the getMove
// decision tree) stays native per design §4.2's budget: the AbstractMonster
// move-history machinery doesn't fit the table shape. jaw_worm_take_turn
// matches the pump's MonsterTurnFn signature exactly (action_queue.hpp) and is
// passed straight to pump() as its step-5 monster-turn hook.
//
// Provenance (read from D:\STS_BG_Mod\SlayTheSpireDecompiled; stat/move numbers
// are cited per-column in registry/monsters.yaml):
//   * com.megacrit.cardcrawl.monsters.exordium.JawWorm -- getMove decision tree
//     (JawWorm.java:148-184), takeTurn's unconditional RollMoveAction
//     (:120-146), ascension stat branches (:81-104), move ids
//     CHOMP=1/BELLOW=2/THRASH=3 (:65-67).
//   * com.megacrit.cardcrawl.monsters.AbstractMonster -- rollMove() ==
//     getMove(aiRng.random(99)) (:465-467), init() calls rollMove() once before
//     the first turn (:712-715), setMove appends nextMove to moveHistory at
//     DECISION time (:431-437), lastMove/lastTwoMoves history predicates
//     (:469-491), setHp(min,max) == monsterHpRng.random(min,max) with
//     currentHealth = maxHealth (:765-775).
//
// SCOPE: jaw_worm_take_turn ENQUEUES the current move's effect program from the
// generated table (Chomp damage, Bellow Strength+block, Thrash damage+block) in
// JawWorm.takeTurn's addToBottom order (JawWorm.java:120-146), then rolls the
// next move. The move to execute is the decided move in move_history[0];
// effects are queued (never applied inline) so they resolve through the pump
// exactly like a card's.
//
// DRAW-COUNTING CONVENTION (must match tests/fixtures/gen_jaw_worm_fixture.py to
// the draw): jaw_worm_init performs decision #1 -- rollMove's forced-first-move
// path, which STILL consumes one aiRng.random(99) draw (rollMove always draws)
// but ignores its value and forces Chomp. Each jaw_worm_take_turn call executes
// the currently-decided move (enqueues its effects, see SCOPE) then rolls the
// NEXT move:
// always one aiRng.random(99) draw, plus on the tiebreak branches one
// aiRng.randomBoolean draw -- so the aiRng counter advances by 1 or 2 per turn.
// monsterHpRng is drawn exactly once, in jaw_worm_init.

#include <cstdint>

#include "sts/engine/combat_state.hpp"
#include "sts/registry/monster_table.hpp"  // generated: kJawWorm def (build tree)

namespace sts::engine {

// The skeleton plays S1's fixed difficulty: Ironclad / Act 1 / A20 (design doc
// §9; stage-b §1.1). The generated table carries the full per-ascension-tier
// columns; this constant selects which column the skeleton combat reads until
// the run layer threads a real ascension through combat_begin.
inline constexpr int32_t kSkeletonAscension = 20;

// Jaw Worm move ids -- byte-identical to JawWorm.java:65-67 (generated from
// monsters.yaml move_id), so fixture rows and move_history bytes cross-reference
// the source directly. 0 stays the move_history empty-slot sentinel (no move id
// is 0), which lets last_move / last_two_moves test move_history slots without
// a separate length field.
inline constexpr uint8_t kMoveChomp = sts::registry::kJawWormMoveChomp;
inline constexpr uint8_t kMoveBellow = sts::registry::kJawWormMoveBellow;
inline constexpr uint8_t kMoveThrash = sts::registry::kJawWormMoveThrash;

// Telegraphed intent for MonsterState.intent (uint8_t) -- generated alongside
// the monster table. The game's AbstractMonster.Intent enum is player-facing
// telegraphing, not mechanics; NONE is the value-init default.
using MonsterIntent = sts::registry::MonsterIntent;

// Jaw Worm A20 HP range -- the a7 column of registry/monsters.yaml
// (JawWorm.java:81-82 setHp(42,46) for ascension >= 7), resolved at the
// skeleton's fixed ascension. Exposed as named constants because tests assert
// the rolled HP lands in this range; move-effect amounts have no such
// re-export -- jaw_worm_take_turn reads them straight from the table.
inline constexpr int kJawWormHpMin =
    sts::registry::kJawWorm.hp_min(kSkeletonAscension);
inline constexpr int kJawWormHpMax =
    sts::registry::kJawWorm.hp_max(kSkeletonAscension);

// Construct the Jaw Worm in monster slot `monster_index`: set id, roll HP from
// monster_hp_rng (one inclusive random(42,46) draw -> hp == max_hp), zero
// block/flags/powers/history, and perform decision #1 (the forced-first-move
// Chomp), consuming one ai_rng.random(99) draw whose value is ignored.
void jaw_worm_init(CombatState& state, uint8_t monster_index) noexcept;

// One monster turn (design doc §5.2 step 5; JawWorm.takeTurn). Signature is
// MonsterTurnFn-compatible so it is passed directly as pump()'s take_turn hook.
// Executes the currently-decided move (enqueues its effect program from the
// generated table, see SCOPE) then rolls the next move via the getMove decision
// tree on ai_rng, updating move_history and intent.
void jaw_worm_take_turn(CombatState& state, uint8_t monster_index) noexcept;

// --- The hardMode variant (S2.26; the Act-3 "Jaw Worm Horde") ----------------
//
// PURELY ADDITIVE, AND DELIBERATELY SO. Everything above is skeleton content
// whose Stage-A fixtures pin the ordinary Exordium Jaw Worm byte for byte, so
// the variant is expressed as NEW exported functions plus a pad0 latch that
// jaw_worm_init already writes as 0 -- not one line of jaw_worm_init /
// jaw_worm_take_turn / the getMove tree changes.
//
// WHAT THE BOOLEAN IS. JawWorm has two constructors (JawWorm.java:71-110): the
// 2-arg one delegates with `hard = false`, and the 3-arg one has exactly one
// caller in the game -- MonsterHelper's "Jaw Worm Horde", which builds three
// worms with `true` (MonsterHelper.java:549-550). It changes EXACTLY TWO things
// and neither is a stat:
//
//   (a) `firstMove = false` (:77-79). The ordinary worm's getMove short-circuits
//       its first decision to a forced CHOMP (:150-154); a hard worm does not,
//       so its OPENING telegraph is decided by the full num tree against an
//       EMPTY move history. Crucially the DRAW COUNT is unchanged: every arm's
//       history predicate is false on an empty history, so no tiebreak
//       randomBoolean is reached and the opening still costs exactly one
//       random(99). What changes is the outcome distribution -- 25% Chomp / 30%
//       Thrash / 45% Bellow instead of 100% Chomp.
//
//   (b) usePreBattleAction (:112-118) stops being empty: ApplyPowerAction
//       Strength(bellowStr) THEN GainBlockAction(bellowBlock), in that
//       addToBottom order. Those are the SAME two numbers in the SAME order as
//       the BELLOW move's registry program, so the pre-battle body queues that
//       program rather than restating 5 and 9 in code.
//
// EVERY setHp RANGE AND EVERY TIER COLUMN SITS OUTSIDE THE hardMode GUARD
// (:81-104), which is what lets the id-1 registry row serve both variants
// unchanged. That is the answer to the deferred ledger row.
//
// WHY NOT A SECOND MonsterId: `monster_from_game_id` is a by-game-id lookup and
// two rows sharing "JawWorm" would make it ambiguous. WHY NOT A per-emit variant
// column on the encounter composition: that is the general answer, and it would
// touch ResolvedGroup, both spawn signatures and MonsterInitFn for one caller.
// The chosen shape is the LAGAVULIN PRECEDENT -- a bespoke second init selected
// by a named branch at the combat-start site (src/engine/run_advance.cpp) --
// which costs no schema change and no registry vocabulary. Lagavulin's branch
// keys off an event-variant enum and this one keys off the ENCOUNTER KEY, which
// is the one difference worth flagging: if a second behavioural monster-ctor
// boolean ever lands, the rule of two fires and the per-emit column becomes
// correct. Checked, and it will not from the Act-2/3 roster:
// `Cultist(x, y, boolean talk)` (Cultist.java:57-75), which the Awakened One
// group passes `false`, is PURELY PRESENTATION (a dialog latch).

// hardMode's marker, in the monster-type-scoped scratch byte. The ORDINARY init
// writes pad0 = 0, so an Exordium worm reads this clear and every hard-mode
// branch is dead for it -- which is exactly what keeps the Stage-A fixtures
// byte-identical.
inline constexpr uint8_t kJawWormPadHardMode = 0x01u;

// `new JawWorm(x, y, true)` + init(). Identical to jaw_worm_init except that it
// sets the hardMode latch and does NOT force the opening Chomp: the same single
// ai_rng.random(99) is drawn, and this time its value is READ.
void jaw_worm_init_hard(CombatState& state, uint8_t monster_index) noexcept;

// JawWorm.usePreBattleAction (JawWorm.java:112-118). A NO-OP unless the record
// carries kJawWormPadHardMode -- which IS the `if (this.hardMode)` guard, and is
// why registering this for MonsterId::JAW_WORM is safe for the ordinary worm: a
// registered no-op queues nothing and draws nothing, exactly as the nullptr it
// replaces did.
void jaw_worm_use_pre_battle_action(CombatState& state,
                                    uint8_t monster_index) noexcept;

}  // namespace sts::engine
