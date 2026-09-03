#pragma once

// Single source of truth for the state-struct / trajectory schema version
// (design doc §8). This constant "must be bumped by any struct edit" -- any
// change to CombatState's or RunState's field layout (a new field, a widened
// type, a reordered group, a changed capacity) is a schema change and this
// number goes up by one.
//
// The trajectory writer stamps this value into each trace file's header
// (`{magic 'STS0', schema_version u32, state_size u32, record[]}`, design doc
// §8); loaders refuse mismatched versions. Both CombatState and RunState expose
// it as a `static constexpr uint32_t kSchemaVersion = SCHEMA_VERSION;` member so
// the value lives in exactly one place (this header) yet is reachable through
// either struct.
//
// It is deliberately NOT a per-instance data field: a stored field would be
// zeroed by the value-initialization every state undergoes (design doc §4.1),
// making it useless as a stamp, and it would waste bytes in every snapshot. The
// stamp belongs to the trajectory container, not to each state.

#include <cstdint>

namespace sts::engine {

// SCHEMA VERSION LOG. Each entry records WHICH change forced the bump, because
// that is the only thing that answers "will this old trace still load?".
//
// TASK-ID-ALLOWED-BEGIN: schema-version provenance. Each bump below names the
// task that made it. This is the one place a bare task id earns its keep -- a
// refused trace is diagnosed by finding the decision that moved the layout, and
// the task ledger (docs/stage-b-tasks.md) is keyed by these ids. Everywhere
// else in src/ and include/, task ids are removed; see tests/no_task_ids_test.
// v1 (=1): the Stage-A trajectory container -- a CombatState-only record stream
//   (`{magic 'STS0', schema_version u32, state_size u32, record[]}`).
// v2 (=2): B1.6 adds a per-record `state_kind` discriminator so one container
//   can hold both CombatState and RunState records (design §3.3 run-level
//   traces). The header now advertises BOTH sizeof(CombatState) and
//   sizeof(RunState) for the loader's refusal check. The 20 frozen v1 combat
//   fixtures still load via a compatibility read in the v2 loader (they are
//   NOT regenerated). The struct layouts themselves are unchanged
//   (sizeof(CombatState)/sizeof(RunState) identical); the bump reflects the
//   container-format change, per §8's "bumped by any struct/format edit" and
//   design §3.3's "schema-version bump". See tools/diff_harness/sts/diff/trace.hpp.
// v3 (=3): B4.3 additively extends RunState (design §2.6): the NeowEvent rng
//   (14th stream), the three event-pity floats, the shop purge cost, the
//   potion-slot count, the event/shrine/special pool-membership bitsets, and the
//   five relic-pool orders; plus the schema-v2 map reorientation (kMapRows/
//   kMapCols renamed to game-native 15 floors x 7 cols -- a rename only, the 105
//   MapNode layout is byte-identical). sizeof(RunState) grows, so per §8 this is
//   a schema bump. The trace v2 container FORMAT is unchanged (still the
//   state_kind discriminator + both struct sizes in the header); the bump is
//   carried by the header's stamped version AND the run_state_size refusal check,
//   so an old-sized RunState trace is refused. The 20 frozen v1 combat fixtures
//   are untouched (kTraceFormatV1, zero regeneration); no run-level (RUN/v2)
//   trace goldens are committed, so nothing is regenerated. The on-disk v2 format
//   tag tracks this constant (kTraceFormatV2 == SCHEMA_VERSION).
// v4 (=4): B3.12 (multi-monster combat + encounter framework) grows kMonsterCap
//   5 -> 7 so CombatState can hold the dead-in-place records a fully-split Slime
//   Boss leaves behind (design §4.4 / scoping report §1.5). sizeof(CombatState)
//   grows by two MonsterState slots (224 B: 3672 -> 3896), so per §8 this is a
//   schema bump. The 20 combat fixtures are REGENERATED via the checked-in
//   generator (tools/fixture_gen/gen_combat_fixtures.cpp) -- they carry the new
//   state_size but are proven byte-equivalent modulo a single zero-run inserted
//   at the old monsters[]-array end (the zero-diff-in-meaning precedent;
//   scratchpad/b312_fixture_proof.py). The 20 fixtures stamp the DECOUPLED
//   on-disk format tag kTraceFormatV1 (=1), NOT this constant, so their header
//   schema_version is unchanged; only their state_size field moves. The trace v2
//   container format is unchanged; kTraceFormatV2 follows this constant to 4.
// TASK-ID-ALLOWED-END
//
// v5 (=5): the MonsterState.flags widening (owner-approved 2026-07-26, branch
//   monsterflags-widen -- not a ledger task, so no task id here). `flags` grows
//   uint16_t -> uint32_t under the two-region allocation policy (combat_state.hpp:
//   bits 0-23 type-scoped and reusable across monster types that cannot co-occur,
//   bits 24-31 global), and kMonsterFlagEscaped moves from bit 15 (the last free
//   bit of the old word, inside what is now the type-scoped region) to bit 24
//   (bottom of the global region). No stored VALUE changes meaning: every
//   type-scoped bit keeps its old value, and no committed trace carries a set
//   Escaped bit. sizeof(MonsterState) 112 -> 116, sizeof(CombatState)
//   3896 -> 3928, so per §8 this is a schema bump. The 20 combat fixtures are
//   REGENERATED via the checked-in generator (tools/fixture_gen/
//   gen_combat_fixtures.cpp) under the established byte-level
//   zero-diff-in-meaning discipline for layout bumps: every difference is zero
//   padding at compiler-derived offsets, every pre-existing byte preserved in
//   order. The fixtures stamp the DECOUPLED on-disk tag kTraceFormatV1 (=1) as
//   before; only their state_size field moves. kTraceFormatV2 follows this
//   constant to 5.
// v6 (=6): the PowerSlot second number + instanced powers + the combat-gold
//   accumulator (owner-approved 2026-07-27; the allocation is recorded in the
//   ledger's Wave-B block). THREE changes ride one bump because they are one
//   struct edit:
//     (a) `PowerSlot` grows {u16 power_id, i16 amount} -> {u16 power_id,
//         i16 amount, i16 counter, i16 pad0}: 4 -> 8 bytes (types.hpp). `counter`
//         is the SECOND per-instance number real powers carry beside `amount`
//         (PanachePower.damage, PanachePower.java:28; TheBombPower.damage,
//         TheBombPower.java:26), and it is observable -- the oracle emits it by
//         field-name reflection (GameStateConverter:895-903). Every PRE-EXISTING
//         power keeps counter == 0 with unchanged semantics; `amount` stays the
//         oracle-visible stack number everywhere. Combust's private hpLoss is
//         DELIBERATELY not migrated: its CombatState.flags bits are load-bearing
//         on committed behaviour.
//     (b) a per-power-def `instanced` marker (registry/powers.yaml -> PowerDef):
//         op_apply_power APPENDS a fresh slot instead of merging by power_id.
//         The Bomb is the only user (TheBombPower's ID is "TheBomb" + a static
//         counter, :31-32, so two bombs NEVER merge in the game). No layout
//         change of its own -- the generated PowerDef is not serialized.
//     (c) `CombatState.combat_gold` (uint16), the in-combat gold accrual Hand of
//         Greed needs, settled through the run layer's single gain_gold door at
//         the combat fold-back. It reuses the former `pad_piles[2]`, so it moves
//         NO offset and costs NO bytes.
//   sizeof(PowerSlot) 4 -> 8, sizeof(MonsterState) 116 -> 212,
//   sizeof(CombatState) 3928 -> 4696, so per §8 this is a schema bump. The 20
//   combat fixtures are REGENERATED via the checked-in generator
//   (tools/fixture_gen/gen_combat_fixtures.cpp) under the established byte-level
//   zero-diff-in-meaning discipline: the old bytes are transformed by a
//   MECHANICAL expansion (4 zero bytes appended to each of the 192 PowerSlot
//   rows, at compiler-derived offsets) and the result is byte-identical to the
//   regenerated files -- every pre-existing byte preserved in order, every new
//   byte zero. The fixtures stamp the DECOUPLED on-disk tag kTraceFormatV1 (=1)
//   as before; only their state_size field moves. kTraceFormatV2 follows this
//   constant to 6.
// v7 (=7): the Act-2/3 shared monster framework (owner-approved 2026-08-07; the
//   allocation is recorded in the ledger's "S2 Wave-3 allocations" block).
//   THREE changes ride one bump because they are one struct edit, and only the
//   first of them costs a byte:
//     (a) `kMonsterCap` 7 -> 23 (combat_state.hpp). Three Act-2/3 mid-combat
//         spawners -- Gremlin Leader, The Collector, Reptomancer -- each grow
//         their RECORD count without bound as a fight lasts, because the game
//         never removes a dead record. 23 is the largest value the 8192 B
//         CombatState ceiling admits (measured: 24 gives 8304, over by 112).
//         The GRANTED value was 24; the grant was estimated against pre-v6
//         struct sizes. The measured cap/size table and the two rejected
//         alternatives are in the kMonsterCap comment in combat_state.hpp.
//     (b) `MonsterState.draw_x` (int16), the horizontal POSITION KEY that turns
//         SpawnMonsterAction's smart positioning into a shared helper instead
//         of per-spawner hand-derived slot arithmetic. It reuses the former
//         `pad1[2]`, so it moves NO offset and costs NO bytes.
//     (c) `CombatState.pending_obtain[3]` + `pending_obtain_count`, the
//         in-combat master-deck obtain accumulator the OBTAIN_CARD opcode
//         writes and run_advance drains each pump step -- the combat_gold
//         precedent, for the same layer-boundary reason (combat cannot reach
//         RunState). It reuses the former `pad_relics[7]`: also zero bytes.
//   `kMonsterFlagHalfDead` (global flag bit 25) rides along and costs nothing:
//   `flags` is already uint32_t with bits 25-31 free, so it is a stored-VALUE
//   addition only, and no committed trace carries it set.
//   sizeof(MonsterState) is UNCHANGED at 212; sizeof(CombatState) 4696 -> 8088,
//   so per §8 this is a schema bump. The 20 combat fixtures are REGENERATED via
//   the checked-in generator (tools/fixture_gen/gen_combat_fixtures.cpp) under
//   the established byte-level zero-diff-in-meaning discipline: this change is a
//   pure ARRAY EXTENSION, so every pre-existing byte is preserved in order and
//   the only difference is a zero run appended at the old monsters[]-array end
//   -- the same mechanical shape as the v4 kMonsterCap 5 -> 7 bump. The fixtures
//   stamp the DECOUPLED on-disk tag kTraceFormatV1 (=1) as before, so their
//   header schema_version is unchanged and the v1 compatibility read is
//   RETAINED; only their state_size field moves. kTraceFormatV2 follows this
//   constant to 7.
//   NOTE this bump is NOT confined to CombatState, unlike every earlier one.
//   PublicView embeds `PvMonster monsters[kMonsterCap]` plus two kMonsterCap-
//   sized roll arrays, and RunActionMask embeds per-(card, monster) and
//   per-(potion, monster) target grids, so the observation record moves too:
//   PUBLIC_VIEW_VERSION 3 -> 4, twin fixtures regenerated. That is a MID-RECORD
//   move, not a tail append, so v3 records are NOT readable as v4 -- see the
//   schema-evolution note in docs/public-view-audit.md.
// v8 (=8): the boss-chest boss-relic offer storage (S2.47 -- the ledger row is
//   the conventions-§5 planned site for this bump). `BossChestState` (16 bytes:
//   relics[3] u16, screen/seen/chose_relic u8, pad[7]) moves from transient
//   RunController storage into RunState as a PURE TAIL APPEND after neow_rng,
//   so the three offers the boss chest pops at room entry become visible to the
//   oracle translator (BOSS_REWARD.screen_state.relics now emits) and to
//   diff_run_states -- the storage prerequisite of design §6 S2-G2 item 2's
//   zero-diff boss-relic pick. Every pre-v8 RunState byte keeps its offset
//   (static_asserts at run_state.hpp prove append + no tail padding); a pad
//   carve was checked and rejected -- the remaining declared pads are scattered
//   1-2 byte holes that cannot legally hold 3×uint16. sizeof(RunState)
//   2184 -> 2200, so per §8 this is a schema bump. sizeof(CombatState) is
//   UNCHANGED, so the 20 frozen v1 combat fixtures are NOT regenerated (their
//   loader checks only the combat state size); no v2/RUN trace goldens are
//   committed, so nothing else on disk moves. kTraceFormatV2 follows this
//   constant to 8. PublicView is UNCHANGED (the offers already reach it through
//   the v1-reserved boss_relic_choice_reserved fields, gated on `seen`), so
//   PUBLIC_VIEW_VERSION stays 4; the twin fixture file is regenerated only
//   because its header stamps engine_schema_version -- its cases and view
//   payloads are byte-identical. RunController's sizeof is net unchanged (the
//   16-byte member left the controller as its RunState grew by 16), and
//   BossChestState's byte classification (offers hidden until `seen`) carries
//   over verbatim into the RunState table in byte_class.hpp.
// v9 (=9): the run-outcome kind and the Act-4 floor base (S3.31 -- the
//   s3-tasks.md row is the conventions-§5 planned site for this bump, and the
//   ledger names it as the ONLY owner of 8 -> 9). A PAD CARVE, not an append:
//   `RunState::victory_kind` (u8) and `RunState::act4_floor_base` (u8) are cut
//   out of the two bytes `pad_gold_align[2]` declared between max_hp and the
//   4-aligned gold, so NO OFFSET MOVES, sizeof(RunState) stays 2200 and
//   sizeof(CombatState) is untouched (two static_asserts at run_state.hpp prove
//   the carve closes the hole exactly). Both former padding bytes were
//   value-init zero on every path that produces a RunState, and 0 is exactly
//   the correct v9 reading of them -- RunVictoryKind::NONE and "no Act-4
//   crossing" -- so every v8 record still reads back byte-identically AND
//   meaning-identically.
//   WHY THE VERSION MOVES AT ALL when no byte did: a v8 reader cannot tell "0
//   because this byte is padding" from "0 because the run has no outcome yet",
//   and a v8 WRITER left the byte INDETERMINATE on any path that did not
//   value-initialise (the conventions-§8 "a struct compared with memcmp must
//   declare its padding" incident is exactly that hazard). The stamp is what
//   says these two bytes are now interpretable. It also front-loads the Act-4
//   crossing's field so S3.32 needs no second bump -- which is the whole point
//   of the ledger naming one owner.
//   `victory_kind` replaces the `run_is_victory` BOOLEAN with the game's own
//   independent victory / trueVictor pair (Metrics.java:82,107;
//   DeathScreen.java:291-299 vs VictoryScreen.java:254-269); run_is_victory()
//   keeps its name and meaning as `kind != NONE`.
//   sizeof(CombatState) is UNCHANGED, so the 20 frozen v1 combat fixtures are
//   REGENERATED via the checked-in generator (tools/fixture_gen/
//   gen_combat_fixtures.cpp) and come out BYTE-IDENTICAL -- the strongest form
//   of the zero-diff-in-meaning proof, since the fixtures stamp the DECOUPLED
//   on-disk tag kTraceFormatV1 (=1) and their state_size reads sizeof(
//   CombatState), neither of which moved. No RUN/v2 trace goldens are
//   committed. kTraceFormatV2 follows this constant to 9.
//   PublicView is UNCHANGED and PUBLIC_VIEW_VERSION stays 6: the outcome kind
//   enters the observation record at S3.51, which owns the 6 -> 7 bump
//   (s3-design §7). The twin fixture file is not committed, so nothing on disk
//   moves for the header's engine_schema_version stamp either.
inline constexpr uint32_t SCHEMA_VERSION = 9;

}  // namespace sts::engine
