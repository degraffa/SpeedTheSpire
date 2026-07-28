#pragma once

// Native relic-hook plumbing (internal to src/engine -- NOT public API). The
// native escape hatch used to be one 380-line switch in relic_hooks.cpp; it is
// now per-RelicTier translation units under src/engine/relics/ plus the dispatch
// table in relic_hooks.cpp, mirroring the monster_dispatch.cpp split (per-monster
// TUs + a switch returning function pointers, `default: return nullptr` for the
// not-yet-implemented ids).
//
// Each native relic's body is a free function with the RelicNativeFn signature,
// declared in its tier header (relics_starter.hpp, relics_common.hpp,
// relics_uncommon.hpp, ...). The dispatch table itself is now GENERATED from
// registry/relics.yaml (the STS_REGISTRY_NATIVE_RELICS X-macro in the generated
// relic_table.hpp), so adding a relic tier (rares+shop, boss+specials) is: one
// new .cpp, one new header, one CMakeLists line -- and NO edit to
// relic_hooks.cpp or to any file an earlier tier owns. That is what makes the
// remaining tiers genuinely parallel.

#include <cstdint>

#include "sts/engine/combat_state.hpp"  // CombatState
#include "sts/engine/relic_hooks.hpp"   // RelicHook, RelicHookContext
#include "sts/engine/run_state.hpp"     // RelicSlot
#include "sts/engine/types.hpp"         // RelicId

namespace sts::engine {

// One native relic's hook body. `slot` is the responding relic's live slot
// (counter mutation writes here); the body inspects `hook` and returns without
// queuing anything for the hooks it does not answer.
//
// RelicNativeSig is the FUNCTION type (RelicNativeFn is a pointer to it); the
// generated dispatch table declares each handler as `extern RelicNativeSig fn;`,
// which both spells the declaration once and pins every native body to exactly
// this signature (a mismatched definition mangles differently -> link error).
using RelicNativeSig = void(CombatState& s, RelicHook hook, RelicSlot& slot,
                            const RelicHookContext& ctx) noexcept;
using RelicNativeFn = RelicNativeSig*;

// The dispatch table (relic_hooks.cpp, generated from registry/relics.yaml): the
// native body for `id`, or nullptr for a non-native / unrecognized id. A native
// relic whose combat body is DEFERRED maps to its explicit empty body (defined in
// the batch TU that registered it), not to nullptr -- see the
// STS_REGISTRY_NATIVE_RELICS expansion in relic_hooks.cpp.
[[nodiscard]] RelicNativeFn relic_native_fn(RelicId id) noexcept;

// "Bloodied": at or below HALF max HP, half rounded DOWN. The game spells the
// test three times and all three agree for integer HP -- preBattlePrep's int
// division `currentHealth <= maxHealth / 2` (AbstractPlayer.java:1575), the
// damage-side cross's float `currentHealth <= maxHealth / 2.0f` (:1476), and
// the heal-side cross's `currentHealth > maxHealth / 2.0f`
// (AbstractCreature.java:404, which is this negated). `hp * 2 <= max` is the
// exact integer form of each: for max 7, all three answer hp <= 3. Spelled once
// here so the sites that key off it (Red Skull's two crosses, Meat on the
// Bone's pre-victory heal) cannot drift apart.
[[nodiscard]] inline bool player_is_bloodied(const CombatState& s) noexcept {
    return static_cast<int32_t>(s.player_hp) * 2 <= s.player_max_hp;
}

// The relics' onNotBloodied fan-out (AbstractCreature.heal:404-408, reached
// through AbstractPlayer.heal:1544-1552), fired by heal_player_with_relics when
// a heal leaves the player above half max HP.
//
// A HAND-WRITTEN fan-out rather than a registry RelicHook: Red Skull is the
// game's only MECHANICAL implementor -- `grep -rn onNotBloodied com/` finds
// AbstractRelic's empty base, the two call sites, MeatOnTheBone's
// presentation-only `stopPulse()` (MeatOnTheBone.java:47-50) and RedSkull -- so
// a new hook id would move kRelicHookCount for one row. Philosopher's Stone's
// onSpawnMonster (monster_dispatch.cpp) and Velvet Choker's canPlay veto are
// the precedents. Defined in relics_common.cpp beside relic_native_red_skull,
// so the -3 and the +3 it undoes stay in one file.
void dispatch_relics_on_not_bloodied(CombatState& s, RelicSlot* relics,
                                     uint8_t count) noexcept;

// Heal the player by `n`, clamped to max HP (HealAction semantics). No HEAL opcode
// exists; a pure heal has no queue-ordering interplay
// with other S1 relic effects, so it is applied directly at dispatch time.
inline void heal_player(CombatState& s, int32_t n) noexcept {
    int32_t hp = static_cast<int32_t>(s.player_hp) + n;
    if (hp > s.player_max_hp) {
        hp = s.player_max_hp;
    }
    s.player_hp = static_cast<int16_t>(hp);
}

}  // namespace sts::engine
