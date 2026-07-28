#pragma once

// Action-queue mechanics + the getNextAction pump (design doc §5.1-§5.2).
// Provenance: com.megacrit.cardcrawl.actions.GameActionManager
// (GameActionManager.java, esp. getNextAction lines 185-367). This layer is
// QUEUE ORDERING/MECHANICS only -- it decides *which* item resolves next and
// maintains the four queues (design doc §5.1). It does NOT interpret opcodes or
// resolve card plays: opcode execution lives in the effect interpreter
// (interp.hpp) and card-play resolution in card_play.hpp, both invoked by the
// pump as it pops each item.
//
// -------------------------------------------------------------------------
// monster_attacks_queued reset placement (non-obvious, Java-faithful). The
// decompiled GameActionManager.java sets `monsterAttacksQueued` to `true`
// (declaration + line 304) but NEVER sets it back to false anywhere in the
// whole tree (grep-confirmed: 3 occurrences, one file, none `= false`). Taken
// literally that flag can never re-open step 4, so monsters would be queued at
// most once and never take a second turn -- not the game's observable behavior,
// so this decompiled copy is missing the reset. Clearing it in the start-of-turn
// sequence (step 6) would leave the flag false *during the player's turn*, so
// the first mid-turn pump (a card play) would wrongly fire step 4 and run a
// spurious monster turn. The correct placement -- and what design doc §5.2
// step 6's generic "reset per-turn counters" actually enumerates
// (cardsPlayedThisTurn, turnHasEnded, ..., NOT monsterAttacksQueued;
// GameActionManager.java:333,349-351) -- is: clear the flag when the turn *ends*
// (the end-turn sentinel, step 3), priming step 4 to queue monsters exactly
// once, and leave it set through the whole next player turn. Start-of-turn
// (step 6) does NOT touch it, matching the Java. This keeps the invariant
// "monster_attacks_queued is true throughout the player's turn" so card-play
// pumps never trip step 4.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// --- Reserved sentinels / placeholders --------------------------------------

// End-turn sentinel: a "null-card" card_queue item (design doc §5.1: "a
// null-card item is the end-turn sentinel"; GameActionManager.java:198-199 keys
// off `cardQueue.get(0).card == null`). CardQueueItem.card_index is a
// CardPoolIndex (uint8_t) into the 160-row pool (indices 0..159), so 255 is a
// permanently-out-of-range marker that can never collide with a real pool row.
// It is deliberately distinct from CardId::NONE (== 0): index 0 is a *valid*
// pool slot (that merely happens to hold an empty card), whereas 255 is "not a
// card reference at all". The sentinel is constructed via make_end_turn_sentinel().
inline constexpr CardPoolIndex kEndTurnSentinel = 255;

// DRAW opcode value for the start-of-turn DrawCardAction the pump queues
// (design doc §5.2 step 6). Mirror of interp.hpp's Opcode::DRAW; interp.hpp's
// value is authoritative and action_queue.cpp static_asserts the two agree
// (kOpcodeDrawCard == (uint16_t)Opcode::DRAW).
inline constexpr uint16_t kOpcodeDrawCard = 4;

// Reserved actor index meaning "the player" in an ActionQueueItem's src/tgt
// (combat_state.hpp: "player is a reserved sentinel, monsters are monster-array
// indices"). 0xFF cannot alias a monster slot (0..4).
inline constexpr uint8_t kActorPlayer = 0xFF;

// Dynamic-target sentinels for an ActionQueueItem's `tgt`. They
// cannot alias a monster slot (0..4) or the player (0xFF). execute_opcode
// resolves them at EXECUTE time (not enqueue), matching the game's per-action
// resolution of AoE / random targets:
//   * kActorAllEnemies -- the effect fans out over every LIVE monster, computing
//     a SEPARATE DamageInfo per target (DamageAllEnemiesAction.update snapshots
//     the monster list and skips isDeadOrEscaped; DamageAllEnemiesAction.java:
//     56-83). Whirlwind queues one such item per energy point (WhirlwindAction).
//   * kActorRandomEnemy -- the effect hits ONE uniformly-random live monster,
//     rolled with a single card_random_rng draw AT EXECUTE TIME
//     (AttackDamageRandomEnemyAction.update: getRandomMonster(cardRandomRng) is
//     re-rolled per hit). Multi-hit-random cards queue N such items so each hit
//     rolls its own target against the then-live monsters.
inline constexpr uint8_t kActorAllEnemies = 0xFD;
inline constexpr uint8_t kActorRandomEnemy = 0xFE;

