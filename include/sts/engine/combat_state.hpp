#pragma once

// CombatState -- the full in-combat game state (design doc §4.2), budget
// <= 8192 bytes (see the static_assert at the bottom of this file for why the
// original 4 KB figure was raised). This is the struct the batch API's advance()
// steps (design doc §7) and the diff harness snapshots (design doc §8). It is
// derived from RunState at combat start and folded back at combat end; the two
// never alias
// (design doc §4.4). The RunState->CombatState derivation is LIVE:
// enter_combat (src/engine/run_advance.cpp) mirrors combat_begin but seeds the
// player sheet (hp/max_hp) and the relic mirror from the run. combat_begin
// (advance.hpp) remains the standalone entry point that takes no RunState.
//
// Design doc §4.1 principles enforced here:
//   * trivially copyable, no pointers, no heap -- snapshot is a memcpy;
//   * fixed-capacity arrays with counts -- overflow is a hard assert in the
//     rule code that fills them, not a reallocation;
//   * all cross-references are indices, never pointers (piles hold uint8_t
//     indices into the card-instance pool, monster/card queues hold indices);
//   * value-initialized before use so byte-hashing is padding-stable -- the
//     struct is a plain aggregate (no user-declared constructors) precisely so
//     `CombatState s{};` zero-fills it, including padding (design doc §4.1,
//     verified by state_test's "two value-initialized states hash-equal" case).
//
// SCOPE (design doc §9): the M1 walking skeleton is Ironclad vs. Jaw Worm only.
// Most capacities below (128-deep piles, 5 monster slots, 24 power slots) are
// intentional forward design per the §4.2 table -- the skeleton exercises only
// a small corner of them. Fields whose exact bit-layout the design doc leaves
// open (the `flags` words, `misc`, monster `intent`/`move_history` encodings)
// are minimal documented placeholders; their semantics are defined by the
// action-queue pump, monster AI, and effect layers, not invented here.
//
// The four action queues (design doc §5.1) are `action_queue` (the game's
// `actions` list), `pre_turn_actions` (the game's `preTurnActions` list;
// `addToTurnStart` prepends here, GameActionManager.java:59,145), `card_queue`,
// and `monster_queue`. `pre_turn_actions` uses the same `ActionQueueItem`
// element type as the main queue, and `turn_has_ended` is the bookkeeping flag
// the pump's §5.2-step-6 gate reads.

#include <cstdint>
#include <type_traits>

#include "sts/engine/schema.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/engine/run_state.hpp"  // RelicSlot, kRelicCap (combat relic mirror)

