#pragma once

// The Gremlin Leader (GremlinLeader.java, read in full; registry/monsters.yaml
// id 37). The Act-2 elite that SUMMONS -- the first mid-combat spawner in the
// roster that is not a split, and the reason S2.2F built `draw_x`,
// `smart_position_for`, the spawn path's pre-battle bit and the post-super death
// slot.
//
// The registry row carries the HP columns, the three moves' intents and their
// effect programs; move SELECTION, the summon machinery and the ENCOURAGE
// fan-out are native, per design doc B section 4.2.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), all read in full:
//   GremlinLeader.java:36-242; SummonGremlinAction.java (whole file);
//   MonsterHelper.java:507-509 (the encounter), :767-778 (spawnGremlin);
//   MinionPower.java:14-34; ApplyPowerAction.java:80-82,96-99;
//   AbstractMonster.java:431-491,705-715,765-775,915-919,925-951.
//
// SEVEN THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) getMove RECURSES, AND EACH RECURSION TAKES A FRESH SEEDED DRAW.
//     Two of the three arms end in `this.getMove(AbstractDungeon.aiRng.random(
//     50, 99))` (:163) and `this.getMove(AbstractDungeon.aiRng.random(0, 80))`
//     (:174). A transcription that reuses the original `num` diverges on the
//     first recursion, and one that unrolls a fixed number of levels diverges on
//     the second: termination here is PROBABILISTIC. The body models it as a
//     loop that re-draws until an arm terminates, which is exactly the Java's
//     control flow with the stack flattened.
//
//     It really can loop more than once, and only in one place: the `num >= 80,
//     lastMove(STAB)` arm re-draws `random(0, 80)`, and a re-drawn 80 lands in
//     the SAME arm. Every other path terminates on the first recursion. Modelled,
//     not bounded.
//
// (2) numAliveGremlins() IS NOT "the gremlins[] array". It walks the WHOLE group
//     and counts every record that is `!= null && != this && !isDying`
//     (:191-198) -- so it includes summoned gremlins, includes the two the
//     encounter built, does NOT exclude escaped monsters, and is NOT restricted
//     to the three summon slots. The engine predicate is therefore
//     `i != mi && hp > 0`, which is `record_alive` minus self.
//
// (3) THE SUMMON'S POOL PICK AND HP ROLL HAPPEN AT QUEUE TIME, ITS MOVE ROLL AT
//     RESOLVE TIME, AND THE LEADER'S OWN ROLL LANDS THIRD. This is the batch's
//     most fragile ordering. SummonGremlinAction's CONSTRUCTOR (:33-46) --
//     evaluated at `addToBottom`, not at update() -- does four things: pick the
//     gremlins[] slot, draw `aiRng.random(0, 7)` over an 8-entry key pool
//     (:59-89), build the gremlin (whose ctor draws monster_hp_rng), and run the
//     player's relics' onSpawnMonster. Only then does update() (:101-124) run the
//     child's init() -- its ONE ai_rng rollMove -- and insert the record.
//
//     So a RALLY turn's stream reads, in order:
//         aiRng pool pick #1, monsterHpRng HP #1,      (queue time)
//         aiRng pool pick #2, monsterHpRng HP #2,      (queue time)
//         aiRng child #1 rollMove,                     (resolve, spawn 1)
//         aiRng child #2 rollMove,                     (resolve, spawn 2)
//         aiRng LEADER rollMove                        (resolve, trailing item)
//     and the leader's own roll is THIRD on ai_rng because both spawns were
//     queued ahead of the trailing RollMoveAction, while the Minion/Angry items
//     each spawn appends land BEHIND it.
//
// (4) THE SUMMON POOL IS ai_rng; THE ENCOUNTER POOL IS misc_rng. The eight
//     entries are identical -- {Warrior, Warrior, Thief, Thief, Fat, Fat,
//     Tsundere, Wizard} -- but `MonsterHelper.spawnGremlin` (:767-778, the two
//     minions the encounter builds) draws miscRng and `getRandomGremlin` (:89)
//     draws aiRng. encounters.yaml id 35 already owns the miscRng one, correctly;
//     the two must NOT be unified.
//
// (5) `gremlins[3]` NEEDS NO STORAGE -- IT IS DERIVABLE FROM `draw_x`, and that
//     is the design decision this module makes, argued rather than defaulted.
//     `identifySlot` (:48-54) returns the first index whose entry is `null ||
//     isDying`, and the ONLY thing the answer is used for is which POSX the new
//     gremlin gets. Since GremlinLeader.POSX = {-366, -170, -532} are three
//     DISTINCT values, and every gremlin in this fight sits at exactly one of
//     them (the encounter builds its two at POSX[0] and POSX[1],
//     MonsterHelper.java:507-509; every summon takes its slot's POSX), "slot k is
//     occupied" is exactly "some LIVE record has draw_x == POSX[k]".
//
//     The alternative -- storing three record indices in the leader -- was
//     rejected for a concrete reason, not a stylistic one: record indices SHIFT
//     on every insertion (spawn_monster_at_slot), so a stored map would need
//     remapping at exactly the moment the summon machinery is mid-flight, and
//     nothing else in CombatState carries a remappable index. It would also cost
//     15 bits (three 5-bit indices at kMonsterCap 23) against a 7-bit flag
//     surplus and one pad0 byte. Deriving costs nothing and cannot go stale.
//
//     The derivation is EXACT and not merely close, because a slot is only ever
//     reassigned when its occupant is null-or-dying and a dying record never
//     revives: at most one LIVE record can carry any given POSX at a time, so
//     "the newest occupant of slot k is dying" and "no live record sits at
//     POSX[k]" are the same statement.
//
// (6) usePreBattleAction APPLIES MINION TO A NULL. `gremlins[2] = null` and the
//     loop then does `ApplyPowerAction(m, m, new MinionPower(this))` for all
//     three (:98-100). ApplyPowerAction.update opens with `if (this.target ==
//     null || this.target.isDeadOrEscaped()) { isDone = true; return; }`
//     (ApplyPowerAction.java:96-99) -- a documented NO-OP, not a crash -- so
//     exactly TWO applications land. The engine queues two items and says so
//     here rather than leaving a silent 2 where the Java has a 3.
//
// (7) die() IS A POST-`super.die()` BODY. `super.die()` runs FIRST (:226) and is
//     what sets the leader's own isDying; only then does the EscapeAction
//     fan-out (:237-240) walk the group skipping `isDying`, which is how the
//     leader excludes ITSELF. Put on the pre-super side it would try to escape
//     its own corpse. So it registers in monster_die_after_fn, not
//     monster_die_fn, and the two ShoutAction loops (:228-236) are presentation
//     that draws nothing.
//
//     The escapees never re-telegraph Intent.ESCAPE -- see monster_gremlin.hpp
//     note (1) for why that matters to BLOCK_RANDOM_MONSTER.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// GremlinLeader.POSX (GremlinLeader.java:49), the three summon-slot positions,
// indexed by the gremlins[] slot identifySlot returns. Stored as the integer
// `offsetX` the ctor receives, which is exactly MonsterState::draw_x's units:
// drawX = WIDTH*0.75 + offsetX*xScale is monotone in offsetX (xScale > 0), so
// comparing these gives the same order the Java's float comparison gives.
//
// NOTE THE ORDER: slot 2 is the LEFTMOST (-532), so a summon into the third slot
// always lands at list position 0 while slot 1 (-170) lands furthest right of the
// three. The table is deliberately not sorted -- it is indexed by slot.
inline constexpr int16_t kGremlinLeaderSlotX[3] = {-366, -170, -532};

