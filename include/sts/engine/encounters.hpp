#pragma once

// Encounter framework (design doc §5.2): the two RNG-driven halves of
// "what monsters does this combat hold".
//
//   1. generate_monster_lists() -- the RUN-scoped monsterRng pool draw
//      (Exordium.generateMonsters -> generateWeak/Strong/Elites + initializeBoss).
//      Produces the ordered encounter-KEY lists a run walks through (weak-first,
//      then strong; a separate elite list; the shuffled boss list).
//   2. resolve_composition() -- the FLOOR-scoped miscRng composition draw
//      (MonsterHelper.getEncounter + its spawn* helpers). Turns one encounter key
//      into the concrete spawn-order list of monster game_ids.
//
// Both are pure functions over an RngStream (the caller supplies the stream at the
// right state -- run-scoped monsterRng for lists, floor-scoped miscRng for
// compositions), so they are unit-testable against hand-derived draw sequences
// without a full dungeon. The pool/weight/exclusion tables and the composition
// programs are DATA (registry/encounters.yaml -> generated encounter_table.hpp);
// this file is the interpreter.
//
// Provenance (read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * Exordium.generateMonsters/generateWeak/Strong/Elites/generateExclusions/
//     initializeBoss (Exordium.java:110-221).
//   * MonsterInfo.normalizeWeights/roll/compareTo (MonsterInfo.java:27-52) -- the
//     stable ascending-weight sort BEFORE rolling (TRAP 1) + the normalized
//     cumulative-weight walk.
//   * AbstractDungeon.populateMonsterList/populateFirstStrongEnemy
//     (AbstractDungeon.java:1057-1096) -- no-immediate-repeat + no-A-B-A (weak/
//     strong) / no-immediate-repeat only (elites); the exclusion rejection loop.
//   * MonsterHelper.getEncounter + spawn helpers (MonsterHelper.java:389-836).
//   * Collections.shuffle(new Random(monsterRng.randomLong())) for the boss list
//     (Exordium.java:206) -- the JDK-LCG Fisher-Yates (jdk_shuffle, golden-tested).

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "sts/engine/combat_state.hpp"     // kMonsterCap
#include "sts/engine/rng_stream.hpp"
#include "sts/registry/encounter_table.hpp"  // generated: kEncounters, EncounterDef

namespace sts::engine {

// --- Composition resolution (miscRng) ---------------------------------------

// Capacity of a group's CONSTRUCTION trace (below). The widest S1 program is
// Exordium Thugs: two PICKs of three candidates each = 6 constructions.
inline constexpr uint8_t kConstructedCap = 12;

// One resolved monster group: the spawn-order list of monster game_ids (== turn
// order). game_ids are the game's AbstractMonster.ID strings (the join keys to
// monsters.yaml); the spawn layer maps them to MonsterId. Capacity kMonsterCap
// (7) covers the largest S1 composition (Lots of Slimes = 5) with headroom.
//
// `constructed`/`kept_mask` carry the CONSTRUCTION trace: every monster the
// game's list build CONSTRUCTS, in construction order, bit i of the mask set
// when constructed[i] is a kept member. The two lists are identical except
// under a PICK step (bottomGet*, MonsterHelper.java:799-822), which
// eager-constructs its whole candidate ArrayList before random(0, n-1) keeps
// one -- and every construction has already drawn its max HP off monsterHpRng
// (setHp in the AbstractMonster ctor; a Louse ctor draws biteDamage too,
// LouseNormal.java:60 / LouseDefensive.java:63). The discarded candidates'
// draws are permanently consumed, so the spawn layer must burn them in trace
// order or every kept monster's HP is read from the wrong stream position --
// the STS01789 class: the game's floor-10 Thugs rolled {30, 49}, a kept-only
// sim rolled {33, 51}, and the killing blow the game landed left the sim's
// slime alive to keep attacking. Kept members appear in the trace in slot
// order, so `members` is exactly the kept subsequence of `constructed`.
struct ResolvedGroup {
    uint8_t count = 0;
    std::array<std::string_view, kMonsterCap> members{};
    uint8_t constructed_count = 0;
    std::array<std::string_view, kConstructedCap> constructed{};
    uint16_t kept_mask = 0;
};

// Resolve an encounter's composition program into a ResolvedGroup, consuming
// miscRng draws in the exact game order (the CompStep contract in
// encounter_table.hpp). Does NOT spawn/roll HP -- that is the monsterHpRng phase
// (spawn_group). Pure over `misc_rng`.
[[nodiscard]] ResolvedGroup resolve_composition(
    const sts::registry::EncounterDef& enc, RngStream& misc_rng) noexcept;

// Convenience: resolve by encounter game key (Exordium pool / getEncounter
// string). Returns false (and leaves `out` unspecified) for an unknown key.
[[nodiscard]] bool resolve_encounter(std::string_view key, RngStream& misc_rng,
                                     ResolvedGroup& out) noexcept;

// --- Pool draw (monsterRng) -------------------------------------------------

// Act-1 (Exordium) list-generation bounds. monster_list = 3 weak + 1 first-strong
// + 12 strong = 16; elite_list = 10; boss_list = 3 (shuffled; [0] is the act boss).
inline constexpr int kMaxMonsterList = 16;
inline constexpr int kMaxEliteList = 10;
inline constexpr int kMaxBossList = 3;

// The run's generated monster lists (encounter KEYS, in walk order). `monster_list`
// is the shared weak-then-strong combat list; `elite_list` the separate elite
// list; `boss_list` the shuffled boss order.
struct MonsterLists {
    std::array<std::string_view, kMaxMonsterList> monster_list{};
    uint8_t monster_list_count = 0;
    std::array<std::string_view, kMaxEliteList> elite_list{};
    uint8_t elite_list_count = 0;
    std::array<std::string_view, kMaxBossList> boss_list{};
    uint8_t boss_list_count = 0;
};

// Generate the act's monster lists from `monster_rng` (the run-scoped stream), in
// the exact Exordium draw order: weak (3), first-strong (exclusion loop) + strong
// (12), elites (10), then one randomLong() seeding the boss-list shuffle. `act`
// selects the pool rows (only act 1 is populated). Pure over
// `monster_rng` (advances it by the full draw sequence).
void generate_monster_lists(int32_t act, RngStream& monster_rng,
                            MonsterLists& out) noexcept;

}  // namespace sts::engine
