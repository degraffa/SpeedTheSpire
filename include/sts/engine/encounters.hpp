#pragma once

// Encounter framework (design doc §5.2): the two RNG-driven halves of
// "what monsters does this combat hold".
//
//   1. generate_monster_lists() -- the RUN-scoped monsterRng pool draw
//      (Exordium.generateMonsters -> generateWeak/Strong/Elites + initializeBoss).
//      Produces the ordered encounter-key-id lists a run walks through
//      (weak-first, then strong; a separate elite list; the shuffled boss
//      list).
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

// List-generation bounds, sized on the WIDEST act. Act 1 (Exordium) is the
// widest: monster_list = 3 weak + 1 first-strong + 12 strong = 16. Acts 2-3
// draw only TWO weak entries (TheCity.java:88-92 / TheBeyond.java:85-89), so
// their monster_list is 15 -- see weak_segment_for_act / monster_list_len_for_act
// below. elite_list = 10 and boss_list = 3 in every act.
inline constexpr int kMaxMonsterList = 16;
inline constexpr int kMaxEliteList = 10;
inline constexpr int kMaxBossList = 3;

// generateMonsters' weak draw count, the ONE act-dependent number in list
// generation: Exordium.generateMonsters calls generateWeakEnemies(3)
// (Exordium.java:110-112) while TheCity (:88-92) and TheBeyond (:85-89) call
// generateWeakEnemies(2). Everything else -- generateStrongEnemies(12),
// generateElites(10), the one randomLong boss shuffle -- is identical.
//
// It is a shared constant rather than a literal at the generation site because
// it also indexes the FIRST-STRONG slot: index `weak_segment_for_act(act)` is
// where populateFirstStrongEnemy's exclusion-rejection loop runs, and the Markov
// continuation (continue_monster_lists) has to land on exactly that index to
// reproduce the run-start draw sequence.
[[nodiscard]] constexpr uint8_t weak_segment_for_act(int32_t act) noexcept {
    return act == 1 ? uint8_t{3} : uint8_t{2};
}

// generateStrongEnemies' count -- act-independent (12 in all three dungeons).
inline constexpr int32_t kStrongSegment = 12;
// generateElites' count -- act-independent (10 in all three dungeons).
inline constexpr int32_t kEliteSegment = 10;

// The generated monster_list length for an act: weak + 1 first-strong + 12
// strong. 16 in Act 1, 15 in Acts 2-3.
//
// THIS NUMBER IS THE MARGIN THE EMPTY-LIST REGENERATION ARM DEPENDS ON.
// nextRoomTransition's `if (monsterList.isEmpty()) generateStrongEnemies(12)`
// (AbstractDungeon.java:1701-1706) consumes the RUN-LIFETIME monsterRng and
// would be observable for the rest of the run, so its reachability is a
// counting argument, re-derived per act -- see next_room_transition_impl's
// assert in run_advance.cpp.
[[nodiscard]] constexpr uint8_t monster_list_len_for_act(int32_t act) noexcept {
    return static_cast<uint8_t>(weak_segment_for_act(act) + 1 +
                                kStrongSegment);
}
static_assert(monster_list_len_for_act(1) == 16);
static_assert(monster_list_len_for_act(2) == 15);
static_assert(monster_list_len_for_act(3) == 15);
static_assert(monster_list_len_for_act(1) <= kMaxMonsterList);

// --- The encounter-key ID space ---------------------------------------------
//
// WHAT A LIST SLOT HOLDS, AND WHY IT IS AN INTEGER.
//
// It used to be a `std::string_view` into the generated registry's `.rodata`,
// which made `MonsterLists` -- and therefore `RunController`, which embeds it
// -- a struct carrying POINTERS. `RunController` is `static_assert`ed trivially
// copyable and is memcpy'd everywhere (the batch `advance()` API, `twin.cpp`,
// snapshot/keyframe writers), and trivially copyable is NOT position
// independent. Two consequences, both observed rather than reasoned about:
//
//   * CROSS-PROCESS: a controller written to disk by one process and read back
//     by another carries dangling pointers. The training repo's snapshot bank
//     (SpireTrainer T2.1) SEGFAULTED the first time `bank_check` loaded a bank
//     another process had written, in `encode_public_view` -> `encode_prefix`,
//     dereferencing a stored view. It worked around it consumer-side (store
//     registry ids beside the state, zero the views, rebind on restore); this
//     is the durable fix, and it retires that workaround.
//   * DETERMINISM: under ASLR the addresses differ per launch, so any byte hash
//     of a controller was a function of WHERE THE BINARY LOADED. `tools/fuzz`
//     hit exactly that -- a trajectory hashed stably inside one process and
//     differently in the next, so every emitted reproducer "failed to
//     reproduce" -- and had to hash the keys by their characters instead.
//
// So a slot holds an `EncounterKeyId`, and text is resolved AT THE USE SITE
// (`encounter_key_of`) by the handful of consumers that need it: the
// `enter_combat` composition join, the oracle-replay list comparison, the
// distribution checks and the scratch witnesses. Nothing on the hot path
// resolves anything.
//
// THE VALUE IS THE REGISTRY'S CANONICAL id, not a per-row id. `encounter_key_id`
// goes through `registry::encounter_by_game_id`, which returns the LOWEST-id row
// carrying a game key -- and a key can appear in two pools (Act 3's
// "3 Darklings" is both WEAK and STRONG, trap 8). Every consumer already
// resolved a stored key that way, so canonicalising on the way IN keeps this
// change a pure representation change: `PublicView`'s encounter bytes, the
// composition program a slot resolves to, and the exclusion comparisons are all
// bit-for-bit what they were when the slot held the string.
using EncounterKeyId = uint8_t;