namespace sts::engine {

// --- Capacities (design doc §4.2 table) -------------------------------------

inline constexpr int kPowerCap = 24;       // player and per-monster power slots
inline constexpr int kCardPoolCap = 160;   // one shared card-instance pool
inline constexpr int kHandCap = 10;
inline constexpr int kDrawCap = 128;
inline constexpr int kDiscardCap = 128;
inline constexpr int kExhaustCap = 128;
inline constexpr int kLimboCap = 8;
// kMonsterCap grew 5 -> 7 for multi-monster combat + the encounter framework.
// The largest INITIAL S1 group is 5 (Lots of Slimes), but mid-combat splits retain
// dead records in place (SuicideAction/die() never remove from MonsterGroup.monsters,
// SuicideAction.java:29-34 / AbstractMonster.java:925-951): Slime Boss fully split =
// 1 boss(dead) + 2 Large(dead) + 4 Medium = 7 simultaneous records. Sentry's
// index-parity opener and SpawnMonsterAction smart-positioning read list indices
// among ALL (incl. dead) records, so index identity must be stable -- growing the
// cap (rather than compacting dead records) preserves it. Sized once here so the
// framework covers the worst split case above (scoping report §1.5/§6). Budget at
// the time of that change: MonsterState was 112 B, so 7 slots = 784 B and
// sizeof(CombatState) went 3672 -> 3896, inside the then-4 KB ceiling; that was
// schema bump 3 -> 4. (The flags widening later grew MonsterState to 116 B and
// CombatState to 3928 B -- schema bump 4 -> 5, see the static_asserts below.)
//
// It then grew 7 -> 23 for the Act-2/3 monster wave (schema v7). Three
// mid-combat spawners can each exceed 7 records, and NONE of them has a bound
// that can be DERIVED -- every one grows with fight length, because the game
// never removes a dead record:
//   * Gremlin Leader -- 3 initial + 2 per RALLY, and RALLY is re-chosen
//     whenever numAliveGremlins() < 2 (GremlinLeader.java:144-189). Two rallies
//     already reach 7.
//   * The Collector -- 1 boss + 2 initial TorchHeads, +2 per REVIVE, and REVIVE
//     has NO once-per-combat latch (TheCollector.java:180-203).
//   * Reptomancer -- respawns daggers whenever canSpawn() allows, and the
//     daggers self-destruct on their second turn (Reptomancer.java:117-146), so
//     records accumulate for as long as the fight lasts.
// So 23 is a BUDGET pick with a hard assert behind it, not a proof. It is the
// LARGEST value the 8192 B CombatState ceiling admits, MEASURED (compiler
// probe over a patched copy of this header), not predicted:
//
//   cap | MonsterState | CombatState | headroom to 8192
//    7  |     212      |    4696     |   +3496   (before)
//   20  |     212      |    7456     |    +736
//   23  |     212      |    8088     |    +104   <-- chosen
//   24  |     212      |    8304     |    -112   (the Wave-3 grant; does NOT fit)
//   25  |     212      |    8512     |    -320
//
// The Wave-3 allocation granted 24. It does not fit, and WHY the grant was one
// too many is worth recording: the scouting estimate was made against
// MonsterState = 116 B / CombatState = 3928 B, which are PRE-schema-v6 numbers.
// The v6 PowerSlot widening (4 -> 8 B, over kPowerCap = 24 slots per monster)
// took MonsterState to 212 B and made every slot nearly twice as expensive.
// Note 24 measures 8304, not the naive 8300: the slot-count parity moves the
// implicit tail padding by 4 B, which is exactly why these are probe results
// and not arithmetic.
//
// Two options were considered and NOT taken, recorded so the next reader does
// not re-derive them:
//   * raising the 8192 ceiling -- it was raised 4096 -> 8192 by the project
//     owner (design §11) and is not a number a framework task may move;
//   * splitting kPowerCap into a smaller per-MONSTER power cap -- measured, and
//     it does work (at cap 24, a 20-slot monster power cap gives CombatState
//     7504; a 16-slot one, 6704), but it is a new capacity constant carrying a
//     truncation risk on any monster that stacks many powers. Recorded for S3
//     if capacity pressure returns.
//
// 23 vs 24 costs nothing real: no consumer above has a derived safe bound, so
// both numbers are the same kind of guess, and the hard assert in
// spawn_monster_at_slot is what actually protects the invariant.
inline constexpr int kMonsterCap = 23;
inline constexpr int kActionQueueCap = 64;
inline constexpr int kCardQueueCap = 16;
inline constexpr int kMonsterQueueCap = 5;
// preTurnActions capacity (design doc §5.1). `addToTurnStart` prepends only
// start-of-next-turn relic/power actions here, a handful per turn in the
// skeleton's scope, so 16 is generous headroom (16 * 12 B = 192 B, comfortably
// inside CombatState's size budget -- a CEILING of 8192 B against an actual
// sizeof of 3928 B; see the static_assert at the bottom of this file, and note
// the ceiling was raised from 4096 on 2026-07-24). Sized to match the main
// action ring's element type/idiom rather than trimmed to a tight bound.
inline constexpr int kPreTurnActionQueueCap = 16;
// In-combat master-deck obtain accumulator depth (schema v7; see the field's
// comment on CombatState.pending_obtain). Sized to exactly consume the 7 bytes
// that were `pad_relics`, so the accumulator costs nothing.
inline constexpr int kPendingObtainCap = 3;
// In-combat BELT obtain accumulator depth (see CombatState.pending_potion). The
// OBTAIN_POTION opcode is ObtainPotionAction's first tick, and the combat layer
// can no more reach RunState.potions than it can the master deck, so the
// resolution accrues here and the run layer places it at the command boundary.
// Sized to the belt itself: Entropic Brew queues exactly `potionSlots` obtains
// in one use (EntropicBrew.java:40-42) and the belt holds at most five slots
// (run_state.hpp kPotionCap -- asserted against this constant where the drain
// lives), and to exactly consume the 6 bytes that were `pad_rng_align`
// (1 byte of count + 5 one-byte ids), so it costs nothing.
inline constexpr int kPendingPotionCap = 5;

// CombatState.flags BIT 0 IS RETIRED -- deliberately left unallocated.
//
// It used to be kCombatFlagFrailJustApplied: FrailPower.justApplied, for the
// PLAYER's instance only. That was always the shape's limit, and it became the
// bug when Vulnerable and Weak needed the same latch: those two can sit on the
// player and all five monsters at once, so one whole-combat bit cannot describe
// them, and a monster-owned Frail could never tick either. The latch for all
// three duration debuffs now lives per-instance in the slot's own
// `PowerSlot.counter` (types.hpp; src/engine/powers/power_duration_debuff.hpp).
//
// The bit is not reused. A stale reader that still tested bit 0 would silently
// see "not just applied" instead of failing, and the symbol is gone so any such
// reader is a compile error instead.

// CombatState.flags bit for the room's cannotLose latch (monster split framework).
// Set by the CANNOT_LOSE opcode and cleared by CAN_LOSE (CannotLoseAction.java:
// 12-15 / CanLoseAction.java:12-15). While set, the pump's all-monsters-dead
// victory transition is suppressed (AbstractMonster.updateDeathAnimation:869
// gates endBattle() on !cannotLose), so a splitting slime's suicide cannot end
// the combat before its children's SPAWN_MONSTER actions resolve. Player death
// is NOT gated (the Java latch only guards the victory branch).
inline constexpr uint32_t kCombatFlagCannotLose = 1u << 1;

// CombatState.flags bit for the room's `mugged` latch (AbstractRoom.mugged).
// Set SYNCHRONOUSLY when a thief's Escape move runs (Looter.takeTurn case
// ESCAPE, Looter.java:128 -- before the queued EscapeAction resolves). The
// reward layer reads it: a mugged room's combat reward keeps no gold and the
// thief's stolen gold is gone with it. Nothing in the combat layer reads it
// back.
inline constexpr uint32_t kCombatFlagMugged = 1u << 2;

// CombatState.flags bit for the PLAYER's escape (Smoke Bomb). SmokeBomb.use
// (SmokeBomb.java:37-48) marks the room `smoked` and sets player.isEscaping +
// a 2.5s escapeTimer; the timer expiring is what ends the battle
// (AbstractPlayer.updateEscapeAnimation, AbstractPlayer.java:2281-2292 --
// endBattle() unconditionally, NOT gated on cannotLose). With no animation
// clock the three Java fields (smoked, isEscaping, the timer) collapse into
// this one bit; the pump's combat-over check reads it every step, which is what
// lets a combat end while monsters are still alive.
inline constexpr uint32_t kCombatFlagPlayerEscaped = 1u << 3;

// CombatState.flags bit for Centennial Puzzle's once-per-combat latch.
//
// CentennialPuzzle's flag is a `private static boolean usedThisCombat`
// (CentennialPuzzle.java:21) -- a COMBAT-GLOBAL, not a per-relic-instance field.
// wasHPLost reads and sets it (CentennialPuzzle.java:41,46) and atPreBattle
// resets it to false (CentennialPuzzle.java:33-34). The relic's own
// `this.counter` is NEVER touched anywhere in the class, so it keeps
// AbstractRelic's -1 for the life of the run and CommunicationMod reports -1.
//
// It therefore may NOT live in RelicSlot.counter: fold_back_combat
// (run_advance.cpp) copies every mirrored counter into RunState.relics[i].counter,
// which diff_run_states compares against the translated capture field-for-field
// (tools/diff_harness/src/differ.cpp). A latch stored there is a guaranteed
// `relics[i].counter: -1 -> 0` divergence the instant the relic is acquired.
//
// A CombatState flags bit is the exact analogue of a static field with an
// atPreBattle reset, and it needs no reset code: enter_combat value-initializes
// a fresh `CombatState s{}` for every combat (run_advance.cpp), so the latch is
// clear at every atPreBattle and set only within one combat. Bit 4 was
// previously zero, so nothing about the struct's size or schema changes -- the
// same shape as the Combust hpLoss field below. CombatState.flags is
// compared only by the replay tool's opt-in COMBAT TRIAGE print, which its own
// call site documents as "not part of the acceptance ... never a pass/fail
// signal" -- exactly as for kCombatFlagMugged / kCombatFlagCannotLose /
// kCombatFlagPlayerEscaped, none of which the translator reconstructs either.
inline constexpr uint32_t kCombatFlagCentennialPuzzleUsed = 1u << 4;

// CombatState.flags bit for Red Skull's private `isActive` latch.
//
// RedSkull declares `private boolean isActive` (RedSkull.java:24); it never
// writes AbstractRelic.counter, so CommunicationMod reports the inherited -1
// for the entire run.  Storing the latch in RelicSlot.counter therefore leaks a
// private field into an oracle-visible one and folds the wrong 0/1 back into
// RunState.  Fresh G7 capture STS200002 pinned that exact failure at the first
// Red Skull reward: oracle -1, old simulator 0.
//
// Red Skull is unique in the S1 relic pools, so one combat-global bit is the
// exact reachable shape.  atBattleStart clears it, the queued RedSkull$1
// decider and onBloodied set it, and onNotBloodied clears it.  A fresh
// CombatState also starts clear, so this consumes no layout or schema change.
// Like Centennial Puzzle's private/static latch above, this internal bit is not
// reconstructed by the translator and is not an acceptance-diff field.
inline constexpr uint32_t kCombatFlagRedSkullActive = 1u << 5;

[[nodiscard]] inline constexpr bool combat_red_skull_active(
    uint32_t flags) noexcept {
    return (flags & kCombatFlagRedSkullActive) != 0u;
}

// CombatState.flags bit for Necronomicon's private `activated` latch (S2.34
// claim, stage-b-tasks.md "S2.34 allocations").
//
// Necronomicon declares `private boolean activated = true` (Necronomicon.
// java:28) and never writes AbstractRelic.counter, so the oracle-visible
// counter stays -1 for the whole run -- RelicSlot.counter is OFF LIMITS for
// exactly the Red Skull reason above. Stored INVERTED ("used this turn") so
// that a value-init CombatState is ARMED, matching the field's `= true`
// initializer, and no fixture byte moves. atTurnStart clears it every turn --
// turn 1 included, so whatever a previous combat left behind is unobservable
// -- and the once-per-turn replay in relic_native_necronomicon sets it.
// Internal, not translator-reconstructed, not an acceptance-diff field.
inline constexpr uint32_t kCombatFlagNecronomiconUsed = 1u << 6;

// CombustPower keeps a private hpLoss counter distinct from its visible damage
// amount. The player-owned counter lives in otherwise-reserved CombatState
// flags: the cards are self-only, and this preserves the frozen PowerSlot/POD
// layout while reproducing CombustPower.stackPower's +1 hpLoss per application.
inline constexpr uint32_t kCombatFlagCombustHpLossShift = 8u;
inline constexpr uint32_t kCombatFlagCombustHpLossMask = 0xFFu << kCombatFlagCombustHpLossShift;

// CombatState.flags: how many Fairy in a Bottle potions the belt still holds.
//
// FairyPotion is never USED (canUse() is `return false`, FairyPotion.java:47-50).
// It fires from AbstractPlayer.damage (AbstractPlayer.java:1482-1497), the
// ORDINARY damage path, on ANY lethal HP write:
//
//     if (this.currentHealth < 1) {
//         if (!this.hasRelic("Mark of the Bloom")) {
//             if (this.hasPotion("FairyPotion")) {
//                 for (AbstractPotion p : this.potions) {
//                     if (!p.ID.equals("FairyPotion")) continue;
//                     p.flash(); this.currentHealth = 0; p.use(this);
//                     topPanel.destroyPotion(p.slot);
//                     return;                              // <-- no death
//                 }
//             } else if (hasRelic("Lizard Tail") && counter == -1) { ... }
//         }
//         this.isDead = true; ...
//     }
//
// WHY A COMBAT-STATE MIRROR AND NOT A RUN-LAYER CHECK. potions.hpp records a
// deliberate layer boundary: the potion BELT lives in RunState and CombatState
// has none. But the fix cannot live at the run layer either, because
// AbstractPlayer.damage RETURNS -- the player is alive for the REST OF THE SAME
// ACTION, while the engine's pump ends the combat on its next step's
// combat-over check and finish_combat_after_action would already have routed to
// RUN_OVER. A post-hoc run-layer revive would be observably wrong. So only the
// FACT of an armed Fairy is mirrored in, exactly as the relic mirror does, and
// the run layer burns the real slots at fold-back.
//
// A COUNT rather than a bit, because multiple Fairies are legal (kPotionCap is
// 5; at A20 potion_slot_count is 2) and the Java loop `return`s on the FIRST
// match -- exactly ONE is consumed per lethal event and a second held Fairy
// survives for a later one. Three bits cover the whole belt with margin. Bit 19
// of this stage's 16-19 allocation is RELEASED unspent.
//
// Storage rationale is Centennial Puzzle's and the elite-room bit's below: bits
// 16-18 were previously zero, so no offset, no sizeof and no SCHEMA_VERSION
// move, and enter_combat's fresh `CombatState s{}` is the per-combat reset. A
// standalone combat built by combat_begin (advance.cpp) has no belt at all, so
// the count stays 0 there -- the right answer rather than a gap, since a bare
// CombatState caller genuinely holds no potions.
inline constexpr uint32_t kCombatFlagFairyArmedShift = 16u;
inline constexpr uint32_t kCombatFlagFairyArmedMask =
    0x7u << kCombatFlagFairyArmedShift;

[[nodiscard]] inline constexpr uint8_t combat_fairy_armed(
    uint32_t flags) noexcept {
    return static_cast<uint8_t>((flags & kCombatFlagFairyArmedMask) >>
                                kCombatFlagFairyArmedShift);
}
[[nodiscard]] inline constexpr uint32_t with_combat_fairy_armed(
    uint32_t flags, uint8_t n) noexcept {
    return (flags & ~kCombatFlagFairyArmedMask) |
           ((static_cast<uint32_t>(n) << kCombatFlagFairyArmedShift) &
            kCombatFlagFairyArmedMask);
}

// CombatState.flags bit for the ROOM's `eliteTrigger` (AbstractRoom.java:99,
// default false). It is per-ROOM state in the game and per-COMBAT here, which
// is exact: a room hosts one combat and the flag is set before that combat is
// constructed, never during it.
//
// The COMPLETE producer list, from `grep -rn eliteTrigger com/`:
//   * MonsterRoomElite ctor          (MonsterRoomElite.java:33)
//   * DeadAdventurer, an ACT-1 EVENT (DeadAdventurer.java:116) -- set on an
//     EventRoom just before its combat, which is why the marker cannot be
//     derived from RoomType alone and enter_combat takes it as an argument
//   * Colosseum                      (Colosseum.java:75) -- Act 2, no S1 row
// MonsterRoomBoss does NOT set it (MonsterRoomBoss.java:22-24 sets only
// mapSymbol). A BOSS room is therefore NOT an elite room, and Sling of Courage
// / Preserved Insect do NOT fire there -- see energy_master (action_queue.cpp)
// for the one consumer that wants "elite OR boss" and builds it from this bit
// plus the monsters' own EnemyType.
//
// The COMPLETE consumer list: Sling.atBattleStart (Sling.java:30-37),
// PreservedInsect.atBattleStart (PreservedInsect.java:30-41),
// SlaversCollar.beforeEnergyPrep (SlaversCollar.java:46-57), and
// AbstractCreature.java:371 -- a render-scale tweak with no state effect.
//
// Storage rationale is the Centennial Puzzle one directly above: bit 20 was
// previously zero, so no offset, no sizeof and no SCHEMA_VERSION move, and
// enter_combat's fresh `CombatState s{}` is the per-combat reset. A
// standalone combat built by combat_begin (advance.cpp) has no room, so the
// bit stays clear there -- the same answer the game gives for an
// AbstractRoom whose ctor never set it.
inline constexpr uint32_t kCombatFlagEliteRoom = 1u << 20;

[[nodiscard]] inline bool combat_is_elite_room(uint32_t flags) noexcept {
    return (flags & kCombatFlagEliteRoom) != 0u;
}

// CombatState.flags bits for Orange Pellets' three type latches.
//
// OrangePellets declares them as `private static boolean SKILL / POWER / ATTACK`
// (OrangePellets.java:21-23) -- STATICS, i.e. combat-global rather than per-relic
// -instance, which is the same shape as Centennial Puzzle's usedThisCombat and
// the same reason they may not live in RelicSlot.counter: the game never writes
// OrangePellets.counter, so it stays at AbstractRelic's -1 and any write here
// would fold out to RunState and diverge from the capture.
//
// Their only clear is atTurnStart (:34-39); atPreBattle does NOT touch them. A
// fresh value-initialised CombatState is equivalent because turn 1's atTurnStart
// runs before any card can be played (AbstractRoom.java:253) -- said explicitly
// rather than implying the game resets them at combat start, because it does not.
//
// Bits 21-23 were previously zero, so no offset, no sizeof, no SCHEMA_VERSION.
inline constexpr uint32_t kCombatFlagOrangePelletsAttack = 1u << 21;
inline constexpr uint32_t kCombatFlagOrangePelletsSkill = 1u << 22;
inline constexpr uint32_t kCombatFlagOrangePelletsPower = 1u << 23;
// CombatState.flags bit for Art of War's `gainEnergyNext`, stored INVERTED as
// "an ATTACK has been played this turn".
//
// ArtOfWar (ArtOfWar.java:23-24, :52-82) keeps two per-combat booleans:
//     atPreBattle : firstTurn = true; gainEnergyNext = true;
//     atTurnStart : if (gainEnergyNext && !firstTurn) addToBot GainEnergyAction(1);
//                   firstTurn = false; gainEnergyNext = true;
//     onUseCard   : if (card.type == ATTACK) gainEnergyNext = false;
// i.e. +1 energy at the start of turn N (N >= 2) iff no ATTACK was played during
// turn N-1.
//
// Only ONE of the two needs storage. `firstTurn` is derivable: the relic hook
// runs from start_of_turn BEFORE `++s.turn` (action_queue.cpp), and combat
// construction leaves s.turn == 0, so the first atTurnStart of a combat is
// exactly `s.turn == 0`. Storing `gainEnergyNext` INVERTED keeps the
// value-initialised default correct with no atPreBattle write: a fresh
// CombatState means "no attack played yet", which is what atPreBattle's
// `gainEnergyNext = true` says.
//
// A flags bit and not RelicSlot.counter for the usual reason: ArtOfWar's counter
// is never written in the game, stays at AbstractRelic's -1, and
// fold_back_combat would carry any write into RunState where the differ compares
// it (the Centennial Puzzle divergence class).
inline constexpr uint32_t kCombatFlagArtOfWarAttackPlayed = 1u << 24;

// ---- S3.42: the player's FACING (AbstractCreature.flipHorizontal) -----------
//
// CombatState.flags bit for `AbstractDungeon.player.flipHorizontal`
// (AbstractCreature.java:74, `= false`). It exists for exactly one reader:
// AbstractMonster.applyBackAttack (AbstractMonster.java:1015-1017)
//
//     return player.hasPower("Surrounded")
//         && (   player.flipHorizontal && player.drawX < this.drawX
//             || !player.flipHorizontal && player.drawX > this.drawX);
//
// which is the whole gate on the hard-coded 1.5x back-attack multiplier. SET
// means the player is facing LEFT (the Java's `flipHorizontal == true`).
//
// THE COMPLETE PRODUCER LIST, from `grep -rn flipHorizontal com/` with every hit
// read (presentation-only draw/skeleton hits excluded):
//   * AbstractDungeon.java:1802-1806 -- room entry. The Shield-and-Spear room
//     moves the player to WIDTH/2 and DOES NOT reset the flag; every OTHER room
//     entry moves them to WIDTH*0.25 and sets it false. A fresh `CombatState s{}`
//     per combat (enter_combat, run_advance.cpp) therefore reproduces both arms
//     exactly, with no reset code: the guards' room is entered from a room that
//     already cleared it.
//   * AbstractPlayer.playCard :1291-1293 -- `if (hasPower("Surrounded"))
//     flipHorizontal = hoveredMonster.drawX < drawX`, for an ENEMY /
//     SELF_AND_ENEMY targeted card. Reproduced in queue_card_play (card_play.cpp),
//     which IS playCard's seam.
//   * PotionPopUp :199-201 -- the same two lines for a TARGETED potion.
//     Reproduced in use_potion (potions.cpp).
//   * SpireShield.die :170 == SpireSpear.die :177 -- face the survivor.
//     Reproduced in the guards' monster_die_after_fn bodies.
//   * SmokeBomb.use :37-48 -- `flipHorizontal = !flipHorizontal` (SmokeBomb.java:
//     44), and NOT reproduced: Smoke Bomb's own action ends the combat on the
//     same beat (it sets escapeTimer/isEscaping, not an EscapeAction -- that
//     class is monster-escape-only; AbstractPlayer.updateEscapeAnimation,
//     :2281-2292, is what counts the timer down and calls endBattle()), so no
//     later read of the flag exists. Recorded rather than left to inference.
//   * AbstractPlayer.java:2288 -- cleared at the end of the escape animation,
//     which this engine has no clock for and whose combat is already over.
//
// Storage rationale is the Centennial Puzzle / elite-room one: bit 25 was
// RELEASED UNSPENT by the Stage-B grant table and was previously zero, so no
// offset, no `sizeof`, no `SCHEMA_VERSION` and no committed fixture byte moves,
// and enter_combat's fresh `CombatState s{}` is the per-combat reset. Like
// kCombatFlagMugged / kCombatFlagCannotLose, it is internal, not reconstructed by
// the translator, and not an acceptance-diff field.
inline constexpr uint32_t kCombatFlagPlayerFacingLeft = 1u << 25;

[[nodiscard]] inline constexpr bool player_facing_left(uint32_t flags) noexcept {
    return (flags & kCombatFlagPlayerFacingLeft) != 0u;
}

inline constexpr uint32_t kCombatFlagOrangePelletsMask =
    kCombatFlagOrangePelletsAttack | kCombatFlagOrangePelletsSkill |
    kCombatFlagOrangePelletsPower;

// kCardPoolCap == 160 fits in a uint8_t index (0..159 <= 255), so every pile
// stores its members as uint8_t indices into card_pool.
using CardPoolIndex = uint8_t;
static_assert(kCardPoolCap <= 256,
              "card-pool indices are uint8_t; pool cannot exceed 256 rows");

// --- CombatPhase ------------------------------------------------------------

// Coarse combat phase (design doc §4.2 header group). The action-queue pump
// drives the fine-grained WAITING_ON_USER / resolving distinction via the
// queues; this enum is the top-level state the batch API reports. Minimal
// placeholder set for the skeleton -- extended as later phases need it.
enum class CombatPhase : uint8_t {
    NONE = 0,          // uninitialized (value-init default)
    WAITING_ON_USER,   // player may act (design doc §5.2 step 7)
    RESOLVING,         // pump is draining queues
    COMBAT_OVER,       // player or all monsters dead
};

// --- ActionQueueItem (design doc §5.1, §4.2) --------------------------------

// One entry of the main action ring (`actions`). Storage only; the queue
// mechanics live in action_queue.hpp. `opcode` indexes the effect-interpreter
// op set (design doc §5.5/§6: DAMAGE/BLOCK/APPLY_POWER/...);
// `src`/`tgt` are actor indices (player is a reserved sentinel, monsters are
// monster-array indices); `amount` is the signed op argument; `flags` is a
// reserved per-action bitfield. Field widths are exactly design doc §4.2's
// `{opcode: u16, src: u8, tgt: u8, amount: i32, flags: u32}` -> 12 bytes, no
// padding.
struct ActionQueueItem {
    uint16_t opcode;
    uint8_t src;
    uint8_t tgt;
    int32_t amount;
    uint32_t flags;
};

static_assert(std::is_trivially_copyable_v<ActionQueueItem>);
static_assert(sizeof(ActionQueueItem) == 12,
              "ActionQueueItem must be 12 bytes (design doc §4.2: "
              "u16+u8+u8+i32+u32)");

// --- CardQueueItem (design doc §5.1, §4.2) ----------------------------------

// One pending card play. Storage only; the index-1 front-insertion rule and the
// null-card end-turn sentinel (design doc §5.1, trap 9) live in the queue
// mechanics (action_queue.hpp). `card_index` references the shared card-instance
// pool; `target` is a monster-array index (or a reserved sentinel for no/auto
// target).
struct CardQueueItem {
    CardPoolIndex card_index;
    uint8_t target;
};

static_assert(std::is_trivially_copyable_v<CardQueueItem>);
static_assert(sizeof(CardQueueItem) == 2);

// --- MonsterQueueItem (design doc §5.1, §4.2) -------------------------------

// One monster awaiting its turn. Storage only; queueMonsters / takeTurn
// ordering lives in the pump (action_queue.hpp). `monster_index` references the
// monster array; `flags` is a reserved per-entry bitfield (e.g. a future
// "already acted" marker).
struct MonsterQueueItem {
    uint8_t monster_index;
    uint8_t flags;
};

static_assert(std::is_trivially_copyable_v<MonsterQueueItem>);
static_assert(sizeof(MonsterQueueItem) == 2);

// --- MonsterState (design doc §4.2 monsters row) ----------------------------

// One monster slot: `{hp, maxHp, block, move history ×3, intent, flags}` plus 24
// power slots (design doc §4.2). hp/max_hp/block are int16 to match the player's
// widths (A20 numbers are small; signed leaves headroom for transient negative
// bookkeeping). `move_history` holds the last three move ids most-recent-first
// -- Jaw Worm's AI needs last-move and last-two-moves (design doc §9); the move
// id encoding is defined by the monster module (monster_jaw_worm.hpp), here it
// is an opaque uint8_t. `intent` is the telegraphed next move id. `flags` is a
// per-monster bitfield governed by the two-region policy directly below.
// `power_count` is the live length of `powers` (parallels the pile counts);
// empty slots also read PowerId::NONE.
//
// ---- MonsterState.flags: the two-region allocation policy ------------------
//
// The 32-bit `flags` word is split into two regions with different rules. The
// old 16-bit word was exhausted by LINEAR allocation -- a fresh bit for every
// monster type even though no record is ever two types at once -- so ~9
// flag-using Act-1 types consumed all 16 bits (the Guardian alone took 5).
// Acts 2-4 add many more stateful monsters; allocated linearly, 32 bits would
// run out too. The policy below is what makes 32 bits sufficient for the full
// game:
//
//   * Bits 0-23 -- TYPE-SCOPED and REUSABLE. A bit here means whatever the
//     OWNING monster type says it means, and is read only by code that already
//     knows `monster_id` (the type's own module, or a power native reached
//     through a power only that type can own). Two monster types MAY use the
//     same bit -- with different meanings -- provided no single monster record
//     can ever be both types at once. A new monster type therefore allocates
//     from 0-23 and should DELIBERATELY REUSE bits held by types it can never
//     co-occur with, rather than extend upward.
//     Reuse caveat: a bit consumed by a POWER's native body rather than the
//     monster's own module (RitualSkip by RITUAL's at_end_of_round,
//     CurlUpTriggered by CURL_UP's on-attacked body and the CURL_UP
//     apply/remove paths in interp_powers.cpp) is scoped to the set of types
//     that can OWN that power, not to one type -- a type reusing such a bit
//     must also never be able to own the power.
//     The existing Act-1 bits below keep their historical values; they are NOT
//     re-based (re-basing regenerates every fixture and buys nothing).
//
//   * Bits 24-31 -- GLOBAL and SCARCE. Flags read TYPE-AGNOSTICALLY, i.e. by
//     code that does not know which monster it is looking at. Each global bit
//     is unique forever; there are only 8, by design. A candidate belongs here
//     ONLY if some reader genuinely cannot check `monster_id` first. Today
//     there are exactly two: ESCAPED (bit 24), read by monster_dead_or_escaped()
//     below from the pump, interpreter, targeting, damage and power-walk
//     layers; and HALF_DEAD (bit 25, schema v7), read by
//     monster_basically_dead() from the pump's turn gate, the monster-queue
//     population, applyPreTurnLogic and the combat-over test.
//
// The next allocator's rule: take type-scoped bits from 0-23, reusing where
// the types provably cannot co-occur; a global bit is a design decision to be
// argued for, never a default.
//
// PER-MONSTER-TYPE fields (`flags` bits 0-23, `pad0`) carry monster-specific
// meaning, interpreted only by that monster's native module:
//   * `flags` bit kMonsterFlagRitualSkip -- Cultist: the RitualPower's `skipFirst`
//     (RitualPower.java:19,46-55). Set when the Cultist casts Incantation; the
//     RITUAL native at_end_of_round body consumes it to skip the first tick.
//   * `flags` bit kMonsterFlagCurlUpTriggered -- Louse: CurlUpPower.triggered
//     (CurlUpPower.java:25,38-43). Set synchronously by onAttacked before its
//     queued GainBlock/RemoveSpecificPower actions resolve, preventing a queued
//     multi-hit attack from triggering more than once; cleared on power removal.
//   * `pad0` -- MONSTER-TYPE-SCOPED SCRATCH, with NO single global layout. Each
//     monster module owns the WHOLE byte and subdivides it (or does not) as it
//     needs, because no record is ever two monster types at once. Read it only
//     after checking `monster_id`, and do not treat another monster's
//     subdivision as a constraint on yours. Current interpretations:
//       - Louse (Normal/Defensive): the per-instance rolled bite damage
//         (monsterHpRng draw in the ctor, LouseNormal.java:60). 5..8 fits a byte;
//         the louse turn reads it for the BITE DamageAction (monster_louse.cpp).
//       - Gremlin Wizard: `currentCharge`, the 1..4 charge-up counter that gates
//         Ultimate Blast (GremlinWizard.java:43,60-73). A plain whole-byte
//         counter. See monster_gremlin.cpp.
//       - Lagavulin: two 4-bit counters, idleCount in the low nibble and
//         debuffTurnCount in the high nibble (Lagavulin.java:70-71). Both are
//         bounded by their own state machine (idleCount stops mattering at 3,
//         debuffTurnCount is reset to 0 the turn after it reaches 2), so a
//         nibble each is ample. See monster_lagavulin.cpp.
//       - The Guardian: the HP baseline its Mode Shift accumulator measures
//         cumulative damage from -- a WHOLE-byte value, not a packed pair. The
//         Guardian's fixed 240/250 HP sheet (TheGuardian.java:97-106) fits
//         uint8_t's 0..255 with room to spare. See monster_guardian.cpp.
//       - Red Slaver: BIT FLAGS, not a counter -- bit 0 is
//         SlaverRed.usedEntangle (SlaverRed.java:55,90), the once-per-combat
//         latch its getMove reads at :145 and :149. Its sibling latch
//         `firstTurn` (:56,140-141) needs no storage: it is true only during the
//         init() rollMove, which is a distinct entry point here. This is the
//         first pad0 user to subdivide the byte into flags rather than treat it
//         as one value, which is exactly what "no single global layout" above
//         permits. See monster_slaver.hpp/.cpp.
//       - Hexaghost: the Divider per-hit base damage, computed on the ACTIVATE
//         turn as `player.currentHealth / 12 + 1` (Hexaghost.java:151) and
//         spent by the six hits of the NEXT turn (:157-165). Another WHOLE-byte
//         value: it saturates at 255, which needs a player above 3047 HP to
//         reach. See monster_hexaghost.cpp.
//   * `flags` bit kMonsterFlagSplitTriggered -- large slimes: the
//     `splitTriggered` one-shot latch (AcidSlime_L.java:71,150 /
//     SpikeSlime_L.java:66,138). Set synchronously by the damage() interrupt
//     the first time currentHealth falls to <= maxHealth/2 so later hits do not
//     re-queue the split telegraph.
//   * `flags` bits kMonsterFlagLagavulin{Asleep,IsOut,OutTriggered} -- Lagavulin:
//     the three independent booleans of its sleep/wake machine (`asleep` set once
//     by the ctor argument, `isOut` set by changeState("OPEN"), `isOutTriggered`
//     the one-shot latch that stops a second hit re-stunning it;
//     Lagavulin.java:67-69,85,88-91,185,204).
//   * `flags` bits kMonsterFlagGuardian* -- The Guardian: the two mode latches
//     plus the count of Defensive-Mode flips so far, which is what makes the
//     mode-shift threshold GROW (TheGuardian.java:61,245).
//   * `flags` bit kMonsterFlagByrdFlying -- Byrd: `isFlying` (Byrd.java:72,124,
//     165), the airborne latch its getMove reads to choose between the flying
//     move tree and the unconditional grounded HEADBUTT. Set by GO_AIRBORNE,
//     cleared by the GROUNDED change of state that Flight's removal triggers.
//   * `flags` bit kMonsterFlagSphericSecondMove -- Spheric Guardian:
//     `secondMove` (SphericGuardian.java:60), the one-shot that makes the
//     SECOND decision a forced Frail Attack before the strict Big-Attack /
//     Block-Attack alternation starts.
//   * `flags` bits kMonsterFlagHexaghostOrb* / *BurnUpgraded -- Hexaghost:
//     `orbActiveCount` (the ONLY thing its six presentation orbs contribute to
//     combat state) and the `burnUpgraded` latch (Hexaghost.java:92-93). See
//     monster_hexaghost.hpp for why the orbs need no records of their own.
// These are mutually exclusive across monster types (a Cultist never bites; a
// Louse never has Ritual), so no field is read under two meanings at once.
struct MonsterState {
    uint16_t monster_id;              // MonsterId; NONE == empty slot
    int16_t hp;
    int16_t max_hp;
    int16_t block;
    uint32_t flags;                   // two-region bitfield: 0-23 type-scoped,
                                      // 24-31 global (policy comment above)
    uint8_t move_history[3];          // last 3 move ids, [0] = most recent
    uint8_t intent;                   // telegraphed next move id
    uint8_t power_count;              // live length of powers[]
    uint8_t pad0;                     // monster-type-scoped scratch byte (see above)
    // The monster's HORIZONTAL POSITION KEY -- the `offsetX` its Java ctor was
    // given (AbstractMonster.java:139-152). It occupies what was `pad1[2]`, so
    // it costs ZERO bytes and moves no offset (the combat_gold-into-pad_piles
    // precedent below).
    //
    // WHY IT IS STORED rather than hand-derived. `SpawnMonsterAction`'s smart
    // positioning inserts a spawned record at
    //     position = (count of leading records with mo.drawX < m.drawX)
    // (SpawnMonsterAction.java:50-56), walking ALL records, dead included, and
    // `drawX = Settings.WIDTH * 0.75f + offsetX * Settings.xScale`
    // (AbstractMonster.java:152) is a strictly monotone affine function of
    // `offsetX` (xScale > 0). So the insertion slot is decidable from this one
    // integer, and comparing `offsetX` gives the SAME order as comparing the
    // float drawX -- which is why no float belongs in CombatState.
    //
    // Before this field every spawner hand-derived its slots from the Java
    // coordinates (monster_slime_boss.cpp's `spike_slot = mi` / `acid_slot =
    // mi + 2`, whose own comment admits the boss layout "must recompute indices
    // ... instead of reusing p/p+2 blindly"). The Act-2/3 wave lands four
    // dagger positions, two orb positions, two torch-head positions and three
    // gremlin positions at once; hand-derivation across all of them is the kind
    // of thing that is got wrong silently. With the key stored, smart
    // positioning is `smart_position_for()` below and needs no per-monster
    // arithmetic at all.
    //
    // int16_t is ample: every `offsetX` in the game is a small signed constant
    // (the widest in Acts 1-3 are Reptomancer's POSX {210, -220, 180, -250} and
    // the Gremlin Leader's {-366, -170, -532}). It is a PURE ORDERING KEY --
    // nothing reads it as a distance, so its units never matter.
    int16_t draw_x;
    PowerSlot powers[kPowerCap];
};

// ALL constants below are TYPE-SCOPED (region 0-23) except kMonsterFlagEscaped,
// which is GLOBAL (region 24-31) -- see the policy comment above the struct.
//
// Cultist RitualPower.skipFirst (MonsterState.flags bit): while set, the RITUAL
// power's next at_end_of_round Strength tick is skipped (RitualPower.java:48-53).
inline constexpr uint32_t kMonsterFlagRitualSkip = 0x0001u;
inline constexpr uint32_t kMonsterFlagCurlUpTriggered = 0x0002u;
// Large slime splitTriggered latch (AcidSlime_L.java:71,145-151 /
// SpikeSlime_L.java:66,133-139); see the MonsterState comment above.
inline constexpr uint32_t kMonsterFlagSplitTriggered = 0x0004u;
// Lagavulin's three sleep/wake booleans (Lagavulin.java:67-69). `asleep` is the
// ctor argument and never changes; `isOut` means the shell is open (set by
// changeState("OPEN"), :185); `isOutTriggered` is the one-shot latch that makes
// the damage() wake fire at most once (:69,204). They are NOT redundant: between
// the damage interrupt and the queued open, isOutTriggered is set while isOut is
// still false, and a LETHAL hit latches isOutTriggered without ever opening
// (changeState's !isDying guard, :184).
inline constexpr uint32_t kMonsterFlagLagavulinAsleep = 0x0008u;
inline constexpr uint32_t kMonsterFlagLagavulinIsOut = 0x0010u;
inline constexpr uint32_t kMonsterFlagLagavulinOutTriggered = 0x0020u;

// Bits 0x0008 / 0x0010 / 0x0020 belong to Lagavulin's three sleep/wake booleans
// and are not free; this batch was allocated from 0x0040 up rather than
// appending into them. A gap in this bitfield is legal -- nothing indexes it.
//
// The Guardian (TheGuardian.java, see monster_guardian.hpp). Two latches and a
// counter, all read only by monster_guardian.cpp:
//   * OPEN               -- `isOpen` (:76,248,262): Offensive Mode, the only
//                           mode in which damage accumulates toward a shift.
//   * CLOSE_UP_TRIGGERED -- `closeUpTriggered` (:77,263,289): set SYNCHRONOUSLY
//                           the instant the threshold is reached, so the hits
//                           still queued behind a multi-hit attack cannot
//                           trigger a second flip before the queued change of
//                           state resolves and clears `isOpen`.
//   * SHIFT_COUNT        -- how many times Defensive Mode has been entered. The
//                           threshold is dmgThreshold + 10 * this
//                           (dmgThresholdIncrease, :61,245), which is what makes
//                           the flip point GROW each cycle. Three bits (0..7) is
//                           ample: at A20 the thresholds run 40, 50, 60, 70 ...,
//                           so reaching an 8th flip would need 40+50+...+110 ==
//                           600 cumulative damage against a 250 HP sheet. The
//                           increment saturates rather than wrapping.
inline constexpr uint32_t kMonsterFlagGuardianOpen = 0x0040u;
inline constexpr uint32_t kMonsterFlagGuardianCloseUpTriggered = 0x0080u;
inline constexpr uint32_t kMonsterFlagGuardianShiftShift = 8u;
inline constexpr uint32_t kMonsterFlagGuardianShiftMask = 0x0700u;

// Hexaghost (Hexaghost.java, see monster_hexaghost.hpp). Bits 0x0001..0x0700
// are taken by the five monsters above, so this batch was allocated from 0x0800
// up. (When the field was 16 bits wide this left only 0x8000 free, which the
// Escaped bit then took -- the exhaustion that forced the widening and the
// two-region policy above. Escaped now lives in the global region at bit 24.)
//   * ORB_COUNT     -- `orbActiveCount` (:93), the whole combat-relevant residue
//                      of the six presentation orbs. getMove switches on it
//                      (:224-252) and nothing else reads it. Its range is 0..6:
//                      changeState "Activate" sets 6 (:268), "Activate Orb"
//                      increments and stops the cycle at 6 (:278-280), and
//                      "Deactivate" resets to 0 (:289) -- so three bits hold it
//                      exactly, with 7 unreachable.
//   * BURN_UPGRADED -- `burnUpgraded` (:92,205-207): a one-shot set by the first
//                      Inferno, after which every Burn Sear creates is upgraded
//                      (:183-185). Never cleared.
inline constexpr uint32_t kMonsterFlagHexaghostOrbShift = 11u;
inline constexpr uint32_t kMonsterFlagHexaghostOrbMask = 0x3800u;
inline constexpr uint32_t kMonsterFlagHexaghostBurnUpgraded = 0x4000u;

// S2.21 (Act-2 city normals I). Allocated from 0x8000 up -- FRESH bits, not a
// reuse of Lagavulin's 0x0008/0x0010/0x0020, and that choice is argued rather
// than defaulted because the policy above prefers reuse where types provably
// cannot co-occur (which these do not: an Act-1 elite is never an Act-2 normal,
// so reuse WOULD have been sound).
//
// The reason not to: reuse conserves a resource that is not scarce. The
// widening to 32 bits left bits 15-23 free, this batch needs two of them, and
// six remain after it -- while a reused bit makes every raw read of `flags` (a
// debugger, a log line, a future audit) ambiguous until the reader has also
// checked monster_id. The existing precedent already chose the same way: the
// Guardian's batch was allocated from 0x0040 "rather than appending into"
// Lagavulin's bits and left the gap (see the note above kMonsterFlagGuardianOpen).
//
// Only TWO bits are spent. The third this batch was granted -- the Chosen's
// `usedHex` -- turned out to need NO storage at all, and the reason is worth
// recording because it is not obvious from the Java. At A17+ (the engine's fixed
// A20) getMove's first arm is `if (!usedHex) { usedHex = true; HEX; return; }`
// (Chosen.java:154-158), and getMove runs from exactly two places: init()'s
// rollMove and a queued RollMoveAction. So the latch is false on precisely the
// init call and true forever after -- it IS "is this the init call", which the
// module's init/roll split already answers structurally. The Red Slaver's
// `firstTurn` needs no storage for the same reason (see the pad0 note above).
// The bit would become real if the sub-A17 arm (:174-197) were ever made live,
// because there `firstTurn` forces a POKE opener and HEX moves to the SECOND
// decision -- a genuine second-call latch. The unused bit is RELEASED to free
// rather than left as a gap: "a gap costs nothing" holds for registry ids, whose
// numbering is append-only, and NOT for a bitfield, where the supply is finite
// and nothing ever encoded the value -- the same call the unused fuzz MoveCat
// and CardFlag contingencies made.

// Byrd `isFlying` (Byrd.java:72,124,165). SET == airborne, and it starts SET
// because the field initializer is `= true`. It is NOT derivable from the
// presence of the Flight power, and the two genuinely diverge: changeState
// ("GROUNDED") clears this the moment Flight's onRemove fires, while case 2
// (GO_AIRBORNE) sets it SYNCHRONOUSLY at :124, one queue slot AHEAD of the
// ApplyPowerAction that re-grants Flight at :126. Its only reader is the Byrd's
// own getMove (:186), which sends a grounded Byrd to HEADBUTT unconditionally.
inline constexpr uint32_t kMonsterFlagByrdFlying = 0x8000u;

// Spheric Guardian `secondMove` (SphericGuardian.java:60,152-155). SET == the
// second getMove has not happened yet, so the next decision is the forced
// FRAIL_ATTACK; cleared when it does. Distinct from its sibling `firstMove`
// (:59,147-151), which needs no storage because it is consumed on the init
// rollMove -- this one is consumed on the FIRST QUEUED roll, one decision later,
// which the init/roll split does not distinguish for free.
inline constexpr uint32_t kMonsterFlagSphericSecondMove = 0x10000u;

// S3.42 -- the two Act-4 guards' `moveCount` (SpireShield.java:41,:115,:136 ==
// SpireSpear.java:42,:118,:139). BOTH classes carry the same field with the same
// `% 3` cycle and the same trailing `++moveCount`, so ONE type-scoped 2-bit
// range serves both: an encounter never contains a third reader, the two rows are
// the same encounter's two slots, and neither ever coexists with a Spheric
// Guardian or a Byrd. Bits 17-18 of the type-scoped region (0-23) were free.
//
// TWO BITS ARE ENOUGH AND THE WRAP IS THE JAVA'S. The Java field is a plain
// `int` that increments forever and is only ever read as `moveCount % 3`, so any
// counter congruent mod 3 is observationally identical. Stored mod 3 (0/1/2 --
// the value 3 is never written), which makes the wrap free and the stored value
// the cycle index itself.
inline constexpr uint32_t kMonsterFlagSpireGuardMoveCountShift = 17u;
inline constexpr uint32_t kMonsterFlagSpireGuardMoveCountMask = 0x60000u;

[[nodiscard]] inline constexpr uint8_t spire_guard_move_count(
    const MonsterState& m) noexcept {
    return static_cast<uint8_t>((m.flags & kMonsterFlagSpireGuardMoveCountMask) >>
                                kMonsterFlagSpireGuardMoveCountShift);
}
// `++this.moveCount`, stored mod 3 (see above).
inline void spire_guard_bump_move_count(MonsterState& m) noexcept {
    const uint32_t next = (spire_guard_move_count(m) + 1u) % 3u;
    m.flags = (m.flags & ~kMonsterFlagSpireGuardMoveCountMask) |
              (next << kMonsterFlagSpireGuardMoveCountShift);
}

// Maw `roared` (Maw.java:58,90-95,119-122). SET == the ROAR has RESOLVED, which
// is what releases getMove from forcing it. Note the Java sets the field in
// takeTurn (:94), NOT in getMove, so the opening telegraph is always ROAR and
// the flag is still clear while the roar's own turn is being decided.
//
// THE FIRST DELIBERATE REUSE UNDER THE TYPE-SCOPED POLICY, and the point of the
// policy. It takes 0x0004, the value the large slimes' `splitTriggered` holds --
// chosen over the two lower bits precisely because those two (RitualSkip,
// CurlUpTriggered) are consumed by a POWER's native body and are therefore
// scoped to every type that can OWN Ritual or Curl Up, a wider claim to have to
// check. splitTriggered is read only by monster_slime_large.cpp, so the question
// is the narrow one: can one record be both a large slime and a Maw? It cannot
// -- nothing splits into, spawns or transforms a Maw; its only producer is the
// solo Act-3 "Maw" group (encounters.yaml id 50). Extending upward instead would
// have spent a fresh bit on a monster that can never co-occur with anything.
//
// Its sibling `turnCount` (:59) is NOT here: it is a counter, not a latch, and
// it takes the WHOLE of MonsterState.pad0 -- see monster_maw.hpp.
inline constexpr uint32_t kMonsterFlagMawRoared = 0x0004u;

// Writhing Mass `firstMove` (WrithingMass.java:44,146-147) and `usedMegaDebuff`
// (:45,116,169) need NO flag bits at all: both are one-bit latches read only by
// monster_writhing_mass.cpp, which owns the whole of MonsterState.pad0 for that
// monster type and subdivides it (the Slaver / Looter precedent). The same is
// true of the Jaw Worm's hardMode marker. Recorded here so a reader looking for
// this batch's flag spend finds the answer rather than an absence.
// S2.28 (Act-3 Beyond bosses). FOUR type-scoped bits, and they are a DELIBERATE
// REUSE of the Hexaghost's four (0x0800/0x1000/0x2000 = its orbActiveCount mask,
// 0x4000 = its burnUpgraded latch) rather than an append at bit 17. That reverses
// the call S2.21 made, and the reason it reverses is that the resource stopped
// being cheap:
//
//   * The policy above says a new type "should DELIBERATELY REUSE bits held by
//     types it can never co-occur with, rather than extend upward". S2.21 argued
//     the other way -- reuse conserves something that is not scarce, and a reused
//     bit makes a raw `flags` read ambiguous until the reader also checks
//     monster_id -- and with one batch in flight that argument held.
//   * S2 wave 3 is SIX CONCURRENT BATCHES, all granted "type-scoped flag bits
//     next 17" (docs/stage-b-tasks.md). Six batches appending from one shared
//     cursor is either a coordination problem or six collisions in bits 17-23,
//     of which there are seven. Reuse has no such failure mode: two batches that
//     independently reuse the same bit for monsters that cannot co-occur are
//     BOTH correct, and no integration merge can break them.
//   * The non-co-occurrence here is as strong as it gets, and it is structural
//     rather than a coincidence of pools: the Hexaghost is an ACT-1 BOSS and
//     these four are ACT-3 BOSSES, and a combat contains exactly one boss
//     encounter. (Mind Bloom's Act-1-boss re-fight, S2.33, is a separate combat
//     with its own monster records; it does not put a Hexaghost and an Awakened
//     One in one group.) Neither side can own the other's powers either, so the
//     power-owned-bit caveat above does not bite: every one of these four is read
//     ONLY by its own monster's module.
//
// So bits 17-23 are left free for the five sibling batches, and this block spends
// nothing new.
//
//   * AWAKENED_FORM1     -- AwakenedOne.form1 (:75,315). SET == phase 1, the
//                           form the boss starts in; cleared by the damage()
//                           override at the phase transition, never restored.
//                           getMove switches on it into two DISJOINT trees.
//   * AWAKENED_FIRST_TURN-- AwakenedOne.firstTurn (:76,183,248,314). Both trees
//                           read it and they treat it DIFFERENTLY: the phase-1
//                           arm clears it inside getMove (:248), the phase-2 arm
//                           does NOT (:262-265) and it is takeTurn case 5 that
//                           clears it (:183). The transition SETS it again (:314),
//                           which is what makes Dark Echo the opener of phase 2.
//   * TIME_EATER_USED_HASTE -- TimeEater.usedHaste (:73,179). A one-shot latch:
//                           Haste fires at most once per fight however far the
//                           boss is healed back down.
//   * SHAPE_ATTACKING    -- Donu.isAttacking (Donu.java:52,70,110,118) AND
//                           Deca.isAttacking (Deca.java:56,74,119,128) -- ONE bit
//                           for two types, which is the type-scoped region
//                           working as designed. The pair co-occurs in the SAME
//                           GROUP, and that is fine: the rule is that no single
//                           RECORD is ever two types at once, and each record
//                           reads only its own bit. Their INITIAL values differ
//                           (Deca true, Donu false), which is what puts them
//                           permanently out of phase.
inline constexpr uint32_t kMonsterFlagAwakenedForm1 = 0x0800u;
inline constexpr uint32_t kMonsterFlagAwakenedFirstTurn = 0x1000u;
inline constexpr uint32_t kMonsterFlagTimeEaterUsedHaste = 0x2000u;
inline constexpr uint32_t kMonsterFlagShapeAttacking = 0x4000u;

// S2.27 (Act-3 Beyond ELITES). ONE type-scoped bit, spent THREE TIMES over --
// 0x0800 again, the same reuse-not-append call S2.28 made one paragraph above
// and for the same reason (six concurrent wave-3 batches cannot share an
// append cursor without colliding, while two batches reusing one bit for
// monsters that cannot co-occur are both correct).
//
// The three carriers are Nemesis.firstMove (Nemesis.java:65,148-156),
// Reptomancer.firstMove (Reptomancer.java:61,169-172) and SnakeDagger.firstMove
// (SnakeDagger.java:43,92-95). They get ONE constant each rather than one shared
// name because their Java fields are three unrelated declarations; the VALUE is
// shared, and that is the type-scoped region working as designed.
//
// THE REPTOMANCER AND THE DAGGER DO CO-OCCUR -- they are in the same group, and
// a Reptomancer can summon three more daggers on top. That is fine and is the
// Donu/Deca adjudication verbatim: the rule is that no single RECORD is ever two
// types at once, and each record reads only its own type's bit. Sharing with the
// Hexaghost's orb mask is safe for the S2.28 reason plus one more: these three
// are ACT-3 ELITES and an elite room never contains a boss.
//
// SET == THE FIRST MOVE IS STILL PENDING, i.e. the bit mirrors the Java field's
// `true` initializer rather than inverting it, and every init writes it
// explicitly (kMonsterFlagAwakenedFirstTurn does the same). A zeroed record is
// therefore NOT a valid un-inited monster of these types, which is exactly the
// property that makes a missing init a visible bug rather than a plausible one.
inline constexpr uint32_t kMonsterFlagNemesisFirstMove = 0x0800u;
inline constexpr uint32_t kMonsterFlagReptomancerFirstMove = 0x0800u;
inline constexpr uint32_t kMonsterFlagSnakeDaggerFirstMove = 0x0800u;
//
// NO BIT AND NO NEW FIELD FOR THE OTHER THREE PIECES OF S2.27 STATE, recorded
// here so a reader looking for this batch's spend finds the answer:
//   * GiantHead.count (GiantHead.java:53) and Nemesis.scytheCooldown (:56) are
//     small per-instance INTEGERS, not latches, and each lives in that monster
//     type's MonsterState::pad0 (the Book of Stabbing / Darkling precedent).
//   * Reptomancer.daggers[4] (:60) needs no storage at all, for the Gremlin
//     Leader's reason: which POSX slot is free is a pure function of which
//     `draw_x` values currently carry a live record, and draw_x already stores
//     that key. See monster_reptomancer.hpp note (3).
// S2.24 (Act-2 City bosses). FOUR type-scoped bits, the SAME deliberate reuse
// of the Hexaghost's 0x0800-0x4000 that S2.28 argued above -- and a THREE-way
// share now, which the region's rule permits for the same structural reason:
// a combat contains exactly one boss encounter, so an Act-2 boss record can
// co-occur with neither the Act-1 Hexaghost nor S2.28's Act-3 bosses, and
// every one of these bits is read only by its own monster's module. (The two
// City-boss MINIONS spend nothing here: the Torch Head has no per-instance
// state at all, and the Bronze Orb's one latch is USED_STASIS below --
// disjoint from its boss's bits even though they share a group, because a bit
// is per-RECORD and each record reads only its own type's meaning.) The
// power-owned-bit caveat does not bite: none of these is a power's latch.
//
//   * CHAMP_THRESHOLD    -- Champ.thresholdReached (Champ.java:91,263-264): the
//                           below-half-HP one-shot that fires ANGER and turns
//                           the EXECUTE pattern on. Set at DECISION time inside
//                           getMove, never cleared -- healing back above half
//                           does not disarm it.
//   * BRONZE_ORB_USED_STASIS -- BronzeOrb.usedStasis (BronzeOrb.java:44,87-90):
//                           once-per-combat theft latch, set at DECISION time
//                           when getMove picks STASIS. Per-RECORD, so each of
//                           the two orbs steals once.
//   * COLLECTOR_INITIAL_SPAWN -- TheCollector.initialSpawn (TheCollector.java:
//                           76,133,182): SET == the opening SPAWN has not been
//                           TAKEN yet. Unlike the consumed-at-init firstTurn
//                           latches it is cleared in takeTurn case 1 (:133),
//                           not in getMove, so it needs storage (the Spheric
//                           secondMove reasoning).
//   * COLLECTOR_ULT_USED -- TheCollector.ultUsed (TheCollector.java:75,159):
//                           once-per-combat MEGA_DEBUFF latch, set in takeTurn
//                           case 4 (:159) -- takeTurn-time, not decision-time,
//                           which is why it too needs storage.
inline constexpr uint32_t kMonsterFlagChampThreshold = 0x0800u;
inline constexpr uint32_t kMonsterFlagBronzeOrbUsedStasis = 0x1000u;
inline constexpr uint32_t kMonsterFlagCollectorInitialSpawn = 0x2000u;
inline constexpr uint32_t kMonsterFlagCollectorUltUsed = 0x4000u;

// S3.43 (the Act-4 BOSS). ONE latch and TWO packed counters, all type-scoped,
// all read only by monster_corrupt_heart.cpp, and all a DELIBERATE REUSE of the
// 0x0800-0x10000 span the Hexaghost, the Act-2 City bosses, the Act-3 Beyond
// bosses, the S2.27 elites, the Byrd (0x8000) and the Spheric Guardian
// (0x10000) already hold. The reuse is legal for the region's own reason,
// argued rather than assumed: `The Heart` is a SOLO BOSS group
// (MonsterHelper.java:596-598, encounters.yaml 63) in Act 4, so a Corrupt Heart
// record can co-occur with no other monster AT ALL -- not another boss, not an
// elite, not a normal -- and every bit below is read only after `monster_id ==
// CORRUPT_HEART`. The power-owned-bit caveat does not bite either: none of
// these is a power's latch (Invincible's second number is PowerSlot.counter,
// Beat of Death has none).
//
// WHY flags AND NOT pad0, which is also free for this type: all three are
// consequences of OBSERVED events -- which move was telegraphed, how many moves
// have been decided, how many BUFF moves have resolved -- so they belong in the
// word PvMonster carries in full (public_view.hpp), not in the byte it
// deliberately omits because that byte can hold an unrevealed construction roll.
//
//   * FIRST_MOVE   -- CorruptHeart.isFirstMove (CorruptHeart.java:61,173-177).
//                     SET == the opening DEBILITATE is still pending, the
//                     Nemesis/Awakened convention (set explicitly by init, so a
//                     zeroed record is not a plausible un-inited Heart). Its arm
//                     RETURNS EARLY, which is why the opener does not advance
//                     MOVE_COUNT -- the one selection in the roster that skips
//                     its own increment.
//   * MOVE_COUNT   -- CorruptHeart.moveCount (:62,178,199), stored MOD 3 in two
//                     bits. Exact, not a compression: the field's ONLY reader is
//                     `this.moveCount % 3` (:178), and `++` then `% 3` commutes
//                     with `(x + 1) % 3`.
//   * BUFF_COUNT   -- CorruptHeart.buffCount (:63,128,149), the buff-ladder rung,
//                     three bits SATURATING AT 4. Exact for the same kind of
//                     reason: the readers are `== 0`, `== 1`, `== 2`, `== 3` and
//                     `default:` (:129-147), so every value >= 4 selects the same
//                     arm (StrengthPower(50), forever) and holding it at 4
//                     is indistinguishable from letting it run.
inline constexpr uint32_t kMonsterFlagCorruptHeartFirstMove = 0x0800u;
inline constexpr uint32_t kMonsterFlagCorruptHeartMoveCountShift = 12u;
inline constexpr uint32_t kMonsterFlagCorruptHeartMoveCountMask = 0x3000u;
inline constexpr uint32_t kMonsterFlagCorruptHeartBuffCountShift = 14u;
inline constexpr uint32_t kMonsterFlagCorruptHeartBuffCountMask = 0x1C000u;

// ESCAPED -- the FIRST global flag bit, bit 24, the bottom of the 24-31 global
// region: the monster left the fight ALIVE. (HALF_DEAD, bit 25, is the second;
// it is declared just below.)
// AbstractMonster.escape (AbstractMonster.java:915-919) sets `isEscaping`; the
// escape animation then latches `escaped` (updateEscapeAnimation, :894-906).
// This engine has no animation clock, so the two Java booleans collapse into
// this one bit, set by the ESCAPE opcode (EscapeAction.java:21-28 -> escape())
// at resolve time. Producers, in landing order: the Looter (Looter.java:126-133)
// and the Mugger (Mugger.java:124-133) in Act 1, and GremlinLeader.die()
// (GremlinLeader.java:237-240) in Act 2, which queues one EscapeAction per
// surviving record when the leader falls.
//
// THE GREMLINS' OWN MOVE 99 IS STILL UNREACHABLE, IN EVERY ACT -- amended by
// S2.23 from "unreachable in Act 1", which left the Act-2 answer open. Re-derived
// rather than assumed: `escapeNext()` has no caller anywhere in the tree, and the
// only `deathReact()` call is BanditBear.java:131, whose group (Bandits) contains
// no gremlin. So the leader's fan-out reaches escape() DIRECTLY, without ever
// setting move 99 and without telegraphing Intent.ESCAPE -- which is the
// difference that matters to BLOCK_RANDOM_MONSTER's intent-based filter (see
// monster_gremlin.hpp note (1)).
//
// An escaped monster keeps its positive hp -- it is NOT dying -- so every
// liveness read that means "in the fight" must test one of the two liveness
// predicates below rather than hp alone. Being global, this bit is read without checking
// monster_id, unlike every type-scoped bit above -- which is exactly why it
// lives in the global region and must never be reused.
//
// (History: as a uint16_t bit it was 0x8000 -- the last free bit of the old
// 16-bit field, sitting inside what is now the type-scoped region. The v5
// widening moved it to bit 24 so the region boundary is honest.)
inline constexpr uint32_t kMonsterFlagEscaped = 1u << 24;

// HALF_DEAD -- the SECOND global flag bit, bit 25: `AbstractMonster.halfDead`.
// A monster at 0 HP that the fight is NOT over with, and that STILL TAKES ITS
// TURN. Two producers, both Act-2/3:
//   * the Darkling (Darkling.java:200-243): `damage()` sets halfDead at hp <= 0
//     while the room is `cannotLose`, telegraphs move 4 (COUNT), and a later
//     REINCARNATE heals it back;
//   * the Awakened One (AwakenedOne.java:281-320): the same shape, driving the
//     phase-1 -> phase-2 REBIRTH.
//
// WHY GLOBAL rather than type-scoped, argued rather than defaulted (the policy
// above requires the argument): the readers are the pump's step-5 turn gate,
// the step-4 monster-queue population, applyPreTurnLogic and the combat-over
// test. Every one of them asks "is this record still in the fight" WITHOUT
// knowing which monster it is looking at -- exactly the test that put ESCAPED
// in the global region. A type-scoped bit would force each of those to check
// `monster_id == DARKLING || monster_id == AWAKENED_ONE` first, which is the
// ambiguity the two-region policy exists to prevent, and it would have to grow
// a term per future producer.
inline constexpr uint32_t kMonsterFlagHalfDead = 1u << 25;

// --- Liveness predicates ------------------------------------------------------
// The game's monster liveness is NOT `hp > 0`, and it is NOT ONE predicate --
// it is THREE, and they DISAGREE on halfDead. That disagreement is the whole
// mechanism behind the Darkling's REINCARNATE and the Awakened One's REBIRTH:
//
//   AbstractCreature.isDeadOrEscaped   (:780-790) = isDying || halfDead || isEscaping
//   MonsterGroup.areMonstersBasicallyDead (:90-95) = isDying ||             isEscaping
//   AbstractCard.cardPlayable            (:854-860) = isDying
//
// A halfDead monster is therefore OUT for targeting and IN for the fight: it
// cannot be hit or chosen at random, yet it is queued, takes its turn, loses
// block and runs start-of-turn powers -- which is how it ever reaches the turn
// that revives it. And it is IN for card PLAYABILITY too, which is the third
// sense: cardPlayable's first conjunct reads the bare `m.isDying` field, so a
// card aimed at a halfDead monster still passes canUse and still runs the whole
// onPlayCard fan-out before GameActionManager's own dead-target block
// (GameActionManager.java:263-282) throws the play away without useCard.
//
// This engine models isDying as `hp <= 0 && !halfDead` (die() and SuicideAction
// both zero HP; the Darkling / Awakened One halfDead branch zeroes HP WITHOUT
// setting isDying, Darkling.java:201-231), isEscaping/escaped as
// kMonsterFlagEscaped, and halfDead as kMonsterFlagHalfDead. halfDead IMPLIES
// hp == 0 (the Java only ever sets it on the hp <= 0 branch), which is the
// pleasant part: it makes monster_dead_or_escaped ALREADY EXACT for
// isDeadOrEscaped with no edit, so every targeting caller stayed correct when
// the split landed. The other two senses each need their own predicate.
[[nodiscard]] inline bool monster_escaped(const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagEscaped) != 0u;
}
[[nodiscard]] inline bool monster_half_dead(const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagHalfDead) != 0u;
}
// AbstractCreature.isDying, on its own. The CARD-PLAYABILITY sense
// (AbstractCard.cardPlayable:854-860): a halfDead monster is NOT dying, so it
// does not veto a play the way a genuinely dead one does. Use ONLY where the
// Java reads the bare field; anything that AIMS wants monster_dead_or_escaped.
[[nodiscard]] inline bool monster_is_dying(const MonsterState& m) noexcept {
    return m.hp <= 0 && !monster_half_dead(m);
}
// AbstractCreature.isDeadOrEscaped (:780-790). The TARGETING sense: a halfDead
// monster counts as DEAD. Use for anything that picks or aims at a monster --
// the target reticle, RANDOM_ENEMY, AoE fan-outs, apply-power guards.
[[nodiscard]] inline bool monster_dead_or_escaped(const MonsterState& m) noexcept {
    return m.hp <= 0 || monster_escaped(m);
}
// MonsterGroup.areMonstersBasicallyDead (:90-95). The IN-THE-FIGHT sense: a
// halfDead monster counts as ALIVE. Use for combat-over, the monster turn
// queue, applyPreTurnLogic, and the end-of-round power walks.
[[nodiscard]] inline bool monster_basically_dead(const MonsterState& m) noexcept {
    return monster_is_dying(m) || monster_escaped(m);
}

