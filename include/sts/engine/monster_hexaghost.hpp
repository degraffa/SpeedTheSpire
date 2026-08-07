#pragma once

// Hexaghost: the orb-count move cycle, the player-HP-derived Divider, and the
// Inferno burn-upgrade latch.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled; every file read in full):
//   Hexaghost.java:50-95   (fields: the six move ids DIVIDER=1 TACKLE=2
//                           INFLAME=3 SEAR=4 ACTIVATE=5 INFERNO=6 (:79-84),
//                           searDmg 6, strengthenBlockAmt 12, fireTackleCount
//                           2, infernoHits 6, and the `activated` /
//                           `burnUpgraded` / `orbActiveCount` state),
//     :96-128  (ctor: EnemyType.BOSS (:98); setHp(int) -- a FIXED sheet, 264
//               from A9 else 250 (:102-106), but STILL ONE monsterHpRng draw
//               (setHp(int) == setHp(hp, hp), AbstractMonster.java:765-779;
//               Random.java:58-61 advances on a degenerate range too); a
//               DESCENDING stat chain (:107-122) -- A19+ str 3 / burns 2 /
//               tackle 6 / inferno 3, A4+ str 2 / burns 1 / tackle 6 /
//               inferno 3, else str 2 / burns 1 / tackle 5 / inferno 2; the
//               damage list [fireTackle, sear 6, DIVIDER -1, inferno]),
//     :130-134 (usePreBattleAction: UnlockTracker + BGM precache only),
//     :136-143 (createOrbs -- see WHAT AN ORB IS below),
//     :145-216 (takeTurn: the six move bodies),
//     :218-254 (getMove: the selection, keyed ENTIRELY on orbActiveCount),
//     :256-292 (changeState: Activate / Activate Orb / Deactivate),
//     :294-305 (die: presentation + onBossVictoryLogic, a run-layer concern);
//   Burn.java:31-35 (magicNumber 2), :56-64 (upgrade -> 4);
//   BurnIncreaseAction.java:25-51;
//   ShowCardAndAddToDiscardEffect.java:24-49 (the ctor itself does
//     `discardPile.addToTop(card)` -- the "effect" is only the animation);
//   RollMoveAction.java:17-21;
//   AbstractMonster.java:99 (EnemyType default), 431-437 (setMove/moveHistory),
//     465-467 (init -> rollMove -> aiRng.random(99)).
//
// ---------------------------------------------------------------------------
// WHAT AN ORB IS -- the modelling decision this task was required to record.
//
// `HexaghostOrb` is NOT an AbstractMonster. It extends nothing, holds only
// x/y/index/activated/hidden/playedSfx, and its whole surface is
// activate/deactivate/hide/update (HexaghostOrb.java:19-59); `HexaghostBody` is
// a rotating sprite (HexaghostBody.java:18-66). The six orbs are constructed
// into a private ArrayList (Hexaghost.java:61,136-143), never added to the
// MonsterGroup, never targeted, never damaged, and hold no HP. They occupy no
// monster slot and take no turn.
//
// The ONLY combat-relevant quantity they carry is the scalar `orbActiveCount`
// (:93), and it is read in exactly one place: getMove's switch (:224-252). Its
// range is 0..6.
//
// It therefore needs neither monster `misc` fields nor extra powers, and no
// CombatState additive change -- so no SCHEMA_VERSION bump and no fixture
// regeneration (design §4.4). Three spare bits of the existing
// `MonsterState::flags` word hold it, alongside one bit for `burnUpgraded`; the
// bit constants live in combat_state.hpp with the other per-monster flags.
// Modelling an orb as a power slot would have been strictly worse: powers are
// visible to the power-hook dispatch and to the observation layer, and these
// are neither.
//
// Two further pieces of Java state need no storage at all:
//   * `activated` (:91) is a one-shot that getMove sets on its FIRST call
//     (:220-222). That call is init's rollMove, which hexaghost_init performs,
//     so every later getMove sees it true. There is no path back to false.
//   * The Divider base damage IS per-combat state and does need storage: it is
//     computed on the ACTIVATE turn and spent on the NEXT turn. It takes the
//     whole `pad0` scratch byte, exactly as The Guardian's HP baseline does.
//
// ---------------------------------------------------------------------------
// WHAT IS NATIVE AND WHY. The move EFFECTS are registry data (monsters.yaml id
// 22) except where they cannot be:
//   * DIVIDER's six hits are `player.currentHealth / 12 + 1` (:151) -- read off
//     the PLAYER at the ACTIVATE turn, so no ascension column can hold it;
//   * SEAR's created Burn is upgraded once `burnUpgraded` is set (:183-185) --
//     a per-combat latch, not a tier column;
//   * BurnIncreaseAction (:204) upgrades the Burns already in the draw and
//     discard piles, which is not an op any registry program can author.
// The SELECTION is native for the usual reason: it is not a move tree. getMove
// switches on orbActiveCount alone, so the fight is a fixed opener followed by
// a 7-long cycle:
//     turn 1  ACTIVATE                     (orbs -> 6)
//     turn 2  DIVIDER                      (orbs -> 0)
//     then, repeating: SEAR TACKLE SEAR INFLAME TACKLE SEAR INFERNO
// -- one orb lit per SEAR/TACKLE/INFLAME, all six doused by INFERNO.
//
// THE ROLLED `num` IS NEVER READ. getMove(int num) does not mention its
// argument (:218-254), exactly as with Lagavulin and the Slime Boss. Every
// RollMoveAction still consumes one aiRng.random(99) draw, so the DRAW COUNT is
// the observable that has to be pinned, and there is no roll-driven branch for
// an independent-XS128 fixture to cover.
//
// WHY THE TAIL WORK RIDES ON THE QUEUED ROLL. Five of the six move bodies end
// with the same two-item tail: a ChangeStateAction, then a RollMoveAction
// (:166-167, :175-176, :187-188, :195-196, :208-209). Nothing is ever queued
// between them, and neither action queues anything of its own -- changeState
// mutates orbActiveCount and plays sounds (:256-292), and BurnIncreaseAction
// upgrades cards in place and adds three via ShowCardAndAddToDiscardEffect,
// whose CONSTRUCTOR does the discard-pile insert (ShowCardAndAddToDiscardEffect
// .java:48). The run of actions from BurnIncreaseAction through RollMoveAction
// is therefore atomic with respect to the rest of the queue, and this engine
// resolves it as one queued ROLL_MOVE item (hexaghost_roll_move below). That is
// exact, not an approximation: no interleaving is possible for it to get wrong.
// The alternative -- doing the state change synchronously in takeTurn -- would
// have moved it ahead of the move's own damage, which is a real reordering.
//
// The ACTIVATE turn is the one body with no RollMoveAction (:148-154): it does
// a DIRECT setMove(DIVIDER), so no move is rolled and no aiRng draw happens
// between turn 1 and turn 2.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Constructor + init(), folded: the fixed registry HP sheet (setHp(int),
// Hexaghost.java:102-106 -- fixed value, but ONE monsterHpRng draw all the
// same, AbstractMonster.java:765-779 + Random.java:58-61) and the one
// aiRng.random(99) draw
// whose value getMove discards in favour of the forced opening ACTIVATE
// (:220-222).
void hexaghost_init(CombatState& state, uint8_t monster_index) noexcept;