// Ironclad's BASE hand size -- CharacterStrings/AbstractPlayer.masterHandSize
// seeded from info.cardDraw (AbstractPlayer.java:133-134, :346). The game draws
// `gameHandSize`, which preBattlePrep snapshots from masterHandSize
// (AbstractPlayer.java:1579) and turn-N reads at GameActionManager.java:361.
// This constant is the base only: game_hand_size() below is what the draw line
// actually queues, because Snecko Eye's onEquip adds 2 to masterHandSize
// (SneckoEye.java:29-32).
inline constexpr int32_t kStartOfTurnDrawCount = 5;

// Ironclad's BASE energy per turn -- the value EnergyManager's ctor is
// constructed with (EnergyManager.java:16-18). It is NOT the whole per-turn
// number: ten BOSS relics do `++AbstractDungeon.player.energy.energyMaster` in
// onEquip, so energy_master() below is what the recharge line reads. Energy is
// SET to that value every turn via EnergyManager.recharge() -- not additive,
// except under Ice Cream. The game wires recharge() through a presentation-layer
// PlayerTurnEffect queued alongside the start-of-turn DrawCardAction; it is
// presentation-adjacent but outcome-affecting, so it stays in scope.
inline constexpr int16_t kIroncladBaseEnergy = 3;

// --- The two derived per-combat player numbers ------------------------------
//
// `EnergyManager.energyMaster` (EnergyManager.java:14) and
// `AbstractPlayer.masterHandSize` (AbstractPlayer.java:133) are STORED fields in
// the game and DERIVED here, from the CombatState relic mirror. The derivation
// is faithful rather than merely convenient, and the reason is a timing one
// worth stating once:
//
//   * The game reads each field exactly ONCE per combat. `prep()` copies
//     energyMaster into `EnergyManager.energy` (EnergyManager.java:20-23) and
//     preBattlePrep copies masterHandSize into gameHandSize
//     (AbstractPlayer.java:1579); every later read that turn -- recharge()'s
//     `this.energy` (:25-41), the turn-N `DrawCardAction(gameHandSize)`
//     (GameActionManager.java:361) -- reads the SNAPSHOT, not the master.
//   * The relic mirror `s.relics` is that snapshot. It is written once, at
//     combat construction (advance.cpp combat_begin / run_advance.cpp
//     enter_combat), and no S1 path adds or removes a relic mid-combat -- relic
//     acquisition is a run-layer event between combats. So a per-turn scan of
//     the mirror returns the same number every turn of one combat, which is
//     exactly what a `prep()` snapshot returns.
//
// SLAVER'S COLLAR, the eleventh writer, is DERIVED here too -- and the
// derivation is EXACT, which took reading the escape path to establish.
// SlaversCollar.beforeEnergyPrep (SlaversCollar.java:46-57) is a CONDITIONAL,
// per-combat `++energyMaster` keyed on `eliteTrigger || any monster is
// EnemyType.BOSS`, paired with an `onVictory` `--` guarded by the relic's
// `pulse` (:59-65). `beginLongPulse` sets `pulse = true`
// (AbstractRelic.java:849-852), so the pair is balanced on every combat whose
// end runs AbstractRoom.endBattle -- and endBattle calls `player.onVictory()`
// UNCONDITIONALLY (AbstractRoom.java:413-421), including on a Smoke Bomb
// escape, whose only route out is updateEscapeAnimation ->
// getCurrRoom().endBattle() (AbstractPlayer.java:2281-2292). The single path
// that skips the `--` is AbstractPlayer.onVictory's `if (!this.isDying)` guard
// (AbstractPlayer.java:1951-1964), i.e. player DEATH -- which ends the run, so
// no later combat can observe the leak.
//
// The Deferred-obligations row (and the stage-2 seam note it carries) claimed
// the increment leaks across a fled combat and that a derivation therefore
// could not be faithful. That reading is WRONG at the Java, and correcting it
// is what makes this row cost zero storage: no CombatState field, no
// RunState/RunController carry, no SCHEMA_VERSION question. The delta between
// combats is provably 0.