static_assert(std::is_trivially_copyable_v<MonsterState>);
// 20 = 8 (id/hp/max_hp/block) + 4 (flags u32) + 3 (move_history) + 1 (intent)
// + 1 (power_count) + 1 (pad0) + 2 (draw_x, schema v7 -- it took over the two
// bytes that were pad1, so this figure did NOT move). Was 16 + 4*kPowerCap before the
// flags widening (schema v5) and 20 + 4*kPowerCap before the PowerSlot counter
// widening (schema v6, sizeof(PowerSlot) 4 -> 8).
static_assert(sizeof(MonsterState) == 20 + 8 * kPowerCap,
              "MonsterState layout drifted -- update SCHEMA_VERSION");
static_assert(sizeof(PowerSlot) == 8,
              "the MonsterState size assert above spells the PowerSlot stride "
              "as a literal 8; keep the two in step");

// --- CombatState ------------------------------------------------------------

struct CombatState {
    // Schema stamp (design doc §8) -- compile-time, no per-instance storage.
    static constexpr uint32_t kSchemaVersion = SCHEMA_VERSION;

    // -- header (design doc §4.2 header group) --
    uint8_t phase;                    // CombatPhase
    uint8_t pad_header;               // explicit padding, value-init zeroed
    uint16_t turn;                    // turn counter (design doc §5.2 step 6)
    uint32_t flags;                   // reserved combat-wide bitfield

