#pragma once

// Per-MonsterId init/turn dispatch + multi-monster spawn. Generalizes the
// skeleton's hard-wired single-Jaw-Worm seam: combat_begin now spawns an
// arbitrary monster GROUP, and the pump's step-5 monster-turn hook dispatches to
// each acting monster's own turn function by its id (the "Stage B generalizes the
// MonsterTurnFn per the monster registry" note in advance.hpp/action_queue.hpp).
//
// Each monster module registers its init/turn in the switches below. Every
// MonsterId in the registry now has a module, so those two switches are
// exhaustive and carry no `default:` -- adding an enumerator makes -Wswitch
// name them. Their trailing returns (nullptr / default_monster_turn) exist only
// for an id that matches no case at all, which a valid monster record cannot
// produce; they are not a "not implemented yet" path.

#include <cstdint>
#include <span>

#include "sts/engine/action_queue.hpp"  // MonsterTurnFn, default_monster_turn
#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"  // MonsterDef (shared effect-queue helper)

namespace sts::engine {

// Telegraphed intent, generated alongside the monster table (mirrors the alias in
// monster_jaw_worm.hpp; identical type-alias redeclaration is well-formed).
using MonsterIntent = sts::registry::MonsterIntent;

// --- Move-history helpers (AbstractMonster.java:431-491) --------------------
// Shared by the native monster modules. move_history[0] == most recent (Java
// moveHistory.last()); the 0 empty-slot sentinel means `== move` (move != 0)
// already encodes the "history non-empty" checks.

// setMove: record the decided intent and push the move onto the 3-slot ring at
// DECISION time (AbstractMonster.setMove appends to moveHistory).
inline void set_monster_move(MonsterState& m, uint8_t move,
                             MonsterIntent intent) noexcept {
    m.intent = static_cast<uint8_t>(intent);
    m.move_history[2] = m.move_history[1];
    m.move_history[1] = m.move_history[0];
    m.move_history[0] = move;
}

// lastMove(byte): the most-recent decided move == move (:469-474).
inline bool last_move_is(const MonsterState& m, uint8_t move) noexcept {
    return m.move_history[0] == move;
}

// lastMoveBefore(byte): the move decided one BEFORE the most recent one
// (:437-444). move_history[1] is that slot, and the ring's 0 empty-slot sentinel
// already encodes the "history shorter than 2" false answer (a real move id is
// never 0).
//
// PROMOTED from a file-local helper in monster_gremlin_nob.cpp (rule of two,
// conventions §7): the Nob's A18 branch was the only reader in the Act-1 roster,
// and the note there said so; the Snake Plant's A17 arm (SnakePlant.java:132) is
// the second, so the copy is gone and both call this.
inline bool last_move_before_is(const MonsterState& m, uint8_t move) noexcept {
    return m.move_history[1] == move;
}

// lastTwoMoves(byte): the two most-recent decided moves both == move (:486-491).
inline bool last_two_moves_are(const MonsterState& m, uint8_t move) noexcept {
    return m.move_history[0] == move && m.move_history[1] == move;
}

// The skeleton plays S1's fixed difficulty (Ironclad / Act 1 / A20). Shared by
// every native monster module so their tier columns resolve at one ascension
// until the run layer threads a real ascension through combat_begin. (Mirrors
// monster_jaw_worm.hpp's kSkeletonAscension, kept there for the fixture pin.)
inline constexpr int32_t kMonsterAscension = 20;

// Enqueue a decided move's effect program from the generated MonsterDef, in
// takeTurn addToBottom order, resolving each tier amount at kMonsterAscension --
// the jaw_worm queue_move_effects generalized to any monster/move (design §4.2:
// move effects are data; selection is native). SELF-targeted steps hit `mi`,
// PLAYER steps hit the player; APPLY_POWER's PowerId rides in `flags` (extra).
// (Per-instance amounts -- e.g. the Louse's rolled bite damage -- are NOT in the
// table; the monster module queues those itself, see monster_louse.cpp.)
void queue_monster_move_effects(CombatState& state, uint8_t mi,
                                const sts::registry::MonsterDef& def,
                                uint8_t move) noexcept;

// `target_override` sentinel for queue_monster_move_effect below: resolve the
// target from the STEP (SELF -> mi, PLAYER -> the player), as the whole-program
// helper above always does. kActorPlayer is a legal override value, so the
// sentinel cannot be a real actor index -- 0xFF is neither a monster slot nor the
// player.
inline constexpr uint8_t kMoveTargetFromStep = 0xFFu;

// Queue ONE step of a decided move's program -- the per-step body
// queue_monster_move_effects loops over, exposed because two S2.22 monsters need
// it and neither can use the whole-program helper:
//
//   * the Snecko's TAIL SKIPS a step below A17 (the Weak between the damage and
//     the Vulnerable is inside an ascension branch, Snecko.java:112-114), and an
//     effect list expresses per-tier amounts but not per-tier PRESENCE;
//   * the Healer's HEAL and BUFF FAN ONE step out over every live group member
//     (Healer.java:104-107,114-117), a count no effect list can carry, so the row
//     authors one SELF-targeted template and the module retargets it per member.
//
// `target_override` is kMoveTargetFromStep to use the step's own target, or an
// actor index (a monster slot, or kActorPlayer) to force one. Everything else --
// the tier resolution at kMonsterAscension, the `extra` packing, MAKE_CARD's
// CardPile split into `src` -- is identical to the whole-program helper, because
// this IS its body.
void queue_monster_move_effect(CombatState& state, uint8_t mi,
                               const sts::registry::MonsterDef& def,
                               uint8_t move, uint8_t effect_index,
                               uint8_t target_override) noexcept;

// A monster's spawn-time init: set id, roll HP (monster_hp_rng), do the first
// rollMove (ai_rng) -- the jaw_worm_init shape, generalized. Every registry
// monster has one; nullptr comes back only for MonsterId::NONE or an id outside
// the enum.
using MonsterInitFn = void (*)(CombatState& state, uint8_t monster_index);

// A monster's usePreBattleAction (design §5.2; AbstractMonster.usePreBattleAction),
// run AFTER all ctors+init(), in spawn order (the player's preBattlePrep phase). It
// may draw floor-scoped RNG (e.g. the Louse's curl-up monster_hp_rng roll) and set
// up start-of-combat powers. nullptr == the monster has no pre-battle action.
using MonsterPreBattleFn = void (*)(CombatState& state, uint8_t monster_index);

// The pre-battle function for a monster id, or nullptr if it has none.
[[nodiscard]] MonsterPreBattleFn monster_pre_battle_fn(MonsterId id) noexcept;

// --- Monster split framework seams -------------------------------------------

// A monster's queued-roll body (RollMoveAction.update -> rollMove; no liveness
// check, RollMoveAction.java:17-21). Only monsters whose turns QUEUE ROLL_MOVE
// items (the large slimes: their rolls must resolve after mid-turn interrupt
// SetMoveActions and even posthumously after a split) register one; monsters
// that roll inline in their MonsterTurnFn return nullptr and a ROLL_MOVE item
// targeting them stays a safe no-op.
using MonsterRollMoveFn = void (*)(CombatState& state, uint8_t monster_index);
[[nodiscard]] MonsterRollMoveFn monster_roll_move_fn(MonsterId id) noexcept;

// The ROLL_MOVE opcode body: dispatch to the target's queued-roll function if
// it has one (called by execute_opcode; safe no-op otherwise).
void roll_monster_move(CombatState& state, uint8_t monster_index) noexcept;

// A monster's spawn-at-fixed-HP init: the game's "construct with newHealth,
// then init()" pair (NO monster_hp_rng draw -- the 4-arg slime ctors pass
// newHealth straight through, AbstractMonster.java:139,150 -- then exactly one
// aiRng rollMove). nullptr == the monster cannot be mid-combat spawned yet.
using MonsterSpawnAtHpFn = void (*)(CombatState& state, uint8_t monster_index,
                                    int16_t hp);
[[nodiscard]] MonsterSpawnAtHpFn monster_spawn_at_hp_fn(MonsterId id) noexcept;

// SpawnMonsterAction.update (SpawnMonsterAction.java:42-73) minus presentation:
// insert a fresh record at `slot` (records at >= slot shift up one -- the game
// list-inserts and NEVER removes dead records, MonsterGroup.java:35-40), remap
// monster_queue indices >= slot, ++monster_count (hard assert at kMonsterCap),
// then run the id's spawn-at-hp init (the child's aiRng roll happens HERE, at
// resolve time). Pending action_queue items are NOT remapped -- the Java holds
// object references, so any action queued to resolve after a spawn must
// pre-compute its post-insertion slot (see monster_slime_large.cpp's trailing
// ROLL_MOVE). The insertion slot itself is pre-computed at QUEUE time from the
// smart-positioning drawX rule (monster_slime_large.hpp).
//
// WHO SETS `draw_x`: the spawned monster's OWN spawn-at-hp init fn, not this
// function. The position key is a per-type, per-slot constant out of the Java
// class's POSX table (Reptomancer's {210,-220,180,-250}, the Gremlin Leader's
// {-366,-170,-532}, and so on), so the module that owns the class owns the
// table. This function deliberately takes no draw_x argument: the SPAWN_MONSTER
// item has no field wide enough for a signed position, and threading one would
// duplicate a constant the module already has.
//
// Until a batch populates it, `draw_x` is uniformly 0 across every landed
// monster, so smart_position_for is unused by landed content and the slime split
// keeps its hand-derived slots -- no existing spawn changes.
//
// `run_pre_battle` runs the spawned monster's usePreBattleAction after its init,
// which is SummonGremlinAction's behaviour and NOT SpawnMonsterAction's -- the
// two Java actions differ here, so it is a parameter rather than a policy. It is
// what gives a summoned Gremlin Warrior its Angry power. The Java runs it at
// `isDone`, i.e. after the MinionPower the spawn itself queues, so a caller that
// needs both orders them that way.
void spawn_monster_at_slot(CombatState& state, uint8_t slot, MonsterId id,
                           int16_t hp, bool run_pre_battle = false) noexcept;

// SpawnMonsterAction's SMART POSITIONING (SpawnMonsterAction.java:50-56), and
// GremlinLeader's identical getSmartPosition (SummonGremlinAction.java):
//
//     int position = 0;
//     for (AbstractMonster mo : getCurrRoom().monsters.monsters) {
//         if (!(this.m.drawX > mo.drawX)) break;
//         ++position;
//     }
//
// Note it BREAKS at the first record that fails the test -- it does not count
// matches across the whole list -- so this reproduces the break, not a count.
// The two differ whenever the list is not sorted by drawX, which initial groups
// need not be (MonsterHelper constructs in its own order).
//
// The comparison is `>` and therefore STRICT: a newcomer with the SAME x as an
// existing record stops there and is inserted BEFORE it. That happens whenever a
// position is recycled, which every one of the Act-2/3 spawners does.
//
// `draw_x` is a monotone stand-in for the Java's float drawX (see
// MonsterState::draw_x): drawX = WIDTH*0.75 + offsetX*xScale with xScale > 0, so
// comparing the stored offsetX gives the identical order. DEAD RECORDS COUNT --
// the Java walks the whole group list, and this engine retains dead records in
// place precisely so that stays true.
[[nodiscard]] uint8_t smart_position_for(const CombatState& state,
                                         int16_t draw_x) noexcept;

// Post-damage monster hook -- the AbstractMonster.damage() override seam, run
// AFTER a hit fully lands (op_damage / op_lose_hp, ANY damage type: the Java
// override wraps super.damage() unconditionally, and LoseHPAction also routes
// through damage(), LoseHPAction.java:41). Dispatches by monster_id; the large
// slimes' split interrupt is the first consumer. No-op for everyone else.
//
// `hp_lost` is the HP the hit actually removed (0 when block ate all of it) --
// the `currentHealth != previousHealth` an override can test. The slimes' split
// interrupts read only resulting state and ignore it; Lagavulin's wake needs it,
// because a hit fully absorbed by its sleeping armour must NOT wake it.
void on_monster_damaged(CombatState& state, uint8_t monster_index,
                        int32_t hp_lost) noexcept;

// --- The death edge (AbstractMonster.die overrides) --------------------------

// A monster's own die() body: the part a subclass runs BEFORE `super.die()`.
//
// WHY THIS EXISTS AS A DISPATCH SLOT rather than a special case somewhere. Ten
// Act-1 monsters override die() and every one of them is presentation (a sound,
// a shake, a time-scale) -- so until now the death edge needed no monster-side
// seam at all, and the two things the BASE die() does (the dying monster's own
// powers' onDeath, then the player's relics' onMonsterDeath) were dispatched
// directly. The Mugger is the first override with COMBAT-VISIBLE content, and it
// has two pieces:
//
//     public void die() {
//         this.playDeathSfx();                          // ONE SEEDED aiRng draw
//         ... animation ...
//         if (this.stolenGold > 0)
//             AbstractDungeon.getCurrRoom().addStolenGoldToRewards(stolenGold);
//         super.die();                                  // powers, then relics
//     }
//                                          (Mugger.java:156-165, :147-154)
//
// The aiRng.random(2) in playDeathSfx is the point: a Mugger's death MOVES THE
// SHARED AI STREAM, so every later monster decision in that combat shifts. The
// Looter's identical-looking playDeathSfx rolls UNSEEDED MathUtils and costs
// nothing (Looter.java:151-157) -- the two thieves differ here, which is exactly
// why this cannot be a blanket "thieves draw on death" rule.
//
// ORDERING IS PART OF THE CONTRACT: this fires at the SAME edge as
// dispatch_on_death and strictly BEFORE it, because the subclass body runs
// before `super.die()` -- and super.die() is what the power/relic fan-outs model.
// Acts 2-4 have more overrides with real content, which is why this is a general
// slot and not a Mugger branch.
//
// THE RETURN VALUE IS A VETO: true == "SUPPRESS super.die()". Two Act-2/3
// monsters override die() to call `super.die()` CONDITIONALLY and skip it
// entirely the rest of the time:
//
//     public void die() { if (!getCurrRoom().cannotLose) super.die(); }
//                                              (Darkling.java:239-243)
//     // AwakenedOne.die():356-375 has the same !cannotLose guard
//
// When the guard suppresses `super.die()`, NONE of what super.die() does may
// run: not the dying monster's powers' onDeath, not the player's relics'
// onMonsterDeath, not isDying. The Darkling's damage() override then re-fires
// those two fan-outs BY HAND exactly once, which is the whole reason the veto
// exists -- without it they would fire twice per half-death.
//
// A body that returns false leaves the edge exactly as it was, so the Mugger
// (the only pre-existing entry) is unchanged.
//
// nullptr == this monster's die() is presentation only (or absent), which is
// equivalent to a body returning false. Spell an explicit nullptr case rather
// than leaning on the `default:` when the class DOES declare die(), so the
// reading is checkable.
using MonsterDieFn = bool (*)(CombatState& state, uint8_t monster_index);
[[nodiscard]] MonsterDieFn monster_die_fn(MonsterId id) noexcept;

// The POST-`super.die()` half of a die() override.
//
// MonsterDieFn above models the part a subclass runs BEFORE `super.die()`. That
// is the Mugger's shape, and it is the WRONG side of the line for most of the
// Act-2/3 overrides, which call `super.die()` FIRST and then do their work:
//
//   * Reptomancer (:157-165)  super.die(), then SuicideAction every surviving
//                             monster. The ordering is what makes
//                             `!m.isDead && !m.isDying` skip the Reptomancer
//                             ITSELF -- super.die() set isDying first. Run this
//                             body on the pre-super side and the Reptomancer
//                             suicides itself in an infinite regress.
//   * BronzeAutomaton (:176-190) / TheCollector (:227-242)
//                             super.die(), onBossVictoryLogic, then the same
//                             suicide sweep.
//   * AwakenedOne (:356-375)  super.die(), then EscapeAction every surviving
//                             Cultist.
//
// So this is a SECOND slot rather than a phase argument: each slot's ordering
// claim stays literally true, and a body lands on exactly one side.
//
// NOT RUN WHEN THE VETO FIRED. If MonsterDieFn returned true then `super.die()`
// did not happen, and a post-super body is by definition part of what did not
// happen. (Neither of today's veto users has a post-super body, so this costs
// nothing today; it is written down because the combination is what a future
// reader will get wrong.)
//
// nullptr == no post-super content.
using MonsterDieAfterFn = void (*)(CombatState& state, uint8_t monster_index);
[[nodiscard]] MonsterDieAfterFn monster_die_after_fn(MonsterId id) noexcept;

// The death-edge hook: run the dying monster's own die() body. Called from the
// monster-death edge in interp/interp_damage.cpp (both op_damage's and
// op_lose_hp's), immediately before dispatch_on_death.
//
// Returns the VETO: true == the caller must SKIP dispatch_on_death and the
// relic onMonsterDeath fan-out, because `super.die()` was suppressed. Safe
// no-op returning false for a monster with no die() body of its own.
[[nodiscard]] bool dispatch_monster_die(CombatState& state,
                                        uint8_t monster_index) noexcept;

// Run the dying monster's POST-`super.die()` body, if it has one. The caller
// invokes this only when the veto did NOT fire, immediately after the power and
// relic death fan-outs. Safe no-op otherwise.
void dispatch_monster_die_after(CombatState& state,
                                uint8_t monster_index) noexcept;

// The init function for a monster id; nullptr only for NONE / an id outside the
// enum (see the exhaustiveness note at the top of this file).
[[nodiscard]] MonsterInitFn monster_init_fn(MonsterId id) noexcept;

// The turn function for a monster id; default_monster_turn (a live no-op) only
// for NONE / an id outside the enum. Never nullptr -- dispatch_monster_turn
// calls the result unconditionally.
[[nodiscard]] MonsterTurnFn monster_turn_fn(MonsterId id) noexcept;

// MonsterTurnFn-compatible step-5 hook: dispatch to the acting monster's own turn
// function by monsters[monster_index].monster_id. Pass this to pump() in place of
// the old hard-wired jaw_worm_take_turn.
void dispatch_monster_turn(CombatState& state, uint8_t monster_index) noexcept;

// Spawn `group` into monster slots [0, group.size()) in spawn order: set
// monster_count, then call each monster's init fn in order. The five RNG streams
// are independent, so per-monster init (ctor HP roll + rollMove folded) yields the
// same PER-STREAM sequences as the game's phased "all ctors (monster_hp_rng) then
// all init() (ai_rng)" ordering: monster_hp_rng sees the HP rolls in spawn order,
// ai_rng sees the rollMoves in spawn order. (usePreBattleAction -- a later
// monster_hp_rng phase, e.g. Louse curl-up -- is the separate
// use_pre_battle_actions seam below.) An id with no init fn -- NONE, or a corrupt
// record -- hard-asserts rather than spawning a blank.
void spawn_group(CombatState& state, std::span<const MonsterId> group) noexcept;

// Spawn a resolved group's CONSTRUCTION trace (ResolvedGroup, encounters.hpp):
// walk `constructed` in order; a kept entry (bit i of `kept_mask`) is spawned
// into the next monster slot exactly as spawn_group does, and a DISCARDED PICK
// candidate burns its constructor's monster_hp_rng draws without spawning --
// the game constructed it (setHp; a Louse's biteDamage too) and then dropped
// it on the ArrayList pick (MonsterHelper.java:799-822), so the draws are
// consumed either way and every kept monster's HP roll must sit at its
// construction-order position (the STS01789 class). A discarded candidate is
// never init()ed, so it draws NO ai_rng. Equivalent to spawn_group when the
// mask keeps everything.
void spawn_group_trace(CombatState& state,
                       std::span<const MonsterId> constructed,
                       uint16_t kept_mask) noexcept;

// The ctor-only monster_hp_rng burn for one discarded candidate: the setHp
// draw over the id's A7-column HP range, plus every registry roll with timing
// CONSTRUCTOR_AFTER_HP on the MONSTER_HP stream (today: the Louse biteDamage).
// PRE_BATTLE rolls (Curl Up) do NOT burn -- usePreBattleAction runs only on
// spawned monsters.
void burn_unspawned_ctor_rolls(CombatState& state, MonsterId id) noexcept;

// Run every live monster's usePreBattleAction in spawn order (design §5.2: the
// player's preBattlePrep phase, AFTER spawn_group's ctors+init). This is the
// second monster_hp_rng phase (Louse curl-up rolls) -- separate from the ctor HP
// rolls, but on the same stream, so it extends monster_hp_rng's sequence in spawn
// order. A monster with no pre-battle fn is skipped (no draw). combat_begin calls
// this right after spawn_group.
void use_pre_battle_actions(CombatState& state) noexcept;

}  // namespace sts::engine