// The empty slot. Registry ids are assigned from 1 upward (registry/
// encounters.yaml), so 0 names no encounter -- which is also the value
// `PublicView` has always carried for a key it could not resolve.
inline constexpr EncounterKeyId kNoEncounterKey = 0;

// ENGINE-LOCAL KEY IDS: game keys the ENGINE names that registry/encounters.yaml
// does not carry. Act 4 is the only case -- `TheEnding.generateMonsters` /
// `initializeBoss` add three literal "Shield and Spear" / "The Heart" entries
// with no RNG at all (s3-design §5 trap 3), and those rows are S3.41's grant.
// Before this change the list simply held the literal and every registry join
// on it returned nullptr; the id space has to be able to say the same thing.
//
// Resolution is REGISTRY-FIRST (`encounter_key_id`), so the day the rows land
// these ids stop being produced and every consumer keeps working unchanged --
// the same way the string spelling grew into its registry row.
inline constexpr EncounterKeyId kEngineKeyBase = 200;

inline constexpr std::string_view kActEndingEncounterKey = sts::registry::kAct4EliteEncounter;  // S3.41 row 62
inline constexpr std::string_view kActEndingBossKey = sts::registry::kAct4BossEncounter;  // S3.41 row 63
inline constexpr EncounterKeyId kEngineKeyShieldAndSpear = kEngineKeyBase + 0;
inline constexpr EncounterKeyId kEngineKeyTheHeart = kEngineKeyBase + 1;

// The registry has to stay clear of the engine-local block. Its ids are
// append-only and its row count is well under this base, so the margin is wide;
// this fires long before an engine-local id could alias a registry one.
static_assert(sts::registry::kEncounterCount < kEngineKeyBase,
              "registry encounter ids have grown into the engine-local key "
              "block -- raise kEngineKeyBase (and nothing else: the ids are "
              "engine-internal, never persisted)");

// Resolve a game key (an Exordium pool / getEncounter string) to the id a list
// slot stores. Registry-first; then the engine-local table; then kNoEncounterKey
// for a key nothing knows.
[[nodiscard]] EncounterKeyId encounter_key_id(std::string_view key) noexcept;

// The inverse: the game key a stored id names, or an empty view for
// kNoEncounterKey / an id nothing knows. This is the ONLY place a list slot
// becomes text.
[[nodiscard]] std::string_view encounter_key_of(EncounterKeyId id) noexcept;

// The registry row a stored id names, or nullptr -- which is what an
// engine-local id (Act 4, until S3.41) and kNoEncounterKey both give, exactly
// as `encounter_by_game_id` gave nullptr for their strings.
[[nodiscard]] const sts::registry::EncounterDef* encounter_def_of(
    EncounterKeyId id) noexcept;

// The run's generated monster lists (encounter key IDS, in walk order).
// `monster_list` is the shared weak-then-strong combat list; `elite_list` the
// separate elite list; `boss_list` the shuffled boss order.
//
// NO PADDING, DECLARED OR OTHERWISE. The three 7-byte `pad_*` members this
// struct used to carry existed only because `std::string_view`'s 8-byte
// alignment inserted a gap after each `uint8_t` count, and an implicit gap in a
// struct that is memcmp'd and byte-HASHED is indeterminate (conventions §8's
// rule-of-three: RunState, CombatState/RunController, and MonsterLists itself
// -- each read zero on Linux and garbage on Windows).
// A `uint8_t` array has alignment 1, so the gaps are gone rather than declared,
// and the struct is 32 bytes of pure content. The tripwire's
// `NoDeclaredGapsInByteHashedStructs` and
// `EveryByteOfAByteHashedStructBelongsToAMember` still hold it.
struct MonsterLists {
    std::array<EncounterKeyId, kMaxMonsterList> monster_list{};
    uint8_t monster_list_count = 0;
    std::array<EncounterKeyId, kMaxEliteList> elite_list{};
    uint8_t elite_list_count = 0;
    std::array<EncounterKeyId, kMaxBossList> boss_list{};
    uint8_t boss_list_count = 0;
};
static_assert(alignof(MonsterLists) == 1,
              "MonsterLists is all uint8_t -- alignment 1 is what makes it "
              "gap-free wherever it is embedded");