    // -- player (design doc §4.2 player group) --
    int16_t player_hp;
    int16_t player_max_hp;
    int16_t player_block;
    int16_t player_energy;            // small; int16 matches the hp/block widths
    uint8_t stance;                   // stance id (0 = None); skeleton is stanceless
    uint8_t cards_played_this_turn;   // per-turn counter (design doc §4.2)
    uint8_t player_power_count;       // live length of player_powers[]
    // AbstractPlayer.damagedThisCombat (AbstractPlayer.java:1466, incremented
    // right after updateCardsOnDamage): the count of positive in-combat
    // player HP-loss EVENTS witnessed so far, one per cards_took_player_
    // damage call (interp_damage.cpp), not per HP point. Occupies what was
    // `pad_player` -- zero bytes, zero offset moved, the pad_* precedent
    // (conventions.md 8). Its one consumer today is Blood for Blood's
    // makeCopy() override (BloodForBlood.java:60-67): `tmp.updateCost(
    // -AbstractDungeon.player.damagedThisCombat)` on every FRESHLY
    // instantiated copy (Discovery/Codex/Dead Branch -- any "pull a base
    // library copy from the combat pool" site), which is why a Blood for
    // Blood drawn from Discovery mid-fight starts already reduced by however
    // many hits already landed (S3.53 sweep witness s2v3_wave2_STS227212_ps88
    // / _STS228756_ps285). A single byte is generous: no A20 fight comes
    // close to 255 HP-loss events.
    uint8_t damaged_this_combat;
    PowerSlot player_powers[kPowerCap];

