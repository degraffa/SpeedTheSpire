#include "sts/translate/combat_vitals.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"

namespace sts::translate {

namespace eng = sts::engine;
namespace reg = sts::registry;

namespace {

// ---- the sim-side projection ----------------------------------------------

[[nodiscard]] VitalsPower project_power(const eng::PowerSlot& p) {
    VitalsPower out;
    const auto id = static_cast<eng::PowerId>(p.power_id);
    out.amount = p.amount;
    if (id == eng::PowerId::NONE) {
        out.known = false;  // a NONE inside a live count: carried, not skipped
    } else {
        out.id = std::string(reg::power_game_id(id));
    }
    return out;
}

void project_powers(const eng::PowerSlot* slots, uint8_t count,
                    std::vector<VitalsPower>& out) {
    const uint8_t n = count > eng::kPowerCap ? static_cast<uint8_t>(eng::kPowerCap) : count;
    out.reserve(n);
    for (uint8_t i = 0; i < n; ++i) out.push_back(project_power(slots[i]));
}

[[nodiscard]] VitalsCard project_card(const eng::CardInstance& c) {
    VitalsCard out;
    const auto id = static_cast<eng::CardId>(c.card_id);
    out.upgrades = c.upgrade;
    if (id == eng::CardId::NONE) {
        out.known = false;
    } else {
        out.id = std::string(reg::card_game_id(id));
    }
    // costForTurn as the game would report it (combat_vitals.hpp VitalsCard).
    // The registry stores the game's two negative sentinels as FLAGS with
    // base_cost 0 (stsgen/emit/cards.py parse_card_flags), and the per-instance
    // flags word carries the authored half at the instance's live upgrade level
    // (advance.cpp seeds it from card_flags(def, upgrade), and an in-combat
    // upgrade() re-reads it), so reading the bits here is reading the same fact
    // the dump prints -- not a guess about the row.
    if (eng::has_card_flag(c.flags, eng::CardFlag::XCOST)) {
        out.cost = -1;
    } else if (eng::has_card_flag(c.flags, eng::CardFlag::UNPLAYABLE)) {
        out.cost = -2;
    } else {
        out.cost = static_cast<int>(c.cost_now);
    }
    return out;
}

// `skip` names pool rows the pile holds that the projection must not publish
// (the Stasis exclusion below); an empty set publishes everything.
void project_pile(const eng::CombatState& s, const eng::CardPoolIndex* idx,
                  uint8_t count, int cap, std::vector<VitalsCard>& out,
                  const std::set<int>* skip = nullptr) {
    const int n = count > cap ? cap : count;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const eng::CardPoolIndex pi = idx[i];
        if (pi >= eng::kCardPoolCap) continue;  // corrupt index: nothing to name
        if (skip != nullptr && skip->count(static_cast<int>(pi)) != 0) continue;
        out.push_back(project_card(s.card_pool[pi]));
    }
}

// The pool rows parked in limbo by a LIVE Stasis power (the Bronze Orb's stolen
// card). The engine holds the stolen card in limbo with its pool index + 1 in
// the STASIS slot's `counter` (power_stasis.hpp); the game holds it in
// `StasisPower.card`, a field of the power object that the protocol serializes
// in NO pile -- it is in neither the hand, the draw, the discard nor the
// exhaust of the dump, and there is no limbo entry for it either. So the two
// sides describe the same fact with different storage, and the sim's rows for
// it are dropped from the projection rather than read as extra cards. Only
// those exact rows: everything else in limbo still compares.
[[nodiscard]] std::set<int> stasis_held_pool_rows(const eng::CombatState& s) {
    std::set<int> held;
    const uint8_t mc = s.monster_count > eng::kMonsterCap
                           ? static_cast<uint8_t>(eng::kMonsterCap) : s.monster_count;
    for (uint8_t m = 0; m < mc; ++m) {
        const eng::MonsterState& mon = s.monsters[m];
        const uint8_t pc = mon.power_count > eng::kPowerCap
                               ? static_cast<uint8_t>(eng::kPowerCap) : mon.power_count;
        for (uint8_t i = 0; i < pc; ++i) {
            if (static_cast<eng::PowerId>(mon.powers[i].power_id) != eng::PowerId::STASIS)
                continue;
            const int c = mon.powers[i].counter;
            if (c > 0 && c <= static_cast<int>(eng::kCardPoolCap)) held.insert(c - 1);
        }
    }
    return held;
}

// ---- the compare ----------------------------------------------------------

// An unresolved id renders with a leading '?'. An unresolved id that is also
// EMPTY is the sim-side shape -- a NONE slot inside a live count -- and gets a
// spelled name, so a row for it reads `player.powers[?<none>]` rather than
// `player.powers[?]`.
[[nodiscard]] std::string unresolved_name(const std::string& id) {
    return id.empty() ? "?<none>" : ("?" + id);
}

[[nodiscard]] std::string card_key(const VitalsCard& c) {
    std::string k = c.known ? c.id : unresolved_name(c.id);
    if (c.upgrades == 1) k += "+";
    else if (c.upgrades > 1) k += "+" + std::to_string(c.upgrades);
    return k;
}

[[nodiscard]] std::string power_key(const VitalsPower& p) {
    return p.known ? p.id : unresolved_name(p.id);
}

// The `<domain>:<id>` spelling `VitalsReport::unknown_ids` carries.
[[nodiscard]] std::string unknown_entry(const char* domain, const std::string& id) {
    return std::string(domain) + ":" + (id.empty() ? "<none>" : id);
}

// id -> the sorted amounts of every instance carrying that id. Instanced
// powers (The Bomb) are the reason this is a multiset and not a map to one
// number: two fuses at 3 and 2 must not read as one power.
using PowerSet = std::map<std::string, std::vector<int>>;

// A SPENT Artifact -- amount <= 0 -- is not a power on either side, and only
// one of the two still lists it. `ApplyPowerAction` (:106-138) consumes a stack
// per nullified debuff and the game destroys the power at 0; this engine leaves
// the 0-stack slot in place on purpose, because `amount <= 0` already reads as
// "no charges" everywhere that asks and the pump has no power-GC pass
// (power_hooks.cpp `apply_power_blocked_by_artifact`, whose comment is the
// record of that decision). Dropping it from BOTH sides is what keeps a
// documented representation choice out of the divergence list; a live Artifact
// at any positive amount is compared exactly like every other power.
[[nodiscard]] bool is_spent_artifact(const VitalsPower& p) {
    return p.known && p.amount <= 0 &&
           p.id == reg::power_game_id(eng::PowerId::ARTIFACT);
}

[[nodiscard]] PowerSet power_set(const std::vector<VitalsPower>& ps) {
    PowerSet out;
    for (const VitalsPower& p : ps) {
        if (is_spent_artifact(p)) continue;
        out[power_key(p)].push_back(p.amount);
    }
    for (auto& [k, v] : out) std::sort(v.begin(), v.end());
    return out;
}

// Is every element of `a` present in `b` with at least the same multiplicity?
// Both are sorted. Used only by the HAND_SELECT containment rule below.
[[nodiscard]] bool is_submultiset(const std::vector<int>& a,
                                  const std::vector<int>& b) {
    std::size_t i = 0;
    for (int x : a) {
        while (i < b.size() && b[i] < x) ++i;
        if (i >= b.size() || b[i] != x) return false;
        ++i;
    }
    return true;
}

[[nodiscard]] std::string amounts_repr(const std::vector<int>& v) {
    if (v.size() == 1) return std::to_string(v[0]);
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s += "|";
        s += std::to_string(v[i]);
    }
    return s + "]";
}