static_assert(sizeof(MonsterLists) ==
                  kMaxMonsterList + kMaxEliteList + kMaxBossList + 3,
              "MonsterLists is its three lists plus their three counts and "
              "nothing else -- no padding, declared or implicit");

// Generate the act's monster lists from `monster_rng` (the run-scoped stream),
// in the exact generateMonsters draw order: weak (weak_segment_for_act(act)),
// first-strong (exclusion loop) + strong (12), elites (10), then one
// randomLong() seeding the boss-list shuffle. `act` selects the pool rows
// (1 = Exordium, 2 = TheCity, 3 = TheBeyond). Pure over `monster_rng` (advances
// it by the full draw sequence).
//
// Acts 2 and 3 run off the SAME continuing monsterRng the Act-1 lists consumed
// -- it is a run-lifetime stream that dungeonTransitionSetup never touches
// (s2-design §4.2) -- so the caller passes rs.monster_rng at whatever counter
// the previous act left it.
void generate_monster_lists(int32_t act, RngStream& monster_rng,
                            MonsterLists& out) noexcept;

// --- Suffix continuation (the belief sampler's encounter row) ----------------
//
// CONDITION, DON'T REROLL (training-plan §2.4). List generation is SEQUENTIAL
// with last-one / last-two exclusions -- i.e. a Markov chain -- so the exact
// conditional law of the unconsumed suffix given the observed prefix is "run
// the same chain forward from where the prefix left off". This continues
// `lists` past its first `monster_keep` / `elite_keep` entries with `rng`,
// preserving the kept prefixes byte-for-byte and each list's original length,
// and applying the SAME rules generate_monster_lists applies at each index:
//   * indices [0,W)  -- WEAK pool, no immediate repeat, no A-B-A, where
//                       W == weak_segment_for_act(act) (3 in Act 1, 2 after);
//   * index W        -- STRONG pool through the first-strong exclusion loop,
//                       keyed on the LAST weak entry;
//   * indices (W,N)  -- STRONG pool, no immediate repeat, no A-B-A;
//   * elites         -- ELITE pool, no immediate repeat only.
// Because the rejection rules read only the one or two entries before the
// cursor, a continuation from `keep` is exactly the run-start generation
// conditioned on that prefix. `continue_monster_lists(act, rng, 0, 0, lists)`
// on a list pair whose counts are the run-start lengths therefore reproduces
// generate_monster_lists' two lists exactly, from an identically-positioned
// stream -- the equivalence the sampler tests assert.
//
// A keep count larger than the list's length is clamped to it. The boss list
// is NOT touched here (its conditional law is a different shape) -- see
// condition_boss_list.
void continue_monster_lists(int32_t act, RngStream& rng, uint8_t monster_keep,
                            uint8_t elite_keep, MonsterLists& lists) noexcept;

// The boss list's own row: `boss_list[0]` is PUBLIC (the act boss is named on
// the map from floor 1), and the run-start order is a uniform shuffle, so the
// exact conditional law of the remainder given the observed head is a uniform
// permutation of the entries past the observed prefix. Preserves that prefix and
// the multiset.
//
// `keep` IS THE OBSERVED PREFIX LENGTH, and it is the reason this takes an
// argument at all (S2.28). It was `1` implicitly, because in S1 -- and in every
// non-A20 act -- only entry 0 is ever named. THE A20 DOUBLE BOSS CONSUMES A
// SECOND ENTRY: once the player has walked into the second Act-3 boss room, that
// boss's identity has been REVEALED by the monsters standing in front of them,
// so `boss_list[1]` is public and permuting it would make a hidden twin disagree
// with the truth on a value the player can see -- which is exactly what the leak
// gate exists to catch, in the direction that FAILS the gate rather than leaking.
// The caller computes it the same way it computes the monster/elite prefixes:
// the cursor, plus one while standing in a room of that kind. A `keep` below 1 is
// raised to 1 (entry 0 is public from act start whether or not the cursor passed
// it) and a `keep` past the count leaves the list alone.
//
// The permutation is a plain Fisher-Yates over `rng` -- this is sampler-side
// belief machinery, never a game-parity path, so it deliberately does not
// reproduce Collections.shuffle.
void condition_boss_list(MonsterLists& lists, RngStream& rng,
                         uint8_t keep = 1) noexcept;

}  // namespace sts::engine