    // -- shared card-instance pool (design doc §4.2: "one pool, piles
    //    reference it"). Piles below store uint8_t indices into this array. --
    CardInstance card_pool[kCardPoolCap];

    // -- piles: index lists into card_pool + counts (design doc §4.2) --
    CardPoolIndex hand[kHandCap];
    CardPoolIndex draw[kDrawCap];
    CardPoolIndex discard[kDiscardCap];
    CardPoolIndex exhaust[kExhaustCap];
    CardPoolIndex limbo[kLimboCap];
    uint8_t hand_count;
    uint8_t draw_count;
    uint8_t discard_count;
    uint8_t exhaust_count;
    uint8_t limbo_count;
    uint8_t monster_count;            // live length of monsters[]
    // Gold EARNED INSIDE this combat. The run layer banks it into RunState.gold
    // at each step boundary (run_advance.cpp sync_live_gold, S2.48 -- a raw +=,
    // the relic reads paid at the kill) and the combat fold-back zeroes it with
    // the bank marker. The game has no such field: AbstractPlayer.gainGold writes
    // the run purse the instant GreedAction sees a kill (GreedAction.java:38), and
    // CombatState deliberately keeps no duplicate of RunState.gold, so an in-combat
    // producer needs somewhere to accrue. uint16 is ample -- Hand of Greed pays
    // 20/25 per kill and a combat has at most 7 monster records.
    //
    // A COMBAT-ONLY replay (no run layer) simply carries the accumulator: nothing
    // in the combat layer reads it back, so no combat behaviour depends on it.
    //
    // It occupies what was `pad_piles[2]`, so it costs ZERO bytes and moves no
    // offset -- the turn_has_ended precedent below (a real field living in what
    // would otherwise be ring padding).
    uint16_t combat_gold;