void push(VitalsReport& r, std::string field, std::string game, std::string sim) {
    r.diffs.push_back(VitalsFieldDiff{std::move(field), std::move(game), std::move(sim)});
}

void cmp_int(VitalsReport& r, const std::string& field, int game, int sim) {
    if (game != sim) push(r, field, std::to_string(game), std::to_string(sim));
}

void cmp_bool(VitalsReport& r, const std::string& field, bool game, bool sim) {
    if (game != sim) push(r, field, game ? "true" : "false", sim ? "true" : "false");
}

void cmp_powers(VitalsReport& r, const std::string& prefix,
                const std::vector<VitalsPower>& game,
                const std::vector<VitalsPower>& sim) {
    const PowerSet g = power_set(game);
    const PowerSet s = power_set(sim);
    std::set<std::string> keys;
    for (const auto& [k, v] : g) keys.insert(k);
    for (const auto& [k, v] : s) keys.insert(k);
    for (const std::string& k : keys) {
        const auto gi = g.find(k);
        const auto si = s.find(k);
        const std::string gr = gi == g.end() ? "(absent)" : amounts_repr(gi->second);
        const std::string sr = si == s.end() ? "(absent)" : amounts_repr(si->second);
        if (gr != sr) push(r, prefix + "[" + k + "]", gr, sr);
    }
}