// The native takeTurn dispatch (Hexaghost.java:145-216): each move's data
// program plus the parts that cannot be data -- Divider's player-HP-derived
// six hits, Sear's conditionally-upgraded Burn, and the queued tail.
void hexaghost_take_turn(CombatState& state, uint8_t monster_index) noexcept;

// The queued ROLL_MOVE body: the folded ChangeStateAction (+ Inferno's
// BurnIncreaseAction) followed by rollMove -> getMove. See "WHY THE TAIL WORK
// RIDES ON THE QUEUED ROLL" above. Consumes exactly one aiRng.random(99) draw
// and discards its value.
void hexaghost_roll_move(CombatState& state, uint8_t monster_index) noexcept;

// --- State accessors, exposed so the tier-2 test reads the same bits the
//     module writes rather than re-deriving the packing -------------------

// `orbActiveCount` (Hexaghost.java:93), 0..6.
[[nodiscard]] uint8_t hexaghost_orb_count(const MonsterState& monster) noexcept;

// `burnUpgraded` (Hexaghost.java:92), set by the first Inferno (:205-207).
[[nodiscard]] bool hexaghost_burn_upgraded(const MonsterState& monster) noexcept;

// The Divider per-hit base damage stored on the ACTIVATE turn, or 0 before it.
[[nodiscard]] uint8_t hexaghost_divider_damage(
    const MonsterState& monster) noexcept;

// `AbstractDungeon.player.currentHealth / 12 + 1` (Hexaghost.java:151) -- Java
// int division, truncating. Exposed so the test pins the FORMULA against the
// cited line rather than a table of expected outputs.
[[nodiscard]] int32_t hexaghost_divider_base(int32_t player_hp) noexcept;

}  // namespace sts::engine