    // The 2 bytes MonsterState's 4-byte alignment inserts before monsters[].
    //
    // DECLARED, not implicit, and the difference is not cosmetic: CombatState is
    // byte-compared and byte-hashed (hash_state, the diff harness, the twenty
    // committed fixtures), and conventions §8 records the incident where exactly
    // this shape -- an undeclared gap in a memcmp'd struct, with a comment
    // asserting value-initialization zeroes it -- produced a one-byte difference
    // between two translations of one capture on Windows only. Neither of the two
    // ways a state is normally produced writes a byte belonging to no member:
    // aggregate-initialisation initialises MEMBERS, and the implicit copy
    // constructor copies MEMBERWISE. Making it a member is what makes it written.
    // Adding it changes no offset and no size, so no fixture and no
    // SCHEMA_VERSION moves (the same terms as the RunState pad_* elimination).
    // Found by the T0.5 classification tripwire (byte_class.hpp), which is also
    // the executable member walk that keeps it declared.
    uint8_t pad_monsters[2];

    // -- monsters (design doc §4.2) --
    MonsterState monsters[kMonsterCap];

    // -- action queue: fixed ring + bookkeeping (design doc §5.1/§4.2). Storage
    //    only; the queue mechanics live in action_queue.hpp. head/tail/count are
    //    the ring cursors the pump maintains. --
    ActionQueueItem action_queue[kActionQueueCap];
    uint8_t action_head;
    uint8_t action_tail;
    uint8_t action_count;
    uint8_t pad_actionq;              // explicit padding