// `containment_only`: report a key only where the CAPTURE has more of it than
// the sim. That is the HAND_SELECT hand (header exclusion): the opening action
// held its ineligible cards aside in a list the protocol never serializes, so
// the capture's hand can only be a SUBSET of the sim's, never a superset. An
// extra card capture-side is still a real divergence and is still reported.
void cmp_pile(VitalsReport& r, const std::string& pile,
              const std::vector<VitalsCard>& game,
              const std::vector<VitalsCard>& sim,
              bool containment_only = false) {
    std::map<std::string, int> g;
    std::map<std::string, int> s;
    for (const VitalsCard& c : game) ++g[card_key(c)];
    for (const VitalsCard& c : sim) ++s[card_key(c)];
    std::set<std::string> keys;
    for (const auto& [k, v] : g) keys.insert(k);
    for (const auto& [k, v] : s) keys.insert(k);
    for (const std::string& k : keys) {
        const auto gi = g.find(k);
        const auto si = s.find(k);
        const int gc = gi == g.end() ? 0 : gi->second;
        const int sc = si == s.end() ? 0 : si->second;
        if (gc == sc) continue;
        if (containment_only && gc < sc) continue;
        push(r, pile + "[" + k + "]", std::to_string(gc), std::to_string(sc));
    }
}

// ---- the cost compare (combat_vitals.hpp, the `--costs` block) -------------

// card key -> the SORTED costs of every instance carrying that key. Same key as
// `cmp_pile`, so a cost row names a card exactly the way a vitals pile row does.
using CostSet = std::map<std::string, std::vector<int>>;

[[nodiscard]] CostSet cost_set(const std::vector<VitalsCard>& cards) {
    CostSet out;
    for (const VitalsCard& c : cards) out[card_key(c)].push_back(c.cost);
    for (auto& [k, v] : out) std::sort(v.begin(), v.end());
    return out;
}

// Is this row the game's animation-deferred `resetAttributes` rather than a
// cost divergence? The conjuncts and their citations are in combat_vitals.hpp's
// `--costs` block; `deferred_pile` is the caller's answer to the first one.
//
// IT IS A MULTISET DIFFERENCE, NOT A POSITIONAL WALK, and that is not a
// refinement -- a positional walk over the two SORTED lists gets the answer
// wrong whenever a key has several instances. `[0|1] -> [1|2]` is one card the
// game has not reset yet (0 against the sim's 2) beside one both sides agree on
// at 1; sorting pairs it as (0,1) and (1,2) instead, and the second pair --
// a capture-side 1, not 0 -- then fails a per-position test that the row
// actually satisfies. So cancel what the two sides have in common first, and
// judge only what is left over: every remaining CAPTURE cost must be 0 (the
// `setCostForTurn` family: Corruption, Mummified Hand, Infernal Blade,
// Madness) and every remaining SIM cost must be positive (the value the reset
// restored). Anything else -- a capture cost that is low but not zero, a
// capture cost HIGHER than the sim's -- is a divergence and is reported.
[[nodiscard]] bool deferred_reset_shape(const std::vector<int>& game,
                                        const std::vector<int>& sim,
                                        bool deferred_pile) {
    if (!deferred_pile || game.size() != sim.size()) return false;
    // Both are sorted ascending (`cost_set`), so the common part is a
    // straight merge walk.
    std::vector<int> rest_game;
    std::vector<int> rest_sim;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < game.size() && j < sim.size()) {
        if (game[i] == sim[j]) { ++i; ++j; continue; }
        if (game[i] < sim[j]) rest_game.push_back(game[i++]);
        else rest_sim.push_back(sim[j++]);
    }
    while (i < game.size()) rest_game.push_back(game[i++]);
    while (j < sim.size()) rest_sim.push_back(sim[j++]);
    if (rest_game.empty() || rest_game.size() != rest_sim.size()) return false;
    for (int c : rest_game) {
        if (c != 0) return false;
    }
    for (int c : rest_sim) {
        if (c <= 0) return false;
    }
    return true;
}