// The leader's own `offsetX` (GremlinLeader.java:66, the ctor's 9th argument).
// Larger than all three slot positions, so every summon inserts BEFORE it and
// the leader's own list index grows by one per summon -- which is why the
// trailing RollMoveAction's target is pre-computed (monster_dispatch.hpp: pending
// action_queue items are NOT remapped across a spawn).
inline constexpr int16_t kGremlinLeaderDrawX = 35;

void gremlin_leader_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (:93-101): the Minion markers on the two initial gremlins.
// It is also where this module writes the two initial records' `draw_x`, for the
// reason spelled out in the body -- the positions are an ENCOUNTER property
// (MonsterHelper.java:507-509 passes POSX[0] and POSX[1] to spawnGremlin) that
// the shared gremlin init cannot know, and this is the one place in the engine
// that the Java itself licenses to hard-code group indices 0 and 1.
void gremlin_leader_use_pre_battle_action(CombatState& state,
                                          uint8_t monster_index) noexcept;

void gremlin_leader_take_turn(CombatState& state,
                              uint8_t monster_index) noexcept;

// The trailing RollMoveAction (:133) sits OUTSIDE the switch, so every move body
// reaches it; getMove reads the rolled num on every arm. Queued, not inline --
// on a RALLY turn it must resolve AFTER both spawns' init rolls (note (3)).
void gremlin_leader_roll_move(CombatState& state,
                              uint8_t monster_index) noexcept;

// getMove (:144-189) at an arbitrary ascension, exposed so the tier-2 tests can
// exercise arms the fixed A20 never selects on its own. `num` is the
// aiRng.random(99) the caller already drew; the recursive arms draw their own.
void gremlin_leader_decide_move(CombatState& state, uint8_t monster_index,
                                int32_t num) noexcept;

// die() AFTER super.die() (:224-241): EscapeAction over every non-dying record.
// Registered in monster_die_after_fn -- see note (7).
void gremlin_leader_die_after(CombatState& state,
                              uint8_t monster_index) noexcept;

// numAliveGremlins (:191-198), exposed for the tests that drive getMove's three
// arms directly. Counts every record but `monster_index` that is not dying.
[[nodiscard]] int32_t gremlin_leader_num_alive_gremlins(
    const CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