    // -- pre-turn action queue (design doc §5.1: `preTurnActions`).
    //    `addToTurnStart` prepends here (GameActionManager.java:145); the pump
    //    drains it right after the main action queue (§5.2 step 2). Same ring
    //    shape/cursors as `action_queue`. --
    ActionQueueItem pre_turn_actions[kPreTurnActionQueueCap];
    uint8_t pre_turn_head;
    uint8_t pre_turn_tail;
    uint8_t pre_turn_count;
    // Set by the end-turn sentinel (design doc §5.2 step 3 / §5.4), cleared by
    // the start-of-turn sequence (§5.2 step 6); gates that step-6 branch. The
    // game's `GameActionManager.turnHasEnded`.
    uint8_t turn_has_ended;           // 0/1; fills what would be ring padding

    // -- card queue (design doc §5.1: pending card plays, cap 16) --
    CardQueueItem card_queue[kCardQueueCap];
    uint8_t card_queue_count;
    uint8_t pad_cardq;                // explicit padding

    // -- monster queue (design doc §5.1: monsters awaiting turn). Cap stays 5
    //    even though kMonsterCap grew to 7: queueMonsters only enqueues
    //    LIVE monsters (MonsterGroup.queueMonsters skips dead/escaped,
    //    MonsterGroup.java:117-122), and the max simultaneously-alive S1 group is
    //    5 (Lots of Slimes). Splits raise the RECORD count past 5 but not the live
    //    count (a splitting parent dies as its children spawn), so 5 is the true
    //    turn-queue bound; the 2 extra monster RECORD slots are for dead-in-place
    //    retention, not extra queued turns. --
    MonsterQueueItem monster_queue[kMonsterQueueCap];
    uint8_t monster_queue_count;
    uint8_t monster_attacks_queued;   // design doc §5.2 step 4 flag (0/1)