// `containment_only` is the HAND_SELECT rule, inherited from `cmp_pile`: the
// capture's hand is a subset of the sim's, so a key whose capture-side cost
// list is a sub-multiset of the sim's is the held-aside remainder and is not
// judged. Anything the capture has that the sim does not still reports.
void cmp_costs(VitalsReport& r, const std::string& pile,
               const std::vector<VitalsCard>& game,
               const std::vector<VitalsCard>& sim,
               bool containment_only = false,
               bool deferred_pile = false) {
    const CostSet g = cost_set(game);
    const CostSet s = cost_set(sim);
    std::set<std::string> keys;
    for (const auto& [k, v] : g) keys.insert(k);
    for (const auto& [k, v] : s) keys.insert(k);
    for (const std::string& k : keys) {
        const auto gi = g.find(k);
        const auto si = s.find(k);
        const bool has_g = gi != g.end();
        const bool has_s = si != s.end();
        if (has_g && has_s && gi->second == si->second) continue;
        if (containment_only && has_g && has_s &&
            is_submultiset(gi->second, si->second)) {
            continue;
        }
        // A MEMBERSHIP divergence is `--vitals`' row to report, not this
        // compare's: reporting it here too would double-count the same fact
        // under a `.cost` name that says nothing about cost, and there is no
        // like-for-like pairing to read a cost delta out of. That covers both
        // shapes -- a key only one side holds, and a key both hold but in
        // different NUMBERS (the live case is a limbo Armaments the capture
        // has twice and the sim once). Only a key both sides hold the same
        // number of, with different costs, is a cost divergence.
        if (!has_g || !has_s) continue;
        if (gi->second.size() != si->second.size()) continue;
        VitalsFieldDiff row{pile + "[" + k + "].cost",
                            amounts_repr(gi->second), amounts_repr(si->second)};
        if (deferred_reset_shape(gi->second, si->second, deferred_pile)) {
            r.tolerated.push_back(std::move(row));
            continue;
        }
        r.diffs.push_back(std::move(row));
    }
}

void collect_unknown(std::set<std::string>& out, const CombatVitals& v) {
    for (const VitalsPower& p : v.player_powers)
        if (!p.known) out.insert(unknown_entry("power", p.id));
    for (const VitalsMonster& m : v.monsters) {
        if (!m.known) out.insert(unknown_entry("monster", m.id));
        for (const VitalsPower& p : m.powers)
            if (!p.known) out.insert(unknown_entry("power", p.id));
    }
    for (const std::vector<VitalsCard>* pile :
         {&v.hand, &v.draw, &v.discard, &v.exhaust, &v.limbo}) {
        for (const VitalsCard& c : *pile)
            if (!c.known) out.insert(unknown_entry("card", c.id));
    }
}

}  // namespace

CombatVitals vitals_from_combat_state(const eng::CombatState& s) {
    CombatVitals v;
    v.turn = s.turn;
    v.player_block = s.player_block;
    v.player_energy = s.player_energy;
    project_powers(s.player_powers, s.player_power_count, v.player_powers);

    const uint8_t mc = s.monster_count > eng::kMonsterCap
                           ? static_cast<uint8_t>(eng::kMonsterCap) : s.monster_count;
    v.monsters.reserve(mc);
    for (uint8_t i = 0; i < mc; ++i) {
        const eng::MonsterState& m = s.monsters[i];
        VitalsMonster vm;
        const auto id = static_cast<eng::MonsterId>(m.monster_id);
        if (id == eng::MonsterId::NONE) vm.known = false;
        else vm.id = std::string(reg::monster_game_id(id));
        vm.hp = m.hp;
        vm.block = m.block;
        vm.half_dead = eng::monster_half_dead(m);
        vm.gone = eng::monster_dead_or_escaped(m);
        project_powers(m.powers, m.power_count, vm.powers);
        v.monsters.push_back(std::move(vm));
    }

    project_pile(s, s.hand, s.hand_count, eng::kHandCap, v.hand);
    project_pile(s, s.draw, s.draw_count, eng::kDrawCap, v.draw);
    project_pile(s, s.discard, s.discard_count, eng::kDiscardCap, v.discard);
    project_pile(s, s.exhaust, s.exhaust_count, eng::kExhaustCap, v.exhaust);
    const std::set<int> stasis_held = stasis_held_pool_rows(s);
    project_pile(s, s.limbo, s.limbo_count, eng::kLimboCap, v.limbo, &stasis_held);
    return v;
}