// The player's energyMaster for this combat: kIroncladBaseEnergy, plus one per
// owned copy of each of the ten BOSS relics whose onEquip increments it, plus
// one for a Slaver's Collar in an elite-or-boss encounter.
[[nodiscard]] int16_t energy_master(const CombatState& state) noexcept;

// SlaversCollar.beforeEnergyPrep's own condition, without the relic test:
// `getCurrRoom().eliteTrigger` OR any monster with `type == EnemyType.BOSS`
// (SlaversCollar.java:47-51). The elite half is kCombatFlagEliteRoom
// (combat_state.hpp); the boss half reads the live `enemy_type` registry column
// through MonsterDef::is_boss(), so no monster id is hard-coded. Exposed
// because it is a two-source predicate worth testing directly.
[[nodiscard]] bool combat_is_elite_or_boss(const CombatState& state) noexcept;

// The player's gameHandSize for this combat: kStartOfTurnDrawCount plus 2 per
// owned Snecko Eye (SneckoEye.java:29-32 writes masterHandSize, NOT
// energyMaster -- two different fields, two different relics' worth of effect).
[[nodiscard]] int32_t game_hand_size(const CombatState& state) noexcept;

// --- Monster-turn extension point -------------------------------------------

// Step 5 of the pump (design doc §5.2) runs one monster's turn. The queue
// mechanics here know no monster-specific behavior; the monster module supplies
// the real move-selection/turn logic. The seam is a plain function pointer (this
// engine is data-oriented: no virtual dispatch, no heap) invoked as
// `take_turn(state, monster_index)` for each live monster popped from the
// monster queue. Callers pass a real AI function (jaw_worm_take_turn); tests can
// pass default_monster_turn (a no-op) or a lightweight probe.
using MonsterTurnFn = void (*)(CombatState& state, uint8_t monster_index);

// The default step-5 hook: the monster does nothing. It exists so pump()'s
// signature has a valid default and the queue mechanics are testable with zero
// monster-AI knowledge.
void default_monster_turn(CombatState& state, uint8_t monster_index) noexcept;

// --- Queue insertion primitives (design doc §5.1) ---------------------------

// Main action ring (`actions`). addToBottom appends
// (GameActionManager.java:96-100); addToTop prepends (lines 139-143). The game
// makes these no-ops outside the COMBAT room phase; the sim has no non-combat
// phase inside CombatState, so they are unconditional here. Overflow is a hard
// assert (design doc §4.1), never a reallocation.
void add_to_bottom(CombatState& state, ActionQueueItem item) noexcept;
void add_to_top(CombatState& state, ActionQueueItem item) noexcept;

// preTurnActions ring. addToTurnStart prepends (GameActionManager.java:145).
void add_to_turn_start(CombatState& state, ActionQueueItem item) noexcept;

// Card queue (`cardQueue`). Normal enqueue appends to the bottom
// (addCardQueueItem(c) -> addCardQueueItem(c, false); GameActionManager.java:
// 110,114-116). Front-of-queue insertion is TRAP 9 (design doc §10 item 9,
// §5.1; GameActionManager.java:102-108): when the queue is non-empty the item
// goes to index **1**, not 0 -- the currently-resolving head card stays at
// index 0 -- and only lands at index 0 when the queue was empty. Overflow is a
// hard assert.
void add_card_to_queue_bottom(CombatState& state, CardQueueItem item) noexcept;
void add_card_to_queue_top(CombatState& state, CardQueueItem item) noexcept;

// End-turn sentinel constructors/predicate (see kEndTurnSentinel).
[[nodiscard]] CardQueueItem make_end_turn_sentinel() noexcept;
[[nodiscard]] bool is_end_turn_sentinel(CardQueueItem item) noexcept;

// --- Low-level ring pops (front) --------------------------------------------
// Exposed for unit tests (drain-order verification) and reused internally by
// the pump. Return false and leave *out untouched when the ring is empty.
bool pop_action_front(CombatState& state, ActionQueueItem& out) noexcept;
bool pop_pre_turn_front(CombatState& state, ActionQueueItem& out) noexcept;