    // -- combat relic mirror: an addition beyond design §4.3's literal RunState
    //    list. The player's relics in
    //    acquisition order (== trigger order, trap 8), mirrored from RunState.relics
    //    at combat_begin so in-combat relic hooks (relic_hooks.hpp player_relics)
    //    read a live list rather than an empty view. This field is only the
    //    STORAGE the dispatch reads; the run-level fold-back that populates and
    //    refreshes it across combats lives in run_advance.cpp
    //    (enter_combat). Capacity == kRelicCap (== RunState.relics) so the fold is
    //    a plain array copy; kRelicCap = 40 covers S1 A20 runs (which can
    //    accumulate ~30+ relics) with margin. Value-init leaves it empty, so the
    //    20 combat fixtures carry a zeroed mirror (dispatch stays a no-op there). --
    RelicSlot relics[kRelicCap];
    uint8_t relic_count;
    // -- the in-combat MASTER-DECK obtain accumulator (schema v7) --
    //
    // Cards a monster made the player OBTAIN mid-combat, held here until the
    // run layer drains them. The Writhing Mass's MEGA_DEBUFF
    // (WrithingMass.java:118, `AddCardToDeckAction(CardLibrary.getCard(
    // "Parasite").makeCopy())`) is the first and only in-combat master-deck
    // writer in Acts 1-3 -- every other obtain path is a run-layer event or a
    // relic pickup.
    //
    // WHY AN ACCUMULATOR and not a direct write: the combat layer cannot reach
    // RunState. `execute_opcode` and the whole interp/action_queue layer take
    // `CombatState&` only, and `advance.hpp`'s standalone entry point takes no
    // RunState at all -- that boundary is deliberate (see combat_gold above,
    // which solves the identical problem for Hand of Greed's in-combat gold).
    // The OBTAIN_CARD opcode writes here; run_advance drains it through the
    // single `add_card_to_master_deck` door, which is what makes the OMAMORI
    // gate apply for free rather than being re-implemented.
    //
    // WHY DRAINED PER PUMP STEP rather than at the combat fold-back: Omamori's
    // counter decrement happens the instant the action resolves in the Java
    // (ShowCardAndObtainEffect's constructor, :30-45). Deferring the drain to
    // fold_back_combat would slide that decrement past the relic-counter copy
    // that the fold performs, which would silently CLOBBER it. Draining each
    // step keeps the ordering honest and the timing effectively immediate.
    //
    // A COMBAT-ONLY replay (no run layer) simply accrues and never drains:
    // nothing in the combat layer reads it back, so no combat behaviour depends
    // on it -- the same property combat_gold has.
    //
    // Capacity 3 is generous: the only producer is once-per-combat (the Mass's
    // `usedMegaDebuff` latch), the drain runs every pump step so the slots are
    // reclaimed almost immediately, and the count SATURATES rather than
    // overflowing (dropping a fourth same-step obtain is strictly better than
    // corrupting the struct, and it is unreachable).
    //
    // The two fields exactly consume what was `pad_relics[7]`: 1 byte of count
    // + 3 * 2 bytes of ids == 7. So they cost ZERO bytes, move NO offset, and
    // `monster_hp_rng` stays exactly where it was -- the combat_gold-into-
    // pad_piles precedent. `pending_obtain` lands 2-aligned because
    // `relic_count` sits at an even offset (RelicSlot is 4-aligned).
    uint8_t pending_obtain_count;                // live length of pending_obtain[]
    uint16_t pending_obtain[kPendingObtainCap];  // CardId per pending obtain
    // The in-combat BELT obtain accumulator -- pending_obtain's sibling for
    // potions. The OBTAIN_POTION opcode (ObtainPotionAction.update's first
    // tick, ObtainPotionAction.java:29-38) appends the rolled PotionId here and
    // the run layer drains it onto RunState.potions at every combat-phase
    // command boundary (run_advance.cpp drain_pending_potions), applying the
    // action's own Sozu gate and AbstractPlayer.obtainPotion's first-empty-slot
    // placement there. Its producer is Entropic Brew's in-combat branch, whose
    // obtains are QUEUED in the game (EntropicBrew.java:41 addToBot) and so
    // wait behind an open discovery screen -- the very ordering a synchronous
    // belt write cannot express (capture STS224800, floor 25). PotionId fits a
    // byte (kPotionsCount is asserted <= 255 beside the drain), and the count
    // SATURATES rather than overflowing, the pending_obtain contract.
    //
    // The two fields exactly consume what was `pad_rng_align[6]`: the 6 bytes
    // RngStream's 8-byte alignment inserts before the stream block (`pad_relics`
    // above rounds the relic mirror to 8 bytes but does NOT reach the stream
    // block). Zero bytes, no offset moves, monster_hp_rng stays where it was --
    // the combat_gold-into-pad_piles / pending_obtain-into-pad_relics precedent,
    // and byte_class.hpp's row moves from PADDING to PUBLIC.
    uint8_t pending_potion_count;               // live length of pending_potion[]
    uint8_t pending_potion[kPendingPotionCap];  // PotionId per pending obtain

    // -- RNG: the 5 floor-scoped streams (design doc §3.4 / §3.6). Named
    //    exactly as the game's streams so combat_begin() can derive each via
    //    floor_stream(seed, floor) with an obvious 1:1 mapping. RngStream is
    //    8-byte aligned, so CombatState is 8-byte aligned; the bytes that
    //    alignment costs are declared above (the pending_potion accumulator,
    //    formerly pad_rng_align) rather than left to the compiler. --
    RngStream monster_hp_rng;         // monster max-HP rolls
    RngStream ai_rng;                 // monster move selection
    RngStream shuffle_rng;            // deck shuffles (feeds the JDK LCG)
    RngStream card_random_rng;        // in-combat card randomness (random targets)
    RngStream misc_rng;               // everything else
};

static_assert(std::is_trivially_copyable_v<CombatState>,
              "CombatState must be trivially copyable (design doc §4.1: "
              "snapshot = memcpy)");
// Size budget (design doc §4.2, raised 4096 -> 8192 by the project owner on
// 2026-07-24; recorded in stage-b-design.md §11).
//
// Actual sizeof(CombatState) at the time of the change: 3896 B. Under the old
// 4096 ceiling that left ~200 B of headroom, while the most recent capacity
// bump alone (kMonsterCap 5 -> 7) cost 224 B. The remaining relic tiers and the
// unbuilt run-layer content are still to land, so the next routine capacity bump
// would have tripped this assert mid-task, with no authority to move a frozen
// budget.
//
// 8192 == RunState's existing ceiling (run_state.hpp:213), so the two budgets
// are now at parity. Raising the ceiling is deliberately the cheap option: it
// changes no field, no offset, and no serialized layout, so SCHEMA_VERSION is
// NOT bumped and no golden fixture is regenerated. The alternative (trimming
// the POD layout to fit) would have been a schema + fixture change -- exactly
// the stop-the-line class of change this avoids.
//
// This is a CEILING, not a target. sizeof(CombatState) was 3896 B when the
// ceiling was raised; the schema-v5 flags widening (MonsterState 112 -> 116 B,
// plus alignment padding) grew it to 3928 B, and the schema-v6 PowerSlot counter
// widening (4 -> 8 B per slot, over 24 player slots + 7 x 24 monster slots =
// 192 rows, MonsterState 116 -> 212 B) grew it to 4696 B -- +768, still inside
// the budget with ~3.4 KB to spare. The combat-gold accumulator added in the
// same bump costs nothing: it reuses the former pad_piles[2].
//
// SCHEMA V7 SPENT NEARLY ALL OF THE REMAINING HEADROOM. kMonsterCap 7 -> 23
// grew it 4696 -> 8088, leaving 104 B. The other two v7 additions cost nothing
// by construction (MonsterState.draw_x reuses pad1[2]; the pending-obtain
// accumulator reuses pad_relics[7]). See the kMonsterCap comment at the top of
// this file for the measured cap/size table and for why 23 rather than the
// granted 24 -- and note what the 104 B means in practice: ONE more monster
// slot costs 212 B, so the cap cannot rise again without either moving the
// ceiling (an owner decision) or splitting kPowerCap into a smaller
// per-monster power cap.
static_assert(sizeof(CombatState) <= 8192,
              "CombatState exceeds its 8 KB budget (design doc §4.2)");
// Pinned deliberately, the PublicView precedent: this is a byte-hashed,
// byte-compared, fixture-serialized struct, so a size change is a schema event
// and must be reviewed as one rather than noticed later through a fixture
// mismatch. 4696 through v6; v7 is kMonsterCap 7 -> 23.
static_assert(sizeof(CombatState) == 8088,
              "sizeof(CombatState) moved -- this is a SCHEMA CHANGE: bump "
              "SCHEMA_VERSION, add a schema.hpp version-log entry, regenerate "
              "the 20 combat fixtures via tools/fixture_gen, and re-check "
              "byte_class.hpp's tiling table");

}  // namespace sts::engine