std::string VitalsReport::to_string() const {
    std::ostringstream os;
    for (const VitalsFieldDiff& d : diffs)
        os << d.field << ": " << d.game << " -> " << d.sim << "\n";
    return os.str();
}

VitalsReport diff_combat_costs(const CombatVitals& game, const CombatVitals& sim) {
    VitalsReport r;
    const bool partial = game.hand_partial || sim.hand_partial;
    // The third argument is the HAND_SELECT containment rule; the fourth says
    // whether this pile is one of the game's ANIMATION-DEFERRED reset seams
    // (draw / discard via Soul, exhaust via ExhaustCardEffect). Hand and limbo
    // are deliberately not -- see the header.
    cmp_costs(r, "hand", game.hand, sim.hand, partial, false);
    cmp_costs(r, "draw", game.draw, sim.draw, false, true);
    cmp_costs(r, "discard", game.discard, sim.discard, false, true);
    cmp_costs(r, "exhaust", game.exhaust, sim.exhaust, false, true);
    cmp_costs(r, "limbo", game.limbo, sim.limbo, false, false);

    std::set<std::string> unknown;
    collect_unknown(unknown, game);
    collect_unknown(unknown, sim);
    r.unknown_ids.assign(unknown.begin(), unknown.end());
    return r;
}

VitalsReport diff_combat_vitals(const CombatVitals& game, const CombatVitals& sim) {
    VitalsReport r;
    cmp_int(r, "turn", game.turn, sim.turn);
    cmp_int(r, "player.block", game.player_block, sim.player_block);
    cmp_int(r, "player.energy", game.player_energy, sim.player_energy);
    cmp_powers(r, "player.powers", game.player_powers, sim.player_powers);

    cmp_int(r, "monsters.count", static_cast<int>(game.monsters.size()),
            static_cast<int>(sim.monsters.size()));
    const std::size_t n = std::min(game.monsters.size(), sim.monsters.size());
    for (std::size_t i = 0; i < n; ++i) {
        const VitalsMonster& g = game.monsters[i];
        const VitalsMonster& s = sim.monsters[i];
        const std::string p = "monsters[" + std::to_string(i) + "]";
        if (g.id != s.id || g.known != s.known) {
            push(r, p + ".id", g.known ? g.id : "?" + g.id, s.known ? s.id : "?" + s.id);
            continue;  // the slot holds two different monsters: nothing below is like-for-like
        }
        cmp_int(r, p + ".hp", g.hp, s.hp);
        cmp_bool(r, p + ".gone", g.gone, s.gone);
        cmp_bool(r, p + ".half_dead", g.half_dead, s.half_dead);
        // Block and powers of a dead/escaped monster are wall-clock state
        // capture-side (header: AbstractMonster.update clears powers when the
        // death animation's deathTimer expires) and rule-inert on both sides,
        // so they are compared only for a monster still IN the fight -- alive,
        // or halfDead (which keeps its powers and takes its turn).
        const bool in_fight = (!g.gone && !s.gone) || (g.half_dead && s.half_dead);
        if (!in_fight) continue;
        cmp_int(r, p + ".block", g.block, s.block);
        cmp_powers(r, p + ".powers", g.powers, s.powers);
    }

    // The HAND_SELECT exclusion is a property of the CAPTURE'S dump, so the
    // flag is read off whichever side carries it (only the translator sets it).
    cmp_pile(r, "hand", game.hand, sim.hand, game.hand_partial || sim.hand_partial);
    cmp_pile(r, "draw", game.draw, sim.draw);
    cmp_pile(r, "discard", game.discard, sim.discard);
    cmp_pile(r, "exhaust", game.exhaust, sim.exhaust);
    cmp_pile(r, "limbo", game.limbo, sim.limbo);

    std::set<std::string> unknown;
    collect_unknown(unknown, game);
    collect_unknown(unknown, sim);
    r.unknown_ids.assign(unknown.begin(), unknown.end());
    return r;
}

}  // namespace sts::translate