// --- Pump (design doc §5.2 getNextAction priority order) --------------------

// Classification of a single pump step -- what the priority check did this
// iteration. Lets tests assert resolution *ordering* (e.g. every RAN_ACTION
// precedes every RAN_PRE_TURN) without an effect interpreter to observe.
enum class PumpOutcome : uint8_t {
    RAN_ACTION = 0,      // step 1: popped/executed an action_queue item
    RAN_PRE_TURN,        // step 2: popped/executed a pre_turn_actions item
    END_TURN_SENTINEL,   // step 3: head card_queue item was the end-turn sentinel
    RAN_CARD_QUEUE,      // step 3: head was a non-sentinel card (resolved via card_play.hpp)
    QUEUED_MONSTERS,     // step 4: populated monster_queue from live monsters
    RAN_MONSTER,         // step 5: ran one monster's turn via the extension point
    STARTED_TURN,        // step 6: ran the start-of-turn sequence (turn++)
    WAITING_ON_USER,     // step 7: control returns to the player (terminal)
    COMBAT_OVER,         // terminal: player dead, player escaped (Smoke Bomb's
                         // kCombatFlagPlayerEscaped), or every monster dead or
                         // escaped (monster_dead_or_escaped -- the
                         // areMonstersBasicallyDead complement)
};

struct PumpStepResult {
    PumpOutcome outcome;
    // Valid for RAN_ACTION / RAN_PRE_TURN: the item that was consumed.
    ActionQueueItem executed;
    // Valid for RAN_MONSTER: the monster slot whose turn ran.
    uint8_t monster_index;
};

// Run exactly one getNextAction priority step (design doc §5.2). Advances the
// queues by one and returns what happened. WAITING_ON_USER and COMBAT_OVER are
// terminal; every other outcome means "call again to keep resolving".
PumpStepResult pump_step(CombatState& state, MonsterTurnFn take_turn) noexcept;

// Drive the queues to quiescence: repeatedly pump_step until control returns to
// the player (phase WAITING_ON_USER) or combat ends (phase COMBAT_OVER). This
// is the "pump the loop after a player action" half of the engine step function
// (design doc §5.2). `take_turn` is the step-5 monster-turn seam (default:
// no-op); callers pass real Jaw Worm AI (jaw_worm_take_turn).
void pump(CombatState& state,
          MonsterTurnFn take_turn = default_monster_turn) noexcept;

// Combat start: run the game's turn-1 block and leave the combat in
// WAITING_ON_USER with turn == 1, the opening hand drawn and energy set. The one
// entry point every combat-construction path uses, so no caller hand-rolls it.
//
// It is NOT getNextAction's step 6. AbstractRoom.update (AbstractRoom.java:
// 236-243) sets turnHasEnded = true and immediately queues
// GainEnergyAndEnableControlsAction, whose update clears the flag again
// (GainEnergyAndEnableControlsAction.java:35) before the queue can ever drain
// down to the step-6 branch (GameActionManager.java:329). So the combat-start
// block runs the start-of-turn sequence WITHOUT the monsters'
// applyEndOfTurnPowers pass (GameActionManager.java:331) that step 6 opens with.
// A monster power that is present at combat start and binds an end-of-round hook
// -- a sleeping Lagavulin's Metallicize (Lagavulin.java:106-107;
// MetallicizePower.java:38-42) -- would otherwise trigger once before the player
// has taken a single turn.
//
// The post-draw POWER pass is omitted for the same reason: the combat-start block
// carries applyStartOfTurnPostDrawRelics (AbstractRoom.java:254) but no
// applyStartOfTurnPostDrawPowers line, and the game calls that only from step 6
// (GameActionManager.java:363). A PLAYER power binding atStartOfTurnPostDraw
// (Brutality, Demon Form) therefore does not fire on turn 1.
//
// The pre-battle actions queued by usePreBattleAction drain FIRST, because
// AbstractRoom.update only lets waitTimer reach 0 (and the turn-1 block run) once
// the action queue is empty (AbstractRoom.java:229-235).
void begin_first_turn(CombatState& state,
                      MonsterTurnFn take_turn = default_monster_turn) noexcept;

}  // namespace sts::engine
